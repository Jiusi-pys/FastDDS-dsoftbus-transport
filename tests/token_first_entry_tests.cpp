#include "token_first_entry.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mdds_probe;
namespace {
constexpr std::uint64_t target = 671793191;
constexpr std::uint64_t original = 0x100000001ULL; // Restoration must not truncate to AccessTokenID.
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
TokenAdmission Admission() { return {TokenEntryMode::Record, true, true, target}; }

struct Fake : TokenEntryOperations {
    TaskCheckpoint task{true, true, 10, 10, 1, 10};
    OwnedTokenObservation observed{target, 1, 1, 0, "fastdds_dsoftbus_probe", 2, 2, 0, {0}, 0};
    std::vector<std::string> trace;
    std::uint64_t initial = original, selected_readback = target, restore_readback = original;
    int selected_rc = 0, restore_rc = 0, gets = 0, sets = 0, verifies = 0, providers = 0;
    bool verify_throws = false, provider_result = true, close_result = true, provider_throws = false;
    bool selected_throws = false, selected_read_throws = false, restore_throws = false, restore_read_throws = false;
    std::string diagnostic_throws;
    bool equal_logged = false;
    TaskCheckpoint ObserveTasks() override { trace.push_back("tasks"); return task; }
    std::uint64_t GetSelf() override {
        ++gets;
        if (sets == 0) { trace.push_back("get-original"); return initial; }
        if (sets == 1) {
            trace.push_back("get-target");
            if (selected_read_throws) throw std::runtime_error("selected readback exception");
            return selected_readback;
        }
        trace.push_back("get-restored");
        if (restore_read_throws) throw std::runtime_error("restore readback exception");
        return restore_readback;
    }
    int SetSelf(std::uint64_t token) override {
        ++sets;
        if (sets == 1) {
            Check(token == target, "selected wrong token"); trace.push_back("set-target");
            if (selected_throws) throw std::runtime_error("selected Set exception");
            return selected_rc;
        }
        Check(token == initial, "restoration must use exact full-width original");
        trace.push_back("restore-original");
        if (restore_throws) throw std::runtime_error("restore Set exception");
        return restore_rc;
    }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override {
        ++verifies; Check(token == target, "verification fallback or wrong target");
        trace.push_back(std::string("verify-") + phase);
        if (verify_throws) throw std::runtime_error("actual injected verify exception");
        return observed;
    }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int) override {
        if (std::string(stage) == "original_token") equal_logged = first == second;
        if (diagnostic_throws == stage) throw std::runtime_error("actual injected diagnostic exception");
    }
    bool EnterProvider() override {
        ++providers; trace.push_back("provider");
        if (provider_throws) throw std::runtime_error("provider exception");
        return provider_result;
    }
    bool CloseProvider() override { trace.push_back("close"); return close_result; }
};

void NoSdk(const Fake& fake) {
    Check(fake.sets == 0 && fake.verifies == 0 && fake.providers == 0, "pre-admission accessed Set/SDK");
}
void RestoredWithoutProvider(const Fake& fake) {
    Check(fake.sets == 2 && fake.providers == 0, "failure must restore once and never enter provider");
    Check(fake.trace.back() == "get-restored", "restore readback missing");
}

