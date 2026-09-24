#include "softbus_cancel_contract.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <stdexcept>

namespace mdds_probe {
namespace {
bool ValidServerAdmission(std::uint32_t value) noexcept { return value == 1000 || value == 3000; }
}
CancelServerSchedule::CancelServerSchedule(std::uint32_t admission_ms)
    : admission_us_(static_cast<std::int64_t>(admission_ms) * 1000) {
    if (!ValidServerAdmission(admission_ms)) throw std::invalid_argument("invalid_server_admission_fixture");
}
std::int64_t CancelRoleRunLimitUs(bool server, std::uint32_t admission_ms) noexcept {
    if (!ValidServerAdmission(admission_ms)) return -1;
    return server ? static_cast<std::int64_t>(admission_ms) * 1000 + cancel_run_us + cancel_return_budget_us : cancel_run_us;
}
std::int64_t CancelRequiredWatchdogUs(bool server, std::uint32_t admission_ms) noexcept {
    if (!ValidServerAdmission(admission_ms)) return -1;
    return server && admission_ms == 3000 ? 13000000 : 10000000;
}
CancelSendAdmission TryBeginCancelSend(CancelContract& contract, std::uint32_t sequence,
        std::uint64_t tid, std::int64_t at, bool stopped) {
    if (at < 0 || at >= cancel_run_us - cancel_return_budget_us) return CancelSendAdmission::AdmissionClosed;
    if (stopped) return CancelSendAdmission::Stopped;
    return contract.BeginSend(sequence, tid, at) ? CancelSendAdmission::Admitted : CancelSendAdmission::ContractRejected;
}
CancelSettleDecision DecideCancelSettle(bool server, std::uint32_t settle_ms,
        const CancelObservation& state, std::int64_t at) noexcept {
    if (settle_ms == 0) return CancelSettleDecision::NotRequested;
    if (server || state.selected || state.failed || state.watchdog_terminated || !state.workers_joined ||
            (state.send_enter_us >= 0 && state.send_return_us < state.send_enter_us))
        return CancelSettleDecision::SkipContext;
    if (settle_ms > 1000 || at < 0 || state.workers_joined_us < 0 || at < state.workers_joined_us ||
            (state.send_enter_us >= 0 && state.workers_joined_us < state.send_return_us) ||
            at > cancel_run_us - static_cast<std::int64_t>(settle_ms) * 1000)
        return CancelSettleDecision::FailedSchedule;
    return CancelSettleDecision::Wait;
}
bool SetCancelWorkloadOption(CancelWorkload& work, std::string_view key, std::string_view text) noexcept
{
    const unsigned bit = key == "--hold-ms" ? 1U : key == "--record-bytes" ? 2U : key == "--max-records" ? 4U :
            key == "--settle-ms" ? 8U : key == "--server-admission-ms" ? 16U : key == "--delivery-window-ms" ? 32U : 0U;
    if (bit == 0 || (work.specified & bit) != 0 || text.empty()) return false;
    for (char c : text) if (c < '0' || c > '9') return false;
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    if (bit == 1) work.hold_ms = value;
    else if (bit == 2) work.record_bytes = value;
    else if (bit == 4) work.max_records = value;
    else if (bit == 8) work.settle_ms = value;
    else if (bit == 16) work.server_admission_ms = value;
    else work.delivery_window_ms = value;
    work.specified |= bit;
    return true;
}
bool ValidateCancelWorkload(const CancelWorkload& work, bool bytes_profile) noexcept
{
    if ((work.specified & 32U) != 0 && work.delivery_window_ms != 3000) return false;
    if (work.delivery_window_ms != 0 && (bytes_profile || work.delivery_window_ms != 3000 || work.hold_ms != 4500 ||
            work.settle_ms != 0 || work.max_records != 64 || work.server_admission_ms != 3000 ||
            (work.record_bytes != 2097152 && work.record_bytes != 2097124))) return false;
    // 6000ms is one explicit negative-test condition beyond the observed native
    // five-second poll, not a general expansion of operation/cancellation time.
    return (work.hold_ms <= 4500 || work.hold_ms == 6000) && work.settle_ms <= 1000 &&
           ValidServerAdmission(work.server_admission_ms) && work.record_bytes >= 80 &&
           work.record_bytes <= (bytes_profile ? cancel_record_bytes : cancel_candidate_raw_record_max) &&
           work.max_records > 0 && work.max_records <= cancel_record_limit &&
           static_cast<std::uint64_t>(work.record_bytes) * work.max_records <= cancel_traffic_limit;
}
bool CancelServerSchedule::AdmitPeer(std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (at < 0) return false;
    if (first_verified_bind_us_ < 0) first_verified_bind_us_ = at;
    return first_verified_bind_us_ <= admission_us_;
}
void CancelServerSchedule::ObservePeerShutdown(std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (first_verified_bind_us_ >= 0 && at >= first_verified_bind_us_ && peer_shutdown_us_ < 0)
        peer_shutdown_us_ = at;
}
CancelServerObservation CancelServerSchedule::Snapshot(std::int64_t at) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    CancelServerObservation result;
    result.first_verified_bind_us = first_verified_bind_us_;
    result.peer_shutdown_us = peer_shutdown_us_;
    if (first_verified_bind_us_ < 0) {
        result.state = at >= 0 && at <= admission_us_ ?
                CancelServerState::WaitingForPeer : CancelServerState::ScheduleNotAdmitted;
    } else if (first_verified_bind_us_ > admission_us_) {
        result.state = CancelServerState::ScheduleNotAdmitted;
    } else {
        result.peer_window_end_us = first_verified_bind_us_ + cancel_run_us;
        if (peer_shutdown_us_ >= first_verified_bind_us_ && peer_shutdown_us_ <= result.peer_window_end_us)
            result.state = CancelServerState::PeerCompleted;
        else result.state = at < result.peer_window_end_us ?
                CancelServerState::PeerActive : CancelServerState::PeerWindowExpired;
    }
    return result;
}
bool CancelCleanupWithinDeadline(bool server, std::int64_t at, std::uint32_t admission_ms) noexcept
{
    return at >= 0 && at <= CancelRoleRunLimitUs(server, admission_ms);
}

