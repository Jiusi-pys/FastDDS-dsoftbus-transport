#include "Measurement.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fastdds_dsoftbus::experiment::perf {
namespace {
constexpr std::array<CaseSpec, 6> cases{{
    {CaseId::RTT_1K, "RTT_1K", Kind::Rtt, 1024, 1056, 1068, 500, 10, 100, 3},
    {CaseId::LOAD_1K, "LOAD_1K", Kind::Load, 1024, 1056, 1068, 1000, 10, 100, 10},
    {CaseId::RTT_4K, "RTT_4K", Kind::Rtt, 4096, 4128, 4140, 500, 10, 100, 3},
    {CaseId::LOAD_4K, "LOAD_4K", Kind::Load, 4096, 4128, 4140, 1000, 10, 100, 10},
    {CaseId::RTT_256K, "RTT_256K", Kind::Rtt, 262144, 262176, 262188, 100, 3, 10, 2},
    {CaseId::LOAD_256K, "LOAD_256K", Kind::Load, 262144, 262176, 262188, 40, 3, 4, 2}
}};
bool ValidIdentity(const Identity& id)
{
    return id.run != 0 && static_cast<unsigned>(id.case_id) < cases.size() &&
           static_cast<unsigned>(id.phase) <= 1 && id.sequence != 0 &&
           id.sequence <= (id.phase == Phase::Formal ? cases[static_cast<unsigned>(id.case_id)].formal_count :
                   cases[static_cast<unsigned>(id.case_id)].warmup_count);
}
void Put(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * (width - i - 1)));
}
uint64_t Get(const std::vector<uint8_t>& bytes, std::size_t offset, unsigned width)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
uint8_t Pattern(const Identity& id, std::size_t offset)
{
    // The entire business vector binds run, case, phase, sequence and byte offset.
    const uint64_t mixed = (id.run >> ((offset & 7u) * 8u)) ^
            (uint64_t{id.sequence} * 131u) ^ (offset * 31u) ^ (offset >> 8) ^
            (static_cast<uint64_t>(id.case_id) * 17u) ^ (static_cast<uint64_t>(id.phase) * 97u);
    return static_cast<uint8_t>(mixed);
}
}
const CaseSpec& GetCase(CaseId id)
{
    if (static_cast<unsigned>(id) >= cases.size()) throw std::out_of_range("performance case");
    return cases[static_cast<unsigned>(id)];
}
std::array<CaseId, 6> CaseOrder(uint32_t repeat)
{
    if (repeat > 3) throw std::out_of_range("performance repeat");
    std::array<CaseId, 6> result{};
    const unsigned start = repeat == 0 ? 0 : (repeat - 1) * 2;
    for (unsigned i = 0; i < result.size(); ++i) result[i] = static_cast<CaseId>((start + i) % 6);
    return result;
}
uint32_t PlannedCount(const CaseSpec& spec, Phase phase, Mode mode)
{
    if (phase == Phase::Warmup) return mode == Mode::Trace ? 0 : spec.warmup_count;
    return mode == Mode::Trace ? spec.trace_count : spec.formal_count;
}
const char* PhaseName(Phase phase) { return phase == Phase::Warmup ? "WARMUP" : "FORMAL"; }
const char* ModeName(Mode mode) { return mode == Mode::Trace ? "TRACE" : "MEASURE"; }
bool Identity::operator==(const Identity& other) const
{
    return run == other.run && case_id == other.case_id && phase == other.phase && sequence == other.sequence;
}
bool FillPayload(const Identity& id, std::vector<uint8_t>& bytes)
{
    if (!ValidIdentity(id)) return false;
    const auto& spec = GetCase(id.case_id);
    bytes.resize(spec.vector_bytes);
    Put(bytes, 0, 0x4d504631, 4); bytes[4] = 1; bytes[5] = static_cast<uint8_t>(spec.kind);
    bytes[6] = static_cast<uint8_t>(id.case_id); bytes[7] = static_cast<uint8_t>(id.phase);
    Put(bytes, 8, id.run, 8); Put(bytes, 16, id.sequence, 4); Put(bytes, 20, spec.business_bytes, 4); Put(bytes, 24, 0, 8);
    for (std::size_t i = 32; i < bytes.size(); ++i) bytes[i] = Pattern(id, i - 32);
    return true;
}
bool ValidatePayload(uint32_t index, const std::vector<uint8_t>& bytes, Identity& decoded)
{
    if (bytes.size() < 32 || bytes.size() > kInboundCapacity || Get(bytes, 0, 4) != 0x4d504631 || bytes[4] != 1 ||
            bytes[6] >= cases.size() || bytes[7] > 1 || Get(bytes, 24, 8) != 0) return false;
    const Identity id{Get(bytes, 8, 8), static_cast<CaseId>(bytes[6]), static_cast<Phase>(bytes[7]),
                     static_cast<uint32_t>(Get(bytes, 16, 4))};
    if (!ValidIdentity(id) || index != id.sequence) return false;
    const auto& spec = GetCase(id.case_id);
    if (bytes[5] != static_cast<uint8_t>(spec.kind) || bytes.size() != spec.vector_bytes || Get(bytes, 20, 4) != spec.business_bytes) return false;
    for (std::size_t i = 32; i < bytes.size(); ++i) if (bytes[i] != Pattern(id, i - 32)) return false;
    decoded = id;
    return true;
}
bool CheckedAdd(uint64_t& value, uint64_t amount)
{
    if (amount > std::numeric_limits<uint64_t>::max() - value) return false;
    value += amount;
    return true;
}
int64_t ClockNowNs()
{
    if (!std::chrono::steady_clock::is_steady) return kUnobservedNs;
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const long double ns = std::chrono::duration<long double, std::nano>(elapsed).count();
    if (!std::isfinite(ns) || ns < 0 || ns >= static_cast<long double>(INT64_MAX)) return kUnobservedNs;
    // The floating conversion guards the representable range only. Preserve the
    // integer clock's resolution (including when uptime exceeds 2^53 ns).
    return std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
}
Ledger::Ledger(std::size_t capacity) : capacity_(std::min(capacity, kRecordCapacity)) {}
bool Ledger::Initialize(uint64_t run, Mode mode)
{
    // Reinitialization is not permitted: immutable run identity lasts for the process.
    if (run_ != 0 || run == 0) return false;
    run_ = run;
    for (const auto& spec : cases) for (unsigned p = 0; p != 2; ++p) {
        const auto c = static_cast<unsigned>(spec.id);
        const auto phase = static_cast<Phase>(p);
        offsets_[c][p] = size_;
        counts_[c][p] = PlannedCount(spec, phase, mode);
        for (uint32_t sequence = 1; sequence <= counts_[c][p]; ++sequence) {
            if (size_ >= capacity_) { Add(counters_.lost_records); overflow_ = true; continue; }
            auto& record = records_[size_++];
            record.identity = {run, spec.id, phase, sequence};
            record.business_bytes = spec.business_bytes; record.vector_bytes = spec.vector_bytes; record.serialized_bytes = spec.serialized_bytes;
        }
    }
    return !overflow_;
}
SampleRecord* Ledger::Find(const Identity& id)
{
    return const_cast<SampleRecord*>(static_cast<const Ledger&>(*this).Find(id));
}
const SampleRecord* Ledger::Find(const Identity& id) const
{
    const auto c = static_cast<unsigned>(id.case_id), p = static_cast<unsigned>(id.phase);
    if (id.run != run_ || c >= cases.size() || p > 1 || id.sequence == 0 || id.sequence > counts_[c][p]) return nullptr;
    const auto offset = offsets_[c][p] + id.sequence - 1;
    if (offset >= size_ || !(records_[offset].identity == id)) return nullptr;
    return &records_[offset];
}
void Ledger::MarkAttempt(const Identity& id, int64_t before_ns)
{
    if (auto* row = Find(id)) {
        if (row->flags & Attempted) { RecordError(1, id, before_ns); return; }
        row->flags = (row->flags | Attempted) & ~NotAttempted;
        row->write_before_ns = before_ns;
    } else { Add(counters_.out_of_range); }
}
void Ledger::MarkTimeout(const Identity& id)
{
    if (auto* row = Find(id)) row->flags |= TimedOut;
    else Add(counters_.out_of_range);
}
Arrival Ledger::Observe(const Identity& id, int64_t take_ns, bool exact_source, bool active)
{
    if (id.run != run_) { Add(counters_.wrong_run); return Arrival::WrongRun; }
    auto* row = Find(id);
    if (!row) { Add(counters_.out_of_range); return Arrival::OutOfRange; }
    if (!exact_source) { Add(counters_.wrong_source); return Arrival::WrongSource; }
    if (row->flags & Validated) {
        Add(row->duplicates); Add(counters_.duplicates);
        return Arrival::Duplicate;
    }
    if (!active && !(row->flags & Attempted)) { Add(counters_.inactive); return Arrival::Inactive; }
    row->first_take_ns = take_ns;
    row->flags |= Validated;
    if (GetCase(id.case_id).kind == Kind::Rtt && (row->flags & Attempted) &&
            (row->write_before_ns < 0 || take_ns <= row->write_before_ns)) {
        row->flags |= InvalidClock;
        Add(counters_.invalid_clock);
    }
    if (!active || (row->flags & TimedOut)) {
        MarkLate(id);
        return Arrival::Late;
    }
    return Arrival::First;
}
void Ledger::MarkLate(const Identity& id, bool after_observation)
{
    auto* row = Find(id);
    if (!row) { Add(counters_.out_of_range); return; }
    if (!(row->flags & Validated)) { CountInvalid(); return; }
    if (!(row->flags & Late)) { row->flags |= Late; Add(counters_.late); }
    if (after_observation) row->flags |= AfterObservation;
}
bool Ledger::ReserveEcho(const Identity& id, bool stage_allows_echo)
{
    auto* row = Find(id);
    if (!row || !stage_allows_echo || GetCase(id.case_id).kind != Kind::Rtt ||
            !(row->flags & Validated) || (row->flags & (EchoReserved | Late | AfterObservation | Invalid))) return false;
    row->flags |= EchoReserved;
    return true;
}
void Ledger::RecordError(uint32_t reason, const Identity& id, int64_t time_ns)
{
    Add(counters_.errors);
    if (error_size_ == errors_.size()) { Add(counters_.lost_error_details); overflow_ = true; return; }
    errors_[error_size_++] = {id, time_ns, reason};
}
void Ledger::CountInvalid() { Add(counters_.invalid); }
void Ledger::Add(uint64_t& value, uint64_t amount) { if (!CheckedAdd(value, amount)) overflow_ = true; }
bool SuccessfulRtt(const SampleRecord& row)
{
    return GetCase(row.identity.case_id).kind == Kind::Rtt && row.write_rc == 0 &&
           (row.flags & Attempted) && (row.flags & Validated) &&
           !(row.flags & (TimedOut | Late | AfterObservation | Invalid | InvalidClock)) &&
           row.write_before_ns >= 0 && row.first_take_ns > row.write_before_ns;
}
Statistics ComputeStatistics(int64_t* values_ns, std::size_t count)
{
    Statistics result;
    if (!values_ns || count == 0 || count > kRecordCapacity) return result;
    long double total = 0;
    for (std::size_t i = 0; i < count; ++i) { if (values_ns[i] < 0) return result; total += values_ns[i]; }
    std::sort(values_ns, values_ns + count);
    result.observed = true; result.count = count; result.minimum_ns = values_ns[0]; result.maximum_ns = values_ns[count - 1];
    result.mean_ns = static_cast<double>(total / count);
    result.p50_ns = values_ns[(50 * count + 99) / 100 - 1];
    result.p95_ns = values_ns[(95 * count + 99) / 100 - 1];
    result.p99_ns = values_ns[(99 * count + 99) / 100 - 1];
    return result;
}
Rate RatePerSecond(uint64_t count, int64_t begin_ns, int64_t end_ns)
{
    if (begin_ns < 0 || end_ns <= begin_ns) return {};
    return {true, static_cast<double>(static_cast<long double>(count) * 1000000000.0L / (end_ns - begin_ns))};
}
Rate BusinessGoodput(uint64_t unique_samples, uint32_t business_bytes, int64_t begin_ns, int64_t end_ns)
{
    if (business_bytes != 0 && unique_samples > UINT64_MAX / business_bytes) return {};
    return RatePerSecond(unique_samples * business_bytes, begin_ns, end_ns);
}
AbsolutePacer::AbsolutePacer(int64_t begin_ns, uint32_t hz, uint32_t count)
    : begin_ns_(begin_ns), period_ns_(0), end_ns_(begin_ns), count_(count), valid_(false)
{
    if (begin_ns < 0 || hz == 0 || hz > 1000000000u || 1000000000u % hz != 0 || count == 0) return;
    period_ns_ = 1000000000 / hz;
    if (count > static_cast<uint64_t>(INT64_MAX - begin_ns) / period_ns_) return;
    end_ns_ = begin_ns + period_ns_ * count;
    valid_ = true;
}
PaceDecision AbsolutePacer::Poll(int64_t now_ns)
{
    PaceDecision result;
    if (!valid_ || next_ >= count_) { result.finished = true; return result; }
    if (now_ns >= end_ns_) {
        result.finished = true; result.missed_first = next_ + 1; result.missed_count = count_ - next_; next_ = count_;
        return result;
    }
    const auto next_ns = begin_ns_ + period_ns_ * next_;
    result.next_ns = next_ns;
    if (now_ns < next_ns) return result;
    const auto current = static_cast<uint32_t>((now_ns - begin_ns_) / period_ns_);
    result.ready = true; result.sequence = current + 1;
    if (current > next_) { result.missed_first = next_ + 1; result.missed_count = current - next_; }
    next_ = current + 1;
    result.next_ns = begin_ns_ + period_ns_ * next_;
    return result;
}
std::size_t MeasurementStorageBytes(std::size_t inbound_capacity, std::size_t outbound_capacity)
{
    if (inbound_capacity > kInboundCapacity || outbound_capacity > kOutboundCapacity) return 0;
    // The fixed 256 KiB covers errors, resources (<=64), bounded statistics scratch,
    // offsets, metadata and counters. Ledger's row and auxiliary sizes are checked.
    static_assert(sizeof(Ledger) <= kRecordCapacity * sizeof(SampleRecord) + 262144, "bounded ledger auxiliary storage");
    return kRecordCapacity * sizeof(SampleRecord) + inbound_capacity + outbound_capacity + 262144;
}
}
