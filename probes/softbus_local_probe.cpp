// Minimal same-device/same-process native Socket diagnostic, not a transport.
#include "softbus_local_pair.hpp"
#include "softbus_probe_profile.hpp"
#include "token_first_entry.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <softbus_bus_center.h>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <memory>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>
namespace {
using namespace mdds_probe;
using Clock = std::chrono::steady_clock;
constexpr char pkg[] = "com.kaihong.mdds";
constexpr char process_name[] = "fastdds_dsoftbus_probe";
constexpr char permission[] = "ohos.permission.DISTRIBUTED_DATASYNC";
static_assert(probe_bytes_data_type == DATA_TYPE_BYTES && probe_raw_data_type == DATA_TYPE_RAW_STREAM &&
        probe_encrypted_raw_data_type == DATA_TYPE_RAW_STREAM_ENCRYPED, "Socket data-type ABI changed");
struct LocalRuntime {
    const Clock::time_point start = Clock::now();
    const ProbeProfileDescription profile;
    std::unique_ptr<LocalPairProbeContract> pair;
    void* provider = nullptr;
    decltype(&Socket) create = nullptr;
    decltype(&Listen) listen = nullptr;
    decltype(&Bind) bind = nullptr;
    decltype(&SendBytes) send_bytes = nullptr;
    decltype(&SendStream) send_stream = nullptr;
    decltype(&Shutdown) shutdown = nullptr;
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> exception{false};
    unsigned shutdowns = 0, server_records = 0, client_records = 0;
    explicit LocalRuntime(ProbeProfile value) : profile(*DescribeProbeProfile(value)) {}
    long long Now() const { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count(); }
    void Log(const char* event, int id, int rc) const {
        std::printf("event=%s pid=%d tid=%ld invocation=local:%d:%s:%d steady_us=%lld socket=%d rc=%d\n",
                event, getpid(), syscall(SYS_gettid), getpid(), event, id, Now(), id, rc);
    }
};
// Two process-lifetime function tables. No SDK generation/context is invented.
// This probe has one native incarnation per table and does not test reuse.
LocalRuntime* runtime = nullptr;
template<LocalSide Side, class Action> void Callback(Action action) noexcept {
    auto* value = runtime;
    if (!value || !value->pair) return;
    ++value->callbacks;
    try { action(*value, value->pair->Channel(Side)); }
    catch (...) { value->exception = true; }
    --value->callbacks;
}
template<LocalSide Side> void Bound(int id, PeerSocketInfo peer) noexcept {
    Callback<Side>([&](LocalRuntime& value, SoftbusRecordContract& channel) {
        value.Log("OnBind", id, 0);
        if (Side != LocalSide::Listener) { channel.Fail("unexpected_client_OnBind"); return; }
        if (value.pair->BeginAccepted(id)) {
            // ID retained before copying any borrowed SDK string.
            try { value.pair->CompleteAccepted(id, CopyRecordPeer(peer.name, peer.networkId, peer.pkgName, peer.dataType)); }
            catch (...) { channel.FailBoundCopy(id); throw; }
        }
    });
}
template<LocalSide Side> void Bytes(int id, const void* data, std::uint32_t size) noexcept {
    Callback<Side>([&](LocalRuntime& value, SoftbusRecordContract& channel) {
        value.Log("OnBytes", id, static_cast<int>(size));
        if (value.profile.send_method != ProbeSendMethod::Bytes) channel.Fail("wrong_callback_type");
        else channel.OnBytes(id, data, size);
    });
}
template<LocalSide Side> void Stream(int id, const StreamData* data, const StreamData*, const StreamFrameInfo*) noexcept {
    Callback<Side>([&](LocalRuntime& value, SoftbusRecordContract& channel) {
        value.Log("OnStream", id, data ? data->bufLen : -1);
        if (value.profile.send_method != ProbeSendMethod::Stream ||
                !ValidateProbeStreamFrame(data != nullptr, data ? data->buf : nullptr, data ? data->bufLen : 0, record_queue_bytes))
            channel.Fail("wrong_stream_frame");
        else channel.OnBytes(id, data->buf, static_cast<std::size_t>(data->bufLen));
    });
}
template<LocalSide Side> void Closed(int id, ShutdownReason reason) noexcept {
    Callback<Side>([&](LocalRuntime& value, SoftbusRecordContract& channel) {
        value.Log("OnShutdown", id, reason); channel.OnShutdown(id, reason);
    });
}
template<LocalSide Side> void Error(int id, std::int32_t code) noexcept {
    Callback<Side>([&](LocalRuntime& value, SoftbusRecordContract& channel) {
        value.Log("OnError", id, code); channel.OnError(id, code);
    });
}
template<LocalSide Side> ISocketListener Listener() {
    ISocketListener result{};
    result.OnBind = Bound<Side>; result.OnBytes = Bytes<Side>; result.OnStream = Stream<Side>;
    result.OnShutdown = Closed<Side>; result.OnError = Error<Side>;
    return result;
}
bool Send(LocalRuntime& value, int id, std::vector<std::uint8_t>& data) {
    value.Log("send_enter", id, static_cast<int>(data.size()));
    const auto before = value.Now();
    int rc;
    if (value.profile.send_method == ProbeSendMethod::Bytes)
        rc = value.send_bytes(id, data.data(), static_cast<std::uint32_t>(data.size()));
    else {
        StreamData stream{reinterpret_cast<char*>(data.data()), static_cast<int>(data.size())};
        StreamData ext{}; StreamFrameInfo frame{};
        rc = value.send_stream(id, &stream, &ext, &frame);
    }
    const auto duration = value.Now()-before;
    value.Log("send_return", id, rc);
    return rc == 0 && duration <= 1000000;
}
bool RunPair(LocalRuntime& value) {
    value.provider = dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so", RTLD_NOW | RTLD_LOCAL);
    if (!value.provider) return false;
    value.create = reinterpret_cast<decltype(&Socket)>(dlsym(value.provider,"Socket"));
    value.listen = reinterpret_cast<decltype(&Listen)>(dlsym(value.provider,"Listen"));
    value.bind = reinterpret_cast<decltype(&Bind)>(dlsym(value.provider,"Bind"));
    value.send_bytes = reinterpret_cast<decltype(&SendBytes)>(dlsym(value.provider,"SendBytes"));
    value.send_stream = reinterpret_cast<decltype(&SendStream)>(dlsym(value.provider,"SendStream"));
    value.shutdown = reinterpret_cast<decltype(&Shutdown)>(dlsym(value.provider,"Shutdown"));
    const auto local = reinterpret_cast<decltype(&GetLocalNodeDeviceInfo)>(dlsym(value.provider,"GetLocalNodeDeviceInfo"));
    if (!value.create || !value.listen || !value.bind || !value.send_bytes || !value.send_stream || !value.shutdown || !local)
        return false;
    NodeBasicInfo info{};
    const int local_rc = local(pkg, &info);
    value.Log("GetLocalNodeDeviceInfo_return", 0, local_rc);
    std::string network;
    if (local_rc != 0 || !CopyRouteNetworkId(info.networkId, sizeof(info.networkId), network)) return false;
    std::string listen_name = std::string(pkg) + ".local." + std::to_string(getpid()) + ".server";
    std::string dial_name = std::string(pkg) + ".local." + std::to_string(getpid()) + ".client";
    value.pair = std::make_unique<LocalPairProbeContract>(listen_name, dial_name, network, value.profile.peer_data_type);
    auto server = Listener<LocalSide::Listener>();
    auto client = Listener<LocalSide::Client>();
    QosTV qos[] = {{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};
    SocketInfo server_info{};
    server_info.name = listen_name.data(); server_info.pkgName = const_cast<char*>(pkg);
    server_info.dataType = static_cast<TransDataType>(value.profile.socket_data_type);
    const int server_id = value.create(server_info);
    value.Log("Socket_listener_return", server_id, server_id);
    if (!value.pair->Created(LocalSide::Listener, server_id)) return false;
    const int listen_rc = value.listen(server_id, qos, 2, &server);
    value.Log("Listen_return", server_id, listen_rc);
    if (listen_rc != 0) return false;
    SocketInfo client_info = server_info;
    client_info.name = dial_name.data(); client_info.peerName = listen_name.data(); client_info.peerNetworkId = network.data();
    const int client_id = value.create(client_info);
    value.Log("Socket_client_return", client_id, client_id);
    if (!value.pair->Created(LocalSide::Client, client_id)) return false;
    value.Log("Bind_self_enter", client_id, 0);
    const int bind_rc = value.bind(client_id, qos, 2, &client);
    value.Log("Bind_self_return", client_id, bind_rc);
    if (!value.pair->Channel(LocalSide::Client).BindReturned(client_id, bind_rc)) return false;
    auto data = MakeProbeRecord(0);
    if (!Send(value, client_id, data)) return false;
    while (value.Now() < 6000000 && !value.pair->Failed() && !value.exception) {
        for (LocalSide side : {LocalSide::Listener, LocalSide::Client}) {
            RecordEvent event;
            if (!value.pair->Channel(side).WaitPop(event, Clock::now())) continue;
            if (event.kind == RecordEventKind::Bytes) {
                unsigned& count = side == LocalSide::Listener ? value.server_records : value.client_records;
                const bool valid = count == 0 && ValidateProbeRecord(0, event.bytes);
                value.Log(side == LocalSide::Listener ? "server_record_exact" : "client_record_exact", event.socket, valid ? 0 : -1);
                if (!valid) return false;
                ++count;
                if (side == LocalSide::Listener && !Send(value, event.socket, event.bytes)) return false;
            }
        }
        if (value.server_records == 1 && value.client_records == 1) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
bool Close(LocalRuntime& value) {
    const auto until = Clock::now() + std::chrono::milliseconds(200);
    do {
        if (value.pair && value.shutdown) for (int id : value.pair->ClaimShutdownTargets()) {
            value.Log("Shutdown_enter", id, 0); value.shutdown(id);
            ++value.shutdowns; value.Log("Shutdown_return", id, 0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (Clock::now() < until && value.Now() < 7000000);
    return value.Now() <= 7000000 && !value.exception && value.callbacks == 0;
}
class TokenOperations final : public TokenEntryOperations {
public:
    explicit TokenOperations(ProbeProfile value) : profile(value) {}
    TaskCheckpoint ObserveTasks() override {
        TaskCheckpoint out; out.process_id = getpid(); out.current_tid = syscall(SYS_gettid);
        DIR* dir = opendir("/proc/self/task"); out.opened = dir != nullptr;
        if (dir) {
            bool valid = true;
            for (;;) {
                errno = 0; const auto* entry = readdir(dir);
                if (!entry) { out.complete = valid && errno == 0; break; }
                if (entry->d_name[0] == '.') continue;
                std::uint64_t tid = 0; const auto* end = entry->d_name + std::strlen(entry->d_name);
                const auto parsed = std::from_chars(entry->d_name, end, tid);
                if (parsed.ec != std::errc{} || parsed.ptr != end || tid == 0) valid = false;
                if (out.count == 0) out.sole_tid = tid;
                if (out.count < 2) ++out.count;
            }
            if (closedir(dir) != 0) out.complete = false;
        }
        return out;
    }
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    int SetSelf(std::uint64_t token) override { return SetSelfTokenID(token); }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override {
        using namespace OHOS::Security::AccessToken;
        const auto id = static_cast<AccessTokenID>(token);
        const auto lookup = AccessTokenKit::GetNativeTokenId(process_name);
        const auto type = AccessTokenKit::GetTokenTypeFlag(id);
        NativeTokenInfo info{}; const int rc = AccessTokenKit::GetNativeTokenInfo(id, info);
        std::vector<std::int32_t> states;
        const int query = AccessTokenKit::VerifyAccessToken(id, std::vector<std::string>{permission}, states, true);
        std::printf("stage=identity phase=%s lookup_match=%d query_rc=%d states=%zu\n", phase, lookup == id, query, states.size());
        return {lookup, static_cast<int>(type), static_cast<int>(TOKEN_NATIVE), rc, std::move(info.processName),
            static_cast<int>(info.apl), static_cast<int>(APL_NORMAL), query, std::move(states), static_cast<int>(PERMISSION_GRANTED)};
    }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int code) override {
        std::printf("stage=%s expected=%llu observed=%llu rc=%d\n", stage,
                static_cast<unsigned long long>(first), static_cast<unsigned long long>(second), code);
    }
    bool EnterProvider() override { runtime = new LocalRuntime(profile); return RunPair(*runtime); }
    bool CloseProvider() override { return runtime ? Close(*runtime) : true; }
private:
    ProbeProfile profile;
};
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::uint64_t token = 0; ProbeProfile profile = ProbeProfile::Bytes;
    if (argc != 6 || std::strcmp(argv[1], "--profile") || !ParseProbeProfile(argv[2], profile) ||
            std::strcmp(argv[3], "--expected-token") || !ParseTokenArgument(argv[4], token) ||
            std::strcmp(argv[5], "--preflight-confirmed") || !AdmitTokenEntry({TokenEntryMode::Record,true,true,token})) {
        std::fprintf(stderr,"Usage: softbus_local_probe --profile bytes|raw|raw-encrypted --expected-token OWNED_ID --preflight-confirmed\n");
        return 64;
    }
    std::printf("scope=NATIVE_SOCKET_LOCAL_PAIR topology=SAME_DEVICE_SAME_PROCESS profile=%s target=LOCAL_NETWORK_ID writes=NONE production=NOT_IMPLEMENTED\n",
            DescribeProbeProfile(profile)->name);
    TokenOperations operations(profile); TokenFirstEntry entry(operations);
    const bool ok = entry.Run({TokenEntryMode::Record,true,true,token});
    const bool success = ok && runtime && runtime->pair && !runtime->pair->Failed() &&
            runtime->server_records == 1 && runtime->client_records == 1 &&
            runtime->pair->Channel(LocalSide::Listener).Snapshot().byte_callbacks == 1 &&
            runtime->pair->Channel(LocalSide::Client).Snapshot().byte_callbacks == 1;
    std::printf("result=%s token_restored=%d shutdown_returns=%u callbacks_observed_active=%u cleanup=OBSERVED_KNOWN_IDS_ONLY sdk_quiescence=NOT_PROVEN\n",
            success ? "PASS" : "FAIL", entry.RestoreSucceeded(), runtime ? runtime->shutdowns : 0,
            runtime ? runtime->callbacks.load() : 0);
    return success ? 0 : 6;
}
