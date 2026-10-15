#include "../../integrations/ros2/HostIoObservation.hpp"
#include <cassert>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <io.h>
#include <fcntl.h>
namespace io=fastdds_dsoftbus::ros2_host::io_observation;
int main(int argc,char** argv){
    const std::string mode=argc==1?"normal":argv[1];
    if(argc>2||(mode!="normal"&&mode!="overflow"&&mode!="output-failure"))return 2;
    WSADATA wsa{};assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    if(mode!="normal"){
        io::enabled=true;
        if(mode=="overflow"){
            FILE* redirected=nullptr;assert(freopen_s(&redirected,"NUL","w",stdout)==0);
            for(std::size_t i=0;i<=io::capacity;++i)io::Record({"TEST_OVERFLOW"});
            assert(!io::Export("0123456789abcdef",0));
        }else{
            io::Record({"TEST_OUTPUT_FAILURE"});
            // A valid CRT writer with no pipe reader fails in WriteFile. Closing
            // stdout's CRT descriptor instead invokes the invalid-parameter
            // handler before Export can report the actual output error.
            int pipes[2]{};assert(_pipe(pipes,4096,_O_BINARY)==0);
            const int output=_fileno(stdout),saved=_dup(output);assert(saved>=0);
            assert(setvbuf(stdout,nullptr,_IONBF,0)==0);
            assert(_dup2(pipes[1],output)==0);
            assert(_close(pipes[1])==0&&_close(pipes[0])==0);
            // Prove the valid OS writer is actually unwritable, independently
            // of Export. CRT errno mappings are not this observer's contract.
            const auto handle=reinterpret_cast<HANDLE>(_get_osfhandle(output));
            assert(handle!=INVALID_HANDLE_VALUE);
            const char marker='x';DWORD written=0;
            assert(!WriteFile(handle,&marker,1,&written,nullptr)&&written==0);
            assert(!io::Export("0123456789abcdef",0));
            assert(std::ferror(stdout)!=0);
            assert(_dup2(saved,output)==0&&_close(saved)==0);
            std::clearerr(stdout);
        }
        WSACleanup();return 0;
    }
    std::map<std::string,std::string> config;
    assert(!io::ConfigurationEnabled(config));
    config["host_io_observation"]="0";assert(!io::ConfigurationEnabled(config));
    config["host_io_observation"]="1";assert(io::ConfigurationEnabled(config));
    for(const auto* value:{"","2","-1","true","01","1x"," 1"}){
        config["host_io_observation"]=value;bool rejected=false;
        try{(void)io::ConfigurationEnabled(config);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    }
    char bytes[8]{};
    // Deterministic syscall scripts use the exact transfer loop invoked by the
    // Host seam. Enabled/disabled observation must preserve call arguments/results.
    for(bool enabled:{false,true}){
        io::enabled=enabled;
        for(bool write:{false,true}){
            for(int terminal:{0,SOCKET_ERROR,6}){
                const auto start=io::ledger.Reserved();int calls=0,error_calls=0;
                auto event=io::Connection(write?"SEND_TRANSFER":"RECV_TRANSFER",2,11,42);event.phase="BODY";event.send=19;
                const bool result=io::Transfer(bytes,8,write,event,[&](char* data,int size){
                    ++calls;if(calls==1){assert(data==bytes&&size==8);return 2;}
                    assert(calls==2&&data==bytes+2&&size==6);WSASetLastError(10054);return terminal;
                },[&]{++error_calls;assert(calls==2);return WSAGetLastError();});
                assert(result==(terminal==6)&&calls==2&&error_calls==(terminal==SOCKET_ERROR?1:0));
                assert(WSAGetLastError()==10054);
                if(enabled){io::Event captured;assert(io::ledger.Read(start,captured));
                    assert(captured.send==19&&captured.epoch==11&&captured.handle==42&&captured.calls==2&&captured.partial==1);
                    assert(captured.error_known==(terminal==SOCKET_ERROR));
                    assert(captured.error==(terminal==SOCKET_ERROR?10054:0));
                    assert(captured.begin<captured.last_call_begin&&captured.last_call_begin<captured.sequence);
                    if(terminal==0)assert(std::strcmp(captured.termination,write?"ZERO_PROGRESS":"EOF")==0);
                    if(terminal==6)assert(captured.result==8&&captured.requested==8);
                    else assert(captured.result==terminal&&captured.requested==6);
                }else assert(io::ledger.Reserved()==start);
            }
        }
    }
    // Prefix failure does not call the body; reproduce production short circuit.
    int calls=0;auto prefix=io::Connection("SEND_TRANSFER",2,12,42);prefix.phase="PREFIX";
    const auto start=io::ledger.Reserved();
    const bool transferred=io::Transfer(bytes,4,true,prefix,[&](char*,int){++calls;return SOCKET_ERROR;},[]{return 10038;})&&
        io::Transfer(bytes,8,true,prefix,[&](char*,int){++calls;return 8;},[]{return 0;});
    assert(!transferred&&calls==1);io::Event failure;assert(io::ledger.Read(start,failure)&&failure.error==10038&&failure.partial==0);
    // Concurrent append has no overwrite; all fixed slots are published once.
    io::Ledger<1024> concurrent;std::vector<std::thread> workers;
    for(int thread=0;thread<8;++thread)workers.emplace_back([&,thread]{for(int i=0;i<128;++i){io::Event e;e.sequence=thread*128+i+1;concurrent.Append(e);}});
    for(auto& thread:workers)thread.join();assert(concurrent.Reserved()==1024);
    std::set<std::uint64_t> ids;for(std::size_t i=0;i<1024;++i){io::Event e;assert(concurrent.Read(i,e));ids.insert(e.sequence);}assert(ids.size()==1024);
    io::Event extra;extra.sequence=99999;concurrent.Append(extra);assert(concurrent.Reserved()==1025);assert(!concurrent.Read(1024,extra));
    io::Event first;assert(concurrent.Read(0,first)&&first.sequence!=99999);
    // Same raw handle in distinct observed epochs never aliases the identity.
    auto one=io::Connection("CONNECTION_INSTALL",2,1,42),two=io::Connection("CONNECTION_INSTALL",2,2,42);
    assert(one.handle==two.handle&&one.epoch!=two.epoch);
    WSACleanup();
}
