#include "V1NativeNameTestSupport.hpp"
#include "FakePublicSocketLink.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastdds/dds/log/Log.hpp>
#include <Windows.h>
#include <TlHelp32.h>
#include <array>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

using namespace fastdds_dsoftbus::dsoftbus;
namespace {
namespace dds=eprosima::fastdds::dds;
namespace rtps=eprosima::fastrtps::rtps;
using namespace std::chrono_literals;
void T2Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void T2LoadedDll(){
    const auto expected=_wgetenv(L"T2_EXPECTED_FASTDDS_DLL");
    T2Check(expected&&*expected,"explicit expected stock DLL path");
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    T2Check(snapshot!=INVALID_HANDLE_VALUE,"Toolhelp module snapshot");
    MODULEENTRY32W row{};row.dwSize=sizeof(row);bool found=false;
    for(BOOL ok=Module32FirstW(snapshot,&row);ok;ok=Module32NextW(snapshot,&row)){
        if(_wcsicmp(row.szModule,L"fastrtps-2.14.dll"))continue;
        found=_wcsicmp(row.szExePath,expected)==0;
        std::wcout<<L"ACTUAL_FASTDDS_DLL="<<row.szExePath<<L'\n';
    }
    CloseHandle(snapshot);T2Check(found,"loaded DLL is not the pinned new stock Host artifact");
}
v1::DeviceIdentity T2Identity(unsigned side,unsigned epoch){return {"stock-views-"+std::to_string(epoch),151,side?"B":"A"};}
std::string T2Listener(unsigned side,unsigned epoch){
    std::string result;
    T2Check(v1::AddressCodec::EncodeScopedServiceName({T2Identity(side,epoch).name_space,151,17+side,v1::Profile::BytesNative},
        {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},result),"encoded service");
    return result;
}
phase0::PublicSocketConfiguration T2Configuration(unsigned side,unsigned epoch){
    phase0::PublicSocketConfiguration c;
    const auto outgoing=[&](unsigned s){return std::string("host.public.")+(s?"B.":"A.")+"outgoing"+(epoch?std::to_string(epoch):"");};
    c.identity={outgoing(side),{910+side*4+epoch*100},{1010+side*4+epoch*100}};
    c.listener_identity={T2Listener(side,epoch),{911+side*4+epoch*100},{1011+side*4+epoch*100}};
    c.package_name="host.public.dds";c.peer_name=T2Listener(1-side,epoch);
    c.peer_network_id=side?"model.net.A":"model.net.B";c.expected_peer_initiator_name=outgoing(1-side);
    c.bind_qos={{0,80},{1,2000}};c.listen_qos=c.bind_qos;c.max_message_size=8192;return c;
}
DSoftBusTransportDescriptor T2Descriptor(unsigned side,unsigned epoch){
    const auto c=T2Configuration(side,epoch);DSoftBusTransportDescriptor d(8192);
    V1Deployment input{T2Identity(side,epoch),17+side,v1::Profile::BytesNative,"stock.views",
        {{T2Identity(1-side,epoch),18-side,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
    T2Check(d.ConfigureV1(ScopedHostDeployment(input)).code==DirectoryError::None,"descriptor configuration");return d;
}
struct T2Sample {std::uint32_t cycle=0,source=0,ordinal=0;std::array<std::uint8_t,32> payload{};};
// A test application type using the unmodified stock public TopicDataType API.
// XCDR1 little-endian encapsulation, three uint32 fields and an octet array.
class T2SampleType final:public dds::TopicDataType {
public:
    T2SampleType(){setName("TransportViewSample");m_typeSize=48;m_isGetKeyDefined=false;}
    bool serialize(void* p,rtps::SerializedPayload_t* out) override {
        if(!p||!out||out->max_size<48)return false;const auto& s=*static_cast<T2Sample*>(p);
        out->data[0]=0;out->data[1]=1;out->data[2]=out->data[3]=0;
        const std::uint32_t values[]={s.cycle,s.source,s.ordinal};
        for(unsigned f=0;f<3;++f)for(unsigned i=0;i<4;++i)out->data[4+f*4+i]=std::uint8_t(values[f]>>(i*8));
        std::copy(s.payload.begin(),s.payload.end(),out->data+16);out->length=48;out->encapsulation=1;return true;
    }
    bool deserialize(rtps::SerializedPayload_t* in,void* p) override {
        if(!p||!in||in->length!=48||in->data[0]!=0||in->data[1]!=1)return false;
        auto& s=*static_cast<T2Sample*>(p);std::uint32_t* fields[]={&s.cycle,&s.source,&s.ordinal};
        for(unsigned f=0;f<3;++f){*fields[f]=0;for(unsigned i=0;i<4;++i)*fields[f]|=std::uint32_t(in->data[4+f*4+i])<<(i*8);}
        std::copy(in->data+16,in->data+48,s.payload.begin());return true;
    }
    std::function<std::uint32_t()> getSerializedSizeProvider(void*) override{return []{return 48u;};}
    void* createData() override{return new T2Sample;}
    void deleteData(void* p) override{delete static_cast<T2Sample*>(p);}
    bool getKey(void*,rtps::InstanceHandle_t*,bool) override{return false;}
};
struct T2Reader final:dds::DataReaderListener {
    struct Receipt {T2Sample sample;rtps::GUID_t writer;};
    std::mutex gate;std::condition_variable changed;int matches=0;
    std::vector<dds::InstanceHandle_t> matched_writers;
    std::vector<Receipt> receipts;bool hold=false,entered=false,release=false;
    void on_subscription_matched(dds::DataReader*,const dds::SubscriptionMatchedStatus& s) override {
        std::lock_guard<std::mutex> lock(gate);matches=s.current_count;
        auto found=std::find(matched_writers.begin(),matched_writers.end(),s.last_publication_handle);
        if(s.current_count_change>0&&found==matched_writers.end())matched_writers.push_back(s.last_publication_handle);
        else if(s.current_count_change<0&&found!=matched_writers.end())matched_writers.erase(found);
        changed.notify_all();
    }
    void on_data_available(dds::DataReader* r) override {
        std::unique_lock<std::mutex> lock(gate);
        T2Sample sample;dds::SampleInfo info;
        while(r->take_next_sample(&sample,&info)==0)if(info.valid_data)receipts.push_back({sample,info.sample_identity.writer_guid()});
        if(hold){entered=true;changed.notify_all();changed.wait(lock,[&]{return release;});}
        changed.notify_all();
    }
    void WaitMatched(){std::unique_lock<std::mutex> lock(gate);T2Check(changed.wait_for(lock,2s,[&]{return matches>0;}),"reader did not match through selected transport");if(matches!=1)std::cerr<<"READER_MATCH_COUNT="<<matches<<'\n';T2Check(matches==1,"unexpected writer/domain match");}
    void WaitNone(){std::unique_lock<std::mutex> lock(gate);T2Check(!changed.wait_for(lock,150ms,[&]{return matches||!receipts.empty();}),"different domains matched or delivered");}
    void WaitData(const T2Sample& expected,const rtps::GUID_t& writer){
        std::unique_lock<std::mutex> lock(gate);T2Check(changed.wait_for(lock,2s,[&]{return !receipts.empty();}),"real DDS reader has no sample");
        T2Check(receipts.size()==1,"duplicate/unexpected sample at independent receiver");const auto& got=receipts.front();
        T2Check(got.sample.cycle==expected.cycle&&got.sample.source==expected.source&&got.sample.ordinal==expected.ordinal&&
            got.sample.payload==expected.payload&&got.writer==writer,"current writer/sample/payload ledger mismatch");receipts.clear();
    }
};
struct T2Writer final:dds::DataWriterListener {
    std::mutex gate;std::condition_variable changed;int matches=0;
    void on_publication_matched(dds::DataWriter*,const dds::PublicationMatchedStatus& s) override {
        std::lock_guard<std::mutex> lock(gate);matches=s.current_count;changed.notify_all();
    }
    void WaitMatched(){std::unique_lock<std::mutex> lock(gate);T2Check(changed.wait_for(lock,2s,[&]{return matches>0;}),"writer did not match through selected transport");if(matches!=1)std::cerr<<"WRITER_MATCH_COUNT="<<matches<<'\n';T2Check(matches==1,"unexpected reader/domain match");}
};
struct T2Worker {
    experimental::Runtime& runtime;std::promise<WorkWaitResult> terminal;std::future<WorkWaitResult> done=terminal.get_future();std::thread thread;
    explicit T2Worker(experimental::Runtime& r):runtime(r),thread([this]{try{
        for(;;){
            auto generation=runtime.WorkGeneration();
            for(;;){
                const auto tx=runtime.PumpSend();bool progress=tx.status==NativeStatus::None;
                if(progress)T2Check(tx.actual_return_code&&*tx.actual_return_code==0,"Fake SDK returned send error");
                while(public_socket_fake::PumpOne())progress=true;
                if(runtime.PumpReceive()==phase0::BackendStatus::None)progress=true;
                if(!progress)break;
            }
            const auto result=runtime.WaitForWork(generation);
            if(result!=WorkWaitResult::Work&&result!=WorkWaitResult::Deadline){terminal.set_value(result);return;}
        }
    }catch(...){terminal.set_exception(std::current_exception());}}){}
    ~T2Worker(){if(thread.joinable()){runtime.RequestPumpStop();thread.join();}}
    void Join(){T2Check(done.wait_for(2s)==std::future_status::ready,"owner close did not unblock worker");T2Check(done.get()==WorkWaitResult::Closed,"owner worker did not close naturally");thread.join();}
};
struct T2Endpoint {
    dds::DomainParticipant* participant=nullptr;dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::unique_ptr<T2Reader> received;std::unique_ptr<T2Writer> sent;
};
struct T2LifecycleFixture {
    const unsigned epoch;const bool cross_transport_only;std::array<std::unique_ptr<experimental::Runtime>,2> runtimes;
    std::array<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>,2> descriptors;
    std::array<std::unique_ptr<T2Worker>,2> workers;std::array<std::array<T2Endpoint,2>,2> endpoints;
    dds::TypeSupport type{new T2SampleType};ServiceRecordTable& records=ServiceRecordTable::Process();
    std::mutex ready_gate;std::condition_variable ready;unsigned arrived=0;bool finished=false;
    explicit T2LifecycleFixture(unsigned e=0,bool initialise_fake=true,bool cross_only=true,bool normal_default=false,
            std::shared_ptr<ProcessReceiveObserver> observer={}):epoch(e),cross_transport_only(cross_only){
        if(initialise_fake)public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
        public_socket_fake::SetListenerNames({T2Listener(0,epoch),T2Listener(1,epoch)});
        public_socket_fake::SetInitiatorNames({T2Configuration(0,epoch).identity.service_name,T2Configuration(1,epoch).identity.service_name});
        public_socket_fake::SetFrameDetail(false);
        auto* f=dds::DomainParticipantFactory::get_instance();
        const char xml[]="<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
        T2Check(f->load_XML_profiles_string(xml,sizeof(xml)-1)==0,"public XML intraprocess OFF configuration");
        for(unsigned side=0;side<2;++side){
            if(normal_default){
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
                if(observer)runtimes[side]=std::make_unique<experimental::Runtime>(T2Configuration(side,epoch),observer);
                else
#else
                T2Check(!observer,"observer overload requires its consistent build variant");
#endif
                runtimes[side]=std::make_unique<experimental::Runtime>(T2Configuration(side,epoch));
            }else runtimes[side]=std::make_unique<experimental::Runtime>(T2Configuration(side,epoch),experimental::RuntimeLifetime::PersistentNativeAnchor);
            T2Check(runtimes[side]->SetPreConnectGate([this]{std::unique_lock<std::mutex> lock(ready_gate);++arrived;ready.notify_all();return ready.wait_for(lock,2s,[&]{return arrived==2;});}),"native initial listener barrier");
            descriptors[side]=runtimes[side]->MakeDescriptor(T2Descriptor(side,epoch));T2Check(bool(descriptors[side]),"persistent descriptor");
        }
        for(unsigned side=0;side<2;++side)workers[side]=std::make_unique<T2Worker>(*runtimes[side]);
    }
    ~T2LifecycleFixture(){
        if(finished)return;
        try{for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)Remove(side,group);
            for(auto& r:runtimes)if(r)r->Close();}catch(...){std::cerr<<"cleanup after primary failure\n";}
    }
    bool Create(unsigned side,unsigned group,unsigned domain=151,bool common_topic=false,bool require_success=true){
        auto& a=endpoints[side][group];T2Check(!a.participant,"overwriting Participant");
        a.received=std::make_unique<T2Reader>();a.sent=std::make_unique<T2Writer>();
        const auto d=T2Descriptor(side,epoch);dds::DomainParticipantQos q;
        q.transport().use_builtin_transports=false;q.transport().user_transports={descriptors[side]};
        // This configuration isolates the cross-Participant Transport path.
        // It is not evidence for default/self-local DDS data delivery.
        if(cross_transport_only)q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");
        auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();
        w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(d.directory.locals.front().local_locator);
        w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(d.directory.locals.front().local_locator);w.default_multicast_locator_list.clear();
        w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(d.directory.routes.front().key.remote_locator);
        w.builtin.discovery_config.initial_announcements.count=1;
        a.participant=dds::DomainParticipantFactory::get_instance()->create_participant(domain,q);
        if(!require_success)std::cout<<"ACTUAL_FACTORY_CAPACITY side="<<side<<" group="<<group<<" returned="<<static_cast<const void*>(a.participant)<<'\n';
        if(!a.participant){T2Check(!require_success,"unmodified public Factory create");return false;}
        T2Check(type.register_type(a.participant)==0,"public type registration");
        auto* topic=a.participant->create_topic(common_topic?"ViewDomainTopic":group?"ViewQ":"ViewP",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* pub=a.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto* sub=a.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        T2Check(topic&&pub&&sub,"public topic/pub/sub");
        auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;
        wq.reliability().kind=rq.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;
        wq.data_sharing().off();rq.data_sharing().off();
        a.writer=pub->create_datawriter(topic,wq,a.sent.get());a.reader=sub->create_datareader(topic,rq,a.received.get());
        T2Check(a.writer&&a.reader,"public Writer/Reader creation");
        return true;
    }
    void Pair(unsigned group,bool common_topic=false){
        std::exception_ptr errors[2];std::thread threads[2];
        for(unsigned side=0;side<2;++side)threads[side]=std::thread([&,side]{try{Create(side,group,151,common_topic);}catch(...){errors[side]=std::current_exception();}});
        for(auto& t:threads)t.join();for(const auto& error:errors)if(error)std::rethrow_exception(error);
    }
    void Traffic(unsigned group,unsigned cycle,unsigned peer_group=2){
        if(peer_group==2)peer_group=group;
        const std::array<unsigned,2> groups{{group,peer_group}};
        for(unsigned side=0;side<2;++side){endpoints[side][groups[side]].received->WaitMatched();endpoints[side][groups[side]].sent->WaitMatched();}
        const auto before=public_socket_fake::Observe();
        for(unsigned side=0;side<2;++side){
            auto& a=endpoints[side][groups[side]];auto& b=endpoints[1-side][groups[1-side]];T2Sample sample;
            sample.cycle=cycle;sample.source=side+2*groups[side];sample.ordinal=1;
            for(unsigned i=0;i<sample.payload.size();++i)sample.payload[i]=std::uint8_t(cycle*7+side*13+groups[side]*19+i);
            T2Check(a.writer->write(&sample,dds::HANDLE_NIL)==0,"stock DDS write");
            b.received->WaitData(sample,a.writer->guid());
            T2Check(a.writer->wait_for_acknowledgments({1,0})==0,"stock reliable acknowledgment");
        }
        const auto after=public_socket_fake::Observe();
        T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"DDS data bypassed actual Fake link");
        std::cout<<"DDS_LEDGER cycle="<<cycle<<" group="<<group<<" writes=2 takes=2 exact_current_writer=1\n";
    }
    void Remove(unsigned side,unsigned group){
        auto& a=endpoints[side][group];if(!a.participant)return;
        T2Check(a.participant->delete_contained_entities()==0,"stock delete_contained_entities");
        T2Check(dds::DomainParticipantFactory::get_instance()->delete_participant(a.participant)==0,"stock public Factory delete");
        a.participant=nullptr;a.writer=nullptr;a.reader=nullptr;a.received.reset();a.sent.reset();
    }
    void TraceMatches(){
        const char* digits="0123456789abcdef";
        for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){const auto& a=endpoints[side][group];if(!a.participant)continue;
            std::vector<dds::InstanceHandle_t> matched;{std::lock_guard<std::mutex> lock(a.received->gate);matched=a.received->matched_writers;}
            const auto own=a.writer->get_instance_handle();
            std::cout<<"MATCH_DIAGNOSTIC side="<<side<<" group="<<group<<" domain="<<a.participant->get_domain_id()<<" own_writer=";
            for(unsigned i=0;i<16;++i){const auto b=own.value[i];std::cout<<digits[b>>4]<<digits[b&15];}
            for(const auto& h:matched){std::cout<<" matched_writer=";for(unsigned i=0;i<16;++i){const auto b=h.value[i];std::cout<<digits[b>>4]<<digits[b&15];}}
            std::cout<<" count="<<matched.size()<<" SELF_LOCAL_DATA=NOT_TESTED\n";
        }
    }
    void Observe(unsigned cycle,unsigned views){
        const auto slots=records.SnapshotUsage();const auto sdk=public_socket_fake::Observe();
        T2Check(slots.records_created==4,"Participant lifetime consumed a new exposed record");
        T2Check(sdk.sockets[0]==2&&sdk.sockets[1]==2&&sdk.binds[0]==1&&sdk.binds[1]==1,"Participant lifetime rebuilt native sockets");
        T2Check(sdk.live==6&&sdk.shutdowns[0]==0&&sdk.shutdowns[1]==0,"live anchor unexpectedly shut down");
        for(unsigned side=0;side<2;++side){const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(*runtimes[side]);
            T2Check(s.live_views==views&&s.receive.active_applications==views,"view membership/receiver count");
            if(!views)T2Check(s.receive.records==0&&s.receive.bytes==0&&s.receive.retained_applications==0,"zero-view receive residue");
            std::cout<<"ANCHOR cycle="<<cycle<<" side="<<side<<" views="<<s.live_views<<" apps="<<s.receive.active_applications
                <<" records="<<s.receive.records<<" bytes="<<s.receive.bytes<<" native_owners="<<s.native.owners
                <<" retained_ids="<<s.native.owned_ids<<" SDK_live_global="<<sdk.live<<" records_created="<<slots.records_created<<'\n';
        }
    }
    void Finish(){
        for(unsigned group=0;group<2;++group)for(unsigned side=0;side<2;++side)Remove(side,group);
        for(auto& r:runtimes)T2Check(r->Close(),"explicit zero-view Runtime.Close");
        for(auto& worker:workers)worker->Join();
        const auto sdk=public_socket_fake::Observe();
        T2Check(sdk.live==0&&sdk.queued==0&&sdk.bytes==0&&sdk.invalid==0,"final Fake live/queue/invalid residue");
        for(auto& r:runtimes){const auto s=phase0::RuntimeProbeAccess::ObserveRuntime(*r);
            T2Check(s.closed&&s.live_views==0&&s.active_pumps==0&&s.close.adapter_drained&&s.receive.records==0&&
                s.receive.bytes==0&&s.receive.active_applications==0&&s.receive.retained_applications==0&&
                s.native.callbacks==0&&s.native.receives==0&&s.native.setup.reserved==0&&s.native.setup.entered==0&&
                s.native.send.reserved==0&&s.native.send.entered==0&&s.native.close.reserved==0&&s.native.close.entered==0,
                "final adapter/queue/lease activity residue");
            std::cout<<"CLOSED retained_ids="<<s.native.owned_ids<<" retained_not_quiescent="
                <<s.native.phases[static_cast<unsigned>(NativeOwnerPhase::RetainedNotQuiescent)]
                <<" SDK_quiescence=UNKNOWN Shutdown_return_type=void\n";
            auto descriptor=r->MakeDescriptor(T2Descriptor(&r==&runtimes[0]?0:1,epoch));
            T2Check(!descriptor||!descriptor->create_transport(),"closed owner reopened");
        }
        finished=true;
    }
};
void T2FactoryViews(){
    T2LifecycleFixture f;f.Pair(0);f.Traffic(0,1);f.Observe(1,1);
    const auto before=public_socket_fake::Observe();
    T2Check(!f.runtimes[0]->Close(),"Close accepted live Participant views");
    f.Pair(1);f.Traffic(0,2);f.Traffic(1,3);f.Observe(3,2);
    T2Check(!f.descriptors[0]->create_transport()&&!f.descriptors[1]->create_transport(),"third view must exceed unchanged bound");
    f.Remove(0,0);f.Traffic(1,4);
    T2Check(phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]).live_views==1,"closing P removed Q");
    T2Check(f.Create(0,0),"replacement public Participant");f.Traffic(0,5);f.Traffic(1,6);
    f.Remove(0,1);f.Remove(1,1);
    // With P held active, simultaneous borrowed admissions compete for the one
    // remaining slot. Hold the winner until both calls have returned.
    using Transport=eprosima::fastdds::rtps::TransportInterface;
    std::array<std::unique_ptr<Transport>,2> borrowed;
    std::mutex gate;std::condition_variable ready;unsigned arrived=0;
    std::thread threads[2];std::exception_ptr errors[2];
    for(unsigned i=0;i<2;++i)threads[i]=std::thread([&,i]{try{
        {std::unique_lock<std::mutex> lock(gate);++arrived;ready.notify_all();
            T2Check(ready.wait_for(lock,2s,[&]{return arrived==2;}),"bounded admission race barrier");}
        borrowed[i].reset(f.descriptors[0]->create_transport());
    }catch(...){errors[i]=std::current_exception();}});
    for(auto& t:threads)t.join();for(const auto& e:errors)if(e)std::rethrow_exception(e);
    T2Check(unsigned(bool(borrowed[0]))+unsigned(bool(borrowed[1]))==1,"exactly one remaining-slot race winner");
    T2Check(!f.Create(0,1,151,false,false),"public Factory accepted third view");
    T2Check(!f.runtimes[0]->Close(),"borrowed winner did not hold owner open");
    borrowed[0].reset();borrowed[1].reset();f.Traffic(0,7);f.Observe(7,1);
    const auto after=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==before.sockets[side]&&after.listens[side]==before.listens[side]&&
        after.binds[side]==before.binds[side]&&after.shutdowns[side]==before.shutdowns[side],"view lifecycle changed native anchor");
    f.Finish();std::cout<<"FACTORY_TWO_VIEWS third_refused=1 remaining_slot_race_winners=1 native_unchanged=1\n";
}
void T2RepeatedViews(){
    T2LifecycleFixture f;
    for(unsigned cycle=1;cycle<=40;++cycle){
        for(unsigned side=0;side<2;++side)T2Check(bool(f.runtimes[side]->MakeDescriptor(T2Descriptor(side,0))),"repeated descriptor issuance");
        f.Pair(0);f.Traffic(0,cycle);
        for(unsigned side=0;side<2;++side)f.Remove(side,0);
        f.Observe(cycle,0);
    }
    f.Finish();std::cout<<"REPEAT cycles=40 FakeWorldReset_inside_loop=0 writes=80 takes=80\n";
}
void T2Domains(){
    T2LifecycleFixture f;std::exception_ptr errors[2];
    std::thread a([&]{try{f.Create(0,0,151,true);}catch(...){errors[0]=std::current_exception();}});
    std::thread b([&]{try{f.Create(1,0,152,true);}catch(...){errors[1]=std::current_exception();}});
    a.join();b.join();for(const auto& e:errors)if(e)std::rethrow_exception(e);
    try{for(unsigned side=0;side<2;++side)f.endpoints[side][0].received->WaitNone();}
    catch(...){f.TraceMatches();throw;} // retain the original domain failure
    f.Create(0,1,152,true);f.Create(1,1,151,true);
    f.Traffic(0,1,1);f.Traffic(1,2,0);f.Finish();
}
void T2Borrowed(){
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    experimental::Runtime runtime(T2Configuration(0,0),experimental::RuntimeLifetime::PersistentNativeAnchor);
    const auto d=runtime.MakeDescriptor(T2Descriptor(0,0));T2Check(bool(d),"borrow descriptor");
    using Transport=eprosima::fastdds::rtps::TransportInterface;
    std::unique_ptr<Transport> first(d->create_transport()),second(d->create_transport());
    T2Check(first&&second&&!runtime.Close(),"two borrowed views did not hold owner open");
    T2Check(!d->create_transport(),"third borrowed view exceeded unchanged bound");
    T2Check(!public_socket_fake::Observe().live,"uninitialised borrowed views unexpectedly opened SDK sockets");
    first.reset();T2Check(!runtime.Close(),"closing first borrow removed second");
    std::unique_ptr<Transport> replacement(d->create_transport());T2Check(bool(replacement),"failed admission leaked a slot");
    second.reset();T2Check(!runtime.Close(),"replacement borrow did not retain owner");
    replacement.reset();T2Check(runtime.Close(),"zero borrowed views did not allow close");
    T2Check(!d->create_transport(),"explicitly closed owner resurrected");
    std::cout<<"BORROWED_TWO third_refused=1 independent_close=1 reused_slot=1 SDK_live=0\n";
}
void T2ClosedCallback(){
    auto old=std::make_unique<T2LifecycleFixture>();old->Pair(0);old->Traffic(0,1);
    const auto saved=public_socket_fake::SaveAcceptedBytes(0);old->Finish();old.reset();
    T2LifecycleFixture fresh(1,false);fresh.Pair(0);
    T2Check(public_socket_fake::CurrentSocket(0,2)==saved.socket,"Fake did not reuse the original numeric ID");
    fresh.Traffic(0,2);
    const auto current=public_socket_fake::SaveAcceptedBytes(0);
    T2Check(current.function!=saved.function,"distinct native incarnation reused old callback entrance");
    const auto state=fresh.runtimes[0]->ObserveSocket();const std::array<std::uint8_t,8193> stale{};
    saved.function(saved.socket,stale.data(),static_cast<std::uint32_t>(stale.size()));
    T2Check(fresh.runtimes[0]->ObserveSocket().rejected_callbacks==state.rejected_callbacks,
        "closed old native callback entered the different new owner");
    current.function(current.socket,stale.data(),static_cast<std::uint32_t>(stale.size()));
    T2Check(fresh.runtimes[0]->ObserveSocket().rejected_callbacks==state.rejected_callbacks+1,
        "current callback poison did not distinguish the new owner");
    fresh.Traffic(0,3);fresh.Finish();
    std::cout<<"OLD_FUNCTION_SAME_NUMERIC_ID_REJECTED different_service_entrance=1 same_service_recreate=UNSUPPORTED FakeWorldReset=0\n";
}
struct T2ReleaseGuard {
    T2Reader* receiver;bool armed=true;
    ~T2ReleaseGuard(){if(armed)Release();}
    void Release(){std::lock_guard<std::mutex> lock(receiver->gate);armed=false;receiver->release=true;receiver->changed.notify_all();}
};
void T2HeldReceiver(){
    T2LifecycleFixture f;f.Pair(0);f.Traffic(0,1);
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> capacity_guard(f.descriptors[0]->create_transport());
    T2Check(bool(capacity_guard),"second borrowed capacity guard");
    auto& old=f.endpoints[0][0];auto& peer=f.endpoints[1][0];
    T2Sample sample;sample.cycle=91;sample.source=1;sample.ordinal=1;
    for(unsigned i=0;i<32;++i)sample.payload[i]=std::uint8_t(0xa0+i);
    std::mutex captured_gate;std::vector<std::uint8_t> captured;
    public_socket_fake::SetPacketObserver([&](int side,const std::uint8_t* p,std::size_t n){
        if(side!=1||std::search(p,p+n,sample.payload.begin(),sample.payload.end())==p+n)return;
        std::lock_guard<std::mutex> lock(captured_gate);captured.assign(p,p+n);
    });
    {std::lock_guard<std::mutex> lock(old.received->gate);old.received->hold=true;}
    T2ReleaseGuard release{old.received.get()};
    T2Check(peer.writer->write(&sample,dds::HANDLE_NIL)==0,"held real DDS write");
    {std::unique_lock<std::mutex> lock(old.received->gate);
        T2Check(old.received->changed.wait_for(lock,2s,[&]{return old.received->entered;}),"actual old receiver callback did not enter");}
    old.received->WaitData(sample,peer.writer->guid());
    std::vector<std::uint8_t> frame;{std::lock_guard<std::mutex> lock(captured_gate);frame=captured;}
    public_socket_fake::SetPacketObserver({});T2Check(!frame.empty(),"no actual SDK RTPS data frame captured");
    const auto saved=public_socket_fake::SaveAcceptedBytes(0);
    // The actual original SDK callback only copies/admit/notifies. It must return
    // with old receiver work queued even while DDS is running its first call.
    saved.function(saved.socket,frame.data(),static_cast<std::uint32_t>(frame.size()));
    const auto queued=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);
    T2Check(queued.receive.queued>0&&queued.receive.delivering>0,"old receiver admission/lease was not actually held");
    const auto native_before=public_socket_fake::Observe();
    auto deletion=std::async(std::launch::async,[&]{f.Remove(0,0);});
    const bool blocked=deletion.wait_for(80ms)==std::future_status::timeout;
    const bool refused=!f.runtimes[0]->Close();
    const bool borrowed_refused=!f.descriptors[0]->create_transport();
    auto replacement=std::async(std::launch::async,[&]{return f.Create(0,1,151,false,false);});
    const bool replacement_completed=replacement.wait_for(80ms)==std::future_status::ready;
    release.Release();
    T2Check(blocked&&refused&&borrowed_refused,"public deletion/owner Close ignored borrowed old receiver");
    T2Check(deletion.wait_for(2s)==std::future_status::ready,"public deletion did not finish after receiver release");deletion.get();
    T2Check(replacement.wait_for(2s)==std::future_status::ready,"public replacement call did not complete");
    const bool replacement_created=replacement.get();
    T2Check(replacement_completed&&!replacement_created,"held membership did not refuse public Factory replacement");
    capacity_guard.reset(); // release its independent admission before the zero-view gap
    const auto gap=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);
    T2Check(gap.live_views==0&&gap.receive.active_applications==0&&gap.receive.records==0&&gap.receive.bytes==0&&
        gap.receive.retained_applications==0,"old input did not naturally drain before replacement");
    T2Check(phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[1]).live_views==1,"remote B was closed with old A");
    f.Create(0,0);f.Traffic(0,3);f.Observe(3,1);
    const auto native_after=public_socket_fake::Observe();
    T2Check(native_after.sockets[1]==native_before.sockets[1]&&native_after.listens[1]==native_before.listens[1]&&
        native_after.binds[1]==native_before.binds[1]&&native_after.shutdowns[1]==native_before.shutdowns[1],"remote B native lifecycle changed");
    f.Finish();
    std::cout<<"HELD_OLD_RECEIVER queued="<<queued.receive.queued<<" public_delete_blocked=1 owner_close_refused=1 held_replacement_refused=1 remote_B_native_unchanged=1 replacement_exact_writer_and_payload=1\n";
}
void T2SelfMatchDiagnostic(){
    T2LifecycleFixture f(0,true,false);f.Pair(0);
    for(unsigned side=0;side<2;++side){auto& r=*f.endpoints[side][0].received;std::unique_lock<std::mutex> lock(r.gate);
        T2Check(r.changed.wait_for(lock,2s,[&]{return r.matches==2;}),"default own+peer match diagnostic");}
    f.TraceMatches();f.Finish();
    std::cout<<"SELF_MATCH_DIAGNOSTIC_ONLY default_local_sample_delivery=NOT_TESTED\n";
}
void T2UdpComparison(bool fixed,bool same_domain){
    // Process-only test configuration. No interface other than loopback is
    // permitted, and no multicast, external discovery or system setting changes.
    for(const auto* n:{"FASTDDS_DEFAULT_PROFILES_FILE","FASTRTPS_DEFAULT_PROFILES_FILE","FASTDDS_ENVIRONMENT_FILE",
        "ROS_DISCOVERY_SERVER","ROS_SUPER_CLIENT","FASTDDS_BUILTIN_TRANSPORTS","FASTDDS_STATISTICS"})T2Check(_putenv_s(n,"")==0,"test process environment");
    T2Check(_putenv_s("SKIP_DEFAULT_XML","1")==0,"skip process default XML");
    eprosima::fastdds::dds::Log::SetVerbosity(eprosima::fastdds::dds::Log::Info);
    auto* factory=dds::DomainParticipantFactory::get_instance();
    const char xml[]="<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
    T2Check(factory->load_XML_profiles_string(xml,sizeof(xml)-1)==0,"stock UDP XML intraprocess OFF");
    std::array<T2Endpoint,2> apps;
    struct Cleanup {
        std::array<T2Endpoint,2>& apps;dds::DomainParticipantFactory* factory;
        ~Cleanup(){for(auto& a:apps)if(a.participant){a.participant->delete_contained_entities();factory->delete_participant(a.participant);}}
    } cleanup{apps,factory};
    dds::TypeSupport type(new T2SampleType);
    for(unsigned side=0;side<2;++side){
        auto descriptor=std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();descriptor->interfaceWhiteList={"127.0.0.1"};
        dds::DomainParticipantQos q;q.transport().use_builtin_transports=false;q.transport().user_transports={descriptor};
        q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");
        auto& w=q.wire_protocol();w.participant_id=static_cast<int>(side);w.builtin.avoid_builtin_multicast=true;
        Locator meta;meta.kind=LOCATOR_KIND_UDPv4;meta.address[12]=127;meta.address[15]=1;
        Locator user(meta),peer(meta);
        if(fixed){meta.port=57240+2*side;user.port=meta.port+1;peer.port=57240+2*(1-side);}
        // In mapped mode all ports stay zero here: stock fill/configure maps its
        // own domain and Participant id using standard PortParameters.
        w.builtin.metatrafficMulticastLocatorList.clear();w.default_multicast_locator_list.clear();
        w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(meta);
        w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(user);
        w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(peer);
        auto& a=apps[side];a.received=std::make_unique<T2Reader>();a.sent=std::make_unique<T2Writer>();
        const unsigned domain=151+(!same_domain&&side?1:0);
        a.participant=factory->create_participant(domain,q);T2Check(a.participant,"pure loopback UDP Participant");
        T2Check(type.register_type(a.participant)==0,"UDP type");
        auto* topic=a.participant->create_topic("StockUdpDomainControl",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* pub=a.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto* sub=a.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        T2Check(topic&&pub&&sub,"UDP entities");auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;
        wq.data_sharing().off();rq.data_sharing().off();wq.reliability().kind=rq.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;
        a.writer=pub->create_datawriter(topic,wq,a.sent.get());a.reader=sub->create_datareader(topic,rq,a.received.get());T2Check(a.writer&&a.reader,"UDP endpoints");
        std::cout<<"UDP_LOOPBACK side="<<side<<" domain="<<domain<<" interfaceWhiteList=127.0.0.1 builtin=0 multicast=0 meta_port="<<meta.port<<" user_port="<<user.port<<" peer_port="<<peer.port<<'\n';
    }
    if(fixed||same_domain){
        for(auto& a:apps){a.received->WaitMatched();a.sent->WaitMatched();}
        for(unsigned side=0;side<2;++side){T2Sample sample;sample.cycle=1;sample.source=side;sample.ordinal=1;sample.payload.fill(std::uint8_t(31+side));
            T2Check(apps[side].writer->write(&sample,dds::HANDLE_NIL)==0,"UDP public write");apps[1-side].received->WaitData(sample,apps[side].writer->guid());}
        std::cout<<"UDP_CONTROL exact_current_writer_payload=1 writes=2 takes=2\n";
    }else{
        for(auto& a:apps)a.received->WaitNone();
        T2Sample sample;sample.cycle=9;sample.source=0;sample.ordinal=1;
        T2Check(apps[0].writer->write(&sample,dds::HANDLE_NIL)==0,"unmatched UDP write return");
        for(auto& a:apps)a.received->WaitNone();
        std::cout<<"UDP_DOMAIN_MAPPED_NEGATIVE matches=0 receives=0 bounded_observation=600ms\n";
    }
}
}
int main(int argc,char** argv){try{
    T2Check(argc==2,"one case required");T2LoadedDll();const std::string name=argv[1];
    if(name=="factory")T2FactoryViews();else if(name=="repeat")T2RepeatedViews();
    else if(name=="domains")T2Domains();else if(name=="borrowed")T2Borrowed();
    else if(name=="closed-callback")T2ClosedCallback();else if(name=="held-receiver")T2HeldReceiver();
    else if(name=="self-match-diagnostic")T2SelfMatchDiagnostic();
    else if(name=="udp-fixed-cross-domain")T2UdpComparison(true,false);
    else if(name=="udp-mapped-same-domain")T2UdpComparison(false,true);
    else if(name=="udp-mapped-cross-domain")T2UdpComparison(false,false);
    else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<name<<(name.rfind("udp-",0)==0?" HOST_STOCK_UDP_LOOPBACK_ONLY":" HOST_FAKE_SOCKET_ONLY")<<" DEVICE_TESTED=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
