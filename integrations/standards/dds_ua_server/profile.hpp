#pragma once
#include "../opcua_gateway/profile.hpp"
#include <sstream>
namespace d2 {
struct Config{unsigned domain,rep,port,capacity,run;std::string topic;std::filesystem::path stop;};
inline Config load(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("unreadable D2 config");
    std::string bytes(16385,'\0');in.read(bytes.data(),bytes.size());auto size=in.gcount();
    if(size>16384 || (!in.eof() && in.fail()))throw std::runtime_error("D2 config size/read bound");
    bytes.resize(static_cast<size_t>(size));std::istringstream input(bytes);std::map<std::string,std::string> f;std::string line;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty()||line[0]=='#')continue;
        auto p=line.find('=');if(p==std::string::npos||!d1::printable(line)||!f.emplace(line.substr(0,p),line.substr(p+1)).second)throw std::runtime_error("D2 config duplicate/invalid line");
    }
    auto take=[&](const char* k){auto it=f.find(k);if(it==f.end())throw std::runtime_error(std::string("missing ")+k);auto v=it->second;f.erase(it);return v;};
    if(take("direction")!="dds_to_ua")throw std::runtime_error("D2 direction must be dds_to_ua");
    Config c{};c.domain=d1::number(take("domain"),0,225);c.rep=d1::number(take("representation"),0,2);if(c.rep==1)throw std::runtime_error("representation 0 or 2");
    c.topic=take("topic");if(c.topic.empty()||c.topic.size()>128||c.topic.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("D2 topic bound");
    c.port=d1::number(take("port"),1024,65535);c.capacity=d1::number(take("max_instances"),1,64);c.run=d1::number(take("run_ms"),1000,600000);
    auto stop=take("stop_file");if(stop.empty()||stop.size()>1024||!std::filesystem::path(stop).is_absolute())throw std::runtime_error("D2 absolute stop path");c.stop=std::filesystem::path(stop);if(!f.empty())throw std::runtime_error("unknown D2 key");return c;
}
}
