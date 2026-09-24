#include "HotClose.hpp"
#include <cstdio>
#include <string>
using namespace mdds::experiment;
struct CloseFixture {
    ParticipantCloseState state;
    bool participant_handle=true,writer_handle=true,children_alive=true,participant_alive=true;
    bool writer_delete_ok=false,peer_drain_ok=true,contained_ok=true,participant_ok=true;
    unsigned drains=0,contained_calls=0,participant_calls=0,observes=0,stale_access=0;
    bool Close(){return ExecuteParticipantClose(state,[&]{return participant_handle;},[&]{return writer_handle;},[&]{
        ++drains;if(!children_alive||!participant_alive){++stale_access;return false;}
        if(writer_delete_ok)writer_handle=false;return writer_delete_ok&&peer_drain_ok;
    },[&]{++contained_calls;if(contained_ok)children_alive=false;return contained_ok;},[&]{
        ++participant_calls;if(participant_ok){children_alive=false;participant_alive=false;}return participant_ok;
    },[&]{writer_handle=false;},[&]{participant_handle=false;},[&]{++observes;return !participant_alive;});}
};
int main(int argc,char** argv){const std::string name=argc>1?argv[1]:"";CloseFixture f;bool pass=false;
    if(name=="writer-error-physical-delete") {const bool first=f.Close(),second=f.Close();pass=!first&&!second&&!f.writer_handle&&!f.participant_handle&&f.drains==1&&f.participant_calls==1&&!f.stale_access;}
    if(name=="peer-drain-error-sticky") {f.writer_delete_ok=true;f.peer_drain_ok=false;const bool first=f.Close(),second=f.Close();pass=!first&&!second&&f.drains==1&&f.observes==1;}
    if(name=="contained-delete-participant-retry") {f.participant_ok=false;const bool first=f.Close();f.participant_ok=true;const bool second=f.Close();pass=!first&&!second&&!f.stale_access&&!f.writer_handle&&f.drains==1&&f.participant_calls==2;}
    if(name=="successful-idempotence") {f.writer_delete_ok=true;const bool first=f.Close(),second=f.Close();pass=first&&second&&f.drains==1&&f.contained_calls==1&&f.participant_calls==1&&f.observes==1&&!f.writer_handle;}
    std::printf("close_case=%s result=%s drains=%u stale=%u physical_deletes=%u observes=%u\n",name.c_str(),pass?"PASS":"FAIL",f.drains,f.stale_access,f.participant_calls,f.observes);return pass?0:1;
}
