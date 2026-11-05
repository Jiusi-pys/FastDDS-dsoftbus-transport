// LD_PRELOAD library that selects the DSoftBus Transport for an unmodified ROS 2 process.
//
// rmw_fastrtps builds each Participant from the Fast DDS default QoS, so installing the Transport
// there before main() is enough; no RMW, ROS or application code changes. Any failure ends the
// process: running on after a failure would silently use another transport or none.
//
// Not covered: the writer-side content filter workaround. reader_filters_allocation has no XML form and
// rmw_fastrtps never sets it, so ROS applications must not create ContentFilteredTopics while writers
// are active (docs/FASTDDS_VERSIONS_AND_RELEASE.md, content-filter race).
#include "AutoloadConfig.hpp"
#include "DeviceDeployment.hpp"
#include "../stock/ScopedTransportBootstrap.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <memory>
#include <sstream>
#include <sys/file.h>
#include <thread>
#include <unistd.h>

namespace {
namespace ds = fastdds_dsoftbus::dsoftbus;
namespace dd = fastdds_dsoftbus::device_deployment;
namespace st = fastdds_dsoftbus::stock_transport;
namespace ra = fastdds_dsoftbus::ros2_autoload;
namespace fd = eprosima::fastdds::dds;

constexpr char kPackage[] = "com.kaihong.mdds";
constexpr int kRefusedExit = 71;
constexpr int kGateSeconds = 180;

struct State {
    ra::Config config;
    std::shared_ptr<fd::DomainParticipantFactory> factory; // keeps the singleton past Runtime close
    std::unique_ptr<st::StockTransportBootstrap> bootstrap;
    dd::PumpedRuntime pumped;
    pid_t owner = getpid(); // a forked child inherits the atexit handler, not the threads or SDK session
};
State* g_state = nullptr; // deliberately never freed: exit paths may still reference it

[[noreturn]] void Refuse(const std::string& why)
{
    std::fprintf(stderr, "FDDS_AUTOLOAD_REFUSED reason=%s\n", why.c_str());
    std::fflush(stderr);
    _exit(kRefusedExit);
}

// One config names one native socket, so a second process would fail inside the SDK while looking healthy, and
// hear nothing. The kernel releases the lock with the process, however it ends.
void TakeConfigLock(const char* path)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) Refuse("config not readable");
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) Refuse("config already in use by another process: use the hub for more than one process per board");
}

std::string ReadConfigFile()
{
    const char* path = std::getenv("FASTDDS_DSOFTBUS_AUTOLOAD_CONFIG");
    if (path == nullptr || path[0] != '/') Refuse("FASTDDS_DSOFTBUS_AUTOLOAD_CONFIG must be an absolute path");
    TakeConfigLock(path);
    std::ifstream in(path);
    if (!in) Refuse("config not readable");
    std::ostringstream text; text << in.rdbuf();
    return text.str();
}

// libsoftbus_client loads libffrt, whose SIGINT/SIGTERM handler faults off an FFRT thread. rclcpp::init
// saves whatever handler it finds and chains to it, so Ctrl-C would crash the process. Only libffrt's
// handler is removed, before rclcpp sees it; a handler owned by anything else is left in place.
void ClearFfrtSignalHandler(int number)
{
    struct sigaction current {};
    if (sigaction(number, nullptr, &current) != 0) Refuse("sigaction query");
    const void* handler = (current.sa_flags & SA_SIGINFO) != 0 ? reinterpret_cast<const void*>(current.sa_sigaction)
                                                               : reinterpret_cast<const void*>(current.sa_handler);
    if (handler == reinterpret_cast<const void*>(SIG_DFL) || handler == reinterpret_cast<const void*>(SIG_IGN)) return;
    Dl_info owner {};
    if (dladdr(handler, &owner) == 0 || owner.dli_fname == nullptr || std::strstr(owner.dli_fname, "libffrt") == nullptr) return;
    struct sigaction reset {};
    reset.sa_handler = SIG_DFL;
    sigemptyset(&reset.sa_mask);
    if (sigaction(number, &reset, nullptr) != 0) Refuse("sigaction reset");
    std::printf("FDDS_AUTOLOAD_SIGNAL_RESET signal=%d owner=libffrt\n", number);
    std::fflush(stdout);
}

bool WaitForGo(const std::string& path)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(kGateSeconds);
    while (access(path.c_str(), F_OK) != 0) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

