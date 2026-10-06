// Isolated P0 negative option-stage diagnostic, never a production workaround.
// Uses public platform Socket/inner_socket interfaces. No Bind, Listen, Session
// API, private unlock, or worker detachment is allowed in this experiment.
#include "softbus_options_contract.hpp"
#include "token_first_entry.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <inner_socket.h>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
using namespace fastdds_dsoftbus_probe;
using Clock = std::chrono::steady_clock;
constexpr char process_name[] = "fastdds_dsoftbus_probe";
constexpr char permission_name[] = "ohos.permission.DISTRIBUTED_DATASYNC";
constexpr char package_name[] = "com.kaihong.mdds";
static_assert(sizeof(bool) == 1, "the inspected target option ABI uses one-byte bool");
static_assert(OPT_LEVEL_SOFTBUS == 1 && OPT_TYPE_SUPPORT_ACK == 3 && OPT_TYPE_NEED_ACK == 4,
        "inspected target option values must remain explicit");
std::uint64_t Tid() noexcept { return static_cast<std::uint64_t>(syscall(SYS_gettid)); }

class OptionsRuntime {
public:
    explicit OptionsRuntime(std::uint64_t value) : token(value) {
        const auto tick = std::chrono::duration_cast<std::chrono::microseconds>(start.time_since_epoch()).count();
        run_id = "opt-" + std::to_string(getpid()) + "-" + std::to_string(tick);
        name = "com.kaihong.mdds." + run_id;
    }
    std::int64_t Now() const noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    }
    void Log(const char* event, unsigned invocation, int rc = 0) const noexcept {
        std::fprintf(stdout, "event=%s run_id=%s pid=%ld tid=%llu invocation_id=%s:%ld:%u steady_us=%lld socket=%d rc=%d clock_scope=LOCAL_ONLY\n",
                event, run_id.c_str(), static_cast<long>(getpid()), static_cast<unsigned long long>(Tid()),
                run_id.c_str(), static_cast<long>(getpid()), invocation, static_cast<long long>(Now()), socket, rc);
    }
    bool Run() {
        evidence.admission.main_tid = Tid();
        provider = dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so", RTLD_NOW | RTLD_LOCAL);
        if (!provider) throw std::runtime_error("provider_load_failed");
        create = reinterpret_cast<decltype(&Socket)>(dlsym(provider, "Socket"));
        get = reinterpret_cast<decltype(&GetSocketOpt)>(dlsym(provider, "GetSocketOpt"));
        set = reinterpret_cast<decltype(&SetSocketOpt)>(dlsym(provider, "SetSocketOpt"));
        shutdown = reinterpret_cast<decltype(&Shutdown)>(dlsym(provider, "Shutdown"));
        if (!create || !get || !set || !shutdown) throw std::runtime_error("provider_symbols_missing");
        SocketInfo info{}; info.name = const_cast<char*>(name.c_str());
        info.pkgName = const_cast<char*>(package_name); info.dataType = DATA_TYPE_BYTES;
        Log("Socket_enter", 0); socket = create(info); Log("Socket_return", 0, socket);
        if (socket <= 0) { evidence.failure = true; return false; }
        evidence.admission.initial = Query("initial", 1, nullptr);
        if (!OptionsKnownUnsupported(evidence.admission.initial)) {
            const auto& initial = evidence.admission.initial;
            const bool supported = initial.returned && initial.sdk_rc == 0 && initial.reported_size == sizeof(bool) && initial.raw_value == 1;
            Log(supported ? "NOT_TRIGGERED_SUPPORT_ACK_TRUE" : "initial_query_invalid_or_failed", 1, initial.sdk_rc);
            evidence.failure = !supported;
            return supported;
        }
        if (!WorkerQuery("baseline", 2, baseline, evidence.admission.baseline, evidence.admission.baseline_joined_us)) return false;
        if (!OptionsMaySetNeedAck(evidence.admission)) {
            Log("negative_option_trigger_not_admitted", 2); evidence.failure = true; return false;
        }
        bool requested = true;
        ++evidence.admission.set_attempts;
        Log("Set_NEED_ACK_true_enter", 3);
        evidence.set_entered_us = Now();
        evidence.set_sdk_rc = set(socket, OPT_LEVEL_SOFTBUS, OPT_TYPE_NEED_ACK, &requested, sizeof(requested));
        evidence.set_returned_us = Now();
        Log("Set_NEED_ACK_true_return", 3, evidence.set_sdk_rc);
        std::fprintf(stdout, "event=option_result run_id=%s pid=%ld invocation_id=%s:%ld:3 requested_NEED_ACK=1 sdk_rc=%d entered_us=%lld returned_us=%lld effective_NEED_ACK=NOT_QUERIED support_query_is_not_effective_NEED_ACK=1\n",
                run_id.c_str(), static_cast<long>(getpid()), run_id.c_str(), static_cast<long>(getpid()), evidence.set_sdk_rc,
                static_cast<long long>(evidence.set_entered_us), static_cast<long long>(evidence.set_returned_us));
        evidence.same_thread = Query("same_thread_after_set", 4, nullptr);
        // A same-thread return does not prove a recursive mutex was released.
        // Keep a separate actual thread for the discriminating second query.
        return WorkerQuery("cross_thread_after_set", 5, after, evidence.cross_thread, evidence.cross_thread_joined_us);
    }
    bool Close() {
        // Any live worker must remain owned and joined. If it cannot exit, the
        // external ten-second safety watchdog ends a FAIL run; that is not normal
        // cancellation, resource settlement, token restoration, or recovery.
        if (worker.joinable()) { Log("cleanup_join_wait_enter", 6); worker.join(); Log("cleanup_join_return", 6); }
        if (socket > 0 && shutdown) {
            Log("Shutdown_enter", 7); evidence.shutdown_entered_us = Now();
            shutdown(socket);
            evidence.shutdown_returned_us = Now(); Log("Shutdown_return", 7);
        }
        return !evidence.failure;
    }
    void Summary(bool restored, bool entry_success) {
        if (!restored || !entry_success) evidence.failure = true;
        const auto verdict = EvaluateOptionsCompletion(evidence);
        std::fprintf(stdout, "result=%s run_id=%s pid=%ld set_attempts=%u set_sdk_rc=%d set_entered_us=%lld set_returned_us=%lld baseline_joined_us=%lld cross_thread_joined_us=%lld shutdown_entered_us=%lld shutdown_returned_us=%lld worker_joinable=%d token_restored=%d api_family=PUBLIC_PLATFORM_INNER_KIT application_NDK_contract=NOT_CLAIMED native_resource_settlement=NOT_PROVEN production_qualification=BLOCKED\n",
                OptionsVerdictName(verdict), run_id.c_str(), static_cast<long>(getpid()), evidence.admission.set_attempts,
                evidence.set_sdk_rc, static_cast<long long>(evidence.set_entered_us), static_cast<long long>(evidence.set_returned_us),
                static_cast<long long>(evidence.admission.baseline_joined_us), static_cast<long long>(evidence.cross_thread_joined_us),
                static_cast<long long>(evidence.shutdown_entered_us), static_cast<long long>(evidence.shutdown_returned_us),
                worker.joinable(), restored);
        exit_code = verdict == OptionsVerdict::Failed ? 6 : verdict == OptionsVerdict::NotTriggered ? 2 : 0;
    }
    int exit_code = 6;
