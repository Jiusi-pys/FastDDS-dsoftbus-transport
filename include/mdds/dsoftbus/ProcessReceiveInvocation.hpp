#ifndef MDDS_DSOFTBUS_PROCESS_RECEIVE_INVOCATION_HPP
#define MDDS_DSOFTBUS_PROCESS_RECEIVE_INVOCATION_HPP
#include <mdds/dsoftbus/InputChannels.hpp>
namespace mdds::dsoftbus {
namespace detail {class ProcessReceiveBridge;struct ProcessReceiveExecution;}
// One already-admitted original receiver/native execution. External dispatch
// only; keep the original receiver alive through drain, even before Run starts.
// Exclusive object ownership; abandoning it settles leases without a callback.
class ProcessReceiveInvocation final
{
public:
    ProcessReceiveInvocation() noexcept;
    ~ProcessReceiveInvocation();
    ProcessReceiveInvocation(ProcessReceiveInvocation&&) noexcept;
    ProcessReceiveInvocation& operator=(ProcessReceiveInvocation&&) noexcept;
    ProcessReceiveInvocation(const ProcessReceiveInvocation&)=delete;
    ProcessReceiveInvocation& operator=(const ProcessReceiveInvocation&)=delete;
    InputDeliveryResult Run();
private:
    friend class detail::ProcessReceiveBridge;
    explicit ProcessReceiveInvocation(std::unique_ptr<detail::ProcessReceiveExecution>) noexcept;
    std::unique_ptr<detail::ProcessReceiveExecution> state_;
};
}
#endif
