#pragma once
#include "DualExecution.hpp"
#include "DualScenarioChecks.hpp"
#include "DualValidation.hpp"
#include <cstdint>
#include <mutex>
#include <vector>
namespace mdds::experiment {
inline EndpointIdentity ProgramIdentity(const std::string& run,bool board,unsigned instance,unsigned generation,bool hot,EndpointKind kind) {
    auto id=DualIdentity(run,board,instance,kind);
    if(hot) {id[8]=0x48;id[9]=static_cast<std::uint8_t>(generation>>8);id[10]=static_cast<std::uint8_t>(generation);}
    return id;
}
inline std::string HotPayload(const std::string& run,unsigned instance,unsigned generation,bool board,unsigned index) {
    return "hot/"+run+"/"+std::to_string(instance)+"/"+std::to_string(generation)+"/"+(board?"A":"B")+"/"+std::to_string(index);
}
inline std::string HotReadyPayload(const std::string& run,bool board,unsigned cycle,unsigned phase) {
    return "hot/"+run+"/Q/"+(board?"A":"B")+"/ready/"+std::to_string(cycle)+"/"+std::to_string(phase);
}

struct HotIdentitySnapshot {std::string name;std::uint64_t runtime=0,application=0,receiver=0,token=0,native_endpoint=0,receiver_lease=0;int server_entry=-1,client_entry=-1;EndpointIdentity participant{},writer{},reader{};};
template<class StatsLike> HotIdentitySnapshot MakeHotIdentity(const std::string& name,const StatsLike& v,
        const EndpointIdentity& participant,const EndpointIdentity& writer,const EndpointIdentity& reader) {
    return {name,v.runtime_id,v.application_incarnation,v.receiver_incarnation,v.logical_token,v.instance_id,v.receiver_lease_id,v.server_callback_slot,v.client_callback_slot,participant,writer,reader};
}
inline bool HotIdentityValid(const HotIdentitySnapshot& a){return !a.name.empty()&&a.runtime&&a.application&&a.receiver&&a.token&&a.native_endpoint&&a.receiver_lease&&a.server_entry>=0;}
inline bool HotReplacement(const HotIdentitySnapshot& a,const HotIdentitySnapshot& b){return HotIdentityValid(a)&&HotIdentityValid(b)&&a.name==b.name&&a.runtime==b.runtime&&a.native_endpoint==b.native_endpoint&&a.server_entry==b.server_entry&&a.client_entry==b.client_entry&&a.application!=b.application&&a.receiver!=b.receiver&&a.token!=b.token;}
inline bool HotContinuous(const HotIdentitySnapshot& a,const HotIdentitySnapshot& b){return HotIdentityValid(a)&&HotIdentityValid(b)&&a.name==b.name&&a.runtime==b.runtime&&a.native_endpoint==b.native_endpoint&&a.server_entry==b.server_entry&&a.client_entry==b.client_entry&&a.application==b.application&&a.receiver==b.receiver&&a.token==b.token&&a.receiver_lease==b.receiver_lease&&a.participant==b.participant&&a.writer==b.writer&&a.reader==b.reader;}
inline bool HotFreshDiscovery(std::uint64_t events,bool expected_current,bool matched){return events>0&&expected_current&&matched;}
inline bool ValidHotCycles(unsigned cycles){return cycles==1||cycles==10||cycles==100;}
// Bounded program evidence ledger, not adapter History. Full source/payload
// validation happens before Accept. Invalid/duplicate data remains a failure.
class HotDataLedger {
public:
    explicit HotDataLedger(unsigned limit):seen_(limit+1,false){}
    bool Accept(unsigned index,bool exact){std::lock_guard<std::mutex> lock(mutex_);if(!exact||!index||index>=seen_.size()||seen_[index]){++invalid_;return false;}seen_[index]=true;++count_;return true;}
    unsigned Count()const{std::lock_guard<std::mutex> lock(mutex_);return count_;}
    bool Complete(unsigned expected)const{std::lock_guard<std::mutex> lock(mutex_);if(invalid_||expected>=seen_.size()||count_!=expected)return false;for(unsigned i=1;i<=expected;++i)if(!seen_[i])return false;return true;}
private:mutable std::mutex mutex_;std::vector<bool> seen_;unsigned count_=0,invalid_=0;
};
// Q carries ordinary application readiness samples. A certificate has a full
// cycle+phase identity; an earlier phase can never satisfy a later barrier.
class HotReadyLedger {
    struct Entry{bool peer=false,sealed=false,sent=false;};
public:
    explicit HotReadyLedger(unsigned cycles):cycles_(cycles),entries_((cycles+1)*3){}
    bool Receive(unsigned cycle,unsigned phase){std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(cycle,phase);if(!e||e->peer)return false;e->peer=true;return true;}
    bool Seal(unsigned cycle,unsigned phase){std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(cycle,phase);if(!e||e->sealed)return false;e->sealed=true;return true;}
    bool Sent(unsigned cycle,unsigned phase){std::lock_guard<std::mutex> lock(mutex_);auto* e=Find(cycle,phase);if(!e||!e->sealed||e->sent)return false;e->sent=true;return true;}
    bool Complete(unsigned cycle,unsigned phase)const{std::lock_guard<std::mutex> lock(mutex_);if(cycle>cycles_||phase>=3)return false;const auto& e=entries_[cycle*3+phase];return e.peer&&e.sealed&&e.sent;}
private:Entry* Find(unsigned c,unsigned p){return c<=cycles_&&p<3?&entries_[c*3+p]:nullptr;}mutable std::mutex mutex_;unsigned cycles_;std::vector<Entry> entries_;
};
struct HotFailure {bool present=false;std::string stage,predicate,expected,observed;unsigned cycle=0,instance=0;};
class HotExecution {
public:
    HotExecution(std::string run,std::string side):run_(std::move(run)),side_(std::move(side)){}
    HotFailure first_failure,cleanup_failure;
    template<class A>bool Step(const char* stage,unsigned cycle,unsigned instance,A action){
        std::printf("stage=%s event=ENTER cycle=%u instance=%u run=%s side=%s\n",stage,cycle,instance,run_.c_str(),side_.c_str());
        try {const auto result=action();if(result.ok){std::printf("stage=%s result=PASS cycle=%u instance=%u run=%s side=%s\n",stage,cycle,instance,run_.c_str(),side_.c_str());return true;}
            Capture(first_failure,"first_failure",stage,cycle,result);}
        catch(const std::exception&){Capture(first_failure,"first_failure",stage,cycle,DualStepResult::Fail("action_exception","normal_return","std_exception",instance));}
        catch(...){Capture(first_failure,"first_failure",stage,cycle,DualStepResult::Fail("action_exception","normal_return","non_std_exception",instance));}
        return false;
    }
    void Cleanup(const DualStepResult& result){if(!result.ok)Capture(cleanup_failure,"cleanup_failure","HOT_FINAL_CLEANUP",0,result);else std::printf("stage=HOT_FINAL_CLEANUP result=PASS run=%s side=%s\n",run_.c_str(),side_.c_str());}
private:
    void Capture(HotFailure& out,const char* record,const char* stage,unsigned cycle,const DualStepResult& r){if(out.present)return;out={true,stage,r.predicate,r.expected,r.observed,cycle,r.instance};std::printf("record=%s stage=%s cycle=%u predicate=%s expected=%s observed=%s instance=%u side=%s run_id=%s\n",record,stage,cycle,r.predicate.c_str(),r.expected.c_str(),r.observed.c_str(),r.instance,side_.c_str(),run_.c_str());}
    std::string run_,side_;
};
}
