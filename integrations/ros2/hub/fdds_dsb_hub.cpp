// Per-board hub for ROS 2 over DSoftBus. It owns the board's one DSoftBus link, through the same Transport and
// Runtime as the direct path, and routes frames between local ROS processes and the peer board's hub.
//
//   fdds_dsb_hub --supervise [--recover-bus-after <seconds>] <config> <socket path>    restarts the worker when it exits
//   fdds_dsb_hub [--recover-bus-after <seconds>] <config> <socket path>                the worker
//
// The worker exits when the link cannot be used, and a fresh process opens a new one: a Runtime cannot reopen
// its native sockets. Attached processes reconnect by themselves, so RTPS sees an outage, not a failure.
//
// An outage of about 45 s takes the peer off the DSoftBus network, and it does not return when the route does, though
// restarting softbus_server brings it back. --recover-bus-after does that once the peer has been missing that long. It
// is off by default because it interrupts every DSoftBus user on the board, and it needs permission to kill the service.
#include "../autoload/AutoloadConfig.hpp"
#include "../autoload/DeviceDeployment.hpp"
#include "HubRouter.hpp"
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/common/LocatorsIterator.hpp>
#include <fastdds/rtps/common/PortParameters.h>
#include <fastdds/rtps/transport/SenderResource.h>
#include <fastdds/rtps/transport/TransportReceiverInterface.h>
#include <dirent.h>
#include <fcntl.h>
#include <softbus_bus_center.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace ds = fastdds_dsoftbus::dsoftbus;
namespace dd = fastdds_dsoftbus::device_deployment;
namespace ra = fastdds_dsoftbus::ros2_autoload;
namespace hub = fastdds_dsoftbus::ros2_hub;
namespace rt = eprosima::fastdds::rtps;
using Locator = eprosima::fastrtps::rtps::Locator_t;
using Clock = std::chrono::steady_clock;
using Frame = std::vector<std::uint8_t>;

constexpr char kPackage[] = "com.kaihong.mdds";
constexpr int kRefusedExit = 71;
constexpr int kLinkLostExit = 75;
constexpr int kBusRestartedExit = 76;
constexpr std::size_t kClientQueueBytes = 32u << 20;
constexpr std::size_t kLinkQueueBytes = 32u << 20;
constexpr int kSocketBufferBytes = 4 << 20;
// Both hubs must be listening when either connects. A worker that fails to connect is restarted sooner than this
// wait, so the two listening windows always overlap.
constexpr auto kPeerListenWait = std::chrono::seconds(4);
constexpr auto kRestartPause = std::chrono::seconds(2);
constexpr auto kLinkSendLimit = std::chrono::seconds(2);
constexpr auto kPeerPollPeriod = std::chrono::seconds(2);
constexpr int kPeerMissingPolls = 5; // about 10 s; shorter stays a route outage, which the open link survives
// Restarting the bus interrupts every user on the board, so a peer that stays away gets ever fewer attempts.
constexpr int kMaxBusRestartPauseSeconds = 300;
constexpr auto kHealthyRun = std::chrono::seconds(120);

std::atomic<int> g_stop_fd{-1}, g_stats_fd{-1};
void Signal(const std::atomic<int>& target)
{
    const std::uint64_t one = 1;
    const int fd = target.load();
    if (fd >= 0) (void)!::write(fd, &one, sizeof(one));
}
void OnStopSignal(int) { Signal(g_stop_fd); }
void OnStatsSignal(int) { Signal(g_stats_fd); }

