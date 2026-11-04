#include "TransportBootstrap.hpp"
#include "V1NativeNameTestSupport.hpp"
#include "FakePublicSocketLink.hpp"
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.h>
#include <condition_variable>
#include <future>
#include <iostream>
#include <thread>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#endif

using namespace fastdds_dsoftbus::dsoftbus;
using namespace fastdds_dsoftbus::stock_transport;
using namespace std::chrono_literals;
namespace rtps=eprosima::fastdds::rtps;
namespace {
void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
void WorkLoadedDll() {
#ifdef _WIN32
    const auto expected=_wgetenv(L"T2_EXPECTED_FASTDDS_DLL");
    check(expected&&*expected,"explicit expected stock DLL path");
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    check(snapshot!=INVALID_HANDLE_VALUE,"work Toolhelp module snapshot");
    MODULEENTRY32W row{};row.dwSize=sizeof(row);bool found=false;
    for(BOOL ok=Module32FirstW(snapshot,&row);ok;ok=Module32NextW(snapshot,&row)){
        if(_wcsicmp(row.szModule,L"fastrtps-2.14.dll"))continue;
        found=_wcsicmp(row.szExePath,expected)==0;
        std::wcout<<L"ACTUAL_FASTDDS_DLL="<<row.szExePath<<std::endl;
    }
    CloseHandle(snapshot);check(found,"work loaded DLL differs from pinned stock path");
#endif
}
void SignalContract() {
    RuntimeWorkSignal signal;
    auto seen=signal.Generation();
    signal.Notify(); // Publication after empty drain, before wait.
    check(signal.Wait(seen)==WorkWaitResult::Work,"lost notify before wait");
    auto blocked=std::async(std::launch::async,[&]{return signal.Wait(seen);});
    check(blocked.wait_for(80ms)==std::future_status::timeout,"idle wait did not sleep");
    signal.RequestStop();
    check(blocked.wait_for(2s)==std::future_status::ready&&blocked.get()==WorkWaitResult::StopRequested,
        "stop did not unblock idle waiter");
    signal.Close();
    check(signal.Wait(seen)==WorkWaitResult::Closed,"close distinct from stop");
    RuntimeWorkSignal expiry;
    seen=expiry.Generation();
    const auto due=RuntimeWorkSignal::Clock::now()+80ms;
    check(expiry.Wait(seen,due)==WorkWaitResult::Deadline&&RuntimeWorkSignal::Clock::now()>=due,
        "wait did not reach real deadline");
    // Once the real expired record is removed, caller recomputes max: no stale
    // immediate timeout or periodic timer is retained inside the signal.
    auto after_expiry=std::async(std::launch::async,[&]{return expiry.Wait(seen);});
    check(after_expiry.wait_for(80ms)==std::future_status::timeout,"stale expiry caused spin");
    expiry.Close();check(after_expiry.get()==WorkWaitResult::Closed,"expiry waiter close");
}
struct WorkReceiver final:rtps::TransportReceiverInterface {
    std::mutex gate;std::condition_variable changed;
    std::vector<std::vector<std::uint8_t>> received;
    std::thread::id delivered_on;
    bool hold=false,entered=false,release=false;
    void OnDataReceived(const eprosima::fastrtps::rtps::octet* p,std::uint32_t n,
            const Locator&,const Locator&) override {
        std::unique_lock<std::mutex> lock(gate);
        received.emplace_back(p,p+n);delivered_on=std::this_thread::get_id();entered=true;changed.notify_all();
        if(hold)changed.wait(lock,[&]{return release;});
    }
};
struct Worker final {
    experimental::Runtime& runtime;
    std::mutex gate;std::condition_variable changed;
    unsigned cycles=0,sends=0;bool paused=false,resume=false;
    const bool pause_first;
    std::promise<WorkWaitResult> terminal;
    std::future<WorkWaitResult> done=terminal.get_future();
    std::thread thread;
    explicit Worker(experimental::Runtime& r,bool pause=false):runtime(r),pause_first(pause),thread([this]{
        try {
            for(;;) {
                auto generation=runtime.WorkGeneration();
                unsigned sent=0;
                while(runtime.PumpSend().status==NativeStatus::None)++sent;
                while(runtime.PumpReceive()==phase0::BackendStatus::None){}
                {
                    std::unique_lock<std::mutex> lock(gate);
                    sends+=sent;++cycles;
                    if(pause_first&&cycles==1)paused=true;
                    changed.notify_all();
                    // Deterministic test scheduling of a caller-owned thread,
                    // after actual drains and before Wait. No provider observer
                    // grants work, changes a deadline, or runs the pump.
                    if(paused)changed.wait(lock,[&]{return resume;});
                }
                const auto result=runtime.WaitForWork(generation);
                if(result==WorkWaitResult::Work||result==WorkWaitResult::Deadline)continue;
                terminal.set_value(result);return;
            }
        } catch(...) {terminal.set_exception(std::current_exception());}
    }){}
    ~Worker(){
        if(thread.joinable()){
            {std::lock_guard<std::mutex> lock(gate);resume=true;changed.notify_all();}
            runtime.RequestPumpStop();thread.join();
        }
    }
    void WaitCycle(unsigned n) {
        std::unique_lock<std::mutex> lock(gate);
        check(changed.wait_for(lock,3s,[&]{return cycles>=n;}),"worker drain watchdog");
    }
    WorkWaitResult Join() {
        check(done.wait_for(3s)==std::future_status::ready,"worker did not terminate");
        auto result=done.get();thread.join();return result;
    }
};
struct WorkPair final {
    static v1::DeviceIdentity Identity(unsigned side) {return {"stock-host-work",151,side?"synthetic-B":"synthetic-A"};}
    static std::string Listener(unsigned side) {
        std::string value;
        check(v1::AddressCodec::EncodeScopedServiceName({Identity(side).name_space,151,17+side,v1::Profile::BytesNative},
            {v1::NativeNameContract::PackageScopedV1,"host.public.dds","host.public.dds.v1"},value),"service name");
        return value;
    }
    static phase0::PublicSocketConfiguration Config(unsigned side) {
        phase0::PublicSocketConfiguration value;
        value.identity={side?"host.public.B.outgoing":"host.public.A.outgoing",{910+side*4},{1010+side*4}};
        value.listener_identity={Listener(side),{911+side*4},{1011+side*4}};
        value.package_name="host.public.dds";value.peer_name=Listener(1-side);
        value.peer_network_id=side?"model.net.A":"model.net.B";
        value.expected_peer_initiator_name=side?"host.public.A.outgoing":"host.public.B.outgoing";
        value.bind_qos={{0,80},{1,2000}};value.listen_qos=value.bind_qos;value.max_message_size=8192;
        return value;
    }
    static DSoftBusTransportDescriptor Descriptor(unsigned side) {
        const auto c=Config(side);DSoftBusTransportDescriptor value(8192);
        V1Deployment input{Identity(side),17+side,v1::Profile::BytesNative,"stock.host",
            {{Identity(1-side),18-side,c.peer_network_id,c.identity.service_name,{c.expected_peer_initiator_name}}}};
        check(value.ConfigureV1(ScopedHostDeployment(input)).code==DirectoryError::None,"descriptor");return value;
    }
};
// Lazy-TX contract (2026-10-26): a view's outgoing circuit is dialed only by
// its first send after genuine Ready; a receive-only peer never dials. Native
// Fake rows are therefore listeners + outgoing of each sender + the matching
// accepted row on its peer.
void ExpectLazyNative(unsigned live,unsigned binds_a,unsigned binds_b,const char* why){
    const auto s=public_socket_fake::Observe();
    if(s.live!=live||s.binds[0]!=binds_a||s.binds[1]!=binds_b)
        std::cout<<"LAZY_NATIVE_MISMATCH why="<<why<<" live="<<s.live<<" binds="<<s.binds[0]<<','<<s.binds[1]<<std::endl;
    check(s.live==live&&s.binds[0]==binds_a&&s.binds[1]==binds_b,why);
}
void RunWorkLifecycle(const std::string& mode) {
    const bool factory_case=mode=="factory"||mode=="factory-held-native";
    const bool stop_case=mode=="stop",inflight=mode=="stop-inflight";
    const bool unsupported=mode=="unsupported",held_native=mode=="factory-held-native";
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),!factory_case);
    public_socket_fake::SetListenerNames({WorkPair::Listener(0),WorkPair::Listener(1)});
    auto ca=WorkPair::Descriptor(0),cb=WorkPair::Descriptor(1);
    auto ac=WorkPair::Config(0),bc=WorkPair::Config(1);
    ac.bounded_reaccept_host=unsupported;bc.bounded_reaccept_host=unsupported;
    experimental::Runtime a(ac),b(bc);
    std::mutex start_gate;std::condition_variable start_ready;unsigned arrived=0;
    auto barrier=[&]{std::unique_lock<std::mutex> lock(start_gate);++arrived;start_ready.notify_all();
        return start_ready.wait_for(lock,3s,[&]{return arrived==2;});};
    check(a.SetPreConnectGate(barrier)&&b.SetPreConnectGate(barrier),"preconnect barrier");
    auto da=a.MakeDescriptor(ca),db=b.MakeDescriptor(cb);check(da&&db,"descriptors");
    auto& factory=*eprosima::fastdds::dds::DomainParticipantFactory::get_instance();
    StockTransportBootstrap bootstrap(factory);
    WorkReceiver ra,rb;
    rb.hold=inflight;
    std::unique_ptr<rtps::TransportInterface> ta,tb(db->create_transport());
    eprosima::fastdds::dds::DomainParticipant* participant=nullptr;
    bool peer=false;
    std::thread other([&]{peer=tb&&tb->init()&&tb->OpenInputChannel(cb.directory.locals.front().local_locator,&rb,8192);});
    if(factory_case){
        check(bootstrap.Install(151,da,{ca.directory.locals.front().local_locator},
            {ca.directory.routes.front().key.remote_locator})==BootstrapResult::Ok,"public defaults");
        participant=factory.create_participant(151,eprosima::fastdds::dds::PARTICIPANT_QOS_DEFAULT);
    } else {
        ta.reset(da->create_transport());
        check(ta&&ta->init()&&ta->OpenInputChannel(ca.directory.locals.front().local_locator,&ra,8192),"A input");
    }
    other.join();check(peer&&(!factory_case||participant),"actual native pair");
    const auto before_confirmation=public_socket_fake::Observe();
    const auto* ga=dynamic_cast<DSoftBusTransport*>(ta.get());const auto* gb=dynamic_cast<DSoftBusTransport*>(tb.get());
    const auto sa=ga?ga->domain_snapshot():ViewDomainSnapshot{},sb=gb?gb->domain_snapshot():ViewDomainSnapshot{};
    const auto runtime_a=phase0::RuntimeProbeAccess::ObserveRuntime(a),runtime_b=phase0::RuntimeProbeAccess::ObserveRuntime(b);
    const auto socket_a=a.ObserveSocket(),socket_b=b.ObserveSocket();
    std::cout<<"WORK_PRE_RAW_CONFIRM_DIAGNOSTIC mode="<<mode<<" raw_confirmation_executed=0"
        <<" live="<<before_confirmation.live<<" sockets_a="<<before_confirmation.sockets[0]<<" sockets_b="<<before_confirmation.sockets[1]
        <<" listens_a="<<before_confirmation.listens[0]<<" listens_b="<<before_confirmation.listens[1]
        <<" binds_a="<<before_confirmation.binds[0]<<" binds_b="<<before_confirmation.binds[1]
        <<" guard_a_present="<<bool(ga)<<" guard_a_expected_known="<<sa.expected_domain_known<<" guard_a_expected="<<sa.expected_domain_id
        <<" guard_a_actual_known="<<bool(sa.first_observed_domain)<<" guard_a_actual="<<sa.first_observed_domain.value_or(0)<<" guard_a_state="<<(ga?static_cast<int>(sa.state):-1)
        <<" guard_b_present="<<bool(gb)<<" guard_b_expected_known="<<sb.expected_domain_known<<" guard_b_expected="<<sb.expected_domain_id
        <<" guard_b_actual_known="<<bool(sb.first_observed_domain)<<" guard_b_actual="<<sb.first_observed_domain.value_or(0)<<" guard_b_state="<<(gb?static_cast<int>(sb.state):-1)
        <<" apps_a="<<runtime_a.receive.active_applications<<" apps_b="<<runtime_b.receive.active_applications
        <<" circuits_a="<<socket_a.domain_circuits<<" circuits_b="<<socket_b.domain_circuits
        <<" api_error_a_known="<<bool(socket_a.first_api_error)<<" api_error_b_known="<<bool(socket_b.first_api_error)<<std::endl;
    if(unsupported){
        check(public_socket_fake::Observe().live==6,"six live native Fake sockets");
        const auto legacy_observation=phase0::RuntimeProbeAccess::ObserveDomainCircuits(a);
        std::cout<<"WORK_LEGACY_CIRCUIT_SNAPSHOT status="<<static_cast<int>(legacy_observation.status)
            <<" count="<<legacy_observation.count<<" native_live="<<public_socket_fake::Observe().live<<std::endl;
        check(legacy_observation.status==host_test::DomainCircuitObservationStatus::Disabled&&legacy_observation.count==0,
            "legacy experiment unexpectedly reported domain circuits");
        auto ga=a.WorkGeneration(),gb=b.WorkGeneration();
        check(a.WaitForWork(ga)==WorkWaitResult::UnsupportedConfiguration&&
            b.WaitForWork(gb)==WorkWaitResult::UnsupportedConfiguration,"active experiment entered unsupported sleep");
        ta.reset();tb.reset();
        const auto closed=public_socket_fake::Observe();
        check(closed.live==0&&closed.queued==0&&closed.bytes==0&&closed.invalid==0,"unsupported-mode cleanup");
        check(a.Close()&&b.Close(),"unsupported-mode close");return;
    }
    // The raw component driver, unlike the real Factory side, owns its selected
    // domain. Keep a real SenderResource denial before its explicit public hook.
    auto confirm_raw=[&](rtps::TransportInterface& transport,const DSoftBusTransportDescriptor& config){
        auto* guarded=dynamic_cast<DSoftBusTransport*>(&transport);
        check(guarded&&guarded->domain_snapshot().state==ViewDomainState::Await,"raw view not awaiting actual domain");
        rtps::SendResourceList pending;
        const auto remote=config.directory.routes.front().key.remote_locator;
        check(transport.OpenOutputChannel(pending,remote),"raw Await output resource");
        std::vector<Locator> targets{remote};rtps::Locators first(targets.begin()),last(targets.end());
        const std::uint8_t opaque[]{1,3,5,7};
        check(!pending.front()->send(opaque,sizeof(opaque),&first,&last,std::chrono::steady_clock::now()+3s),"raw Await submitted bytes");
        rtps::LocatorList peers;fastdds_dsoftbus::binding::PortParameters ports;
        Locator initial=remote;
        check(transport.configureInitialPeerLocator(initial,ports,151,peers)&&guarded->domain_snapshot().state==ViewDomainState::Ready,
            "controlled raw domain151 confirmation");
    };
    confirm_raw(*tb,cb);if(!factory_case)confirm_raw(*ta,ca);
    // Confirmation alone opens no TX; only the Factory Participant has sent (SPDP).
    if(factory_case)ExpectLazyNative(4,1,0,"lazy TX: only the sending Factory side dialed");
    else ExpectLazyNative(2,0,0,"lazy TX: confirmed raw views must not dial before a send");
    if(factory_case){
        const auto active=phase0::RuntimeProbeAccess::ObserveDomainCircuits(a);
        const host_test::DomainCircuitObservation* active_domain=nullptr;
        for(const auto& entry:active.entries)if(entry)active_domain=&*entry;
        std::cout<<"WORK_ACTIVE_CIRCUIT_SNAPSHOT status="<<static_cast<int>(active.status)
            <<" root_closed="<<active.root_closed<<" terminal_adapters_available="<<active.terminal_adapters_available
            <<" count="<<active.count<<" domain="<<(active_domain?active_domain->domain:0)
            <<" native_available="<<(active_domain&&bool(active_domain->native))
            <<" adapter_available="<<(active_domain&&bool(active_domain->adapter))<<std::endl;
        check(active.status==host_test::DomainCircuitObservationStatus::Available&&!active.root_closed&&
            !active.terminal_adapters_available&&active.count==1&&active_domain&&active_domain->domain==151&&
            active_domain->native&&!active_domain->adapter,"active circuit exposed a terminal adapter snapshot");
    }
    std::mutex native_gate;std::condition_variable native_changed;bool native_entered=false,native_release=false;
    if(held_native)public_socket_fake::SetSendObserver([&](int side,int){
        if(side!=0)return;
        std::unique_lock<std::mutex> lock(native_gate);
        if(native_entered)return;
        native_entered=true;native_changed.notify_all();
        native_changed.wait(lock,[&]{return native_release;});
    });
    Worker wa(a,!factory_case&&!stop_case),wb(b);
    const auto receiver_thread=wb.thread.get_id();
    if(held_native){
        std::unique_lock<std::mutex> lock(native_gate);
        check(native_changed.wait_for(lock,3s,[&]{return native_entered;}),"actual DDS native send did not enter");
        check(phase0::RuntimeProbeAccess::ObserveConnection(a).native.send.entered==1,"native send lease not held");
    } else wa.WaitCycle(1);
    wb.WaitCycle(1);
    {
        std::unique_lock<std::mutex> lock(wb.gate);
        const auto cycles=wb.cycles;
        check(!wb.changed.wait_for(lock,80ms,[&]{return wb.cycles!=cycles;}),
            "idle Runtime worker repeatedly drained without work");
    }
    bool b_joined=false;
    if(!factory_case&&!stop_case){
        rtps::SendResourceList senders;
        check(ta->OpenOutputChannel(senders,ca.directory.routes.front().key.remote_locator),"output resource");
        const std::vector<std::uint8_t> expected{1,3,5,7};
        for(unsigned i=0;i<3;++i){
            auto bytes=expected;
            std::vector<Locator> peers{ca.directory.routes.front().key.remote_locator};
            rtps::Locators first(peers.begin()),last(peers.end());
            check(senders.front()->send(bytes.data(),static_cast<std::uint32_t>(bytes.size()),&first,&last,
                std::chrono::steady_clock::now()+3s),"actual TX admission");
            std::fill(bytes.begin(),bytes.end(),0xff);
        }
        // Three accepted batches were published after the worker's empty drain
        // and before its wait. Resume must drain all three without another send.
        {std::lock_guard<std::mutex> lock(wa.gate);check(wa.paused,"lost wake scheduling barrier");wa.resume=true;wa.changed.notify_all();}
        wa.WaitCycle(2);
        {std::lock_guard<std::mutex> lock(wa.gate);check(wa.sends==3,"coalesced TX not fully drained");}
        for(unsigned i=0;i<3;++i)check(public_socket_fake::PumpOne(),"actual queued SDK Fake frame");
        if(inflight){
            {
                std::unique_lock<std::mutex> lock(rb.gate);
                check(rb.changed.wait_for(lock,3s,[&]{return rb.entered;}),"receiver not actually running");
            }
            b.RequestPumpStop();
            check(wb.done.wait_for(80ms)==std::future_status::timeout,"stop cancelled running receiver unsafely");
            ExpectLazyNative(4,1,0,"stop closed native resources with receiver running");
            {std::lock_guard<std::mutex> lock(rb.gate);rb.release=true;rb.changed.notify_all();}
            check(wb.Join()==WorkWaitResult::StopRequested,"running receiver did not settle before stop result");
            b_joined=true;
        }
        {
            std::unique_lock<std::mutex> lock(rb.gate);
            check(rb.changed.wait_for(lock,3s,[&]{return rb.received.size()==3;}),"RX work did not wake worker");
            for(const auto& bytes:rb.received)check(bytes==expected,"borrowed buffer was not copied");
            check(rb.delivered_on==receiver_thread&&rb.delivered_on!=std::this_thread::get_id(),
                "SDK callback dispatched RTPS instead of queuing");
        }
        senders.clear();
    }
    if(stop_case){
        a.RequestPumpStop();check(wa.Join()==WorkWaitResult::StopRequested,"explicit stop result");
        ExpectLazyNative(2,0,0,"stop silently closed native resources");
    }
    if(factory_case){
        const auto before=public_socket_fake::Observe();
        if(held_native){
            // Default (no replacement/node experiment): real OnShutdown while
            // an original send lease is held, then original public Factory
            // deletion on its control thread. No manual pump advances close.
            public_socket_fake::DisconnectDirection(0,true);
            auto deletion=std::async(std::launch::async,[&]{return factory.delete_participant(participant);});
            check(deletion.wait_for(80ms)==std::future_status::timeout,"delete ignored running native lease");
            {std::lock_guard<std::mutex> lock(native_gate);native_release=true;native_changed.notify_all();}
            check(deletion.wait_for(3s)==std::future_status::ready&&deletion.get()==0,"delete failed after last lease settlement");
        } else check(factory.delete_participant(participant)==0,"stock public Factory deletion");
        check(wa.Join()==WorkWaitResult::Closed,"natural Transport close did not wake idle worker");
        public_socket_fake::SetSendObserver({});
        const auto state=phase0::RuntimeProbeAccess::ObserveConnection(a);
        const auto native=state.native;
        std::cout<<"AFTER_PUBLIC_DELETE native_owners="<<native.owners<<" retained_ids="<<native.owned_ids
            <<" retained_not_quiescent="<<native.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]
            <<" callbacks="<<native.callbacks<<" receives="<<native.receives
            <<" setup="<<native.setup.reserved<<','<<native.setup.entered
            <<" send="<<native.send.reserved<<','<<native.send.entered
            <<" close="<<native.close.reserved<<','<<native.close.entered<<'\n';
        // owned_ids counts deliberately retained original identities even after
        // Shutdown. Do not erase these tombstones or claim SDK quiescence. Every
        // owner must be terminal; all actual activity/credits must be zero.
        // Lazy TX: A owns its listener and its dialed outgoing only; receive-only B
        // never dialed, so A has no accepted owner.
        check(native.owners==2&&native.owned_ids==2&&
            native.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]==2,
            "not all original owners reached retained terminal state");
        check(native.callbacks==0&&native.receives==0&&
            native.setup.reserved==0&&native.setup.entered==0&&native.send.reserved==0&&native.send.entered==0&&
            native.close.reserved==0&&native.close.entered==0,"pending native resources after natural delete");
        const auto cleanup_runtime=phase0::RuntimeProbeAccess::ObserveRuntime(a);
        const auto cleanup_global=public_socket_fake::Observe();const auto cleanup_socket=a.ObserveSocket();
        std::cout<<"WORK_FACTORY_ROOT_CLEANUP_DIAGNOSTIC mode="<<mode<<" root_phase="<<static_cast<int>(state.adapter.phase)
            <<" root_input_open="<<state.adapter.input_open<<" root_provider_stopped="<<state.adapter.provider_stopped
            <<" root_ingress_active="<<state.adapter.ingress_active<<" root_ingress_rejected="<<state.adapter.ingress_rejected
            <<" root_provider_exceptions="<<state.adapter.provider_exceptions<<" root_provider_status="<<static_cast<int>(state.adapter.provider_status)
            <<" runtime_closed="<<cleanup_runtime.closed<<" runtime_sealed="<<cleanup_runtime.sealed<<" active_pumps="<<cleanup_runtime.active_pumps
            <<" admitted_outstanding="<<cleanup_runtime.runtime_admitted_outstanding
            <<" close_status="<<static_cast<int>(cleanup_runtime.close.status)<<" cleanup_status="<<static_cast<int>(cleanup_runtime.close.cleanup_status)
            <<" adapter_drained="<<cleanup_runtime.close.adapter_drained
            <<" apps="<<cleanup_runtime.receive.active_applications<<" detaching_apps="<<cleanup_runtime.receive.detaching_applications
            <<" retained_apps="<<cleanup_runtime.receive.retained_applications<<" records="<<cleanup_runtime.receive.records
            <<" bytes="<<cleanup_runtime.receive.bytes<<" copying="<<cleanup_runtime.receive.copying<<" queued="<<cleanup_runtime.receive.queued
            <<" taken="<<cleanup_runtime.receive.taken<<" delivering="<<cleanup_runtime.receive.delivering
            <<" circuits="<<cleanup_socket.domain_circuits<<" family_ids_retained="<<cleanup_socket.family_ids_retained
            <<" family_collisions="<<cleanup_socket.family_collisions
            <<" global_live="<<cleanup_global.live<<" global_queued="<<cleanup_global.queued<<" global_bytes="<<cleanup_global.bytes<<" global_invalid="<<cleanup_global.invalid
            <<" sockets_a="<<cleanup_global.sockets[0]<<" sockets_b="<<cleanup_global.sockets[1]
            <<" binds_a="<<cleanup_global.binds[0]<<" binds_b="<<cleanup_global.binds[1]
            <<" shutdowns_a="<<cleanup_global.shutdowns[0]<<" shutdowns_b="<<cleanup_global.shutdowns[1]<<std::endl;
        unsigned root_entry_index=0;
        for(const auto* diagnostic:{&state.adapter.binding,&state.adapter.listener,&state.adapter.accepted}){
            std::cout<<"WORK_FACTORY_ROOT_ENTRY_DIAGNOSTIC index="<<root_entry_index++<<" phase="<<static_cast<int>(diagnostic->phase)
                <<" socket="<<diagnostic->socket<<" created_record="<<diagnostic->created_record<<" owner_closed="<<diagnostic->owner_closed
                <<" owner_slot="<<diagnostic->owner_slot<<" owner_incarnation="<<diagnostic->owner_incarnation
                <<" membership="<<diagnostic->membership<<" application_bound="<<diagnostic->application_bound<<" receiver="<<diagnostic->receiver_open
                <<" callbacks="<<diagnostic->callbacks_in_flight<<" owner_callbacks="<<diagnostic->owner_callback_inflight<<" owner_receives="<<diagnostic->owner_receive_inflight
                <<" records="<<diagnostic->receive.records<<" bytes="<<diagnostic->receive.bytes<<" copying="<<diagnostic->receive.copying
                <<" queued="<<diagnostic->receive.queued<<" taken="<<diagnostic->receive.taken<<" delivering="<<diagnostic->receive.delivering<<std::endl;
        }
        const auto circuit_observation=phase0::RuntimeProbeAccess::ObserveDomainCircuits(a);
        std::cout<<"WORK_CIRCUIT_SNAPSHOT status="<<static_cast<int>(circuit_observation.status)
            <<" root_closed="<<circuit_observation.root_closed<<" terminal_adapters_available="<<circuit_observation.terminal_adapters_available
            <<" count="<<circuit_observation.count<<" cross_group_atomic=0"<<std::endl;
        for(std::size_t slot=0;slot<circuit_observation.entries.size();++slot){
            const auto& sampled=circuit_observation.entries[slot];
            if(!sampled){std::cout<<"WORK_CIRCUIT slot="<<slot<<" present=0"<<std::endl;continue;}
            std::cout<<"WORK_CIRCUIT slot="<<slot<<" present=1 domain="<<sampled->domain
                <<" adapter_available="<<bool(sampled->adapter)<<" native_available="<<bool(sampled->native)<<std::endl;
            if(sampled->native){const auto& n=*sampled->native;
                std::cout<<"WORK_CIRCUIT_NATIVE slot="<<slot<<" owners="<<n.owners<<" retained_ids="<<n.owned_ids
                    <<" retained_not_quiescent="<<n.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]
                    <<" callbacks="<<n.callbacks<<" receives="<<n.receives<<" setup_reserved="<<n.setup.reserved<<" setup_entered="<<n.setup.entered
                    <<" send_reserved="<<n.send.reserved<<" send_entered="<<n.send.entered<<" close_reserved="<<n.close.reserved<<" close_entered="<<n.close.entered<<std::endl;
            }
            if(sampled->adapter){const auto& actual=*sampled->adapter;
                std::cout<<"WORK_CIRCUIT_ADAPTER slot="<<slot<<" phase="<<static_cast<int>(actual.phase)<<" input_open="<<actual.input_open
                    <<" stopped="<<actual.provider_stopped<<" ingress_active="<<actual.ingress_active<<" exceptions="<<actual.provider_exceptions<<std::endl;
                unsigned entry_index=0;
                for(const auto* entry:{&actual.binding,&actual.listener,&actual.accepted}){
                    std::cout<<"WORK_CIRCUIT_ENTRY slot="<<slot<<" index="<<entry_index++<<" phase="<<static_cast<int>(entry->phase)
                        <<" socket="<<entry->socket<<" created_record="<<entry->created_record<<" owner_closed="<<entry->owner_closed
                        <<" owner_slot="<<entry->owner_slot<<" owner_incarnation="<<entry->owner_incarnation<<" record_incarnation="<<entry->record_incarnation
                        <<" membership="<<entry->membership<<" application_bound="<<entry->application_bound<<" receiver_open="<<entry->receiver_open
                        <<" callbacks="<<entry->callbacks_in_flight<<" owner_callbacks="<<entry->owner_callback_inflight<<" owner_receives="<<entry->owner_receive_inflight
                        <<" records="<<entry->receive.records<<" bytes="<<entry->receive.bytes<<" copying="<<entry->receive.copying
                        <<" queued="<<entry->receive.queued<<" taken="<<entry->receive.taken<<" delivering="<<entry->receive.delivering<<std::endl;
                }
            }
        }
        check(state.adapter.phase==phase0::AdapterPhase::Closed&&!state.adapter.input_open&&state.adapter.ingress_active==0,
            "adapter activity after natural delete");
        check(circuit_observation.status==host_test::DomainCircuitObservationStatus::Available&&
            circuit_observation.root_closed&&circuit_observation.terminal_adapters_available&&circuit_observation.count==1,
            "closed circuit snapshot unavailable or unexpected circuit count");
        const host_test::DomainCircuitObservation* closed_circuit=nullptr;
        for(const auto& entry:circuit_observation.entries)if(entry)closed_circuit=&*entry;
        check(closed_circuit&&closed_circuit->domain==151&&closed_circuit->adapter&&closed_circuit->native,
            "original domain151 terminal circuit resources unavailable");
        const auto& circuit_adapter=*closed_circuit->adapter;
        const auto& circuit_native=*closed_circuit->native;
        check(circuit_adapter.phase==phase0::AdapterPhase::Closed&&!circuit_adapter.input_open&&circuit_adapter.ingress_active==0,
            "circuit adapter activity after natural delete");
        std::cout<<"WORK_TERMINAL_CIRCUIT_NATIVE owners="<<circuit_native.owners<<" owned_ids="<<circuit_native.owned_ids
            <<" retained="<<circuit_native.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]<<std::endl;
        // Lazy TX: the circuit owns only A's dialed outgoing; B never dialed A.
        check(circuit_native.owners==1&&circuit_native.owned_ids==1&&
            circuit_native.phases[static_cast<std::size_t>(NativeOwnerPhase::RetainedNotQuiescent)]==1,
            "circuit original identities not retained in terminal state");
        check(circuit_native.callbacks==0&&circuit_native.receives==0&&
            circuit_native.setup.reserved==0&&circuit_native.setup.entered==0&&
            circuit_native.send.reserved==0&&circuit_native.send.entered==0&&
            circuit_native.close.reserved==0&&circuit_native.close.entered==0,"pending circuit native activity after natural delete");
        std::cout<<"WORK_CLEANUP_OWNER_MAPPING root_native_includes_circuit=1 circuit_listener_metadata_only=1"
            <<" owner_fields_after_release=UNAVAILABLE SDK_QUIESCENCE=UNPROVEN"<<std::endl;
        // Lazy TX: receive-only B never dialed, so the circuit accepted entry was never used.
        for(const auto* entry:{&circuit_adapter.binding,&circuit_adapter.listener,&state.adapter.listener}){
            std::cout<<"CLOSED_ENTRY phase="<<static_cast<int>(entry->phase)<<" socket="<<entry->socket
                <<" membership="<<entry->membership<<" receiver="<<entry->receiver_open
                <<" callbacks="<<entry->callbacks_in_flight<<" records="<<entry->receive.records
                <<" bytes="<<entry->receive.bytes<<'\n';
            check(entry->phase==BindingPhase::Closed&&entry->socket==-1&&!entry->membership&&
                !entry->application_bound&&!entry->receiver_open&&entry->callbacks_in_flight==0&&
                entry->owner_callback_inflight==0&&entry->owner_receive_inflight==0&&
                entry->receive.records==0&&entry->receive.bytes==0&&entry->receive.copying==0&&
                entry->receive.queued==0&&entry->receive.taken==0&&entry->receive.delivering==0,
                "binding or receive resources remain after natural delete");
        }
        for(const auto* unused:{&state.adapter.binding,&state.adapter.accepted,&circuit_adapter.accepted}){
            std::cout<<"UNUSED_ROOT_ENTRY phase="<<static_cast<int>(unused->phase)<<" created_record="<<unused->created_record
                <<" socket="<<unused->socket<<" membership="<<unused->membership<<" receiver="<<unused->receiver_open<<std::endl;
            check(unused->phase==BindingPhase::Empty&&!unused->created_record&&unused->socket==-1&&!unused->membership&&
                !unused->application_bound&&!unused->receiver_open&&unused->callbacks_in_flight==0&&
                unused->owner_callback_inflight==0&&unused->owner_receive_inflight==0&&
                unused->receive.records==0&&unused->receive.bytes==0&&unused->receive.copying==0&&
                unused->receive.queued==0&&unused->receive.taken==0&&unused->receive.delivering==0,
                "unused root entry unexpectedly owns binding or receive resources");
        }
        const auto after=public_socket_fake::Observe();
        std::cout<<"AFTER_A_DELETE fake_live="<<after.live<<" A_shutdowns="<<after.shutdowns[0]
            <<" A_terminal_callbacks="<<after.notified[0]<<'\n';
        check(after.live==1&&after.shutdowns[0]+after.notified[0]==2,
            "original A Fake SDK entries not all terminated");
        const auto callback_before=a.ObserveSocket();
        public_socket_fake::LateBytes(0); // Saved original SDK callback and old numeric ID.
        const auto callback_after=a.ObserveSocket();
        check(callback_after.callback_entries==callback_before.callback_entries+1&&
            callback_after.rejected_callbacks==callback_before.rejected_callbacks+1,
            "closed original entry accepted late bytes");
        const auto late=phase0::RuntimeProbeAccess::ObserveConnection(a);
        check(late.native.owned_ids==2&&late.native.callbacks==0&&late.native.receives==0&&
            late.adapter.binding.receive.records==0&&late.adapter.listener.receive.records==0&&
            late.adapter.accepted.receive.records==0,"late old-ID callback acquired resources");
        std::cout<<"LATE_OLD_ID_CALLBACK_REJECTED=1 SDK_QUIESCENCE=UNPROVEN\n";
        check(after.shutdowns[0]>before.shutdowns[0]&&after.live<before.live,"natural socket closure absent");
        check(factory.get_default_participant_qos().transport().user_transports.front()==da,"descriptor not retained by defaults");
        check(bootstrap.Release()==BootstrapResult::Ok,"release defaults");
    } else {
        ta.reset();if(!stop_case)check(wa.Join()==WorkWaitResult::Closed,"A natural close");
    }
    tb.reset();if(!b_joined)check(wb.Join()==WorkWaitResult::Closed,"B natural close");
    const auto closed=public_socket_fake::Observe();
    std::cout<<"AFTER_PAIR_CLOSE fake_live="<<closed.live<<" queued="<<closed.queued
        <<" bytes="<<closed.bytes<<" invalid="<<closed.invalid<<'\n';
    check(closed.live==0&&closed.queued==0&&closed.bytes==0&&closed.invalid==0,"native residue");
    check(a.Close()&&b.Close(),"idempotent cleanup after proof");
}
}
int main(int argc,char** argv) {
    try {
        WorkLoadedDll();
        check(argc==2,"one case");const std::string name=argv[1];
        if(name=="signal")SignalContract();
        else if(name=="pair"||name=="factory"||name=="stop"||name=="stop-inflight"||
                name=="factory-held-native"||name=="unsupported")RunWorkLifecycle(name);
        else throw std::runtime_error("unknown case");
        std::cout<<"PASS "<<name<<" SOCKET_BOUNDARY=HOST_FAKE DEVICE_TESTED=0\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
