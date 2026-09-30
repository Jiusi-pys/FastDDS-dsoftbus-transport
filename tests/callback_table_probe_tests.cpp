#include "softbus_callback_table_oracle.hpp"
#include <cstdio>
#include <stdexcept>
using namespace fastdds_dsoftbus_probe;
namespace { void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); } }
int main() {
    try {
        const std::uint64_t run = 0x0123456789abcdefULL;
        for (unsigned seq = 0; seq != 4; ++seq) {
            auto record = MakeCallbackTableRecord(run, seq);
            unsigned decoded = 99;
            Check(ValidateCallbackTableRecord(record.data(), record.size(), run, decoded) && decoded == seq,
                    "exact per-record identity and content");
            Check(!ValidateCallbackTableRecord(record.data(), record.size()-1, run, decoded), "short record rejected");
            Check(!ValidateCallbackTableRecord(record.data(), record.size(), run+1, decoded), "wrong run rejected");
            for (std::size_t offset = 0; offset != record.size(); ++offset) {
                record[offset] ^= 1;
                Check(!ValidateCallbackTableRecord(record.data(), record.size(), run, decoded), "every corrupted byte rejected");
                record[offset] ^= 1;
            }
            auto adjacent = MakeCallbackTableRecord(run, (seq+1)%4);
            for (std::size_t offset = 32; offset < record.size(); ++offset) record[offset] = adjacent[offset];
            Check(!ValidateCallbackTableRecord(record.data(), record.size(), run, decoded), "cross-record splice rejected");
        }
        unsigned decoded = 99;
        Check(!ValidateCallbackTableRecord(nullptr, 64, run, decoded), "null record rejected");
        CallbackTableObservation shared[]{{0,11,1},{1,11,2},{2,12,2},{3,11,2}};
        CallbackTableObservation separate[]{{0,11,1},{1,11,1},{2,12,2},{3,11,1}};
        Check(ClassifyCallbackTable(shared,4,11,12)==CallbackTableVerdict::SharedClientTable,"shared model recognized");
        Check(ClassifyCallbackTable(separate,4,11,12)==CallbackTableVerdict::PerSocketTable,"independent model recognized");
        Check(ClassifyCallbackTable(shared,3,11,12)==CallbackTableVerdict::Incomplete,"missing record not model proof");
        Check(ClassifyCallbackTable(shared,4,11,11)==CallbackTableVerdict::Inconsistent,"aliased sockets rejected");
        shared[0].entrance=2;
        Check(ClassifyCallbackTable(shared,4,11,12)==CallbackTableVerdict::Inconsistent,"baseline must be F1");
        shared[0].entrance=1; shared[3].sequence=2;
        Check(ClassifyCallbackTable(shared,4,11,12)==CallbackTableVerdict::Inconsistent,"duplicate sequence rejected");
        shared[3]={3,12,2};
        Check(ClassifyCallbackTable(shared,4,11,12)==CallbackTableVerdict::Inconsistent,"wrong socket rejected");
        shared[3]={3,11,1};
        Check(ClassifyCallbackTable(shared,4,11,12)==CallbackTableVerdict::Inconsistent,"mixed entry observations remain unresolved");
        std::puts("callback_table_oracle PASS record_integrity=ALL_BYTES models=SHARED_AND_SEPARATE negative_cases=PASS");
        return 0;
    } catch (const std::exception& error) { std::printf("FAIL %s\n",error.what()); return 1; }
}
