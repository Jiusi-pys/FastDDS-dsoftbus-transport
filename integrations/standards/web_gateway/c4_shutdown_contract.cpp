#include "c4_shutdown.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <thread>
int main() {
    unsigned closed=0,checked=0;
    const auto normal=c4::finish_transport([&]{++closed;return true;},[&]{++checked;return true;});
    if(!normal.success()||closed!=1||checked!=1)return 1;
    const auto close_failure=c4::finish_transport([]{return false;},[]{return true;});
    if(close_failure.closed||!close_failure.healthy||close_failure.success())return 2;
    std::atomic<bool> failed{false};
    std::promise<void> release;auto ready=release.get_future();
    std::thread worker([&]{ready.wait();failed=true;});
    // The failure is impossible before close releases the worker. A premature
    // health snapshot incorrectly returns success, deterministically.
    const auto late=c4::finish_transport([&]{release.set_value();worker.join();return true;},[&]{return !failed.load();});
    if(!late.closed||late.healthy||late.success())return 3;
    unsigned cleanup=0;
    const auto prior_failure=c4::finish_transport([&]{++cleanup;return true;},[]{return false;});
    if(!prior_failure.closed||prior_failure.healthy||prior_failure.success()||cleanup!=1)return 4;
    std::cout<<"C4_SHUTDOWN_CONTRACT PASS cases=4\n";
}
