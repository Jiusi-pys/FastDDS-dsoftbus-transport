#include "ExperimentalTransport.hpp"
#include "PerformanceObservation.hpp"
#include <fastdds_dsoftbus/dsoftbus/ProcessCoordinator.hpp>
#include <map>
#include <sstream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <thread>
#include <utility>
#include <vector>

namespace fastdds_dsoftbus::experiment {
using Clock = std::chrono::steady_clock;
using Bytes = std::vector<std::uint8_t>;
using Codec = dsoftbus::v1::AddressCodec;
namespace model = dsoftbus;
namespace {
constexpr std::size_t records_limit = 64, bytes_limit = 1024 * 1024;
constexpr std::size_t peer_limit=2;
std::size_t PeerCount(const Config& c){return 1+c.additional_peers.size();}
const Locator& PeerLocator(const Config& c,std::size_t i){return i?c.additional_peers[i-1].locator:c.peer_locator;}
const std::string& PeerService(const Config& c,std::size_t i){return i?c.additional_peers[i-1].service:c.peer_service;}
const std::string& PeerNetwork(const Config& c,std::size_t i){return i?c.additional_peers[i-1].network_id:c.peer_network_id;}
std::size_t PeerIndex(const Config& c,const Locator& locator){for(std::size_t i=0;i<PeerCount(c);++i)if(PeerLocator(c,i)==locator)return i;return peer_limit;}
constexpr std::size_t DialSlot(std::size_t peer){return 1+2*peer;}
constexpr std::size_t AcceptedSlot(std::size_t peer){return 2+2*peer;}
constexpr std::string_view native_prefix="com.kaihong.mdds.";
enum class Role { Listener, Outbound, Accepted };
const char* RoleName(Role r) { return r == Role::Listener ? "listener" : r == Role::Outbound ? "outbound" : "accepted"; }
std::array<char,33> Hex16(const std::uint8_t* p) {
    std::array<char,33> out{};constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<16;++i){out[2*i]=hex[p[i]>>4];out[2*i+1]=hex[p[i]&15];}return out;
}
std::uint64_t Hash(const void* ptr, std::size_t n) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(ptr);
    std::uint64_t h = 14695981039346656037ULL;
    for (std::size_t i=0; i<n; ++i) { h ^= bytes[i]; h *= 1099511628211ULL; }
    return h;
}
// Each real entry function chooses one service-record/role incarnation before
// inspecting the SDK numeric socket. This index is adapter identity only; it
// does not invent a generation/context in the public callback signature.
struct ProcessRegistry {
    const Clock::time_point start=Clock::now();
    struct Entry { std::shared_ptr<State> state; bool exposed=false, retired=false; };
    struct Service {
        std::shared_ptr<State> state;
        std::shared_ptr<Control> control;
        std::string name;
        bool active=false;
    };
    struct Owner { int socket=-1; State* state=nullptr; };
    std::mutex mutex;
    std::array<Entry,ProcessStats::role_entry_limit> server{},client{};
    std::array<std::atomic<State*>,ProcessStats::role_entry_limit> server_ingress{},client_ingress{};
    std::array<std::atomic<std::uint64_t>,2*ProcessStats::role_entry_limit> missing_rx_state{};
    std::array<Service,ProcessStats::role_entry_limit> services{};
    std::array<Owner,ProcessStats::native_owner_limit> owners{};
    std::uint64_t next_instance=0;
};
ProcessRegistry& Registry() { static auto* registry=new ProcessRegistry; return *registry; }
void FlushProcessRxAdmissions();
long long ProcessSteadyUs() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-Registry().start).count(); }
bool ReserveInstance(const std::shared_ptr<State>& state,const Config& config,
        const std::shared_ptr<Control>& control,int& service,int& server,int& client,std::uint64_t& identity) {
    auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
    std::size_t active=0;
    for(const auto& record:registry.services) if(record.state) {
        active+=record.active;
        // Retired same-name recreation is not this experiment's topology.
        // Never infer SDK record continuity from equal service-name text.
        if(record.name==config.local_service || record.control==control) return false;
    }
    if(active==ProcessStats::state_limit) return false;
    service=server=client=-1;
    for(std::size_t i=0;i<registry.services.size();++i)if(!registry.services[i].state){service=static_cast<int>(i);break;}
    for(std::size_t i=0;i<registry.server.size();++i)if(!registry.server[i].state){server=static_cast<int>(i);break;}
    for(std::size_t i=0;i<registry.client.size();++i)if(!registry.client[i].state){client=static_cast<int>(i);break;}
    if(service<0||server<0||client<0)return false;
    // Finish potentially throwing name allocation before publishing any
    // reservation. No SDK side effect occurs before this reservation succeeds.
    ProcessRegistry::Service record{state,control,config.local_service,true};
    registry.services[service]=std::move(record);
    registry.server[server].state=state;registry.client[client].state=state;
    identity=++registry.next_instance;return true;
}
void ExposeEntry(State* state,int slot,bool server) {
    auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
    auto& entry=server?registry.server[slot]:registry.client[slot];
    // Repeated registration on the same live record/role preserves this entry.
    if(entry.state.get()!=state||entry.retired)return;
    entry.exposed=true;
    (server?registry.server_ingress[slot]:registry.client_ingress[slot]).store(state,std::memory_order_release);
}
bool ClaimNativeOwner(State* state,int socket) {
    auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
    for(const auto& owner:registry.owners)if(owner.socket==socket)return false;
    for(auto& owner:registry.owners)if(!owner.state){owner={socket,state};return true;}
    return false;
}
void ReleaseNativeOwner(State* state,int socket) {
    auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
    for(auto& owner:registry.owners)if(owner.socket==socket&&owner.state==state){owner={};return;}
}
void RetireInstance(State* state,int service,int server,int client) {
    auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
    if(service<0||registry.services[service].state.get()!=state)return;
    bool exposed=false;
    for(bool is_server:{true,false}) {
        auto& entry=is_server?registry.server[server]:registry.client[client];
        if(entry.exposed){entry.retired=true;exposed=true;}
        else entry={}; // An entry never passed to SDK can be reserved again.
    }
    if(exposed)registry.services[service].active=false;
    else registry.services[service]={};
}
bool DecodeNativeServiceName(std::string_view name,dsoftbus::v1::ServiceAddress& output) {
    if(name.size()>Codec::max_service_name_bytes||name.substr(0,native_prefix.size())!=native_prefix)return false;
    return Codec::DecodeServiceName(name.substr(native_prefix.size()),output);
}
}

bool EncodeNativeServiceName(const dsoftbus::v1::ServiceAddress& address,std::string& output) {
    std::string canonical;
    if(!Codec::EncodeServiceName(address,canonical)||canonical.size()+native_prefix.size()>Codec::max_service_name_bytes)return false;
    std::string native(native_prefix);native+=canonical;output=std::move(native);return true;
}

RtpsObservation ObserveRtps(const void* pointer, std::size_t size) noexcept
{
    try {
    RtpsObservation out;
    if (!pointer || size < 20) return out;
    const auto* b = static_cast<const std::uint8_t*>(pointer);
    if (std::memcmp(b, "RTPS", 4) || b[4] != 2) return out;
    std::array<std::uint8_t,12> prefix{}, destination{};
    std::copy(b+8,b+20,prefix.begin());
    std::size_t offset=20;
    bool found_writer=false, shape=true, destination_known=false;
    while (offset < size) {
        if (size-offset < 4) return {};
        const bool little=(b[offset+1]&1)!=0;
        const auto length = little ? unsigned(b[offset+2]) | (unsigned(b[offset+3])<<8) :
                (unsigned(b[offset+2])<<8) | unsigned(b[offset+3]);
        const auto body = offset+4;
        // RTPS zero length consumes the remainder except for PAD and INFO_TS,
        // whose empty bodies leave following submessages available. Match the
        // Fast DDS 3.6.2 MessageReceiver::readSubmessageHeader boundary rule.
        const bool consumes_remainder = length == 0 && b[offset] != 0x01 && b[offset] != 0x09;
        const auto end = consumes_remainder ? size : body+length;
        if (end > size) return {};
        const auto read16=[&](std::size_t p) -> std::uint16_t {
            return static_cast<std::uint16_t>(little ? unsigned(b[p]) | (unsigned(b[p+1])<<8) :
                    (unsigned(b[p])<<8) | unsigned(b[p+1]));
        };
        const auto read32=[&](std::size_t p) -> std::uint32_t {
            std::uint32_t value=0;
            for(unsigned i=0;i<4;++i) value|=std::uint32_t(b[p+i])<<(8*(little?i:3-i));
            return value;
        };
        const auto signed32=[](std::uint32_t high) -> std::int32_t {
            return static_cast<std::int32_t>((high&0x80000000u) ?
                    static_cast<std::int64_t>(high)-0x100000000LL : high);
        };
        // This fixed Fast DDS build appends one 56-byte eProsima statistics
        // body on flush. Keep it inside the observed whole packet; never strip
        // it or generalize arbitrary vendor submessages into a drop candidate.
        const bool statistics=b[offset]==0x80 && b[6]==0x01 && b[7]==0x0f;
        const bool known_terminal_statistics=statistics && end-body==56 && end==size && out.statistics_submessages==0;
        if(statistics) { ++out.statistics_submessages; out.statistics_body_bytes=end-body; }
        if(b[offset]!=0x01 && b[offset]!=0x09 && b[offset]!=0x0e && b[offset]!=0x16 && !known_terminal_statistics) shape=false;
        if(b[offset]==0x09 && end-body!=((b[offset+1]&2)?0u:8u)) shape=false;
        if (b[offset] == 0x0c) { // INFO_SRC replaces the source prefix for following submessages.
            if (end-body < 20) return {};
            std::copy(b+body+8,b+body+20,prefix.begin());
        } else if(b[offset]==0x0e) {
            if(end-body!=12) return {};
            // As in Fast DDS 3.6.2 proc_Submsg_InfoDST, UNKNOWN does not
            // overwrite an earlier destination. With none present, stay unknown.
            if(std::any_of(b+body,b+end,[](auto value){return value!=0;})) {
                std::copy(b+body,b+end,destination.begin()); destination_known=true;
            }
        } else if (b[offset] == 0x15 || b[offset] == 0x16) {
            if (end-body < (b[offset] == 0x15 ? 20u : 32u)) return {};
            if(b[offset]==0x15) ++out.data_submessages;
            const auto* w=b+body+8;
            if(!found_writer){std::copy(prefix.begin(),prefix.end(),out.first_writer_guid.begin());std::copy(w,w+4,out.first_writer_guid.begin()+12);found_writer=true;}
            const std::uint32_t id=(std::uint32_t(w[0])<<24)|(std::uint32_t(w[1])<<16)|(std::uint32_t(w[2])<<8)|w[3];
            if (id==0x000100c2) ++out.spdp;
            else if (id==0x000003c2) { ++out.sedp; ++out.sedp_publications; }
            else if (id==0x000004c2) { ++out.sedp; ++out.sedp_subscriptions; }
            else if (w[3]==0x02 || w[3]==0x03) ++out.user;
            if(b[offset]==0x15){
                DataObservation data;
                std::copy(prefix.begin(),prefix.end(),data.writer_guid.begin());
                std::copy(w,w+4,data.writer_guid.begin()+12);
                data.sequence_hi=signed32(read32(body+12));data.sequence_lo=read32(body+16);
                data.submessage_offset=offset;data.submessage_body_bytes=end-body;
                out.data.push_back(data);
            }
            if (b[offset] == 0x16) {
                // Fixed RTPS DATA_FRAG fields, before optional inline QoS and
                // fragment payload. sample_size is serializedPayload.length,
                // not application payload bytes or this native message length.
                FragmentObservation fragment;
                std::copy(prefix.begin(),prefix.end(),fragment.writer_guid.begin());
                std::copy(w,w+4,fragment.writer_guid.begin()+12);
                const auto high=read32(body+12);
                fragment.sequence_hi=static_cast<std::int32_t>((high&0x80000000u) ?
                        static_cast<std::int64_t>(high)-0x100000000LL : high);
                fragment.sequence_lo=read32(body+16);
                fragment.fragment_start=read32(body+20);
                fragment.fragment_count=read16(body+24);
                fragment.fragment_size=read16(body+26);
                fragment.sample_size=read32(body+28);
                // Retain raw fields even for semantically invalid input. Empty
                // fragment_count has no inclusive range; report end=0. The DDS
                // receiver remains responsible for protocol validation.
                fragment.fragment_end=fragment.fragment_count ?
                        std::uint64_t(fragment.fragment_start)+fragment.fragment_count-1 : 0;
                fragment.submessage_offset=offset;
                fragment.submessage_body_bytes=end-body;
                fragment.inline_qos=(b[offset+1]&2)!=0;
                fragment.key_flag=(b[offset+1]&4)!=0;
                fragment.octets_to_inline_qos=read16(body+2);
                if(fragment.octets_to_inline_qos>=28 && fragment.octets_to_inline_qos<=end-body-4) {
                    auto payload=body+4+fragment.octets_to_inline_qos;
                    bool known=!fragment.inline_qos;
                    // Only locate the inline-QoS sentinel using bounded PID,
                    // length and four-byte alignment. Parameter meaning and
                    // application decoding remain solely with Fast DDS.
                    while(fragment.inline_qos && end-payload>=4) {
                        const auto parameter=read16(payload),parameter_length=read16(payload+2);
                        payload+=4;
                        if(parameter==1) { known=parameter_length==0; break; }
                        const auto aligned=(std::size_t(parameter_length)+3)&~std::size_t(3);
                        if(aligned>end-payload) break;
                        payload+=aligned;
                    }
                    if(known) {
                        fragment.payload_bounds_known=true; fragment.payload_offset=payload;
                        fragment.payload_bytes=end-payload;
                    }
                }
                if(!fragment.payload_bounds_known || fragment.key_flag) shape=false;
                // At most one entry per >=36 native bytes. No sample-size-
                // based allocation, payload copy, reconstruction or filtering.
                out.fragments.push_back(fragment);
            }
        } else if(b[offset]==0x06 || b[offset]==0x07 || b[offset]==0x12 || b[offset]==0x13) {
            const auto kind=b[offset];
            const auto required=(kind==0x06 || kind==0x13) ? 24u : 28u;
            if(end-body<required) return {};
            ReliabilityControlObservation control;
            control.kind=kind; control.source_prefix=prefix; control.destination_prefix=destination;
            control.destination_prefix_known=destination_known; control.submessage_offset=offset;
            std::copy(b+body,b+body+4,control.reader_id.begin());
            std::copy(b+body+4,b+body+8,control.writer_id.begin());
            const bool reader_origin=kind==0x06 || kind==0x12;
            const bool source_known=std::any_of(prefix.begin(),prefix.end(),[](auto value){return value!=0;});
            control.reader_prefix_known=reader_origin?source_known:destination_known;
            control.writer_prefix_known=reader_origin?destination_known:source_known;
            const auto& reader_prefix=reader_origin?prefix:destination;
            const auto& writer_prefix=reader_origin?destination:prefix;
            std::copy(reader_prefix.begin(),reader_prefix.end(),control.reader_guid.begin());
            std::copy(writer_prefix.begin(),writer_prefix.end(),control.writer_guid.begin());
            std::copy(control.reader_id.begin(),control.reader_id.end(),control.reader_guid.begin()+12);
            std::copy(control.writer_id.begin(),control.writer_id.end(),control.writer_guid.begin()+12);
            control.sequence_hi=signed32(read32(body+8)); control.sequence_lo=read32(body+12);
            // Only ACKNACK and HEARTBEAT define Final; NACK_FRAG's bit1
            // is reserved, and is not evidence of any final acknowledgment.
            control.final_flag=(kind==0x06 || kind==0x07) && (b[offset+1]&2);
            control.liveliness_flag=kind==0x07 && (b[offset+1]&4);
            if(reader_origin) {
                const auto bits_offset=body+(kind==0x12?20:16);
                if(kind==0x12) control.fragment_base=read32(body+16);
                control.num_bits=read32(bits_offset);
                if(control.num_bits>256) return {};
                const auto words=(control.num_bits+31)/32;
                if(end-body!=required+4*words) return {};
                for(std::uint32_t i=0;i<words;++i) control.bitmap[i]=read32(bits_offset+4+4*i);
                control.count=read32(bits_offset+4+4*words);
            } else if(kind==0x07) {
                if(end-body!=28) return {};
                control.last_sequence_hi=signed32(read32(body+16)); control.last_sequence_lo=read32(body+20);
                control.count=read32(body+24);
            } else {
                if(end-body!=24) return {};
                control.last_fragment=read32(body+16); control.count=read32(body+20);
            }
            // Observation only: retain raw bases/bitmap/counts. Receiver owns
            // semantic validity and acknowledgment/retransmission decisions.
            out.controls.push_back(control);
        }
        offset=end;
        // MessageReceiver advances a nonterminal body to the next 4-byte
        // submessage-header boundary; padding is not a separate submessage.
        if (offset<size) offset+=(4-(offset&3))&3;
    }
    out.valid=true;
    out.drop_shape_candidate=shape && out.fragments.size()==1 && out.data_submessages==0;
    return out;
    } catch (...) {
        // Evidence collection must not throw into or control the data path.
        return {};
    }
}

