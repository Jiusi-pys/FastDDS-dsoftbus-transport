#pragma once
#include <cstddef>
#include <cstdint>
namespace mdds::dsoftbus::dds_experiment {
// Observation only, never consulted by any route/admission/protocol decision.
// Borrow ends at return. Called only after the shim's buffer validation.
bool DropRegressionRecord(std::int32_t,const std::uint8_t*,std::size_t) noexcept;
// Local post-entry observation identity only; NOT SDK original-event identity
// or a record incarnation. Correlates this callback decision with synchronous copy.
struct IngressObservationScope {
    explicit IngressObservationScope(std::uint64_t) noexcept;
    ~IngressObservationScope();
    IngressObservationScope(const IngressObservationScope&)=delete;
    IngressObservationScope& operator=(const IngressObservationScope&)=delete;
private:std::uint64_t previous_;
};
std::uint64_t SocketFrame(const char*,std::int32_t,const std::uint8_t*,std::size_t) noexcept;
}
