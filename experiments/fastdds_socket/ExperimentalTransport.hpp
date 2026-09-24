#pragma once

// Dedicated, default-OFF engineering experiment. No production qualification.
#include <mdds/dsoftbus/V1Address.hpp>
#include <mdds/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>
#include <socket.h>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mdds::experiment {
namespace rtps = eprosima::fastdds::rtps;
using Locator = rtps::Locator_t;

// Experiment deployment mapping only: package-authorized native namespace plus
// the unchanged canonical V1 service address. No locator or wire-codec change.
// Overlength/invalid input returns false without changing output.
bool EncodeNativeServiceName(const dsoftbus::v1::ServiceAddress&,std::string& output);

struct SocketApi {
    decltype(&::Socket) create = nullptr;
    decltype(&::Listen) listen = nullptr;
    decltype(&::Bind) bind = nullptr;
    decltype(&::SendBytes) send_bytes = nullptr;
    decltype(&::Shutdown) shutdown = nullptr;
};
struct PeerConfig {
    Locator locator;
    std::string service;
    std::string network_id;
};
struct Config {
    std::string run_id;
    std::string instance_name; // Evidence label only; never a native event identity.
    Locator local_locator;
    Locator peer_locator;
    std::string local_service;
    std::string peer_service;
    std::string peer_network_id;
    std::string package_name = "com.kaihong.mdds";
    std::uint32_t max_message_size = 60000;
    bool enable_rx_drop = false; // Explicit capability in a separately opted-in test build only.
    // Dedicated bounded matrix: primary above plus at most one explicit peer.
    std::vector<PeerConfig> additional_peers;
};
struct FragmentObservation {
    std::array<std::uint8_t,16> writer_guid{};
    std::int32_t sequence_hi = 0;
    std::uint32_t sequence_lo = 0, fragment_start = 0, sample_size = 0;
    std::uint16_t fragment_count = 0, fragment_size = 0;
    std::uint64_t fragment_end = 0;
    std::size_t submessage_offset = 0;
    std::size_t submessage_body_bytes = 0;
    bool inline_qos = false, key_flag = false, payload_bounds_known = false;
    std::uint16_t octets_to_inline_qos = 0;
    // Native message offset and measured remainder after fixed fields/inline
    // QoS; payload_bytes includes up to three RTPS alignment padding bytes.
    std::size_t payload_offset = 0, payload_bytes = 0;
};
struct DataObservation {
    std::array<std::uint8_t,16> writer_guid{};
    std::int32_t sequence_hi=0;
    std::uint32_t sequence_lo=0;
    std::size_t submessage_offset=0,submessage_body_bytes=0;
};
struct ReliabilityControlObservation {
    // Original submessage id: ACKNACK=0x06, HEARTBEAT=0x07,
    // NACK_FRAG=0x12, HEARTBEAT_FRAG=0x13. No inferred peer identity.
    std::uint8_t kind = 0;
    std::array<std::uint8_t,12> source_prefix{}, destination_prefix{};
    bool destination_prefix_known = false;
    std::array<std::uint8_t,4> reader_id{}, writer_id{};
    std::array<std::uint8_t,16> reader_guid{}, writer_guid{};
    bool reader_prefix_known = false, writer_prefix_known = false;
    // ACKNACK bitmap base / HEARTBEAT first / *_FRAG writerSN.
    std::int32_t sequence_hi = 0, last_sequence_hi = 0;
    std::uint32_t sequence_lo = 0, last_sequence_lo = 0;
    std::uint32_t fragment_base = 0, last_fragment = 0, num_bits = 0;
    // Raw RTPS bitmap words; bit i is 1u << (31-(i%32)) in word i/32.
    std::array<std::uint32_t,8> bitmap{};
    std::uint32_t count = 0;
    bool final_flag = false, liveliness_flag = false;
    std::size_t submessage_offset = 0;
};
struct RtpsObservation {
    bool valid = false;
    std::uint64_t spdp = 0, sedp = 0, sedp_publications = 0, sedp_subscriptions = 0, user = 0;
    std::array<std::uint8_t,16> first_writer_guid{};
    // One entry per DATA_FRAG, not one per native message or DDS sample.
    std::vector<FragmentObservation> fragments;
    // Raw identity of every DATA submessage, independent of first_writer_guid.
    // Diagnostic only; never modifies or filters receiver input.
    std::vector<DataObservation> data;
    std::vector<ReliabilityControlObservation> controls;
    std::uint64_t data_submessages = 0; // DATA only, including builtin writers.
    std::uint64_t statistics_submessages = 0;
    std::size_t statistics_body_bytes = 0;
    // Structural observation only. The ordinary observer never filters data.
    bool drop_shape_candidate = false;
};
// Read-only observer of original RTPS bytes; never adds framing or changes data.
RtpsObservation ObserveRtps(const void*, std::size_t) noexcept;

