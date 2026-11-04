#pragma once
namespace fastdds_dsoftbus::experiment {
struct ParticipantCloseState {bool terminal=false,failed=false,children_deleted=false,participant_deleted=false,result=false;};
// Public DDS delete operations and ownership invalidation are deliberately
// passed through this exact orchestration, also used by ParticipantRun::Close.
template<class HasParticipant,class NeedDrain,class Drain,class DeleteContained,class DeleteParticipant,
        class ClearContained,class ClearParticipant,class Observe>
bool ExecuteParticipantClose(ParticipantCloseState& state,HasParticipant has,NeedDrain need,Drain drain,
        DeleteContained contained,DeleteParticipant participant,ClearContained clear_children,ClearParticipant clear,Observe observe) {
    if(state.terminal)return state.result;
    bool deleted=true;
    if(has()) {
        if(!state.children_deleted&&need())deleted=drain();
        const bool children_removed=contained();
        if(children_removed){state.children_deleted=true;clear_children();}
        const bool participant_removed=participant();
        if(participant_removed){
            // A successful parent removal invalidates every child handle,
            // even if an earlier best-effort deletion returned an error.
            state.participant_deleted=true;state.children_deleted=true;
            clear_children();clear();
        }
        deleted=deleted&&children_removed&&participant_removed;
    } else state.participant_deleted=true;
    const bool observed=observe();
    state.failed=state.failed||!deleted||!observed;
    state.result=!state.failed;
    // No more entity calls or observations after physical removal. A failed
    // first outcome remains failed while other owners can still be cleaned up.
    state.terminal=state.participant_deleted;
    return state.result;
}
}