CancelVerdict EvaluateCancelObservation(const CancelObservation& value) noexcept
{
    if (value.failed || value.watchdog_terminated || !value.workers_joined) return CancelVerdict::Failed;
    if (!value.selected) return CancelVerdict::NotExercised;
    if (value.sequence >= cancel_record_limit || value.sender_tid == 0 || value.shutdown_tid == 0 ||
            value.sender_tid == value.shutdown_tid || value.send_enter_us < 0 ||
            value.shutdown_request_us < value.send_enter_us ||
            value.shutdown_request_us - value.send_enter_us < cancel_blocked_us ||
            value.send_return_us < value.send_enter_us || value.workers_joined_us < value.send_return_us)
        return CancelVerdict::Failed;
    if (value.shutdown_overlap_lost) {
        return value.shutdown_call_enter_us < 0 && value.shutdown_return_us < 0 ?
               CancelVerdict::NotExercised : CancelVerdict::Failed;
    }
    if (value.shutdown_call_enter_us < value.shutdown_request_us ||
            value.shutdown_return_us < value.shutdown_call_enter_us ||
            value.send_return_us - value.shutdown_request_us > cancel_return_budget_us ||
            value.shutdown_return_us - value.shutdown_request_us > cancel_return_budget_us ||
            value.workers_joined_us < value.send_return_us || value.workers_joined_us < value.shutdown_return_us ||
            value.workers_joined_us - value.shutdown_request_us > cancel_return_budget_us)
        return CancelVerdict::Failed;
    // FinishSend publishes the native return timestamp. It can reveal that the
    // send returned before the boundary check while publication was scheduled
    // later; that interval also cannot prove a cancellation overlap.
    if (value.send_return_us < value.shutdown_call_enter_us) return CancelVerdict::NotExercised;
    return CancelVerdict::TimelyReturnsObserved;
}

const char* CancelVerdictName(CancelVerdict verdict) noexcept
{
    switch (verdict) {
        case CancelVerdict::NotExercised: return "NOT_EXERCISED";
        case CancelVerdict::TimelyReturnsObserved: return "TIMELY_RETURNS_OBSERVED";
        case CancelVerdict::Failed: return "FAIL";
    }
    return "FAIL";
}

