#include "ScenarioChecks.hpp"
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static bool Set(const char* name, const char* value) {
#ifdef _WIN32
    return _putenv_s(name,value)==0;
#else
    return setenv(name,value,1)==0;
#endif
}
int main(int argc,char** argv) {
    if(argc!=2 || (std::strcmp(argv[1],"skip") && std::strcmp(argv[1],"control"))) return 64;
    const bool skip=std::strcmp(argv[1],"skip")==0;
    for(const char* name : {"FASTDDS_DEFAULT_PROFILES_FILE","FASTRTPS_DEFAULT_PROFILES_FILE","FASTDDS_ENVIRONMENT_FILE",
            "SKIP_DEFAULT_XML_FILE","SKIP_DEFAULT_XML"}) if(!Set(name,"")) return 2;
if(!Set(fastdds_dsoftbus::experiment::skip_default_xml_variable,skip?"1":"0")) return 2;
    eprosima::fastdds::dds::DomainParticipantQos qos;
    auto* factory=eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    // Public entry used by create_participant; get_default_qos alone does not
    // trigger default XML loading in Fast DDS 3.6.2.
    factory->load_profiles();
    const auto rc=factory->get_default_participant_qos(qos);
    if(rc!=eprosima::fastdds::dds::RETCODE_OK) return 3;
    const bool loaded=qos.name()=="mdds-xml-control";
    std::printf("xml_control mode=%s variable=%s fixture_loaded=%d result=%s participant_created=0\n",
argv[1],fastdds_dsoftbus::experiment::skip_default_xml_variable,loaded,loaded!=skip?"PASS":"FAIL");
    return loaded!=skip?0:1;
}
