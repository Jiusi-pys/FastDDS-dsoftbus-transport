#include <fastdds/dds/core/policy/QosPolicies.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include "BoundedRecordPubSubTypes.h"
#include <fastdds/rtps/common/SerializedPayload.h>
#else
#include "BoundedRecordPubSubTypes.hpp"
#include <fastdds/rtps/common/SerializedPayload.hpp>
#endif
#include <iostream>

int main()
{
    Standards::BoundedRecord input;
    input.id(17);
    input.sequence_number(42);
    input.label("bounded IDL fixture");
    input.payload({0, 1, 127, 128, 255});
    Standards::BoundedRecordPubSubType support;
    for (auto representation : {eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION,
                               eprosima::fastdds::dds::XCDR2_DATA_REPRESENTATION}) {
        Standards::BoundedRecord output;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        eprosima::fastrtps::rtps::SerializedPayload_t payload(1024);
        if (!support.serialize(&input, &payload, representation) || !support.deserialize(&payload, &output) ||
#else
        eprosima::fastdds::rtps::SerializedPayload_t payload(1024);
        if (!support.serialize(&input, payload, representation) || !support.deserialize(payload, &output) ||
#endif
                input.id() != output.id() || input.sequence_number() != output.sequence_number() ||
                input.label() != output.label() || input.payload() != output.payload()) {
            std::cerr << "generated type roundtrip failed for representation " << representation << std::endl;
            return 1;
        }
    }
    std::cout << "generated C++ keyed bounded type: XCDR1 and XCDR2 PASS" << std::endl;
    return 0;
}
