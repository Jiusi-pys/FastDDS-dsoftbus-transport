#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_PROCESS_CLOCK_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_PROCESS_CLOCK_HPP
#include <atomic>
#include <chrono>
#include <memory>
namespace fastdds_dsoftbus::dsoftbus {
// Controlled model time, or steady time by default. Reading never invokes a
// user callback or allocates; safe for short expiry checks under internal gates.
// An injected driver keeps ticks monotonic and in this steady duration domain.
class ProcessClock final
{
public:
    using Clock=std::chrono::steady_clock;
    using Tick=Clock::duration::rep;
    ProcessClock() noexcept = default;
    explicit ProcessClock(std::shared_ptr<const std::atomic<Tick>> ticks) noexcept : ticks_(std::move(ticks)) {}
    Clock::time_point Now() const noexcept
    {return ticks_?Clock::time_point(Clock::duration(ticks_->load())):Clock::now();}
private:
    std::shared_ptr<const std::atomic<Tick>> ticks_;
};
}
#endif