bool CancelContract::BeginSend(std::uint32_t sequence, std::uint64_t tid, std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (observation_.selected) return false;
    if (in_flight_ || observation_.failed || sequence >= cancel_record_limit || tid == 0 || at < 0) {
        observation_.failed = true; return false;
    }
    observation_.sequence = sequence; observation_.sender_tid = tid;
    observation_.send_enter_us = at; observation_.send_return_us = -1;
    if (observation_.first_send_enter_us < 0) observation_.first_send_enter_us = at;
    in_flight_ = true;
    return true;
}

bool CancelContract::FinishSend(std::uint32_t sequence, int rc, std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!in_flight_ || sequence != observation_.sequence || at < observation_.send_enter_us) {
        observation_.failed = true; return false;
    }
    observation_.send_return_us = at; observation_.send_rc = rc; in_flight_ = false;
    if (observation_.completed_calls++ == 0) observation_.first_send_return_us = at;
    const auto duration = at - observation_.send_enter_us;
    if (duration > observation_.longest_send_us) {
        observation_.longest_sequence = sequence; observation_.longest_send_rc = rc;
        observation_.longest_send_enter_us = observation_.send_enter_us;
        observation_.longest_send_return_us = at; observation_.longest_send_us = duration;
    }
    return true;
}

bool CancelContract::ClaimShutdown(std::uint64_t tid, std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (observation_.selected || observation_.failed || !in_flight_ || tid == 0 ||
            tid == observation_.sender_tid || at < observation_.send_enter_us ||
            at - observation_.send_enter_us < cancel_blocked_us) return false;
    observation_.selected = true; observation_.shutdown_tid = tid; observation_.shutdown_request_us = at;
    return true;
}

bool CancelContract::ShutdownReturned(std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!observation_.selected || observation_.shutdown_overlap_lost || observation_.shutdown_call_enter_us < 0 ||
            observation_.shutdown_return_us >= 0 || at < observation_.shutdown_call_enter_us) {
        observation_.failed = true; return false;
    }
    observation_.shutdown_return_us = at; return true;
}
bool CancelContract::EnterShutdownCall(std::int64_t at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!observation_.selected || observation_.failed || observation_.shutdown_call_enter_us >= 0 ||
            observation_.shutdown_overlap_lost || at < observation_.shutdown_request_us) {
        observation_.failed = true; return false;
    }
    if (!in_flight_) {
        observation_.shutdown_overlap_lost = true;
        return false;
    }
    observation_.shutdown_call_enter_us = at;
    return true;
}
void CancelContract::Joined(std::int64_t at) {
    std::lock_guard<std::mutex> lock(mutex_);
    observation_.workers_joined = true; observation_.workers_joined_us = at;
}
void CancelContract::Fail() { std::lock_guard<std::mutex> lock(mutex_); observation_.failed = true; }
CancelObservation CancelContract::Snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return observation_; }

