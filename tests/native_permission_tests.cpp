#include "native_permission_flow.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace fastdds_dsoftbus_probe;

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
        std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

class FakeApi final : public NativePermissionApi
{
public:
    uint64_t original = 7, current = 7, service_token = 123, written_token = 123;
    int initial_permission = 0, permission = 0;
    int lookup_error_on = 0, lookup_calls = 0;
    int owned_unregistered_on = 0;
    uint64_t unregistered_lookup_token = 0;
    bool native = true, reload_applies = true, post_reload_unknown = false;
    bool switch_readback_bad = false, restore_readback_bad = false;
    int switch_result = 0, restore_result = 0, reload_result = 0;
    int32_t socket_result = 17;
    bool grant_written = false, wrote = false, restored_call = false;
    std::string throw_on;
    std::vector<std::string> calls;
    std::vector<uint64_t> set_ids;
    std::vector<int32_t> shutdown_ids;

    void note(const std::string& event)
    {
        calls.push_back(event);
        if (event == throw_on) throw std::runtime_error("injected " + event);
    }

    unsigned count(const std::string& event) const
    {
        return static_cast<unsigned>(std::count(calls.begin(), calls.end(), event));
    }

    TokenLookup LookupProjectToken() override
    {
        note("lookup");
        ++lookup_calls;
        if (lookup_calls == lookup_error_on) return {-71, 0};
        if (lookup_calls == owned_unregistered_on)
            return {native_lookup_owned_unregistered, unregistered_lookup_token};
        return {0, service_token};
    }

    uint64_t GetSelfToken() override
    {
        note("self");
        if (restored_call && restore_readback_bad) return 0;
        if (!restored_call && current == written_token && switch_readback_bad) return 0;
        return current;
    }

    uint64_t WriteProjectToken(bool grant) override
    {
        note(grant ? "grant" : "revoke");
        grant_written = grant;
        wrote = true;
        return written_token;
    }

    int SetSelfToken(uint64_t token) override
    {
        set_ids.push_back(token);
        current = token; // Even a failing switch may have changed observable state.
        restored_call = token == original;
        note(restored_call ? "restore" : "switch");
        return restored_call ? restore_result : switch_result;
    }

    int ReloadNativeTokens() override
    {
        note("reload");
        if (reload_result == 0 && reload_applies && wrote)
        {
            service_token = post_reload_unknown ? 0 : written_token;
            permission = grant_written ? 0 : -1;
        }
        return reload_result;
    }

    bool IsNativeToken(uint64_t token) override
    {
        note("native");
        return native && token != 0 && token == service_token;
    }

    int VerifyDatasync(uint64_t token) override
    {
        note("verify");
        CHECK(token == service_token && native && token != 0);
        return permission;
    }

    int32_t CreateSocket() override
    {
        note("socket");
        CHECK(current == service_token && permission == 0);
        return socket_result;
    }

    void ShutdownSocket(int32_t socket) override
    {
        shutdown_ids.push_back(socket);
        note("shutdown");
    }
};

NativePermissionResult run(FakeApi& api, NativePermissionMode mode, uint64_t expected = 123,
        bool recover_unregistered = false)
{
    std::ostringstream log;
    const auto result = RunNativePermissionProbe({mode, expected, recover_unregistered}, api, log);
    CHECK(!result.persistent_state_confirmed);
    CHECK(log.str().find("persistent_state=UNCONFIRMED") != std::string::npos);
    return result;
}

void grant_success()
{
    FakeApi api;
    api.service_token = 0;
    api.permission = -1;
    auto result = run(api, NativePermissionMode::Grant, 0);
    CHECK(result.success && result.exit_code == 0 && result.token == 123 && result.restored);
    CHECK(api.count("grant") == 1 && api.count("revoke") == 0 && api.count("reload") == 1);
    CHECK(api.count("socket") == 0 && api.count("shutdown") == 0);
    CHECK(api.current == api.original && api.set_ids == std::vector<uint64_t>({123, 7}));
    CHECK(api.permission == 0); // No automatic revoke; runner must verify/clean persistent state.

    FakeApi existing;
    existing.permission = -1;
    result = run(existing, NativePermissionMode::Grant);
    CHECK(result.success && result.restored);
    CHECK(existing.count("grant") == 1 && existing.count("revoke") == 0);
}

