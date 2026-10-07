// Fragment-geometry evidence for the receive path.
//
// Task: SECURITY_ON_ROS_CLOSURE_FROM_A885A37.
//
// The previous round observed the reader reservation with fragment_size = 0,
// because the public RTPSReader::reserveCache(change, size) overload passes zero
// and CacheChange_t::calculate_required_fragmented_payload_size short-circuits on
// that. This file exercises the geometry with real, non-zero fragment sizes,
// including the values the wire can actually carry that must be refused.
//
// Two things are measured separately and are not interchangeable:
//   * the geometry helper's own decision, called directly, and
//   * what the reader then asks its payload pool for.
//
// The pool is test-owned, records every requested size, and refuses anything
// above a small ceiling. That ceiling is a safety control and an observation
// device, never a product limit, and it is also not evidence that the library
// refuses the input: the request still happened and is reported.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <fastdds/dds/log/Log.hpp>
#include <fastdds/rtps/RTPSDomain.h>
#include <fastdds/rtps/attributes/HistoryAttributes.h>
#include <fastdds/rtps/attributes/RTPSParticipantAttributes.h>
#include <fastdds/rtps/attributes/ReaderAttributes.h>
#include <fastdds/rtps/common/CacheChange.h>
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastdds/rtps/history/IPayloadPool.h>
#include <fastdds/rtps/history/ReaderHistory.h>
#include <fastdds/rtps/participant/RTPSParticipant.h>
#include <fastdds/rtps/reader/RTPSReader.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/utils/IPLocator.h>

namespace {

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

constexpr uint32_t kPoolCeiling = 64u * 1024u * 1024u;
constexpr uint32_t kAdvisorySampleSize = 0x7FFFFFFFu;

struct PoolObservation
{
    std::vector<uint32_t> requests;
    uint32_t granted = 0;
    uint32_t denied = 0;

    std::string request_list() const
    {
        std::string text;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            text += (i ? "," : "");
            text += std::to_string(requests[i]);
        }
        return text.empty() ? std::string("none") : text;
    }
};

class SpyPayloadPool : public IPayloadPool
{
public:

    explicit SpyPayloadPool(
            PoolObservation& observation)
        : observation_(observation)
    {
    }

    bool get_payload(
            uint32_t size,
            CacheChange_t& cache_change) override
    {
        observation_.requests.push_back(size);
        if (size > kPoolCeiling)
        {
            ++observation_.denied;
            return false;
        }
        void* memory = std::malloc(size ? size : 1);
        if (memory == nullptr)
        {
            ++observation_.denied;
            return false;
        }
        cache_change.serializedPayload.data = static_cast<octet*>(memory);
        cache_change.serializedPayload.max_size = size;
        cache_change.serializedPayload.length = 0;
        cache_change.payload_owner(this);
        ++observation_.granted;
        return true;
    }

    bool get_payload(
            SerializedPayload_t& data,
            IPayloadPool*& data_owner,
            CacheChange_t& cache_change) override
    {
        observation_.requests.push_back(data.length);
        if (data.length > kPoolCeiling)
        {
            ++observation_.denied;
            return false;
        }
        void* memory = std::malloc(data.length ? data.length : 1);
        if (memory == nullptr)
        {
            ++observation_.denied;
            return false;
        }
        std::memcpy(memory, data.data, data.length);
        cache_change.serializedPayload.data = static_cast<octet*>(memory);
        cache_change.serializedPayload.max_size = data.length;
        cache_change.serializedPayload.length = data.length;
        cache_change.payload_owner(this);
        if (data_owner == nullptr)
        {
            data_owner = this;
        }
        ++observation_.granted;
        return true;
    }

    bool release_payload(
            CacheChange_t& cache_change) override
    {
        std::free(cache_change.serializedPayload.data);
        cache_change.serializedPayload.data = nullptr;
        cache_change.serializedPayload.max_size = 0;
        cache_change.serializedPayload.length = 0;
        cache_change.payload_owner(nullptr);
        return true;
    }

private:

