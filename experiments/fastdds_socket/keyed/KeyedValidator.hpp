#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
namespace mdds::experiment::keyed
{
inline bool ExpectedLifecycleState(const std::string& case_id, const std::string& phase, unsigned key,
                                   unsigned state)
{
    if (phase != "CHANGE" || key != 17)
        return false;
    if (case_id == "K3" || case_id == "K4_TRUE")
        return state == 2;
    return case_id == "K4_FALSE" && state == 4;
}
using SampleKey = std::tuple<std::string, unsigned, unsigned>;
struct Observation
{
    std::string writer, handle;
    std::int32_t hi = 0;
    std::uint32_t lo = 0, key = 0, ordinal = 0;
    bool payload_valid = true;
};
class KeyedValidator
{
  public:
    explicit KeyedValidator(std::set<std::string> writers) : writers_(std::move(writers)) {}
    void AllowOnly(std::set<SampleKey> expected)
    {
        allowed_ = std::move(expected);
        enforce_allowed_ = true;
    }
    bool Data(const Observation &x, bool take)
    {
        if (!failure_.empty())
            return false;
        if (!writers_.count(x.writer))
            return Fail("wrong_writer");
        if (!x.payload_valid)
            return Fail("payload_mismatch");
        if (x.hi < 0 || (x.hi == 0 && x.lo == 0) || x.handle.empty() || !x.key || !x.ordinal)
            return Fail("invalid_data_identity");
        auto kh = key_handle_.find(x.key);
        auto hk = handle_key_.find(x.handle);
        if ((kh != key_handle_.end() && kh->second != x.handle) ||
            (hk != handle_key_.end() && hk->second != x.key))
            return Fail("instance_handle_key_alias");
        const auto id = std::make_tuple(x.writer, x.hi, x.lo);
        const SampleKey sample{x.writer, x.key, x.ordinal};
        if (enforce_allowed_ && !allowed_.count(sample))
            return Fail("sample_outside_frozen_set");
        const auto old = identities_.find(id);
        if (old != identities_.end() && old->second != sample)
            return Fail("sample_identity_changed");
        const auto prior = samples_.find(sample);
        if (prior != samples_.end() && prior->second != id)
            return Fail("application_resend");
        if (taken_.count(id))
            return Fail("sample_after_take");
        key_handle_[x.key] = x.handle;
        handle_key_[x.handle] = x.key;
        identities_[id] = sample;
        samples_[sample] = id;
        seen_.insert(sample);
        if (take)
            taken_.insert(id);
        return true;
    }
    bool State(const std::string &handle, unsigned state)
    {
        if (!handle_key_.count(handle))
            return Fail("state_without_prior_valid_handle");
        if (state != 1 && state != 2 && state != 4)
            return Fail("invalid_instance_state");
        return failure_.empty();
    }
    std::string Failure() const { return failure_; }
    const std::set<SampleKey> &Seen() const { return seen_; }
    std::size_t Unique() const { return seen_.size(); }
    unsigned Key(const std::string &h) const
    {
        const auto i = handle_key_.find(h);
        return i == handle_key_.end() ? 0 : i->second;
    }

  private:
    bool Fail(const char *message)
    {
        if (failure_.empty())
            failure_ = message;
        return false;
    }
    using Identity = std::tuple<std::string, std::int32_t, std::uint32_t>;
    std::set<std::string> writers_;
    std::string failure_;
    std::set<SampleKey> seen_, allowed_;
    bool enforce_allowed_ = false;
    std::map<std::string, unsigned> handle_key_;
    std::map<unsigned, std::string> key_handle_;
    std::map<Identity, SampleKey> identities_;
    std::map<SampleKey, Identity> samples_;
    std::set<Identity> taken_;
};
} // namespace mdds::experiment::keyed
