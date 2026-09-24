#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include "NativeSendExecutionInternal.hpp"
#include "NativeReceiveInternal.hpp"
#include <algorithm>
#include <limits>
#include <mutex>
#include <new>

namespace mdds::dsoftbus {
namespace {
constexpr std::size_t no_index = static_cast<std::size_t>(-1);
constexpr std::size_t owner_capacity = 12, namespace_capacity = 8;
constexpr std::uint64_t identity_limit = std::numeric_limits<std::uint64_t>::max();
template<class T> NativeResult<T> NativeFailure(NativeStatus code) noexcept
{ return {std::nullopt, {code}}; }
NativeIssue NativeIssueFor(NativeStatus code,ConfigIndex first={},ConfigIndex second={}) noexcept
{ return {code,first,second,{}}; }
bool NativeBytesValid(const std::string& s,std::size_t max) noexcept
{ return !s.empty()&&s.size()<=max&&s.find('\0')==std::string::npos; }
bool NativeSameKey(const RouteKey& a,const RouteKey& b) noexcept
{ return a.local_locator==b.local_locator&&a.remote_locator==b.remote_locator; }
std::size_t NativeFindLocal(const DirectoryConfiguration& c,const Locator& l) noexcept
{
    for(std::size_t i=0;i<c.locals.size();++i) if(c.locals[i].local_locator==l) return i;
    return no_index;
}
std::size_t NativeFindRoute(const DirectoryConfiguration& c,const RouteKey& key) noexcept
{
    for(std::size_t i=0;i<c.routes.size();++i) if(NativeSameKey(c.routes[i].key,key)) return i;
    return no_index;
}
bool NativeSameConfiguration(const NativeTableConfiguration& a,const NativeTableConfiguration& b) noexcept
{
    if(a.service_pkg_name!=b.service_pkg_name||a.directions!=b.directions||a.allow_closed_dial_retry!=b.allow_closed_dial_retry||
            a.directory.locals.size()!=b.directory.locals.size()||a.directory.routes.size()!=b.directory.routes.size()) return false;
    const auto& x=a.limits;const auto& y=b.limits;
    if(x.namespaces!=y.namespaces||x.total_owners!=y.total_owners||x.setup_calls!=y.setup_calls||
            x.send_calls!=y.send_calls||x.close_calls!=y.close_calls) return false;
    for(std::size_t i=0;i<a.directory.locals.size();++i) {
        const auto& l=a.directory.locals[i];const auto& r=b.directory.locals[i];
        if(l.local_locator!=r.local_locator||l.callback_namespace!=r.callback_namespace||l.local_listen_name!=r.local_listen_name) return false;
    }
    for(std::size_t i=0;i<a.directory.routes.size();++i) {
        const auto& l=a.directory.routes[i];const auto& r=b.directory.routes[i];
        if(!NativeSameKey(l.key,r.key)||l.route_version!=r.route_version) return false;
        const auto& f=l.fields;const auto& g=r.fields;
        if(f.remote_network_id!=g.remote_network_id||f.remote_listen_name!=g.remote_listen_name||
                f.local_initiator_name!=g.local_initiator_name||f.expected_remote_initiator_names!=g.expected_remote_initiator_names||
                f.data_type!=g.data_type) return false;
    }
    return true;
}
NativeIssue NativeValidateConfiguration(const NativeTableConfiguration& c)
{
    const auto& l=c.limits;
    if(!l.namespaces||l.namespaces>namespace_capacity||!l.total_owners||l.total_owners>owner_capacity||
            !l.setup_calls||l.setup_calls>NativeLimits::setup_call_capacity||!l.send_calls||l.send_calls>2||l.close_calls!=1) return NativeIssueFor(NativeStatus::InvalidLimits);
    // Subtraction first: no size addition can overflow even for hostile sizes.
    if(c.directory.locals.size()>l.total_owners||c.directory.routes.size()>l.total_owners-c.directory.locals.size())
        return NativeIssueFor(NativeStatus::InvalidLimits);
    if(c.directions.size()!=c.directory.routes.size()||std::any_of(c.directions.begin(),c.directions.end(),
            [](RouteDirection d){return d!=RouteDirection::Dial&&d!=RouteDirection::Accept&&d!=RouteDirection::Both;}))
        return NativeIssueFor(NativeStatus::InvalidDirection);
    if(c.allow_closed_dial_retry&&std::any_of(c.directions.begin(),c.directions.end(),
            [](RouteDirection d){return d!=RouteDirection::Both;})) return NativeIssueFor(NativeStatus::InvalidConfiguration);
    const auto both=static_cast<std::size_t>(std::count(c.directions.begin(),c.directions.end(),RouteDirection::Both));
    if(both>l.total_owners-c.directory.locals.size()-c.directory.routes.size()) return NativeIssueFor(NativeStatus::InvalidLimits);
    if(!NativeBytesValid(c.service_pkg_name,64)) return NativeIssueFor(NativeStatus::InvalidString);
    constexpr char special[]="distributeddata-default";
    for(std::size_t i=0;i<c.directory.locals.size();++i)
        if(c.directory.locals[i].local_listen_name.compare(0,sizeof(special)-1,special)==0)
            return NativeIssueFor(NativeStatus::UnsupportedSpecialSocketName,{ConfigIndexKind::Local,i});
    for(std::size_t i=0;i<c.directory.routes.size();++i)
        if(c.directions[i]!=RouteDirection::Accept&&c.directory.routes[i].fields.local_initiator_name.compare(0,sizeof(special)-1,special)==0)
            return NativeIssueFor(NativeStatus::UnsupportedSpecialSocketName,{ConfigIndexKind::Route,i});
    // The snapshot is only a construction validator. Runtime never calls its
    // value-copying Resolve/Find APIs, which would allocate in callback paths.
    const auto directory=EndpointDirectory::Build(c.directory);
    if(!directory.value) {
        auto issue=NativeIssueFor(NativeStatus::DirectoryRejected);issue.directory_issue=directory.issue;
        if(directory.issue.code==DirectoryError::NamespaceConflict) {
            issue.code=NativeStatus::CallbackNamespaceConflict;
            // Determine which same-role slot failed without conflating it with K.
            bool local_conflict=false;
            for(std::size_t i=0;i<c.directory.locals.size();++i)
                for(std::size_t j=0;j<i;++j) if(c.directory.locals[i].local_listen_name==c.directory.locals[j].local_listen_name&&
                        c.directory.locals[i].callback_namespace!=c.directory.locals[j].callback_namespace) local_conflict=true;
            const auto kind=local_conflict?ConfigIndexKind::Local:ConfigIndexKind::Route;
            issue.first={kind,directory.issue.conflicting_entry};issue.second={kind,directory.issue.entry};
        }
        return issue;
    }
    std::size_t distinct=0;
    for(std::size_t i=0;i<c.directory.locals.size();++i) {
        bool first=true;
        for(std::size_t j=0;j<i;++j) {
            if(c.directory.locals[i].callback_namespace==c.directory.locals[j].callback_namespace) first=false;
            if(c.directory.locals[i].local_listen_name==c.directory.locals[j].local_listen_name)
                return NativeIssueFor(NativeStatus::CreateKeyConflict,{ConfigIndexKind::Local,j},{ConfigIndexKind::Local,i});
        }
        if(first&&++distinct>l.namespaces) return NativeIssueFor(NativeStatus::InvalidLimits);
    }
    for(std::size_t i=0;i<c.directory.routes.size();++i) {
        if(c.directions[i]==RouteDirection::Accept) continue;
        const auto& a=c.directory.routes[i].fields;
        for(std::size_t j=0;j<i;++j) {
            if(c.directions[j]==RouteDirection::Accept) continue;
            const auto& b=c.directory.routes[j].fields;
            if(a.local_initiator_name==b.local_initiator_name&&a.remote_listen_name==b.remote_listen_name&&a.remote_network_id==b.remote_network_id)
                return NativeIssueFor(NativeStatus::CreateKeyConflict,{ConfigIndexKind::Route,j},{ConfigIndexKind::Route,i});
        }
    }
    return {};
}
}

namespace detail {
struct NativeOwnerCell
{
    bool used=false;
    std::size_t local=no_index,route=no_index,ns=no_index;
    NativeOwnerObservation facts{};
};
enum class NativeOperationPhase { Free, Reserved, Entered };
struct NativeOperationCell
{
    NativeOperationPhase phase=NativeOperationPhase::Free;
    NativeCallKind kind=NativeCallKind::Socket;
    std::size_t owner=no_index;
    std::uint64_t owner_incarnation=0,incarnation=0;
};
struct NativeState
{
    explicit NativeState(const NativeTableConfiguration& input) : config(input)
    {
        for(std::size_t i=0;i<config.directory.locals.size();++i) {
            std::size_t ns=0;
            for(;ns<namespace_count;++ns)
                if(config.directory.locals[namespace_locals[ns]].callback_namespace==config.directory.locals[i].callback_namespace) break;
            if(ns==namespace_count) namespace_locals[namespace_count++]=i;
            local_namespaces[i]=ns;
        }
    }
    const NativeTableConfiguration config;
    std::mutex gate;
    std::array<NativeOwnerCell,owner_capacity> owners{};
    // Disjoint physical pools: setup[0..1], send[2..3], close[4]. A
    // configured setup limit of one keeps cell 1 unavailable to all kinds.
    std::array<NativeOperationCell,NativeLimits::setup_call_capacity+2+1> operations{};
    std::array<std::size_t,owner_capacity> local_namespaces{};
    std::array<std::size_t,namespace_capacity> namespace_locals{};
    std::size_t namespace_count=0,owner_count=0,owner_high_water=0,callbacks=0,receives=0;
    std::uint64_t next_identity=1,generation_high_water=0;
    bool sealed=false,entered_context_lost=false;
    NativeStatus seal_reason=NativeStatus::None;
    NativeEgressUsage egress_usage{};

