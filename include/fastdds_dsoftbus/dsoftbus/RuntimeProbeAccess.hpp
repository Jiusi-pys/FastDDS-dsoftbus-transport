#pragma once
#ifndef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
#error "RuntimeProbeAccess is exclusive to the opt-in probe link variant"
#endif
#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>
#include <functional>
#include <fastdds_dsoftbus/dsoftbus/HostRuntimeShutdownPoint.hpp>
namespace fastdds_dsoftbus::dsoftbus::experimental { class Runtime; }
namespace fastdds_dsoftbus::dsoftbus::host_test { struct HostLoopObservation; struct DomainCircuitObservations; }
namespace fastdds_dsoftbus::dsoftbus::phase0 {
struct BoundedConnectionSnapshot {
    AdapterObservation adapter;
    NativeUsageSnapshot native;
    bool started=false,sealed=false;
};
// Experiment assembly permission only. Not a platform capability or factory.
class RuntimeProbeAccess final {
public:
    static AdapterBuildResult Build(ProviderAdapterDomain&,ServiceRecordTable&,NativeOwnerTable&,
            ApplicationAttachments&,std::shared_ptr<NativeBackend>,const BindingRequest&);
    static std::shared_ptr<NativeSendProvider> SendBridge(ProviderAdapter&,NativeOwnerTable&);
    // Private assembly, same single control-thread setup contract as descriptor
    // issuance. No installed public entry and no authority over SDK drain.
    static bool BeforeNativeClose(experimental::Runtime&,
            std::function<void(const host_test::RuntimeShutdownPoint&)>);
    static bool BoundedReplacementGate(experimental::Runtime&,std::function<bool()>);
    static BackendStatus RequestNormalEnd(experimental::Runtime&,RecordRole) noexcept;
    static BoundedConnectionSnapshot ObserveConnection(const experimental::Runtime&) noexcept;
    static host_test::HostLoopObservation ObserveRuntime(const experimental::Runtime&) noexcept;
    static host_test::DomainCircuitObservations ObserveDomainCircuits(const experimental::Runtime&) noexcept;
    static NativeRouteSnapshot ObserveRoute(const experimental::Runtime&) noexcept;
    static void SealBoundedAdmission(experimental::Runtime&) noexcept;
};
// Private opt-in scheduling seam. Scope-owned callable and thread-local
// restoration; only the arming thread's successful domain input attachment
// invokes it. No native capability, installed API, or production setter.
class ArmedDomainInputPublicationGate final {
public:
    explicit ArmedDomainInputPublicationGate(std::function<void()>) noexcept;
    ~ArmedDomainInputPublicationGate();
    ArmedDomainInputPublicationGate(const ArmedDomainInputPublicationGate&) = delete;
    ArmedDomainInputPublicationGate& operator=(const ArmedDomainInputPublicationGate&) = delete;
private:
    std::function<void()> gate_;
    const std::function<void()>* previous_;
};
bool RuntimeProbeBeforeConnect() noexcept;
bool InvokeArmedPreConnectGate() noexcept;
// Assembly seam, never installed and not a public API. It arms the owning
// Runtime's coordination gate for exactly the Initialize() call executed on the
// arming thread, so a gate can never reach a transport created by another
// Runtime. The callable is held by value for that whole scope.
class ArmedPreConnectGate final
{
public:
    explicit ArmedPreConnectGate(std::function<bool()>) noexcept;
    ~ArmedPreConnectGate();
    ArmedPreConnectGate(const ArmedPreConnectGate&) = delete;
    ArmedPreConnectGate& operator=(const ArmedPreConnectGate&) = delete;
private:
    std::function<bool()> gate_;
    const std::function<bool()>* previous_;
};
}
