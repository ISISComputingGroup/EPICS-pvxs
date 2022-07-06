/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * pvxs is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */

#include <vector>

#include <pvxs/source.h>
#include <pvxs/nt.h>
#include <pvxs/log.h>
#include <utilpvt.h>

#include <errSymTbl.h>

#include <dbStaticLib.h>
#include <dbAccess.h>
#include <dbChannel.h>
#include <dbEvent.h>

namespace {
using namespace pvxs;

DEFINE_LOGGER(_logname, "pvxs.ioc.search");

struct DBEntry {
    DBENTRY ent{};
    DBEntry() {
        dbInitEntry(pdbbase, &ent);
    }
    ~DBEntry() {
        dbFinishEntry(&ent);
    }
    operator DBENTRY*() { return &ent; }
    DBENTRY* operator->() { return &ent; }
};

struct DBErrMsg {
    long status = 0;
    char msg[40];
    DBErrMsg(long sts=0) {
        (*this) = sts;
    }
    DBErrMsg& operator=(long sts) {
        status=sts;
        if(!sts) {
            msg[0] = '\0';
        } else {
            errSymLookup(sts, msg, sizeof(msg));
            msg[sizeof(msg)-1] = '\0';
        }
        return *this;
    }
    explicit operator bool() const { return status; }
    const char* c_str() const { return msg; }
};

struct dbEventCtxFree {
    void operator()(dbEventCtx ctxt) {
        db_close_events(ctxt);
    }
};

struct dbEventSubscriptionFree {
    void operator()(dbEventSubscription ctxt) {
        db_cancel_event(ctxt);
    }
};

struct DBSingleSource : public server::Source
{
    List allrecords;
    std::unique_ptr<std::remove_pointer<dbEventCtx>::type, dbEventCtxFree> dbe;

    DBSingleSource()
        :dbe(db_init_events())
    {
        if(!dbe)
            throw std::runtime_error("Unable to allocate dbEvent context");

        if(db_start_events(dbe.get(), "XSRVsingle", nullptr, nullptr, epicsThreadPriorityCAServerLow)) {
            throw std::runtime_error("Unable to start dbEvent context");
        }

        auto names(std::make_shared<std::set<std::string>>());

        DBEntry db;
        for(long status = dbFirstRecordType(db); !status; status=dbNextRecordType(db)) {
            for(status = dbFirstRecord(db); !status; status=dbNextRecord(db)) {
                names->insert(db->precnode->recordname);
            }
        }

        allrecords.names = names;
    }

    virtual void onSearch(Search &op) override final
    {
        for(auto& pv : op) {
            if(dbChannelTest(pv.name())) {
                pv.claim();
                log_debug_printf(_logname, "Claiming '%s'\n", pv.name());
            }
        }
    }

    virtual void onCreate(std::unique_ptr<server::ChannelControl> &&op) override final
    {
        dbChannel *dbchan = dbChannelCreate(op->name().c_str());
        if(!dbchan) {
            log_debug_printf(_logname, "Ignore requested channel for '%s'\n", op->name().c_str());
            return;
        }
        log_debug_printf(_logname, "Accepting channel for '%s'\n", op->name().c_str());

        std::shared_ptr<dbChannel> chan(dbchan, [](dbChannel* ch) {
            dbChannelDelete(ch);
        });

        if(DBErrMsg err = dbChannelOpen(chan.get())) {
            // op->error(); // TODO:
            return;
        }

        TypeCode valuetype(TypeCode::Null);
        short dbrType = dbChannelFinalFieldType(chan.get());
        bool longstring = false;

        if(dbChannelFieldType(chan.get())==DBF_STRING && dbChannelElements(chan.get())==1
                && dbrType && dbChannelFinalElements(chan.get())>1)
        {
            // single DBF_STRING being cast to DBF_CHAR array.  aka long string
            valuetype = TypeCode::String;
            longstring = true;

        } else if(dbrType==DBF_ENUM && dbChannelFinalElements(chan.get())==1) {

        } else {
            switch(dbrType) {
            case DBF_INLINK:
            case DBF_OUTLINK:
            case DBF_FWDLINK:
                dbrType = DBF_CHAR;
                longstring = true;
                break;
            }

            switch(dbrType) {
    #define CASE(DBF, PVX) case DBF_ ## DBF: valuetype = TypeCode::PVX; break
            CASE(CHAR, Int8);
            CASE(UCHAR, UInt8);
            CASE(SHORT, Int16);
            CASE(USHORT, UInt16);
            CASE(LONG, Int32);
            CASE(ULONG, UInt32);
            CASE(INT64, Int64);
            CASE(UINT64, UInt64);
            CASE(FLOAT, Float32);
            CASE(DOUBLE, Float64);
            CASE(STRING, String);
            CASE(INLINK, String);
            CASE(OUTLINK, String);
            CASE(FWDLINK, String);
    #undef CASE
            }

            if(valuetype!=TypeCode::Null && dbChannelFinalElements(chan.get())==1) {
                valuetype = valuetype.arrayOf();
            }
        }

        Value prototype(nt::NTScalar{valuetype}.create()); // TODO: enable meta

        op->onOp([chan, prototype](std::unique_ptr<server::ConnectOp>&& cop) {

            cop->onGet([](std::unique_ptr<server::ExecOp>&& eop) {
                // TODO
            });

            cop->onPut([](std::unique_ptr<server::ExecOp>&& eop, Value&& val) {
                // TODO
            });

            cop->connect(prototype);
        });

        op->onSubscribe([this, chan, prototype](std::unique_ptr<server::MonitorSetupOp>&& setup) {
            auto ctrl(setup->connect(prototype));

            // db_add_event()

            ctrl->onStart([](bool start) {
                // TODO
                if(start) {
                    // db_event_enable()
                    // db_post_single_event()

                } else {
                    // db_event_disable()
                }
            });

        });
    }

    virtual List onList() override final
    {
        return allrecords;
    }

    virtual void show(std::ostream &strm) override final
    {
    }
};

} // namespace