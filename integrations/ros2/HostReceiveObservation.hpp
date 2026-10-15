#pragma once
#include <fastdds_dsoftbus/dsoftbus/ApplicationAttachments.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostReceiveJournal.hpp>
#include <cstdio>
#include <stdexcept>

namespace fastdds_dsoftbus::ros2_host_receive {
namespace rx=fastdds_dsoftbus::dsoftbus::host_rx_observation;
using namespace fastdds_dsoftbus::dsoftbus;
template<class Map> bool ConfigurationEnabled(const Map& values) {
    const auto entry=values.find("host_receive_observation");
    if(entry==values.end()||entry->second=="0")return false;
    if(entry->second=="1")return true;
    throw std::runtime_error("host_receive_observation requires 0 or 1");
}
class Observer final:public ProcessReceiveObserver {
public:
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& d,const std::uint8_t*,std::size_t) noexcept override {
        rx::Event e;e.kind=rx::Kind::Stage;e.stage=static_cast<int>(stage);
        e.scope=d.observation_scope;e.record=d.observation_record;e.correlation=d.correlation;
        e.application=d.identity.application_incarnation;e.receiver=d.identity.receiver_incarnation;
        e.logical_token=d.identity.logical_token;e.size=d.size;e.ingress=rx::CurrentIngress();rx::Append(e);
    }
    void OnDecision(const ReceiveDecision& d) noexcept override {
        rx::Event e;e.kind=rx::Kind::Decision;e.decision=d;
        e.scope=d.bridge.observation_scope;e.record=d.bridge.observation_record;e.gate=d.bridge.observation_gate;
        e.correlation=d.correlation;e.size=d.size;e.ingress=rx::CurrentIngress();rx::Append(e);
    }
};
inline const char* Name(rx::Kind kind) noexcept {
    switch(kind){
    case rx::Kind::Charge:return "CHARGE";case rx::Kind::CapacityRefusal:return "CAPACITY_REFUSAL";
    case rx::Kind::Queue:return "QUEUE";case rx::Kind::Take:return "TAKE";case rx::Kind::Enter:return "ENTER";
    case rx::Kind::Requeue:return "REQUEUE";case rx::Kind::RefundRecord:return "REFUND_RECORD";
    case rx::Kind::RefundCapture:return "REFUND_CAPTURE";case rx::Kind::Stage:return "STAGE";
    case rx::Kind::Decision:return "DECISION";
    }return "UNKNOWN";
}
// Only after all callback/producer/pump threads join. No native pin or payload
// is retained. Ledger occupancy and actual payload destruction are distinct.
inline bool Export(const char* run,unsigned side,unsigned long pid) noexcept {
    if(!rx::Enabled())return true;
    const auto reserved=rx::journal.Reserved();std::uint64_t published=0;bool output_ok=true;
    for(std::size_t i=0;i<rx::capacity&&i<reserved;++i){rx::Event e;if(!rx::journal.Read(i,e))continue;++published;
        const auto& d=e.decision;const auto& a=d.bridge;
        if(std::fprintf(stdout,
            "HOST_RX_EVENT schema=1 run=%s side=%c pid=%lu index=%zu kind=%s scope=%llu record=%llu gate=%llu correlation=%llu application=%llu receiver=%llu logical_token=%llu local=%zu size=%zu records=%zu bytes=%zu copying=%zu queued=%zu taken=%zu delivering=%zu local_records=%zu local_bytes=%zu records_limit=%zu bytes_limit=%zu local_records_limit=%zu local_bytes_limit=%zu copying_limit=%zu application_copying_known=%d application_copying=%zu application_copying_limit=%zu gate_capacity_mask=%u gate_capacity_reason=%d phase=%d stage=%d ingress=%llu ingress_socket=%d ingress_epoch=%llu ingress_handle=%llu ingress_size=%zu layer=%d branch=%d decision_sequence=%llu binding_record=%llu decision_socket=%d role=%d decision_phase=%d bridge_called=%d refused=%d refused_total=%zu status_known=%d status=%d admission_stage=%d reason=%d result_known=%d result=%d capacity_known=%d capacity_mask=%u capacity_records=%zu capacity_bytes=%zu capacity_local_records=%zu capacity_local_bytes=%zu capacity_copying=%zu\n",
            run,side?'B':'A',pid,i,Name(e.kind),e.scope,e.record,e.gate,e.correlation,e.application,e.receiver,e.logical_token,
            e.local,e.size,e.records,e.bytes,e.copying,e.queued,e.taken,e.delivering,e.local_records,e.local_bytes,
            e.records_limit,e.bytes_limit,e.local_records_limit,e.local_bytes_limit,e.copying_limit,int(e.application_copying_known),e.application_copying,e.application_copying_limit,e.capacity_mask,e.capacity_reason,e.phase,e.stage,
            e.ingress.ordinal,e.ingress.socket,e.ingress.epoch,e.ingress.handle,e.ingress.size,
            static_cast<int>(d.layer),static_cast<int>(d.branch),d.sequence,d.record_incarnation,d.socket,d.role,d.phase,
            int(d.bridge_called),int(d.refused_receive),d.refused_receive_total,int(d.status_known),d.status,
            static_cast<int>(a.stage),static_cast<int>(a.reason),int(a.result_known),a.result,int(a.capacity_known),a.capacity_mask,
            a.records_used,a.bytes_used,a.local_records_used,a.local_bytes_used,a.copying_used)<0)output_ok=false;
    }
    const bool complete=rx::journal.Complete()&&published==reserved&&output_ok;
    if(std::fprintf(stdout,"HOST_RX_CAPTURE schema=1 run=%s side=%c pid=%lu capacity=%zu reserved=%llu published=%llu complete=%d ordering=BUDGET_GATE_PER_SCOPE payload=NOT_CAPTURED native=NOT_VERIFIED\n",
            run,side?'B':'A',pid,rx::capacity,reserved,published,int(complete))<0)output_ok=false;
    return std::fflush(stdout)==0&&std::ferror(stdout)==0&&complete&&output_ok;
}
}
