#include "PortableFixture.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
using namespace fastdds_dsoftbus::experiment::perf;
    if(argc!=8 || (std::string(argv[2])!="A" && std::string(argv[2])!="B") ||
        (std::string(argv[4])!="TRACE" && std::string(argv[4])!="MEASURE") ||
        (std::string(argv[7])!="HOST_CONTRACT" && std::string(argv[7])!="FULL")) return 64;
    char* end=nullptr; const auto port=std::strtoul(argv[6],&end,10);
    if(!end||*end||port<1024||port>65000) return 64;
    char* repeat_end=nullptr; const auto repeat=std::strtoul(argv[5],&repeat_end,10);
    if(!repeat_end||*repeat_end||repeat<1||repeat>3) return 64;
#ifdef _WIN32
    if(_putenv_s("SKIP_DEFAULT_XML","1")!=0) return 64;
#else
    if(setenv("SKIP_DEFAULT_XML","1",1)!=0) return 64;
#endif
    Options options; options.run=argv[1]; options.board_a=std::string(argv[2])=="A";
    options.control_directory=argv[3]; options.mode=std::string(argv[4])=="TRACE"?Mode::Trace:Mode::Measure;
    options.repeat=static_cast<unsigned>(repeat); options.host_contract=std::string(argv[7])=="HOST_CONTRACT";
    unsigned quiet_on=0,quiet_off=0;
    options.quiet_observer=[&](bool quiet){quiet?++quiet_on:++quiet_off;};
    PerformanceFixture fixture(options);
    const bool good=fixture.OpenReference(static_cast<uint16_t>(port))&&fixture.Run();
    const bool clean=fixture.Close();
    const bool pass=good&&clean&&fixture.FirstFailure().empty()&&fixture.CleanupFailure().empty();
    std::printf("PERF_REFERENCE result=%s native_proof=NONE quiet_on=%u quiet_off=%u first_failure=%s cleanup_failure=%s\n",
        pass?"PASS":"FAIL",quiet_on,quiet_off,fixture.FirstFailure().empty()?"none":fixture.FirstFailure().c_str(),fixture.CleanupFailure().empty()?"none":fixture.CleanupFailure().c_str());
    return pass?0:1;
}
