#include <atomic>
#include <cstdint>
#include <cstddef>
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
std::atomic<std::uint64_t> event_count{0};
void TraceEvent(const char*,std::uint64_t,int,std::int32_t,std::int64_t,std::uint64_t) noexcept {++event_count;}
void TraceText(const char*,const char*) noexcept {++event_count;}
}
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
std::atomic<std::uint64_t> frame_count{0};
std::uint64_t SocketFrame(const char*,std::int32_t,const std::uint8_t*,std::size_t) noexcept {return ++frame_count;}
}
