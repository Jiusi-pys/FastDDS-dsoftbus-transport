#pragma once
// Injected only at the existing DDS Subscriber configuration seam, before topic
// reference/Reader construction. No RMW, RTPS protocol or callback patch.
#include "ReaderPayloadGuard.hpp"
#include "PayloadResourceConfig.hpp"
#include <atomic>
#include <cstdio>
namespace fastdds_dsoftbus { namespace payload_resource {
struct Authority {std::mutex mutex;bool latched=false;Config config;std::string path;std::weak_ptr<Budget> budget;std::uint64_t next_reader=0;};
inline Authority& authority(){static Authority value;return value;}
inline bool select_reader_pool(std::shared_ptr<eprosima::fastrtps::rtps::IPayloadPool>& current) {
    const char* path=std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG");
    if(!path) {
        auto& a=authority();std::lock_guard<std::mutex> lock(a.mutex);
        if(a.latched&&a.config.enabled){std::fputs("FDDST_PAYLOAD_POOL_BIND_REJECTED reason=required_config_removed\n",stderr);return false;}
        return true;
    }
    try {
        const auto config=read(path);auto& a=authority();std::lock_guard<std::mutex> lock(a.mutex);
        if(a.latched&&(a.path!=path||a.config.content!=config.content))throw std::invalid_argument("payload authority/config drift");
        if(!a.latched){a.latched=true;a.path=path;a.config=config;}
        if(!config.enabled)return true;
        if(current)throw std::invalid_argument("foreign/custom payload pool bypass");
        auto budget=a.budget.lock();if(!budget){budget=std::make_shared<Budget>(Limits{config.m,config.b,config.n});a.budget=budget;}
        if(a.next_reader==UINT64_MAX)throw std::invalid_argument("payload reader scope exhausted");
        auto pool=Pool::create(budget,++a.next_reader);current=pool;
        std::fprintf(stderr,"FDDST_PAYLOAD_POOL_BOUND scope=%llu pool=%p domain=%p M=%u B=%llu N=%u config=%016llx profile=TEST_ONLY_BOUNDED\n",
            static_cast<unsigned long long>(pool->reader_scope()),static_cast<void*>(pool.get()),static_cast<void*>(budget.get()),config.m,
            static_cast<unsigned long long>(config.b),config.n,static_cast<unsigned long long>(config.identity));std::fflush(stderr);
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"FDDST_PAYLOAD_POOL_BIND_REJECTED reason=%s\n",e.what());std::fflush(stderr);return false;}
    catch(...){std::fputs("FDDST_PAYLOAD_POOL_BIND_REJECTED reason=allocation_or_unknown\n",stderr);return false;}
}
inline void reader_created(const std::shared_ptr<eprosima::fastrtps::rtps::IPayloadPool>& current,const void* reader,const char* topic) {
    const auto pool=std::dynamic_pointer_cast<Pool>(current);if(!pool)return;
    std::fprintf(stderr,"FDDST_PAYLOAD_READER_CREATED reader=%p scope=%llu pool=%p topic=%s\n",reader,
        static_cast<unsigned long long>(pool->reader_scope()),static_cast<void*>(pool.get()),topic);std::fflush(stderr);
}
inline bool snapshot(Snapshot& output) {
    auto& a=authority();std::shared_ptr<Budget> budget;
    {std::lock_guard<std::mutex> lock(a.mutex);budget=a.budget.lock();if(!a.latched||!a.config.enabled)return false;}
    output=budget?budget->snapshot():Snapshot{};return true;
}
} }
