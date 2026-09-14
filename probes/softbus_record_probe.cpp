// S2 native Socket record experiment, not a Fast DDS Transport implementation.
// This executable NEVER creates/reloads/revokes a token or writes token JSON.
#include "softbus_record_contract.hpp"
#include "token_first_entry.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <softbus_bus_center.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
using namespace mdds_probe;
using namespace OHOS::Security::AccessToken;
using Clock = std::chrono::steady_clock;
constexpr char process_name[] = "fastdds_dsoftbus_probe";
constexpr char permission[] = "ohos.permission.DISTRIBUTED_DATASYNC";
constexpr char package_name[] = "com.kaihong.mdds";
constexpr char provider_path[] = "/system/lib64/platformsdk/libsoftbus_client.z.so";
constexpr auto run_budget = std::chrono::seconds(7);
constexpr auto quiet_observation = std::chrono::milliseconds(200);

struct Options {
    bool server = false;
    bool list_nodes = false;
    std::uint64_t token = 0;
    std::string local_name, remote_listen_name, peer_network_id, expected_initiator_name, expected_peer_network_id;
};

void Usage()
{
    std::cerr << "Usage: softbus_record_probe --server --local-name <listen>"
        " --expected-remote-initiator-name <dial> --expected-peer-network-id <64hex>"
        " --expected-token <owned-id> --preflight-confirmed\n"
        "   or: softbus_record_probe --list-nodes --expected-token <owned-id> --preflight-confirmed\n"
        "--list-nodes queries current local/online network IDs only; creates no Socket and changes no network membership.\n"
        "   or: softbus_record_probe --client --local-name <dial> --remote-listen-name <listen>"
        " --peer-network-id <64hex> --expected-peer-network-id <same64hex>"
        " --expected-token <owned-id> --preflight-confirmed\n"
        "Requires externally verified owned normal/DATASYNC identity; performs no token-file writes.\n"
        "Record-mode token allowlist: 671793191 (A), 671320465 (B); board binding is externally verified.\n"
        "--preflight-confirmed is an operator assertion, not internal authorization evidence.\n"
        "Experiment: one process, one peer, five byte records, 7s absolute run budget.\n"
        "SDK calls are not cancellable by this budget; use external 10s watchdog (FAIL if triggered).\n";
}
bool ValidName(const std::string& name)
{
    constexpr const char* prefix = "com.kaihong.mdds.";
    if (name.size() <= std::char_traits<char>::length(prefix) || name.size() > 63 || name.rfind(prefix, 0) != 0)
        return false;
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
bool ValidNetwork(const std::string& network)
{
    if (network.size() != 64) return false;
    for (unsigned char c : network)
        if (!((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9'))) return false;
    return true;
}
bool ParseOptions(int argc, char** argv, Options& options)
{
    bool mode = false, token = false, preflight = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key(argv[i]);
        if ((key == "--server" || key == "--client" || key == "--list-nodes") && !mode)
        { mode = true; options.server = key == "--server"; options.list_nodes = key == "--list-nodes"; }
        else if (key == "--preflight-confirmed" && !preflight) preflight = true;
        else if (key == "--expected-token" && !token && i + 1 < argc)
        {
            token = true; const std::string value(argv[++i]);
            if (!ParseTokenArgument(value, options.token)) return false;
        }
        else
        {
            std::string* field = key == "--local-name" ? &options.local_name :
                key == "--remote-listen-name" ? &options.remote_listen_name :
                key == "--peer-network-id" ? &options.peer_network_id :
                key == "--expected-remote-initiator-name" ? &options.expected_initiator_name :
                key == "--expected-peer-network-id" ? &options.expected_peer_network_id : nullptr;
            if (!field || !field->empty() || i + 1 == argc) return false;
            *field = argv[++i];
            if (field->empty()) return false;
        }
    }
    if (!mode || !token || !preflight || options.token == 0 ||
            options.token > std::numeric_limits<AccessTokenID>::max()) return false;
    if (options.list_nodes)
        return options.local_name.empty() && options.remote_listen_name.empty() && options.peer_network_id.empty() &&
                options.expected_initiator_name.empty() && options.expected_peer_network_id.empty();
    if (!AdmitTokenEntry({TokenEntryMode::Record, true, preflight, options.token})) return false;
    if (!ValidName(options.local_name) || !ValidNetwork(options.expected_peer_network_id)) return false;
    if (options.server)
        return ValidName(options.expected_initiator_name) && options.remote_listen_name.empty() && options.peer_network_id.empty();
    return options.expected_initiator_name.empty() && ValidName(options.remote_listen_name) &&
            ValidNetwork(options.peer_network_id) && options.peer_network_id == options.expected_peer_network_id;
}

OwnedTokenObservation VerifyOwnedToken(std::uint64_t token, const char* phase)
{
    const auto id = static_cast<AccessTokenID>(token);
    const auto lookup = AccessTokenKit::GetNativeTokenId(process_name);
    const auto type = AccessTokenKit::GetTokenTypeFlag(id);
    NativeTokenInfo info{};
    const int info_rc = AccessTokenKit::GetNativeTokenInfo(id, info);
    const std::vector<std::string> names{permission};
    std::vector<int32_t> states;
    const int query_rc = AccessTokenKit::VerifyAccessToken(id, names, states, true);
    const bool identity = lookup == id && type == TOKEN_NATIVE && info_rc == 0 &&
        info.processName == process_name && info.apl == APL_NORMAL;
    const bool granted = query_rc == 0 && states.size() == 1 && states[0] == PERMISSION_GRANTED;
    std::cout << "stage=identity phase=" << phase << " lookup_match=" << (lookup == id)
        << " type=" << static_cast<int>(type) << " info_rc=" << info_rc << " identity_match=" << identity
        << " query_rc=" << query_rc << " state_count=" << states.size() << " granted=" << granted << '\n';
    return {lookup, static_cast<int>(type), static_cast<int>(TOKEN_NATIVE), info_rc,
        std::move(info.processName), static_cast<int>(info.apl), static_cast<int>(APL_NORMAL),
        query_rc, std::move(states), static_cast<int>(PERMISSION_GRANTED)};
}

TaskCheckpoint ObserveMainTask()
{
    TaskCheckpoint observed;
    observed.process_id = static_cast<std::uint64_t>(getpid());
    const auto current_tid = syscall(SYS_gettid);
    if (current_tid > 0) observed.current_tid = static_cast<std::uint64_t>(current_tid);
    DIR* tasks = opendir("/proc/self/task");
    observed.opened = tasks != nullptr;
    if (tasks)
    {
        bool valid = true;
        for (;;)
        {
            errno = 0;
            const auto* entry = readdir(tasks);
            if (!entry) { observed.complete = valid && errno == 0; break; }
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
            const auto* end = entry->d_name + std::strlen(entry->d_name);
            std::uint64_t tid = 0;
            const auto parsed = std::from_chars(entry->d_name, end, tid);
            if (parsed.ec != std::errc{} || parsed.ptr != end || tid == 0) valid = false;
            if (observed.count == 0) observed.sole_tid = tid;
            // Saturation at 2 cannot turn a multi-task checkpoint into one;
            // enumeration still runs to EOF and checks every entry and close.
            if (observed.count < 2) ++observed.count;
        }
        if (closedir(tasks) != 0) observed.complete = false;
    }
    std::cout << "stage=local_thread_checkpoint pid=" << observed.process_id
        << " tid=" << observed.current_tid << " opened=" << observed.opened << " complete=" << observed.complete
        << " task_count_capped_at_2=" << observed.count << " sole_tid=" << observed.sole_tid
        << " single_main=" << IsSingleMainTask(observed)
        << " scope=LOCAL_CHECKPOINT_ONLY historical_ipc=NOT_PROVEN future_races=NOT_EXCLUDED\n";
    return observed;
}

bool ListOnlineNodes()
{
    // Deliberately separate from Runtime/LoadProvider: this mode cannot create
    // or bind a Socket. The provider stays loaded until process exit.
    void* provider = dlopen(provider_path, RTLD_NOW | RTLD_LOCAL);
    if (!provider) throw std::runtime_error("route_provider_load_failed");
    const auto get_local = reinterpret_cast<decltype(&GetLocalNodeDeviceInfo)>(dlsym(provider,"GetLocalNodeDeviceInfo"));
    const auto get_all = reinterpret_cast<decltype(&GetAllNodeDeviceInfo)>(dlsym(provider,"GetAllNodeDeviceInfo"));
    const auto free_nodes = reinterpret_cast<decltype(&FreeNodeInfo)>(dlsym(provider,"FreeNodeInfo"));
    if (!get_local || !get_all || !free_nodes) throw std::runtime_error("route_provider_symbols_missing");
    const auto started = Clock::now();
    NodeBasicInfo local{};
    std::cout << "stage=GetLocalNodeDeviceInfo action=START\n";
    const int local_rc = get_local(package_name,&local);
    std::cout << "stage=GetLocalNodeDeviceInfo action=RETURN rc=" << local_rc << " elapsed_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count() << '\n';
    std::string local_id;
    if (local_rc != 0 || !CopyRouteNetworkId(local.networkId,sizeof(local.networkId),local_id))
    { std::cout << "stage=route_validation result=FAIL reason=invalid_local_query\n"; return false; }
    NodeBasicInfo* returned = nullptr;
    int count = -1;
    bool freed = false;
    const auto release = [free_nodes,&freed](NodeBasicInfo* nodes) noexcept {
        free_nodes(nodes); freed = true;
        try { std::cout << "stage=FreeNodeInfo action=RETURN\n"; } catch (...) {}
    };
    const auto query_started=Clock::now();
    std::cout << "stage=GetAllNodeDeviceInfo action=START\n";
    const int all_rc=get_all(package_name,&returned,&count);
    // Adopt immediately: validation/output exceptions cannot lose this buffer.
    std::unique_ptr<NodeBasicInfo,decltype(release)> owned(returned,release);
    std::cout << "stage=GetAllNodeDeviceInfo action=RETURN rc=" << all_rc << " count=" << count
        << " data_present=" << (returned!=nullptr) << " elapsed_ms="
        << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-query_started).count() << '\n';
    if(all_rc!=0 || !ValidateRouteNodeCount(count,returned!=nullptr))
    { std::cout << "stage=route_validation result=FAIL reason=invalid_remote_query\n"; return false; }
    std::vector<std::string> remote_ids;
    remote_ids.reserve(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i)
        if(!AppendRouteNetworkId(local_id,returned[i].networkId,sizeof(returned[i].networkId),remote_ids))
        { std::cout << "stage=route_validation result=FAIL reason=invalid_or_duplicate_remote\n"; return false; }
    const bool had_buffer=returned!=nullptr;
    owned.reset(); // Complete release before any successful route summary.
    if(had_buffer && !freed) return false;
    // Full network IDs are necessary route configuration, not stable identity
    // or authentication evidence. Caller stores this output in private run logs.
    std::cout << "route_kind=local network_id=" << local_id << '\n';
    for(std::size_t i=0;i<remote_ids.size();++i)
        std::cout << "route_kind=remote index=" << i << " network_id=" << remote_ids[i] << '\n';
    std::cout << "stage=route_summary query_valid=true remote_count=" << remote_ids.size()
        << " route_available=" << !remote_ids.empty() << " free_returned=" << freed
        << " allocation_present=" << had_buffer << " node_observation_cap=32 communication=NOT_RUN\n";
    return true;
}

struct Runtime {
    Options options;
    SoftbusRecordContract contract;
    Clock::time_point start = Clock::now();
    Clock::time_point deadline = start + run_budget;
    std::atomic<unsigned> active_callbacks{0};
    std::atomic<bool> callback_exception{false};
    void* provider = nullptr;
    decltype(&Socket) socket = nullptr;
    decltype(&Listen) listen = nullptr;
    decltype(&Bind) bind = nullptr;
    decltype(&SendBytes) send = nullptr;
    decltype(&Shutdown) shutdown = nullptr;
    ISocketListener listener{};
    std::size_t shutdown_returned = 0;
    bool sdk_threshold_failed = false;
    explicit Runtime(Options value)
        : options(std::move(value)), contract(options.server,
              options.server ? options.expected_initiator_name : options.remote_listen_name,
              options.expected_peer_network_id, DATA_TYPE_BYTES) {}
};

// Intentionally process-lifetime storage, not a leaked worker thread. The SDK
// does not expose a verified callback-drain API. No callback touches stack state,
// and the provider/state remain alive beyond the finite quiet observation.
Runtime* runtime = nullptr;

template<class Action> void Callback(Action action) noexcept
{
    auto* value = runtime;
    if (!value) return;
    ++value->active_callbacks;
    try { action(*value); }
    catch (...) { value->callback_exception.store(true); }
    --value->active_callbacks;
}
void Bound(int socket, PeerSocketInfo peer) noexcept
{
    Callback([&](Runtime& value) {
        if (!value.contract.BeginBind(socket)) return;
        try
        {
            auto copied=CopyRecordPeer(peer.name,peer.networkId,peer.pkgName,peer.dataType);
            value.contract.CompleteBind(socket,std::move(copied));
        }
        catch (...)
        {
            // Accepted ownership was already captured, without an allocation.
            // No SDK call occurs here; the main thread takes cleanup ownership.
            value.contract.FailBoundCopy(socket);
            value.callback_exception.store(true);
        }
    });
}
void Bytes(int socket, const void* data, std::uint32_t size) noexcept
{ Callback([&](Runtime& value) { value.contract.OnBytes(socket, data, size); }); }
void Closed(int socket, ShutdownReason reason) noexcept
{ Callback([&](Runtime& value) { value.contract.OnShutdown(socket, reason); }); }
void Error(int socket, std::int32_t code) noexcept
{ Callback([&](Runtime& value) { value.contract.OnError(socket, code); }); }
void UnexpectedMessage(int socket, const void*, std::uint32_t) noexcept
{ Callback([&](Runtime& value) { value.contract.OnError(socket, -1); }); }

long long At(Runtime& value, Clock::time_point at = Clock::now())
{ return std::chrono::duration_cast<std::chrono::milliseconds>(at - value.start).count(); }
void LogEvent(Runtime& value, const RecordEvent& event)
{
    const char* kind = event.kind == RecordEventKind::Bound ? "OnBind" : event.kind == RecordEventKind::Bytes ?
        "OnBytes" : event.kind == RecordEventKind::Shutdown ? "OnShutdown" : "OnError";
    std::cout << "event=" << kind << " at_ms=" << At(value,event.at) << " socket=" << event.socket
        << " code=" << event.code << " before_bind_return=" << event.before_bind_return;
    if (event.kind == RecordEventKind::Bound)
        std::cout << " name_bytes=" << event.peer.name.size() << " network_bytes=" << event.peer.network_id.size()
            << " package_bytes=" << event.peer.package.size() << " data_type=" << event.peer.data_type
            << " peer_match=" << event.peer_match << " package_auth=NOT_USED";
    if (event.kind == RecordEventKind::Bytes)
        std::cout << " bytes=" << event.bytes.size() << " fnv1a=" << RecordHash(event.bytes);
    std::cout << '\n';
}

void LoadProvider(Runtime& value)
{
    value.provider = dlopen(provider_path, RTLD_NOW | RTLD_LOCAL);
    if (!value.provider) throw std::runtime_error("provider_load_failed");
    value.socket = reinterpret_cast<decltype(&Socket)>(dlsym(value.provider,"Socket"));
    value.listen = reinterpret_cast<decltype(&Listen)>(dlsym(value.provider,"Listen"));
    value.bind = reinterpret_cast<decltype(&Bind)>(dlsym(value.provider,"Bind"));
    value.send = reinterpret_cast<decltype(&SendBytes)>(dlsym(value.provider,"SendBytes"));
    value.shutdown = reinterpret_cast<decltype(&Shutdown)>(dlsym(value.provider,"Shutdown"));
    if (!value.socket || !value.listen || !value.bind || !value.send || !value.shutdown)
        throw std::runtime_error("provider_symbols_missing");
    // Entire current SDK structure initialized, including newer optional fields.
    value.listener = ISocketListener{};
    value.listener.OnBind = Bound; value.listener.OnBytes = Bytes;
    value.listener.OnShutdown = Closed; value.listener.OnError = Error;
    value.listener.OnMessage = UnexpectedMessage;
}
bool SendRecord(Runtime& value, int socket, const std::vector<std::uint8_t>& bytes, std::size_t index)
{
    if (Clock::now() >= value.deadline || !value.contract.Snapshot().ready) return false;
    const auto started = Clock::now();
    std::cout << "stage=SendBytes action=START at_ms=" << At(value,started) << " socket=" << socket
        << " index=" << index << " bytes=" << bytes.size() << " fnv1a=" << RecordHash(bytes) << '\n';
    const auto rc = value.send(socket, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    const auto ended = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(ended-started).count();
    const bool threshold = elapsed <= 1000;
    std::cout << "stage=SendBytes action=RETURN at_ms=" << At(value,ended) << " rc=" << rc << " elapsed_ms=" << elapsed
        << " observation_threshold_pass=" << threshold << " submitted_result=RETURNED delivery=SEPARATE\n";
    value.sdk_threshold_failed |= !threshold;
    return rc == 0 && threshold && ended < value.deadline;
}

bool RunRecords(Runtime& value)
{
    LoadProvider(value);
    char package[] = "com.kaihong.mdds";
    SocketInfo info{};
    info.name = value.options.local_name.data(); info.pkgName = package; info.dataType = DATA_TYPE_BYTES;
    if (!value.options.server)
    { info.peerName = value.options.remote_listen_name.data(); info.peerNetworkId = value.options.peer_network_id.data(); }
    auto started = Clock::now();
    std::cout << "stage=Socket action=START at_ms=" << At(value,started) << '\n';
    const int created = value.socket(info);
    std::cout << "stage=Socket action=RETURN at_ms=" << At(value) << " rc=" << created
        << " elapsed_ms=" << At(value)-At(value,started) << '\n';
    if (!value.contract.RegisterCreated(created)) return false;
    if (Clock::now() >= value.deadline) { value.contract.Fail("deadline_after_socket"); return false; }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(value.deadline-Clock::now()).count();
    const QosTV qos[] = {{QOS_TYPE_MIN_BW,80}, {QOS_TYPE_MAX_WAIT_TIMEOUT,static_cast<int>(remaining < 4000 ? remaining : 4000)}};
    started = Clock::now();
    std::cout << "stage=" << (value.options.server?"Listen":"Bind") << " action=START at_ms=" << At(value,started)
        << " socket=" << created << " registered_before_call=true wait_ms=" << qos[1].value << '\n';
    const int rc = value.options.server ? value.listen(created,qos,2,&value.listener) : value.bind(created,qos,2,&value.listener);
    std::cout << "stage=" << (value.options.server?"Listen":"Bind") << " action=RETURN at_ms=" << At(value)
        << " rc=" << rc << " elapsed_ms=" << At(value)-At(value,started) << '\n';
    if ((!value.options.server && !value.contract.BindReturned(created,rc)) || rc != 0)
    { value.contract.Fail("bind_or_listen_failed"); return false; }
    if (!value.options.server)
        for (std::size_t i=0;i<record_probe_lengths.size();++i)
        {
            const auto bytes=MakeProbeRecord(i);
            if (!SendRecord(value,created,bytes,i)) { value.contract.Fail("client_send_failed"); return false; }
        }
    std::size_t received=0;
    bool peer_shutdown=false;
    while (Clock::now() < value.deadline)
    {
        if (value.callback_exception.load()) value.contract.Fail("callback_exception");
        RecordEvent event;
        const auto poll = std::min(value.deadline,Clock::now()+std::chrono::milliseconds(20));
        if (value.contract.WaitPop(event,poll))
        {
            LogEvent(value,event);
            if(event.kind==RecordEventKind::Bytes)
            {
                const bool valid=ValidateProbeRecord(received,event.bytes);
                std::cout << "stage=validate_record index=" << received << " all_bytes_match=" << valid << '\n';
                if(!valid) { value.contract.Fail("record_boundary_or_payload_mismatch"); break; }
                if(value.options.server && !SendRecord(value,event.socket,event.bytes,received))
                { value.contract.Fail("echo_send_failed"); break; }
                ++received;
            }
            if(event.kind==RecordEventKind::Shutdown)
            {
                peer_shutdown=event.socket==value.contract.Snapshot().connection;
                if(received!=record_probe_lengths.size()) value.contract.Fail("shutdown_before_all_records");
                break;
            }
        }
        if(value.contract.Snapshot().failed) break;
        if(!value.options.server && received==record_probe_lengths.size()) break;
    }
    const bool complete=received==record_probe_lengths.size() && (!value.options.server || peer_shutdown);
    if(!complete && !value.contract.Snapshot().failed) value.contract.Fail("absolute_run_deadline");
    std::cout << "stage=record_summary received=" << received << " expected=" << record_probe_lengths.size()
        << " peer_shutdown_observed=" << peer_shutdown << " complete=" << complete << '\n';
    return complete && !value.contract.Snapshot().failed;
}

bool CloseOwned(Runtime& value) noexcept
{
    bool okay=true;
    const auto started=Clock::now();
    const auto observe_until=started+quiet_observation;
    try
    {
        do
        {
            for(int socket : value.contract.ClaimShutdownTargets())
            {
                const auto before=Clock::now();
                std::cout << "stage=Shutdown action=START at_ms=" << At(value,before) << " socket=" << socket << '\n';
                if(!value.shutdown) { okay=false; continue; }
                value.shutdown(socket); ++value.shutdown_returned;
                std::cout << "stage=Shutdown action=RETURN at_ms=" << At(value) << " socket=" << socket
                    << " elapsed_ms=" << At(value)-At(value,before) << " quiescence=NOT_VERIFIED\n";
            }
            RecordEvent event;
            while(value.contract.WaitPop(event,Clock::now())) LogEvent(value,event);
            if(Clock::now()<observe_until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while(Clock::now()<observe_until);
        const auto state=value.contract.Snapshot();
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count();
        okay=okay && !state.fatal && state.untracked_count==0 && elapsed<=3000 &&
            state.shutdown_claimed==state.owned && value.shutdown_returned==state.owned;
        std::cout << "stage=close_summary elapsed_ms=" << elapsed << " owned=" << state.owned
            << " shutdown_claimed=" << state.shutdown_claimed << " shutdown_returned=" << value.shutdown_returned
            << " active_callbacks=" << value.active_callbacks.load() << " bind_callbacks=" << state.bind_callbacks
            << " byte_callbacks=" << state.byte_callbacks << " shutdown_callbacks=" << state.shutdown_callbacks
            << " callbacks_after_close=" << state.late_callbacks << " discarded_records=" << state.discarded_records
            << " early_bytes=" << state.before_bind_return << " failure=" << state.failure
            << " bound_copy_failures=" << state.bound_copy_failures << " fatal=" << state.fatal
            << " untracked_first_id=" << state.untracked_first_id << " untracked_count=" << state.untracked_count
            << " untracked_count_kind=CALLBACK_OBSERVATIONS cleanup="
            << (state.fatal || state.untracked_count!=0 ? "UNCONFIRMED" : "OBSERVED_KNOWN_HANDLES_ONLY")
            << " quiet_observation_ms=" << quiet_observation.count() << " quiescence=NOT_VERIFIED\n";
        // Extra, duplicated or discarded records during the entire observed
        // callback window cannot be hidden by stopping the main loop at five.
        const bool exact_records=state.byte_callbacks==record_probe_lengths.size() && state.discarded_records==0;
        std::cout << "stage=observed_count exact_five_callbacks=" << exact_records << '\n';
        okay=okay && exact_records && !state.failed && !value.callback_exception.load() && !value.sdk_threshold_failed;
    }
    catch(...) { okay=false; }
    return okay;
}

class ProbeTokenOperations final : public TokenEntryOperations {
public:
    explicit ProbeTokenOperations(Options& options) noexcept : options_(options), query_only_(options.list_nodes) {}
    TaskCheckpoint ObserveTasks() override { return ObserveMainTask(); }
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    int SetSelf(std::uint64_t token) override { return SetSelfTokenID(token); }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override
    { return VerifyOwnedToken(token, phase); }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int code) override
    {
        std::cout << "stage=" << stage << " pid=" << getpid() << " tid=" << syscall(SYS_gettid);
        if (std::strcmp(stage, "original_token") == 0)
            std::cout << " original=" << first << " selected=" << second << " nonzero=" << (first != 0)
                << " original_already_target=" << (first == second)
                << " transition=" << (first == second ? "NONE_ALREADY_TARGET" : "ATTEMPT_PENDING");
        else if (std::strcmp(stage, "select_token") == 0 || std::strcmp(stage, "restore_token") == 0)
            std::cout << " expected=" << first << " readback=" << second << " rc=" << code
                << " readback_match=" << (first == second) << " scope=CURRENT_MAIN_THREAD_ONLY";
        else std::cout << " result=FAIL code=" << code;
        std::cout << '\n';
    }
    bool EnterProvider() override
    {
        std::cout << "stage=identity_setup_complete elapsed_ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-identity_started_).count()
            << " record_budget_ms=7000 record_budget_start=RUNTIME_CREATION"
            << " ordering=" << (query_only_ ? "LEGACY_MANAGER_BEFORE_SELECT" : "TOKEN_BEFORE_FIRST_EXPLICIT_MANAGER_OR_PROVIDER")
            << " historical_ipc=NOT_PROVEN sdk_thread_tokens=NOT_MEASURED\n";
        if (query_only_) return ListOnlineNodes();
        runtime = new Runtime(std::move(options_));
        return RunRecords(*runtime);
    }
    bool CloseProvider() override { return runtime ? CloseOwned(*runtime) : true; }
private:
    Options& options_;
    const bool query_only_;
    const Clock::time_point identity_started_ = Clock::now();
};
}

int main(int argc,char** argv)
{
    std::cout << std::unitbuf;
    Options options;
    if(!ParseOptions(argc,argv,options)) { Usage(); return 64; }
    std::cout << "scope=" << (options.list_nodes?"READONLY_ROUTE_QUERY":"SOCKET_RECORD_EXPERIMENT")
        << " role=" << (options.list_nodes?"list-nodes":options.server?"server":"client")
        << " processName=" << process_name << " package=" << package_name << " uid=" << getuid() << " pid=" << getpid()
        << " writes=NONE expected_token=" << options.token
        << " external_ownership_preflight=OPERATOR_ASSERTED internally_verified_preflight=false\n";
    const bool query_only=options.list_nodes;
    ProbeTokenOperations operations(options);
    TokenFirstEntry token(operations);
    bool success = token.Run({query_only ? TokenEntryMode::ListNodes : TokenEntryMode::Record, true, true, options.token});
    const bool restored=token.RestoreSucceeded();
    // A terminal callback during token restoration must not be hidden by an
    // earlier close summary. This remains a finite observation, not quiescence.
    const auto final_state=runtime ? runtime->contract.Snapshot() : RecordSnapshot{};
    success=success && restored && !final_state.fatal && !final_state.failed;
    std::cout << "result=" << (success?"PASS":"FAIL") << " scope="
        << (query_only?"READONLY_ROUTE_QUERY":"SOCKET_RECORD_OBSERVATION")
        << " fatal=" << final_state.fatal << " untracked_first_id=" << final_state.untracked_first_id
        << " untracked_count=" << final_state.untracked_count
        << " cleanup=" << (final_state.fatal || final_state.untracked_count!=0 ? "UNCONFIRMED" :
                           query_only ? "NO_SOCKET_RESOURCES" : "OBSERVED_KNOWN_HANDLES_ONLY")
        << " token_restored=" << restored << " quiescence=NOT_VERIFIED dds=NOT_RUN transport=NOT_IMPLEMENTED\n";
    return success?0:6;
}
