// HOST structural observer tests. No Fast DDS, fake Socket provider or device.
#include "NativeObservation.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
std::uint64_t SocketFrame(const char*,std::int32_t,const std::uint8_t*,std::size_t) noexcept;
}
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
void TraceText(const char*,const char*) noexcept;
}
namespace observation=fastdds_dsoftbus::ros2_target_observation;
namespace experiment=fastdds_dsoftbus::experiment;
using fastdds_dsoftbus::dsoftbus::dds_experiment::SocketFrame;
std::atomic<std::size_t> allocations{0};
void* operator new(std::size_t size){++allocations;if(auto p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}

std::vector<std::uint8_t> packet(std::uint32_t sequence,unsigned parameters=2,unsigned bytes=24,unsigned encoding=3){
    std::vector<std::uint8_t> out{'R','T','P','S',2,3,1,15};out.insert(out.end(),12,0xa7);
    std::vector<std::uint8_t> body{0,0,16,0,0,0,0,0,0,1,0,0xc2,0,0,0,0,
        std::uint8_t(sequence),std::uint8_t(sequence>>8),std::uint8_t(sequence>>16),std::uint8_t(sequence>>24),0,std::uint8_t(encoding),0,0};
    for(unsigned i=0;i<parameters;++i){
        body.insert(body.end(),{std::uint8_t(i%2?0x32:0x31),0,std::uint8_t(bytes),0});
        for(unsigned j=0;j<bytes;++j)body.push_back(j==3?0x40:j==4?1:std::uint8_t(j));
    }
    body.insert(body.end(),{1,0,0,0});
    out.insert(out.end(),{0x15,5,std::uint8_t(body.size()),std::uint8_t(body.size()>>8)});
    out.insert(out.end(),body.begin(),body.end());return out;
}
void submit(std::uint32_t sequence,unsigned parameters=2,unsigned bytes=24,unsigned encoding=3){
    const auto p=packet(sequence,parameters,bytes,encoding);SocketFrame("SDK_SEND",7,p.data(),p.size());
}
void repetition(){
    for(unsigned i=1;i<=3000;++i)submit(i);
    observation::V1LocatorSnapshot value;
    assert(observation::CaptureV1(value));
    // The identical original R36 observer fails this expectation by exhausting
    // its append-only array; the original failure must remain recorded.
    assert(!value.faults&&value.advert_count==2&&value.adverts.size()==256);
#ifndef FASTDDS_DSOFTBUS_LEGACY_OBSERVER
    assert(value.adverts_observed==6000&&value.adverts_repeated==5998&&value.batches_repeated==2999);
    for(unsigned i=0;i<2;++i){const auto& item=value.adverts[i];
        assert(item.observation==1&&item.last_observation==3000&&item.occurrences==3000);
        assert(item.value.sequence_low==1&&item.last_sequence_low==3000&&item.batch_size==2);
    }
#endif
    assert(observation::Flush());
}
#ifndef FASTDDS_DSOFTBUS_LEGACY_OBSERVER
using Snapshot=observation::V1LocatorSnapshot;
using Fault=Snapshot::Fault;
experiment::SpdpLocatorBatch batch(unsigned sequence=1){
    const auto p=packet(sequence);const auto result=experiment::ObserveSpdpLocators(p.data(),p.size());
    assert(result.valid&&result.count==2);return result;
}
void keys(){
    const auto original=batch();
    for(unsigned dimension=0;dimension<10;++dimension){
        Snapshot ledger;ledger.RecordAdverts(1,true,7,original);auto changed=original;
        bool sending=true;int socket=7;
        switch(dimension){
            case 0:sending=false;break;case 1:socket=8;break;
            case 2:changed.values[0].writer[0]^=1;break;case 3:changed.values[0].pid=0x32;break;
            case 4:changed.values[0].serialized_bytes=20;break;case 5:changed.values[0].kind^=1;break;
            case 6:changed.values[0].address[0]^=1;break;case 7:changed.values[0].port^=1;break;
            case 8:changed.values[0].data_submessage_offset+=4;break;
            case 9:std::swap(changed.values[0],changed.values[1]);break;
        }
        ledger.RecordAdverts(2,sending,socket,changed);
        assert(!ledger.faults&&ledger.advert_count==4&&ledger.adverts_repeated==0);
    }
}
void coherence(){
    Snapshot ledger;auto a=batch(5),b=batch(6);b.values[1].address[0]^=1;
    ledger.RecordAdverts(5,true,7,a);ledger.RecordAdverts(6,true,7,b);
    a.values[0].sequence_low=a.values[1].sequence_low=1;ledger.RecordAdverts(1,true,7,a);
    a.values[0].sequence_low=a.values[1].sequence_low=8;ledger.RecordAdverts(8,true,7,a);
    assert(ledger.advert_count==4&&ledger.adverts_observed==8&&ledger.adverts_repeated==4);
    assert(ledger.adverts[0].observation==ledger.adverts[1].observation&&ledger.adverts[0].observation==1);
    assert(ledger.adverts[0].value.sequence_low==1&&ledger.adverts[1].value.sequence_low==1);
    assert(ledger.adverts[0].last_observation==8&&ledger.adverts[1].last_observation==8);
    assert(ledger.adverts[2].observation==6&&ledger.adverts[3].observation==6);
}
void distinct_capacity(){
    Snapshot ledger;auto value=batch();
    for(unsigned i=0;i<128;++i){value.values[0].port=i;ledger.RecordAdverts(i+1,true,7,value);}
    assert(ledger.advert_count==256&&!ledger.faults);
    value.values[0].port=128;ledger.RecordAdverts(129,true,7,value);
    assert(ledger.advert_count==256&&ledger.faults==1&&ledger.fault_counts[std::size_t(Fault::AdvertCapacity)]==1);
    value.values[0].port=0;ledger.RecordAdverts(130,true,7,value);
    assert(ledger.advert_count==256&&ledger.adverts[0].occurrences==2&&ledger.faults==1);
    Snapshot partial;value.count=1;
    for(unsigned i=0;i<255;++i){value.values[0].port=i;partial.RecordAdverts(i+1,true,7,value);}
    value.count=2;value.values[0].port=999;partial.RecordAdverts(256,true,7,value);
    assert(partial.advert_count==255&&partial.fault_counts[std::size_t(Fault::AdvertCapacity)]==1);
}
void counters(){
    constexpr auto maximum=(std::numeric_limits<std::uint64_t>::max)();auto value=batch();
    for(unsigned dimension=0;dimension<4;++dimension){
        Snapshot ledger;ledger.RecordAdverts(1,true,7,value);
        if(dimension==0)ledger.adverts_observed=maximum;
        if(dimension==1)ledger.adverts_repeated=maximum;
        if(dimension==2)ledger.batches_repeated=maximum;
        if(dimension==3)ledger.adverts[0].occurrences=maximum;
        ledger.RecordAdverts(2,true,7,value);
        assert(ledger.faults==1&&ledger.fault_counts[std::size_t(Fault::CounterCapacity)]==1);
    }
    Snapshot ledger;ledger.RecordAdverts(0,true,7,value);
    assert(ledger.fault_counts[std::size_t(Fault::CounterCapacity)]==1);
    ledger.faults=maximum;ledger.fault_counts[std::size_t(Fault::CounterCapacity)]=maximum;
    ledger.RecordFault(Fault::CounterCapacity);assert(ledger.faults==maximum);
}
void no_allocation(){
    Snapshot ledger;auto value=batch();const auto before=allocations.load();
    for(unsigned i=1;i<=100000;++i){value.values[0].sequence_low=i;value.values[1].sequence_low=i;ledger.RecordAdverts(i,true,7,value);}
    assert(allocations.load()==before&&!ledger.faults&&ledger.advert_count==2&&ledger.adverts_observed==200000);
}
void concurrency(){
    std::vector<std::thread> workers;
    for(unsigned thread=0;thread<4;++thread)workers.emplace_back([thread]{for(unsigned i=0;i<500;++i)submit(1+thread*500+i);});
    for(auto& worker:workers)worker.join();Snapshot value;assert(observation::CaptureV1(value));
    assert(!value.faults&&value.advert_count==2&&value.adverts_observed==4000&&value.adverts_repeated==3998);
    for(unsigned i=0;i<2;++i)assert(value.adverts[i].occurrences==2000&&value.adverts[i].observation==1&&value.adverts[i].last_observation==2000);
    assert(observation::Flush());
}
void negative(const std::string& mode){
    Fault expected=Fault::InvalidFrame;
    if(mode=="invalid_frame")submit(1,2,20);
    else if(mode=="unsupported_frame"){submit(1,2,24,0);expected=Fault::UnsupportedFrame;}
    else if(mode=="parser_capacity"){submit(1,33);expected=Fault::ParserCapacity;}
    else if(mode=="invalid_selection"){
        fastdds_dsoftbus::dsoftbus::runtime_probe::TraceText("V1_SELECTED_LOCATOR","bad");expected=Fault::InvalidSelection;
    }else if(mode=="selection_capacity"){
        for(unsigned i=0;i<257;++i)fastdds_dsoftbus::dsoftbus::runtime_probe::TraceText("V1_SELECTED_LOCATOR","40000000:01000001:0102030405060708090a0b0c0d0e0f10");
        expected=Fault::SelectionCapacity;
    }else if(mode=="advert_capacity"){
        for(unsigned i=0;i<129;++i){auto p=packet(i+1);p[8]=std::uint8_t(i);SocketFrame("SDK_SEND",7,p.data(),p.size());}
        expected=Fault::AdvertCapacity;
    }else std::abort();
    Snapshot value;assert(observation::CaptureV1(value)&&value.faults==1);
    assert(value.fault_counts[std::size_t(expected)]==1);assert(!observation::Flush());
}
#endif
int main(int argc,char** argv){
    assert(argc==2);const std::string mode=argv[1];
    observation::Begin("1234567890abcdef",0);observation::SetSourceForHostInjection();
    if(mode=="repetition")repetition();
#ifndef FASTDDS_DSOFTBUS_LEGACY_OBSERVER
    else if(mode=="keys")keys();else if(mode=="coherence")coherence();
    else if(mode=="distinct_capacity")distinct_capacity();else if(mode=="counters")counters();
    else if(mode=="no_allocation")no_allocation();else if(mode=="concurrency")concurrency();
    else negative(mode);
#else
    else return 2;
#endif
    std::cerr<<"HOST_LOCATOR_OBSERVATION_PASS mode="<<mode<<" device_tested=0\n";return 0;
}
