#pragma once
#include "../run_contract.h"
#include "owned_flow.hpp"
#include <array>
#include <string>
#include <stdexcept>
#include <chrono>
namespace stage1_goal {
// One common two-board record; Linux parents are per-side, never a fabricated
// shared parent PID. This private application format does not change DDS QoS.
enum PairField {PFormat,PRun,PImageSha,PBridgeSha,PNamespace,PDomain,PPackage,PPrefix,
    APid,AParent,ABoard,AToken,AUdid,ANetwork,ASlot,AInitiator,
    BPid,BParent,BBoard,BToken,BUdid,BNetwork,BSlot,BInitiator,PairCount};
struct PairRecord {
    std::array<std::string,PairCount> f;
    static constexpr const char* key[PairCount]={"format","run","image_sha","bridge_sha","namespace","domain","package","prefix",
        "a_pid","a_parent","a_board","a_token","a_udid","a_network","a_slot","a_initiator",
        "b_pid","b_parent","b_board","b_token","b_udid","b_network","b_slot","b_initiator"};
    static void need(bool v,const char* c){if(!v)throw std::invalid_argument(c);}
    uint64_t num(PairField k,uint64_t max=UINT64_MAX)const{const auto& s=f[k];need(!s.empty()&&(s.size()==1||s[0]!='0'),"PAIR_NUMBER");uint64_t v=0;for(char c:s){need(c>='0'&&c<='9'&&uint64_t(c-'0')<=max&&v<=(max-uint64_t(c-'0'))/10,"PAIR_NUMBER");v=v*10+uint64_t(c-'0');}return v;}
    static void bounded(const std::string& s,size_t max){need(!s.empty()&&s.size()<=max,"PAIR_TEXT_BOUND");for(unsigned char c:s)need(c>=33&&c<=126,"PAIR_TEXT");}
    void validate()const{
        need(f[PFormat]=="S1DIRECTPAIR1","PAIR_PURPOSE");need(f[PRun].size()==16&&f[PRun].find_first_not_of("0123456789abcdef")==std::string::npos&&f[PRun]!="0000000000000000","PAIR_RUN");
        need(f[PImageSha].size()==64&&f[PBridgeSha].size()==64&&f[PImageSha].find_first_not_of("0123456789abcdef")==std::string::npos&&f[PBridgeSha].find_first_not_of("0123456789abcdef")==std::string::npos,"PAIR_HASH");
        bounded(f[PNamespace],116);bounded(f[PPrefix],95);need(f[PPackage]==NS_PACKAGE&&f[PPrefix].compare(0,std::strlen(NS_PACKAGE)+1,std::string(NS_PACKAGE)+".")==0&&f[PPrefix].find("..")==std::string::npos&&f[PPrefix].back()!='.',"PAIR_PACKAGE");(void)num(PDomain,UINT32_MAX);
        need(f[ABoard]=="3e01ff55454d202020104033bf453b00"&&f[BBoard]=="3e01ff55454d202020104433991c3b00","PAIR_BOARDS");
        for(unsigned base:{unsigned(APid),unsigned(BPid)}){need(num(static_cast<PairField>(base),UINT32_MAX)>0&&num(static_cast<PairField>(base+1),UINT32_MAX)>0&&num(static_cast<PairField>(base+3),UINT32_MAX)>0,"PAIR_OS_CONTEXT");for(unsigned off:{4u,5u})bounded(f[base+off],64);(void)num(static_cast<PairField>(base+6),0x00fffffe);bounded(f[base+7],127);}
        need(f[AUdid]!=f[BUdid]&&f[ASlot]!=f[BSlot]&&f[AInitiator]!=f[BInitiator],"PAIR_DISTINCT_ENDPOINTS");
        // PIDs/parents may numerically coincide on different kernels.
    }
    std::string encode()const{validate();std::string out;for(unsigned i=0;i<PairCount;++i)out+=std::string(key[i])+"="+f[i]+"\n";need(out.size()<=8192,"PAIR_TOTAL");return out;}
    static PairRecord parse(const std::string& s){need(!s.empty()&&s.size()<=8192&&s.back()=='\n',"PAIR_FRAME");PairRecord p;size_t at=0;for(unsigned i=0;i<PairCount;++i){auto end=s.find('\n',at);auto prefix=std::string(key[i])+"=";need(end!=std::string::npos&&end-at<=512&&s.compare(at,prefix.size(),prefix)==0,"PAIR_KEY_ORDER");p.f[i]=s.substr(at+prefix.size(),end-at-prefix.size());at=end+1;}need(at==s.size(),"PAIR_TRAILING");p.validate();return p;}
    template<size_t N>static void copy(char(&p)[N],const std::string& s){need(s.size()<N,"PAIR_POD_SIZE");std::memcpy(p,s.c_str(),s.size()+1);}
    NS_RunRequest request(unsigned side,const std::string& digest,uint64_t until)const{
        validate();need(side<2&&digest.size()==64&&digest.find_first_not_of("0123456789abcdef")==std::string::npos,"PAIR_REQUEST_CONTEXT");NS_RunRequest q{};q.header={NS_RUN_VERSION,sizeof(q),NS_SCOPE,0};q.sample_count=3;
        auto& d=q.deployment;d.header={NS_VERSION,sizeof(d),NS_SCOPE,0};d.cookie=NS_COOKIE;d.ownership=NS_BORROWED;d.external_approval=NS_EXTERNAL_APPROVAL;d.deadline_ns=until;d.domain=static_cast<uint32_t>(num(PDomain,UINT32_MAX));copy(d.name_space,f[PNamespace]);copy(d.package_name,f[PPackage]);copy(d.prefix,f[PPrefix]);
        const unsigned local=side?BPid:APid,peer=side?APid:BPid;copy(d.udid,f[local+4]);copy(d.peer_udid,f[peer+4]);copy(d.peer_network,f[peer+5]);d.slot=static_cast<uint32_t>(num(static_cast<PairField>(local+6),0x00fffffe));d.peer_slot=static_cast<uint32_t>(num(static_cast<PairField>(peer+6),0x00fffffe));copy(d.initiator,f[local+7]);copy(d.peer_initiator,f[peer+7]);
        uint64_t run=0;for(char c:f[PRun])run=(run<<4)|uint64_t(c<='9'?c-'0':c-'a'+10);
        for(unsigned i=0;i<2;++i){auto& e=i?q.peer:q.local;unsigned s=i?1-side:side,b=s?BPid:APid;e.run=run;e.side=s;e.domain=d.domain;e.pid=static_cast<uint32_t>(num(static_cast<PairField>(b),UINT32_MAX));e.self_token=num(static_cast<PairField>(b+3),UINT32_MAX);copy(e.board,f[b+2]);copy(e.executable_sha,f[PImageSha]);copy(e.deployment_sha,digest);copy(e.name_space,f[PNamespace]);}
        return q;
    }
};
}
