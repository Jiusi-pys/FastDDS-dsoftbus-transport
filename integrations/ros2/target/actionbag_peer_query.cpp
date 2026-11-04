// Fixed new-identity public-directory query. No Socket or provisioning calls.
#include "identity/actionbag_token_bridge.h"
#include "identity/token_first_entry.hpp"
#include "softbus_record_contract.hpp"
#include "softbus_probe_profile.hpp"
#include <softbus_bus_center.h>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
using namespace fastdds_dsoftbus_probe;
constexpr char package[] = "com.kaihong.mdds";
constexpr char provider_path[] = "/system/lib64/platformsdk/libsoftbus_client.z.so";

class ActionbagPeerOperations final : public TokenEntryOperations {
public:
    TaskCheckpoint ObserveTasks() override {
        TaskCheckpoint result;
        result.process_id = static_cast<std::uint64_t>(getpid());
        const auto tid = syscall(SYS_gettid);
        if (tid > 0) result.current_tid = static_cast<std::uint64_t>(tid);
        DIR* tasks = opendir("/proc/self/task");
        result.opened = tasks != nullptr;
        if (!tasks) return result;
        bool valid = true;
        for (;;) {
            errno = 0;
            const auto* entry = readdir(tasks);
            if (!entry) { result.complete = valid && errno == 0; break; }
            if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
            std::uint64_t value = 0;
            const auto end = entry->d_name + std::strlen(entry->d_name);
            const auto parsed = std::from_chars(entry->d_name, end, value);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !value) valid = false;
            if (!result.count) result.sole_tid = value;
            if (result.count < 2) ++result.count;
        }
        if (closedir(tasks)) result.complete = false;
        std::printf("PEER_TASKS pid=%llu tid=%llu count=%zu complete=%d\n",
            static_cast<unsigned long long>(result.process_id), static_cast<unsigned long long>(result.current_tid), result.count, int(result.complete));
        return result;
    }
    std::uint64_t GetSelf() override { return MddsActionbagTokenBridgeGetSelfToken(); }
    int SetSelf(std::uint64_t token) override { return MddsActionbagTokenBridgeSetSelfToken(token); }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char*) override {
        MddsTokenBridgeResult raw{};
        const int rc = MddsActionbagTokenBridgeVerify(static_cast<std::uint32_t>(token), &raw);
        std::printf("PEER_IDENTITY bridge=%d called=%u granted=%d\n", rc, raw.called, raw.granted);
        OwnedTokenObservation result;
        if (rc || raw.called != 15) return result;
        result.lookup = raw.lookupToken; result.type = raw.type; result.native_type = raw.nativeType;
        result.info_rc = raw.infoRc; result.process_name = raw.processName; result.apl = raw.apl;
        result.normal_apl = raw.normalApl; result.query_rc = raw.permissionRc;
        result.permission_granted = raw.permissionGranted;
        if (raw.permissionCount == 1) result.states.push_back(raw.permissionState);
        return result;
    }
    void Diagnostic(const char* stage, std::uint64_t expected, std::uint64_t actual, int rc) override {
        std::printf("PEER_TOKEN stage=%s matches=%d rc=%d\n", stage, int(expected == actual), rc);
    }
    bool EnterProvider() override { return false; }
    bool CloseProvider() override { return false; }
};