void reject_identity_ambiguity()
{
    for (const auto mode : {NativePermissionMode::Grant, NativePermissionMode::SocketCheck, NativePermissionMode::Revoke})
    {
        FakeApi wrong;
        auto result = run(wrong, mode, 124);
        CHECK(!result.success && result.exit_code != 0);
        CHECK(wrong.count("grant") + wrong.count("revoke") + wrong.count("switch") + wrong.count("socket") == 0);

        FakeApi ambiguous_zero;
        result = run(ambiguous_zero, mode, 0);
        CHECK(!result.success);
        CHECK(ambiguous_zero.count("grant") + ambiguous_zero.count("revoke") + ambiguous_zero.count("switch") == 0);

        FakeApi missing;
        missing.service_token = 0;
        result = run(missing, mode);
        CHECK(!result.success);
        CHECK(missing.count("grant") + missing.count("revoke") + missing.count("switch") == 0);

        FakeApi query_failure;
        query_failure.lookup_error_on = 1;
        result = run(query_failure, mode, mode == NativePermissionMode::Grant ? 0 : 123);
        CHECK(!result.success);
        CHECK(query_failure.count("grant") + query_failure.count("revoke") + query_failure.count("switch") == 0);

        FakeApi not_native;
        not_native.native = false;
        result = run(not_native, mode);
        CHECK(!result.success);
        CHECK(not_native.count("grant") + not_native.count("revoke") + not_native.count("switch") == 0);
    }

    FakeApi invalid;
    CHECK(!run(invalid, static_cast<NativePermissionMode>(99)).success);
    CHECK(invalid.calls.empty());
}

void invalid_creation_or_original()
{
    for (const auto mode : {NativePermissionMode::Grant, NativePermissionMode::Revoke})
    {
        FakeApi zero;
        zero.written_token = 0;
        auto result = run(zero, mode);
        CHECK(!result.success);
        CHECK(zero.count("switch") + zero.count("reload") + zero.count("socket") == 0);

        FakeApi mismatched;
        mismatched.written_token = 987;
        result = run(mismatched, mode);
        CHECK(!result.success);
        CHECK(mismatched.count("switch") + mismatched.count("reload") + mismatched.count("socket") == 0);

        FakeApi no_original;
        no_original.original = no_original.current = 0;
        result = run(no_original, mode);
        CHECK(!result.success && !result.restored);
        CHECK(no_original.count("grant") + no_original.count("revoke") + no_original.count("switch") == 0);
    }
    FakeApi first_zero;
    first_zero.service_token = first_zero.written_token = 0;
    CHECK(!run(first_zero, NativePermissionMode::Grant, 0).success);
    CHECK(first_zero.count("switch") + first_zero.count("reload") == 0);
}

void switch_failure_restores()
{
    for (const auto mode : {NativePermissionMode::Grant, NativePermissionMode::SocketCheck, NativePermissionMode::Revoke})
    {
        FakeApi failed;
        failed.switch_result = -5;
        auto result = run(failed, mode);
        CHECK(!result.success && result.restored);
        CHECK(failed.count("restore") == 1 && failed.current == failed.original);
        CHECK(failed.count("reload") + failed.count("socket") == 0);

        FakeApi wrong_readback;
        wrong_readback.switch_readback_bad = true;
        result = run(wrong_readback, mode);
        CHECK(!result.success && result.restored);
        CHECK(wrong_readback.count("restore") == 1);
        CHECK(wrong_readback.count("reload") + wrong_readback.count("socket") == 0);
    }
}

void grant_verification_failures()
{
    FakeApi reload_failed;
    reload_failed.reload_result = -1;
    auto result = run(reload_failed, NativePermissionMode::Grant);
    CHECK(!result.success && result.restored && reload_failed.count("socket") == 0);

    FakeApi still_denied;
    still_denied.permission = -1;
    still_denied.reload_applies = false;
    result = run(still_denied, NativePermissionMode::Grant);
    CHECK(!result.success && result.restored && still_denied.count("socket") == 0);

    FakeApi post_missing;
    post_missing.post_reload_unknown = true;
    result = run(post_missing, NativePermissionMode::Grant);
    CHECK(!result.success && result.restored && post_missing.count("socket") == 0);

    FakeApi post_query_error;
    post_query_error.lookup_error_on = 2;
    result = run(post_query_error, NativePermissionMode::Grant);
    CHECK(!result.success && result.restored && post_query_error.count("socket") == 0);
    CHECK(post_query_error.count("revoke") == 0);
}

