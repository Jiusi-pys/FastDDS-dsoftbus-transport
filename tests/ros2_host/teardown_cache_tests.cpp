#include "../../integrations/ros2/TeardownDiagnostics.hpp"
#include <cassert>
#include <string>
#include <iostream>
namespace td=fastdds_dsoftbus::ros2_teardown;
struct Code {unsigned n;unsigned operator()()const{return n;}};
bool reject(const char*,std::size_t,void*) {return false;}
bool throws(const char*,std::size_t,void*) {throw 1;}
bool collect(const char* p,std::size_t n,void* context){static_cast<std::string*>(context)->append(p,n);return true;}
int flush_error(std::FILE*) {return EOF;}
int main(){
 std::atexit([]{td::Snapshot value;assert(td::Capture(&value));assert(value.first_present&&value.first.value==4);std::cout<<"TEARDOWN_EXIT_ORDER_CAPTURE_PASS\n";});
 td::Begin("1234567890abcdef",0);
 const auto token=reinterpret_cast<void*>(std::uintptr_t(0xdeadbeef));
 {td::Call call("Publisher_delete_datawriter",token,token);assert(call.Return(Code{4}).n==4);}
 {td::Call call("Factory_delete_participant",token,token);assert(call.Return(Code{4},"ACTIVE_ENTITIES","PUBLIC_RETURN_CODE").n==4);}
 for(unsigned i=0;i<td::capacity+20;++i)td::Record("later_result",token,token,"DDS_RETURN_CODE",true,1,0,"INJECTED_TEST");
 td::Snapshot s;assert(td::Capture(&s));assert(s.first_present&&s.first.value==4);
 assert(std::string(s.first.operation)=="Publisher_delete_datawriter");
 assert(s.first.object==0xdeadbeef&&s.count==td::capacity&&s.overflow>0);
 assert(!td::Write(s,"UNIT_TEST",reject,nullptr));assert(!td::Write(s,"UNIT_TEST",throws,nullptr));
 std::string output;assert(td::Write(s,"UNIT_TEST",collect,&output));
 assert(output.find("reserved_first=1")!=std::string::npos&&output.find("completeness=INCOMPLETE")!=std::string::npos);
 assert(output.find("value=4")!=std::string::npos);
 td::ControlThread();td::FlushControl("FLUSH_FAILURE_TEST",false,flush_error);
 td::Snapshot after;assert(td::Capture(&after));assert(after.output_errors==1);
 assert(td::store().exported==0&&!td::store().first_exported);
 // No subject dereference: tokens already stand for retired resources.
 std::cout<<"TEARDOWN_CACHE_PASS first_preserved=1 fixed_capacity=256 unsafe_token_dereference=0 output_failure_observed=1\n";
}
