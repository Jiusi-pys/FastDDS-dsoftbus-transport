#pragma once
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#include <functional>
#include <memory>
namespace fastdds_dsoftbus::dsoftbus::phase0 { class RuntimeProbeAccess; }
namespace fastdds_dsoftbus::dsoftbus { class ProcessReceiveObserver; }

namespace fastdds_dsoftbus::dsoftbus::experimental {
enum class RuntimeLifetime { Participant, PersistentNativeAnchor };
// Opt-in, unqualified Transport assembly. No worker threads are created.
// Keep this object and its descriptor alive through Participant deletion. Pump on
// caller-owned threads during DDS operations. Close seals and waits for entered
// work, then wakes idle workers; join workers before destroying this object.
// All creation/deletion/Close calls belong on an external control thread, never a
// receiver callback. This API gives no SDK drain or cancellation guarantee.
class Runtime final {
public:
    explicit Runtime(phase0::PublicSocketConfiguration);
    // Explicit reusable mode: at most two active or borrowed Participant views,
    // with independent receiver leases on the same live native anchor. There is
    // no cumulative Participant quota within that native incarnation.
    // Deleting the last view leaves native anchors alive. Close refuses while a
    // view exists; after final Close this owner cannot be reopened. SDK drain and
    // native-incarnation reuse remain unproved. The original constructor keeps
    // normal Participant lifetime: at most two independent views, native closes
    // on the last view (including unused borrowed facades), then cannot reopen.
    // Default-off reaccept/node-route experiments retain their exclusive limits.
    Runtime(phase0::PublicSocketConfiguration,RuntimeLifetime);
#if defined(FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_HOST_C_SEAM) || defined(FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST)
    Runtime(phase0::PublicSocketConfiguration,std::shared_ptr<ProcessReceiveObserver>);
#endif
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
    // Normal Participant signs one descriptor; repeated issuance is refused.
    // That same descriptor creates two independent active/pending factory views.
    // PersistentNativeAnchor may issue multiple compatible descriptors, sharing
    // the same two-view bound across their factory invocations.
    // Normal first issuance prepares metadata only; SDK opening waits for input.
    // A facade allocation failure refunds its own unpublished admission. Actual
    // metadata Start failure is terminal. Reaccept/node-route remain one-shot.
    // Incompatible namespace/queue configuration is refused by normal view init.
    // Normal and Persistent assemblies keep one validated native deployment
    // root. Each view binds once, from the real public actual-DID callback, to
    // one of at most two distinct native domain circuits under that root.
    // Explicit expected_domain_id remains a constraint, not an override. With
    // no usable callback the view remains Await and cannot send/receive RTPS.
    // This bounded capability is not unlimited contexts, multi-peer discovery,
    // native close/recreate, or automatic native deployment configuration.
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>
    MakeDescriptor(const DSoftBusTransportDescriptor&);
    // Explicit stock assembly: validate the native identity's stored primitive
    // domain and require the later actual Fast DDS domain callback per view.
    // Missing callbacks remain Await; this does not select another native domain.
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>
    MakeStockDescriptor(const DSoftBusTransportDescriptor&);
    NativeSendRunResult PumpSend() noexcept;
    phase0::BackendStatus PumpReceive() noexcept;
    // Caller-owned progress thread: snapshot, drain BOTH TX/RX to no work, then
    // wait with that snapshot. Coalesced notifications are not queue credits.
    // Wait sleeps until work, a real queued RX expiry, stop, or natural shutdown.
    // Do not wait or request control from a receiver/SDK callback. Retain Runtime
    // throughout waits; join workers before destruction. No DDS lifecycle calls
    // occur here. Stop only wakes workers; it does not close sockets/Participants.
    // The default-off bounded reaccept/node-route experiments have additional
    // control work: Wait refuses these configurations immediately with
    // UnsupportedConfiguration. A caller must handle that terminal result; it
    // must not repeatedly call Wait or treat it as reconnect capability.
    std::uint64_t WorkGeneration() const noexcept;
    WorkWaitResult WaitForWork(std::uint64_t& generation);
    void RequestPumpStop() noexcept;
    bool Close() noexcept;
    phase0::PublicSocketObservation ObserveSocket() const;
private:
    friend class phase0::RuntimeProbeAccess;
    struct Impl;
    struct Descriptor;
    std::shared_ptr<Impl> impl_;
};
}
