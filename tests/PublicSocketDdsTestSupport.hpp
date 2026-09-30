#pragma once
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include "DSoftBusKeyedSamplePubSubTypes.hpp"
#include "HostParticipantEvidence.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <functional>
#include <sstream>

#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include "FakePublicSocketLink.hpp"
namespace public_dds {
using namespace fastdds_dsoftbus::dsoftbus;using namespace fastdds_dsoftbus::dsoftbus::phase0;using namespace fastdds_dsoftbus::dsoftbus::host_test;using namespace host_dds;
namespace dds=eprosima::fastdds::dds;namespace rtps=eprosima::fastdds::rtps;using namespace std::chrono_literals;
void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::string Guid(const rtps::GUID_t& g){return Hex(g.guidPrefix.value,12)+Hex(g.entityId.value,4);}
std::string Handle(const dds::InstanceHandle_t& h){return Hex(h.value,16);}
std::uint64_t Sequence(const rtps::SequenceNumber_t& n){return(std::uint64_t(std::uint32_t(n.high))<<32)|n.low;}
Locator Local(int side){EndpointId id{};id[0]=10;id[14]=128;id[15]=std::uint8_t(side+1);Locator l;Require(LocatorCodec::Encode(id,l),"locator");return l;}
std::string Name(int side,const char* role){return std::string("host.public.")+(side?"B.":"A.")+role;}
std::string Network(int side){return side?"model.net.B":"model.net.A";}
DSoftBusTransportDescriptor Configuration(int side){DSoftBusTransportDescriptor d(8192);auto local=Local(side),peer=Local(1-side);d.directory.locals={{local,Name(side,"namespace"),Name(side,"listener")}};d.directory.routes={{{local,peer},{Network(1-side),Name(1-side,"listener"),Name(side,"outgoing"),{Name(1-side,"outgoing")},RecordDataType::Bytes},1}};d.default_metatraffic_unicast={local};d.default_user_unicast={local};return d;}
PublicSocketConfiguration PublicConfig(int side){PublicSocketConfiguration c;c.identity={Name(side,"outgoing"),{std::uint64_t(910+side*4)},{std::uint64_t(1010+side*4)}};c.listener_identity={Name(side,"listener"),{std::uint64_t(911+side*4)},{std::uint64_t(1011+side*4)}};c.package_name="host.public.dds";c.peer_name=Name(1-side,"listener");c.peer_network_id=Network(1-side);c.bind_qos={{0,65536}};c.listen_qos=c.bind_qos;c.max_message_size=8192;return c;}
struct Descriptor final:rtps::TransportDescriptorInterface {
    DSoftBusTransportDescriptor config;std::shared_ptr<HostProviderLoop> loop;mutable std::atomic<unsigned> factories{0};
    Descriptor(int side,std::shared_ptr<HostProviderLoop> p):TransportDescriptorInterface(8192,1),config(Configuration(side)),loop(std::move(p)){}
    rtps::TransportInterface* create_transport()const override{++factories;return HostFacadeTestAccess::Create(config,loop).release();}
    std::uint32_t min_send_buffer_size()const override{return config.min_send_buffer_size();}
};
void Isolate(){
    for(const auto* name:{"FASTDDS_DEFAULT_PROFILES_FILE","FASTRTPS_DEFAULT_PROFILES_FILE","FASTDDS_ENVIRONMENT_FILE","ROS_DISCOVERY_SERVER","ROS_SUPER_CLIENT","FASTDDS_BUILTIN_TRANSPORTS","FASTDDS_STATISTICS"})Require(_putenv_s(name,"")==0,"clear process-only DDS environment");
    Require(_putenv_s("SKIP_DEFAULT_XML","1")==0,"skip default XML");
    eprosima::fastdds::LibrarySettings s;s.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
    auto* f=dds::DomainParticipantFactory::get_instance();Require(f->set_library_settings(s)==dds::RETCODE_OK,"intraprocess set");
    Require(f->get_library_settings(s)==dds::RETCODE_OK&&s.intraprocess_delivery==eprosima::fastdds::INTRAPROCESS_OFF,"intraprocess effective");
}
dds::DomainParticipantQos Qos(int side,std::shared_ptr<rtps::TransportDescriptorInterface> desc){
    dds::DomainParticipantQos q;q.name("HostNative"+std::to_string(side));q.transport().use_builtin_transports=false;q.transport().user_transports={std::move(desc)};
    q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");
    auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(Local(side));w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(Local(side));w.default_multicast_locator_list.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(Local(1-side));
    w.builtin.discovery_config.leaseDuration_announcementperiod={1,0};w.builtin.discovery_config.initial_announcements.count=5;w.builtin.discovery_config.initial_announcements.period={0,200000000};return q;
}
template<class Q>void EndpointQos(Q& q){
    q.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;q.reliability().max_blocking_time={1,0};
    q.durability().kind=dds::VOLATILE_DURABILITY_QOS;q.history().kind=dds::KEEP_LAST_HISTORY_QOS;q.history().depth=16;
    q.resource_limits().max_samples=32;q.resource_limits().max_instances=1;q.resource_limits().max_samples_per_instance=32;q.resource_limits().allocated_samples=16;q.data_sharing().off();
}
template<class Q>bool EffectiveEndpoint(const Q& q){return q.reliability().kind==dds::RELIABLE_RELIABILITY_QOS&&q.durability().kind==dds::VOLATILE_DURABILITY_QOS&&q.history().kind==dds::KEEP_LAST_HISTORY_QOS&&q.history().depth==16&&q.resource_limits().max_samples==32&&q.resource_limits().max_instances==1&&q.resource_limits().max_samples_per_instance==32&&q.data_sharing().kind()==dds::OFF;}

struct Observer final:ProcessReceiveObserver {
    int side;std::shared_ptr<Trace> trace;
    Observer(int i,std::shared_ptr<Trace> t):side(i),trace(std::move(t)){}
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& d,const std::uint8_t* p,std::size_t n)noexcept override{
        const auto native=d.native.Observe();
        const char* name=stage==ReceiveStage::Captured?"RX_CAPTURED":stage==ReceiveStage::OwnedCopy?"RX_OWNED":stage==ReceiveStage::Running?"RX_RUNNING":"RX_OTHER";
        if(stage==ReceiveStage::Captured||stage==ReceiveStage::OwnedCopy||stage==ReceiveStage::Running){
            if(native.role!=NativeOwnerRole::Accepted||!native.owned_id)trace->Add({"TRACE_ERROR","","","non_accepted_receive_owner",side});
            trace->Frame(name,side,p,n,d.correlation,d.identity.application_incarnation,d.identity.receiver_incarnation);
            trace->Add({"ORIGINAL_ACCEPTED_LEASE","","","owner="+std::to_string(native.incarnation)+",generation="+std::to_string(native.generation)+",socket="+std::to_string(native.owned_id.value_or(-1)),side,0,0,d.correlation,d.identity.application_incarnation,d.identity.receiver_incarnation});
        }
    }
};

}
