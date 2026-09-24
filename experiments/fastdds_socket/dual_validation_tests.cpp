#include "DualValidation.hpp"
#include "DualExecution.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace mdds::experiment;
static void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
static constexpr const char* run="0123456789abcdef";
static void Event(DualDiscovery& d,const EndpointIdentity& guid,EndpointKind kind,DiscoveryChange change=DiscoveryChange::Discovered) {
    namespace dds=eprosima::fastdds::dds;namespace rtps=eprosima::fastdds::rtps;
    if(kind==EndpointKind::Writer) {dds::PublicationBuiltinTopicData info;info.guid=DualGuid(guid);info.topic_name="mdds-0123456789abcdef";info.type_name="HelloWorld";
        d.Writer(change==DiscoveryChange::Discovered?rtps::WriterDiscoveryStatus::DISCOVERED_WRITER:change==DiscoveryChange::Updated?rtps::WriterDiscoveryStatus::CHANGED_QOS_WRITER:rtps::WriterDiscoveryStatus::REMOVED_WRITER,info);}
    else {dds::SubscriptionBuiltinTopicData info;info.guid=DualGuid(guid);info.topic_name="mdds-0123456789abcdef";info.type_name="HelloWorld";
        d.Reader(change==DiscoveryChange::Discovered?rtps::ReaderDiscoveryStatus::DISCOVERED_READER:change==DiscoveryChange::Updated?rtps::ReaderDiscoveryStatus::CHANGED_QOS_READER:rtps::ReaderDiscoveryStatus::REMOVED_READER,info);}
}
static void Pair(DualDiscovery& d,bool board,unsigned group) {
    Event(d,DualIdentity(run,board,group,EndpointKind::Writer),EndpointKind::Writer);
    Event(d,DualIdentity(run,board,group,EndpointKind::Reader),EndpointKind::Reader);
}
static void DiscoveryCase(const std::string& name) {
    DualDiscovery d;const auto writer=DualIdentity(run,false,2,EndpointKind::Writer);
    const auto reader=DualIdentity(run,false,2,EndpointKind::Reader);
    unsigned raw_participants=1,raw_writers=0,raw_readers=0;
    const auto pair=[&](bool board,unsigned group){Pair(d,board,group);++raw_writers;++raw_readers;};
    const auto valid=[&]{return d.Validate(raw_participants,raw_writers,raw_readers,writer,reader,"mdds-0123456789abcdef","HelloWorld");};
    if(name=="D1") {pair(true,2);pair(false,2);Require(valid(),"local plus expected remote must pass; raw discovery count is not remote identity");Require(d.Snapshot().raw_events==4,"raw events preserved");}
    else if(name=="D2") {pair(true,2);Require(!valid(),"only local cannot pass");}
    else if(name=="D3") {pair(true,2);pair(false,1);Require(!valid(),"equal raw count with wrong remote must fail");}
    else if(name=="D4") {pair(true,2);pair(false,2);Event(d,writer,EndpointKind::Writer);++raw_writers;Event(d,reader,EndpointKind::Reader,DiscoveryChange::Updated);Require(valid(),"duplicate/update must preserve correct current identity");Require(d.Snapshot().raw_events==6 && d.Snapshot().active.size()==4,"events preserved while GUIDs deduplicated");}
    else if(name=="D5") {pair(false,2);Event(d,writer,EndpointKind::Writer,DiscoveryChange::Removed);Require(!valid(),"historical removed expected writer cannot pass");Require(d.Snapshot().raw_events==3,"removal is a preserved event");}
    else if(name=="D6") {for(const auto& wrong: {std::pair<bool,unsigned>{true,1},{false,1},{true,2}}) {DualDiscovery other;Pair(other,wrong.first,wrong.second);Require(!other.Validate(1,1,1,writer,reader,"mdds-0123456789abcdef","HelloWorld"),"P2 cannot accept local P1, remote P1 or local P2");}}
    else if(name=="D7") {pair(true,2);pair(false,2);pair(false,1);Require(valid(),"unrelated discovery does not imply wrong match");d.RemoveParticipant(ParticipantIdentity(writer));Require(!valid(),"participant removal invalidates its active endpoints");}
    else if(name=="D8") {pair(false,2);eprosima::fastdds::dds::PublicationBuiltinTopicData info;info.guid=DualGuid(writer);info.topic_name="wrong-topic";info.type_name="HelloWorld";d.Writer(eprosima::fastdds::rtps::WriterDiscoveryStatus::CHANGED_QOS_WRITER,info);Require(!valid(),"updated wrong scope cannot pass stale original topic");}
    else if(name=="D9") {DualReadyState ready;ready.Peer();Require(!ready.Complete() && !ready.Sent(),"early peer READY does not grant local validation");Require(!ready.MarkSent(),"READY may not be emitted before validation");pair(false,2);Require(valid(),"current snapshot must pass before READY");ready.Validate();Require(ready.MarkSent(),"local validation permits ready send");Event(d,writer,EndpointKind::Writer,DiscoveryChange::Removed);Require(!valid(),"new current validation still rejects removed endpoint");Require(ready.Complete(),"sealed successful snapshot survives peer subsequent teardown");}
    else throw std::runtime_error("unknown discovery case");
}
static void StageCase(const std::string& name) {
    const unsigned n=static_cast<unsigned>(std::stoul(name.substr(1)));
    const DualStage failure=n==1?DualStage::SecondMatch:n==2?DualStage::OverlapData:n==3?DualStage::OverlapAck:
        n==4?DualStage::PreDeleteValidate:n==5?DualStage::P1Delete:n==8?DualStage::SecondCreate:n==9?DualStage::PostDeleteAck:n==10?DualStage::PostDeleteValidate:DualStage::PostDeleteData;
    DualExecution flow(run,"A");unsigned called=0;
    const bool pass=ExecuteDual(flow,[&](DualStage stage){++called;return stage==failure?
        DualStepResult::Fail("injected_predicate","expected_value","observed_value",2):DualStepResult::Pass();});
    Require(!pass && called>0,"injected real orchestration action must fail");
    Require(flow.first_failure.present && flow.first_failure.stage==failure,"first failure must capture actual stage, not stale second-match label");
    Require(flow.first_failure.predicate=="injected_predicate" && flow.first_failure.expected=="expected_value" &&
        flow.first_failure.observed=="observed_value" && flow.first_failure.side=="A" && flow.first_failure.run_id==run && flow.first_failure.instance==2,
        "first failure must preserve predicate and identity context");
    if(n==7) {flow.Cleanup(DualStepResult::Fail("cleanup_predicate","settled","active",1));Require(flow.cleanup_failure.present && flow.cleanup_failure.predicate=="cleanup_predicate","cleanup failure separately retained");Require(flow.first_failure.stage==failure && flow.first_failure.predicate=="injected_predicate","cleanup cannot overwrite first business failure");}
}
static void ExceptionCase(const std::string& name) {
    const auto stage=name=="X1"?DualStage::SecondCreate:name=="X2"?DualStage::OverlapData:DualStage::PreDeleteValidate;
    DualExecution flow(run,"B");bool escaped=false,result=true;
    try {result=ExecuteDual(flow,[&](DualStage current){if(current==stage) {if(name=="X4") return DualStepResult::Fail("business_predicate","true","false",0);throw std::runtime_error("injected_action_exception");}return DualStepResult::Pass();});}
    catch(...) {escaped=true;}
    std::printf("exception_case=%s operation_exception_escaped=%d first_failure_present=%d actual_phase_captured=%d\n",
        name.c_str(),escaped,flow.first_failure.present,flow.first_failure.stage==stage);
    Require(!escaped && !result,"action exception must become a failed outcome at the actual stage");
    if(name!="X4") Require(flow.first_failure.present && flow.first_failure.stage==stage && flow.first_failure.predicate=="action_exception" &&
        flow.first_failure.expected=="normal_return" && flow.first_failure.observed=="std_exception" && flow.first_failure.run_id==run &&
        flow.first_failure.side=="B" && flow.first_failure.instance==(stage==DualStage::SecondCreate?2u:0u),
        "action exception must preserve stage, category and identity context");
    if(name=="X4") {
        bool second_attempted=false,settlement_attempted=false,cleanup_result=true;escaped=false;
        try {cleanup_result=ExecuteDualCleanup(flow,[&](unsigned instance){if(instance==1) throw std::runtime_error("injected_cleanup_exception");
            if(instance==2) second_attempted=true;else if(instance==0) settlement_attempted=true;return DualStepResult::Pass();});}
        catch(...) {escaped=true;}
        std::printf("cleanup_exception_escaped=%d second_cleanup_attempted=%d settlement_attempted=%d cleanup_failure_present=%d\n",
            escaped,second_attempted,settlement_attempted,flow.cleanup_failure.present);
        Require(!escaped && !cleanup_result && second_attempted && settlement_attempted,"exceptional P1 cleanup must not skip P2 or settlement observation");
        Require(flow.cleanup_failure.present && flow.cleanup_failure.stage==DualStage::FinalCleanup && flow.cleanup_failure.instance==1 &&
            flow.cleanup_failure.predicate=="cleanup_exception" && flow.cleanup_failure.observed=="std_exception","cleanup exception independently captured");
        Require(flow.first_failure.stage==stage && flow.first_failure.predicate=="business_predicate","cleanup must not override the business failure");
    }
}
int main(int argc,char** argv) {try {Require(argc==2,"case required");const std::string name=argv[1];if(name[0]=='D') DiscoveryCase(name);else if(name[0]=='X') ExceptionCase(name);else StageCase(name);std::printf("PASS %s shared actual experimental validation/orchestration path\n",name.c_str());return 0;}catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}}
