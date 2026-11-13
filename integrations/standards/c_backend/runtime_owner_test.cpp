#include "runtime_owner.hpp"
#include <memory>
#include <thread>
#include <vector>
#include <stdexcept>
#include <iostream>
struct FakeRuntime {
    int attempts=0,stops=0; bool throw_close=false;
    void RequestPumpStop() { ++stops; }
    bool Close() { ++attempts; if(throw_close) throw std::runtime_error("injected close"); return attempts>=3; }
};
struct Pumped { std::unique_ptr<FakeRuntime> rt{new FakeRuntime}; std::thread pump; };
static void need(bool v) { if(!v) throw std::runtime_error("owner invariant"); }
int main() {
    Pumped r; std::vector<int> descriptors{1}; auto* original=r.rt.get();
    r.pump=std::thread([]{});
    need(!b4_detail::close_runtime(r,descriptors));
    need(r.rt.get()==original && r.rt->attempts==1 && !r.pump.joinable() && descriptors.empty());
    need(!b4_detail::close_runtime(r,descriptors));
    need(r.rt.get()==original && r.rt->attempts==2);
    need(b4_detail::close_runtime(r,descriptors));
    need(r.rt.get()==original && r.rt->attempts==3 && r.rt->stops==3);
    r.rt->throw_close=true;
    bool caught=false;
    try { (void)b4_detail::close_runtime(r,descriptors); } catch(const std::runtime_error&) { caught=true; }
    need(caught && r.rt.get()==original && r.rt->attempts==4);
    r.rt->throw_close=false;
    need(b4_detail::close_runtime(r,descriptors) && r.rt->attempts==5);
    std::cout<<"B4_OWNER PASS\n";
}
