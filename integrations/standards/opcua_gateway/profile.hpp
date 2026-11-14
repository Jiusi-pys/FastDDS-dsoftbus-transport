#pragma once
#include "../c_backend/b4.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace d1 {
inline unsigned number(const std::string& text, unsigned low, unsigned high) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("unsigned decimal required");
    size_t end = 0; auto value = std::stoull(text, &end);
    if (end != text.size() || value < low || value > high) throw std::runtime_error("numeric bound");
    return static_cast<unsigned>(value);
}
struct Item { unsigned ns, node, key; std::string label; };
struct Config {
    std::string endpoint, topic;
    unsigned domain, representation, capacity, timeout, reconnect, retries, publish, sampling, ua_queue, match, run;
    std::filesystem::path stop;
    std::vector<Item> items;
};
inline bool printable(const std::string& value) {
    for (unsigned char c : value) if (c < 32 || c > 126) return false;
    return true;
}
inline Config load(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 16384) throw std::runtime_error("configuration exceeds 16KiB");
    std::ifstream input(path); if (!input) throw std::runtime_error("configuration unreadable");
    std::map<std::string, std::string> fields; std::vector<std::string> items;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto equal = line.find('=');
        if (equal == std::string::npos || !printable(line)) throw std::runtime_error("invalid config line");
        auto key = line.substr(0, equal), value = line.substr(equal + 1);
        if (key == "item") { items.push_back(value); if (items.size() > 8) throw std::runtime_error("max 8 items"); }
        else if (!fields.emplace(key, value).second) throw std::runtime_error("duplicate config key");
    }
    if (!input.eof()) throw std::runtime_error("configuration read failure");
    auto take = [&](const char* name) { auto it=fields.find(name); if(it==fields.end()) throw std::runtime_error(std::string("missing ")+name); auto value=it->second; fields.erase(it); return value; };
    if (take("direction") != "ua_to_dds") throw std::runtime_error("only ua_to_dds supported; D2 is separate");
    Config c{}; c.endpoint=take("endpoint");
    const std::string prefix="opc.tcp://127.0.0.1:";
    if (c.endpoint.rfind(prefix,0)!=0) throw std::runtime_error("D1 SecurityPolicy None restricted to numeric loopback");
    number(c.endpoint.substr(prefix.size()),1024,65535);
    c.topic=take("topic");
    if(c.topic.empty() || c.topic.size()>128 || c.topic.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) throw std::runtime_error("topic bound/characters");
    c.domain=number(take("domain"),0,225); c.representation=number(take("representation"),0,2);
    if(c.representation==1) throw std::runtime_error("representation must be 0 or 2");
    c.capacity=number(take("queue_capacity"),1,256); c.timeout=number(take("request_timeout_ms"),100,3000);
    c.reconnect=number(take("reconnect_ms"),100,5000); c.retries=number(take("retry_limit"),1,100);
    c.publish=number(take("publish_ms"),50,1000); c.sampling=number(take("sampling_ms"),10,1000);
    c.ua_queue=number(take("ua_queue_size"),1,256); c.match=number(take("match_timeout_ms"),100,10000);
    c.run=number(take("run_ms"),1000,600000); auto stop=take("stop_file");
    if(stop.empty() || stop.size()>1024) throw std::runtime_error("stop file bound");
    c.stop=std::filesystem::absolute(stop);
    if(!fields.empty() || items.empty()) throw std::runtime_error("unknown config key or no items");
    std::set<std::pair<unsigned,unsigned>> nodes; std::set<unsigned> keys;
    for(const auto& text:items) {
        std::array<std::string,4> parts; size_t pos=0;
        for(size_t i=0;i<3;++i) { auto sep=text.find(';',pos); if(sep==std::string::npos) throw std::runtime_error("item needs ns;numeric_node;key;label"); parts[i]=text.substr(pos,sep-pos); pos=sep+1; }
        parts[3]=text.substr(pos);
        Item item{number(parts[0],0,65535),number(parts[1],0,UINT32_MAX),number(parts[2],0,UINT32_MAX),parts[3]};
        if(item.label.size()>64 || item.label.find(';')!=std::string::npos) throw std::runtime_error("label bound/semicolon");
        if(!nodes.emplace(item.ns,item.node).second || !keys.insert(item.key).second) throw std::runtime_error("duplicate node/key");
        c.items.push_back(item);
    }
    return c;
}
enum class Failure { none, overflow, quality, type, dds, callback, ua_setup, deadline, retry_limit, cleanup };
inline const char* name(Failure f) {
    switch(f) {
        case Failure::none:return "none"; case Failure::overflow:return "queue_overflow";
        case Failure::quality:return "ua_quality"; case Failure::type:return "ua_type";
        case Failure::dds:return "dds_operation"; case Failure::callback:return "callback_exception";
        case Failure::ua_setup:return "ua_setup"; case Failure::deadline:return "deadline";
        case Failure::retry_limit:return "retry_limit"; case Failure::cleanup:return "cleanup";
    } return "unknown";
}
// Fixed allocation, copied values, no DDS work while holding this mutex.
class Queue {
    std::array<b4_record,256> data_{}; size_t head_=0,size_=0,capacity_,peak_=0;
    bool closed_=false; std::mutex mutex_; std::condition_variable ready_;
public:
    explicit Queue(size_t capacity):capacity_(capacity) { if(!capacity || capacity>256) throw std::runtime_error("queue capacity"); }
    bool push(const b4_record& record) { std::lock_guard<std::mutex> lock(mutex_); if(closed_ || size_==capacity_) return false; data_[(head_+size_)%capacity_]=record; ++size_; if(size_>peak_) peak_=size_; ready_.notify_one(); return true; }
    bool pop(b4_record& record) { std::unique_lock<std::mutex> lock(mutex_); ready_.wait(lock,[&]{return closed_ || size_;}); if(!size_) return false; record=data_[head_]; head_=(head_+1)%capacity_; --size_; return true; }
    void close() { std::lock_guard<std::mutex> lock(mutex_); closed_=true; ready_.notify_all(); }
    size_t peak() { std::lock_guard<std::mutex> lock(mutex_); return peak_; }
};
}
