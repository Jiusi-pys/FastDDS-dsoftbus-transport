#pragma once
#include <array>
#include <cstdio>
#include <exception>
#include <string>
namespace mdds::experiment {
enum class DualStage {FirstWorking,SecondCreate,SecondMatch,OverlapData,OverlapAck,PreDeleteValidate,PreDeleteReady,P1Delete,PostDeleteData,PostDeleteAck,PostDeleteValidate,FinalReady,FinalCleanup};
inline const char* DualStageName(DualStage s) {
    switch(s) {
    case DualStage::FirstWorking:return "DUAL_FIRST_WORKING";
    case DualStage::SecondCreate:return "DUAL_SECOND_CREATE";
    case DualStage::SecondMatch:return "DUAL_SECOND_MATCH";
    case DualStage::OverlapData:return "DUAL_OVERLAP_DATA";
    case DualStage::OverlapAck:return "DUAL_OVERLAP_ACK";
    case DualStage::PreDeleteValidate:return "DUAL_PRE_DELETE_VALIDATE";
    case DualStage::PreDeleteReady:return "DUAL_PRE_DELETE_READY";
    case DualStage::P1Delete:return "DUAL_P1_DELETE";
    case DualStage::PostDeleteData:return "DUAL_P2_POST_DELETE_DATA";
    case DualStage::PostDeleteAck:return "DUAL_P2_POST_DELETE_ACK";
    case DualStage::PostDeleteValidate:return "DUAL_POST_DELETE_VALIDATE";
    case DualStage::FinalReady:return "DUAL_FINAL_READY";
    default:return "DUAL_FINAL_CLEANUP";
    }
}
struct DualStepResult {
    bool ok=true;std::string predicate,expected,observed;unsigned instance=0;
    static DualStepResult Pass(){return {};}
    static DualStepResult Fail(std::string p,std::string e,std::string o,unsigned i){return {false,std::move(p),std::move(e),std::move(o),i};}
};
struct DualFailure {bool present=false;DualStage stage=DualStage::FirstWorking;std::string predicate,expected,observed,side,run_id;unsigned instance=0;bool details_incomplete=false;};
class DualExecution {
public:
    DualExecution(std::string run,std::string side):run_(std::move(run)),side_(std::move(side)){
        // Reserve attribution context before entering any fallible stage.
        first_failure.run_id=cleanup_failure.run_id=run_;
        first_failure.side=cleanup_failure.side=side_;
    }
    DualFailure first_failure,cleanup_failure;
    void Enter(DualStage stage){current_=stage;std::printf("stage=%s event=ENTER run=%s side=%s\n",DualStageName(stage),run_.c_str(),side_.c_str());}
    bool Complete(const DualStepResult& result) {
        if(result.ok){std::printf("stage=%s result=PASS run=%s side=%s\n",DualStageName(current_),run_.c_str(),side_.c_str());return true;}
        if(!first_failure.present) Capture(first_failure,current_,result.predicate.c_str(),result.expected.c_str(),result.observed.c_str(),result.instance,"first_failure");return false;
    }
    void Cleanup(const DualStepResult& result){if(!result.ok && !cleanup_failure.present) Capture(cleanup_failure,DualStage::FinalCleanup,
        result.predicate.c_str(),result.expected.c_str(),result.observed.c_str(),result.instance,"cleanup_failure");
        else if(result.ok) std::printf("stage=DUAL_FINAL_CLEANUP result=PASS run=%s side=%s\n",run_.c_str(),side_.c_str());}
    void ActionException(const char* observed,unsigned instance) noexcept {
        if(!first_failure.present) Capture(first_failure,current_,"action_exception","normal_return",observed,instance,"first_failure");
    }
    void CleanupException(const char* observed,unsigned instance) noexcept {
        if(!cleanup_failure.present) Capture(cleanup_failure,DualStage::FinalCleanup,"cleanup_exception","normal_return",observed,instance,"cleanup_failure");
    }
private:
    void Capture(DualFailure& out,DualStage stage,const char* predicate,const char* expected,const char* observed,unsigned instance,const char* record) noexcept {
        // Latch attribution before copying optional allocating details. This is
        // not a promise of logging or native recovery under arbitrary OOM.
        out.present=true;out.stage=stage;out.instance=instance;
        try {out.predicate=predicate;out.expected=expected;out.observed=observed;}
        catch(...) {out.details_incomplete=true;}
        std::printf("record=%s stage=%s predicate=%s expected=%s observed=%s side=%s instance=%u run_id=%s details_incomplete=%d\n",
            record,DualStageName(stage),predicate,expected,observed,side_.c_str(),instance,run_.c_str(),out.details_incomplete);
    }
    DualStage current_=DualStage::FirstWorking;std::string run_,side_;
};
inline unsigned DualStageInstance(DualStage stage) noexcept {
    switch(stage) {
    case DualStage::FirstWorking:return 1;
    case DualStage::SecondCreate:case DualStage::SecondMatch:case DualStage::PreDeleteReady:
    case DualStage::PostDeleteData:case DualStage::PostDeleteAck:case DualStage::PostDeleteValidate:case DualStage::FinalReady:return 2;
    default:return 0; // Overlap and P1-delete actions also inspect the surviving P2.
    }
}
template<class Action> bool ExecuteDual(DualExecution& flow,Action action) {
    constexpr std::array<DualStage,12> stages{{DualStage::FirstWorking,DualStage::SecondCreate,DualStage::SecondMatch,DualStage::OverlapData,DualStage::OverlapAck,DualStage::PreDeleteValidate,DualStage::PreDeleteReady,DualStage::P1Delete,DualStage::PostDeleteData,DualStage::PostDeleteAck,DualStage::PostDeleteValidate,DualStage::FinalReady}};
    for(const auto stage:stages) {
        flow.Enter(stage);
        try {if(!flow.Complete(action(stage))) return false;}
        catch(const std::exception&) {flow.ActionException("std_exception",DualStageInstance(stage));return false;}
        catch(...) {flow.ActionException("non_std_exception",DualStageInstance(stage));return false;}
    }
    return true;
}
template<class Action> bool ExecuteDualCleanup(DualExecution& flow,Action action) {
    bool complete=true;
    // Keep P1, P2, aggregate-observation order. One failed/throwing operation
    // cannot skip the remaining owned instance; no operation is retried here.
    for(const auto instance:{1u,2u,0u}) {
        try {const auto result=action(instance);if(!result.ok) {flow.Cleanup(result);complete=false;}}
        catch(const std::exception&) {flow.CleanupException("std_exception",instance);complete=false;}
        catch(...) {flow.CleanupException("non_std_exception",instance);complete=false;}
    }
    if(complete) flow.Cleanup(DualStepResult::Pass());
    return complete;
}
}
