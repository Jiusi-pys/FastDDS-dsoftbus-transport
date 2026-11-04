// Reuse the real stock DDS fixture without changing its original source,
// assertions or separately registered legacy domains case.
#define main R39LegacyViewsMain
#include "runtime_views_tests.cpp"
#undef main
#include "ScopedTransportBootstrap.hpp"
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <future>

namespace {
class DomainIterator final : public eprosima::fastdds::rtps::LocatorsIterator {
public:
    DomainIterator(const Locator& value, bool end = false) : value_(value), end_(end) {}
    DomainIterator& operator++() override { end_ = true; return *this; }
    bool operator==(const eprosima::fastdds::rtps::LocatorsIterator& other) const override {
        const auto* p = dynamic_cast<const DomainIterator*>(&other); return p && p->end_ == end_;
    }
    bool operator!=(const eprosima::fastdds::rtps::LocatorsIterator& other) const override { return !(*this == other); }
    const Locator& operator*() const override { return value_; }
private:
    const Locator value_; bool end_;
};
bool DomainSend(fastdds_dsoftbus::binding::SenderResource& sender, const Locator& remote, std::uint8_t marker = 42) {
    DomainIterator begin(remote), end(remote, true);
    const std::uint8_t bytes[]{marker, 19, 83, 7};
    return sender.send(bytes, sizeof(bytes), &begin, &end, std::chrono::steady_clock::now() + 2s);
}
struct DomainProvider final : NativeSendProvider {
    unsigned calls = 0;
    std::int32_t Send(NativeSocketId, const Route&, const std::uint8_t*, std::size_t,
            SendQueue::Clock::time_point, std::int32_t) override { ++calls; return 0; }
};
void DomainCore() {
    auto d = T2Descriptor(0, 0);
    NativeTableConfiguration config;
    config.service_pkg_name = d.native_name_scope.authorized_package;
    config.directory = d.directory; config.directions = {RouteDirection::Dial};
    auto built = NativeOwnerTable::Build(config); T2Check(bool(built), "native table");
    auto table = std::move(*built.value);
    auto ns = table->AcquireNamespace(config.directory.locals.front().local_locator, config); T2Check(bool(ns), "namespace");
    auto dial = table->ReserveDial(*ns.value, config.directory.routes.front().key); T2Check(bool(dial), "dial reserve");
    T2Check(dial.value->socket_call->CallEntered() == NativeStatus::None, "Socket model entry");
    T2Check(dial.value->socket_call->Complete(NativeSocketResult::Id(73)).status == NativeStatus::None, "Socket model return");
    auto bind = table->BeginBind(dial.value->owner); T2Check(bool(bind), "bind reserve");
    T2Check(bind.value->CallEntered() == NativeStatus::None, "bind entry");
    T2Check(bind.value->Complete(SyncBindResult::Success()).status == NativeStatus::None, "bind return");
    NativeSendExecution execution(*table);
    auto reg = execution.CreateRuntime(config, {}, d.send_limits); T2Check(bool(reg), "egress registration");
    auto guard = ViewDomainGuard::Create(151);
    eprosima::fastdds::rtps::SendResourceList senders;
    T2Check(execution.Attach(*reg.value, senders, guard).ok(), "guarded attach");
    auto* retained = senders.front().get();
    T2Check(execution.Attach(*reg.value, senders, guard).status == SenderAttachStatus::AlreadyAttached && senders.size() == 1,
            "guarded attach did not deduplicate real sender");
    T2Check(execution.Attach(*reg.value, senders, ViewDomainGuard::Create(151)).status == SenderAttachStatus::DomainGuardConflict,
            "equal expected domain incorrectly reused another view guard");
    T2Check(execution.Attach(*reg.value, senders).status == SenderAttachStatus::DomainGuardConflict,
            "legacy attach removed a scoped binding");
    const auto remote = config.directory.routes.front().key.remote_locator;
    T2Check(!DomainSend(*retained, remote) && !execution.Take(*reg.value), "Await sender admitted native work");
    T2Check(guard->Confirm(151) == ViewDomainState::Ready, "domain confirmation");
    T2Check(DomainSend(*retained, remote), "Ready real sender");
    auto work = execution.Take(*reg.value); T2Check(bool(work), "take queued real send");
    auto provider = std::make_shared<DomainProvider>();
    auto invocation = execution.TryAdmit(*work.value, provider); T2Check(bool(invocation), "admit real native invocation");
    guard->Confirm(152);
    const auto refused = invocation.value->Run();
    T2Check(refused.outcome == NativeSendOutcome::NotEntered && refused.domain_denial == DomainAdmission::Refused && provider->calls == 0,
            "queued native invocation bypassed Refused");
    T2Check(execution.Observe(*reg.value).admitted_outstanding == 0, "refused invocation retained admission");
    T2Check(guard->Confirm(151) == ViewDomainState::Refused && !DomainSend(*retained, remote), "Refused guard resurrected");
    T2Check(guard->Snapshot().first_refused_domain == 152, "matching late callback erased refusal cause");
    guard->Close(); T2Check(!DomainSend(*retained, remote), "cached sender bypassed Closed");
    T2Check(guard->Snapshot().active == std::array<std::uint64_t, 3>{}, "domain permit residue");
    T2Check(execution.Revoke(*reg.value) == NativeStatus::None, "normal egress cleanup");
    auto closed_reg = execution.CreateRuntime(config, {}, d.send_limits); T2Check(bool(closed_reg), "second bounded registration");
    auto closed_guard = ViewDomainGuard::Create(151); closed_guard->Confirm(151);
    eprosima::fastdds::rtps::SendResourceList closed_senders;
    T2Check(execution.Attach(*closed_reg.value, closed_senders, closed_guard).ok() && DomainSend(*closed_senders.front(), remote), "Closed control send");
    auto closed_work = execution.Take(*closed_reg.value); T2Check(bool(closed_work), "Closed control work");
    auto closed_invocation = execution.TryAdmit(*closed_work.value, provider); T2Check(bool(closed_invocation), "Closed control invocation");
    closed_guard->Close(); const auto closed = closed_invocation.value->Run();
    T2Check(closed.outcome == NativeSendOutcome::NotEntered && closed.domain_denial == DomainAdmission::Closed && provider->calls == 0,
            "admitted old invocation bypassed Closed");
    T2Check(execution.Observe(*closed_reg.value).admitted_outstanding == 0, "Closed invocation retained admission");
    T2Check(execution.Revoke(*closed_reg.value) == NativeStatus::None, "Closed registration cleanup");
    std::cout << "REAL_SENDER Await=DENIED Ready=ACCEPTED Refused=DENIED Closed=DENIED NATIVE_PROVIDER_CALLS=0\n";
}
struct DomainReceiver final : eprosima::fastdds::rtps::TransportReceiverInterface {
    std::mutex gate; std::condition_variable changed; unsigned calls = 0; bool hold = false, entered = false, release = false;
    std::uint8_t marker = 0;
    void OnDataReceived(const eprosima::fastrtps::rtps::octet* bytes, std::uint32_t size, const Locator&, const Locator&) override {
        std::unique_lock<std::mutex> lock(gate); T2Check(size == 4 && bytes[1] == 19 && bytes[2] == 83 && bytes[3] == 7, "owned receive bytes");
        ++calls; marker = bytes[0]; entered = true; changed.notify_all();
        if (hold) T2Check(changed.wait_for(lock, 3s, [&] { return release; }), "receiver hold watchdog");
    }
};
void DomainTransport() {
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(), true);
    public_socket_fake::SetListenerNames({T2Listener(0, 0), T2Listener(1, 0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0, 0).identity.service_name, T2Configuration(1, 0).identity.service_name});
    experimental::Runtime a(T2Configuration(0, 0), experimental::RuntimeLifetime::PersistentNativeAnchor),
        b(T2Configuration(1, 0), experimental::RuntimeLifetime::PersistentNativeAnchor);
    std::mutex gate; std::condition_variable changed; unsigned arrived = 0;
    auto ready = [&] { std::unique_lock<std::mutex> lock(gate); ++arrived; changed.notify_all();
        return changed.wait_for(lock, 2s, [&] { return arrived == 2; }); };
    T2Check(a.SetPreConnectGate(ready) && b.SetPreConnectGate(ready), "listener readiness barrier");
    auto da = T2Descriptor(0, 0), db = T2Descriptor(1, 0);
    auto ad = a.MakeStockDescriptor(da), bd = b.MakeDescriptor(db); T2Check(ad && bd, "explicit and default guarded descriptors");
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(ad->create_transport()), tb(bd->create_transport());
    auto* scoped = dynamic_cast<DSoftBusTransport*>(ta.get()); T2Check(scoped, "concrete scoped Transport");
    DomainReceiver ra, rb, replacement;
    std::exception_ptr errors[2];
    std::thread other([&] { try { T2Check(tb->init() && tb->OpenInputChannel(db.directory.locals.front().local_locator, &rb, 8192), "peer input"); }
        catch (...) { errors[1] = std::current_exception(); } });
    try { T2Check(ta->init() && ta->OpenInputChannel(da.directory.locals.front().local_locator, &ra, 8192), "scoped input"); }
    catch (...) { errors[0] = std::current_exception(); }
    other.join(); for (auto e : errors) if (e) std::rethrow_exception(e);
    const auto local = da.directory.locals.front().local_locator, remote = da.directory.routes.front().key.remote_locator;
    const auto before = public_socket_fake::Observe();
    T2Check(ta->OpenInputChannel(local, &ra, 8192), "same locator/receiver did not reuse stable proxy");
    T2Check(!ta->OpenInputChannel(local, &replacement, 8192), "different receiver silently retargeted proxy");
    T2Check(public_socket_fake::Observe().sockets[0] == before.sockets[0], "duplicate open created native socket");
    eprosima::fastdds::rtps::SendResourceList as, bs;
    T2Check(ta->OpenOutputChannel(as, remote) && tb->OpenOutputChannel(bs, db.directory.routes.front().key.remote_locator), "real output resources");
    auto drain = [&] { for (unsigned i = 0; i < 32; ++i) { a.PumpSend(); b.PumpSend(); public_socket_fake::PumpOne(); a.PumpReceive(); b.PumpReceive(); } };
    T2Check(!DomainSend(*as.front(), remote), "Await send accepted");
    const auto peer_remote = db.directory.routes.front().key.remote_locator;
    T2Check(!DomainSend(*bs.front(), peer_remote), "default peer bypassed Await");
    // This raw component driver explicitly selects domain151. Actual stock
    // Participant cases obtain their domain only from stock's own callback.
    eprosima::fastdds::rtps::LocatorList peer_peers;
    fastdds_dsoftbus::binding::PortParameters peer_ports;
    Locator peer_initial = peer_remote;
    T2Check(tb->configureInitialPeerLocator(peer_initial, peer_ports, 151, peer_peers), "controlled raw peer domain callback");
    T2Check(DomainSend(*bs.front(), peer_remote), "confirmed raw peer send"); drain();
    const auto scoped_before_assert = scoped->domain_snapshot();
    std::cout << "SCOPED_AWAIT_DIAGNOSTIC ra_calls=" << ra.calls << " guard_denied_rx=" << scoped_before_assert.denied[2]
              << " guard_state=" << static_cast<unsigned>(scoped_before_assert.state)
              << " receive_active_applications=" << phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications << std::endl;
    T2Check(ra.calls == 0 && scoped_before_assert.state == ViewDomainState::Await &&
            phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.active_applications == 0, "Await proxy attached or delivered RTPS");
    eprosima::fastdds::rtps::SendResourceList fresh_as;
    T2Check(ta->OpenOutputChannel(fresh_as, remote) && !fresh_as.empty(), "Await fresh real sender resource");
    const auto await_native = public_socket_fake::Observe();
    T2Check(!DomainSend(*as.front(), remote) && !DomainSend(*fresh_as.front(), remote), "Await cached/fresh sender admitted bytes");
    T2Check(scoped->domain_snapshot().denied[0] > 0 && a.PumpSend().outcome == NativeSendOutcome::NotEntered, "Await sender entered provider work");
    const auto await_after = public_socket_fake::Observe();
    T2Check(await_after.sends[0] == await_native.sends[0] && await_after.forwards[0] == await_native.forwards[0], "Await sender entered native provider");
    eprosima::fastdds::rtps::LocatorList peers;
    fastdds_dsoftbus::binding::PortParameters ports;
    Locator initial = remote;
    T2Check(ta->configureInitialPeerLocator(initial, ports, 151, peers), "matching domain callback");
    T2Check(DomainSend(*bs.front(), db.directory.routes.front().key.remote_locator, 71), "positive receive send"); drain();
    T2Check(ra.calls == 1 && ra.marker == 71, "Ready proxy positive missing");
    ra.hold = true; ra.entered = false;
    T2Check(DomainSend(*bs.front(), db.directory.routes.front().key.remote_locator, 72), "held lease send");
    b.PumpSend(); public_socket_fake::PumpOne();
    auto delivering = std::async(std::launch::async, [&] { return a.PumpReceive(); });
    { std::unique_lock<std::mutex> lock(ra.gate); T2Check(ra.changed.wait_for(lock, 2s, [&] { return ra.entered; }), "real receiver lease not entered"); }
    auto closing = std::async(std::launch::async, [&] { ta->shutdown(); });
    T2Check(closing.wait_for(50ms) == std::future_status::timeout, "close did not wait for original receiver lease");
    { std::lock_guard<std::mutex> lock(ra.gate); ra.release = true; ra.changed.notify_all(); }
    T2Check(closing.wait_for(2s) == std::future_status::ready && delivering.wait_for(2s) == std::future_status::ready, "close drain watchdog");
    closing.get(); delivering.get();
    T2Check(scoped->domain_snapshot().state == ViewDomainState::Closed && !DomainSend(*as.front(), remote), "old cached sender reopened");
    ta.reset();
    ta.reset(ad->create_transport()); scoped = dynamic_cast<DSoftBusTransport*>(ta.get()); T2Check(scoped, "new view");
    T2Check(scoped->domain_snapshot().state == ViewDomainState::Await, "new view retained old Ready/Closed state");
    T2Check(ta->init() && ta->OpenInputChannel(local, &replacement, 8192), "replacement input");
    peers.clear(); T2Check(ta->configureInitialPeerLocator(initial, ports, 151, peers), "fresh domain confirmation");
    T2Check(!DomainSend(*as.front(), remote), "old sender followed replacement");
    T2Check(DomainSend(*bs.front(), db.directory.routes.front().key.remote_locator, 73), "replacement send"); drain();
    T2Check(replacement.calls == 1 && replacement.marker == 73 && ra.calls == 2, "old receiver lease retargeted");
    peers.clear(); T2Check(!ta->configureInitialPeerLocator(initial, ports, 152, peers), "mismatching domain accepted");
    T2Check(!ta->configureInitialPeerLocator(initial, ports, 151, peers), "Refused became Ready");
    T2Check(DomainSend(*bs.front(), db.directory.routes.front().key.remote_locator, 74), "refusal RX control send"); drain();
    T2Check(replacement.calls == 1 && scoped->domain_snapshot().denied[2] > 0, "Refused proxy delivered");
    fresh_as.clear();
    ta.reset(); tb.reset(); as.clear(); bs.clear(); T2Check(a.Close() && b.Close(), "anchor close");
    const auto final = public_socket_fake::Observe(); T2Check(final.live == 0 && final.queued == 0 && final.bytes == 0 && final.invalid == 0, "final Fake residue");
    std::cout << "STABLE_RX Await=DENIED Ready=DELIVERED Refused=DENIED OLD_LEASE_DRAIN=PASS REPLACEMENT_GUARD=FRESH\n";
}
void DomainPositive() {
    T2LifecycleFixture f;
    for (unsigned side = 0; side < 2; ++side) { f.descriptors[side] = f.runtimes[side]->MakeStockDescriptor(T2Descriptor(side, 0)); T2Check(bool(f.descriptors[side]), "stock descriptor"); }
    f.Pair(0, true); f.Traffic(0, 1); f.Remove(0, 0); f.Create(0, 0, 151, true); f.Traffic(0, 2); f.Finish();
}
void DomainDefaultCreate(T2LifecycleFixture& f, unsigned domain) {
    auto& app = f.endpoints[0][0]; T2Check(!app.participant, "default view replacement");
    app.received = std::make_unique<T2Reader>(); app.sent = std::make_unique<T2Writer>();
    app.participant = dds::DomainParticipantFactory::get_instance()->create_participant(domain, dds::PARTICIPANT_QOS_DEFAULT);
    std::cout << "ORIGINAL_DEFAULT_FACTORY domain=" << domain << " returned=" << static_cast<const void*>(app.participant) << '\n';
    T2Check(app.participant, "original public default Factory result");
    T2Check(f.type.register_type(app.participant) == 0, "default type registration");
    auto* topic = app.participant->create_topic("ViewDomainTopic", f.type.get_type_name(), dds::TOPIC_QOS_DEFAULT);
    auto* pub = app.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT); auto* sub = app.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
    T2Check(topic && pub && sub, "default entities");
    auto wq = dds::DATAWRITER_QOS_DEFAULT; auto rq = dds::DATAREADER_QOS_DEFAULT;
    wq.reliability().kind = rq.reliability().kind = dds::RELIABLE_RELIABILITY_QOS; wq.data_sharing().off(); rq.data_sharing().off();
    app.writer = pub->create_datawriter(topic, wq, app.sent.get()); app.reader = sub->create_datareader(topic, rq, app.received.get());
    T2Check(app.writer && app.reader, "default endpoints");
}
void DomainBootstrap() {
    T2LifecycleFixture f;
    auto& factory = *dds::DomainParticipantFactory::get_instance(); const auto previous = factory.get_default_participant_qos();
    auto base = previous; base.properties().properties().emplace_back("fastdds.ignore_local_endpoints", "true");
    T2Check(factory.set_default_participant_qos(base) == 0, "public diagnostic configuration");
    fastdds_dsoftbus::stock_transport::StockTransportBootstrap bootstrap(factory);
    T2Check(fastdds_dsoftbus::stock_transport::InstallStockTransport(bootstrap, *f.runtimes[0], T2Descriptor(0, 0)) == fastdds_dsoftbus::stock_transport::BootstrapResult::Ok,
            "explicit stock bootstrap helper");
    f.descriptors[1] = f.runtimes[1]->MakeStockDescriptor(T2Descriptor(1, 0)); T2Check(bool(f.descriptors[1]), "remote stock descriptor");
    std::exception_ptr errors[2];
    std::thread a([&] { try { DomainDefaultCreate(f, 151); } catch (...) { errors[0] = std::current_exception(); } });
    std::thread b([&] { try { f.Create(1, 0, 151, true); } catch (...) { errors[1] = std::current_exception(); } });
    a.join(); b.join(); for (auto e : errors) if (e) std::rethrow_exception(e);
    f.Traffic(0, 1); f.Remove(0, 0); DomainDefaultCreate(f, 152); f.endpoints[0][0].received->WaitNone();
    f.Remove(0, 0); DomainDefaultCreate(f, 151); f.Traffic(0, 3); f.Remove(0, 0); f.Remove(1, 0);
    T2Check(bootstrap.Release() == fastdds_dsoftbus::stock_transport::BootstrapResult::Ok, "original public Bootstrap release");
    T2Check(factory.set_default_participant_qos(previous) == 0, "restore public diagnostic configuration"); f.Finish();
    std::cout << "DEFAULT_QOS_SELECTED_DOMAIN_POSITIVE=PASS MISMATCH_REFUSED=PASS AUTOMATIC_SECOND_DOMAIN=NOT_PROVIDED\n";
}
void DomainUnconfirmed(bool unsupported) {
    T2LifecycleFixture f;
    struct PeerSendWitness {std::mutex gate;std::condition_variable changed;bool seen=false;};
    const auto witness=std::make_shared<PeerSendWitness>();
    // Copies held by an already-entered observer retain their own state even
    // during failure unwinding. Clearing the global callback is not a drain.
    struct ResetPeerSendObserver {~ResetPeerSendObserver(){public_socket_fake::SetSendObserver({});}} reset_observer;
    public_socket_fake::SetSendObserver([witness](int side,int){if(side==1){
        std::lock_guard<std::mutex> lock(witness->gate);witness->seen=true;witness->changed.notify_all();}});

    auto& factory = *dds::DomainParticipantFactory::get_instance(); const auto previous = factory.get_default_participant_qos();
    auto base = previous; base.properties().properties().emplace_back("fastdds.ignore_local_endpoints", "true");
    T2Check(factory.set_default_participant_qos(base) == 0, "public unconfirmed configuration");
    fastdds_dsoftbus::stock_transport::StockTransportBootstrap bootstrap(factory);
    T2Check(fastdds_dsoftbus::stock_transport::InstallStockTransport(bootstrap, *f.runtimes[0], T2Descriptor(0, 0)) == fastdds_dsoftbus::stock_transport::BootstrapResult::Ok,
            "unconfirmed scoped bootstrap");
    const auto installed = factory.get_default_participant_qos(); auto changed = installed;
    auto& peers = changed.wire_protocol().builtin.initialPeersList; peers.clear();
    if (unsupported) { Locator peer; peer.kind = LOCATOR_KIND_UDPv4; peer.port = 12000; peer.address[12] = 127; peer.address[15] = 1; peers.push_back(peer); }
    T2Check(factory.set_default_participant_qos(changed) == 0, "public missing-callback configuration");
    f.descriptors[1] = f.runtimes[1]->MakeStockDescriptor(T2Descriptor(1, 0)); T2Check(bool(f.descriptors[1]), "unconfirmed remote descriptor");
    std::exception_ptr errors[2];
    std::thread a([&] { try { DomainDefaultCreate(f, 151); } catch (...) { errors[0] = std::current_exception(); } });
    std::thread b([&] { try { f.Create(1, 0, 151, true); } catch (...) { errors[1] = std::current_exception(); } });
    a.join(); b.join(); for (auto e : errors) if (e) std::rethrow_exception(e);
    {std::unique_lock<std::mutex> lock(witness->gate);
        T2Check(witness->changed.wait_for(lock,2s,[&]{return witness->seen;}),"unconfirmed peer actual send witness");}
    const auto native = public_socket_fake::Observe();
    std::cout << "SCOPED_UNCONFIRMED_DIAGNOSTIC unsupported=" << unsupported << " native_live=" << native.live
              << " binds_a=" << native.binds[0] << " binds_b=" << native.binds[1]
              << " sockets_a=" << native.sockets[0] << " sockets_b=" << native.sockets[1]
              << " listens_a=" << native.listens[0] << " listens_b=" << native.listens[1] << std::endl;
    const bool old_eager_pair=native.live==6&&native.binds[0]==1&&native.binds[1]==1;
    const auto observed_a=f.runtimes[0]->ObserveSocket(),observed_b=f.runtimes[1]->ObserveSocket();
    const auto usage_a=phase0::RuntimeProbeAccess::ObserveRuntime(*f.runtimes[0]);
    std::cout<<"UNCONFIRMED_CONTRACT old_eager_pair="<<old_eager_pair<<" peer_actual_send=1 A_apps="<<usage_a.receive.active_applications
        <<" A_detaching="<<usage_a.receive.detaching_applications<<" A_outstanding="<<usage_a.runtime_admitted_outstanding
        <<" view_guard_live=UNKNOWN live_circuit_owner_generation=UNKNOWN"<<std::endl;
    T2Check(native.listens[0]==1&&native.listens[1]==1&&observed_a.listener_socket&&observed_b.listener_socket,"unconfirmed healthy listener controls");
    T2Check(native.sockets[0]==1&&native.binds[0]==0&&native.sends[0]==0&&!observed_a.outgoing_socket&&
        std::none_of(observed_a.domain_outgoing_sockets.begin(),observed_a.domain_outgoing_sockets.end(),[](const auto& id){return id.has_value();})&&
        !usage_a.receive.active_applications&&!usage_a.receive.detaching_applications&&!usage_a.runtime_admitted_outstanding,"unconfirmed view acquired TX or application admission");
    T2Check(native.live==4&&native.sockets[1]==2&&native.binds[1]==1&&native.sends[1]>0&&
        std::any_of(observed_b.domain_outgoing_sockets.begin(),observed_b.domain_outgoing_sockets.end(),[](const auto& id){return id.has_value();})&&
        std::any_of(observed_a.domain_accepted_sockets.begin(),observed_a.domain_accepted_sockets.end(),[](const auto& id){return id.has_value();}),"unconfirmed confirmed-peer native control unavailable");
    f.endpoints[0][0].received->WaitNone();
    f.Remove(0, 0); f.Remove(1, 0);
    T2Check(factory.set_default_participant_qos(installed) == 0, "restore installed public defaults");
    T2Check(bootstrap.Release() == fastdds_dsoftbus::stock_transport::BootstrapResult::Ok, "unconfirmed public release");
    T2Check(factory.set_default_participant_qos(previous) == 0, "restore unconfirmed fixture defaults"); f.Finish();
    std::cout << "PUBLIC_FACTORY_VALID=1 PEERS=" << (unsupported ? "UNSUPPORTED" : "EMPTY")
              << " LISTENER_AND_CONFIRMED_PEER_NATIVE_CONTROL=1 UNCONFIRMED_TX_DENIED=1 NO_APPLICATION_SAMPLES=1 MISSING_CALLBACK_CONTRACT=AWAIT_NOT_READY VIEW_GUARD_LIVE=UNKNOWN NO_MISSING_CALLBACK_TERMINAL=1\n";
}
void DomainInputReopen() {
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(), true);
    public_socket_fake::SetListenerNames({T2Listener(0, 0), T2Listener(1, 0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0, 0).identity.service_name, T2Configuration(1, 0).identity.service_name});
    experimental::Runtime a(T2Configuration(0, 0)), b(T2Configuration(1, 0));
    std::mutex gate; std::condition_variable changed; unsigned arrived = 0;
    auto ready = [&] { std::unique_lock<std::mutex> lock(gate); ++arrived; changed.notify_all();
        return changed.wait_for(lock, 2s, [&] { return arrived == 2; }); };
    T2Check(a.SetPreConnectGate(ready) && b.SetPreConnectGate(ready), "reopen listener readiness barrier");
    const auto da = T2Descriptor(0, 0), db = T2Descriptor(1, 0);
    auto ad = a.MakeStockDescriptor(da), bd = b.MakeStockDescriptor(db); T2Check(ad && bd, "reopen NativeBound descriptors");
    // Receivers outlive owned transports even if a reproduction assertion throws.
    DomainReceiver original, replacement, peer;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(ad->create_transport()), tb(bd->create_transport());
    auto* view = dynamic_cast<DSoftBusTransport*>(ta.get()); T2Check(view && tb, "reopen original owned Transport");
    std::exception_ptr errors[2];
    std::thread other([&] { try { T2Check(tb->init() && tb->OpenInputChannel(db.directory.locals.front().local_locator, &peer, 8192), "reopen peer input"); }
        catch (...) { errors[1] = std::current_exception(); } });
    try { T2Check(ta->init() && ta->OpenInputChannel(da.directory.locals.front().local_locator, &original, 8192), "reopen original input"); }
    catch (...) { errors[0] = std::current_exception(); }
    other.join(); for (auto e : errors) if (e) std::rethrow_exception(e);
    const auto local = da.directory.locals.front().local_locator;
    auto ar = da.directory.routes.front().key.remote_locator, br = db.directory.routes.front().key.remote_locator;
    fastdds_dsoftbus::binding::PortParameters ports; eprosima::fastdds::rtps::LocatorList peers;
    // Explicit raw public Transport driver selects151. This is not a stock
    // Factory callback or evidence about ROS_DOMAIN_ID installation.
    T2Check(ta->configureInitialPeerLocator(ar, ports, 151, peers) && tb->configureInitialPeerLocator(br, ports, 151, peers), "reopen actual151 driver confirmation");
    eprosima::fastdds::rtps::SendResourceList senders; T2Check(tb->OpenOutputChannel(senders, br) && !senders.empty(), "reopen actual peer sender");
    T2Check(DomainSend(*senders.front(), br, 79) && b.PumpSend().actual_return_code == 0 && public_socket_fake::PumpOne(), "reopen initial native marker");
    a.PumpReceive(); T2Check(original.calls == 1 && original.marker == 79, "reopen initial exact positive marker");
    const auto native_before = public_socket_fake::Observe(); const unsigned old_calls = original.calls;
    T2Check(DomainSend(*senders.front(), br, 80) && b.PumpSend().actual_return_code == 0 && public_socket_fake::PumpOne(), "reopen queued original lease marker");
    // Leave marker80 in the original application's admitted work before close.
    const bool close_ok = ta->CloseInputChannel(local);
    const bool open_ok = ta->OpenInputChannel(local, &replacement, 8192);
    peers.clear(); const bool confirmed = ta->configureInitialPeerLocator(ar, ports, 151, peers);
    a.PumpReceive(); // Retired admitted work may not target the replacement.
    const bool submitted = DomainSend(*senders.front(), br, 81);
    const auto sent = b.PumpSend(); const bool forwarded = public_socket_fake::PumpOne();
    const auto received = a.PumpReceive();
    const auto state = view->domain_snapshot(); const auto runtime = phase0::RuntimeProbeAccess::ObserveRuntime(a);
    const auto native_after = public_socket_fake::Observe();
    std::cout << "SAME_FACADE_INPUT_REOPEN_DIAGNOSTIC close=" << close_ok << " open=" << open_ok << " actual151_callback=" << confirmed
        << " guard_state=" << static_cast<unsigned>(state.state) << " old_calls_before=" << old_calls << " old_calls_after=" << original.calls
        << " replacement_calls=" << replacement.calls << " replacement_marker=" << unsigned(replacement.marker)
        << " active_applications=" << runtime.receive.active_applications << " submitted=" << submitted
        << " native_return_zero=" << (sent.actual_return_code == 0) << " forwarded=" << forwarded << " receive_status=" << static_cast<int>(received)
        << " sockets_a_before=" << native_before.sockets[0] << " sockets_a_after=" << native_after.sockets[0]
        << " sockets_b_before=" << native_before.sockets[1] << " sockets_b_after=" << native_after.sockets[1]
        << " binds_a_before=" << native_before.binds[0] << " binds_a_after=" << native_after.binds[0]
        << " binds_b_before=" << native_before.binds[1] << " binds_b_after=" << native_after.binds[1] << std::endl;
    T2Check(close_ok && open_ok && confirmed && state.state == ViewDomainState::Ready, "same-facade public Close/Open/confirmation failed");
    T2Check(submitted && sent.actual_return_code == 0 && forwarded, "same-facade fresh marker did not traverse native peer");
    T2Check(replacement.calls == 1 && replacement.marker == 81 && original.calls == old_calls, "same-facade reopened input missing fresh exact marker or retargeted old lease");
    T2Check(runtime.receive.active_applications == 1, "same-facade successful reopen has no unique application lease");
    for (unsigned side = 0; side < 2; ++side) T2Check(native_after.sockets[side] == native_before.sockets[side] && native_after.binds[side] == native_before.binds[side], "same-facade reopen rebuilt native resources");
    ta.reset(); tb.reset(); senders.clear(); T2Check(a.Close() && b.Close(), "same-facade final Runtime close");
    const auto end = public_socket_fake::Observe(); T2Check(!end.live && !end.queued && !end.bytes && !end.invalid, "same-facade reopen final Fake residue");
    std::cout << "SAME_FACADE_INPUT_REOPEN marker79=ORIGINAL marker80=RETIRED marker81=REPLACEMENT_ONCE native_incarnation=UNCHANGED HOST_FAKE_ONLY DEVICE_TESTED=0\n";
}
void DomainInputEarlyCycles() {
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(), true);
    public_socket_fake::SetListenerNames({T2Listener(0, 0), T2Listener(1, 0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0, 0).identity.service_name, T2Configuration(1, 0).identity.service_name});
    experimental::Runtime a(T2Configuration(0, 0)), b(T2Configuration(1, 0));
    const auto da=T2Descriptor(0,0),db=T2Descriptor(1,0);auto ad=a.MakeDescriptor(da),bd=b.MakeDescriptor(db);
    T2Check(ad&&bd,"early generic descriptors");DomainReceiver receivers[2],listener_only,peer;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(ad->create_transport()),anchor(ad->create_transport()),tb(bd->create_transport());
    auto* view=dynamic_cast<DSoftBusTransport*>(ta.get());T2Check(view&&anchor&&tb&&ta->init()&&anchor->init()&&tb->init(),"early two-view admission");
    const auto local=da.directory.locals.front().local_locator;auto ar=da.directory.routes.front().key.remote_locator,br=db.directory.routes.front().key.remote_locator;
    // This sibling only establishes the physical root listener. Target ta has
    // never requested an input, and neither A view has an application lease.
    T2Check(anchor->OpenInputChannel(local,&listener_only,8192)&&tb->OpenInputChannel(db.directory.locals.front().local_locator,&peer,8192),"early physical listeners");
    fastdds_dsoftbus::binding::PortParameters ports;eprosima::fastdds::rtps::LocatorList peers;
    T2Check(tb->configureInitialPeerLocator(br,ports,151,peers),"early peer actual151");
    eprosima::fastdds::rtps::SendResourceList as,bs;T2Check(ta->OpenOutputChannel(as,ar)&&tb->OpenOutputChannel(bs,br),"early cached real senders");
    T2Check(!DomainSend(*as.front(),ar,89),"early Await cached sender bypass");
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers),"actual151 callback before target Open");
    const auto before_open=phase0::RuntimeProbeAccess::ObserveRuntime(a);
    T2Check(view->domain_snapshot().state==ViewDomainState::Ready&&before_open.receive.active_applications==0,"early callback manufactured RX lease");
    T2Check(DomainSend(*as.front(),ar,90)&&a.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"cached TX before target input did not enter native");
    b.PumpReceive();T2Check(peer.calls==1&&peer.marker==90,"cached TX before target input missing exact peer marker");
    T2Check(anchor->CloseInputChannel(local),"early sibling input close");anchor.reset();
    T2Check(ta->OpenInputChannel(local,&receivers[0],8192),"first requested input after actual callback");
    T2Check(DomainSend(*bs.front(),br,92)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"first input native marker");
    a.PumpReceive();T2Check(receivers[0].calls==1&&receivers[0].marker==92,"first input after callback missing lease");
    const auto base=public_socket_fake::Observe();const auto ids_a=a.ObserveSocket().family_ids_retained,ids_b=b.ObserveSocket().family_ids_retained;
    for(unsigned round=0;round<12;++round){
        const unsigned old=round%2,next=1-old;const auto old_calls=receivers[old].calls,next_calls=receivers[next].calls;
        const auto queued=std::uint8_t(100+2*round),fresh=std::uint8_t(101+2*round);
        T2Check(DomainSend(*bs.front(),br,queued)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"cycle old admitted marker");
        const bool closed=ta->CloseInputChannel(local),opened=ta->OpenInputChannel(local,&receivers[next],8192);
        a.PumpReceive();const bool submitted=DomainSend(*bs.front(),br,fresh);const auto sent=b.PumpSend();const bool forwarded=public_socket_fake::PumpOne();a.PumpReceive();
        const auto state=phase0::RuntimeProbeAccess::ObserveRuntime(a);const auto native=public_socket_fake::Observe();
        const auto now_ids_a=a.ObserveSocket().family_ids_retained,now_ids_b=b.ObserveSocket().family_ids_retained;
        std::cout<<"INPUT_INCARNATION_CYCLE round="<<round+1<<" close="<<closed<<" open="<<opened<<" old_calls="<<receivers[old].calls<<" new_calls="<<receivers[next].calls
            <<" marker="<<unsigned(receivers[next].marker)<<" app="<<state.receive.active_applications<<" owner_controls="<<state.view_controls<<" ids_a="<<now_ids_a<<" ids_b="<<now_ids_b<<std::endl;
        T2Check(closed&&opened&&submitted&&sent.actual_return_code==0&&forwarded,"cycle public input/native operation failed");
        T2Check(receivers[old].calls==old_calls&&receivers[next].calls==next_calls+1&&receivers[next].marker==fresh,"cycle retargeted old lease or lost exact fresh marker");
        T2Check(state.receive.active_applications==1&&state.receive.detaching_applications==0,"cycle application ownership");
        T2Check(now_ids_a==ids_a&&now_ids_b==ids_b,"input cycle consumed native retained ID");
        for(unsigned side=0;side<2;++side)T2Check(native.sockets[side]==base.sockets[side]&&native.binds[side]==base.binds[side],"input cycle rebuilt native circuit");
    }
    ta.reset();tb.reset();as.clear();bs.clear();T2Check(a.Close()&&b.Close(),"early/cycles final close");const auto end=public_socket_fake::Observe();
    T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"early/cycles final residue");
    std::cout<<"INPUT_EARLY_CYCLES target_before_Open_cached_TX_peer_marker90=1 root_sibling_listener=1 cycles=12 SDK_drain=UNKNOWN DEVICE_TESTED=0\n";
}

