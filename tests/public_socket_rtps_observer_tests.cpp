#include "../probes/PublicSocketRtpsObservation.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// Deterministic observer-allocation failure; used only by this standalone test.
static bool fail_allocations = false;
void* operator new(std::size_t size)
{
    if (fail_allocations) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {
using Packet = std::vector<std::uint8_t>;
using Writer = std::array<std::uint8_t, 4>;
constexpr Writer first_writer{{0, 0, 1, 3}}, second_writer{{0, 0, 2, 3}};

Packet Header()
{
    Packet packet(20, 0);
    packet[0] = 'R'; packet[1] = 'T'; packet[2] = 'P'; packet[3] = 'S';
    packet[4] = 2; packet[5] = 3;
    for (std::size_t i = 0; i < 12; ++i) packet[8 + i] = static_cast<std::uint8_t>(i + 1);
    return packet;
}

void AppendHeader(Packet& packet, std::uint8_t id, std::uint8_t flags, std::uint16_t length)
{
    packet.push_back(id); packet.push_back(flags);
    packet.push_back(static_cast<std::uint8_t>((flags & 1) ? length : length >> 8));
    packet.push_back(static_cast<std::uint8_t>((flags & 1) ? length >> 8 : length));
}

void AppendData(Packet& packet, const Writer& writer = first_writer, bool little = true, bool terminal = false)
{
    AppendHeader(packet, 0x15, little ? 1 : 0, terminal ? 0 : 20);
    const auto body = packet.size();
    packet.resize(body + 20, 0);
    std::copy(writer.begin(), writer.end(), packet.begin() + body + 8);
}

bool Observes(const char* name, const Packet& packet, std::uint64_t count,
        const Writer& expected_first = first_writer)
{
    const auto original = packet;
const auto observed = fastdds_dsoftbus::experiment::ObserveRtps(packet.data(), packet.size());
    const bool prefix = std::equal(packet.begin() + 8, packet.begin() + 20, observed.first_writer_guid.begin());
    const bool writer = std::equal(expected_first.begin(), expected_first.end(), observed.first_writer_guid.begin() + 12);
    const bool pass = observed.valid && observed.user == count && prefix && writer && packet == original;
    std::printf("%s %s valid=%d user=%llu first_guid_matches=%d unchanged=%d\n",
        pass ? "PASS" : "FAIL", name, observed.valid, static_cast<unsigned long long>(observed.user),
        prefix && writer, packet == original);
    return pass;
}

void Put(Packet& packet, std::size_t offset, std::uint32_t value, unsigned width, bool little)
{
    for (unsigned i = 0; i < width; ++i)
        packet[offset + i] = static_cast<std::uint8_t>(value >> (8 * (little ? i : width - 1 - i)));
}

bool DataContracts(bool little)
{
    auto packet=Header();const auto first=packet.size();AppendData(packet,first_writer,little);
    Put(packet,first+4+12,0x01020304,4,little);Put(packet,first+4+16,0xa1b2c3d4,4,little);
    AppendHeader(packet,0x0c,1,20);const auto source=packet.size();packet.resize(source+20,0);
    for(unsigned i=0;i<12;++i)packet[source+8+i]=static_cast<std::uint8_t>(0x80+i);
    const auto second=packet.size();AppendData(packet,second_writer,!little);
    Put(packet,second+4+12,0xffffffff,4,!little);Put(packet,second+4+16,0x50607080,4,!little);
const auto original=packet;const auto seen=fastdds_dsoftbus::experiment::ObserveRtps(packet.data(),packet.size());
    bool pass=seen.valid&&seen.data.size()==2&&seen.data_submessages==2&&seen.fragments.empty()&&packet==original&&
        std::equal(packet.begin()+8,packet.begin()+20,seen.first_writer_guid.begin())&&
        std::equal(first_writer.begin(),first_writer.end(),seen.first_writer_guid.begin()+12);
    if(seen.data.size()==2){const auto& a=seen.data[0];const auto& b=seen.data[1];
        pass=pass&&a.sequence_hi==0x01020304&&a.sequence_lo==0xa1b2c3d4&&a.submessage_offset==first&&a.submessage_body_bytes==20&&
            std::equal(packet.begin()+8,packet.begin()+20,a.writer_guid.begin())&&std::equal(first_writer.begin(),first_writer.end(),a.writer_guid.begin()+12)&&
            b.sequence_hi==-1&&b.sequence_lo==0x50607080&&b.submessage_offset==second&&b.submessage_body_bytes==20&&
            std::equal(packet.begin()+source+8,packet.begin()+source+20,b.writer_guid.begin())&&std::equal(second_writer.begin(),second_writer.end(),b.writer_guid.begin()+12);
    }
    std::printf("%s DATA_per_submessage_guid_sequence_endian_INFO_SRC_%s entries=%zu unchanged=%d\n",pass?"PASS":"FAIL",little?"LE_FIRST":"BE_FIRST",seen.data.size(),packet==original);return pass;
}

std::size_t AppendFragment(Packet& packet, const Writer& writer, bool little,
        std::uint32_t sequence_hi, std::uint32_t sequence_lo, std::uint32_t start,
        std::uint16_t count, std::uint16_t fragment_size, std::uint32_t sample_size)
{
    const auto offset = packet.size();
    const auto length = static_cast<std::uint16_t>(32 + count * fragment_size);
    AppendHeader(packet, 0x16, little ? 1 : 0, length);
    const auto body = packet.size();
    packet.resize(body + length, 0x6a);
    Put(packet, body, 0, 2, little); Put(packet, body + 2, 28, 2, little);
    std::copy(writer.begin(), writer.end(), packet.begin() + body + 8);
    Put(packet, body + 12, sequence_hi, 4, little); Put(packet, body + 16, sequence_lo, 4, little);
    Put(packet, body + 20, start, 4, little); Put(packet, body + 24, count, 2, little);
    Put(packet, body + 26, fragment_size, 2, little); Put(packet, body + 28, sample_size, 4, little);
    return offset;
}

bool FragmentContracts(bool little)
{
    auto packet = Header();
    AppendData(packet); // first_writer_guid must remain this DATA's writer.
    const auto first_offset = AppendFragment(packet, second_writer, little,
        0x01020304, 0xa1b2c3d4, 0x0102, 3, 8, 1048580);
    AppendHeader(packet, 0x0c, 1, 20);
    const auto info = packet.size(); packet.resize(info + 20, 0);
    for (unsigned i = 0; i < 12; ++i) packet[info + 8 + i] = static_cast<std::uint8_t>(0x80 + i);
    AppendHeader(packet, 0x01, 1, 0);
    AppendHeader(packet, 0x09, 3, 0);
    const auto second_offset = AppendFragment(packet, first_writer, !little,
        0xffffffff, 0x10203040, 7, 2, 16, 262156);
    const auto original = packet;
const auto observed = fastdds_dsoftbus::experiment::ObserveRtps(packet.data(), packet.size());
    bool pass = observed.valid && observed.user == 3 && observed.fragments.size() == 2 && packet == original &&
        std::equal(packet.begin() + 8, packet.begin() + 20, observed.first_writer_guid.begin()) &&
        std::equal(first_writer.begin(), first_writer.end(), observed.first_writer_guid.begin() + 12);
    if (observed.fragments.size() == 2) {
        const auto& a = observed.fragments[0]; const auto& b = observed.fragments[1];
        pass = pass && a.sequence_hi == 0x01020304 && a.sequence_lo == 0xa1b2c3d4 &&
            a.fragment_start == 0x0102 && a.fragment_count == 3 && a.fragment_end == 0x0104 &&
            a.fragment_size == 8 && a.sample_size == 1048580 && a.submessage_offset == first_offset &&
            a.submessage_body_bytes == 56 &&
            std::equal(packet.begin() + 8, packet.begin() + 20, a.writer_guid.begin()) &&
            std::equal(second_writer.begin(), second_writer.end(), a.writer_guid.begin() + 12) &&
            b.sequence_hi == -1 && b.sequence_lo == 0x10203040 &&
            b.fragment_start == 7 && b.fragment_count == 2 && b.fragment_end == 8 &&
            b.fragment_size == 16 && b.sample_size == 262156 && b.submessage_offset == second_offset &&
            b.submessage_body_bytes == 64 &&
            std::equal(packet.begin() + info + 8, packet.begin() + info + 20, b.writer_guid.begin()) &&
            std::equal(first_writer.begin(), first_writer.end(), b.writer_guid.begin() + 12);
    }
    std::printf("%s DATA_FRAG_per_submessage_mixed_endian_INFO_SRC_%s count=%zu unchanged=%d\n",
        pass ? "PASS" : "FAIL", little ? "LE_FIRST" : "BE_FIRST", observed.fragments.size(), packet == original);
    return pass;
}

bool ObserverAllocationFailure()
{
    auto packet = Header();
    AppendFragment(packet, first_writer, true, 0, 1, 1, 1, 4, 4);
    const auto original = packet;
    fail_allocations = true;
const auto observed = fastdds_dsoftbus::experiment::ObserveRtps(packet.data(), packet.size());
    fail_allocations = false;
    const bool pass = !observed.valid && observed.fragments.empty() && packet == original;
    std::printf("%s DATA_FRAG_allocation_failure_returns_empty_observation_payload_unchanged\n", pass ? "PASS" : "FAIL");
    return pass;
}

bool AlignedFragments()
{
    auto packet = Header();
    AppendFragment(packet, first_writer, true, 0, 7, 1, 1, 5, 10);
    while (packet.size() % 4) packet.push_back(0); // RTPS next-header alignment.
    const auto second_offset = AppendFragment(packet, first_writer, true, 0, 7, 2, 1, 5, 10);
    const auto original = packet;
const auto seen = fastdds_dsoftbus::experiment::ObserveRtps(packet.data(), packet.size());
    const bool pass = seen.valid && seen.fragments.size() == 2 &&
        seen.fragments[1].submessage_offset == second_offset && seen.fragments[1].fragment_start == 2 && packet == original;
    std::printf("%s DATA_FRAG_odd_payload_alignment_following_fragment\n", pass ? "PASS" : "FAIL");
    return pass;
}

void AppendDestination(Packet& packet, std::uint8_t seed)
{
    AppendHeader(packet,0x0e,1,12);
    for(unsigned i=0;i<12;++i) packet.push_back(seed ? static_cast<std::uint8_t>(seed+i) : 0);
}

std::size_t AppendControl(Packet& packet, std::uint8_t kind, bool little,
        std::uint32_t bits=1, std::uint32_t first_word=0x80000000u)
{
    const auto offset=packet.size();
    const auto words=(bits+31)/32;
    const auto length=kind==0x06 ? 24+4*words : kind==0x12 ? 28+4*words : kind==0x07 ? 28 : 24;
    AppendHeader(packet,kind,static_cast<std::uint8_t>((little?1:0)|(kind==0x06?2:kind==0x07?6:0)),
        static_cast<std::uint16_t>(length));
    const auto body=packet.size(); packet.resize(body+length,0);
    const Writer reader{{0,0,9,4}};
    std::copy(reader.begin(),reader.end(),packet.begin()+body);
    std::copy(first_writer.begin(),first_writer.end(),packet.begin()+body+4);
    Put(packet,body+8,0,4,little); Put(packet,body+12,1,4,little);
    if(kind==0x06 || kind==0x12) {
        const auto bitmap_base=body+(kind==0x12?20:16);
        if(kind==0x12) Put(packet,body+16,3,4,little);
        Put(packet,bitmap_base,bits,4,little);
        for(unsigned i=0;i<words;++i) Put(packet,bitmap_base+4+4*i,i==0?first_word:0x80000000u,4,little);
        Put(packet,bitmap_base+4+4*words,7,4,little);
    } else if(kind==0x07) {
        Put(packet,body+16,0,4,little); Put(packet,body+20,3,4,little); Put(packet,body+24,7,4,little);
    } else { Put(packet,body+16,5,4,little); Put(packet,body+20,7,4,little); }
    return offset;
}

bool ReliabilityControls(bool little)
{
    auto packet=Header();
    AppendDestination(packet,0x70);
    const auto nack=AppendControl(packet,0x12,little,33,0xa0000000u);
    AppendControl(packet,0x06,!little,0);
    AppendControl(packet,0x07,little);
    AppendControl(packet,0x13,!little);
    const auto original=packet;
const auto seen=fastdds_dsoftbus::experiment::ObserveRtps(packet.data(),packet.size());
    bool pass=seen.valid && seen.controls.size()==4 && !seen.drop_shape_candidate && packet==original;
    if(seen.controls.size()==4) {
        const auto& n=seen.controls[0]; const auto& a=seen.controls[1];
        const auto& h=seen.controls[2]; const auto& f=seen.controls[3];
        pass=pass && n.kind==0x12 && n.sequence_hi==0 && n.sequence_lo==1 && n.fragment_base==3 &&
            n.num_bits==33 && n.bitmap[0]==0xa0000000u && n.bitmap[1]==0x80000000u &&
            n.count==7 && n.submessage_offset==nack && !n.final_flag &&
            n.reader_prefix_known && n.writer_prefix_known && n.destination_prefix_known &&
            std::equal(packet.begin()+8,packet.begin()+20,n.reader_guid.begin()) &&
            n.writer_guid[0]==0x70 && n.writer_guid[11]==0x7b && n.writer_guid[15]==3 &&
            a.kind==0x06 && a.sequence_lo==1 && a.num_bits==0 && a.count==7 && a.final_flag &&
            h.kind==0x07 && h.sequence_lo==1 && h.last_sequence_lo==3 && h.count==7 &&
            h.final_flag && h.liveliness_flag && h.writer_guid[0]==1 && h.reader_guid[0]==0x70 &&
            f.kind==0x13 && f.sequence_lo==1 && f.last_fragment==5 && f.count==7;
    }
    std::printf("%s reliability_controls_endian_%s_bitmap_MSB_GUID_direction_unchanged\n",pass?"PASS":"FAIL",little?"LE":"BE");
    return pass;
}

bool ControlIdentityAndBounds()
{
    auto unknown=Header(); AppendControl(unknown,0x12,true);
const auto first=fastdds_dsoftbus::experiment::ObserveRtps(unknown.data(),unknown.size());
    bool pass=first.valid && first.controls.size()==1;
    if(pass) pass=first.controls[0].reader_prefix_known && !first.controls[0].writer_prefix_known &&
        !first.controls[0].destination_prefix_known && first.controls[0].writer_guid[0]==0 &&
        first.controls[0].writer_id==first_writer && first.controls[0].writer_guid[15]==3;
    auto changed=Header(); AppendDestination(changed,0x80); AppendControl(changed,0x12,true);
    AppendHeader(changed,0x0c,1,20); const auto body=changed.size(); changed.resize(body+20,0);
    for(unsigned i=0;i<12;++i)changed[body+8+i]=static_cast<std::uint8_t>(0x40+i);
    AppendDestination(changed,0); // UNKNOWN leaves preceding destination in Fast DDS 3.6.2.
    AppendControl(changed,0x12,false);
const auto seen=fastdds_dsoftbus::experiment::ObserveRtps(changed.data(),changed.size());
    pass=pass && seen.valid && seen.controls.size()==2 && seen.controls[0].reader_guid[0]==1 &&
        seen.controls[1].reader_guid[0]==0x40 && seen.controls[1].writer_guid[0]==0x80 &&
        std::all_of(seen.first_writer_guid.begin(),seen.first_writer_guid.end(),[](auto b){return b==0;});
    for(const auto kind:{0x06,0x12}) {
        auto excessive=Header(); AppendControl(excessive,static_cast<std::uint8_t>(kind),true,257);
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(excessive.data(),excessive.size()).valid;
        auto truncated=Header(); AppendControl(truncated,static_cast<std::uint8_t>(kind),false,33); truncated.pop_back();
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(truncated.data(),truncated.size()).valid;
        auto zero=Header(); AppendControl(zero,static_cast<std::uint8_t>(kind),false,0);
const auto z=fastdds_dsoftbus::experiment::ObserveRtps(zero.data(),zero.size());
        pass=pass && z.valid && z.controls.size()==1 && z.controls[0].num_bits==0 && z.controls[0].count==7;
    }
    std::printf("%s reliability_control_unknown_peer_INFO_SRC_bounds_zero_bitmaps\n",pass?"PASS":"FAIL");
    return pass;
}

bool DropShape()
{
    auto packet=Header(); AppendHeader(packet,0x01,1,0); AppendHeader(packet,0x09,3,0);
    AppendDestination(packet,0x70); AppendFragment(packet,first_writer,true,0,1,3,1,4,20);
const auto one=fastdds_dsoftbus::experiment::ObserveRtps(packet.data(),packet.size());
    bool pass=one.valid && one.drop_shape_candidate && one.data_submessages==0 && one.fragments.size()==1;
auto mixed=packet; AppendData(mixed); auto seen=fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size());
    pass=pass && seen.valid && !seen.drop_shape_candidate && seen.data_submessages==1;
    mixed=packet; AppendFragment(mixed,first_writer,true,0,1,4,1,4,20);
seen=fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size()); pass=pass && seen.valid && !seen.drop_shape_candidate;
mixed=packet; AppendControl(mixed,0x07,true); seen=fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size());
    pass=pass && seen.valid && !seen.drop_shape_candidate;
    mixed=packet; AppendHeader(mixed,0x80,1,4); mixed.insert(mixed.end(),4,0);
