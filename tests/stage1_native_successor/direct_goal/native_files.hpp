#pragma once
#include "owned_flow.hpp"
#include "../entry_admission.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
namespace stage1_goal {
// One implementation TU includes the historical non-inline SHA header. This
// internal SDK-ABI facade avoids an ODR collision without altering frozen code.
namespace pure_hash {std::string digest(const void*,std::size_t);}
using Clock=std::chrono::steady_clock;
uint64_t now_ns();
std::string file_hash(const std::string&,size_t=16*1048576);
std::string image_path();
std::string self_image_hash();
int sealed_image(const std::string&,const std::string&);
Point native_point();
bool single_main();
class Files final {
    struct State;std::unique_ptr<State> s_;
public:
    explicit Files(std::string root);~Files();Files(const Files&)=delete;Files& operator=(const Files&)=delete;
    const std::string& root()const;
    void publish(const std::string&,const std::vector<uint8_t>&);
    void publish(const std::string& n,const std::string& v){publish(n,std::vector<uint8_t>(v.begin(),v.end()));}
    std::vector<uint8_t> read(const std::string&,size_t)const;
    bool wait(const std::string&,Clock::time_point,int cancel_fd=-1)const;
};
struct Scalar {
    using Get=uint64_t(*)();using Set=int32_t(*)(uint64_t);
    void* retained=nullptr;Get get_fn=nullptr;Set set_fn=nullptr;
    explicit Scalar(const char* expected_provider_sha);
    uint64_t get()const;int set(uint64_t)const;Point point()const{return native_point();}
};
int sealed_state(const ResumeState&);
ResumeState read_state(int);
Admission bound_admission(const stage1_entry::Record&,const Point&,const char* digest,bool actual_fixed_lookup);
bool query_owned_child(const stage1_entry::Record&,uint64_t original,uint64_t until,std::string& observation);
std::string text(const std::vector<uint8_t>&);
}
