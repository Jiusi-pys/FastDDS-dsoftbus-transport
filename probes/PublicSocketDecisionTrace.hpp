#pragma once
#include <mdds/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
namespace mdds::dsoftbus::dds_experiment {
void RecordDecision(const ReceiveDecision&,std::uint64_t ingress,unsigned side) noexcept;
void DumpDecisions() noexcept;
}
