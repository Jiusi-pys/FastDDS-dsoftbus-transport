#pragma once
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#include <functional>
#include <memory>

namespace fastdds_dsoftbus::dsoftbus::experimental {
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
    // Opt-in, one-shot coordinator for the existing isolated assembly hook. It
    // runs exactly once inside this runtime's transport creation, after this
    // side's listener is published and before the outgoing native connect, so a
    // caller can wait for a peer's listener without retrying the connect. The
    // callable receives no socket, record, owner or lease handle and must not
    // start a second lifecycle; returning false fails the connect. Leaving it
    // unset keeps the previous unconditional continue. Set it before
    // MakeDescriptor; a later call is rejected and changes nothing. The gate is
    // owned by this Runtime; another Runtime's gate is independent. A replacement
    // before descriptor issuance affects only this Runtime. Pass an empty
    // function to clear its gate. The callable is retained by value throughout
    // its synchronous invocation. Returns whether the requested state was applied.
    bool SetPreConnectGate(std::function<bool()>);
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
