// Actual bounded formatters sharing stdout. No SDK, ROS, or device authority.
#include "../../examples/ros2_transport/FailureProtocol.hpp"
#include "../../integrations/ros2/ResidualDiagnostics.hpp"
#include <cassert>
#include <thread>
extern "C" void FastddsDsoftbusFailureSnapshot(std::uint64_t) noexcept {}
int main(){
    namespace d=fastdds_dsoftbus::ros2_teardown;
    assert(d::InitializeResiduals());
    for(unsigned i=0;i<8;++i){
        d::ResidualSnapshot s;s.known_mask=7;s.return_known=true;s.selected_return=4;
        s.counts[2]=32;s.stored=32;std::strcpy(s.reason,"ACTIVE_ENTITIES");
        for(unsigned k=0;k<32;++k){s.objects[k].kind=2;s.objects[k].object=1000+i*32+k;}
        assert(d::RecordResidual(s));
    }
    ros_failure_protocol::Report("1234567890abcdef","A",101,"TEARDOWN","PARTICIPANT_DELETE_FAILED");
    std::thread exporter([]{assert(d::WriteResiduals("1234567890abcdef",0,101,fastdds_dsoftbus::ros2_diagnostics::Stdout,nullptr));});
    for(unsigned i=0;i<256;++i)ros_failure_protocol::Report("1234567890abcdef","A",101,"TEARDOWN","PARTICIPANT_DELETE_FAILED",true);
    exporter.join();return 0;
}
