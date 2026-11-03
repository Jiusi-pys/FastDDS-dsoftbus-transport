#include "PerformanceNativeConfig.hpp"
#include <cstdio>
#include <stdexcept>
using namespace fastdds_dsoftbus::experiment;
static unsigned checks=0;
static void Need(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
int main(){try{
    const std::string run="0123456789abcdef";
    for(bool a:{true,false}){
        const std::string dir="/data/local/tmp/mdds_perf_"+run+(a?"_A":"_B");
        for(const char* mode:{"TRACE","MEASURE"})for(const char* repeat:{"1","2","3"}){
            const char* av[]={"--performance-mode",mode,"--repeat",repeat,"--control-dir",dir.c_str()};
            PerformanceRequest request;const bool allowed=std::string(mode)=="MEASURE"||std::string(repeat)=="1";
            Need(ParsePerformanceRequest(6,av,run,a,request)==allowed,"exact finite mode/repeat policy");
            if(!allowed)continue;
            Config c;c.run_id=run;c.peer_network_id=std::string(64,'a');
fastdds_dsoftbus::dsoftbus::EndpointId local{},peer{};local[0]=1;peer[0]=2;
using C=fastdds_dsoftbus::dsoftbus::v1::AddressCodec;using P=fastdds_dsoftbus::dsoftbus::v1::Profile;
            Need(C::EncodeLocator({local,a?1u:2u,P::BytesNative},c.local_locator),"local");
            Need(C::EncodeLocator({peer,a?2u:1u,P::BytesNative},c.peer_locator),"peer");
            Need(EncodeNativeServiceName({"exp-"+run,73,a?1u:2u,P::BytesNative},c.local_service),"local service");
            Need(EncodeNativeServiceName({"exp-"+run,73,a?2u:1u,P::BytesNative},c.peer_service),"peer service");
            Config out;Need(BuildPerformanceNativeConfig(request,run,a,c,out),"dedicated native config");
            Need(out.local_locator==c.local_locator&&out.peer_locator==c.peer_locator&&!out.enable_rx_drop,"unchanged locator and loss capability");
            c.max_message_size=60001;Need(!BuildPerformanceNativeConfig(request,run,a,c,out),"do not enlarge transport budget");
            c.max_message_size=60000;c.enable_rx_drop=true;Need(!BuildPerformanceNativeConfig(request,run,a,c,out),"no controlled drop");
        }
        for(const char* mode:{"RAW","trace","MEASURE+DROP"}){
            const char* av[]={"--performance-mode",mode,"--repeat","1","--control-dir",dir.c_str()};PerformanceRequest p;
            Need(!ParsePerformanceRequest(6,av,run,a,p),"mode rejected before SDK");
        }
        const char* bad[]={"--performance-mode","MEASURE","--repeat","4","--control-dir",dir.c_str()};PerformanceRequest p;
        Need(!ParsePerformanceRequest(6,bad,run,a,p),"no extra repetitions");
        bad[3]="1";bad[5]="/data/local/tmp/other";Need(!ParsePerformanceRequest(6,bad,run,a,p),"owned control directory");
        bad[5]=dir.c_str();Need(!ParsePerformanceRequest(5,bad,run,a,p),"exact argument count");
        Need(!ParsePerformanceRequest(6,bad,"stale",a,p),"valid fresh run shape");
        const std::string zero_run(16,'0'),zero_dir="/data/local/tmp/mdds_perf_"+zero_run+(a?"_A":"_B");
        bad[5]=zero_dir.c_str();
        Need(!ParsePerformanceRequest(6,bad,zero_run,a,p),"zero run rejected before SDK, matching measurement core");
    }
    std::printf("PASS checks=%u\n",checks);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAIL check=%u %s\n",checks,e.what());return 1;}}
