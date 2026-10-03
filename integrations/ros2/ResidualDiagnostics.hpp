#pragma once
// Fixed value copies of live DDS containers. No resource or reference ownership.
#include "StartupDiagnostics.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <algorithm>
namespace fastdds_dsoftbus { namespace ros2_teardown {
constexpr std::size_t residual_object_capacity=32, residual_snapshot_capacity=8;
struct ResidualObject {
    unsigned kind=0, sample_order=0;
    std::uint64_t object=0, implementation=0;
    bool referenced_known=false, referenced=false, name_known=false, type_known=false, text_truncated=false;
    char name[128]{},type_name[128]{};
};
struct ResidualSnapshot {
    std::uint64_t id=0,participant=0,delete_call=0;
    std::uint64_t counts[3]{},topic_factories=0,filtered_topics=0;
    unsigned known_mask=0;
    bool return_known=false,capture_failed=false;
    std::int64_t selected_return=0;
    std::size_t stored=0;
    char reason[32]{};
    ResidualObject objects[residual_object_capacity]{};
};
static_assert(std::is_trivially_copyable<ResidualSnapshot>::value,"Residual capture must own only bounded values");
struct ResidualStore {
    std::mutex lock;
    ResidualSnapshot values[residual_snapshot_capacity]{},exports[residual_snapshot_capacity]{};
    std::size_t count=0,exported=0;
    std::uint64_t attempts=0,dropped=0;
};
inline std::atomic<ResidualStore*>& residual_slot(){static std::atomic<ResidualStore*> slot{nullptr};return slot;}
inline bool InitializeResiduals() noexcept {
    if(residual_slot().load(std::memory_order_acquire))return true;
    try {
        auto* value=new ResidualStore;
        ResidualStore* expected=nullptr;
        if(!residual_slot().compare_exchange_strong(expected,value,std::memory_order_release,std::memory_order_acquire))delete value;
        return true;
    }catch(...){return false;}
}
inline void CopyResidualText(char* out,std::size_t capacity,const char* data,std::size_t length,bool& truncated) noexcept {
    const auto n=(std::min)(length,capacity-1);std::memcpy(out,data,n);out[n]=0;truncated|=length>=capacity;
}
inline bool RecordResidual(ResidualSnapshot& value) noexcept {
    auto* store=residual_slot().load(std::memory_order_acquire);if(!store)return false;
    try {std::lock_guard<std::mutex> lock(store->lock);value.id=++store->attempts;
        if(store->count==residual_snapshot_capacity){++store->dropped;return false;}
        store->values[store->count++]=value;return true;
    }catch(...){return false;}
}
inline bool CaptureResidual(std::size_t index,ResidualSnapshot* out) noexcept {
    auto* store=residual_slot().load(std::memory_order_acquire);if(!store||!out)return false;
    try {std::lock_guard<std::mutex> lock(store->lock);if(index>=store->count)return false;*out=store->values[index];return true;}catch(...){return false;}
}
inline bool WriteResiduals(const char* run,unsigned side,long process,ros2_diagnostics::Writer writer,void* context) noexcept {
    auto* store=residual_slot().load(std::memory_order_acquire);if(!store||!writer)return false;
    std::size_t first=0,last=0;std::uint64_t dropped=0;
    try {
        // Called only by the existing exclusive FlushControl exporter. Copy
        // under this leaf lock; all container locks have already been released.
        {std::lock_guard<std::mutex> lock(store->lock);first=store->exported;last=store->count;dropped=store->dropped;
            for(std::size_t i=first;i<last;++i)store->exports[i]=store->values[i];}
        bool ok=true;char line[1024];
        for(std::size_t i=first;i<last;++i){const auto& v=store->exports[i];
            char counts[3][24],returned[24],factories[24],filtered[24],total_text[24];
            for(unsigned k=0;k<3;++k)if(v.known_mask&(1u<<k))std::snprintf(counts[k],sizeof(counts[k]),"%llu",static_cast<unsigned long long>(v.counts[k]));else std::strcpy(counts[k],"NOT_OBSERVED");
            if(v.return_known)std::snprintf(returned,sizeof(returned),"%lld",static_cast<long long>(v.selected_return));else std::strcpy(returned,"NOT_OBSERVED");
            const auto total=v.counts[0]+v.counts[1]+v.counts[2];
            bool text_truncated=false;for(std::size_t j=0;j<v.stored;++j)text_truncated|=v.objects[j].text_truncated;
            const bool truncated=total>v.stored||text_truncated;
            if(v.known_mask&4){std::snprintf(factories,sizeof(factories),"%llu",static_cast<unsigned long long>(v.topic_factories));std::snprintf(filtered,sizeof(filtered),"%llu",static_cast<unsigned long long>(v.filtered_topics));}
            else {std::strcpy(factories,"NOT_OBSERVED");std::strcpy(filtered,"NOT_OBSERVED");}
            if(v.known_mask==7)std::snprintf(total_text,sizeof(total_text),"%llu",static_cast<unsigned long long>(total));else std::strcpy(total_text,"NOT_OBSERVED");
            int n=std::snprintf(line,sizeof(line),"ROS_RESIDUAL_SUMMARY schema=1 run=%s side=%c pid=%ld snapshot=%llu participant=%llu delete_call=%llu reason=%s pubs=%s subs=%s topics=%s topic_factories=%s filtered_topics=%s object_stored=%zu object_total=%s object_limit=%zu truncated=%d selected_return=%s return_scope=SELECTED_BRANCH_OR_NOT_OBSERVED sampling=SEQUENTIAL_NONATOMIC completeness=%s\n",run,side?'B':'A',process,
                static_cast<unsigned long long>(v.id),static_cast<unsigned long long>(v.participant),static_cast<unsigned long long>(v.delete_call),v.reason,counts[0],counts[1],counts[2],factories,filtered,v.stored,total_text,residual_object_capacity,int(truncated),returned,v.known_mask==7&&!v.capture_failed&&!truncated?"CAPTURED":"INCOMPLETE");
            ok=n>0&&static_cast<std::size_t>(n)<sizeof(line)&&writer(line,static_cast<std::size_t>(n),context)&&ok;
            for(std::size_t j=0;j<v.stored;++j){const auto& e=v.objects[j];char name[255],type[255];
                auto hex=[](const char* text,char* out){std::size_t k=0;while(k<127&&text[k]){const unsigned b=static_cast<unsigned char>(text[k]);out[k*2]="0123456789abcdef"[b>>4];out[k*2+1]="0123456789abcdef"[b&15];++k;}out[k*2]=0;if(!k)std::strcpy(out,"-");};
                hex(e.name,name);hex(e.type_name,type);
                n=std::snprintf(line,sizeof(line),"ROS_RESIDUAL_OBJECT schema=1 run=%s side=%c pid=%ld snapshot=%llu ordinal=%zu kind=%u sample_order=%u object=%llu implementation=%llu name_known=%d name_hex=%s type_known=%d type_hex=%s referenced=%s text_truncated=%d reference_scope=FAST_DDS_ENDPOINT_REFERENCE_BOOL\n",run,side?'B':'A',process,static_cast<unsigned long long>(v.id),j+1,e.kind,e.sample_order,static_cast<unsigned long long>(e.object),static_cast<unsigned long long>(e.implementation),int(e.name_known),name,int(e.type_known),type,e.referenced_known?(e.referenced?"1":"0"):"NOT_OBSERVED",int(e.text_truncated));
                ok=n>0&&static_cast<std::size_t>(n)<sizeof(line)&&writer(line,static_cast<std::size_t>(n),context)&&ok;
            }
        }
        if(dropped){const int n=std::snprintf(line,sizeof(line),"ROS_RESIDUAL_CAPTURE schema=1 run=%s side=%c pid=%ld omitted=%llu completeness=INCOMPLETE\n",run,side?'B':'A',process,static_cast<unsigned long long>(dropped));ok=n>0&&static_cast<std::size_t>(n)<sizeof(line)&&writer(line,static_cast<std::size_t>(n),context)&&ok;}
        if(ok){std::lock_guard<std::mutex> lock(store->lock);store->exported=last;}
        return ok;
    }catch(...){return false;}
}
}}
