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

struct BasicTest {
    IValue initial;
    server::SharedPV mbox;
    server::Server serv;
    client::Context cli;

    epicsEvent evt;
    std::shared_ptr<client::Subscription> sub;

    BasicTest()
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

    void subscribe(const char *name)
    {
        sub = cli.monitor(name)
                .maskConnected(false)
                .maskDisconnected(false)
                .event([this](client::Subscription& sub) {
                    testDiag("Event %s", __func__);
                    evt.trigger();
                })
                .exec();
    }

    void post(int32_t v)
    {
        auto update(initial.cloneEmpty());
        update["value"] = v;
        mbox.post(update.freeze());
    }
};

struct TestLifeCycle : public BasicTest
{
    TestLifeCycle()
    {
        serv.start();
        mbox.open(initial);
        subscribe("mailbox");

        cli.hurryUp();

        testDiag("Wait for Connected event");
        testOk1(!!evt.wait(5.0));

        testThrows<client::Connected>([this](){
            sub->pop();
        });
    }

    void phase1()
    {
        testShow()<<"begin "<<__func__;

        testDiag("Wait for Data update event");
        testOk1(!!evt.wait(5.0));

        if(auto val = sub->pop()) {
            testEq(val["value"].as<int32_t>(), 42);
        } else {
            testFail("Missing data update");
        }

        post(123);

        testDiag("Wait for Data update event 2");
        testOk1(!!evt.wait(5.0));

        if(auto val = sub->pop()) {
            testEq(val["value"].as<int32_t>(), 123);
        } else {
            testFail("Missing data update 2");
        }

        testShow()<<"end "<<__func__;
    }

    void phase2(bool howdisconn)
    {
        testShow()<<"begin "<<__func__;

        if(howdisconn) {
            testDiag("Stopping server");
            serv.stop();
        } else {
            testDiag("close() mbox");
            mbox.close();
        }

        testDiag("Wait for Disconnected event");
        testOk1(!!evt.wait(5.0));

        testThrows<client::Disconnect>([this](){
            sub->pop();
        });

        testShow()<<"end "<<__func__;
    }

    void testBasic(bool howdisconn)
    {
        testShow()<<__func__<<" "<<howdisconn;
        phase1();
        phase2(howdisconn);
    }

    void testSecond()
    {
        testShow()<<__func__;

        epicsEvent evt2;

        auto mbox2(server::SharedPV::buildReadonly());
        mbox2.open(initial);
        serv.addPV("mailbox2", mbox2);

        auto sub2 = cli.monitor("mailbox2")
                        .maskConnected(true)
                        .maskDisconnected(false)
                        .event([&evt2](client::Subscription& sub) {
                            testDiag("Event %s", __func__);
                            evt2.trigger();
                        })
                        .exec();

        phase1();

        testDiag("Wait for Data update event on mbox2");
        testOk1(!!evt2.wait(5.0));

        if(auto val = sub2->pop()) {
            testEq(val["value"].as<int32_t>(), 42);
        } else {
            testFail("Missing data update");
        }

        phase2(false);

        // closing mbox should not disconnect mbox2.

        auto update(initial.cloneEmpty());
        update["value"] = 39;
        mbox2.post(update.freeze());

        testDiag("Wait for Data update event2 on mbox2");
        testOk1(!!evt2.wait(5.0));

        if(auto val = sub2->pop()) {
            testEq(val["value"].as<int32_t>(), 39);
        } else {
            testFail("Missing data update");
        }
    }
};

} // namespace

MAIN(testmon)
{
    testPlan(0);
    logger_config_env();
    TestLifeCycle().testBasic(true);
    TestLifeCycle().testBasic(false);
    TestLifeCycle().testSecond();
    cleanup_for_valgrind();
    return testDone();
}
