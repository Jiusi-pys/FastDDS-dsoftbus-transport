#include <winsock2.h>
#include <windows.h>
#undef SendMessage
#include "HostSocket.hpp"
#include <socket.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::mutex mutex;
std::condition_variable changed;
unsigned callbacks=0;
bool sentinel_seen=false;
void bytes(int32_t,const void* p,uint32_t n) {
    std::lock_guard<std::mutex> lock(mutex);++callbacks;
    if(n==8&&std::memcmp(p,"sentinel",8)==0)sentinel_seen=true;
    changed.notify_all();
}
bool negotiate(int32_t,PeerSocketInfo){return true;}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void put16(std::vector<unsigned char>& b,std::size_t at,unsigned value){b[at]=value&255;b[at+1]=(value>>8)&255;}
void put32(std::vector<unsigned char>& b,std::size_t at,unsigned value){for(unsigned i=0;i<4;++i)b[at+i]=(value>>(8*i))&255;}
std::vector<unsigned char> frame(const std::vector<unsigned char>& sample,unsigned first,unsigned writer=2) {
    constexpr unsigned fragment_size=80; // Independent unit fixture geometry, not the ROS sample.
    const auto start=std::size_t(first-1)*fragment_size;
    const auto count=std::min<std::size_t>(fragment_size,sample.size()-start);
    std::vector<unsigned char> b(56+count,0);std::memcpy(b.data(),"RTPS",4);b[4]=2;b[5]=3;b[6]=1;b[7]=15;
    for(unsigned i=8;i<20;++i)b[i]=static_cast<unsigned char>(i);
    b[20]=0x16;b[21]=1;put16(b,22,32+count);put16(b,26,28);
    b[32]=writer;b[35]=3;put32(b,40,17);put32(b,44,first);put16(b,48,1);
    put16(b,50,fragment_size);put32(b,52,static_cast<unsigned>(sample.size()));
    std::copy(sample.begin()+start,sample.begin()+start+count,b.begin()+56);return b;
}
struct LinkCleanup {~LinkCleanup(){fastdds_dsoftbus::ros2_host::StopLink();}};
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string mode=argv[1],run=argv[2],control=argv[3];
    try {
        // Reserve a loopback port before Host WSA setup; every child has a fresh run ID.
        WSADATA data;check(WSAStartup(MAKEWORD(2,2),&data)==0,"test WSAStartup");
        SOCKET probe=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};
        address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        check(::bind(probe,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"test port bind");
        int size=sizeof(address);check(getsockname(probe,reinterpret_cast<sockaddr*>(&address),&size)==0,"test port query");
        const auto port=ntohs(address.sin_port);closesocket(probe);WSACleanup();
        fastdds_dsoftbus::ros2_host::LinkConfiguration configuration;
        configuration.side=1;configuration.port=configuration.peer_port=port;configuration.run=run;
        configuration.package="host.ros.test";configuration.local_listener="test.listener";
        configuration.peer_listener="test.listener";configuration.peer_outgoing="test.outgoing";
#ifndef FASTDDS_DSOFTBUS_FRAGMENT_CONTROL_PRE_FIX
        if(mode!="default")configuration.fragment_control=control;
#endif
        if(mode!="noaction") {
            std::ofstream command(control+".DROP",std::ios::binary);
            command<<"HOST_FRAGMENT_CONTROL schema=1 run="<<run<<" side=B pid="<<(GetCurrentProcessId()+(mode=="stale-pid"?1:0))<<" action=DROP\n";
        }
        fastdds_dsoftbus::ros2_host::ConfigureLink(std::move(configuration));LinkCleanup cleanup;
        ISocketListener listener{};listener.OnBytes=&bytes;listener.OnNegotiate=&negotiate;
        check(::Listen(1,nullptr,0,&listener)==0,"actual Host Listen");
        check(::Bind(2,nullptr,0,&listener)==0,"actual Host Bind");
        std::string text=(mode=="wrong-run"?"fedcba9876543210":run)+":best_effort:B:5"+std::string(120,'x');
        std::vector<unsigned char> sample(8+text.size()+1,0);sample[1]=1;put32(sample,4,text.size()+1);
        std::copy(text.begin(),text.end(),sample.begin()+8);
        if(mode=="invalid-bounds")put32(sample,4,1);
        if(mode=="serialization")sample[1]=3;
        auto first=frame(sample,1);
        auto last=frame(sample,mode=="nonlast"?2:static_cast<unsigned>((sample.size()+79)/80),mode=="wrong-writer"?9:2);
        if(mode=="wrong-sequence")put32(last,40,18);
        if(mode=="multi-submessage") {
            while(last.size()%4)last.push_back(0);
            auto unrelated=frame(sample,2,9);last.insert(last.end(),unrelated.begin()+20,unrelated.end());
        }
        check(::SendBytes(2,first.data(),static_cast<uint32_t>(first.size()))==0,"first actual Host SendBytes");
        check(::SendBytes(2,last.data(),static_cast<uint32_t>(last.size()))==0,"last actual Host SendBytes");
        if(mode=="drop-once")check(::SendBytes(2,last.data(),static_cast<uint32_t>(last.size()))==0,"repeated last actual Host SendBytes");
        check(::SendBytes(2,"sentinel",8)==0,"ordered sentinel actual Host SendBytes");
        std::unique_lock<std::mutex> lock(mutex);
        check(changed.wait_for(lock,std::chrono::seconds(3),[]{return sentinel_seen;}),"ordered Host receive did not complete");
        // Receiving the sentinel proves every preceding TCP frame was processed;
        // no sleep or absent log is used as evidence of the deliberately dropped frame.
        const auto observed=callbacks;lock.unlock();
        std::printf("HOST_FRAGMENT_TEST mode=%s callbacks=%u expected=%u original_api_calls=%u sentinel_seen=1\n",mode.c_str(),observed,mode=="drop"?2:3,mode=="drop-once"?4:3);
        check(observed==(mode=="drop"?2u:3u),"target last-frame transfer/drop assertion");
        std::printf("HOST_FRAGMENT_TEST result=PASS mode=%s\n",mode.c_str());return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"HOST_FRAGMENT_TEST result=FAIL reason=%s\n",error.what());return 1;}
}
