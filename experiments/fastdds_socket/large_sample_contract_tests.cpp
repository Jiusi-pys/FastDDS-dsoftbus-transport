#include "LargeSampleContract.hpp"
#include "large_sample/ExperimentLargeSamplePubSubTypes.hpp"
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <cstdio>
#include <stdexcept>
using namespace mdds::experiment;
static void Require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
int main() {
    try {
        const std::string run="e4f5a6b7c8d90123";
        ExperimentLargeSample sample;
        Require(FillLargeSample(run,1,262144,sample),"valid 256 KiB sample admitted");
        Require(sample.index()==1 && sample.data().size()==262144,"exact configured size/index");
        Require(LargeSampleWriteReady(run,262144,sample),"valid prepared sample admitted before write");
        Require(!LargeSampleWriteReady("e4f5a6b7c8d90124",262144,sample),"other run content rejected");
        const auto first_hash=LargeSampleHash(sample);
        sample.data()[262143]^=1;
        Require(!LargeSampleWriteReady(run,262144,sample),"last byte corruption rejected");
        sample.data()[262143]^=1; sample.index(2);
        Require(!LargeSampleWriteReady(run,262144,sample),"body cannot be reassigned to another index");
        Require(FillLargeSample(run,2,262144,sample) && LargeSampleHash(sample)!=first_hash,"independent index-dependent body");
        for(auto size : {std::size_t(0),std::size_t(1),std::size_t(262143),std::size_t(262145),large_sample_bound+1,
                (std::size_t(1)<<32)+262144}) {
            const auto before=sample;
            Require(!FillLargeSample(run,1,size,sample) && sample==before,"invalid requested size rejected without mutation");
        }
        Require(!FillLargeSample(run,0,262144,sample) && !FillLargeSample(run,4,262144,sample),"only three identities allowed");
        Require(!FillLargeSample("not-a-run",1,262144,sample),"invalid run rejected");
        Require(FillLargeSample(run,3,large_sample_bound,sample),"maximum IDL bounded payload admitted");
        Require(LargeSampleWriteReady(run,large_sample_bound,sample),"entire maximum body validated");
        ExperimentLargeSamplePubSubType type;
        const auto rep=eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION;
        const auto serialized_size=type.calculate_serialized_size(&sample,rep);
        Require(serialized_size>sample.data().size() && serialized_size<=type.max_serialized_type_size,"DDS serialized sample includes CDR metadata");
        eprosima::fastdds::rtps::SerializedPayload_t payload(serialized_size);
        Require(type.serialize(&sample,payload,rep) && payload.length==serialized_size,"real generated bounded type serializes");
        ExperimentLargeSample recovered;
        Require(type.deserialize(payload,&recovered) && recovered==sample,"real generated CDR round trip");
        // The generated C++ vector remains mutable. Check pre-write admission rather
        // than pretending its IDL declaration is a runtime container bound.
        sample.data().push_back(0);
        Require(sample.data().size()==large_sample_bound+1,"generated vector can exceed bound");
        unsigned write_calls=0;
        if(LargeSampleWriteReady(run,large_sample_bound,sample)) ++write_calls;
        Require(write_calls==0,"bound plus one rejected before DDS write admission");
        Require(!LargeSampleWriteReady(run,large_sample_bound+1,sample),"caller cannot raise configured bound");
        std::puts("PASS bounded large sample: run/index/size/content admission; real CDR round trip; oversized vector rejected before write");
        return 0;
    } catch(const std::exception& e) { std::printf("FAIL %s\n",e.what()); return 1; }
}
