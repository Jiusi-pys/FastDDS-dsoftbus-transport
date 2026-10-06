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
};
bool CaptureSubmissions(SubmissionSnapshot&) noexcept;
void Begin(const std::string&, unsigned);
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void SetSourceForHostInjection() noexcept;
#endif
bool Capture(ros2_diagnostics::Snapshot&) noexcept;
bool CapturePrefix(ros2_diagnostics::NativePrefix&) noexcept;
bool ExportPending(ros2_diagnostics::Writer=ros2_diagnostics::Stdout,void* context=nullptr) noexcept; // Control/pump context only; never callback I/O.
bool Flush(); // Control thread only, after local close; not SDK drain proof.
}
