#pragma once
#include <fastdds_dsoftbus/dsoftbus/NativeBackend.hpp>
#include <array>
#include <vector>

namespace fastdds_dsoftbus::dsoftbus::phase0 {
namespace detail {struct PublicSocketState;}
struct PublicSocketQos {std::int32_t type=0,value=0;};
struct PublicSocketConfiguration {
    bool route_lifecycle=false; // explicit, default-off node subscription
    BackendIdentity identity;
    BackendIdentity listener_identity; // explicit dual topology; empty keeps legacy single owner
    RecordRole role=RecordRole::Client;
    std::string package_name,peer_name,peer_network_id;
    std::vector<PublicSocketQos> listen_qos,bind_qos;
    std::size_t max_message_size=0;
    // Admitted peer mapping for factory validation; never sent as an SDK override.
    std::string expected_peer_initiator_name;
    // Explicit bounded Host model only. Real API23 builds must reject this mode.
    bool bounded_reaccept_host=false;
    // Separate real, default-off normal-close experiment. Never a Host/Fake label.
    bool bounded_native_reaccept_experiment=false;
};
enum class PublicSocketOperation {Socket,Listen,Bind,Send,Shutdown,Count};
struct PublicSocketCall {
    std::uint64_t attempts=0,calls=0,returns=0;
    BackendCallResult last;
    std::optional<BackendCallResult> last_call; // preserved across later NotCalled attempts
    std::optional<std::int32_t> handle; // last Socket call success, NOT an error code
};
struct PublicSocketError {PublicSocketOperation operation;std::int32_t code;};
struct PublicSocketObservation {
    bool fake_sdk=false,configured=false,stopped=false,closed=false;
    std::size_t entry_index=invalid_record_slot;
    std::size_t listener_entry_index=invalid_record_slot;
    bool listener_configured=false;
    std::optional<std::int32_t> outgoing_socket,listener_socket,accepted_socket;
    bool outgoing_stopped=false,listener_stopped=false,accepted_stopped=false;
    RecordRole role=RecordRole::Client;
    std::array<PublicSocketCall,static_cast<std::size_t>(PublicSocketOperation::Count)> operations{};
    std::optional<PublicSocketError> first_api_error;
    std::optional<PublicSocketOperation> first_exception;
    std::optional<std::int32_t> first_callback_error,shutdown_reason;
    std::uint64_t callback_entries=0,rejected_callbacks=0,negotiations_denied=0,unsupported_accepts=0,exceptions=0;
    std::uint64_t negotiation_callbacks=0;
    // Fixed entry/parameter storage survives close; no SDK quiescence is inferred.
};
// SDK-free port declaration. Implementation alone includes the authorized public
// headers. Real Kind is NOT admitted by the normal ProviderAdapter/facade gate.
class PublicSocketBackend final:public NativeBackend {
public:
    bool RouteLifecycleEnabled()const noexcept override;
    BackendStatus SubscribeNodes(std::function<void(bool,std::string_view)>) override;
    BackendStatus UnsubscribeNodes() override;
    v1::RouteResult ResolveNode(const v1::DeviceIdentity&,const std::string&) override;
    BackendStatus ConfigurePeerRoute(const Route&) override;
    explicit PublicSocketBackend(PublicSocketConfiguration);
    ~PublicSocketBackend() override;
    BackendKind Kind()const noexcept override;
    BackendIdentity Identity()const override;
    BackendTopology Topology()const noexcept override;
    bool BoundedReacceptHost()const noexcept override;
    bool BoundedReacceptExperiment()const noexcept override;
    BackendStatus ResetHostConnection(RecordRole,const NativeOwnerLease&) override;
    BackendStatus ResetBoundedConnection(RecordRole,const NativeOwnerLease&) override;
    std::size_t MaxMessageSize()const noexcept override;
    BackendIdentity ListenerIdentity()const override;
    BackendStatus ConfigureEntrance(const RecordEntrance&) override;
    BackendSocketResult Create(ProviderCallbacks) override;
    BackendSocketResult CreateListener(ProviderCallbacks) override;
    BackendStatus CommitAcceptedOwner(const RecordEntrance&,const NativeOwnerLease&) override;
    BackendCallResult Listen(std::int32_t) override;
    BackendCallResult Bind(std::int32_t) override;
    BackendSendResult Send(std::int32_t,const std::uint8_t*,std::size_t,std::chrono::steady_clock::time_point) override;
    BackendCallResult Shutdown(std::int32_t) override;
    PublicSocketObservation Observe()const;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK
    // Fake-build scheduling only, never an SDK callback context or proof input.
    void SetAcceptedCommitHookForTest(void(*)(void*) noexcept,void*) noexcept;
#endif
private:
    std::shared_ptr<detail::PublicSocketState> state_;
};
}