seen=fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size()); pass=pass && seen.valid && !seen.drop_shape_candidate;
    mixed=packet; AppendHeader(mixed,0x0c,1,20); mixed.insert(mixed.end(),20,0);
seen=fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size()); pass=pass && seen.valid && !seen.drop_shape_candidate;
mixed=packet; mixed.pop_back(); pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(mixed.data(),mixed.size()).drop_shape_candidate;
    std::printf("%s loss_shape_single_fragment_only_whole_valid_envelope\n",pass?"PASS":"FAIL");
    return pass;
}

bool ControlBoundariesAndAllocationFailure()
{
    bool pass=true;
    for(const auto kind:{0x06,0x07,0x12,0x13}) {
        auto short_body=Header(); AppendControl(short_body,static_cast<std::uint8_t>(kind),true);
        const auto shortened=static_cast<std::uint16_t>(kind==0x06||kind==0x13?23:27);
        short_body.resize(24+shortened); Put(short_body,22,shortened,2,true);
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(short_body.data(),short_body.size()).valid;
    }
    for(const auto kind:{0x06,0x12}) {
        auto full=Header(); AppendControl(full,static_cast<std::uint8_t>(kind),false,256,1);
        const auto original=full;
auto seen=fastdds_dsoftbus::experiment::ObserveRtps(full.data(),full.size());
        pass=pass && seen.valid && seen.controls.size()==1 && seen.controls[0].num_bits==256 &&
            seen.controls[0].bitmap[0]==1 && seen.controls[0].bitmap[7]==0x80000000u &&
            seen.controls[0].count==7 && full==original;
fail_allocations=true; seen=fastdds_dsoftbus::experiment::ObserveRtps(full.data(),full.size()); fail_allocations=false;
        pass=pass && !seen.valid && seen.controls.empty() && seen.fragments.empty() && full==original;
    }
    std::printf("%s control_fixed_bounds_256bits_raw_words_allocation_failure_unchanged\n",pass?"PASS":"FAIL");
    return pass;
}

