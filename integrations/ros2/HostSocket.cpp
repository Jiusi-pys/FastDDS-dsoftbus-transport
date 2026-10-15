// Host-only public Socket C seam. It moves opaque, length-bounded bytes between
// independent processes and never parses RTPS/ROS or manufactures DDS results.
#include <winsock2.h>
#include <windows.h>
#undef SendMessage
#include "HostSocket.hpp"
#include "HostIoObservation.hpp"
#include <softbus_bus_center.h>
#include <socket.h>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <condition_variable>
#include <fstream>

namespace fastdds_dsoftbus::ros2_host {
namespace {
LinkConfiguration cfg;
std::mutex gate,send_gate;
constexpr std::size_t socket_capacity=16;
std::array<SOCKET,socket_capacity> sockets=[] {std::array<SOCKET,socket_capacity> a{};a.fill(INVALID_SOCKET);return a;}();
std::array<bool,socket_capacity> dial{},terminal{};
std::array<std::uint64_t,socket_capacity> connection_epochs{};
std::uint64_t next_connection_epoch=0; // observational only, protected by gate
namespace io=io_observation;
std::condition_variable terminal_changed;
int next_id=1,outgoing_id=-1,accepted_id=-1;
bool controlled_end=false,pause_reported=false;
INodeStateCb* node_callback=nullptr;
bool node_online=true,node_offline_sent=false,node_online_sent=false;
std::string node_network;
unsigned node_queries=0;
const ISocketListener* listener=nullptr;
std::thread accept_thread,read_thread;
std::atomic<bool> stop{false};
std::atomic<std::uint64_t> sends{0},ingress{0},send_errors{0};
int socket_count=0;
HANDLE ready=nullptr,peer_ready=nullptr;
bool transfer(SOCKET s,char* p,int n,bool write,int id,std::uint64_t epoch,const char* phase,std::uint64_t send_id=0) {
    auto event=io::Connection(write?"SEND_TRANSFER":"RECV_TRANSFER",id,epoch,s);event.phase=phase;event.send=send_id;
    return io::Transfer(p,n,write,event,[&](char* data,int bytes){return write ? ::send(s,data,bytes,0) : ::recv(s,data,bytes,0);},[]{return WSAGetLastError();});
}
void close_observed(SOCKET s,int id,std::uint64_t epoch,const char* site,bool shut) {
    auto event=io::Connection("CLOSE_ENTER",id,epoch,s);event.phase=site;io::Record(event);
    if(shut){event.kind="SHUTDOWN_RETURN";event.begin=io::Point();event.result=::shutdown(s,SD_BOTH);event.error=event.result==SOCKET_ERROR?WSAGetLastError():0;event.error_known=event.result==SOCKET_ERROR;event.result_known=true;io::Record(event);}
    event.kind="CLOSE_RETURN";event.begin=io::Point();event.result=closesocket(s);event.error=event.result==SOCKET_ERROR?WSAGetLastError():0;event.error_known=event.result==SOCKET_ERROR;event.result_known=true;io::Record(event);
}
void peer(PeerSocketInfo& p,bool incoming) {
    thread_local std::string network_copy;
    {std::lock_guard<std::mutex> lock(gate);network_copy=node_network;}
    p.name=const_cast<char*>((incoming?cfg.peer_outgoing:cfg.peer_listener).c_str());
    p.pkgName=const_cast<char*>(cfg.package.c_str());
    p.networkId=const_cast<char*>(network_copy.c_str());p.dataType=DATA_TYPE_BYTES;
}
void receive(SOCKET s,int id,const ISocketListener* callback,std::uint64_t epoch) {
    io::Record(io::Connection("RECEIVE_ENTER",id,epoch,s));
    while(!stop.load()) {
        std::uint32_t n=0;if(!transfer(s,reinterpret_cast<char*>(&n),4,false,id,epoch,"PREFIX"))break;
        n=ntohl(n);if(!n||n>65536) {io::Record(io::Connection("RECEIVE_INVALID_LENGTH",id,epoch,s));std::fputs("HOST_LINK_BAD_LENGTH\n",stderr);break;}
        std::vector<unsigned char> bytes(n);if(!transfer(s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(n),false,id,epoch,"BODY"))break;
        if(callback&&callback->OnBytes&&!stop.load()){++ingress;callback->OnBytes(id,bytes.data(),n);}
    }
    // Model the actual terminal notification of this owned Host connection.
    // This is not an SDK cancellation/epoch claim and never invents a data take.
    io::Record(io::Connection(stop.load()?"RECEIVE_EXIT_STOP_OBSERVED":"RECEIVE_EXIT",id,epoch,s));
    if(!stop.load()&&callback&&callback->OnShutdown){io::Record(io::Connection("ON_SHUTDOWN_ENTER",id,epoch,s));callback->OnShutdown(id,SHUTDOWN_REASON_PEER);io::Record(io::Connection("ON_SHUTDOWN_RETURN",id,epoch,s));}
    {std::lock_guard<std::mutex> lock(gate);if(sockets[id]==s){close_observed(s,id,epoch,"RECEIVE_CLEANUP",false);sockets[id]=INVALID_SOCKET;}terminal[id]=true;}
    terminal_changed.notify_all();
}
}
void ConfigureLink(LinkConfiguration c) {
    cfg=std::move(c);io::enabled=cfg.io_observation;node_network=cfg.side?"host.net.A":"host.net.B";WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup");
    const auto name="Local\\FastddsDsoftbusRos_"+cfg.run+"_"+std::to_string(cfg.side);
    ready=CreateEventA(nullptr,TRUE,FALSE,name.c_str());if(!ready||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("stale listener event");
}
bool WaitPeerListener(unsigned ms) {
    const auto name="Local\\FastddsDsoftbusRos_"+cfg.run+"_"+std::to_string(1-cfg.side);
    const auto end=GetTickCount64()+ms;
    while(GetTickCount64()<end) {
        peer_ready=OpenEventA(SYNCHRONIZE,FALSE,name.c_str());
        if(peer_ready)return WaitForSingleObject(peer_ready,static_cast<DWORD>(end-GetTickCount64()))==WAIT_OBJECT_0;
        // Bounded polling of an actual named-event creation, not a blind delay
        // replacing discovery or reader evidence.
        Sleep(1);
    }return false;
}
void StopLink() {
    io::Record({"LINK_STOP_STORE_ENTER"});
    stop.store(true);
    io::Record({"LINK_STOP_STORE_RETURN"});
    {std::lock_guard<std::mutex> l(gate);for(std::size_t id=0;id<sockets.size();++id){auto& s=sockets[id];if(s!=INVALID_SOCKET){close_observed(s,static_cast<int>(id),connection_epochs[id],"STOP_LINK",true);s=INVALID_SOCKET;}}}
    io::Record({"LINK_JOIN_ENTER"});
    if(accept_thread.joinable())accept_thread.join();if(read_thread.joinable())read_thread.join();
    io::Record({"LINK_JOIN_RETURN"});
    if(ready)CloseHandle(ready);if(peer_ready)CloseHandle(peer_ready);WSACleanup();
}
LinkObservation ObserveLink(){return {sends.load(),ingress.load(),send_errors.load()};}
void ReacceptControlReceipt(const char* action){
    if(!cfg.bounded_reaccept)return;
    std::ofstream file(cfg.reaccept_control+"."+action,std::ios::binary);
    file<<"HOST_REACCEPT run="<<cfg.run<<" side="<<cfg.side<<" action="<<action<<"\n";
}
bool ReacceptControlStep(){
    if(!cfg.bounded_reaccept)return true;
    const auto selected=[&](const char* action){std::ifstream file(cfg.reaccept_control+"."+action);std::string line;if(!std::getline(file,line))return false;if(!line.empty()&&line.back()=='\r')line.pop_back();return line=="HOST_REACCEPT run="+cfg.run+" side="+std::to_string(cfg.side)+" action="+action;};
    // Keep immutable commands: Windows readers need not grant delete sharing.
    if(selected("RESUME")||!selected("PAUSE"))return true;
    if(!pause_reported){ReacceptControlReceipt("PAUSED");pause_reported=true;}
    if(!controlled_end&&(selected("BREAK_OUTGOING")||selected("BREAK_ACCEPTED"))){
        const bool outgoing=selected("BREAK_OUTGOING");int id;
        {std::lock_guard<std::mutex> lock(gate);id=outgoing?outgoing_id:accepted_id;}
        if(id<=0)throw std::runtime_error("Host controlled end requires live original ID");
        // Raw Host connection ending. The original receive thread delivers the
        // unchanged public OnShutdown; only Runtime pumping may create a replacement.
        Shutdown(id);
        std::unique_lock<std::mutex> lock(gate);
        if(!terminal_changed.wait_for(lock,std::chrono::seconds(3),[&]{return terminal[id];}))throw std::runtime_error("Host terminal callback did not return");
        controlled_end=true;lock.unlock();ReacceptControlReceipt("BROKEN");
    }
    return false;
}
void NodeControlStep(){
    if(!cfg.route_lifecycle)return;
    const auto selected=[&](const char* phase){std::ifstream file(cfg.reaccept_control+"."+phase);std::string line;if(!std::getline(file,line))return false;if(!line.empty()&&line.back()=='\r')line.pop_back();return line=="NATIVE_CONTROL run="+cfg.run+" side="+(cfg.side?"B":"A")+" phase="+phase;};
    INodeStateCb* cb=nullptr;NodeBasicInfo info{};bool emit=false,online=false;
    {std::lock_guard<std::mutex> lock(gate);if(!node_callback)return;
        if(!node_offline_sent&&selected("NODE_OFFLINE")){node_offline_sent=true;node_online=false;emit=true;}
        else if(node_offline_sent&&!node_online_sent&&(selected("NODE_ONLINE_NEW")||selected("NODE_ONLINE_SAME"))){node_online_sent=true;node_online=true;online=true;emit=true;if(selected("NODE_ONLINE_NEW"))node_network+=".return";}
        if(emit){cb=node_callback;std::copy(node_network.begin(),node_network.end(),info.networkId);}}
    if(cb){if(online)cb->onNodeOnline(&info);else cb->onNodeOffline(&info);
        std::fprintf(stdout,"HOST_NODE_EVENT run=%s side=%c state=%s source=HOST_PUBLIC_NODE_API_MODEL\n",cfg.run.c_str(),cfg.side?'B':'A',online?"ONLINE":"OFFLINE");std::fflush(stdout);}
}
}
extern "C" {
int32_t RegNodeDeviceStateCb(const char*,INodeStateCb* cb){std::lock_guard<std::mutex> lock(fastdds_dsoftbus::ros2_host::gate);fastdds_dsoftbus::ros2_host::node_callback=cb;return 0;}
int32_t UnregNodeDeviceStateCb(INodeStateCb* cb){std::lock_guard<std::mutex> lock(fastdds_dsoftbus::ros2_host::gate);if(fastdds_dsoftbus::ros2_host::node_callback==cb)fastdds_dsoftbus::ros2_host::node_callback=nullptr;return 0;}
int32_t GetAllNodeDeviceInfo(const char*,NodeBasicInfo** info,int32_t* count){using namespace fastdds_dsoftbus::ros2_host;std::lock_guard<std::mutex> lock(gate);++node_queries;*count=node_online?1:0;*info=*count?static_cast<NodeBasicInfo*>(std::calloc(1,sizeof(NodeBasicInfo))):nullptr;if(*count&&!*info)return -1;if(*info)std::copy(node_network.begin(),node_network.end(),(*info)->networkId);std::fprintf(stdout,"HOST_NODE_QUERY run=%s side=%c count=%u online=%d source=HOST_PUBLIC_NODE_API_MODEL\n",cfg.run.c_str(),cfg.side?'B':'A',node_queries,int(node_online));std::fflush(stdout);return 0;}
void FreeNodeInfo(NodeBasicInfo* info){std::free(info);}
int32_t GetNodeKeyInfo(const char*,const char* network,NodeDeviceInfoKey key,uint8_t* info,int32_t size){
    using namespace fastdds_dsoftbus::ros2_host;std::lock_guard<std::mutex> lock(gate);
    if(!node_online||!network||node_network!=network||key!=NODE_KEY_UDID||!info||size<=static_cast<int32_t>(cfg.peer_udid.size()))return -1;
    std::memcpy(info,cfg.peer_udid.c_str(),cfg.peer_udid.size()+1);
    std::fprintf(stdout,"HOST_NODE_IDENTITY run=%s side=%c network=%s key=UDID result=0 source=HOST_PUBLIC_NODE_API_MODEL\n",cfg.run.c_str(),cfg.side?'B':'A',network);std::fflush(stdout);return 0;
}
}
extern "C" int32_t Socket(SocketInfo info) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(info.dataType!=DATA_TYPE_BYTES||!info.name||!info.pkgName)return -601;
    std::lock_guard<std::mutex> l(gate);
    if(cfg.route_lifecycle&&info.peerName){
        if(!node_online||!info.peerNetworkId||node_network!=info.peerNetworkId)return -609;
        std::fprintf(stdout,"HOST_SOCKET_ROUTE run=%s side=%c network=%s queries=%u source=HOST_PUBLIC_NODE_API_MODEL\n",cfg.run.c_str(),cfg.side?'B':'A',info.peerNetworkId,node_queries);std::fflush(stdout);
    }
    if(!cfg.bounded_reaccept){if(socket_count==2)return -602;return ++socket_count;}
    if(next_id>=int(socket_capacity))return -602;
    const int id=next_id++;dial[id]=info.peerName!=nullptr;++socket_count;return id;
}
extern "C" int32_t Listen(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id!=1||!cb)return -603;
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.port);
    if(s==INVALID_SOCKET||::bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||::listen(s,1)!=0){if(s!=INVALID_SOCKET)close_observed(s,id,0,"LISTEN_SETUP_FAILURE",false);return -604;}
    {std::lock_guard<std::mutex> l(gate);sockets[1]=s;connection_epochs[1]=++next_connection_epoch;io::Record(io::Connection("CONNECTION_INSTALL",1,connection_epochs[1],s));listener=cb;}
    accept_thread=std::thread([s,cb]{do {SOCKET incoming=::accept(s,nullptr,nullptr);if(incoming==INVALID_SOCKET)return;
        int id=3;std::uint64_t epoch=0;{std::lock_guard<std::mutex> l(gate);if(cfg.bounded_reaccept){if(next_id>=int(socket_capacity)){close_observed(incoming,-1,0,"ACCEPT_CAPACITY_FAILURE",false);return;}id=next_id++;}sockets[id]=incoming;epoch=connection_epochs[id]=++next_connection_epoch;io::Record(io::Connection("CONNECTION_INSTALL",id,epoch,incoming));accepted_id=id;}
        PeerSocketInfo p{};peer(p,true);
        if(!cb->OnNegotiate||!cb->OnNegotiate(id,p)){Shutdown(id);if(!cfg.bounded_reaccept)return;continue;}
        if(cb->OnBind)cb->OnBind(id,p);receive(incoming,id,cb,epoch);
        }while(cfg.bounded_reaccept&&!stop.load());});
    SetEvent(ready);std::fprintf(stderr,"HOST_LISTENER_READY side=%u\n",cfg.side);std::fflush(stderr);return 0;
}
extern "C" int32_t Bind(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(!cb||id<=0||id>=int(socket_capacity)||(cfg.bounded_reaccept?!dial[id]:id!=2))return -605;
    if(read_thread.joinable())read_thread.join();
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.peer_port);
    if(s==INVALID_SOCKET||::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){if(s!=INVALID_SOCKET)close_observed(s,id,0,"BIND_SETUP_FAILURE",false);return -606;}
    std::uint64_t epoch=0;{std::lock_guard<std::mutex> l(gate);sockets[id]=s;epoch=connection_epochs[id]=++next_connection_epoch;io::Record(io::Connection("CONNECTION_INSTALL",id,epoch,s));outgoing_id=id;}
    PeerSocketInfo p{};peer(p,false);if(cb->OnBind)cb->OnBind(id,p);
    read_thread=std::thread([s,cb,id,epoch]{receive(s,id,cb,epoch);});return 0;
}
extern "C" int32_t SendBytes(int32_t id,const void* data,uint32_t n) {
    using namespace fastdds_dsoftbus::ros2_host;
    const auto send_id=io::enabled?io::send_ids.fetch_add(1,std::memory_order_relaxed)+1:0;
    auto event=io::Connection("SEND_ENTER",id,0,INVALID_SOCKET);event.send=send_id;event.requested=static_cast<int>(n);io::Record(event);
    const auto finish=[&](int result){event.kind="SEND_RETURN";event.result_known=true;event.result=result;io::Record(event);return result;};
    if(!data||!n||n>65536||id<=1||id>=int(socket_capacity)||(!cfg.bounded_reaccept&&id!=2&&id!=3))return finish(-607);
    std::lock_guard<std::mutex> s(send_gate);SOCKET target;{std::lock_guard<std::mutex> l(gate);target=sockets[id];event.epoch=connection_epochs[id];event.handle=static_cast<std::uint64_t>(target);}
    event.kind="SEND_CAPTURE";io::Record(event);
    if(target==INVALID_SOCKET)return finish(-608);
    ++sends;
    if(!cfg.forward)return finish(0);std::uint32_t size=htonl(n);
    if(transfer(target,reinterpret_cast<char*>(&size),4,true,id,event.epoch,"PREFIX",send_id)&&transfer(target,const_cast<char*>(static_cast<const char*>(data)),static_cast<int>(n),true,id,event.epoch,"BODY",send_id))return finish(0);
    ++send_errors;return finish(-609);
}
extern "C" void Shutdown(int32_t id) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id<1||id>=int(socket_capacity))return;std::lock_guard<std::mutex> l(gate);
    if(sockets[id]!=INVALID_SOCKET){close_observed(sockets[id],id,connection_epochs[id],"PUBLIC_SHUTDOWN",true);sockets[id]=INVALID_SOCKET;}
}