    NativeStatus CheckReceive(const NativeIdentityConstraint& pin) const noexcept
    {
        if (!pin.state_) return NativeStatus::InvalidHandle;
        if (pin.state_.get() != this) return NativeStatus::ForeignState;
        if (!ValidOwner(pin.slot_, pin.incarnation_)) return NativeStatus::StaleHandle;
        const auto& cell = owners[pin.slot_]; const auto& f = cell.facts;
        if (f.generation != pin.generation_) return NativeStatus::StaleGeneration;
        if (f.route_version != pin.route_version_) return NativeStatus::StaleVersion;
        if (cell.route >= config.directory.routes.size()) return NativeStatus::WrongRole;
        const auto& route = config.directory.routes[cell.route];
        if (route.key.local_locator != pin.local_ || route.key.remote_locator != pin.remote_)
            return NativeStatus::InvalidHandle;
        if (route.route_version != pin.route_version_) return NativeStatus::StaleVersion;
        if (sealed) return NativeStatus::ServiceClosing;
        if (f.close_intent) return NativeStatus::Revoked;
        if (f.phase == NativeOwnerPhase::Binding) return NativeStatus::NotReady;
        return f.phase == NativeOwnerPhase::Ready && f.owned_id ? NativeStatus::None : NativeStatus::WrongState;
    }

