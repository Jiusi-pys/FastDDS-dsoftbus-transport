#define NOMINMAX
#include <windows.h>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=3&&argc!=4)return 2;
    const bool disabled=argc==4&&std::string(argv[3])=="autoenable-disabled";
    if(argc==4&&!disabled)return 2;
    const auto library=std::filesystem::absolute(argv[1]);
    const auto config=std::filesystem::absolute(argv[2]);
    auto module=LoadLibraryA(library.string().c_str());if(!module)return 3;
    using Count=unsigned(*)();
    auto closes=reinterpret_cast<Count>(GetProcAddress(module,"ProbeCloseCount"));
    auto notices=reinterpret_cast<Count>(GetProcAddress(module,"ProbePublicDeleteCount"));
    auto factories=reinterpret_cast<Count>(GetProcAddress(module,"ProbeTransportFactoryCount"));
    if(!closes||!notices||!factories)return 4;
    {std::ofstream out(config);out<<"bridge_library="<<library.generic_string()<<"\n";if(!out)return 5;}
    if(_putenv_s("FASTDDS_DSOFTBUS_REQUIRED_CONFIG",config.string().c_str()))return 6;
    auto* factory=eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    if(disabled) {
        eprosima::fastdds::dds::DomainParticipantFactoryQos qos;
        factory->get_qos(qos);qos.entity_factory().autoenable_created_entities=false;
        if(factory->set_qos(qos)!=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK)return 7;
    }
    auto* participant=factory->create_participant(201,eprosima::fastdds::dds::PARTICIPANT_QOS_DEFAULT);
    if(disabled) {
        if(!participant)return 8;
        const auto deleted=factory->delete_participant(participant);
        const bool pass=deleted==eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK&&factories()==0&&closes()==1&&notices()==1;
        std::cout<<"FACTORY_DISABLED result="<<(pass?"PASS":"FAIL")<<" factory_calls="<<factories()<<" closes="<<closes()<<" public_delete_notices="<<notices()<<'\n';
        FreeLibrary(module);return pass?0:1;
    }
    const bool pass=participant==nullptr&&factories()==1&&closes()==1&&notices()==0;
    std::cout<<"FACTORY_CONSTRUCTION result="<<(pass?"PASS":"FAIL")
             <<" participant_null="<<(participant==nullptr)<<" factories="<<factories()
             <<" closes="<<closes()<<" public_delete_notices="<<notices()
             <<" scope=REAL_FASTDDS_FACTORY_INJECTED_TRANSPORT_FAILURE_NOT_RUNTIME_OR_DEVICE\n";
    if(participant)factory->delete_participant(participant);
    FreeLibrary(module);return pass?0:1;
}
