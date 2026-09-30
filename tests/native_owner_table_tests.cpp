#include <fastdds_dsoftbus/dsoftbus/NativeOwnerTable.hpp>
#include "../src/NativeReceiveInternal.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>

thread_local long native_fail_after = -1;
thread_local bool native_deny_allocations = false;
thread_local std::size_t native_allocations = 0;
void* operator new(std::size_t n)
{
    ++native_allocations;
    if (native_deny_allocations) throw std::bad_alloc();
    if (native_fail_after == 0) { native_fail_after = -1; throw std::bad_alloc(); }
    if (native_fail_after > 0) --native_fail_after;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace fastdds_dsoftbus::dsoftbus;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ")+std::to_string(__LINE__)+": " #x); } while(false)
namespace fastdds_dsoftbus::dsoftbus {
class NativeOwnerTableTestAccess
{
public:
    static auto Receive(NativeOwnerTable& t, const NativeIdentityConstraint& c)
    {
        auto guard = detail::NativeReceiveBridge::Acquire(t, c);
        return detail::NativeReceiveBridge::Enter(guard);
    }
    static auto ReceiveStatus(NativeOwnerTable& t, const NativeIdentityConstraint& c)
    { auto guard = detail::NativeReceiveBridge::Acquire(t, c); return guard.status(); }
    static void MovedReceive(NativeOwnerTable& t, const NativeIdentityConstraint& c)
    {
        // Lease cleanup must occur after the guard releases the original gate.
        detail::NativeReceiveLease execution;
        {
            auto original = detail::NativeReceiveBridge::Acquire(t, c);
            auto moved = std::move(original);
            CHECK(original.status() == NativeStatus::InvalidHandle);
            CHECK(!detail::NativeReceiveBridge::Enter(original));
            execution = detail::NativeReceiveBridge::Enter(moved);
            CHECK(execution);
            CHECK(!detail::NativeReceiveBridge::Enter(moved));
        }
        CHECK(t.SnapshotUsage().receives == 1);
        execution = {};
        CHECK(t.SnapshotUsage().receives == 0);
    }
    static NativeIdentityConstraint WrongGeneration(NativeIdentityConstraint c) { ++c.generation_; return c; }
    static NativeIdentityConstraint WrongVersion(NativeIdentityConstraint c) { ++c.route_version_; return c; }
    static auto Send(NativeOwnerTable& t,const NativeOwnerLease& o,std::uint64_t v,std::uint64_t g)
    { return t.TestReserveSend(o,v,g); }
    static std::weak_ptr<void> Weak(const NativeOwnerTable& t) { return t.state_; }
    static void NextIdentity(NativeOwnerTable& t,std::uint64_t n) { t.TestSetNextIdentity(n); }
    static void CallbackCount(NativeOwnerTable& t,const NativeOwnerLease& o,std::size_t n)
    { t.TestSetCallbackCount(o,n); }
    static NativeOwnerLease Stale(NativeOwnerLease o) { ++o.incarnation_; return o; }
    static NativeStatus WrongKind(SocketCallTicket& t) { return t.token_.Enter(NativeCallKind::Bind); }
    static NativeStatus StaleOperation(SocketCallTicket& t)
    {
        const auto old=t.token_.operation_incarnation_;
        ++t.token_.operation_incarnation_;
        const auto result=t.CallEntered();
        t.token_.operation_incarnation_=old;
        return result;
    }
};
}
namespace {
static_assert(!std::is_copy_constructible_v<NativeOwnerTable>);
static_assert(!std::is_move_constructible_v<NativeOwnerTable>);
static_assert(std::is_copy_constructible_v<NativeOwnerLease>);
static_assert(std::is_copy_constructible_v<NamespaceLease>);
static_assert(!std::is_constructible_v<NativeOwnerLease,std::size_t,std::uint64_t>);
static_assert(!std::is_constructible_v<NamespaceLease,std::size_t>);
static_assert(!std::is_copy_constructible_v<SocketCallTicket>);
static_assert(!std::is_copy_constructible_v<ListenCallTicket>);
static_assert(!std::is_copy_constructible_v<BindCallTicket>);
static_assert(!std::is_copy_constructible_v<ShutdownCallTicket>);
static_assert(!std::is_copy_constructible_v<NativeCallbackLease>);
static_assert(!std::is_move_assignable_v<SocketCallTicket>);
static_assert(!std::is_move_assignable_v<ShutdownCallTicket>);
static_assert(!std::is_move_assignable_v<NativeCallbackLease>);
static_assert(std::is_nothrow_move_constructible_v<SocketCallTicket>);
static_assert(std::is_nothrow_move_constructible_v<ShutdownCallTicket>);
static_assert(!std::is_invocable_v<decltype(&BindCallTicket::Complete),BindCallTicket&,SyncListenResult>);
static_assert(!std::is_invocable_v<decltype(&SocketCallTicket::Complete),SocketCallTicket&,SyncBindResult>);

struct DenyAllocations
{
    const std::size_t start=native_allocations;
    DenyAllocations() { native_deny_allocations=true; }
    ~DenyAllocations() { native_deny_allocations=false; }
};
Locator L(std::uint8_t n)
{ EndpointId e{};e[15]=n;Locator l;CHECK(LocatorCodec::Encode(e,l));return l; }
NativeTableConfiguration Config(std::size_t routes=1,RouteDirection direction=RouteDirection::Dial)
{
    NativeTableConfiguration c;
    c.service_pkg_name="com.kaihong.mdds";
    c.directory.locals.push_back({L(2),"namespace-owned-long-name","listen-owned-long-name"});
    for(std::size_t i=0;i<routes;++i) {
        Route r;r.key={L(2),L(static_cast<std::uint8_t>(10+i))};
        const auto suffix=std::to_string(i);
        r.fields={"network-owned-long-value-"+suffix,"remote-listen-owned-"+suffix,
            "dial-owned-long-name-"+suffix,{"peer-initiator-owned-"+suffix},RecordDataType::Bytes};
        c.directory.routes.push_back(r);c.directions.push_back(direction);
    }
    return c;
}
std::unique_ptr<NativeOwnerTable> Table(const NativeTableConfiguration& c)
{ auto t=NativeOwnerTable::Build(c);CHECK(t);CHECK(*t.value);return std::move(*t.value); }
NamespaceLease Space(NativeOwnerTable& t,const NativeTableConfiguration& c,std::size_t local=0)
{ auto s=t.AcquireNamespace(c.directory.locals[local].local_locator,c);CHECK(s);CHECK(*s.value);return *s.value; }
NativeReservation Dial(NativeOwnerTable& t,const NamespaceLease& s,const NativeTableConfiguration& c,std::size_t route=0)
{
    auto r=t.ReserveDial(s,c.directory.routes[route].key);CHECK(r);
    CHECK(r.value->creation==NativeCreation::CreateRequired&&r.value->socket_call);
    return std::move(*r.value);
}
NativeOwnerLease SocketKnown(NativeOwnerTable& t,const NamespaceLease& s,const NativeTableConfiguration& c,
        std::size_t route,NativeSocketId id)
{
    auto r=Dial(t,s,c,route);CHECK(r.socket_call->CallEntered()==NativeStatus::None);
    CHECK(r.socket_call->Complete(NativeSocketResult::Id(id)).status==NativeStatus::None);
    CHECK(r.owner.Observe().phase==NativeOwnerPhase::SocketKnown);return r.owner;
}
NativeOwnerLease Ready(NativeOwnerTable& t,const NamespaceLease& s,const NativeTableConfiguration& c,
        std::size_t route,NativeSocketId id)
{
    auto o=SocketKnown(t,s,c,route,id);auto b=t.BeginBind(o);CHECK(b);
    CHECK(b.value->CallEntered()==NativeStatus::None);
    CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    CHECK(o.Observe().phase==NativeOwnerPhase::Ready);return o;
}
ObservedPeerKey Peer(const NativeTableConfiguration& c,std::size_t route=0)
{
    const auto& r=c.directory.routes[route];
    return {c.directory.locals[0].callback_namespace,r.fields.remote_network_id,
        r.fields.expected_remote_initiator_names[0],RecordDataType::Bytes};
}
void EmptyCalls(NativeOwnerTable& t)
{
    auto u=t.SnapshotUsage();CHECK(u.setup.entered==0&&u.setup.reserved==0);
    CHECK(u.send.entered==0&&u.send.reserved==0&&u.close.entered==0&&u.close.reserved==0);
}
void NoReady(NativeOwnerTable& t,const NativeTableConfiguration& c,std::size_t route=0)
{ CHECK(!t.TryResolve(c.directory.routes[route].key,c.directory.routes[route].route_version)); }
void ObservedStop(const NativeOwnerLease& o)
{
    const auto x=o.Observe();CHECK(x.on_shutdown_seen&&x.close_intent);
    CHECK(x.phase!=NativeOwnerPhase::Ready&&x.quiescence==NativeQuiescence::Unproven);
    CHECK(o.TryClaimShutdown().issue.code==NativeStatus::ShutdownAlreadyObserved);
}

void N1Configuration()
{
    auto c=Config(2);auto valid=Table(c);CHECK(valid->SnapshotUsage().owners==0);
    auto bad=c;bad.directory.routes[1].fields=c.directory.routes[0].fields;
    bad.directory.routes[1].fields.expected_remote_initiator_names={"distinct-alias-long-enough"};
    CHECK(EndpointDirectory::Build(bad.directory).value); // Not a reverse-key collision.
    auto conflict=NativeOwnerTable::Build(bad);
    CHECK(!conflict&&conflict.issue.code==NativeStatus::CreateKeyConflict);
    CHECK(conflict.issue.first.kind==ConfigIndexKind::Route&&conflict.issue.first.index==0);
    CHECK(conflict.issue.second.kind==ConfigIndexKind::Route&&conflict.issue.second.index==1);
    bad=c;bad.directory.locals.push_back({L(3),c.directory.locals[0].callback_namespace,c.directory.locals[0].local_listen_name});
    CHECK(EndpointDirectory::Build(bad.directory).value);
    auto listener_collision=NativeOwnerTable::Build(bad);
    CHECK(!listener_collision&&listener_collision.issue.code==NativeStatus::CreateKeyConflict);
    CHECK(listener_collision.issue.first.kind==ConfigIndexKind::Local&&listener_collision.issue.first.index==0);
    CHECK(listener_collision.issue.second.kind==ConfigIndexKind::Local&&listener_collision.issue.second.index==1);
    bad.directory.locals[1].local_listen_name="different-listen-owned-name";CHECK(NativeOwnerTable::Build(bad));
    bad=c;bad.directory.routes[1].fields.local_initiator_name=bad.directory.routes[0].fields.local_initiator_name;
    CHECK(NativeOwnerTable::Build(bad)); // Same name, different peers, same callback slot namespace.
    bad.directory.routes[1].fields.remote_listen_name=bad.directory.routes[0].fields.remote_listen_name;
    CHECK(NativeOwnerTable::Build(bad)); // Network ID still distinguishes K.
    bad=c;bad.directory.routes[0].fields.local_initiator_name=bad.directory.locals[0].local_listen_name;
    CHECK(NativeOwnerTable::Build(bad)); // Listener and Dial differ by peer fields, not role alone.
    bad=c;bad.directory.locals.push_back({L(3),"different-namespace","different-listen"});
    bad.directory.routes[1].key.local_locator=L(3);
    bad.directory.routes[1].fields.local_initiator_name=bad.directory.routes[0].fields.local_initiator_name;
    auto ns=NativeOwnerTable::Build(bad);CHECK(!ns&&ns.issue.code==NativeStatus::CallbackNamespaceConflict);
    CHECK(ns.issue.first.kind==ConfigIndexKind::Route&&ns.issue.first.index==0&&ns.issue.second.index==1);
    bad=c;bad.directory.locals[0].local_listen_name="distributeddata-default-excluded";
    CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::UnsupportedSpecialSocketName);
    bad=c;bad.directory.routes[0].fields.local_initiator_name="distributeddata-default-excluded";
    CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::UnsupportedSpecialSocketName);
    bad=c;bad.directory.routes[0].fields.remote_listen_name="distributeddata-default-remote";
    CHECK(NativeOwnerTable::Build(bad));
    bad=c;bad.directions.pop_back();CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidDirection);
    bad=c;bad.directions.push_back(RouteDirection::Dial);CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidDirection);
    bad=c;bad.directions[0]=static_cast<RouteDirection>(99);CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidDirection);
    for(auto member:{&NativeLimits::namespaces,&NativeLimits::total_owners,&NativeLimits::setup_calls,
            &NativeLimits::send_calls,&NativeLimits::close_calls}) {
        bad=c;bad.limits.*member=0;CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidLimits);
        bad=c;bad.limits.*member=std::numeric_limits<std::size_t>::max();
        CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidLimits);
    }
    bad=c;bad.limits.total_owners=2;CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidLimits);
    for(const auto& pkg:{std::string{},std::string(65,'p'),std::string("p\0q",3)}) {
        bad=c;bad.service_pkg_name=pkg;CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::InvalidString);
    }
    bad=c;bad.service_pkg_name=std::string(64,'p');CHECK(NativeOwnerTable::Build(bad));
    bad=c;bad.directory.routes[0].route_version=0;CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::DirectoryRejected);
    bad=c;bad.directory.routes[1].fields=c.directory.routes[0].fields;
    CHECK(NativeOwnerTable::Build(bad).issue.code==NativeStatus::DirectoryRejected); // Reverse conflict first.
    const auto before=native_allocations;{auto measured=Table(c);}
    const auto count=native_allocations-before;CHECK(count>0);
    for(std::size_t i=0;i<count;++i) {
        native_fail_after=static_cast<long>(i);auto failed=NativeOwnerTable::Build(c);native_fail_after=-1;
        CHECK(!failed&&failed.issue.code==NativeStatus::AllocationFailure);
        CHECK(valid->SnapshotUsage().owners==0);CHECK(NativeOwnerTable::Build(c));
    }
    std::cout<<"N1 CONFIG construction_allocation_points="<<count<<'\n';
}

