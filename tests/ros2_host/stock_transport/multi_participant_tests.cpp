// Real stock Factory/Writer/Reader, project Transport and explicit Fake C SDK.
// Reuse application/ledger helpers; no Factory or protocol implementation patch.
#include <cstdlib>
#include <new>
namespace {thread_local long pv_fail_after=-1;thread_local bool pv_failed=false;}
void* operator new(std::size_t n){
    if(pv_fail_after==0){pv_fail_after=-1;pv_failed=true;throw std::bad_alloc();}
    if(pv_fail_after>0)--pv_fail_after;
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define main R39PriorViewsMain
#include "runtime_views_tests.cpp"
#undef main
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>

namespace {
// Observation-only delegate: returns precisely the owned pointer supplied by
// the production descriptor. Fixed storage neither allocates nor wraps Transport.
struct CapturedDescriptor final:eprosima::fastdds::rtps::TransportDescriptorInterface {
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> original;
    mutable std::array<DSoftBusTransport*,8> views{};
    mutable std::atomic<unsigned> count{0};
    explicit CapturedDescriptor(std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> d)
        :TransportDescriptorInterface(d->maxMessageSize,1),original(std::move(d)){}
    eprosima::fastdds::rtps::TransportInterface* create_transport()const override{
        auto* value=original->create_transport();
        if(value){const auto n=count.fetch_add(1);if(n<views.size())views[n]=dynamic_cast<DSoftBusTransport*>(value);}
        return value;
    }
    std::uint32_t min_send_buffer_size()const override{return original->min_send_buffer_size();}
};
using Included=std::array<std::array<bool,2>,2>;
const Included all{{{{true,true}},{{true,true}}}};
std::array<std::shared_ptr<CapturedDescriptor>,2> Scope(T2LifecycleFixture& f){
    std::array<std::shared_ptr<CapturedDescriptor>,2> captures;
    for(unsigned side=0;side<2;++side){
        auto d=f.runtimes[side]->MakeStockDescriptor(T2Descriptor(side,f.epoch));T2Check(bool(d),"explicit scoped descriptor");
        captures[side]=std::make_shared<CapturedDescriptor>(d);f.descriptors[side]=captures[side];
    }
    return captures;
}
void WaitEdges(T2LifecycleFixture& f,const Included& included){
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
        auto& app=f.endpoints[side][group];if(!app.participant||!included[side][group])continue;
        std::vector<dds::InstanceHandle_t> expected;
        for(unsigned remote=0;remote<2;++remote)if(included[1-side][remote]&&f.endpoints[1-side][remote].participant)
            expected.push_back(f.endpoints[1-side][remote].writer->get_instance_handle());
        auto& reader=*app.received;
        {std::unique_lock<std::mutex> lock(reader.gate);
            T2Check(reader.changed.wait_for(lock,2s,[&]{
                if(reader.matched_writers.size()!=expected.size()||reader.matches!=int(expected.size()))return false;
                for(const auto& h:expected)if(std::find(reader.matched_writers.begin(),reader.matched_writers.end(),h)==reader.matched_writers.end())return false;
                return true;
            }),"exact remote writer GUID set on each independent reader");}
        auto& writer=*app.sent;
        {std::unique_lock<std::mutex> lock(writer.gate);
            T2Check(writer.changed.wait_for(lock,2s,[&]{return writer.matches==int(expected.size());}),"exact remote reader count");}
    }
}
unsigned Traffic(T2LifecycleFixture& f,const Included& included,unsigned cycle){
    WaitEdges(f,included);unsigned takes=0,writes=0;const auto before=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group){
        auto& source=f.endpoints[side][group];if(!source.participant||!included[side][group])continue;
        T2Sample value;value.cycle=cycle;value.source=side+2*group;value.ordinal=cycle*4+value.source;
        for(unsigned i=0;i<value.payload.size();++i)value.payload[i]=std::uint8_t(cycle*29+value.source*17+i);
        T2Check(source.writer->write(&value,dds::HANDLE_NIL)==0,"real stock DDS write");++writes;
        for(unsigned remote=0;remote<2;++remote){
            auto& target=f.endpoints[1-side][remote];if(!target.participant||!included[1-side][remote])continue;
            target.received->WaitData(value,source.writer->guid());++takes;
            const auto handle=source.writer->get_instance_handle();const char* digits="0123456789abcdef";
            std::cout<<"CROSS_EDGE cycle="<<cycle<<" from="<<side<<':'<<group<<" to="<<1-side<<':'<<remote<<" writer=";
            for(unsigned i=0;i<16;++i)std::cout<<digits[handle.value[i]>>4]<<digits[handle.value[i]&15];
            std::cout<<" exact_GUID_payload=1 ordinal="<<value.ordinal<<'\n';
        }
        T2Check(source.writer->wait_for_acknowledgments({1,0})==0,"real reliable acknowledgment");
    }
    const auto after=public_socket_fake::Observe();
    T2Check(after.forwards[0]>before.forwards[0]&&after.forwards[1]>before.forwards[1],"actual Socket C link frames in both directions");
    std::cout<<"CROSS_LEDGER cycle="<<cycle<<" writes="<<writes<<" takes="<<takes<<" local_edges=NOT_PROVIDED other_transport=OFF intraprocess=OFF DataSharing=OFF\n";
    return takes;
}
void NativeUnchanged(const public_socket_fake::Snapshot& before){
    const auto after=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(after.sockets[side]==before.sockets[side]&&after.listens[side]==before.listens[side]&&
        after.binds[side]==before.binds[side]&&after.shutdowns[side]==before.shutdowns[side],"view changes rebuilt or closed native anchors");
}
void CrossAndClose(){
    T2LifecycleFixture f;const auto captures=Scope(f);f.Pair(0,true);f.Pair(1,true);
    T2Check(Traffic(f,all,1)==8,"all four cross pairs, eight directed takes");f.TraceMatches();f.Observe(1,2);
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group)
        T2Check(captures[side]->views[group]&&captures[side]->views[group]->domain_snapshot().state==ViewDomainState::Ready,"independent view domain confirmation");
    const auto native=public_socket_fake::Observe();const auto old=f.endpoints[0][0].writer->get_instance_handle();
    const auto old_identity=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]).receive.active_applications;
    T2Check(old_identity==2&&!f.runtimes[0]->Close(),"live views reject owner Close");
    f.Remove(0,0);Included remaining=all;remaining[0][0]=false;
    T2Check(Traffic(f,remaining,2)==4,"surviving A1 communicates with both B views");
    T2Check(phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]).live_views==1,"independent first-view close");
    T2Check(f.Create(0,0,151,true),"replacement in returned view slot");
    T2Check(!(f.endpoints[0][0].writer->get_instance_handle()==old),"replacement Writer GUID differs from closed writer");
    {std::lock_guard<std::mutex> lock(f.endpoints[0][0].received->gate);
        T2Check(f.endpoints[0][0].received->receipts.empty(),"replacement inherited an old application sample");}
    T2Check(Traffic(f,all,3)==8,"all cross edges after independent replacement");NativeUnchanged(native);f.Observe(3,2);f.Finish();
    std::cout<<"MULTI_REMOTE_TWO_BY_TWO directed_takes=20 independent_close=1 replacement=1 native_listener_per_Runtime=1 complete_local_all_to_all=NOT_PROVIDED\n";
}
void AllocationRollback(){
    T2LifecycleFixture f;f.Pair(0);f.Traffic(0,1);const auto native=public_socket_fake::Observe();unsigned failures=0,successes=0;
    for(long fail=0;fail<24;++fail){
        pv_failed=false;pv_fail_after=fail;
        std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> view(f.descriptors[0]->create_transport());
        pv_fail_after=-1;
        if(pv_failed){++failures;T2Check(!view,"failed factory allocation published a partial view");}
        else{++successes;T2Check(bool(view),"nonfailing second borrowed view");}
        view.reset();const auto state=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);
        T2Check(state.live_views==1&&state.receive.active_applications==1,"rollback consumed or removed another view");NativeUnchanged(native);
    }
    T2Check(failures>=2,"actual Join/facade allocation failure exercised");
    f.Pair(1);f.Traffic(0,2);f.Traffic(1,3);f.Observe(3,2);NativeUnchanged(native);f.Finish();
    std::cout<<"ALLOCATION_ROLLBACK attempts=24 actual_failures="<<failures<<" borrowed_success="<<successes<<" independent_primary_healthy=1\n";
}
void PerViewDomain(){
    T2LifecycleFixture f;const auto captures=Scope(f);f.Pair(0,true);
    T2Check(f.Create(0,1,151,true)&&f.Create(1,1,152,true),"original Factory returns preserved for scoped views");
    Included allowed=all;allowed[1][1]=false;
    T2Check(Traffic(f,allowed,1)==4,"matching sibling views remain healthy");
    auto* refused=captures[1]->views[1];T2Check(refused,"actual second transport captured");
    const auto snapshot=refused->domain_snapshot();
    T2Check(snapshot.expected_domain_id==151&&snapshot.first_observed_domain==152&&snapshot.state==ViewDomainState::Refused,"second view has its own refused state");
    for(unsigned side=0;side<2;++side)for(unsigned group=0;group<2;++group)if(allowed[side][group])
        T2Check(captures[side]->views[group]->domain_snapshot().state==ViewDomainState::Ready,"refusal contaminated a matching view");
    f.endpoints[1][1].received->WaitNone();T2Sample unmatched;unmatched.cycle=99;unmatched.source=3;
    const auto rc=f.endpoints[1][1].writer->write(&unmatched,dds::HANDLE_NIL);
    f.endpoints[1][1].received->WaitNone();
    std::cout<<"REFUSED_VIEW Factory_nonnull=1 actual_domain=152 expected_domain=151 unmatched_write_rc="<<int(rc())<<" transport_TX_success=NOT_CLAIMED\n";
    const auto native=public_socket_fake::Observe();f.Remove(1,1);
    T2Check(Traffic(f,allowed,2)==4,"closing refused view preserves matching sibling");NativeUnchanged(native);f.Finish();
}
}
int main(int argc,char** argv){try{
    T2LoadedDll();T2Check(argc==2,"case required");const std::string which=argv[1];
    if(which=="cross-close")CrossAndClose();else if(which=="allocation")AllocationRollback();else if(which=="per-view-domain")PerViewDomain();
    else throw std::runtime_error("unknown multi-view case");
    std::cout<<"PASS stock multi_participant "<<which<<'\n';return 0;
}catch(const std::exception& e){pv_fail_after=-1;std::cerr<<"FAIL stock multi_participant "<<e.what()<<'\n';return 1;}}