    bool ValidOwner(std::size_t slot,std::uint64_t incarnation) const noexcept
    { return slot<owners.size()&&owners[slot].used&&owners[slot].facts.incarnation==incarnation; }
    NativeStatus CheckOwner(const NativeOwnerLease& lease) const noexcept
    {
        if(!lease.state_) return NativeStatus::InvalidHandle;
        if(lease.state_.get()!=this) return NativeStatus::ForeignState;
        return ValidOwner(lease.slot_,lease.incarnation_)?NativeStatus::None:NativeStatus::StaleHandle;
    }
    std::size_t FindId(NativeSocketId id) const noexcept
    {
        for(std::size_t i=0;i<owners.size();++i) if(owners[i].used&&owners[i].facts.owned_id==id) return i;
        return no_index;
    }
    std::size_t FindPlan(NativeOwnerRole role,std::size_t index) const noexcept
    {
        std::size_t retired=no_index;
        for(std::size_t i=0;i<owners.size();++i) if(owners[i].used&&owners[i].facts.role==role&&
                (role==NativeOwnerRole::Listener?owners[i].local:owners[i].route)==index) {
            if(!owners[i].facts.close_intent) return i;
            retired=i;
        }
        return retired;
    }
    std::size_t ReadyRouteOwner(std::size_t route,std::uint64_t original_generation=0) const noexcept
    {
        // A queued send keeps its original generation even if a preferred dial
        // becomes READY later. Preference is applied only at initial selection.
        const auto direction=config.directions[route];
        for(const auto role:{NativeOwnerRole::Dial,NativeOwnerRole::Accepted}) {
            if((role==NativeOwnerRole::Dial&&direction==RouteDirection::Accept)||
                    (role==NativeOwnerRole::Accepted&&direction==RouteDirection::Dial)) continue;
            for(std::size_t i=0;i<owners.size();++i) {
                const auto& c=owners[i];const auto& f=c.facts;
                if(c.used&&c.route==route&&f.role==role&&f.phase==NativeOwnerPhase::Ready&&
                        !f.close_intent&&f.owned_id&&(!original_generation||f.generation==original_generation)) return i;
            }
        }
        return no_index;
    }
    bool IdentitiesAvailable(std::uint64_t count) const noexcept
    { return count<identity_limit&&next_identity<=identity_limit-count; }
    std::size_t FreeOwner() const noexcept
    {
        if(owner_count>=config.limits.total_owners) return no_index;
        for(std::size_t i=0;i<owners.size();++i) if(!owners[i].used) return i;
        return no_index;
    }
    std::size_t FreeOperation(NativeCallKind kind) const noexcept
    {
        const std::size_t first=kind==NativeCallKind::Shutdown?NativeLimits::setup_call_capacity+2:
                kind==NativeCallKind::Send?NativeLimits::setup_call_capacity:0;
        const std::size_t count=kind==NativeCallKind::Shutdown?config.limits.close_calls:
                kind==NativeCallKind::Send?config.limits.send_calls:config.limits.setup_calls;
        for(std::size_t i=first;i<first+count;++i) if(operations[i].phase==NativeOperationPhase::Free) return i;
        return no_index;
    }
    bool HasOperation(std::size_t owner,NativeCallKind kind) const noexcept
    {
        for(const auto& op:operations) if(op.phase!=NativeOperationPhase::Free&&op.owner==owner&&op.kind==kind) return true;
        return false;
    }
    void CreateOwner(std::size_t slot,NativeOwnerRole role,std::size_t local,std::size_t route,std::size_t ns) noexcept
    {
        auto& cell=owners[slot];cell=NativeOwnerCell{};cell.used=true;cell.local=local;cell.route=route;cell.ns=ns;
        auto& f=cell.facts;f.status=NativeStatus::None;f.slot=slot;f.role=role;
        f.incarnation=next_identity++;f.generation=f.incarnation;
        if(route!=no_index) f.route_version=config.directory.routes[route].route_version;
        generation_high_water=f.generation;
        ++owner_count;owner_high_water=std::max(owner_high_water,owner_count);
    }
    void Mint(NativeOperationToken& token,const std::shared_ptr<NativeState>& self,
            std::size_t owner,std::size_t operation,NativeCallKind kind) noexcept
    {
        auto& op=operations[operation];op={NativeOperationPhase::Reserved,kind,owner,owners[owner].facts.incarnation,next_identity++};
        token.state_=self;token.owner_slot_=owner;token.owner_incarnation_=op.owner_incarnation;
        token.operation_slot_=operation;token.operation_incarnation_=op.incarnation;token.kind_=kind;
        if(kind==NativeCallKind::Shutdown) owners[owner].facts.shutdown_reserved=true;
    }
    void RefreshStopped(NativeOwnerCell& cell) noexcept
    {
        auto& f=cell.facts;
        if(f.on_shutdown_seen) f.phase=NativeOwnerPhase::RetainedNotQuiescent;
        else if(f.close_intent) {
            if(f.owned_id) f.phase=f.shutdown_returned?NativeOwnerPhase::RetainedNotQuiescent:NativeOwnerPhase::ClosePending;
            else if(!f.socket_entered||f.socket_returned) f.phase=NativeOwnerPhase::FailedRetained;
        }
    }
    void ReleaseReserved(std::size_t operation) noexcept
    {
        auto& op=operations[operation];auto& cell=owners[op.owner];
        const auto kind=op.kind;op.phase=NativeOperationPhase::Free;
        if(kind==NativeCallKind::Socket) {
            // Only never-entered creation has no external effects and may reuse
            // its physical cell. Its old incarnation immediately becomes stale.
            cell.used=false;--owner_count;return;
        }
        if(kind==NativeCallKind::Shutdown) cell.facts.shutdown_reserved=false;
        if((kind==NativeCallKind::Bind||kind==NativeCallKind::Listen)&&!cell.facts.close_intent)
            cell.facts.phase=NativeOwnerPhase::SocketKnown;
        RefreshStopped(cell);
    }
    void CloseOwner(std::size_t slot,bool observed=false) noexcept
    {
        auto& cell=owners[slot];auto& f=cell.facts;
        f.close_intent=true;if(observed) f.on_shutdown_seen=true;
        for(std::size_t i=0;i<operations.size();++i) {
            const auto& op=operations[i];
            if(op.owner==slot&&op.phase==NativeOperationPhase::Reserved&&
                    (op.kind!=NativeCallKind::Shutdown||f.on_shutdown_seen)) ReleaseReserved(i);
        }
        if(cell.used) RefreshStopped(cell);
    }
    NativeStatus CheckToken(const NativeOperationToken& token,NativeCallKind kind) const noexcept
    {
        if(token.settled_) return NativeStatus::AlreadySettled;
        if(token.kind_!=kind) return NativeStatus::WrongOperationKind;
        if(!ValidOwner(token.owner_slot_,token.owner_incarnation_)) return NativeStatus::Revoked;
        if(token.operation_slot_>=operations.size()) return NativeStatus::StaleHandle;
        const auto& op=operations[token.operation_slot_];
        if(op.incarnation!=token.operation_incarnation_||op.phase==NativeOperationPhase::Free) {
            // A real close cancellation remains revoked even after pool reuse;
            // a malformed token on a healthy owner is instead stale.
            return owners[token.owner_slot_].facts.close_intent?NativeStatus::Revoked:NativeStatus::StaleHandle;
        }
        if(op.owner!=token.owner_slot_||op.owner_incarnation!=token.owner_incarnation_) return NativeStatus::StaleHandle;
        return op.kind==kind?NativeStatus::None:NativeStatus::WrongOperationKind;
    }
    NativeResult<NativeReservation> Reserve(const std::shared_ptr<NativeState>& self,NativeOwnerRole role,
            std::size_t local,std::size_t route,std::size_t ns) noexcept
    {
        const auto existing=FindPlan(role,role==NativeOwnerRole::Listener?local:route);
        if(existing!=no_index) {
            const auto& f=owners[existing].facts;
            if(!f.close_intent&&(f.phase==NativeOwnerPhase::Ready||f.phase==NativeOwnerPhase::Listening)) {
                NativeReservation value;value.owner=NativeOwnerLease(self,existing,f.incarnation);
                return {std::move(value),{}};
            }
            const bool pending=!f.close_intent&&(f.phase==NativeOwnerPhase::Reserved||f.phase==NativeOwnerPhase::SocketKnown||f.phase==NativeOwnerPhase::Binding);
            const bool retry=role==NativeOwnerRole::Dial&&config.allow_closed_dial_retry&&
                    config.directions[route]==RouteDirection::Both&&f.close_intent&&f.socket_returned&&
                    (!f.owned_id||f.shutdown_returned||f.on_shutdown_seen)&&!f.callback_inflight&&!f.receive_inflight&&
                    !HasOperation(existing,NativeCallKind::Socket)&&!HasOperation(existing,NativeCallKind::Bind)&&
                    !HasOperation(existing,NativeCallKind::Send)&&!HasOperation(existing,NativeCallKind::Shutdown);
            if(!retry) return NativeFailure<NativeReservation>(pending?NativeStatus::ExistingPending:NativeStatus::OwnerUnavailable);
        }
        const auto slot=FreeOwner(),op=FreeOperation(NativeCallKind::Socket);
        if(slot==no_index) return NativeFailure<NativeReservation>(NativeStatus::AdmissionSaturated);
        if(op==no_index) return NativeFailure<NativeReservation>(NativeStatus::SetupBusy);
        if(!IdentitiesAvailable(2)) return NativeFailure<NativeReservation>(NativeStatus::IdentityExhausted);
        CreateOwner(slot,role,local,route,ns);
        NativeReservation value;value.owner=NativeOwnerLease(self,slot,owners[slot].facts.incarnation);
        value.creation=NativeCreation::CreateRequired;value.socket_call.emplace();
        Mint(value.socket_call->token_,self,slot,op,NativeCallKind::Socket);
        return {std::move(value),{}};
    }
    NativeStatus BeginSetup(std::size_t owner,NativeCallKind kind) const noexcept
    {
        if(sealed) return NativeStatus::ServiceClosing;
        const auto& f=owners[owner].facts;
        if((kind==NativeCallKind::Listen&&f.role!=NativeOwnerRole::Listener)||
                (kind==NativeCallKind::Bind&&f.role!=NativeOwnerRole::Dial)) return NativeStatus::WrongRole;
        if(f.close_intent) return NativeStatus::OwnerUnavailable;
        if(f.phase!=NativeOwnerPhase::SocketKnown) return NativeStatus::WrongState;
        // More than one global setup cell must not permit concurrent Listen
        // invocations against the same owner while its phase is SocketKnown.
        if(HasOperation(owner,kind)) return NativeStatus::SetupBusy;
        if(FreeOperation(kind)==no_index) return NativeStatus::SetupBusy;
        if(!IdentitiesAvailable(1)) return NativeStatus::IdentityExhausted;
        return NativeStatus::None;
    }
};

std::shared_ptr<NativeState> NativeEgressBridge::Bind(NativeOwnerTable& table) noexcept
{ return table.state_; }
NativeStatus NativeEgressBridge::Reserve(RuntimeCeilingLease& lease,const NativeTableConfiguration& exact) noexcept
{
    auto& s=*lease.state_;const auto& l=lease.limits_;
    if(!l.peer_records||!l.peer_bytes||!l.global_records||!l.global_bytes||!l.max_record_bytes||
            l.max_record_bytes>std::numeric_limits<std::uint32_t>::max()||
            l.max_record_bytes>l.peer_bytes||l.max_record_bytes>l.global_bytes||
            l.global_records>NativeEgressUsage::record_limit||l.global_bytes>NativeEgressUsage::byte_limit)
        return NativeStatus::InvalidLimits;
    std::lock_guard<std::mutex> lock(s.gate);
    if(s.sealed) return NativeStatus::ServiceClosing;
    if(!NativeSameConfiguration(s.config,exact)) return NativeStatus::ConfigurationMismatch;
    auto& u=s.egress_usage;
    if(u.runtimes==NativeEgressUsage::runtime_limit||l.global_records>NativeEgressUsage::record_limit-u.records||
            l.global_bytes>NativeEgressUsage::byte_limit-u.bytes) return NativeStatus::AdmissionSaturated;
    ++u.runtimes;u.records+=l.global_records;u.bytes+=l.global_bytes;lease.charged_=true;
    return NativeStatus::None;
}
RuntimeCeilingLease::~RuntimeCeilingLease() noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(charged_) {auto& u=state_->egress_usage;--u.runtimes;u.records-=limits_.global_records;u.bytes-=limits_.global_bytes;}
}
NativeStatus NativeEgressBridge::Publish(EgressState& r) noexcept
{
    auto& s=*r.ceiling_->state_;std::lock_guard<std::mutex> lock(s.gate);
    if(s.sealed) return NativeStatus::ServiceClosing;
    r.active_=true;return NativeStatus::None;
}
NativeStatus NativeEgressBridge::Check(const std::shared_ptr<NativeState>& s,const EgressState& r) noexcept
{
    if(s!=r.ceiling_->state_) return NativeStatus::ForeignState;
    std::lock_guard<std::mutex> lock(s->gate);
    if(s->sealed) return NativeStatus::ServiceClosing;
    return r.active_?NativeStatus::None:NativeStatus::Revoked;
}
NativeStatus NativeEgressBridge::Revoke(const std::shared_ptr<NativeState>& s,EgressState& r) noexcept
{
    if(s!=r.ceiling_->state_) return NativeStatus::ForeignState;
    std::lock_guard<std::mutex> lock(s->gate);r.active_=false;return NativeStatus::None;
}
NativeEgressUsage NativeEgressBridge::Usage(const std::shared_ptr<NativeState>& s) noexcept
{std::lock_guard<std::mutex> lock(s->gate);return s->egress_usage;}
bool NativeEgressBridge::Resolve(const RuntimeCeilingLease& lease,const Route& r,SendConnectionSnapshot& output) noexcept
{
    auto& s=*lease.state_;std::lock_guard<std::mutex> lock(s.gate);
    if(s.sealed) return false;
    const auto route=NativeFindRoute(s.config.directory,r.key);
    if(route==no_index||s.config.directory.routes[route].route_version!=r.route_version) return false;
    const auto owner=s.ReadyRouteOwner(route);
    if(owner==no_index) return false;
    const auto& f=s.owners[owner].facts;
    if(f.close_intent||f.phase!=NativeOwnerPhase::Ready) return false;
    output={r.key,r.route_version,f.generation};return true;
}
NativeStatus NativeEgressBridge::Admit(const std::shared_ptr<NativeState>& s,EgressState& r,
        const RuntimeIdentityHandle& identity,const Route& route,std::uint64_t generation,
        SendQueue::Clock::time_point deadline,NativeSendCallTicket& ticket,NativeSocketId& id,
        const std::shared_ptr<NativeSendInvocationState>& control) noexcept
{
    if(s!=r.ceiling_->state_) return NativeStatus::ForeignState;
    std::lock_guard<std::mutex> lock(s->gate);
    if(s->sealed) return NativeStatus::ServiceClosing;
    if(!r.active_) return NativeStatus::Revoked;
    if(!identity||identity!=r.identity_) return NativeStatus::InvalidHandle;
    if(SendQueue::Clock::now()>=deadline) return NativeStatus::DeadlineExpired;
    const auto index=NativeFindRoute(s->config.directory,route.key);
    if(index==no_index) return NativeStatus::NotFound;
    if(s->config.directory.routes[index].route_version!=route.route_version) return NativeStatus::StaleVersion;
    const auto owner=s->ReadyRouteOwner(index,generation);
    if(owner==no_index) return s->ReadyRouteOwner(index)==no_index?NativeStatus::NotReady:NativeStatus::StaleGeneration;
    const auto& f=s->owners[owner].facts;
    if(f.close_intent||f.phase!=NativeOwnerPhase::Ready||!f.owned_id) return NativeStatus::NotReady;
    if(f.generation!=generation) return NativeStatus::StaleGeneration;
    const auto slot=s->FreeOperation(NativeCallKind::Send);
    if(slot==no_index||s->HasOperation(owner,NativeCallKind::Send)) return NativeStatus::SendBusy;
    if(!s->IdentitiesAvailable(1)) return NativeStatus::IdentityExhausted;
    std::size_t control_slot=0;
    if(control) {
        while(control_slot<r.controls_.size()&&r.controls_[control_slot].occupied) ++control_slot;
        if(control_slot==r.controls_.size()) return NativeStatus::SendBusy;
    }
    s->Mint(ticket.token_,s,owner,slot,NativeCallKind::Send);id=*f.owned_id;
    if(control) {
        r.controls_[control_slot].occupied=true;r.controls_[control_slot].control=control;
        ++r.admitted_outstanding_;control->slot_=control_slot;
        control->phase_=NativeSendInvocationState::Phase::Reserved;
    }
    return NativeStatus::None;
}
NativeStatus NativeEgressBridge::Enter(NativeSendCallTicket& ticket,SendQueue::Clock::time_point deadline) noexcept
{
    auto& t=ticket.token_;if(!t.state_) return NativeStatus::InvalidHandle;
    auto& s=*t.state_;std::lock_guard<std::mutex> lock(s.gate);
    return EnterLocked(ticket,deadline);
}
NativeStatus NativeEgressBridge::EnterLocked(NativeSendCallTicket& ticket,SendQueue::Clock::time_point deadline) noexcept
{
    auto& t=ticket.token_;auto& s=*t.state_;
    const auto status=s.CheckToken(t,NativeCallKind::Send);if(status!=NativeStatus::None) return status;
    auto& op=s.operations[t.operation_slot_];
    if(op.phase==NativeOperationPhase::Entered) return NativeStatus::AlreadyEntered;
    // Sample AFTER waiting for this gate; no stale pre-lock deadline sample.
    if(SendQueue::Clock::now()>=deadline) {s.ReleaseReserved(t.operation_slot_);t.settled_=true;return NativeStatus::DeadlineExpired;}
    op.phase=NativeOperationPhase::Entered;return NativeStatus::None;
}
NativeStatus NativeEgressBridge::EnterInvocation(NativeSendInvocationState& c,
        std::shared_ptr<NativeSendResources>& output) noexcept
{
    if(!c.registration_) return NativeStatus::InvalidHandle;
    auto& s=*c.registration_->ceiling_->state_;std::lock_guard<std::mutex> lock(s.gate);
    if(c.phase_!=NativeSendInvocationState::Phase::Reserved) return NativeStatus::Revoked;
    output=std::move(c.resources_); // Required empty output, sole operational owner.
    const auto status=EnterLocked(output->ticket_,c.deadline_);
    c.phase_=status==NativeStatus::None?NativeSendInvocationState::Phase::Entered:NativeSendInvocationState::Phase::Settling;
    return status;
}
void NativeEgressBridge::CancelInvocation(NativeSendInvocationState& c,
        std::shared_ptr<NativeSendResources>& output) noexcept
{
    if(!c.registration_) return;
    auto& s=*c.registration_->ceiling_->state_;std::lock_guard<std::mutex> lock(s.gate);
    if(c.phase_!=NativeSendInvocationState::Phase::Reserved) return;
    c.phase_=NativeSendInvocationState::Phase::Settling;output=std::move(c.resources_);
}
void NativeEgressBridge::SettleInvocation(NativeSendInvocationState& c) noexcept
{
    auto& r=*c.registration_;auto& s=*r.ceiling_->state_;std::lock_guard<std::mutex> lock(s.gate);
    if(c.phase_!=NativeSendInvocationState::Phase::Settling&&c.phase_!=NativeSendInvocationState::Phase::Entered) return;
    c.phase_=NativeSendInvocationState::Phase::Terminal;
    r.controls_[c.slot_].control.reset();r.controls_[c.slot_].occupied=false;--r.admitted_outstanding_;
}
NativeStatus NativeEgressBridge::BeginRevoke(const std::shared_ptr<NativeState>& s,
        const std::shared_ptr<EgressState>& r,NativeRevokeTicket& ticket) noexcept
{
    if(s!=r->ceiling_->state_) return NativeStatus::ForeignState;
    std::lock_guard<std::mutex> lock(s->gate);
    ticket.registration_=r;r->runtime_->FreezeAdmissions();r->active_=false;r->strong_revoked_=true;
    for(std::size_t i=0;i<r->controls_.size();++i) {
        // Pin even non-Reserved controls directly in the empty holder: a last
        // temporary strong reference must never destruct inside this gate.
        ticket.controls_[i]=r->controls_[i].control.lock();
        if(ticket.controls_[i]&&ticket.controls_[i]->phase_==NativeSendInvocationState::Phase::Reserved) {
            ticket.controls_[i]->phase_=NativeSendInvocationState::Phase::Settling;
            ticket.resources_[i]=std::move(ticket.controls_[i]->resources_);
        }
    }
    return NativeStatus::None;
}
void NativeEgressBridge::QueueBarrier(EgressState& r) noexcept
{std::lock_guard<std::mutex> lock(r.ceiling_->state_->gate);r.queue_barrier_done_=true;}
NativeEgressActivity NativeEgressBridge::Observe(const std::shared_ptr<NativeState>& s,const EgressState& r) noexcept
{
    if(s!=r.ceiling_->state_) return {NativeStatus::ForeignState};
    std::lock_guard<std::mutex> lock(s->gate);
    NativeEgressActivity result;
    result.status=NativeStatus::None;result.admitted_outstanding=r.admitted_outstanding_;
    result.submission_inflight=r.runtime_->ProducerCount();result.admission_closed=r.strong_revoked_;
    result.queue_barrier_crossed=r.queue_barrier_done_;
    result.drained=result.admission_closed&&result.queue_barrier_crossed&&
            result.admitted_outstanding==0&&result.submission_inflight==0;
    return result;
}
NativeCompletion NativeEgressBridge::Fault(NativeSendCallTicket& ticket,NativeProviderFault fault,
        std::optional<std::int32_t> code) noexcept
{
    auto& t=ticket.token_;if(!t.state_) return {NativeStatus::InvalidHandle};
    auto& s=*t.state_;std::lock_guard<std::mutex> lock(s.gate);
    const auto status=s.CheckToken(t,NativeCallKind::Send);if(status!=NativeStatus::None) return {status};
    auto& op=s.operations[t.operation_slot_];auto& cell=s.owners[t.owner_slot_];
    if(op.phase!=NativeOperationPhase::Entered) return {NativeStatus::NotEntered,cell.facts};
    auto& f=cell.facts;f.provider_fault=fault;f.invalid_provider_return=code;
    f.failure=NativeStatus::ProviderContractFault;f.close_intent=true;
    op.phase=NativeOperationPhase::Free;t.settled_=true;s.RefreshStopped(cell);
    return {NativeStatus::ProviderContractFault,f};
}
std::mutex& NativeEgressBridge::TestGate(const std::shared_ptr<NativeState>& s) noexcept {return s->gate;}

NativeReceiveGuard::NativeReceiveGuard(const NativeIdentityConstraint& pin) noexcept
    : state_(pin.state_), gate_(state_->gate), status_(state_->CheckReceive(pin)), slot_(pin.slot_) {}
NativeReceiveGuard::NativeReceiveGuard(NativeReceiveGuard&& other) noexcept
    : state_(std::move(other.state_)), gate_(std::move(other.gate_)),
      status_(std::exchange(other.status_, NativeStatus::InvalidHandle)),
      slot_(std::exchange(other.slot_, 0)), entered_(std::exchange(other.entered_, false)) {}
NativeReceiveGuard NativeReceiveBridge::Acquire(const NativeOwnerTable& table,
        const NativeIdentityConstraint& pin) noexcept
{
    if (!pin.state_) return NativeReceiveGuard(NativeStatus::InvalidHandle);
    if (table.state_ != pin.state_) return NativeReceiveGuard(NativeStatus::ForeignState);
    return NativeReceiveGuard(pin);
}
NativeReceiveLease NativeReceiveBridge::Enter(NativeReceiveGuard& guard) noexcept
{
    if (!guard.state_ || !guard.gate_.owns_lock() ||
            guard.status_ != NativeStatus::None || guard.entered_) return {};
    auto& state = *guard.state_; auto& facts = state.owners[guard.slot_].facts;
    if (state.receives == std::numeric_limits<std::size_t>::max() ||
            facts.receive_inflight == std::numeric_limits<std::size_t>::max()) {
        guard.status_ = NativeStatus::CounterExhausted;
        return {};
    }
    ++state.receives; ++facts.receive_inflight; guard.entered_ = true;
    return NativeReceiveLease(guard.state_, guard.slot_, facts.incarnation);
}
NativeReceiveLease::NativeReceiveLease(std::shared_ptr<NativeState> s,
        std::size_t slot, std::uint64_t incarnation) noexcept
    : state_(std::move(s)), slot_(slot), incarnation_(incarnation) {}
NativeReceiveLease::~NativeReceiveLease() noexcept { Release(); }
NativeReceiveLease::NativeReceiveLease(NativeReceiveLease&& other) noexcept
    : state_(std::move(other.state_)), slot_(other.slot_), incarnation_(other.incarnation_) {}
NativeReceiveLease& NativeReceiveLease::operator=(NativeReceiveLease&& other) noexcept
{
    if (this != &other) {
        Release(); state_ = std::move(other.state_); slot_ = other.slot_; incarnation_ = other.incarnation_;
    }
    return *this;
}
void NativeReceiveLease::Release() noexcept
{
    auto state = std::move(state_);
    if (!state) return;
    std::lock_guard<std::mutex> lock(state->gate);
    --state->receives;
    // N1 never reuses a native owner after external creation; its incarnation
    // remains pinned. No callback-quiescence conclusion follows from this count.
    if (state->ValidOwner(slot_, incarnation_)) --state->owners[slot_].facts.receive_inflight;
}
}

