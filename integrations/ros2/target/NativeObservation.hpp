#pragma once
#include <string>
#include "../StartupDiagnostics.hpp"
#include "../NativePrefix.hpp"
#include "../../../probes/V1LocatorObservation.hpp"
#include "../../../probes/SedpEndpointObservation.hpp"
namespace fastdds_dsoftbus::ros2_target_observation {
struct V1LocatorSnapshot {
    struct Advert {std::uint64_t observation=0;bool sending=false;std::int32_t socket=-1;experiment::SpdpLocatorObservation value{};};
    struct Selected {std::uint32_t kind=0,port=0;std::array<std::uint8_t,16> address{};};
    std::array<Advert,256> adverts{};std::array<Selected,256> selected{};
    std::size_t advert_count=0,selected_count=0;
    std::uint64_t faults=0;
};
bool CaptureV1(V1LocatorSnapshot&) noexcept;
struct EndpointSnapshot {std::array<experiment::SedpEndpoint,64> values{};std::size_t count=0;bool complete=true;};
bool CaptureEndpoints(EndpointSnapshot&) noexcept;
struct SubmissionSnapshot {
    struct Item {std::uint64_t observation=0,last_observation=0,attempts=0,sender=0;std::array<std::uint8_t,16> writer{};std::int32_t high=0,status=0;std::uint32_t low=0;bool route_known=false,route_available=false;std::uint64_t route_revision=0;bool queue_admitted=false;};
    std::array<Item,512> values{};std::size_t count=0;std::uint64_t overflow=0,uncorrelated=0;
    // Local admission results survive encrypted, fragmented and noncontiguous
    // layouts. They do not assert native-call entry or business delivery.
    struct Result {std::uint64_t operation=0,sender=0,bytes=0,route_revision=0;
        int status=-1,queue_result=-1;bool queue_known=false,route_known=false,
        route_state_known=false,route_available=false,decode_complete=false;};
    std::uint64_t total=0,refused=0,counter_overflow=0,unknown_status=0;
    std::array<std::uint64_t,11> status_counts{};
    std::array<std::uint64_t,7> queue_counts{};
    std::uint64_t queue_unknown=0,queue_invalid=0;
    bool first_refusal_present=false;Result first_refusal{};
};
bool CaptureSubmissions(SubmissionSnapshot&) noexcept;
// A value copy made by the Host observer from the original ReceiveDecision.
// No dependency on a Runtime, retained identity handle or borrowed payload.
struct ReceiveDecisionSnapshot {
    struct Item {
        int layer=-1,branch=-1,role=-1,phase=-1,status=-1,stage=-1,reason=-1,
            result=-1,native_status=-1,application_phase=-1,receiver_phase=-1;
        std::int32_t socket=-1;
        std::uint64_t sequence=0,record_incarnation=0,correlation=0,bytes=0,
            refused_receive_total=0,native_slot=0,native_incarnation=0,
            native_generation=0,native_route_version=0,application_incarnation=0,
            logical_token=0,receiver_incarnation=0;
        bool rejected=false,branch_known=false,bridge_called=false,refused_receive=false,
            status_known=false,result_known=false,native_status_known=false,
            native_known=false,application_known=false,receiver_known=false,
            capacity_known=false,lifecycle_known=false,stopped=false,record_limit_known=false;
        std::size_t record_limit=0,records_used=0,records_limit=0,bytes_used=0,bytes_limit=0,
            local_records_used=0,local_records_limit=0,local_bytes_used=0,local_bytes_limit=0,
            copying_used=0,copying_limit=0,application_copying=0,application_copying_limit=0;
        char stage_name[24]{},reason_name[40]{};
    };
    std::uint64_t total=0,refused=0,counter_overflow=0,unknown_branch=0,unknown_reason=0,capture_dropped=0;
    std::array<std::uint64_t,16> branch_counts{};
    std::array<std::uint64_t,64> reason_counts{};
    bool first_refusal_present=false;Item first_refusal{};
};
void ObserveReceiveDecision(const ReceiveDecisionSnapshot::Item&) noexcept;
bool CaptureReceiveDecisions(ReceiveDecisionSnapshot&) noexcept;
bool ExportAdmissions() noexcept; // After Host pump/link joins; scalar metadata only.
void Begin(const std::string&, unsigned);
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void SetSourceForHostInjection() noexcept;
#endif
bool Capture(ros2_diagnostics::Snapshot&) noexcept;
enum class CaptureState { Ready, Busy, Unavailable, Error };
CaptureState TryCapture(ros2_diagnostics::Snapshot&) noexcept;
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void WithStartupGateForHostTest(void (*)(void*), void*);
#endif
bool CapturePrefix(ros2_diagnostics::NativePrefix&) noexcept;
bool ExportPending(ros2_diagnostics::Writer=ros2_diagnostics::Stdout,void* context=nullptr) noexcept; // Control/pump context only; never callback I/O.
bool Flush(); // Control thread only, after local close; not SDK drain proof.
}
