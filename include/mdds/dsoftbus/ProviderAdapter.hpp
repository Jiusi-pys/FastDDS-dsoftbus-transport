#ifndef MDDS_PROVIDER_ADAPTER_HPP
#define MDDS_PROVIDER_ADAPTER_HPP

#include <mdds/dsoftbus/NativeBackend.hpp>
#include <mdds/dsoftbus/ServiceRecordBinding.hpp>
namespace mdds::dsoftbus { class NativeSendProvider; }
namespace mdds::dsoftbus::host_test { class HostProviderLoop; }

namespace mdds::dsoftbus::phase0 {
namespace detail { struct ProviderDomainState; }
enum class AdapterPhase { Constructed, Deploying, Connected, Published, Closing, Closed, Failed };
enum class SdkDrain { Unproven };
struct BackendCloseResult {
    BackendStatus status = BackendStatus::InvalidState;
    bool adapter_drained = false;
    SdkDrain sdk_drain = SdkDrain::Unproven;
};
struct AdapterObservation {
    AdapterPhase phase = AdapterPhase::Constructed;
    bool provider_stopped = false;
    bool input_open = false;
    std::size_t ingress_active = 0, ingress_rejected = 0, provider_exceptions = 0;
    BackendStatus provider_status = BackendStatus::None;
    std::size_t send_calls = 0;
    std::int32_t last_priority = 0;
    BindingObservation binding;
};
// Explicit bounded Host registration domain (8 live declarations), no singleton,
// SDK object or future-application queue. Outlives no native resource by itself.
class ProviderAdapterDomain final {
public:
    ProviderAdapterDomain();
private:
    friend class ProviderAdapter;
    std::shared_ptr<detail::ProviderDomainState> state_;
};
class ProviderAdapter;
struct AdapterBuildResult {
    BackendStatus status = BackendStatus::NotImplemented;
    std::unique_ptr<ProviderAdapter> adapter;
};
// Explicit HostMock-only prototype. No production factory uses it. The optional
// Host facade controller alone can obtain its private ticketed-send bridge.
// Control methods
// and destruction are externally serialized and outside receiver callbacks.
// Native table, attachments and receiver outlive this adapter. Callback handles
// may outlive it and reject; they cannot acquire a future registration.
class ProviderAdapter final {
public:
    static AdapterBuildResult Build(ProviderAdapterDomain&, ServiceRecordTable&,
            NativeOwnerTable&, ApplicationAttachments&, std::shared_ptr<NativeBackend>,
            const BindingRequest&);
    ~ProviderAdapter();
    ProviderAdapter(const ProviderAdapter&) = delete;
    ProviderAdapter& operator=(const ProviderAdapter&) = delete;
    BackendStatus Initialize() noexcept;
    BackendStatus Publish() noexcept;
    BackendStatus DispatchOne() noexcept;
    BackendStatus CloseInput() noexcept;
    BackendStatus ReopenInput(InputChannels::Receiver*) noexcept;
    BackendCloseResult Close() noexcept;
    AdapterObservation Observe() const noexcept;
private:
    friend class ::mdds::dsoftbus::host_test::HostProviderLoop;
    std::shared_ptr<NativeSendProvider> BindSendProvider(NativeOwnerTable&) const noexcept;
    void SealIngress() noexcept;
    explicit ProviderAdapter(std::shared_ptr<detail::ProviderAdapterState>) noexcept;
    std::shared_ptr<detail::ProviderAdapterState> state_;
};
} // namespace mdds::dsoftbus::phase0
#endif
