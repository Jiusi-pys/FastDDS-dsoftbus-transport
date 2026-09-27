#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <mutex>
namespace mdds::dsoftbus::runtime_probe {
struct ProbeConfig {
    std::string run,peer_network;
    std::string service_prefix; // main fixes this to mdds.p<run>; Host fake supplies its static directory
    std::string package="com.kaihong.mdds";
    int side=0;
};
using Record=std::array<std::uint8_t,256>;
bool ValidConfig(const ProbeConfig&) noexcept;
void ObserveTokenDiagnostic(std::string& first,const char* phase,std::uint64_t expected,std::uint64_t actual,int rc);
const char* RestorationLabel(unsigned set_calls,bool restored) noexcept;
Record MakeRecord(const std::string&,int side,unsigned sequence);
class RecordLedger {
public:
    RecordLedger(std::string run,int local_side):run_(std::move(run)),side_(local_side){}
    bool Accept(const void*,std::size_t) noexcept;
    unsigned Count()const noexcept;
    unsigned Errors()const noexcept;
private:
    const std::string run_;const int side_;mutable std::mutex gate_;
    std::array<bool,5> seen_{};unsigned count_=0,errors_=0;
};
struct ProbeResult {bool success=false,kind_is_real=false,default_rejected=false,drained=false;unsigned received=0,sent=0,invalid=0;std::string first_failure;};
int ProbeLoadCheck() noexcept;
ProbeResult RunProbe(const ProbeConfig&,const std::function<bool(const char*)>& stage_gate,
        const std::function<void()>& host_link_pump={});
}
