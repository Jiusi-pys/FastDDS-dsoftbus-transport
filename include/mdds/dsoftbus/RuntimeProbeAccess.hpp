#pragma once
#ifndef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
#error "RuntimeProbeAccess is exclusive to the opt-in probe link variant"
#endif
#include <mdds/dsoftbus/ProviderAdapter.hpp>
#include <functional>
namespace mdds::dsoftbus::phase0 {
// Experiment assembly permission only. Not a platform capability or factory.
class RuntimeProbeAccess final {
public:
    static AdapterBuildResult Build(ProviderAdapterDomain&,ServiceRecordTable&,NativeOwnerTable&,
            ApplicationAttachments&,std::shared_ptr<NativeBackend>,const BindingRequest&);
    static std::shared_ptr<NativeSendProvider> SendBridge(ProviderAdapter&,NativeOwnerTable&);
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