struct Stats {
    std::uint64_t runtime_id=0,application_incarnation=0,receiver_incarnation=0,logical_token=0;
    bool application_detached=false,app_clean_observed=false;
    std::uint64_t instance_id = 0, receiver_lease_id = 0;
    int server_callback_slot = -1, client_callback_slot = -1;
    std::uint64_t tx_accepted = 0, tx_rejected = 0, tx_send_returns = 0, tx_send_failures = 0;
    std::uint64_t tx_diagnostic_errors = 0;
    std::uint64_t rx_owned = 0, receiver_calls = 0, receiver_returns = 0;
    std::uint64_t tx_spdp = 0, tx_sedp = 0, tx_user = 0;
    std::uint64_t rx_spdp = 0, rx_sedp = 0, rx_user = 0;
    // Actual observed submessages: SDK-successful TX / receiver-return RX.
    // These are neither unique samples nor acknowledgments.
    std::uint64_t tx_data_frag = 0, rx_data_frag = 0;
    std::uint64_t tx_sedp_publications = 0, tx_sedp_subscriptions = 0;
    std::uint64_t rx_sedp_publications = 0, rx_sedp_subscriptions = 0;
    std::uint64_t peer_shutdown_events = 0;
    std::uint64_t no_app_drops = 0, rx_drops = 0, unexpected_callbacks = 0, peer_errors = 0;
    std::uint64_t listener_creations = 0, native_owners = 0, shutdown_returns = 0;
    std::uint64_t sdk_calls_active = 0, callbacks_active = 0, receiver_leases = 0;
    std::uint64_t tx_records = 0, tx_bytes = 0, rx_records = 0, rx_bytes = 0;
    // Task budget above differs from actual adapter-owned payload lifetime.
    // These counts do not describe allocator capacity, pooling, or SDK memory.
    std::uint64_t live_tx_blocks = 0, live_tx_payload_bytes = 0;
    std::uint64_t live_rx_blocks = 0, live_rx_payload_bytes = 0;
    bool tx_worker_joined = false, rx_worker_joined = false;
    bool closed = false, clean_observed = false, fatal = false;
};
// Limits are reservations for concurrently active experimental instances, not
// a cumulative Participant/connection quota. Exposed callback entries have a
// separate process-lifetime, non-reusable incarnation capacity.
struct ProcessStats {
    static constexpr std::size_t state_limit=2, worker_limit=4, native_owner_limit=6;
    static constexpr std::size_t role_entry_limit=8, record_limit=128, byte_limit=2*1024*1024;
    std::uint64_t active_states=0, reserved_workers=0, native_owners=0;
    std::uint64_t tx_records=0, tx_bytes=0, rx_records=0, rx_bytes=0;
    std::uint64_t live_tx_blocks=0, live_tx_payload_bytes=0, live_rx_blocks=0, live_rx_payload_bytes=0;
    std::uint64_t exposed_entries=0, tombstone_entries=0, free_entries=0;
};
// Aggregate observation: per-instance snapshots are taken after releasing the
// process registry lock. This is not an atomic instant across running workers.
ProcessStats GetProcessStats();
void PrintProcessSummary(const char* stage="snapshot");
struct State;
struct Application;
struct RuntimeCore;
struct RuntimeObserver;
struct RuntimeStats {
    std::uint64_t runtime_id=0,active_applications=0,active_receivers=0,active_native_endpoints=0,native_owners=0;
    std::uint64_t queued_tx=0,queued_rx=0,queued_bytes=0,copy_active=0,delivery_active=0,retained_applications=0;
    std::uint64_t exposed_entries=0,tombstone_entries=0;
    bool stopped=false;
};
struct RxAdmissionRecord {
    dsoftbus::ReceiveAdmissionDiagnostic decision;
    std::uint64_t invocation=0,runtime_id=0,ingress_receiver_marker=0;
    int socket=-1,callback_slot=-1;
    bool entry_state_known=false,adapter_state_known=false;
    bool endpoint_limit_known=false;
    std::size_t endpoint_limit=0;
    // owner-present=1, role-match=2, close-claimed=4, ready=8,
    // binding=16, closing=32, retired=64, terminal=128.
    std::uint32_t entry_state=0,adapter_state=0;
};
struct RxAdmissionSummary {
    static constexpr std::size_t capacity=1024;
    std::uint64_t total=0,stored=0,overflow=0;
    std::array<std::uint64_t,static_cast<std::size_t>(dsoftbus::RxAdmissionReason::Count)> reasons{};
};
class ProcessRuntime final {
public:
    static std::shared_ptr<ProcessRuntime> Acquire(const std::vector<Config>&,SocketApi);
    ~ProcessRuntime();
    bool Finalize();
    RuntimeStats Snapshot() const;
    // Read-only metadata diagnostics; no payload/native/application ownership.
    RxAdmissionSummary ReceiveAdmissionSummary(std::size_t endpoint=0) const;
    RxAdmissionRecord ReceiveAdmissionRecord(std::size_t endpoint,std::size_t index) const;
private:
    friend class ExperimentalTransport;
    friend class RuntimeTestAccess;
    // Host-only scheduling seam: set before concurrency, clear after join.
    // No native event identity/context is supplied or inferred.
    static void SetAcquireRetainedHook(std::function<void(std::uint64_t)>);
    struct Impl;
    explicit ProcessRuntime(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
struct RxDropRule {
    std::array<std::uint8_t,16> writer_guid{};
    std::int32_t sequence_hi = 0;
    std::uint32_t sequence_lo = 1, fragment_start = 3, sample_size = 262156;
    std::uint16_t fragment_count = 1;
};
struct RxDropSnapshot {
    bool armed = false;
    RxDropRule rule;
    std::uint64_t dropped_messages = 0, dropped_invocation = 0, dropped_hash = 0;
    std::size_t dropped_len = 0;
    std::uint64_t recovered_range_deliveries = 0;
};
class Control final {
public:
    Stats Snapshot() const;
    void PrintSummary() const;
    bool ArmRxDrop(const RxDropRule&) noexcept;
    RxDropSnapshot DropSnapshot() const;
private:
    friend struct State;
    friend struct Application;
    friend struct RuntimeCore;
    friend struct RuntimeObserver;
    friend class ContractTestAccess;
    friend class RuntimeTestAccess;
    friend class MultiPeerTestAccess;
    mutable std::mutex mutex_;
    std::condition_variable payloads_changed_;
    Stats stats_;
    std::shared_ptr<Control> native_control_;
    std::string instance_name_;
    bool rx_drop_enabled_ = false, rx_drop_closed_ = false;
    RxDropSnapshot rx_drop_;
    // Installed before Stop only by the host contract fixture. No SDK context
    // or event identity is supplied, and the native program never sets it.
    std::function<void()> before_final_retire_;
    std::function<void()> before_send_, before_tx_join_, after_tx_settle_, after_rx_settle_;
    std::function<void(std::weak_ptr<const void>)> after_tx_admit_;
    std::function<void()> before_rx_copy_,after_rx_admit_,after_rx_queue_,detach_requested_;
    std::function<void()> before_attach_publish_;
    std::function<void()> before_app_allocate_;
    // Host-only deterministic allocation-fault seam, set before submissions.
    // No callback is invoked while the queue-publication lock is held.
    std::size_t tx_publish_fault_after_=static_cast<std::size_t>(-1);
};
struct ExperimentalDescriptor final : rtps::TransportDescriptorInterface {
    ExperimentalDescriptor(Config, SocketApi, std::shared_ptr<Control>,std::shared_ptr<ProcessRuntime> = {});
    rtps::TransportInterface* create_transport() const override;
    std::uint32_t min_send_buffer_size() const override { return maxMessageSize; }
    const Config config;
    const SocketApi api;
    const std::shared_ptr<Control> control;
    const std::shared_ptr<ProcessRuntime> runtime;
};
class ExperimentalTransport final : public rtps::TransportInterface {
public:
    explicit ExperimentalTransport(const ExperimentalDescriptor&);
    ~ExperimentalTransport() override;
    bool init(const rtps::PropertyPolicy* = nullptr, const std::uint32_t& = 0) override;
    bool IsInputChannelOpen(const Locator&) const override;
    bool IsLocatorSupported(const Locator&) const override;
    bool is_locator_allowed(const Locator&) const override;
    bool is_locator_reachable(const Locator&) override;
    Locator RemoteToMainLocal(const Locator&) const override;
    using TransportInterface::transform_remote_locator;
    bool transform_remote_locator(const Locator&, Locator&) const override;
    bool OpenOutputChannel(rtps::SendResourceList&, const Locator&) override;
    bool OpenOutputChannels(rtps::SendResourceList&, const rtps::LocatorSelectorEntry&) override;
    void CloseOutputChannels(rtps::SendResourceList&, const rtps::LocatorSelectorEntry&) override;
    bool OpenInputChannel(const Locator&, rtps::TransportReceiverInterface*, std::uint32_t) override;
    // Control-thread only. Calling Close/shutdown on a receiver stack is unsupported.
    bool CloseInputChannel(const Locator&) override;
    bool DoInputLocatorsMatch(const Locator&, const Locator&) const override;
    rtps::LocatorList NormalizeLocator(const Locator&) override;
    void select_locators(rtps::LocatorSelector&) const override;
    bool is_local_locator(const Locator&) const override;
    rtps::TransportDescriptorInterface* get_configuration() override;
    void AddDefaultOutputLocator(rtps::LocatorList&) override;
    bool getDefaultMetatrafficMulticastLocators(rtps::LocatorList&, std::uint32_t) const override;
    bool getDefaultMetatrafficUnicastLocators(rtps::LocatorList&, std::uint32_t) const override;
    bool getDefaultUnicastLocators(rtps::LocatorList&, std::uint32_t) const override;
    bool fillMetatrafficMulticastLocator(Locator&, std::uint32_t) const override;
    bool fillMetatrafficUnicastLocator(Locator&, std::uint32_t) const override;
    bool configureInitialPeerLocator(Locator&, const rtps::PortParameters&, std::uint32_t, rtps::LocatorList&) const override;
    bool fillUnicastLocator(Locator&, std::uint32_t) const override;
    std::uint32_t max_recv_buffer_size() const override;
    bool is_localhost_allowed() const override { return false; }
    void shutdown() override;
private:
    ExperimentalDescriptor descriptor_;
    std::shared_ptr<State> state_;
    std::shared_ptr<ProcessRuntime> runtime_;
    std::shared_ptr<Application> application_;
    bool implicit_runtime_=false;
};
} // namespace mdds::experiment