Stats Control::Snapshot() const {
    Stats result;std::shared_ptr<Control> native;
    {std::lock_guard<std::mutex> lock(mutex_);result=stats_;native=native_control_;}
    if(native){const auto n=native->Snapshot();result.instance_id=n.instance_id;result.server_callback_slot=n.server_callback_slot;result.client_callback_slot=n.client_callback_slot;
        result.listener_creations=n.listener_creations;result.native_owners=n.native_owners;result.shutdown_returns=n.shutdown_returns;
        result.sdk_calls_active=n.sdk_calls_active;result.callbacks_active=n.callbacks_active;result.peer_shutdown_events=n.peer_shutdown_events;
        result.no_app_drops=n.no_app_drops;result.rx_drops=n.rx_drops;result.unexpected_callbacks=n.unexpected_callbacks;result.peer_errors=n.peer_errors;
        result.tx_worker_joined=n.tx_worker_joined;result.rx_worker_joined=n.rx_worker_joined;result.closed=n.closed;result.fatal=result.fatal||n.fatal;
        result.clean_observed=result.app_clean_observed&&!result.fatal&&(n.closed?n.clean_observed:true);}
    return result;
}
ProcessStats GetProcessStats() {
    ProcessStats result;
    std::array<std::shared_ptr<Control>,ProcessStats::state_limit> controls{};
    std::size_t count=0;
    {
        auto& registry=Registry();std::lock_guard<std::mutex> lock(registry.mutex);
        for(const auto& service:registry.services)if(service.active)controls[count++]=service.control;
        result.active_states=count;result.reserved_workers=count*2;
        for(const auto& owner:registry.owners)result.native_owners+=owner.state!=nullptr;
        for(bool server:{true,false})for(const auto& entry:server?registry.server:registry.client) {
            result.free_entries+=!entry.state;
            result.exposed_entries+=entry.exposed;
            result.tombstone_entries+=entry.exposed&&entry.retired;
        }
    }
    for(const auto& control:controls)if(control) {
        const auto stats=control->Snapshot();
        result.tx_records+=stats.tx_records;result.tx_bytes+=stats.tx_bytes;
        result.rx_records+=stats.rx_records;result.rx_bytes+=stats.rx_bytes;
        result.live_tx_blocks+=stats.live_tx_blocks;result.live_tx_payload_bytes+=stats.live_tx_payload_bytes;
        result.live_rx_blocks+=stats.live_rx_blocks;result.live_rx_payload_bytes+=stats.live_rx_payload_bytes;
    }
    return result;
}
void PrintProcessSummary(const char* stage) {
    FlushProcessRxAdmissions();
    const auto s=GetProcessStats();
    std::printf("EXPERIMENT_PROCESS_BUDGET stage=%s active_states=%llu reserved_workers=%llu native_owners=%llu tx_records=%llu tx_bytes=%llu rx_records=%llu rx_bytes=%llu live_tx_blocks=%llu live_tx_payload_bytes=%llu live_rx_blocks=%llu live_rx_payload_bytes=%llu exposed_entries=%llu tombstone_entries=%llu free_entries=%llu state_limit=2 worker_limit=4 owner_limit=6 per_direction_record_limit=128 per_direction_byte_limit=2097152 entry_limit=16 snapshot=AGGREGATED_NOT_ATOMIC sdk_memory=UNOBSERVED\n",
        stage?stage:"snapshot",static_cast<unsigned long long>(s.active_states),static_cast<unsigned long long>(s.reserved_workers),
        static_cast<unsigned long long>(s.native_owners),static_cast<unsigned long long>(s.tx_records),static_cast<unsigned long long>(s.tx_bytes),
        static_cast<unsigned long long>(s.rx_records),static_cast<unsigned long long>(s.rx_bytes),
        static_cast<unsigned long long>(s.live_tx_blocks),static_cast<unsigned long long>(s.live_tx_payload_bytes),
        static_cast<unsigned long long>(s.live_rx_blocks),static_cast<unsigned long long>(s.live_rx_payload_bytes),
        static_cast<unsigned long long>(s.exposed_entries),static_cast<unsigned long long>(s.tombstone_entries),static_cast<unsigned long long>(s.free_entries));
    std::fflush(stdout);
}
bool Control::ArmRxDrop(const RxDropRule& rule) noexcept {
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    try {
        if(rule.sequence_hi<0 || (rule.sequence_hi==0&&rule.sequence_lo==0) || !rule.fragment_start ||
                rule.fragment_count!=1 || !rule.sample_size || rule.sample_size>1048640 ||
                (rule.writer_guid[15]!=0x02&&rule.writer_guid[15]!=0x03)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!rx_drop_enabled_ || rx_drop_closed_ || rx_drop_.armed) return false;
        rx_drop_.rule=rule;rx_drop_.armed=true;return true;
    } catch (...) { return false; }
#else
    (void)rule;return false;
#endif
}
RxDropSnapshot Control::DropSnapshot() const { std::lock_guard<std::mutex> lock(mutex_); return rx_drop_; }
void Control::PrintSummary() const
{
    const auto s=Snapshot();
    std::string name;{std::lock_guard<std::mutex> lock(mutex_);name=instance_name_;}
    std::printf("EXPERIMENT_UNQUALIFIED_SUMMARY tx_accepted=%llu tx_returns=%llu tx_fail=%llu rx_owned=%llu receiver_calls=%llu receiver_returns=%llu tx_spdp=%llu tx_sedp_pub=%llu tx_sedp_sub=%llu tx_user=%llu rx_spdp=%llu rx_sedp_pub=%llu rx_sedp_sub=%llu rx_user=%llu owners=%llu shutdown_returns=%llu peer_shutdown=%llu sdk_active=%llu callbacks_active=%llu leases=%llu tx_records=%llu tx_bytes=%llu rx_records=%llu rx_bytes=%llu tx_join=%d rx_join=%d closed=%d clean_observed=%d fatal=%d native_internal_settlement=UNOBSERVED instance_name=%s instance_id=%llu server_callback_slot=%d client_callback_slot=%d\n",
        static_cast<unsigned long long>(s.tx_accepted),static_cast<unsigned long long>(s.tx_send_returns),static_cast<unsigned long long>(s.tx_send_failures),
        static_cast<unsigned long long>(s.rx_owned),static_cast<unsigned long long>(s.receiver_calls),static_cast<unsigned long long>(s.receiver_returns),
        static_cast<unsigned long long>(s.tx_spdp),static_cast<unsigned long long>(s.tx_sedp_publications),static_cast<unsigned long long>(s.tx_sedp_subscriptions),static_cast<unsigned long long>(s.tx_user),
        static_cast<unsigned long long>(s.rx_spdp),static_cast<unsigned long long>(s.rx_sedp_publications),static_cast<unsigned long long>(s.rx_sedp_subscriptions),static_cast<unsigned long long>(s.rx_user),
        static_cast<unsigned long long>(s.native_owners),static_cast<unsigned long long>(s.shutdown_returns),static_cast<unsigned long long>(s.peer_shutdown_events),
        static_cast<unsigned long long>(s.sdk_calls_active),static_cast<unsigned long long>(s.callbacks_active),static_cast<unsigned long long>(s.receiver_leases),
        static_cast<unsigned long long>(s.tx_records),static_cast<unsigned long long>(s.tx_bytes),static_cast<unsigned long long>(s.rx_records),static_cast<unsigned long long>(s.rx_bytes),
        s.tx_worker_joined,s.rx_worker_joined,s.closed,s.clean_observed,s.fatal,name.c_str(),static_cast<unsigned long long>(s.instance_id),s.server_callback_slot,s.client_callback_slot);
    std::printf("EXPERIMENT_PAYLOAD_LIFETIME live_tx_blocks=%llu live_tx_payload_bytes=%llu live_rx_blocks=%llu live_rx_payload_bytes=%llu task_bytes=RESERVED_TASK_PAYLOAD_BUDGET allocation_strategy=PER_MESSAGE_VECTOR sdk_memory=UNOBSERVED instance_name=%s instance_id=%llu\n",
        static_cast<unsigned long long>(s.live_tx_blocks),static_cast<unsigned long long>(s.live_tx_payload_bytes),
        static_cast<unsigned long long>(s.live_rx_blocks),static_cast<unsigned long long>(s.live_rx_payload_bytes),name.c_str(),static_cast<unsigned long long>(s.instance_id));
    std::printf("EXPERIMENT_FRAGMENT_COUNTS tx_data_frag=%llu rx_data_frag=%llu tx_basis=SDK_SUCCESSFUL_SEND rx_basis=RECEIVER_RETURN unit=OBSERVED_SUBMESSAGES instance_name=%s instance_id=%llu\n",
        static_cast<unsigned long long>(s.tx_data_frag),static_cast<unsigned long long>(s.rx_data_frag),name.c_str(),static_cast<unsigned long long>(s.instance_id));
    std::printf("EXPERIMENT_TX_DIAGNOSTICS post_admission_errors=%llu instance_name=%s instance_id=%llu\n",static_cast<unsigned long long>(s.tx_diagnostic_errors),name.c_str(),static_cast<unsigned long long>(s.instance_id));
    std::fflush(stdout);
}