    PoolObservation& observation_;
};

std::vector<std::string> g_failures;
std::vector<std::string> g_lines;

void record(
        const std::string& name,
        const std::string& expectation,
        const std::string& observed,
        bool pass)
{
    g_lines.push_back("{\"name\": \"" + name + "\", \"expectation\": \"" + expectation +
                      "\", \"observed\": \"" + observed + "\", \"pass\": " +
                      (pass ? "true" : "false") + "}");
    if (!pass)
    {
        g_failures.push_back(name);
    }
    std::printf("[%s] %s -> %s\n", pass ? "PASS" : "FAIL", name.c_str(), observed.c_str());
}

// Direct observation of the fragment geometry decision.
void geometry_case(
        const std::string& name,
        uint32_t payload_size,
        uint16_t fragment_size,
        bool expect_ok,
        uint32_t expect_min_required)
{
    uint32_t minimum = 0;
    const bool ok = CacheChange_t::calculate_required_fragmented_payload_size(
        payload_size, fragment_size, minimum);
    std::string observed = std::string("ok=") + (ok ? "true" : "false");
    if (ok)
    {
        observed += ", min_required=" + std::to_string(minimum);
    }
    const bool pass = (ok == expect_ok) && (!ok || minimum == expect_min_required);
    record(name,
           std::string("ok=") + (expect_ok ? "true" : "false") +
           (expect_ok ? ", min_required=" + std::to_string(expect_min_required) : ""),
           observed, pass);
}

RTPSParticipant* make_participant(
        uint32_t domain,
        uint16_t metatraffic_port)
{
    RTPSParticipantAttributes attributes;
    attributes.useBuiltinTransports = false;
    auto udp = std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
    udp->interfaceWhiteList.push_back("127.0.0.1");
    attributes.userTransports.push_back(udp);
    attributes.builtin.metatrafficMulticastLocatorList.clear();
    Locator_t unicast;
    IPLocator::createLocator(LOCATOR_KIND_UDPv4, "127.0.0.1", metatraffic_port, unicast);
    attributes.builtin.metatrafficUnicastLocatorList.push_back(unicast);
    return RTPSDomain::createParticipant(domain, attributes);
}

} // namespace

