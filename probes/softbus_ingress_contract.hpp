#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// HOST DIAGNOSTIC ONLY. This model is not wired to the native backend. It
// deliberately cannot reconstruct an event's identity before adapter ingress.
namespace mdds_probe {
using PublicBytesCallback = void (*)(std::int32_t, const void*, std::uint32_t);
constexpr std::size_t ingress_diagnostic_slots = 8; // NOT a production limit.
constexpr std::size_t ingress_diagnostic_records = 4;
constexpr std::size_t ingress_diagnostic_message_bytes = 256;

enum class IncarnationState { Free, ReservedUnexposed, Exposed, Tombstoned };
namespace ingress_detail { struct Application; struct Captured; }

class DiagnosticReceiver {
public:
    virtual ~DiagnosticReceiver() = default;
    virtual void Receive(std::int32_t socket, const std::vector<std::uint8_t>& bytes) = 0;
};

class NativeTableToken {
public:
    explicit operator bool() const noexcept { return reservation_ != 0; }
private:
    friend class IngressDiagnosticBank;
    std::size_t slot_ = 0;
    std::uint64_t reservation_ = 0;
};

class DiagnosticAttachment {
public:
    explicit operator bool() const noexcept { return static_cast<bool>(application_); }
private:
    friend class IngressDiagnosticBank;
    std::shared_ptr<ingress_detail::Application> application_;
};

// A move-only, already-acquired receiver lease plus owned bytes. Delivery never
// consults the current attachment. Detach remains pending while this exists.
class CapturedIngressRecord {
public:
    CapturedIngressRecord() noexcept;
    ~CapturedIngressRecord();
    CapturedIngressRecord(CapturedIngressRecord&&) noexcept;
    CapturedIngressRecord& operator=(CapturedIngressRecord&&) noexcept;
    CapturedIngressRecord(const CapturedIngressRecord&) = delete;
    CapturedIngressRecord& operator=(const CapturedIngressRecord&) = delete;
    explicit operator bool() const noexcept;
    bool Deliver(); // consumes the lease even if the receiver throws
private:
    friend class IngressDiagnosticBank;
    std::unique_ptr<ingress_detail::Captured> captured_;
};

struct IngressDiagnosticStats {
    std::size_t exposed = 0;
    std::size_t tombstoned = 0;
    std::size_t captured = 0;
    std::size_t outstanding = 0;
    std::size_t dropped_retired = 0;
    std::size_t dropped_no_application = 0;
    std::size_t dropped_unknown_socket = 0;
    std::size_t dropped_invalid = 0;
    std::size_t dropped_budget = 0;
};

class IngressDiagnosticBank final {
public:
    // One bank for the complete process lifetime. No reset/destructor API.
    static IngressDiagnosticBank& Process();
    NativeTableToken Reserve();
    bool AbandonUnexposed(const NativeTableToken&);
    PublicBytesCallback Expose(const NativeTableToken&, std::int32_t socket);
    bool Retire(const NativeTableToken&);
    DiagnosticAttachment Attach(const NativeTableToken&, std::shared_ptr<DiagnosticReceiver>);
    bool BeginDetach(const NativeTableToken&, const DiagnosticAttachment&);
    bool DetachComplete(const DiagnosticAttachment&) const;
    CapturedIngressRecord Take(const NativeTableToken&);
    IngressDiagnosticStats Stats() const;

private:
    IngressDiagnosticBank() = default;
    IngressDiagnosticBank(const IngressDiagnosticBank&) = delete;
    IngressDiagnosticBank& operator=(const IngressDiagnosticBank&) = delete;
    template<std::size_t Slot>
    static void OnBytes(std::int32_t, const void*, std::uint32_t) noexcept;
    static void Ingress(std::size_t, std::int32_t, const void*, std::uint32_t) noexcept;
};
} // namespace mdds_probe
