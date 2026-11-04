#pragma once
#include "../run_contract.h"
#include <array>
namespace stage1_control {
// Records observations only. No identity API, authorization, destructor restore
// or claim about kernel/other-thread/parent state is provided by this journal.
class RestorationJournal final {
public:
    enum class Evidence {HostFake,NativeUnverified};
    enum class Event {Capture,SelectAttempt,SelectReadback,Construct,Cancel,LastUse,RestoreAttempt,RestoreReadback,Uncertain};
    enum class State {Empty,Captured,Selecting,Selected,SelectFailed,Business,Stopping,LastUse,Restoring,FakeComplete,EntryReadbackOnly,Uncertain};
    struct Row {Event event;uint64_t pid,tid,token;int rc;bool last_use;};
    RestorationJournal(uint64_t pid,uint64_t tid,Evidence e):pid_(pid),tid_(tid),evidence_(e){}
    int record(Event e,uint64_t pid,uint64_t tid,uint64_t token=0,int rc=0,bool last_use=false)noexcept{
        if(!pid_||pid_!=tid_||pid!=pid_||tid!=tid_||used_==rows_.size()||state_==State::Uncertain||state_==State::FakeComplete||state_==State::EntryReadbackOnly)return NS_CONTEXT;
        State next=state_;
        switch(e){
        case Event::Capture:if(state_!=State::Empty||!token||rc)return NS_STATE;original_=token;next=State::Captured;break;
        case Event::SelectAttempt:if(state_!=State::Captured||!token||token==original_)return NS_STATE;target_=token;next=State::Selecting;break;
        case Event::SelectReadback:if(state_!=State::Selecting)return NS_STATE;next=rc==0&&token==target_?State::Selected:State::SelectFailed;break;
        case Event::Construct:if(state_!=State::Selected)return NS_STATE;business_=true;next=State::Business;break;
        case Event::Cancel:if(state_!=State::Business&&state_!=State::Selected&&state_!=State::SelectFailed)return NS_STATE;next=State::Stopping;break;
        case Event::LastUse:if(state_!=State::Stopping||!last_use||rc){next=State::Uncertain;break;}next=State::LastUse;break;
        case Event::RestoreAttempt:
            if(token!=original_||(state_!=State::LastUse&&!(state_==State::SelectFailed&&!business_))){next=State::Uncertain;break;}
            next=State::Restoring;break;
        case Event::RestoreReadback:
            if(state_!=State::Restoring)return NS_STATE;
            next=rc||token!=original_?State::Uncertain:evidence_==Evidence::HostFake?State::FakeComplete:State::EntryReadbackOnly;break;
        case Event::Uncertain:next=State::Uncertain;break;
        default:return NS_CONTEXT;
        }
        rows_[used_++]={e,pid,tid,token,rc,last_use};state_=next;return next==State::Uncertain?NS_NOT_READY:NS_OK;
    }
    State state()const noexcept{return state_;}
    size_t size()const noexcept{return used_;}
    uint64_t original()const noexcept{return original_;}
    bool native_restoration_verified()const noexcept{return false;}
private:
    uint64_t pid_,tid_,original_=0,target_=0;Evidence evidence_;State state_=State::Empty;bool business_=false;size_t used_=0;std::array<Row,16> rows_{};
};
}