NativeOwnerTable::NativeOwnerTable(std::shared_ptr<detail::NativeState> s) noexcept : state_(std::move(s)) {}
NativeOwnerTable::~NativeOwnerTable() noexcept { RequestServiceClose(); }
NativeResult<std::unique_ptr<NativeOwnerTable>> NativeOwnerTable::Build(const NativeTableConfiguration& input) noexcept
{
    try {
        const auto issue=NativeValidateConfiguration(input);
        if(issue.code!=NativeStatus::None) return {std::nullopt,issue};
        auto state=std::make_shared<detail::NativeState>(input);
        return {std::unique_ptr<NativeOwnerTable>(new NativeOwnerTable(std::move(state))),{}};
    } catch(const std::bad_alloc&) { return NativeFailure<std::unique_ptr<NativeOwnerTable>>(NativeStatus::AllocationFailure); }
}
NativeResult<NamespaceLease> NativeOwnerTable::AcquireNamespace(const Locator& local,const NativeTableConfiguration& exact) const noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->sealed) return NativeFailure<NamespaceLease>(NativeStatus::ServiceClosing);
    if(!NativeSameConfiguration(state_->config,exact)) return NativeFailure<NamespaceLease>(NativeStatus::ConfigurationMismatch);
    const auto index=NativeFindLocal(state_->config.directory,local);
    if(index==no_index) return NativeFailure<NamespaceLease>(NativeStatus::NotFound);
    return {NamespaceLease(state_,state_->local_namespaces[index]),{}};
}
NativeResult<NativeReservation> NativeOwnerTable::ReserveListener(const NamespaceLease& ns,const Locator& local) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(!ns.state_) return NativeFailure<NativeReservation>(NativeStatus::InvalidHandle);
    if(ns.state_!=state_) return NativeFailure<NativeReservation>(NativeStatus::ForeignState);
    if(state_->sealed) return NativeFailure<NativeReservation>(NativeStatus::ServiceClosing);
    const auto i=NativeFindLocal(state_->config.directory,local);
    if(i==no_index) return NativeFailure<NativeReservation>(NativeStatus::NotFound);
    if(state_->local_namespaces[i]!=ns.namespace_index_) return NativeFailure<NativeReservation>(NativeStatus::InvalidHandle);
    return state_->Reserve(state_,NativeOwnerRole::Listener,i,no_index,ns.namespace_index_);
}
NativeResult<NativeReservation> NativeOwnerTable::ReserveDial(const NamespaceLease& ns,const RouteKey& key) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(!ns.state_) return NativeFailure<NativeReservation>(NativeStatus::InvalidHandle);
    if(ns.state_!=state_) return NativeFailure<NativeReservation>(NativeStatus::ForeignState);
    if(state_->sealed) return NativeFailure<NativeReservation>(NativeStatus::ServiceClosing);
    const auto route=NativeFindRoute(state_->config.directory,key);
    if(route==no_index) return NativeFailure<NativeReservation>(NativeStatus::NotFound);
    const auto local=NativeFindLocal(state_->config.directory,key.local_locator);
    if(state_->local_namespaces[local]!=ns.namespace_index_) return NativeFailure<NativeReservation>(NativeStatus::InvalidHandle);
    if(state_->config.directions[route]==RouteDirection::Accept) return NativeFailure<NativeReservation>(NativeStatus::DirectionMismatch);
    return state_->Reserve(state_,NativeOwnerRole::Dial,local,route,ns.namespace_index_);
}
NativeResult<ListenCallTicket> NativeOwnerTable::BeginListen(const NativeOwnerLease& owner) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    auto status=state_->CheckOwner(owner);
    if(status==NativeStatus::None) status=state_->BeginSetup(owner.slot_,NativeCallKind::Listen);
    if(status!=NativeStatus::None) return NativeFailure<ListenCallTicket>(status);
    ListenCallTicket ticket;state_->Mint(ticket.token_,state_,owner.slot_,state_->FreeOperation(NativeCallKind::Listen),NativeCallKind::Listen);
    return {std::move(ticket),{}};
}
NativeResult<BindCallTicket> NativeOwnerTable::BeginBind(const NativeOwnerLease& owner) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    auto status=state_->CheckOwner(owner);
    if(status==NativeStatus::None) status=state_->BeginSetup(owner.slot_,NativeCallKind::Bind);
    if(status!=NativeStatus::None) return NativeFailure<BindCallTicket>(status);
    BindCallTicket ticket;state_->Mint(ticket.token_,state_,owner.slot_,state_->FreeOperation(NativeCallKind::Bind),NativeCallKind::Bind);
    state_->owners[owner.slot_].facts.phase=NativeOwnerPhase::Binding;
    return {std::move(ticket),{}};
}
NativeInboundResult NativeOwnerTable::ObserveInbound(const NamespaceLease& ns,NativeSocketId id,const ObservedPeerKey& peer) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(!ns.state_) return {NativeStatus::InvalidHandle};
    if(ns.state_!=state_) return {NativeStatus::ForeignState};
    if(state_->sealed) return {NativeStatus::ServiceClosing};
    if(id<0) return {NativeStatus::InvalidResult};
    if(ns.namespace_index_>=state_->namespace_count) return {NativeStatus::InvalidHandle};
    const auto& config=state_->config;
    const auto& name=config.directory.locals[state_->namespace_locals[ns.namespace_index_]].callback_namespace;
    NativeStatus status=NativeStatus::None;std::size_t route=no_index;
    if(!NativeBytesValid(peer.callback_namespace,255)||!NativeBytesValid(peer.remote_network_id,64)||
            !NativeBytesValid(peer.actual_remote_socket_name,255)||peer.callback_namespace!=name) status=NativeStatus::UnknownPeer;
    else if(peer.actual_data_type!=RecordDataType::Bytes) status=NativeStatus::UnsupportedDataType;
    else {
        for(std::size_t i=0;i<config.directory.routes.size();++i) {
            const auto& r=config.directory.routes[i];
            const auto local=NativeFindLocal(config.directory,r.key.local_locator);
            if(state_->local_namespaces[local]!=ns.namespace_index_||r.fields.remote_network_id!=peer.remote_network_id) continue;
            if(std::find(r.fields.expected_remote_initiator_names.begin(),r.fields.expected_remote_initiator_names.end(),
                    peer.actual_remote_socket_name)!=r.fields.expected_remote_initiator_names.end()) {route=i;break;}
        }
        if(route==no_index) status=NativeStatus::UnknownPeer;
        else if(config.directions[route]==RouteDirection::Dial) status=NativeStatus::DirectionMismatch;
    }
    const auto known=state_->FindId(id);
    if(known!=no_index) {
        const auto& cell=state_->owners[known];const auto& f=cell.facts;
        if(status==NativeStatus::None&&cell.ns==ns.namespace_index_&&cell.route==route&&f.role==NativeOwnerRole::Accepted&&
                f.phase==NativeOwnerPhase::Ready&&!f.close_intent)
            return {NativeStatus::None,NativeOwnerLease(state_,known,f.incarnation),true,false};
        // An observed numeric ID never transfers an existing owner's rights.
        return {NativeStatus::ForeignIdCollision};
    }
    if(status==NativeStatus::None&&state_->FindPlan(NativeOwnerRole::Accepted,route)!=no_index) status=NativeStatus::DuplicateConnection;
    const auto slot=state_->FreeOwner();
    if(slot==no_index) {
        state_->sealed=true;state_->seal_reason=NativeStatus::AdmissionSaturated;
        return {NativeStatus::AdmissionSaturated,std::nullopt,false,true};
    }
    if(!state_->IdentitiesAvailable(1)) {
        state_->sealed=true;state_->seal_reason=NativeStatus::IdentityExhausted;
        return {NativeStatus::IdentityExhausted,std::nullopt,false,true};
    }
    const bool accepted=status==NativeStatus::None;
    const auto local=route==no_index?no_index:NativeFindLocal(config.directory,config.directory.routes[route].key.local_locator);
    state_->CreateOwner(slot,accepted?NativeOwnerRole::Accepted:NativeOwnerRole::Rejected,local,accepted?route:no_index,ns.namespace_index_);
    auto& f=state_->owners[slot].facts;f.owned_id=id;
    f.phase=accepted?NativeOwnerPhase::Ready:NativeOwnerPhase::ClosePending;
    f.close_intent=!accepted;f.failure=status;
    return {status,NativeOwnerLease(state_,slot,f.incarnation),accepted,false};
}
NativeResult<SendConnectionSnapshot> NativeOwnerTable::TryResolve(const RouteKey& key,std::uint64_t version) const noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->sealed) return NativeFailure<SendConnectionSnapshot>(NativeStatus::ServiceClosing);
    const auto route=NativeFindRoute(state_->config.directory,key);
    if(route==no_index) return NativeFailure<SendConnectionSnapshot>(NativeStatus::NotFound);
    if(state_->config.directory.routes[route].route_version!=version) return NativeFailure<SendConnectionSnapshot>(NativeStatus::StaleVersion);
    const auto owner=state_->ReadyRouteOwner(route);
    if(owner==no_index||state_->owners[owner].facts.close_intent||state_->owners[owner].facts.phase!=NativeOwnerPhase::Ready)
        return NativeFailure<SendConnectionSnapshot>(NativeStatus::NotReady);
    return {SendConnectionSnapshot{key,version,state_->owners[owner].facts.generation},{}};
}
void NativeOwnerTable::RequestServiceClose() noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->sealed=true;if(state_->seal_reason==NativeStatus::None) state_->seal_reason=NativeStatus::ServiceClosing;
    for(std::size_t i=0;i<state_->owners.size();++i) if(state_->owners[i].used) state_->CloseOwner(i);
}
NativeUsageSnapshot NativeOwnerTable::SnapshotUsage() const noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);NativeUsageSnapshot u;
    u.namespaces=state_->namespace_count;u.owners=state_->owner_count;u.callbacks=state_->callbacks;u.receives=state_->receives;
    u.owner_high_water=state_->owner_high_water;u.generation_high_water=state_->generation_high_water;
    u.limits=state_->config.limits;u.setup.limit=u.limits.setup_calls;u.send.limit=u.limits.send_calls;u.close.limit=u.limits.close_calls;
    u.admission_sealed=state_->sealed;u.seal_reason=state_->seal_reason;u.entered_context_lost=state_->entered_context_lost;
    for(const auto& owner:state_->owners) if(owner.used) {
        ++u.phases[static_cast<std::size_t>(owner.facts.phase)];if(owner.facts.owned_id) ++u.owned_ids;
    }
    for(const auto& op:state_->operations) {
        auto& counter=op.kind==NativeCallKind::Shutdown?u.close:op.kind==NativeCallKind::Send?u.send:u.setup;
        if(op.phase==detail::NativeOperationPhase::Reserved) ++counter.reserved;
        if(op.phase==detail::NativeOperationPhase::Entered) ++counter.entered;
    }
    return u;
}
NativeResult<detail::NativeSendCallTicket> NativeOwnerTable::TestReserveSend(const NativeOwnerLease& owner,std::uint64_t version,std::uint64_t generation) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);const auto check=state_->CheckOwner(owner);
    if(check!=NativeStatus::None) return NativeFailure<detail::NativeSendCallTicket>(check);
    const auto& f=state_->owners[owner.slot_].facts;
    if(state_->sealed) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::ServiceClosing);
    if(f.phase!=NativeOwnerPhase::Ready||f.close_intent) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::NotReady);
    if(f.route_version!=version) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::StaleVersion);
    if(f.generation!=generation) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::StaleGeneration);
    const auto slot=state_->FreeOperation(NativeCallKind::Send);
    if(slot==no_index||state_->HasOperation(owner.slot_,NativeCallKind::Send)) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::SendBusy);
    if(!state_->IdentitiesAvailable(1)) return NativeFailure<detail::NativeSendCallTicket>(NativeStatus::IdentityExhausted);
    detail::NativeSendCallTicket ticket;state_->Mint(ticket.token_,state_,owner.slot_,slot,NativeCallKind::Send);
    return {std::move(ticket),{}};
}
void NativeOwnerTable::TestSetNextIdentity(std::uint64_t value) noexcept
{ std::lock_guard<std::mutex> lock(state_->gate);state_->next_identity=value; }
void NativeOwnerTable::TestSetCallbackCount(const NativeOwnerLease& owner,std::size_t count) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->CheckOwner(owner)!=NativeStatus::None) return;
    state_->owners[owner.slot_].facts.callback_inflight=count;
    // Test-only counter boundary injection is allowed only without a live lease
    // for this owner. Recompute the total conservatively, without wrapping.
    state_->callbacks=0;
    for(const auto& cell:state_->owners) if(cell.used) {
        const auto remaining=std::numeric_limits<std::size_t>::max()-state_->callbacks;
        state_->callbacks+=std::min(remaining,cell.facts.callback_inflight);
    }
}

