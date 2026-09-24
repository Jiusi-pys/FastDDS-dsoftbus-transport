#include "PortableFixture.hpp"
#include "ExperimentLargeSamplePubSubTypes.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 3) return 64;
    char* end = nullptr;
    const auto port = std::strtoul(argv[2], &end, 10);
    if (!end || *end || port < 1024 || port > 65000) return 64;
    // Each contract uses real Fast DDS Participants and public DDS APIs.
    // A malformed length must traverse DDS serialization/take before rejection.
    bool pass=false;
    if(std::string(argv[1])=="serialization") {
        using namespace mdds::experiment::perf;
        ExperimentLargeSamplePubSubType type;
        pass=true;
        for(auto id:{CaseId::RTT_1K,CaseId::RTT_4K,CaseId::RTT_256K}) {
            Identity expected{0xc001c001c001c001ULL,id,Phase::Formal,7},decoded;
            ExperimentLargeSample source,recovered; source.index(expected.sequence);
            pass=pass&&FillPayload(expected,source.data());
            const auto size=type.calculate_serialized_size(&source,eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION);
            eprosima::fastdds::rtps::SerializedPayload_t payload(size);
            pass=pass&&size==GetCase(id).serialized_bytes&&size==source.data().size()+12&&
                type.serialize(&source,payload,eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION)&&payload.length==size&&
                type.deserialize(payload,&recovered)&&recovered==source&&ValidatePayload(recovered.index(),recovered.data(),decoded)&&decoded==expected;
        }
    } else pass = mdds::experiment::perf::PerformanceFixture::ContractTest(argv[1], static_cast<std::uint16_t>(port));
    std::printf("PERF_FIXTURE_CONTRACT test=%s result=%s native_proof=NONE\n", argv[1], pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
