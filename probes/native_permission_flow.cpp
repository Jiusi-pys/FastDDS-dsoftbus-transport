#include "native_permission_flow.hpp"

#include <ostream>
#include <stdexcept>

namespace mdds_probe {

NativePermissionResult RunNativePermissionProbe(const NativePermissionOptions& options,
        NativePermissionApi& api, std::ostream& log)
{
    NativePermissionResult result;
    uint64_t original = 0;
    bool switch_attempted = false;
    bool operation_passed = false;
    bool log_failed = false;

    // A broken output stream must not prevent restoration. Before a stateful
    // operation, however, failure to emit its stage record aborts that operation.
    const auto emit = [&](const std::string& message)
            {
                try
                {
                    log << "native_permission " << message << '\n' << std::flush;
                    if (log.good()) return true;
                }
                catch (...) {}
                log_failed = true;
                return false;
            };
    const auto stage = [&](const std::string& message)
            {
                if (!emit(message)) throw std::runtime_error("stage_log_failed");
            };
    const auto require = [](bool condition, const char* reason)
            {
                if (!condition) throw std::runtime_error(reason);
            };

    try
    {
        const bool grant = options.mode == NativePermissionMode::Grant;
        const bool socket = options.mode == NativePermissionMode::SocketCheck;
        const bool revoke = options.mode == NativePermissionMode::Revoke;
        require(grant || socket || revoke, "invalid_mode");
        require(grant || options.expected_token != 0, "expected_token_required");
        require(!options.recover_unregistered || (revoke && options.expected_token != 0),
                "unregistered_recovery_requires_revoke_and_owned_token");
        stage(std::string("begin mode=") + (grant ? "grant" : (socket ? "socket-check" : "revoke")) +
                " expected_token=" + std::to_string(options.expected_token) +
                " recover_unregistered=" + (options.recover_unregistered ? "true" : "false"));

        original = api.GetSelfToken();
        stage("original_token=" + std::to_string(original));
        require(original != 0, "original_token_unknown");
        const auto before = api.LookupProjectToken();
        stage("preflight_lookup status=" + std::to_string(before.status) + " token=" + std::to_string(before.token));
        const bool recover_owned_unregistered = options.recover_unregistered &&
                before.status == native_lookup_owned_unregistered && before.token == 0;
        require(before.status == 0 || recover_owned_unregistered, "preflight_lookup_error");
        if (recover_owned_unregistered)
        {
            // The external guard has proved ownership of the persisted ID.
            // Service registration may not yet have happened after a failed
            // grant. Permit clearing that one entry, without claiming DENIED.
            stage("recovery_preflight=external_json_ownership_confirmed service_identity=UNREGISTERED");
        }
        else if (options.expected_token == 0)
        {
            require(grant && before.token == 0, "first_grant_requires_confirmed_absence");
            stage("preflight_absence=confirmed_by_api_or_external_guard");
        }
        else
        {
            require(before.token == options.expected_token, "preflight_token_mismatch_or_unknown");
            const bool native = api.IsNativeToken(before.token);
            stage(std::string("preflight_native=") + (native ? "true" : "false"));
            require(native, "preflight_not_native_or_unknown");
            const int permission = api.VerifyDatasync(before.token);
            stage("preflight_permission=" + std::to_string(permission));
            require(permission == 0 || permission == -1, "preflight_permission_query_error");
        }

        result.token = options.expected_token;
        if (!socket)
        {
            stage(std::string("write_begin action=") + (grant ? "grant" : "revoke") +
                    " persistent_state=UNCONFIRMED");
            result.token = api.WriteProjectToken(grant);
            stage("write_return token=" + std::to_string(result.token));
            require(result.token != 0, "write_returned_zero");
            require(options.expected_token == 0 || result.token == options.expected_token,
                    "write_returned_unexpected_token");
        }

        stage("switch_begin token=" + std::to_string(result.token));
        switch_attempted = true; // A failing/throwing Set may still change identity.
        const int set_result = api.SetSelfToken(result.token);
        stage("switch_return=" + std::to_string(set_result));
        require(set_result == 0, "switch_failed");
        const uint64_t switched = api.GetSelfToken();
        stage("switch_readback=" + std::to_string(switched));
        require(switched == result.token, "switch_readback_mismatch");

        if (!socket)
        {
            stage("reload_begin");
            const int reload_result = api.ReloadNativeTokens();
            stage("reload_return=" + std::to_string(reload_result));
            require(reload_result == 0, "reload_failed");
        }

        // Re-read identity after switch/reload before interpreting DENIED. An
        // absent token or lookup error cannot count as successful revocation.
        const auto after = api.LookupProjectToken();
        stage("verification_lookup status=" + std::to_string(after.status) + " token=" + std::to_string(after.token));
        require(after.status == 0 && after.token == result.token && after.token != 0,
                "verification_token_mismatch_or_query_error");
        const bool native = api.IsNativeToken(result.token);
        stage(std::string("verification_native=") + (native ? "true" : "false"));
        require(native, "verification_not_native_or_unknown");
        const int permission = api.VerifyDatasync(result.token);
        stage("verification_permission=" + std::to_string(permission));
        require(permission == 0 || permission == -1, "verification_permission_query_error");
        require(permission == (revoke ? -1 : 0), revoke ? "revocation_not_effective" : "permission_not_granted");

        if (socket)
        {
            stage("socket_begin");
            const int32_t id = api.CreateSocket();
            // Once the ID is returned, cleanup must precede any fallible output.
            if (id > 0)
            {
                const bool logged = emit("socket_return=" + std::to_string(id) + " shutdown_begin");
                api.ShutdownSocket(id); // Exactly one attempt, even if it throws.
                stage("shutdown_returned id=" + std::to_string(id));
                require(logged, "socket_stage_log_failed");
            }
            else
            {
                stage("socket_return=" + std::to_string(id));
                throw std::runtime_error("socket_failed");
            }
        }
        operation_passed = true;
        result.reason = "mode_checks_passed_external_persistent_verification_required";
    }
    catch (const std::exception& error)
    {
        result.reason = error.what();
        emit("operation_failed reason=" + result.reason);
    }
    catch (...)
    {
        result.reason = "unknown_api_exception";
        emit("operation_failed reason=unknown_api_exception");
    }

    if (original != 0)
    {
        bool restore_call_ok = true;
        if (switch_attempted)
        {
            // Restoration proceeds even if logging or the earlier API failed.
            emit("restore_begin token=" + std::to_string(original));
            try
            {
                const int rc = api.SetSelfToken(original);
                restore_call_ok = rc == 0;
                emit("restore_return=" + std::to_string(rc));
            }
            catch (...)
            {
                restore_call_ok = false;
                emit("restore_exception");
            }
        }
        try
        {
            const uint64_t actual = api.GetSelfToken();
            result.restored = restore_call_ok && actual == original;
            emit("restore_readback=" + std::to_string(actual));
        }
        catch (...)
        {
            result.restored = false;
            emit("restore_readback_exception");
        }
    }
    if (switch_attempted && !result.restored)
    {
        result.reason += "; original_identity_restore_unconfirmed";
    }
    result.success = operation_passed && result.restored && !log_failed;
    result.exit_code = result.success ? 0 : 1;
    emit(std::string("result=") + (result.success ? "PASS" : "FAIL") +
            " token=" + std::to_string(result.token) + " restored=" + (result.restored ? "true" : "false") +
            " persistent_state=UNCONFIRMED reason=" + result.reason);
    if (log_failed)
    {
        result.success = false;
        result.exit_code = 1;
    }
    return result;
}

} // namespace mdds_probe
