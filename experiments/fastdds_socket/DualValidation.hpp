#pragma once
#include "DualScenarioChecks.hpp"
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <cstring>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
namespace fastdds_dsoftbus::experiment {
enum class EndpointKind { Participant, Writer, Reader };
enum class DiscoveryChange { Discovered, Updated, Removed };
// Explicit identity fixture of this dedicated two-board program. The full run
// value is retained in the first eight bytes, with a board discriminator. The
// first 4/8 byte DDS host/process comparisons are fixture groupings, not proof
// of the actual OS host/process. Actual pid/deployment records supply that proof.
inline EndpointIdentity DualIdentity(const std::string& run,bool board_a,unsigned instance,EndpointKind kind) {
    EndpointIdentity out{};
    if(run.size()!=16 || instance<1 || instance>2) return out;
    const auto nibble=[](char c)->unsigned{return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);};
    for(unsigned i=0;i<8;++i) out[i]=static_cast<std::uint8_t>((nibble(run[2*i])<<4)|nibble(run[2*i+1]));
    out[0]^=board_a?0xa1:0xb2;out[8]=0x44;out[9]=0x55;out[10]=0x41;out[11]=static_cast<std::uint8_t>(instance);
    if(kind==EndpointKind::Participant) {out[14]=1;out[15]=0xc1;}
    else {out[14]=kind==EndpointKind::Writer?1:2;out[15]=kind==EndpointKind::Writer?3:4;}
    return out;
}
inline EndpointIdentity ParticipantIdentity(EndpointIdentity endpoint) {
    endpoint[12]=endpoint[13]=0;endpoint[14]=1;endpoint[15]=0xc1;return endpoint;
}
inline EndpointIdentity DualBytes(const eprosima::fastdds::rtps::GUID_t& guid) {
    EndpointIdentity out;std::memcpy(out.data(),guid.guidPrefix.value,12);std::memcpy(out.data()+12,guid.entityId.value,4);return out;
}
inline eprosima::fastdds::rtps::GUID_t DualGuid(const EndpointIdentity& identity) {
    eprosima::fastdds::rtps::GUID_t out;std::memcpy(out.guidPrefix.value,identity.data(),12);std::memcpy(out.entityId.value,identity.data()+12,4);return out;
}
inline void ApplyDualIdentityFixture(eprosima::fastdds::dds::DomainParticipantQos& qos,const std::string& run,bool board_a,unsigned instance) {
    qos.wire_protocol().prefix=DualGuid(DualIdentity(run,board_a,instance,EndpointKind::Participant)).guidPrefix;
}
struct DiscoveryEndpoint {EndpointIdentity participant{};EndpointKind kind=EndpointKind::Writer;std::string topic,type;};
struct DiscoverySnapshot {std::uint64_t raw_events=0;std::map<EndpointIdentity,DiscoveryEndpoint> active;};
// Program-only readiness state: one control thread validates and sends; the
// DDS callback can publish peer readiness before or after that point.
class DualReadyState {
public:
    void Peer(){peer_=true;}
    void Validate(){validated_=true;}
    bool CanSend()const{return validated_ && !sent_;}
    bool MarkSent(){if(!validated_) return false;sent_=true;return true;}
    bool Sent()const{return sent_;}
    bool Complete()const{return validated_ && sent_ && peer_;}
private:
    std::atomic<bool> peer_{false};bool validated_=false,sent_=false;
};
class DualDiscovery {
public:
    void Writer(eprosima::fastdds::rtps::WriterDiscoveryStatus change,const eprosima::fastdds::dds::PublicationBuiltinTopicData& info) {
        using S=eprosima::fastdds::rtps::WriterDiscoveryStatus;
        const auto guid=DualBytes(info.guid);
        Endpoint(change==S::REMOVED_WRITER?DiscoveryChange::Removed:change==S::DISCOVERED_WRITER?DiscoveryChange::Discovered:DiscoveryChange::Updated,
            guid,ParticipantIdentity(guid),EndpointKind::Writer,info.topic_name.c_str(),info.type_name.c_str());
    }
    void Reader(eprosima::fastdds::rtps::ReaderDiscoveryStatus change,const eprosima::fastdds::dds::SubscriptionBuiltinTopicData& info) {
        using S=eprosima::fastdds::rtps::ReaderDiscoveryStatus;
        const auto guid=DualBytes(info.guid);
        Endpoint(change==S::REMOVED_READER?DiscoveryChange::Removed:change==S::DISCOVERED_READER?DiscoveryChange::Discovered:DiscoveryChange::Updated,
            guid,ParticipantIdentity(guid),EndpointKind::Reader,info.topic_name.c_str(),info.type_name.c_str());
    }
    void Endpoint(DiscoveryChange change,const EndpointIdentity& guid,const EndpointIdentity& participant,
            EndpointKind kind,const std::string& topic,const std::string& type) {
        std::lock_guard<std::mutex> lock(mutex_);++state_.raw_events;
        if(change==DiscoveryChange::Removed) state_.active.erase(guid);
        else state_.active[guid]={participant,kind,topic,type};
    }
    void RemoveParticipant(const EndpointIdentity& participant) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto i=state_.active.begin();i!=state_.active.end();) {
            if(i->second.participant==participant) i=state_.active.erase(i);else ++i;
        }
    }
    DiscoverySnapshot Snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
    bool Validate(unsigned,unsigned,unsigned,const EndpointIdentity& writer,
            const EndpointIdentity& reader,const std::string& topic,const std::string& type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto participant=ParticipantIdentity(writer);
        if(writer==reader || participant!=ParticipantIdentity(reader)) return false;
        const auto expected=[&](const EndpointIdentity& guid,EndpointKind kind) {
            const auto found=state_.active.find(guid);
            return found!=state_.active.end() && found->second.participant==participant &&
                found->second.kind==kind && found->second.topic==topic && found->second.type==type;
        };
        return expected(writer,EndpointKind::Writer) && expected(reader,EndpointKind::Reader);
    }
private:
    mutable std::mutex mutex_;DiscoverySnapshot state_;
};
}
