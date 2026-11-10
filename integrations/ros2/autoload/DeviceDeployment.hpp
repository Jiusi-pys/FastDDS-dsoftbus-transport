#pragma once
// Board deployment helpers shared by the device suite and the ROS 2 autoload library: native token,
// V1 peer configuration, Socket backend configuration, and one Runtime with its pump thread.
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fastdds_dsoftbus::device_deployment {
namespace ds = fastdds_dsoftbus::dsoftbus;

inline void Require(bool value, const char* what) { if (!value) throw std::runtime_error(what); }

inline void SetSelfToken(const std::string& token)
{
    if (token.empty()) return;
    void* h = dlopen("libtokensetproc_shared.z.so", RTLD_NOW);
    Require(h != nullptr, "dlopen libtokensetproc_shared.z.so");
    auto set = reinterpret_cast<int (*)(std::uint64_t)>(dlsym(h, "SetSelfTokenID"));
    auto get = reinterpret_cast<std::uint64_t (*)()>(dlsym(h, "GetSelfTokenID"));
    Require(set && get, "SetSelfTokenID/GetSelfTokenID");
    const std::uint64_t id = std::strtoull(token.c_str(), nullptr, 10);
    Require(set(id) == 0 && get() == id, "SetSelfTokenID");
}

struct Deploy {
    std::string name_space, udid, peer_udid, peer_network, prefix, initiator, peer_initiator;
    std::string package, callback_namespace;
    std::uint32_t domain = 151, slot = 0, peer_slot = 0;
};

inline Deploy MakeDeploy(const std::string& package, const std::string& callback_namespace, unsigned side,
        const std::string& run, std::uint32_t domain, const std::string& udid, const std::string& peer_udid,
        const std::string& peer_network)
{
    Deploy d; d.package = package; d.callback_namespace = callback_namespace;
    d.name_space = "md_" + run + run + run + run; d.domain = domain; d.udid = udid; d.peer_udid = peer_udid;
    d.peer_network = peer_network; d.prefix = package + ".dds." + run;
    d.slot = side ? 102 : 101; d.peer_slot = side ? 101 : 102;
    d.initiator = d.prefix + (side ? ".TB" : ".TA"); d.peer_initiator = d.prefix + (side ? ".TA" : ".TB");
    return d;
}

inline ds::DSoftBusTransportDescriptor Configuration(const Deploy& q, bool feature_limits)
{
    ds::V1Deployment d; d.identity = {q.name_space, q.domain, q.udid}; d.slot = q.slot; d.callback_namespace = q.callback_namespace;
    d.native_name_scope = {ds::v1::NativeNameContract::PackageScopedV1, q.package, q.prefix};
    ds::V1PeerDeployment p; p.identity = {q.name_space, q.domain, q.peer_udid}; p.slot = q.peer_slot; p.network_id = q.peer_network;
    p.local_initiator_name = q.initiator; p.expected_remote_initiator_names = {q.peer_initiator}; p.native_name_scope = d.native_name_scope; d.peers = {p};
    ds::DSoftBusTransportDescriptor c(65536); Require(c.ConfigureV1(d).code == ds::DirectoryError::None, "ConfigureV1");
    c.close_at_destruction = true; // peers learn of a clean exit from the Participant's dispose
    if (feature_limits) {
        c.max_participants = 4; c.receive_records = 256; c.receive_bytes = 2u << 20;
        // Secure Participants receive into buffers of at most 65535 bytes (Fast DDS uint16 cap).
        c.maxMessageSize = 65500; c.send_limits.max_record_bytes = 65500;
        c.send_limits.peer_records = 256; c.send_limits.peer_bytes = 2u << 20; c.send_limits.global_records = 256; c.send_limits.global_bytes = 2u << 20;
    }
    return c;
}

inline ds::phase0::PublicSocketConfiguration Backend(const std::string& package, const ds::DSoftBusTransportDescriptor& c)
{
    const auto& r = c.directory.routes.at(0).fields; ds::phase0::PublicSocketConfiguration b;
    b.package_name = package; b.identity = {r.local_initiator_name, {1}, {1}}; b.listener_identity = {c.directory.locals.at(0).local_listen_name, {2}, {2}};
    b.peer_name = r.remote_listen_name; b.peer_network_id = r.remote_network_id; b.expected_peer_initiator_name = r.expected_remote_initiator_names.at(0);
    b.max_message_size = 65536; b.listen_qos = {{0, 80}, {1, 2000}}; b.bind_qos = b.listen_qos; return b;
}

// One Runtime plus its pump thread.
struct PumpedRuntime {
    std::unique_ptr<ds::experimental::Runtime> rt; std::thread pump; std::atomic<bool> failed{false}, tolerate{false};
    std::atomic<std::uint32_t> send_failures{0};
    std::atomic<std::int32_t> last_send_failure{0};
    std::atomic<std::uint32_t> send_failure_events{0};
    std::atomic<std::uint64_t> sdk_sends{0}, received_records{0}; // what actually crossed the SDK
    // The first reason `failed` was set: 0 none, 1 send, 2 receive status, 3 wait result, 4 pump exception.
    std::atomic<int> failure_source{0};
    std::atomic<std::int64_t> failure_value{0};
    void latch(int source, std::int64_t value)
    {
        int none = 0;
        if (failure_source.compare_exchange_strong(none, source)) failure_value = value;
        failed = true;
    }
    std::function<void(std::int32_t)> on_first_send_failure; // set before start()
    void start()
    {
        pump = std::thread([this] { try { for (;;) {
            auto g = rt->WorkGeneration();
            for (;;) { auto r = rt->PumpSend();
                if (r.outcome == ds::NativeSendOutcome::ReturnedSdkCode && r.actual_return_code && *r.actual_return_code == 0) ++sdk_sends;
                if (r.outcome == ds::NativeSendOutcome::ReturnedInvalidCode || r.outcome == ds::NativeSendOutcome::ReturnedByException ||
                        (r.actual_return_code && *r.actual_return_code != 0)) {
                    if (++send_failure_events == 1 && on_first_send_failure) on_first_send_failure(r.actual_return_code.value_or(-1));
                    last_send_failure = r.actual_return_code.value_or(-1);
                    if (tolerate.load() && r.outcome != ds::NativeSendOutcome::ReturnedByException) ++send_failures;
                    else latch(1, r.actual_return_code.value_or(-1)); }
                if (r.status != ds::NativeStatus::None) break; }
            ds::phase0::BackendStatus rx;
            while ((rx = rt->PumpReceive()) == ds::phase0::BackendStatus::None) ++received_records;
            if (rx != ds::phase0::BackendStatus::NoWork && rx != ds::phase0::BackendStatus::InvalidState) latch(2, static_cast<int>(rx));
            auto w = rt->WaitForWork(g);
            if (w != ds::WorkWaitResult::Work && w != ds::WorkWaitResult::Deadline) {
                if (w != ds::WorkWaitResult::StopRequested && w != ds::WorkWaitResult::Closed) latch(3, static_cast<int>(w)); return; }
        } } catch (...) { latch(4, 0); } });
    }
    bool stop_and_close(std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>>& descs)
    {
        if (rt) rt->RequestPumpStop(); if (pump.joinable()) pump.join();
        descs.clear(); bool closed = false; try { closed = rt && rt->Close(); } catch (...) {}
        if (!closed) (void)rt.release(); // never destroy a Runtime that may still be in use
        return closed;
    }
};
}
