#include "softbus_lifecycle_contract.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace mdds_probe;
namespace {
using Clock = std::chrono::steady_clock;
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
const std::string peer_name = "com.kaihong.mdds.dial.b", network(64, 'b');
RecordPeer Peer() { return CopyRecordPeer(peer_name.c_str(), network.c_str(), "not-an-auth-identity", 2); }
void Prepare(LifecycleContract& c) {
    Check(c.RegisterSocket(1) == LifecycleAdmission::Owner, "listener owner");
    Check(c.Accept(2, Peer()), "accepted peer"); Check(c.SetReady(2), "ready");
}
struct ThrowFactory : LifecycleThreadFactory {
    std::thread Create(std::function<void()>) override { throw std::runtime_error("thread creation failure"); }
};
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> cases{
        {"LC3 alias has one close owner", [] {
            LifecycleContract c(peer_name, network, 2);
            Check(c.RegisterSocket(7) == LifecycleAdmission::Owner, "first owner");
            Check(c.RegisterSocket(7) == LifecycleAdmission::Alias, "second alias");
            Check(c.Snapshot().owned == 1 && c.Snapshot().aliases == 1, "deduplicated ownership");
            Check(c.Snapshot().shutdown_claimed == 0, "alias rejection does not shutdown");
            c.RequestClose(Clock::now()+std::chrono::seconds(1));
            Check(c.ClaimShutdown() == 7 && c.ClaimShutdown() == 0, "exactly one shutdown");
            Check(c.ShutdownReturned(7), "return known claimed id");
            Check(c.RegisterSocket(7) == LifecycleAdmission::Conflict, "retired number cannot become new epoch");
        }},
        {"LC3 different IDs are independently owned, invalid is not", [] {
            LifecycleContract c(peer_name, network, 2);
            Check(c.RegisterSocket(-6) == LifecycleAdmission::Invalid, "SDK error not a socket");
            Check(c.Snapshot().owned == 0, "invalid is not owned");
            c.RegisterSocket(8); c.RegisterSocket(9); c.RequestClose(Clock::now()+std::chrono::seconds(1));
            Check(c.ClaimShutdown() == 9 && c.ClaimShutdown() == 8 && c.ClaimShutdown() == 0, "all owners reverse order");
        }},
        {"LC3 full owner ledger is fatal and retains unknown positive id", [] {
            LifecycleContract c(peer_name, network, 2);
            for (int id=1; id<=int(LifecycleContract::owner_limit); ++id)
                Check(c.RegisterSocket(id)==LifecycleAdmission::Owner,"bounded slots");
            Check(c.RegisterSocket(99)==LifecycleAdmission::Full,"no overwrite");
            auto s=c.Snapshot(); Check(s.fatal && s.untracked_id==99 && s.owned==LifecycleContract::owner_limit,"unknown resource cannot disappear");
        }},
        {"LC2 peer copy validation, accepted ownership survives rejection", [] {
            LifecycleContract c(peer_name, network, 2); c.RegisterSocket(1);
            auto p=Peer(); p.name="unknown"; Check(!c.Accept(2,std::move(p)),"unknown denied");
            Check(c.Snapshot().owned==2 && c.Snapshot().failed,"rejected socket cleanup retained");
            c.RequestClose(Clock::now()+std::chrono::seconds(1));
            Check(c.ClaimShutdown()==2 && c.ClaimShutdown()==1,"accepted then listener");
        }},
        {"LC2 RAII decrements active after callback exception", [] {
            LifecycleContract c(peer_name, network, 2); Prepare(c);
            try { LifecycleCallbackLease lease(c,2,44); Check(c.Snapshot().active_callbacks==1,"active"); throw 4; }
            catch(int) {}
            Check(c.Snapshot().active_callbacks==0 && c.Snapshot().callback_count==1,"exception leaves active");
        }},
        {"LC2 bytes copied, closing discards, no false ready", [] {
            LifecycleContract c(peer_name, network, 2); Prepare(c); auto b=MakeProbeRecord(4);
            c.Receive(2,b.data(),b.size()); b[0]^=1; RecordEvent e;
            Check(c.Pop(e) && ValidateProbeRecord(4,e.bytes),"self owned full bytes");
            c.RequestClose(Clock::now()+std::chrono::seconds(1)); c.Receive(2,b.data(),b.size());
            Check(!c.SetReady(2) && !c.Pop(e),"late success or bytes not delivered");
            Check(c.Snapshot().discarded==1 && c.Snapshot().received==2,"late record observable");
        }},
        {"LC2 close deadline stays anchored and callback drain is independent", [] {
            LifecycleContract c(peer_name, network, 2); Prepare(c);
            LifecycleCallbackLease lease(c,2); const auto first=c.RequestClose(Clock::now()+std::chrono::seconds(5),std::chrono::milliseconds(20));
            const auto repeated=c.RequestClose(Clock::now()+std::chrono::seconds(6)); Check(first==repeated,"not reset");
            while(int id=c.ClaimShutdown()) Check(c.ShutdownReturned(id),"shutdown return");
            auto s=c.Snapshot(); Check(s.shutdown_returned==s.owned && s.active_callbacks==1,"shutdown return is not drain");
        }},
        {"LC2 callback event ledger overflow is failure", [] {
            LifecycleContract c(peer_name, network, 2);
            for(std::size_t i=0;i<LifecycleContract::event_limit+1;++i)c.Report(LifecycleEventKind::CallBegin,1);
            Check(c.Snapshot().failed && c.Snapshot().events==LifecycleContract::event_limit,"no silent missing events");
        }},
        {"LC2 shutdown event keeps actual closer TID without fabricated RC", [] {
            LifecycleContract c(peer_name,network,2);c.RegisterSocket(7);c.RequestClose(Clock::now()+std::chrono::seconds(1));
            Check(c.ClaimShutdown()==7,"claimed");c.Report(LifecycleEventKind::ShutdownStarted,7,0,0,88);
            Check(c.ShutdownReturned(7,88),"actual returned thread");
            LifecycleEvent event;bool start=false,returned=false;
            for(std::size_t i=0;c.EventAt(i,event);++i) {
                if(event.kind==LifecycleEventKind::ShutdownStarted)start=event.socket==7&&event.tid==88;
                if(event.kind==LifecycleEventKind::ShutdownReturned)returned=start&&event.socket==7&&event.tid==88;
            }
            Check(returned,"start and return attributable to one closer");
        }},
        {"LC2 invalid, unknown and byte queue overflow cannot be hidden", [] {
            LifecycleContract c(peer_name,network,2);Prepare(c);auto bytes=MakeProbeRecord(4);
            for(int i=0;i<4;++i)c.Receive(2,bytes.data(),bytes.size());
            Check(c.Snapshot().queued_bytes==240000&&!c.Snapshot().failed,"bounded receive bytes");
            c.Receive(2,bytes.data(),bytes.size());Check(c.Snapshot().failed,"capacity overflow");
            LifecycleContract d(peer_name,network,2);Prepare(d);d.Receive(99,bytes.data(),bytes.size());
            Check(d.Snapshot().fatal&&d.Snapshot().untracked_id==99,"unknown resource recorded");
            LifecycleContract e(peer_name,network,2);Prepare(e);e.Receive(2,nullptr,32);
            Check(e.Snapshot().failed&&e.Snapshot().queued==0,"bad pointer rejected");
        }},
        {"worker timeout retains a real joinable thread then can join", [] {
            ManagedLifecycleWorker worker; LifecycleThreadFactory factory; std::mutex m; std::condition_variable cv; bool release=false;
            Check(worker.Start(factory,[&]{std::unique_lock<std::mutex> lock(m); cv.wait(lock,[&]{return release;});}),"actual thread starts");
            Check(!worker.JoinUntil(Clock::now()+std::chrono::milliseconds(10)) && worker.Joinable(),"timeout retains joinable");
            {std::lock_guard<std::mutex> lock(m);release=true;} cv.notify_all();
            Check(worker.JoinUntil(Clock::now()+std::chrono::seconds(1)) && !worker.Joinable(),"actual join after release");
        }},
        {"worker creation and job exceptions do not claim successful execution", [] {
            ManagedLifecycleWorker worker; ThrowFactory broken; bool called=false;
            Check(!worker.Start(broken,[&]{called=true;}) && !called && !worker.Joinable(),"creation failure");
            LifecycleThreadFactory factory; Check(worker.Start(factory,[]{throw 9;}),"thread starts");
            Check(worker.JoinUntil(Clock::now()+std::chrono::seconds(1)) && worker.Threw(),"exception recorded after real join");
        }}
    };
    int failed=0; for(const auto& test:cases)try{test.second();std::cout<<"PASS "<<test.first<<'\n';}
        catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';}
    std::cout<<"tests="<<cases.size()<<" failed="<<failed<<" scope=HOST_CONTRACT_NOT_DEVICE\n"; return failed?1:0;
}
