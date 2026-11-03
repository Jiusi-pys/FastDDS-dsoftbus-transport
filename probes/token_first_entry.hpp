#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastdds_dsoftbus_probe {

enum class TokenEntryMode { Record, ListNodes };

struct TokenAdmission {
    TokenEntryMode mode = TokenEntryMode::Record;
    bool cli_valid = false;
    bool preflight_asserted = false; // Operator assertion, not internally verified authorization.
    std::uint64_t target = 0;
};

bool ParseTokenArgument(std::string_view text, std::uint64_t& token) noexcept;
bool AdmitTokenEntry(const TokenAdmission& admission) noexcept;

struct TaskCheckpoint {
    bool opened = false;
    bool complete = false;
    std::uint64_t process_id = 0;
    std::uint64_t current_tid = 0;
    std::size_t count = 0;
    std::uint64_t sole_tid = 0;
};

bool IsSingleMainTask(const TaskCheckpoint& checkpoint) noexcept;

// Raw manager results and explicit SDK enum values keep the real decision
// predicate host-testable without substituting a precomputed 'verified' flag.
struct OwnedTokenObservation {
    std::uint64_t lookup = 0;
    int type = -1;
    int native_type = 0;
    int info_rc = -1;
    std::string process_name;
    int apl = -1;
    int normal_apl = 0;
    int query_rc = -1;
    std::vector<std::int32_t> states;
    int permission_granted = 0;
};

bool IsOwnedNormalGranted(std::uint64_t target, const OwnedTokenObservation& observation) noexcept;

class TokenEntryOperations {
public:
    virtual ~TokenEntryOperations() = default;
    virtual TaskCheckpoint ObserveTasks() = 0;
    virtual std::uint64_t GetSelf() = 0;
    virtual int SetSelf(std::uint64_t token) = 0;
    virtual OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) = 0;
    virtual void Diagnostic(const char* stage, std::uint64_t first,
            std::uint64_t second, int code) = 0;
    virtual bool EnterProvider() = 0;
    virtual bool CloseProvider() = 0;
};

// One short-lived experiment on the calling/main thread. This does not switch
// existing SDK workers or establish SDK quiescence. Operations outlive this guard.
class TokenFirstEntry {
public:
    explicit TokenFirstEntry(TokenEntryOperations& operations) noexcept;
    ~TokenFirstEntry();
    TokenFirstEntry(const TokenFirstEntry&) = delete;
    TokenFirstEntry& operator=(const TokenFirstEntry&) = delete;
    bool Run(const TokenAdmission& admission) noexcept;
    bool Restore() noexcept;
    bool RestoreSucceeded() const noexcept { return restored_; }

private:
    bool Initialize(const TokenAdmission& admission);
    TokenEntryOperations& operations_;
    std::uint64_t original_ = 0;
    bool used_ = false;
    bool attempted_ = false;
    bool restoration_attempted_ = false;
    bool restored_ = true;
};

} // namespace fastdds_dsoftbus_probe
