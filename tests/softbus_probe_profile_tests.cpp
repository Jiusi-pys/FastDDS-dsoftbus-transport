#include "softbus_probe_profile.hpp"
#include "softbus_record_contract.hpp"
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace fastdds_dsoftbus_probe;
namespace {
void Require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
}

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"PROFILE-01 exact names and unchanged failure output", [] {
            ProbeProfile profile = ProbeProfile::Bytes;
            Require(ParseProbeProfile("bytes", profile) && profile == ProbeProfile::Bytes, "bytes accepted");
            Require(ParseProbeProfile("raw", profile) && profile == ProbeProfile::Raw, "raw accepted");
            Require(ParseProbeProfile("raw-encrypted", profile) && profile == ProbeProfile::RawEncrypted,
                    "encrypted raw accepted");
            for (const auto* invalid : {"", "RAW", "stream", "raw_encrypted", "bytes ", " raw"})
            {
                Require(!ParseProbeProfile(invalid, profile), "unknown profile rejected");
                Require(profile == ProbeProfile::RawEncrypted, "failed parse does not select bytes");
            }
        }},
        {"PROFILE-02 native type and send dispatch", [] {
            const auto* bytes = DescribeProbeProfile(ProbeProfile::Bytes);
            const auto* raw = DescribeProbeProfile(ProbeProfile::Raw);
            const auto* encrypted = DescribeProbeProfile(ProbeProfile::RawEncrypted);
            Require(bytes && raw && encrypted, "all supported descriptions present");
            Require(bytes->socket_data_type == 2 && bytes->peer_data_type == 2 &&
                    bytes->send_method == ProbeSendMethod::Bytes, "Bytes API dispatch");
            Require(raw->socket_data_type == 4 && raw->peer_data_type == 4 &&
                    raw->send_method == ProbeSendMethod::Stream, "raw stream API dispatch");
            Require(encrypted->socket_data_type == 8 && encrypted->peer_data_type == 4 &&
                    encrypted->send_method == ProbeSendMethod::Stream, "encrypted request normalizes to stream peer type");
            Require(std::string(bytes->name) == "bytes" && std::string(raw->name) == "raw" &&
                    std::string(encrypted->name) == "raw-encrypted", "canonical diagnostic names");
            Require(DescribeProbeProfile(static_cast<ProbeProfile>(255)) == nullptr, "unknown enum cannot silently fall back");
        }},
        {"PROFILE-03 signed stream length and pointer bounds", [] {
            char sentinel = 'x';
            Require(ValidateProbeStreamFrame(true, &sentinel, 1, 1), "one byte boundary");
            Require(ValidateProbeStreamFrame(true, &sentinel, 60000, 60000), "maximum boundary");
            Require(!ValidateProbeStreamFrame(false, &sentinel, 1, 60000), "null StreamData rejected");
            Require(!ValidateProbeStreamFrame(true, nullptr, 1, 60000), "null payload rejected");
            Require(!ValidateProbeStreamFrame(true, &sentinel, 0, 60000), "zero rejected");
            Require(!ValidateProbeStreamFrame(true, &sentinel, -1, 60000), "negative length rejected before conversion");
            Require(!ValidateProbeStreamFrame(true, &sentinel, std::numeric_limits<int>::min(), 60000), "minimum int rejected");
            Require(!ValidateProbeStreamFrame(true, &sentinel, 60001, 60000), "over limit rejected");
            Require(!ValidateProbeStreamFrame(true, &sentinel, std::numeric_limits<int>::max(), 60000), "huge length rejected");
            Require(!ValidateProbeStreamFrame(true, &sentinel, 1, 0), "zero capacity rejected");
        }},
        {"PROFILE-04 stream data is owned before callback returns", [] {
            const std::string network(64, 'a');
            for (auto profile : {ProbeProfile::Raw, ProbeProfile::RawEncrypted})
            {
                const auto* description = DescribeProbeProfile(profile);
                SoftbusRecordContract contract(true, "peer", network, description->peer_data_type);
                Require(contract.RegisterCreated(1) && contract.BeginBind(2), "capture accepted ownership");
                contract.CompleteBind(2, CopyRecordPeer("peer", network.c_str(), "diagnostic", 4));
                Require(contract.Snapshot().ready, "normalized peer type accepted");
                RecordEvent event;
                Require(contract.WaitPop(event, SoftbusRecordContract::Clock::now()) &&
                        event.kind == RecordEventKind::Bound, "consume bind event");
                auto data = MakeProbeRecord(0);
                Require(ValidateProbeStreamFrame(true, data.data(), static_cast<int>(data.size()), record_queue_bytes),
                        "valid stream accepted by view validation");
                contract.OnBytes(2, data.data(), data.size());
                for (auto& byte : data) byte = 0;
                Require(contract.WaitPop(event, SoftbusRecordContract::Clock::now()) &&
                        ValidateProbeRecord(0, event.bytes), "record queue owns stream data");
            }
        }},
        {"IDENTITY-01 success requires nonempty bounded C string", [] {
            const std::string identity(64, 'a');
            const auto good = ObserveProbeIdentity(0, identity.c_str(), identity.size() + 1);
            Require(good.valid && good.nul_terminated && good.length == 64, "successful complete UDID observed");
            const auto short_id = ObserveProbeIdentity(0, "test-id", 8);
            Require(short_id.valid && short_id.nul_terminated && short_id.length == 7,
                    "bounded observation does not invent a hex or exact-length requirement");
            const auto single = ObserveProbeIdentity(0, "a", 2);
            Require(single.valid && single.length == 1, "terminator at final byte accepted");
        }},
        {"IDENTITY-02 empty zero and unterminated buffers rejected", [] {
            std::array<char, 65> zero{};
            const auto empty = ObserveProbeIdentity(0, zero.data(), zero.size());
            Require(!empty.valid && empty.nul_terminated && empty.length == 0, "all binary zero is no identity");
            const std::string zeros(64, '0');
            const auto placeholder = ObserveProbeIdentity(0, zeros.c_str(), 65);
            Require(!placeholder.valid && placeholder.nul_terminated && placeholder.length == 64,
                    "all ASCII zeros is not stable identity evidence");
            std::array<char, 65> unended;
            unended.fill('a');
            const auto missing_nul = ObserveProbeIdentity(0, unended.data(), unended.size());
            Require(!missing_nul.valid && !missing_nul.nul_terminated && missing_nul.length == 0,
                    "unterminated buffer has no proven identity length");
            Require(!ObserveProbeIdentity(0, nullptr, 65).valid, "null buffer rejected");
            Require(!ObserveProbeIdentity(0, "a", 0).valid, "zero capacity rejected");
            Require(!ObserveProbeIdentity(0, "a", 1).valid, "terminator outside capacity rejected");
        }},
        {"IDENTITY-EXPORT dispatch is local only; availability keeps peers", [] {
            std::vector<std::string> calls;
            const auto query=[&](const std::string& id,const char* role,std::size_t index) {calls.emplace_back(std::string(role)+":"+id+":"+std::to_string(index));return true;};
            const std::vector<std::string> peers{"unit-peer-0","unit-peer-1"};
            const auto local=CheckProbeIdentitySet("unit-local",peers,true,query);
            Require(local.complete&&local.remote_expected==0&&calls==std::vector<std::string>{"local:unit-local:0"}, "actual export query dispatch contains local only");
            calls.clear();const auto legacy=CheckProbeIdentitySet("unit-local",peers,false,query);
            Require(legacy.complete&&legacy.remote_valid==2&&calls==std::vector<std::string>{"local:unit-local:0","remote:unit-peer-0:0","remote:unit-peer-1:1"}, "legacy actual targets/roles/indices preserved");
            Require(!CheckProbeIdentitySet("unit-local",{},true,query).complete, "no online route cannot satisfy dual-board input");
        }},
        {"IDENTITY-EXPORT exact private path and bytes", [] {
            Require(ValidIdentityExportPath("/data/local/tmp/mdds_dds_1234567890abcdef_A/local-udid.bin"), "exact A output");
            Require(ValidIdentityExportPath("/data/local/tmp/mdds_dds_1234567890abcdef_B/local-udid.bin"), "exact B output");
            for(const char* bad : {"/tmp/local-udid.bin", "/data/local/tmp/mdds_dds_1234567890abcdef_C/local-udid.bin", "/data/local/tmp/mdds_dds_1234567890abcdef_A/../local-udid.bin", "/data/local/tmp/mdds_dds_1234567890ABCDEF_A/local-udid.bin"})
                Require(!ValidIdentityExportPath(bad), "foreign/escape/identity rejected");
            Require(ValidIdentityExportBytes("unit-udid",9), "synthetic exact bytes accepted");
            Require(!ValidIdentityExportBytes(nullptr,9)&&!ValidIdentityExportBytes("",0), "missing rejected");
            Require(!ValidIdentityExportBytes("a\nb",3)&&!ValidIdentityExportBytes("a\rb",3)&&!ValidIdentityExportBytes("a\0b",3), "configuration line ambiguity rejected");
            const std::string large(65,'a');Require(!ValidIdentityExportBytes(large.data(),large.size()), "V1 length bound");
        }},
        {"IDENTITY-03 failed query never validates stale buffer", [] {
            const auto failed = ObserveProbeIdentity(-42, "stale-but-complete", 19);
            Require(!failed.valid && !failed.nul_terminated && failed.length == 0, "failed API provides no string evidence");
            Require(!ObserveProbeIdentity(1, "a", 2).valid, "every nonzero native result rejected");
            Require(!ObserveProbeIdentity(-1, nullptr, 65).valid, "failed null buffer safe");
        }}
    };
    unsigned failures = 0;
    for (const auto& test : tests)
    {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failures; std::cout << "FAIL " << test.first << ": " << e.what() << '\n'; }
        catch (...) { ++failures; std::cout << "FAIL " << test.first << ": unknown exception\n"; }
    }
    std::cout << "cases=" << tests.size() << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
