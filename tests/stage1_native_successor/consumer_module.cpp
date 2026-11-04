#include "native_contract.h"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <cstring>
#include <string>
#include <time.h>
static constexpr char cpp_abi[]=NS_STR(_LIBCPP_ABI_NAMESPACE);
static_assert(sizeof(cpp_abi)==5&&cpp_abi[2]=='n'&&cpp_abi[3]=='1',"Use actual SDK __n1 headers, not alternate __1 or macro repair");
static_assert(sizeof(NS_Result)==32&&sizeof(NS_ConsumerApi)==32,"Native SDK POD layout");
namespace ds=fastdds_dsoftbus::dsoftbus;
static uint64_t ticks(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000u+uint64_t(t.tv_nsec);}
static bool text(const char* p,size_t n){return p&&p[0]&&std::memchr(p,0,n);}
static int32_t describe(char* out,uint32_t cap){
    static constexpr char info[]="Native SDK consumer; stock2.14.5/currentProduct; cpp=__n1; traffic=NOT_IMPLEMENTED; native_quiescence=UNPROVEN";
    if(!out||cap<sizeof(info))return NS_BOUND;std::memcpy(out,info,sizeof(info));return NS_OK;
}
static int32_t prepare(const NS_Request* q,const NS_Control* c,NS_Result* out){
    if(!q||!c||!out)return NS_BOUND;
    int rc=ns_header(&q->header,sizeof(*q));if(rc)return rc;
    rc=ns_header(&c->header,sizeof(*c));if(rc)return rc;rc=ns_header(&out->header,sizeof(*out));if(rc)return rc;
    if(q->cookie!=NS_COOKIE||c->cookie!=q->cookie||q->ownership!=NS_BORROWED)return NS_OWNER;
    if(q->external_approval!=NS_EXTERNAL_APPROVAL||!c->context||!c->allow||!c->cancelled)return NS_AUTH;
    if(!text(q->name_space,sizeof(q->name_space))||!text(q->udid,sizeof(q->udid))||!text(q->peer_udid,sizeof(q->peer_udid))||!text(q->peer_network,sizeof(q->peer_network))||!text(q->package_name,sizeof(q->package_name))||!text(q->prefix,sizeof(q->prefix))||!text(q->initiator,sizeof(q->initiator))||!text(q->peer_initiator,sizeof(q->peer_initiator)))return NS_BOUND;
    if(std::strcmp(q->package_name,NS_PACKAGE)||!q->deadline_ns||!ticks()||ticks()>=q->deadline_ns)return NS_CONTEXT;
    if(c->cancelled(c->context)!=0)return NS_CANCELLED;
    /* No available frontend supplies a true SDK-control grant in this increment. */
    if(c->allow(c->context,1,q->deadline_ns)!=NS_OK)return NS_NOT_READY;
    if(c->cancelled(c->context)!=0)return NS_CANCELLED;
    const uint64_t admitted_at=ticks();
    if(!admitted_at||admitted_at>=q->deadline_ns)return NS_CONTEXT;
    NS_Result r{{NS_VERSION,sizeof(r),NS_SCOPE,0},0,0,1,NS_STATE};
    try {
        ds::V1Deployment d;d.identity={q->name_space,q->domain,q->udid};d.slot=q->slot;d.callback_namespace="native.successor.preparation";
        d.native_name_scope={ds::v1::NativeNameContract::PackageScopedV1,q->package_name,q->prefix};
        ds::V1PeerDeployment peer;peer.identity={q->name_space,q->domain,q->peer_udid};peer.slot=q->peer_slot;peer.network_id=q->peer_network;
        peer.local_initiator_name=q->initiator;peer.expected_remote_initiator_names={q->peer_initiator};peer.native_name_scope=d.native_name_scope;d.peers={peer};
        ds::DSoftBusTransportDescriptor config(65536);
        if(config.ConfigureV1(d).code!=ds::DirectoryError::None){*out=r;return NS_CONTEXT;}
        const auto& route=config.directory.routes.front().fields;
        ds::phase0::PublicSocketConfiguration backend;backend.package_name=q->package_name;backend.identity={route.local_initiator_name,{1},{1}};
        backend.listener_identity={config.directory.locals.front().local_listen_name,{2},{2}};backend.peer_name=route.remote_listen_name;backend.peer_network_id=route.remote_network_id;
        backend.expected_peer_initiator_name=route.expected_remote_initiator_names.front();backend.max_message_size=65536;backend.listen_qos={{0,80},{1,2000}};backend.bind_qos=backend.listen_qos;
        ds::experimental::Runtime runtime(backend);auto descriptor=runtime.MakeStockDescriptor(config);r.stage=1;
        if(!descriptor){runtime.RequestPumpStop();r.closed=runtime.Close();*out=r;return NS_STATE;}
        eprosima::fastdds::dds::DomainParticipantQos qos;qos.transport().use_builtin_transports=false;qos.transport().user_transports={descriptor};
        /* No Participant/input/output/pump/thread or traffic is started here. */
        qos.transport().user_transports.clear();descriptor.reset();runtime.RequestPumpStop();r.closed=runtime.Close();r.status=r.closed?NS_OK:NS_STATE;
        *out=r;return r.status; /* closed refers only to facade; never SDK callback quiet. */
    }catch(...){*out=r;return NS_STATE;}
}
extern "C" NS_EXPORT int32_t ns_consumer_get_api(uint32_t version,uint32_t capacity,NS_ConsumerApi* out){
    if(version!=NS_VERSION)return NS_ABI;if(capacity!=sizeof(NS_ConsumerApi))return NS_SIZE;if(!out)return NS_BOUND;
    const NS_ConsumerApi api={{NS_VERSION,sizeof(NS_ConsumerApi),NS_SCOPE,0},describe,prepare};*out=api;return NS_OK;
}