void socket_mode_read_only_permissions()
{
    {
        constexpr int32_t id = 17;
        FakeApi api;
        api.socket_result = id;
        const auto result = run(api, NativePermissionMode::SocketCheck);
        CHECK(result.success && result.restored && result.token == 123);
        CHECK(api.count("grant") + api.count("revoke") + api.count("reload") == 0);
        CHECK(api.count("socket") == 1 && api.shutdown_ids == std::vector<int32_t>({id}));
        CHECK(api.current == api.original);
        CHECK(std::find(api.calls.begin(), api.calls.end(), "shutdown") <
                std::find(api.calls.begin(), api.calls.end(), "restore"));
    }

    FakeApi zero_id;
    zero_id.socket_result = 0;
    auto zero_result = run(zero_id, NativePermissionMode::SocketCheck);
    CHECK(!zero_result.success && zero_result.exit_code != 0 && zero_result.restored);
    CHECK(zero_id.count("socket") == 1 && zero_id.count("shutdown") == 0);
    CHECK(zero_id.count("grant") + zero_id.count("revoke") + zero_id.count("reload") == 0);
    CHECK(zero_id.current == zero_id.original);

    FakeApi denied;
    denied.permission = -1;
    auto result = run(denied, NativePermissionMode::SocketCheck);
    CHECK(!result.success);
    CHECK(denied.count("socket") + denied.count("grant") + denied.count("revoke") + denied.count("reload") == 0);

    FakeApi unknown;
    unknown.permission = -9;
    result = run(unknown, NativePermissionMode::SocketCheck);
    CHECK(!result.success);
    CHECK(unknown.count("socket") == 0);

    FakeApi failed;
    failed.socket_result = -42;
    result = run(failed, NativePermissionMode::SocketCheck);
    CHECK(!result.success && result.restored);
    CHECK(failed.count("socket") == 1 && failed.count("shutdown") == 0);
    CHECK(failed.count("grant") + failed.count("revoke") + failed.count("reload") == 0);
}

void revoke_requires_explicit_denied()
{
    FakeApi good;
    auto result = run(good, NativePermissionMode::Revoke);
    CHECK(result.success && result.restored && result.token == 123);
    CHECK(good.count("revoke") == 1 && good.count("reload") == 1);
    CHECK(good.count("grant") + good.count("socket") + good.count("shutdown") == 0);
    CHECK(good.permission == -1 && good.service_token == 123);

    for (const int permission : {0, -9})
    {
        FakeApi unchanged;
        unchanged.reload_applies = false;
        unchanged.permission = permission;
        result = run(unchanged, NativePermissionMode::Revoke);
        CHECK(!result.success);
        CHECK(unchanged.count("socket") == 0);
    }

    FakeApi disappeared;
    disappeared.post_reload_unknown = true;
    result = run(disappeared, NativePermissionMode::Revoke);
    CHECK(!result.success && result.restored);
    CHECK(disappeared.count("socket") == 0);
}

void restoration_failure_is_failure()
{
    for (const auto mode : {NativePermissionMode::Grant, NativePermissionMode::SocketCheck, NativePermissionMode::Revoke})
    {
        FakeApi failed;
        failed.restore_result = -11;
        auto result = run(failed, mode);
        CHECK(!result.success && result.exit_code != 0 && !result.restored);
        CHECK(failed.count("restore") == 1);

        FakeApi mismatch;
        mismatch.restore_readback_bad = true;
        result = run(mismatch, mode);
        CHECK(!result.success && result.exit_code != 0 && !result.restored);
        CHECK(mismatch.count("restore") == 1);
    }
}

void api_exceptions_restore_and_never_chain_writes()
{
    for (const auto& event : {"switch", "reload"})
    {
        FakeApi api;
        api.throw_on = event;
        const auto result = run(api, NativePermissionMode::Grant);
        CHECK(!result.success && result.restored && api.count("restore") == 1);
        CHECK(api.count("grant") == 1 && api.count("revoke") == 0 && api.count("socket") == 0);
    }

    for (const auto& event : {"socket", "shutdown"})
    {
        FakeApi api;
        api.throw_on = event;
        const auto result = run(api, NativePermissionMode::SocketCheck);
        CHECK(!result.success && result.restored && api.count("restore") == 1);
        CHECK(api.count("grant") + api.count("revoke") + api.count("reload") == 0);
        CHECK(api.count("shutdown") == (std::string(event) == "shutdown" ? 1 : 0));
    }
}

