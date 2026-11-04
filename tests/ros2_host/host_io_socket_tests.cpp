#include "../../integrations/ros2/HostIoObservation.hpp"
#include "../../integrations/ros2/HostSocket.hpp"
#include <socket.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace host=fastdds_dsoftbus::ros2_host;
namespace io=host::io_observation;
namespace {
std::mutex gate;
std::condition_variable changed;
unsigned binds=0,received=0,shutdowns=0;
bool payload_correct=true;
int accepted=-1;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool negotiate(int32_t,PeerSocketInfo){return true;}
void bound(int32_t id,PeerSocketInfo){std::lock_guard<std::mutex> lock(gate);++binds;accepted=id;changed.notify_all();}
void bytes(int32_t id,const void* value,uint32_t size){std::lock_guard<std::mutex> lock(gate);++received;payload_correct=payload_correct&&id==accepted&&size==3&&std::memcmp(value,"abc",3)==0;changed.notify_all();}
void closed(int32_t id,ShutdownReason reason){std::lock_guard<std::mutex> lock(gate);++shutdowns;payload_correct=payload_correct&&id==accepted&&reason==SHUTDOWN_REASON_PEER;changed.notify_all();}
template<class Predicate> void await(Predicate predicate,const char* message){std::unique_lock<std::mutex> lock(gate);check(changed.wait_for(lock,std::chrono::seconds(2),predicate),message);}
struct SocketOwner {
    SOCKET value=INVALID_SOCKET;
    ~SocketOwner(){if(value!=INVALID_SOCKET)closesocket(value);}
    void Close(){check(closesocket(value)==0,"client close");value=INVALID_SOCKET;}
};
struct LinkOwner {bool configured=false,stopped=false;~LinkOwner(){if(configured&&!stopped)host::StopLink();}void Stop(){host::StopLink();stopped=true;}};
// A regression in StopLink's existing blocking join must fail this owned child,
// not hang CTest. No production thread is detached or cancellation introduced.
struct Deadline {
    HANDLE done=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    std::thread watchdog;
    Deadline(){check(done!=nullptr,"watchdog event");watchdog=std::thread([this]{if(WaitForSingleObject(done,8000)!=WAIT_OBJECT_0){std::fputs("HOST_IO_SOCKET_TEST result=FAIL reason=CHILD_DEADLINE\n",stderr);std::fflush(stderr);TerminateProcess(GetCurrentProcess(),124);}});}
    ~Deadline(){SetEvent(done);if(watchdog.joinable())watchdog.join();CloseHandle(done);}
};
void connect_bounded(SOCKET socket,const sockaddr_in& address){
    u_long nonblocking=1;check(ioctlsocket(socket,FIONBIO,&nonblocking)==0,"nonblocking client");
    const int result=::connect(socket,reinterpret_cast<const sockaddr*>(&address),sizeof(address));
    if(result==SOCKET_ERROR){check(WSAGetLastError()==WSAEWOULDBLOCK,"client connect");fd_set writable,failed;FD_ZERO(&writable);FD_ZERO(&failed);FD_SET(socket,&writable);FD_SET(socket,&failed);timeval timeout{2,0};
        check(select(0,nullptr,&writable,&failed,&timeout)>0&&FD_ISSET(socket,&writable)&&!FD_ISSET(socket,&failed),"client connect deadline");
        int error=0,length=sizeof(error);check(getsockopt(socket,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&error),&length)==0&&error==0,"client connect result");}
    nonblocking=0;check(ioctlsocket(socket,FIONBIO,&nonblocking)==0,"blocking client");
    DWORD timeout=2000;check(setsockopt(socket,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"client send deadline");
}
void send_all(SOCKET socket,const void* data,int size){const char* cursor=static_cast<const char*>(data);while(size){const int sent=::send(socket,cursor,size,0);check(sent>0,"client send");cursor+=sent;size-=sent;}}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    const std::string mode=argv[1],switch_value=argv[2];
    if((mode!="eof"&&mode!="reset"&&mode!="invalid-length"&&mode!="local-stop"&&mode!="send-positive")||(switch_value!="on"&&switch_value!="off"))return 2;
    try{
        Deadline deadline;
        WSADATA startup{};check(WSAStartup(MAKEWORD(2,2),&startup)==0,"fixture WSAStartup");
        struct CleanupWsa {~CleanupWsa(){WSACleanup();}} cleanup_wsa;
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        SocketOwner reservation;reservation.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(reservation.value!=INVALID_SOCKET,"port reservation socket");
        check(::bind(reservation.value,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"port reservation bind");
        int length=sizeof(address);check(getsockname(reservation.value,reinterpret_cast<sockaddr*>(&address),&length)==0,"port reservation query");reservation.Close();
        char run[17]{};std::snprintf(run,sizeof(run),"%08lx%08lx",GetCurrentProcessId(),GetTickCount());
        host::LinkConfiguration config;config.run=run;config.side=0;config.port=ntohs(address.sin_port);config.peer_port=config.port;
        config.package="host.ros.test";config.local_listener="fixture.listener";config.peer_listener="fixture.peer";config.peer_outgoing="fixture.outgoing";config.io_observation=switch_value=="on";
        ISocketListener listener{};listener.OnNegotiate=&negotiate;listener.OnBind=&bound;listener.OnBytes=&bytes;listener.OnShutdown=&closed;
        LinkOwner link;host::ConfigureLink(config);link.configured=true;
        SocketInfo info{};info.name=const_cast<char*>(config.local_listener.c_str());info.pkgName=const_cast<char*>(config.package.c_str());info.dataType=DATA_TYPE_BYTES;
        const int logical=::Socket(info);check(logical==1,"actual Host Socket result");
        const int listen_result=::Listen(logical,nullptr,0,&listener);check(listen_result==0,"actual Host Listen result");
        SocketOwner client;client.value=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(client.value!=INVALID_SOCKET,"client socket");connect_bounded(client.value,address);
        await([]{return binds==1;},"actual Host OnBind deadline");
        if(mode=="send-positive"){
            DWORD timeout=2000;check(setsockopt(client.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout))==0,"client receive deadline");
            check(::SendBytes(accepted,"abc",3)==0,"actual Host SendBytes success");
            unsigned char frame[7]{};int offset=0;while(offset<7){const int count=::recv(client.value,reinterpret_cast<char*>(frame)+offset,7-offset,0);check(count>0,"client frame receive");offset+=count;}
            check(frame[0]==0&&frame[1]==0&&frame[2]==0&&frame[3]==3&&std::memcmp(frame+4,"abc",3)==0,"exact framed opaque send");
        }else if(mode=="eof"){
            const auto size=htonl(3);send_all(client.value,&size,4);send_all(client.value,"abc",3);
            await([]{return received==1;},"actual Host OnBytes deadline");check(::shutdown(client.value,SD_SEND)==0,"client graceful send end");
            await([]{return shutdowns==1;},"EOF callback deadline");
        }else if(mode=="reset"){
            linger reset{1,0};check(setsockopt(client.value,SOL_SOCKET,SO_LINGER,reinterpret_cast<const char*>(&reset),sizeof(reset))==0,"client reset option");client.Close();
            await([]{return shutdowns==1;},"reset callback deadline");
        }else if(mode=="invalid-length"){
            const auto size=htonl(65537);send_all(client.value,&size,4);await([]{return shutdowns==1;},"invalid length callback deadline");
        }
        link.Stop(); // Also exercises local-stop with client still connected.
        const auto observation=host::ObserveLink();
        check(observation.sends==(mode=="send-positive"?1u:0u)&&observation.send_errors==0,"unchanged Host counters/joins");
        check(binds==1&&received==(mode=="eof"?1u:0u)&&shutdowns==((mode=="local-stop"||mode=="send-positive")?0u:1u)&&payload_correct,"unchanged callback contract");
        check(observation.ingress==received,"ingress accounting");
        const auto reserved=io::ledger.Reserved();
        if(switch_value=="off")check(reserved==0,"disabled observation wrote records");
        else{
            check(reserved>0&&reserved<=io::capacity,"bounded observation completeness");
            bool eof=false,error=false,invalid=false,stop=false;unsigned send_prefix=0,send_body=0,send_returns=0;std::uint64_t send_id=0;std::uint64_t receiver_epoch=0;unsigned shutdown_enter=0,shutdown_return=0;
            for(std::size_t i=0;i<reserved;++i){io::Event e;check(io::ledger.Read(i,e),"unpublished observation");
                if(e.socket==accepted){
                    if(std::strcmp(e.kind,"SEND_TRANSFER")==0){check(e.result_known&&!e.error_known&&std::strcmp(e.termination,"NONE")==0,"successful transfer classification");
                        if(!send_id)send_id=e.send;check(send_id!=0&&send_id==e.send,"send association");
                        if(std::strcmp(e.phase,"PREFIX")==0){++send_prefix;check(e.result==4,"prefix result");}
                        if(std::strcmp(e.phase,"BODY")==0){++send_body;check(e.result==3,"body result");}}
                    if(std::strcmp(e.kind,"SEND_RETURN")==0){++send_returns;check(e.result==0&&e.send==send_id,"send result association");}if(e.epoch){if(!receiver_epoch)receiver_epoch=e.epoch;check(receiver_epoch==e.epoch,"accepted connection epoch changed");}
                    if(std::strcmp(e.kind,"RECV_TRANSFER")==0){if(std::strcmp(e.termination,"EOF")==0){eof=true;check(e.result_known&&e.result==0&&!e.error_known,"EOF has stale error");}
                        if(std::strcmp(e.termination,"SOCKET_ERROR")==0){error=true;check(e.result==SOCKET_ERROR&&e.error_known&&e.error!=0,"missing actual Winsock error");}}
                    if(std::strcmp(e.kind,"RECEIVE_INVALID_LENGTH")==0)invalid=true;
                    if(std::strcmp(e.kind,"ON_SHUTDOWN_ENTER")==0)++shutdown_enter;
                    if(std::strcmp(e.kind,"ON_SHUTDOWN_RETURN")==0)++shutdown_return;}
                if(std::strcmp(e.kind,"LINK_STOP_STORE_RETURN")==0)stop=true;
            }
            check(receiver_epoch!=0&&stop&&shutdown_enter==shutdowns&&shutdown_return==shutdowns,"connection/callback/stop observation");
            if(mode=="eof")check(eof&&!error&&!invalid,"EOF diagnostic classification");
            if(mode=="reset")check(error&&!eof&&!invalid,"reset diagnostic classification");
            if(mode=="invalid-length")check(invalid&&!eof&&!error,"invalid length diagnostic classification");
            if(mode=="send-positive")check(send_prefix==1&&send_body==1&&send_returns==1,"actual send observations");
            check(io::Export(run,0),"observation export completeness");
        }
        std::printf("HOST_IO_SOCKET_TEST result=PASS mode=%s observation=%s socket_result=%d listen_result=%d binds=%u bytes=%u shutdowns=%u ingress=%llu sends=%llu send_errors=%llu joined=1\n",mode.c_str(),switch_value.c_str(),logical,listen_result,binds,received,shutdowns,observation.ingress,observation.sends,observation.send_errors);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"HOST_IO_SOCKET_TEST result=FAIL reason=%s\n",error.what());return 1;}
}
