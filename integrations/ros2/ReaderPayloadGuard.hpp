#pragma once
// Public IPayloadPool resource policy, not a transport/protocol implementation.
// B counts physical payload capacity including six hidden tail bytes; allocator bookkeeping, cache metadata,
// CDR objects, builtin/crypto and SDK buffers are separate scopes.
#include <fastdds/rtps/history/IPayloadPool.h>
#include <fastdds/rtps/common/CacheChange.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace fastdds_dsoftbus { namespace payload_resource {
struct Limits {
    std::uint32_t max_sample_bytes;
    std::uint64_t budget_bytes;
    std::uint32_t max_allocations;
    bool operator==(const Limits& x) const noexcept {return max_sample_bytes==x.max_sample_bytes&&budget_bytes==x.budget_bytes&&max_allocations==x.max_allocations;}
};
struct Snapshot {
    std::uint64_t active_bytes=0,retained_bytes=0,cached_payload_bytes=0,live_records=0;
    std::uint64_t allocator_calls=0,allocation_failures=0,size_rejections=0,budget_rejections=0,count_rejections=0;
    std::uint64_t final_frees=0,invalid_releases=0;
    std::uint64_t high_water_bytes=0;
    std::uint32_t last_request_bytes=0,last_charge_bytes=0,last_rejection=0; // 1=M, 2=B, 3=N, 4=allocator.
};
class Pool;
class Budget {
    friend class Pool;
    struct Slot {unsigned char* data=nullptr;std::uint32_t capacity=0,charged=0;std::uint64_t references=0;Pool* owner=nullptr;};
    Limits limits_;
    mutable std::mutex mutex_;
    std::vector<Slot> slots_; // Lazy, reused bounded metadata; no payload cache.
    Snapshot state_;
public:
    explicit Budget(Limits limits):limits_(limits) {
        // The older fragment count expression adds a uint16 fragment size.
        if(limits.max_sample_bytes<4||limits.max_sample_bytes>UINT32_MAX-UINT16_MAX||
                limits.budget_bytes<limits.max_sample_bytes||!limits.max_allocations||
                limits.max_allocations>std::numeric_limits<std::size_t>::max()/sizeof(Slot))
            throw std::invalid_argument("payload resource limits");
    }
    Limits limits() const noexcept{return limits_;}
    Snapshot snapshot() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
};
class Pool final:public eprosima::fastrtps::rtps::IPayloadPool,public std::enable_shared_from_this<Pool> {
    using Change=eprosima::fastrtps::rtps::CacheChange_t;
    using Payload=eprosima::fastrtps::rtps::SerializedPayload_t;
    std::shared_ptr<Budget> budget_;
    std::uint64_t reader_scope_;
    std::uint32_t owned_records_=0;
    std::shared_ptr<Pool> self_pin_; // Exists iff a physical buffer is still live.
    Pool(std::shared_ptr<Budget> budget,std::uint64_t reader):budget_(std::move(budget)),reader_scope_(reader){}
    Budget::Slot* find(unsigned char* data) noexcept {
        for(auto& slot:budget_->slots_)if(slot.data==data&&slot.owner==this)return &slot;
        return nullptr;
    }
    static void clear(Change& c) noexcept {
        c.serializedPayload.data=nullptr;c.serializedPayload.length=0;c.serializedPayload.max_size=0;c.serializedPayload.pos=0;c.payload_owner(nullptr);
    }
    static void assign(Change& c,const Budget::Slot& slot,Pool* owner,std::uint32_t length,std::uint16_t encapsulation) noexcept {
        c.serializedPayload.data=slot.data;c.serializedPayload.max_size=slot.capacity;c.serializedPayload.length=length;
        c.serializedPayload.encapsulation=encapsulation;c.serializedPayload.pos=0;c.payload_owner(owner);
    }
public:
    static std::shared_ptr<Pool> create(const std::shared_ptr<Budget>& budget,std::uint64_t reader) {
        if(!budget||!reader)throw std::invalid_argument("payload budget/scope missing");
        return std::shared_ptr<Pool>(new Pool(budget,reader));
    }
    std::uint64_t reader_scope() const noexcept{return reader_scope_;}
    const std::shared_ptr<Budget>& budget() const noexcept{return budget_;}
    bool get_payload(std::uint32_t size,Change& change) override {
        std::lock_guard<std::mutex> lock(budget_->mutex_);
        auto& s=budget_->state_;const auto& l=budget_->limits_;
        s.last_request_bytes=size;s.last_charge_bytes=0;s.last_rejection=0;
        // 2.14.7 StatelessReader can reuse a buffer with changed fragment
        // geometry without calling the pool again. CacheChange's in-payload
        // fragment index may touch up to six bytes beyond raw sampleSize.
        // Keep that physical headroom hidden from max_size, and charge it to B.
        if(change.payload_owner()!=nullptr)return false;
        if(!size||size>l.max_sample_bytes){++s.size_rejections;s.last_rejection=1;return false;}
        const std::uint32_t charged=size+6u; // M validation leaves ample uint32 headroom.
        if(charged>l.budget_bytes-s.retained_bytes){++s.budget_rejections;s.last_rejection=2;return false;}
        if(s.live_records>=l.max_allocations){++s.count_rejections;s.last_rejection=3;return false;}
        std::size_t index=0;while(index<budget_->slots_.size()&&budget_->slots_[index].data)++index;
        try {
            if(index==budget_->slots_.size())budget_->slots_.emplace_back();
            if(!owned_records_)self_pin_=shared_from_this();
        }catch(...){++s.allocation_failures;s.last_rejection=4;return false;}
        // One critical section covers reservation, allocation and publication.
        s.retained_bytes+=charged;s.active_bytes+=charged;++s.live_records;++s.allocator_calls;
        auto* bytes=static_cast<unsigned char*>(std::malloc(charged));
        if(!bytes){s.retained_bytes-=charged;s.active_bytes-=charged;--s.live_records;++s.allocation_failures;s.last_rejection=4;if(!owned_records_)self_pin_.reset();return false;}
        s.last_charge_bytes=charged;if(s.retained_bytes>s.high_water_bytes)s.high_water_bytes=s.retained_bytes;
        auto& slot=budget_->slots_[index];slot.data=bytes;slot.capacity=size;slot.charged=charged;slot.references=1;slot.owner=this;++owned_records_;
        assign(change,slot,this,0,change.serializedPayload.encapsulation);return true;
    }
    bool get_payload(Payload& data,eprosima::fastrtps::rtps::IPayloadPool*& data_owner,Change& change) override {
        if(change.payload_owner()!=nullptr)return false;
        if(data_owner==this) {
            std::lock_guard<std::mutex> lock(budget_->mutex_);auto* slot=find(data.data);
            // Trusted same-owner loan acquisition never competes for B/N.
            if(!slot||data.length>slot->capacity||slot->references==UINT64_MAX)return false;
            ++slot->references;assign(change,*slot,this,data.length,data.encapsulation);return true;
        }
        if(data.length>data.max_size||(!data.data&&data.length))return false;
        if(!get_payload(data.length,change))return false;
        std::memcpy(change.serializedPayload.data,data.data,data.length);
        change.serializedPayload.length=data.length;change.serializedPayload.encapsulation=data.encapsulation;
        if(data_owner==nullptr) {
            std::lock_guard<std::mutex> lock(budget_->mutex_);auto* slot=find(change.serializedPayload.data);
            if(!slot||slot->references==UINT64_MAX)return false; // Defensive: never dereference a missing slot.
            ++slot->references; // Receiver owns a separate reference until fanout ends.
            data.data=slot->data;data.max_size=slot->capacity;data_owner=this;
        } // Foreign owner and all of its input fields remain untouched.
        return true;
    }
    bool release_payload(Change& change) override {
        std::shared_ptr<Pool> final_pin;
        {
            std::lock_guard<std::mutex> lock(budget_->mutex_);
            auto* slot=change.payload_owner()==this?find(change.serializedPayload.data):nullptr;
            if(!slot||!slot->references){++budget_->state_.invalid_releases;return false;}
            if(--slot->references==0) {
                const auto capacity=slot->charged;std::free(slot->data);*slot=Budget::Slot{};
                auto& s=budget_->state_;s.active_bytes-=capacity;s.retained_bytes-=capacity;--s.live_records;++s.final_frees;
                if(--owned_records_==0)final_pin=std::move(self_pin_);
            }
            clear(change);
        } // No pool destruction or callback under the budget mutex.
        return true;
    }
};
} }