// Runs through atexit, not an ELF destructor: Close calls the SDK, which crashes once the libraries it
// uses are being finalized. Registered in the constructor, so it follows rclcpp's own exit handlers.
void AutoloadStop()
{
    if (g_state == nullptr) return;
    auto& s = *g_state;
    if (getpid() != s.owner) return;
    const auto domain = s.config.domain;
    if (!s.factory->lookup_participants(domain).empty()) {
        // The Runtime may still be in use, so it stays open and this run is not a normal close. The pump
        // stops anyway: libraries are finalized next, and it must not call into them.
        s.pumped.rt->RequestPumpStop();
        if (s.pumped.pump.joinable()) s.pumped.pump.join();
        std::fprintf(stderr, "FDDS_AUTOLOAD_CLOSE_INCOMPLETE reason=participants_remain\n");
        std::fflush(stderr);
        return;
    }
    const bool released = s.bootstrap->Release() == st::BootstrapResult::Ok;
    std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> none;
    const bool closed = s.pumped.stop_and_close(none);
    const bool pump_failed = s.pumped.failed.load(); // read after the pump stopped, so nothing is missed
    if (released && closed && !pump_failed) {
        std::printf("FDDS_AUTOLOAD_CLOSED ok=1 sdk_sends=%llu received_records=%llu send_failures=%u last_code=%d\n",
            static_cast<unsigned long long>(s.pumped.sdk_sends.load()), static_cast<unsigned long long>(s.pumped.received_records.load()),
            s.pumped.send_failure_events.load(), s.pumped.last_send_failure.load());
        std::fflush(stdout);
    } else {
        std::fprintf(stderr, "FDDS_AUTOLOAD_CLOSE_INCOMPLETE released=%d closed=%d pump_failed=%d failure_source=%d failure_value=%lld send_failures=%u last_code=%d\n",
            released, closed, pump_failed, s.pumped.failure_source.load(), static_cast<long long>(s.pumped.failure_value.load()),
            s.pumped.send_failure_events.load(), s.pumped.last_send_failure.load());
        std::fflush(stderr);
    }
}

__attribute__((constructor)) void AutoloadStart()
{
    try {
        auto state = std::make_unique<State>();
        const auto problem = ra::ParseConfig(ReadConfigFile(), state->config);
        if (!problem.empty()) Refuse("config: " + problem);
        const auto& c = state->config;
        const auto environment = ra::CheckRosEnvironment([](const char* n) { return std::getenv(n); }, c.domain);
        if (!environment.empty()) Refuse(environment);
        dd::SetSelfToken(c.token);
        const auto deployment = dd::MakeDeploy(kPackage, "dsb.ros2", c.side, c.run, c.domain, c.local_udid, c.peer_udid, c.peer_network);
        const auto descriptor = dd::Configuration(deployment, true);
        state->pumped.rt = std::make_unique<ds::experimental::Runtime>(
            dd::Backend(kPackage, descriptor), ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
        const std::string go = c.go_file;
        const unsigned side = c.side;
        if (!state->pumped.rt->SetPreConnectGate([go, side] {
                std::printf("FDDS_AUTOLOAD_LISTENING side=%u\n", side); std::fflush(stdout);
                return WaitForGo(go); }))
            Refuse("pre-connect gate not applied");
        state->factory = fd::DomainParticipantFactory::get_shared_instance();
        state->bootstrap = std::make_unique<st::StockTransportBootstrap>(*state->factory);
        if (st::InstallStockTransport(*state->bootstrap, *state->pumped.rt, descriptor) != st::BootstrapResult::Ok)
            Refuse("InstallStockTransport");
        // Same library setting as the device suite: traffic stays on the Transport.
        static constexpr char kSettings[] = "<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings>"
            "<intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
        using ReturnCode = eprosima::fastrtps::types::ReturnCode_t; // Jazzy line (Fast DDS 2.14) only
        if (state->factory->load_XML_profiles_string(kSettings, sizeof(kSettings) - 1) != ReturnCode::RETCODE_OK)
            Refuse("library settings");
        // A failed send is normal when the peer process exits or a route drops: RTPS recovers, or the peer
        // is gone. So it is counted and reported, and the test oracle checks when it happened. Exceptions
        // still latch failure.
        state->pumped.tolerate = true;
        state->pumped.on_first_send_failure = [](std::int32_t code) {
            std::fprintf(stderr, "FDDS_AUTOLOAD_SEND_FAILURE code=%d\n", code); std::fflush(stderr); };
        state->pumped.start();
        ClearFfrtSignalHandler(SIGINT);
        ClearFfrtSignalHandler(SIGTERM);
        std::printf("FDDS_AUTOLOAD_READY side=%u domain=%u run=%s\n", c.side, c.domain, c.run.c_str());
        std::fflush(stdout);
        g_state = state.release();
        if (std::atexit(AutoloadStop) != 0) Refuse("atexit");
    } catch (const std::exception& error) {
        Refuse(error.what());
    } catch (...) {
        Refuse("unknown exception");
    }
}
}
