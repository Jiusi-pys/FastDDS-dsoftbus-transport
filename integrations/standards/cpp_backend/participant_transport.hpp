#pragma once
// Application-owned public ParticipantQos customization. An empty scope keeps
// the original explicit UDPv4 profile. No DDS/RTPS semantics live here.
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#if __has_include(<fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#endif
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
namespace standards_transport {
using Configure=std::function<void(unsigned,eprosima::fastdds::dds::DomainParticipantQos&)>;
// Participant creation in these fixtures is confined to the owning main thread.
// A scope must outlive all of its participants; worker threads inherit nothing.
inline thread_local Configure current;
class Scope {
public:
    explicit Scope(Configure configure) {
        if(current||!configure)throw std::runtime_error("participant transport scope invalid/nested");
        current=std::move(configure);
    }
    ~Scope(){current={};}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline void configure(unsigned domain,eprosima::fastdds::dds::DomainParticipantQos& qos){
    if(current){current(domain,qos);return;}
    qos.transport().use_builtin_transports=false;
    qos.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
}
}