void TI01() {
    Fake f; TokenFirstEntry entry(f); Check(entry.Run(Admission()), "success run failed");
    const std::vector<std::string> expected{"tasks", "get-original", "set-target", "get-target",
        "verify-after_select", "provider", "close", "restore-original", "get-restored"};
    Check(f.trace == expected, "explicit Verify must follow successful selected-token readback");
}
void TI02() {
    for (const char* text : {"", "0", "4294967296", "18446744073709551616", "-1", "+671793191", "671793191x", " 671793191"}) {
        std::uint64_t value = 99;
        Check(!ParseTokenArgument(text, value) && value == 99, "malformed/overflow token accepted or output changed");
    }
    for (const char* text : {"671793191", "671320465"}) {
        std::uint64_t value = 0; Check(ParseTokenArgument(text, value), "owned token rejected");
        auto a = Admission(); a.target = value; Check(AdmitTokenEntry(a), "owned token admission rejected");
    }
    for (int variant = 0; variant != 6; ++variant) {
        Fake f; TokenFirstEntry e(f); auto a = Admission();
        if (variant == 0) a.target = 0;
        if (variant == 1) a.target = 4294967296ULL;
        if (variant == 2) a.target = 123;
        if (variant == 3) a.cli_valid = false;
        if (variant == 4) a.preflight_asserted = false;
        if (variant == 5) a.mode = static_cast<TokenEntryMode>(-1);
        Check(!e.Run(a), "invalid admission succeeded"); NoSdk(f); Check(f.trace.empty(), "invalid admission performed observation");
    }
}
void TI03() {
    for (int variant = 0; variant != 7; ++variant) {
        Fake f;
        if (variant == 0) f.task.opened = false;
        if (variant == 1) f.task.complete = false;
        if (variant == 2) f.task.count = 0;
        if (variant == 3) f.task.count = 2;
        if (variant == 4) f.task.sole_tid = 11;
        if (variant == 5) f.task.current_tid = 11;
        if (variant == 6) f.task.process_id = 0;
        TokenFirstEntry e(f); Check(!e.Run(Admission()), "invalid task checkpoint admitted"); NoSdk(f);
    }
}
void TI04() {
    Fake f; f.initial = 0; TokenFirstEntry e(f); Check(!e.Run(Admission()), "zero original accepted"); NoSdk(f);
}
void TI05() {
    Fake f; f.selected_rc = -1; TokenFirstEntry e(f); Check(!e.Run(Admission()), "failed Set accepted");
    RestoredWithoutProvider(f); Check(f.verifies == 0, "failed Set must not verify");
}
void TI06() {
    for (auto value : {0ULL, 123ULL}) {
        Fake f; f.selected_readback = value; TokenFirstEntry e(f); Check(!e.Run(Admission()), "bad readback admitted");
        RestoredWithoutProvider(f); Check(f.verifies == 0, "bad readback must not verify");
    }
}
void TI07() {
    for (int variant = 0; variant != 9; ++variant) {
        Fake f;
        if (variant == 0) f.observed.lookup = 123;
        if (variant == 1) f.observed.type = 99;
        if (variant == 2) f.observed.info_rc = -1;
        if (variant == 3) f.observed.process_name = "other";
        if (variant == 4) f.observed.apl = 99;
        if (variant == 5) f.observed.query_rc = -1;
        if (variant == 6) f.observed.states.clear();
        if (variant == 7) f.observed.states = {0, 0};
        if (variant == 8) f.observed.states = {-1};
        TokenFirstEntry e(f); Check(!e.Run(Admission()), "identity or permission predicate failure accepted");
        RestoredWithoutProvider(f); Check(f.verifies == 1, "unexpected fallback or duplicate verification");
        Check(f.trace[4] == "verify-after_select", "verification did not run under selected identity");
    }
}
void TI08() {
    for (int variant = 0; variant != 4; ++variant) {
        Fake f;
        if (variant == 0) f.verify_throws = true;
        if (variant == 1) f.diagnostic_throws = "select_token";
        if (variant == 2) f.selected_throws = true;
        if (variant == 3) f.selected_read_throws = true;
        TokenFirstEntry e(f); Check(!e.Run(Admission()), "exception became success"); RestoredWithoutProvider(f);
    }
}
void TI09() {
    for (int variant = 0; variant != 5; ++variant) {
        Fake f;
        if (variant == 0) f.restore_rc = -1;
        if (variant == 1) f.restore_readback = 123;
        if (variant == 2) f.diagnostic_throws = "restore_token";
        if (variant == 3) f.restore_throws = true;
        if (variant == 4) f.restore_read_throws = true;
        { TokenFirstEntry e(f); Check(!e.Run(Admission()), "restore failure became success");
          Check(!e.Restore() && !e.Restore() && !e.RestoreSucceeded(), "restore failure was not sticky"); }
        Check(f.sets == 2, "repeated cleanup/destructor restored more than once");
    }
}
void TI10() {
    for (int variant = 0; variant != 3; ++variant) {
        Fake f;
        if (variant == 0) f.provider_result = false; // Same false result used by record timeout/error.
        if (variant == 1) f.close_result = false;
        if (variant == 2) f.provider_throws = true;
        TokenFirstEntry e(f); Check(!e.Run(Admission()), "record/close failure hidden by successful restore");
        Check(e.RestoreSucceeded() && f.sets == 2, "failed record did not restore");
    }
}
void TI11() {
    Fake f; f.initial = target; f.restore_readback = target;
    TokenFirstEntry e(f); Check(e.Run(Admission()), "already-target case failed");
    Check(f.equal_logged && f.sets == 2 && f.verifies == 1, "already-target not explicitly distinguished/verified/restored");
}
void TI12() {
    Fake f; f.observed.states = {-1}; auto a = Admission(); a.mode = TokenEntryMode::ListNodes;
    TokenFirstEntry e(f); Check(!e.Run(a), "DENIED manager unexpectedly succeeded");
    Check(f.sets == 0 && f.providers == 0 && f.verifies == 1, "manager DENIED entered token-first or provider path");
    Check(f.trace == std::vector<std::string>{"get-original", "verify-before_select"}, "legacy manager order changed");
    Fake granted;
    granted.task.complete = false; // The manager path does not use record admission checkpoint.
    TokenFirstEntry manager(granted); Check(manager.Run(a), "GRANTED manager unexpectedly failed");
    Check(granted.trace == std::vector<std::string>{"get-original", "verify-before_select", "set-target",
        "get-target", "verify-after_select", "provider", "close", "restore-original", "get-restored"},
        "legacy manager GRANTED sequence changed");
    Check(!manager.Run(a) && granted.providers == 1, "entry controller reused for another operation");
}
} // namespace

int main() {
    const struct { const char* name; void (*test)(); } cases[] = {
        {"TI-01", TI01}, {"TI-02", TI02}, {"TI-03", TI03}, {"TI-04", TI04},
        {"TI-05", TI05}, {"TI-06", TI06}, {"TI-07", TI07}, {"TI-08", TI08},
        {"TI-09", TI09}, {"TI-10", TI10}, {"TI-11", TI11}, {"TI-12", TI12}};
    int failures = 0;
    for (const auto& test : cases) {
        try { test.test(); std::cout << test.name << " PASS\n"; }
        catch (const std::exception& e) { ++failures; std::cerr << test.name << " FAIL " << e.what() << '\n'; }
    }
    std::cout << "groups=12 failures=" << failures << " scope=HOST_SHARED_CONTROLLER_ONLY\n";
    return failures == 0 ? 0 : 1;
}