// Own every worker until after the publication gate is released, including
// exception paths. A wedged production worker is a process failure: never tear
// down receivers/proxies while it is still running.
struct PublicationWorkers {
    std::mutex gate;
    std::condition_variable changed;
    bool entered=false,released=false;
    unsigned hits=0;
    std::future<bool> binding,closing;
    void Release() noexcept {std::lock_guard<std::mutex> lock(gate);released=true;changed.notify_all();}
    static void Drain(std::future<bool>& worker) noexcept {
        if(!worker.valid())return;
        if(worker.wait_for(3s)!=std::future_status::ready){
            std::cerr<<"P1_CLEANUP worker watchdog; no live-object teardown"<<std::endl;std::abort();
        }
        try{worker.get();}catch(...){std::cerr<<"P1_CLEANUP worker exception during unwinding"<<std::endl;}
    }
    ~PublicationWorkers(){Release();Drain(binding);Drain(closing);}
};
void DomainInputPublication() {
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
    experimental::Runtime a(T2Configuration(0,0)),b(T2Configuration(1,0));
    std::mutex startup;std::condition_variable startup_changed;unsigned arrived=0;
    auto ready=[&]{std::unique_lock<std::mutex> lock(startup);++arrived;startup_changed.notify_all();
        return startup_changed.wait_for(lock,2s,[&]{return arrived==2;});};
    T2Check(a.SetPreConnectGate(ready)&&b.SetPreConnectGate(ready),"publication listener gate");
    const auto da=T2Descriptor(0,0),db=T2Descriptor(1,0);auto ad=a.MakeDescriptor(da),bd=b.MakeDescriptor(db);
    T2Check(ad&&bd,"publication generic descriptors");
    DomainReceiver original,replacement,peer; // Outlive transports and all workers.
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(ad->create_transport()),tb(bd->create_transport());
    T2Check(ta&&tb,"publication owned transports");
    auto* view=dynamic_cast<DSoftBusTransport*>(ta.get());T2Check(view,"publication concrete target");
    const auto local=da.directory.locals.front().local_locator;
    auto ar=da.directory.routes.front().key.remote_locator,br=db.directory.routes.front().key.remote_locator;
    std::exception_ptr errors[2];
    std::thread other([&]{try{T2Check(tb->init()&&tb->OpenInputChannel(db.directory.locals.front().local_locator,&peer,8192),"publication peer input");}
        catch(...){errors[1]=std::current_exception();}});
    try{T2Check(ta->init()&&ta->OpenInputChannel(local,&original,8192),"publication target input");}catch(...){errors[0]=std::current_exception();}
    other.join();for(auto error:errors)if(error)std::rethrow_exception(error);
    fastdds_dsoftbus::binding::PortParameters ports;eprosima::fastdds::rtps::LocatorList peers;
    T2Check(tb->configureInitialPeerLocator(br,ports,151,peers),"publication peer actual151");
    eprosima::fastdds::rtps::SendResourceList as,bs;
    T2Check(ta->OpenOutputChannel(as,ar)&&tb->OpenOutputChannel(bs,br),"publication cached senders");
    const auto before=phase0::RuntimeProbeAccess::ObserveRuntime(a);
    T2Check(ta->IsInputChannelOpen(local)&&view->domain_snapshot().state==ViewDomainState::Await&&
        !before.receive.active_applications,"publication requires open Await target without application");
    PublicationWorkers workers; // Declared last: release/get before every resource above.
    workers.binding=std::async(std::launch::async,[&]{
        phase0::ArmedDomainInputPublicationGate armed([&]{
            std::unique_lock<std::mutex> lock(workers.gate);++workers.hits;workers.entered=true;workers.changed.notify_all();
            T2Check(workers.changed.wait_for(lock,4s,[&]{return workers.released;}),"publication release watchdog");
        });
        auto remote=ar;fastdds_dsoftbus::binding::PortParameters p;eprosima::fastdds::rtps::LocatorList out;
        return ta->configureInitialPeerLocator(remote,p,151,out);
    });
    {std::unique_lock<std::mutex> lock(workers.gate);
        T2Check(workers.changed.wait_for(lock,2s,[&]{return workers.entered;}),"publication Attach-return gate not reached");}
    const auto held=phase0::RuntimeProbeAccess::ObserveRuntime(a);
    T2Check(held.receive.active_applications==1&&held.receive.detaching_applications==0&&
        view->domain_snapshot().state==ViewDomainState::Await,"publication did not hold real unpublished lease");
    workers.closing=std::async(std::launch::async,[&]{return ta->CloseInputChannel(local);});
    const auto deadline=std::chrono::steady_clock::now()+2s;
    bool public_close=false;
    while(!(public_close=!ta->IsInputChannelOpen(local))&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    // This observes only committed_open.exchange(false), not a private mutex wait.
    const bool close_finished=workers.closing.wait_for(250ms)==std::future_status::ready;
    const auto during=phase0::RuntimeProbeAccess::ObserveRuntime(a);
    const auto native_at_gate=public_socket_fake::Observe();
    std::cout<<"P1_HELD gate_hits="<<workers.hits<<" active="<<held.receive.active_applications
        <<" public_close_atomic="<<public_close<<" close_finished="<<close_finished
        <<" live_lease_during_close="<<during.receive.active_applications<<" controls="<<during.view_controls<<std::endl;
    workers.Release();
    T2Check(workers.binding.wait_for(3s)==std::future_status::ready&&workers.closing.wait_for(3s)==std::future_status::ready,
        "publication worker completion watchdog");
    const bool bound=workers.binding.get(),closed=workers.closing.get();
    const auto after=phase0::RuntimeProbeAccess::ObserveRuntime(a);
    std::cout<<"P1_RELEASE callback="<<bound<<" close="<<closed<<" active="<<after.receive.active_applications
        <<" detaching="<<after.receive.detaching_applications<<" old_calls="<<original.calls<<std::endl;
    T2Check(workers.hits==1&&public_close,"publication precise gate/public close not observed");
    T2Check(closed&&!ta->IsInputChannelOpen(local)&&!after.receive.active_applications&&
        !after.receive.detaching_applications&&!after.view_controls&&!original.calls,"publication close left stale lease/control");
    if(close_finished){
        // Same positive oracle, safely fail the partial old-synchronization
        // overlay only after releasing/getting workers. Never pump stale RX.
        ta.reset();tb.reset();as.clear();bs.clear();
        T2Check(a.Close()&&b.Close(),"publication early-close cleanup");
        const auto end=public_socket_fake::Observe();
        T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"publication early-close residue");
        std::cerr<<"P1_ORACLE early_close="<<close_finished<<" live_unpublished_lease="<<during.receive.active_applications
            <<" cleanup_complete=1 stale_RX_pumped=0"<<std::endl;
        T2Check(false,"publication close returned before unpublished lease retired");
    }
    // Binding may legitimately be cancelled if close wins the transaction.
    // A refused guard is sticky: recover via a fresh view rather than demand
    // callback success or pretend that its old guard became Ready again.
    if(!bound){
        T2Check(view->domain_snapshot().state==ViewDomainState::Refused,"publication cancellation guard");
        ta.reset();ta.reset(ad->create_transport());T2Check(ta&&ta->init(),"publication cancelled view recovery");
    }else T2Check(view->domain_snapshot().state==ViewDomainState::Ready,"publication successful binding guard");
    T2Check(DomainSend(*bs.front(),br,160)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"publication closed marker send");
    a.PumpReceive();T2Check(!original.calls&&!replacement.calls,"publication closed input delivered");
    const auto first_tx=public_socket_fake::Observe();
    std::cout<<"P1_FIRST_TX gate_Socket_A="<<native_at_gate.sockets[0]<<" gate_Socket_B="<<native_at_gate.sockets[1]
        <<" gate_Bind_A="<<native_at_gate.binds[0]<<" gate_Bind_B="<<native_at_gate.binds[1]
        <<" after160_Socket_A="<<first_tx.sockets[0]<<" after160_Socket_B="<<first_tx.sockets[1]
        <<" after160_Bind_A="<<first_tx.binds[0]<<" after160_Bind_B="<<first_tx.binds[1]<<std::endl;
    // Existing marker160 is the first real TX. Establish this prerequisite
    // explicitly rather than count its one creation as an input-reopen effect.
    T2Check(std::uint64_t(first_tx.sockets[1])==std::uint64_t(native_at_gate.sockets[1])+1&&
        std::uint64_t(first_tx.binds[1])==std::uint64_t(native_at_gate.binds[1])+1&&
        first_tx.sockets[0]==native_at_gate.sockets[0]&&first_tx.binds[0]==native_at_gate.binds[0],"publication first TX creation boundary");
    const auto native=first_tx;
    const auto native_a=a.ObserveSocket(),native_b=b.ObserveSocket();
    const auto observed=[&](const auto& x,const auto& y){return x.listener_socket==y.listener_socket&&
        x.outgoing_socket==y.outgoing_socket&&x.accepted_socket==y.accepted_socket&&
        x.domain_outgoing_sockets==y.domain_outgoing_sockets&&x.domain_accepted_sockets==y.domain_accepted_sockets;};
    T2Check(native_a.listener_socket&&native_b.listener_socket&&
        std::any_of(native_a.domain_accepted_sockets.begin(),native_a.domain_accepted_sockets.end(),[](const auto& x){return x.has_value();})&&
        std::any_of(native_b.domain_outgoing_sockets.begin(),native_b.domain_outgoing_sockets.end(),[](const auto& x){return x.has_value();}),"publication observable socket prerequisites");

    T2Check(ta->OpenInputChannel(local,&replacement,8192),"publication safe reopen");
    if(!bound){auto remote=ar;peers.clear();T2Check(ta->configureInitialPeerLocator(remote,ports,151,peers),"publication fresh view actual151");}
    const auto reopened_native=public_socket_fake::Observe();
    for(unsigned side=0;side<2;++side)T2Check(native.sockets[side]==reopened_native.sockets[side]&&
        native.binds[side]==reopened_native.binds[side],"publication Open/DID changed native counters");
    T2Check(observed(native_a,a.ObserveSocket())&&observed(native_b,b.ObserveSocket()),"publication Open/DID changed observed sockets");
    T2Check(DomainSend(*bs.front(),br,161)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"publication reopened marker send");
    a.PumpReceive();const auto recovered=phase0::RuntimeProbeAccess::ObserveRuntime(a);const auto recovered_native=public_socket_fake::Observe();
    T2Check(!original.calls&&replacement.calls==1&&replacement.marker==161&&recovered.receive.active_applications==1&&
        !recovered.receive.detaching_applications,"publication receiver isolation/reopen lease");
    for(unsigned side=0;side<2;++side)T2Check(native.sockets[side]==recovered_native.sockets[side]&&
        native.binds[side]==recovered_native.binds[side],"publication input reopen recreated native circuit");
    T2Check(observed(native_a,a.ObserveSocket())&&observed(native_b,b.ObserveSocket()),"publication marker161 changed observed sockets");
    ta.reset();tb.reset();as.clear();bs.clear();T2Check(a.Close()&&b.Close(),"publication final close");
    const auto end=public_socket_fake::Observe();T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"publication final residue");
    std::cout<<"P1_RECOVERY closed_marker160_dropped=1 fresh_marker161_once=1 old_receiver_calls=0 native_counter_recreation=0 observed_socket_ids_stable=1 live_circuit_owner_identity=UNKNOWN live_owner_generation=UNKNOWN HOST_FAKE_ONLY=1 DEVICE_TESTED=0\n";
}

