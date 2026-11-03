#pragma once
#include <softbus_bus_center.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
namespace fake_node {
extern std::mutex gate;
extern std::map<std::string,std::string> nodes;
extern INodeStateCb* callback;
extern unsigned registrations,unregistrations,queries,identity_queries;
extern int registration_result,unregistration_result,query_result,key_result;
extern std::function<void()> before_query,before_registration;
void Reset();void Online(const std::string&);void Offline(const std::string&);
}