NativeOwnerLease::NativeOwnerLease(std::shared_ptr<detail::NativeState> s,std::size_t n,std::uint64_t i) noexcept
    : state_(std::move(s)),slot_(n),incarnation_(i) {}
NativeOwnerLease::operator bool() const noexcept { return static_cast<bool>(state_); }
bool NativeOwnerLease::operator==(const NativeOwnerLease& b) const noexcept
{ return state_==b.state_&&slot_==b.slot_&&incarnation_==b.incarnation_; }
NativeStatus NativeOwnerLease::RequestClose() const noexcept
{
    if(!state_) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);const auto status=state_->CheckOwner(*this);
    if(status==NativeStatus::None) state_->CloseOwner(slot_);
    return status;
}
NativeResult<ShutdownCallTicket> NativeOwnerLease::TryClaimShutdown() const noexcept
{
    if(!state_) return NativeFailure<ShutdownCallTicket>(NativeStatus::InvalidHandle);
    std::lock_guard<std::mutex> lock(state_->gate);const auto status=state_->CheckOwner(*this);
    if(status!=NativeStatus::None) return NativeFailure<ShutdownCallTicket>(status);
    const auto& f=state_->owners[slot_].facts;
    if(f.on_shutdown_seen) return NativeFailure<ShutdownCallTicket>(NativeStatus::ShutdownAlreadyObserved);
    if(f.shutdown_returned) return NativeFailure<ShutdownCallTicket>(NativeStatus::ShutdownAlreadyReturned);
    if(f.shutdown_entered) return NativeFailure<ShutdownCallTicket>(NativeStatus::ShutdownAlreadyEntered);
    if(!f.owned_id) return NativeFailure<ShutdownCallTicket>(f.socket_entered&&!f.socket_returned?NativeStatus::AwaitSocketReturn:NativeStatus::NoOwnedId);
    if(!f.close_intent) return NativeFailure<ShutdownCallTicket>(NativeStatus::WrongState);
    if(f.shutdown_reserved) return NativeFailure<ShutdownCallTicket>(NativeStatus::ExistingPending);
    const auto slot=state_->FreeOperation(NativeCallKind::Shutdown);
    if(slot==no_index) return NativeFailure<ShutdownCallTicket>(NativeStatus::CloseBusy);
    if(!state_->IdentitiesAvailable(1)) return NativeFailure<ShutdownCallTicket>(NativeStatus::IdentityExhausted);
    ShutdownCallTicket ticket;state_->Mint(ticket.token_,state_,slot_,slot,NativeCallKind::Shutdown);
    return {std::move(ticket),{}};
}
NativeOwnerObservation NativeOwnerLease::Observe() const noexcept
{
    if(!state_) return {};
    std::lock_guard<std::mutex> lock(state_->gate);const auto status=state_->CheckOwner(*this);
    if(status!=NativeStatus::None) {NativeOwnerObservation result;result.status=status;return result;}
    return state_->owners[slot_].facts;
}
NamespaceLease::NamespaceLease(std::shared_ptr<detail::NativeState> s,std::size_t n) noexcept
    : state_(std::move(s)),namespace_index_(n) {}
