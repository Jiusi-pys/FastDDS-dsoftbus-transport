#ifndef MDDS_APPLICATION_ATTACHMENTS_INTERNAL_HPP
#define MDDS_APPLICATION_ATTACHMENTS_INTERNAL_HPP
#include <mdds/dsoftbus/ApplicationAttachments.hpp>
namespace mdds::dsoftbus::detail {
// Single original reservation/record, never a second payload storage or queue.
// Destroy only outside native/coordinator gates; refunds and copy activity settle
// here even when allocation, native validation or application commit fails.
class AttachmentReceiveCapture final
{
public:
    AttachmentReceiveCapture() noexcept;
    ~AttachmentReceiveCapture() noexcept;
    AttachmentReceiveCapture(const AttachmentReceiveCapture&)=delete;
    AttachmentReceiveCapture& operator=(const AttachmentReceiveCapture&)=delete;
private:
    friend class ::mdds::dsoftbus::ApplicationAttachments;
    friend class ProcessReceiveBridge;
    std::shared_ptr<AttachmentApplication> app_;
    InputChannelTicket ticket_;
    NativeIdentityConstraint native_;
    ReceiveDiagnostic diagnostic_;
    std::shared_ptr<ProcessReceiveObserver> observer_;
    Locator remote_;
    ProcessClock::Clock::time_point expiry_=ProcessClock::Clock::time_point::max();
    std::size_t index_=0,global_index_=0,size_=0;
    std::uint64_t incarnation_=0;
    bool active_=false,reserved_=false;
    std::unique_ptr<AttachmentRecord> record_;
};
struct ReceiveApplicationActivity
{
    bool valid=false,frozen=false;
    std::size_t controls=0,copying=0,dispatching=0;
};
}
#endif
