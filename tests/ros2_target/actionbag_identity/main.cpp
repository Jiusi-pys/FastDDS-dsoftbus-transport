#include "actionbag_identity.hpp"
#include "token_first_entry.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mdds_actionbag;
using namespace fastdds_dsoftbus_probe;
namespace {
void Check(bool value) { if (!value) throw std::runtime_error("identity contract failed"); }
struct Api final : ActionbagIdentityApi {
    std::uint64_t self = 0x100000001ULL, returned = 42;
    bool health = true, marker = true, granted = true, throw_create = false;
    int create_calls = 0, sets = 0, reload_rc = 0, select_rc = 0, restore_rc = 0;
    std::uint32_t existing = 0;
    std::vector<std::string> calls;
    std::uint64_t GetSelf() override { calls.push_back("self"); return self; }
    bool LegacyHealthy(char side) override { Check(side == 'A' || side == 'B'); calls.push_back("health"); return health; }
    std::uint32_t LookupNew() override { calls.push_back("lookup"); return existing; }
    bool ClaimAttempt() override { calls.push_back("marker"); return marker; }
    std::uint64_t CreateExact() override { ++create_calls; calls.push_back("create"); if (throw_create) throw 1; return returned; }
    int SetSelf(std::uint64_t value) override { ++sets; self = value; calls.push_back(sets == 1 ? "select" : "restore"); return sets == 1 ? select_rc : restore_rc; }
    int Reload() override { calls.push_back("reload"); return reload_rc; }
    bool NewGranted(std::uint32_t token) override { Check(token == returned); calls.push_back("verify"); return granted; }
};
struct Operations final : TokenEntryOperations {
    std::uint64_t self = 99;
    int sets = 0, verifies = 0;
    std::string name = identity_name;
    bool granted = true;
    TaskCheckpoint ObserveTasks() override { return {true, true, 7, 7, 1, 7}; }
    std::uint64_t GetSelf() override { return self; }
    int SetSelf(std::uint64_t value) override { ++sets; self = value; return 0; }
    OwnedTokenObservation VerifyOwned(std::uint64_t value, const char*) override {
        ++verifies; return {value, 1, 1, 0, name, 1, 1, 0, {granted ? 0 : -1}, 0};
    }
    void Diagnostic(const char*, std::uint64_t, std::uint64_t, int) override {}
    bool EnterProvider() override { throw std::runtime_error("Begin must not enter business"); }
    bool CloseProvider() override { throw std::runtime_error("Begin must not close business"); }
};
} // namespace

int main()
{
    try {
        Api success;
        auto result = ActionbagProvision(success, 'A');
        Check(result.success && result.restored && result.restore_attempted && success.create_calls == 1 && success.sets == 2);
        Check(success.self == 0x100000001ULL);
        Check(success.calls == std::vector<std::string>{"self", "health", "lookup", "marker", "create", "select", "self", "reload", "verify", "restore", "self"});
        for (int failure = 0; failure < 3; ++failure) {
            Api api;
            if (failure == 0) api.health = false;
            if (failure == 1) api.existing = 42;
            if (failure == 2) api.marker = false;
            result = ActionbagProvision(api, 'B');
            Check(!result.success && !result.write_attempted && result.restored && api.create_calls == 0 && api.sets == 0);
        }
        for (int failure = 0; failure < 7; ++failure) {
            Api api;
            if (failure == 0) api.returned = 0;
            if (failure == 1) api.throw_create = true;
            if (failure == 2) api.returned = 0x100000001ULL;
            if (failure == 3) api.select_rc = -1;
            if (failure == 4) api.reload_rc = -1;
            if (failure == 5) api.granted = false;
            if (failure == 6) api.restore_rc = -1;
            result = ActionbagProvision(api, 'A');
            Check(!result.success && result.write_attempted && api.create_calls == 1);
            Check(failure < 3 ? api.sets == 0 : api.sets == 2);
            Check(failure == 6 ? !result.restored : result.restored);
        }
        Api invalid;
        Check(!ActionbagProvision(invalid, 'X').write_attempted && invalid.create_calls == 0);
        for (const auto kind : {TokenIdentity::LegacyProbe, TokenIdentity::ActionBag}) {
            for (const auto mode : {TokenEntryMode::Record, TokenEntryMode::ListNodes}) {
                Operations op;
                op.name = kind == TokenIdentity::LegacyProbe ? "fastdds_dsoftbus_probe" : identity_name;
                TokenFirstEntry entry(op);
                Check(entry.Begin({mode, true, true, 42, kind}));
                Check(entry.Restore() && op.self == 99 && op.sets == 2);
                Check(op.verifies == (mode == TokenEntryMode::Record ? 1 : 2));
            }
        }
        Operations default_new;
        TokenFirstEntry default_entry(default_new);
        Check(!default_entry.Begin({TokenEntryMode::Record, true, true, 42}));
        Check(default_new.sets == 2 && default_new.self == 99); // Default stays legacy.
        Operations wrong_old;
        wrong_old.name = "fastdds_dsoftbus_probe";
        TokenFirstEntry exact_entry(wrong_old);
        Check(!exact_entry.Begin({TokenEntryMode::Record, true, true, 42, TokenIdentity::ActionBag}));
        Operations denied;
        denied.granted = false;
        TokenFirstEntry denied_entry(denied);
        Check(!denied_entry.Begin({TokenEntryMode::Record, true, true, 42, TokenIdentity::ActionBag}));
        Check(denied.sets == 2 && denied.self == 99);
        Check(!AdmitTokenEntry({TokenEntryMode::Record, true, true, 42, static_cast<TokenIdentity>(99)}));
        std::cout << "PASS scope=LOCAL_FIXED_IDENTITY_FLOW_NO_PLATFORM_CALLS\n";
        return 0;
    } catch (...) { std::cerr << "FAIL identity contract\n"; return 1; }
}
