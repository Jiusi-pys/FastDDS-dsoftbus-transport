// Real product pool contract tests; no SDK/HDC/network and no giant allocation.
#include "../../integrations/ros2/ReaderPayloadGuard.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#if defined(PAYLOAD_TEST_WRAP_MALLOC)
static bool fail_next_payload=false;
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __wrap_malloc(std::size_t size) {if(fail_next_payload){fail_next_payload=false;return nullptr;}return __real_malloc(size);}
#endif
using namespace fastdds_dsoftbus::payload_resource;
using eprosima::fastrtps::rtps::CacheChange_t;
using eprosima::fastrtps::rtps::SerializedPayload_t;
using eprosima::fastrtps::rtps::IPayloadPool;
int main() {
    auto budget=std::make_shared<Budget>(Limits{64,140,2});
    auto a=Pool::create(budget,1),b=Pool::create(budget,2);
    CacheChange_t one,two,over;
    assert(a->get_payload(64,one));assert(one.serializedPayload.max_size==64);
    assert(!b->get_payload(65,over));assert(budget->snapshot().allocator_calls==1);
    assert(b->get_payload(64,two));assert(!a->get_payload(1,over));
    assert(budget->snapshot().retained_bytes==140&&budget->snapshot().live_records==2);
    one.serializedPayload.length=64;IPayloadPool* owner=a.get();
    CacheChange_t loan;assert(a->get_payload(one.serializedPayload,owner,loan));
    assert(budget->snapshot().allocator_calls==2&&loan.serializedPayload.data==one.serializedPayload.data);
    assert(a->release_payload(one));assert(budget->snapshot().retained_bytes==140);
    assert(a->release_payload(loan));assert(budget->snapshot().retained_bytes==70);
    assert(b->release_payload(two));assert(budget->snapshot().retained_bytes==0);
    // Receiver-owned borrowed bytes must never be freed as if they were ours.
    unsigned char wire[16]{};SerializedPayload_t incoming;incoming.data=wire;incoming.length=16;incoming.max_size=16;
    IPayloadPool* rx_owner=nullptr;CacheChange_t copied;
    assert(a->get_payload(incoming,rx_owner,copied));assert(rx_owner==a.get()&&incoming.data!=wire);
    CacheChange_t receiver;receiver.serializedPayload.data=incoming.data;receiver.serializedPayload.max_size=incoming.max_size;receiver.payload_owner(rx_owner);
    incoming.data=nullptr;assert(a->release_payload(copied));assert(budget->snapshot().retained_bytes==22);
    assert(a->release_payload(receiver));assert(budget->snapshot().retained_bytes==0);
    CacheChange_t foreign_source,foreign_copy;assert(b->get_payload(16,foreign_source));
    foreign_source.serializedPayload.length=16;owner=b.get();auto* original=foreign_source.serializedPayload.data;
    assert(a->get_payload(foreign_source.serializedPayload,owner,foreign_copy));
    assert(owner==b.get()&&foreign_source.serializedPayload.data==original&&foreign_copy.serializedPayload.data!=original);
    assert(a->release_payload(foreign_copy));assert(b->release_payload(foreign_source));
    assert(budget->snapshot().retained_bytes==0);
    auto count_budget=std::make_shared<Budget>(Limits{64,256,1});auto count_pool=Pool::create(count_budget,4);
    assert(count_pool->get_payload(8,one));assert(!count_pool->get_payload(8,two));
    assert(count_budget->snapshot().count_rejections==1&&count_budget->snapshot().allocator_calls==1);
    assert(count_pool->release_payload(one));
#if defined(PAYLOAD_TEST_WRAP_MALLOC)
    auto calls=budget->snapshot().allocator_calls;fail_next_payload=true;
    assert(!a->get_payload(8,over));assert(budget->snapshot().allocation_failures==1);
    assert(budget->snapshot().allocator_calls==calls+1&&budget->snapshot().retained_bytes==0&&budget->snapshot().live_records==0);
    assert(a->get_payload(8,over));assert(a->release_payload(over));
#endif
    // Legal changed-geometry reuse writes beyond advertised size8, but remains
    // inside the charged14-byte allocation. Actual upstream bookkeeping runs.
    CacheChange_t geometry;assert(a->get_payload(8,geometry));
    geometry.serializedPayload.length=8;geometry.setFragmentSize(4);geometry.setFragmentSize(5);
    assert(geometry.serializedPayload.max_size==8&&budget->snapshot().retained_bytes==14);
    assert(a->release_payload(geometry));
    // A surviving raw-owner reference keeps pool code/object alive.
    CacheChange_t held;assert(a->get_payload(8,held));auto weak=std::weak_ptr<Pool>(a);a.reset();
    assert(!weak.expired());assert(held.payload_owner()->release_payload(held));assert(weak.expired());
    // Two reader wrappers race ONE domain, never separate remaining budgets.
    auto c=Pool::create(budget,3);std::atomic<unsigned> granted{0};std::atomic<bool> release{false};
    auto contender=[&](std::shared_ptr<Pool> pool){CacheChange_t item;if(pool->get_payload(64,item)){++granted;while(!release.load())std::this_thread::yield();pool->release_payload(item);}};
    std::thread x(contender,b),y(contender,c);while(granted.load()<2)std::this_thread::yield();
    assert(budget->snapshot().retained_bytes==140);assert(!c->get_payload(1,over));release=true;x.join();y.join();
    assert(budget->snapshot().retained_bytes==0&&budget->snapshot().live_records==0);
    // Double-spend race: eight Reader wrappers hammer ONE budget that fits exactly three 64-byte payloads
    // (charge 70 each, B=210). If check and reservation were not one atomic step, more than three could be
    // held at once or the high-water mark would pass B. Releases settle exactly once.
    {
        auto race=std::make_shared<Budget>(Limits{64,210,1000});
        std::atomic<int> held{0},max_held{0};std::atomic<unsigned> grants{0};
        std::vector<std::thread> workers;
        for(unsigned t=0;t<8;++t)workers.emplace_back([&,t]{
            auto reader=Pool::create(race,100+t);
            for(unsigned i=0;i<2000;++i){
                CacheChange_t item;
                if(!reader->get_payload(64,item))continue;
                const int now=++held;int seen=max_held.load();
                while(now>seen&&!max_held.compare_exchange_weak(seen,now)){}
                ++grants;std::this_thread::yield();--held;
                const bool released=reader->release_payload(item);
                assert(released);(void)released;
            }
        });
        for(auto& w:workers)w.join();
        const auto after=race->snapshot();
        assert(max_held.load()<=3&&grants.load()>0);
        assert(after.high_water_bytes<=210&&after.retained_bytes==0&&after.live_records==0&&after.invalid_releases==0);
        assert(after.final_frees==grants.load());
        std::printf("PAYLOAD_RESOURCE_RACE grants=%u max_simultaneous=%d budget_rejections=%llu high_water=%llu\n",
            grants.load(),max_held.load(),static_cast<unsigned long long>(after.budget_rejections),
            static_cast<unsigned long long>(after.high_water_bytes));
    }
    std::puts("PAYLOAD_RESOURCE_POOL result=PASS scope=REAL_POOL_COMPONENT_NOT_ROS_DEVICE");
}
