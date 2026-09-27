#include "PublicSocketRtpsObservation.hpp"
#include <algorithm>
#include <cstring>
// Exact parser body extracted from reviewed ExperimentalTransport.cpp at6943b6c.
namespace mdds::experiment {
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

}