bool ActionbagSaveIdentity(const std::string& path, const void* bytes, std::size_t size)
{
    if (!ValidIdentityExportPath(path) || !ValidIdentityExportBytes(bytes, size)) return false;
    const auto parent = path.substr(0, path.rfind('/'));
    const int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) return false;
    struct stat owned{};
    if (fstat(directory, &owned) || !S_ISDIR(owned.st_mode) || owned.st_uid != getuid() || (owned.st_mode & 0022)) {
        close(directory); return false;
    }
    const int file = openat(directory, "local-udid.bin", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (file < 0) { close(directory); return false; }
    struct stat created{};
    bool valid = fstat(file, &created) == 0 && S_ISREG(created.st_mode) &&
        (created.st_mode & 0777) == 0600 && created.st_uid == getuid();
    std::size_t offset = 0;
    while (valid && offset < size) {
        const auto count = write(file, static_cast<const unsigned char*>(bytes) + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { valid = false; break; }
        offset += static_cast<std::size_t>(count);
    }
    if (fsync(file)) valid = false;
    if (close(file)) valid = false;
    if (!valid) {
        struct stat current{};
        if (fstatat(directory, "local-udid.bin", &current, AT_SYMLINK_NOFOLLOW) == 0 &&
                current.st_dev == created.st_dev && current.st_ino == created.st_ino) (void)unlinkat(directory, "local-udid.bin", 0);
    }
    if (close(directory)) valid = false;
    return valid;
}

bool ActionbagQueryNodes(const std::string& output)
{
    void* provider = dlopen(provider_path, RTLD_NOW | RTLD_LOCAL);
    if (!provider) throw std::runtime_error("peer_provider_load_failed");
    // SDK workers may outlive these synchronous queries; retain DSO to process exit.
    const auto local_query = reinterpret_cast<decltype(&GetLocalNodeDeviceInfo)>(dlsym(provider, "GetLocalNodeDeviceInfo"));
    const auto all_query = reinterpret_cast<decltype(&GetAllNodeDeviceInfo)>(dlsym(provider, "GetAllNodeDeviceInfo"));
    const auto release = reinterpret_cast<decltype(&FreeNodeInfo)>(dlsym(provider, "FreeNodeInfo"));
    const auto key_query = reinterpret_cast<decltype(&GetNodeKeyInfo)>(dlsym(provider, "GetNodeKeyInfo"));
    if (!local_query || !all_query || !release || !key_query) throw std::runtime_error("peer_provider_symbol_missing");
    NodeBasicInfo local{};
    const int local_rc = local_query(package, &local);
    std::printf("PEER_LOCAL rc=%d\n", local_rc);
    std::string local_id;
    if (local_rc || !CopyRouteNetworkId(local.networkId, sizeof(local.networkId), local_id)) return false;
    NodeBasicInfo* returned = nullptr;
    int count = -1;
    const int all_rc = all_query(package, &returned, &count);
    const auto free_node = [release](NodeBasicInfo* nodes) noexcept {
        release(nodes);
        std::puts("PEER_BUFFER released=1 allocation=1");
    };
    std::unique_ptr<NodeBasicInfo, decltype(free_node)> owned(returned, free_node);
    std::printf("PEER_ONLINE rc=%d count=%d allocation=%d\n", all_rc, count, int(returned != nullptr));
    if (all_rc || !ValidateRouteNodeCount(count, returned != nullptr)) return false;
    std::vector<std::string> remotes;
    for (int i = 0; i < count; ++i)
        if (!AppendRouteNetworkId(local_id, returned[i].networkId, sizeof(returned[i].networkId), remotes)) return false;
    const bool had_buffer = returned != nullptr;
    owned.reset();
    if (!had_buffer) std::puts("PEER_BUFFER released=0 allocation=0");
    if (remotes.empty()) return false;
    std::array<std::uint8_t, UDID_BUF_LEN> identity;
    identity.fill(0xa5); // The native API must supply its own terminator.
    const int key_rc = key_query(package, local_id.c_str(), NODE_KEY_UDID, identity.data(), static_cast<std::int32_t>(identity.size()));
    const auto observed = ObserveProbeIdentity(key_rc, identity.data(), identity.size());
    const bool exported = observed.valid && ActionbagSaveIdentity(output, identity.data(), observed.length);
    identity.fill(0);
    std::printf("PEER_UDID rc=%d terminated=%d valid=%d bytes=%zu exported=%d mode=0600 identity_bytes=NOT_LOGGED\n",
        key_rc, int(observed.nul_terminated), int(observed.valid), observed.length, int(exported));
    if (!exported) return false;
    std::printf("route_kind=local network_id=%s\n", local_id.c_str());
    for (std::size_t i = 0; i < remotes.size(); ++i)
        std::printf("route_kind=remote index=%zu network_id=%s\n", i, remotes[i].c_str());
    std::printf("PEER_DIRECTORY valid=1 remotes=%zu free_returned=1 cap=32 communication=NOT_RUN\n", remotes.size());
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::uint64_t token = 0;
    if (argc != 8 || std::strcmp(argv[1], "--list-nodes") || std::strcmp(argv[2], "--check-identities") ||
            std::strcmp(argv[3], "--expected-token") || !ParseTokenArgument(argv[4], token) ||
            std::strcmp(argv[5], "--preflight-confirmed") || std::strcmp(argv[6], "--identity-output") ||
            !ValidIdentityExportPath(argv[7])) return 2;
    ActionbagPeerOperations operations;
    TokenFirstEntry identity(operations);
    if (!identity.Begin({TokenEntryMode::Record, true, true, token, TokenIdentity::ActionBag})) {
        std::printf("PEER_QUERY_FINAL success=0 admitted=0 restored=%d socket_calls=0 provisioning_calls=0\n", int(identity.RestoreSucceeded()));
        return 3;
    }
    bool success = false;
    try { success = ActionbagQueryNodes(argv[7]); }
    catch (...) { std::puts("PEER_QUERY_EXCEPTION result=STOPPED"); }
    const bool restored = identity.Restore();
    std::printf("PEER_QUERY_FINAL success=%d admitted=1 restored=%d socket_calls=0 provisioning_calls=0\n", int(success), int(restored));
    return success && restored ? 0 : 3;
}
