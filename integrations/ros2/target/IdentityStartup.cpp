// Installed integration startup; the ordinary business source remains ROS-only.
#include "identity/token_first_entry.hpp"
#include "identity/token_bridge.h"
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <unistd.h>
#include <exception>
#include "FailureProtocol.hpp"

extern int FastddsDsoftbusRosBusinessMain(int, char**);
namespace {
class Operations final : public fastdds_dsoftbus_probe::TokenEntryOperations {
public:
    fastdds_dsoftbus_probe::TaskCheckpoint ObserveTasks() override {
        fastdds_dsoftbus_probe::TaskCheckpoint result;
        result.process_id = static_cast<std::uint64_t>(getpid());
        result.current_tid = result.process_id; // This entry is the process main.
        DIR* directory = opendir("/proc/self/task");
        result.opened = directory != nullptr;
        if (!directory) return result;
        bool valid = true;
        for (;;) {
            errno = 0; auto* item = readdir(directory);
            if (!item) { result.complete = valid && errno == 0; break; }
            if (item->d_name[0] == '.') continue;
            std::uint64_t tid = 0;
            const auto end = item->d_name + std::strlen(item->d_name);
            const auto parsed = std::from_chars(item->d_name, end, tid);
            if (parsed.ec != std::errc{} || parsed.ptr != end || !tid) valid = false;
            if (!result.count) result.sole_tid = tid;
            if (result.count < 2) ++result.count;
        }
        if (closedir(directory)) result.complete = false;
        std::fprintf(stdout,"ROS_IDENTITY_TASKS pid=%ld count=%zu complete=%d\n",static_cast<long>(getpid()),result.count,int(result.complete));
        return result;
    }
    std::uint64_t GetSelf() override { return MddsTokenBridgeGetSelfToken(); }
    int SetSelf(std::uint64_t value) override { return MddsTokenBridgeSetSelfToken(value); }
    fastdds_dsoftbus_probe::OwnedTokenObservation VerifyOwned(std::uint64_t value, const char* phase) override {
        MddsTokenBridgeResult native{};
        const int rc = MddsTokenBridgeVerify(static_cast<std::uint32_t>(value), &native);
        std::fprintf(stdout,"ROS_IDENTITY phase=%s bridge=%d called=%u info_rc=%d permission_rc=%d granted=%d\n",
            phase,rc,native.called,native.infoRc,native.permissionRc,native.granted);
        fastdds_dsoftbus_probe::OwnedTokenObservation result;
        if (rc || native.called != 15) return result;
        result.lookup=native.lookupToken; result.type=native.type; result.native_type=native.nativeType;
        result.info_rc=native.infoRc; result.process_name=native.processName; result.apl=native.apl;
        result.normal_apl=native.normalApl; result.query_rc=native.permissionRc;
        result.permission_granted=native.permissionGranted;
        if(native.permissionCount==1)result.states.push_back(native.permissionState);
        return result;
    }
    void Diagnostic(const char* stage, std::uint64_t expected, std::uint64_t actual, int rc) override {
        std::fprintf(stdout,"ROS_IDENTITY stage=%s matches=%d rc=%d\n",stage,int(expected==actual),rc);std::fflush(stdout);
    }
    bool EnterProvider() override { return false; } // Begin/Restore only; never used to model provider success.
    bool CloseProvider() override { return false; }
};
}
int main(int argc, char** argv) {
    if (!std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG"))
        return FastddsDsoftbusRosBusinessMain(argc,argv);
    const char* selected = std::getenv("FASTDDS_DSOFTBUS_NATIVE_TOKEN");
    const char* asserted = std::getenv("FASTDDS_DSOFTBUS_IDENTITY_ADMITTED");
    std::uint64_t token = 0;
    if (!selected || !asserted || std::strcmp(asserted,"1") ||
            !fastdds_dsoftbus_probe::ParseTokenArgument(selected,token)) {
        std::fputs("ROS_IDENTITY_REJECTED missing_or_invalid_external_input\n",stderr);return 3;
    }
    Operations operations;
    fastdds_dsoftbus_probe::TokenFirstEntry identity(operations);
    if (!identity.Begin({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,token})) return 3;
    // Identity is selected and verified before any rclcpp init/thread/entity.
    int business=4;
    try { business=FastddsDsoftbusRosBusinessMain(argc,argv); }
    catch(const std::exception& error) {
        (void)error;
        if(argc==4)ros_failure_protocol::Report(argv[2],argv[1],static_cast<long>(getpid()),"BUSINESS_ENTRY","UNHANDLED_EXCEPTION");
    }
    catch(...) { if(argc==4)ros_failure_protocol::Report(argv[2],argv[1],static_cast<long>(getpid()),"BUSINESS_ENTRY","UNKNOWN_EXCEPTION"); }
    const bool restored = identity.Restore();
    std::fprintf(stdout,"ROS_IDENTITY_FINAL restored=%d business_rc=%d\n",int(restored),business);std::fflush(stdout);
    return restored ? business : 6;
}
