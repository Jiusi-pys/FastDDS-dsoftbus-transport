#pragma once
#include <string>
#include <stdexcept>
namespace stage1_goal {
// Bounded LF record lookup at line starts only: uid must not match euid.
inline std::string goal_field(const std::string& b,const char* name){
    if(b.empty()||b.size()>4096||b.back()!='\n'||!name||!*name)throw std::invalid_argument("GOAL_FIELD_FRAME");
    for(unsigned char c:b)if(c!='\n'&&(c<33||c>126))throw std::invalid_argument("GOAL_FIELD_TEXT");
    const auto key=std::string(name)+"=";std::string result;bool found=false;unsigned count=0;
    for(size_t at=0;at<b.size();){auto end=b.find('\n',at);if(end==std::string::npos||end==at||end-at>1024||++count>32)throw std::invalid_argument("GOAL_FIELD_BOUND");
        if(b.compare(at,key.size(),key)==0){if(found)throw std::invalid_argument("GOAL_FIELD_DUPLICATE");result=b.substr(at+key.size(),end-at-key.size());if(result.empty())throw std::invalid_argument("GOAL_FIELD_EMPTY");found=true;}
        at=end+1;
    }
    if(!found)throw std::invalid_argument("GOAL_FIELD_MISSING");return result;
}
}
