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

namespace fastdds_dsoftbus::ros2_host {
namespace {
LinkConfiguration cfg;
std::mutex gate,send_gate;
std::array<SOCKET,4> sockets{INVALID_SOCKET,INVALID_SOCKET,INVALID_SOCKET,INVALID_SOCKET};
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
}
extern "C" int32_t Socket(SocketInfo info) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(info.dataType!=DATA_TYPE_BYTES||!info.name||!info.pkgName)return -601;
    std::lock_guard<std::mutex> l(gate);if(socket_count==2)return -602;return ++socket_count;
}
extern "C" int32_t Listen(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id!=1||!cb)return -603;
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.port);
    if(s==INVALID_SOCKET||::bind(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0||::listen(s,1)!=0){if(s!=INVALID_SOCKET)closesocket(s);return -604;}
    {std::lock_guard<std::mutex> l(gate);sockets[1]=s;listener=cb;}
    accept_thread=std::thread([s,cb]{SOCKET incoming=::accept(s,nullptr,nullptr);if(incoming==INVALID_SOCKET)return;
        {std::lock_guard<std::mutex> l(gate);sockets[3]=incoming;}
        PeerSocketInfo p{};peer(p,true);
        if(!cb->OnNegotiate||!cb->OnNegotiate(3,p)){Shutdown(3);return;}
        if(cb->OnBind)cb->OnBind(3,p);receive(incoming,3,cb);});
    SetEvent(ready);std::fprintf(stderr,"HOST_LISTENER_READY side=%u\n",cfg.side);std::fflush(stderr);return 0;
}
extern "C" int32_t Bind(int32_t id,const QosTV*,uint32_t,const ISocketListener* cb) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id!=2||!cb)return -605;
    SOCKET s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(cfg.peer_port);
    if(s==INVALID_SOCKET||::connect(s,reinterpret_cast<sockaddr*>(&a),sizeof(a))!=0){if(s!=INVALID_SOCKET)closesocket(s);return -606;}
    {std::lock_guard<std::mutex> l(gate);sockets[2]=s;}
    PeerSocketInfo p{};peer(p,false);if(cb->OnBind)cb->OnBind(2,p);
    read_thread=std::thread([s,cb]{receive(s,2,cb);});return 0;
}
extern "C" int32_t SendBytes(int32_t id,const void* data,uint32_t n) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(!data||!n||n>65536||(id!=2&&id!=3))return -607;
    std::lock_guard<std::mutex> s(send_gate);SOCKET target;{std::lock_guard<std::mutex> l(gate);target=sockets[id];}
    if(target==INVALID_SOCKET)return -608;
    ++sends;
    if(!cfg.forward)return 0;std::uint32_t size=htonl(n);
    if(transfer(target,reinterpret_cast<char*>(&size),4,true)&&transfer(target,const_cast<char*>(static_cast<const char*>(data)),static_cast<int>(n),true))return 0;
    ++send_errors;return -609;
}
extern "C" void Shutdown(int32_t id) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id<1||id>3)return;std::lock_guard<std::mutex> l(gate);
    if(sockets[id]!=INVALID_SOCKET){::shutdown(sockets[id],SD_BOTH);closesocket(sockets[id]);sockets[id]=INVALID_SOCKET;}
}
