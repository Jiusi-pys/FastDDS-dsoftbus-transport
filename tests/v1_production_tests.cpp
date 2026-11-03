#include "V1NativeNameTestSupport.hpp"
#include "HostProviderTestSupport.hpp"
#include <fastdds_dsoftbus/dsoftbus/V1Address.hpp>
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/LocatorSelectorEntry.hpp>
#include <algorithm>

namespace fastdds_dsoftbus::dsoftbus {
class DSoftBusTransportTestAccess {
public:
    static std::unique_ptr<DSoftBusTransport> Create(const DSoftBusTransportDescriptor& config, std::shared_ptr<const ConnectionGenerationSource> source) {
        return std::unique_ptr<DSoftBusTransport>(new DSoftBusTransport(config,DSoftBusTransport::HostModelTag{},std::move(source)));
    }
};
class EndpointDirectoryTestAccess {
public:
    static DirectoryResult<EndpointDirectory::Snapshot> Forced(const DirectoryConfiguration& config) {
        return EndpointDirectory::BuildImpl(config, false);
    }
};
}
namespace {
Locator V1(const char* udid, std::uint32_t slot) {
    EndpointId key{}; Locator value;
    Check(v1::AddressCodec::DeriveKey({"alpha",0x01020304,udid},key),"real identity hash");
    Check(v1::AddressCodec::EncodeLocator({key,slot,v1::Profile::BytesNative},value),"V1 encode");
    return value;
}
void Production() {
    DSoftBusTransportDescriptor config(64);
    Check(config.ConfigureV1(ScopedHostDeployment({{"alpha",0x01020304,"udid-1"},17,v1::Profile::BytesNative,"v1.ns",
        {{{"alpha",0x01020304,"udid-2"},29,"model.net","model.dial",{"model.peer"}}}})).code==DirectoryError::None,"production configuration");
    const auto local=config.directory.locals.front().local_locator,remote=config.directory.routes.front().key.remote_locator;
    const std::uint8_t golden[]={0xc3,0x79,0x77,0x84,0x97,0x6e,0xd6,0x8c,0xef,0x55,0xb2,0xf0,0xf2,0x2e,0x8c,0xc4};
    Check(std::equal(std::begin(golden),std::end(golden),local.address),"independent golden production hash");
    auto mock=std::make_shared<Mock>(); auto loop=std::make_shared<HostProviderLoop>(mock);
    auto transport=HostFacadeTestAccess::Create(config,loop);
    Check(transport->init(),"production V1 nontrivial port must initialize (G2/A1 RED)");
    Receiver receiver; Check(transport->OpenInputChannel(local,&receiver,64),"actual V1 input");
    rtps::SendResourceList senders;Check(transport->OpenOutputChannel(senders,remote),"actual V1 sender");
    DSoftBusTransport::LocatorList meta,user;
    Check(transport->getDefaultMetatrafficUnicastLocators(meta,0)&&transport->getDefaultUnicastLocators(user,0),"advertisement hooks");
    Check(meta.size()==1&&user.size()==1&&*meta.begin()==local&&*user.begin()==local,"complete matching meta/user");
    Check(local.port==0x01000012&&remote.port==0x0100001e,"beyond port=1");
    Check(transport->IsLocatorSupported(remote)&&transport->is_locator_allowed(remote),"matching full locator allowed");
    for(int field=0;field<4;++field){auto wrong=remote;if(field==0)wrong.kind++;if(field==1)wrong.address[5]^=1;if(field==2)wrong.port=0x0200001e;if(field==3)wrong.port++;
        Check(!transport->is_locator_allowed(wrong)&&!transport->is_locator_reachable(wrong),"changed complete locator excluded");}
    Locator wrong=remote;wrong.port=0x0200001e;Check(!transport->IsLocatorSupported(wrong),"profile excluded");
    auto before=config;auto bad=V1Deployment{{"alpha",0x01020304,""},17,v1::Profile::BytesNative,"v1.ns",{}};
    Check(config.ConfigureV1(ScopedHostDeployment(bad)).code==DirectoryError::InvalidIdentity&&config.default_user_unicast==before.default_user_unicast,"missing identity no publication");
    bad.identity.udid="udid-1";bad.slot=0x00ffffff;
    Check(config.ConfigureV1(ScopedHostDeployment(bad)).code==DirectoryError::InvalidLocator&&config.default_user_unicast==before.default_user_unicast,"slot overflow no exposure");
    auto old=EndpointDirectory::Build(config.directory);Check(bool(old.value),"existing directory");
    auto conflicting=config.directory;conflicting.identities.push_back(conflicting.identities.front());conflicting.identities.back().udid="synthetic-conflict";
    const auto collision=EndpointDirectoryTestAccess::Forced(conflicting);
    Check(!collision.value&&collision.issue.code==DirectoryError::IdentityCollision,"forced collision actual transactional directory");
    Check((*old.value)->ResolveOutbound(config.directory.routes.front().key).value->fields.remote_network_id=="model.net","collision preserves old route");
    transport->shutdown(); Check(mock->closes==1,"actual owner close once");
}
void SelectionAndSlots() {
    auto config=Config();auto key=EndpointId{};key.fill(0xff);Locator first,second;
    Check(v1::AddressCodec::EncodeLocator({key,17,v1::Profile::BytesNative},first)&&v1::AddressCodec::EncodeLocator({key,29,v1::Profile::BytesNative},second),"FF two-slot locators");
    config.directory.routes[0].key.remote_locator=first;
    auto route=config.directory.routes[0];route.key.remote_locator=second;route.fields.expected_remote_initiator_names={"model.peer.slot2"};config.directory.routes.push_back(route);
    struct Generation final:ConnectionGenerationSource {bool TryResolve(const Route& route,SendConnectionSnapshot& out)const override{out={route.key,route.route_version,1};return true;}};
    auto transport=DSoftBusTransportTestAccess::Create(config,std::make_shared<Generation>());
    Check(transport->init(),"real facade two-slot config");
    rtps::LocatorSelectorEntry one(6,0),two(2,0);one.enabled=two.enabled=true;
    auto foreign=first;foreign.kind=1;auto unknown=first;unknown.address[1]^=1;auto profile=first;profile.port=0x02000012;auto slot=first;slot.port++;
    for(const auto& value:{first,foreign,unknown,profile,slot,second})Check(one.unicast.push_back(value)!=nullptr,"selector fixture capacity");
    Check(two.unicast.push_back(first)&&two.unicast.push_back(second),"duplicate second entry");
    rtps::LocatorSelector selector(eprosima::fastdds::ResourceLimitedContainerConfig::fixed_size_configuration(2));
    Check(selector.add_entry(&one)&&selector.add_entry(&two),"real selector entries");selector.selection_start();transport->select_locators(selector);
    std::vector<Locator> values;selector.for_each([&](const Locator& value){values.push_back(value);});
    Check(values.size()==2&&values[0]==first&&values[1]==second,"full selector FF/different slots/duplicate suppression");
    transport->shutdown();
    // Low-level fixture uses HostModel generation injection: one HostProviderLoop supports one route only.
    // Exercise directory, queue and real selector separately from the single-route SDK seam.
    auto plan=EgressPlan::Build(config.directory,{});Check(bool(plan.value),"full two-slot egress");
    SendQueueLimits limits;limits.max_record_bytes=64;limits.peer_records=1;limits.peer_bytes=64;limits.global_records=2;limits.global_bytes=128;
    SendQueue queue(limits);const std::uint8_t byte=7;SendBufferView buffer{&byte,1};SendTargetView targets[]={{&config.directory.routes[0],1},{&config.directory.routes[1],1}};
    Check(queue.EnqueueBatch(targets,2,0,SendQueue::Clock::now()+1s,&buffer,1,1)==SendAdmission::Accepted,"same address different slots admitted");
    Check(queue.SnapshotUsage().records==2&&queue.SnapshotUsage().peers==2,"full endpoint budgets");
    Check(queue.EnqueueBatch(targets,2,0,SendQueue::Clock::now()+1s,&buffer,1,1)!=SendAdmission::Accepted,"global/endpoint capacity enforced");
    queue.Close();Check(queue.SnapshotUsage().records==0&&queue.SnapshotUsage().peers==0,"all charges released");
}

}
int main() {try {Production();SelectionAndSlots();std::cout<<"V1_PRODUCTION_PATH_PASS\n";return 0;}
catch(const std::exception& e){std::cerr<<"V1_PRODUCTION_PATH_FAIL "<<e.what()<<'\n';return 1;}}
