#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
enum class ViewDomainState { Await, Ready, Refused, Closed };
enum class DomainAdmission { Allowed, Awaiting, Refused, Closed, CounterExhausted };
enum class DomainOperation : std::size_t { Submission, NativeSend, Receive };
struct ViewDomainSnapshot {
    bool configured = false;
    bool expected_domain_known = false; // false until the public callback in automatic mode
    std::uint32_t expected_domain_id = 0;
    std::optional<std::uint32_t> first_observed_domain, last_observed_domain;
    std::optional<std::uint32_t> first_refused_domain;
    ViewDomainState state = ViewDomainState::Await, before_close = ViewDomainState::Await;
    std::uint64_t callbacks = 0;
    std::array<std::uint64_t, 3> active{}, denied{};
};

// One instance per Transport view, never per descriptor/native anchor. A permit
// admits one synchronous operation; no guard lock is held during that operation.
class ViewDomainGuard final : public std::enable_shared_from_this<ViewDomainGuard> {
public:
    class Permit final {
    public:
        Permit() noexcept = default; // Unscoped operation.
        ~Permit() { Reset(); }
        Permit(Permit&& other) noexcept
            : owner_(std::move(other.owner_)), operation_(other.operation_), status_(other.status_) {}
        Permit& operator=(Permit&& other) noexcept {
            if (this != &other) { Reset(); owner_ = std::move(other.owner_); operation_ = other.operation_; status_ = other.status_; }
            return *this;
        }
        Permit(const Permit&) = delete;
        Permit& operator=(const Permit&) = delete;
        explicit operator bool() const noexcept { return status_ == DomainAdmission::Allowed; }
        DomainAdmission status() const noexcept { return status_; }
    private:
        friend class ViewDomainGuard;
        Permit(std::shared_ptr<ViewDomainGuard> owner, DomainOperation operation, DomainAdmission status) noexcept
            : owner_(std::move(owner)), operation_(operation), status_(status) {}
        void Reset() noexcept {
            if (owner_) { owner_->Leave(operation_); owner_.reset(); }
        }
        std::shared_ptr<ViewDomainGuard> owner_;
        DomainOperation operation_ = DomainOperation::Submission;
        DomainAdmission status_ = DomainAdmission::Allowed;
    };
    static std::shared_ptr<ViewDomainGuard> Create(std::uint32_t expected) {
        return std::shared_ptr<ViewDomainGuard>(new ViewDomainGuard(expected));
    }
    static std::shared_ptr<ViewDomainGuard> CreateUnbound(std::uint32_t deployment_domain) {
        return std::shared_ptr<ViewDomainGuard>(new ViewDomainGuard(deployment_domain, true));
    }
    bool automatic() const noexcept { return automatic_; }
    std::uint32_t expected_domain_id() const noexcept { return expected_; }
    ViewDomainState Confirm(std::uint32_t actual) noexcept {
        ViewDomainState result;
        bool changed = false;
        std::uint32_t expected = expected_;
        {
            std::lock_guard<std::mutex> lock(gate_);
            Increment(snapshot_.callbacks);
            const bool mismatch = automatic_ ?
                (snapshot_.first_observed_domain && *snapshot_.first_observed_domain != actual) : actual != expected_;
            if (!snapshot_.first_observed_domain) snapshot_.first_observed_domain = actual;
            if (automatic_ && !mismatch) { snapshot_.expected_domain_id = actual; snapshot_.expected_domain_known = true; }
            expected = snapshot_.expected_domain_id;
            snapshot_.last_observed_domain = actual;
            const auto previous = snapshot_.state;
            if (previous != ViewDomainState::Closed && previous != ViewDomainState::Refused) {
                if (mismatch) {
                    snapshot_.first_refused_domain = actual; snapshot_.state = ViewDomainState::Refused;
                }
                else if (previous == ViewDomainState::Await) snapshot_.state = ViewDomainState::Ready;
            }
            result = snapshot_.state; changed = result != previous;
        }
        if (changed) std::fprintf(stderr, "DSoftBus domain confirmation expected=%u actual=%u reason=%s\n",
                expected, actual, result == ViewDomainState::Ready ? "Matched" : "DomainMismatch");
        return result;
    }
    // Binding failed after a real callback. Keep this view fail-closed; no
    // future callback may turn a failed/closed circuit into a new incarnation.
    void Refuse(std::uint32_t actual) noexcept {
        std::lock_guard<std::mutex> lock(gate_);
        Increment(snapshot_.callbacks);
        if (!snapshot_.first_observed_domain) snapshot_.first_observed_domain=actual;
        snapshot_.last_observed_domain=actual;
        if(snapshot_.state!=ViewDomainState::Closed) {
            snapshot_.first_refused_domain=actual;snapshot_.state=ViewDomainState::Refused;
        }
    }
    Permit TryEnter(DomainOperation operation) noexcept {
        const auto index = static_cast<std::size_t>(operation);
        DomainAdmission status = DomainAdmission::Allowed;
        bool first_denial = false;
        std::uint32_t expected=expected_;
        std::optional<std::uint32_t> actual;
        {
            std::lock_guard<std::mutex> lock(gate_);
            switch (snapshot_.state) {
                case ViewDomainState::Await: status = DomainAdmission::Awaiting; break;
                case ViewDomainState::Refused: status = DomainAdmission::Refused; break;
                case ViewDomainState::Closed: status = DomainAdmission::Closed; break;
                case ViewDomainState::Ready: break;
            }
            if (status == DomainAdmission::Allowed && snapshot_.active[index] == (std::numeric_limits<std::uint64_t>::max)())
                status = DomainAdmission::CounterExhausted;
            if (status == DomainAdmission::Allowed) ++snapshot_.active[index];
            else { first_denial = snapshot_.denied[index] == 0; Increment(snapshot_.denied[index]); }
            expected=snapshot_.expected_domain_id;
            actual = snapshot_.first_refused_domain ? snapshot_.first_refused_domain : snapshot_.last_observed_domain;
        }
        if (first_denial) std::fprintf(stderr, "DSoftBus domain admission expected=%u actual_known=%u actual=%u operation=%u reason=%s\n",
                expected, actual ? 1u : 0u, actual.value_or(0), static_cast<unsigned>(operation),
                status == DomainAdmission::Awaiting ? "AwaitingDomainCallback" : status == DomainAdmission::Refused ?
                "DomainMismatch" : status == DomainAdmission::Closed ? "ViewClosed" : "CounterExhausted");
        return Permit(status == DomainAdmission::Allowed ? shared_from_this() : nullptr, operation, status);
    }
    void Close() noexcept {
        ViewDomainState previous;
        std::uint32_t expected=expected_;
        std::optional<std::uint32_t> actual;
        {
            std::lock_guard<std::mutex> lock(gate_);
            if (snapshot_.state == ViewDomainState::Closed) return;
            previous = snapshot_.state; snapshot_.before_close = previous; snapshot_.state = ViewDomainState::Closed;
            expected=snapshot_.expected_domain_id;
            actual = snapshot_.first_refused_domain ? snapshot_.first_refused_domain : snapshot_.last_observed_domain;
        }
        std::fprintf(stderr, "DSoftBus domain close expected=%u actual_known=%u actual=%u reason=%s\n", expected,
                actual ? 1u : 0u, actual.value_or(0), previous == ViewDomainState::Await ?
                "ClosedBeforeDomainConfirmation" : "ViewClosed");
    }
    ViewDomainSnapshot Snapshot() const noexcept {
        std::lock_guard<std::mutex> lock(gate_); return snapshot_;
    }
private:
    explicit ViewDomainGuard(std::uint32_t expected, bool automatic=false) : expected_(expected), automatic_(automatic) {
        snapshot_.configured = true; snapshot_.expected_domain_id = expected;
        snapshot_.expected_domain_known = !automatic;
    }
    static void Increment(std::uint64_t& value) noexcept {
        if (value != (std::numeric_limits<std::uint64_t>::max)()) ++value;
    }
    void Leave(DomainOperation operation) noexcept {
        std::lock_guard<std::mutex> lock(gate_); --snapshot_.active[static_cast<std::size_t>(operation)];
    }
    const std::uint32_t expected_;
    const bool automatic_;
    mutable std::mutex gate_;
    ViewDomainSnapshot snapshot_;
};
} // namespace fastdds_dsoftbus::dsoftbus
