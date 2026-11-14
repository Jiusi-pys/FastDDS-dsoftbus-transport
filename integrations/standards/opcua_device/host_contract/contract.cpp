#include "../../cpp_backend/participant_transport.hpp"
#include <thread>
using Qos=eprosima::fastdds::dds::DomainParticipantQos;
static void require(bool ok){if(!ok)throw std::runtime_error("D5 participant transport contract");}
int main(){
    Qos first;standards_transport::configure(219,first);
    require(!first.transport().use_builtin_transports&&first.transport().user_transports.size()==1);
    require(bool(std::dynamic_pointer_cast<eprosima::fastdds::rtps::UDPv4TransportDescriptor>(first.transport().user_transports[0])));
    unsigned calls=0;bool rejected=false,isolated=false;
    {
        standards_transport::Scope owner([&](unsigned domain,Qos& qos){require(domain==223);++calls;qos.name("custom");});
        Qos selected;standards_transport::configure(223,selected);require(selected.name()=="custom"&&selected.transport().user_transports.empty()&&calls==1);
        try{standards_transport::Scope nested([](unsigned,Qos&){});}catch(const std::runtime_error&){rejected=true;}
        std::thread worker([&]{Qos q;standards_transport::configure(219,q);isolated=q.transport().user_transports.size()==1;});worker.join();
        require(rejected&&isolated&&calls==1);
    }
    Qos restored;standards_transport::configure(219,restored);require(restored.transport().user_transports.size()==1);
    bool thrown=false;
    {standards_transport::Scope failing([](unsigned,Qos&){throw std::runtime_error("failure");});try{Qos q;standards_transport::configure(219,q);}catch(const std::runtime_error&){thrown=true;}}
    require(thrown&&!standards_transport::current);return 0;
}