NamespaceLease::operator bool() const noexcept { return static_cast<bool>(state_); }
NativeResult<NativeCallbackLease> NamespaceLease::AdmitCallback(NativeSocketId id) const noexcept
{
    if(!state_) return NativeFailure<NativeCallbackLease>(NativeStatus::InvalidHandle);
    std::lock_guard<std::mutex> lock(state_->gate);
    if(namespace_index_>=state_->namespace_count) return NativeFailure<NativeCallbackLease>(NativeStatus::InvalidHandle);
    const auto slot=state_->FindId(id);
    if(slot==no_index) return NativeFailure<NativeCallbackLease>(NativeStatus::UnknownSocket);
    auto& cell=state_->owners[slot];
    if(cell.ns!=namespace_index_) return NativeFailure<NativeCallbackLease>(NativeStatus::ForeignIdCollision);
    if(state_->callbacks==std::numeric_limits<std::size_t>::max()||cell.facts.callback_inflight==std::numeric_limits<std::size_t>::max())
        return NativeFailure<NativeCallbackLease>(NativeStatus::CounterExhausted);
    ++state_->callbacks;++cell.facts.callback_inflight;
    NativeCallbackLease lease;lease.owner_=NativeOwnerLease(state_,slot,cell.facts.incarnation);
    lease.captured_=cell.facts;lease.active_=true;return {std::move(lease),{}};
}
NativeCallbackLease::NativeCallbackLease(NativeCallbackLease&& other) noexcept
    : owner_(std::move(other.owner_)),captured_(other.captured_),active_(std::exchange(other.active_,false)) {}
