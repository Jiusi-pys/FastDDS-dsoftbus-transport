// Host-only public Socket C seam. It moves opaque, length-bounded bytes between
// independent processes. Default forwarding never parses RTPS/ROS. An explicit
// negative test profile alone may select one observed last DATA_FRAG for loss;
// it never manufactures DDS results or adds a reliability protocol.
#include <winsock2.h>
#include <windows.h>
#undef SendMessage
#include "HostSocket.hpp"
#include "../../probes/PublicSocketRtpsObservation.hpp"
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
std::atomic<bool> threads_joined{false};
std::atomic<std::uint64_t> sends{0},ingress{0},send_errors{0};
std::atomic<std::uint64_t> fragment_drops{0};
// One original value-only sample identity. This gate protects diagnostic state
// only; parsing, command I/O and output happen outside it and all ownership gates.
std::mutex fragment_gate;
struct FragmentTarget {
    bool known=false,selected=false;
    std::array<std::uint8_t,16> writer{};
    std::int32_t high=0;
    std::uint32_t low=0,sample_size=0;
    std::uint16_t fragment_size=0;
} fragment_target;
struct FragmentSelection {
    bool drop=false;
    experiment::FragmentObservation original;
};
bool FragmentDropCommand() {
    std::ifstream command(cfg.fragment_control+".DROP",std::ios::binary);
    char line[192]{};if(!command.getline(line,sizeof(line)))return false;
    auto length=std::strlen(line);if(length&&line[length-1]=='\r')line[--length]='\0';
    // Exactly one bounded line; stale PID, wrong run or trailing data is ignored.
    if(command.peek()!=std::char_traits<char>::eof())return false;
    char expected[192]{};
    std::snprintf(expected,sizeof(expected),"HOST_FRAGMENT_CONTROL schema=1 run=%s side=B pid=%lu action=DROP",cfg.run.c_str(),static_cast<unsigned long>(GetCurrentProcessId()));
    return std::strcmp(line,expected)==0;
}
FragmentSelection SelectFragmentDrop(const void* data,std::uint32_t n) {
    if(cfg.fragment_control.empty())return {}; // No parser on the default Host path.
    const auto observed=experiment::ObserveRtps(data,n);
    // Only one DATA_FRAG with its allowed RTPS metadata/statistics may be lost.
    // A packet containing other data/control work is forwarded unchanged.
    if(!observed.valid||!observed.drop_shape_candidate||observed.fragments.size()!=1)return {};
    const auto& value=observed.fragments.front();
    if(!value.payload_bounds_known||value.key_flag||!value.fragment_start||!value.fragment_count||!value.fragment_size||!value.sample_size)return {};
    const std::uint64_t total=(std::uint64_t(value.sample_size)+value.fragment_size-1)/value.fragment_size;
    if(value.fragment_end>total||value.payload_offset>n||value.payload_bytes>n-value.payload_offset)return {};
    const auto begin=std::uint64_t(value.fragment_start-1)*value.fragment_size;
    if(begin>=value.sample_size)return {};
    const auto expected_bytes=std::min<std::uint64_t>(std::uint64_t(value.fragment_count)*value.fragment_size,value.sample_size-begin);
    if(value.payload_bytes<expected_bytes||value.payload_bytes>expected_bytes+3)return {};
    bool identifies=false;
    if(value.fragment_start==1&&value.sample_size>=9) {
        const auto* p=static_cast<const std::uint8_t*>(data)+value.payload_offset;
        const auto prefix=cfg.run+":best_effort:B:5";
        // This opt-in fixture supports exactly the observed std_msgs String
        // CDR_LE representation: encapsulation(4), string length(4), bytes/NUL.
        // String length includes NUL and equals sample_size-8. The next real
        // fixture byte is padding 'x', not ':' and not an ordinal digit.
        if(value.payload_bytes>=8+prefix.size()+1&&p[0]==0&&p[1]==1&&p[2]==0&&p[3]==0) {
            const auto length=std::uint32_t(p[4])|(std::uint32_t(p[5])<<8)|(std::uint32_t(p[6])<<16)|(std::uint32_t(p[7])<<24);
            identifies=length==value.sample_size-8&&length>prefix.size()+1&&
                    std::memcmp(p+8,prefix.data(),prefix.size())==0&&p[8+prefix.size()]=='x';
            if(identifies&&std::uint64_t(8)+length<=value.payload_bytes)identifies=p[8+length-1]==0;
        }
    }
    FragmentTarget original;
    {
        std::lock_guard<std::mutex> lock(fragment_gate);
        if(identifies&&!fragment_target.known)fragment_target={true,false,value.writer_guid,value.sequence_hi,value.sequence_lo,value.sample_size,value.fragment_size};
        original=fragment_target;
    }
    if(!original.known||original.selected||original.writer!=value.writer_guid||original.high!=value.sequence_hi||original.low!=value.sequence_lo||
            original.sample_size!=value.sample_size||original.fragment_size!=value.fragment_size||value.fragment_end!=total||!FragmentDropCommand())return {};
    {
        std::lock_guard<std::mutex> lock(fragment_gate);
        if(fragment_target.selected)return {};
        fragment_target.selected=true;
    }
    return {true,value};
}
int socket_count=0;
HANDLE ready=nullptr,peer_ready=nullptr;
bool transfer(SOCKET s,char* p,int n,bool write) {
    while(n>0) {int k=write ? ::send(s,p,n,0) : ::recv(s,p,n,0);if(k<=0)return false;p+=k;n-=k;}return true;
}
void peer(PeerSocketInfo& p,bool incoming) {
    thread_local std::string network_copy;
    {std::lock_guard<std::mutex> lock(gate);network_copy=node_network;}
    p.name=const_cast<char*>((incoming?cfg.peer_outgoing:cfg.peer_listener).c_str());
    p.pkgName=const_cast<char*>(cfg.package.c_str());
    p.networkId=const_cast<char*>(network_copy.c_str());p.dataType=DATA_TYPE_BYTES;
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
    cfg=std::move(c);node_network=cfg.side?"host.net.A":"host.net.B";WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)throw std::runtime_error("WSAStartup");
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
    threads_joined=true;
    if(ready)CloseHandle(ready);if(peer_ready)CloseHandle(peer_ready);WSACleanup();
}
// Control-side observation only: after pumps and Host threads have been joined.
LinkObservation ObserveLink(){return {sends.load(),ingress.load(),send_errors.load(),threads_joined.load(),fragment_drops.load()};}
bool EndOriginalSendForHostTest(std::int32_t id){
    {std::lock_guard<std::mutex> lock(gate);if(id<=0||id>=int(socket_capacity)||id!=outgoing_id||sockets[id]==INVALID_SOCKET)return false;}
    ::Shutdown(id); // existing public Host seam, not a private SDK handle
    std::unique_lock<std::mutex> lock(gate);
    return terminal_changed.wait_for(lock,std::chrono::seconds(3),[&]{return terminal[id];});
}
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
    // The actual public Host API has entered. The original borrowed arguments
    // are observed before send_gate; this is not a pre-entry provider refusal.
    const auto selected=SelectFragmentDrop(data,n);
    std::int32_t result=0;bool dropped=false;
    {
        std::lock_guard<std::mutex> s(send_gate);SOCKET target;{std::lock_guard<std::mutex> l(gate);target=sockets[id];}
        if(target==INVALID_SOCKET)result=-608;
        else {
            ++sends;
            if(cfg.forward&&selected.drop)dropped=true;
            else if(cfg.forward) {
                std::uint32_t size=htonl(n);
                if(!transfer(target,reinterpret_cast<char*>(&size),4,true)||!transfer(target,const_cast<char*>(static_cast<const char*>(data)),static_cast<int>(n),true)){++send_errors;result=-609;}
            }
        }
    }
    if(dropped) {
        ++fragment_drops;
        char writer[33]{};for(unsigned i=0;i<16;++i)std::snprintf(writer+2*i,3,"%02x",selected.original.writer_guid[i]);
        std::fprintf(stdout,"HOST_FRAGMENT_DROP schema=1 run=%s side=B pid=%lu original_socket=%d writer=%s sequence_hi=%d sequence_lo=%u first=%u count=%u fragment_size=%u sample_size=%u bytes=%u api_called=1 actual_host_write=0 native_result=0 expected_negative=1 drops=1 identity=ORIGINAL_BORROWED_ARGS\n",cfg.run.c_str(),static_cast<unsigned long>(GetCurrentProcessId()),id,writer,selected.original.sequence_hi,selected.original.sequence_lo,selected.original.fragment_start,selected.original.fragment_count,selected.original.fragment_size,selected.original.sample_size,n);
        std::fflush(stdout);
    }
    return result;
}
extern "C" void Shutdown(int32_t id) {
    using namespace fastdds_dsoftbus::ros2_host;
    if(id<1||id>=int(socket_capacity))return;std::lock_guard<std::mutex> l(gate);
    if(sockets[id]!=INVALID_SOCKET){::shutdown(sockets[id],SD_BOTH);closesocket(sockets[id]);sockets[id]=INVALID_SOCKET;}
}