void N1Cache()
{
    const auto c=Config();auto t=Table(c);auto s=Space(*t,c);auto o=Ready(*t,s,c,0,100);
    auto l=t->ReserveListener(s,L(2));CHECK(l);CHECK(l.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(l.value->socket_call->Complete(NativeSocketResult::Id(101)).status==NativeStatus::None);
    auto listen=t->BeginListen(l.value->owner);CHECK(listen);CHECK(listen.value->CallEntered()==NativeStatus::None);
    CHECK(listen.value->Complete(SyncListenResult::Success()).status==NativeStatus::None);
    const auto initial=t->SnapshotUsage();const auto obs=o.Observe();
    for(unsigned i=0;i<64;++i) {
        auto exact=c;auto ns=Space(*t,exact);auto hit=t->ReserveDial(ns,exact.directory.routes[0].key);
        CHECK(hit&&hit.value->creation==NativeCreation::NoCreate&&!hit.value->socket_call);
        CHECK(hit.value->owner==o&&hit.value->owner.Observe().generation==obs.generation);
        auto lh=t->ReserveListener(ns,L(2));CHECK(lh&&lh.value->creation==NativeCreation::NoCreate);
        CHECK(lh.value->owner==l.value->owner);
        auto snapshot=t->TryResolve(c.directory.routes[0].key,1);CHECK(snapshot&&snapshot.value->generation==obs.generation);
        const auto u=t->SnapshotUsage();CHECK(u.owners==initial.owners&&u.owned_ids==initial.owned_ids);
        CHECK(u.namespaces==initial.namespaces&&u.owner_high_water==initial.owner_high_water);
        CHECK(u.generation_high_water==initial.generation_high_water);EmptyCalls(*t);
    }
    for(unsigned field=0;field<8;++field) {
        auto changed=c;
        if(field==0) changed.service_pkg_name+="x";
        if(field==1) changed.directory.locals[0].callback_namespace+="x";
        if(field==2) changed.directory.locals[0].local_listen_name+="x";
        if(field==3) changed.directory.routes[0].fields.remote_network_id+="x";
        if(field==4) changed.directory.routes[0].fields.expected_remote_initiator_names.push_back("alias2");
        if(field==5) changed.directory.routes[0].route_version=2;
        if(field==6) changed.directions[0]=RouteDirection::Accept;
        if(field==7) changed.limits.total_owners=11;
        CHECK(t->AcquireNamespace(L(2),changed).issue.code==NativeStatus::ConfigurationMismatch);
    }
    auto original=c;auto own=Table(original);original.directory.routes[0].fields.remote_network_id="mutated";
    CHECK(own->AcquireNamespace(L(2),c));CHECK(!own->AcquireNamespace(L(2),original));
    CHECK(o.RequestClose()==NativeStatus::None);
    CHECK(t->ReserveDial(s,c.directory.routes[0].key).issue.code==NativeStatus::OwnerUnavailable);
    NoReady(*t,c);std::cout<<"N1 MODEL_CACHE_REUSE cycles=64 (not DDS Participants)\n";
}

void N1Tickets()
{
    const auto c=Config(2);auto t=Table(c);auto s=Space(*t,c);auto r=Dial(*t,s,c);
    CHECK(t->ReserveDial(s,c.directory.routes[0].key).issue.code==NativeStatus::ExistingPending);
    CHECK(t->ReserveDial(s,c.directory.routes[1].key).issue.code==NativeStatus::SetupBusy);
    CHECK(t->SnapshotUsage().owners==1&&t->SnapshotUsage().setup.reserved==1);
    CHECK(r.socket_call->Complete(NativeSocketResult::Id(10)).status==NativeStatus::NotEntered);
    CHECK(NativeOwnerTableTestAccess::WrongKind(*r.socket_call)==NativeStatus::WrongOperationKind);
    CHECK(NativeOwnerTableTestAccess::StaleOperation(*r.socket_call)==NativeStatus::StaleHandle);
    CHECK(r.socket_call->CallEntered()==NativeStatus::None);
    CHECK(r.socket_call->CallEntered()==NativeStatus::AlreadyEntered);
    CHECK(r.socket_call->Complete(NativeSocketResult::Id(-1)).status==NativeStatus::InvalidResult);
    CHECK(r.socket_call->Complete(NativeSocketResult::Error(0)).status==NativeStatus::InvalidResult);
    CHECK(t->SnapshotUsage().setup.entered==1);
    CHECK(r.socket_call->Complete(NativeSocketResult::Id(0)).status==NativeStatus::None); // Nonnegative SDK domain.
    CHECK(r.socket_call->Complete(NativeSocketResult::Id(0)).status==NativeStatus::AlreadySettled);
    EmptyCalls(*t);CHECK(r.owner.Observe().owned_id==0);
    CHECK(t->BeginListen(r.owner).issue.code==NativeStatus::WrongRole);
    auto b=t->BeginBind(r.owner);CHECK(b);CHECK(t->SnapshotUsage().setup.reserved==1);
    CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::NotEntered);
    CHECK(b.value->CallEntered()==NativeStatus::None);
    CHECK(b.value->Complete(SyncBindResult::Error(1)).status==NativeStatus::InvalidResult);
    CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::AlreadySettled);
    CHECK(t->TryResolve(c.directory.routes[0].key,1));EmptyCalls(*t); // No client OnBind needed.
    CHECK(t->TryResolve(c.directory.routes[0].key,2).issue.code==NativeStatus::StaleVersion);
    auto other=Table(c);auto other_s=Space(*other,c);
    CHECK(other->BeginBind(r.owner).issue.code==NativeStatus::ForeignState);
    CHECK(t->ReserveDial(other_s,c.directory.routes[1].key).issue.code==NativeStatus::ForeignState);
    CHECK(t->BeginBind(NativeOwnerTableTestAccess::Stale(r.owner)).issue.code==NativeStatus::StaleHandle);
    NativeOwnerLease empty;CHECK(empty.Observe().status==NativeStatus::InvalidHandle);
    CHECK(empty.RequestClose()==NativeStatus::InvalidHandle);
    NamespaceLease empty_ns;CHECK(empty_ns.AdmitCallback(0).issue.code==NativeStatus::InvalidHandle);
    SocketCallTicket empty_ticket;CHECK(empty_ticket.CallEntered()==NativeStatus::InvalidHandle);
    auto r2=Dial(*t,s,c,1);auto moved=std::move(*r2.socket_call);
    CHECK(r2.socket_call->CallEntered()==NativeStatus::InvalidHandle);
    CHECK(moved.CallEntered()==NativeStatus::None);
    CHECK(moved.Complete(NativeSocketResult::Error(-42)).status==NativeStatus::None);
    CHECK(r2.owner.Observe().phase==NativeOwnerPhase::FailedRetained&&r2.owner.Observe().error_code==-42);
    CHECK(r2.owner.TryClaimShutdown().issue.code==NativeStatus::NoOwnedId);
    CHECK(t->ReserveDial(s,c.directory.routes[1].key).issue.code==NativeStatus::OwnerUnavailable);
    auto lr=t->ReserveListener(s,L(2));CHECK(lr);CHECK(lr.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(lr.value->socket_call->Complete(NativeSocketResult::Id(20)).status==NativeStatus::None);
    CHECK(t->BeginBind(lr.value->owner).issue.code==NativeStatus::WrongRole);
    auto lt=t->BeginListen(lr.value->owner);CHECK(lt);CHECK(lt.value->Abandon()==NativeStatus::Abandoned);
    CHECK(lr.value->owner.Observe().phase==NativeOwnerPhase::SocketKnown);
    auto lt2=t->BeginListen(lr.value->owner);CHECK(lt2);CHECK(lt2.value->CallEntered()==NativeStatus::None);
    CHECK(lt2.value->Complete(SyncListenResult::Success()).status==NativeStatus::None);
    CHECK(lr.value->owner.Observe().phase==NativeOwnerPhase::Listening);EmptyCalls(*t);
}

