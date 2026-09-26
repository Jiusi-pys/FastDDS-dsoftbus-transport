#ifndef MDDS_NATIVE_BACKEND_HPP
#define MDDS_NATIVE_BACKEND_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mdds::dsoftbus::phase0 {
namespace detail { struct ProviderAdapterState; }
enum class BackendKind { Unavailable, HostMock, Experimental, DSoftBusProvider };
enum class BackendStatus {
    None, NativeBackendUnavailable, NotImplemented, InvalidIdentity, DuplicateRecord,
    InvalidState, ProviderStopped, ProviderException, BindingRejected, InvalidBuffer,
    ForeignSocket, NoWork, CallbackFailure, Capacity, PendingIdentity, ChannelClosed, AllocationFailure
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
    BackendStatus OnBytes(std::int32_t, const void*, std::uint32_t) const noexcept;
    BackendStatus OnShutdown(std::int32_t) const noexcept;
private:
    friend struct detail::ProviderAdapterState;
    explicit ProviderCallbacks(std::weak_ptr<detail::ProviderAdapterState> state) noexcept
        : state_(std::move(state)) {}
    std::weak_ptr<detail::ProviderAdapterState> state_;
};
struct BackendSocketResult {
    BackendStatus status = BackendStatus::NativeBackendUnavailable;
    std::optional<std::int32_t> socket;
};
struct BackendSendResult {
    BackendStatus status = BackendStatus::NativeBackendUnavailable;
    std::optional<std::int32_t> provider_return; // absent if not called
};
// Future provider port, not production registration. Implementations never receive
// a binding entrance or receiver pointer. Create retains only a weak callback
// handle. The eventual SDK shim must supply fixed record/role trampolines: this
// handle does not pretend the SDK has a context or generation argument.
class NativeBackend {
public:
    virtual ~NativeBackend() = default;
    virtual BackendKind Kind() const noexcept = 0;
    virtual BackendIdentity Identity() const = 0;
    virtual BackendSocketResult Create(ProviderCallbacks) = 0;
    virtual BackendStatus Listen(std::int32_t) = 0;
    virtual BackendStatus Bind(std::int32_t) = 0;
    // Synchronous borrow only. Phase 0 declared this port; the opt-in Host loop
    // invokes it exclusively behind NativeSendExecution's original owner/payload
    // ticket. This is still not an implemented or qualified SDK provider.
    virtual BackendSendResult Send(std::int32_t, const std::uint8_t*, std::size_t,
            std::chrono::steady_clock::time_point) = 0;
    virtual BackendStatus Shutdown(std::int32_t) = 0;
};
class NullBackend final : public NativeBackend {
public:
    BackendKind Kind() const noexcept override { return BackendKind::Unavailable; }
    BackendIdentity Identity() const override { return {}; }
    BackendSocketResult Create(ProviderCallbacks) override { return {}; }
    BackendStatus Listen(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
    BackendStatus Bind(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
    BackendSendResult Send(std::int32_t, const std::uint8_t*, std::size_t,
            std::chrono::steady_clock::time_point) override { return {}; }
    BackendStatus Shutdown(std::int32_t) override { return BackendStatus::NativeBackendUnavailable; }
};
} // namespace mdds::dsoftbus::phase0
#endif
