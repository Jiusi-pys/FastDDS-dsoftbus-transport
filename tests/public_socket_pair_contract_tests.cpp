#include "HostProviderTestSupport.hpp"
#include "FakePublicSocketLink.hpp"
#include <mdds/dsoftbus/PublicSocketBackend.hpp>

// A6: Host BYTES ownership contract. This is not an extra DDS/RAW scenario.
namespace {
std::string Name(int side,const char* role){return std::string("host.public.")+(side?"B.":"A.")+role;}
std::string Network(int side){return side?"model.net.B":"model.net.A";}
struct Pair {
    Receiver receivers[2];std::shared_ptr<PublicSocketBackend> backend[2];
    std::shared_ptr<HostProviderLoop> loop[2];std::unique_ptr<DSoftBusTransport> facade[2];rtps::SendResourceList senders[2];
    Pair(){
        public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        for(int side=0;side<2;++side){
            DSoftBusTransportDescriptor d(64);d.directory.locals={{L(2+side),Name(side,"namespace"),Name(side,"listener")}};
            d.directory.routes={{{L(2+side),L(3-side)},{Network(1-side),Name(1-side,"listener"),Name(side,"outgoing"),{Name(1-side,"outgoing")},RecordDataType::Bytes},1}};
            d.default_metatraffic_unicast={L(2+side)};d.default_user_unicast={L(2+side)};
            PublicSocketConfiguration c;c.identity={Name(side,"outgoing"),{std::uint64_t(1200+side*2)},{1300}};c.listener_identity={Name(side,"listener"),{std::uint64_t(1201+side*2)},{1301}};
            c.package_name="host.public.dds";c.peer_name=Name(1-side,"listener");c.peer_network_id=Network(1-side);c.bind_qos={{0,65536}};c.listen_qos=c.bind_qos;c.max_message_size=64;
            backend[side]=std::make_shared<PublicSocketBackend>(c);loop[side]=std::make_shared<HostProviderLoop>(backend[side]);facade[side]=HostFacadeTestAccess::Create(d,loop[side]);
        }
        std::thread init[2];std::exception_ptr errors[2];Cleanup join{[&]{for(auto& t:init)if(t.joinable())t.join();}};
        for(int i=0;i<2;++i)init[i]=std::thread([&,i]{try{Check(facade[i]->init()&&facade[i]->OpenInputChannel(L(2+i),&receivers[i],64),"real facade dual input");}catch(...){errors[i]=std::current_exception();}});
        for(auto& t:init)t.join();for(auto& e:errors)if(e)std::rethrow_exception(e);
        for(int i=0;i<2;++i)Check(facade[i]->OpenOutputChannel(senders[i],L(3-i)),"real sender resource");
    }
    ~Pair(){for(auto& f:facade)if(f)f->shutdown();}
    bool Submit(int side){std::uint8_t b[]={3,7};std::vector<rtps::NetworkBuffer> bufs{{b,2}};std::vector<Locator> peers{L(3-side)};rtps::Locators it(peers.begin()),end(peers.end());return senders[side].front()->send(bufs,2,&it,&end,std::chrono::steady_clock::now()+2s,0);}
    void Deliver(int side){const auto before=receivers[1-side].calls.load();Check(Submit(side),"live outgoing admission");Check(loop[side]->PumpSend().actual_return_code==0,"actual outgoing SendBytes");Check(public_socket_fake::PumpOne(),"Fake byte forward");Check(loop[1-side]->PumpReceive()==BackendStatus::None&&receivers[1-side].calls==before+1,"opposite accepted still receives after other leg stop");}
};
void HalfClose(){Pair f;f.Deliver(0);f.Deliver(1);const auto a=f.loop[0]->Observe(),b=f.loop[1]->Observe();
    Check(f.loop[0]->CloseAccepted()==BackendStatus::None,"A accepted local close");
    f.Deliver(0); // A outgoing -> B accepted remains a valid, independent lane.
    Check(!f.Submit(1),"stopped B outgoing must not select accepted as fallback egress");
    const auto aa=f.loop[0]->Observe(),bb=f.loop[1]->Observe();
    Check(aa.adapter.binding.owner_incarnation==a.adapter.binding.owner_incarnation&&bb.adapter.accepted.owner_incarnation==b.adapter.accepted.owner_incarnation,"surviving identities unchanged");
    Check(bb.receive.active_applications==1&&bb.receive.records==0&&bb.tx.records==0,"surviving RX valid and no wrong-role queued TX");
}
}
int main(){try{HalfClose();std::cout<<"PASS A6_INDEPENDENT_DIRECTED_LEGS Fake_public_BYTESTransport\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