void N1CloseBeforeSocket()
{
    const auto c=Config(2);
    for(unsigned branch=0;branch<3;++branch) {
        auto t=Table(c);auto s=Space(*t,c);auto original=Ready(*t,s,c,0,70);
        auto r=Dial(*t,s,c,1);CHECK(r.socket_call->CallEntered()==NativeStatus::None);
        CHECK(r.owner.RequestClose()==NativeStatus::None);
        CHECK(r.owner.TryClaimShutdown().issue.code==NativeStatus::AwaitSocketReturn);
        CHECK(t->SnapshotUsage().setup.entered==1&&t->SnapshotUsage().close.reserved==0);
        const auto result=branch==0?NativeSocketResult::Id(71):branch==1?NativeSocketResult::Id(70):NativeSocketResult::Error(-7);
        {
            DenyAllocations deny;
            const auto completion=r.socket_call->Complete(result);
            CHECK(completion.status==(branch==1?NativeStatus::ForeignIdCollision:NativeStatus::None));
            CHECK(t->SnapshotUsage().setup.entered==0);
            CHECK(original.Observe().phase==NativeOwnerPhase::Ready&&original.Observe().owned_id==70);
            if(branch==0) {
                CHECK(r.owner.Observe().phase==NativeOwnerPhase::ClosePending&&r.owner.Observe().owned_id==71);
                auto close=r.owner.TryClaimShutdown();CHECK(close);CHECK(close.value->CallEntered()==NativeStatus::None);
                CHECK(close.value->CompleteShutdownReturned()==NativeStatus::None);
                CHECK(r.owner.Observe().shutdown_returned&&r.owner.Observe().quiescence==NativeQuiescence::Unproven);
            } else {
                CHECK(!r.owner.Observe().owned_id&&r.owner.Observe().phase==NativeOwnerPhase::FailedRetained);
                CHECK(r.owner.TryClaimShutdown().issue.code==NativeStatus::NoOwnedId);
                CHECK(r.owner.Observe().error_code==(branch==2?-7:0));
            }
            CHECK(native_allocations==deny.start);
        }
        CHECK(t->SnapshotUsage().owners==2);NoReady(*t,c,1);EmptyCalls(*t);
    }
    auto t=Table(c);auto s=Space(*t,c);NativeOwnerLease stale;
    for(unsigned i=0;i<64;++i) {
        auto r=Dial(*t,s,c);stale=r.owner;
        if(i%2) CHECK(r.socket_call->Abandon()==NativeStatus::Abandoned);
        else CHECK(r.owner.RequestClose()==NativeStatus::None);
        const auto cancelled=r.socket_call->CallEntered();
        CHECK(cancelled==NativeStatus::Revoked||cancelled==NativeStatus::AlreadySettled);
        CHECK(stale.Observe().status==NativeStatus::StaleHandle);
        CHECK(t->SnapshotUsage().owners==0&&t->SnapshotUsage().owned_ids==0);EmptyCalls(*t);
    }
    CHECK(t->SnapshotUsage().owner_high_water==1);
    auto r=Dial(*t,s,c);CHECK(t->BeginBind(stale).issue.code==NativeStatus::StaleHandle);
    CHECK(r.socket_call->Abandon()==NativeStatus::Abandoned);
}

