#pragma once
// Test-executable diagnostics only. No NS_* POD, API table or return-code change.
#include <atomic>
#include <cstdint>
#include <string>

struct NS_RunHandle;
namespace stage1_startup {
enum class Stage : std::uint32_t {
    None=0, ExecuteGuard=1, ExecuteChecks=2, SessionConstruction=3,
    Configuration=4, RuntimeConstruction=5, GateRegistration=6,
    DescriptorCreation=7, FactoryLookup=8, XmlLoad=9, XmlMode=10,
    WorkerStart=11, ParticipantCreation=12, AfterParticipant=13
};
enum class Reason : std::uint32_t {
    None=0, LockBusy=1, HandleReused=2, GateRegistrationRejected=3,
    DescriptorNull=4, XmlLoadRejected=5, XmlModeMismatch=6,
    ParticipantNull=7, StatusThrown=8, UnexpectedException=9
};
enum class GateStage : std::uint32_t {None=0, ObserveListener=1, CheckListener=2, EmitEvent=3, WaitRelease=4, Completed=5};
enum class GateReason : std::uint32_t {None=0, ListenerRejected=1, EmitFailed=2, WaitFailed=3, UnexpectedException=4};
struct Snapshot {
    std::uint32_t available=0, entered_stage=0, first_reason=0, failure_stage=0;
    std::uint32_t gate_entered_stage=0, gate_reason=0, gate_failure_stage=0;
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"paired diagnostic CAS must not introduce a runtime lock");
class Recorder {
    std::atomic<std::uint32_t> entered_{0},gate_entered_{0};
    std::atomic<std::uint64_t> first_{0},gate_first_{0};
    static void first(std::atomic<std::uint64_t>& slot,std::uint32_t reason,std::uint32_t stage) noexcept {
        std::uint64_t empty=0;
        const auto pair=(std::uint64_t(stage)<<32)|reason;
        (void)slot.compare_exchange_strong(empty,pair,std::memory_order_acq_rel,std::memory_order_acquire);
    }
public:
    void enter(Stage stage) noexcept {entered_.store(static_cast<std::uint32_t>(stage),std::memory_order_release);}
    void gate_enter(GateStage stage) noexcept {gate_entered_.store(static_cast<std::uint32_t>(stage),std::memory_order_release);}
    void fail(Reason reason,Stage stage) noexcept {first(first_,static_cast<std::uint32_t>(reason),static_cast<std::uint32_t>(stage));}
    void gate_fail(GateReason reason,GateStage stage) noexcept {first(gate_first_,static_cast<std::uint32_t>(reason),static_cast<std::uint32_t>(stage));}
    Stage entered() const noexcept {return static_cast<Stage>(entered_.load(std::memory_order_acquire));}
    Snapshot snapshot() const noexcept {
        const auto a=first_.load(std::memory_order_acquire),g=gate_first_.load(std::memory_order_acquire);
        return {1,entered_.load(std::memory_order_acquire),std::uint32_t(a),std::uint32_t(a>>32),
          gate_entered_.load(std::memory_order_acquire),std::uint32_t(g),std::uint32_t(g>>32)};
    }
};
inline bool check(Recorder& r,bool value,Reason reason,Stage stage) noexcept {if(!value)r.fail(reason,stage);return value;}
inline bool gate_check(Recorder& r,bool value,GateReason reason,GateStage stage) noexcept {if(!value)r.gate_fail(reason,stage);return value;}
inline Snapshot snapshot_or_unknown(const Recorder* r) noexcept {return r?r->snapshot():Snapshot{};}
// Defined in consumer_run.cpp, linked directly with direct_main.cpp. Hidden by
// the unchanged executable visibility profile; never added to a public C API.
#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
Snapshot inspect(const NS_RunHandle*) noexcept;
inline std::string record(const Snapshot& s) {
    return "format=S1GOALCAUSE1\navailable="+std::to_string(s.available)+
      "\nentered_stage="+std::to_string(s.entered_stage)+"\nfirst_reason="+std::to_string(s.first_reason)+
      "\nfailure_stage="+std::to_string(s.failure_stage)+"\ngate_entered_stage="+std::to_string(s.gate_entered_stage)+
      "\ngate_reason="+std::to_string(s.gate_reason)+"\ngate_failure_stage="+std::to_string(s.gate_failure_stage)+"\n";
}
template<class Publish,class Fallback>
void publish(const Snapshot& s,Publish&& sink,Fallback&& unavailable) noexcept {
    try {sink(record(s));}
    catch(...) {try {unavailable();}catch(...) {}}
}
}
