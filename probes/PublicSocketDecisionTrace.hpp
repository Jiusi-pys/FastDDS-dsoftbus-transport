#pragma once
#include <fastdds_dsoftbus/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
void RecordDecision(const ReceiveDecision&,std::uint64_t ingress,unsigned side) noexcept;
void DumpDecisions() noexcept;
}
