#pragma once
#ifndef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
#error "RuntimeProbeAccess is exclusive to the opt-in probe link variant"
#endif
#include <mdds/dsoftbus/ProviderAdapter.hpp>
namespace mdds::dsoftbus::phase0 {
// Experiment assembly permission only. Not a platform capability or factory.
class RuntimeProbeAccess final {
public:
    static AdapterBuildResult Build(ProviderAdapterDomain&,ServiceRecordTable&,NativeOwnerTable&,
            ApplicationAttachments&,std::shared_ptr<NativeBackend>,const BindingRequest&);
    static std::shared_ptr<NativeSendProvider> SendBridge(ProviderAdapter&,NativeOwnerTable&);
};
bool RuntimeProbeBeforeConnect() noexcept;
}