NativeCallbackLease::~NativeCallbackLease() noexcept
{
    if(!active_||!owner_.state_) return;
    std::lock_guard<std::mutex> lock(owner_.state_->gate);
    if(owner_.state_->ValidOwner(owner_.slot_,owner_.incarnation_)) {
        --owner_.state_->owners[owner_.slot_].facts.callback_inflight;--owner_.state_->callbacks;
    }
    // Members (and potentially the last State owner) destruct after this lock.
}
NativeStatus NativeCallbackLease::ObserveShutdown() noexcept
{
    if(!active_||!owner_.state_) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(owner_.state_->gate);
    if(!owner_.state_->ValidOwner(owner_.slot_,owner_.incarnation_)) return NativeStatus::StaleHandle;
    owner_.state_->CloseOwner(owner_.slot_,true);return NativeStatus::None;
}
NativeOwnerObservation NativeCallbackLease::CapturedOwner() const noexcept
{ return active_?captured_:NativeOwnerObservation{}; }

NativeResult<NativeIdentityConstraint> NativeCallbackLease::CaptureIdentity() const noexcept
{
    if (!active_ || !owner_.state_) return NativeFailure<NativeIdentityConstraint>(NativeStatus::InvalidHandle);
    auto& state = *owner_.state_; std::lock_guard<std::mutex> lock(state.gate);
    if (!state.ValidOwner(owner_.slot_, owner_.incarnation_))
        return NativeFailure<NativeIdentityConstraint>(NativeStatus::StaleHandle);
    const auto& cell = state.owners[owner_.slot_];
    if (cell.route >= state.config.directory.routes.size())
        return NativeFailure<NativeIdentityConstraint>(NativeStatus::WrongRole);
    NativeIdentityConstraint pin;
    pin.state_ = owner_.state_; pin.slot_ = owner_.slot_; pin.incarnation_ = owner_.incarnation_;
    pin.generation_ = captured_.generation; pin.route_version_ = captured_.route_version;
    const auto& route = state.config.directory.routes[cell.route];
    pin.local_ = route.key.local_locator; pin.remote_ = route.key.remote_locator;
    const auto status = state.CheckReceive(pin);
    if (status != NativeStatus::None && status != NativeStatus::NotReady)
        return NativeFailure<NativeIdentityConstraint>(status);
    return {std::move(pin), {}};
}
NativeOwnerObservation NativeIdentityConstraint::Observe() const noexcept
{
    if (!state_) return {};
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto status = state_->CheckReceive(*this);
    if (!state_->ValidOwner(slot_, incarnation_)) return {status};
    auto result = state_->owners[slot_].facts;
    result.status = status;
    return result;
}

