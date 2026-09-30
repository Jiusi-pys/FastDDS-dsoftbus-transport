#include <mdds/dsoftbus/ExperimentalRuntime.hpp>
#include <mdds/dsoftbus/HostProviderLoop.hpp>
#include <mdds/dsoftbus/RuntimeProbeAccess.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace mdds::dsoftbus::phase0 {
// Existing isolated assembly hook: continue to the real Bind call. This is not
// permission, peer-ready, deadline or platform-proof evidence. Applications must
// establish deployment/authorization prerequisites before constructing DDS.
// With no armed gate the behaviour is unchanged. A callable that throws is a
// refusal, exactly like returning false.
bool RuntimeProbeBeforeConnect() noexcept {
    return InvokeArmedPreConnectGate();
}
}
namespace mdds::dsoftbus::experimental {
struct Runtime::Impl {
    explicit Impl(phase0::PublicSocketConfiguration config)
        : backend(std::make_shared<phase0::PublicSocketBackend>(std::move(config))),
          loop(std::make_shared<host_test::HostProviderLoop>(backend)) {}
    std::shared_ptr<phase0::PublicSocketBackend> backend;
    std::shared_ptr<host_test::HostProviderLoop> loop;
    std::atomic<bool> issued{false}, created{false};
};
struct Runtime::Descriptor final : eprosima::fastdds::rtps::TransportDescriptorInterface {
    Descriptor(const DSoftBusTransportDescriptor& value, std::shared_ptr<Impl> state)
        : TransportDescriptorInterface(value.maxMessageSize,1), config(value), impl(std::move(state)) {}
    eprosima::fastdds::rtps::TransportInterface* create_transport() const override {
        if(impl->created.exchange(true)) return nullptr;
        try { return HostFacadeTestAccess::Create(config,impl->loop).release(); }
        catch(...) { return nullptr; }
    }
    std::uint32_t min_send_buffer_size() const override { return config.min_send_buffer_size(); }
    DSoftBusTransportDescriptor config;
    std::shared_ptr<Impl> impl;
};
Runtime::Runtime(phase0::PublicSocketConfiguration config) : impl_(std::make_shared<Impl>(std::move(config))) {}
bool Runtime::SetPreConnectGate(std::function<bool()> gate) {
    // Refuse to change the coordination point once this runtime's descriptor, and
    // therefore its one-shot factory path, exists. The gate belongs to this
    // Runtime's own loop, so it can never affect another runtime's connect.
    if (impl_->issued.load()) return false;
    return impl_->loop->SetPreConnectGate(std::move(gate));
}
Runtime::~Runtime() = default;
std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>
Runtime::MakeDescriptor(const DSoftBusTransportDescriptor& config) {
    if(impl_->issued.exchange(true)) return {};
    return std::make_shared<Descriptor>(config,impl_);
}
NativeSendRunResult Runtime::PumpSend() noexcept { return impl_->loop->PumpSend(); }
phase0::BackendStatus Runtime::PumpReceive() noexcept { return impl_->loop->PumpReceive(); }
bool Runtime::Close() noexcept { return impl_->loop->Shutdown(); }
phase0::PublicSocketObservation Runtime::ObserveSocket() const { return impl_->backend->Observe(); }
}
