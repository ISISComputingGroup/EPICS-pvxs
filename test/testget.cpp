/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <atomic>

#include <testMain.h>

#include <epicsUnitTest.h>

#include <epicsEvent.h>

#include <pvxs/unittest.h>
#include <pvxs/log.h>
#include <pvxs/client.h>
#include <pvxs/server.h>
#include <pvxs/sharedpv.h>
#include <pvxs/source.h>
#include <pvxs/nt.h>

namespace {
using namespace pvxs;

struct Tester {
    IValue initial;
    server::SharedPV mbox;
    server::Server serv;
    client::Context cli;

    Tester()
        :mbox(server::SharedPV::buildReadonly())
        ,serv(server::Config::isolated()
              .build()
              .addPV("mailbox", mbox))
        ,cli(serv.clientConfig().build())
    {
        testShow()<<"Server:\n"<<serv.config()
                  <<"Client:\n"<<cli.config();

        auto ival = nt::NTScalar{TypeCode::Int32}.create();
        ival["value"] = 42;
        initial = ival.freeze();
    }

    void testWait()
    {
        client::Result actual;
        epicsEvent done;

        auto op = cli.get("mailbox")
                .result([&actual, &done](client::Result&& result) {
                    actual = std::move(result);
                    done.trigger();
                })
                .exec();

        cli.hurryUp();

        if(testOk1(done.wait(5.0))) {
            testEq(actual()["value"].as<int32_t>(), 42);
        } else {
            testSkip(1, "timeout");
        }
    }

    void loopback()
    {
        testShow()<<__func__;

        mbox.open(initial);
        serv.start();

        testWait();
    }

    void lazy()
    {
        testShow()<<__func__;

        std::atomic<bool> onFC{false}, onLD{false};

        mbox.onFirstConnect([this, &onFC](){
            testShow()<<__func__;

            mbox.open(initial);
            onFC.store(true);
        });
        mbox.onLastDisconnect([this, &onLD](){
            testShow()<<__func__;
            mbox.close();
            onLD.store(true);
        });

        serv.start();

        testWait();

        serv.stop();

        testOk1(!mbox.isOpen());
        testOk1(!!onFC.load());
        testOk1(!!onLD.load());
    }

    void timeout()
    {
        testShow()<<__func__;

        client::Result actual;
        epicsEvent done;

        // server not started

        auto op = cli.info("mailbox")
                .result([&actual, &done](client::Result&& result) {
                    actual = std::move(result);
                    done.trigger();
                })
                .exec();

        cli.hurryUp();

        testOk1(!done.wait(1.1));
    }

    void cancel()
    {
        testShow()<<__func__;

        client::Result actual;
        epicsEvent done;

        serv.start();

        // not storing Operation -> immediate cancel()
        cli.info("mailbox")
                .result([&actual, &done](client::Result&& result) {
                    actual = std::move(result);
                    done.trigger();
                })
                .exec();

        cli.hurryUp();

        testOk1(!done.wait(2.1));
    }
};

struct ErrorSource : public server::Source
{
    const bool phase = false;
    const IValue type;
    explicit ErrorSource(bool phase)
        :phase(phase)
        ,type(nt::NTScalar{TypeCode::Int32}.create().freeze())
    {}

    virtual void onSearch(Search &op) override final
    {
        for(auto& name : op) {
            name.claim();
        }
    }
    virtual void onCreate(std::unique_ptr<server::ChannelControl> &&op) override final
    {
        auto chan = std::move(op);

        chan->onOp([this](std::unique_ptr<server::ConnectOp>&& op) {
            if(!phase) {
                op->error("haha");
                return;
            }
            op->onGet([](std::unique_ptr<server::ExecOp>&& op) {
                op->error("nice try");
            });
            op->connect(type);
        });
    }
};

void testError(bool phase)
{
    testShow()<<__func__<<" phase="<<phase;

    auto serv = server::Config::isolated()
            .build()
            .addSource("err", std::make_shared<ErrorSource>(phase))
            .start();

    auto cli = serv.clientConfig().build();

    client::Result actual;
    epicsEvent done;

    auto op = cli.get("mailbox")
            .result([&actual, &done](client::Result&& result) {
                actual = std::move(result);
                done.trigger();
            })
            .exec();

    cli.hurryUp();

    if(testOk1(done.wait(5.0))) {
        testThrows<client::RemoteError>([&actual]() {
            auto val = actual();
            testShow()<<"unexpected result\n"<<val;
        });

    } else {
        testSkip(1, "timeout");
    }
}

} // namespace

MAIN(testget)
{
    testPlan(13);
    logger_config_env();
    Tester().loopback();
    Tester().lazy();
    Tester().timeout();
    Tester().cancel();
    testError(false);
    testError(true);
    cleanup_for_valgrind();
    return testDone();
}
