#pragma once
// Fixed four-record oracle for the callback-table ownership experiment only.
#include <array>
#include <cstddef>
#include <cstdint>
namespace mdds_probe {
using CallbackTableRecord = std::array<std::uint8_t, 64>;
inline constexpr std::array<unsigned, 4> callback_record_slots{{1, 1, 2, 1}};
inline CallbackTableRecord MakeCallbackTableRecord(std::uint64_t run, unsigned sequence) {
    CallbackTableRecord record{};
    if (sequence >= callback_record_slots.size()) return record;
    record[0]='C'; record[1]='B'; record[2]='T'; record[3]=1;
    for (unsigned i=0;i<8;++i) record[4+i]=static_cast<std::uint8_t>(run>>(i*8));
    record[12]=static_cast<std::uint8_t>(sequence);
    record[13]=static_cast<std::uint8_t>(callback_record_slots[sequence]);
    record[14]=sequence == 0 ? 0 : 1;
    record[15]=0xa5;
    for (std::size_t i=16;i<record.size();++i)
        record[i]=static_cast<std::uint8_t>((run>>((i%8)*8))+i*7+sequence*31+record[13]*17+record[14]*13);
    return record;
}
inline bool ValidateCallbackTableRecord(const void* data, std::size_t size, std::uint64_t run, unsigned& sequence) {
    if (!data || size != CallbackTableRecord{}.size()) return false;
    const auto* bytes=static_cast<const std::uint8_t*>(data);
    if (bytes[12]>=callback_record_slots.size()) return false;
    const auto expected=MakeCallbackTableRecord(run,bytes[12]);
    for (std::size_t i=0;i<size;++i) if (bytes[i]!=expected[i]) return false;
    sequence=bytes[12];
    return true;
}
struct CallbackTableObservation { unsigned sequence; int socket; unsigned entrance; };
enum class CallbackTableVerdict { Incomplete, SharedClientTable, PerSocketTable, Inconsistent };
inline CallbackTableVerdict ClassifyCallbackTable(const CallbackTableObservation* observations, std::size_t count,
        int first, int second) {
    if (first<=0 || second<=0 || first==second || count>4 || (!observations && count))
        return CallbackTableVerdict::Inconsistent;
    bool shared=true, separate=true;
    for (std::size_t i=0;i<count;++i) {
        const auto& observation=observations[i];
        const int expected_socket=callback_record_slots[i]==1 ? first : second;
        if (observation.sequence!=i || observation.socket!=expected_socket ||
                (observation.entrance!=1 && observation.entrance!=2) || (i==0 && observation.entrance!=1))
            return CallbackTableVerdict::Inconsistent;
        if (i>0 && observation.entrance!=2) shared=false;
        if (observation.entrance!=callback_record_slots[i]) separate=false;
    }
    if (count<4) return CallbackTableVerdict::Incomplete;
    if (shared) return CallbackTableVerdict::SharedClientTable;
    if (separate) return CallbackTableVerdict::PerSocketTable;
    return CallbackTableVerdict::Inconsistent;
}
}
