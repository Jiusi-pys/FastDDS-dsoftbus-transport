#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <algorithm>
namespace mdds::experiment {
using EndpointIdentity = std::array<std::uint8_t,16>;
inline bool DualRemoteEndpoint(bool qos_valid,const EndpointIdentity& local,const EndpointIdentity& remote,bool same_process) noexcept {
    return qos_valid && !same_process && local!=remote &&
        std::any_of(local.begin(),local.begin()+12,[](std::uint8_t byte){ return byte!=0; }) &&
        std::any_of(remote.begin(),remote.begin()+12,[](std::uint8_t byte){ return byte!=0; }) &&
        !std::equal(local.begin(),local.begin()+12,remote.begin());
}
inline bool DualRemotePair(bool qos_valid,const EndpointIdentity& local,const EndpointIdentity& remote_writer,
        const EndpointIdentity& remote_reader,bool writer_same_process,bool reader_same_process) noexcept {
    return DualRemoteEndpoint(qos_valid,local,remote_writer,writer_same_process) &&
        DualRemoteEndpoint(qos_valid,local,remote_reader,reader_same_process) && remote_writer!=remote_reader &&
        std::equal(remote_writer.begin(),remote_writer.begin()+12,remote_reader.begin());
}
// Prerequisites for the fixed, two-Participant engineering experiment only.
// Peer sample 5 is ordinary DDS test data, sent after that peer's first
// Participant deletion returned; it is not an adapter control protocol.
inline bool ValidDualOptions(bool dual,std::size_t sample_bytes,bool controlled_loss) noexcept {
    return !dual || (sample_bytes==0 && !controlled_loss);
}
inline unsigned DualSlot(bool board_a,unsigned instance) noexcept {
    return instance>=1 && instance<=2 ? 2*instance-(board_a?1:0) : 0;
}
struct DualProgress {
    unsigned first_written=0,first_received=0,second_written=0,second_received=0;
    unsigned overlap_rounds=0,after_both_delete_writes=0,invalid=0;
    bool first_matching=false,second_matching=false,first_ack=false,second_ack=false;
    bool first_deleted=false,second_identity_unchanged=false,second_deleted=false,process_budget_settled=false;
};
inline bool CanCreateSecond(const DualProgress& p) noexcept {
    return p.first_matching && p.first_written>=2 && (p.first_received&3)==3 && !p.invalid;
}
inline bool CanDeleteFirst(const DualProgress& p) noexcept {
    return CanCreateSecond(p) && p.second_matching && p.first_written==6 && p.first_received==0x3f &&
        p.second_written>=4 && (p.second_received&0xf)==0xf && p.overlap_rounds==4 && p.first_ack && p.second_ack;
}
inline bool CanPublishAfterBothDeletes(const DualProgress& p) noexcept {
    return CanDeleteFirst(p) && p.first_deleted && p.second_identity_unchanged && (p.second_received&0x10) && !p.invalid;
}
inline bool DualScenarioComplete(const DualProgress& p) noexcept {
    return CanPublishAfterBothDeletes(p) && p.second_written==10 && p.second_received==0x3ff &&
        p.after_both_delete_writes==5 && p.second_deleted && p.process_budget_settled;
}
}
