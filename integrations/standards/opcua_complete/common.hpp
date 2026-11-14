#pragma once
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace d4 {
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
inline void check(bool condition,const char* error){if(!condition)throw std::runtime_error(error);}
inline void good(UA_StatusCode code){if(code!=UA_STATUSCODE_GOOD)throw std::runtime_error(UA_StatusCode_name(code));}
inline unsigned number(const std::string& text,unsigned maximum=60000){
    check(!text.empty()&&text.size()<=10&&text.find_first_not_of("0123456789")==std::string::npos,"D4 unsigned integer");
    auto value=std::stoull(text);check(value<=maximum,"D4 integer bound");return static_cast<unsigned>(value);
}
inline std::string hex(const std::vector<uint8_t>& bytes){
    std::string out;const char* digits="0123456789abcdef";
    for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;
}
inline std::vector<uint8_t> unhex(const std::string& text,size_t limit){
    check(text.size()%2==0&&text.size()<=limit*2,"D4 hex length");std::vector<uint8_t> out;
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("D4 hex digit");};
    for(size_t n=0;n<text.size();n+=2)out.push_back(static_cast<uint8_t>(digit(text[n])*16+digit(text[n+1])));return out;
}
inline std::vector<std::string> lines(const fs::path& file,size_t max_size=9000){
    check(fs::file_size(file)<=max_size,"D4 control size");std::ifstream stream(file);check(bool(stream),"D4 control file");std::vector<std::string> values;std::string line;
    while(std::getline(stream,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();values.push_back(line);}check(stream.eof(),"D4 control read");return values;
}
struct Config{
    unsigned domain=219,rep=2,port=4847,run_ms=90000,capacity=8,retention_ms=2000;
    std::string service="D4",topic="D4Record",endpoint="opc.tcp://127.0.0.1:4848";
    fs::path stop;
};
inline Config config(const fs::path& file){
    auto values=lines(file,4096);std::map<std::string,std::string> items;
    for(const auto& value:values){auto at=value.find('=');check(at!=std::string::npos&&items.emplace(value.substr(0,at),value.substr(at+1)).second,"D4 duplicate/config key");}
    check(items.size()==10,"D4 exact configuration keys");
    Config c;c.domain=number(items.at("domain"),225);c.rep=number(items.at("rep"),2);check(c.rep==0||c.rep==2,"D4 representation");
    c.port=number(items.at("port"),65535);check(c.port>=1024,"D4 port");c.run_ms=number(items.at("run_ms"),300000);check(c.run_ms>=1000,"D4 run budget");
    c.capacity=number(items.at("capacity"),16);check(c.capacity>=1,"D4 capacity");c.retention_ms=number(items.at("retention_ms"),60000);check(c.retention_ms>=100,"D4 retention");
    c.service=items.at("service");c.topic=items.at("topic");c.endpoint=items.at("endpoint");c.stop=fs::path(items.at("stop"));
    for(const auto* name:{&c.service,&c.topic})check(!name->empty()&&name->size()<=100&&name->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,"D4 DDS name");
    const std::string prefix="opc.tcp://127.0.0.1:";check(c.endpoint.rfind(prefix,0)==0,"D4 numeric loopback endpoint");check(number(c.endpoint.substr(prefix.size()),65535)>=1024,"D4 endpoint port");
    check(c.stop.is_absolute()&&c.stop.string().size()<=512&&!fs::exists(c.stop),"D4 new absolute stop path");return c;
}
class UaValue{
    const UA_DataType* type_;
public:
    void* data;
    explicit UaValue(const UA_DataType* type):type_(type),data(UA_new(type)){check(data,"D4 UA allocation");}
    ~UaValue(){UA_delete(data,type_);}
    UaValue(const UaValue&)=delete;
    UaValue& operator=(const UaValue&)=delete;
};
inline std::vector<uint8_t> encode(const void* value,const UA_DataType* type){
    size_t size=UA_calcSizeBinary(value,type);check(size&&size<=4096,"D4 encoded size");std::vector<uint8_t> bytes(size);UA_ByteString out{size,bytes.data()};good(UA_encodeBinary(value,type,&out));bytes.resize(out.length);return bytes;
}
inline UA_StatusCode decode(const std::vector<uint8_t>& bytes,void* value,const UA_DataType* type){
    if(bytes.empty()||bytes.size()>4096)return UA_STATUSCODE_BADDECODINGERROR;
    UA_ByteString input{bytes.size(),const_cast<UA_Byte*>(bytes.data())};auto code=UA_decodeBinary(&input,value,type,nullptr);
    if(code==UA_STATUSCODE_BADOUTOFMEMORY)return code;
    if(code!=UA_STATUSCODE_GOOD)return UA_STATUSCODE_BADDECODINGERROR;
    // The public decoder does not expose its offset: require an exact canonical body.
    try{if(encode(value,type)!=bytes)return UA_STATUSCODE_BADDECODINGERROR;}catch(...){return UA_STATUSCODE_BADDECODINGERROR;}
    return UA_STATUSCODE_GOOD;
}
}