private:
    struct WorkerState {
        std::atomic<std::int64_t> entered_us{-1};
        std::atomic<std::int64_t> returned_us{-1};
        std::atomic<std::uint64_t> tid{0};
        std::atomic<bool> done{false};
        OptionsQueryObservation result;
    };
    OptionsQueryObservation Query(const char* phase, unsigned invocation, WorkerState* state) {
        OptionsQueryObservation result; result.tid = Tid();
        // Inspect raw object representation, never read an invalid bool written
        // by a mismatched library. Correctly sized storage is passed to the API.
        unsigned char raw = 0xa5; std::int32_t size = sizeof(bool);
        Log("Get_SUPPORT_ACK_enter", invocation);
        result.entered_us = Now();
        if (state) { state->tid = result.tid; state->entered_us = result.entered_us; }
        result.sdk_rc = get(socket, OPT_LEVEL_SOFTBUS, OPT_TYPE_SUPPORT_ACK, &raw, &size);
        result.returned_us = Now();
        if (state) state->returned_us = result.returned_us;
        result.returned = true; result.reported_size = size; result.raw_value = raw;
        std::fprintf(stdout, "event=Get_SUPPORT_ACK_return run_id=%s phase=%s pid=%ld tid=%llu invocation_id=%s:%ld:%u sdk_rc=%d bool_size=%zu reported_size=%d raw_value=%u entered_us=%lld returned_us=%lld clock_scope=LOCAL_ONLY boundary=EXTERNAL_MARKER\n",
                run_id.c_str(), phase, static_cast<long>(getpid()), static_cast<unsigned long long>(result.tid),
                run_id.c_str(), static_cast<long>(getpid()), invocation, result.sdk_rc, sizeof(bool), result.reported_size,
                raw, static_cast<long long>(result.entered_us), static_cast<long long>(result.returned_us));
        return result;
    }
    bool WorkerQuery(const char* phase, unsigned invocation, WorkerState& state,
            OptionsQueryObservation& output, std::int64_t& joined) {
        const auto launched = Now();
        worker = std::thread([this, &state, phase, invocation] {
            try {
                const auto actual = GetSelfTokenID();
                Log("worker_token_observation", invocation, actual == token ? 0 : -1);
                if (actual != token || actual == 0) {
                    Log("worker_token_mismatch", invocation, -1);
                } else state.result = Query(phase, invocation, &state);
            } catch (...) { Log("worker_exception", invocation, -1); }
            state.done = true;
        });
        while (!state.done.load()) {
            const auto entered = state.entered_us.load();
            if (Now() - (entered >= 0 ? entered : launched) > options_worker_budget_us) {
                evidence.failure = true;
                const auto returned = state.returned_us.load(), observed = Now();
                std::fprintf(stdout, "result=FAIL event=worker_deadline run_id=%s phase=%s pid=%ld invocation_id=%s:%ld:%u worker_tid=%llu entered_us=%lld sdk_returned_us=%lld observed_us=%lld native_worker_active=%d worker_completion_not_observed=1 deadline_us=1500000 cleanup=JOIN_REQUIRED external_watchdog=SAFETY_ONLY watchdog_termination_is_normal_cleanup=0 call_boundary=EXTERNAL_MARKER\n",
                        run_id.c_str(), phase, static_cast<long>(getpid()), run_id.c_str(), static_cast<long>(getpid()), invocation,
                        static_cast<unsigned long long>(state.tid.load()), static_cast<long long>(entered), static_cast<long long>(returned),
                        static_cast<long long>(observed), OptionsNativeCallActive(entered, returned, observed));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Deliberately remains blocking when the native operation is stuck.
        // There is no detach, process-exit success, or foreign mutex unlock.
        Log("worker_join_enter", invocation); worker.join(); joined = Now(); Log("worker_join_return", invocation);
        output = state.result;
        if (!OptionsWorkerReturnedWithinBudget(output) || joined - output.entered_us > options_worker_budget_us)
            evidence.failure = true;
        return !evidence.failure;
    }
    const std::uint64_t token;
    Clock::time_point start = Clock::now();
    std::string run_id, name;
    void* provider = nullptr; // Process-lifetime; no unproven dlclose/quiescence.
    decltype(&Socket) create = nullptr;
    decltype(&GetSocketOpt) get = nullptr;
    decltype(&SetSocketOpt) set = nullptr;
    decltype(&Shutdown) shutdown = nullptr;
    int socket = 0;
    std::thread worker;
    WorkerState baseline, after;
    OptionsCompletion evidence;
};

class OptionsTokenOperations final : public TokenEntryOperations {
public:
    explicit OptionsTokenOperations(std::uint64_t value) : token(value) {}
    TaskCheckpoint ObserveTasks() override {
        TaskCheckpoint result; result.process_id = static_cast<std::uint64_t>(getpid()); result.current_tid = Tid();
        DIR* directory = opendir("/proc/self/task"); result.opened = directory != nullptr;
        if (directory) {
            bool valid = true;
            for (;;) {
                errno = 0; const auto* item = readdir(directory);
                if (!item) { result.complete = valid && errno == 0; break; }
                if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0) continue;
                std::uint64_t tid = 0; const char* end = item->d_name + std::strlen(item->d_name);
                const auto parsed = std::from_chars(item->d_name, end, tid);
                if (parsed.ec != std::errc{} || parsed.ptr != end || tid == 0) valid = false;
                if (result.count == 0) result.sole_tid = tid;
                if (result.count < 2) ++result.count;
            }
            if (closedir(directory) != 0) result.complete = false;
        }
        std::printf("stage=local_thread_checkpoint count=%zu single_main=%d\n", result.count, IsSingleMainTask(result));
        return result;
    }
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    int SetSelf(std::uint64_t selected) override { return SetSelfTokenID(selected); }
    OwnedTokenObservation VerifyOwned(std::uint64_t selected, const char* phase) override {
        using namespace OHOS::Security::AccessToken;
        const auto id = static_cast<AccessTokenID>(selected);
        const auto lookup = AccessTokenKit::GetNativeTokenId(process_name);
        const auto type = AccessTokenKit::GetTokenTypeFlag(id);
        NativeTokenInfo info{}; const int rc = AccessTokenKit::GetNativeTokenInfo(id, info);
        std::vector<std::int32_t> states;
        const int query = AccessTokenKit::VerifyAccessToken(id, std::vector<std::string>{permission_name}, states, true);
        std::printf("stage=identity phase=%s lookup_match=%d type=%d info_rc=%d query_rc=%d state_count=%zu\n",
                phase, lookup == id, static_cast<int>(type), rc, query, states.size());
        return {lookup, static_cast<int>(type), static_cast<int>(TOKEN_NATIVE), rc, std::move(info.processName),
            static_cast<int>(info.apl), static_cast<int>(APL_NORMAL), query, std::move(states), static_cast<int>(PERMISSION_GRANTED)};
    }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int code) override {
        std::printf("stage=%s first=%llu second=%llu code=%d scope=CURRENT_MAIN_THREAD_ONLY\n", stage,
                static_cast<unsigned long long>(first), static_cast<unsigned long long>(second), code);
    }
    bool EnterProvider() override { runtime = new OptionsRuntime(token); return runtime->Run(); }
    bool CloseProvider() override { return runtime ? runtime->Close() : true; }
    const std::uint64_t token;
    OptionsRuntime* runtime = nullptr; // Diagnostic state outlives every SDK call.
};
int Run(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::uint64_t token = 0; bool preflight = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--expected-token") == 0 && token == 0 && i + 1 < argc) {
            if (!ParseTokenArgument(argv[++i], token)) return 64;
        } else if (std::strcmp(argv[i], "--preflight-confirmed") == 0 && !preflight) preflight = true;
        else return 64;
    }
    if (!AdmitTokenEntry({TokenEntryMode::Record, true, preflight, token})) {
        std::fprintf(stderr, "Usage: softbus_options_probe --expected-token OWNED_ID --preflight-confirmed\n"); return 64;
    }
    std::printf("scope=UNSUPPORTED_NEED_ACK_NEGATIVE_DIAGNOSTIC worker_budget_us=1500000 external_watchdog_us=10000000 socket_profile=BYTES socket_stage=CREATED_UNBOUND_UNLISTENED permission_writes=NONE api_family=PUBLIC_PLATFORM_INNER_KIT preflight=OPERATOR_ASSERTED native_resource_settlement=NOT_PROVEN\n");
    OptionsTokenOperations operations(token); TokenFirstEntry entry(operations);
    const bool success = entry.Run({TokenEntryMode::Record, true, true, token});
    if (!operations.runtime) { std::printf("result=FAIL provider_entered=0 token_restored=%d\n", entry.RestoreSucceeded()); return 6; }
    operations.runtime->Summary(entry.RestoreSucceeded(), success); return operations.runtime->exit_code;
}
}
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (...) { std::fprintf(stderr, "result=FAIL reason=uncaught_options_probe_exception\n"); return 6; }
}
