#define main prior_native_contract_main
#include "test_native_contract.cpp"
#undef main
#include "parent_protocol.hpp"
#include "v2_route.hpp"
int main(){try{
    // Dependency regression: common Deployment policy changed, so preserve the
    // prior pure Host/Native mapper/size/journal assertions, not old suites.
    check(prior_native_contract_main(0,nullptr)==0,"dependent canonical contract");
    const std::array<int,4> data={10,11,12,13};validate_os_fd_set(14,data);
    for(int gate:{0,1,2,10,11,12,13})reject([&]{validate_os_fd_set(gate,data);});
    for(size_t i=0;i<data.size();++i){auto bad=data;bad[i]=14;reject([&]{validate_os_fd_set(14,bad);});bad=data;bad[i]=2;reject([&]{validate_os_fd_set(14,bad);});}
    auto duplicated=data;duplicated[3]=duplicated[0];reject([&]{validate_os_fd_set(14,duplicated);});
    ParentBoot b{10,11,1,1000,UINT64_C(0x0123456789abcdef),42,std::string(64,'a')};auto bytes=encode_parent_boot(b);auto out=decode_parent_boot(bytes.data(),bytes.size(),11,10,b.run);check(out.side==1&&out.manifest_sha==b.manifest_sha&&out.birth==42,"parent canonical byte order");
    reject([&]{decode_parent_boot(bytes.data(),bytes.size()-1,11,10,b.run);});reject([&]{decode_parent_boot(bytes.data(),bytes.size(),12,10,b.run);});reject([&]{decode_parent_boot(bytes.data(),bytes.size(),11,12,b.run);});reject([&]{decode_parent_boot(bytes.data(),bytes.size(),11,10,b.run+1);});
    for(size_t pos:{size_t(0),size_t(5),size_t(7),size_t(20),size_t(108),size_t(127)}){auto bad=bytes;bad[pos]^=1;reject([&]{decode_parent_boot(bad.data(),bad.size(),11,10,b.run);});}
    auto bad=b;bad.budget_ms=5001;reject([&]{encode_parent_boot(bad);});bad=b;bad.side=2;reject([&]{encode_parent_boot(bad);});bad=b;bad.manifest_sha[0]='X';reject([&]{encode_parent_boot(bad);});
    auto os=input();os.fields[DPurpose]="NATIVE_OS_FIXTURE_ONLY";os.fields[DImage]="/proc/self/exe";os.fields[DModule]="/proc/self/fd/13";os.fields[BBoard]=os.fields[ABoard];const auto text=os.encode_os();check(Deployment::parse_os(text).fields==os.fields,"same-target OS-only purpose");reject([&]{Deployment::parse_native(text);});reject([&]{Deployment::parse(text);});auto q=os.request_os(1,std::string(64,'c'),Clock::now()+std::chrono::seconds(1));check(q.local.pid==12&&q.local.board==os.fields[BBoard]&&q.deployment.external_approval==0,"OS purpose not approval/wire evidence");
    os.fields[BBoard]="OTHER";reject([&]{os.encode_os();});
    check(native_v2_denial()==0,"real Port Client 48-byte mechanical denial");
    std::puts("PASS PARENT_BOOTSTRAP_AND_V2_MECHANICAL_NONBUSINESS no_Native_runtime=1 no_real_consumer_load=1 no_identity=1");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"PARENT_CONTRACT_FAIL %s\n",e.what());return 4;}}
