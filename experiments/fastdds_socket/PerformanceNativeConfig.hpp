#pragma once
#include "KeyedNativeConfig.hpp"
namespace fastdds_dsoftbus::experiment {
struct PerformanceRequest { std::string mode, directory; unsigned repeat=0; };
inline bool ParsePerformanceRequest(int count,const char*const* values,const std::string& run,bool board_a,PerformanceRequest& out) {
    if(count!=6||!values||!KeyedHex(run,16)||run==std::string(16,'0'))return false;
    for(int i=0;i<count;++i)if(!values[i])return false;
    if(std::string(values[0])!="--performance-mode"||std::string(values[2])!="--repeat"||std::string(values[4])!="--control-dir")return false;
    const std::string mode=values[1],repeat=values[3],directory=values[5];
    if((mode!="TRACE"&&mode!="MEASURE")||(repeat!="1"&&repeat!="2"&&repeat!="3")||(mode=="TRACE"&&repeat!="1")||
            directory!="/data/local/tmp/mdds_perf_"+run+(board_a?"_A":"_B"))return false;
    out={mode,directory,static_cast<unsigned>(repeat[0]-'0')};return true;
}
inline bool BuildPerformanceNativeConfig(const PerformanceRequest& request,const std::string& run,bool board_a,const Config& seed,Config& out) {
    const auto repeat=std::to_string(request.repeat);
    const char* args[]={"--performance-mode",request.mode.c_str(),"--repeat",repeat.c_str(),"--control-dir",request.directory.c_str()};
    PerformanceRequest checked;if(!ParsePerformanceRequest(6,args,run,board_a,checked))return false;
    // Reuse the existing one-participant/single-peer experiment address checks.
    // K1 selects that existing validator; no business key is inspected or routed.
    return BuildKeyedNativeConfig("K1",run,board_a,seed,out);
}
}
