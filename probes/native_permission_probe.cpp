// Opt-in board diagnostic. No token writes occur without an explicit mode and
// external JSON/ownership preflight. This is not a production identity service.
#include "native_permission_flow.hpp"
#include <accesstoken_kit.h>
#include <nativetoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <charconv>
#include <chrono>
#include <dlfcn.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using namespace fastdds_dsoftbus_probe;
using namespace OHOS::Security::AccessToken;

void PermissionUsage()
{
    std::cerr << "Usage: native_permission_probe --inspect (read-only ABI/manager query)\n"
              << "       native_permission_probe (--grant|--socket-check|--revoke|--verify-denied)"
              << " --expected-token <decimal-id> --preflight-confirmed [--confirmed-absent|--recover-unregistered]\n"
              << "First grant requires expected-token 0 and confirmed-absent."
              << " All other operations require the owned nonzero token.\n"
              << "Run NativePermissionGuard and save ownership/backup before invocation.\n"
              << "Grant/revoke write persistent native token JSON: do NOT kill them on timeout.\n";
}

int InspectPermissionAbi()
{
    // This path deliberately does not instantiate NativePermissionPlatform:
    // no persistent writes, token switching, Reload, or DSoftBus calls.
    const uint64_t original = GetSelfTokenID();
    bool valid = false;
    std::cout << "mode=inspect uid=" << getuid() << " euid=" << geteuid()
              << " self=" << original << " writes=NONE\n";
    try
    {
        if (original != 0 && original <= std::numeric_limits<AccessTokenID>::max())
        {
            const auto id = static_cast<AccessTokenID>(original);
            const auto type = AccessTokenKit::GetTokenTypeFlag(id);
            std::cout << "stage=inspect_type type=" << static_cast<int>(type) << '\n';
            if (type == TOKEN_NATIVE || type == TOKEN_SHELL)
            {
                uint32_t version = 0;
                const int version_rc = AccessTokenKit::GetVersion(version);
                std::cout << "stage=inspect_version code=" << version_rc << " version=" << version << '\n';
                NativeTokenInfo info{};
                const int info_rc = AccessTokenKit::GetNativeTokenInfo(id, info);
                // Do not print uncontrolled process-name bytes into structured logs.
                std::cout << "stage=inspect_info code=" << info_rc
                          << " name_bytes=" << (info_rc == 0 ? info.processName.size() : 0) << '\n';
                AccessTokenID lookup = 0;
                if (info_rc == 0 && !info.processName.empty())
                {
                    lookup = AccessTokenKit::GetNativeTokenId(info.processName);
                    std::cout << "stage=inspect_lookup token=" << lookup << '\n';
                }
                const std::vector<std::string> names{native_probe_permission};
                std::vector<int32_t> states;
                const int query_rc = AccessTokenKit::VerifyAccessToken(id, names, states, true);
                const bool known = query_rc == 0 && states.size() == 1 &&
                        (states.front() == PERMISSION_GRANTED || states.front() == PERMISSION_DENIED);
                std::cout << "stage=inspect_permission code=" << query_rc << " states=" << states.size();
                if (known) std::cout << " permission=" << states.front();
                std::cout << '\n';
                valid = version_rc == 0 && info_rc == 0 && !info.processName.empty() && lookup == id && known;
            }
        }
    }
    catch (...)
    {
        std::cout << "stage=inspect_exception result=FAIL\n";
    }
    const uint64_t after = GetSelfTokenID();
    valid = valid && after == original;
    std::cout << "result=" << (valid ? "PASS" : "FAIL") << " scope=READONLY_ABI_QUERY"
              << " self_after=" << after << " self_unchanged=" << (after == original ? "true" : "false")
              << " project_grant=NOT_RUN revoke=NOT_RUN communication=NOT_RUN\n";
    return valid ? 0 : 5;
}

