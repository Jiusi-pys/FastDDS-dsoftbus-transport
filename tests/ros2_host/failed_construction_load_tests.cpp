// Actual diagnostic helper, bounded local ACK fixtures; no ROS/SDK/device.
#include "../../examples/ros2_transport/FailedConstructionLoad.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    const std::string mode=argv[1],prefix=argv[2];
#if defined(_WIN32)
    _putenv_s("FDDST_ROS_FAILURE_LOAD_CAPTURE",mode=="default"?"":"1");
    _putenv_s("FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX",prefix.c_str());
#else
    if(mode=="default")unsetenv("FDDST_ROS_FAILURE_LOAD_CAPTURE");else setenv("FDDST_ROS_FAILURE_LOAD_CAPTURE","1",1);
    setenv("FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX",prefix.c_str(),1);
#endif
    if(mode!="default"&&mode!="timeout") {
        std::ofstream out(prefix+".FAILED_CONSTRUCTION_LOAD");
        out<<"ROS_FAILURE_LOAD_RELEASE schema=1 run=1234567890abcdef side=A pid="<<(mode=="badpid"?999:123)
            <<" failure_ref="<<(mode=="badref"?8:7)<<" stage=FAILED_CONSTRUCTION reason="
            <<(mode=="failed"?"CAPTURE_FAILED":"CAPTURE_COMPLETE")<<'\n';
        if(mode=="multiline")out<<"extra\n";
    }
    const bool accepted=ros_failure_protocol::CaptureFailedConstructionLoad("1234567890abcdef","A",123,7,std::chrono::milliseconds(30));
    const bool expected=mode=="default"||mode=="complete";
    if(accepted!=expected)return 1;
    // Acceptance of observation cannot turn construction error into success.
    std::cout<<"FAILED_CONSTRUCTION_LOAD_TEST result=PASS application_result=4 scope=HOST_COMPONENT_NOT_ROS_SDK_DEVICE\n";
    return 0;
}
