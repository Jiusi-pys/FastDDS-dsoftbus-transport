#pragma once
#include <cstdint>
#include <string>
namespace stage1_goal {
enum class GoalPhase:uint32_t {Entry=0,Scalar=1,Boot=2,Admission=3,Query=4,Image=5,Select=6,State=7,Exec=8,Resume=9,Self=10,Probe=11,LocalNode=12,NodeKey=13,Pair=14,Api=15,Create=16,Watch=17,Execute=18,Drain=19,Destroy=20,Restore=21,Result=22};
struct GoalDiagnostic {
    GoalPhase phase=GoalPhase::Entry;bool rc_valid=false;int32_t native_rc=0;
    void at(GoalPhase p){phase=p;rc_valid=false;native_rc=0;}
    void returned(int32_t rc){native_rc=rc;rc_valid=true;}
    std::string fields()const{return "phase="+std::to_string(static_cast<uint32_t>(phase))+"\nnative_rc_valid="+std::to_string(rc_valid)+"\nnative_rc="+std::to_string(native_rc)+"\n";}
};
}
