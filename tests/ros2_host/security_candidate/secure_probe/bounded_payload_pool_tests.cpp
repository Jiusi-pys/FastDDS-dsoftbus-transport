// Bounded payload-pool observation for the RTPS reader admission path.
//
// Task: CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492.
//
// This test answers two questions that the network-level matrix cannot answer:
//
//   1. Where is the allocation decision made on the receive path? It is made by
//      RTPSReader::reserve_cache, which derives the reservation size from the
//      incoming sample size when the reader history is not fixed-size, and then
//      asks an IPayloadPool for that many bytes.
//   2. Does a large representable sampleSize actually reach an allocation
//      request for a reader that is admitted? Under
//      PREALLOCATED_WITH_REALLOC_MEMORY_MODE it does; a fixed PREALLOCATED
//      history with a smaller payload maximum refuses before the pool is asked.
//
// The pool used here is a test-owned IPayloadPool. It records every requested
// size and refuses anything above a small ceiling, so nothing large is ever
// really allocated on the development machine. That ceiling is a test safety
// control and an observation device. It is NOT a product limit and it is NOT
// evidence that the library refuses the input: the reader still made the
// request, and this test reports that fact.
//
// This is deliberately a direct call into a public library method. It shows
// local behaviour only and does not show that a real protected receive entry
// would have stopped the input first; the security matrix carries that
// evidence separately.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fastdds/dds/log/Log.hpp>
#include <fastdds/rtps/RTPSDomain.h>
#include <fastdds/rtps/attributes/RTPSParticipantAttributes.h>
#include <fastdds/rtps/common/CacheChange.h>
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastdds/rtps/history/IPayloadPool.h>
#include <fastdds/rtps/attributes/HistoryAttributes.h>
#include <fastdds/rtps/history/ReaderHistory.h>
#include <fastdds/rtps/participant/RTPSParticipant.h>
#include <fastdds/rtps/attributes/ReaderAttributes.h>
#include <fastdds/rtps/reader/RTPSReader.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/utils/IPLocator.h>

