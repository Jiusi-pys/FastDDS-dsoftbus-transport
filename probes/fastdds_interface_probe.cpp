#include "fastdds_interface_probe.hpp"

#include <algorithm>
#include <stdexcept>

namespace mdds_probe {

ProbeSenderResource::ProbeSenderResource(std::shared_ptr<ProbeSendObservation> observation, uint32_t max_bytes)
    : SenderResource(interface_probe_kind), observation_(std::move(observation)), max_bytes_(max_bytes)
{
    if (!observation_ || max_bytes_ == 0)
    {
        throw std::invalid_argument("probe sender requires observation storage and nonzero capacity");
    }
    send_lambda_ = [this](const std::vector<NetworkBuffer>& buffers, uint32_t size,
            rtps::LocatorsIterator* begin, rtps::LocatorsIterator* end,
            const Deadline& deadline, int32_t priority)
            { return record(buffers, size, begin, end, deadline, priority); };
    clean_up = [observation = observation_]() { ++observation->cleanup_count; };
}

ProbeSenderResource::~ProbeSenderResource()
{
    release();
}

void ProbeSenderResource::release()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!released_)
    {
        released_ = true;
        clean_up();
    }
}

bool ProbeSenderResource::record(const std::vector<NetworkBuffer>& buffers, uint32_t total_bytes,
        rtps::LocatorsIterator* begin, rtps::LocatorsIterator* end,
        const Deadline& deadline, int32_t priority)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (released_ || total_bytes == 0 || total_bytes > max_bytes_ || !begin || !end ||
            deadline <= std::chrono::steady_clock::now())
    {
        return false;
    }

    ProbeSendRecord record;
    record.deadline = deadline;
    record.priority = priority;
    record.bytes.reserve(total_bytes);
    uint32_t remaining = total_bytes;
    for (const auto& buffer : buffers)
    {
        if (remaining == 0) break;
        const auto count = (std::min)(buffer.size, remaining);
        if (count == 0) continue;
        if (!buffer.buffer) return false;
        const auto* data = static_cast<const rtps::octet*>(buffer.buffer);
        record.bytes.insert(record.bytes.end(), data, data + count);
        // Subtraction of a bounded prefix avoids overflow in sum(buffer sizes).
        remaining -= count;
    }
    if (remaining != 0) return false;

    for (; *begin != *end; ++(*begin))
    {
        const auto& locator = **begin;
        if (locator.kind != interface_probe_kind || locator.port != 1) return false;
        record.targets.push_back(locator);
    }
    if (record.targets.empty() || deadline <= std::chrono::steady_clock::now()) return false;

    // True means one complete, owned observation was recorded synchronously.
    // It does not mean SDK submission, peer delivery, or a production queue.
    std::lock_guard<std::mutex> observation_lock(observation_->mutex);
    observation_->records.push_back(std::move(record));
    return true;
}

InterfaceProbeDescriptor::InterfaceProbeDescriptor(uint32_t max_bytes)
    : TransportDescriptorInterface(max_bytes, 1), local(interface_probe_kind, 1),
      observation(std::make_shared<ProbeSendObservation>())
{
    local.address[15] = 1;
}

rtps::TransportInterface* InterfaceProbeDescriptor::create_transport() const
{
    return new InterfaceProbeTransport(*this);
}

uint32_t InterfaceProbeDescriptor::min_send_buffer_size() const { return maxMessageSize; }

InterfaceProbeTransport::InterfaceProbeTransport(const InterfaceProbeDescriptor& descriptor)
    : TransportInterface(interface_probe_kind), descriptor_(descriptor)
{
}

InterfaceProbeTransport::~InterfaceProbeTransport()
{
    shutdown();
}

bool InterfaceProbeTransport::init(const rtps::PropertyPolicy*, const uint32_t&)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !stopped_ && descriptor_.maxMessageSize != 0 && descriptor_.observation &&
           is_locator_allowed(descriptor_.local);
}

bool InterfaceProbeTransport::IsInputChannelOpen(const rtps::Locator& locator) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return open_ && locator == descriptor_.local;
}
bool InterfaceProbeTransport::IsLocatorSupported(const rtps::Locator& loc) const
{
    return loc.kind == interface_probe_kind;
}
bool InterfaceProbeTransport::is_locator_allowed(const rtps::Locator& loc) const
{
    return IsLocatorSupported(loc) && loc.port == 1;
}
bool InterfaceProbeTransport::is_locator_reachable(const rtps::Locator&) { return false; }
rtps::Locator InterfaceProbeTransport::RemoteToMainLocal(const rtps::Locator&) const { return descriptor_.local; }
bool InterfaceProbeTransport::OpenOutputChannel(rtps::SendResourceList& resources, const rtps::Locator& locator)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || !is_locator_allowed(locator) || !descriptor_.observation || descriptor_.maxMessageSize == 0)
    {
        return false;
    }
    resources.emplace_back(new ProbeSenderResource(descriptor_.observation, descriptor_.maxMessageSize));
    return true;
}

