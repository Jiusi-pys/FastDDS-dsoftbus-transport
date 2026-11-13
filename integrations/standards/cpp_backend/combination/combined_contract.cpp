#include B3_FIRST
#include B3_SECOND
#include B3_COMBINED
#include <iostream>
#include <stdexcept>

int main() {
    try {
        Combined::Envelope input;
        input.first().id(42); input.first().label("shared include");
        input.second().id(17); input.second().label("second model");
        Combined::EnvelopePubSubType support;
#if !defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        Shadow::CarrierPubSubType nested;
        Shadow_CarrierPubSubType flattened;
        nested.register_type_object_representation();
        flattened.register_type_object_representation();
#endif
        for (auto rep : {eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION,
                         eprosima::fastdds::dds::XCDR2_DATA_REPRESENTATION}) {
            Combined::Envelope output;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
            eprosima::fastrtps::rtps::SerializedPayload_t payload(65536);
            bool ok = support.serialize(&input, &payload, rep) && support.deserialize(&payload, &output);
#else
            eprosima::fastdds::rtps::SerializedPayload_t payload(65536);
            bool ok = support.serialize(&input, payload, rep) && support.deserialize(payload, &output);
            support.register_type_object_representation();
#endif
            if (!ok || !(input == output)) throw std::runtime_error("combination roundtrip");
        }
        std::cout << "B3_COMBINATION PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