bool KnownStatisticsShape()
{
    auto packet=Header(); packet[6]=1; packet[7]=15;
    AppendDestination(packet,0x70); AppendHeader(packet,0x09,1,8); packet.insert(packet.end(),8,0);
    AppendFragment(packet,first_writer,true,0,1,3,1,8,40);
    AppendHeader(packet,0x80,1,56); packet.insert(packet.end(),56,0);
    const auto original=packet;
const auto seen=fastdds_dsoftbus::experiment::ObserveRtps(packet.data(),packet.size());
    bool pass=seen.valid && seen.drop_shape_candidate && seen.statistics_submessages==1 &&
        seen.statistics_body_bytes==56 && packet==original;
    auto unknown=packet; unknown[7]=14;
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(unknown.data(),unknown.size()).drop_shape_candidate;
    auto short_stats=packet; short_stats.resize(short_stats.size()-4); Put(short_stats,short_stats.size()-54,52,2,true);
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(short_stats.data(),short_stats.size()).drop_shape_candidate;
    auto repeated=packet; AppendHeader(repeated,0x80,1,56); repeated.insert(repeated.end(),56,0);
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(repeated.data(),repeated.size()).drop_shape_candidate;
    auto nonterminal=packet; AppendHeader(nonterminal,0x01,1,0);
pass=pass && !fastdds_dsoftbus::experiment::ObserveRtps(nonterminal.data(),nonterminal.size()).drop_shape_candidate;
    std::printf("%s known_eProsima_statistics_terminal_body56_once_whole_packet_shape\n",pass?"PASS":"FAIL");
    return pass;
}

