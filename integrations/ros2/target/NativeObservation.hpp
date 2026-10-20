#pragma once
#include <limits>
#include <string>
#include "../StartupDiagnostics.hpp"
#include "../NativePrefix.hpp"
#include "../../../probes/V1LocatorObservation.hpp"
#include "../../../probes/SedpEndpointObservation.hpp"
namespace fastdds_dsoftbus::ros2_target_observation {
struct V1LocatorSnapshot {
    struct Advert {
        std::uint64_t observation=0;
        bool sending=false;
        std::int32_t socket=-1;
        experiment::SpdpLocatorObservation value{};
        std::uint64_t occurrences=0,last_observation=0;
        std::uint32_t last_sequence_high=0,last_sequence_low=0;
        std::size_t batch_size=0;
    };
    struct Selected {std::uint32_t kind=0,port=0;std::array<std::uint8_t,16> address{};};
    std::array<Advert,256> adverts{};std::array<Selected,256> selected{};
    std::size_t advert_count=0,selected_count=0;
    std::uint64_t faults=0;
    enum class Fault :std::size_t {InvalidFrame,UnsupportedFrame,ParserCapacity,AdvertCapacity,
                                 InvalidSelection,SelectionCapacity,CounterCapacity,Count};
    std::array<std::uint64_t,std::size_t(Fault::Count)> fault_counts{};
    std::uint64_t adverts_observed=0,adverts_repeated=0,batches_repeated=0;

    void RecordFault(Fault reason) noexcept {
        // Saturation stays nonzero and fatal; diagnostics must never wrap clean.
        constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();
        if(faults!=maximum)++faults;
        auto& count=fault_counts[std::size_t(reason)];
        if(count!=maximum)++count;
    }
    void RecordAdverts(std::uint64_t observation,bool sending,std::int32_t socket,
                       const experiment::SpdpLocatorBatch& batch) noexcept {
        if(batch.unsupported){RecordFault(Fault::UnsupportedFrame);return;}
        if(batch.overflow){RecordFault(Fault::ParserCapacity);return;}
        if(!batch.valid||batch.count>batch.values.size()){RecordFault(Fault::InvalidFrame);return;}
        if(!batch.count)return;
        constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();
        if(!observation){RecordFault(Fault::CounterCapacity);return;}
        if(batch.count>maximum-adverts_observed){RecordFault(Fault::CounterCapacity);return;}
        adverts_observed+=batch.count;
        for(std::size_t first=0;first<advert_count;) {
            const auto count=adverts[first].batch_size;
            // Only internally admitted, complete batches are stored. Fail closed
            // if the snapshot invariant ever becomes inconsistent.
            if(!count||count>advert_count-first){RecordFault(Fault::InvalidFrame);return;}
            bool same=count==batch.count;
            for(std::size_t j=0;same&&j<count;++j) {
                const auto& stored=adverts[first+j];
                const auto& a=stored.value;const auto& b=batch.values[j];
                same=stored.sending==sending&&stored.socket==socket&&a.writer==b.writer&&
                    a.pid==b.pid&&a.serialized_bytes==b.serialized_bytes&&a.kind==b.kind&&
                    a.address==b.address&&a.port==b.port&&a.data_submessage_offset==b.data_submessage_offset;
            }
            if(same) {
                if(batch.count>maximum-adverts_repeated||batches_repeated==maximum){
                    RecordFault(Fault::CounterCapacity);return;
                }
                for(std::size_t j=0;j<count;++j)if(adverts[first+j].occurrences==maximum){
                    RecordFault(Fault::CounterCapacity);return;
                }
                for(std::size_t j=0;j<count;++j) {
                    auto& stored=adverts[first+j];++stored.occurrences;
                    // Parser work can acquire the mutex out of observation-ID
                    // order. Keep coherent earliest/latest source batches.
                    if(observation<stored.observation){stored.observation=observation;stored.value=batch.values[j];}
                    if(observation>stored.last_observation){
                        stored.last_observation=observation;
                        stored.last_sequence_high=batch.values[j].sequence_high;
                        stored.last_sequence_low=batch.values[j].sequence_low;
                    }
                }
                adverts_repeated+=batch.count;++batches_repeated;return;
            }
            first+=count;
        }
        // Never append a partial batch: same-DATA meta/user evidence must remain
        // coherent. Distinct identities still exhaust the original 256 slots.
        if(batch.count>adverts.size()-advert_count){RecordFault(Fault::AdvertCapacity);return;}
        for(std::size_t j=0;j<batch.count;++j) {
            auto& stored=adverts[advert_count++];
            stored.observation=stored.last_observation=observation;
            stored.sending=sending;stored.socket=socket;stored.value=batch.values[j];
            stored.occurrences=1;stored.batch_size=batch.count;
            stored.last_sequence_high=stored.value.sequence_high;
            stored.last_sequence_low=stored.value.sequence_low;
        }
    }
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
