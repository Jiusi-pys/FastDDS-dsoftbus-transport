#include "HotScenario.hpp"
#include "DualValidation.hpp"
#include <cstdio>
#include <stdexcept>
using namespace fastdds_dsoftbus::experiment;
int main(int argc,char** argv) {
    const std::string name=argc>1?argv[1]:""; bool pass=false;
    HotIdentitySnapshot a{"same",1,11,21,31,7,1,3,4},b=a,q{"Q",1,12,22,32,8,2,5,6};
    b.application=13;b.receiver=23;b.token=33;
    if(name=="HID1") pass=HotReplacement(a,b);
    if(name=="SNAPSHOT") {
        struct NativeFields{std::uint64_t runtime_id=1,application_incarnation=2,receiver_incarnation=3,logical_token=4,instance_id=5,receiver_lease_id=6;int server_callback_slot=7,client_callback_slot=8;} fields;
        const auto pg=ProgramIdentity("0123456789abcdef",true,2,0,true,EndpointKind::Participant),wg=ProgramIdentity("0123456789abcdef",true,2,0,true,EndpointKind::Writer),rg=ProgramIdentity("0123456789abcdef",true,2,0,true,EndpointKind::Reader);
        const auto value=MakeHotIdentity("Q",fields,pg,wg,rg);
        auto replaced=wg;++replaced[10];const auto other=MakeHotIdentity("Q",fields,pg,replaced,rg);
        pass=value.participant==pg&&value.writer==wg&&value.reader==rg&&value.application==2&&value.receiver==3&&value.token==4&&!HotContinuous(value,other);
    }
    if(name=="HID2") {pass=true;for(unsigned field=0;field<3;++field){auto bad=b;if(field==0)bad.application=a.application;if(field==1)bad.receiver=a.receiver;if(field==2)bad.token=a.token;pass=pass&&!HotReplacement(a,bad);} }
    if(name=="HID3"||name=="HID4") {
        DualDiscovery old,current;const auto w=DualIdentity("0123456789abcdef",false,1,EndpointKind::Writer),r=DualIdentity("0123456789abcdef",false,1,EndpointKind::Reader);
        auto put=[&](DualDiscovery& d,DiscoveryChange change){d.Endpoint(change,w,ParticipantIdentity(w),EndpointKind::Writer,"T","H");d.Endpoint(change,r,ParticipantIdentity(r),EndpointKind::Reader,"T","H");};
        put(old,DiscoveryChange::Discovered);const bool historical=old.Validate(1,2,2,w,r,"T","H");
        const bool empty=HotFreshDiscovery(current.Snapshot().raw_events,current.Validate(0,0,0,w,r,"T","H"),true);
        put(current,DiscoveryChange::Discovered);const bool fresh=HotFreshDiscovery(current.Snapshot().raw_events,current.Validate(1,2,2,w,r,"T","H"),true);
        put(current,DiscoveryChange::Removed);const bool removed=HotFreshDiscovery(current.Snapshot().raw_events,current.Validate(1,2,2,w,r,"T","H"),true);
        pass=historical&&!empty&&fresh&&!removed&&old.Snapshot().raw_events==2&&current.Snapshot().raw_events==4;
    }
    if(name=="HID5") {pass=HotContinuous(q,q);for(unsigned f=0;f<10;++f){auto bad=q;if(f==0)++bad.runtime;if(f==1)++bad.application;if(f==2)++bad.receiver;if(f==3)++bad.token;if(f==4)++bad.native_endpoint;if(f==5)++bad.server_entry;if(f==6)++bad.client_entry;if(f==7)++bad.participant[0];if(f==8)++bad.writer[0];if(f==9)++bad.reader[0];pass=pass&&!HotContinuous(q,bad);} }
    if(name=="OPTIONS") pass=ValidHotCycles(1)&&ValidHotCycles(10)&&ValidHotCycles(100)&&!ValidHotCycles(0)&&!ValidHotCycles(2)&&!ValidHotCycles(101);
    if(name=="FIXTURE") {
        const std::string run="0123456789abcdef";const auto base=ProgramIdentity(run,true,1,0,true,EndpointKind::Writer);
        pass=ProgramIdentity(run,true,1,0,false,EndpointKind::Writer)==DualIdentity(run,true,1,EndpointKind::Writer);
        std::map<EndpointIdentity,unsigned> identities;
        for(unsigned generation=0;generation<=100;++generation){const auto id=ProgramIdentity(run,true,1,generation,true,EndpointKind::Writer);pass=pass&&std::equal(id.begin(),id.begin()+8,base.begin())&&identities.emplace(id,generation).second;}
        pass=pass&&ProgramIdentity(run,false,1,0,true,EndpointKind::Writer)!=base&&ProgramIdentity(run,true,2,0,true,EndpointKind::Writer)!=base;
    }
    if(name=="LEDGER") {HotDataLedger ledger(201);pass=ledger.Accept(1,true)&&!ledger.Accept(1,true)&&!ledger.Accept(202,true)&&!ledger.Accept(2,false)&&ledger.Count()==1&&!ledger.Complete(1);HotDataLedger good(201);for(unsigned i=1;i<=201;++i)pass=pass&&good.Accept(i,true);pass=pass&&good.Complete(201);}
    if(name=="READY") {HotReadyLedger ledger(100);pass=ledger.Receive(1,0)&&!ledger.Complete(1,0)&&ledger.Seal(1,0)&&ledger.Sent(1,0)&&ledger.Complete(1,0)&&!ledger.Complete(2,0)&&!ledger.Receive(1,0)&&!ledger.Receive(101,0)&&!ledger.Receive(0,3);}
    if(name=="FAILURE") {HotExecution f("run","A");pass=!f.Step("HOT_REMATCH",3,1,[]{return DualStepResult::Fail("fresh_match","true","false",1);});f.Cleanup(DualStepResult::Fail("close","true","false",0));pass=pass&&f.first_failure.stage=="HOT_REMATCH"&&f.first_failure.cycle==3&&f.first_failure.predicate=="fresh_match"&&f.cleanup_failure.present;}
    if(name=="EXCEPTION") {HotExecution f("run","A");pass=!f.Step("HOT_NEW_DATA",4,1,[]()->DualStepResult{throw std::runtime_error("x");});pass=pass&&f.first_failure.stage=="HOT_NEW_DATA"&&f.first_failure.predicate=="action_exception";}
    std::printf("hot_case=%s result=%s\n",name.c_str(),pass?"PASS":"FAIL");return pass?0:1;
}