namespace detail {
NativeOperationToken::NativeOperationToken(NativeOperationToken&& other) noexcept
    : state_(std::move(other.state_)),owner_slot_(other.owner_slot_),operation_slot_(other.operation_slot_),
      owner_incarnation_(other.owner_incarnation_),operation_incarnation_(other.operation_incarnation_),kind_(other.kind_),settled_(other.settled_) {}
NativeOperationToken::~NativeOperationToken() noexcept
{
    if(!state_||settled_) return;
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->CheckToken(*this,kind_)!=NativeStatus::None) return;
    auto& op=state_->operations[operation_slot_];
    if(op.phase==NativeOperationPhase::Reserved) state_->ReleaseReserved(operation_slot_);
    else if(op.phase==NativeOperationPhase::Entered) {
        state_->entered_context_lost=true;state_->owners[owner_slot_].facts.entered_context_lost=true;
    }
}
NativeStatus NativeOperationToken::Enter(NativeCallKind kind) noexcept
{
    if(!state_) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);const auto status=state_->CheckToken(*this,kind);
    if(status!=NativeStatus::None) return status;
    auto& op=state_->operations[operation_slot_];auto& f=state_->owners[owner_slot_].facts;
    if(op.phase==NativeOperationPhase::Entered) return NativeStatus::AlreadyEntered;
    // RequestClose/OnShutdown atomically revoke all affected Reserved cells.
    // There is no unprotected Ready check between this gate and model admission.
    op.phase=NativeOperationPhase::Entered;
    if(kind==NativeCallKind::Socket) f.socket_entered=true;
    if(kind==NativeCallKind::Shutdown) {f.shutdown_reserved=false;f.shutdown_entered=true;}
    return NativeStatus::None;
}
NativeCompletion NativeOperationToken::Complete(NativeCallKind kind,bool success,std::int32_t result) noexcept
{
    if(!state_) return {NativeStatus::InvalidHandle};
    std::lock_guard<std::mutex> lock(state_->gate);const auto check=state_->CheckToken(*this,kind);
    if(check!=NativeStatus::None) return {check};
    auto& op=state_->operations[operation_slot_];auto& cell=state_->owners[owner_slot_];auto& f=cell.facts;
    if(op.phase!=NativeOperationPhase::Entered) return {NativeStatus::NotEntered,f};
    const bool valid=kind==NativeCallKind::Socket?(success?result>=0:result<0):(success?result==0:result<0);
    if(!valid) return {NativeStatus::InvalidResult,f};
    NativeStatus status=NativeStatus::None;
    if(kind==NativeCallKind::Socket) {
        f.socket_returned=true;
        if(success) {
            const auto existing=state_->FindId(result);
            if(existing!=no_index&&existing!=owner_slot_) {
                f.failure=NativeStatus::ForeignIdCollision;f.close_intent=true;f.phase=NativeOwnerPhase::FailedRetained;
                status=NativeStatus::ForeignIdCollision;
            } else {f.owned_id=result;f.phase=NativeOwnerPhase::SocketKnown;}
        } else {f.error_code=result;f.failure=NativeStatus::OwnerUnavailable;f.close_intent=true;f.phase=NativeOwnerPhase::FailedRetained;}
    } else if(kind==NativeCallKind::Listen||kind==NativeCallKind::Bind) {
        if(success&&!f.close_intent) f.phase=kind==NativeCallKind::Listen?NativeOwnerPhase::Listening:NativeOwnerPhase::Ready;
        else if(!success) {f.error_code=result;f.failure=NativeStatus::OwnerUnavailable;f.close_intent=true;}
    } else if(kind==NativeCallKind::Shutdown) f.shutdown_returned=true;
    else if(kind==NativeCallKind::Send&&!success) {f.error_code=result;f.failure=NativeStatus::OwnerUnavailable;f.close_intent=true;}
    // Return is the sole refund for an Entered call, even after service/owner
    // close. No allocation or destruction of user-owned objects occurs here.
    op.phase=NativeOperationPhase::Free;settled_=true;
    state_->RefreshStopped(cell);
    return {status,f};
}
NativeStatus NativeOperationToken::Abandon(NativeCallKind kind) noexcept
{
    if(!state_) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);const auto status=state_->CheckToken(*this,kind);
    if(status!=NativeStatus::None) return status;
    if(state_->operations[operation_slot_].phase==NativeOperationPhase::Entered) return NativeStatus::EnteredPending;
    state_->ReleaseReserved(operation_slot_);settled_=true;return NativeStatus::Abandoned;
}
NativeStatus NativeSendCallTicket::CallEntered() noexcept { return token_.Enter(NativeCallKind::Send); }
NativeCompletion NativeSendCallTicket::Complete(std::int32_t code) noexcept { return token_.Complete(NativeCallKind::Send,code==0,code); }
NativeStatus NativeSendCallTicket::Abandon() noexcept { return token_.Abandon(NativeCallKind::Send); }
}
NativeStatus SocketCallTicket::CallEntered() noexcept { return token_.Enter(NativeCallKind::Socket); }
NativeCompletion SocketCallTicket::Complete(NativeSocketResult result) noexcept
{
    if(result.kind!=NativeSocketResult::Kind::Id&&result.kind!=NativeSocketResult::Kind::Error) return {NativeStatus::InvalidResult};
    return token_.Complete(NativeCallKind::Socket,result.kind==NativeSocketResult::Kind::Id,result.value);
}
NativeStatus SocketCallTicket::Abandon() noexcept { return token_.Abandon(NativeCallKind::Socket); }
NativeStatus ListenCallTicket::CallEntered() noexcept { return token_.Enter(NativeCallKind::Listen); }
NativeCompletion ListenCallTicket::Complete(SyncListenResult result) noexcept { return token_.Complete(NativeCallKind::Listen,result.success,result.error); }
NativeStatus ListenCallTicket::Abandon() noexcept { return token_.Abandon(NativeCallKind::Listen); }
NativeStatus BindCallTicket::CallEntered() noexcept { return token_.Enter(NativeCallKind::Bind); }
NativeCompletion BindCallTicket::Complete(SyncBindResult result) noexcept { return token_.Complete(NativeCallKind::Bind,result.success,result.error); }
NativeStatus BindCallTicket::Abandon() noexcept { return token_.Abandon(NativeCallKind::Bind); }
NativeStatus ShutdownCallTicket::CallEntered() noexcept { return token_.Enter(NativeCallKind::Shutdown); }
NativeStatus ShutdownCallTicket::CompleteShutdownReturned() noexcept { return token_.Complete(NativeCallKind::Shutdown,true,0).status; }
NativeStatus ShutdownCallTicket::Abandon() noexcept { return token_.Abandon(NativeCallKind::Shutdown); }
} // namespace mdds::dsoftbus
