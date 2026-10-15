// Actual Windows HostSocket -> PublicSocketBackend -> Runtime receive admission.
// Opaque component payload, single-process TCP self-loop; neither ROS nor SDK.
#include "../../integrations/ros2/HostIoObservation.hpp"
#include "../../integrations/ros2/HostSocket.hpp"
#include "../../integrations/ros2/HostReceiveObservation.hpp"
#include "../../integrations/ros2/HostCallbackObservation.hpp"
#include "../../integrations/ros2/target/NativeObservation.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>
#include <socket.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace d=fastdds_dsoftbus::dsoftbus;
namespace host=fastdds_dsoftbus::ros2_host;
namespace rx=d::host_rx_observation;
namespace obs=fastdds_dsoftbus::ros2_host_receive;
namespace {
using namespace d;
constexpr std::size_t frame_size=65500;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void await(F predicate,const char* message){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate()){check(std::chrono::steady_clock::now()<end,message);Sleep(1);}
}
struct Deadline {
    HANDLE done=CreateEventA(nullptr,TRUE,FALSE,nullptr);std::thread worker;
    Deadline(){check(done!=nullptr,"watchdog create");worker=std::thread([this]{
        if(WaitForSingleObject(done,30000)!=WAIT_OBJECT_0){
            std::fputs("HOST_RX_SOCKET_DEADLINE\n",stderr);std::fflush(stderr);TerminateProcess(GetCurrentProcess(),124);
        }});}
    ~Deadline(){SetEvent(done);if(worker.joinable())worker.join();CloseHandle(done);}
};
std::vector<std::uint8_t> frame(unsigned sequence){
    std::vector<std::uint8_t> bytes(frame_size);
    for(std::size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<std::uint8_t>((i+sequence)%251);
    return bytes;
}
struct Observer final:ProcessReceiveObserver {
    obs::Observer recorder;
    struct Slot {ReceiveDecision decision;rx::Ingress ingress;std::atomic<bool> ready{false};};
    std::array<Slot,8> bindings{};
    std::atomic<unsigned> reserved{0},released{0};std::atomic<bool> overflow{false};
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& diagnostic,const std::uint8_t* bytes,std::size_t size) noexcept override {
        recorder.OnReceive(stage,diagnostic,bytes,size);
        if(stage==ReceiveStage::RecordReleased)released.fetch_add(1,std::memory_order_release);
    }
    void OnDecision(const ReceiveDecision& value) noexcept override {
        recorder.OnDecision(value);
        if(value.layer!=ReceiveDecisionLayer::Binding||!value.bridge_called)return;
        const auto index=reserved.fetch_add(1,std::memory_order_relaxed);
        if(index>=bindings.size()){overflow.store(true);return;}
        auto& slot=bindings[index];slot.decision=value;slot.ingress=rx::CurrentIngress();
        slot.ready.store(true,std::memory_order_release);
    }
    const Slot& Wait(unsigned index){
        check(index<bindings.size(),"decision bounds");
        await([&]{return bindings[index].ready.load(std::memory_order_acquire);},"original binding decision timeout");
        return bindings[index];
    }
};
struct Receiver final:eprosima::fastdds::rtps::TransportReceiverInterface {
    std::array<unsigned,5> sequences{};unsigned delivered=0;bool correct=true;
    void OnDataReceived(const std::uint8_t* bytes,uint32_t size,const Locator&,const Locator&) override {
        if(size!=frame_size||delivered>=sequences.size()){correct=false;return;}
        const unsigned sequence=bytes[0];const auto expected=frame(sequence);
        correct=correct&&std::memcmp(bytes,expected.data(),size)==0;
        sequences[delivered++]=sequence;
    }
};
struct Setup {
    DSoftBusTransportDescriptor descriptor{frame_size};phase0::PublicSocketConfiguration socket;Locator local;
    Setup(){
        V1Deployment deployment{{"host-rx-fixture",151,"synthetic-A"},17,v1::Profile::BytesNative,"fixture",
            {{{"host-rx-fixture",151,"synthetic-B"},18,"host.net.B","host.rx.fixture.outgoing",{"host.rx.fixture.peer.outgoing"}}}};
        deployment.native_name_scope={v1::NativeNameContract::PackageScopedV1,"host.ros.test","host.ros.test.rx.fixture.v1"};
        for(auto& peer:deployment.peers)peer.native_name_scope=deployment.native_name_scope;
        const auto issue=descriptor.ConfigureV1(deployment);
        if(issue.code!=DirectoryError::None)
            throw std::runtime_error("descriptor deployment DirectoryError="+std::to_string(static_cast<int>(issue.code)));
        local=descriptor.directory.locals.front().local_locator;
        socket.identity={"host.rx.fixture.outgoing",{910},{1010}};
        socket.listener_identity={descriptor.directory.locals.front().local_listen_name,{911},{1011}};
        socket.package_name="host.ros.test";socket.peer_name=descriptor.directory.routes.front().fields.remote_listen_name;
        socket.peer_network_id="host.net.B";socket.expected_peer_initiator_name="host.rx.fixture.peer.outgoing";
        socket.max_message_size=frame_size;socket.bind_qos={{0,80},{1,2000}};socket.listen_qos=socket.bind_qos;
    }
};
struct LinkOwner {bool armed=true;void Close(){if(armed){armed=false;host::StopLink();}}~LinkOwner(){Close();}};
struct RuntimeOwner {
    experimental::Runtime& runtime;Locator local;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> transport;
    bool open=false,closed=false;
    bool Close(){
        if(closed)return true;closed=true;
        if(transport){if(open){transport->CloseInputChannel(local);open=false;}transport->shutdown();transport.reset();}
        return runtime.Close();
    }
    ~RuntimeOwner(){try{(void)Close();}catch(...){}}
};
}
int main(int argc,char** argv){
    if(argc!=5)return 2;
    try{
        Deadline deadline;const std::string run=argv[1],port_text=argv[2];std::size_t used=0;
        const auto port=std::stoul(port_text,&used);
        check(run.size()==16&&run.find_first_not_of("0123456789abcdef")==std::string::npos,"run identity");
        check(used==port_text.size()&&port>0&&port<=65535,"port");
        for(unsigned i=0;i<2;++i){
            const char* module=i?"fastcdr-2.2.dll":"fastrtps-2.14.dll";
            char loaded[MAX_PATH]{};const auto handle=GetModuleHandleA(module);
            check(handle&&GetModuleFileNameA(handle,loaded,MAX_PATH),"loaded dependency identity");
            check(std::filesystem::path(argv[3+i]).is_absolute()&&std::filesystem::equivalent(argv[3+i],loaded),"dependency source mismatch");
            std::printf("HOST_RX_SOCKET_MODULE module=%s path=%s\n",module,loaded);
        }
        check(rx::Configure(true),"receive journal configure");
        fastdds_dsoftbus::ros2_target_observation::Begin(run,0);
        Setup setup;host::LinkConfiguration link;link.side=0;link.port=static_cast<uint16_t>(port);
        link.peer_port=link.port;link.run=run;link.io_observation=true;
        link.package=setup.socket.package_name;link.local_listener=setup.socket.listener_identity.service_name;
        link.peer_listener=setup.socket.peer_name;link.peer_outgoing=setup.socket.expected_peer_initiator_name;
        host::ConfigureLink(link);LinkOwner link_owner;
        auto observer=std::make_shared<Observer>();experimental::Runtime runtime(setup.socket,observer);Receiver receiver;
        auto descriptor=runtime.MakeDescriptor(setup.descriptor);check(bool(descriptor),"runtime descriptor");
        RuntimeOwner owner{runtime,setup.local};owner.transport.reset(descriptor->create_transport());
        check(owner.transport&&owner.transport->init(),"transport init");
        owner.open=owner.transport->OpenInputChannel(setup.local,&receiver,frame_size);check(owner.open,"input open");
        await([&]{return phase0::RuntimeProbeAccess::ObserveConnection(runtime).adapter.accepted_phase==phase0::AcceptedPhase::Active;},"accepted activation");
        const auto sockets=runtime.ObserveSocket();check(sockets.outgoing_socket&&sockets.accepted_socket,"actual socket owners");
        auto send=[&](unsigned sequence,unsigned index){
            const auto payload=frame(sequence);check(::SendBytes(*sockets.outgoing_socket,payload.data(),static_cast<uint32_t>(payload.size()))==0,"Host SendBytes");
            const auto& observed=observer->Wait(index);
            check(observed.ingress.ordinal==index+1&&observed.ingress.socket==*sockets.accepted_socket&&
                observed.ingress.epoch>0&&observed.ingress.handle!=0&&observed.ingress.size==frame_size,"original ScopedIngress identity");
            std::printf("HOST_RX_SOCKET_ADMISSION schema=1 run=%s side=A pid=%lu attempt=%u payload=%u ingress=%llu socket=%d epoch=%llu handle=%llu size=%zu scope=%llu record=%llu gate=%llu reason=%d\n",
                run.c_str(),GetCurrentProcessId(),index+1,sequence,observed.ingress.ordinal,observed.ingress.socket,
                observed.ingress.epoch,observed.ingress.handle,observed.ingress.size,observed.decision.bridge.observation_scope,
                observed.decision.bridge.observation_record,observed.decision.bridge.observation_gate,static_cast<int>(observed.decision.bridge.reason));
            return observed.decision;
        };
        std::uint64_t scope=0;std::array<std::uint64_t,5> records{};
        for(unsigned i=0;i<4;++i){const auto decision=send(101+i,i);
            check(decision.branch==ReceiveDecisionBranch::BridgeAccepted&&!decision.refused_receive&&
                decision.bridge.reason==RxAdmissionReason::AcceptedQueue,"held admission");
            if(!scope)scope=decision.bridge.observation_scope;
            check(scope&&decision.bridge.observation_scope==scope&&decision.bridge.observation_record,"immutable admission scope");
            records[i]=decision.bridge.observation_record;
        }
        const auto refused=send(105,4);const auto& cap=refused.bridge;
        check(receiver.delivered==0&&observer->released.load()==0,"no delivery or release before directed pump");
        check(refused.branch==ReceiveDecisionBranch::BridgeRefused&&refused.refused_receive&&cap.result_known&&
            cap.result==static_cast<int>(ReceiveAdmissionResult::Capacity)&&cap.reason==RxAdmissionReason::LocalBytes&&
            cap.capacity_known&&cap.capacity_mask==8&&cap.local_bytes_used==262000&&cap.local_bytes_limit==262144&&
            cap.local_records_used==4&&cap.observation_scope==scope&&cap.observation_record==0,"original LOCAL_BYTES refusal gate");
        for(unsigned i=0;i<4;++i)check(runtime.PumpReceive()==phase0::BackendStatus::None,"held receive pump");
        check(receiver.correct&&receiver.delivered==4&&observer->released.load()==4,"held records delivered and released");
        for(unsigned i=0;i<4;++i)check(receiver.sequences[i]==101+i,"held payload order");
        const auto recovery=send(105,5);records[4]=recovery.bridge.observation_record;
        check(recovery.branch==ReceiveDecisionBranch::BridgeAccepted&&recovery.bridge.observation_scope==scope&&records[4],"explicit resend admission");
        for(unsigned i=0;i<4;++i)check(records[i]!=records[4],"recovery new immutable record");
        check(runtime.PumpReceive()==phase0::BackendStatus::None,"recovery receive pump");
        check(receiver.correct&&receiver.delivered==5&&receiver.sequences[4]==105&&observer->released.load()==5,"resend payload delivery and release");
        check(owner.Close(),"runtime resource-complete close");link_owner.Close();
        const auto native=phase0::RuntimeProbeAccess::ObserveConnection(runtime).native;const auto io=host::ObserveLink();
        check(native.callbacks==0&&native.receives==0&&!native.entered_context_lost,"native ownership drained");
        check(io.sends==6&&io.ingress==6&&io.send_errors==0&&!observer->overflow.load()&&observer->reserved.load()==6,"exact callback accounting");
        check(host::io_observation::Export(run.c_str(),0),"complete IO and connection export");
        check(obs::Export(run.c_str(),0,GetCurrentProcessId()),"complete receive export");
        check(fastdds_dsoftbus::ros2_host_callback::ExportCoverage(run.c_str(),0,GetCurrentProcessId(),io.ingress,phase0::RuntimeProbeAccess::ObserveConnection(runtime).adapter),"complete independent callback coverage");
        std::printf("HOST_RX_SOCKET_DONE schema=1 run=%s side=A pid=%lu port=%lu sends=6 ingress=6 held=4 refused=1 delivered=5 released=5 runtime_closed=1 joined=1 payload=OPAQUE_COMPONENT ros=NOT_TESTED native=NOT_TESTED historical=NOT_REPRODUCED\n",
            run.c_str(),GetCurrentProcessId(),port);std::fflush(stdout);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"HOST_RX_SOCKET_FAILED reason=%s\n",error.what());return 1;}
}
