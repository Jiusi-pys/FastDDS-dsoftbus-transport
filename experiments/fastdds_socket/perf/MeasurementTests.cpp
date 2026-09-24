#include "Measurement.hpp"
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
using namespace mdds::experiment::perf;
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main()
{
    try {
        Require(GetCase(CaseId::RTT_1K).vector_bytes == 1056, "frozen 1K vector");
        Require(GetCase(CaseId::LOAD_256K).serialized_bytes == 262188, "XCDR1 bytes");
        Require(CaseOrder(2)[0] == CaseId::RTT_4K && CaseOrder(3)[0] == CaseId::RTT_256K, "repeat rotation");
        uint64_t measure_vectors = 0, trace_vectors = 0;
        for (const auto case_id : CaseOrder(1)) {
            const auto& spec = GetCase(case_id);
            const uint64_t directions = spec.kind == Kind::Rtt ? 2 : 1;
            measure_vectors += (spec.formal_count + spec.warmup_count) * uint64_t{spec.vector_bytes} * directions;
            trace_vectors += spec.trace_count * uint64_t{spec.vector_bytes} * directions;
        }
        Require(measure_vectors == 75805344 && trace_vectors == 1656000, "frozen payload reservation budgets");
        Identity id{0x0123456789abcdefULL, CaseId::RTT_1K, Phase::Formal, 1}, decoded;
        std::vector<uint8_t> bytes;
        Require(FillPayload(id, bytes) && bytes.size() == 1056, "fill bounded full envelope");
        Require(bytes[0] == 0x4d && bytes[4] == 1 && bytes[8] == 1 && bytes[15] == 0xef, "network byte order");
        Require(ValidatePayload(1, bytes, decoded) && decoded == id, "decode same identity");
        bytes.back() ^= 1;
        Require(!ValidatePayload(1, bytes, decoded), "last byte corruption rejected");
        bytes.back() ^= 1;
        Require(!ValidatePayload(2, bytes, decoded), "index sequence mismatch rejected");
        bytes[31] = 1;
        Require(!ValidatePayload(1, bytes, decoded), "reserved bytes rejected");
        Require(FillPayload(id, bytes), "restore");
        bytes.resize(1057);
        Require(!ValidatePayload(1, bytes, decoded), "wrong decoded length rejected");
        auto ledger = std::make_unique<Ledger>();
        Require(ledger->Initialize(id.run, Mode::Measure) && ledger->size() == 3186, "all planned rows exist");
        auto* row = ledger->Find(id);
        Require(row && (row->flags & NotAttempted), "initial not attempted retained");
        Identity old = id; --old.run;
        Require(ledger->Observe(old, 100, true, true) == Arrival::WrongRun && ledger->Find(old) == nullptr, "old run never adopted");
        Require(ledger->Observe(id, 100, true, false) == Arrival::Inactive, "future stage never adopted");
        Require(ledger->Observe(id, 100, false, true) == Arrival::WrongSource, "wrong exact writer source");
        Require(ledger->Observe(id, 100, true, true) == Arrival::First, "first full valid take");
        Require(ledger->ReserveEcho(id, true) && !ledger->ReserveEcho(id, true), "at most one echo attempt even failure");
        Require(ledger->Observe(id, 200, true, true) == Arrival::Duplicate && row->first_take_ns == 100, "duplicate cannot overwrite first arrival");
        Identity id2 = id; id2.sequence = 2;
        ledger->MarkAttempt(id2, 300); ledger->Find(id2)->write_rc = 0; ledger->MarkTimeout(id2);
        Require(ledger->Observe(id2, 400, true, true) == Arrival::Late && (ledger->Find(id2)->flags & TimedOut), "timeout retained after late take");
        Require(!ledger->ReserveEcho(id2, false), "stopped stage never echoes");
        Require(!SuccessfulRtt(*ledger->Find(id2)), "timeout excluded from success RTT");
        const Identity historical{id.run, CaseId::LOAD_1K, Phase::Formal, 1};
        Require(ledger->Observe(historical, 500, true, true) == Arrival::First, "historical stage admits its original identity");
        const auto late_before = ledger->counters().late;
        ledger->MarkLate(historical);
        Require((ledger->Find(historical)->flags & Late) && ledger->counters().late == late_before + 1, "explicit historical late updates checked total");
        ledger->MarkLate(historical, true);
        ledger->MarkLate(historical, true);
        Require((ledger->Find(historical)->flags & AfterObservation) && ledger->counters().late == late_before + 1, "after observation flag and repeated late count only once");
        ledger->MarkLate(id2, true);
        Require(ledger->counters().late == late_before + 1 && (ledger->Find(id2)->flags & TimedOut), "timeout late observation is not counted twice");
        const Identity not_received{id.run, CaseId::LOAD_1K, Phase::Formal, 2};
        ledger->MarkLate(not_received);
        Require(!(ledger->Find(not_received)->flags & Late) && ledger->counters().late == late_before + 1, "never received identity cannot fabricate late delivery");
        row->flags |= Attempted; row->write_rc = 1; row->write_before_ns = 10;
        Require(!SuccessfulRtt(*row), "delivered write error excluded from success RTT");
        row->write_rc = 0;
        Require(SuccessfulRtt(*row), "unique timely valid write success accepted");
        row->first_take_ns = row->write_before_ns;
        Require(!SuccessfulRtt(*row), "equal timestamps excluded from successful RTT");
        row->first_take_ns = row->write_before_ns - 1;
        Require(!SuccessfulRtt(*row), "reversed timestamps excluded from successful RTT");
        row->write_before_ns = 0; row->first_take_ns = 1;
        Require(SuccessfulRtt(*row), "absolute timestamp zero remains valid with positive RTT");
        const auto clock_invalid_before = ledger->counters().invalid_clock;
        for (uint32_t seq = 3; seq <= 4; ++seq) {
            const Identity clock_id{id.run, CaseId::RTT_1K, Phase::Formal, seq};
            ledger->MarkAttempt(clock_id, 600);
            auto* clock_row = ledger->Find(clock_id); clock_row->write_rc = 0;
            const int64_t take_ns = seq == 3 ? 600 : 599;
            Require(ledger->Observe(clock_id, take_ns, true, true) == Arrival::First, "clock anomaly retains raw unique delivery");
            Require(clock_row->first_take_ns == take_ns && (clock_row->flags & Validated) &&
                    (clock_row->flags & InvalidClock) && !SuccessfulRtt(*clock_row), "clock anomaly visible but excluded from RTT statistics");
            Require(ledger->Observe(clock_id, 650, true, true) == Arrival::Duplicate && clock_row->first_take_ns == take_ns,
                    "later duplicate cannot replace anomalous first clock");
        }
        Require(ledger->counters().invalid_clock == clock_invalid_before + 2, "invalid clock counter exactly once per unique anomalous RTT");
        auto small = std::make_unique<Ledger>(2);
        Require(!small->Initialize(id.run, Mode::Measure) && small->overflowed() && small->size() == 2, "record overflow explicit no expansion");
        for (unsigned i = 0; i != 129; ++i) ledger->RecordError(7, id, 50);
        Require(ledger->error_count() == 128 && ledger->counters().errors == 129 && ledger->overflowed(), "error detail cap and total retained");
        uint64_t counter = std::numeric_limits<uint64_t>::max();
        Require(!CheckedAdd(counter, 1) && counter == std::numeric_limits<uint64_t>::max(), "checked counter overflow");
        std::array<int64_t, 5> raw{{9, 1, 5, 3, 7}};
        auto stats = ComputeStatistics(raw.data(), raw.size());
        Require(stats.observed && stats.count == 5 && stats.minimum_ns == 1 && stats.mean_ns == 5 && stats.p50_ns == 5 && stats.p95_ns == 9 && stats.p99_ns == 9, "nearest rank no trimming");
        Require(!ComputeStatistics(nullptr, 0).observed, "empty population unavailable");
        Require(!RatePerSecond(1, 1, 1).observed && !RatePerSecond(1, 2, 1).observed, "nonpositive denominator unavailable");
        Require(RatePerSecond(10, 0, 2000000000).value == 5, "actual interval separate from requested rate");
        Require(BusinessGoodput(10, 1024, 0, 2000000000).value == 5120, "business bytes/sec excludes envelope");
        AbsolutePacer pacer(1000000000, 10, 10);
        auto first = pacer.Poll(1000000000);
        Require(first.ready && first.sequence == 1, "first absolute slot");
        auto slow = pacer.Poll(1350000000);
        Require(slow.ready && slow.sequence == 4 && slow.missed_first == 2 && slow.missed_count == 2, "miss slots without catchup");
        Require(!pacer.Poll(1350000000).ready, "no repeated catchup burst");
        auto end = pacer.Poll(2000000000);
        Require(end.finished && !end.ready && end.missed_count == 6, "horizon skips tail not final burst");
        Require(MeasurementStorageBytes(1048576, 262176) <= kStorageCap && MeasurementStorageBytes(1048577,262176) == 0, "actual payload capacity contract");
        Require(ClockNowNs() >= 0 && sizeof(SampleRecord) <= 256, "steady nanoseconds and bounded consolidated record");
        std::printf("CORE_STORAGE record_bytes=%zu ledger_bytes=%zu capacity=%zu planned_measure=3186 declared_bytes=%zu cap_bytes=%zu\n",
                sizeof(SampleRecord), sizeof(Ledger), kRecordCapacity, MeasurementStorageBytes(kInboundCapacity, kOutboundCapacity), kStorageCap);
        std::puts("PASS measurement core: envelope/full pattern, identity/duplicates/timeout, bounded rows/errors, quantiles, pacing, rates/storage");
        return 0;
    } catch (const std::exception& e) { std::printf("FAIL %s\n", e.what()); return 1; }
}