struct RuntimeCore;
struct Application final : rtps::TransportReceiverInterface {
    std::weak_ptr<RuntimeCore> runtime;
    State* state=nullptr;
    const std::shared_ptr<Control> control;
    model::ProcessApplication claim;
    model::ReceiverBinding binding;
    rtps::TransportReceiverInterface* receiver=nullptr;
    bool detaching=false,detached=false;
    std::size_t submits=0,tx_records=0;
    std::uint64_t id=0,token=0;
    explicit Application(std::shared_ptr<Control> c):control(std::move(c)){}
    template<class F> void Update(F f){std::lock_guard<std::mutex> lock(control->mutex_);f(control->stats_);}
    void OnDataReceived(const std::uint8_t*,std::uint32_t,const Locator&,const Locator&) override;
};
struct RuntimeObserver final:model::ProcessReceiveObserver {
    std::weak_ptr<RuntimeCore> core;
    void OnReceive(model::ReceiveStage,const model::ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override;
};
struct RuntimeCore final:std::enable_shared_from_this<RuntimeCore> {
    const std::uint64_t id;
    const std::vector<Config> configs;
    const SocketApi api;
    model::NativeTableConfiguration native_config;
    std::unique_ptr<model::ProcessCoordinator> process;
    std::shared_ptr<RuntimeObserver> observer;
    std::vector<std::shared_ptr<State>> states;
    mutable std::mutex gate;
    std::condition_variable changed;
    std::map<std::uint64_t,std::weak_ptr<Application>> apps;
    struct RxAccounting {std::shared_ptr<Application> app;bool settled=false,admitted=false;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,RxAccounting> rx_accounting;
    std::vector<bool> attaching;
    bool stopping=false,stopped=false,stop_result=false;
    RuntimeCore(std::uint64_t i,std::vector<Config> c,SocketApi a):id(i),configs(std::move(c)),api(a){}
    bool Build();
    std::shared_ptr<Application> Attach(const Config&,const std::shared_ptr<Control>&);
    void Detach(const std::shared_ptr<Application>&);
    bool Stop();
    RuntimeStats Snapshot()const;
    std::shared_ptr<Application> FindApp(std::uint64_t);
};
struct ByteSpan { const std::uint8_t* pointer;std::size_t length;const std::uint8_t* data()const{return pointer;}std::size_t size()const{return length;} };
thread_local model::ReceiveDiagnostic current_receive;

struct State:std::enable_shared_from_this<State> {
    const Config config;
    const SocketApi api;
    const std::shared_ptr<Control> control;
    std::weak_ptr<RuntimeCore> runtime;
    model::NamespaceLease native_namespace;
    Locator model_local;
    std::array<Locator,peer_limit> model_peers{};
    std::shared_ptr<Application> application;
    std::weak_ptr<Control> last_detached_control;
    const Clock::time_point start=Clock::now();
    std::atomic<std::uint64_t> invocation{0};
    std::uint64_t instance_id=0;
    int service_record=-1, server_slot=-1, client_slot=-1;
    mutable std::mutex gate;
    std::condition_variable changed;
    bool initialized=false, closing=false, closed=false, retired=false, input_closing=false;
    std::array<bool,peer_limit> ever_ready{};
    struct Owner { int id=-1; Role role=Role::Listener; bool ready=false, close_claimed=false, binding=false, terminal=false; model::NativeOwnerLease native;std::size_t peer_index=0; };
    std::array<Owner,5> owners{{{-1,Role::Listener,false,false},{-1,Role::Outbound,false,false},{-1,Role::Accepted,false,false},{-1,Role::Outbound,false,false},{-1,Role::Accepted,false,false}}};
    // Readback marker only. Execution ownership and all receive/copy/drain
    // counters belong to ProcessCoordinator/ApplicationAttachments/InputChannels.
    struct Lease { rtps::TransportReceiverInterface* receiver=nullptr; bool enabled=true; std::uint64_t id=0; };
    std::shared_ptr<Lease> current;
    struct Record { std::shared_ptr<const Bytes> bytes; std::shared_ptr<Lease> lease; std::uint64_t id=0; int socket=-1; Role role=Role::Outbound; std::shared_ptr<Application> app; std::uint64_t receiver_id=0; std::size_t peer_index=0; };
    // Preserve the already-tested bounded TX ownership path. Every task and
    // submission pins its original application; joint Detach drains it before
    // releasing the coordinator's application. SDK TX is not executed by N2.
    std::deque<Record> tx;
    std::size_t tx_count=0,tx_bytes=0,callback_count=0;
    std::thread tx_worker,rx_worker;
    ISocketListener server_table{},client_table{};
    // Fixed metadata storage: 1024 records per State, at most eight retained
    // service incarnations. No payload/lease references. Overflow never changes
    // admission; cumulative counters preserve its visibility.
    mutable std::mutex rx_diagnostic_gate;
    std::mutex rx_emitter_gate;
    std::array<RxAdmissionRecord,RxAdmissionSummary::capacity> rx_decisions{};
    RxAdmissionSummary rx_summary;
    std::size_t rx_emitted=0;
    std::uint64_t rx_snapshot_sequence=0;
    State(Config c,SocketApi a,std::shared_ptr<Control> s):config(std::move(c)),api(a),control(std::move(s)){}
    void RecordRxAdmission(const RxAdmissionRecord& record) {
        std::lock_guard<std::mutex> lock(rx_diagnostic_gate);
        ++rx_summary.total;
        const auto reason=static_cast<std::size_t>(record.decision.reason);
        ++rx_summary.reasons[reason<rx_summary.reasons.size()?reason:0];
        if(rx_summary.stored<rx_decisions.size())rx_decisions[rx_summary.stored++]=record;
        else ++rx_summary.overflow;
    }
    RxAdmissionSummary RxSummary() const {
        std::lock_guard<std::mutex> lock(rx_diagnostic_gate);return rx_summary;
    }
    RxAdmissionRecord RxRecord(std::size_t index) const {
        std::lock_guard<std::mutex> lock(rx_diagnostic_gate);
        return index<rx_summary.stored?rx_decisions[index]:RxAdmissionRecord{};
    }
    void FlushRxAdmissions() {
        // Only the emitter lock spans I/O. Callback append never acquires it.
        std::lock_guard<std::mutex> emit(rx_emitter_gate);
        const auto summary=RxSummary();const auto sequence=++rx_snapshot_sequence;
        while(rx_emitted<summary.stored) {
            const auto r=RxRecord(rx_emitted++);const auto& d=r.decision;
            std::printf("EXPERIMENT_RX_ADMISSION schema=1 run=%s instance_id=%llu runtime_id=%llu callback_slot=%d socket=%d invocation=%llu "
                "stage=%s reason=%s result_known=%d result=%d candidate_count=%u size=%zu endpoint_limit_known=%d endpoint_limit=%zu record_limit_known=%d record_limit=%zu native_status_known=%d native_status=%d "
                "native_known=%d native_slot=%llu native_incarnation=%llu native_generation=%llu native_route_version=%llu identity_origin=ADAPTER_CAPTURE_NOT_SDK_EVENT "
                "application_known=%d application_incarnation=%llu logical_token=%llu receiver_known=%d receiver_incarnation=%llu receiver_identity_kind=ENTRY_SNAPSHOT_NOT_EXECUTION_LEASE "
                "entry_state_known=%d entry_state=%u adapter_state_known=%d adapter_state=%u ingress_receiver_marker=%llu lifecycle_known=%d stopped=%d application_phase=%d receiver_phase=%d "
                "capacity_known=%d capacity_mask=%u records_used=%zu records_limit=%zu bytes_used=%zu bytes_limit=%zu local_records_used=%zu local_records_limit=%zu local_bytes_used=%zu local_bytes_limit=%zu copying_used=%zu copying_limit=%zu application_copying=%zu application_copying_limit=%zu payload_identity=UNOBSERVED\n",
                config.run_id.c_str(),static_cast<unsigned long long>(instance_id),static_cast<unsigned long long>(r.runtime_id),r.callback_slot,r.socket,static_cast<unsigned long long>(r.invocation),
                model::RxAdmissionStageName(d.stage),model::RxAdmissionReasonName(d.reason),d.result_known,d.result,d.candidate_count,d.size,r.endpoint_limit_known,r.endpoint_limit,d.record_limit_known,d.record_limit,d.native_status_known,static_cast<int>(d.native_status),
                d.native_known,static_cast<unsigned long long>(d.native_slot),static_cast<unsigned long long>(d.native_incarnation),static_cast<unsigned long long>(d.native_generation),static_cast<unsigned long long>(d.native_route_version),
                d.application_known,static_cast<unsigned long long>(d.application_incarnation),static_cast<unsigned long long>(d.logical_token),d.receiver_known,static_cast<unsigned long long>(d.receiver_incarnation),
                r.entry_state_known,r.entry_state,r.adapter_state_known,r.adapter_state,static_cast<unsigned long long>(r.ingress_receiver_marker),d.lifecycle_known,d.stopped,d.application_phase,d.receiver_phase,
                d.capacity_known,d.capacity_mask,d.records_used,d.records_limit,d.bytes_used,d.bytes_limit,d.local_records_used,d.local_records_limit,d.local_bytes_used,d.local_bytes_limit,d.copying_used,d.copying_limit,d.application_copying,d.application_copying_limit);
        }
        const auto legacy=control->Snapshot();
        std::printf("EXPERIMENT_RX_ADMISSION_SUMMARY schema=1 run=%s instance_id=%llu snapshot_seq=%llu total=%llu stored=%llu overflow=%llu capacity=%zu emitted_records=%zu complete_records=%d coverage=OBSERVED_UNTIL_SNAPSHOT record_bytes=%zu store_bytes=%zu legacy_rx_drops=%llu legacy_no_app_drops=%llu legacy_unexpected_callbacks=%llu legacy_snapshot=AGGREGATED_NOT_ATOMIC unexpected_callbacks_scope=ALL_NATIVE_EVENTS\n",
            config.run_id.c_str(),static_cast<unsigned long long>(instance_id),static_cast<unsigned long long>(sequence),static_cast<unsigned long long>(summary.total),static_cast<unsigned long long>(summary.stored),static_cast<unsigned long long>(summary.overflow),RxAdmissionSummary::capacity,rx_emitted,summary.overflow==0,sizeof(RxAdmissionRecord),sizeof(rx_decisions),static_cast<unsigned long long>(legacy.rx_drops),static_cast<unsigned long long>(legacy.no_app_drops),static_cast<unsigned long long>(legacy.unexpected_callbacks));
        for(std::size_t i=0;i<summary.reasons.size();++i)if(summary.reasons[i])
            std::printf("EXPERIMENT_RX_ADMISSION_REASON_COUNT schema=1 run=%s instance_id=%llu snapshot_seq=%llu reason=%s count=%llu unit=CALLBACK_ADMISSION\n",config.run_id.c_str(),static_cast<unsigned long long>(instance_id),static_cast<unsigned long long>(sequence),model::RxAdmissionReasonName(static_cast<model::RxAdmissionReason>(i)),static_cast<unsigned long long>(summary.reasons[i]));
        std::fflush(stdout);
    }
    void Identify() {
        std::lock_guard<std::mutex> lock(control->mutex_);
        control->instance_name_=config.instance_name.empty()?"single":config.instance_name;
        control->stats_.instance_id=instance_id;
        control->stats_.server_callback_slot=server_slot;
        control->stats_.client_callback_slot=client_slot+static_cast<int>(ProcessStats::role_entry_limit);
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
        control->rx_drop_enabled_=config.enable_rx_drop;
#endif
    }
    const char* Label() const { return config.instance_name.empty()?"single":config.instance_name.c_str(); }
    int CallbackSlot(Role role) const { return role==Role::Outbound?client_slot+static_cast<int>(ProcessStats::role_entry_limit):server_slot; }
    template<class F> void Update(F f) { std::lock_guard<std::mutex> lock(control->mutex_); f(control->stats_); }
    std::shared_ptr<Bytes> OwnPayload(std::size_t size,bool transmit,const std::shared_ptr<Application>& app,const std::uint8_t* input=nullptr) {
        auto allocated=input?std::make_unique<Bytes>(input,input+size):std::make_unique<Bytes>(size);
        Update([&](Stats& s){if(transmit){++s.live_tx_blocks;s.live_tx_payload_bytes+=size;}
            else{++s.live_rx_blocks;s.live_rx_payload_bytes+=size;}});
        // Only Control is retained. Final release can occur under gate; this
        // deleter therefore never reacquires gate or calls SDK/receiver code.
        if(app) app->Update([&](Stats& s){if(transmit){++s.live_tx_blocks;s.live_tx_payload_bytes+=size;}else{++s.live_rx_blocks;s.live_rx_payload_bytes+=size;}});
        return std::shared_ptr<Bytes>(allocated.release(),[c=control,a=app?app->control:nullptr,transmit,size](Bytes* payload){
            delete payload;
            if(a){std::lock_guard<std::mutex> lock(a->mutex_);auto& s=a->stats_;if(transmit){--s.live_tx_blocks;s.live_tx_payload_bytes-=size;}else{--s.live_rx_blocks;s.live_rx_payload_bytes-=size;}a->payloads_changed_.notify_all();}
            std::lock_guard<std::mutex> lock(c->mutex_);
            auto& s=c->stats_;
            if(transmit){--s.live_tx_blocks;s.live_tx_payload_bytes-=size;}
            else{--s.live_rx_blocks;s.live_rx_payload_bytes-=size;}
            c->payloads_changed_.notify_all();
        });
    }
    std::size_t ModelPeerIndex(const Locator& remote)const{for(std::size_t i=0;i<PeerCount(config);++i)if(model_peers[i]==remote)return i;throw std::logic_error("unknown captured peer route");}
    void Log(const char* event,std::uint64_t id,int socket,Role role,int rc,const Bytes* bytes=nullptr,bool has_rc=true,std::uint64_t lease_id=0,std::size_t route=0) const {
        const ByteSpan span{bytes?bytes->data():nullptr,bytes?bytes->size():0};LogSpan(event,id,socket,role,rc,bytes?&span:nullptr,has_rc,lease_id,route);
    }
    void LogSpan(const char* event,std::uint64_t id,int socket,Role role,int rc,const ByteSpan* bytes=nullptr,bool has_rc=true,std::uint64_t lease_id=0,std::size_t route=0) const {
        if(auto* observation=CurrentPerformanceObservation()) {
            observation->Span(event,id,instance_id,socket,rc,has_rc,bytes?bytes->size():0,lease_id);
            if(observation->Quiet())return; // Output only; caller accounting continues unchanged.
        }
        const auto t=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count();
        const auto seen=bytes ? ObserveRtps(bytes->data(),bytes->size()) : RtpsObservation{};
        const auto payload_hash=bytes ? Hash(bytes->data(),bytes->size()) : 0;
        const auto& remote=PeerLocator(config,route);
        const auto local=Hex16(config.local_locator.address),peer=Hex16(remote.address),writer=Hex16(seen.first_writer_guid.data());
        char result[24];if(has_rc)std::snprintf(result,sizeof(result),"%d",rc);else std::snprintf(result,sizeof(result),"VOID");
        std::printf("EXPERIMENT_UNQUALIFIED run=%s event=%s invocation=%llu steady_us=%lld socket=%d owner=%s rc=%s len=%zu hash=%016llx spdp=%llu sedp_pub=%llu sedp_sub=%llu user=%llu first_writer_guid=%s local_kind=%d local_key=%s local_port=%u peer_kind=%d peer_key=%s peer_port=%u rtps_observed=%d statistics_submessages=%llu statistics_body_bytes=%zu instance_name=%s instance_id=%llu callback_slot=%d callback_role=%s owner_slot=%d receiver_lease_id=%llu local_service=%s process_steady_us=%lld\n",
            config.run_id.c_str(),event,static_cast<unsigned long long>(id),static_cast<long long>(t),socket,RoleName(role),result,
            bytes?bytes->size():0,static_cast<unsigned long long>(payload_hash),
            static_cast<unsigned long long>(seen.spdp),static_cast<unsigned long long>(seen.sedp_publications),static_cast<unsigned long long>(seen.sedp_subscriptions),static_cast<unsigned long long>(seen.user),
            writer.data(),config.local_locator.kind,local.data(),config.local_locator.port,remote.kind,peer.data(),remote.port,seen.valid,
            static_cast<unsigned long long>(seen.statistics_submessages),seen.statistics_body_bytes,Label(),static_cast<unsigned long long>(instance_id),CallbackSlot(role),role==Role::Outbound?"client":"server",role==Role::Listener?0:static_cast<int>(role==Role::Outbound?DialSlot(route):AcceptedSlot(route)),static_cast<unsigned long long>(lease_id),config.local_service.c_str(),ProcessSteadyUs());
        for(const auto& data:seen.data){
            const auto guid=Hex16(data.writer_guid.data());
            std::printf("EXPERIMENT_DATA run=%s event=%s invocation=%llu socket=%d owner=%s rc=%s writer_guid=%s sequence_hi=%d sequence_lo=%u submessage_offset=%zu submessage_body_bytes=%zu native_len=%zu native_hash=%016llx instance_name=%s instance_id=%llu callback_slot=%d receiver_lease_id=%llu\n",
                config.run_id.c_str(),event,static_cast<unsigned long long>(id),socket,RoleName(role),result,guid.data(),
                static_cast<int>(data.sequence_hi),data.sequence_lo,data.submessage_offset,data.submessage_body_bytes,
                bytes->size(),static_cast<unsigned long long>(payload_hash),Label(),static_cast<unsigned long long>(instance_id),CallbackSlot(role),static_cast<unsigned long long>(lease_id));
        }
        for (const auto& fragment:seen.fragments) {
            const auto guid=Hex16(fragment.writer_guid.data());
            std::printf("EXPERIMENT_DATA_FRAG run=%s event=%s invocation=%llu socket=%d owner=%s rc=%s writer_guid=%s sequence_hi=%d sequence_lo=%u fragment_start=%u fragment_count=%u fragment_end=%llu fragment_size=%u sample_size=%u submessage_offset=%zu submessage_body_bytes=%zu inline_qos=%d key_flag=%d octets_to_inline_qos=%u payload_bounds_known=%d payload_offset=%zu payload_bytes=%zu native_len=%zu native_hash=%016llx instance_name=%s instance_id=%llu callback_slot=%d receiver_lease_id=%llu\n",
                config.run_id.c_str(),event,static_cast<unsigned long long>(id),socket,RoleName(role),result,guid.data(),
                static_cast<int>(fragment.sequence_hi),fragment.sequence_lo,fragment.fragment_start,
                static_cast<unsigned>(fragment.fragment_count),static_cast<unsigned long long>(fragment.fragment_end),
                static_cast<unsigned>(fragment.fragment_size),fragment.sample_size,fragment.submessage_offset,fragment.submessage_body_bytes,
                fragment.inline_qos,fragment.key_flag,static_cast<unsigned>(fragment.octets_to_inline_qos),
                fragment.payload_bounds_known,fragment.payload_offset,fragment.payload_bytes,
                bytes->size(),static_cast<unsigned long long>(payload_hash),Label(),static_cast<unsigned long long>(instance_id),CallbackSlot(role),static_cast<unsigned long long>(lease_id));
        }
        for(const auto& control:seen.controls) {
            const char* kind=control.kind==0x06?"ACKNACK":control.kind==0x07?"HEARTBEAT":
                    control.kind==0x12?"NACK_FRAG":"HEARTBEAT_FRAG";
            const auto reader_guid=Hex16(control.reader_guid.data()),writer_guid=Hex16(control.writer_guid.data());
            const auto hex=[](const std::uint8_t* p,std::size_t n,char* output) {
                constexpr char digits[]="0123456789abcdef";
                for(std::size_t i=0;i<n;++i){output[2*i]=digits[p[i]>>4];output[2*i+1]=digits[p[i]&15];}
                output[2*n]=0;
            };
            char source[25],destination[25],reader_id[9],writer_id[9],bitmap[73]{};
            hex(control.source_prefix.data(),12,source); hex(control.destination_prefix.data(),12,destination);
            hex(control.reader_id.data(),4,reader_id); hex(control.writer_id.data(),4,writer_id);
            const auto words=(control.num_bits+31)/32;
            for(std::uint32_t i=0;i<words;++i)
                std::snprintf(bitmap+9*i,sizeof(bitmap)-9*i,"%08x%s",control.bitmap[i],i+1<words?",":"");
            std::printf("EXPERIMENT_RTPS_CONTROL run=%s event=%s invocation=%llu socket=%d owner=%s rc=%s kind=%s source_prefix=%s destination_prefix=%s destination_known=%d reader_id=%s writer_id=%s reader_guid=%s reader_prefix_known=%d writer_guid=%s writer_prefix_known=%d sequence_hi=%d sequence_lo=%u last_sequence_hi=%d last_sequence_lo=%u fragment_base=%u last_fragment=%u num_bits=%u bitmap_MSB_first=%s count=%u final=%d liveliness=%d submessage_offset=%zu native_len=%zu native_hash=%016llx instance_name=%s instance_id=%llu callback_slot=%d receiver_lease_id=%llu\n",
                config.run_id.c_str(),event,static_cast<unsigned long long>(id),socket,RoleName(role),result,kind,
                source,destination,control.destination_prefix_known,reader_id,writer_id,
                reader_guid.data(),control.reader_prefix_known,writer_guid.data(),control.writer_prefix_known,
                static_cast<int>(control.sequence_hi),control.sequence_lo,static_cast<int>(control.last_sequence_hi),
                control.last_sequence_lo,control.fragment_base,control.last_fragment,control.num_bits,words?bitmap:"EMPTY",
                control.count,control.final_flag,control.liveliness_flag,control.submessage_offset,bytes->size(),
                static_cast<unsigned long long>(payload_hash),Label(),static_cast<unsigned long long>(instance_id),CallbackSlot(role),static_cast<unsigned long long>(lease_id));
        }
        std::fflush(stdout);
    }
    void LogApplication(const char* stage,const std::shared_ptr<Application>& app,std::uint64_t invocation,int socket,std::uint64_t receiver_id)const{
        if(auto* observation=CurrentPerformanceObservation()) {
            observation->Text(1);if(observation->Quiet())return;
        }
        auto core=runtime.lock();
        std::printf("EXPERIMENT_APPLICATION_TX run=%s stage=%s runtime_id=%llu instance_id=%llu application_incarnation=%llu receiver_incarnation=%llu logical_token=%llu invocation=%llu socket=%d process_steady_us=%lld\n",config.run_id.c_str(),stage,static_cast<unsigned long long>(core?core->id:0),static_cast<unsigned long long>(instance_id),static_cast<unsigned long long>(app->id),static_cast<unsigned long long>(receiver_id),static_cast<unsigned long long>(app->token),static_cast<unsigned long long>(invocation),socket,ProcessSteadyUs());std::fflush(stdout);
    }
    template<class F> int Call(const char* event,int socket,Role role,F f,std::size_t peer=0) {
        const auto id=++invocation;
        Update([](Stats& s){++s.sdk_calls_active;}); Log(event,id,socket,role,0,nullptr,true,0,peer);
        const int rc=f();
        Update([](Stats& s){--s.sdk_calls_active;}); Log("SDK_return",id,socket,role,rc,nullptr,true,0,peer); return rc;
    }
    bool Peer(const PeerSocketInfo& p,std::size_t peer) const {
        // Public API borrowed C strings; bound the reads to the actual public limits.
        const auto equals=[](const char* p,const std::string& s,std::size_t bound) {
            if (!p || s.size()>=bound) return false;
            std::size_t n=0; while(n<bound && p[n]) ++n;
            return n==s.size() && n<bound && std::memcmp(p,s.data(),n)==0;
        };
        return peer<PeerCount(config)&&p.dataType==DATA_TYPE_BYTES && equals(p.name,PeerService(config,peer),256) && equals(p.networkId,PeerNetwork(config,peer),65);
    }
    Owner* Find(int id) { for(auto& o:owners) if(o.id==id && id>0) return &o; return nullptr; }
    std::uint32_t RxState(const Owner* owner,bool role_match) const noexcept {
        return (owner?1u:0u)|(role_match?2u:0u)|(owner&&owner->close_claimed?4u:0u)|
            (owner&&owner->ready?8u:0u)|(owner&&owner->binding?16u:0u)|(closing?32u:0u)|
            (retired?64u:0u)|(owner&&owner->terminal?128u:0u);
    }
    bool Enter(bool server,int slot,int socket,const char* event,std::uint64_t& id,RxAdmissionRecord* diagnostic=nullptr) {
        int owner_slot=-1;bool role_match=false,was_retired=false,was_closing=false;
        std::uint64_t receiver_marker=0;long long observed_us=0;
        {
            std::lock_guard<std::mutex> lock(gate);
            id=++invocation;
            const auto* owner=Find(socket);
            owner_slot=owner?static_cast<int>(owner-&owners[0]):-1;
            role_match=owner&&(server?owner->role!=Role::Outbound:owner->role==Role::Outbound);
            receiver_marker=current&&current->enabled?current->id:0;
            was_retired=retired;was_closing=closing;observed_us=ProcessSteadyUs();
            if(diagnostic){diagnostic->invocation=id;diagnostic->entry_state_known=true;diagnostic->entry_state=RxState(owner,role_match);
                diagnostic->endpoint_limit_known=true;diagnostic->endpoint_limit=config.max_message_size;
                diagnostic->ingress_receiver_marker=receiver_marker;
                if(retired)diagnostic->decision.Decide(model::RxAdmissionStage::NativeEntry,model::RxAdmissionReason::Retired);}
            if(retired)Update([](Stats& s){++s.unexpected_callbacks;});
            else {++callback_count;Update([](Stats& s){++s.callbacks_active;});}
        }
        // Preserve the ingress snapshot and callback accounting while moving
        // blocking output outside the State ownership gate.
        auto* observation=CurrentPerformanceObservation();
        if(observation)observation->Text(0);
        if(!observation||!observation->Quiet()) {
        std::printf("EXPERIMENT_NATIVE_INGRESS run=%s instance_name=%s instance_id=%llu callback_slot=%d callback_role=%s native_event=%s socket=%d owner_slot=%d owner_match=%d receiver_lease_id=%llu retired=%d closing=%d local_service=%s invocation=%llu process_steady_us=%lld\n",
            config.run_id.c_str(),Label(),static_cast<unsigned long long>(instance_id),slot,server?"server":"client",event,socket,
            owner_slot,role_match,static_cast<unsigned long long>(receiver_marker),was_retired,was_closing,config.local_service.c_str(),static_cast<unsigned long long>(id),observed_us);
        std::fflush(stdout);
        }
        return !was_retired;
    }
    void Leave() { std::lock_guard<std::mutex> lock(gate); --callback_count; Update([](Stats& s){--s.callbacks_active;}); changed.notify_all(); }
    bool Negotiate(int socket,PeerSocketInfo p) {
        std::size_t peer=peer_limit;for(std::size_t i=0;i<PeerCount(config);++i)if(Peer(p,i)){peer=i;break;}
        std::lock_guard<std::mutex> lock(gate);
        if(peer==peer_limit) Update([](Stats& s){++s.peer_errors;});
        if(closing || peer==peer_limit || socket<=0 || Find(socket) || owners[AcceptedSlot(peer)].id>0 || !ClaimNativeOwner(this,socket)) return false;
        // Affirmative admission owns this native handle immediately. OnBind
        // is not guaranteed to follow negotiation, so Stop must close it too.
        auto core=runtime.lock();if(!core){ReleaseNativeOwner(this,socket);return false;}
        const model::ObservedPeerKey observed{config.local_service,PeerNetwork(config,peer),PeerService(config,peer),model::RecordDataType::Bytes};
        auto admitted=core->process->Native().ObserveInbound(native_namespace,socket,observed);
        if(!admitted.accepted||!admitted.owner){ReleaseNativeOwner(this,socket);Update([](Stats& s){s.fatal=true;});return false;}
        owners[AcceptedSlot(peer)]={socket,Role::Accepted,false,false,true,false,*admitted.owner,peer};
        Update([](Stats& s){++s.native_owners;});
        return true;
    }
    void Bound(bool server,int socket,PeerSocketInfo peer) {
        std::lock_guard<std::mutex> lock(gate);
        auto* owner=Find(socket);
        // Conversion requires our affirmative negotiation, not a numeric ID
        // found only in an unsolicited/late callback. Never close an unknown ID.
        if(!server || !owner || owner->role!=Role::Accepted || !owner->binding || owner->terminal || owner->close_claimed) {
            Update([](Stats& s){++s.unexpected_callbacks;}); return;
        }
        const bool valid=Peer(peer,owner->peer_index);
        if(!valid) Update([](Stats& s){++s.peer_errors;s.fatal=true;});
        owner->binding=false;
        owner->terminal=!valid;
        owner->ready=valid&&!closing;
        if(owner->ready) ever_ready[owner->peer_index]=true;
        changed.notify_all();
    }
    void NativeClosed(bool server,int socket,int code) {
        std::lock_guard<std::mutex> lock(gate);
        auto* o=Find(socket);
        if(!o || (server && o->role==Role::Outbound) || (!server && o->role!=Role::Outbound)) {
            Update([](Stats& s){++s.unexpected_callbacks;}); return;
        }
        auto admitted=native_namespace.AdmitCallback(socket);if(admitted)admitted.value->ObserveShutdown();
        o->ready=false;o->binding=false;o->terminal=true;
        if(!closing && o->role!=Role::Listener) Update([](Stats& s){++s.peer_shutdown_events;});
        Log("OnShutdown",++invocation,socket,o->role,code,nullptr,true,0,o->peer_index);
        changed.notify_all();
    }
    void NativeError(bool server,int socket,int code) {
        Role role=server?Role::Accepted:Role::Outbound;
        std::size_t peer=0;
        {
            std::lock_guard<std::mutex> lock(gate);
            auto* owner=Find(socket);
            if(closing||!owner||(server&&owner->role==Role::Outbound)||(!server&&owner->role!=Role::Outbound)) {
                Update([](Stats& s){++s.unexpected_callbacks;});return;
            }
            role=owner->role;peer=owner->peer_index;owner->ready=false;owner->binding=false;owner->terminal=true;
            Update([](Stats& s){s.fatal=true;});
        }
        Log("OnError",++invocation,socket,role,code,nullptr,true,0,peer);
    }
    void Received(bool server,int socket,const void* data,std::uint32_t size,std::uint64_t id,RxAdmissionRecord& record) {
        auto& diagnostic=record.decision;
        const auto reject=[&](model::RxAdmissionReason reason){diagnostic.Decide(model::RxAdmissionStage::Adapter,reason);};
        auto core=runtime.lock();if(!core){reject(model::RxAdmissionReason::RuntimeMissing);return;}record.runtime_id=core->id;
        const Role role=server?Role::Accepted:Role::Outbound;
        {
            std::lock_guard<std::mutex> lock(gate);auto* o=Find(socket);
            record.adapter_state_known=true;record.adapter_state=RxState(o,o&&o->role==role);
            if(!o||o->role!=role||o->close_claimed||(!o->ready&&!(role==Role::Outbound&&o->binding))){
                reject(!o?model::RxAdmissionReason::OwnerMissing:o->role!=role?model::RxAdmissionReason::WrongRole:o->close_claimed?model::RxAdmissionReason::CloseClaimed:model::RxAdmissionReason::NotReady);
                Update([](Stats& s){++s.unexpected_callbacks;});return;}
            if(closing){reject(model::RxAdmissionReason::Closing);return;}
            // The process model has a shared absolute record ceiling; each
            // endpoint still enforces its own immutable transport capacity
            // before the canonical copy and receiver admission occur.
            if(!data||!size||size>config.max_message_size){reject(!data?model::RxAdmissionReason::NullBuffer:!size?model::RxAdmissionReason::ZeroSize:model::RxAdmissionReason::TooLarge);Update([](Stats& s){++s.rx_drops;});return;}
        }
        auto callback=native_namespace.AdmitCallback(socket);
        diagnostic.native_status_known=true;diagnostic.native_status=callback.issue.code;
        if(!callback){diagnostic.Decide(model::RxAdmissionStage::NativeCallback,model::RxAdmissionReason::NativeCallbackRejected);Update([](Stats& s){++s.unexpected_callbacks;});return;}
        const auto captured=callback.value->CapturedOwner();
        diagnostic.native_known=true;diagnostic.native_slot=captured.slot;diagnostic.native_incarnation=captured.incarnation;
        diagnostic.native_generation=captured.generation;diagnostic.native_route_version=captured.route_version;
        auto identity=callback.value->CaptureIdentity();
        diagnostic.native_status=identity.issue.code;
        if(!identity){diagnostic.Decide(model::RxAdmissionStage::NativeIdentity,model::RxAdmissionReason::NativeIdentityRejected);Update([](Stats& s){++s.unexpected_callbacks;});return;}
        const auto result=core->process->Receive(*identity.value,data,size,Clock::now()+std::chrono::seconds(2),id,&diagnostic);
        if(result==model::ReceiveAdmissionResult::NoReceiver){Update([](Stats& s){++s.no_app_drops;});
            std::printf("EXPERIMENT_DROP_NO_APPLICATION run=%s runtime_id=%llu instance_id=%llu socket=%d invocation=%llu disposition=TERMINAL_NOT_SAVED process_steady_us=%lld\n",config.run_id.c_str(),static_cast<unsigned long long>(core->id),static_cast<unsigned long long>(instance_id),socket,static_cast<unsigned long long>(id),ProcessSteadyUs());std::fflush(stdout);}
        else if(result!=model::ReceiveAdmissionResult::Accepted)Update([](Stats& s){++s.rx_drops;});
        changed.notify_all();core->changed.notify_all();
    }
    bool Submit(const std::shared_ptr<Application>& app,const std::vector<rtps::NetworkBuffer>& buffers,std::uint32_t size,rtps::LocatorsIterator* begin,rtps::LocatorsIterator* end,const Clock::time_point& deadline) {
        const auto reject=[&] {Update([](Stats& s){++s.tx_rejected;});app->Update([](Stats& s){++s.tx_rejected;});return false;};
        {std::lock_guard<std::mutex> lock(gate);if(closing||app->detaching||app->detached||application!=app)return reject();++app->submits;}
        struct SubmitExit {State* s;std::shared_ptr<Application> a;~SubmitExit(){std::lock_guard<std::mutex> lock(s->gate);--a->submits;s->changed.notify_all();}} submit_exit{this,app};
        if(!begin || !end || !size || size>config.max_message_size || Clock::now()>=deadline) return reject();
        std::array<bool,peer_limit> selected{};std::size_t targets=0;
        for(;*begin!=*end;++*begin) {
            const auto& remote=**begin;
            const auto peer=PeerIndex(config,remote);
            if(peer<peer_limit){if(!selected[peer]){selected[peer]=true;++targets;}}
            else if(remote.kind==Codec::kind) return reject();
        }
        if(!targets) return reject();
        std::size_t available=0;
        for(const auto& b:buffers) { if(b.size&&!b.buffer) return reject(); available+=b.size; }
        if(available<size) return reject();
        {
            std::lock_guard<std::mutex> lock(gate);
            if(closing || app->detaching || !initialized || targets>records_limit-tx_count || targets*size>bytes_limit-tx_bytes || Clock::now()>=deadline) return reject();
            tx_count+=targets;app->tx_records+=targets;tx_bytes+=targets*size; Update([&](Stats& s){s.tx_records=tx_count;s.tx_bytes=tx_bytes;});app->Update([&](Stats& s){s.tx_records+=targets;s.tx_bytes+=targets*size;});
        }
        // Queue publication transfers settlement duty to TxLoop/Stop. Keep
        // that fact even if later diagnostic work throws after the worker has
        // already settled the task; it can never become pre-admission again.
        bool admitted=false;
        try {
            auto block=OwnPayload(size,true,app);
            std::size_t offset=0;
            for(const auto& b:buffers) {
                const auto n=std::min<std::size_t>(b.size,size-offset);
                if(n) std::memcpy(block->data()+offset,b.buffer,n);
                offset+=n; if(offset==size) break;
            }
            std::array<std::uint64_t,peer_limit> ids{};
            for(std::size_t i=0;i<PeerCount(config);++i)if(selected[i])ids[i]=++invocation;
            const auto receiver_identity=app->control->Snapshot().receiver_incarnation;
            {
                std::lock_guard<std::mutex> lock(gate);
                if(closing || app->detaching || Clock::now()>=deadline) {for(std::size_t i=0;i<targets;++i)ReleaseTx(size,app);return reject();}
                const auto before=tx.size();
                try{
                    for(std::size_t i=0;i<PeerCount(config);++i)if(selected[i]){
                        if(tx.size()-before==app->control->tx_publish_fault_after_)throw std::bad_alloc();
                        tx.push_back({block,nullptr,ids[i],-1,Role::Outbound,app,receiver_identity,i});
                    }
                }catch(...){while(tx.size()>before)tx.pop_back();throw;}
                admitted=true;
                Update([&](Stats& s){s.tx_accepted+=targets;});app->Update([&](Stats& s){s.tx_accepted+=targets;}); changed.notify_all();
            }
            try {if(app->control->after_tx_admit_)app->control->after_tx_admit_(block);}
            catch(...) {Update([](Stats& s){++s.tx_diagnostic_errors;});app->Update([](Stats& s){++s.tx_diagnostic_errors;});}
            for(std::size_t i=0;i<PeerCount(config);++i)if(selected[i]){
                Log("SenderResource_accepted",ids[i],-1,Role::Outbound,0,block.get(),true,0,i);
                LogApplication("TX_ACCEPTED",app,ids[i],-1,receiver_identity);
            }
            return true;
        } catch(...) {
            if(admitted) {Update([](Stats& s){++s.tx_diagnostic_errors;});app->Update([](Stats& s){++s.tx_diagnostic_errors;});return true;}
            std::lock_guard<std::mutex> lock(gate);for(std::size_t i=0;i<targets;++i)ReleaseTx(size,app);return reject();
        }
    }
    void ReleaseTx(std::size_t size,const std::shared_ptr<Application>& app) { --app->tx_records;app->Update([&](Stats& s){--s.tx_records;s.tx_bytes-=size;}); --tx_count;tx_bytes-=size; Update([&](Stats& s){s.tx_records=tx_count;s.tx_bytes=tx_bytes;});changed.notify_all(); }
    void CloseOwner(int socket,Role role) {
        const auto id=++invocation;
        std::size_t peer=0;model::NativeOwnerLease native_owner;{std::lock_guard<std::mutex> lock(gate);if(auto* owner=Find(socket)){native_owner=owner->native;peer=owner->peer_index;}}
        if(native_owner)native_owner.RequestClose();auto ticket=native_owner.TryClaimShutdown();
        if(ticket)ticket.value->CallEntered();
        Update([](Stats& s){++s.sdk_calls_active;});Log("Shutdown_enter",id,socket,role,0,nullptr,false,0,peer);
        api.shutdown(socket);
        if(ticket)ticket.value->CompleteShutdownReturned();
        Log("Shutdown_return",id,socket,role,0,nullptr,false,0,peer);
        ReleaseNativeOwner(this,socket);
        Update([](Stats& s){--s.sdk_calls_active;++s.shutdown_returns;});
    }
    std::pair<int,Role> ReadySocket(std::size_t peer) {
        const auto dial_slot=DialSlot(peer);
        {
            std::lock_guard<std::mutex> lock(gate);
            if(closing) return {-1,Role::Outbound};
            for(auto i:{dial_slot,AcceptedSlot(peer)}) if(owners[i].ready) return {owners[i].id,owners[i].role};
            if(ever_ready[peer] || owners[dial_slot].terminal) return {-1,Role::Outbound}; // No failure recovery in this experiment.
        }
        SocketInfo info{};
        info.name=const_cast<char*>(config.local_service.c_str()); info.pkgName=const_cast<char*>(config.package_name.c_str());
        info.peerName=const_cast<char*>(PeerService(config,peer).c_str()); info.peerNetworkId=const_cast<char*>(PeerNetwork(config,peer).c_str());
        info.dataType=DATA_TYPE_BYTES;
        auto core=runtime.lock();if(!core)return {-1,Role::Outbound};
        auto reservation=core->process->Native().ReserveDial(native_namespace,{model_local,model_peers[peer]});
        if(!reservation||!reservation.value->socket_call)return {-1,Role::Outbound};
        auto& socket_call=*reservation.value->socket_call;
        if(socket_call.CallEntered()!=model::NativeStatus::None)return {-1,Role::Outbound};
        const int socket=Call("Socket_outbound_enter",-1,Role::Outbound,[&]{return api.create(info);},peer);
        const auto completed=socket_call.Complete(socket>0?model::NativeSocketResult::Id(socket):model::NativeSocketResult::Error(socket));
        if(socket<=0||completed.status!=model::NativeStatus::None) return {-1,Role::Outbound};
        bool stop=false;
        {
            std::lock_guard<std::mutex> lock(gate);
            if(Find(socket)||!ClaimNativeOwner(this,socket)) {
                // An ambiguous numeric handle is not authority to close a
                // different live owner. Preserve its ownership; fail this path.
                Update([](Stats& s){s.fatal=true;});return {-1,Role::Outbound};
            }
            owners[dial_slot]={socket,Role::Outbound,false,false,true,false,reservation.value->owner,peer}; Update([](Stats& s){++s.native_owners;});
            stop=closing; if(stop) owners[dial_slot].close_claimed=true;
        }
        if(stop) {CloseOwner(socket,Role::Outbound);return {-1,Role::Outbound};}
        const QosTV qos[]={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};
        ExposeEntry(this,client_slot,false);
        auto bind=core->process->Native().BeginBind(reservation.value->owner);
        if(!bind||bind.value->CallEntered()!=model::NativeStatus::None){
            {std::lock_guard<std::mutex> lock(gate);owners[dial_slot].close_claimed=true;}
            CloseOwner(socket,Role::Outbound);
            {std::lock_guard<std::mutex> lock(gate);owners[dial_slot]={-1,Role::Outbound,false,false};}
            return {-1,Role::Outbound};
        }
        const int rc=Call("Bind_enter",socket,Role::Outbound,[&]{return api.bind(socket,qos,2,&client_table);},peer);
        bind.value->Complete(rc==0?model::SyncBindResult::Success():model::SyncBindResult::Error(rc));
        bool close=false,terminal=false;
        {
            std::lock_guard<std::mutex> lock(gate);
            auto& o=owners[dial_slot];
            const bool pending=o.id==socket&&o.binding&&!o.terminal;
            terminal=o.terminal;
            o.binding=false;
            if(rc==0&&pending&&!closing&&!o.close_claimed) {o.ready=true;ever_ready[peer]=true;return {socket,Role::Outbound};}
            if(!o.close_claimed) {o.close_claimed=true;close=true;}
        }
        if(close) CloseOwner(socket,Role::Outbound);
        {
            std::lock_guard<std::mutex> lock(gate);
            // The service record stays alive through the listener. Retry only
            // on a NEW upper-layer send, re-registering the identical table.
            if(!closing&&!terminal) owners[dial_slot]={-1,Role::Outbound,false,false};
        }
        return {-1,Role::Outbound};
    }
    void TxLoop() {
        for(;;) {
            Record r;
            {std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[&]{return closing||!tx.empty();});if(closing)break;r=std::move(tx.front());tx.pop_front();}
            const auto target=ReadySocket(r.peer_index); int rc=-1;
            if(target.first>0) {
                if(r.app->control->before_send_)r.app->control->before_send_();
                LogApplication("TX_NATIVE_ENTER",r.app,r.id,target.first,r.receiver_id);
                Update([](Stats& s){++s.sdk_calls_active;});Log("SendBytes_enter",r.id,target.first,target.second,0,r.bytes.get(),true,0,r.peer_index);
                rc=api.send_bytes(target.first,r.bytes->data(),static_cast<std::uint32_t>(r.bytes->size()));
                Log("SendBytes_return",r.id,target.first,target.second,rc,r.bytes.get(),true,0,r.peer_index);
                LogApplication("TX_NATIVE_RETURN",r.app,r.id,target.first,r.receiver_id);
                const auto seen=ObserveRtps(r.bytes->data(),r.bytes->size());
                Update([&](Stats& s){--s.sdk_calls_active;++s.tx_send_returns;if(rc==0){s.tx_spdp+=seen.spdp;s.tx_sedp+=seen.sedp;s.tx_user+=seen.user;s.tx_sedp_publications+=seen.sedp_publications;s.tx_sedp_subscriptions+=seen.sedp_subscriptions;s.tx_data_frag+=seen.fragments.size();}});
                r.app->Update([&](Stats& s){++s.tx_send_returns;if(rc==0){s.tx_spdp+=seen.spdp;s.tx_sedp+=seen.sedp;s.tx_user+=seen.user;s.tx_sedp_publications+=seen.sedp_publications;s.tx_sedp_subscriptions+=seen.sedp_subscriptions;s.tx_data_frag+=seen.fragments.size();}});
            }
            if(rc!=0){Update([](Stats& s){++s.tx_send_failures;});r.app->Update([](Stats& s){++s.tx_send_failures;});}
            {std::lock_guard<std::mutex> lock(gate);ReleaseTx(r.bytes->size(),r.app);}
            if(r.app->control->after_tx_settle_)r.app->control->after_tx_settle_();
        }
    }
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    static bool RuleIdentity(const FragmentObservation& f,const RxDropRule& rule) {
        return f.writer_guid==rule.writer_guid && f.sequence_hi==rule.sequence_hi &&
            f.sequence_lo==rule.sequence_lo && f.sample_size==rule.sample_size;
    }
    static bool CompleteFragmentPayload(const FragmentObservation& f) {
        if(!f.fragment_start || !f.fragment_count || !f.fragment_size || !f.sample_size ||
                !f.payload_bounds_known || f.key_flag) return false;
        const auto total=(std::uint64_t(f.sample_size)+f.fragment_size-1)/f.fragment_size;
        if(f.fragment_end>total) return false;
        const auto offset=std::uint64_t(f.fragment_start-1)*f.fragment_size;
        if(offset>=f.sample_size) return false;
        const auto required=std::min<std::uint64_t>(std::uint64_t(f.fragment_count)*f.fragment_size,f.sample_size-offset);
        return f.payload_bytes>=required && f.payload_bytes<=required+3;
    }
    bool MaybeDrop(const ByteSpan& bytes,std::uint64_t id,int socket,Role role,const std::shared_ptr<Control>& c) {
        const auto& control=c;
        if(!config.enable_rx_drop) return false;
        const auto state=control->DropSnapshot();
        if(!state.armed || state.dropped_messages) return false;
        const auto seen=ObserveRtps(bytes.data(),bytes.size());
        if(!seen.valid || !seen.drop_shape_candidate || seen.fragments.size()!=1) return false;
        const auto& f=seen.fragments.front();const auto& rule=state.rule;
        if(!RuleIdentity(f,rule) || f.fragment_start!=rule.fragment_start || f.fragment_count!=rule.fragment_count ||
                !CompleteFragmentPayload(f)) return false;
        const auto hash=Hash(bytes.data(),bytes.size());
        {
            std::lock_guard<std::mutex> lock(control->mutex_);
            auto& drop=control->rx_drop_;
            if(control->rx_drop_closed_ || !drop.armed || drop.dropped_messages) return false;
            drop.dropped_messages=1;drop.dropped_invocation=id;
            drop.dropped_len=bytes.size();drop.dropped_hash=hash;
        }
        // Entire immutable native RTPS message is retired here. Only metadata
        // is retained; no bytes are changed, cached for replay or reassembled.
        LogSpan("ControlledRxDrop",id,socket,role,0,&bytes,false,0,ModelPeerIndex(current_receive.native.remote()));
        return true;
    }
    void ObserveDeliveredRange(const ByteSpan& bytes,std::uint64_t id,int socket,Role role,const RtpsObservation& seen,const std::shared_ptr<Control>& c) {
        const auto& control=c;
        if(!config.enable_rx_drop || !seen.valid) return;
        bool delivered=false;
        {
            std::lock_guard<std::mutex> lock(control->mutex_);auto& drop=control->rx_drop_;
            if(!drop.dropped_messages) return;
            const auto end=std::uint64_t(drop.rule.fragment_start)+drop.rule.fragment_count-1;
            for(const auto& f:seen.fragments) if(RuleIdentity(f,drop.rule)&&CompleteFragmentPayload(f) &&
                    f.fragment_start<=drop.rule.fragment_start&&f.fragment_end>=end) {
                ++drop.recovered_range_deliveries;delivered=true;break;
            }
        }
        if(delivered) LogSpan("ControlledRxRangeDelivered",id,socket,role,0,&bytes,false,0,ModelPeerIndex(current_receive.native.remote()));
    }
#endif
    void RxLoop() {
        auto core=runtime.lock();if(!core)return;
        for(;;){
            {std::unique_lock<std::mutex> lock(gate);if(closing)break;}
            auto next=core->process->NextReceive();
            if(next){current_receive=next.value->Observation();next.value->Run();current_receive={};}
            else {std::unique_lock<std::mutex> lock(gate);changed.wait_for(lock,std::chrono::milliseconds(1),[&]{return closing;});}
        }
    }
    bool Start();
    void Stop() {
        std::vector<std::pair<int,Role>> targets;
        {
            std::unique_lock<std::mutex> lock(gate);
            if(closing) {changed.wait(lock,[&]{return closed;});return;}
            closing=true;
            {std::lock_guard<std::mutex> control_lock(control->mutex_);control->rx_drop_closed_=true;}
            if(current)current->enabled=false;
            for(auto& r:tx)ReleaseTx(r.bytes->size(),r.app);tx.clear();
            changed.notify_all();
        }
        if(application&&application->control->before_tx_join_)application->control->before_tx_join_();
        // Normal stop seals admission, then joins the unique SDK TX caller
        // BEFORE local native close. A previously selected send may finish.
        // A blocked Bind/Send still depends on PF-01; this is no cancellation
        // deadline and no gate is held around either SDK calls or the join.
        if(tx_worker.joinable())tx_worker.join();
        Update([](Stats& s){s.tx_worker_joined=true;});
        Log("Stop_tx_worker_joined",++invocation,-1,Role::Listener,0,nullptr,false);
        {
            std::lock_guard<std::mutex> lock(gate);
            for(auto& o:owners) if(o.id>0&&!o.close_claimed) {o.close_claimed=true;o.ready=false;targets.emplace_back(o.id,o.role);}
        }
        for(auto target:targets)CloseOwner(target.first,target.second);
        if(rx_worker.joinable())rx_worker.join();
        Update([](Stats& s){s.rx_worker_joined=true;});
        targets.clear();
        if(auto last=last_detached_control.lock())if(last->before_final_retire_)last->before_final_retire_();
        {
            std::unique_lock<std::mutex> lock(gate);
            changed.wait(lock,[&]{return callback_count==0&&tx_count==0;});
            for(auto& o:owners) if(o.id>0&&!o.close_claimed){o.close_claimed=true;targets.emplace_back(o.id,o.role);}
            // Claim every owner from callbacks already admitted to the adapter
            // and retire both fixed entrances at the SAME linearization point.
            // SDK events which have not entered by here remain a platform
            // dependency; the tombstone only prevents later application access.
            retired=true;
        }
        for(auto target:targets)CloseOwner(target.first,target.second);
        {
            // Tasks and callbacks can settle before Submit's final log drops
            // its payload reference. Retired ingress prevents new allocations;
            // wait with Control's separate condition, never with gate held.
            std::unique_lock<std::mutex> lock(control->mutex_);
            control->payloads_changed_.wait(lock,[&]{return control->stats_.live_tx_blocks==0&&control->stats_.live_rx_blocks==0;});
        }
        RetireInstance(this,service_record,server_slot,client_slot);
        FlushRxAdmissions();
        {
            std::unique_lock<std::mutex> lock(gate);
            current.reset();closed=true;
            Update([](Stats& s){s.closed=true;s.clean_observed=!s.fatal&&s.sdk_calls_active==0&&s.callbacks_active==0&&s.receiver_leases==0&&s.tx_records==0&&s.rx_records==0&&s.live_tx_blocks==0&&s.live_rx_blocks==0&&s.native_owners==s.shutdown_returns;});
            changed.notify_all();
        }
    }
};

namespace {
void FlushProcessRxAdmissions() {
    std::array<std::shared_ptr<State>,ProcessStats::role_entry_limit> states{};
    auto& registry=Registry();
    {std::lock_guard<std::mutex> lock(registry.mutex);
        for(std::size_t i=0;i<states.size();++i)states[i]=registry.services[i].state;}
    for(const auto& state:states)if(state)state->FlushRxAdmissions();
    std::uint64_t total=0;
    for(std::size_t i=0;i<registry.missing_rx_state.size();++i){
        const auto count=registry.missing_rx_state[i].load(std::memory_order_relaxed);total+=count;
        if(count)std::printf("EXPERIMENT_RX_ENTRY_UNCORRELATED schema=1 callback_slot=%zu reason=MISSING_STATE count=%llu unit=UNCORRELATED_CALLBACK_ENTRY coverage=OBSERVED_UNTIL_SNAPSHOT\n",i,static_cast<unsigned long long>(count));
    }
    std::printf("EXPERIMENT_RX_ENTRY_UNCORRELATED_SUMMARY schema=1 total=%llu unit=UNCORRELATED_CALLBACK_ENTRY coverage=OBSERVED_UNTIL_SNAPSHOT\n",static_cast<unsigned long long>(total));
}
template<std::size_t Slot,bool Server,class F> void Callback(int socket,const char* event,F action) noexcept {
    auto& registry=Registry();
    auto* s=(Server?registry.server_ingress[Slot]:registry.client_ingress[Slot]).load(std::memory_order_acquire);
    std::uint64_t id=0;
    if(!s||!s->Enter(Server,static_cast<int>(Slot+(Server?0:ProcessStats::role_entry_limit)),socket,event,id))return;
    try {action(*s,id);}catch(...){s->Update([](Stats& v){v.fatal=true;});}
    s->Leave();
}
template<std::size_t Slot,bool Server> void Bound(int socket,PeerSocketInfo peer) {Callback<Slot,Server>(socket,"OnBind",[&](State& s,std::uint64_t){s.Bound(Server,socket,peer);});}
template<std::size_t Slot,bool Server> void Received(int socket,const void* data,std::uint32_t size) {
    auto& registry=Registry();constexpr auto slot=Slot+(Server?0:ProcessStats::role_entry_limit);
    auto* state=(Server?registry.server_ingress[Slot]:registry.client_ingress[Slot]).load(std::memory_order_acquire);
    if(!state){registry.missing_rx_state[slot].fetch_add(1,std::memory_order_relaxed);return;}
    RxAdmissionRecord record;record.socket=socket;record.callback_slot=static_cast<int>(slot);record.decision.size=size;
    std::uint64_t id=0;
    const bool entered=state->Enter(Server,static_cast<int>(slot),socket,"OnBytes",id,&record);
    if(entered){
        try{state->Received(Server,socket,data,size,id,record);}
        catch(...){record.decision.Decide(model::RxAdmissionStage::NativeEntry,model::RxAdmissionReason::CallbackException);state->Update([](Stats& v){v.fatal=true;});}
    }
    // State/native/budget gates and transient callback owner are gone. Append
    // before Leave, so admitted callbacks' records precede Stop's drain flush.
    state->RecordRxAdmission(record);
    if(entered)state->Leave();
}
template<std::size_t Slot,bool Server> void Closed(int socket,ShutdownReason reason) {Callback<Slot,Server>(socket,"OnShutdown",[&](State& s,std::uint64_t){s.NativeClosed(Server,socket,reason);});}
template<std::size_t Slot,bool Server> void Error(int socket,std::int32_t code) {Callback<Slot,Server>(socket,"OnError",[&](State& s,std::uint64_t){s.NativeError(Server,socket,code);});}
template<std::size_t Slot> bool Negotiate(int socket,PeerSocketInfo peer) {
    bool accepted=false;Callback<Slot,true>(socket,"OnNegotiate",[&](State& s,std::uint64_t){accepted=s.Negotiate(socket,peer);});return accepted;
}
template<std::size_t Slot,bool Server> ISocketListener FixedTable() {
    ISocketListener table{};table.OnBind=Bound<Slot,Server>;table.OnBytes=Received<Slot,Server>;
    table.OnShutdown=Closed<Slot,Server>;table.OnError=Error<Slot,Server>;
    if(Server)table.OnNegotiate=Negotiate<Slot>;
    return table;
}
template<bool Server,std::size_t... Slots> std::array<ISocketListener,sizeof...(Slots)> FixedTables(std::index_sequence<Slots...>) {
    return {{FixedTable<Slots,Server>()...}};
}
template<bool Server> ISocketListener Table(std::size_t slot) {
    static const auto tables=FixedTables<Server>(std::make_index_sequence<ProcessStats::role_entry_limit>{});
    return tables[slot];
}
class Sender final:public rtps::SenderResource {
public:
    explicit Sender(std::shared_ptr<State> s,std::shared_ptr<Application> a):SenderResource(Codec::kind),state(std::move(s)),application(std::move(a)) {
        send_lambda_=[this](const std::vector<NetworkBuffer>& b,std::uint32_t n,rtps::LocatorsIterator* begin,rtps::LocatorsIterator* end,const Clock::time_point& deadline,std::int32_t){return state->Submit(application,b,n,begin,end,deadline);};
    }
    void add_locators_to_list(rtps::LocatorList_t& out) const override {out.push_back(state->config.local_locator);}
    const std::shared_ptr<State> state;
    const std::shared_ptr<Application> application;
};
bool ValidConfig(const Config& c) {
#ifndef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    if(c.enable_rx_drop) return false;
#endif
    if(c.additional_peers.size()>1)return false;
    dsoftbus::v1::EndpointAddress local{},peer{};
    dsoftbus::v1::ServiceAddress ls{},ps{};
    if(!Codec::DecodeLocator(c.local_locator,local)||!Codec::DecodeLocator(c.peer_locator,peer)||
       !DecodeNativeServiceName(c.local_service,ls)||!DecodeNativeServiceName(c.peer_service,ps))return false;
    const auto label=[](unsigned char x){return (x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'||x=='_';};
    const bool primary=c.instance_name.size()<=32&&std::all_of(c.instance_name.begin(),c.instance_name.end(),label)&&
        local.profile==dsoftbus::v1::Profile::BytesNative&&peer.profile==local.profile&&ls.profile==local.profile&&ps.profile==peer.profile&&
        ls.slot==local.slot&&ps.slot==peer.slot&&ls.name_space==ps.name_space&&ls.domain==ps.domain&&c.local_locator!=c.peer_locator&&
        c.peer_network_id.size()==64&&c.peer_network_id.find('\0')==std::string::npos&&c.package_name=="com.kaihong.mdds"&&
        !c.run_id.empty()&&c.run_id.size()<=96&&std::all_of(c.run_id.begin(),c.run_id.end(),[](unsigned char x){return (x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'||x=='_';})&&
        c.max_message_size>=512&&c.max_message_size<=60000;
    if(!primary)return false;
    for(const auto& extra:c.additional_peers){dsoftbus::v1::EndpointAddress endpoint{};dsoftbus::v1::ServiceAddress service{};
        if(!Codec::DecodeLocator(extra.locator,endpoint)||!DecodeNativeServiceName(extra.service,service)||
           endpoint.profile!=local.profile||service.profile!=local.profile||service.slot!=endpoint.slot||
           service.name_space!=ls.name_space||service.domain!=ls.domain||extra.locator==c.local_locator||extra.locator==c.peer_locator||
           extra.network_id.size()!=64||extra.network_id.find('\0')!=std::string::npos||
           (extra.service==c.peer_service&&extra.network_id==c.peer_network_id))return false;
    }
    return true;
}
}


bool State::Start(){
    if(initialized)return true;auto core=runtime.lock();if(!core)return false;
    if(!ReserveInstance(shared_from_this(),config,control,service_record,server_slot,client_slot,instance_id))return false;
    Identify();server_table=Table<true>(server_slot);client_table=Table<false>(client_slot);
    auto ns=core->process->Native().AcquireNamespace(model_local,core->native_config);if(!ns){Stop();return false;}native_namespace=*ns.value;
    auto reservation=core->process->Native().ReserveListener(native_namespace,model_local);
    if(!reservation||!reservation.value->socket_call){Stop();return false;}
    if(reservation.value->socket_call->CallEntered()!=model::NativeStatus::None){Stop();return false;}
    SocketInfo info{};info.name=const_cast<char*>(config.local_service.c_str());info.pkgName=const_cast<char*>(config.package_name.c_str());info.dataType=DATA_TYPE_BYTES;
    const int socket=Call("Socket_listener_enter",-1,Role::Listener,[&]{return api.create(info);});
    const auto complete=reservation.value->socket_call->Complete(socket>0?model::NativeSocketResult::Id(socket):model::NativeSocketResult::Error(socket));
    if(socket<=0||complete.status!=model::NativeStatus::None){Stop();return false;}
    if(!ClaimNativeOwner(this,socket)){Update([](Stats& v){v.fatal=true;});Stop();return false;}
    {std::lock_guard<std::mutex> lock(gate);owners[0]={socket,Role::Listener,false,false,false,false,reservation.value->owner};Update([](Stats& v){++v.native_owners;++v.listener_creations;});}
    auto ticket=core->process->Native().BeginListen(reservation.value->owner);
    if(!ticket||ticket.value->CallEntered()!=model::NativeStatus::None){Stop();return false;}
    const QosTV qos[]={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};
    ExposeEntry(this,server_slot,true);
    const int rc=Call("Listen_enter",socket,Role::Listener,[&]{return api.listen(socket,qos,2,&server_table);});
    ticket.value->Complete(rc==0?model::SyncListenResult::Success():model::SyncListenResult::Error(rc));
    if(rc!=0){Stop();return false;}
    try{tx_worker=std::thread([self=shared_from_this()]{self->TxLoop();});rx_worker=std::thread([self=shared_from_this()]{self->RxLoop();});}catch(...){Stop();return false;}
    {std::lock_guard<std::mutex> lock(gate);initialized=true;}
    std::printf("stage=Listen action=RETURN rc=0 readiness=REAL_FASTDDS_DSOFTBUS_EXPERIMENT run=%s instance_name=%s instance_id=%llu runtime_id=%llu server_callback_slot=%d client_callback_slot=%d\n",config.run_id.c_str(),Label(),static_cast<unsigned long long>(instance_id),static_cast<unsigned long long>(core->id),server_slot,client_slot+static_cast<int>(ProcessStats::role_entry_limit));std::fflush(stdout);return true;
}

void Application::OnDataReceived(const std::uint8_t* data,std::uint32_t size,const Locator& local,const Locator& remote){
    const auto diagnostic=current_receive;const auto native=diagnostic.native.Observe();
    if(local!=state->model_local||remote!=diagnostic.native.remote())throw std::logic_error("captured input route mismatch");
    const auto peer=state->ModelPeerIndex(remote);
    const int socket=native.owned_id.value_or(-1);const Role role=native.role==model::NativeOwnerRole::Dial?Role::Outbound:Role::Accepted;
    const ByteSpan bytes{data,size};
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    if(state->MaybeDrop(bytes,diagnostic.correlation,socket,role,control))return;
#endif
    state->LogSpan("FastDDS_receiver_enter",diagnostic.correlation,socket,role,0,&bytes,true,diagnostic.identity.receiver_incarnation,peer);
    Update([](Stats& v){++v.receiver_calls;});
    try{receiver->OnDataReceived(data,size,state->config.local_locator,PeerLocator(state->config,peer));}catch(...){Update([](Stats& v){v.fatal=true;});throw;}
    state->LogSpan("FastDDS_receiver_return",diagnostic.correlation,socket,role,0,&bytes,true,diagnostic.identity.receiver_incarnation,peer);
    const auto seen=ObserveRtps(data,size);Update([&](Stats& v){++v.receiver_returns;v.rx_spdp+=seen.spdp;v.rx_sedp+=seen.sedp;v.rx_sedp_publications+=seen.sedp_publications;v.rx_sedp_subscriptions+=seen.sedp_subscriptions;v.rx_user+=seen.user;v.rx_data_frag+=seen.fragments.size();});
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    state->ObserveDeliveredRange(bytes,diagnostic.correlation,socket,role,seen,control);
#endif
}
std::shared_ptr<Application> RuntimeCore::FindApp(std::uint64_t id){std::lock_guard<std::mutex> lock(gate);auto it=apps.find(id);return it==apps.end()?nullptr:it->second.lock();}
void RuntimeObserver::OnReceive(model::ReceiveStage stage,const model::ReceiveDiagnostic& d,const std::uint8_t* bytes,std::size_t size)noexcept{
    auto runtime=core.lock();if(!runtime)return;auto app=runtime->FindApp(d.identity.application_incarnation);if(!app)return;
    try{
        const auto key=std::make_pair(d.identity.application_incarnation,d.correlation);
        const auto native=d.native.Observe();const auto role=native.role==model::NativeOwnerRole::Dial?Role::Outbound:Role::Accepted;
        auto* observation=CurrentPerformanceObservation();
        if(observation)observation->Text(2);
        if(!observation||!observation->Quiet()) {
        std::printf("EXPERIMENT_APPLICATION_RX stage=%u runtime_id=%llu instance_id=%llu application_incarnation=%llu receiver_incarnation=%llu logical_token=%llu invocation=%llu socket=%d bytes=%zu process_steady_us=%lld\n",static_cast<unsigned>(stage),static_cast<unsigned long long>(runtime->id),static_cast<unsigned long long>(app->state->instance_id),static_cast<unsigned long long>(d.identity.application_incarnation),static_cast<unsigned long long>(d.identity.receiver_incarnation),static_cast<unsigned long long>(d.identity.logical_token),static_cast<unsigned long long>(d.correlation),native.owned_id.value_or(-1),d.size,ProcessSteadyUs());
        }
        if(stage==model::ReceiveStage::Captured){if(app->control->before_rx_copy_)app->control->before_rx_copy_();}
        else if(stage==model::ReceiveStage::OwnedCopy){
            {std::lock_guard<std::mutex> lock(runtime->gate);runtime->rx_accounting.emplace(key,RuntimeCore::RxAccounting{app});}
            const auto account=[&](Stats& v){++v.rx_owned;++v.rx_records;v.rx_bytes+=d.size;++v.live_rx_blocks;v.live_rx_payload_bytes+=d.size;};app->Update(account);app->state->Update(account);
            const ByteSpan span{bytes,size};app->state->LogSpan("OnBytes_owned_copy",d.correlation,native.owned_id.value_or(-1),role,0,&span,true,d.identity.receiver_incarnation,app->state->ModelPeerIndex(d.native.remote()));
        } else if(stage==model::ReceiveStage::Queued){if(app->control->after_rx_queue_)app->control->after_rx_queue_();}
        else if(stage==model::ReceiveStage::Admitted){
            {std::lock_guard<std::mutex> lock(runtime->gate);auto it=runtime->rx_accounting.find(key);if(it!=runtime->rx_accounting.end())it->second.admitted=true;}
            app->Update([](Stats& v){++v.receiver_leases;});if(app->control->after_rx_admit_)app->control->after_rx_admit_();
        }else if(stage==model::ReceiveStage::Settled||stage==model::ReceiveStage::RecordReleased){
            bool settle=false,admitted=false,release=stage==model::ReceiveStage::RecordReleased;
            {std::lock_guard<std::mutex> lock(runtime->gate);auto it=runtime->rx_accounting.find(key);if(it==runtime->rx_accounting.end())return;settle=!it->second.settled;admitted=it->second.admitted;it->second.settled=true;if(release)runtime->rx_accounting.erase(it);}
            if(settle){const auto f=[&](Stats& v){--v.rx_records;v.rx_bytes-=d.size;};app->Update(f);app->state->Update(f);if(admitted)app->Update([](Stats& v){--v.receiver_leases;});}
            if(stage==model::ReceiveStage::Settled&&app->control->after_rx_settle_)app->control->after_rx_settle_();
            if(release){const auto f=[&](Stats& v){--v.live_rx_blocks;v.live_rx_payload_bytes-=d.size;};app->Update(f);app->state->Update(f);app->control->payloads_changed_.notify_all();app->state->control->payloads_changed_.notify_all();}
        }
        app->state->changed.notify_all();runtime->changed.notify_all();
    }catch(...){app->Update([](Stats& v){v.fatal=true;});}
}
namespace {
bool SameConfig(const Config& a,const Config& b){return a.run_id==b.run_id&&a.instance_name==b.instance_name&&a.local_locator==b.local_locator&&a.peer_locator==b.peer_locator&&a.local_service==b.local_service&&a.peer_service==b.peer_service&&a.peer_network_id==b.peer_network_id&&a.package_name==b.package_name&&a.max_message_size==b.max_message_size&&a.enable_rx_drop==b.enable_rx_drop&&a.additional_peers.size()==b.additional_peers.size()&&std::equal(a.additional_peers.begin(),a.additional_peers.end(),b.additional_peers.begin(),[](const PeerConfig& x,const PeerConfig& y){return x.locator==y.locator&&x.service==y.service&&x.network_id==y.network_id;});}
bool SameApi(const SocketApi& a,const SocketApi& b){return a.create==b.create&&a.listen==b.listen&&a.bind==b.bind&&a.send_bytes==b.send_bytes&&a.shutdown==b.shutdown;}
}
bool RuntimeCore::Build(){
    if(configs.empty()||configs.size()>ProcessStats::state_limit||!api.create||!api.listen||!api.bind||!api.send_bytes||!api.shutdown)return false;
    native_config.service_pkg_name=configs.front().package_name;native_config.limits.total_owners=12;
    // Each fixed endpoint can have one setup caller (listener initialization
    // or its unique TX worker). Preserve that bounded concurrency across the
    // shared runtime, rather than failing Q while P1 has a valid setup ticket.
    native_config.limits.setup_calls=configs.size();native_config.limits.close_calls=1;
    native_config.allow_closed_dial_retry=true;
    std::vector<std::pair<Locator,Locator>> aliases;
    auto alias=[&](const Locator& wire){for(const auto& p:aliases)if(p.first==wire)return p.second;model::EndpointId id{};id[0]=0x41;id[15]=static_cast<std::uint8_t>(aliases.size()+2);Locator value;model::LocatorCodec::Encode(id,value);aliases.emplace_back(wire,value);return value;};
    for(const auto& c:configs){if(!ValidConfig(c)||c.package_name!=native_config.service_pkg_name)return false;
        const auto local=alias(c.local_locator);
        native_config.directory.locals.push_back({local,c.local_service,c.local_service});
        for(std::size_t i=0;i<PeerCount(c);++i){model::Route r;r.key={local,alias(PeerLocator(c,i))};r.fields={PeerNetwork(c,i),PeerService(c,i),c.local_service,{PeerService(c,i)},model::RecordDataType::Bytes};native_config.directory.routes.push_back(r);native_config.directions.push_back(model::RouteDirection::Both);}
    }
    model::AttachmentLimits limits;limits.logical_slots=configs.size();limits.retained_applications=4;limits.records=128;limits.bytes=2*1024*1024;limits.local_records=64;limits.local_bytes=1024*1024;
    observer=std::make_shared<RuntimeObserver>();observer->core=shared_from_this();
    auto built=model::ProcessCoordinator::Build(native_config,limits,model::ProcessClock{},std::chrono::seconds(2),observer);if(!built)return false;process=std::move(*built.value);
    std::printf("EXPERIMENT_RUNTIME_BUDGET runtime_id=%llu declared_endpoints=%zu setup_limit=%zu send_limit=%zu close_limit=%zu setup_capacity=%zu\n",
        static_cast<unsigned long long>(id),configs.size(),native_config.limits.setup_calls,native_config.limits.send_calls,native_config.limits.close_calls,model::NativeLimits::setup_call_capacity);
    for(std::size_t i=0;i<configs.size();++i){auto state=std::make_shared<State>(configs[i],api,std::make_shared<Control>());state->runtime=shared_from_this();state->model_local=alias(configs[i].local_locator);for(std::size_t p=0;p<PeerCount(configs[i]);++p)state->model_peers[p]=alias(PeerLocator(configs[i],p));states.push_back(state);}attaching.resize(configs.size());return true;
}
std::shared_ptr<Application> RuntimeCore::Attach(const Config& config,const std::shared_ptr<Control>& control){
    std::size_t index=configs.size();
    {std::lock_guard<std::mutex> lock(gate);if(stopping||stopped)return {};for(std::size_t i=0;i<configs.size();++i)if(SameConfig(configs[i],config)){index=i;break;}if(index==configs.size()||attaching[index])return {};attaching[index]=true;}
    auto state=states[index];std::shared_ptr<Application> app;model::ProcessApplication prepared_claim;
    auto done=[&]{std::lock_guard<std::mutex> lock(gate);attaching[index]=false;changed.notify_all();};
    try{
        {std::lock_guard<std::mutex> lock(state->gate);if(state->application){done();return {};}}
        std::vector<model::EgressSelection> selections;selections.reserve(PeerCount(config));for(std::size_t peer=0;peer<PeerCount(config);++peer)selections.push_back({state->model_peers[peer],state->model_local,1});
        auto created=process->AttachApplication({state->model_local},selections,{64,1024*1024,64,1024*1024,config.max_message_size});
        if(!created){done();return {};}
        prepared_claim=*created.value;
        if(control->before_app_allocate_)control->before_app_allocate_();
        app=std::make_shared<Application>(control);app->claim=*created.value;app->runtime=shared_from_this();app->state=state.get();const auto identity=process->ObserveApplication(app->claim);app->id=identity.application_incarnation;app->token=identity.logical_token;
        {std::lock_guard<std::mutex> lock(control->mutex_);if(control->stats_.application_incarnation){process->BeginDetach(app->claim);process->ProgressDetach(app->claim);done();return {};}control->instance_name_=config.instance_name.empty()?"single":config.instance_name;control->native_control_=state->control;control->stats_.runtime_id=id;control->stats_.application_incarnation=identity.application_incarnation;control->stats_.logical_token=identity.logical_token;
#ifdef FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
        control->rx_drop_enabled_=config.enable_rx_drop;
#endif
        }
        if(control->before_attach_publish_)control->before_attach_publish_();
        {std::lock_guard<std::mutex> lock(state->gate);state->application=app;}
        {std::lock_guard<std::mutex> lock(gate);apps.emplace(app->id,app);}
        if(!state->Start()){Detach(app);done();return {};}
        std::printf("EXPERIMENT_APPLICATION_ATTACH runtime_id=%llu instance_id=%llu application_incarnation=%llu logical_token=%llu service=%s\n",static_cast<unsigned long long>(id),static_cast<unsigned long long>(state->instance_id),static_cast<unsigned long long>(app->id),static_cast<unsigned long long>(identity.logical_token),config.local_service.c_str());std::fflush(stdout);done();return app;
    }catch(...){if(app)Detach(app);else if(prepared_claim){process->BeginDetach(prepared_claim);process->ProgressDetach(prepared_claim);}done();return {};}
}
void RuntimeCore::Detach(const std::shared_ptr<Application>& app){
    auto* state=app->state;
    {std::unique_lock<std::mutex> lock(state->gate);if(app->detached)return;if(app->detaching){state->changed.wait(lock,[&]{return app->detached;});return;}app->detaching=true;if(state->current)state->current->enabled=false;
        for(auto it=state->tx.begin();it!=state->tx.end();){if(it->app==app){state->ReleaseTx(it->bytes->size(),app);it=state->tx.erase(it);}else ++it;}}
    process->BeginDetach(app->claim);
    if(app->control->detach_requested_)app->control->detach_requested_();
    if(app->control->before_tx_join_)app->control->before_tx_join_();
    for(;;){
        bool tx_done;{std::lock_guard<std::mutex> lock(state->gate);tx_done=app->tx_records==0&&app->submits==0;}
        if(tx_done&&process->ProgressDetach(app->claim).phase==model::ProcessApplicationPhase::Detached)break;
        std::unique_lock<std::mutex> lock(state->gate);state->changed.wait_for(lock,std::chrono::milliseconds(1));
    }
    {std::unique_lock<std::mutex> lock(app->control->mutex_);app->control->payloads_changed_.wait(lock,[&]{return app->control->stats_.live_tx_blocks==0&&app->control->stats_.live_rx_blocks==0;});app->control->rx_drop_closed_=true;app->control->stats_.application_detached=true;app->control->stats_.app_clean_observed=!app->control->stats_.fatal&&app->control->stats_.tx_records==0&&app->control->stats_.rx_records==0&&app->control->stats_.receiver_leases==0;}
    {std::lock_guard<std::mutex> lock(state->gate);app->binding={};app->claim={};app->receiver=nullptr;state->current.reset();if(state->application==app)state->application.reset();state->last_detached_control=app->control;app->detached=true;state->changed.notify_all();}
    {std::lock_guard<std::mutex> lock(gate);apps.erase(app->id);changed.notify_all();}
    std::printf("EXPERIMENT_APPLICATION_DETACHED runtime_id=%llu instance_id=%llu application_incarnation=%llu native_table_retained=1 process_steady_us=%lld\n",static_cast<unsigned long long>(id),static_cast<unsigned long long>(state->instance_id),static_cast<unsigned long long>(app->id),ProcessSteadyUs());std::fflush(stdout);
}
bool RuntimeCore::Stop(){
    {std::unique_lock<std::mutex> lock(gate);if(stopped)return stop_result;if(stopping){changed.wait(lock,[&]{return stopped;});return stop_result;}if(!apps.empty()||std::any_of(attaching.begin(),attaching.end(),[](bool b){return b;}))return false;stopping=true;}
    process->BeginShutdown();for(const auto& state:states)state->Stop();process->Native().RequestServiceClose();
    bool clean=process->PollShutdown().phase==model::ProcessShutdownPhase::ApplicationsDrained;
    for(const auto& state:states)clean=clean&&state->control->Snapshot().clean_observed;
    {std::lock_guard<std::mutex> lock(gate);stop_result=clean;stopped=true;changed.notify_all();}return clean;
}
RuntimeStats RuntimeCore::Snapshot()const{
    RuntimeStats out;out.runtime_id=id;{std::lock_guard<std::mutex> lock(gate);out.stopped=stopped;}
    auto rx=process->ReceiveUsage();out.active_applications=rx.active_applications;out.retained_applications=rx.retained_applications;out.queued_rx=rx.records;out.queued_bytes=rx.bytes;out.copy_active=rx.copying;out.delivery_active=rx.delivering;
    for(const auto& state:states){std::lock_guard<std::mutex> lock(state->gate);out.active_native_endpoints+=state->initialized&&!state->closed;out.active_receivers+=state->current&&state->current->enabled;out.queued_tx+=state->tx_count;out.queued_bytes+=state->tx_bytes;for(const auto& o:state->owners)out.native_owners+=o.id>0&&!o.close_claimed;}
    const auto p=GetProcessStats();out.exposed_entries=p.exposed_entries;out.tombstone_entries=p.tombstone_entries;return out;
}
struct ProcessRuntime::Impl {std::shared_ptr<RuntimeCore> core;explicit Impl(std::shared_ptr<RuntimeCore> c):core(std::move(c)){};};
ProcessRuntime::ProcessRuntime(std::unique_ptr<Impl> p):impl_(std::move(p)){}
ProcessRuntime::~ProcessRuntime(){if(impl_&&impl_->core)impl_->core->Stop();}
namespace {std::function<void(std::uint64_t)>& AcquireRetainedHook(){static std::function<void(std::uint64_t)> hook;return hook;}}
void ProcessRuntime::SetAcquireRetainedHook(std::function<void(std::uint64_t)> hook){AcquireRetainedHook()=std::move(hook);}
std::shared_ptr<ProcessRuntime> ProcessRuntime::Acquire(const std::vector<Config>& configs,SocketApi api){
    struct Registry {std::mutex gate;std::vector<std::weak_ptr<ProcessRuntime>> entries;std::uint64_t next=0;};static Registry registry;
    // Declare every potential last ProcessRuntime owner BEFORE the guard.
    // Resizing happens before weak.lock acquires any ownership. Thus normal
    // iteration, early return, allocation failure and exception unwinding all
    // destroy the registry guard before releasing any captured/new runtime.
    std::unique_ptr<ProcessRuntime> unshared;
    std::shared_ptr<ProcessRuntime> created;
    std::vector<std::shared_ptr<ProcessRuntime>> retained;
    std::lock_guard<std::mutex> lock(registry.gate);
    retained.resize(registry.entries.size());std::size_t captured=0;
    for(auto it=registry.entries.begin();it!=registry.entries.end();){
        auto& runtime=retained[captured++];runtime=it->lock();
        if(!runtime){it=registry.entries.erase(it);continue;}auto core=runtime->impl_->core;
        if(AcquireRetainedHook())AcquireRetainedHook()(core->id);
        if(SameApi(core->api,api)&&configs.size()==core->configs.size()&&std::equal(configs.begin(),configs.end(),core->configs.begin(),SameConfig)){
            std::lock_guard<std::mutex> core_lock(core->gate);
            if(core->stopping&&!core->stopped)return {};
            if(!core->stopped)return runtime;
        }
        ++it;
    }
    try{
        auto core=std::make_shared<RuntimeCore>(++registry.next,configs,api);if(!core->Build())return {};
        unshared.reset(new ProcessRuntime(std::make_unique<Impl>(core)));
        // Unlike the raw-pointer constructor, shared_ptr(unique_ptr&&) leaves
        // the unique owner intact if its control-block allocation fails.
        created=std::shared_ptr<ProcessRuntime>(std::move(unshared));
        registry.entries.push_back(created);return created;
    }catch(...){return {};}
}
bool ProcessRuntime::Finalize(){return impl_->core->Stop();}
RuntimeStats ProcessRuntime::Snapshot()const{return impl_->core->Snapshot();}
RxAdmissionSummary ProcessRuntime::ReceiveAdmissionSummary(std::size_t endpoint)const{
    return endpoint<impl_->core->states.size()?impl_->core->states[endpoint]->RxSummary():RxAdmissionSummary{};
}
RxAdmissionRecord ProcessRuntime::ReceiveAdmissionRecord(std::size_t endpoint,std::size_t index)const{
    return endpoint<impl_->core->states.size()?impl_->core->states[endpoint]->RxRecord(index):RxAdmissionRecord{};
}

ExperimentalDescriptor::ExperimentalDescriptor(Config c,SocketApi a,std::shared_ptr<Control> s,std::shared_ptr<ProcessRuntime> r)
    :TransportDescriptorInterface(c.max_message_size,1),config(std::move(c)),api(a),control(std::move(s)),runtime(std::move(r)){}
rtps::TransportInterface* ExperimentalDescriptor::create_transport() const {return new ExperimentalTransport(*this);}
ExperimentalTransport::ExperimentalTransport(const ExperimentalDescriptor& d):TransportInterface(Codec::kind),descriptor_(d){}
ExperimentalTransport::~ExperimentalTransport(){shutdown();}
bool ExperimentalTransport::init(const rtps::PropertyPolicy*,const std::uint32_t& nofrag) {
    if(application_||!descriptor_.control||!ValidConfig(descriptor_.config)||nofrag>descriptor_.config.max_message_size)return false;
    runtime_=descriptor_.runtime;implicit_runtime_=!runtime_;
    if(!runtime_)runtime_=ProcessRuntime::Acquire({descriptor_.config},descriptor_.api);
    if(!runtime_)return false;
    if(!SameApi(runtime_->impl_->core->api,descriptor_.api))return false;
    application_=runtime_->impl_->core->Attach(descriptor_.config,descriptor_.control);
    if(!application_){if(implicit_runtime_)runtime_->Finalize();runtime_.reset();return false;}
    state_=runtime_->impl_->core->states.at(static_cast<std::size_t>(std::find_if(runtime_->impl_->core->configs.begin(),runtime_->impl_->core->configs.end(),[&](const Config& c){return c.local_locator==descriptor_.config.local_locator;})-runtime_->impl_->core->configs.begin()));
    return true;
}
bool ExperimentalTransport::IsInputChannelOpen(const Locator& l)const {
    if(!state_||l!=descriptor_.config.local_locator)return false;
    std::lock_guard<std::mutex> lock(state_->gate);return !state_->closing&&state_->current&&state_->current->enabled;
}
bool ExperimentalTransport::IsLocatorSupported(const Locator& l)const{return Codec::Supports(l,dsoftbus::v1::Profile::BytesNative);}
bool ExperimentalTransport::is_locator_allowed(const Locator& l)const{return IsLocatorSupported(l)&&(l==descriptor_.config.local_locator||PeerIndex(descriptor_.config,l)<peer_limit);}
bool ExperimentalTransport::is_locator_reachable(const Locator& l){return IsLocatorSupported(l)&&PeerIndex(descriptor_.config,l)<peer_limit;}
Locator ExperimentalTransport::RemoteToMainLocal(const Locator& l)const{return PeerIndex(descriptor_.config,l)<peer_limit?descriptor_.config.local_locator:Locator{};}
bool ExperimentalTransport::transform_remote_locator(const Locator& l,Locator& out)const{if(!IsLocatorSupported(l)||PeerIndex(descriptor_.config,l)==peer_limit)return false;out=l;return true;}
bool ExperimentalTransport::OpenOutputChannel(rtps::SendResourceList& list,const Locator& l){
    if(!state_||!application_||!is_locator_allowed(l))return false;
    {std::lock_guard<std::mutex> lock(state_->gate);if(state_->closing||!state_->initialized)return false;}
    for(const auto& r:list)if(const auto* s=dynamic_cast<const Sender*>(r.get()))if(s->state==state_)return true;
    list.push_back(std::make_unique<Sender>(state_,application_));return true;
}
bool ExperimentalTransport::OpenOutputChannels(rtps::SendResourceList& list,const rtps::LocatorSelectorEntry& e){
    for(auto i:e.state.unicast)if(i<e.unicast.size()&&PeerIndex(descriptor_.config,e.unicast[i])<peer_limit)return OpenOutputChannel(list,e.unicast[i]);return false;
}
void ExperimentalTransport::CloseOutputChannels(rtps::SendResourceList&,const rtps::LocatorSelectorEntry&){}
bool ExperimentalTransport::OpenInputChannel(const Locator& l,rtps::TransportReceiverInterface* receiver,std::uint32_t capacity){
    if(!state_||!application_||l!=descriptor_.config.local_locator||!receiver||capacity<descriptor_.config.max_message_size)return false;
    auto core=runtime_->impl_->core;
    {std::lock_guard<std::mutex> lock(state_->gate);if(application_->detaching||state_->closing)return false;
      if(application_->receiver)return application_->receiver==receiver;application_->receiver=receiver;}
    auto opened=core->process->OpenReceiver(application_->claim,state_->model_local,application_.get());
    if(opened.result!=model::ReceiverOpenResult::Opened&&opened.result!=model::ReceiverOpenResult::AlreadyOpen){std::lock_guard<std::mutex> lock(state_->gate);application_->receiver=nullptr;return false;}
    application_->binding=opened.binding;const auto identity=core->process->ObserveReceiver(opened.binding);
    {std::lock_guard<std::mutex> lock(state_->gate);state_->current=std::make_shared<State::Lease>();state_->current->receiver=receiver;state_->current->id=identity.receiver_incarnation;}
    application_->Update([&](Stats& v){v.receiver_incarnation=identity.receiver_incarnation;v.receiver_lease_id=identity.receiver_incarnation;});
    return true;
}
bool ExperimentalTransport::CloseInputChannel(const Locator& l){
    if(!state_||!application_||l!=descriptor_.config.local_locator)return false;
    auto core=runtime_->impl_->core;
    {std::unique_lock<std::mutex> lock(state_->gate);if(state_->input_closing){state_->changed.wait(lock,[&]{return !state_->input_closing;});return true;}if(!application_->receiver)return true;state_->input_closing=true;if(state_->current)state_->current->enabled=false;}
    core->process->CloseReceiver(application_->binding);
    {std::lock_guard<std::mutex> lock(state_->gate);application_->binding={};application_->receiver=nullptr;state_->current.reset();state_->input_closing=false;state_->changed.notify_all();}return true;
}
bool ExperimentalTransport::DoInputLocatorsMatch(const Locator& a,const Locator& b)const{return a==b&&a==descriptor_.config.local_locator;}
rtps::LocatorList ExperimentalTransport::NormalizeLocator(const Locator& l){rtps::LocatorList out;if(is_locator_allowed(l))out.push_back(l);return out;}
void ExperimentalTransport::select_locators(rtps::LocatorSelector& selector)const{
    auto& entries=selector.transport_starts();std::array<bool,peer_limit> selected{};
    for(std::size_t i=0;i<entries.size();++i){auto* e=entries[i];if(!e->transport_should_process||!e->allowed_to_send)continue;
        for(std::size_t j=0;j<e->unicast.size();++j){const auto peer=PeerIndex(descriptor_.config,e->unicast[j]);if(peer<peer_limit&&!selected[peer]){
            const bool exists=std::find(e->state.unicast.begin(),e->state.unicast.end(),j)!=e->state.unicast.end();
            if(exists||e->state.unicast.push_back(j)){selected[peer]=true;selector.select(i);}
        }}
    }
}
bool ExperimentalTransport::is_local_locator(const Locator& l)const{return l==descriptor_.config.local_locator;}
rtps::TransportDescriptorInterface* ExperimentalTransport::get_configuration(){return &descriptor_;}
void ExperimentalTransport::AddDefaultOutputLocator(rtps::LocatorList& out){out.push_back(descriptor_.config.local_locator);}
bool ExperimentalTransport::getDefaultMetatrafficMulticastLocators(rtps::LocatorList&,std::uint32_t)const{return false;}
bool ExperimentalTransport::getDefaultMetatrafficUnicastLocators(rtps::LocatorList& out,std::uint32_t)const{out.push_back(descriptor_.config.local_locator);return true;}
bool ExperimentalTransport::getDefaultUnicastLocators(rtps::LocatorList& out,std::uint32_t)const{out.push_back(descriptor_.config.local_locator);return true;}
bool ExperimentalTransport::fillMetatrafficMulticastLocator(Locator&,std::uint32_t)const{return false;}
bool ExperimentalTransport::fillMetatrafficUnicastLocator(Locator& l,std::uint32_t)const{return l==descriptor_.config.local_locator;}
bool ExperimentalTransport::configureInitialPeerLocator(Locator& l,const rtps::PortParameters&,std::uint32_t,rtps::LocatorList& out)const{if(PeerIndex(descriptor_.config,l)==peer_limit)return false;out.push_back(l);return true;}
bool ExperimentalTransport::fillUnicastLocator(Locator& l,std::uint32_t)const{return l==descriptor_.config.local_locator;}
std::uint32_t ExperimentalTransport::max_recv_buffer_size()const{return descriptor_.config.max_message_size;}
void ExperimentalTransport::shutdown(){
    if(application_&&runtime_){runtime_->impl_->core->Detach(application_);application_.reset();}
    if(implicit_runtime_&&runtime_)runtime_->Finalize();
}
} // namespace fastdds_dsoftbus::experiment
