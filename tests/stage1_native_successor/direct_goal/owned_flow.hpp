#pragma once
#include "../native_contract.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

namespace stage1_goal {
// A run-bound operator record is data, not OS authority or proof of consent.
// Native entry must also bind the fresh owned-launcher BOOT, exact artifacts,
// fixed OWN lookup and the actual scalar provider. No selector is in this file.
struct Point {uint64_t pid=0,tid=0,parent=0,birth=0,now=0;unsigned tasks=0;uint32_t uid=0,euid=0,gid=0,egid=0;uint64_t wall=0;};
struct Admission {
    uint64_t run=0,pid=0,parent=0,birth=0,caller=0,target=0,until=0;
    uint64_t issued_wall=0,until_wall=0;uint32_t uid=0,euid=0,gid=0,egid=0;
    uint32_t side=2;char own[128]{},board[40]{},image_sha[65]{},record_sha[65]{};
    bool fresh_owned_lookup=false,operator_scope_bound=false;
};
enum class FlowState:uint32_t {Empty,Refused,Captured,Attempted,Selected,ExecReturned,RestoreAttempted,Restored,Uncertain};
enum class FlowError:int32_t {None=0,Context=1,Record=2,Caller=3,Select=4,Readback=5,Restore=6,Exec=7,Freshness=8};
struct FlowResult {FlowState state=FlowState::Empty;FlowError error=FlowError::None;int select_rc=-1,restore_rc=-1;bool select_attempted=false,restore_attempted=false;uint64_t original=0,target=0,selected=0,restored=0;};
inline bool hash(const char* p){if(!p)return false;for(unsigned i=0;i<64;++i)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return false;return p[64]==0;}
template<size_t N>bool equal(const char(&p)[N],const char* s){return std::memchr(p,0,N)&&!std::strcmp(p,s);}
inline bool admission(const Admission& a,const Point& p){
    const char* b=a.side==0?"3e01ff55454d202020104033bf453b00":"3e01ff55454d202020104433991c3b00";
    return a.run&&a.side<2&&a.pid==p.pid&&a.parent==p.parent&&a.birth==p.birth&&p.pid&&p.pid==p.tid&&p.tasks==1
        &&a.caller&&a.target&&a.target<=UINT32_MAX&&a.until>p.now&&a.until-p.now<=UINT64_C(120000000000)
        &&a.uid==p.uid&&a.euid==p.euid&&a.gid==p.gid&&a.egid==p.egid
        &&p.wall&&a.issued_wall<=p.wall&&p.wall<a.until_wall&&a.until_wall>a.issued_wall&&a.until_wall-a.issued_wall<=300
        &&equal(a.own,NS_ACTIONBAG)&&equal(a.board,b)&&hash(a.image_sha)&&hash(a.record_sha)
        &&a.fresh_owned_lookup&&a.operator_scope_bound;
}

// Ops is fixed Native code (point/GetSelf/SetSelf) or explicitly marked Host
// fake code. Native callers cannot supply it via argv, environment or record.
template<class Ops> class OwnedExecFlow final {
    Ops& ops_;Point entry_{};FlowResult result_{};
    bool same_context(const Point& p,bool sole=false){return p.pid==entry_.pid&&p.tid==entry_.tid&&p.parent==entry_.parent&&p.birth==entry_.birth&&p.uid==entry_.uid&&p.euid==entry_.euid&&p.gid==entry_.gid&&p.egid==entry_.egid&&(!sole||p.tasks==1);}
    bool same_entry(){return same_context(ops_.point());}
public:
    explicit OwnedExecFlow(Ops& o):ops_(o){}
    OwnedExecFlow(const OwnedExecFlow&)=delete;OwnedExecFlow& operator=(const OwnedExecFlow&)=delete;
    const FlowResult& result()const{return result_;}
    bool select(const Admission& a){
        if(result_.state!=FlowState::Empty)return false;
        entry_=ops_.point();if(!admission(a,entry_)){result_.state=FlowState::Refused;result_.error=FlowError::Record;return false;}
        // Only a live scalar read supplies original. Never copy a.caller into
        // a restoration target: compare the admission to the actual two reads.
        const auto first=ops_.get(),second=ops_.get();
        if(!first||first!=second||first!=a.caller){
            result_.state=FlowState::Refused;result_.error=FlowError::Caller;return false;
        }
        const auto last=ops_.point();
        if(!same_context(last,true)){result_.state=FlowState::Refused;result_.error=FlowError::Context;return false;}
        if(last.now<entry_.now||last.wall<entry_.wall||!admission(a,last)){result_.state=FlowState::Refused;result_.error=FlowError::Freshness;return false;}
        result_.original=first;result_.target=a.target;result_.state=FlowState::Captured;
        if(first==a.target){result_.selected=first;result_.select_rc=0;result_.state=FlowState::Selected;return true;}
        result_.select_attempted=true;result_.state=FlowState::Attempted; // BEFORE possible effect
        result_.select_rc=ops_.set(a.target);result_.selected=ops_.get();
        if(result_.select_rc||result_.selected!=a.target||!same_context(ops_.point(),true)){
            result_.error=result_.select_rc?FlowError::Select:FlowError::Readback;
            (void)restore();return false;
        }
        result_.state=FlowState::Selected;return true;
    }
    bool restore(){
        if(result_.restore_attempted||result_.state==FlowState::Restored)return result_.state==FlowState::Restored;
        if(!result_.original||!same_entry()){result_.state=FlowState::Uncertain;result_.error=FlowError::Context;return false;}
        if(!result_.select_attempted){
            result_.restored=ops_.get();result_.restore_rc=0;
        }else{
            result_.restore_attempted=true;result_.state=FlowState::RestoreAttempted;
            result_.restore_rc=ops_.set(result_.original);result_.restored=ops_.get();
        }
        const bool ok=!result_.restore_rc&&result_.restored==result_.original;
        result_.state=ok?FlowState::Restored:FlowState::Uncertain;if(!ok)result_.error=FlowError::Restore;
        return ok;
    }
    void exec_returned(){result_.state=FlowState::ExecReturned;result_.error=FlowError::Exec;(void)restore();}
    // Successful exec has no destructor/RAII restoration. The new image must
    // validate owned BOOT/admission/provenance and perform its own main restore.
};

struct ResumeState {
    uint64_t run=0,pid=0,parent=0,birth=0,original=0,target=0,until=0;
    uint32_t side=2,attempted=0;char image_sha[65]{},admission_sha[65]{},boot_sha[65]{},lookup_sha[65]{};
};
constexpr size_t state_bytes=336;
inline void put64(uint8_t* p,uint64_t n){for(unsigned i=0;i<8;++i)p[i]=static_cast<uint8_t>(n>>(8*i));}
inline uint64_t get64(const uint8_t* p){uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(p[i])<<(8*i);return n;}
inline std::array<uint8_t,state_bytes> encode_state(const ResumeState& s){
    std::array<uint8_t,state_bytes> b{};std::memcpy(b.data(),"NSGX0001",8);
    const uint64_t n[]={s.run,s.pid,s.parent,s.birth,s.original,s.target,s.until,s.side,s.attempted};
    for(unsigned i=0;i<9;++i)put64(b.data()+8+8*i,n[i]);
    std::memcpy(b.data()+80,s.image_sha,64);std::memcpy(b.data()+144,s.admission_sha,64);std::memcpy(b.data()+208,s.boot_sha,64);std::memcpy(b.data()+272,s.lookup_sha,64);return b;
}
inline bool decode_state(const uint8_t* b,size_t n,ResumeState& out){
    if(!b||n!=state_bytes||std::memcmp(b,"NSGX0001",8))return false;ResumeState s{};
    s.run=get64(b+8);s.pid=get64(b+16);s.parent=get64(b+24);s.birth=get64(b+32);s.original=get64(b+40);s.target=get64(b+48);s.until=get64(b+56);
    const auto side=get64(b+64),attempted=get64(b+72);if(side>1||attempted>1)return false;s.side=static_cast<uint32_t>(side);s.attempted=static_cast<uint32_t>(attempted);
    std::memcpy(s.image_sha,b+80,64);std::memcpy(s.admission_sha,b+144,64);std::memcpy(s.boot_sha,b+208,64);std::memcpy(s.lookup_sha,b+272,64);
    if(!s.run||!s.pid||!s.parent||!s.birth||!s.original||!s.target||s.target>UINT32_MAX||!s.until||!hash(s.image_sha)||!hash(s.admission_sha)||!hash(s.boot_sha)||!hash(s.lookup_sha))return false;
    out=s;return true;
}
inline bool resume_bound(const ResumeState& s,const Admission& a,const Point& p,const char* observed_boot_sha,const char* observed_lookup_sha){
    // Seals alone are not provenance. Admission must originate in the actual
    // owned launcher BOOT and operator receipt, including its live original.
    return p.pid==p.tid&&p.tasks==1&&s.pid==p.pid&&s.parent==p.parent&&s.birth==p.birth&&s.run==a.run&&s.side==a.side
        &&p.uid==a.uid&&p.euid==a.euid&&p.gid==a.gid&&p.egid==a.egid
        &&s.original==a.caller&&s.target==a.target&&s.until==a.until&&p.now<s.until&&s.until-p.now<=UINT64_C(120000000000)
        &&!std::strcmp(s.image_sha,a.image_sha)&&!std::strcmp(s.admission_sha,a.record_sha)
        &&hash(observed_boot_sha)&&!std::strcmp(s.boot_sha,observed_boot_sha)
        &&hash(observed_lookup_sha)&&!std::strcmp(s.lookup_sha,observed_lookup_sha);
}
} // namespace stage1_goal
