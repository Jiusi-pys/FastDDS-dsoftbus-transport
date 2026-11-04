#pragma once
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
namespace fastdds_dsoftbus::experiment {
inline constexpr char ignore_local_endpoints_property[]="fastdds.ignore_local_endpoints";
inline void ApplyDualParticipantPolicy(eprosima::fastdds::dds::DomainParticipantQos& qos) {
    // 3.6.2 EDP reads this public property at Participant construction. It
    // suppresses that Participant's own Writer/Reader pairing; INTRAPROCESS_OFF
    // alone only controls delivery and does not suppress the match.
    qos.properties().properties().emplace_back(ignore_local_endpoints_property,"true");
    // Separate public PDP filter excludes other Participants of this process.
    // Do not filter different hosts: the fixed peer is on the other board.
    qos.wire_protocol().builtin.discovery_config.ignoreParticipantFlags=
        eprosima::fastdds::rtps::ParticipantFilteringFlags::FILTER_SAME_PROCESS;
}
inline bool DualParticipantPolicyRequested(const eprosima::fastdds::dds::DomainParticipantQos& qos) {
    unsigned count=0;
    for(const auto& property:qos.properties().properties()) {
        if(property.name()==ignore_local_endpoints_property) {
            if(property.value()!="true") return false;
            ++count;
        }
    }
    return count==1 && qos.wire_protocol().builtin.discovery_config.ignoreParticipantFlags==
        eprosima::fastdds::rtps::ParticipantFilteringFlags::FILTER_SAME_PROCESS;
}
}