void N1ClosePool()
{
    const auto c=Config(2);auto t=Table(c);auto s=Space(*t,c);
    auto a=Ready(*t,s,c,0,80),b=Ready(*t,s,c,1,81);
    CHECK(a.RequestClose()==NativeStatus::None);CHECK(a.RequestClose()==NativeStatus::None);
    auto ac=a.TryClaimShutdown();CHECK(ac);CHECK(ac.value->CallEntered()==NativeStatus::None);
    CHECK(ac.value->Abandon()==NativeStatus::EnteredPending);
    CHECK(b.RequestClose()==NativeStatus::None);
    CHECK(b.TryClaimShutdown().issue.code==NativeStatus::CloseBusy);
    CHECK(b.Observe().phase==NativeOwnerPhase::ClosePending&&t->SnapshotUsage().close.entered==1);
    CHECK(ac.value->CompleteShutdownReturned()==NativeStatus::None);
    CHECK(ac.value->CompleteShutdownReturned()==NativeStatus::AlreadySettled);
    CHECK(a.TryClaimShutdown().issue.code==NativeStatus::ShutdownAlreadyReturned);
    auto bc=b.TryClaimShutdown();CHECK(bc);CHECK(bc.value->Abandon()==NativeStatus::Abandoned);
    CHECK(t->SnapshotUsage().close.reserved==0&&b.Observe().phase==NativeOwnerPhase::ClosePending);
    // Both threads start from a real barrier. The ticket survives in its future,
    // so the losing claimant cannot observe an already-destructed reservation.
    std::promise<void> start;auto gate=start.get_future().share();
    // MSVC future stores through move assignment. Keep the deliberately
    // nonassignable operation result behind an assignable test-only owner.
    auto one=std::async(std::launch::async,[copy=b,gate]{gate.wait();
        return std::make_unique<NativeResult<ShutdownCallTicket>>(copy.TryClaimShutdown());});
    auto two=std::async(std::launch::async,[copy=b,gate]{gate.wait();
        return std::make_unique<NativeResult<ShutdownCallTicket>>(copy.TryClaimShutdown());});
    start.set_value();
    if(one.wait_for(std::chrono::seconds(3))!=std::future_status::ready||
            two.wait_for(std::chrono::seconds(3))!=std::future_status::ready) std::abort();
    auto first=one.get(),second=two.get();CHECK(static_cast<bool>(*first)!=static_cast<bool>(*second));
    auto& winner=*first?*first:*second;CHECK(t->SnapshotUsage().close.reserved==1);
    CHECK(winner.value->CallEntered()==NativeStatus::None);
    CHECK(winner.value->CompleteShutdownReturned()==NativeStatus::None);EmptyCalls(*t);
    CHECK(t->SnapshotUsage().owned_ids==2&&t->SnapshotUsage().owners==2);
}

void N1LateShutdown()
{
    const auto c=Config();
    for(bool bind_success:{true,false}) {
        auto t=Table(c);auto s=Space(*t,c);auto o=SocketKnown(*t,s,c,0,90);
        auto bind=t->BeginBind(o);CHECK(bind);CHECK(bind.value->CallEntered()==NativeStatus::None);
        {
            auto cb=s.AdmitCallback(90);CHECK(cb);CHECK(cb.value->CapturedOwner().phase==NativeOwnerPhase::Binding);
            CHECK(cb.value->ObserveShutdown()==NativeStatus::None); // Before ANY RequestClose.
            CHECK(t->SnapshotUsage().setup.entered==1);ObservedStop(o);
            CHECK(o.RequestClose()==NativeStatus::None);ObservedStop(o);NoReady(*t,c);
            const auto result=bind_success?SyncBindResult::Success():SyncBindResult::Error(-95);
            CHECK(bind.value->Complete(result).status==NativeStatus::None);
            CHECK(o.Observe().error_code==(bind_success?0:-95));ObservedStop(o);EmptyCalls(*t);
            CHECK(o.Observe().callback_inflight==1);
        }
        CHECK(o.Observe().callback_inflight==0);ObservedStop(o);
        t->RequestServiceClose();ObservedStop(o);t.reset();ObservedStop(o);
    }
    for(bool entered:{false,true}) {
        auto t=Table(c);auto s=Space(*t,c);auto o=Ready(*t,s,c,0,91);
        CHECK(o.RequestClose()==NativeStatus::None);auto shutdown=o.TryClaimShutdown();CHECK(shutdown);
        if(entered) CHECK(shutdown.value->CallEntered()==NativeStatus::None);
        auto cb=s.AdmitCallback(91);CHECK(cb);CHECK(cb.value->ObserveShutdown()==NativeStatus::None);
        if(!entered) {
            CHECK(t->SnapshotUsage().close.reserved==0&&t->SnapshotUsage().close.entered==0);
            CHECK(shutdown.value->CallEntered()==NativeStatus::Revoked);
            CHECK(shutdown.value->Abandon()==NativeStatus::Revoked);
            CHECK(!o.Observe().shutdown_entered&&!o.Observe().shutdown_returned);ObservedStop(o);
        } else {
            CHECK(t->SnapshotUsage().close.entered==1);
            CHECK(shutdown.value->Abandon()==NativeStatus::EnteredPending);
            CHECK(shutdown.value->CompleteShutdownReturned()==NativeStatus::None);
            CHECK(o.Observe().shutdown_entered&&o.Observe().shutdown_returned);
            CHECK(t->SnapshotUsage().close.entered==0);ObservedStop(o);
        }
        CHECK(o.RequestClose()==NativeStatus::None);ObservedStop(o);NoReady(*t,c);
    }
    // Explicit Close wins without OnShutdown: late Bind may settle but never revive.
    auto t=Table(c);auto s=Space(*t,c);auto o=SocketKnown(*t,s,c,0,92);
    auto b=t->BeginBind(o);CHECK(b);CHECK(b.value->CallEntered()==NativeStatus::None);
    CHECK(o.RequestClose()==NativeStatus::None);auto closer=o.TryClaimShutdown();CHECK(closer);
    CHECK(closer.value->CallEntered()==NativeStatus::None);
    CHECK(closer.value->CompleteShutdownReturned()==NativeStatus::None);
    CHECK(t->SnapshotUsage().setup.entered==1&&!o.Observe().on_shutdown_seen);
    CHECK(b.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);NoReady(*t,c);EmptyCalls(*t);
    CHECK(o.Observe().quiescence==NativeQuiescence::Unproven);
}

