#include "../probes/PublicSocketDdsExperiment.hpp"
#include "../probes/PublicSocketDdsTrace.hpp"
#include "../probes/PublicSocketRuntimeTrace.hpp"
#include "../probes/PublicSocketDecisionStore.hpp"
#include "FakePublicSocketLink.hpp"
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include "MddsKeyedSample.hpp"
#include "MddsKeyedSamplePubSubTypes.hpp"
#include "ExperimentLargeSamplePubSubTypes.hpp"
#include <socket.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <stdexcept>
using namespace mdds::dsoftbus;
using namespace mdds::dsoftbus::dds_experiment;
using namespace std::chrono_literals;
void Check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char**argv){try{
    Check(argc==2,"case");std::string test=argv[1];
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);
    Check(IsolateProcess(),"effective process isolation");
    if(test=="sampling-transition"){
        std::array<std::uint8_t,44> p{};std::memcpy(p.data(),"RTPS",4);p[4]=2;p[20]=21;p[21]=1;p[22]=20;p[35]=0xc2;p[40]=1;
        mdds::dsoftbus::runtime_probe::SetObservationCycle(1);
        SetAfterFrameSelectionForTest([]{mdds::dsoftbus::runtime_probe::SetObservationCycle(2);});
        SocketFrame("SDK_SEND",7,p.data(),p.size());SetAfterFrameSelectionForTest({});
        Check(FrameCount()==1&&FrameCycleForTest(0)==1,"selected frame retains original observation cycle");
        SocketFrame("SDK_SEND",7,p.data(),p.size());Check(FrameCount()==1&&FrameOverflow()==0,"next observation obeys new window");
        DumpFrames();return 0;
    }
    if(test=="sampling"){
        std::array<std::uint8_t,44> p{};std::memcpy(p.data(),"RTPS",4);p[4]=2;p[20]=21;p[21]=1;p[22]=20;p[35]=0xc2;p[40]=1;
        mdds::dsoftbus::runtime_probe::SetObservationCycle(2);
        for(unsigned i=0;i<20000;++i){SocketFrame("SDK_SEND",7,p.data(),p.size());mdds::dsoftbus::runtime_probe::TraceEvent("SendBytes_return",1,1,7,0,44);}
        Check(FrameCount()==0&&FrameOverflow()==0&&mdds::dsoftbus::runtime_probe::TraceCount()==0,"target planned omission precedes storage");
        mdds::dsoftbus::runtime_probe::TraceEvent("SendBytes_return",1,1,7,-5,44);
        mdds::dsoftbus::runtime_probe::TraceEvent("OnBytesRejected",1,1,7,-5,44);
        Check(mdds::dsoftbus::runtime_probe::TraceCount()==2,"target errors never omitted");
        DecisionStore<2> store;ReceiveDecision d;d.branch=ReceiveDecisionBranch::Forwarded;d.status_known=true;d.status=0;
        for(unsigned i=0;i<20000;++i)store.Record(d,i,0,false);
        Check(store.Capture().omitted==20000&&store.Capture().total==20000&&store.Capture().count==0,"decision aggregate retained");
        d.branch=ReceiveDecisionBranch::InvalidBuffer;d.refused_receive=true;
        for(unsigned i=0;i<3;++i)store.Record(d,i,0,false);
        Check(store.Capture().count==2&&store.Capture().overflow==1&&store.Capture().omitted==20000,"required decision overflow retained");
        DumpFrames();mdds::dsoftbus::runtime_probe::DumpTrace();return 0;
    }
    if(test=="trace"){
        std::array<std::uint8_t,44> p{};std::memcpy(p.data(),"RTPS",4);p[4]=2;p[20]=21;p[21]=1;p[22]=20;p[35]=0xc2;p[40]=1;
        SocketFrame("UNIT",7,nullptr,44);SocketFrame("UNIT",7,p.data(),19);Check(FrameCount()==0,"invalid bytes never parsed");
        p[22]=21;SocketFrame("UNIT",7,p.data(),p.size());Check(FrameCount()==0,"truncated submessage not evidence");p[22]=20;
        SocketFrame("UNIT",7,p.data(),p.size());Check(FrameCount()==1,"actual bounded DATA parse");
        for(unsigned i=0;i<FrameLimit();++i)SocketFrame("UNIT",7,p.data(),p.size());Check(FrameCount()==FrameLimit()&&FrameOverflow()==1,"bounded trace overflow visible");return 0;
    }
    if(test=="default"){
        EndpointId a{},b{};a[0]=10;a[15]=1;b[0]=10;b[15]=2;Locator l,p;LocatorCodec::Encode(a,l);LocatorCodec::Encode(b,p);
        DSoftBusTransportDescriptor d(8192);d.directory.locals={{l,"host.public.A.namespace","host.public.A.listener"}};d.directory.routes={{{l,p},{"model.net.B","host.public.B.listener","host.public.A.outgoing",{"host.public.B.outgoing"},RecordDataType::Bytes},1}};d.default_metatraffic_unicast={l};d.default_user_unicast={l};
        std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> t(d.create_transport());auto actual=dynamic_cast<DSoftBusTransport*>(t.get());Check(actual&&!actual->init()&&actual->last_error().error==FacadeError::NativeBackendUnavailable,"experiment variant default factory stays unavailable");Check(public_socket_fake::Observe().sockets[0]==0,"default no public call");return 0;
    }
    if(test=="terminal-status"){host_test::HostLoopObservation v;v.sealed=true;Check(ExpectedTerminal({NativeStatus::ServiceClosing},v,true),"known close refused");for(auto status:{NativeStatus::ForeignState,NativeStatus::WrongRole,NativeStatus::InvalidConfiguration,NativeStatus::ProviderContractFault,NativeStatus::AllocationFailure,NativeStatus::OwnerUnavailable})Check(!ExpectedTerminal({status},v,true),"foreign/configuration/provider errors never terminal success");Check(ExpectedTerminal({NativeStatus::Revoked},v,true)&&ExpectedTerminal({NativeStatus::NotReady},v,true)&&!ExpectedTerminal({NativeStatus::Revoked},v,false),"close statuses require observed teardown");Check(!ExpectedTerminal({NativeStatus::NotReady,NativeSendOutcome::ReturnedSdkCode,-1},v,true),"SDK error not waived");return 0;}
    MddsKeyedSamplePubSubType().register_type_object_representation();
    ExperimentLargeSamplePubSubType().register_type_object_representation();
    Config c[2];for(unsigned i=0;i<2;++i){c[i].run="0123456789abcdef";c[i].side=i;c[i].peer_network=i?"model.net.A":"model.net.B";c[i].service_prefix="host.public";c[i].delete_first=test=="a-first"?0:1;}
    if(test=="invalid"){c[0].side=2;auto v=Run(c[0],[](const char*){return true;});Check(!v.success&&public_socket_fake::Observe().sockets[0]==0,"invalid rejected before SDK");return 0;}
    if(test=="stage-failure"){auto v=Run(c[0],[](const char*){return false;});Check(!v.success&&v.drained&&public_socket_fake::Observe().live==0,"listener-stage failure public DDS cleanup");return 0;}
    const bool regression=test=="BE_DELAYED_DROP"||test=="F256"||test=="F1M"||test=="REL_LOSS"||test=="BE_LOSS"||test=="VOL_LATE"||test=="TL_LATE"||test=="KEYED_LAST";
    const bool cycling=test=="SHARED_CYCLE_2"||test=="SHARED_CYCLE_10"||test=="SHARED_CYCLE_100";
    const bool shared=cycling||test=="SHARED_W1"||test=="SHARED_W2"||test=="SHARED_DELETE_FAILURE";
    if(shared)for(auto& v:c){v.scenario=test=="SHARED_DELETE_FAILURE"||cycling?"SHARED_W1":test;v.cycles=cycling?unsigned(std::stoul(test.substr(13))):1;if(test=="SHARED_CYCLE_100"){v.scenario="SHARED_W2";v.delete_first=0;}}
    if(cycling)trace->Sampling(1);
    if(regression){public_socket_fake::UseBoundedBackpressure();for(auto& v:c)v.scenario=test=="BE_DELAYED_DROP"?"BE_LOSS":test;}
    Check(shared||regression||test=="a-first"||test=="b-first"||test=="late-extra"||test=="qos","known case");
    std::mutex gate;std::condition_variable cv;std::map<std::string,unsigned> counts;bool deleted[2]{},aborted=false,extra_acked=false;unsigned after_wait=0;std::atomic<bool> stop{false};
    std::thread bytes([&]{while(!stop){if(!public_socket_fake::PumpOne())std::this_thread::sleep_for(1ms);}});
    Result result[2];std::thread sides[2];
    for(unsigned i=0;i<2;++i)sides[i]=std::thread([&,i]{
      SetSharedDeleteFailureForTest(test=="SHARED_DELETE_FAILURE"&&i==0);
      if(test=="late-extra")SetAfterWaitHook([&,i](eprosima::fastdds::dds::DataWriter* writer){
        {std::unique_lock<std::mutex> l(gate);++after_wait;cv.notify_all();Check(cv.wait_for(l,10s,[&]{return after_wait==2;}),"both readers finished initial ledger");}
        if(i==0){MddsKeyedSample sample;sample.key(1);sample.run(0x0123456789abcdefULL);sample.writer(1);sample.ordinal(6);for(unsigned k=0;k<32;++k)sample.payload()[k]=std::uint8_t(6+k);eprosima::fastdds::rtps::WriteParams late;Check(writer->write(&sample,late)==eprosima::fastdds::dds::RETCODE_OK,"real late DDS write");Check(writer->wait_for_acknowledgments({3,0})==eprosima::fastdds::dds::RETCODE_OK,"late DDS accepted by peer history");std::lock_guard<std::mutex> l(gate);extra_acked=true;cv.notify_all();std::cout<<"HOST_EXTRA_DDS_WRITE_ACKED\n";}
        else {std::unique_lock<std::mutex> l(gate);Check(cv.wait_for(l,5s,[&]{return extra_acked;}),"late ack observed before new take");}
      });
      result[i]=Run(c[i],[&,i](const char* p){std::unique_lock<std::mutex> l(gate);std::string stage=p;
        if(cycling&&stage.rfind("C",0)==0)trace->Cycle(unsigned(std::stoul(stage.substr(1,stage.find('_')-1))));
        if(stage=="DDS_DELETED"||stage=="C"+std::to_string(c[i].cycles)+"_DDS_DELETED"){deleted[i]=true;cv.notify_all();return true;}
        if(stage=="DELETE"||stage=="C"+std::to_string(c[i].cycles)+"_DELETE")return i==c[i].delete_first||cv.wait_for(l,20s,[&]{return deleted[1-i]||aborted;});
        ++counts[stage];cv.notify_all();const bool ready=cv.wait_for(l,20s,[&]{return counts[stage]==2||aborted;})&&!aborted;l.unlock();if(ready&&test=="BE_DELAYED_DROP"&&i==0&&stage=="MATCH")std::this_thread::sleep_for(2s);return ready;});SetAfterWaitHook({});if(!result[i].success){std::lock_guard<std::mutex> l(gate);aborted=true;cv.notify_all();}});
    for(auto& t:sides)t.join();stop=true;bytes.join();DumpFrames();if(regression)mdds::dsoftbus::runtime_probe::DumpTrace();
    if(shared){
        mdds::dsoftbus::runtime_probe::DumpTrace();
        if(test=="SHARED_DELETE_FAILURE"){Check(!result[0].success&&result[0].first_failure=="SHARED_DELETE"&&result[0].drained&&result[1].drained&&public_socket_fake::Observe().live==0,"real API deletion failure retains ownership and bounded cleanup succeeds");std::cout<<"PASS_SHARED_FAILED_DELETE_OWNERSHIP\n";return 0;}
        for(unsigned i=0;i<2;++i){std::cout<<"SHARED_CONTRACT side="<<i<<" failure="<<result[i].first_failure<<'\n';Check(result[i].success&&result[i].drained&&result[i].factories==c[i].cycles+2&&result[i].writes==15*c[i].cycles&&result[i].takes==15*c[i].cycles,"actual shared target fixture");for(unsigned group=0;group<3;++group)Check(!result[i].shared_writers[group].empty()&&result[i].shared_remote_writers[group]==result[1-i].shared_writers[group]&&result[i].shared_remote_readers[group]==result[1-i].shared_readers[group],"actual opposite incarnation identities");}
        auto sdk=public_socket_fake::Observe();Check(sdk.live==0&&sdk.invalid==0&&sdk.sockets[0]==2&&sdk.sockets[1]==2&&sdk.listens[0]==1&&sdk.listens[1]==1&&sdk.binds[0]==1&&sdk.binds[1]==1&&FrameOverflow()==0,"one native topology and bounded evidence");std::cout<<"PASS_SHARED_TARGET_ASSEMBLY_FAKE_API\n";return 0;
    }
    if(test=="late-extra"){Check(extra_acked&&!result[1].success&&result[1].first_failure=="AFTER_ACK_EXTRA","late real DDS sample must not disappear at delete");Check(result[0].drained&&result[1].drained,"negative cleanup");return 0;}
    {auto v=public_socket_fake::Observe();std::cout<<"FAKE_QUEUE invalid="<<v.invalid<<" peak_records="<<v.record_peak<<" peak_bytes="<<v.byte_peak<<" queued="<<v.queued<<"\n";if(v.invalid)trace->Dump(0);}
    for(unsigned i=0;i<2;++i){std::cout<<"CONTRACT_SIDE "<<i<<" failure="<<result[i].first_failure<<'\n';Check(result[i].success&&result[i].drained&&result[i].factories==1&&result[i].writes==(regression?(i?0:test=="KEYED_LAST"?10:test=="VOL_LATE"||test=="TL_LATE"?6:3):5)&&result[i].takes==(regression?(i?((test=="BE_LOSS"||test=="BE_DELAYED_DROP")?2:test=="VOL_LATE"?3:test=="TL_LATE"||test=="KEYED_LAST"?6:3):0):5),"actual factory/shim/Reader/deletion");Check(result[i].remote_writer==result[1-i].writer&&result[i].remote_reader==result[1-i].reader,"exact opposite identities");}
    if(test=="REL_LOSS"||test=="BE_LOSS"||test=="BE_DELAYED_DROP")Check(DropIsolationObserved(),"original dropped callback never enters Binding");
    if(test=="BE_DELAYED_DROP")Check(result[1].absence_after_drop_us>=3000000,"complete observation starts after actual delayed drop");
    auto sdk=public_socket_fake::Observe();Check(sdk.live==0&&sdk.invalid==0&&sdk.sends[0]>0&&sdk.sends[1]>0&&FrameOverflow()==0,"settled actual Fake public calls");
    if(test=="qos")for(unsigned side=0;side<2;++side)for(auto actual:{sdk.listen_qos[side],sdk.bind_qos[side]}){std::cout<<"FAKE_QOS side="<<side<<" count="<<actual.count<<" types="<<actual.type[0]<<","<<actual.type[1]<<" values="<<actual.value[0]<<","<<actual.value[1]<<" expected_wait="<<int(QOS_TYPE_MAX_WAIT_TIMEOUT)<<'\n';Check(actual.count==2&&actual.type[0]==QOS_TYPE_MIN_BW&&actual.value[0]==80&&actual.type[1]==QOS_TYPE_MAX_WAIT_TIMEOUT&&actual.value[1]==2000,"actual Fake Listen/Bind parameters must use authorized MAX_WAIT_TIMEOUT, not RTT_LEVEL");}
    std::cout<<"PASS_EXPERIMENT_FACADE_REAL_KIND_FAKE_C_BOUNDARY\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
