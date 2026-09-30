#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/examples/DSoftBusKeyedSamplePubSubTypes.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace fd=eprosima::fastdds::dds;
using namespace fastdds_dsoftbus::dsoftbus;
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
[[noreturn]] void FailCleanup(const char* why) {
    std::cerr<<why<<"; failed process termination, NOT normal SDK cancellation/drain\n"<<std::flush;
    // Do not unwind Runtime while DDS may still own its transport or SDK work.
    std::abort();
}
Locator logical(unsigned side) {EndpointId id{};id[0]=10;id[15]=static_cast<std::uint8_t>(side+1);Locator l;check(LocatorCodec::Encode(id,l),"locator");return l;}
int main(int argc,char** argv) {
    if(argc!=5 || (std::string(argv[1])!="0" && std::string(argv[1])!="1")) {
        std::cerr<<"usage: fastdds_dsoftbus_socket_consumer <side:0|1> <peer-network-id> <authorized-package> <shared-service-prefix>\n";
        return 2;
    }
    const unsigned side=std::string(argv[1])=="1";
    auto name=[&](unsigned s,const char* role){return std::string(argv[4])+(s?".B.":".A.")+role;};
    phase0::PublicSocketConfiguration b;
    b.identity={name(side,"outgoing"),{910+side*4},{1010+side*4}};
    b.listener_identity={name(side,"listener"),{911+side*4},{1011+side*4}};
    b.package_name=argv[3];b.peer_network_id=argv[2];b.peer_name=name(1-side,"listener");
    b.max_message_size=8192;b.bind_qos={{0,80},{1,2000}};b.listen_qos=b.bind_qos;
    DSoftBusTransportDescriptor config(8192);
    config.directory.locals={{logical(side),name(side,"namespace"),b.listener_identity.service_name}};
    config.directory.routes={{{logical(side),logical(1-side)},{b.peer_network_id,b.peer_name,b.identity.service_name,{name(1-side,"outgoing")},RecordDataType::Bytes},1}};
    config.default_metatraffic_unicast={logical(side)};config.default_user_unicast={logical(side)};
    experimental::Runtime runtime(b);
    auto descriptor=runtime.MakeDescriptor(config);
    fd::DomainParticipant* participant=nullptr;
    auto* factory=fd::DomainParticipantFactory::get_instance();
    std::atomic<bool> stop{false},pump_failed{false};
    // Caller-owned driver stays alive during DDS creation, writes and deletion.
    std::thread pump([&]{while(!stop.load()) {
        auto send=runtime.PumpSend();
        if(send.outcome==NativeSendOutcome::ReturnedInvalidCode || send.outcome==NativeSendOutcome::ReturnedByException ||
                (send.actual_return_code && *send.actual_return_code!=0))pump_failed=true;
        auto rx=runtime.PumpReceive();
        if(rx!=phase0::BackendStatus::None && rx!=phase0::BackendStatus::NoWork && rx!=phase0::BackendStatus::InvalidState)pump_failed=true;
        std::this_thread::yield();
    }});
    int result=1;
    try {
        fd::DomainParticipantQos qos; qos.transport().use_builtin_transports=false;
        qos.transport().user_transports.push_back(descriptor);
        qos.wire_protocol().builtin.initialPeersList.push_back(logical(1-side));
        participant=factory->create_participant(177,qos);check(participant,"participant unavailable or setup failed");
        fd::TypeSupport type(new DSoftBusKeyedSamplePubSubType());check(type.register_type(participant)==fd::RETCODE_OK,"type");
        auto* topic=participant->create_topic("mdds_installed_sample",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        check(topic&&pub&&sub,"entities");
        auto* writer=pub->create_datawriter(topic,fd::DATAWRITER_QOS_DEFAULT);auto* reader=sub->create_datareader(topic,fd::DATAREADER_QOS_DEFAULT);check(writer&&reader,"endpoints");
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);fd::PublicationMatchedStatus matched;
        do {writer->get_publication_matched_status(matched);if(matched.current_count>0)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(std::chrono::steady_clock::now()<deadline&&!pump_failed);
        check(matched.current_count>0&&!pump_failed,"match deadline or pump failure");
        DSoftBusKeyedSample sample;sample.key(side+1);sample.ordinal(1);check(writer->write(&sample)==fd::RETCODE_OK,"write");
        bool received=false;fd::SampleInfo info;
        while(std::chrono::steady_clock::now()<deadline&&!pump_failed) {
            if(reader->take_next_sample(&sample,&info)==fd::RETCODE_OK&&info.valid_data&&sample.key()==2-side&&sample.ordinal()==1){received=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(received&&!pump_failed,"remote take deadline or pump failure");result=0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    if(participant) {
        if(participant->delete_contained_entities()!=fd::RETCODE_OK)FailCleanup("DDS entity deletion failed");
        if(factory->delete_participant(participant)!=fd::RETCODE_OK)FailCleanup("DDS Participant deletion failed");
    }
    stop=true;pump.join(); // No detach or deadline claim for an active SDK call.
    if(!runtime.Close())FailCleanup("Local runtime did not close");
    if(pump_failed)result=1;
    const auto observed=runtime.ObserveSocket();
    if(observed.first_api_error||observed.first_callback_error||observed.exceptions)result=1;
    std::cout<<"isolated experiment result="<<result<<" SDK drain=UNPROVEN\n";
    return result;
}