void N1Inbound()
{
    const auto c=Config(2,RouteDirection::Accept);auto t=Table(c);auto s=Space(*t,c);const auto p=Peer(c);
    CHECK(s.AdmitCallback(200).issue.code==NativeStatus::UnknownSocket);
    CHECK(t->ReserveDial(s,c.directory.routes[0].key).issue.code==NativeStatus::DirectionMismatch);
    auto inbound=t->ObserveInbound(s,200,p);CHECK(inbound.status==NativeStatus::None&&inbound.accepted&&inbound.owner);
    auto owner=*inbound.owner;CHECK(owner.Observe().role==NativeOwnerRole::Accepted);
    auto same=t->ObserveInbound(s,200,p);CHECK(same.accepted&&same.owner&&*same.owner==owner);
    CHECK(t->SnapshotUsage().owners==1&&t->SnapshotUsage().owned_ids==1);
    auto second=t->ObserveInbound(s,201,p);
    CHECK(second.status==NativeStatus::DuplicateConnection&&!second.accepted&&second.owner);
    CHECK(second.owner->Observe().role==NativeOwnerRole::Rejected&&second.owner->Observe().owned_id==201);
    CHECK(second.owner->Observe().phase==NativeOwnerPhase::ClosePending);
    auto foreign=t->ObserveInbound(s,200,Peer(c,1));
    CHECK(foreign.status==NativeStatus::ForeignIdCollision&&!foreign.accepted&&!foreign.owner);
    CHECK(owner.Observe().phase==NativeOwnerPhase::Ready&&!owner.Observe().close_intent);
    auto wrong=p;wrong.actual_data_type=RecordDataType::Unsupported;
    auto badtype=t->ObserveInbound(s,202,wrong);CHECK(badtype.status==NativeStatus::UnsupportedDataType&&badtype.owner);
    wrong=p;wrong.actual_remote_socket_name="unknown";
    auto unknown=t->ObserveInbound(s,203,wrong);CHECK(unknown.status==NativeStatus::UnknownPeer&&unknown.owner);
    const auto original=owner.Observe();
    {
        auto held=s.AdmitCallback(200);CHECK(held);CHECK(held.value->CapturedOwner().generation==original.generation);
        CHECK(owner.RequestClose()==NativeStatus::None);
        CHECK(held.value->CapturedOwner().phase==NativeOwnerPhase::Ready); // Observation, NOT delivery authority.
        CHECK(held.value->ObserveShutdown()==NativeStatus::None);ObservedStop(owner);
        CHECK(owner.Observe().callback_inflight==1);
    }
    CHECK(owner.Observe().callback_inflight==0);ObservedStop(owner);
    auto retained=t->ObserveInbound(s,200,p);CHECK(!retained.accepted&&!retained.owner);
    auto other=Table(c);auto other_s=Space(*other,c);
    CHECK(t->ObserveInbound(other_s,204,p).status==NativeStatus::ForeignState);
    CHECK(t->ObserveInbound(s,-1,p).status==NativeStatus::InvalidResult);
    auto limited=c;limited.limits.total_owners=3;auto full=Table(limited);auto fs=Space(*full,limited);
    CHECK(full->ObserveInbound(fs,300,p).accepted);
    CHECK(full->ObserveInbound(fs,301,wrong).owner);CHECK(full->ObserveInbound(fs,302,wrong).owner);
    auto rejected=full->ObserveInbound(fs,303,wrong);
    CHECK(rejected.status==NativeStatus::AdmissionSaturated&&rejected.stop_ingress_required&&!rejected.owner);
    CHECK(full->SnapshotUsage().owners==3&&full->SnapshotUsage().admission_sealed);
    auto dc=Config();auto dt=Table(dc);auto ds=Space(*dt,dc);
    auto direction=dt->ObserveInbound(ds,400,Peer(dc));
    CHECK(direction.status==NativeStatus::DirectionMismatch&&!direction.accepted&&direction.owner);
    // Accepted-event accounting and callback leases must not allocate after Build.
    const auto good=Peer(c,1);
    {
        DenyAllocations deny;auto add=t->ObserveInbound(s,205,good);CHECK(add.accepted&&add.owner);
        auto cb=s.AdmitCallback(205);CHECK(cb);CHECK(cb.value->ObserveShutdown()==NativeStatus::None);
        CHECK(native_allocations==deny.start);
    }
}

void N1Lifetime()
{
    const auto c=Config();std::weak_ptr<void> weak;
    {
        auto t=Table(c);weak=NativeOwnerTableTestAccess::Weak(*t);auto s=Space(*t,c);auto r=Dial(*t,s,c);
        CHECK(r.socket_call->CallEntered()==NativeStatus::None);t.reset();CHECK(!weak.expired());
        CHECK(r.owner.TryClaimShutdown().issue.code==NativeStatus::AwaitSocketReturn);
        CHECK(r.socket_call->Complete(NativeSocketResult::Id(500)).status==NativeStatus::None);
        CHECK(r.owner.Observe().phase==NativeOwnerPhase::ClosePending);
        auto callback=s.AdmitCallback(500);CHECK(callback); // Original namespace can record terminal facts.
        auto shutdown=r.owner.TryClaimShutdown();CHECK(shutdown);CHECK(shutdown.value->CallEntered()==NativeStatus::None);
        CHECK(callback.value->ObserveShutdown()==NativeStatus::None);
        CHECK(shutdown.value->CompleteShutdownReturned()==NativeStatus::None);CHECK(!weak.expired());
        ObservedStop(r.owner);
    }
    CHECK(weak.expired()); // Model memory lifetime only, NOT SDK quiescence.
    auto t=Table(c);auto s=Space(*t,c);auto o=Ready(*t,s,c,0,501);t->RequestServiceClose();
    CHECK(t->SnapshotUsage().admission_sealed);
    CHECK(t->AcquireNamespace(L(2),c).issue.code==NativeStatus::ServiceClosing);
    CHECK(t->ReserveDial(s,c.directory.routes[0].key).issue.code==NativeStatus::ServiceClosing);
    CHECK(t->ObserveInbound(s,502,Peer(c)).status==NativeStatus::ServiceClosing);
    NoReady(*t,c);auto cb=s.AdmitCallback(501);CHECK(cb);CHECK(cb.value->ObserveShutdown()==NativeStatus::None);
    auto lost=Table(c);auto ls=Space(*lost,c);NativeOwnerLease owner;
    {
        auto r=Dial(*lost,ls,c);owner=r.owner;CHECK(r.socket_call->CallEntered()==NativeStatus::None);
        // Deliberate caller-precondition violation: diagnose, never synthesize return.
    }
    const auto u=lost->SnapshotUsage();CHECK(u.setup.entered==1&&u.entered_context_lost);
    CHECK(owner.Observe().entered_context_lost&&!owner.Observe().socket_returned);
    CHECK(owner.RequestClose()==NativeStatus::None);
    CHECK(owner.TryClaimShutdown().issue.code==NativeStatus::AwaitSocketReturn);
}

