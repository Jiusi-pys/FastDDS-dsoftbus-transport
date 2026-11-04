#pragma once
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <functional>

namespace fastdds_dsoftbus::ros2_integration {
// Thin assembly holder for the existing Runtime. No queue or protocol state.
struct BridgeOwner {
    std::unique_ptr<dsoftbus::experimental::Runtime> runtime;
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> descriptor;
    std::atomic<bool> stop{false}, failed{false};
    std::optional<dsoftbus::NativeSendRunResult> first_send_failure; // pump only, read after join
    std::thread pump;
    std::string run;
    unsigned side = 0;
    bool public_delete_completed = false;
    std::function<void()> diagnostic_control;
    std::function<void()> node_diagnostic_close; // observation only, after local Close
    std::function<bool()> before_pump; // explicit Host scheduling fixture only

    void StartPump() {
        pump = std::thread([this] {
            while (!stop.load()) {
                if(before_pump){try{if(!before_pump()){std::this_thread::yield();continue;}}catch(...){failed=true;}}
                const auto send = runtime->PumpSend();
                if (send.outcome == dsoftbus::NativeSendOutcome::RefusedDeadline ||
                        send.outcome == dsoftbus::NativeSendOutcome::ReturnedInvalidCode ||
                        send.outcome == dsoftbus::NativeSendOutcome::ReturnedByException ||
                        (send.actual_return_code && *send.actual_return_code != 0)) {
                    failed = true;
                    if(!first_send_failure) {
                        first_send_failure=send;
                        const auto owner=send.original_owner.value_or(dsoftbus::NativeOwnerObservation{});
                        std::fprintf(stderr,"FDDST_PUMP_SEND_FAILURE outcome=%d status=%d result_present=%d original_owner_known=%d original_owner_slot=%llu original_owner_incarnation=%llu original_generation=%llu original_route_version=%llu close_intent=%d provider_fault=%d source=ORIGINAL_TICKET sdk_epoch=NOT_PROVEN\n",
                            int(send.outcome),int(send.status),int(bool(send.actual_return_code)),int(bool(send.original_owner)),
                            static_cast<unsigned long long>(owner.slot),static_cast<unsigned long long>(owner.incarnation),
                            static_cast<unsigned long long>(owner.generation),static_cast<unsigned long long>(owner.route_version),
                            int(owner.close_intent),int(owner.provider_fault));
                    }
                }
                const auto receive = runtime->PumpReceive();
                if (receive != dsoftbus::phase0::BackendStatus::None &&
                        receive != dsoftbus::phase0::BackendStatus::NoWork &&
                        receive != dsoftbus::phase0::BackendStatus::InvalidState) failed = true;
                // All provider/pump calls have returned. This optional control
                // exporter neither enters SDK callbacks nor observes/closes Runtime.
                if(diagnostic_control){try{diagnostic_control();}catch(...){/* primary state retained */}}
                std::this_thread::yield();
            }
            if(diagnostic_control){try{diagnostic_control();}catch(...){}}
        });
    }
    // Invoke only after successful public DDS deletion. Incomplete close retains
    // this holder through the factory lease; no detach or early object release.
    int CloseLocal() {
        stop = true;
        if (pump.joinable()) pump.join();
        const bool closed=runtime&&runtime->Close();
        if(node_diagnostic_close){try{node_diagnostic_close();}catch(...){failed=true;}}
        if (!closed) return 1;
        return failed.load() ? 2 : 0;
    }
};
}
