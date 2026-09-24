#include "MatrixNativeConfig.hpp"
#include <iostream>
#include <cstring>
using namespace mdds::experiment;
namespace v1=mdds::dsoftbus::v1;
int main(){
    const std::string run="0123456789abcdef";Config seed;seed.run_id=run;seed.peer_network_id=std::string(64,'a');
    std::vector<MatrixDomainKeys> keys;
    for(unsigned domain:{73u,74u}){MatrixDomainKeys k;k.domain=domain;v1::AddressCodec::DeriveKey({"exp-"+run,domain,"model-A"},k.local);v1::AddressCodec::DeriveKey({"exp-"+run,domain,"model-B"},k.peer);keys.push_back(k);}
    unsigned tests=0;auto check=[&](bool value,const char* what){++tests;if(!value){std::cerr<<"FAIL "<<what<<"\n";std::exit(1);}};
    check(ValidMatrixRequest("M1",run,true,"/data/local/tmp/mdds_matrix_"+run+"_A"),"valid bounded matrix request");
    check(!ValidMatrixRequest("UNKNOWN",run,true,"/data/local/tmp/mdds_matrix_"+run+"_A"),"case allowlist");
    check(!ValidMatrixRequest("M1",run,true,"/tmp/other"),"control directory scoped to current run");
    check(!ValidMatrixRequest("M1",run,true,"/data/local/tmp/mdds_matrix_"+run+"_B"),"side binding");
    std::vector<Config> out;
    check(BuildMatrixNativeConfigs("M1",run,true,seed,keys,out)&&out.size()==1&&out[0].additional_peers.empty(),"two readers one participant remain one route");
    check(BuildMatrixNativeConfigs("M2",run,true,seed,keys,out)&&out.size()==1&&out[0].additional_peers.size()==1,"two remote participants use one local and two peers");
    check(std::memcmp(out[0].peer_locator.address,out[0].additional_peers[0].locator.address,16)==0&&out[0].peer_locator.port!=out[0].additional_peers[0].locator.port,"same device distinct full locators");
    check(BuildMatrixNativeConfigs("M2",run,false,seed,keys,out)&&out.size()==2&&out[0].peer_locator==out[1].peer_locator&&out[0].local_locator.port!=out[1].local_locator.port,"two B participants independently address same A writer");
    check(BuildMatrixNativeConfigs("I2",run,true,seed,keys,out)&&out.size()==2&&out[0].local_locator!=out[1].local_locator&&out[0].local_service.find("00000049")!=std::string::npos&&out[1].local_service.find("0000004a")!=std::string::npos,"domain also binds service and identity");
    const auto saved=out[0].local_service;keys.pop_back();
    check(!BuildMatrixNativeConfigs("I2",run,true,seed,keys,out)&&out[0].local_service==saved,"missing domain rollback");
    check(!BuildMatrixNativeConfigs("WRONG",run,true,seed,keys,out),"no unknown topology");
    std::cout<<"matrix native configuration "<<tests<<"/11 PASS SDK_NOT_CALLED\n";return 0;
}
