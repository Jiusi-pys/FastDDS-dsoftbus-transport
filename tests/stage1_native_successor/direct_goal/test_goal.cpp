#include "owned_flow.hpp"
#include "pair_record.hpp"
#include "pair_controller.hpp"
#include "../supervision/owned_policy.h"
#include "query_wait.hpp"
#include "goal_diagnostics.hpp"
#include "goal_record.hpp"
#include <cstdio>
#include <vector>
#include <functional>
#include <stdexcept>
using namespace stage1_goal;
namespace {
unsigned tests=0,failures=0;
void check(bool v,const char* name){++tests;if(!v){++failures;std::printf("FAIL %s\n",name);}}
template<size_t N>void copy(char(&p)[N],const char* s){const auto n=std::strlen(s);if(n>=N)throw std::runtime_error("fixture bound");std::memcpy(p,s,n+1);}
struct Ops {
    Point p{99,99,77,123,150,1,0,0,0,0,150};uint64_t self=1;int target_rc=0,restore_rc=0;
    bool partial_change=false;std::vector<uint64_t> sets;std::function<void(uint64_t)> before_set;std::function<void()> before_get;
    Point point(){return p;}uint64_t get(){if(before_get)before_get();return self;}
    int set(uint64_t n){if(before_set)before_set(n);sets.push_back(n);int rc=n==1?restore_rc:target_rc;if(!rc||partial_change)self=n;return rc;}
};
Admission record(){Admission a{};a.run=7;a.pid=99;a.parent=77;a.birth=123;a.caller=1;a.target=2;a.until=200;a.issued_wall=100;a.until_wall=200;a.side=0;copy(a.own,NS_ACTIONBAG);copy(a.board,"3e01ff55454d202020104033bf453b00");std::memset(a.image_sha,'a',64);std::memset(a.record_sha,'b',64);a.fresh_owned_lookup=true;a.operator_scope_bound=true;return a;}
ResumeState saved(){ResumeState s{};s.run=7;s.pid=99;s.parent=77;s.birth=123;s.original=1;s.target=2;s.until=200;s.side=0;s.attempted=1;std::memset(s.image_sha,'a',64);std::memset(s.admission_sha,'b',64);std::memset(s.boot_sha,'c',64);std::memset(s.lookup_sha,'d',64);return s;}
void flows(){
    {Ops o;auto a=record();a.operator_scope_bound=false;OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty(),"default_requires_bound_operator_input_no_OS_grant_from_flag");}
    {Ops o;auto a=record();a.fresh_owned_lookup=false;OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty(),"missing_actual_lookup_refused");}
    {Ops o;auto a=record();a.caller=55;OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty(),"record_cannot_choose_saved_original");}
    {Ops o;auto a=record();OwnedExecFlow<Ops> f(o);bool attempted_before=false;o.before_set=[&](uint64_t n){if(n==2)attempted_before=f.result().select_attempted&&f.result().state==FlowState::Attempted;};check(f.select(a)&&attempted_before,"attempt_recorded_before_set");check(!f.select(a)&&o.sets.size()==1,"select_one_shot");check(f.restore()&&o.sets==std::vector<uint64_t>({2,1}),"normal_pair_original_from_live_getter");check(f.restore()&&o.sets.size()==2,"restore_never_retried");}
    {Ops o;o.target_rc=-1;o.partial_change=true;auto a=record();OwnedExecFlow<Ops> f(o);check(!f.select(a)&&f.result().restore_attempted&&o.self==1&&o.sets.size()==2,"failed_set_may_change_requires_one_restore");}
    {Ops o;auto a=record();OwnedExecFlow<Ops> f(o);check(f.select(a),"exec_fixture_select");f.exec_returned();check(f.result().state==FlowState::Restored&&f.result().error==FlowError::Exec&&o.sets.size()==2,"exec_return_is_failure_even_if_restored");}
    {Ops o;auto a=record();OwnedExecFlow<Ops> f(o);check(f.select(a),"wrong_thread_fixture_select");o.p.tid=100;check(!f.restore()&&o.sets.size()==1&&f.result().state==FlowState::Uncertain,"wrong_entry_revokes_restore_no_foreign_set");}
    {Ops o;auto a=record();o.self=2;a.caller=2;OwnedExecFlow<Ops> f(o);check(f.select(a)&&!f.result().select_attempted&&f.restore()&&o.sets.empty(),"already_owned_no_unnecessary_identity_mutation");}
    {Ops o;auto a=record();o.before_get=[&]{o.p.wall=200;};OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty()&&f.result().error==FlowError::Freshness,"expiry_during_live_getters_rejected_before_set");}
    {Ops o;auto a=record();o.before_get=[&]{o.p.tasks=2;};OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty(),"worker_appears_during_getters_zero_sets");}
    {Ops o;auto a=record();o.before_get=[&]{o.p.euid=123;};OwnedExecFlow<Ops> f(o);check(!f.select(a)&&o.sets.empty(),"UID_context_changes_before_set_refused");}
    {Ops o;auto a=record();OwnedExecFlow<Ops> f(o);check(f.select(a),"restore_UID_fixture_selected");o.p.egid=123;check(!f.restore()&&o.sets.size()==1,"changed_original_UID_group_revokes_restore");}
    {auto a=record();auto s=saved();Ops o;o.p.tasks=2;check(!resume_bound(s,a,o.p,s.boot_sha,s.lookup_sha),"postexec_not_sole_main_refused");o.p.tasks=1;o.p.uid=123;check(!resume_bound(s,a,o.p,s.boot_sha,s.lookup_sha),"postexec_UID_not_hash_bound_BOOT_refused");}
}
void state_codec(){auto s=saved();auto b=encode_state(s);ResumeState r{};check(b.size()==336&&b[8]==7&&b[9]==0&&decode_state(b.data(),b.size(),r)&&r.original==1,"explicit_little_endian_no_padding");check(!decode_state(b.data(),b.size()-1,r),"state_truncation");auto bad=b;bad[0]^=1;check(!decode_state(bad.data(),bad.size(),r),"state_version");auto a=record();Ops o;check(resume_bound(s,a,o.p,s.boot_sha,s.lookup_sha),"resume_full_boot_admission_binding");s.original=999;check(!resume_bound(s,a,o.p,s.boot_sha,s.lookup_sha),"sealed_state_alone_not_original_provenance");s=saved();o.p.parent=88;check(!resume_bound(s,a,o.p,s.boot_sha,s.lookup_sha),"actual_parent_binding");o.p.parent=77;char wrong[65]{};std::memset(wrong,'e',64);check(!resume_bound(s,a,o.p,wrong,s.lookup_sha),"actual_boot_hash_binding");check(!resume_bound(s,a,o.p,s.boot_sha,wrong),"actual_fixed_lookup_hash_binding");}
PairRecord pair(){PairRecord p;p.f={"S1DIRECTPAIR1","0000000000000007",std::string(64,'a'),std::string(64,'b'),"goal", "151",NS_PACKAGE,"com.kaihong.mdds.goal","99","77","3e01ff55454d202020104033bf453b00","2","UDID_A","NET_A","1","com.kaihong.mdds.goal.TA","99","77","3e01ff55454d202020104433991c3b00","3","UDID_B","NET_B","2","com.kaihong.mdds.goal.TB"};return p;}
template<class F>bool refused(F f){try{f();return false;}catch(...){return true;}}
void pairs(){auto p=pair();auto text=p.encode();auto parsed=PairRecord::parse(text);auto q=parsed.request(0,std::string(64,'c'),1000);check(q.local.pid==99&&q.peer.pid==99&&q.local.side==0&&q.peer.side==1&&q.sample_count==3,"two_kernels_can_share_numeric_PID");check(q.deployment.domain==151&&!std::strcmp(q.deployment.package_name,NS_PACKAGE)&&!std::strcmp(q.deployment.peer_network,"NET_B"),"pair_exact_current_peer_scope");check(refused([&]{PairRecord::parse(text+"extra=1\n");}),"pair_unknown_trailing");check(refused([&]{PairRecord::parse(text.substr(0,text.size()-1));}),"pair_incomplete");p.f[PPackage]="org.unadopted";check(refused([&]{p.validate();}),"original_project_package_only");p=pair();p.f[AToken]="18446744073709551616";check(refused([&]{p.validate();}),"token_overflow");p=pair();p.f[BPid]="0";check(refused([&]{p.validate();}),"no_fabricated_missing_peer_PID");}
void controller(){
    using namespace stage1_control;auto p=pair();auto a=p.request(0,std::string(64,'c'),1000),b=p.request(1,std::string(64,'c'),1000);
    auto end=stage1_run::Clock::now()+std::chrono::seconds(10);PairCoordinator c(a.local,b.local,end,true);
    std::array<std::vector<uint8_t>,2> journal;std::array<uint32_t,2> seq{{1,1}};bool ok=true;
    auto append=[&](unsigned side,uint32_t kind,unsigned ordinal=0){Frame f;f.kind=Kind::Event;f.direction=Direction::Endpoint;f.sequence=seq[side]++;f.envelope=side?b.local:a.local;f.nonce=f.envelope.run;auto& e=f.event;e.header={NS_RUN_VERSION,sizeof(e),NS_SCOPE,0};e.envelope=f.envelope;e.kind=kind;e.ordinal=ordinal;e.socket=3;e.flags=NS_HOST_FIXTURE_LISTEN;e.writer_matches=e.reader_matches=1;unsigned producer=kind==NS_TAKE?1-side:side;std::memset(e.writer_guid,int(31+producer),16);if(ordinal){e.sequence=100*producer+ordinal;auto v=stage1_run::payload(e.envelope.run,producer,ordinal);std::memcpy(e.payload,v.data(),v.size());}auto v=encode(f);journal[side].insert(journal[side].end(),v.begin(),v.end());return c.ingest(side,journal[side]);};
    auto deliver=[&](unsigned sequence,unsigned release){if(!c.has_release())return false;auto f=c.release_frames();Frame x,y;if(decode(f[0].data(),f[0].size(),x)!=Error::None||decode(f[1].data(),f[1].size(),y)!=Error::None||x.sequence!=sequence||y.sequence!=sequence||x.release!=release||y.release!=release)return false;return c.confirm_both_delivered(sequence);};
    ok=append(0,NS_ROOT_LISTENER)&&!c.has_release()&&append(1,NS_ROOT_LISTENER)&&deliver(1,NS_CONNECT_RELEASE)&&append(1,NS_MATCHED)&&append(0,NS_MATCHED)&&deliver(2,NS_SEND_RELEASE);
    for(unsigned side=0;side<2;++side){for(unsigned i=1;i<=3;++i)ok=append(side,NS_WRITE,i)&&ok;for(unsigned i=1;i<=3;++i)ok=append(side,NS_TAKE,i)&&ok;ok=append(side,NS_ACK)&&ok;}
    ok=deliver(3,NS_DATA_CHECK_RELEASE)&&ok;ok=append(0,NS_DATA_DONE)&&append(1,NS_DATA_DONE)&&deliver(4,NS_DELETE_RELEASE)&&ok;ok=append(1,NS_TEARDOWN)&&append(0,NS_TEARDOWN)&&c.complete()&&ok;
    check(ok,"actual_pair_controller_two_journal_release_1_2_4_3_guid_SN_payload_teardown");
    check(c.ingest(0,journal[0])&&c.complete(),"unchanged_atomic_snapshot_not_duplicate_event");auto altered=journal[0];altered.back()^=1;check(!c.ingest(0,altered)&&c.error(),"old_journal_prefix_cannot_change");
    PairCoordinator truncated(a.local,b.local,end,true);auto short_frame=journal[0];short_frame.resize(25);check(!truncated.ingest(0,short_frame),"complete_publication_short_tail_is_failure");
    PairCoordinator early(a.local,b.local,end,true);check(!early.ingest(0,journal[0]),"future_events_before_both_release_deliveries_refused");
    PairCoordinator real(a.local,b.local,end,false);uint32_t body=0;check(header_size(journal[0].data(),24,body)==Error::None,"fixture_header");std::vector<uint8_t> root(journal[0].begin(),journal[0].begin()+24+body);check(!real.ingest(0,root),"fixture_ROOT_cannot_attest_native_LISTEN");
    PairCoordinator cancelled(a.local,b.local,end,true);cancelled.cancel();check(!cancelled.ingest(0,root),"explicit_controller_cancel_revokes_release");
    PairCoordinator expired(a.local,b.local,stage1_run::Clock::now(),true);check(!expired.ingest(0,root),"absolute_controller_deadline");
}
struct PolicyFixture {int signals=0;bool terminal=false;};
int observed(void* p,uint32_t pid,OW_Observation* o){auto& f=*static_cast<PolicyFixture*>(p);*o={f.terminal?pid:0,f.terminal?1:0,0};return OW_OK;}
int identified(void*,uint32_t pid,OW_Identity* o){*o={pid,77,123,1};return OW_OK;}
int signalled(void* p,uint32_t){++static_cast<PolicyFixture*>(p)->signals;return OW_OK;}
int reaped(void*,uint32_t,int* status){*status=0;return OW_OK;}
void finite_window(){
    const OW_Ops ops{observed,identified,signalled,reaped};OW_Child c{};PolicyFixture f;
    check(ow_begin(&c,99,77,123,0,120000)==OW_OK&&c.deadline_ms==120000&&c.hard_ms==122000,"new_DDS_120s_plus_common2s_no_deadline_reset");
    check(ow_step(&c,&ops,&f,100000,0)==OW_ACTIVE&&f.signals==0,"new_window_does_not_reuse_old8s_case");
    check(ow_step(&c,&ops,&f,120000,0)==OW_SIGNALLED&&f.signals==1,"one_created_direct_child_at_own_deadline");
    check(ow_step(&c,&ops,&f,120001,0)==OW_SIGNALLED&&f.signals==1,"no_duplicate_timeout_signal");
    f.terminal=true;check(ow_step(&c,&ops,&f,120002,0)==OW_REAPED&&c.status_valid&&f.signals==1,"same_wait_right_finite_reap");
    check(ow_begin(&c,99,77,123,0,120001)==OW_ERROR&&ow_begin(&c,99,77,123,INT64_MAX-1000,100)==OW_ERROR,"new_window_still_finite_overflow_refused");
    check(ow_begin(&c,99,77,123,0,120000)==OW_OK,"unknown_window_setup");f={};check(ow_step(&c,&ops,&f,122000,0)==OW_UNKNOWN&&f.signals==0,"missed_common_deadline_revokes_not_late_kill");
}
void wait_and_diagnostics(){
    QueryObservation q{true,false,true,false,false,0,QueryTerminal::None};check(classify_query_wait(99,q)==QueryWait::Running,"query_only_consistent_empty_is_running");q.empty=false;check(classify_query_wait(99,q)==QueryWait::Unknown,"query_empty_status_inconsistent_revokes_signal");
    q={true,false,false,true,true,99,QueryTerminal::Exited};check(classify_query_wait(99,q)==QueryWait::Terminal,"query_exact_child_valid_terminal");q.pid=100;check(classify_query_wait(99,q)==QueryWait::Unknown,"query_wrong_PID_never_running_signal");q.pid=99;q.terminal=QueryTerminal::Invalid;check(classify_query_wait(99,q)==QueryWait::Unknown,"query_bad_CLD_code_unknown");
    q={false,true,false,false,false,0,QueryTerminal::Invalid};check(classify_query_wait(99,q)==QueryWait::Interrupted,"query_EINTR_fresh_observation_only");q.interrupted=false;check(classify_query_wait(99,q)==QueryWait::Unknown,"query_ECHILD_other_wait_error_revokes");q={true,false,false,false,true,99,QueryTerminal::Exited};check(classify_query_wait(99,q)==QueryWait::Unknown,"query_terminal_wrong_signo_revokes");
    GoalDiagnostic d;d.at(GoalPhase::LocalNode);d.returned(-13);auto primary=d;d.at(GoalPhase::Restore);d.returned(0);check(primary.phase==GoalPhase::LocalNode&&primary.rc_valid&&primary.native_rc==-13&&d.native_rc==0,"primary_API_failure_not_overwritten_by_restore");d.at(GoalPhase::Pair);check(!d.rc_valid&&d.native_rc==0,"phase_without_native_return_not_faked");check(primary.fields()=="phase=12\nnative_rc_valid=1\nnative_rc=-13\n","diagnostic_only_fixed_enum_and_native_numeric_RC");
    check(goal_field("uid=12\neuid=34\ngid=56\negid=78\n","uid")=="12"&&goal_field("uid=12\neuid=34\ngid=56\negid=78\n","gid")=="56","BOOT_UID_keys_are_not_euid_substrings");
    check(refused([]{goal_field("uid=12\nuid=34\n","uid");})&&refused([]{goal_field("uid=12\r\n","uid");}),"BOOT_duplicate_or_non_LF_records_rejected");
}
}
int main(){flows();state_codec();pairs();controller();finite_window();wait_and_diagnostics();std::printf("GOAL_POLICY_HOST_FAKE tests=%u failures=%u noIdentityAPI=1 noSDK=1 noDDS=1 noNativeExec=1\n",tests,failures);return failures?1:0;}