class NativePermissionPlatform final : public NativePermissionApi
{
public:
    NativePermissionPlatform(bool confirmed_absent, bool recover_unregistered)
        : confirmed_absent_(confirmed_absent), recover_unregistered_(recover_unregistered) {}

    TokenLookup LookupProjectToken() override
    {
        const auto id = AccessTokenKit::GetNativeTokenId(native_probe_process_name);
        if (id != 0) return {0, id};
        // GetNativeTokenId alone cannot distinguish absent from IPC failure.
        // The caller must have confirmed first-use absence in the full JSON
        // snapshot and ownership record. This is NOT an RPC-health assertion.
        if (confirmed_absent_ && !write_attempted_)
        {
            std::cout << "stage=lookup absent_source=external_preflight rpc_health=UNKNOWN\n";
            return {0, 0};
        }
        if (recover_unregistered_ && !write_attempted_)
        {
            std::cout << "stage=lookup ownership_source=external_preflight manager_token=UNKNOWN\n";
            return {native_lookup_owned_unregistered, 0};
        }
        return {-2, 0};
    }

    uint64_t GetSelfToken() override { return GetSelfTokenID(); }

    uint64_t WriteProjectToken(bool grant) override
    {
        write_attempted_ = true;
        const char* permissions[] = {native_probe_permission};
        NativeTokenInfoParams info{};
        info.processName = native_probe_process_name;
        info.aplStr = native_probe_apl;
        info.permsNum = grant ? 1 : 0;
        info.perms = grant ? permissions : nullptr;
        std::cout << "stage=persistent_write result=START action=" << (grant ? "grant" : "revoke")
                  << " processName=" << info.processName << " apl=normal permissions=" << info.permsNum << '\n';
        const auto id = GetAccessTokenId(&info);
        std::cout << "stage=persistent_write result=RETURNED token=" << id << '\n';
        return id;
    }

    int SetSelfToken(uint64_t token) override { return SetSelfTokenID(token); }
    int ReloadNativeTokens() override { return AccessTokenKit::ReloadNativeTokenInfo(); }

    bool IsNativeToken(uint64_t token) override
    {
        if (token == 0 || token > std::numeric_limits<AccessTokenID>::max()) return false;
        NativeTokenInfo info{};
        const auto id = static_cast<AccessTokenID>(token);
        const int result = AccessTokenKit::GetNativeTokenInfo(id, info);
        std::cout << "stage=native_info code=" << result << " token=" << token << '\n';
        return result == 0 && info.processName == native_probe_process_name && info.apl == APL_NORMAL &&
               AccessTokenKit::GetTokenTypeFlag(id) == TOKEN_NATIVE;
    }

    int VerifyDatasync(uint64_t token) override
    {
        // The vector overload separates an IPC/query error from DENIED. Force
        // the manager query so a cached/local permission cannot prove reload.
        const std::vector<std::string> names{native_probe_permission};
        std::vector<int32_t> states;
        const int result = AccessTokenKit::VerifyAccessToken(static_cast<AccessTokenID>(token), names, states, true);
        std::cout << "stage=permission_query code=" << result << " states=" << states.size();
        if (result == 0 && states.size() == 1) std::cout << " permission=" << states.front();
        std::cout << '\n';
        if (result != 0 || states.size() != 1) return -2;
        return states.front() == PERMISSION_GRANTED ? 0 : states.front() == PERMISSION_DENIED ? -1 : -2;
    }

