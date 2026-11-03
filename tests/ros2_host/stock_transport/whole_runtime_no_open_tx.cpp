#include "V1NativeNameTestSupport.hpp"
#include "FakePublicSocketLink.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#if defined(FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION) || defined(FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK)
#error "This driver must match the reviewed ordinary Runtime archive variant"
#endif
static_assert(_MSC_VER == 1944 && _ITERATOR_DEBUG_LEVEL == 0, "Reviewed Release ABI required");
#ifndef _DLL
#error "Reviewed archive requires /MD"
#endif
using namespace fastdds_dsoftbus::dsoftbus;
namespace rtps = eprosima::fastdds::rtps;
using namespace std::chrono_literals;
namespace {
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
v1::DeviceIdentity Identity(unsigned s) { return {"stock-views-0",151,s?"B":"A"}; }
std::string Listener(unsigned s) {
    std::string name;
    Check(v1::AddressCodec::EncodeScopedServiceName({Identity(s).name_space,151,17+s,v1::Profile::BytesNative},
        {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},name),"listener codec");
    return name;
}
phase0::PublicSocketConfiguration Configuration(unsigned s) {
    phase0::PublicSocketConfiguration c;
    const auto outgoing=[](unsigned x){return std::string("host.public.")+(x?"B.":"A.")+"outgoing";};
    c.identity={outgoing(s),{910+s*4},{1010+s*4}};
    c.listener_identity={Listener(s),{911+s*4},{1011+s*4}};
    c.package_name="host.public.dds";c.peer_name=Listener(1-s);c.peer_network_id=s?"model.net.A":"model.net.B";
    c.expected_peer_initiator_name=outgoing(1-s);c.bind_qos={{0,80},{1,2000}};c.listen_qos=c.bind_qos;c.max_message_size=8192;
    return c;
}
DSoftBusTransportDescriptor Descriptor(unsigned s) {
    const auto c=Configuration(s);DSoftBusTransportDescriptor d(8192);
    V1Deployment input{Identity(s),17+s,v1::Profile::BytesNative,"stock.views",
        {{Identity(1-s),18-s,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
    Check(d.ConfigureV1(ScopedHostDeployment(input)).code==DirectoryError::None,"descriptor config");return d;
}
struct Receiver final:rtps::TransportReceiverInterface {
    unsigned calls=0,invalid=0;std::array<std::uint8_t,4> last{};
    void OnDataReceived(const std::uint8_t* p,std::uint32_t n,const Locator&,const Locator&) override {
        ++calls;if(n!=4){++invalid;return;}std::copy(p,p+4,last.begin());
        if(last[1]!=19||last[2]!=83||last[3]!=7)++invalid;
    }
    bool Exact(unsigned count,std::uint8_t marker)const{return calls==count&&!invalid&&last==std::array<std::uint8_t,4>{marker,19,83,7};}
};
bool Send(fastdds_dsoftbus::binding::SenderResource& sender,const Locator& remote,std::uint8_t marker) {
    std::vector<Locator> targets{remote};rtps::Locators begin(targets.begin()),end(targets.end());
    const std::uint8_t data[]{marker,19,83,7};return sender.send(data,4,&begin,&end,std::chrono::steady_clock::now()+2s);
}
void LoadedDll() {
    const auto expected=_wgetenv(L"T2_EXPECTED_FASTDDS_DLL");Check(expected&&*expected,"expected DLL path");
    const HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    Check(h!=INVALID_HANDLE_VALUE,"own module snapshot");MODULEENTRY32W row{};row.dwSize=sizeof(row);bool found=false;
    for(BOOL ok=Module32FirstW(h,&row);ok;ok=Module32NextW(h,&row))if(!_wcsicmp(row.szModule,L"fastrtps-2.14.dll")){
        found=!_wcsicmp(row.szExePath,expected);std::wcout<<L"ACTUAL_FASTDDS_DLL="<<row.szExePath<<L'\n';
    }
    CloseHandle(h);Check(found,"new driver DLL binding differs");
}
bool NeverOpen(const experimental::Runtime& a,const host_dds::Trace& trace) {
    const auto o=phase0::RuntimeProbeAccess::ObserveRuntime(a);const auto f=public_socket_fake::Observe();
    const auto events=trace.Events();
    const bool listener_created=std::any_of(events.begin(),events.end(),[](const host_dds::Event& e){
        return e.side==0&&e.stage=="FAKE_C_SOCKET"&&e.detail.find(",role=0,")!=std::string::npos;
    });
    std::cout<<"NEVER_OPEN_FIELDS open_calls="<<o.open_calls<<" input="<<o.input_open
        <<" identity_empty="<<(o.receiver_identity==ReceiverIdentity{})
        <<" applications="<<o.receive.active_applications<<" detaching="<<o.receive.detaching_applications
        <<" views="<<o.live_views<<" controls="<<o.view_controls<<" listens="<<f.listens[0]
        <<" listener_created="<<listener_created<<" trace_good="<<trace.Good()<<std::endl;
    return !o.open_calls&&!o.input_open&&o.receiver_identity==ReceiverIdentity{}&&
        !o.receive.active_applications&&!o.receive.detaching_applications&&o.live_views==1&&!o.view_controls&&
        !f.listens[0]&&!listener_created&&trace.Good();
}
void Snapshot(const char* stage,const experimental::Runtime& a,const experimental::Runtime& b,
        DSoftBusTransport* av,DSoftBusTransport* bv,const Receiver& peer) {
    const auto x=phase0::RuntimeProbeAccess::ObserveRuntime(a),y=phase0::RuntimeProbeAccess::ObserveRuntime(b);
    const auto f=public_socket_fake::Observe();
    std::cout<<"SNAP stage="<<stage<<" A_open="<<x.open_calls<<" A_input="<<x.input_open
        <<" A_apps="<<x.receive.active_applications<<" A_views="<<x.live_views<<" A_controls="<<x.view_controls
        <<" A_guard="<<int(av->domain_snapshot().state)<<" B_guard="<<int(bv->domain_snapshot().state)
        <<" B_apps="<<y.receive.active_applications<<" B_calls="<<peer.calls<<" B_marker="<<unsigned(peer.last[0])
        <<" sockets="<<f.sockets[0]<<','<<f.sockets[1]<<" listens="<<f.listens[0]<<','<<f.listens[1]
        <<" binds="<<f.binds[0]<<','<<f.binds[1]<<" sends="<<f.sends[0]<<','<<f.sends[1]
        <<" forwards="<<f.forwards[0]<<','<<f.forwards[1]<<" queued="<<f.queued<<" bytes="<<f.bytes<<std::endl;
}
}
#include "noopen_contract_cases.hpp"
int main(int argc,char** argv) {
    if(argc>1)return ContractCase(argv[1]);
    bool error=false,strict_receipt=false,receiver_await=false,lifecycle=false,never_open=false,cleanup=false;
    auto trace=std::make_shared<host_dds::Trace>();public_socket_fake::Configure(trace,true);
    public_socket_fake::SetListenerNames({Listener(0),Listener(1)});
    public_socket_fake::SetInitiatorNames({Configuration(0).identity.service_name,Configuration(1).identity.service_name});
    experimental::Runtime a(Configuration(0)),b(Configuration(1));
    Receiver original,replacement,peer;rtps::SendResourceList as,bs;
    std::unique_ptr<rtps::TransportInterface> ta,tb;
    try {
        LoadedDll();auto da=Descriptor(0),db=Descriptor(1);auto ad=a.MakeDescriptor(da),bd=b.MakeDescriptor(db);
        Check(ad&&bd,"ordinary descriptors");ta.reset(ad->create_transport());tb.reset(bd->create_transport());
        Check(ta&&tb&&ta->init()&&tb->init(),"one normal facade per Runtime");
        auto* av=dynamic_cast<DSoftBusTransport*>(ta.get());auto* bv=dynamic_cast<DSoftBusTransport*>(tb.get());Check(av&&bv,"actual facade");
        const auto al=da.directory.locals.front().local_locator,bl=db.directory.locals.front().local_locator;
        auto ar=da.directory.routes.front().key.remote_locator,br=db.directory.routes.front().key.remote_locator;
        Check(tb->OpenInputChannel(bl,&peer,8192),"B ordinary input");
        Check(NeverOpen(a,*trace),"A never-open precondition");Check(ta->OpenOutputChannel(as,ar)&&as.size()==1,"cached A sender");
        Check(!Send(*as.front(),ar,200),"Await sender bypass");
        fastdds_dsoftbus::binding::PortParameters ports; eprosima::fastdds::rtps::LocatorList peers;
        const bool b_did=tb->configureInitialPeerLocator(br,ports,151,peers);peers.clear();
        const bool did=ta->configureInitialPeerLocator(ar,ports,151,peers);
        const bool submitted=Send(*as.front(),ar,201);const auto sent=a.PumpSend();
        const bool forwarded=public_socket_fake::PumpOne();const auto received=b.PumpReceive();
        never_open=NeverOpen(a,*trace);Snapshot("WHOLE_A_NEVER_OPEN",a,b,av,bv,peer);
        strict_receipt=did&&submitted&&sent.actual_return_code==0&&forwarded&&peer.Exact(1,201);
        receiver_await=bv->domain_snapshot().state==ViewDomainState::Await;
        std::cout<<"NO_OPEN_ATTEMPT did="<<did<<" submitted="<<submitted<<" native_status="<<int(sent.status)
            <<" native_return_present="<<bool(sent.actual_return_code)<<" native_return="<<sent.actual_return_code.value_or(-9999)
            <<" forwarded="<<forwarded<<" receive_status="<<int(received)<<" exact_B_receipt="<<strict_receipt
            <<" A_never_open="<<never_open<<" B_await="<<receiver_await<<std::endl;
        Check(never_open,"hidden A input/listener/application");Check(did,"A public DID failed");
        Check(submitted&&sent.actual_return_code==0&&forwarded,"A egress/native Fake forwarding failed");
        Check(ta->OpenInputChannel(al,&original,8192),"A first input");
        Check(b_did,"B genuine DID prepared before201");
        Check(tb->OpenOutputChannel(bs,br)&&bs.size()==1,"B actual sender");
        Check(Send(*as.front(),ar,206)&&a.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"B ready receive control");
        b.PumpReceive();Check(peer.Exact(strict_receipt?2:1,206),"B ready exact marker control");
        Check(Send(*bs.front(),br,202)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"first A marker send");
        a.PumpReceive();Check(original.Exact(1,202),"first A exact marker");
        const auto family=a.ObserveSocket().family_ids_retained;const auto before=public_socket_fake::Observe();
        Check(Send(*bs.front(),br,203)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"queue old marker before close");
        const auto old_calls=original.calls;Check(phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.records>0,"old lease work not captured");
        Check(ta->CloseInputChannel(al)&&!ta->IsInputChannelOpen(al),"A input close");
        Check(ta->OpenInputChannel(al,&replacement,8192),"A replacement input");
        peers.clear();Check(ta->configureInitialPeerLocator(ar,ports,151,peers),"A genuine DID after reopen");
        a.PumpReceive();Check(!replacement.calls&&original.calls==old_calls,"old work retargeted replacement");
        Check(Send(*bs.front(),br,204)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"fresh marker send");
        a.PumpReceive();Check(replacement.Exact(1,204)&&original.calls==old_calls,"replacement exact fresh marker");
        const auto after=public_socket_fake::Observe();const auto state=phase0::RuntimeProbeAccess::ObserveRuntime(a);
        Check(state.receive.active_applications==1&&!state.receive.detaching_applications&&a.ObserveSocket().family_ids_retained==family,"input lease/native family ownership");
        for(unsigned s=0;s<2;++s)Check(after.sockets[s]==before.sockets[s]&&after.binds[s]==before.binds[s],"input cycle recreated native");
        lifecycle=true;Snapshot("FIRST_OPEN_CLOSE_REOPEN",a,b,av,bv,peer);
    } catch(const std::exception& e) { error=true;std::cerr<<"ORACLE_ERROR "<<e.what()<<std::endl; }
    // No receiver is destroyed, no held gate/thread exists, until all facade work ends.
    ta.reset();tb.reset();
    bool cached_refused=true;
    try {
        if(!as.empty()){const auto route=Descriptor(0).directory.routes.front().key.remote_locator;
            cached_refused=!Send(*as.front(),route,205);}
    } catch(const std::exception& e) { error=true;std::cerr<<"CACHED_CLOSE_ERROR "<<e.what()<<std::endl; }
    as.clear();bs.clear();const bool ac=a.Close(),bc=b.Close();
    try {
        const auto f=public_socket_fake::Observe();
        const auto o=phase0::RuntimeProbeAccess::ObserveRuntime(a);
        cleanup=cached_refused&&ac&&bc&&!f.live&&!f.queued&&!f.bytes&&!f.invalid&&o.closed&&!o.tx.records&&!o.runtime_admitted_outstanding;
        std::cout<<"CLEANUP A_close="<<ac<<" B_close="<<bc<<" live="<<f.live<<" queued="<<f.queued<<" bytes="<<f.bytes
            <<" invalid="<<f.invalid<<" A_closed="<<o.closed<<" outstanding="<<o.runtime_admitted_outstanding<<std::endl;
    } catch(const std::exception& e) {error=true;std::cerr<<"CLEANUP_ERROR "<<e.what()<<std::endl;}
    if(!cleanup||!trace->Good())error=true;
    trace->Dump(0);
    const int result=error?1:(strict_receipt&&lifecycle&&never_open?0:(receiver_await&&never_open?3:1));
    std::cout<<"WHOLE_RUNTIME_ORACLE exact_B_receipt="<<strict_receipt<<" never_open="<<never_open
        <<" same_A_lifecycle="<<lifecycle<<" cleanup="<<cleanup<<" result="<<result
        <<" classification="<<(result==0?"HOST_ORACLE_PASS":result==3?"BLOCKED_B_DOMAIN_ADMISSION":"FAIL")
        <<" SDK_ABI=UNKNOWN DEVICE_TESTED=0\n";
    return result;
}
