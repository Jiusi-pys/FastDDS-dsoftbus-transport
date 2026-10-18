#pragma once
#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>

namespace fastdds_dsoftbus::ros2_integration {
// Read-only predicates for the explicitly opted-in, single process restart
// experiment. These facts never authorize a native call or establish SDK drain.
struct ProcessRestartObservation {
    int dial=-1, accepted=-1;
    std::uint64_t dial_owner=0, accepted_owner=0;

    static bool Settled(const dsoftbus::NativeOwnerObservation& value,
            int id, std::uint64_t incarnation) noexcept {
        return id>=0 && incarnation && value.incarnation==incarnation &&
            value.owned_id==id && value.close_intent &&
            (value.shutdown_returned || value.on_shutdown_seen) &&
            !value.callback_inflight && !value.receive_inflight &&
            !value.entered_context_lost && value.failure==dsoftbus::NativeStatus::None &&
            value.provider_fault==dsoftbus::NativeProviderFault::None;
    }
    bool OldBothReady(const dsoftbus::phase0::AdapterObservation& view) const noexcept {
        return view.provider_status==dsoftbus::phase0::BackendStatus::None &&
            !view.listener_stopped && view.accepted_phase==dsoftbus::phase0::AcceptedPhase::Empty &&
            Settled(view.retired_outgoing,dial,dial_owner) &&
            Settled(view.retired_accepted,accepted,accepted_owner);
    }
    bool NewBothActive(const dsoftbus::phase0::AdapterObservation& view) const noexcept {
        return view.provider_status==dsoftbus::phase0::BackendStatus::None &&
            !view.listener_stopped && !view.outgoing_stopped &&
            view.accepted_phase==dsoftbus::phase0::AcceptedPhase::Active &&
            view.binding.socket>=0 && view.accepted.socket>=0 &&
            view.binding.socket!=view.accepted.socket &&
            view.binding.socket!=dial && view.binding.socket!=accepted &&
            view.accepted.socket!=dial && view.accepted.socket!=accepted &&
            view.binding.owner_incarnation && view.accepted.owner_incarnation &&
            view.binding.owner_incarnation!=view.accepted.owner_incarnation &&
            view.binding.owner_incarnation!=dial_owner && view.binding.owner_incarnation!=accepted_owner &&
            view.accepted.owner_incarnation!=dial_owner && view.accepted.owner_incarnation!=accepted_owner &&
            Settled(view.retired_outgoing,dial,dial_owner) &&
            Settled(view.retired_accepted,accepted,accepted_owner);
    }
};
}
