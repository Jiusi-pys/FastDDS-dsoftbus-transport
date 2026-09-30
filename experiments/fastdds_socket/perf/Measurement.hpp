#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fastdds_dsoftbus::experiment::perf {
constexpr std::size_t kRecordCapacity = 8192, kErrorCapacity = 128;
constexpr std::size_t kInboundCapacity = 1048576, kOutboundCapacity = 262176, kStorageCap = 4194304;
constexpr int64_t kUnobservedNs = -1;
constexpr int32_t kUnobservedRc = INT32_MIN;
enum class CaseId : uint8_t { RTT_1K, LOAD_1K, RTT_4K, LOAD_4K, RTT_256K, LOAD_256K };
enum class Phase : uint8_t { Warmup, Formal };
enum class Mode : uint8_t { Trace, Measure };
enum class Kind : uint8_t { Rtt = 1, Load = 2 };
struct CaseSpec {
    CaseId id; const char* name; Kind kind;
    uint32_t business_bytes, vector_bytes, serialized_bytes, formal_count, warmup_count, hz, trace_count;
};
const CaseSpec& GetCase(CaseId id);
std::array<CaseId, 6> CaseOrder(uint32_t repeat);
uint32_t PlannedCount(const CaseSpec& spec, Phase phase, Mode mode);
const char* PhaseName(Phase phase);
const char* ModeName(Mode mode);
struct Identity {
    uint64_t run = 0;
    CaseId case_id = CaseId::RTT_1K;
    Phase phase = Phase::Warmup;
    uint32_t sequence = 0;
    bool operator==(const Identity& other) const;
};
bool FillPayload(const Identity& id, std::vector<uint8_t>& bytes);
bool ValidatePayload(uint32_t index, const std::vector<uint8_t>& bytes, Identity& decoded);
bool CheckedAdd(uint64_t& value, uint64_t amount);
int64_t ClockNowNs();

enum RecordFlag : uint32_t {
    NotAttempted = 1u << 0, Attempted = 1u << 1, Validated = 1u << 2,
    TimedOut = 1u << 3, Late = 1u << 4, MissedSlot = 1u << 5,
    EchoReserved = 1u << 6, AfterObservation = 1u << 7, Invalid = 1u << 8,
    InvalidClock = 1u << 9
};
struct SampleRecord {
    Identity identity;
    int64_t write_before_ns = kUnobservedNs, write_after_ns = kUnobservedNs;
    int64_t first_take_ns = kUnobservedNs;
    int64_t echo_before_ns = kUnobservedNs, echo_after_ns = kUnobservedNs;
    std::array<uint8_t, 16> writer_guid{}, source_guid{}, echo_guid{};
    int64_t writer_sn = -1, source_sn = -1, echo_sn = -1;
    uint64_t duplicates = 0;
    uint32_t flags = NotAttempted, business_bytes = 0, vector_bytes = 0, serialized_bytes = 0;
    int32_t write_rc = kUnobservedRc, echo_rc = kUnobservedRc;
};
static_assert(sizeof(SampleRecord) <= 256, "consolidated sample storage contract");
struct ErrorRecord { Identity identity; int64_t time_ns; uint32_t reason; };
struct Counters {
    uint64_t wrong_run = 0, wrong_source = 0, out_of_range = 0, inactive = 0;
    uint64_t duplicates = 0, invalid = 0, late = 0, errors = 0, lost_records = 0, lost_error_details = 0, invalid_clock = 0;
};
enum class Arrival { First, Duplicate, WrongRun, WrongSource, OutOfRange, Inactive, Late };
// Single control-thread owner. No per-sample heap allocation; each expected identity
// exists before timing, including missed and never-attempted submissions.
class Ledger {
public:
    explicit Ledger(std::size_t capacity = kRecordCapacity);
    bool Initialize(uint64_t run, Mode mode);
    SampleRecord* Find(const Identity& id);
    const SampleRecord* Find(const Identity& id) const;
    void MarkAttempt(const Identity& id, int64_t before_ns);
    void MarkTimeout(const Identity& id);
    // Only received/validated identities can be marked late; count each identity
    // once even when it later crosses the after-observation boundary.
    void MarkLate(const Identity& id, bool after_observation = false);
    // Caller fully validates bytes and exact DDS writer identity before this call.
    // active=false prevents future-stage admission; an already attempted/taken old
    // identity can still acquire a late observation without adopting a new stage.
    Arrival Observe(const Identity& id, int64_t take_ns, bool exact_source, bool active);
    bool ReserveEcho(const Identity& id, bool stage_allows_echo);
    void RecordError(uint32_t reason, const Identity& id, int64_t time_ns);
    void CountInvalid();
    bool overflowed() const { return overflow_; }
    const Counters& counters() const { return counters_; }
    std::size_t size() const { return size_; }
    std::size_t error_count() const { return error_size_; }
    SampleRecord* data() { return records_.data(); }
    const SampleRecord* data() const { return records_.data(); }
    const ErrorRecord* errors() const { return errors_.data(); }
private:
    void Add(uint64_t& value, uint64_t amount = 1);
    std::array<SampleRecord, kRecordCapacity> records_{};
    std::array<ErrorRecord, kErrorCapacity> errors_{};
    std::array<std::array<std::size_t, 2>, 6> offsets_{};
    std::array<std::array<uint32_t, 2>, 6> counts_{};
    Counters counters_{};
    std::size_t capacity_, size_ = 0, error_size_ = 0;
    uint64_t run_ = 0;
    bool overflow_ = false;
};
bool SuccessfulRtt(const SampleRecord& record);
struct Statistics {
    bool observed = false; std::size_t count = 0;
    int64_t minimum_ns = 0, p50_ns = 0, p95_ns = 0, p99_ns = 0, maximum_ns = 0;
    double mean_ns = 0;
};
// In-place sort of caller-owned bounded scratch storage; never allocates.
Statistics ComputeStatistics(int64_t* values_ns, std::size_t count);
struct Rate { bool observed = false; double value = 0; };
Rate RatePerSecond(uint64_t count, int64_t begin_ns, int64_t end_ns);
Rate BusinessGoodput(uint64_t unique_samples, uint32_t business_bytes, int64_t begin_ns, int64_t end_ns);
struct PaceDecision {
    bool ready = false, finished = false;
    uint32_t sequence = 0, missed_first = 0, missed_count = 0;
    int64_t next_ns = kUnobservedNs;
};
class AbsolutePacer {
public:
    AbsolutePacer(int64_t begin_ns, uint32_t hz, uint32_t count);
    PaceDecision Poll(int64_t now_ns);
private:
    int64_t begin_ns_, period_ns_, end_ns_;
    uint32_t count_, next_ = 0;
    bool valid_;
};
// Zero means actual capacities violate the frozen legitimate-workload contract.
std::size_t MeasurementStorageBytes(std::size_t inbound_capacity, std::size_t outbound_capacity);
}
