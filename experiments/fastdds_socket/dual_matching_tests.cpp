#include "DualParticipantPolicy.hpp"
#include "DualValidation.hpp"
#include "large_sample/ExperimentLargeSamplePubSubTypes.hpp"
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <cstdio>
#include <cstring>
#include <memory>
namespace sample_dds=eprosima::fastdds::dds;
namespace rtps=eprosima::fastdds::rtps;
int main(int argc,char** argv) {
    if(argc!=2 || (std::strcmp(argv[1],"control") && std::strcmp(argv[1],"isolated") && std::strcmp(argv[1],"fixture"))) return 64;
    const bool fixture=std::strcmp(argv[1],"fixture")==0;
    const bool isolated=fixture || std::strcmp(argv[1],"isolated")==0;
    auto* factory=sample_dds::DomainParticipantFactory::get_instance();
    eprosima::fastdds::LibrarySettings library;library.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
    if(factory->set_library_settings(library)!=sample_dds::RETCODE_OK) return 2;
    sample_dds::DomainParticipantQos qos;
    qos.transport().use_builtin_transports=false;
    auto udp=std::make_shared<rtps::UDPv4TransportDescriptor>();udp->interfaceWhiteList.push_back("127.0.0.1");
    qos.transport().user_transports.push_back(udp);
    qos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();
    qos.wire_protocol().builtin.initialPeersList.clear();
    if(isolated) mdds::experiment::ApplyDualParticipantPolicy(qos);
    const std::string run="0123456789abcdef";
    if(fixture) mdds::experiment::ApplyDualIdentityFixture(qos,run,true,1);
    auto* participant=factory->create_participant(231,qos);
    if(!participant) return 3;
    bool setup=!isolated || mdds::experiment::DualParticipantPolicyRequested(participant->get_qos());
    sample_dds::TypeSupport type(new ExperimentLargeSamplePubSubType());
    setup=setup && type.register_type(participant)==sample_dds::RETCODE_OK;
    auto* topic=setup?participant->create_topic("dual-policy-same-topic",type.get_type_name(),sample_dds::TOPIC_QOS_DEFAULT):nullptr;
    auto* publisher=topic?participant->create_publisher(sample_dds::PUBLISHER_QOS_DEFAULT):nullptr;
    auto* subscriber=topic?participant->create_subscriber(sample_dds::SUBSCRIBER_QOS_DEFAULT):nullptr;
    sample_dds::DataWriterQos wq;wq.data_sharing().off();wq.reliability().kind=sample_dds::RELIABLE_RELIABILITY_QOS;
    sample_dds::DataReaderQos rq;rq.data_sharing().off();rq.reliability().kind=sample_dds::RELIABLE_RELIABILITY_QOS;
    if(fixture){wq.endpoint().entity_id=1;rq.endpoint().entity_id=2;}
    auto* writer=publisher?publisher->create_datawriter(topic,wq):nullptr;
    auto* reader=subscriber?subscriber->create_datareader(topic,rq):nullptr;
    sample_dds::PublicationMatchedStatus ws;sample_dds::SubscriptionMatchedStatus rs;
    setup=writer && reader && writer->get_publication_matched_status(ws)==sample_dds::RETCODE_OK &&
        reader->get_subscription_matched_status(rs)==sample_dds::RETCODE_OK;
    // Fast DDS 3.6.2 pairs local endpoints synchronously during creation.
    // This test checks public DDS policy only; it is not native path evidence.
    const auto expected=isolated?0:1;
    bool matches=setup && ws.current_count==expected && rs.current_count==expected;
    if(fixture && setup) {
        namespace ex=mdds::experiment;
        matches=matches && ex::DualBytes(participant->guid())==ex::DualIdentity(run,true,1,ex::EndpointKind::Participant) &&
            ex::DualBytes(writer->guid())==ex::DualIdentity(run,true,1,ex::EndpointKind::Writer) &&
            ex::DualBytes(reader->guid())==ex::DualIdentity(run,true,1,ex::EndpointKind::Reader);
        ex::ApplyDualIdentityFixture(qos,run,true,2);
        auto* second=factory->create_participant(231,qos);
        matches=matches && second && ex::DualBytes(second->guid())==ex::DualIdentity(run,true,2,ex::EndpointKind::Participant);
        if(second) {
            matches=matches && participant->guid().guidPrefix.is_on_same_process_as(second->guid().guidPrefix) &&
                !second->guid().guidPrefix.is_on_same_process_as(ex::DualGuid(ex::DualIdentity(run,false,2,ex::EndpointKind::Participant)).guidPrefix);
            matches=factory->delete_participant(second)==sample_dds::RETCODE_OK && matches;
        }
        std::printf("scope=HOST_DDS_IDENTITY_FIXTURE actual_guid_readback=%d endpoint_entity_id_readback=%d grouping=FIXTURE_NOT_OS_PROOF\n",matches,matches);
    }
    const auto contained=participant->delete_contained_entities();
    const auto removed=factory->delete_participant(participant);
    const bool pass=matches && contained==sample_dds::RETCODE_OK && removed==sample_dds::RETCODE_OK;
    std::printf("scope=HOST_DDS_POLICY_ONLY mode=%s same_topic=1 intraprocess=OFF data_sharing=OFF writer_matches=%d reader_matches=%d expected=%d contained_rc=%d participant_rc=%d result=%s native_proof=NONE\n",
        argv[1],ws.current_count,rs.current_count,expected,int(contained),int(removed),pass?"PASS":"FAIL");
    return pass?0:1;
}
