#include "PortableFixture.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
int main(int argc,char** argv){
    if(argc!=6){std::fprintf(stderr,"usage: matrix_reference RUN CASE A|B CONTROL_DIRECTORY BASE_PORT\n");return 64;}
    if(std::string(argv[3])!="A"&&std::string(argv[3])!="B")return 64;
    char* end=nullptr;const auto port=std::strtoul(argv[5],&end,10);if(!end||*end||port<1024||port>65000)return 64;
fastdds_dsoftbus::experiment::matrix::MatrixOptions options{argv[1],argv[2],std::string(argv[3])=="A",argv[4]};
fastdds_dsoftbus::experiment::matrix::MatrixFixture fixture(options);
    const bool run=fixture.OpenReference(static_cast<std::uint16_t>(port))&&fixture.Run();
    const bool closed=fixture.Close();const bool pass=run&&closed&&fixture.FirstFailure().empty()&&fixture.CleanupFailure().empty();
    std::printf("MATRIX_REFERENCE scope=HOST_REFERENCE_UDP result=%s first_failure=%s cleanup_failure=%s native_proof=NONE\n",pass?"PASS":"FAIL",fixture.FirstFailure().empty()?"none":fixture.FirstFailure().c_str(),fixture.CleanupFailure().empty()?"none":fixture.CleanupFailure().c_str());
    std::fflush(stdout);return pass?0:1;
}
