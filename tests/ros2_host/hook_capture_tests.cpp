#include "../../integrations/ros2/FastddsHook.hpp"
#include <cassert>
#include <future>
#include <thread>
using namespace fastdds_dsoftbus_hook;
namespace {
std::promise<void> entered,released;auto release=released.get_future().share();
std::atomic<unsigned> closed{0};
void barrier(){entered.set_value();release.wait();}
int close_owner(void*){++closed;return 0;}
}
int main(int argc,char** argv){
    assert(argc==2);auto lease=std::make_shared<Lease>();lease->module=fastdds_dsoftbus_hook_platform::load(argv[1]);assert(lease->module);
    auto set=reinterpret_cast<void(*)(void(*)())>(fastdds_dsoftbus_hook_platform::symbol(lease->module,"PlatformTestCopyBarrier"));assert(set);set(barrier);
    lease->capture=reinterpret_cast<Capture>(fastdds_dsoftbus_hook_platform::symbol(lease->module,"PlatformTestCapture"));assert(lease->capture);
    lease->close=close_owner;lease->owner=&closed;attach(&closed,lease);
    fastdds_dsoftbus::ros2_diagnostics::Snapshot snapshot;int result=-1;
    std::thread copy([&]{result=capture_startup(&snapshot);});assert(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready);
    std::thread closer([&]{after_delete(&closed);});
    while(lease->close_state.load()==0)std::this_thread::yield();
    // Owner close may complete: the getter must not touch it. Module unload
    // remains serialized with the still-running fixed-cache copy.
    fastdds_dsoftbus::ros2_diagnostics::Snapshot late;assert(capture_startup(&late)!=0);
    released.set_value();copy.join();closer.join();assert(result==0&&snapshot.initialized&&closed==1&&lease->close_state==2);assert(capture_startup(&late)!=0);
}
