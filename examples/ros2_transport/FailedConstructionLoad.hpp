#pragma once
// Explicit test-only observation after failed NODE_CREATE unwind. No ROS/SDK
// operations, object cleanup, progress callbacks or participant retry belong here.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
namespace ros_failure_protocol {
inline bool CaptureFailedConstructionLoad(const std::string& run,const std::string& side,long pid,
        std::uint64_t failure_ref,std::chrono::milliseconds budget=std::chrono::seconds(90)) noexcept {
    const char* enabled=std::getenv("FDDST_ROS_FAILURE_LOAD_CAPTURE");
    if(!enabled)return true; // Ordinary/default processes have no diagnostic hold.
    const char* result="INPUT_INVALID";
    try {
        const char* prefix=std::getenv("FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX");
        if(std::string(enabled)!="1"||!prefix||!*prefix||run.size()!=16||
                !std::all_of(run.begin(),run.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||
                (side!="A"&&side!="B")||pid<1||failure_ref<1||budget.count()<1)throw 0;
        std::fprintf(stdout,"ROS_FAILURE_LOAD_READY schema=1 run=%s side=%s pid=%ld failure_ref=%llu stage=FAILED_CONSTRUCTION\n",
            run.c_str(),side.c_str(),pid,static_cast<unsigned long long>(failure_ref));std::fflush(stdout);
        const auto expected="ROS_FAILURE_LOAD_RELEASE schema=1 run="+run+" side="+side+" pid="+std::to_string(pid)+
            " failure_ref="+std::to_string(failure_ref)+" stage=FAILED_CONSTRUCTION reason=";
        const auto end=std::chrono::steady_clock::now()+budget;
        result="ACK_TIMEOUT";
        while(std::chrono::steady_clock::now()<end) {
            std::ifstream file(std::string(prefix)+".FAILED_CONSTRUCTION_LOAD",std::ios::binary);
            if(file) {
                char line[512]{};
                if(!file.getline(line,sizeof(line))||file.peek()!=std::char_traits<char>::eof()){result="ACK_INVALID";break;}
                std::string value(line);if(!value.empty()&&value.back()=='\r')value.pop_back();
                result=value==expected+"CAPTURE_COMPLETE"?"CAPTURE_COMPLETE":value==expected+"CAPTURE_FAILED"?"CAPTURE_FAILED":"ACK_INVALID";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(...){result="INPUT_OR_CAPTURE_EXCEPTION";}
    std::fprintf(stdout,"ROS_FAILURE_LOAD_RESULT schema=1 run=%s side=%s pid=%ld failure_ref=%llu stage=FAILED_CONSTRUCTION result=%s\n",
        run.c_str(),side.c_str(),pid,static_cast<unsigned long long>(failure_ref),result);std::fflush(stdout);
    // This result is observation only; the caller retains its original error/4.
    return std::strcmp(result,"CAPTURE_COMPLETE")==0;
}
}
