// Binding authority tests only; ordinary ROS execution is a separate gate.
#include "../../integrations/ros2/ReaderPayloadBinding.hpp"
#include <cassert>
#include <fstream>
#include <cstdio>
using namespace fastdds_dsoftbus::payload_resource;
using eprosima::fastrtps::rtps::IPayloadPool;
using eprosima::fastrtps::rtps::CacheChange_t;
void environment(const char* value) {
#if defined(_WIN32)
    _putenv_s("FASTDDS_DSOFTBUS_REQUIRED_CONFIG",value?value:"");
#else
    if(value)setenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG",value,1);else unsetenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG");
#endif
}
int main(int argc,char** argv) {
    assert(argc==2);const std::string path=argv[1];
    const std::string config="payload_guard_profile=TEST_ONLY_BOUNDED\npayload_max_sample_bytes=64\npayload_pool_budget_bytes=128\npayload_max_allocations=2\n";
    {std::ofstream out(path);out<<config;}environment(path.c_str());
    std::shared_ptr<IPayloadPool> a,b;assert(select_reader_pool(a));assert(select_reader_pool(b));
    auto pa=std::dynamic_pointer_cast<Pool>(a),pb=std::dynamic_pointer_cast<Pool>(b);
    assert(pa&&pb&&pa!=pb&&pa->budget()==pb->budget());
    CacheChange_t held;assert(pa->get_payload(64,held));auto budget=pa->budget();
    // Arbitrary public custom-pool arguments cannot bypass the protected seam.
    std::shared_ptr<IPayloadPool> foreign=pb;assert(!select_reader_pool(foreign));
    environment(nullptr);std::shared_ptr<IPayloadPool> bypass;assert(!select_reader_pool(bypass));
    environment(path.c_str());{std::ofstream out(path);out<<config<<"unrelated_drift=value\n";}
    assert(!select_reader_pool(bypass));assert(budget->snapshot().retained_bytes==70);
    pa.reset();a.reset(); // Raw payload owner still protects its domain and itself.
    assert(!select_reader_pool(bypass));assert(held.payload_owner()->release_payload(held));
    assert(budget->snapshot().retained_bytes==0);
    assert(!select_reader_pool(bypass)); // Full retirement does not permit configuration drift.
    {std::ofstream out(path);out<<config;}assert(select_reader_pool(bypass));
    std::puts("PAYLOAD_BINDING_AUTHORITY result=PASS scope=CONFIG_COMPONENT_NOT_ROS");
}