    int32_t CreateSocket() override
    {
        // Load DSoftBus only after the flow has verified the selected token.
        // The provider remains loaded until process exit: dlclose is not a
        // substitute for proving SDK-global thread/callback quiescence.
        constexpr const char* path = "/system/lib64/platformsdk/libsoftbus_client.z.so";
        provider_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!provider_) throw std::runtime_error("softbus provider load failed");
        const auto create = reinterpret_cast<decltype(&Socket)>(dlsym(provider_, "Socket"));
        shutdown_ = reinterpret_cast<decltype(&Shutdown)>(dlsym(provider_, "Shutdown"));
        if (!create || !shutdown_) throw std::runtime_error("softbus Socket/Shutdown symbol missing");
        std::string name = "com.kaihong.mdds.probe." + std::to_string(getpid()) + "." +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        char package[] = "com.kaihong.mdds";
        SocketInfo info{};
        info.name = name.data();
        info.pkgName = package;
        info.dataType = DATA_TYPE_BYTES;
        std::cout << "stage=Socket result=START name=" << name << " library=" << path << '\n';
        const auto start = std::chrono::steady_clock::now();
        const auto id = create(info);
        // The pinned allocator starts at 1; SDK ClientShutdown rejects <= 0.
        // The public "errors < 0" comment alone does not establish 0 as usable.
        std::cout << "stage=Socket result=" << (id > 0 ? "PASS" : "FAIL") << " id=" << id << " elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start).count() << '\n';
        return id;
    }

    void ShutdownSocket(int32_t socket) override
    {
        const auto start = std::chrono::steady_clock::now();
        std::cout << "stage=Shutdown result=START id=" << socket << '\n';
        shutdown_(socket);
        std::cout << "stage=Shutdown result=RETURNED elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start).count()
                  << " quiescence=NOT_VERIFIED communication=NOT_RUN\n";
    }
private:
    bool confirmed_absent_ = false;
    bool recover_unregistered_ = false;
    bool write_attempted_ = false;
    void* provider_ = nullptr;
    decltype(&Shutdown) shutdown_ = nullptr;
};
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    if (argc == 2 && std::string(argv[1]) == "--inspect") return InspectPermissionAbi();
    NativePermissionOptions options;
    bool mode_seen = false, token_seen = false, preflight = false, absent = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if ((arg == "--grant" || arg == "--socket-check" || arg == "--revoke" || arg == "--verify-denied") && !mode_seen)
        {
            mode_seen = true;
            options.mode = arg == "--grant" ? NativePermissionMode::Grant :
                    arg == "--revoke" ? NativePermissionMode::Revoke :
                    arg == "--verify-denied" ? NativePermissionMode::VerifyDenied : NativePermissionMode::SocketCheck;
        }
        else if (arg == "--expected-token" && !token_seen && i + 1 < argc)
        {
            token_seen = true;
            const std::string text(argv[++i]);
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), options.expected_token);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) { PermissionUsage(); return 64; }
        }
        else if (arg == "--preflight-confirmed" && !preflight) preflight = true;
        else if (arg == "--confirmed-absent" && !absent) absent = true;
        else if (arg == "--recover-unregistered" && !options.recover_unregistered) options.recover_unregistered = true;
        else { PermissionUsage(); return 64; }
    }
    const bool first_grant = options.mode == NativePermissionMode::Grant && options.expected_token == 0;
    if (!mode_seen || !token_seen || !preflight || absent != first_grant ||
            (!first_grant && options.expected_token == 0) ||
            options.expected_token > std::numeric_limits<AccessTokenID>::max() ||
            (options.recover_unregistered && options.mode != NativePermissionMode::Revoke))
    {
        PermissionUsage();
        return 64;
    }
    std::cout << "processName=" << native_probe_process_name << " expected_token=" << options.expected_token
              << " external_preflight=CONFIRMED persistent_json=NOT_VERIFIED_BY_THIS_PROCESS\n";
    NativePermissionPlatform platform(absent, options.recover_unregistered);
    const auto result = RunNativePermissionProbe(options, platform, std::cout);
    std::cout << "result=" << (result.success ? "PASS" : "FAIL") << " token=" << result.token
              << " restored=" << (result.restored ? "true" : "false")
              << " persistent_json=REQUIRES_EXTERNAL_CHECK communication=NOT_RUN\n";
    return result.exit_code;
}
