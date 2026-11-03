#include "LayerSample.hpp"
#include <iostream>
int main() {
    LayerSampleType type;LayerSample sent;sent.run=0x1234567890abcdefULL;sent.side=1;sent.ordinal=5;
    for(unsigned i=0;i<sent.payload.size();++i)sent.payload[i]=std::uint8_t(i*3);
    eprosima::fastrtps::rtps::SerializedPayload_t payload(64);
    if(!type.serialize(&sent,&payload)||!payload.length||payload.length>64)return 1;
    LayerSample taken;if(!type.deserialize(&payload,&taken)||taken.run!=sent.run||taken.side!=sent.side||taken.ordinal!=sent.ordinal||taken.payload!=sent.payload)return 2;
    payload.length=3;if(type.deserialize(&payload,&taken))return 3;
    eprosima::fastrtps::rtps::SerializedPayload_t small(8);if(type.serialize(&sent,&small))return 4;
    if(type.getKey(&sent,nullptr,false))return 5;
    std::cout<<"DDS214_SAMPLE_TEST_PASS checks=5 serialization=CDR_XCDRv1 scope=USER_TYPE_ONLY\n";return 0;
}
