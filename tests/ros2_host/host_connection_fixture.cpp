// Two independent processes exercise the Windows Host Socket seam, not the SDK.
#include "../../integrations/ros2/HostIoObservation.hpp"
#include "../../integrations/ros2/HostSocket.hpp"
#include <socket.h>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace host = fastdds_dsoftbus::ros2_host;
namespace io = host::io_observation;
namespace {
std::mutex gate;
std::condition_variable changed;
int connected = -1;
unsigned binds = 0, received = 0, shutdowns = 0;
bool correct = true;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool negotiate(int32_t, PeerSocketInfo) { return true; }
void bound(int32_t id, PeerSocketInfo) {
    std::lock_guard<std::mutex> lock(gate);
    ++binds; connected = id; changed.notify_all();
}
void bytes(int32_t id, const void* value, uint32_t size) {
    std::lock_guard<std::mutex> lock(gate);
    ++received;
    correct = correct && id == connected && size == 3 && std::memcmp(value, "abc", 3) == 0;
    changed.notify_all();
}
void closed(int32_t id, ShutdownReason reason) {
    std::lock_guard<std::mutex> lock(gate);
    ++shutdowns;
    correct = correct && id == connected && reason == SHUTDOWN_REASON_PEER;
    changed.notify_all();
}
template<class Predicate> void await(Predicate predicate, const char* message) {
    std::unique_lock<std::mutex> lock(gate);
    check(changed.wait_for(lock, std::chrono::seconds(10), predicate), message);
}
struct Deadline {
    HANDLE done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    std::thread thread;
    Deadline() {
        check(done != nullptr, "watchdog event");
        thread = std::thread([this] {
            if (WaitForSingleObject(done, 45000) != WAIT_OBJECT_0) {
                std::fputs("HOST_DIRECTED_FIXTURE_DEADLINE\n", stderr); std::fflush(stderr);
                TerminateProcess(GetCurrentProcess(), 124);
            }
        });
    }
    ~Deadline() { SetEvent(done); if (thread.joinable()) thread.join(); CloseHandle(done); }
};
struct LinkOwner {
    bool started = false;
    ~LinkOwner() { if (started) host::StopLink(); }
    void Stop() { host::StopLink(); started = false; }
};
struct Controls {
    std::filesystem::path directory;
    std::string run, side;
    DWORD pid;
    std::string Record(const std::string& action) const {
        return "HOST_DIRECTED_CONTROL schema=1 run=" + run + " side=" + side +
            " pid=" + std::to_string(pid) + " action=" + action;
    }
    std::filesystem::path Path(const std::string& action) const { return directory / (side + "." + action); }
    void Receipt(const std::string& action) const {
        const auto path = Path(action), pending = Path(action + ".pending");
        check(!std::filesystem::exists(path), "duplicate receipt");
        { std::ofstream out(pending, std::ios::binary); out << Record(action) << '\n'; out.close(); check(bool(out), "receipt write"); }
        std::filesystem::rename(pending, path);
        std::printf("%s\n", Record(action).c_str()); std::fflush(stdout);
    }
    void Wait(const std::string& action) const {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < end) {
            if (std::filesystem::exists(Path(action))) {
                std::ifstream in(Path(action), std::ios::binary);
                const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                // Python's atomic text publisher uses CRLF on Windows; accept
                // only one exact record with either supported line ending.
                check(content == Record(action) + "\n" || content == Record(action) + "\r\n",
                    "control identity or format");
                std::printf("HOST_DIRECTED_COMMAND schema=1 run=%s side=%s pid=%lu action=%s\n",
                    run.c_str(), side.c_str(), pid, action.c_str()); std::fflush(stdout);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("control deadline: " + action);
    }
};
unsigned port(const char* value) {
    const std::string text(value); std::size_t used = 0;
    const auto result = std::stoul(text, &used);
    check(used == text.size() && result > 0 && result <= 65535, "port");
    return static_cast<unsigned>(result);
}
}
int main(int argc, char** argv) {
    if (argc != 7) return 2;
    try {
        Deadline deadline;
        const std::string side(argv[1]), run(argv[2]), mode(argv[3]);
        check((side == "A" || side == "B") && run.size() == 16 &&
            run.find_first_not_of("0123456789abcdef") == std::string::npos, "identity");
        check(mode == "positive" || mode == "peer-close" || mode == "local-close", "mode");
        Controls controls{std::filesystem::path(argv[6]), run, side, GetCurrentProcessId()};
        host::LinkConfiguration config;
        config.run = run; config.side = side == "B"; config.port = static_cast<uint16_t>(port(argv[4]));
        config.peer_port = static_cast<uint16_t>(port(argv[5])); config.io_observation = true;
        config.package = "host.connection.fixture"; config.local_listener = "fixture.listener." + side;
        config.peer_listener = side == "A" ? "fixture.listener.B" : "fixture.listener.A";
        config.peer_outgoing = side == "A" ? "fixture.outgoing.B" : "fixture.outgoing.A";
        ISocketListener callback{};
        callback.OnNegotiate = negotiate; callback.OnBind = bound; callback.OnBytes = bytes; callback.OnShutdown = closed;
        LinkOwner link;
        host::ConfigureLink(config); link.started = true;
        SocketInfo info{}; info.name = config.local_listener.data(); info.pkgName = config.package.data(); info.dataType = DATA_TYPE_BYTES;
        const int listening = ::Socket(info);
        check(listening == 1 && ::Listen(listening, nullptr, 0, &callback) == 0, "listen");
        check(host::WaitPeerListener(10000), "peer listener deadline");
        if (side == "A") {
            std::string outgoing = "fixture.outgoing.A", network = "host.net.B";
            info.name = outgoing.data(); info.peerName = config.peer_listener.data(); info.peerNetworkId = network.data();
            const int dial = ::Socket(info);
            check(dial == 2 && ::Bind(dial, nullptr, 0, &callback) == 0, "bind");
        }
        await([] { return binds == 1; }, "connected deadline");
        int id;
        { std::lock_guard<std::mutex> lock(gate); id = connected; }
        controls.Receipt("READY");
        const bool close_here = (mode == "peer-close" && side == "B") || (mode == "local-close" && side == "A");
        if (close_here) {
            controls.Wait("CLOSE");
            ::Shutdown(id); // Actual synchronous Host seam close, then publish the receipt.
            controls.Receipt("CLOSED");
        }
        int send_result = 999;
        if (side == "A") {
            controls.Wait("SEND");
            send_result = ::SendBytes(id, "abc", 3);
            check(mode == "positive" ? send_result == 0 : mode == "local-close" ? send_result == -608 :
                (send_result == 0 || send_result == -608 || send_result == -609), "send result contract");
            std::printf("HOST_DIRECTED_SEND schema=1 run=%s side=A pid=%lu socket=%d result=%d\n", run.c_str(), GetCurrentProcessId(), id, send_result);
            std::fflush(stdout); controls.Receipt("SENT");
        } else if (mode == "positive") {
            await([] { return received == 1; }, "payload deadline");
            controls.Receipt("RECEIVED");
        }
        controls.Wait("STOP");
        link.Stop(); // Both callbacks are joined before reading/exporting their final state.
        const auto observation = host::ObserveLink();
        check(binds == 1 && correct && received == (mode == "positive" && side == "B" ? 1u : 0u), "callback contract");
        check(observation.ingress == received, "ingress accounting");
        check(observation.sends == (side == "A" && send_result != -608 ? 1u : 0u) &&
            observation.send_errors == (side == "A" && send_result == -609 ? 1u : 0u), "send accounting");
        check(io::Export(run.c_str(), config.side), "capture export");
        std::printf("HOST_DIRECTED_DONE schema=1 run=%s side=%s pid=%lu mode=%s socket=%d result=%d binds=%u received=%u shutdowns=%u correct=1 sends=%llu ingress=%llu send_errors=%llu joined=1\n",
            run.c_str(), side.c_str(), GetCurrentProcessId(), mode.c_str(), id, send_result, binds, received, shutdowns,
            observation.sends, observation.ingress, observation.send_errors);
        std::fflush(stdout);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "HOST_DIRECTED_FIXTURE_FAILED reason=%s\n", error.what()); return 1;
    }
}
