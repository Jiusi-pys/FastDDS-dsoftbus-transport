#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
namespace fastdds_dsoftbus::experiment::matrix {
using ExpectedSamples=std::map<std::string,std::set<unsigned>>;
struct LedgerCounters {
    unsigned valid_unique=0, duplicate=0, wrong_writer=0, wrong_reader=0;
    unsigned unexpected_ordinal=0,payload_mismatch=0,invalid_sample=0,after_delete=0;
};
class PerReaderValidator {
public:
    PerReaderValidator(std::string reader,ExpectedSamples allowed):reader_(std::move(reader)),allowed_(std::move(allowed)){}
    template<class Publish> bool PublishObservation(const std::string& reader,const std::string& writer,
            std::int32_t hi,std::uint32_t lo,unsigned ordinal,bool payload_valid,bool valid_data,Publish&& publish){
        // The callback owns immutable actual-sample evidence. Publish it before
        // a phase observer can see the corresponding ledger completion. The
        // evidence sink runs without the ledger mutex and makes no DDS call.
        std::forward<Publish>(publish)();
        return Observe(reader,writer,hi,lo,ordinal,payload_valid,valid_data);
    }
    bool Observe(const std::string& reader,const std::string& writer,std::int32_t hi,
            std::uint32_t lo,unsigned ordinal,bool payload_valid,bool valid_data=true){
        std::lock_guard<std::mutex> lock(mutex_);
        if(frozen_){++counts_.after_delete;return Fail("callback_after_reader_delete_complete");}
        if(reader!=reader_){++counts_.wrong_reader;return Fail("wrong_reader_guid");}
        const auto wanted=allowed_.find(writer);
        if(wanted==allowed_.end()){++counts_.wrong_writer;return Fail("wrong_writer_guid");}
        if(!valid_data || hi<0 || (hi==0 && lo==0)){++counts_.invalid_sample;return Fail("invalid_sample");}
        if(!payload_valid){++counts_.payload_mismatch;return Fail("payload_mismatch");}
        if(!wanted->second.count(ordinal)){++counts_.unexpected_ordinal;return Fail("unexpected_ordinal");}
        const auto sequence=std::make_tuple(writer,hi,lo);
        const auto application=std::make_pair(writer,ordinal);
        if(sequences_.count(sequence) || ordinals_.count(application)){
            ++counts_.duplicate;return Fail("duplicate_application_delivery");
        }
        sequences_.insert(sequence);ordinals_.insert(application);seen_[writer].insert(ordinal);
        ++counts_.valid_unique;return failure_.empty();
    }
    bool Complete(const ExpectedSamples& expected) const{
        std::lock_guard<std::mutex> lock(mutex_);return failure_.empty() && seen_==expected;
    }
    unsigned Missing(const ExpectedSamples& expected) const{
        std::lock_guard<std::mutex> lock(mutex_);unsigned n=0;
        for(const auto& writer:expected)for(auto ordinal:writer.second){
            const auto found=seen_.find(writer.first);if(found==seen_.end()||!found->second.count(ordinal))++n;
        }return n;
    }
    void Freeze(){std::lock_guard<std::mutex> lock(mutex_);frozen_=true;}
    LedgerCounters Counters()const{std::lock_guard<std::mutex> lock(mutex_);return counts_;}
    std::string Failure()const{std::lock_guard<std::mutex> lock(mutex_);return failure_;}
private:
    bool Fail(const char* reason){if(failure_.empty())failure_=reason;return false;}
    std::string reader_;ExpectedSamples allowed_,seen_;mutable std::mutex mutex_;
    std::set<std::tuple<std::string,std::int32_t,std::uint32_t>> sequences_;
    std::set<std::pair<std::string,unsigned>> ordinals_;LedgerCounters counts_;
    bool frozen_=false;std::string failure_;
};
}
