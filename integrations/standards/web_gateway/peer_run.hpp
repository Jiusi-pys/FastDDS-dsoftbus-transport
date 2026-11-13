#pragma once
#include <functional>
#include <iostream>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
struct PeerTransport {
    std::function<void(unsigned,eprosima::fastdds::dds::DomainParticipantQos&)> configure;
    std::function<bool()> healthy;
};
int run_peer(int,char**,const PeerTransport&,std::ostream&,std::ostream&);
