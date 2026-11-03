// Fake wire scheduling contract only; this does not test SDK backpressure.
#include "FakePublicSocketLink.hpp"
#include <socket.h>
#include <future>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
using namespace std::chrono_literals;
void Check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(int argc,char**argv){try{
    Check(argc==2,"mode");const std::string mode=argv[1];
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    public_socket_fake::UseBoundedBackpressure();
    ISocketListener cb{};cb.OnBind=[](int,PeerSocketInfo){};cb.OnNegotiate=[](int,PeerSocketInfo){return true;};
    cb.OnBytes=[](int,const void*,uint32_t){};cb.OnShutdown=[](int,ShutdownReason){};
    QosTV q{QOS_TYPE_MIN_BW,80};int out[2];
    for(unsigned i=0;i<2;++i){std::string base=i?"host.public.B.":"host.public.A.",peer=i?"host.public.A.listener":"host.public.B.listener",net=i?"model.net.A":"model.net.B",pkg="host.public.dds";
        auto name=base+"listener";SocketInfo s{name.data(),nullptr,nullptr,pkg.data(),DATA_TYPE_BYTES};auto listener=Socket(s);Check(Listen(listener,&q,1,&cb)==0,"listen");
        name=base+"outgoing";s={name.data(),peer.data(),net.data(),pkg.data(),DATA_TYPE_BYTES};out[i]=Socket(s);
    }
    auto bind=std::async(std::launch::async,[&]{return Bind(out[0],&q,1,&cb);});Check(Bind(out[1],&q,1,&cb)==0&&bind.get()==0,"bind");
    unsigned char payload[1]{42};for(unsigned i=0;i<64;++i)Check(SendBytes(out[0],payload,1)==0,"fill bounded wire");
    auto sending=std::async(std::launch::async,[&]{return SendBytes(out[0],payload,1);});
    const auto end=std::chrono::steady_clock::now()+2s;
    while(public_socket_fake::Observe().capacity_waits==0){Check(std::chrono::steady_clock::now()<end,"entered wait");std::this_thread::yield();}
    Check(sending.wait_for(0ms)==std::future_status::timeout,"capacity blocks completion");
    if(mode=="pop"){Check(public_socket_fake::PumpOne(),"pop");Check(sending.get()==0,"same original owner succeeds after capacity");}
    else if(mode=="close"){Shutdown(out[0]);Check(sending.get()==-704,"closed original owner cannot publish");}
    else if(mode=="timeout"){Check(sending.get()==-705&&public_socket_fake::Observe().capacity_timeouts==1,"bounded deadline failure");}
    else throw std::runtime_error("unknown mode");
    auto v=public_socket_fake::Observe();Check(v.record_peak==64&&v.byte_peak==64&&v.invalid==0,"no cap increase or false success");
    std::cout<<"PASS_FAKE_ONLY "<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
