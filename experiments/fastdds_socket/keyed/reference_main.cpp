#include "PortableFixture.hpp"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv)
{
    if (argc != 6 || (std::string(argv[3]) != "A" && std::string(argv[3]) != "B"))
        return 64;
    char *end = nullptr;
    const auto port = std::strtoul(argv[5], &end, 10);
    if (!end || *end || port < 1024 || port > 65000)
        return 64;
fastdds_dsoftbus::experiment::keyed::KeyedFixture f({argv[1], argv[2], std::string(argv[3]) == "A", argv[4]});
    const bool good = f.OpenReference(static_cast<std::uint16_t>(port)) && f.Run();
    const bool clean = f.Close();
    const bool pass = good && clean && f.FirstFailure().empty() && f.CleanupFailure().empty();
    std::printf("KEYED_REFERENCE scope=HOST_REFERENCE_UDP result=%s first_failure=%s cleanup_failure=%s "
                "native_proof=NONE\n",
                pass ? "PASS" : "FAIL", f.FirstFailure().empty() ? "none" : f.FirstFailure().c_str(),
                f.CleanupFailure().empty() ? "none" : f.CleanupFailure().c_str());
    return pass ? 0 : 1;
}
