#include "deployment.hpp"
#include <cstring>
#include <stdexcept>
#include <algorithm>
namespace stage1_control {
namespace {
const char* keys[DCount]={"format","purpose","run","parent_pid","image_path","image_sha","module_path","module_sha","namespace","domain","package","prefix","budget_ms","a_pid","a_board","a_token","a_udid","a_network","a_slot","a_initiator","b_pid","b_board","b_token","b_udid","b_network","b_slot","b_initiator"};
void need(bool v,const char* why){if(!v)throw std::invalid_argument(why);}
bool hex(const std::string& s,size_t n){return s.size()==n&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
void bounded(const std::string& s,size_t max){need(!s.empty()&&s.size()<=max,"deployment field length");for(unsigned char c:s)need(c>=33&&c<=126,"deployment ASCII");}
template<size_t N>void copy(char(&dest)[N],const std::string& s){need(s.size()<N,"deployment POD field");std::memcpy(dest,s.c_str(),s.size()+1);}
}
uint64_t Deployment::number(DeployField f,uint64_t max)const{const auto& s=fields[f];need(!s.empty()&&(s.size()==1||s.front()!='0'),"deployment canonical number");uint64_t out=0;for(char c:s){need(c>='0'&&c<='9',"deployment decimal");const auto digit=static_cast<uint64_t>(c-'0');need(digit<=max&&out<=(max-digit)/10,"deployment numeric overflow");out=out*10+digit;}return out;}
void Deployment::validate()const{validate_for(false);}
void Deployment::validate_native()const{validate_for(true);}
void Deployment::validate_os()const{validate_for(2);}
void Deployment::validate_for(unsigned native)const{
    need(fields[DFormat]=="S1HANDOFF1"&&fields[DPurpose]==(native==2?"NATIVE_OS_FIXTURE_ONLY":native?"NATIVE_PREPARE_ONLY":"HOST_NONBUSINESS_FIXTURE"),"deployment purpose/default denial");need(hex(fields[DRun],16)&&fields[DRun]!="0000000000000000","deployment nonce");
    for(auto f:{DImageSha,DModuleSha})need(hex(fields[f],64),"deployment hash");
    for(auto f:{DImage,DModule}){const auto& s=fields[f];
        if(native){need(s.size()>1&&s.size()<=512&&s.front()=='/'&&s.back()!='/'&&s.find("//")==std::string::npos&&s.find("/../")==std::string::npos&&s.find("/./")==std::string::npos&&s.substr(s.size()-2)!="/."&&(s.size()<3||s.substr(s.size()-3)!="/.."),"deployment absolute POSIX path");}
        else need(s.size()>=4&&s.size()<=512&&((s[0]>='A'&&s[0]<='Z')||(s[0]>='a'&&s[0]<='z'))&&s[1]==':'&&(s[2]=='\\'||s[2]=='/'),"deployment absolute drive path");
        for(unsigned char c:s)need(c>=32&&c<=126&&c!='\r'&&c!='\n'&&(!native||c!='\\'),"deployment path bytes");}
    bounded(fields[DNamespace],116);need(fields[DPackage]==NS_PACKAGE,"deployment package");bounded(fields[DPrefix],95);const auto head=fields[DPackage]+".";need(fields[DPrefix].compare(0,head.size(),head)==0&&fields[DPrefix].back()!='.'&&fields[DPrefix].find("..")==std::string::npos&&fields[DPrefix].find(".md1")==std::string::npos,"deployment package prefix");
    const auto parent=number(DParent,UINT32_MAX),a=number(APid,UINT32_MAX),b=number(BPid,UINT32_MAX);need(parent&&a&&b&&parent!=a&&parent!=b&&a!=b,"deployment process binding");(void)number(DDomain,UINT32_MAX);need(number(DBudget,5000)>0,"deployment local budget");
    if(native==2)need(fields[ABoard]==fields[BBoard]&&(fields[ABoard]=="3e01ff55454d202020104033bf453b00"||fields[ABoard]=="3e01ff55454d202020104433991c3b00"),"OS fixture one externally attested fixed target");
    else need(fields[ABoard]=="3e01ff55454d202020104033bf453b00"&&fields[BBoard]=="3e01ff55454d202020104433991c3b00","deployment declared board fixture");
    need(number(AToken)>0&&number(BToken)>0,"fixture token data");for(auto f:{AUdid,BUdid,ANetwork,BNetwork})bounded(fields[f],64);need(fields[AUdid]!=fields[BUdid],"deployment distinct UDIDs");
    (void)number(ASlot,0x00fffffe);(void)number(BSlot,0x00fffffe);need(fields[ASlot]!=fields[BSlot],"deployment distinct slots");for(auto f:{AInitiator,BInitiator})bounded(fields[f],127);need(fields[AInitiator]!=fields[BInitiator],"deployment distinct initiators");
}
std::string Deployment::encode()const{return encode_for(false);}
std::string Deployment::encode_native()const{return encode_for(true);}
std::string Deployment::encode_os()const{return encode_for(2);}
std::string Deployment::encode_for(unsigned native)const{validate_for(native);std::string out;for(unsigned i=0;i<DCount;++i)out+=std::string(keys[i])+"="+fields[i]+"\n";need(out.size()<=8192,"deployment total");return out;}
Deployment Deployment::parse(const std::string& text){return parse_for(text,false);}
Deployment Deployment::parse_native(const std::string& text){return parse_for(text,true);}
Deployment Deployment::parse_os(const std::string& text){return parse_for(text,2);}
Deployment Deployment::parse_for(const std::string& text,unsigned native){
    need(!text.empty()&&text.size()<=8192&&text.back()=='\n',"deployment total/truncation");Deployment out;size_t at=0;
    for(unsigned i=0;i<DCount;++i){const auto end=text.find('\n',at);need(end!=std::string::npos&&end-at<=600,"deployment line");const std::string expected=std::string(keys[i])+"=";need(text.compare(at,expected.size(),expected)==0,"deployment order/key");out.fields[i]=text.substr(at+expected.size(),end-at-expected.size());at=end+1;}
    need(at==text.size(),"deployment duplicate/trailing");out.validate_for(native);return out;
}
NS_RunRequest Deployment::request(unsigned side,const std::string& digest,Clock::time_point end)const{return request_for(side,digest,end,false);}
NS_RunRequest Deployment::request_native(unsigned side,const std::string& digest,Clock::time_point end)const{return request_for(side,digest,end,true);}
NS_RunRequest Deployment::request_os(unsigned side,const std::string& digest,Clock::time_point end)const{return request_for(side,digest,end,2);}
NS_RunRequest Deployment::request_for(unsigned side,const std::string& digest,Clock::time_point end,unsigned native)const{
    validate_for(native);need(side<2&&hex(digest,64),"deployment request identity");NS_RunRequest q{};q.header={NS_RUN_VERSION,sizeof(q),NS_SCOPE,0};q.sample_count=3;
    auto& d=q.deployment;d.header={NS_VERSION,sizeof(d),NS_SCOPE,0};d.cookie=NS_COOKIE;d.ownership=NS_BORROWED;d.external_approval=native?0:NS_EXTERNAL_APPROVAL;d.deadline_ns=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count());
    copy(d.name_space,fields[DNamespace]);copy(d.package_name,fields[DPackage]);copy(d.prefix,fields[DPrefix]);d.domain=static_cast<uint32_t>(number(DDomain,UINT32_MAX));
    const auto local=side?BPid:APid,peer=side?APid:BPid;copy(d.udid,fields[local+3]);copy(d.peer_udid,fields[peer+3]);copy(d.peer_network,fields[peer+4]);copy(d.initiator,fields[local+6]);copy(d.peer_initiator,fields[peer+6]);d.slot=static_cast<uint32_t>(number(static_cast<DeployField>(local+5),0x00fffffe));d.peer_slot=static_cast<uint32_t>(number(static_cast<DeployField>(peer+5),0x00fffffe));
    uint64_t run=0;for(char c:fields[DRun])run=(run<<4)|static_cast<uint64_t>(c<='9'?c-'0':c-'a'+10);
    for(unsigned i=0;i<2;++i){auto& e=i?q.peer:q.local;const unsigned s=i?1-side:side;const auto base=s?BPid:APid;e.run=run;e.self_token=number(static_cast<DeployField>(base+2));e.side=s;e.domain=d.domain;e.pid=static_cast<uint32_t>(number(base,UINT32_MAX));copy(e.board,fields[base+1]);copy(e.executable_sha,fields[DImageSha]);copy(e.deployment_sha,digest);copy(e.name_space,fields[DNamespace]);}
    return q;
}
}