int main(
        int argc,
        char** argv)
{
    std::string output_path;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--result" && i + 1 < argc)
        {
            output_path = argv[++i];
        }
    }
    eprosima::fastdds::dds::Log::SetVerbosity(eprosima::fastdds::dds::Log::Warning);

    // --- Geometry helper, non-zero fragment sizes -------------------------
    // fragment_size == 0 is the short-circuit the previous round hit.
    geometry_case("frag_01_zero_fragment_size_short_circuit", 65536, 0, true, 65536);
    // Legal geometry: the minimum never exceeds the payload, so it is clamped.
    geometry_case("frag_02_legal_1024_fragments", 65536, 1024, true, 65536);
    geometry_case("frag_03_payload_at_fragment_size", 1024, 1024, true, 1024);
    geometry_case("frag_04_payload_below_fragment_size", 512, 1024, true, 512);
    // Exactly the documented minimum fragment size that can hold an index.
    geometry_case("frag_05_fragment_size_at_minimum", 65536, 4, true, 65536);
    // Below the minimum: the wire can carry this, and it must be refused.
    geometry_case("frag_06_fragment_size_one_refused", 65536, 1, false, 0);
    geometry_case("frag_07_fragment_size_two_refused", 65536, 2, false, 0);
    geometry_case("frag_08_fragment_size_three_refused", 65536, 3, false, 0);
    // Largest fragment size the wire field can carry.
    geometry_case("frag_09_fragment_size_uint16_max", 1000000, 65535, true, 1000000);
    // Overflow guard boundary.
    geometry_case("frag_10_payload_at_overflow_boundary_ok",
                  std::numeric_limits<uint32_t>::max() - 7u, 1024, true,
                  std::numeric_limits<uint32_t>::max() - 7u);
    geometry_case("frag_11_payload_above_overflow_guard_refused",
                  std::numeric_limits<uint32_t>::max() - 6u, 1024, false, 0);
    geometry_case("frag_12_advisory_payload_with_real_fragments",
                  kAdvisorySampleSize, 65500, true, kAdvisorySampleSize);

    // --- What the reader then asks its pool for ---------------------------
    PoolObservation observation;
    auto pool = std::make_shared<SpyPayloadPool>(observation);
    RTPSParticipant* participant = make_participant(0, 18850);
    if (participant == nullptr)
    {
        std::printf("could not create participant\n");
        return 2;
    }
    HistoryAttributes history_attributes;
    history_attributes.memoryPolicy = PREALLOCATED_WITH_REALLOC_MEMORY_MODE;
    history_attributes.payloadMaxSize = 4096;
    history_attributes.initialReservedCaches = 4;
    history_attributes.maximumReservedCaches = 16;
    ReaderHistory* history = new ReaderHistory(history_attributes);
    ReaderAttributes reader_attributes;
    reader_attributes.endpoint.topicKind = NO_KEY;
    reader_attributes.endpoint.reliabilityKind = RELIABLE;
    RTPSReader* reader = RTPSDomain::createRTPSReader(participant, reader_attributes, pool, history);
    if (reader == nullptr)
    {
        std::printf("could not create reader\n");
        return 2;
    }

    {
        observation.requests.clear();
        observation.granted = 0;
        observation.denied = 0;
        CacheChange_t* change = nullptr;
        const bool reserved = reader->reserveCache(&change, 65536);
        if (reserved && change != nullptr)
        {
            reader->releaseCache(change);
        }
        record("frag_13_reader_request_for_fragmented_shaped_sample",
               "the reader asks the pool for the payload size it was given",
               "reserveCache=" + std::string(reserved ? "true" : "false") +
               ", pool requests=[" + observation.request_list() + "]",
               !observation.requests.empty() && observation.requests.front() == 65536);
    }
    {
        observation.requests.clear();
        observation.granted = 0;
        observation.denied = 0;
        CacheChange_t* change = nullptr;
        const bool reserved = reader->reserveCache(&change, kAdvisorySampleSize);
        if (reserved && change != nullptr)
        {
            reader->releaseCache(change);
        }
        record("frag_14_reader_request_for_advisory_sample_size",
               "the advisory sample size still reaches the pool request",
               "reserveCache=" + std::string(reserved ? "true" : "false") +
               ", pool requests=[" + observation.request_list() + "], denied=" +
               std::to_string(observation.denied),
               !observation.requests.empty() && observation.requests.front() == kAdvisorySampleSize);
    }

    RTPSDomain::removeRTPSReader(reader);
    delete history;
    RTPSDomain::removeRTPSParticipant(participant);
    eprosima::fastdds::dds::Log::Reset();

    const size_t total = g_lines.size();
    const size_t passed = total - g_failures.size();
    std::string report = "{\n  \"cases\": [\n";
    for (size_t i = 0; i < g_lines.size(); ++i)
    {
        report += "    " + g_lines[i];
        report += (i + 1 == g_lines.size()) ? "\n" : ",\n";
    }
    report += "  ],\n  \"cases_total\": " + std::to_string(total) +
              ",\n  \"cases_passed\": " + std::to_string(passed) +
              ",\n  \"test_pool_ceiling_bytes\": " + std::to_string(kPoolCeiling) +
              ",\n  \"note\": \"fragment_size = 0 short-circuits the geometry helper, which is why "
              "the earlier reservation observation used the raw sample size. The pool ceiling is a "
              "test safety control; a request reaching the pool is a statement about admitted "
              "readers, not a product limit.\"\n}\n";
    if (!output_path.empty())
    {
        std::FILE* file = std::fopen(output_path.c_str(), "wb");
        if (file != nullptr)
        {
            std::fwrite(report.data(), 1, report.size(), file);
            std::fclose(file);
        }
    }
    std::printf("%s\n", report.c_str());
    return passed == total ? 0 : 1;
}
