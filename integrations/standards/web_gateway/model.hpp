#pragma once
#include "xml.hpp"
#include <memory>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
namespace c1 {
struct Request { std::string method,target,body,key,accept,content_type,cache_control; };
struct Response { int status=200;std::string body,location;std::chrono::system_clock::time_point modified{}; };
struct Principal {std::string name,owner;std::set<unsigned> domains;std::set<std::string> permissions;unsigned char digest[32]{};};
struct Node;
struct Binding {std::weak_ptr<Node> node;std::string uri;bool reader=false;};
struct ParticipantTransport {
    std::function<void(unsigned,eprosima::fastdds::dds::DomainParticipantQos&)> configure;
    std::function<bool()> healthy;
};
class Gateway {
    Schemas schemas_;
    std::mutex mutex_;
    std::vector<Principal> principals_;
    std::shared_ptr<Node> root_;
    std::vector<std::shared_ptr<Node>> quarantine_;
    std::atomic<bool> stopping_{false};
    unsigned representation_,ttl_;
    bool poisoned_=false;
    ParticipantTransport transport_;
    const std::chrono::system_clock::time_point catalog_modified_=std::chrono::system_clock::now();
    Principal authenticate(const std::string&) const;
    std::shared_ptr<Node> lookup(const std::vector<std::string>&,bool closing=false);
    void check_tree(xmlNode*,const std::string&,const Principal&);
    void check_capacity(xmlNode*,std::shared_ptr<Node> replaced={});
    std::shared_ptr<Node> create(std::shared_ptr<Node>,const std::string&,xmlNode*,const Principal&);
    bool close(std::shared_ptr<Node>);
    std::string serialize(const std::shared_ptr<Node>&) const;
    Response read(std::shared_ptr<Node>,const std::map<std::string,std::string>&,std::unique_lock<std::mutex>&);
    Response wait(std::shared_ptr<Node>,const std::map<std::string,std::string>&,std::unique_lock<std::mutex>&);
public:
    Gateway(const std::string& schemas,const std::string& policy,unsigned representation,unsigned ttl,ParticipantTransport transport={});
    Response handle(const Request&,const Binding* binding=nullptr);
    std::string principal_name(const std::string& key) const {return authenticate(key).name;}
    Binding bind(const std::string& key,const std::string& uri,bool reader);
    bool expire();
    void stop() noexcept { stopping_=true; }
    bool shutdown();
};
}