bool FragmentPayloadBounds(bool little)
{
    auto plain=Header(); const auto offset=AppendFragment(plain,first_writer,little,0,1,3,1,8,40);
const auto a=fastdds_dsoftbus::experiment::ObserveRtps(plain.data(),plain.size());
    bool pass=a.valid && a.fragments.size()==1 && a.fragments[0].payload_bounds_known &&
        !a.fragments[0].inline_qos && !a.fragments[0].key_flag && a.fragments[0].octets_to_inline_qos==28 &&
        a.fragments[0].payload_offset==offset+36 && a.fragments[0].payload_bytes==8;
    auto qos=plain; qos[offset+1]|=2;
    const auto qos_start=offset+36; qos.insert(qos.begin()+qos_start,12,0);
    Put(qos,offset+2,52,2,little); Put(qos,qos_start,0x800f,2,little); Put(qos,qos_start+2,3,2,little);
    Put(qos,qos_start+8,1,2,little); // padded parameter body + PID_SENTINEL(1,0).
    const auto original=qos;
const auto b=fastdds_dsoftbus::experiment::ObserveRtps(qos.data(),qos.size());
    pass=pass && b.valid && b.drop_shape_candidate && b.fragments.size()==1 && b.fragments[0].inline_qos &&
        b.fragments[0].payload_bounds_known && b.fragments[0].payload_offset==qos_start+12 &&
        b.fragments[0].payload_bytes==8 && qos==original;
    auto malformed=qos; Put(malformed,qos_start+2,65535,2,little);
const auto bad=fastdds_dsoftbus::experiment::ObserveRtps(malformed.data(),malformed.size());
    pass=pass && bad.valid && bad.fragments.size()==1 && !bad.fragments[0].payload_bounds_known && !bad.drop_shape_candidate;
    auto no_sentinel=qos; Put(no_sentinel,qos_start+8,2,2,little);
const auto absent=fastdds_dsoftbus::experiment::ObserveRtps(no_sentinel.data(),no_sentinel.size());
    pass=pass && absent.valid && !absent.fragments[0].payload_bounds_known && !absent.drop_shape_candidate;
    auto jump=plain; Put(jump,offset+6,65535,2,little);
const auto invalid_jump=fastdds_dsoftbus::experiment::ObserveRtps(jump.data(),jump.size());
    pass=pass && invalid_jump.valid && !invalid_jump.fragments[0].payload_bounds_known && !invalid_jump.drop_shape_candidate;
    std::printf("%s DATA_FRAG_inline_QoS_bounded_payload_%s_unchanged\n",pass?"PASS":"FAIL",little?"LE":"BE");
    return pass;
}
}