namespace {

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

constexpr uint32_t kPoolCeiling = 64u * 1024u * 1024u;

// The reported unauthenticated sampleSize from the advisory.
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

// Test-owned payload pool. Records and bounds every request.
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

struct CaseResult
{
    std::string name;
    std::string expectation;
    std::string observed;
    bool pass = false;
};

std::vector<CaseResult> g_results;

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

RTPSReader* make_reader(
        RTPSParticipant* participant,
        MemoryManagementPolicy_t policy,
        uint32_t payload_max_size,
        const std::shared_ptr<IPayloadPool>& pool,
        ReaderHistory*& history_out)
{
    HistoryAttributes history_attributes;
    history_attributes.memoryPolicy = policy;
    history_attributes.payloadMaxSize = payload_max_size;
    history_attributes.initialReservedCaches = 4;
    history_attributes.maximumReservedCaches = 16;
    history_out = new ReaderHistory(history_attributes);

    ReaderAttributes reader_attributes;
    reader_attributes.endpoint.topicKind = NO_KEY;
    reader_attributes.endpoint.reliabilityKind = RELIABLE;
    return RTPSDomain::createRTPSReader(participant, reader_attributes, pool, history_out);
}

// expected_pool_request == 0 means "the pool must not be asked at all".
void run_case(
        const std::string& name,
        const std::string& expectation,
        RTPSReader* reader,
        PoolObservation& observation,
        uint32_t sample_size,
        bool expect_granted,
        uint32_t expected_pool_request)
{
    CaseResult result;
    result.name = name;
    result.expectation = expectation;

    observation.requests.clear();
    observation.granted = 0;
    observation.denied = 0;

    CacheChange_t* change = nullptr;
    const bool reserved = reader->reserveCache(&change, sample_size);
    if (reserved && change != nullptr)
    {
        reader->releaseCache(change);
    }

    const bool request_matches = observation.requests.size() == 1
            && observation.requests.front() == expected_pool_request;
    const bool request_absent = observation.requests.empty() && expected_pool_request == 0;
    result.observed = "reserveCache=" + std::string(reserved ? "true" : "false")
            + ", sampleSize=" + std::to_string(sample_size)
            + ", pool requests=[" + observation.request_list() + "]"
            + ", granted=" + std::to_string(observation.granted)
            + ", denied=" + std::to_string(observation.denied);

    result.pass = (reserved == expect_granted) && (request_matches || request_absent);
    g_results.push_back(result);
    std::cout << (result.pass ? "[PASS] " : "[FAIL] ") << name
              << " -> " << result.observed << std::endl;
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

    PoolObservation observation;
    auto pool = std::make_shared<SpyPayloadPool>(observation);

    RTPSParticipant* participant = make_participant(0, 18700);
    if (participant == nullptr)
    {
        std::cout << "could not create participant" << std::endl;
        return 2;
    }

    // --- Resizable history: the reader derives the reservation from the wire
    //     sample size, so a large representable value reaches the pool. ------
    ReaderHistory* resizable_history = nullptr;
    RTPSReader* resizable_reader = make_reader(
        participant, PREALLOCATED_WITH_REALLOC_MEMORY_MODE, 4096, pool, resizable_history);
    if (resizable_reader == nullptr)
    {
        std::cout << "could not create resizable reader" << std::endl;
        return 2;
    }

    run_case("pool_01_ordinary_sample_size",
            "a 4096-byte reservation is granted and the pool sees exactly 4096",
            resizable_reader, observation, 4096, true, 4096);

    run_case("pool_02_advisory_uint32_max_sample_size",
            "the reader asks the pool for the full 0x7FFFFFFF bytes; only the test "
            "ceiling refuses it, so the request itself is reachable",
            resizable_reader, observation, kAdvisorySampleSize, false, kAdvisorySampleSize);

    // --- Fixed history: the reader has a configured payload maximum and must
    //     refuse before the pool is asked. ------------------------------------
    ReaderHistory* fixed_history = nullptr;
    RTPSReader* fixed_reader = make_reader(
        participant, PREALLOCATED_MEMORY_MODE, 4096, pool, fixed_history);
    if (fixed_reader == nullptr)
    {
        std::cout << "could not create fixed reader" << std::endl;
        return 2;
    }

    run_case("pool_03_fixed_history_ordinary_sample_size",
            "a 4096-byte reservation succeeds against the fixed payload maximum",
            fixed_reader, observation, 4096, true, 4096);

    run_case("pool_04_fixed_history_above_payload_maximum",
            "the reservation is clamped to the configured payload maximum of 4096, "
            "so the pool is never asked for the wire sample size",
            fixed_reader, observation, kAdvisorySampleSize, true, 4096);

    // --- Boundary immediately above the configured payload maximum. ---------
    run_case("pool_05_fixed_history_one_byte_above_maximum",
            "the reservation is still clamped to 4096 one byte above the maximum",
            fixed_reader, observation, 4097, true, 4096);

    // --- Boundary immediately at the configured payload maximum. -----------
    run_case("pool_06_fixed_history_exactly_at_maximum",
            "granted with a pool request of exactly the configured maximum",
            fixed_reader, observation, 4096, true, 4096);

    size_t passed = 0;
    for (const CaseResult& result : g_results)
    {
        if (result.pass)
        {
            ++passed;
        }
    }

    std::string report = "{\n  \"cases\": [\n";
    for (size_t i = 0; i < g_results.size(); ++i)
    {
        const CaseResult& result = g_results[i];
        report += "    {\"name\": \"" + result.name + "\", "
                  "\"expectation\": \"" + result.expectation + "\", "
                  "\"observed\": \"" + result.observed + "\", "
                  "\"pass\": " + (result.pass ? "true" : "false") + "}";
        report += (i + 1 == g_results.size()) ? "\n" : ",\n";
    }
    report += "  ],\n  \"cases_total\": " + std::to_string(g_results.size())
            + ",\n  \"cases_passed\": " + std::to_string(passed)
            + ",\n  \"test_pool_ceiling_bytes\": " + std::to_string(kPoolCeiling)
            + ",\n  \"advisory_sample_size\": " + std::to_string(kAdvisorySampleSize)
            + ",\n  \"note\": \"The pool ceiling is a test safety control and an observation "
              "device. A request reaching the pool is a residual risk statement about "
              "admitted readers, not a product limit, and the ceiling refusing it is not "
              "evidence that the library refuses the input.\"\n}\n";

    if (!output_path.empty())
    {
        std::FILE* file = std::fopen(output_path.c_str(), "wb");
        if (file != nullptr)
        {
            std::fwrite(report.data(), 1, report.size(), file);
            std::fclose(file);
        }
    }
    std::cout << report << std::endl;

    RTPSDomain::removeRTPSReader(resizable_reader);
    delete resizable_history;
    RTPSDomain::removeRTPSReader(fixed_reader);
    delete fixed_history;
    RTPSDomain::removeRTPSParticipant(participant);
    eprosima::fastdds::dds::Log::Reset();
    return passed == g_results.size() ? 0 : 1;
}
