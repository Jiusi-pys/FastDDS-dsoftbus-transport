#include "FakePublicSocketLink.hpp"
#include <socket.h>
#include <array>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <stdexcept>
#include <algorithm>
namespace public_socket_fake {
namespace {
using namespace std::chrono_literals;
struct Row {int id=0,side=0,role=0,peer=-1;bool active=false,bound=false,pending_bind=false;std::string name;const ISocketListener* listener=nullptr;};
struct Record {int from=0,to=0,target_id=0;const ISocketListener* callback=nullptr;std::vector<std::uint8_t> bytes;host_dds::DetailStamp observation;};
struct Link {std::mutex gate;std::condition_variable changed;std::array<Row,6> rows{};std::array<bool,2> binding{};Snapshot counts;bool backpressure=false,forwarding=true,delay_bind=false,release_bind=false;std::deque<Record> queue;std::shared_ptr<host_dds::Trace> trace;std::function<void(int,int)> send_observer;};
std::shared_ptr<Link> link;
std::string Name(int side,const char* role){return std::string("host.public.")+(side?"B.":"A.")+role;}
std::string Network(int side){return side?"model.net.B":"model.net.A";}
int Index(int id){for(int side=0;side<2;++side)for(int role=0;role<3;++role)if(id==100+side*100+role)return side*3+role;return -1;}
void Event(const char* name,int side,std::string detail){link->trace->Add({name,"","",std::move(detail),side});}
int Invalid(const char* why){++link->counts.invalid;link->trace->Add({"TRACE_ERROR","","",why,0});return -701;}
}
void Configure(std::shared_ptr<host_dds::Trace> trace,bool forwarding){
    auto p=std::make_shared<Link>();p->trace=std::move(trace);p->forwarding=forwarding;
    for(int i=0;i<2;++i)for(int role=0;role<3;++role){auto& r=p->rows[i*3+role];r.side=i;r.role=role;r.id=100+i*100+role;r.name=Name(i,role==0?"listener":role==1?"outgoing":"accepted");}
    link=std::move(p);
}
void UseBoundedBackpressure(){std::lock_guard<std::mutex> lock(link->gate);link->backpressure=true;}
void SetSendObserver(std::function<void(int,int)> f){std::lock_guard<std::mutex> lock(link->gate);link->send_observer=std::move(f);}
void SetFrameDetail(bool on){link->trace->SetDetail(on);}
Snapshot Observe(){std::lock_guard<std::mutex> lock(link->gate);auto v=link->counts;v.queued=link->queue.size();for(auto& r:link->rows)if(r.active)++v.live;return v;}
void DelayBindCallbacks(){std::lock_guard<std::mutex> lock(link->gate);link->delay_bind=true;}
void ReleaseBindCallbacks(){std::lock_guard<std::mutex> lock(link->gate);link->release_bind=true;}
bool PumpBind(){const ISocketListener* callback=nullptr;int side=0,id=0;
    {std::lock_guard<std::mutex> lock(link->gate);if(!link->release_bind)return false;for(auto& row:link->rows)if(row.pending_bind&&row.active){row.pending_bind=false;callback=row.listener;side=row.side;id=row.id;break;}}
    if(!callback)return false;auto name=Name(1-side,"outgoing"),network=Network(1-side);std::string package="host.public.dds";PeerSocketInfo peer{};peer.name=name.data();peer.networkId=network.data();peer.pkgName=package.data();peer.dataType=DATA_TYPE_BYTES;
    callback->OnBind(id,peer);Event("FAKE_DELAYED_ON_BIND",side,"accepted="+std::to_string(id));return true;
}
bool PumpOne(){if(PumpBind())return true;Record r;bool deliver=false;{
    std::lock_guard<std::mutex> lock(link->gate);if(link->queue.empty())return false;r=std::move(link->queue.front());link->queue.pop_front();link->counts.bytes-=r.bytes.size();link->changed.notify_all();const int index=Index(r.target_id);deliver=index>=0&&link->rows[index].active&&link->rows[index].bound;
    if(deliver)++link->counts.forwards[r.from];}
    if(deliver){link->trace->DetailFrame(r.observation,"FAKE_INGRESS",r.to,r.bytes.data(),r.bytes.size());link->trace->Detail({"FAKE_SAVED_SERVER_CALLBACK","","","accepted="+std::to_string(r.target_id),r.to},r.observation);r.callback->OnBytes(r.target_id,r.bytes.data(),static_cast<uint32_t>(r.bytes.size()));}
    else Event("FAKE_TERMINAL_QUEUE_DROP",r.to,"closed_original_target="+std::to_string(r.target_id));
    return true;
}
bool RejectExtraCandidate(int side){const ISocketListener* callback=nullptr;{std::lock_guard<std::mutex> lock(link->gate);callback=link->rows[side*3].listener;}
    if(!callback)return false;auto name=Name(1-side,"outgoing"),net=Network(1-side);std::string pkg="host.public.dds";PeerSocketInfo p{};p.name=name.data();p.networkId=net.data();p.pkgName=pkg.data();p.dataType=DATA_TYPE_BYTES;
    const bool allowed=callback->OnNegotiate(900+side,p);if(!allowed){{std::lock_guard<std::mutex> lock(link->gate);++link->counts.rejected_candidates;}Event("FAKE_REJECTED_CANDIDATE_DELETE",side,"SDK_owned_uncommitted_id="+std::to_string(900+side));}return !allowed;
}
void LateBytes(int side){const ISocketListener* callback=nullptr;{std::lock_guard<std::mutex> lock(link->gate);callback=link->rows[side*3].listener;}if(callback){std::uint8_t b=7;Event("FAKE_LATE_CALLBACK_PROBE",side,"defensive_closed_entry");callback->OnBytes(102+side*100,&b,1);}}
bool UnsupportedNegotiation2(int side){const ISocketListener* callback=nullptr;{std::lock_guard<std::mutex> lock(link->gate);callback=link->rows[side*3].listener;}
    if(!callback||!callback->OnNegotiate2)throw std::runtime_error("missing actual saved negotiation2 callback");
    auto name=Name(1-side,"outgoing"),network=Network(1-side);std::string package="host.public.dds";PeerSocketInfo peer{};peer.name=name.data();peer.networkId=network.data();peer.pkgName=package.data();peer.dataType=DATA_TYPE_BYTES;
    return callback->OnNegotiate2(900+side,peer,nullptr,nullptr);
}
}
extern "C" {
int32_t Socket(SocketInfo info){using namespace public_socket_fake;std::lock_guard<std::mutex> lock(link->gate);
    if(!info.name||!info.pkgName||info.dataType!=DATA_TYPE_BYTES)return Invalid("Fake Socket invalid config");
    for(auto& row:link->rows)if(row.role!=2&&row.name==info.name){
        if(row.active)return Invalid("Fake duplicate active Socket");
        if(row.role==0&&(info.peerName||info.peerNetworkId))return Invalid("listener has peer");
        if(row.role==1&&(!info.peerName||!info.peerNetworkId||Name(1-row.side,"listener")!=info.peerName||Network(1-row.side)!=info.peerNetworkId))return Invalid("outgoing static peer mismatch");
        row.active=true;++link->counts.sockets[row.side];link->trace->Add({"FAKE_C_SOCKET","","","id="+std::to_string(row.id)+",role="+std::to_string(row.role),row.side});return row.id;
    }return Invalid("unknown static Socket name");
}
int32_t Listen(int32_t id,const QosTV* q,uint32_t n,const ISocketListener* cb){using namespace public_socket_fake;{
    std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||link->rows[k].role!=0||!link->rows[k].active||!q||!n||!cb||!cb->OnBind||!cb->OnNegotiate||!cb->OnBytes||!cb->OnShutdown)return Invalid("Fake Listen invalid");if(n>8)return Invalid("Fake QoS capture bound");auto& observed=link->counts.listen_qos[link->rows[k].side];observed.count=n;for(unsigned i=0;i<n;++i){observed.type[i]=q[i].qos;observed.value[i]=q[i].value;}link->rows[k].listener=cb;link->rows[k].bound=true;++link->counts.listens[link->rows[k].side];}
    link->changed.notify_all();Event("FAKE_C_LISTEN",(id-100)/100,"id="+std::to_string(id));return 0;
}
int32_t Bind(int32_t id,const QosTV* q,uint32_t n,const ISocketListener* cb){using namespace public_socket_fake;int side=0,destination=0,accepted_id=0;const ISocketListener* server=nullptr;
    {
        std::unique_lock<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||link->rows[k].role!=1||!link->rows[k].active||!q||!n||!cb||!cb->OnShutdown)return Invalid("Fake Bind invalid");
        auto& outgoing=link->rows[k];side=outgoing.side;if(n>8)return Invalid("Fake QoS capture bound");auto& observed=link->counts.bind_qos[side];observed.count=n;for(unsigned i=0;i<n;++i){observed.type[i]=q[i].qos;observed.value[i]=q[i].value;}destination=1-side;outgoing.listener=cb;link->binding[side]=true;++link->counts.binds[side];link->changed.notify_all();
        if(!link->changed.wait_for(lock,std::chrono::seconds(3),[&]{return link->binding[0]&&link->binding[1]&&link->rows[destination*3].listener;}))return Invalid("Fake bounded two-Bind setup barrier");
        auto& accepted=link->rows[destination*3+2];if(accepted.active)return Invalid("Fake duplicate normal accepted");accepted.active=true;accepted.listener=link->rows[destination*3].listener;accepted.peer=id;outgoing.peer=accepted.id;server=accepted.listener;accepted_id=accepted.id;
    }
    auto name=Name(side,"outgoing"),network=Network(side);std::string package="host.public.dds";PeerSocketInfo peer{};peer.name=name.data();peer.networkId=network.data();peer.pkgName=package.data();peer.dataType=DATA_TYPE_BYTES;
    Event("FAKE_C_BIND",side,"outgoing="+std::to_string(id)+",destination_accepted="+std::to_string(accepted_id));
    const bool permitted=server->OnNegotiate(accepted_id,peer);Event("FAKE_ON_NEGOTIATE",destination,"accepted="+std::to_string(accepted_id)+",result="+std::to_string(permitted));
    if(!permitted){std::lock_guard<std::mutex> lock(link->gate);link->rows[destination*3+2].active=false;link->rows[side*3+1].peer=-1;++link->counts.rejected_candidates;return -703;}
    bool deferred=false;{std::lock_guard<std::mutex> lock(link->gate);deferred=link->delay_bind;if(deferred)link->rows[destination*3+2].pending_bind=true;}
    if(!deferred){server->OnBind(accepted_id,peer);Event("FAKE_ON_BIND",destination,"accepted="+std::to_string(accepted_id)+",adapter_readiness_checked_by_test_not_assumed_from_void_callback");}
    {std::lock_guard<std::mutex> lock(link->gate);link->rows[destination*3+2].bound=true;link->rows[side*3+1].bound=true;}return 0;
}
int32_t SendBytes(int32_t id,const void* data,uint32_t size){using namespace public_socket_fake;int side=0,to=0,target=0;const ISocketListener* callback=nullptr;std::function<void(int,int)> observer;const auto observation=link->trace->Capture();
    {std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||link->rows[k].role!=1||!data||!size||size>8192)return Invalid("Fake send wrong role/buffer");auto& r=link->rows[k];side=r.side;++link->counts.sends[side];const auto destination=Index(r.peer);if(!r.active||!r.bound||destination<0||!link->rows[destination].active)return -704;if(link->rows[destination].role!=2||!link->rows[destination].bound)return Invalid("Fake send target not accepted");to=link->rows[destination].side;target=r.peer;callback=link->rows[destination].listener;observer=link->send_observer;}
    if(observer)observer(side,id);const auto* p=static_cast<const std::uint8_t*>(data);link->trace->DetailFrame(observation,"FAKE_SEND",side,p,size);
    if(!link->forwarding)return 0;
    {std::unique_lock<std::mutex> lock(link->gate);
        const auto alive=[&]{const auto from=Index(id),to=Index(target);return from>=0&&to>=0&&link->rows[from].active&&link->rows[from].bound&&link->rows[from].peer==target&&link->rows[to].active&&link->rows[to].bound&&link->rows[to].listener==callback;};
        const auto capacity=[&]{return link->queue.size()<64&&size<=512*1024-link->counts.bytes;};
        if(link->backpressure&&!capacity()){++link->counts.capacity_waits;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);if(!link->changed.wait_until(lock,end,[&]{return capacity()||!alive();})){++link->counts.capacity_timeouts;return -705;}}
        if(!alive())return -704;
        if(!capacity())return Invalid("bounded Fake byte queue exhausted");Record r;r.from=side;r.to=to;r.target_id=target;r.callback=callback;r.observation=observation;r.bytes.assign(p,p+size);link->queue.push_back(std::move(r));link->counts.bytes+=size;link->counts.record_peak=std::max(link->counts.record_peak,link->queue.size());link->counts.byte_peak=std::max(link->counts.byte_peak,link->counts.bytes);}return 0;
}
void Shutdown(int32_t id){using namespace public_socket_fake;const ISocketListener* notify=nullptr;int peer=-1,side=0,peer_side=0;
    {std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||!link->rows[k].active){Invalid("Fake unknown/duplicate Shutdown");return;}auto& r=link->rows[k];side=r.side;r.active=false;++link->counts.shutdowns[side];const auto pi=Index(r.peer);if(pi>=0&&link->rows[pi].active){auto& other=link->rows[pi];other.active=false;peer=other.id;peer_side=other.side;notify=other.listener;++link->counts.notified[peer_side];}}
    link->changed.notify_all();Event("FAKE_C_SHUTDOWN",side,"original_owned_id="+std::to_string(id));if(notify){Event("FAKE_TERMINAL_NOTIFICATION",peer_side,"id="+std::to_string(peer));notify->OnShutdown(peer,SHUTDOWN_REASON_PEER);}
}
}