void N1Quota()
{
    const auto c=Config(3);auto t=Table(c);auto s=Space(*t,c);
    auto a=Ready(*t,s,c,0,600),b=Ready(*t,s,c,1,601),d=Ready(*t,s,c,2,602);
    const auto oa=a.Observe(),ob=b.Observe(),od=d.Observe();
    CHECK(NativeOwnerTableTestAccess::Send(*t,a,2,oa.generation).issue.code==NativeStatus::StaleVersion);
    CHECK(NativeOwnerTableTestAccess::Send(*t,a,1,oa.generation+1).issue.code==NativeStatus::StaleGeneration);
    auto as=NativeOwnerTableTestAccess::Send(*t,a,1,oa.generation);CHECK(as);
    CHECK(as.value->CallEntered()==NativeStatus::None);
    CHECK(NativeOwnerTableTestAccess::Send(*t,a,1,oa.generation).issue.code==NativeStatus::SendBusy);
    auto bs=NativeOwnerTableTestAccess::Send(*t,b,1,ob.generation);CHECK(bs);CHECK(bs.value->CallEntered()==NativeStatus::None);
    CHECK(NativeOwnerTableTestAccess::Send(*t,d,1,od.generation).issue.code==NativeStatus::SendBusy);
    CHECK(as.value->Abandon()==NativeStatus::EnteredPending&&t->SnapshotUsage().send.entered==2);
    CHECK(a.RequestClose()==NativeStatus::None);auto close=a.TryClaimShutdown();CHECK(close);
    CHECK(close.value->CallEntered()==NativeStatus::None&&t->SnapshotUsage().send.entered==2);
    auto cb=s.AdmitCallback(600);CHECK(cb);CHECK(cb.value->ObserveShutdown()==NativeStatus::None);
    CHECK(t->SnapshotUsage().send.entered==2&&t->SnapshotUsage().close.entered==1);
    CHECK(as.value->Complete(0).status==NativeStatus::None);CHECK(t->SnapshotUsage().send.entered==1);
    CHECK(close.value->CompleteShutdownReturned()==NativeStatus::None);ObservedStop(a);
    CHECK(bs.value->Complete(-8).status==NativeStatus::None);CHECK(t->SnapshotUsage().send.entered==0);
    auto ds=NativeOwnerTableTestAccess::Send(*t,d,1,od.generation);CHECK(ds);
    CHECK(ds.value->Abandon()==NativeStatus::Abandoned&&t->SnapshotUsage().send.reserved==0);
    auto ds2=NativeOwnerTableTestAccess::Send(*t,d,1,od.generation);CHECK(ds2);
    CHECK(d.RequestClose()==NativeStatus::None);CHECK(ds2.value->CallEntered()==NativeStatus::Revoked);
    CHECK(t->SnapshotUsage().send.reserved==0);
    NativeOwnerTableTestAccess::CallbackCount(*t,b,std::numeric_limits<std::size_t>::max());
    CHECK(s.AdmitCallback(601).issue.code==NativeStatus::CounterExhausted);
    NativeOwnerTableTestAccess::CallbackCount(*t,b,0);
    auto exhausted=Table(c);auto es=Space(*exhausted,c);
    NativeOwnerTableTestAccess::NextIdentity(*exhausted,std::numeric_limits<std::uint64_t>::max());
    CHECK(exhausted->ReserveDial(es,c.directory.routes[0].key).issue.code==NativeStatus::IdentityExhausted);
    CHECK(exhausted->SnapshotUsage().owners==0&&exhausted->SnapshotUsage().setup.reserved==0);
    EmptyCalls(*exhausted);
}
}

void N3ReceiveIdentity()
{
    const auto c = Config(); auto t = Table(c); auto s = Space(*t, c);
    auto owner = Ready(*t, s, c, 0, 700);
    NativeIdentityConstraint pin;
    {
        auto callback = s.AdmitCallback(700); CHECK(callback);
        auto captured = callback.value->CaptureIdentity(); CHECK(captured);
        pin = *captured.value;
        CHECK(t->SnapshotUsage().callbacks == 1);
    }
    CHECK(pin && t->SnapshotUsage().callbacks == 0);
    CHECK(pin.local() == L(2) && pin.remote() == L(10));
    CHECK(pin.Observe().generation == owner.Observe().generation);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t, pin) == NativeStatus::None);
    NativeOwnerTableTestAccess::MovedReceive(*t, pin);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t,
            NativeOwnerTableTestAccess::WrongGeneration(pin)) == NativeStatus::StaleGeneration);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t,
            NativeOwnerTableTestAccess::WrongVersion(pin)) == NativeStatus::StaleVersion);
    auto other = Table(c); auto os = Space(*other, c); auto other_owner = Ready(*other, os, c, 0, 700);
    CHECK(other_owner.Observe().owned_id == owner.Observe().owned_id);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*other, pin) == NativeStatus::ForeignState);
    CHECK(owner.RequestClose() == NativeStatus::None);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t, pin) == NativeStatus::Revoked);
    CHECK(!NativeOwnerTableTestAccess::Receive(*other, pin));
    CHECK(other->SnapshotUsage().receives == 0);
}

void N3ReceiveBindingAndDrain()
{
    const auto c = Config(); auto t = Table(c); auto s = Space(*t, c);
    auto owner = SocketKnown(*t, s, c, 0, 701);
    auto bind = t->BeginBind(owner); CHECK(bind); CHECK(bind.value->CallEntered() == NativeStatus::None);
    NativeIdentityConstraint pin;
    {
        auto callback = s.AdmitCallback(701); CHECK(callback);
        auto captured = callback.value->CaptureIdentity(); CHECK(captured); pin = *captured.value;
    }
    CHECK(t->SnapshotUsage().callbacks == 0);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t, pin) == NativeStatus::NotReady);
    CHECK(!NativeOwnerTableTestAccess::Receive(*t, pin));
    CHECK(bind.value->Complete(SyncBindResult::Success()).status == NativeStatus::None);
    auto execution = NativeOwnerTableTestAccess::Receive(*t, pin); CHECK(execution);
    CHECK(t->SnapshotUsage().receives == 1 && owner.Observe().receive_inflight == 1);
    CHECK(t->SnapshotUsage().callbacks == 0);
    CHECK(owner.RequestClose() == NativeStatus::None);
    CHECK(!NativeOwnerTableTestAccess::Receive(*t, pin));
    CHECK(t->SnapshotUsage().receives == 1);
    execution = {};
    CHECK(t->SnapshotUsage().receives == 0 && owner.Observe().receive_inflight == 0);
    CHECK(owner.Observe().quiescence == NativeQuiescence::Unproven);
}

void BidirectionalRoute()
{
    const auto c=Config(1,RouteDirection::Both);auto t=Table(c);auto s=Space(*t,c);
    const auto& route=c.directory.routes[0];
    auto accepted=t->ObserveInbound(s,702,Peer(c));CHECK(accepted.accepted&&accepted.owner);
    const auto accepted_generation=accepted.owner->Observe().generation;
    CHECK(t->TryResolve(route.key,route.route_version).value->generation==accepted_generation);
    auto dial=Ready(*t,s,c,0,703);
    CHECK(t->TryResolve(route.key,route.route_version).value->generation==dial.Observe().generation);
    CHECK(dial.RequestClose()==NativeStatus::None);
    CHECK(t->TryResolve(route.key,route.route_version).value->generation==accepted_generation);
    auto duplicate=t->ObserveInbound(s,704,Peer(c));
    CHECK(!duplicate.accepted&&duplicate.status==NativeStatus::DuplicateConnection&&duplicate.owner);
    CHECK(t->SnapshotUsage().owners==3);
}

