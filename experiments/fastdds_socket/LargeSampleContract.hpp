#pragma once
#include "large_sample/ExperimentLargeSample.hpp"
#include "ScenarioChecks.hpp"
#include <cstddef>
#include <cstdint>
#include <string>

namespace fastdds_dsoftbus::experiment {
inline constexpr std::size_t large_sample_bound = 1048576;
// This admission applies to the dedicated experiment before DataWriter::write.
// A generated bounded sequence uses std::vector and alone does not enforce its IDL bound.
inline bool LargeSampleSeed(const std::string& run,unsigned index,std::uint64_t& seed) noexcept {
    if(run.size()!=16 || index<1 || index>3) return false;
    seed=1469598103934665603ULL;
    for(const auto c:run) {
        if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
        seed^=static_cast<unsigned char>(c); seed*=1099511628211ULL;
    }
    seed^=0x9e3779b97f4a7c15ULL*index;
    if(!seed) seed=0xd1b54a32d192ed03ULL;
    return true;
}
inline std::uint8_t NextLargeSampleByte(std::uint64_t& state) noexcept {
    state^=state>>12; state^=state<<25; state^=state>>27;
    return static_cast<std::uint8_t>((state*2685821657736338717ULL)>>56);
}
inline bool FillLargeSample(const std::string& run,unsigned index,std::size_t bytes,ExperimentLargeSample& out) {
    std::uint64_t state=0;
    if(!bytes || bytes>large_sample_bound || !ValidSampleBytes(static_cast<std::uint32_t>(bytes)) || !LargeSampleSeed(run,index,state)) return false;
    ExperimentLargeSample sample;
    sample.index(index); sample.data().resize(bytes);
    for(auto& byte:sample.data()) byte=NextLargeSampleByte(state);
    out=std::move(sample);
    return true;
}
inline bool LargeSampleWriteReady(const std::string& run,std::size_t bytes,const ExperimentLargeSample& sample) noexcept {
    std::uint64_t state=0;
    if(!bytes || bytes>large_sample_bound || !ValidSampleBytes(static_cast<std::uint32_t>(bytes)) || sample.data().size()!=bytes ||
            !LargeSampleSeed(run,sample.index(),state)) return false;
    for(const auto byte:sample.data()) if(byte!=NextLargeSampleByte(state)) return false;
    return true;
}
inline std::uint64_t LargeSampleHash(const ExperimentLargeSample& sample) noexcept {
    std::uint64_t hash=1469598103934665603ULL;
    for(auto byte:sample.data()) { hash^=byte; hash*=1099511628211ULL; }
    return hash;
}
}
