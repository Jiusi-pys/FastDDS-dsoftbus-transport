#include "token_first_entry.hpp"
#include "actionbag_identity.hpp"

#include <charconv>
#include <limits>

namespace fastdds_dsoftbus_probe {

bool ParseTokenArgument(std::string_view text, std::uint64_t& token) noexcept
{
    if (text.empty()) return false;
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed == 0 ||
            parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    token = parsed;
    return true;
}

bool AdmitTokenEntry(const TokenAdmission& admission) noexcept
{
    if (admission.identity != TokenIdentity::LegacyProbe && admission.identity != TokenIdentity::ActionBag) return false;
    if (!admission.cli_valid || !admission.preflight_asserted || admission.target == 0 ||
            admission.target > std::numeric_limits<std::uint32_t>::max()) return false;
    if (admission.mode == TokenEntryMode::ListNodes) return true;
    // Registered identity comes from the frozen external deployment input.
    // Record mode still requires one main task, SetSelf/GetSelf readback and
    // the SDK manager's owned/normal/granted checks.
    return admission.mode == TokenEntryMode::Record;
}

bool IsSingleMainTask(const TaskCheckpoint& checkpoint) noexcept
{
    return checkpoint.opened && checkpoint.complete && checkpoint.process_id != 0 &&
           checkpoint.current_tid == checkpoint.process_id && checkpoint.count == 1 &&
           checkpoint.sole_tid == checkpoint.current_tid;
}

bool IsOwnedNormalGranted(std::uint64_t target, const OwnedTokenObservation& observation, TokenIdentity identity) noexcept
{
    const char* name = identity == TokenIdentity::LegacyProbe ? "fastdds_dsoftbus_probe" :
        identity == TokenIdentity::ActionBag ? mdds_actionbag::identity_name : nullptr;
    return observation.lookup == target && observation.type == observation.native_type &&
           name && observation.info_rc == 0 && observation.process_name == name &&
           observation.apl == observation.normal_apl && observation.query_rc == 0 &&
           observation.states.size() == 1 && observation.states[0] == observation.permission_granted;
}

TokenFirstEntry::TokenFirstEntry(TokenEntryOperations& operations) noexcept : operations_(operations) {}
TokenFirstEntry::~TokenFirstEntry() { Restore(); }

bool TokenFirstEntry::Initialize(const TokenAdmission& admission)
{
    if (!AdmitTokenEntry(admission)) return false;
    if (admission.mode == TokenEntryMode::Record && !IsSingleMainTask(operations_.ObserveTasks())) return false;
    original_ = operations_.GetSelf();
    operations_.Diagnostic("original_token", original_, admission.target, 0);
    if (original_ == 0) return false;
    // Only the preserved manager mode may explicitly initialize manager IPC
    // before selection. Record mode first selects and checks the main token.
    if (admission.mode == TokenEntryMode::ListNodes &&
            !IsOwnedNormalGranted(admission.target, operations_.VerifyOwned(admission.target, "before_select"), admission.identity))
        return false;
    attempted_ = true;
    const int rc = operations_.SetSelf(admission.target);
    const auto readback = operations_.GetSelf();
    operations_.Diagnostic("select_token", admission.target, readback, rc);
    if (rc != 0 || readback != admission.target) return false;
    return IsOwnedNormalGranted(admission.target, operations_.VerifyOwned(admission.target, "after_select"), admission.identity);
}

bool TokenFirstEntry::Restore() noexcept
{
    if (!attempted_ || restoration_attempted_) return restored_;
    restoration_attempted_ = true;
    int rc = -1;
    std::uint64_t readback = 0;
    bool calls_ok = true;
    try { rc = operations_.SetSelf(original_); } catch (...) { calls_ok = false; }
    try { readback = operations_.GetSelf(); } catch (...) { calls_ok = false; }
    restored_ = calls_ok && rc == 0 && readback == original_;
    try { operations_.Diagnostic("restore_token", original_, readback, rc); }
    catch (...) { restored_ = false; }
    return restored_;
}

bool TokenFirstEntry::Begin(const TokenAdmission& admission) noexcept
{
    if (used_) return false;
    used_ = true;
    try { if(Initialize(admission)) return true; }
    catch (...) { try { operations_.Diagnostic("initialization_or_record_exception",0,0,-1); } catch (...) {} }
    Restore();
    return false;
}

bool TokenFirstEntry::Run(const TokenAdmission& admission) noexcept
{
    if(!Begin(admission))return false;
    bool succeeded = false, closed = true, entered = false;
    try
    {
        entered = true;
        succeeded = operations_.EnterProvider();
    }
    catch (...)
    {
        succeeded = false;
        try { operations_.Diagnostic("initialization_or_record_exception", 0, 0, -1); } catch (...) {}
    }
    if (entered)
    {
        try { closed = operations_.CloseProvider(); } catch (...) { closed = false; }
    }
    const bool restored = Restore();
    return succeeded && closed && restored;
}

} // namespace fastdds_dsoftbus_probe
