#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <atomic>
#include <cstdio>
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
#include "../probes/PublicSocketRuntimeTrace.hpp"
#endif
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus::phase0 {
// Existing isolated assembly hook: continue to the real Bind call. This is not
// permission, peer-ready, deadline or platform-proof evidence. Applications must
// establish deployment/authorization prerequisites before constructing DDS.
// With no armed gate the behaviour is unchanged. A callable that throws is a
// refusal, exactly like returning false.
bool RuntimeProbeBeforeConnect() noexcept {
    return InvokeArmedPreConnectGate();
}
}
namespace fastdds_dsoftbus::dsoftbus::experimental {
struct Runtime::Impl {
    explicit Impl(phase0::PublicSocketConfiguration config)
        : binding(config), backend(std::make_shared<phase0::PublicSocketBackend>(std::move(config))),
          loop(std::make_shared<host_test::HostProviderLoop>(backend)) {}
    const phase0::PublicSocketConfiguration binding;
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
    const auto issue = config.ValidateV1();
    if (issue.code != DirectoryError::None) {
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
        runtime_probe::TraceEvent("Descriptor_V1_DirectoryError_result",UINT64_MAX,-1,-1,static_cast<int>(issue.code),0);
#endif
        std::fprintf(stderr, "Fast DDS DSoftBus descriptor: V1 configuration error=%u\n", static_cast<unsigned>(issue.code));
        return {};
    }
    const auto& binding=impl_->binding;
    if(binding.bounded_reaccept_host&&!impl_->backend->BoundedReacceptHost())return {};
    if(config.native_name_scope.authorized_package!=binding.package_name || config.directory.locals.size()!=1 ||
       config.directory.routes.size()!=1 || config.directory.locals.front().local_listen_name!=binding.listener_identity.service_name ||
       config.directory.routes.front().fields.remote_listen_name!=binding.peer_name ||
       config.directory.routes.front().fields.remote_network_id!=binding.peer_network_id ||
       config.directory.routes.front().fields.local_initiator_name!=binding.identity.service_name ||
       config.directory.routes.front().fields.expected_remote_initiator_names!=std::vector<std::string>{binding.expected_peer_initiator_name}) {
        std::fputs("Fast DDS DSoftBus descriptor: native deployment binding mismatch\n",stderr);return {};
    }
    if(impl_->issued.exchange(true)) return {};
    return std::make_shared<Descriptor>(config,impl_);
}
NativeSendRunResult Runtime::PumpSend() noexcept { return impl_->loop->PumpSend(); }
phase0::BackendStatus Runtime::PumpReceive() noexcept { return impl_->loop->PumpReceive(); }
bool Runtime::Close() noexcept { return impl_->loop->Shutdown(); }
phase0::PublicSocketObservation Runtime::ObserveSocket() const { return impl_->backend->Observe(); }
}
namespace fastdds_dsoftbus::dsoftbus::phase0 {
bool RuntimeProbeAccess::BeforeNativeClose(experimental::Runtime& runtime,
        std::function<void(const host_test::RuntimeShutdownPoint&)> observer) {
    // Assembly and MakeDescriptor belong to the same external control thread.
    if(runtime.impl_->issued.load())return false;
    return runtime.impl_->loop->SetBeforeNativeClose(std::move(observer));
}
}