bool InterfaceProbeTransport::OpenInputChannel(const rtps::Locator& locator,
        rtps::TransportReceiverInterface* receiver, uint32_t capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_ || closing_ || !receiver || capacity == 0 || descriptor_.maxMessageSize == 0 ||
            locator != descriptor_.local)
    {
        return false;
    }
    const auto bounded_capacity = (std::min)(capacity, descriptor_.maxMessageSize);
    if (open_) return receiver_ == receiver && receive_capacity_ == bounded_capacity;
    receiver_ = receiver;
    receive_capacity_ = bounded_capacity;
    open_ = true;
    return true;
}

bool InterfaceProbeTransport::CloseInputChannel(const rtps::Locator& locator)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (locator != descriptor_.local) return false;
    if (closing_)
    {
        // A concurrent closer owns removal. Do not clear a channel reopened
        // after that close completes while this waiter is rescheduled.
        drained_.wait(lock, [this] { return !closing_; });
        return true;
    }
    if (!open_) return true;
    closing_ = true;
    open_ = false;
    drained_.wait(lock, [this] { return in_flight_ == 0; });
    receiver_ = nullptr;
    receive_capacity_ = 0;
    closing_ = false;
    drained_.notify_all();
    return true;
}
bool InterfaceProbeTransport::DoInputLocatorsMatch(const rtps::Locator& lhs, const rtps::Locator& rhs) const
{
    return is_locator_allowed(lhs) && lhs == rhs;
}
rtps::LocatorList InterfaceProbeTransport::NormalizeLocator(const rtps::Locator&) { return {}; }
void InterfaceProbeTransport::select_locators(rtps::LocatorSelector&) const {}
bool InterfaceProbeTransport::is_local_locator(const rtps::Locator& loc) const { return loc == descriptor_.local; }
rtps::TransportDescriptorInterface* InterfaceProbeTransport::get_configuration() { return &descriptor_; }
void InterfaceProbeTransport::AddDefaultOutputLocator(rtps::LocatorList&) {}
bool InterfaceProbeTransport::getDefaultMetatrafficMulticastLocators(rtps::LocatorList&, uint32_t) const { return false; }
bool InterfaceProbeTransport::getDefaultMetatrafficUnicastLocators(rtps::LocatorList&, uint32_t) const { return false; }
bool InterfaceProbeTransport::getDefaultUnicastLocators(rtps::LocatorList&, uint32_t) const { return false; }
bool InterfaceProbeTransport::fillMetatrafficMulticastLocator(rtps::Locator&, uint32_t) const { return false; }
bool InterfaceProbeTransport::fillMetatrafficUnicastLocator(rtps::Locator&, uint32_t) const { return false; }
bool InterfaceProbeTransport::configureInitialPeerLocator(rtps::Locator&, const rtps::PortParameters&,
        uint32_t, rtps::LocatorList&) const { return false; }
bool InterfaceProbeTransport::fillUnicastLocator(rtps::Locator&, uint32_t) const { return false; }
uint32_t InterfaceProbeTransport::max_recv_buffer_size() const { return descriptor_.maxMessageSize; }
void InterfaceProbeTransport::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    CloseInputChannel(descriptor_.local);
}

bool InterfaceProbeTransport::deliver(const rtps::octet* data, uint32_t size, const rtps::Locator& remote)
{
    rtps::TransportReceiverInterface* receiver;
    std::vector<rtps::octet> owned_data;
    rtps::Locator local_copy, remote_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_ || stopped_ || !data || size == 0 || size > receive_capacity_ || !is_locator_allowed(remote))
        {
            return false;
        }
        owned_data.assign(data, data + size);
        local_copy = descriptor_.local;
        remote_copy = remote;
        receiver = receiver_;
        ++in_flight_;
    }

    // The in-flight count pins receiver association; no transport mutex is held
    // across application code. External close waits even if a callback throws.
    try
    {
        receiver->OnDataReceived(owned_data.data(), size, local_copy, remote_copy);
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --in_flight_;
        drained_.notify_all();
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        --in_flight_;
        drained_.notify_all();
    }
    return true;
}

} // namespace mdds_probe