// The network id changes when a board rejoins the bus, so the peer is found by its UDID each time.
// A query that fails says nothing about the peer, so only an answer without it counts as absent.
struct PeerQuery {
    bool answered = false;
    std::optional<std::string> network;
};
PeerQuery FindPeer(const std::string& udid)
{
    NodeBasicInfo* nodes = nullptr;
    std::int32_t count = 0;
    if (::GetAllNodeDeviceInfo(kPackage, &nodes, &count) != 0) return {};
    PeerQuery query;
    query.answered = true;
    for (std::int32_t i = 0; i < count && !query.network; ++i) {
        char value[UDID_BUF_LEN] = {};
        if (::GetNodeKeyInfo(kPackage, nodes[i].networkId, NODE_KEY_UDID, reinterpret_cast<std::uint8_t*>(value), sizeof(value)) == 0 && udid == value)
            query.network = nodes[i].networkId;
    }
    if (nodes != nullptr) ::FreeNodeInfo(nodes);
    return query;
}

// Found through /proc because the board's tools differ between images.
bool RestartBusService()
{
    DIR* processes = ::opendir("/proc");
    if (processes == nullptr) return false;
    bool killed = false;
    while (const dirent* entry = ::readdir(processes)) {
        const int pid = std::atoi(entry->d_name);
        if (pid <= 0) continue;
        std::ifstream name(std::string("/proc/") + entry->d_name + "/comm");
        std::string text;
        if (std::getline(name, text) && text == "softbus_server" && ::kill(pid, SIGKILL) == 0) killed = true;
    }
    ::closedir(processes);
    return killed;
}

// One locator for the send call.
class OneLocator final : public rt::LocatorsIterator {
public:
    explicit OneLocator(const Locator* value) : value_(value) {}
    LocatorsIterator& operator++() override { value_ = nullptr; return *this; }
    bool operator==(const LocatorsIterator& other) const override { return value_ == static_cast<const OneLocator&>(other).value_; }
    bool operator!=(const LocatorsIterator& other) const override { return !(*this == other); }
    const Locator& operator*() const override { return *value_; }
private:
    const Locator* value_;
};

class Hub final : public rt::TransportReceiverInterface {
public:
    Hub(ra::Config config, std::string path, int recover_after) : config_(std::move(config)), path_(std::move(path)), recover_after_(recover_after) {}

    int Run()
    {
        wake_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        stop_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        stats_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        epoll_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (wake_ < 0 || stop_ < 0 || stats_ < 0 || epoll_ < 0 || !OpenListener()) return Fail("local socket");
        Watch(wake_, EPOLLIN); Watch(stop_, EPOLLIN); Watch(stats_, EPOLLIN); Watch(listener_, EPOLLIN);
        g_stop_fd = stop_;
        g_stats_fd = stats_;
        std::signal(SIGUSR1, OnStatsSignal);
        std::signal(SIGPIPE, SIG_IGN);
        OpenLink();
        const int code = Loop();
        return CloseLink(code);
    }

    // Pump thread: a record from the peer hub.
    void OnDataReceived(const eprosima::fastrtps::rtps::octet* data, std::uint32_t size, const Locator&, const Locator&) override
    {
        ++link_received_;
        {
            std::lock_guard<std::mutex> lock(inbound_gate_);
            if (inbound_bytes_ + size > kLinkQueueBytes) { ++inbound_dropped_; return; }
            inbound_bytes_ += size;
            inbound_.emplace_back(data, data + size);
        }
        const std::uint64_t one = 1;
        (void)!::write(wake_, &one, sizeof(one));
    }

private:
    struct Client {
        int fd = -1;
        hub::BoundedQueue queue{kClientQueueBytes};
    };

    int Fail(const std::string& what)
    {
        std::fprintf(stderr, "FDDS_HUB_REFUSED reason=%s\n", what.c_str());
        std::fflush(stderr);
        return kRefusedExit;
    }

