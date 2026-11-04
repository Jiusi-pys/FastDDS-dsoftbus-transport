#include "KeyedValidator.hpp"
#include <cstdio>
using namespace fastdds_dsoftbus::experiment::keyed;
int main()
{
    unsigned checks = 0, failures = 0;
    auto check = [&](bool x, const char *n)
    {
        ++checks;
        if (!x)
        {
            ++failures;
            std::printf("FAIL %s\n", n);
        }
    };
    const Observation a{"w1", "hA", 0, 1, 17, 1, true}, b{"w1", "hB", 0, 2, 34, 1, true};
    {
        KeyedValidator v({"w1"});
        check(v.Data(a, false), "first_read");
        check(v.Data(a, false), "repeat_read_is_not_duplicate");
        check(v.Data(a, true), "read_then_take");
        check(v.Unique() == 1, "unique_identity_not_operation_count");
        check(!v.Data(a, true), "second_take_rejected");
    }
    {
        KeyedValidator v({"w1"});
        check(v.Data(a, false) && v.Data(b, false), "two_keys");
        check(v.Key("hA") == 17 && v.Key("hB") == 34, "handle_key_bijection");
        auto x = b;
        x.handle = "hA";
        check(!v.Data(x, false), "no_handle_alias");
    }
    {
        KeyedValidator v({"w1", "w2"});
        auto x = a;
        x.writer = "w2";
        check(v.Data(a, false) && v.Data(x, false) && v.Unique() == 2, "same_key_two_writers_not_deduped");
    }
    {
        KeyedValidator v({"w1"});
        auto x = a;
        x.writer = "wrong";
        check(!v.Data(x, false), "wrong_writer");
    }
    {
        KeyedValidator v({"w1"});
        auto x = a;
        x.payload_valid = false;
        check(!v.Data(x, false), "bad_payload");
    }
    {
        KeyedValidator v({"w1"});
        auto x = a;
        x.lo = 0;
        check(!v.Data(x, false), "unknown_data_identity");
    }
    {
        KeyedValidator v({"w1"});
        v.Data(a, false);
        auto x = a;
        x.key = 34;
        check(!v.Data(x, false), "identity_changes_key");
    }
    {
        KeyedValidator v({"w1"});
        v.Data(a, false);
        auto x = a;
        x.lo = 2;
        check(!v.Data(x, false), "application_resend_new_sn");
    }
    {
        KeyedValidator v({"w1"});
        check(!v.State("unknown", 2), "state_unknown_handle_not_adopted");
    }
    {
        KeyedValidator v({"w1"});
        v.Data(a, false);
        check(v.State("hA", 2) && v.State("hA", 4) && v.State("hA", 1), "state_uses_prior_valid_handle");
        check(v.Unique() == 1, "state_not_business_data");
    }
    {
        KeyedValidator v({"w1"});
        v.AllowOnly({SampleKey{"w1", 17, 1}});
        check(v.Data(a, false), "frozen_set_accepts");
        auto x = a;
        x.lo = 2;
        x.ordinal = 99;
        check(!v.Data(x, false), "extra_ordinal_after_completed_set_rejected");
    }
    check(ExpectedLifecycleState("K3", "CHANGE", 17, 2), "dispose_state_contract");
    check(ExpectedLifecycleState("K4_TRUE", "CHANGE", 17, 2), "autodispose_true_contract");
    check(ExpectedLifecycleState("K4_FALSE", "CHANGE", 17, 4), "autodispose_false_contract");
    check(!ExpectedLifecycleState("K3", "CHANGE", 34, 2), "wrong_key_state_rejected");
    check(!ExpectedLifecycleState("K3", "PRE", 17, 2), "premature_state_rejected");
    check(!ExpectedLifecycleState("K1", "CHANGE", 17, 2), "unexpected_case_state_rejected");
    check(!ExpectedLifecycleState("K4_FALSE", "CHANGE", 17, 2), "wrong_dispose_policy_rejected");
    std::printf("KEYED_VALIDATOR checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
