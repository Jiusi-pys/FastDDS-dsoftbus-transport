#include "../V1NativeNameTestSupport.hpp"
#include "../../integrations/ros2/target/NativeObservation.hpp"
#include "../../probes/PublicSocketRuntimeTrace.hpp"
#include "../FakePublicSocketLink.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>
#include <cassert>
#include <condition_variable>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/LocatorSelectorEntry.hpp>
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {std::uint64_t SocketFrame(const char*,std::int32_t,const std::uint8_t*,std::size_t) noexcept;}
using namespace fastdds_dsoftbus;
using namespace dsoftbus;
namespace observation=ros2_target_observation;
namespace diagnostic=ros2_diagnostics;
v1::DeviceIdentity identity(unsigned side) { return {"host-runtime", 151, side ? "synthetic-B" : "synthetic-A"}; }
Locator local(unsigned side) { EndpointId key{}; Locator value; auto id=identity(side); if(!v1::AddressCodec::DeriveKey(id,key)||!v1::AddressCodec::EncodeLocator({key,17+side,v1::Profile::BytesNative},value))std::abort(); return value; }
std::string listener(unsigned side) { std::string name; auto id=identity(side); if(!v1::AddressCodec::EncodeScopedServiceName({id.name_space,id.domain,17+side,v1::Profile::BytesNative},{v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},name))std::abort();return name; }
phase0::PublicSocketConfiguration config(unsigned side){const std::string me=side?"host.public.B.":"host.public.A.",peer=side?"host.public.A.":"host.public.B.";phase0::PublicSocketConfiguration c;c.identity={me+"outgoing",{910+side*4},{1010+side*4}};c.listener_identity={listener(side),{911+side*4},{1011+side*4}};c.package_name="host.public.dds";c.peer_name=listener(1-side);c.peer_network_id=side?"model.net.A":"model.net.B";c.listen_qos={{0,80},{1,2000}};c.bind_qos=c.listen_qos;c.max_message_size=8192;c.expected_peer_initiator_name=peer+"outgoing";return c;}
DSoftBusTransportDescriptor descriptor(unsigned side) {
    auto c=config(side);DSoftBusTransportDescriptor d(8192);
    V1Deployment input{identity(side),17+side,v1::Profile::BytesNative,"library.test",
        {{identity(1-side),18-side,c.peer_network_id,c.identity.service_name,{config(1-side).identity.service_name}}}};
    if(d.ConfigureV1(ScopedHostDeployment(input)).code!=DirectoryError::None)std::abort();return d;
}
struct Receiver:eprosima::fastdds::rtps::TransportReceiverInterface{void OnDataReceived(const eprosima::fastdds::rtps::octet*,std::uint32_t,const Locator&,const Locator&)override{}};
bool append(const char* p,std::size_t n,void* state){static_cast<std::string*>(state)->append(p,n);return true;}
bool throwing(const char*,std::size_t,void*){throw std::runtime_error("injected export error");}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
 public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);public_socket_fake::SetListenerNames({listener(0),listener(1)});
 observation::Begin("1234567890abcdef",0);observation::SetSourceForHostInjection();
 if(mode=="v1_locator") {
  // Native observation receives original RTPS bytes, not codec expected values.
  std::vector<std::uint8_t> packet{'R','T','P','S',2,3,1,15};packet.insert(packet.end(),12,0xa7);
  std::vector<std::uint8_t> body{0,0,16,0,0,0,0,0,0,1,0,0xc2,0,0,0,0,7,0,0,0,0,3,0,0};
  for(unsigned pid:{0x31u,0x32u}){body.insert(body.end(),{std::uint8_t(pid),0,24,0,0,0,0,0x40,1,0,0,1});for(unsigned j=0;j<16;++j)body.push_back(std::uint8_t(j+1));}
  body.insert(body.end(),{1,0,0,0});packet.insert(packet.end(),{0x15,5,std::uint8_t(body.size()),0});packet.insert(packet.end(),body.begin(),body.end());
  dds_experiment::SocketFrame("SDK_SEND",7,packet.data(),packet.size());observation::V1LocatorSnapshot captured;
  assert(observation::CaptureV1(captured)&&captured.advert_count==2&&captured.adverts[0].value.pid==0x31&&captured.adverts[1].value.pid==0x32&&captured.adverts[0].value.port==0x01000001&&captured.adverts[0].value.address==captured.adverts[1].value.address&&!captured.faults);
  experimental::Runtime a(config(0));auto d=a.MakeDescriptor(descriptor(0));assert(d);std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> transport(d->create_transport());assert(transport&&transport->init());
  namespace rtps=eprosima::fastdds::rtps;rtps::LocatorSelectorEntry entry(4,0);entry.enabled=true;
  const auto remote=local(1);auto wrong_profile=remote;wrong_profile.port=0x02000013;auto wrong_slot=remote;wrong_slot.port++;auto wrong_address=remote;wrong_address.address[0]^=1;
  for(const auto& value:{remote,wrong_profile,wrong_slot,wrong_address})assert(entry.unicast.push_back(value));
  rtps::LocatorSelector selector(eprosima::fastdds::ResourceLimitedContainerConfig::fixed_size_configuration(1));assert(selector.add_entry(&entry));selector.selection_start();transport->select_locators(selector);
  assert(observation::CaptureV1(captured)&&captured.selected_count==1&&captured.selected[0].kind==unsigned(remote.kind)&&captured.selected[0].port==remote.port&&std::memcmp(captured.selected[0].address.data(),remote.address,16)==0);
  transport->shutdown();transport.reset();assert(a.Close());std::cout<<"ACTUAL_V1_FRAME_AND_SELECTOR_CAPTURE_PASS synthetic_only=1\n";return 0;
 }
 if(mode=="v1_locator_bad_text") {
  runtime_probe::TraceText("ordinary_key","private text remains redacted");runtime_probe::TraceText("V1_SELECTED_LOCATOR","bad");observation::V1LocatorSnapshot captured;
  assert(observation::CaptureV1(captured)&&captured.faults==1&&captured.selected_count==0);return 0;
 }
 if(mode=="export_pipe"){
  // Stress the actual exporter; event injection here is distinct from the
  // actual SDK-boundary fault tests below and carries no device authority.
  runtime_probe::TraceEvent("Socket_return",0,0,-1,-777,0);
  for(unsigned i=0;i<255;++i)runtime_probe::TraceEvent("Socket_enter",0,0,-1,0,0);
  const bool ok=observation::ExportPending();diagnostic::Snapshot s;assert(observation::Capture(s));
  std::cerr<<"LOCAL_EXPORT_RESULT ok="<<ok<<" first="<<s.first.value<<" errors="<<s.output_errors<<std::endl;
  return s.first_present&&s.first.value==-777?0:1;
 }
 if(mode=="capacity"){
  for(unsigned i=0;i<300;++i)runtime_probe::TraceEvent("Socket_enter",0,0,-1,0,0);
  runtime_probe::TraceEvent("Socket_return",0,0,-1,-777,0);
  std::string secret(80,'x');runtime_probe::TraceText("validated_peer_network",secret.c_str());secret.assign(80,'z');
  diagnostic::Snapshot s;assert(observation::Capture(s));assert(s.first_present&&s.first.value==-777&&s.overflow>0&&s.text_truncated==1);
  std::string output;assert(diagnostic::Write(s,append,&output));assert(output.find("xxxxxxxx")==std::string::npos&&output.find("reserved_first=1")!=std::string::npos);
  assert(!diagnostic::Write(s,throwing,nullptr));assert(!observation::ExportPending(throwing,nullptr));diagnostic::Snapshot after;assert(observation::Capture(after)&&after.first.value==-777&&after.output_errors==1);return 0;
 }
 experimental::Runtime a(config(0));auto d=descriptor(0);
 if(mode=="local")d.directory.locals[0].local_listen_name="wrong.listener";
 if(mode=="socket")public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Socket,-777);
 if(mode=="listen"||mode=="cleanup")public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Listen,-778);
 if(mode=="bind")public_socket_fake::FailNext(public_socket_fake::InjectedOperation::Bind,-779);
 if(mode=="gate")assert(a.SetPreConnectGate([]{return false;}));
 if(mode=="gate_throw")assert(a.SetPreConnectGate([]()->bool{throw std::runtime_error("injected");}));
 auto da=a.MakeDescriptor(d);
 if(mode=="local"){
  assert(!da);diagnostic::Snapshot snapshot;assert(observation::Capture(snapshot)&&snapshot.first_present&&snapshot.first.type==diagnostic::ValueType::LocalStatus&&snapshot.first.value==int(DirectoryError::InvalidIdentity));
  const auto api=public_socket_fake::Observe();assert(!api.sockets[0]&&!api.sockets[1]&&!api.live);return 0;
 }
 assert(da);std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(da->create_transport());Receiver ra;assert(ta&&ta->init());
 if(mode=="capacity_reject"||mode=="null_receiver"){
  assert(!ta->OpenInputChannel(local(0),mode=="null_receiver"?nullptr:&ra,mode=="capacity_reject"?1:8192));
  diagnostic::Snapshot s;assert(observation::Capture(s)&&s.first_present&&s.first.type==diagnostic::ValueType::LocalStatus&&s.entered[0]==0);ta->shutdown();ta.reset();(void)a.Close();return 0;
 }
 bool opened=false;
 if(mode=="cleanup"){
  std::mutex gate;std::condition_variable changed;bool entered=false,released=false;
  public_socket_fake::SetShutdownBarrier([&]{std::unique_lock<std::mutex> l(gate);entered=true;changed.notify_all();changed.wait(l,[&]{return released;});});
  std::thread worker([&]{opened=ta->OpenInputChannel(local(0),&ra,8192);});
  {std::unique_lock<std::mutex> l(gate);assert(changed.wait_for(l,std::chrono::seconds(5),[&]{return entered;}));}
  diagnostic::Snapshot snapshot;assert(observation::Capture(snapshot)&&snapshot.first_present&&snapshot.first.value==-778);
  std::string output;assert(diagnostic::Write(snapshot,append,&output)&&output.find("value=-778")!=std::string::npos);
  assert(observation::ExportPending()); // actual production exporter before blocked cleanup is released
  {std::lock_guard<std::mutex> l(gate);released=true;}changed.notify_all();worker.join();public_socket_fake::SetShutdownBarrier({});assert(!opened);
 }else if(mode=="normal"||mode=="match"){
  experimental::Runtime b(config(1));auto db=b.MakeDescriptor(descriptor(1));std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> tb(db->create_transport());Receiver rb;assert(tb&&tb->init());bool other=false;
  std::thread x([&]{opened=ta->OpenInputChannel(local(0),&ra,8192);}),y([&]{other=tb->OpenInputChannel(local(1),&rb,8192);});x.join();y.join();assert(opened&&other);
  diagnostic::Snapshot before;assert(observation::Capture(before)&&before.entered[0]==4&&before.returned[1]==2&&before.returned[2]==2&&!before.first_present);
  if(mode=="match"){std::string output;assert(diagnostic::Write(before,append,&output,1));assert(output.find("first_observation_seq=0")!=std::string::npos);}
  ta->shutdown();tb->shutdown();ta.reset();tb.reset();assert(a.Close()&&b.Close());return 0;
 }else opened=ta->OpenInputChannel(local(0),&ra,8192);
 assert(!opened);diagnostic::Snapshot s;assert(observation::Capture(s)&&s.first_present&&s.source==diagnostic::Source::HostInjected);
 if(mode=="local")assert(s.entered[0]==0&&s.entered[1]==0&&s.entered[2]==0&&s.first.type==diagnostic::ValueType::LocalStatus);
 if(mode=="socket")assert(s.first.value==-777&&s.entered[1]==0&&s.entered[2]==0&&s.first.type==diagnostic::ValueType::SdkReturn);
 if(mode=="listen")assert(s.first.value==-778&&s.entered[0]==1&&s.entered[2]==0);
 if(mode=="bind")assert(s.first.value==-779&&s.returned[1]==1);
 if(mode=="gate"||mode=="gate_throw")assert(s.returned[1]==1&&s.entered[2]==0&&s.first.type==diagnostic::ValueType::LocalStatus);
 const auto first=s.first.seq;ta->shutdown();ta.reset();(void)a.Close();assert(observation::Capture(s)&&s.first.seq==first);
 for(std::size_t i=0;i<s.count;++i)if(!std::strcmp(s.events[i].operation,"Shutdown_void_return"))assert(s.events[i].type==diagnostic::ValueType::VoidReturn);
 std::cout<<"STARTUP_DIAGNOSTIC_HOST_INJECTION_PASS "<<mode<<std::endl;return 0;
}
