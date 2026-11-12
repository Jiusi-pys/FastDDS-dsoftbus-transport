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
struct Row {int id=0,side=0,role=0,peer=-1;bool active=false,bound=false,pending_bind=false,created=false;unsigned root_generation=0;std::string name,package;const ISocketListener* listener=nullptr;};
struct Record {int from=0,to=0,target_id=0;const ISocketListener* callback=nullptr;std::vector<std::uint8_t> bytes;host_dds::DetailStamp observation;};
struct Link {std::mutex gate;std::condition_variable changed;std::deque<Row> rows=std::deque<Row>(6);std::array<bool,2> binding{};Snapshot counts;bool backpressure=false,forwarding=true,delay_bind=false,release_bind=false;std::deque<Record> queue;std::shared_ptr<host_dds::Trace> trace;std::function<void(int,int)> send_observer;std::function<void(int,const std::uint8_t*,std::size_t)> packet_observer;};
std::shared_ptr<Link> link;
bool replacement_ids=false;
int next_replacement_id=1000;
std::array<std::int32_t,3> injected{};std::function<void()> shutdown_barrier,bind_barrier;
int Failure(InjectedOperation op){const auto i=static_cast<std::size_t>(op);const auto rc=injected[i];injected[i]=0;return rc;}
std::string Name(int side,const char* role){return std::string("host.public.")+(side?"B.":"A.")+role;}
std::string Network(int side){return side?"model.net.B":"model.net.A";}
int Index(int id){for(std::size_t i=0;i<link->rows.size();++i)if(link->rows[i].id==id)return static_cast<int>(i);return -1;}
// Host Socket model capacity only (documented SDK max15, not board evidence).
int AppendRow(int side,int role){if(link->rows.size()==15)return -1;Row row;row.side=side;row.role=role;row.id=next_replacement_id++;link->rows.push_back(std::move(row));return static_cast<int>(link->rows.size()-1);}
bool Text(const char* value,std::size_t limit){if(!value)return false;std::size_t n=0;while(n<=limit&&value[n])++n;return n>0&&n<=limit;}
void Event(const char* name,int side,std::string detail){link->trace->Add({name,"","",std::move(detail),side});}
int Invalid(const char* why){++link->counts.invalid;link->trace->Add({"TRACE_ERROR","","",why,0});return -701;}
}
void Configure(std::shared_ptr<host_dds::Trace> trace,bool forwarding){
    auto p=std::make_shared<Link>();p->trace=std::move(trace);p->forwarding=forwarding;
    for(int i=0;i<2;++i)for(int role=0;role<3;++role){auto& r=p->rows[i*3+role];r.side=i;r.role=role;r.id=100+i*100+role;r.name=Name(i,role==0?"listener":role==1?"outgoing":"accepted");}
    link=std::move(p);injected={};shutdown_barrier={};bind_barrier={};replacement_ids=false;next_replacement_id=1000;
}
void EnableFreshReplacementIds(){std::lock_guard<std::mutex> lock(link->gate);replacement_ids=true;}
int CurrentSocket(int side,int role){std::lock_guard<std::mutex> lock(link->gate);return link->rows[side*3+role].id;}
void DisconnectDirection(int from,bool outgoing_first){
    const ISocketListener *out=nullptr,*in=nullptr;int a=0,b=0;
    {std::lock_guard<std::mutex> lock(link->gate);auto& x=link->rows[from*3+1];auto& y=link->rows[(1-from)*3+2];
        if(!x.active||!y.active||x.peer!=y.id)throw std::runtime_error("disconnect requires original live connection");
        x.active=false;y.active=false;a=x.id;b=y.id;out=x.listener;in=y.listener;++link->counts.notified[from];++link->counts.notified[1-from];}
    // Scheduling only: the original public callbacks receive original numeric IDs.
    if(outgoing_first){out->OnShutdown(a,SHUTDOWN_REASON_PEER);in->OnShutdown(b,SHUTDOWN_REASON_PEER);}
    else{in->OnShutdown(b,SHUTDOWN_REASON_PEER);out->OnShutdown(a,SHUTDOWN_REASON_PEER);}
    link->changed.notify_all();Event("FAKE_CONTROLLED_CONNECTION_END",from,"original_outgoing="+std::to_string(a)+",original_accepted="+std::to_string(b));
}
unsigned LinkDown(int reason){
    struct End{const ISocketListener* listener;int id;int side;};std::vector<End> ends;
    {std::lock_guard<std::mutex> lock(link->gate);
        for(auto& row:link->rows)if(row.active&&row.role!=0&&row.listener)ends.push_back({row.listener,row.id,row.side});}
    // Notify before retiring: a send racing this event is queued and later
    // dropped as terminal, as for a real link that dies under in-flight data.
    for(const auto& e:ends)e.listener->OnShutdown(e.id,static_cast<ShutdownReason>(reason));
    {std::lock_guard<std::mutex> lock(link->gate);
        for(const auto& e:ends){const auto k=Index(e.id);if(k>=0){link->rows[k].active=false;link->rows[k].peer=-1;++link->counts.notified[e.side];}}}
    link->changed.notify_all();Event("FAKE_LINK_DOWN",0,"connections="+std::to_string(ends.size())+",reason="+std::to_string(reason));
    return static_cast<unsigned>(ends.size());
}
void EndConnection(int id,int reason){
    const ISocketListener *first=nullptr,*second=nullptr;int peer=-1;
    {std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);
        if(k<0||!link->rows[k].active||link->rows[k].peer<0)throw std::runtime_error("end requires a live connection");
        auto& x=link->rows[k];peer=x.peer;const auto p=Index(peer);if(p<0||!link->rows[p].active)throw std::runtime_error("end requires a live peer");
        auto& y=link->rows[p];first=x.listener;second=y.listener;x.active=y.active=false;x.peer=y.peer=-1;
        ++link->counts.notified[x.side];++link->counts.notified[y.side];}
    first->OnShutdown(id,static_cast<ShutdownReason>(reason));second->OnShutdown(peer,static_cast<ShutdownReason>(reason));
    link->changed.notify_all();Event("FAKE_CONNECTION_END",0,"id="+std::to_string(id)+",peer="+std::to_string(peer));
}
void FailNext(InjectedOperation op,std::int32_t rc){std::lock_guard<std::mutex> lock(link->gate);injected[static_cast<std::size_t>(op)]=rc;}
void SetShutdownBarrier(std::function<void()> fn){std::lock_guard<std::mutex> lock(link->gate);shutdown_barrier=std::move(fn);}
void SetBindBarrier(std::function<void()> fn){std::lock_guard<std::mutex> lock(link->gate);bind_barrier=std::move(fn);}
void StopListener(int side){
    const ISocketListener* callback=nullptr;int id=-1;
    {std::lock_guard<std::mutex> lock(link->gate);if(side<0||side>1)throw std::runtime_error("listener side");auto& r=link->rows[side*3];if(!r.active||!r.listener)throw std::runtime_error("listener not live");r.active=false;id=r.id;callback=r.listener;++link->counts.notified[side];}
    callback->OnShutdown(id,SHUTDOWN_REASON_PEER);link->changed.notify_all();
}
bool ProbeCandidate(int side,std::int32_t id,const std::string& name,const std::string& network,const std::string& package,int data_type){
    const ISocketListener* cb=nullptr;
    {std::lock_guard<std::mutex> lock(link->gate);if(side<0||side>1)return false;cb=link->rows[side*3].listener;}
    if(!cb||!cb->OnNegotiate)return false;
    PeerSocketInfo peer{};peer.name=const_cast<char*>(name.c_str());peer.networkId=const_cast<char*>(network.c_str());
    peer.pkgName=const_cast<char*>(package.c_str());peer.dataType=static_cast<TransDataType>(data_type);
    return cb->OnNegotiate(id,peer);
}
void UseBoundedBackpressure(){std::lock_guard<std::mutex> lock(link->gate);link->backpressure=true;}
void SetSendObserver(std::function<void(int,int)> f){std::lock_guard<std::mutex> lock(link->gate);link->send_observer=std::move(f);}
void SetListenerNames(const std::array<std::string,2>& names){std::lock_guard<std::mutex> lock(link->gate);for(int i=0;i<2;++i){if(link->rows[i*3].active)throw std::runtime_error("listener names after exposure");link->rows[i*3].name=names[i];}}
void SetInitiatorNames(const std::array<std::string,2>& names){
    std::lock_guard<std::mutex> lock(link->gate);
    for(const auto& row:link->rows)if(row.active)throw std::runtime_error("initiator names while sockets active");
    for(int side=0;side<2;++side)link->rows[side*3+1].name=names[side];
}
std::string OutgoingName(int side){std::lock_guard<std::mutex> lock(link->gate);return link->rows[side*3+1].name;}
SavedBytesCallback SaveAcceptedBytes(int side){
    std::lock_guard<std::mutex> lock(link->gate);
    if(side<0||side>1)throw std::runtime_error("saved callback side");
    const auto& row=link->rows[side*3+2];
    if(!row.active||!row.listener||!row.listener->OnBytes)throw std::runtime_error("no live original accepted callback");
    return {row.id,row.listener->OnBytes}; // copy the function value, not table address
}
void SetPacketObserver(std::function<void(int,const std::uint8_t*,std::size_t)> fn){std::lock_guard<std::mutex> lock(link->gate);link->packet_observer=std::move(fn);}
void SetFrameDetail(bool on){link->trace->SetDetail(on);}
Snapshot Observe(){std::lock_guard<std::mutex> lock(link->gate);auto v=link->counts;v.queued=link->queue.size();for(auto& r:link->rows)if(r.active)++v.live;return v;}
void DelayBindCallbacks(){std::lock_guard<std::mutex> lock(link->gate);link->delay_bind=true;}
void ReleaseBindCallbacks(){std::lock_guard<std::mutex> lock(link->gate);link->release_bind=true;}
bool PumpBind(){const ISocketListener* callback=nullptr;int side=0,id=0;std::string name,network,package;
    {std::lock_guard<std::mutex> lock(link->gate);if(!link->release_bind)return false;for(auto& row:link->rows)if(row.pending_bind&&row.active){
        const auto from=Index(row.peer);if(from<0)continue;row.pending_bind=false;callback=row.listener;side=row.side;id=row.id;
        name=link->rows[from].name;package=link->rows[from].package;network=Network(link->rows[from].side);break;}}
    if(!callback)return false;PeerSocketInfo peer{};peer.name=name.data();peer.networkId=network.data();peer.pkgName=package.data();peer.dataType=DATA_TYPE_BYTES;
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
    if(const auto rc=Failure(InjectedOperation::Socket))return rc;
    if(!Text(info.name,255)||!Text(info.pkgName,64)||info.dataType!=DATA_TYPE_BYTES)return Invalid("Fake Socket invalid config");
    const bool client=info.peerName||info.peerNetworkId;
    int side=-1,k=-1;
    if(client) {
        if(!Text(info.peerName,255)||!Text(info.peerNetworkId,64))return Invalid("Fake outgoing missing peer");
        for(int target=0;target<2;++target)if(link->rows[target*3].name==info.peerName&&Network(target)==info.peerNetworkId)side=1-target;
        if(side<0)return Invalid("Fake outgoing unknown listener/network");
        for(std::size_t i=0;i<link->rows.size();++i)if(link->rows[i].role==1&&link->rows[i].side==side&&link->rows[i].name==info.name){k=static_cast<int>(i);break;}
        if(k<0){auto& first=link->rows[side*3+1];if(!first.active&&(!first.created||first.root_generation!=link->rows[side*3].root_generation))k=side*3+1;else k=AppendRow(side,1);}
    }else for(int i=0;i<2;++i)if(link->rows[i*3].name==info.name){side=i;k=i*3;break;}
    if(k<0)return Invalid("Fake bounded Socket model capacity or unknown server");
    auto& row=link->rows[k];if(row.active)return Invalid("Fake duplicate active Socket");
    if(replacement_ids&&row.role==1&&row.listener)row.id=next_replacement_id++;
    if(row.role==0)++row.root_generation;else row.root_generation=link->rows[side*3].root_generation;
    row.name=info.name;row.package=info.pkgName;row.created=true;row.active=true;row.bound=false;row.peer=-1;
    ++link->counts.sockets[row.side];link->trace->Add({"FAKE_C_SOCKET","","","id="+std::to_string(row.id)+",role="+std::to_string(row.role)+",name="+row.name,row.side});return row.id;
}
int32_t Listen(int32_t id,const QosTV* q,uint32_t n,const ISocketListener* cb){using namespace public_socket_fake;{
    std::lock_guard<std::mutex> lock(link->gate);if(const auto rc=Failure(InjectedOperation::Listen))return rc;const auto k=Index(id);if(k<0||link->rows[k].role!=0||!link->rows[k].active||!q||!n||!cb||!cb->OnBind||!cb->OnNegotiate||!cb->OnBytes||!cb->OnShutdown)return Invalid("Fake Listen invalid");if(n>8)return Invalid("Fake QoS capture bound");auto& observed=link->counts.listen_qos[link->rows[k].side];observed.count=n;for(unsigned i=0;i<n;++i){observed.type[i]=q[i].qos;observed.value[i]=q[i].value;}link->rows[k].listener=cb;link->rows[k].bound=true;++link->counts.listens[link->rows[k].side];}
    link->changed.notify_all();Event("FAKE_C_LISTEN",(id-100)/100,"id="+std::to_string(id));return 0;
}
int32_t Bind(int32_t id,const QosTV* q,uint32_t n,const ISocketListener* cb){using namespace public_socket_fake;int side=0,destination=0,accepted_id=0,out_index=-1,accepted_index=-1;const ISocketListener* server=nullptr;std::string name,network,package;
    std::function<void()> barrier;{std::lock_guard<std::mutex> lock(link->gate);barrier=bind_barrier;}if(barrier)barrier();
    {
        std::unique_lock<std::mutex> lock(link->gate);if(const auto rc=Failure(InjectedOperation::Bind))return rc;const auto k=Index(id);if(k<0||link->rows[k].role!=1||!link->rows[k].active||!q||!n||!cb||!cb->OnShutdown)return Invalid("Fake Bind invalid");
        auto& outgoing=link->rows[k];out_index=k;side=outgoing.side;if(n>8)return Invalid("Fake QoS capture bound");auto& observed=link->counts.bind_qos[side];observed.count=n;for(unsigned i=0;i<n;++i){observed.type[i]=q[i].qos;observed.value[i]=q[i].value;}destination=1-side;outgoing.listener=cb;link->binding[side]=true;++link->counts.binds[side];link->changed.notify_all();
        // A documented Bind targets a listening server; it does not require a
        // second independent client's Bind. The old two-Bind barrier was a seam restriction.
        if(!link->changed.wait_for(lock,std::chrono::seconds(3),[&]{const auto& target=link->rows[destination*3];return target.active&&target.bound&&target.listener;}))return Invalid("Fake bounded listener readiness");
        auto& first=link->rows[destination*3+2];
        if(!first.active&&(!first.created||first.root_generation!=link->rows[destination*3].root_generation||replacement_ids))accepted_index=destination*3+2;
        else accepted_index=AppendRow(destination,2);
        if(accepted_index<0)return Invalid("Fake accepted model capacity");
        auto& accepted=link->rows[accepted_index];if(replacement_ids&&accepted.listener)accepted.id=next_replacement_id++;
        accepted.created=true;accepted.active=true;accepted.bound=false;accepted.root_generation=link->rows[destination*3].root_generation;
        accepted.listener=link->rows[destination*3].listener;accepted.peer=id;outgoing.peer=accepted.id;server=accepted.listener;accepted_id=accepted.id;
        name=outgoing.name;network=Network(side);package=outgoing.package;
    }
    PeerSocketInfo peer{};peer.name=name.data();peer.networkId=network.data();peer.pkgName=package.data();peer.dataType=DATA_TYPE_BYTES;
    Event("FAKE_C_BIND",side,"outgoing="+std::to_string(id)+",destination_accepted="+std::to_string(accepted_id));
    const bool permitted=server->OnNegotiate(accepted_id,peer);Event("FAKE_ON_NEGOTIATE",destination,"accepted="+std::to_string(accepted_id)+",result="+std::to_string(permitted));
    if(!permitted){std::lock_guard<std::mutex> lock(link->gate);link->rows[accepted_index].active=false;link->rows[out_index].peer=-1;++link->counts.rejected_candidates;return -703;}
    bool deferred=false;{std::lock_guard<std::mutex> lock(link->gate);deferred=link->delay_bind;if(deferred)link->rows[accepted_index].pending_bind=true;}
    if(!deferred){server->OnBind(accepted_id,peer);Event("FAKE_ON_BIND",destination,"accepted="+std::to_string(accepted_id)+",adapter_readiness_checked_by_test_not_assumed_from_void_callback");}
    {std::lock_guard<std::mutex> lock(link->gate);link->rows[accepted_index].bound=true;link->rows[out_index].bound=true;}return 0;
}
int32_t SendBytes(int32_t id,const void* data,uint32_t size){using namespace public_socket_fake;int side=0,to=0,target=0;const ISocketListener* callback=nullptr;std::function<void(int,int)> observer;std::function<void(int,const std::uint8_t*,std::size_t)> packet;const auto observation=link->trace->Capture();
    {std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||link->rows[k].role!=1||!data||!size||size>8192)return Invalid("Fake send wrong role/buffer");auto& r=link->rows[k];side=r.side;++link->counts.sends[side];const auto destination=Index(r.peer);if(!r.active||!r.bound||destination<0||!link->rows[destination].active)return -704;if(link->rows[destination].role!=2||!link->rows[destination].bound)return Invalid("Fake send target not accepted");to=link->rows[destination].side;target=r.peer;callback=link->rows[destination].listener;observer=link->send_observer;packet=link->packet_observer;}
    if(packet)packet(side,static_cast<const std::uint8_t*>(data),size);if(observer)observer(side,id);const auto* p=static_cast<const std::uint8_t*>(data);link->trace->DetailFrame(observation,"FAKE_SEND",side,p,size);
    if(!link->forwarding)return 0;
    {std::unique_lock<std::mutex> lock(link->gate);
        const auto alive=[&]{const auto from=Index(id),to=Index(target);return from>=0&&to>=0&&link->rows[from].active&&link->rows[from].bound&&link->rows[from].peer==target&&link->rows[to].active&&link->rows[to].bound&&link->rows[to].listener==callback;};
        const auto capacity=[&]{return link->queue.size()<64&&size<=512*1024-link->counts.bytes;};
        if(link->backpressure&&!capacity()){++link->counts.capacity_waits;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);if(!link->changed.wait_until(lock,end,[&]{return capacity()||!alive();})){++link->counts.capacity_timeouts;return -705;}}
        if(!alive())return -704;
        if(!capacity())return Invalid("bounded Fake byte queue exhausted");Record r;r.from=side;r.to=to;r.target_id=target;r.callback=callback;r.observation=observation;r.bytes.assign(p,p+size);link->queue.push_back(std::move(r));link->counts.bytes+=size;link->counts.record_peak=std::max(link->counts.record_peak,link->queue.size());link->counts.byte_peak=std::max(link->counts.byte_peak,link->counts.bytes);}return 0;
}
void Shutdown(int32_t id){using namespace public_socket_fake;std::function<void()> barrier;{std::lock_guard<std::mutex> lock(link->gate);barrier=shutdown_barrier;}if(barrier)barrier();const ISocketListener* notify=nullptr;int peer=-1,side=0,peer_side=0;
    {std::lock_guard<std::mutex> lock(link->gate);const auto k=Index(id);if(k<0||!link->rows[k].active){Invalid("Fake unknown/duplicate Shutdown");return;}auto& r=link->rows[k];side=r.side;r.active=false;++link->counts.shutdowns[side];const auto pi=Index(r.peer);if(pi>=0&&link->rows[pi].active){auto& other=link->rows[pi];other.active=false;peer=other.id;peer_side=other.side;notify=other.listener;++link->counts.notified[peer_side];}}
    link->changed.notify_all();Event("FAKE_C_SHUTDOWN",side,"original_owned_id="+std::to_string(id));if(notify){Event("FAKE_TERMINAL_NOTIFICATION",peer_side,"id="+std::to_string(peer));notify->OnShutdown(peer,SHUTDOWN_REASON_PEER);}
}
}
