// Read-only public directory query; no Socket, provisioning or Session API.
#include "../../ros2/autoload/DeviceDeployment.hpp"
#include <softbus_bus_center.h>
#include <iostream>
#include <memory>
int main(int argc,char** argv) {
    try {
        namespace dd=fastdds_dsoftbus::device_deployment;
        dd::Require(argc==2,"usage: c4_query NATIVE_TOKEN");dd::SetSelfToken(argv[1]);
        void* library=dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so",RTLD_NOW|RTLD_LOCAL);
        dd::Require(library!=nullptr,"directory provider unavailable");
        auto local=reinterpret_cast<decltype(&GetLocalNodeDeviceInfo)>(dlsym(library,"GetLocalNodeDeviceInfo"));
        auto peers=reinterpret_cast<decltype(&GetAllNodeDeviceInfo)>(dlsym(library,"GetAllNodeDeviceInfo"));
        auto release=reinterpret_cast<decltype(&FreeNodeInfo)>(dlsym(library,"FreeNodeInfo"));
        dd::Require(local&&peers&&release,"directory symbols missing");
        NodeBasicInfo node{};dd::Require(local("com.kaihong.mdds",&node)==0,"local node query failed");
        dd::Require(node.networkId[64]==0,"local network identity malformed");
        std::cout<<"C4_LOCAL network_id="<<node.networkId<<"\n";
        NodeBasicInfo* list=nullptr;int count=-1;int code=peers("com.kaihong.mdds",&list,&count);
        std::unique_ptr<NodeBasicInfo,decltype(release)> owner(list,release);
        dd::Require(code==0&&count>=0&&count<=32&&(count==0||list),"online nodes query failed");
        for(int i=0;i<count;++i){dd::Require(list[i].networkId[64]==0,"remote network identity malformed");std::cout<<"C4_REMOTE network_id="<<list[i].networkId<<"\n";}
        std::cout<<"C4_DIRECTORY PASS count="<<count<<"\n";return 0;
    }catch(const std::exception& e){std::cerr<<"C4_DIRECTORY FAIL "<<e.what()<<"\n";return 1;}
}
