#pragma once
#include "BridgeOwner.hpp"
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#if defined(_WIN32)
#include <process.h>
extern "C" __declspec(dllimport) int FastddsDsoftbusFactoryOwnerCreated(const void*) noexcept;
#else
#include <unistd.h>
extern "C" int FastddsDsoftbusFactoryOwnerCreated(const void*) noexcept;
#endif

namespace fastdds_dsoftbus::ros2_integration {
// Runs on the original DDS shutdown control thread. The factory mutex may still
// be held: never call factory APIs or full Runtime observation from this seam.
inline void InstallShutdownCoordination(BridgeOwner& owner,const void* owner_identity,const std::string& path) {
    const auto pid=static_cast<long>(
#if defined(_WIN32)
            _getpid()
#else
            getpid()
#endif
            );
    if(!dsoftbus::phase0::RuntimeProbeAccess::BeforeNativeClose(*owner.runtime,
            [&owner,owner_identity,path,pid](const dsoftbus::host_test::RuntimeShutdownPoint& point) {
        // An attached autoenable failure was never a successful factory return.
        if(FastddsDsoftbusFactoryOwnerCreated(owner_identity)!=1)return;
        if(!point.stopping||!point.sealed||point.active_pumps!=0)
            throw std::logic_error("shutdown observation point");
        owner.stop=true;
        if(owner.pump.joinable()) {
            if(owner.pump.get_id()==std::this_thread::get_id())
                throw std::logic_error("shutdown self join refused");
            owner.pump.join(); // No loop/adapter/hook/module-copy lock is held.
        }
        const char side=owner.side?'B':'A';
        bool normal=false;
        const char* failure="CONTROL_MISSING_OR_INVALID";
        try {
            if(std::fprintf(stdout,"ROS_RUNTIME_CLOSE_READY schema=1 run=%s side=%c pid=%ld stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1\n",owner.run.c_str(),side,pid)<0||std::fflush(stdout)!=0)
                throw std::runtime_error("ready output");
            const auto prefix="ROS_RUNTIME_CLOSE_RELEASE schema=1 run="+owner.run+" side="+side+
                    " pid="+std::to_string(pid)+" stage=RUNTIME_CLOSE reason=";
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(std::chrono::steady_clock::now()<deadline) {
                std::ifstream input(path,std::ios::binary);
                if(input) {
                    char buffer[512]{};
                    if(!input.getline(buffer,sizeof(buffer))||input.peek()!=std::char_traits<char>::eof())break;
                    std::string row(buffer);if(!row.empty()&&row.back()=='\r')row.pop_back();
                    if(row==prefix+"NORMAL_COMPLETION")normal=true;
                    else if(row==prefix+"WINDOW_FAILED")failure="WINDOW_FAILED";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }catch(...){failure="CONTROL_CAPTURE_EXCEPTION";}
        if(!normal) {
            owner.failed=true;
            std::fprintf(stderr,"ROS_RUNTIME_CLOSE_FAILURE schema=1 run=%s side=%c pid=%ld stage=RUNTIME_CLOSE reason=%s\n",owner.run.c_str(),side,pid,failure);
            std::fflush(stderr);
        }
        // Coordination cannot veto original local adapter cleanup. Its failure
        // remains an execution failure, separate from actual SDK close results.
    }))throw std::runtime_error("shutdown observer installation refused");
}
}
