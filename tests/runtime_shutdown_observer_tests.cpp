#include "HostProviderTestSupport.hpp"

// Tests the real loop and adapter, with the existing bounded Host backend.
int main(int argc,char** argv) {
    try {
        const std::string mode=argc==2?argv[1]:"";
        Check(mode=="once"||mode=="throw"||mode=="nested"||mode=="shared"||mode=="active","exact test case required");
        if(mode=="once") {
            std::cout<<"CASE once"<<std::endl;
            F f; unsigned calls=0;
            Check(f.loop->SetBeforeNativeClose([&](const RuntimeShutdownPoint& p) {
                ++calls;
                Check(p.stopping&&p.sealed&&p.active_pumps==0,"settled local point");
                Check(f.mock->closes==0,"observer precedes physical close");
            }),"install before start");
            f.Init();
            Check(!f.loop->SetBeforeNativeClose({}),"late replacement refused");
            f.Close();f.Close();
            Check(calls==1&&f.mock->closes==1,"once only");
        }
        if(mode=="throw") {
            std::cout<<"CASE throw"<<std::endl;
            F f;Check(f.loop->SetBeforeNativeClose([](const RuntimeShutdownPoint&){throw 7;}),"install throw");
            f.Init();Check(!f.loop->Shutdown(),"observer exception fails result");
            const auto v=f.loop->Observe();
            Check(v.closed&&v.close.adapter_drained&&v.close.status==BackendStatus::None&&v.shutdown_observer_fault,"fault separate from real cleanup");
            Check(!f.loop->Shutdown()&&f.mock->closes==1,"idempotent fault retained");
        }
        if(mode=="nested") {
            std::cout<<"CASE nested"<<std::endl;
            F a,b;
            Check(a.loop->SetBeforeNativeClose([&](const RuntimeShutdownPoint&){Check(b.loop->Shutdown(),"other runtime may close");}),"install A");
            Check(b.loop->SetBeforeNativeClose([&](const RuntimeShutdownPoint&){Check(!a.loop->Shutdown(),"ancestor reentry refused before wait");}),"install B");
            a.Init();b.Init(false); // Independent Runtime with no native owner.
            Check(!a.loop->Shutdown()&&!a.loop->Shutdown(),"ancestor fault persists");
            Check(a.mock->closes==1&&b.mock->closes==0&&b.loop->Observe().closed,"both original cleanups run; unopened B has no SDK close");
        }
        if(mode=="shared") {
            std::cout<<"CASE shared"<<std::endl;
            auto shared=HostProviderLoop::CreateShared(std::make_shared<Mock>());
            Check(!shared->SetBeforeNativeClose([](const RuntimeShutdownPoint&){}),"shared scope refused");
        }
        if(mode=="active") {
            std::cout<<"CASE active"<<std::endl;
            F f;Event entered,release;std::atomic<unsigned> observed{0};
            Check(f.loop->SetBeforeNativeClose([&](const RuntimeShutdownPoint& p){Check(p.active_pumps==0&&f.mock->closes==0,"settlement before observer");++observed;}),"install active test");
            f.Init();f.mock->during_send=[&]{entered.Signal();release.Wait();};Check(f.Send(),"queue active send");
            std::thread send,close;
            Cleanup cleanup{[&]{release.Signal();if(send.joinable())send.join();if(close.joinable())close.join();}};
            send=std::thread([&]{f.loop->PumpSend();});entered.Wait();
            close=std::thread([&]{f.loop->Shutdown();});
            Check(f.loop->WaitSealed(std::chrono::steady_clock::now()+3s),"seal reached");
            Check(observed==0&&f.mock->closes==0,"active call blocks observer and close");
            release.Signal();send.join();close.join();Check(observed==1&&f.mock->closes==1,"settled exactly once");
        }
        std::cout<<"PASS runtime shutdown observer\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
