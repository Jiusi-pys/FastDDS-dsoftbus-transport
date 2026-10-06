#include "../../integrations/ros2/FastddsHook.hpp"
#include <cassert>

namespace hook = fastdds_dsoftbus_hook;
static int calls = 0;
static int result = 0;
static int close_owner(void*) { ++calls; return result; }
static std::shared_ptr<hook::Lease> lease()
{
    auto value = std::make_shared<hook::Lease>();
    value->owner = reinterpret_cast<void*>(1);
    value->close = close_owner;
    return value;
}
int main()
{
    auto unpublished = lease();
    try {
        hook::PendingLeaseGuard guard(unpublished);
        throw 1;
    } catch(int) {}
    assert(hook::unpublished_failure() == unpublished && unpublished->owner);
    assert(calls == 0);
    auto published = lease();
    { hook::PendingLeaseGuard guard(published); guard.disarm(); }
    assert(hook::unpublished_failure() == unpublished && calls == 0);
    // These exercise the actual injected hook, not a summary of its outcomes.
    auto uncertain = lease(); result = 1;
    hook::attach(reinterpret_cast<void*>(2), uncertain);
    hook::mark_created(uncertain);
    assert(!hook::after_delete(reinterpret_cast<void*>(2)));
    assert(uncertain->owner && uncertain->close_state == 3);
    assert(hook::leases().count(reinterpret_cast<void*>(2)) == 1);
    const int once = calls;
    assert(!hook::after_delete(reinterpret_cast<void*>(2)) && calls == once);

    auto failed = lease(); result = 2;
    hook::attach(reinterpret_cast<void*>(3), failed);
    hook::mark_created(failed);
    assert(!hook::after_delete(reinterpret_cast<void*>(3)));
    assert(!failed->owner && failed->close_state == 2);
    assert(!hook::leases().count(reinterpret_cast<void*>(3)));

    auto healthy = lease(); result = 0;
    hook::attach(reinterpret_cast<void*>(4), healthy);
    hook::mark_created(healthy);
    assert(hook::after_delete(reinterpret_cast<void*>(4)));
    assert(!healthy->owner && !hook::leases().count(reinterpret_cast<void*>(4)));

    auto pending = lease(); result = 1;
    hook::fail_pending(pending);
    assert(hook::unpublished_failure() == pending && pending->owner);
    assert(calls == 4);
    std::puts("HOOK_LIFECYCLE_PASS healthy=1 resource_error=1 retained_uncertain=1 pending_retained=1");
}