namespace {
constexpr unsigned char magic[] = {'M', 'D', 'D', 'S', 'C', 'A', 'N', '1'};
unsigned char Pattern(std::size_t offset, std::uint32_t sequence) noexcept
{ return static_cast<unsigned char>((offset * 31U + sequence * 17U) & 0xffU); }
}
std::vector<std::uint8_t> MakeCancelRecord(std::string_view run_id, std::uint32_t sequence, std::size_t record_bytes)
{
    if (run_id.empty() || run_id.size() > 64 || sequence >= cancel_record_limit ||
            record_bytes < 80 || record_bytes > cancel_candidate_raw_record_max)
        throw std::invalid_argument("invalid_cancel_record_identity");
    std::vector<std::uint8_t> result(record_bytes, 0);
    std::copy(std::begin(magic), std::end(magic), result.begin());
    result[8] = static_cast<std::uint8_t>(run_id.size());
    for (unsigned i = 0; i != 4; ++i) result[12 + i] = static_cast<std::uint8_t>(sequence >> (i * 8));
    std::copy(run_id.begin(), run_id.end(), result.begin() + 16);
    for (std::size_t i = 80; i != result.size(); ++i) result[i] = Pattern(i, sequence);
    return result;
}
bool ValidateCancelRecord(const void* data, std::size_t size, std::string_view run_id,
        std::uint32_t& sequence, std::size_t expected_bytes) noexcept
{
    if (!data || expected_bytes < 80 || expected_bytes > cancel_candidate_raw_record_max || size != expected_bytes ||
            run_id.empty() || run_id.size() > 64) return false;
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (std::memcmp(bytes, magic, sizeof(magic)) != 0 || bytes[8] != run_id.size() ||
            std::memcmp(bytes + 16, run_id.data(), run_id.size()) != 0) return false;
    std::uint32_t value = 0;
    for (unsigned i = 0; i != 4; ++i) value |= static_cast<std::uint32_t>(bytes[12 + i]) << (i * 8);
    if (value >= cancel_record_limit) return false;
    for (std::size_t i = 9; i != 12; ++i) if (bytes[i] != 0) return false;
    for (std::size_t i = 16 + run_id.size(); i != 80; ++i) if (bytes[i] != 0) return false;
    for (std::size_t i = 80; i != size; ++i) if (bytes[i] != Pattern(i, value)) return false;
    sequence = value; return true;
}
bool CancelIntegrity::Observe(const void* data, std::size_t size, std::string_view run_id,
        const CancelWorkload& work, std::uint32_t& sequence)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto ordinal = value_.callbacks++;
    value_.received_bytes += size;
    const bool valid = ordinal < work.max_records && ValidateCancelWorkload(work, false) &&
            ValidateCancelRecord(data, size, run_id, sequence, work.record_bytes) && sequence < work.max_records;
    if (!valid) {
        ++value_.invalid_records;
        if (value_.first_bad_ordinal < 0) {
            value_.first_bad_ordinal = ordinal; value_.first_bad_length = size;
            if (data && size >= 16) {
                const auto* bytes = static_cast<const unsigned char*>(data);
                if (std::memcmp(bytes, magic, sizeof(magic)) == 0) {
                    std::uint32_t claimed = 0;
                    for (unsigned i = 0; i != 4; ++i) claimed |= static_cast<std::uint32_t>(bytes[12 + i]) << (i * 8);
                    value_.first_bad_claimed_sequence = claimed;
                }
            }
        }
        return false;
    }
    ++value_.valid_records;
    const auto bit = std::uint64_t{1} << (sequence % 64);
    auto& word = value_.sequences[sequence / 64];
    if ((word & bit) != 0) ++value_.duplicates;
    else { word |= bit; ++value_.unique_records; }
    // FNV-1a is a diagnostic arrival-order digest, not an authenticity primitive.
    // Exact content validation above is the integrity check, not hash equality.
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i != size; ++i) { value_.content_hash ^= bytes[i]; value_.content_hash *= 1099511628211ULL; }
    return true;
}
CancelIntegrityObservation CancelIntegrity::Snapshot() const
{ std::lock_guard<std::mutex> lock(mutex_); return value_; }
bool CancelDeliveryReceiptEligible(std::int64_t released, std::int64_t now) noexcept {
    return released >= 0 && now >= released && now-released >= cancel_delivery_window_us;
}
bool CancelDeliveryWindowAdmitsRecord(bool closed, std::int64_t released, std::int64_t ingress) noexcept {
    return !closed && ingress >= 0 && (released < 0 ||
        (ingress >= released && ingress-released < cancel_delivery_window_us));
}
CancelDeliveryResult EvaluateCancelDelivery(const CancelIntegrityObservation& seen, const CancelWorkload& work) noexcept {
    if (seen.invalid_records || seen.duplicates) return CancelDeliveryResult::ObservedIntegrityFailed;
    if (work.max_records != 64 || seen.callbacks != 64 || seen.valid_records != 64 || seen.unique_records != 64 ||
            seen.received_bytes != static_cast<std::uint64_t>(work.record_bytes)*64 || seen.sequences[0] != ~std::uint64_t{0})
        return CancelDeliveryResult::DeliveryIncomplete;
    return CancelDeliveryResult::Complete;
}
namespace {
constexpr unsigned char receipt_magic[]{'M','D','D','S','D','E','L','1'};
void PutReceipt(std::vector<std::uint8_t>& bytes,std::size_t offset,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i) bytes[offset+i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint64_t GetReceipt(const std::uint8_t* bytes,std::size_t offset,unsigned count) noexcept {
    std::uint64_t value=0;
    for(unsigned i=0;i<count;++i) value|=static_cast<std::uint64_t>(bytes[offset+i])<<(i*8);
    return value;
}
std::uint64_t ReceiptHash(const std::uint8_t* bytes) noexcept {
    std::uint64_t hash=14695981039346656037ULL;
    for(std::size_t i=0;i<124;++i) { hash^=bytes[i]; hash*=1099511628211ULL; }
    return hash; // Diagnostic corruption detection; not an authenticity claim.
}
}
std::vector<std::uint8_t> MakeCancelDeliveryReceipt(std::string_view run,const CancelWorkload& work,
        const CancelIntegrityObservation& seen) {
    if(run.empty() || run.size()>64 || work.delivery_window_ms!=3000 || !ValidateCancelWorkload(work,false))
        throw std::invalid_argument("invalid_delivery_receipt_configuration");
    std::vector<std::uint8_t> result(cancel_delivery_receipt_bytes,0);
    std::copy(std::begin(receipt_magic),std::end(receipt_magic),result.begin());
    result[8]=static_cast<std::uint8_t>(run.size()); result[9]=1;
    std::copy(run.begin(),run.end(),result.begin()+16);
    PutReceipt(result,80,work.record_bytes,4); PutReceipt(result,84,work.max_records,4);
    PutReceipt(result,88,work.delivery_window_ms,4); PutReceipt(result,92,seen.callbacks,4);
    PutReceipt(result,96,seen.unique_records,4); PutReceipt(result,100,seen.invalid_records,4);
    PutReceipt(result,104,seen.duplicates,4); PutReceipt(result,108,seen.received_bytes,8);
    PutReceipt(result,116,seen.sequences[0],8); PutReceipt(result,124,ReceiptHash(result.data()),8);
    return result;
}
bool ParseCancelDeliveryReceipt(const void* data,std::size_t size,std::string_view run,const CancelWorkload& work,
        CancelIntegrityObservation& result) noexcept {
    if(!data || size!=cancel_delivery_receipt_bytes || run.empty() || run.size()>64 ||
            work.delivery_window_ms!=3000 || !ValidateCancelWorkload(work,false)) return false;
    const auto* bytes=static_cast<const std::uint8_t*>(data);
    if(std::memcmp(bytes,receipt_magic,sizeof(receipt_magic)) || bytes[8]!=run.size() || bytes[9]!=1 ||
            std::memcmp(bytes+16,run.data(),run.size()) || GetReceipt(bytes,80,4)!=work.record_bytes ||
            GetReceipt(bytes,84,4)!=work.max_records || GetReceipt(bytes,88,4)!=work.delivery_window_ms ||
            GetReceipt(bytes,124,8)!=ReceiptHash(bytes)) return false;
    for(std::size_t i=10;i<16;++i) if(bytes[i]) return false;
    for(std::size_t i=16+run.size();i<80;++i) if(bytes[i]) return false;
    for(std::size_t i=132;i<size;++i) if(bytes[i]) return false;
    CancelIntegrityObservation parsed;
    parsed.callbacks=static_cast<std::uint32_t>(GetReceipt(bytes,92,4));
    parsed.unique_records=static_cast<std::uint32_t>(GetReceipt(bytes,96,4));
    parsed.invalid_records=static_cast<std::uint32_t>(GetReceipt(bytes,100,4));
    parsed.duplicates=static_cast<std::uint32_t>(GetReceipt(bytes,104,4));
    parsed.received_bytes=GetReceipt(bytes,108,8); parsed.sequences[0]=GetReceipt(bytes,116,8);
    if(parsed.invalid_records>parsed.callbacks || parsed.unique_records>64 ||
            parsed.duplicates>parsed.callbacks || parsed.unique_records>parsed.callbacks-parsed.invalid_records ||
            parsed.duplicates!=parsed.callbacks-parsed.invalid_records-parsed.unique_records) return false;
    unsigned bit_count=0;
    for(auto bits=parsed.sequences[0];bits;bits>>=1) bit_count+=static_cast<unsigned>(bits&1U);
    if(bit_count!=parsed.unique_records) return false;
    parsed.valid_records=parsed.callbacks-parsed.invalid_records;
    result=parsed;
    return true;
}
}