struct DomainReentryReceiver final:eprosima::fastdds::rtps::TransportReceiverInterface {
    std::function<void(const std::uint8_t*,std::uint32_t)> body;
    void OnDataReceived(const std::uint8_t* data,std::uint32_t size,const Locator&,const Locator&)override{body(data,size);}
};
void DomainInputReentry() {
    public_socket_fake::Configure(std::make_shared<host_dds::Trace>(),true);
    public_socket_fake::SetListenerNames({T2Listener(0,0),T2Listener(1,0)});
    public_socket_fake::SetInitiatorNames({T2Configuration(0,0).identity.service_name,T2Configuration(1,0).identity.service_name});
    experimental::Runtime a(T2Configuration(0,0)),b(T2Configuration(1,0));std::mutex gate;std::condition_variable changed;unsigned arrived=0;
    auto ready=[&]{std::unique_lock<std::mutex> lock(gate);++arrived;changed.notify_all();return changed.wait_for(lock,2s,[&]{return arrived==2;});};
    T2Check(a.SetPreConnectGate(ready)&&b.SetPreConnectGate(ready),"reentry listener gate");
    const auto da=T2Descriptor(0,0),db=T2Descriptor(1,0);auto ad=a.MakeStockDescriptor(da),bd=b.MakeStockDescriptor(db);
    T2Check(ad&&bd,"reentry NativeBound descriptors");DomainReentryReceiver receiver;DomainReceiver peer;
    std::unique_ptr<eprosima::fastdds::rtps::TransportInterface> ta(ad->create_transport()),tb(bd->create_transport());T2Check(ta&&tb,"reentry owned Transports");
    const auto local=da.directory.locals.front().local_locator;auto ar=da.directory.routes.front().key.remote_locator,br=db.directory.routes.front().key.remote_locator;
    bool entered=false,released=false,callback_close_refused=false,callback_confirmed=false;
    receiver.body=[&](const std::uint8_t* data,std::uint32_t size){
        T2Check(size==4&&data[0]==147&&data[1]==19&&data[2]==83&&data[3]==7,"reentry exact marker");
        {std::unique_lock<std::mutex> lock(gate);entered=true;changed.notify_all();T2Check(changed.wait_for(lock,2s,[&]{return released;}),"reentry receiver release bound");}
        callback_close_refused=!ta->CloseInputChannel(local);fastdds_dsoftbus::binding::PortParameters p;eprosima::fastdds::rtps::LocatorList out;auto remote=ar;
        callback_confirmed=ta->configureInitialPeerLocator(remote,p,151,out);
    };
    std::exception_ptr errors[2];std::thread other([&]{try{T2Check(tb->init()&&tb->OpenInputChannel(db.directory.locals.front().local_locator,&peer,8192),"reentry peer input");}catch(...){errors[1]=std::current_exception();}});
    try{T2Check(ta->init()&&ta->OpenInputChannel(local,&receiver,8192),"reentry input");}catch(...){errors[0]=std::current_exception();}
    other.join();for(auto e:errors)if(e)std::rethrow_exception(e);
    fastdds_dsoftbus::binding::PortParameters ports;eprosima::fastdds::rtps::LocatorList peers;
    T2Check(ta->configureInitialPeerLocator(ar,ports,151,peers)&&tb->configureInitialPeerLocator(br,ports,151,peers),"reentry actual151 confirmation");
    eprosima::fastdds::rtps::SendResourceList senders;T2Check(tb->OpenOutputChannel(senders,br)&&DomainSend(*senders.front(),br,147)&&b.PumpSend().actual_return_code==0&&public_socket_fake::PumpOne(),"reentry native marker");
    auto delivering=std::async(std::launch::async,[&]{return a.PumpReceive();});
    {std::unique_lock<std::mutex> lock(gate);T2Check(changed.wait_for(lock,2s,[&]{return entered;}),"reentry receiver entered");}
    auto closing=std::async(std::launch::async,[&]{return ta->CloseInputChannel(local);});
    const auto deadline=std::chrono::steady_clock::now()+2s;bool close_entered=false;
    while(!(close_entered=phase0::RuntimeProbeAccess::ObserveRuntime(a).receive.detaching_applications>0)&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    const bool close_waiting=closing.wait_for(0ms)==std::future_status::timeout;
    {std::lock_guard<std::mutex> lock(gate);released=true;changed.notify_all();}
    std::cout<<"INPUT_CLOSE_REENTRY_DIAGNOSTIC observed_application_detaching="<<close_entered<<" close_waiting="<<close_waiting<<std::endl;
    T2Check(close_entered&&close_waiting,"close was not observed waiting for entered receiver");
    T2Check(delivering.wait_for(2s)==std::future_status::ready&&closing.wait_for(2s)==std::future_status::ready,"input close/DID reentry bounded completion");
    delivering.get();T2Check(closing.get()&&callback_close_refused&&callback_confirmed,"receiver reentry deadlocked/refused DID or allowed callback close");
    const auto state=phase0::RuntimeProbeAccess::ObserveRuntime(a);T2Check(!state.receive.active_applications&&!state.receive.detaching_applications,"reentry close residue");
    ta.reset();tb.reset();senders.clear();T2Check(a.Close()&&b.Close(),"reentry final close");const auto end=public_socket_fake::Observe();
    T2Check(!end.live&&!end.queued&&!end.bytes&&!end.invalid,"reentry final Fake residue");
    std::cout<<"INPUT_CLOSE_REENTRY same151_callback=1 callback_close_refused=1 observed_close_entry=1 SDK_drain=UNKNOWN DEVICE_TESTED=0\n";
}
}
int main(int argc, char** argv) {
    try {
        T2LoadedDll(); T2Check(argc == 2, "one domain case required"); const std::string name = argv[1];
        if (name == "core") DomainCore(); else if (name == "transport") DomainTransport();
        else if (name == "input-reopen") DomainInputReopen();
        else if (name == "input-publication") DomainInputPublication();
        else if (name == "input-early-cycles") DomainInputEarlyCycles(); else if (name == "input-reentry") DomainInputReentry();
        else if (name == "positive") DomainPositive(); else if (name == "bootstrap") DomainBootstrap();
        else if (name == "empty-peers") DomainUnconfirmed(false); else if (name == "unsupported-peer") DomainUnconfirmed(true);
        else throw std::runtime_error("unknown domain case");
        std::cout << "PASS scoped " << name << " SOCKET_BOUNDARY=HOST_FAKE DEVICE_TESTED=0\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL scoped " << e.what() << '\n'; return 1; }
}
