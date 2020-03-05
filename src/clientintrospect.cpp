/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <epicsAssert.h>

#include <pvxs/log.h>
#include "clientimpl.h"

namespace pvxs {
namespace client {

DEFINE_LOGGER(setup, "pvxs.client.setup");
DEFINE_LOGGER(io, "pvxs.client.io");

namespace {

struct InfoOp : public OperationBase
{
    std::function<void(Result&&)> done;
    IValue result;

    enum state_t {
        Connecting, // waiting for an active Channel
        Waiting,    // waiting for reply to GET_INFO
        Done,
    } state = Connecting;

    explicit InfoOp(const std::shared_ptr<Channel>& chan)
        :OperationBase(Info, chan)
    {}

    virtual ~InfoOp()
    {
        cancel();
    }

    virtual void cancel() override final {
        auto context = chan->context;
        decltype (done) junk;
        context->tcp_loop.call([this, &junk](){
            if(state==Waiting) {
                chan->conn->sendDestroyRequest(chan->sid, ioid);

                // This opens up a race with an in-flight reply.
                chan->conn->opByIOID.erase(ioid);
                chan->opByIOID.erase(ioid);
            }
            state = Done;
            chan.reset();
            junk = std::move(done);
            // leave opByIOID for GC
        });
    }

    virtual void createOp() override final
    {
        if(state!=Connecting) {
            return;
        }

        auto& conn = chan->conn;

        {
            (void)evbuffer_drain(conn->txBody.get(), evbuffer_get_length(conn->txBody.get()));

            EvOutBuf R(hostBE, conn->txBody.get());

            to_wire(R, chan->sid);
            to_wire(R, ioid);
            // sub-field, which no one knows how to use...
            to_wire(R, "");
        }
        conn->enqueueTxBody(CMD_GET_FIELD);

        log_debug_printf(io, "Server %s channel '%s' GET_INFO\n", conn->peerName.c_str(), chan->name.c_str());

        state = Waiting;
    }

    virtual void disconnected(const std::shared_ptr<OperationBase>& self) override final
    {
        // Do nothing when Connecting or Done
        if(state==Waiting) {
            // return to pending

            chan->pending.push_back(self);
            state = Connecting;
        }
    }
};

} // namespace

void Connection::handle_GET_FIELD()
{
    EvInBuf M(peerBE, segBuf.get(), 16);

    uint32_t ioid=0u;
    Status sts{Status::Fatal};
    MValue prototype;

    from_wire(M, ioid);
    from_wire(M, sts);
    if(sts.isSuccess())
        from_wire_type(M, rxRegistry, prototype);

    if(!M.good()) {
        log_crit_printf(io, "Server %s sends invalid GET_FIELD.  Disconnecting...\n", peerName.c_str());
        bev.reset();
        return;
    }

    std::shared_ptr<Operation> op;
    InfoOp* info;
    {
        auto it = opByIOID.find(ioid);
        if(it==opByIOID.end()
                || !(op = it->second.handle.lock())
                || op->op!=Operation::Info) {
            log_warn_printf(io, "Server %s sends stale GET_FIELD\n", peerName.c_str());
            return;
        }
        info = static_cast<InfoOp*>(op.get());
        opByIOID.erase(it);
        info->chan->opByIOID.erase(ioid);
    }

    if(info->state!=InfoOp::Waiting) {
        log_warn_printf(io, "Server %s ignore second reply to GET_FIELD\n", peerName.c_str());
        return;
    }

    log_debug_printf(io, "Server %s completes GET_FIELD.\n", peerName.c_str());

    info->state = InfoOp::Done;

    if(info->done) {
        auto done = std::move(info->done);
        Result res;
        if(sts.isSuccess()) {
            res = Result(prototype.freeze(), peerName);
        } else {
            res = Result(std::make_exception_ptr(RemoteError(sts.msg)));
        }
        try {
            done(std::move(res));
        }catch(std::exception& e){
            log_err_printf(setup, "Unhandled exception %s in Info result() callback: %s\n", typeid (e).name(), e.what());
        }

    } else {
        info->result = prototype.freeze();
    }
}

std::shared_ptr<Operation> GetBuilder::_exec_info()
{
    std::shared_ptr<Operation> ret;

    assert(!_get);

    ctx->tcp_loop.call([&ret, this]() {
        auto chan = Channel::build(ctx, _name);

        auto op = std::make_shared<InfoOp>(chan);
        op->done = std::move(_result);

        chan->pending.push_back(op);
        chan->createOperations();

        ret = std::move(op);
    });

    return ret;
}

} // namespace client
} // namespace pvxs
