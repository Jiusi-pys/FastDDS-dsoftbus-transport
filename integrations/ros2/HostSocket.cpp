// Host-only public Socket C seam. It moves opaque, length-bounded bytes between
// independent processes and never parses RTPS/ROS or manufactures DDS results.
#include <winsock2.h>
#include <windows.h>
#undef SendMessage
#include "HostSocket.hpp"
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
std::condition_variable terminal_changed;
int next_id=1,outgoing_id=-1,accepted_id=-1;
bool controlled_end=false,pause_reported=false;
const ISocketListener* listener=nullptr;
std::thread accept_thread,read_thread;
std::atomic<bool> stop{false};
std::atomic<std::uint64_t> sends{0},ingress{0},send_errors{0};
int socket_count=0;
HANDLE ready=nullptr,peer_ready=nullptr;
bool transfer(SOCKET s,char* p,int n,bool write) {
    while(n>0) {int k=write ? ::send(s,p,n,0) : ::recv(s,p,n,0);if(k<=0)return false;p+=k;n-=k;}return true;
}
void peer(PeerSocketInfo& p,bool incoming) {
    p.name=const_cast<char*>((incoming?cfg.peer_outgoing:cfg.peer_listener).c_str());
    p.pkgName=const_cast<char*>(cfg.package.c_str());
    p.networkId=const_cast<char*>(cfg.side?"host.net.A":"host.net.B");p.dataType=DATA_TYPE_BYTES;
}
void receive(SOCKET s,int id,const ISocketListener* callback) {
    while(!stop.load()) {
        std::uint32_t n=0;if(!transfer(s,reinterpret_cast<char*>(&n),4,false))break;
        n=ntohl(n);if(!n||n>65536) {std::fputs("HOST_LINK_BAD_LENGTH\n",stderr);break;}
        std::vector<unsigned char> bytes(n);if(!transfer(s,reinterpret_cast<char*>(bytes.data()),static_cast<int>(n),false))break;
        if(callback&&callback->OnBytes&&!stop.load()){++ingress;callback->OnBytes(id,bytes.data(),n);}
    }
    // Model the actual terminal notification of this owned Host connection.
    // This is not an SDK cancellation/epoch claim and never invents a data take.
    if(!stop.load()&&callback&&callback->OnShutdown)callback->OnShutdown(id,SHUTDOWN_REASON_PEER);
    {std::lock_guard<std::mutex> lock(gate);if(sockets[id]==s){closesocket(s);sockets[id]=INVALID_SOCKET;}terminal[id]=true;}
    terminal_changed.notify_all();
}
}
void ConfigureLink(LinkConfiguration c) {
    cfg=std::move(c);WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup");
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
    stop.store(true);
    {std::lock_guard<std::mutex> l(gate);for(auto& s:sockets)if(s!=INVALID_SOCKET){::shutdown(s,SD_BOTH);closesocket(s);s=INVALID_SOCKET;}}
    if(accept_thread.joinable())accept_thread.join();if(read_thread.joinable())read_thread.join();
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
}
extern "C" int32_t Socket(SocketInfo info) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(info.dataType!=DATA_TYPE_BYTES||!info.name||!info.pkgName)return -601;
    std::lock_guard<std::mutex> l(gate);
    if(!cfg.bounded_reaccept){if(socket_count==2)return -602;return ++socket_count;}
    if(next_id>=int(socket_capacity))return -602;
    const int id=next_id++;dial[id]=info.peerName!=nullptr;++socket_count;return id;
}
extern "C" int32_t Listen(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id!=1||!cb)return -603;
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.port);
    if(s==INVALID_SOCKET||::bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||::listen(s,1)!=0){if(s!=INVALID_SOCKET)closesocket(s);return -604;}
    {std::lock_guard<std::mutex> l(gate);sockets[1]=s;listener=cb;}
    accept_thread=std::thread([s,cb]{do {SOCKET incoming=::accept(s,nullptr,nullptr);if(incoming==INVALID_SOCKET)return;
        int id=3;{std::lock_guard<std::mutex> l(gate);if(cfg.bounded_reaccept){if(next_id>=int(socket_capacity)){closesocket(incoming);return;}id=next_id++;}sockets[id]=incoming;accepted_id=id;}
        PeerSocketInfo p{};peer(p,true);
        if(!cb->OnNegotiate||!cb->OnNegotiate(id,p)){Shutdown(id);if(!cfg.bounded_reaccept)return;continue;}
        if(cb->OnBind)cb->OnBind(id,p);receive(incoming,id,cb);
        }while(cfg.bounded_reaccept&&!stop.load());});
    SetEvent(ready);std::fprintf(stderr,"HOST_LISTENER_READY side=%u\n",cfg.side);std::fflush(stderr);return 0;
}
extern "C" int32_t Bind(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(!cb||id<=0||id>=int(socket_capacity)||(cfg.bounded_reaccept?!dial[id]:id!=2))return -605;
    if(read_thread.joinable())read_thread.join();
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.peer_port);
    if(s==INVALID_SOCKET||::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){if(s!=INVALID_SOCKET)closesocket(s);return -606;}
    {std::lock_guard<std::mutex> l(gate);sockets[id]=s;outgoing_id=id;}
    PeerSocketInfo p{};peer(p,false);if(cb->OnBind)cb->OnBind(id,p);
    read_thread=std::thread([s,cb,id]{receive(s,id,cb);});return 0;
}
extern "C" int32_t SendBytes(int32_t id,const void* data,uint32_t n) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(!data||!n||n>65536||id<=1||id>=int(socket_capacity)||(!cfg.bounded_reaccept&&id!=2&&id!=3))return -607;
    std::lock_guard<std::mutex> s(send_gate);SOCKET target;{std::lock_guard<std::mutex> l(gate);target=sockets[id];}
    if(target==INVALID_SOCKET)return -608;
    ++sends;
    if(!cfg.forward)return 0;std::uint32_t size=htonl(n);
    if(transfer(target,reinterpret_cast<char*>(&size),4,true)&&transfer(target,const_cast<char*>(static_cast<const char*>(data)),static_cast<int>(n),true))return 0;
    ++send_errors;return -609;
}
extern "C" void Shutdown(int32_t id) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id<1||id>=int(socket_capacity))return;std::lock_guard<std::mutex> l(gate);
    if(sockets[id]!=INVALID_SOCKET){::shutdown(sockets[id],SD_BOTH);closesocket(sockets[id]);sockets[id]=INVALID_SOCKET;}
}
