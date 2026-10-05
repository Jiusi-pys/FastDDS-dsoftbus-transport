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
    std::thread pump;
    std::string run;
    unsigned side = 0;
    bool public_delete_completed = false;
    std::function<void()> diagnostic_control;
    std::function<bool()> before_pump; // explicit Host scheduling fixture only

    void StartPump() {
        pump = std::thread([this] {
            while (!stop.load()) {
                if(before_pump){try{if(!before_pump()){std::this_thread::yield();continue;}}catch(...){failed=true;}}
                const auto send = runtime->PumpSend();
                if (send.outcome == dsoftbus::NativeSendOutcome::ReturnedInvalidCode ||
                        send.outcome == dsoftbus::NativeSendOutcome::ReturnedByException ||
                        (send.actual_return_code && *send.actual_return_code != 0)) failed = true;
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
        if (!runtime || !runtime->Close()) return 1;
        return failed.load() ? 2 : 0;
    }
};
}