void ClosedDialRetryPolicy()
{
    auto c=Config(1,RouteDirection::Both);c.allow_closed_dial_retry=true;
    auto t=Table(c);auto s=Space(*t,c);auto old=Ready(*t,s,c,0,800);
    auto callback=s.AdmitCallback(800);CHECK(callback);auto pin=callback.value->CaptureIdentity();CHECK(pin);
    CHECK(old.RequestClose()==NativeStatus::None);
    CHECK(!t->ReserveDial(s,c.directory.routes[0].key));
    auto close=old.TryClaimShutdown();CHECK(close);CHECK(close.value->CallEntered()==NativeStatus::None);
    CHECK(!t->ReserveDial(s,c.directory.routes[0].key));
    CHECK(close.value->CompleteShutdownReturned()==NativeStatus::None);
    CHECK(!t->ReserveDial(s,c.directory.routes[0].key)); // Old callback still owns its lease.
    callback.value.reset();
    auto fresh=t->ReserveDial(s,c.directory.routes[0].key);CHECK(fresh&&fresh.value->creation==NativeCreation::CreateRequired);
    CHECK(fresh.value->owner.Observe().incarnation!=old.Observe().incarnation);
    CHECK(fresh.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(fresh.value->socket_call->Complete(NativeSocketResult::Id(801)).status==NativeStatus::None);
    auto bind=t->BeginBind(fresh.value->owner);CHECK(bind);CHECK(bind.value->CallEntered()==NativeStatus::None);
    CHECK(bind.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    CHECK(t->TryResolve(c.directory.routes[0].key,c.directory.routes[0].route_version).value->generation==fresh.value->owner.Observe().generation);
    CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t,*pin.value)==NativeStatus::Revoked);
    auto late=s.AdmitCallback(800);CHECK(late);CHECK(late.value->ObserveShutdown()==NativeStatus::None);
    CHECK(fresh.value->owner.Observe().phase==NativeOwnerPhase::Ready);
    auto invalid=Config();invalid.allow_closed_dial_retry=true;
    CHECK(NativeOwnerTable::Build(invalid).issue.code==NativeStatus::InvalidConfiguration);
}

void ClosedDialRetryOutstandingLeases()
{
    for(unsigned mode=0;mode<3;++mode) {
        auto c=Config(1,RouteDirection::Both);c.allow_closed_dial_retry=true;
        auto t=Table(c);auto s=Space(*t,c);
        auto old=mode==2?SocketKnown(*t,s,c,0,810):Ready(*t,s,c,0,810);
        std::optional<BindCallTicket> bind;
        std::optional<detail::NativeSendCallTicket> send;
        detail::NativeReceiveLease receive;
        if(mode==0) {
            auto callback=s.AdmitCallback(810);CHECK(callback);auto pin=callback.value->CaptureIdentity();CHECK(pin);
            receive=NativeOwnerTableTestAccess::Receive(*t,*pin.value);CHECK(receive);
        } else if(mode==1) {
            auto op=NativeOwnerTableTestAccess::Send(*t,old,old.Observe().route_version,old.Observe().generation);CHECK(op);
            send.emplace(std::move(*op.value));CHECK(send->CallEntered()==NativeStatus::None);
        } else {
            auto op=t->BeginBind(old);CHECK(op);bind.emplace(std::move(*op.value));CHECK(bind->CallEntered()==NativeStatus::None);
        }
        CHECK(old.RequestClose()==NativeStatus::None);
        auto close=old.TryClaimShutdown();CHECK(close);CHECK(close.value->CallEntered()==NativeStatus::None);
        CHECK(close.value->CompleteShutdownReturned()==NativeStatus::None);
        CHECK(!t->ReserveDial(s,c.directory.routes[0].key));
        receive={};
        if(send) CHECK(send->Complete(-1).status==NativeStatus::None);
        if(bind) CHECK(bind->Complete(SyncBindResult::Error(-1)).status==NativeStatus::None);
        auto fresh=t->ReserveDial(s,c.directory.routes[0].key);CHECK(fresh);
        CHECK(fresh.value->owner.Observe().generation!=old.Observe().generation);
        CHECK(fresh.value->socket_call->Abandon()==NativeStatus::Abandoned);
    }
    auto c=Config(1,RouteDirection::Both);c.limits.total_owners=2;
    CHECK(NativeOwnerTable::Build(c).issue.code==NativeStatus::InvalidLimits);
}

