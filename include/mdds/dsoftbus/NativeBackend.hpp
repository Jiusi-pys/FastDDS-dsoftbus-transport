#ifndef MDDS_NATIVE_BACKEND_HPP
#define MDDS_NATIVE_BACKEND_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <mdds/dsoftbus/ServiceRecordTable.hpp>
#include <mdds/dsoftbus/EndpointDirectory.hpp>
namespace mdds::dsoftbus { class NativeOwnerLease; }

namespace mdds::dsoftbus::phase0 {
namespace detail { struct ProviderAdapterState; }
enum class BackendKind { Unavailable, HostMock, Experimental, DSoftBusProvider };
enum class BackendTopology { SingleOwner, ListenerAndOutgoing };
struct BackendPeer {
    std::string name,network_id,package_name;
    RecordDataType data_type=RecordDataType::Unsupported;
    bool operator==(const BackendPeer& p)const noexcept {
        return name==p.name&&network_id==p.network_id&&package_name==p.package_name&&data_type==p.data_type;
    }
};
enum class BackendStatus {
    None, NativeBackendUnavailable, NotImplemented, InvalidIdentity, DuplicateRecord,
    InvalidState, ProviderStopped, ProviderException, BindingRejected, InvalidBuffer,
    ForeignSocket, NoWork, CallbackFailure, Capacity, PendingIdentity, ChannelClosed, AllocationFailure,
    NativeCallFailed
};
// Public API boundary facts, not qualification. Host abstract mocks may provide
// model-only status; a Fake SDK build labels its actual symbol calls separately.
struct BackendCallResult {
    BackendStatus status=BackendStatus::NativeBackendUnavailable;
    bool called=false,returned=false;
    std::optional<std::int32_t> provider_return;
    BackendCallResult()=default;
    BackendCallResult(BackendStatus s) noexcept:status(s){}
    BackendCallResult(BackendStatus s,bool c,bool r,std::optional<std::int32_t> v) noexcept
        :status(s),called(c),returned(r),provider_return(v){}
};
// Provider declarations for Host contract tests, NOT original SDK event identity.
// Record tokens are unique in a registration domain; owner tokens are record-local.
struct ProviderRecordIdentity { std::uint64_t value = 0; };
struct ProviderOwnerIdentity { std::uint64_t value = 0; };
struct BackendIdentity {
    std::string service_name;
    ProviderRecordIdentity record;
    ProviderOwnerIdentity owner;
};
class ProviderCallbacks final {
public:
    ProviderCallbacks() noexcept = default;
    explicit operator bool() const noexcept { return !state_.expired(); }
    BackendStatus OnBytes(std::int32_t, const void*, std::uint32_t) const noexcept;
    BackendStatus OnShutdown(std::int32_t) const noexcept;
    BackendStatus OnError(std::int32_t,std::int32_t) const noexcept;
    BackendStatus OnNegotiate(std::int32_t,const BackendPeer&) const noexcept;
    BackendStatus OnBind(std::int32_t,const BackendPeer&) const noexcept;
    BackendStatus OnNegotiationRejected(std::int32_t) const noexcept;
    BackendStatus OnNativeShutdown(std::int32_t) const noexcept;
private:
    friend struct detail::ProviderAdapterState;
    explicit ProviderCallbacks(std::weak_ptr<detail::ProviderAdapterState> state,RecordRole role=RecordRole::Client) noexcept
        : state_(std::move(state)),role_(role) {}
    std::weak_ptr<detail::ProviderAdapterState> state_;
    RecordRole role_=RecordRole::Client;
};
struct BackendSocketResult {
    BackendStatus status = BackendStatus::NativeBackendUnavailable;
    std::optional<std::int32_t> socket;
    bool api_called=false,api_returned=false;
    std::optional<std::int32_t> provider_error; // negative failure, never a successful handle
};
struct BackendSendResult {
    BackendStatus status = BackendStatus::NativeBackendUnavailable;
    std::optional<std::int32_t> provider_return; // absent if not called
    bool api_called=false,api_returned=false;
};
// Provider port, not production registration. ConfigureEntrance supplies the
// original record/role association only; the public shim registers its own fixed
// trampoline, never the Binding entrance. No receiver pointer is supplied.
// Create retains only a weak callback
// handle. The eventual SDK shim must supply fixed record/role trampolines: this
// handle does not pretend the SDK has a context or generation argument.
class NativeBackend {
public:
    virtual ~NativeBackend() = default;
    virtual BackendKind Kind() const noexcept = 0;
    virtual BackendIdentity Identity() const = 0;
    virtual BackendTopology Topology() const noexcept {return BackendTopology::SingleOwner;}
    virtual BackendIdentity ListenerIdentity() const {return {};}
    virtual BackendStatus ConfigureEntrance(const RecordEntrance&) {return BackendStatus::None;}
    virtual BackendSocketResult Create(ProviderCallbacks) = 0;
    virtual BackendSocketResult CreateListener(ProviderCallbacks) {return {};}
    // Original opaque ownership, not a provider-created token or qualification.
    virtual BackendStatus CommitAcceptedOwner(const RecordEntrance&,const NativeOwnerLease&) {return BackendStatus::NotImplemented;}
    virtual BackendCallResult Listen(std::int32_t) = 0;
    virtual BackendCallResult Bind(std::int32_t) = 0;
    // Synchronous borrow only. Phase 0 declared this port; the opt-in Host loop
    // invokes it exclusively behind NativeSendExecution's original owner/payload
    // ticket. The public Socket shim is implemented for compile/contract tests;
    // real-provider activation and SDK runtime qualification remain unavailable.
    virtual BackendSendResult Send(std::int32_t, const std::uint8_t*, std::size_t,
            std::chrono::steady_clock::time_point) = 0;
    virtual BackendCallResult Shutdown(std::int32_t) = 0;
};
class NullBackend final : public NativeBackend {
public:
    BackendKind Kind() const noexcept override { return BackendKind::Unavailable; }
    BackendIdentity Identity() const override { return {}; }
    BackendSocketResult Create(ProviderCallbacks) override { return {}; }
    BackendCallResult Listen(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
    BackendCallResult Bind(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
    BackendSendResult Send(std::int32_t, const std::uint8_t*, std::size_t,
            std::chrono::steady_clock::time_point) override { return {}; }
    BackendCallResult Shutdown(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
};
} // namespace mdds::dsoftbus::phase0
#endif
