#pragma once
#include <mdds/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <mdds/dsoftbus/PublicSocketBackend.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <memory>

namespace mdds::dsoftbus::experimental {
// Opt-in, unqualified, one Participant per runtime. No worker threads are created.
// Keep this object and its descriptor alive through Participant deletion. Pump on
// caller-owned threads during DDS operations; join them before Close/destruction.
// All creation/deletion/Close calls belong on an external control thread, never a
// receiver callback. This API gives no SDK drain or cancellation guarantee.
class Runtime final {
public:
    explicit Runtime(phase0::PublicSocketConfiguration);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    // Both descriptor issuance and actual transport factory invocation are one-shot.
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>
    MakeDescriptor(const DSoftBusTransportDescriptor&);
    NativeSendRunResult PumpSend() noexcept;
    phase0::BackendStatus PumpReceive() noexcept;
    bool Close() noexcept;
    phase0::PublicSocketObservation ObserveSocket() const;
private:
    struct Impl;
    struct Descriptor;
    std::shared_ptr<Impl> impl_;
};
}
