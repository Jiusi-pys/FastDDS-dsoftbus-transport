#include "PortableFixture.hpp"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
int main(int argc, char **argv)
{
    if (argc != 3)
        return 64;
#ifdef _WIN32
    const unsigned pid = _getpid();
#else
    const unsigned pid = getpid();
#endif
    (void)pid;
    char *end = nullptr;
    const auto port = std::strtoul(argv[2], &end, 10);
    if (!end || *end || port < 1024 || port > 65000)
        return 64;
    const bool pass =
fastdds_dsoftbus::experiment::keyed::KeyedFixture::ContractTest(argv[1], static_cast<std::uint16_t>(port));
    std::printf("KEYED_CONTRACT test=%s result=%s\n", argv[1], pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
