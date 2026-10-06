#include "DualScenarioChecks.hpp"
#include <cstdio>
#include <stdexcept>
using namespace fastdds_dsoftbus::experiment;
static void Require(bool value,const char* text) { if(!value) throw std::runtime_error(text); }
int main() {
    try {
        EndpointIdentity local{},remote_writer{},remote_reader{};
        local[0]=1;local[15]=3;remote_writer[0]=2;remote_writer[15]=3;remote_reader[0]=2;remote_reader[15]=4;
        Require(!DualRemoteEndpoint(true,local,local,true),"local endpoint must not satisfy remote matched reader contract");
        auto sibling=local;sibling[15]=4;
        Require(!DualRemoteEndpoint(true,local,sibling,true),"same Participant different entity remains local");
        Require(DualRemoteEndpoint(true,local,remote_reader,false),"different Participant remote reader accepted");
        Require(!DualRemoteEndpoint(true,local,remote_reader,true),"different Participant of this process is not remote board");
        Require(!DualRemoteEndpoint(false,local,remote_reader,false),"identity cannot repair invalid QoS");
        Require(DualRemotePair(true,local,remote_writer,remote_reader,false,false),"same remote Participant writer/reader pair accepted");
        Require(!DualRemotePair(true,local,local,sibling,true,true),"self pair must not satisfy remote pair contract");
        remote_reader[0]=3;
        Require(!DualRemotePair(true,local,remote_writer,remote_reader,false,false),"remote endpoints from different Participant cannot form fixed pair");
        remote_reader={};
        Require(!DualRemoteEndpoint(true,local,remote_reader,false),"unknown GUID rejected");
        Require(ValidDualOptions(true,0,false),"dual small sample opt-in accepted");
        Require(!ValidDualOptions(true,262144,false),"dual cannot silently add fragmentation");
        Require(!ValidDualOptions(true,0,true),"dual cannot silently add loss injection");
        Require(DualSlot(true,1)==1 && DualSlot(false,1)==2 && DualSlot(true,2)==3 && DualSlot(false,2)==4,
            "fixed pair slots are distinct while namespace/domain remain shared");
        Require(DualSlot(true,0)==0 && DualSlot(false,3)==0,"unsupported instance index rejected");
        DualProgress p;
        Require(!CanCreateSecond(p),"second requires first working");
        p.first_received=3;
        Require(!CanCreateSecond(p),"receive alone does not prove bidirectional operation");
        p.first_written=2;p.first_matching=true;
        Require(CanCreateSecond(p),"first two exact bidirectional samples admit second creation");
        p.first_received=0x3f;p.first_written=6;p.second_received=0x0f;p.second_written=4;
        p.second_matching=true;p.overlap_rounds=4;p.first_ack=p.second_ack=true;
        Require(CanDeleteFirst(p),"both groups active and acknowledged before local deletion");
        p.overlap_rounds=0;
        Require(!CanDeleteFirst(p),"serial activity cannot fake overlap");
        p.overlap_rounds=4;p.first_deleted=true;p.second_identity_unchanged=true;
        Require(!CanPublishAfterBothDeletes(p),"local deletion is not peer deletion");
        p.second_received=0x1f;
        Require(CanPublishAfterBothDeletes(p),"peer ordinary sample5 causally follows peer deletion");
        p.second_identity_unchanged=false;
        Require(!CanPublishAfterBothDeletes(p),"recreated second instance cannot count");
        p.second_identity_unchanged=true;p.second_received=0x3ff;p.second_written=10;p.after_both_delete_writes=5;
        p.second_deleted=true;p.process_budget_settled=true;
        Require(DualScenarioComplete(p),"complete isolated scenario admitted");
        p.invalid=1;
        Require(!DualScenarioComplete(p),"wrong or duplicated sample cannot count");
        p.invalid=0;p.second_deleted=false;
        Require(!DualScenarioComplete(p),"sample success cannot mask final close failure");
        std::puts("PASS dual experiment phase prerequisites and local-delete causality");return 0;
    } catch(const std::exception& e) { std::printf("FAIL %s\n",e.what());return 1; }
}
