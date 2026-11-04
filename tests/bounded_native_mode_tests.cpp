// Host API-shape capability tests. These symbols are test doubles, not SDK evidence.
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <fastdds_dsoftbus/dsoftbus/NativeOwnerTable.hpp>
#include <socket.h>
#include <iostream>
static unsigned calls=0;
extern "C" {
int32_t Socket(SocketInfo){++calls;return 100;}
int32_t Listen(int32_t,const QosTV*,uint32_t,const ISocketListener*){++calls;return 0;}
int32_t Bind(int32_t,const QosTV*,uint32_t,const ISocketListener*){++calls;return 0;}
int32_t SendBytes(int32_t,const void*,uint32_t){++calls;return 0;}
void Shutdown(int32_t){++calls;}
}
int main(){
    using namespace fastdds_dsoftbus::dsoftbus;using namespace phase0;
    PublicSocketConfiguration c;c.identity={"host.native.mode.client",{710},{810}};
    c.listener_identity={"host.native.mode.listener",{711},{811}};c.package_name="host.native.mode";
    c.peer_name="host.native.mode.peer";c.peer_network_id="model.peer";
    c.bind_qos={{0,64}};c.listen_qos=c.bind_qos;c.max_message_size=64;
    PublicSocketBackend ordinary(c);
    if(ordinary.Kind()!=BackendKind::DSoftBusProvider||ordinary.BoundedReacceptExperiment())return 1;
    c.bounded_native_reaccept_experiment=true;PublicSocketBackend requested(c);
#ifdef EXPECT_NATIVE_MODE
    if(requested.Kind()!=BackendKind::DSoftBusProvider||!requested.BoundedReacceptExperiment()||requested.BoundedReacceptHost()||requested.Observe().fake_sdk)return 2;
#else
    if(requested.BoundedReacceptExperiment())return 3;
#endif
    if(requested.ResetBoundedConnection(RecordRole::Client,{})!=BackendStatus::InvalidState)return 4;
    c.bounded_native_reset_limit=2;PublicSocketBackend repeated(c);
#ifdef EXPECT_NATIVE_MODE
    if(!repeated.BoundedReacceptExperiment())return 7;
#else
    if(repeated.BoundedReacceptExperiment())return 8;
#endif
    for(unsigned invalid:{0u,3u}){c.bounded_native_reset_limit=invalid;PublicSocketBackend rejected(c);
        if(rejected.BoundedReacceptExperiment())return 9;}
    c.bounded_native_reset_limit=2;c.bounded_native_reaccept_experiment=false;PublicSocketBackend unrequested(c);
    if(unrequested.BoundedReacceptExperiment())return 10;
    c.bounded_native_reaccept_experiment=true;c.route_lifecycle=true;PublicSocketBackend route(c);
    if(route.BoundedReacceptExperiment())return 11;
    c.route_lifecycle=false;
    c.bounded_reaccept_host=true;PublicSocketBackend mixed(c);
    if(mixed.BoundedReacceptExperiment()||mixed.BoundedReacceptHost())return 5;
    if(calls)return 6;
    std::cout<<"PASS HOST_API_SHAPE_ONLY NATIVE_KIND_EXPLICIT_CAPABILITY DEFAULT_REFUSAL\n";
}