void BoundedConcurrentSetupPool()
{
    auto c=Config(4);c.limits.setup_calls=2;
    auto built=NativeOwnerTable::Build(c);CHECK(built);auto t=std::move(*built.value);auto space=Space(*t,c);
    auto invalid=c;invalid.limits.setup_calls=0;CHECK(NativeOwnerTable::Build(invalid).issue.code==NativeStatus::InvalidLimits);
    invalid.limits.setup_calls=3;CHECK(NativeOwnerTable::Build(invalid).issue.code==NativeStatus::InvalidLimits);
    auto first=Ready(*t,space,c,0,501),second=Ready(*t,space,c,1,502);
    auto listener=t->ReserveListener(space,L(2));CHECK(listener&&listener.value->socket_call);
    auto dial=Dial(*t,space,c,2);const auto owners=t->SnapshotUsage().owners;
    CHECK(t->SnapshotUsage().setup.reserved==2&&t->SnapshotUsage().setup.limit==2);
    CHECK(t->ReserveDial(space,c.directory.routes[3].key).issue.code==NativeStatus::SetupBusy);
    CHECK(t->SnapshotUsage().owners==owners); // Rejection creates no owner.
    CHECK(listener.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(dial.socket_call->CallEntered()==NativeStatus::None);
    auto send1=NativeOwnerTableTestAccess::Send(*t,first,first.Observe().route_version,first.Observe().generation);
    auto send2=NativeOwnerTableTestAccess::Send(*t,second,second.Observe().route_version,second.Observe().generation);
    CHECK(send1&&send2&&send1.value->CallEntered()==NativeStatus::None&&send2.value->CallEntered()==NativeStatus::None);
    CHECK(first.RequestClose()==NativeStatus::None);auto close1=first.TryClaimShutdown();CHECK(close1);
    CHECK(close1.value->CallEntered()==NativeStatus::None);CHECK(second.RequestClose()==NativeStatus::None);
    CHECK(second.TryClaimShutdown().issue.code==NativeStatus::CloseBusy);
    const auto occupied=t->SnapshotUsage();
    CHECK(occupied.setup.entered==2&&occupied.send.entered==2&&occupied.close.entered==1);
    CHECK(close1.value->CompleteShutdownReturned()==NativeStatus::None);
    auto close2=second.TryClaimShutdown();CHECK(close2&&close2.value->CallEntered()==NativeStatus::None);
    CHECK(close2.value->CompleteShutdownReturned()==NativeStatus::None);
    CHECK(send1.value->Complete(0).status==NativeStatus::None&&send2.value->Complete(0).status==NativeStatus::None);
    CHECK(listener.value->socket_call->Complete(NativeSocketResult::Id(510)).status==NativeStatus::None);
    auto listen=t->BeginListen(listener.value->owner);CHECK(listen);CHECK(listen.value->CallEntered()==NativeStatus::None);
    CHECK(dial.socket_call->Complete(NativeSocketResult::Id(511)).status==NativeStatus::None);
    CHECK(t->BeginListen(listener.value->owner).issue.code==NativeStatus::SetupBusy);
    CHECK(t->SnapshotUsage().setup.entered==1&&t->SnapshotUsage().setup.reserved==0);
    // Listen occupies one setup slot; Bind must choose the OTHER free slot,
    // not overwrite a hard-coded operation cell belonging to the listener.
    auto bind=t->BeginBind(dial.owner);CHECK(bind&&bind.value->CallEntered()==NativeStatus::None);
    CHECK(t->SnapshotUsage().setup.entered==2);
    CHECK(t->ReserveDial(space,c.directory.routes[3].key).issue.code==NativeStatus::SetupBusy);
    CHECK(bind.value->Complete(SyncBindResult::Success()).status==NativeStatus::None);
    CHECK(listen.value->Complete(SyncListenResult::Success()).status==NativeStatus::None);
    CHECK(dial.owner.Observe().phase==NativeOwnerPhase::Ready&&listener.value->owner.Observe().phase==NativeOwnerPhase::Listening);
    auto reused=Dial(*t,space,c,3);CHECK(reused.socket_call->Abandon()==NativeStatus::Abandoned);EmptyCalls(*t);
}

// Accepted negotiation holds the existing owner budget but owns no SDK handle.
void PreparedInboundContract()
{
    auto c=Config(1,RouteDirection::Both);c.limits.total_owners=3;
    auto t=Table(c);auto s=Space(*t,c);auto dial=Ready(*t,s,c,0,40);
    auto listener=t->ReserveListener(s,L(2));CHECK(listener);
    CHECK(listener.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(listener.value->socket_call->Complete(NativeSocketResult::Id(41)).status==NativeStatus::None);
    auto listen=t->BeginListen(listener.value->owner);CHECK(listen);
    CHECK(listen.value->CallEntered()==NativeStatus::None);
    CHECK(listen.value->Complete(SyncListenResult::Success()).status==NativeStatus::None);
    const auto peer=Peer(c);auto wrong=peer;wrong.remote_network_id="not-the-peer";
    CHECK(t->PrepareInbound(s,42,wrong).issue.code==NativeStatus::UnknownPeer);
    CHECK(t->PrepareInbound(s,40,peer).issue.code==NativeStatus::ForeignIdCollision);
    CHECK(t->SnapshotUsage().owners==2&&!t->SnapshotUsage().admission_sealed);
    std::uint64_t previous=0;
    for(int i=0;i<8;++i) {
        auto pending=t->PrepareInbound(s,42+i,peer);CHECK(pending);
        auto owner=*pending.value;const auto before=owner.Observe();
        CHECK(before.role==NativeOwnerRole::Accepted&&before.phase==NativeOwnerPhase::Reserved&&!before.owned_id);
        CHECK(before.incarnation>previous);previous=before.incarnation;
        CHECK(t->SnapshotUsage().owners==3&&t->SnapshotUsage().owned_ids==2);
        CHECK(s.AdmitCallback(42+i).issue.code==NativeStatus::UnknownSocket);
        CHECK(owner.TryClaimShutdown().issue.code==NativeStatus::NoOwnedId);
        CHECK(!t->PrepareInbound(s,80+i,peer));
        CHECK(owner.RequestClose()==NativeStatus::None);
        CHECK(owner.Observe().status==NativeStatus::StaleHandle);
        CHECK(owner.RequestClose()==NativeStatus::StaleHandle);
        CHECK(t->SnapshotUsage().owners==2&&!t->SnapshotUsage().admission_sealed);
    }
    auto prepared=t->PrepareInbound(s,99,peer);CHECK(prepared);auto accepted=*prepared.value;
    CHECK(t->CommitInbound(accepted,98)==NativeStatus::InvalidResult);
    CHECK(!accepted.Observe().owned_id);
    CHECK(t->CommitInbound(accepted,99)==NativeStatus::None);
    CHECK(t->CommitInbound(accepted,99)==NativeStatus::None);
    CHECK(accepted.Observe().owned_id==99&&accepted.Observe().phase==NativeOwnerPhase::Ready);
    CHECK(accepted.Observe().incarnation!=dial.Observe().incarnation);
    auto selected=t->TryResolve(c.directory.routes[0].key,1);CHECK(selected&&selected.value->generation==dial.Observe().generation);
    {auto callback=s.AdmitCallback(99);CHECK(callback);CHECK(callback.value->ObserveShutdown()==NativeStatus::None);}
    CHECK(accepted.RequestClose()==NativeStatus::None);
    CHECK(accepted.TryClaimShutdown().issue.code==NativeStatus::ShutdownAlreadyObserved);
    CHECK(dial.Observe().phase==NativeOwnerPhase::Ready&&listener.value->owner.Observe().phase==NativeOwnerPhase::Listening);
    EmptyCalls(*t);
}

void PreparedInboundCapacity()
{
    auto c=Config(1,RouteDirection::Both);c.limits.total_owners=3;c.allow_closed_dial_retry=true;
    auto t=Table(c);auto s=Space(*t,c);auto old=Ready(*t,s,c,0,499);
    {auto callback=s.AdmitCallback(499);CHECK(callback);CHECK(callback.value->ObserveShutdown()==NativeStatus::None);}
    auto outgoing=Ready(*t,s,c,0,500); // existing model retry leaves charged terminal metadata

    auto listener=t->ReserveListener(s,L(2));CHECK(listener);
    CHECK(listener.value->socket_call->CallEntered()==NativeStatus::None);
    CHECK(listener.value->socket_call->Complete(NativeSocketResult::Id(501)).status==NativeStatus::None);
    CHECK(t->PrepareInbound(s,502,Peer(c)).issue.code==NativeStatus::AdmissionSaturated);
    CHECK(t->SnapshotUsage().owners==3&&!t->SnapshotUsage().admission_sealed);
    auto listen=t->BeginListen(listener.value->owner);CHECK(listen);CHECK(listen.value->CallEntered()==NativeStatus::None);CHECK(listen.value->Complete(SyncListenResult::Success()).status==NativeStatus::None);
    CHECK(outgoing.Observe().phase==NativeOwnerPhase::Ready&&listener.value->owner.Observe().phase==NativeOwnerPhase::Listening);
}

void DirectedAcceptedReceiveOnly()
{
    auto c=Config(1,RouteDirection::Both);c.dial_only_egress=true;
    auto t=Table(c);auto ns=Space(*t,c);auto outgoing=Ready(*t,ns,c,0,801);
    auto prepared=t->PrepareInbound(ns,802,Peer(c));CHECK(prepared);CHECK(t->CommitInbound(*prepared.value,802)==NativeStatus::None);
    auto other=c;other.dial_only_egress=false;CHECK(t->AcquireNamespace(L(2),other).issue.code==NativeStatus::ConfigurationMismatch);
    {auto callback=ns.AdmitCallback(801);CHECK(callback);CHECK(callback.value->ObserveShutdown()==NativeStatus::None);}
    CHECK(t->TryResolve(c.directory.routes[0].key,1).issue.code==NativeStatus::NotReady);
    {auto callback=ns.AdmitCallback(802);CHECK(callback);auto original=callback.value->CaptureIdentity();CHECK(original);CHECK(NativeOwnerTableTestAccess::ReceiveStatus(*t,*original.value)==NativeStatus::None);}
    CHECK(prepared.value->Observe().phase==NativeOwnerPhase::Ready);
}

int main(int argc,char** argv)
{
    const std::pair<const char*,void(*)()> tests[]={
        {"N5-PREPARED-INBOUND",PreparedInboundContract},
        {"N5-PREPARED-CAPACITY",PreparedInboundCapacity},
        {"N5-DIRECTED-ACCEPTED",DirectedAcceptedReceiveOnly},
        {"N4-SETUP-POOL",BoundedConcurrentSetupPool},
        {"N4-BIDIRECTIONAL",BidirectionalRoute},
        {"N4-CLOSED-DIAL-RETRY",ClosedDialRetryPolicy},
        {"N4-RETRY-OUTSTANDING",ClosedDialRetryOutstandingLeases},
        {"N1-CFG",N1Configuration},{"N1-CACHE",N1Cache},{"N1-TICKET",N1Tickets},
        {"N1-CLOSE-SOCKET",N1CloseBeforeSocket},{"N1-CLOSE-POOL",N1ClosePool},
        {"N1-LATE",N1LateShutdown},{"N1-INBOUND",N1Inbound},{"N1-LIFETIME",N1Lifetime},{"N1-QUOTA",N1Quota},
        {"N3-RX-IDENTITY",N3ReceiveIdentity},{"N3-RX-BINDING-DRAIN",N3ReceiveBindingAndDrain}};
    unsigned passed=0,run=0;
    for(const auto& test:tests) {
        if(argc>1&&std::string(argv[1])!=test.first) continue;
        ++run;
        try { test.second();++passed;std::cout<<"PASS "<<test.first<<'\n'; }
        catch(const std::exception& e) {
            native_fail_after=-1;native_deny_allocations=false;
            std::cerr<<"FAIL "<<test.first<<": "<<e.what()<<'\n';
        }
    }
    std::cout<<"RESULT "<<passed<<'/'<<run<<" native owner MODEL groups passed; SDK/DDS/Participant NOT_RUN\n";
    return run&&passed==run?0:1;
}
