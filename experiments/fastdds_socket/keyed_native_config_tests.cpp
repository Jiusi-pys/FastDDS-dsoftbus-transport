#include "KeyedNativeConfig.hpp"
#include <iostream>
#include <vector>
namespace ex=mdds::experiment;
namespace v1=mdds::dsoftbus::v1;
namespace {
unsigned checks=0,failures=0;
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
const std::string run="0123456789abcdef";
ex::Config Seed(bool a){
    ex::Config c;c.run_id=run;c.peer_network_id=std::string(64,a?'b':'a');
    mdds::dsoftbus::EndpointId local{},peer{};local[0]=a?1:2;peer[0]=a?2:1;
    v1::AddressCodec::EncodeLocator({local,a?1u:2u,v1::Profile::BytesNative},c.local_locator);
    v1::AddressCodec::EncodeLocator({peer,a?2u:1u,v1::Profile::BytesNative},c.peer_locator);
    ex::EncodeNativeServiceName({"exp-"+run,73,a?1u:2u,v1::Profile::BytesNative},c.local_service);
    ex::EncodeNativeServiceName({"exp-"+run,73,a?2u:1u,v1::Profile::BytesNative},c.peer_service);
    return c;
}
}
int main(){
    for(const char* id:{"K1","K2","K3","K4_TRUE","K4_FALSE","H1","H2","H3","R2","R3"}){
        Check(ex::NativeKeyedCase(id),"supported native case");
        for(bool a:{false,true})Check(ex::ValidKeyedRequest(id,run,a,"/data/local/tmp/mdds_keyed_"+run+(a?"_A":"_B")),"exact current window request");
    }
    for(const char* id:{"R1","M1","K4","RAW","K1;bad",""})Check(!ex::NativeKeyedCase(id),"unsupported native case rejected");
    Check(!ex::ValidKeyedRequest("K1","0123456789abcdeF",true,"/data/local/tmp/mdds_keyed_0123456789abcdeF_A"),"noncanonical run rejected");
    Check(!ex::ValidKeyedRequest("K1",run,true,"/data/local/tmp/mdds_keyed_"+run+"_B"),"wrong side rejected");
    Check(!ex::ValidKeyedRequest("K1",run,true,"/data/local/tmp/mdds_matrix_"+run+"_A"),"other fixture directory rejected");
    Check(!ex::ValidKeyedRequest("K1",run,true,"/data/local/tmp/mdds_keyed_"+run+"_A/../other"),"path traversal rejected");
    for(bool a:{false,true}){
        const auto seed=Seed(a);ex::Config out;
        Check(ex::BuildKeyedNativeConfig("K1",run,a,seed,out),"configured primary retained");
        Check(out.instance_name==(a?"A1":"B1")&&out.local_locator==seed.local_locator&&out.peer_locator==seed.peer_locator&&out.local_service==seed.local_service&&out.peer_service==seed.peer_service&&out.peer_network_id==seed.peer_network_id&&!out.enable_rx_drop&&out.additional_peers.empty(),"no key routes or mutation of native identity");
    }
    auto reject=[&](ex::Config bad,const char* why){ex::Config out;out.instance_name="untouched";Check(!ex::BuildKeyedNativeConfig("K1",run,true,bad,out)&&out.instance_name=="untouched",why);};
    auto bad=Seed(true);bad.enable_rx_drop=true;reject(bad,"drop capability forbidden");
    bad=Seed(true);bad.additional_peers.push_back({});reject(bad,"additional keyed peer forbidden");
    bad=Seed(true);bad.run_id="1111111111111111";reject(bad,"seed run mismatch");
    bad=Seed(true);bad.local_locator=bad.peer_locator;reject(bad,"same local remote locator rejected");
    bad=Seed(true);bad.local_service+="x";reject(bad,"service mapping mismatch");
    bad=Seed(true);bad.peer_network_id="short";reject(bad,"malformed directory identity");
    bad=Seed(true);bad.max_message_size=60001;reject(bad,"explicit message limit not silently changed");
    bad=Seed(false);reject(bad,"wrong deployment slots rejected");
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
