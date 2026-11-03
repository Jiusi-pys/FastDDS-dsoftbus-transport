#pragma once
#include "v2_client.hpp"
namespace stage1_control {
int run_loaded_v2(std::shared_ptr<void>,std::shared_ptr<Port>,GetRunApi,OwnerCheck,void*,const NS_RunRequest&,NS_RunResult&);
int native_v2_denial();
}