int main()
{
    int failures = 0;
    // Fast DDS MessageReceiver::readSubmessageHeader treats zero lengths on
    // PAD/INFO_TS as an empty body, unlike the rest-of-message rule for DATA.
    for (const bool little : {false, true}) {
        auto pad = Header();
        AppendHeader(pad, 0x01, little ? 1 : 0, 0);
        AppendData(pad, first_writer, little);
        failures += !Observes(little ? "PAD(0)+DATA_LE" : "PAD(0)+DATA_BE", pad, 1);

        auto timestamp = Header();
        AppendHeader(timestamp, 0x09, little ? 3 : 2, 0); // Invalidate flag, no timestamp body.
        AppendData(timestamp, first_writer, little);
        failures += !Observes(little ? "INFO_TS(invalidate,0)+DATA_LE" : "INFO_TS(invalidate,0)+DATA_BE", timestamp, 1);
    }

    auto repeated = Header();
    AppendHeader(repeated, 0x01, 1, 0);
    AppendHeader(repeated, 0x09, 3, 0);
    AppendData(repeated);
    AppendHeader(repeated, 0x01, 1, 0);
    AppendData(repeated, second_writer);
    failures += !Observes("PAD_INFO_TS_DATA_PAD_DATA_first_writer_only", repeated, 2);

    auto terminal = Header();
    AppendData(terminal, first_writer, true, true);
    failures += !Observes("DATA(0)_still_consumes_remaining_body", terminal, 1);

    auto nonzero = Header();
    AppendHeader(nonzero, 0x01, 1, 4);
    nonzero.insert(nonzero.end(), 4, 0xff);
    AppendData(nonzero);
    failures += !Observes("PAD(4)+DATA", nonzero, 1);

    auto truncated = Header();
    AppendHeader(truncated, 0x01, 1, 0);
    truncated.insert(truncated.end(), {0x15, 1, 20});
const bool bounded = !fastdds_dsoftbus::experiment::ObserveRtps(truncated.data(), truncated.size()).valid;
    std::printf("%s truncated_header_after_empty_PAD\n", bounded ? "PASS" : "FAIL");
    failures += !bounded;
    failures += !FragmentContracts(false);
    failures += !FragmentContracts(true);
    failures += !DataContracts(false);
    failures += !DataContracts(true);
    failures += !ObserverAllocationFailure();
    failures += !AlignedFragments();
    failures += !ReliabilityControls(false);
    failures += !ReliabilityControls(true);
    failures += !ControlIdentityAndBounds();
    failures += !DropShape();
    failures += !ControlBoundariesAndAllocationFailure();
    failures += !KnownStatisticsShape();
    failures += !FragmentPayloadBounds(false);
    failures += !FragmentPayloadBounds(true);
    return failures ? 1 : 0;
}