void recover_owned_unregistered_requires_post_verification()
{
    FakeApi recover;
    recover.service_token = 0;
    recover.owned_unregistered_on = 1;
    auto result = run(recover, NativePermissionMode::Revoke, 123, true);
    CHECK(result.success && result.restored && result.token == 123);
    CHECK(recover.count("revoke") == 1 && recover.count("grant") == 0);
    CHECK(recover.count("native") == 1 && recover.count("verify") == 1);
    CHECK(recover.count("socket") == 0 && recover.permission == -1);

    for (const auto mode : {NativePermissionMode::Grant, NativePermissionMode::SocketCheck, NativePermissionMode::Revoke})
    {
        FakeApi unauthorized;
        unauthorized.service_token = 0;
        unauthorized.owned_unregistered_on = 1;
        result = run(unauthorized, mode, 123, mode != NativePermissionMode::Revoke);
        CHECK(!result.success);
        CHECK(unauthorized.count("grant") + unauthorized.count("revoke") + unauthorized.count("switch") == 0);
    }
    FakeApi zero_expected;
    zero_expected.service_token = 0;
    zero_expected.owned_unregistered_on = 1;
    CHECK(!run(zero_expected, NativePermissionMode::Revoke, 0, true).success);
    CHECK(zero_expected.count("revoke") == 0);

    FakeApi malformed;
    malformed.owned_unregistered_on = 1;
    malformed.unregistered_lookup_token = 123;
    CHECK(!run(malformed, NativePermissionMode::Revoke, 123, true).success);
    CHECK(malformed.count("revoke") == 0);

    FakeApi rpc_error;
    rpc_error.lookup_error_on = 1;
    CHECK(!run(rpc_error, NativePermissionMode::Revoke, 123, true).success);
    CHECK(rpc_error.count("revoke") == 0);

    FakeApi still_unknown;
    still_unknown.service_token = 0;
    still_unknown.owned_unregistered_on = 1;
    still_unknown.reload_applies = false;
    result = run(still_unknown, NativePermissionMode::Revoke, 123, true);
    CHECK(!result.success && result.restored);
    CHECK(still_unknown.count("revoke") == 1);
    CHECK(still_unknown.count("verify") == 0); // Unknown never means DENIED.

    FakeApi post_error;
    post_error.service_token = 0;
    post_error.owned_unregistered_on = 1;
    post_error.lookup_error_on = 2;
    result = run(post_error, NativePermissionMode::Revoke, 123, true);
    CHECK(!result.success && result.restored && post_error.count("revoke") == 1);

    FakeApi post_not_native;
    post_not_native.service_token = 0;
    post_not_native.owned_unregistered_on = 1;
    post_not_native.native = false;
    result = run(post_not_native, NativePermissionMode::Revoke, 123, true);
    CHECK(!result.success && result.restored && post_not_native.count("verify") == 0);
}

void verify_denied_has_no_mutators()
{
    for (int permission : {-1, 0, -71})
    {
        FakeApi api; api.permission = permission;
        const auto result = run(api, static_cast<NativePermissionMode>(3));
        CHECK(result.success == (permission == -1));
        CHECK(!api.wrote && api.set_ids.empty() && api.shutdown_ids.empty());
        CHECK(api.count("reload") == 0 && api.count("socket") == 0);
        CHECK(api.current == api.original);
    }
    for (int failure = 0; failure < 4; ++failure)
    {
        FakeApi api; api.permission = -1;
        if (failure == 0) api.service_token = 0;
        if (failure == 1) api.native = false;
        if (failure == 2) api.lookup_error_on = 1;
        if (failure == 3) api.throw_on = "verify";
        CHECK(!run(api, static_cast<NativePermissionMode>(3)).success);
        CHECK(!api.wrote && api.set_ids.empty() && api.shutdown_ids.empty());
        CHECK(api.count("reload") == 0 && api.count("socket") == 0);
    }
    FakeApi invalid; invalid.permission = -1;
    CHECK(!run(invalid, static_cast<NativePermissionMode>(3), 0).success);
    CHECK(!run(invalid, static_cast<NativePermissionMode>(3), 999).success);
    CHECK(!run(invalid, static_cast<NativePermissionMode>(3), 123, true).success);
    CHECK(!invalid.wrote && invalid.set_ids.empty() && invalid.count("socket") == 0);
    FakeApi log_failure; log_failure.permission = -1;
    std::ostringstream broken; broken.setstate(std::ios::badbit);
    CHECK(!RunNativePermissionProbe({static_cast<NativePermissionMode>(3),123,false},log_failure,broken).success);
    CHECK(!log_failure.wrote && log_failure.set_ids.empty() && log_failure.count("socket") == 0);
}

struct TestCase { const char* id; void (*run)(); };
} // namespace

int main(int argc, char** argv)
{
    const TestCase cases[] = {{"NP-01", grant_success}, {"NP-02", reject_identity_ambiguity},
        {"NP-03", invalid_creation_or_original}, {"NP-04", switch_failure_restores},
        {"NP-05", grant_verification_failures}, {"NP-06", socket_mode_read_only_permissions},
        {"NP-07", revoke_requires_explicit_denied}, {"NP-08", restoration_failure_is_failure},
        {"NP-09", api_exceptions_restore_and_never_chain_writes},
        {"NP-10", recover_owned_unregistered_requires_post_verification},
        {"NP-11", verify_denied_has_no_mutators}};
    if (argc > 2) return 2;
    unsigned ran = 0, failed = 0;
    for (const auto& test : cases)
    {
        if (argc == 2 && std::string(argv[1]) != test.id) continue;
        ++ran;
        std::cout << test.id << " RUN\n" << std::flush;
        try { test.run(); std::cout << test.id << " PASS\n"; }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << test.id << " FAIL " << error.what() << '\n';
        }
    }
    std::cout << "native permission host flow: " << ran << " executed, " << failed
              << " failed; no device/persistent-state claim\n";
    return ran == 0 ? 2 : (failed == 0 ? 0 : 1);
}
