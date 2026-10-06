#include "../../integrations/ros2/FastddsHook.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

namespace {
std::atomic<unsigned> notifications{0}, closes{0};
std::promise<void> entered, resume;
std::shared_future<void> release = resume.get_future().share();
void deleted(void*) { ++notifications; if (notifications == 1) entered.set_value(); release.wait(); }
int close_owner(void*) { ++closes; return 0; }
}
int main() {
    auto lease = std::make_shared<fastdds_dsoftbus_hook::Lease>();
    lease->owner = &closes; lease->owner_identity=&closes; lease->close = close_owner; lease->public_delete = deleted;
    void* participant = &notifications;
    fastdds_dsoftbus_hook::attach(participant,lease);
    assert(!fastdds_dsoftbus_hook::owner_created(&closes));
    fastdds_dsoftbus_hook::mark_created(lease);
    assert(fastdds_dsoftbus_hook::owner_created(&closes));
    assert(!fastdds_dsoftbus_hook::owner_created(&entered));
    std::thread first([&]{ fastdds_dsoftbus_hook::after_delete(participant); });
    assert(entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    assert(!fastdds_dsoftbus_hook::owner_created(&closes));
    auto second = std::async(std::launch::async,[&]{ return fastdds_dsoftbus_hook::after_delete(participant); });
    const auto competing = second.wait_for(std::chrono::seconds(1));
    resume.set_value(); first.join(); second.get();
    assert(competing == std::future_status::ready);
    assert(notifications == 1 && closes == 1 && lease->close_state == 2);
    assert(!fastdds_dsoftbus_hook::owner_created(&closes)&&lease->owner_identity==&closes);
    assert(fastdds_dsoftbus_hook::after_delete(participant));
    auto failed = std::make_shared<fastdds_dsoftbus_hook::Lease>();
    failed->owner = &closes; failed->close = close_owner; failed->public_delete = deleted;
    fastdds_dsoftbus_hook::fail_pending(failed);
    assert(notifications == 1 && closes == 2);
    // Mirrors the real factory's attached-but-autoenable-failed path.
    auto enabling = std::make_shared<fastdds_dsoftbus_hook::Lease>();
    enabling->owner=&closes;enabling->close=close_owner;enabling->public_delete=deleted;
    fastdds_dsoftbus_hook::attach(&entered,enabling);
    assert(fastdds_dsoftbus_hook::after_delete(&entered));
    assert(notifications == 1 && closes == 3 && !enabling->creation_complete);
    std::cout << "HOOK_CLOSE_PASS public_delete_once=1 failed_construction_not_notified=1\n";
}
