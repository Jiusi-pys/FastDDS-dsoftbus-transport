#include "PublicSocketDecisionTrace.hpp"
#include "PublicSocketDecisionStore.hpp"
#include "PublicSocketRuntimeTrace.hpp"
#include <cstdio>
#include <new>
namespace mdds::dsoftbus::dds_experiment {
#ifdef MDDS_PUBLIC_SOCKET_DECISION_DIAGNOSTICS
namespace {
constexpr std::size_t limit=16384;
using Store=DecisionStore<limit>;
Store& Decisions(){alignas(Store) static unsigned char memory[sizeof(Store)];static Store* p=new(memory) Store;return *p;}
const char* Branch(ReceiveDecisionBranch v) noexcept {
    constexpr const char* names[]={"UNKNOWN","BINDING_PHASE","NO_REGISTRATION","NATIVE_ADMISSION",
        "NATIVE_IDENTITY","LOCAL_MISMATCH","BRIDGE_ACCEPTED","BRIDGE_REFUSED","ADAPTER_STOPPED",
        "ADAPTER_PHASE","CHANNEL_CLOSED","FOREIGN_SOCKET","INVALID_BUFFER","ADAPTER_CAPACITY","FORWARDED"};
    const auto i=static_cast<std::size_t>(v);return i<sizeof(names)/sizeof(*names)?names[i]:"UNKNOWN";
}
}
#endif
void RecordDecision(const ReceiveDecision& d,std::uint64_t ingress,unsigned side) noexcept {
#ifdef MDDS_PUBLIC_SOCKET_DECISION_DIAGNOSTICS
    Decisions().Record(d,ingress,side,runtime_probe::DetailedObservation());
#else
    (void)d;(void)ingress;(void)side;
#endif
}
void DumpDecisions() noexcept {
#ifdef MDDS_PUBLIC_SOCKET_DECISION_DIAGNOSTICS
    const auto snapshot=Decisions().Capture(); // immutable prefix; no lock while printing
    for(std::size_t i=0;i<snapshot.count;++i){const auto& row=Decisions().At(i);const auto& d=row.decision;const auto& b=d.bridge;
        std::printf("RX_DECISION event=%zu side=%u ingress=%llu layer=%s sequence=%llu record=%llu role=%d socket=%d phase=%d size=%zu branch=%s bridge_called=%d refused_receive=%d refused_receive_total=%zu status_known=%d status=%d correlation=%llu stage=%s reason=%s result_known=%d result=%d record_limit_known=%d record_limit=%zu native_known=%d native_status_known=%d native_status=%d owner_slot=%llu owner=%llu generation=%llu route_version=%llu application_known=%d app=%llu logical_token=%llu receiver_known=%d receiver=%llu lifecycle_known=%d app_phase=%d receiver_phase=%d stopped=%d capacity_known=%d capacity_mask=%u records=%zu records_limit=%zu bytes=%zu bytes_limit=%zu local_records=%zu local_records_limit=%zu local_bytes=%zu local_bytes_limit=%zu copying=%zu copying_limit=%zu app_copying=%zu app_copying_limit=%zu candidate_count=%u\n",
            i+1,row.source,(unsigned long long)row.ingress,d.layer==ReceiveDecisionLayer::Binding?"BINDING":"ADAPTER",(unsigned long long)d.sequence,(unsigned long long)d.record_incarnation,d.role,d.socket,d.phase,d.size,Branch(d.branch),d.bridge_called,d.refused_receive,d.refused_receive_total,d.status_known,d.status,(unsigned long long)d.correlation,RxAdmissionStageName(b.stage),RxAdmissionReasonName(b.reason),b.result_known,b.result,b.record_limit_known,b.record_limit,b.native_known,b.native_status_known,static_cast<int>(b.native_status),(unsigned long long)b.native_slot,(unsigned long long)b.native_incarnation,(unsigned long long)b.native_generation,(unsigned long long)b.native_route_version,b.application_known,(unsigned long long)b.application_incarnation,(unsigned long long)b.logical_token,b.receiver_known,(unsigned long long)b.receiver_incarnation,b.lifecycle_known,b.application_phase,b.receiver_phase,b.stopped,b.capacity_known,b.capacity_mask,b.records_used,b.records_limit,b.bytes_used,b.bytes_limit,b.local_records_used,b.local_records_limit,b.local_bytes_used,b.local_bytes_limit,b.copying_used,b.copying_limit,b.application_copying,b.application_copying_limit,b.candidate_count);
    }
    for(std::size_t i=0;i<Store::branches;++i)std::printf("RX_DECISION_BRANCH branch=%s count=%zu\n",Branch(static_cast<ReceiveDecisionBranch>(i)),snapshot.branch_totals[i]);
    for(std::size_t i=0;i<Store::reasons;++i)if(snapshot.bridge_totals[i])std::printf("RX_DECISION_REASON reason=%s count=%zu\n",RxAdmissionReasonName(static_cast<RxAdmissionReason>(i)),snapshot.bridge_totals[i]);
    std::printf("RX_DECISION_SUMMARY count=%zu total=%zu overflow=%zu unknown=%zu limit=%zu storage_bytes=%zu omitted=%zu\n",snapshot.count,snapshot.total,snapshot.overflow,snapshot.unknown,limit,sizeof(Store),snapshot.omitted);
#endif
}
}
