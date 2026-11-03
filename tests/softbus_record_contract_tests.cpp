#include "softbus_record_contract.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>

using namespace fastdds_dsoftbus_probe;
namespace {
const std::string network(64, 'a');
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
RecordPeer Peer(const char* name = "com.kaihong.mdds.client", int type = 2)
{ return CopyRecordPeer(name, network.c_str(), "local-package-not-auth", type); }
bool Pop(SoftbusRecordContract& c, RecordEvent& e) { return c.WaitPop(e, SoftbusRecordContract::Clock::now()); }
void Bind(SoftbusRecordContract& c, int socket, RecordPeer peer)
{ if (c.BeginBind(socket)) c.CompleteBind(socket, std::move(peer)); }
template<class Range> bool SameIds(const Range& actual, std::initializer_list<int> expected)
{ return actual.size()==expected.size() && std::equal(actual.begin(),actual.end(),expected.begin()); }
void ReadyServer(SoftbusRecordContract& c)
{
    Require(c.RegisterCreated(1), "listener registered"); Bind(c,2,Peer());
    RecordEvent e; Require(Pop(c, e) && e.kind == RecordEventKind::Bound && e.peer_match, "bound event");
}
}
int main()
{
    int failed = 0;
    const std::vector<std::pair<const char*, std::function<void()>>> cases{
        {"REC-01 payload and record boundaries", [] {
            std::size_t total = 0;
            for (std::size_t i = 0; i < record_probe_lengths.size(); ++i) {
                auto b = MakeProbeRecord(i); Require(b.size() == record_probe_lengths[i], "candidate length");
                Require(ValidateProbeRecord(i, b), "exact payload"); total += b.size();
                b.back() ^= 1; Require(!ValidateProbeRecord(i, b), "corruption rejected");
                b.pop_back(); Require(!ValidateProbeRecord(i, b), "truncation rejected");
            }
            Require(total < record_queue_bytes, "candidate aggregate bounded");
            Require(!ValidateProbeRecord(5, {}), "out-of-range index rejected");
        }},
        {"REC-02 callback strings and bytes copied", [] {
            char name[] = "com.kaihong.mdds.client"; auto peer = Peer(name); name[0] = 'X';
            Require(peer.name == "com.kaihong.mdds.client", "peer copied");
            SoftbusRecordContract c(true, peer.name, network, 2); Require(c.RegisterCreated(1), "created");
            Bind(c,2,std::move(peer)); RecordEvent e; Require(Pop(c,e), "bind event");
            auto bytes = MakeProbeRecord(0); c.OnBytes(2, bytes.data(), bytes.size()); bytes[0] ^= 1;
            Require(Pop(c,e) && ValidateProbeRecord(0,e.bytes), "record copied");
            Require(c.Snapshot().queued_bytes == 0, "dequeue accounting");
        }},
        {"REC-03 unique peer identity and rejected ownership", [] {
            for (int mode = 0; mode < 3; ++mode) {
                SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); c.RegisterCreated(1);
                auto p = Peer(); if (mode==0) p.name="unknown"; if(mode==1) p.network_id[0]='b'; if(mode==2) p.data_type=1;
                Bind(c,2,std::move(p)); Require(c.Snapshot().failed && !c.Snapshot().ready,"identity rejected");
                auto ids=c.ClaimShutdownTargets(); Require(SameIds(ids,{2,1}),"new rejected is owned and closed before listener");
                Require(c.ClaimShutdownTargets().empty(),"once cleanup");
            }
            Require(!CopyRecordPeer(nullptr,network.c_str(),nullptr,2).fields_valid,"null identity");
            std::string long_name(256,'x'); Require(!CopyRecordPeer(long_name.c_str(),network.c_str(),nullptr,2).fields_valid,"long identity");
        }},
        {"REC-04 count and byte queue limits", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            auto bytes=MakeProbeRecord(0); for(std::size_t i=0;i<record_queue_count;++i)c.OnBytes(2,bytes.data(),bytes.size());
            Require(c.Snapshot().queued==record_queue_count && !c.Snapshot().failed,"count boundary accepted");
            c.OnBytes(2,bytes.data(),bytes.size()); Require(c.Snapshot().failed,"count overflow fail");
            SoftbusRecordContract d(true,"com.kaihong.mdds.client",network,2); ReadyServer(d);
            bytes=MakeProbeRecord(4); for(int i=0;i<4;++i)d.OnBytes(2,bytes.data(),bytes.size());
            Require(d.Snapshot().queued_bytes==240000 && !d.Snapshot().failed,"byte boundary");
            d.OnBytes(2,bytes.data(),bytes.size()); Require(d.Snapshot().failed,"byte overflow fail");
        }},
        {"REC-05 null zero oversize and unknown data rejected", [] {
            for(int mode=0;mode<4;++mode) {
                SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
                std::uint8_t b=0; c.OnBytes(mode==3?99:2,mode==0?nullptr:&b,mode==1?0:mode==2?record_queue_bytes+1:1);
                Require(c.Snapshot().failed && c.Snapshot().queued_bytes==0,"invalid callback rejected before copy");
            }
        }},
        {"REC-06 Bind early callback does not block and close wins", [] {
            SoftbusRecordContract c(false,"com.kaihong.mdds.server",network,2); Require(c.RegisterCreated(7),"client preregistered");
            auto b=MakeProbeRecord(0); c.OnBytes(7,b.data(),b.size());
            Require(c.Snapshot().before_bind_return==1 && !c.Snapshot().ready,"early record staged");
            Require(c.BindReturned(7,0),"successful synchronous bind no OnBind needed");
            RecordEvent e; Require(Pop(c,e) && e.before_bind_return && ValidateProbeRecord(0,e.bytes),"early bytes owned");
            SoftbusRecordContract d(false,"com.kaihong.mdds.server",network,2); d.RegisterCreated(7);
            d.OnBytes(7,b.data(),b.size()); d.OnShutdown(7,2);
            Require(!d.BindReturned(7,0) && !d.Snapshot().ready,"close prevents resurrection");
            while(Pop(d,e))Require(e.kind!=RecordEventKind::Bytes,"closed bytes not delivered");
        }},
        {"REC-07 duplicate IDs retain unique ownership", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            Bind(c,2,Peer()); Require(c.Snapshot().owned==2 && c.Snapshot().failed,"duplicate not overwritten");
            Require(SameIds(c.ClaimShutdownTargets(),{2,1}),"unique shutdown IDs");
            SoftbusRecordContract d(false,"server",network,2); Require(!d.RegisterCreated(0),"zero invalid");
            Require(d.ClaimShutdownTargets().empty(),"zero not closed");
        }},
        {"REC-08 concurrent closing prevents delivery", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            auto b=MakeProbeRecord(0); auto ids=c.ClaimShutdownTargets();
            std::thread t([&]{for(int i=0;i<20;++i)c.OnBytes(2,b.data(),b.size());}); t.join();
            RecordEvent e; Require(!Pop(c,e),"no bytes delivered after close");
            Require(c.Snapshot().late_callbacks==20 && c.Snapshot().queued_bytes==0,"late callback count bounded");
            Require(ids.size()==2 && c.ClaimShutdownTargets().empty(),"no repeat shutdown");
        }},
        {"REC-09 close owns late new IDs but never duplicates", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); c.RegisterCreated(1);
            Require(SameIds(c.ClaimShutdownTargets(),{1}),"listener closing");
            Bind(c,2,Peer("unknown"));
            Require(SameIds(c.ClaimShutdownTargets(),{2}),"new rejected socket still reclaimed");
            Bind(c,2,Peer()); Require(c.ClaimShutdownTargets().empty(),"duplicate never claimed twice");
            Require(c.Snapshot().owned==2,"unique ownership stable");
        }},
        {"REC-10 early records cannot pop before Bind success", [] {
            SoftbusRecordContract c(false,"server",network,2); c.RegisterCreated(7);
            auto bytes=MakeProbeRecord(0); c.OnBytes(7,bytes.data(),bytes.size());
            RecordEvent event; Require(!Pop(c,event),"BINDING does not deliver");
            Require(!c.BindReturned(7,-1),"failed bind not usable");
            Require(!Pop(c,event) && c.Snapshot().queued_bytes==0 && c.Snapshot().discarded_records==1,"failed bind discards early bytes");
        }},
        {"ROUTE-01 bounded terminated network ID", [] {
            std::string result;
            Require(CopyRouteNetworkId(network.c_str(),65,result) && result==network,"valid 64hex route");
            Require(!CopyRouteNetworkId(network.c_str(),64,result),"capacity lacks terminator");
            Require(!CopyRouteNetworkId(nullptr,65,result),"null network rejected");
            auto text=network; text[10]='\0'; Require(!CopyRouteNetworkId(text.c_str(),65,result),"embedded NUL rejected");
            text=network; text[10]='g'; Require(!CopyRouteNetworkId(text.c_str(),65,result),"nonhex rejected");
            text.assign(65,'a'); Require(!CopyRouteNetworkId(text.data(),65,result),"unterminated network rejected");
            Require(result.empty(),"failed output not stale");
        }},
        {"ROUTE-02 bounded count and pointer relation", [] {
            Require(ValidateRouteNodeCount(0,false) && ValidateRouteNodeCount(0,true),"zero routes is valid observation");
            Require(ValidateRouteNodeCount(1,true) && ValidateRouteNodeCount(32,true),"positive bounded array");
            Require(!ValidateRouteNodeCount(1,false),"positive count null pointer");
            Require(!ValidateRouteNodeCount(-1,true) && !ValidateRouteNodeCount(33,true),"invalid count rejected not truncated");
        }},
        {"ROUTE-03 duplicate and local IDs are not remote routes", [] {
            std::vector<std::string> remotes;
            const std::string remote(64,'b');
            Require(AppendRouteNetworkId(network,remote.c_str(),65,remotes),"unique remote copied");
            Require(!AppendRouteNetworkId(network,remote.c_str(),65,remotes),"duplicate remote rejected");
            Require(!AppendRouteNetworkId(network,network.c_str(),65,remotes),"local not remote");
            Require(remotes.size()==1 && remotes.front()==remote,"failed append preserves validated routes");
        }},
        {"R1-01 capture ID before deterministic peer-copy exception", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); c.RegisterCreated(1);
            Require(c.BeginBind(2),"accepted copy phase entered");
            try {
                // This is a controlled throw point between BeginBind and peer
                // copy, not a process-wide operator new override.
                throw std::bad_alloc{};
            } catch(const std::bad_alloc&) { c.FailBoundCopy(2); }
            Require(c.Snapshot().owned==2,"accepted ID survives copy exception");
            Require(c.Snapshot().failed && c.Snapshot().bound_copy_failures==1,"copy error is sticky failure");
            Require(SameIds(c.ClaimShutdownTargets(),{2,1}),"copy failure closes accepted and listener once");
            Require(c.ClaimShutdownTargets().empty(),"copy failure does not repeat close");
        }},
        {"R1-02 cleanup transfer has fixed nonallocating value storage", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            using Targets=decltype(c.ClaimShutdownTargets());
            Require(std::is_trivially_copyable<Targets>::value,"shutdown handoff is fixed plain value not dynamic vector");
            Require(std::is_standard_layout<Targets>::value,"shutdown handoff storage is fixed");
            Require(SameIds(c.ClaimShutdownTargets(),{2,1}),"fixed transfer contains all owned IDs");
        }},
        {"R1-03 capacity exhaustion is fatal and cleanup uncertainty sticky", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); c.RegisterCreated(1);
            for(int id=2;id<=16;++id) Bind(c,id,Peer());
            auto state=c.Snapshot();
            Require(state.owned==15 && state.fatal && state.failed,"16th observed handle makes fatal explicit");
            Require(state.untracked_first_id==16 && state.untracked_count==1,"untracked ID and observation recorded");
            auto targets=c.ClaimShutdownTargets(); Require(targets.size()==15,"known resource cleanup still available");
            Bind(c,17,Peer()); c.OnShutdown(2,2); c.Fail("later_failure"); c.BindReturned(1,0);
            state=c.Snapshot(); Require(state.fatal && state.failed && !state.ready,"later paths never clear fatal");
            Require(state.untracked_first_id==16 && state.untracked_count==2,"first untracked stable, count retained");
            Require(c.ClaimShutdownTargets().empty(),"known IDs never closed twice after overflow");
        }},
        {"R1-04 duplicate healthy or unknown client ID never newly adopted", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            Require(!c.BeginBind(2),"duplicate ID rejected before any peer copy");
            Require(c.Snapshot().owned==2 && !c.Snapshot().fatal,"duplicate remains uniquely owned");
            Require(SameIds(c.ClaimShutdownTargets(),{2,1}) && c.ClaimShutdownTargets().empty(),"only original owners close");
            SoftbusRecordContract d(false,"server",network,2); d.RegisterCreated(7);
            Require(!d.BeginBind(99),"unknown client callback is not our accepted resource");
            Require(d.Snapshot().owned==1 && SameIds(d.ClaimShutdownTargets(),{7}),"unknown client ID is not closed");
        }},
        {"TIMING-01 error preserves callback-time Bind state", [] {
            SoftbusRecordContract c(false,"server",network,2); Require(c.RegisterCreated(7),"client preregistered");
            c.OnError(7,-31);
            Require(!c.BindReturned(7,0) && c.Snapshot().failed && !c.Snapshot().ready,"error still prevents resurrection");
            c.OnError(7,-32);
            RecordEvent event;
            Require(Pop(c,event) && event.kind==RecordEventKind::Error && event.socket==7 && event.code==-31,
                    "first error retains identity and code");
            Require(event.before_bind_return,"queued early error is not relabeled after Bind returns");
            Require(Pop(c,event) && event.kind==RecordEventKind::Error && event.code==-32 && !event.before_bind_return,
                    "later error is marked after Bind return");
            Require(!Pop(c,event) && c.Snapshot().before_bind_return==0,"timing does not change early-byte count");
        }},
        {"TIMING-02 shutdown preserves callback-time Bind state", [] {
            SoftbusRecordContract c(false,"server",network,2); Require(c.RegisterCreated(7),"client preregistered");
            c.OnShutdown(7,2);
            Require(!c.BindReturned(7,0) && !c.Snapshot().ready,"shutdown still prevents resurrection");
            c.OnShutdown(7,3);
            RecordEvent event;
            Require(Pop(c,event) && event.kind==RecordEventKind::Shutdown && event.socket==7 && event.code==2,
                    "first shutdown retains identity and reason");
            Require(event.before_bind_return,"queued early shutdown is not relabeled after Bind returns");
            Require(Pop(c,event) && event.kind==RecordEventKind::Shutdown && event.code==3 && !event.before_bind_return,
                    "later shutdown is marked after Bind return");
            Require(!Pop(c,event) && c.Snapshot().shutdown_callbacks==2,"shutdown event count unchanged");
        }},
        {"TIMING-03 server callbacks have no client Bind return", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); ReadyServer(c);
            c.OnError(2,-31); c.OnShutdown(2,2);
            RecordEvent event;
            Require(Pop(c,event) && event.kind==RecordEventKind::Error && !event.before_bind_return,
                    "server error is never before client Bind return");
            Require(Pop(c,event) && event.kind==RecordEventKind::Shutdown && !event.before_bind_return,
                    "server shutdown is never before client Bind return");
            Require(!Pop(c,event),"server callback queue drained");
        }},
        {"R1-05 shutdown during peer copy cannot resurrect admitted connection", [] {
            SoftbusRecordContract c(true,"com.kaihong.mdds.client",network,2); c.RegisterCreated(1);
            Require(c.BeginBind(2) && c.Snapshot().owned==2,"capture before peer copy");
            c.OnShutdown(2,2); c.CompleteBind(2,Peer());
            Require(!c.Snapshot().ready,"closed entry never becomes READY");
            auto bytes=MakeProbeRecord(0); c.OnBytes(2,bytes.data(),bytes.size());
            RecordEvent event; while(Pop(c,event)) Require(event.kind!=RecordEventKind::Bytes,"closed entry never delivers");
            Require(SameIds(c.ClaimShutdownTargets(),{2,1}),"closed-before-copy retains unique cleanup");
        }}
    };
    for(const auto& test:cases) {
        try { test.second(); std::cout<<test.first<<" PASS\n"; }
        catch(const std::exception& e){++failed; std::cout<<test.first<<" FAIL "<<e.what()<<'\n';}
    }
    std::cout<<"groups="<<cases.size()<<" failed="<<failed<<" scope=HOST_MODEL_ONLY\n";
    return failed?1:0;
}
