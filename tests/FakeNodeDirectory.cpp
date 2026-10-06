#include "FakeNodeDirectory.hpp"
#include <cstdlib>
#include <cstring>
namespace fake_node {
std::mutex gate;std::map<std::string,std::string> nodes;INodeStateCb* callback=nullptr;
unsigned registrations=0,unregistrations=0,queries=0,identity_queries=0;
int registration_result=0,unregistration_result=0,query_result=0,key_result=0;std::function<void()> before_query,before_registration;
void Reset(){std::lock_guard<std::mutex> lock(gate);nodes.clear();callback=nullptr;registrations=unregistrations=queries=identity_queries=0;registration_result=unregistration_result=query_result=key_result=0;before_query={};before_registration={};}
void Event(bool online,const std::string& network){NodeBasicInfo info{};if(network.size()>=sizeof(info.networkId))return;
    std::memcpy(info.networkId,network.data(),network.size());INodeStateCb* cb;
    {std::lock_guard<std::mutex> lock(gate);cb=callback;}if(cb){if(online)cb->onNodeOnline(&info);else cb->onNodeOffline(&info);}}
void Online(const std::string& n){Event(true,n);}void Offline(const std::string& n){Event(false,n);}
}
extern "C" {
int32_t RegNodeDeviceStateCb(const char*,INodeStateCb* callback){std::function<void()> pause;
    {std::lock_guard<std::mutex> lock(fake_node::gate);++fake_node::registrations;pause=fake_node::before_registration;}if(pause)pause();
    std::lock_guard<std::mutex> lock(fake_node::gate);if(fake_node::registration_result)return fake_node::registration_result;fake_node::callback=callback;return 0;}
int32_t UnregNodeDeviceStateCb(INodeStateCb* callback){std::lock_guard<std::mutex> lock(fake_node::gate);++fake_node::unregistrations;if(fake_node::unregistration_result)return fake_node::unregistration_result;if(fake_node::callback==callback)fake_node::callback=nullptr;return 0;}
int32_t GetAllNodeDeviceInfo(const char*,NodeBasicInfo** info,int32_t* count){std::function<void()> pause;
    {std::lock_guard<std::mutex> lock(fake_node::gate);++fake_node::queries;pause=fake_node::before_query;}if(pause)pause();
    std::lock_guard<std::mutex> lock(fake_node::gate);if(fake_node::query_result)return fake_node::query_result;
    *count=static_cast<int32_t>(fake_node::nodes.size());*info=static_cast<NodeBasicInfo*>(std::calloc(*count,sizeof(NodeBasicInfo)));
    if(*count&&!*info)return -1;std::size_t i=0;for(const auto& node:fake_node::nodes){if(node.first.size()>=sizeof((*info)[i].networkId))return -1;std::memcpy((*info)[i++].networkId,node.first.data(),node.first.size());}return 0;}
void FreeNodeInfo(NodeBasicInfo* info){std::free(info);}
int32_t GetNodeKeyInfo(const char*,const char* network,NodeDeviceInfoKey key,uint8_t* info,int32_t size){std::lock_guard<std::mutex> lock(fake_node::gate);++fake_node::identity_queries;
    if(fake_node::key_result)return fake_node::key_result;auto node=fake_node::nodes.find(network);if(key!=NODE_KEY_UDID||node==fake_node::nodes.end()||size<=static_cast<int32_t>(node->second.size()))return -1;
    std::memcpy(info,node->second.c_str(),node->second.size()+1);return 0;}
}
