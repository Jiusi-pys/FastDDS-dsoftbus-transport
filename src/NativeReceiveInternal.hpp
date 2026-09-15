#ifndef MDDS_NATIVE_RECEIVE_INTERNAL_HPP
#define MDDS_NATIVE_RECEIVE_INTERNAL_HPP
#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <mutex>

namespace mdds::dsoftbus::detail {
class ProcessReceiveBridge;

// Final execution accounting only. Never retained by a queued receive record.
class NativeReceiveLease final
{
public:
    NativeReceiveLease() noexcept = default;
    ~NativeReceiveLease() noexcept;
    NativeReceiveLease(NativeReceiveLease&&) noexcept;
    NativeReceiveLease& operator=(NativeReceiveLease&&) noexcept;
    NativeReceiveLease(const NativeReceiveLease&) = delete;
    NativeReceiveLease& operator=(const NativeReceiveLease&) = delete;
    explicit operator bool() const noexcept { return bool(state_); }
private:
    friend class NativeReceiveBridge;
    NativeReceiveLease(std::shared_ptr<NativeState>, std::size_t, std::uint64_t) noexcept;
    void Release() noexcept;
    std::shared_ptr<NativeState> state_;
    std::size_t slot_ = 0;
    std::uint64_t incarnation_ = 0;
};

// Original native gate remains held while the caller takes the attachment gate
// and real receiver lease. No SDK, callback, wait or final cleanup in this scope.
class NativeReceiveGuard final
{
public:
    NativeReceiveGuard(NativeReceiveGuard&&) noexcept;
    NativeReceiveGuard& operator=(NativeReceiveGuard&&) = delete;
    NativeReceiveGuard(const NativeReceiveGuard&) = delete;
    NativeStatus status() const noexcept { return status_; }
private:
    friend class NativeReceiveBridge;
    explicit NativeReceiveGuard(NativeStatus s) noexcept : status_(s) {}
    explicit NativeReceiveGuard(const NativeIdentityConstraint&) noexcept;
    // Declaration order ensures the gate unlocks before the last State drops.
    std::shared_ptr<NativeState> state_;
    std::unique_lock<std::mutex> gate_;
    NativeStatus status_ = NativeStatus::InvalidHandle;
    std::size_t slot_ = 0;
    bool entered_ = false;
};

class NativeReceiveBridge final
{
    friend class ProcessReceiveBridge;
    friend class ::mdds::dsoftbus::NativeOwnerTableTestAccess;
    static NativeReceiveGuard Acquire(const NativeOwnerTable&, const NativeIdentityConstraint&) noexcept;
    static NativeReceiveLease Enter(NativeReceiveGuard&) noexcept;
};
}
#endif
