#include "request_reply.hpp"
#include <iostream>
using namespace d3;
static Identity id(unsigned writer,uint32_t low=1,int32_t high=0){
    Identity result;result.writer_guid().guidPrefix.value[0]=static_cast<uint8_t>(writer);
    result.writer_guid().entityId.value[3]=2;result.sequence_number().high=high;result.sequence_number().low=low;return result;
}
int main(){try{
    auto now=Clock::time_point{};auto later=now+std::chrono::milliseconds(10);
    auto a=id(1),b=id(2),c=id(1,2),reply=id(3);
    require(valid_identity(a)&&!valid_identity(Identity::unknown()),"identity validity");
    require(a!=b&&identity_text(a)!=identity_text(b),"full GUID comparison");
    require(identity_text(id(1,3,1)).find(":4294967299")!=std::string::npos,"64 bit sequence");
    detail::CallTable table(2);Message value;value.operation(42);value.label("exact");value.payload({0,127,255});
    require(table.insert(a,later)&&table.insert(b,later)&&!table.insert(c,later),"bounded pending insertion");
    table.accept(b,reply,value,now);require(table.state(a,now)==Outcome::Pending&&table.state(b,now)==Outcome::Reply,"out of order correlation");
    Message evil=value;evil.label("wrong");table.accept(b,reply,evil,now);
    require(table.stats.duplicates==1&&!table.insert(c,later),"completion retains slot/duplicate");
    Result result;require(table.take(b,result,now)&&result.message.label()=="exact"&&result.message.payload()==value.payload(),"first reply stable");
    require(table.insert(c,later),"take releases capacity");
    table.accept(b,reply,evil,now);require(table.stats.duplicates==2,"retired duplicate");
    require(table.cancel(a,now)&&!table.cancel(a,now),"local cancellation");
    table.accept(a,reply,value,now);require(table.state(a,now)==Outcome::Cancelled&&table.stats.late==1,"cancelled stays terminal");
    table.accept(c,reply,value,later);require(table.state(c,later)==Outcome::Timeout&&table.stats.timeouts==1&&table.stats.late==2,"exact deadline wins");
    table.accept(id(9),reply,value,later);require(table.stats.unmatched==1,"unmatched reply");
    require(table.take(c,result,later)&&result.outcome==Outcome::Timeout,"timeout result");
    require(table.take(a,result,later)&&result.outcome==Outcome::Cancelled,"cancelled result");
    bool reused=false;try{table.insert(c,later);}catch(const std::runtime_error&){reused=true;}require(reused,"retired identity reuse rejected");
    auto d=id(1,4);require(table.insert(d,later),"new slot");table.shutdown();require(table.take(d,result,now)&&result.outcome==Outcome::Closed,"shutdown result");
    Message full;full.label(std::string(64,'x'));full.payload(std::vector<uint8_t>(4096,255));require(valid_message(full),"maximum message");
    full.payload().push_back(1);require(!valid_message(full),"payload overflow");full.payload().clear();full.label(std::string("a\0b",3));require(!valid_message(full),"NUL label");
    bool rejected=false;try{Config bad;bad.capacity=17;Client impossible(bad);}catch(const std::exception&){rejected=true;}
    require(rejected,"configuration before endpoint creation");
    std::cout<<"D3_CONTRACT PASS"<<std::endl;return 0;
}catch(const std::exception& error){std::cerr<<"D3_CONTRACT FAIL "<<error.what()<<std::endl;return 1;}}
