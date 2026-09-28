#include <mdds/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <mdds/dsoftbus/SendQueue.hpp>
#include <mdds/examples/MddsKeyedSamplePubSubTypes.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace fd = eprosima::fastdds::dds;
using namespace mdds::dsoftbus;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main()
{
    auto* factory = fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipant* participant = nullptr;
    try {
        EndpointId local_id{}, peer_id{}; local_id[0]=10; peer_id[0]=10; local_id[15]=1; peer_id[15]=2;
        Locator local, peer;
        require(LocatorCodec::Encode(local_id,local) && LocatorCodec::Encode(peer_id,peer), "locator");
        DSoftBusTransportDescriptor descriptor(8192);
        descriptor.directory.locals={{local,"example.namespace","example.listener"}};
        descriptor.directory.routes={{{local,peer},{"example.network","example.peer","example.dial",{"example.remote"},RecordDataType::Bytes},1}};
        descriptor.default_metatraffic_unicast={local}; descriptor.default_user_unicast={local};
        std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> unavailable(descriptor.create_transport());
        require(unavailable && !unavailable->init(), "default MDDS must remain unavailable");
        unavailable->shutdown(); unavailable.reset();

        // Host DDS API example uses Fast DDS built-in transports, not a fake DSoftBus provider.
        participant=factory->create_participant(177,fd::PARTICIPANT_QOS_DEFAULT);
        require(participant,"participant");
        fd::TypeSupport type(new MddsKeyedSamplePubSubType());
        require(type.register_type(participant)==fd::RETCODE_OK,"register type");
        auto* topic=participant->create_topic("mdds_installed_sample",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* publisher=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
        auto* subscriber=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        require(topic && publisher && subscriber,"entities");
        auto* reader=subscriber->create_datareader(topic,fd::DATAREADER_QOS_DEFAULT);
        auto* writer=publisher->create_datawriter(topic,fd::DATAWRITER_QOS_DEFAULT);
        require(reader && writer,"endpoints");
        MddsKeyedSample sent; sent.key(7); sent.ordinal(1);
        require(writer->write(&sent)==fd::RETCODE_OK,"write");
        MddsKeyedSample received; fd::SampleInfo info;
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5); bool taken=false;
        while(std::chrono::steady_clock::now()<until) {
            if(reader->take_next_sample(&received,&info)==fd::RETCODE_OK && info.valid_data) {taken=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(taken && received.key()==7 && received.ordinal()==1,"take");
        require(participant->delete_contained_entities()==fd::RETCODE_OK,"delete entities");
        require(factory->delete_participant(participant)==fd::RETCODE_OK,"delete participant"); participant=nullptr;
        std::cout<<"DEFAULT_UNAVAILABLE; HOST_FASTDDS_WRITE_TAKE_PASS (not DSoftBus traffic)\n";
        return 0;
    } catch(const std::exception& error) {
        if(participant) {participant->delete_contained_entities();factory->delete_participant(participant);}
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
