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
using namespace fastdds_dsoftbus;
using namespace dsoftbus;
namespace observation=ros2_target_observation;
namespace diagnostic=ros2_diagnostics;
Locator local(unsigned side){EndpointId id{};id[0]=10;id[15]=static_cast<std::uint8_t>(side+1);Locator l;assert(LocatorCodec::Encode(id,l));return l;}
phase0::PublicSocketConfiguration config(unsigned side){const std::string me=side?"host.public.B.":"host.public.A.",peer=side?"host.public.A.":"host.public.B.";phase0::PublicSocketConfiguration c;c.identity={me+"outgoing",{910+side*4},{1010+side*4}};c.listener_identity={me+"listener",{911+side*4},{1011+side*4}};c.package_name="host.public.dds";c.peer_name=peer+"listener";c.peer_network_id=side?"model.net.A":"model.net.B";c.listen_qos={{0,80},{1,2000}};c.bind_qos=c.listen_qos;c.max_message_size=8192;return c;}
DSoftBusTransportDescriptor descriptor(unsigned side){auto c=config(side);DSoftBusTransportDescriptor d(8192);d.directory.locals={{local(side),"startup.test",c.listener_identity.service_name}};d.directory.routes={{{local(side),local(1-side)},{c.peer_network_id,c.peer_name,c.identity.service_name,{config(1-side).identity.service_name},RecordDataType::Bytes},1}};d.default_metatraffic_unicast={local(side)};d.default_user_unicast={local(side)};return d;}
struct Receiver:eprosima::fastdds::rtps::TransportReceiverInterface{void OnDataReceived(const eprosima::fastdds::rtps::octet*,std::uint32_t,const Locator&,const Locator&)override{}};
bool append(const char* p,std::size_t n,void* state){static_cast<std::string*>(state)->append(p,n);return true;}
bool throwing(const char*,std::size_t,void*){throw std::runtime_error("injected export error");}
int main(int argc,char** argv){
 assert(argc==2);const std::string mode=argv[1];
 public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
 observation::Begin("1234567890abcdef",0);observation::SetSourceForHostInjection();
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
 auto da=a.MakeDescriptor(d);std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(da->create_transport());Receiver ra;assert(ta&&ta->init());
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
