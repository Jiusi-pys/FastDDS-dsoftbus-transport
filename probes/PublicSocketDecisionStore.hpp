#pragma once
#include <mdds/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
#include <array>
#include <mutex>

namespace mdds::dsoftbus::dds_experiment {
// Append-only experiment observations. No payload, authority, routing or queue.
template<std::size_t Capacity> class DecisionStore {
public:
    static constexpr std::size_t branches=static_cast<std::size_t>(ReceiveDecisionBranch::Count);
    static constexpr std::size_t reasons=static_cast<std::size_t>(RxAdmissionReason::Count);
    struct Row {ReceiveDecision decision;std::uint64_t ingress=0;unsigned source=0;};
    struct Snapshot {
        std::size_t count=0,total=0,overflow=0,unknown=0,omitted=0;
        std::array<std::size_t,branches> branch_totals{};
        std::array<std::size_t,reasons> bridge_totals{};
    };
    void Record(const ReceiveDecision& d,std::uint64_t ingress,unsigned source=0,bool detailed=true) noexcept {
        std::lock_guard<std::mutex> lock(gate_);
        ++snapshot_.total;
        const auto b=static_cast<std::size_t>(d.branch),r=static_cast<std::size_t>(d.bridge.reason);
        if(b<branches)++snapshot_.branch_totals[b];else ++snapshot_.unknown;
        if(d.bridge_called){if(r<reasons)++snapshot_.bridge_totals[r];else ++snapshot_.unknown;}
        // Only successful packet detail may be omitted. Every refusal/unknown
        // decision still consumes a required row and can overflow visibly.
        if(!detailed&&!d.refused_receive&&d.status_known&&d.status==0&&
                (d.branch==ReceiveDecisionBranch::Forwarded||
                 (d.branch==ReceiveDecisionBranch::BridgeAccepted&&d.bridge_called&&
                  d.bridge.reason==RxAdmissionReason::AcceptedQueue))){++snapshot_.omitted;return;}
        if(snapshot_.count<Capacity)rows_[snapshot_.count++]={d,ingress,source};else ++snapshot_.overflow;
    }
    Snapshot Capture() const noexcept {std::lock_guard<std::mutex> lock(gate_);return snapshot_;}
    // Caller uses only indices below a captured count; published rows never change.
    const Row& At(std::size_t index) const noexcept {return rows_[index];}
private:
    mutable std::mutex gate_;
    std::array<Row,Capacity> rows_{};
    Snapshot snapshot_;
};
}
