// Actual Fast DDS container API test. Existing loader fixture owns one byte;
// it supplies no Transport or Runtime and is not native communication evidence.
#include "../../integrations/ros2/ResidualDiagnostics.hpp"
#include "../../examples/dds214_layer_baseline/LayerSample.hpp"
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <windows.h>
namespace fd=eprosima::fastdds::dds;
namespace diag=fastdds_dsoftbus::ros2_teardown;
using RC=eprosima::fastrtps::types::ReturnCode_t;
using Capture=int(*)(unsigned,diag::ResidualSnapshot*) noexcept;
int main(int argc,char** argv){
    assert(argc==4);const std::string mode=argv[3];
    assert(mode=="empty"||mode=="publisher"||mode=="subscriber"||mode=="topic"||mode=="endpoints"||mode=="capacity"||mode=="snapshots");
    {std::ofstream cfg(argv[2]);cfg<<"bridge_library="<<argv[1]<<"\nrun=0123456789abcdef\nside=0\n";}
    _putenv_s("FASTDDS_DSOFTBUS_REQUIRED_CONFIG",argv[2]);
    auto* factory=fd::DomainParticipantFactory::get_instance();
    auto* module=GetModuleHandleW(L"fastrtps-2.14.dll");assert(module);
    auto capture=reinterpret_cast<Capture>(GetProcAddress(module,"FastddsDsoftbusFactoryCaptureResidual"));
    if(!capture){std::cerr<<"RESIDUAL_OBSERVER_NOT_AVAILABLE scope=HOST_REAL_FASTDDS_API\n";return 2;}
    auto* participant=factory->create_participant(207,fd::PARTICIPANT_QOS_DEFAULT);assert(participant);
    fd::Publisher* pub=nullptr;fd::Subscriber* sub=nullptr;fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;
    std::vector<fd::Topic*> topics;fd::TypeSupport type(new LayerSampleType);
    assert(type.register_type(participant)==RC::RETCODE_OK);
    if(mode=="publisher"||mode=="endpoints"||mode=="snapshots")pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
    if(mode=="subscriber"||mode=="endpoints")sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
    unsigned number=mode=="capacity"?40:mode=="empty"||mode=="publisher"||mode=="subscriber"||mode=="snapshots"?0:1;
    for(unsigned i=0;i<number;++i){auto* t=participant->create_topic("residual_"+std::to_string(i),type.get_type_name(),fd::TOPIC_QOS_DEFAULT);assert(t);topics.push_back(t);}
    if(mode=="endpoints"){
        assert(pub&&sub);fd::DataWriterQos writer_qos;fd::DataReaderQos reader_qos;
        writer_qos.data_sharing().off();reader_qos.data_sharing().off();
        writer=pub->create_datawriter(topics[0],writer_qos);reader=sub->create_datareader(topics[0],reader_qos);assert(writer&&reader);
    }
    const auto identity=reinterpret_cast<std::uintptr_t>(participant);
    const auto returned=factory->delete_participant(participant);
    diag::ResidualSnapshot snapshot;assert(capture(0,&snapshot)==0);
    assert(snapshot.participant==identity&&snapshot.known_mask==7&&!snapshot.capture_failed);
    assert(snapshot.counts[0]==(pub?1u:0u)&&snapshot.counts[1]==(sub?1u:0u)&&snapshot.counts[2]==number);
    assert(snapshot.topic_factories==number&&snapshot.filtered_topics==0);
    assert(snapshot.stored==std::min<std::size_t>((pub?1:0)+(sub?1:0)+number,diag::residual_object_capacity));
    if(mode=="empty"){assert(returned==RC::RETCODE_OK&&!snapshot.return_known);participant=nullptr;}
    else {assert(returned==RC::RETCODE_PRECONDITION_NOT_MET&&snapshot.return_known&&snapshot.selected_return==4);}
    bool found=false;
    for(std::size_t i=0;i<snapshot.stored;++i){const auto& row=snapshot.objects[i];
        assert(row.sample_order==row.kind+1);
        if(row.kind==2){assert(row.name_known&&row.type_known&&row.referenced_known);assert(row.referenced==(mode=="endpoints"));
            bool owned=false;for(auto* t:topics)owned|=row.object==reinterpret_cast<std::uintptr_t>(t);assert(owned);found=true;}
    }
    assert(number==0||found);
    if(mode=="snapshots") {
        // Capacity attack only: exactly eight more known refusals, never retry
        // until success or alter the held entity to evade the precondition.
        for(unsigned i=1;i<9;++i)assert(factory->delete_participant(participant)==RC::RETCODE_PRECONDITION_NOT_MET);
        diag::ResidualSnapshot retained;
        for(unsigned i=0;i<8;++i)assert(capture(i,&retained)==0&&retained.counts[0]==1&&retained.counts[1]==0&&retained.counts[2]==0);
        assert(capture(8,&retained)!=0);
    }
    if(participant){
        if(reader)assert(sub->delete_datareader(reader)==RC::RETCODE_OK);
        if(writer)assert(pub->delete_datawriter(writer)==RC::RETCODE_OK);
        for(auto* topic:topics)assert(participant->delete_topic(topic)==RC::RETCODE_OK);
        if(pub)assert(participant->delete_publisher(pub)==RC::RETCODE_OK);
        if(sub)assert(participant->delete_subscriber(sub)==RC::RETCODE_OK);
        assert(factory->delete_participant(participant)==RC::RETCODE_OK);
        diag::ResidualSnapshot empty;
        if(mode!="snapshots")assert(capture(1,&empty)==0&&empty.counts[0]==0&&empty.counts[1]==0&&empty.counts[2]==0);
    }
    std::cout<<"RESIDUAL_API_TEST mode="<<mode<<" result=PASS no_added_references=1 scope=HOST_REAL_FASTDDS_CONTAINERS_NO_NATIVE\n";
}
