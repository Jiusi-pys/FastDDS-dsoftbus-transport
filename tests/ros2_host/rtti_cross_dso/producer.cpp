// Host RTTI/ownership fixture using public Fast DDS containers. No transport
// communication claim is made by this isolated ABI test.
#include "api.hpp"
#include "../../../examples/dds214_layer_baseline/LayerSample.hpp"
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <stdexcept>
namespace fd=eprosima::fastdds::dds;
using RC=eprosima::fastrtps::types::ReturnCode_t;
namespace {
fd::DomainParticipant* participant=nullptr;
fd::Topic* topic=nullptr;
fd::TypeSupport type(new LayerSampleType);
class OtherDescription final : public fd::TopicDescription {
public:
    OtherDescription():TopicDescription("rtti_negative","rtti_negative"){}
    fd::DomainParticipant* get_participant() const override {return nullptr;}
    fd::TopicDescriptionImpl* get_impl() const override {return nullptr;}
} unrelated;
}
bool create_fixture() {
    if(participant)return false;
    auto qos=fd::PARTICIPANT_QOS_DEFAULT;
    // Fast DDS needs a valid local participant transport even for container
    // tests. Disable discovery and create no endpoints or samples here. This
    // fixture establishes cross-DLL RTTI only, never DSoftBus communication.
    qos.wire_protocol().builtin.discovery_config.discoveryProtocol=
            eprosima::fastrtps::rtps::DiscoveryProtocol_t::NONE;
    participant=fd::DomainParticipantFactory::get_instance()->create_participant(211,qos);
    if(!participant)return false;
    if(type.register_type(participant)!=RC::RETCODE_OK)return false;
    topic=participant->create_topic("rtti_positive",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
    return topic!=nullptr;
}
const fd::TopicDescription* positive_description(){return topic;}
const fd::TopicDescription* unrelated_description(){return &unrelated;}
const std::type_info* producer_description_type(){return &typeid(fd::TopicDescription);}
int destroy_fixture(){
    if(topic){auto rc=participant->delete_topic(topic);if(rc!=RC::RETCODE_OK)return 1;topic=nullptr;}
    if(participant){
        if(participant->unregister_type(type.get_type_name())!=RC::RETCODE_OK)return 2;
        if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant)!=RC::RETCODE_OK)return 3;
        participant=nullptr;
    }
    return 0;
}
void throw_fixture_exception(){throw std::runtime_error("owned cross-DSO exception");}
