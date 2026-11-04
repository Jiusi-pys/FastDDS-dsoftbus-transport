// Actual Fake C Socket -> owned queue -> callback -> actual Trace path.
#include "FakePublicSocketLink.hpp"
#include <socket.h>
#include <future>
#include <stdexcept>
#include <iostream>
namespace {
unsigned received=0; std::uint64_t received_hash=0;
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Setup {
    std::shared_ptr<host_dds::Trace> trace=std::make_shared<host_dds::Trace>();
    ISocketListener cb{};int out[2]{};
    Setup(bool forward=true){
        received=0;received_hash=0;public_socket_fake::Configure(trace,forward);
        cb.OnBind=[](int,PeerSocketInfo){};cb.OnNegotiate=[](int id,PeerSocketInfo){return id<900;};
        cb.OnBytes=[](int,const void* p,uint32_t n){++received;received_hash+=host_dds::Hash(static_cast<const std::uint8_t*>(p),n);};
        cb.OnShutdown=[](int,ShutdownReason){};QosTV q{QOS_TYPE_MIN_BW,80};
        for(int i=0;i<2;++i){std::string base=i?"host.public.B.":"host.public.A.",peer=i?"host.public.A.listener":"host.public.B.listener",net=i?"model.net.A":"model.net.B",pkg="host.public.dds";
            auto name=base+"listener";SocketInfo s{name.data(),nullptr,nullptr,pkg.data(),DATA_TYPE_BYTES};Check(Listen(Socket(s),&q,1,&cb)==0,"Listen");
            name=base+"outgoing";s={name.data(),peer.data(),net.data(),pkg.data(),DATA_TYPE_BYTES};out[i]=Socket(s);}
        auto a=std::async(std::launch::async,[&]{return Bind(out[0],&q,1,&cb);});Check(Bind(out[1],&q,1,&cb)==0&&a.get()==0,"Bind");
    }
    ~Setup(){Shutdown(out[0]);Shutdown(out[1]);Shutdown(100);Shutdown(200);}
};
std::array<std::uint8_t,44> Packet(){std::array<std::uint8_t,44> p{};std::memcpy(p.data(),"RTPS",4);p[20]=0x15;p[21]=1;p[22]=20;p[35]=2;p[40]=1;return p;}
}
int main(int argc,char** argv){try{
    Check(argc==2,"case");std::string mode=argv[1];Setup s(mode!="disabled");auto p=Packet();
    if(mode=="transition"){
        s.trace->Sampling(1);s.trace->Cycle(1);
        Check(SendBytes(s.out[0],p.data(),p.size())==0,"send in sampled cycle");
        s.trace->Cycle(2);Check(public_socket_fake::PumpOne(),"drain queued sampled work");
        Check(s.trace->Has("FAKE_SEND",0,"DATA")&&s.trace->Has("FAKE_INGRESS",1,"DATA"),"queued sampled send must retain ingress detail across cycle transition");
        for(const auto& e:s.trace->Events())if(e.stage=="FAKE_SEND"||e.stage=="FAKE_INGRESS"||e.stage=="FAKE_SAVED_SERVER_CALLBACK")Check(e.cycle==1,"original cycle attribution");
        Check(SendBytes(s.out[0],p.data(),p.size())==0,"omitted send");s.trace->Cycle(1);Check(public_socket_fake::PumpOne(),"omitted old work still forwards");
        Check(received==2&&s.trace->Counts().omitted==3,"omitted enqueue not resampled on dequeue");
    }else{
        bool sampled=mode=="limited"||mode=="sampled"||mode=="errors"||mode=="disabled";
        public_socket_fake::SetFrameDetail(!sampled);
        unsigned count=mode=="overflow"||mode=="sampled"?4000:8;
        auto expected=host_dds::Hash(p.data(),p.size())*count;
        for(unsigned i=0;i<count;++i){Check(SendBytes(s.out[0],p.data(),p.size())==0,"send");if(mode!="disabled")Check(public_socket_fake::PumpOne(),"forward");}
        auto v=public_socket_fake::Observe();Check(v.sends[0]==count&&v.forwards[0]==(mode=="disabled"?0:count)&&received==v.forwards[0]&&v.queued==0&&v.bytes==0,"business counters and obligations");
        Check(received_hash==(mode=="disabled"?0:expected),"actual payload");
        std::cout<<"FAKE_ACTIVITY sends="<<v.sends[0]<<" forwards="<<v.forwards[0]<<" received="<<received<<" payload_hash="<<received_hash<<" queued="<<v.queued<<" bytes="<<v.bytes<<" peak_records="<<v.record_peak<<" peak_bytes="<<v.byte_peak<<'\n';
        if(mode=="errors"){Check(public_socket_fake::RejectExtraCandidate(1),"reject remains live");Check(SendBytes(-1,p.data(),p.size())!=0,"invalid remains rejected");Check(s.trace->Has("TRACE_ERROR",0,"")&&s.trace->Has("FAKE_REJECTED_CANDIDATE_DELETE",1,""),"errors never sampled");}
        else if(mode=="overflow")Check(!s.trace->Good(),"full-detail real overflow still fails");
        else Check(s.trace->Good(),"bounded detail");
        if(sampled)Check(!s.trace->Has("FAKE_SEND",0,"DATA")&&!s.trace->Has("FAKE_INGRESS",1,"DATA"),"Fake detail omitted before storage");
        auto counts=s.trace->Counts();
        if(sampled)Check(counts.omitted==count*(mode=="disabled"?1:3)&&counts.stored==0&&counts.lost==0&&counts.required_lost==0,"planned omissions separately counted");
        if(mode=="overflow")Check(counts.lost>0&&counts.omitted==0,"overflow is not planned omission");
    }
    s.trace->Dump(1);std::cout<<"PASS_FAKE_SAMPLING "<<mode<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FIRST_FAILURE "<<e.what()<<'\n';return 1;}}