    // Two hubs on one path would split the board's processes and claim the same native names. The kernel releases
    // the lock with the process, however it ends.
    bool LockSocketPath()
    {
        const auto lock = path_ + ".lock";
        const int fd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0660);
        return fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
    }

    bool OpenListener()
    {
        sockaddr_un where {};
        if (path_.size() >= sizeof(where.sun_path)) return false;
        where.sun_family = AF_UNIX;
        std::memcpy(where.sun_path, path_.c_str(), path_.size() + 1);
        listener_ = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (listener_ < 0) return false;
        if (!LockSocketPath()) return false;
        (void)::unlink(path_.c_str()); // left by a worker that was killed; the lock proves none is alive
        if (::bind(listener_, reinterpret_cast<const sockaddr*>(&where), sizeof(where)) != 0) return false;
        (void)::chmod(path_.c_str(), 0660);
        return ::listen(listener_, 64) == 0;
    }

    void Watch(int fd, std::uint32_t events)
    {
        epoll_event item {};
        item.events = events; item.data.fd = fd;
        (void)::epoll_ctl(epoll_, EPOLL_CTL_ADD, fd, &item);
    }
    void Rearm(int fd, std::uint32_t events)
    {
        epoll_event item {};
        item.events = events; item.data.fd = fd;
        (void)::epoll_ctl(epoll_, EPOLL_CTL_MOD, fd, &item);
    }

    // The link opens on its own thread, because the native connect may block, and local traffic must not wait.
    void OpenLink()
    {
        link_thread_ = std::thread([this] {
            try {
                dd::SetSelfToken(config_.token);
                if (!WaitForPeer()) return;
                const auto deployment = dd::MakeDeploy(kPackage, "dsb.ros2hub", config_.side, config_.run, config_.domain,
                    config_.local_udid, config_.peer_udid, config_.peer_network);
                auto configuration = dd::Configuration(deployment, true);
                pumped_.rt = std::make_unique<ds::experimental::Runtime>(dd::Backend(kPackage, configuration),
                    ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
                const auto go = config_.go_file;
                if (!pumped_.rt->SetPreConnectGate([go] {
                        std::fprintf(stdout, "FDDS_HUB_LISTENING\n"); std::fflush(stdout);
                        // /dev/null always exists: a deployment needs no go file. The test runner uses one to start both boards together.
                        while (::access(go.c_str(), F_OK) != 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        std::this_thread::sleep_for(kPeerListenWait);
                        return true; }))
                    throw std::runtime_error("pre-connect gate");
                auto descriptor = pumped_.rt->MakeStockDescriptor(configuration);
                transport_.reset(descriptor->create_transport());
                descriptor_ = descriptor;
                if (!transport_ || !transport_->init(nullptr, static_cast<std::uint32_t>(hub::kMaxRecordBytes))) throw std::runtime_error("transport init");
                local_ = configuration.default_user_unicast.front();
                peer_ = configuration.directory.routes.at(0).key.remote_locator;
                pumped_.tolerate = true;
                pumped_.on_first_send_failure = [](std::int32_t code) {
                    std::fprintf(stderr, "FDDS_HUB_SEND_FAILURE code=%d\n", code); std::fflush(stderr); };
                pumped_.start();
                // The order a Participant uses: input, then the domain through the initial peer, then output.
                if (!transport_->OpenInputChannel(local_, this, static_cast<std::uint32_t>(hub::kMaxRecordBytes))) throw std::runtime_error("input channel");
                eprosima::fastrtps::rtps::LocatorList_t peers;
                Locator remote = peer_;
                eprosima::fastrtps::rtps::PortParameters ports;
                if (!transport_->configureInitialPeerLocator(remote, ports, config_.domain, peers)) throw std::runtime_error("domain");
                if (!transport_->OpenOutputChannel(senders_, peer_)) throw std::runtime_error("output channel");
                // libffrt installs its own handlers when the SDK loads, and they fault off an FFRT thread.
                std::signal(SIGINT, OnStopSignal);
                std::signal(SIGTERM, OnStopSignal);
                link_ready_ = true;
                std::fprintf(stdout, "FDDS_HUB_LINK_READY side=%u\n", config_.side); std::fflush(stdout);
                peer_thread_ = std::thread([this] { WatchPeer(); });
                WriteLink();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FDDS_HUB_LINK_FAILED reason=%s\n", error.what()); std::fflush(stderr);
                link_failed_ = true;
                const std::uint64_t one = 1;
                (void)!::write(wake_, &one, sizeof(one));
            }
        });
    }

    // False when the bus was restarted, so a fresh worker starts on the new bus connection.
    bool WaitForPeer()
    {
        const auto start = Clock::now();
        bool reported = false;
        for (;;) {
            if (const auto query = FindPeer(config_.peer_udid); query.network) {
                config_.peer_network = *query.network;
                return true;
            }
            if (!reported) { std::fprintf(stdout, "FDDS_HUB_WAITING_FOR_PEER\n"); std::fflush(stdout); reported = true; }
            if (recover_after_ > 0 && Clock::now() - start >= std::chrono::seconds(recover_after_)) {
                const bool restarted = RestartBusService();
                std::fprintf(stderr, "FDDS_HUB_BUS_RESTART done=%d\n", restarted); std::fflush(stderr);
                bus_restarted_ = true;
                const std::uint64_t one = 1;
                (void)!::write(wake_, &one, sizeof(one));
                return false;
            }
            std::this_thread::sleep_for(kPeerPollPeriod);
        }
    }

    // On its own thread: the query is an IPC to softbus_server, and local traffic must not wait for it.
    void WatchPeer()
    {
        for (;;) {
            std::this_thread::sleep_for(kPeerPollPeriod);
            const auto query = FindPeer(config_.peer_udid);
            if (!query.answered || query.network) peer_missing_ = 0;
            else ++peer_missing_;
        }
    }

    void WriteLink()
    {
        for (;;) {
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(outbound_gate_);
                outbound_ready_.wait(lock, [&] { return !outbound_.empty(); });
                frame = std::move(outbound_.front());
                outbound_.pop_front();
                outbound_bytes_ -= frame.size();
            }
            OneLocator begin(&peer_), end(nullptr);
            const auto deadline = Clock::now() + kLinkSendLimit;
            if (senders_.front()->send(frame.data(), static_cast<std::uint32_t>(frame.size()), &begin, &end, deadline)) ++link_sent_;
            else ++link_send_refused_;
        }
    }

    void ToLink(const std::uint8_t* data, std::size_t size)
    {
        {
            std::lock_guard<std::mutex> lock(outbound_gate_);
            if (!link_ready_.load() || outbound_bytes_ + size > kLinkQueueBytes) { ++outbound_dropped_; return; }
            outbound_bytes_ += size;
            outbound_.emplace_back(data, data + size);
        }
        outbound_ready_.notify_one();
    }

    void ToClient(int id, const std::uint8_t* data, std::size_t size)
    {
        auto& client = *clients_.at(id);
        if (client.queue.Empty()) {
            const auto sent = ::send(client.fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent >= 0) return;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return; // its close arrives through epoll
        }
        if (client.queue.Push(Frame(data, data + size))) Rearm(client.fd, EPOLLIN | EPOLLOUT);
    }

    void Deliver(int origin, const hub::FrameHeader& header, const std::uint8_t* data, std::size_t size)
    {
        ++frames_routed_;
        for (const auto target : router_.Route(origin, header)) {
            if (target == hub::Router::kLink) ToLink(data, size);
            else ToClient(target, data, size);
        }
    }

    void Accept()
    {
        for (;;) {
            const int fd = ::accept4(listener_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0) return;
            (void)::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
            (void)::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
            auto client = std::make_unique<Client>();
            client->fd = fd;
            clients_[fd] = std::move(client);
            Watch(fd, EPOLLIN);
        }
    }

    void Drop(int fd)
    {
        router_.Detach(fd);
        client_drops_ += clients_.at(fd)->queue.Dropped();
        (void)::epoll_ctl(epoll_, EPOLL_CTL_DEL, fd, nullptr);
        ::close(fd);
        clients_.erase(fd);
    }

    void ReadClient(int fd)
    {
        for (int budget = 0; budget < 64; ++budget) { // one busy client cannot starve the rest
            const auto got = ::recv(fd, buffer_.data(), buffer_.size(), MSG_DONTWAIT);
            if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { Drop(fd); return; }
            if (got < 0) return;
            hub::FrameHeader header;
            if (!hub::ParseHeader(buffer_.data(), static_cast<std::size_t>(got), header)) { ++malformed_; continue; }
            switch (header.magic) {
            case hub::kHelloMagic: router_.Attach(fd, header.src); break;
            case hub::kJoinMagic: router_.Join(fd, header.port); break;
            case hub::kLeaveMagic: router_.Leave(fd, header.port); break;
            default:
                if (router_.Attached(fd)) Deliver(fd, header, buffer_.data(), static_cast<std::size_t>(got));
                else ++malformed_;
            }
            // Attach can replace another connection for the same address, which closes nothing here: that
            // connection's own close still arrives through epoll.
        }
    }

    void WriteClient(int fd)
    {
        auto& client = *clients_.at(fd);
        while (!client.queue.Empty()) {
            const auto& frame = client.queue.Front();
            const auto sent = ::send(fd, frame.data(), frame.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return;
                Drop(fd);
                return;
            }
            client.queue.Pop();
        }
        Rearm(fd, EPOLLIN);
    }

    void DrainInbound()
    {
        std::uint64_t ignored = 0;
        (void)!::read(wake_, &ignored, sizeof(ignored));
        std::deque<Frame> frames;
        {
            std::lock_guard<std::mutex> lock(inbound_gate_);
            frames.swap(inbound_);
            inbound_bytes_ = 0;
        }
        for (const auto& frame : frames) {
            hub::FrameHeader header;
            if (hub::ParseHeader(frame.data(), frame.size(), header) && header.magic == hub::kFrameMagic)
                Deliver(hub::Router::kLink, header, frame.data(), frame.size());
            else ++malformed_;
        }
    }

    bool LinkLost() const
    {
        if (link_failed_.load()) return true;
        if (pumped_.failed.load()) {
            std::fprintf(stderr, "FDDS_HUB_LINK_STATE pump_failed=1 source=%d value=%lld\n", pumped_.failure_source.load(),
                static_cast<long long>(pumped_.failure_value.load()));
            return true;
        }
        if (!link_ready_.load()) return false;
        if (peer_missing_.load() >= kPeerMissingPolls) {
            std::fprintf(stderr, "FDDS_HUB_LINK_STATE peer_offline=1\n");
            return true;
        }
        const auto seen = pumped_.rt->ObserveSocket();
        // outgoing_stopped and accepted_stopped describe the base sockets, which a domain circuit does not use.
        const bool lost = seen.closed || seen.stopped || seen.shutdown_reason.has_value() || seen.first_callback_error.has_value();
        if (lost) std::fprintf(stderr, "FDDS_HUB_LINK_STATE closed=%d stopped=%d reason=%d error=%d\n", seen.closed, seen.stopped,
            seen.shutdown_reason.value_or(-1), seen.first_callback_error.value_or(0));
        return lost;
    }

    int Loop()
    {
        std::array<epoll_event, 32> events{};
        auto last_check = Clock::now();
        for (;;) {
            const int ready = ::epoll_wait(epoll_, events.data(), static_cast<int>(events.size()), 500);
            if (ready < 0 && errno != EINTR) return Fail("epoll");
            for (int i = 0; i < ready; ++i) {
                const int fd = events[static_cast<std::size_t>(i)].data.fd;
                const auto flags = events[static_cast<std::size_t>(i)].events;
                if (fd == stop_) return 0;
                if (fd == stats_) {
                    std::uint64_t ignored = 0;
                    (void)!::read(stats_, &ignored, sizeof(ignored));
                    PrintStats(-1);
                    continue;
                }
                if (fd == wake_) { DrainInbound(); continue; }
                if (fd == listener_) { Accept(); continue; }
                if (clients_.count(fd) == 0) continue;
                if ((flags & EPOLLOUT) != 0) WriteClient(fd);
                if (clients_.count(fd) != 0 && (flags & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0) ReadClient(fd);
            }
            if (Clock::now() - last_check >= std::chrono::seconds(1)) {
                last_check = Clock::now();
                if (bus_restarted_.load()) return kBusRestartedExit;
                if (LinkLost()) {
                    std::fprintf(stderr, "FDDS_HUB_LINK_LOST\n"); std::fflush(stderr);
                    return kLinkLostExit;
                }
            }
        }
    }

    void PrintStats(int code)
    {
        std::uint64_t dropped_clients = client_drops_, dropped_out = 0, dropped_in = 0;
        for (const auto& item : clients_) dropped_clients += item.second->queue.Dropped();
        { std::lock_guard<std::mutex> lock(outbound_gate_); dropped_out = outbound_dropped_; }
        { std::lock_guard<std::mutex> lock(inbound_gate_); dropped_in = inbound_dropped_; }
        std::printf("FDDS_HUB_STATS exit=%d clients=%zu routed=%llu link_sent=%llu link_received=%llu sdk_sends=%llu sdk_records=%llu "
            "send_failures=%u refused=%llu dropped_out=%llu dropped_in=%llu dropped_client=%llu malformed=%llu\n", code, clients_.size(),
            static_cast<unsigned long long>(frames_routed_), static_cast<unsigned long long>(link_sent_.load()),
            static_cast<unsigned long long>(link_received_.load()), static_cast<unsigned long long>(pumped_.sdk_sends.load()),
            static_cast<unsigned long long>(pumped_.received_records.load()), pumped_.send_failure_events.load(),
            static_cast<unsigned long long>(link_send_refused_.load()), static_cast<unsigned long long>(dropped_out),
            static_cast<unsigned long long>(dropped_in), static_cast<unsigned long long>(dropped_clients),
            static_cast<unsigned long long>(malformed_));
        std::fflush(stdout);
    }

    int CloseLink(int code)
    {
        PrintStats(code);
        (void)::unlink(path_.c_str());
        // The native connect may still be blocking and SDK close order is not proved during a failure, so the
        // process exits here and the kernel releases everything; the next worker starts clean.
        _exit(code);
    }

    ra::Config config_;
    std::string path_;
    int recover_after_;
    int wake_ = -1, stop_ = -1, stats_ = -1, epoll_ = -1, listener_ = -1;
    hub::Router router_;
    std::map<int, std::unique_ptr<Client>> clients_;
    Frame buffer_ = Frame(hub::kMaxRecordBytes);

    dd::PumpedRuntime pumped_;
    std::shared_ptr<rt::TransportDescriptorInterface> descriptor_;
    std::unique_ptr<rt::TransportInterface> transport_;
    rt::SendResourceList senders_;
    Locator local_, peer_;
    std::thread link_thread_, peer_thread_;
    std::atomic<int> peer_missing_{0};
    std::atomic<bool> bus_restarted_{false};
    std::atomic<bool> link_ready_{false}, link_failed_{false};

    std::mutex inbound_gate_;
    std::deque<Frame> inbound_;
    std::size_t inbound_bytes_ = 0;
    std::uint64_t inbound_dropped_ = 0;

    std::mutex outbound_gate_;
    std::condition_variable outbound_ready_;
    std::deque<Frame> outbound_;
    std::size_t outbound_bytes_ = 0;
    std::uint64_t outbound_dropped_ = 0;

    std::atomic<std::uint64_t> link_sent_{0}, link_received_{0}, link_send_refused_{0};
    std::uint64_t frames_routed_ = 0, malformed_ = 0, client_drops_ = 0;
};

int Worker(const char* config_path, const char* socket_path, int recover_after)
{
    std::ifstream in(config_path);
    if (!in) { std::fprintf(stderr, "FDDS_HUB_REFUSED reason=config not readable\n"); return kRefusedExit; }
    std::ostringstream text; text << in.rdbuf();
    ra::Config config;
    const auto problem = ra::ParseConfig(text.str(), config);
    if (!problem.empty()) { std::fprintf(stderr, "FDDS_HUB_REFUSED reason=config: %s\n", problem.c_str()); return kRefusedExit; }
    std::signal(SIGINT, OnStopSignal);
    std::signal(SIGTERM, OnStopSignal);
    Hub hub_instance(std::move(config), socket_path, recover_after);
    return hub_instance.Run();
}

std::atomic<pid_t> g_worker{0};
std::atomic<bool> g_supervisor_stopping{false};
void ForwardStop(int number)
{
    g_supervisor_stopping = true;
    const pid_t pid = g_worker.load();
    if (pid > 0) ::kill(pid, number);
}

// The worker is a fresh exec, so nothing of the SDK reaches this process.
int Supervise(char* self, char* recover, char* config, char* socket)
{
    std::signal(SIGINT, ForwardStop);
    std::signal(SIGTERM, ForwardStop);
    std::signal(SIGUSR1, SIG_IGN); // meant for the worker, which installs its own handler
    sigset_t stops, previous;
    sigemptyset(&stops);
    sigaddset(&stops, SIGINT);
    sigaddset(&stops, SIGTERM);
    int bus_pause = static_cast<int>(kRestartPause.count());
    while (!g_supervisor_stopping.load()) {
        // Held back until the worker's pid is published, so a stop in between reaches the worker.
        (void)::sigprocmask(SIG_BLOCK, &stops, &previous);
        if (g_supervisor_stopping.load()) break; // arrived before the block
        const auto started = std::chrono::steady_clock::now();
        const pid_t pid = ::fork();
        if (pid < 0) return kRefusedExit;
        if (pid == 0) {
            std::signal(SIGINT, SIG_DFL);
            std::signal(SIGTERM, SIG_DFL);
            (void)::sigprocmask(SIG_SETMASK, &previous, nullptr);
            char flag[] = "--recover-bus-after";
            char* const with_recovery[] = {self, flag, recover, config, socket, nullptr};
            char* const plain[] = {self, config, socket, nullptr};
            ::execv("/proc/self/exe", recover != nullptr ? with_recovery : plain);
            _exit(kRefusedExit);
        }
        g_worker = pid;
        (void)::sigprocmask(SIG_SETMASK, &previous, nullptr);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        g_worker = 0;
        const int code = WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
        std::printf("FDDS_HUB_WORKER_EXIT code=%d\n", code); std::fflush(stdout);
        if (code == kRefusedExit) return code; // configuration: a restart cannot help
        if (code == 0) return 0;
        if (std::chrono::steady_clock::now() - started >= kHealthyRun) bus_pause = static_cast<int>(kRestartPause.count());
        const int pause = code == kBusRestartedExit ? bus_pause : static_cast<int>(kRestartPause.count());
        if (code == kBusRestartedExit) bus_pause = std::min(bus_pause * 2, kMaxBusRestartPauseSeconds);
        for (int left = pause; left > 0 && !g_supervisor_stopping.load(); --left) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
}

int main(int argc, char** argv)
{
    int next = 1;
    const bool supervise = next < argc && std::strcmp(argv[next], "--supervise") == 0;
    if (supervise) ++next;
    char* recover = nullptr;
    if (next + 1 < argc && std::strcmp(argv[next], "--recover-bus-after") == 0) { recover = argv[next + 1]; next += 2; }
    char* end = recover;
    const long seconds = recover != nullptr ? std::strtol(recover, &end, 10) : 0;
    if (argc - next != 2 || (recover != nullptr && (seconds <= 0 || seconds > 86400 || *end != 0))) {
        std::fprintf(stderr, "usage: fdds_dsb_hub [--supervise] [--recover-bus-after <seconds>] <config> <socket path>\n");
        return 2;
    }
    if (supervise) return Supervise(argv[0], recover, argv[next], argv[next + 1]);
    return Worker(argv[next], argv[next + 1], static_cast<int>(seconds));
}
