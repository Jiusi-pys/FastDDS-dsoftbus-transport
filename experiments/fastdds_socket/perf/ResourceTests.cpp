#include "Resources.hpp"
#include <cstdio>
#include <limits>
#include <stdexcept>
#ifndef _WIN32
#include <charconv>
#include <dirent.h>
#include <string_view>
#endif
using namespace fastdds_dsoftbus::experiment::perf;
static void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main()
{
    try {
        const auto parsed = ParseProcStatus("Name:\tfixture\nVmRSS:\t100 kB\nVmHWM:\t200 kB\nThreads:\t7\nFDSize:\t1024\n");
        Require(parsed.rss_bytes.observed && parsed.rss_bytes.value == 102400, "RSS is bytes not kB");
        Require(parsed.hwm_bytes.observed && parsed.hwm_bytes.value == 204800 && parsed.threads.value == 7, "HWM and threads");
        Require(!parsed.fd_count.observed, "FDSize is not open FD count");
        for (const char* malformed : {"", "VmRSS: unavailable\n", "VmRSS: 12 MB\n", "VmRSS: -1 kB\n", "VmRSS: 18446744073709551615 kB\n", "VmRSS: 1 kB garbage\n"})
            Require(!ParseProcStatus(malformed).rss_bytes.observed, "unknown/malformed/overflow unavailable not zero");
        CpuBoundary begin{1000000000, {true, 2000000000}}, end{3000000000, {true, 5000000000}};
        Require(CpuPercent(begin, end).observed && CpuPercent(begin, end).value == 150.0, "CPU is process delta/wall delta may exceed 100");
        const CpuBoundary idle_end{3000000000, {true, 2000000000}};
        Require(CpuPercent(begin, idle_end).observed && CpuPercent(begin, idle_end).value == 0.0, "zero CPU delta remains valid");
        Require(!CpuPercent(end, begin).observed, "negative wall unavailable");
        end.cpu_ns.observed = false;
        Require(!CpuPercent(begin, end).observed, "missing CPU unavailable");
#ifndef _WIN32
        // A single-threaded, independent FD snapshot catches the sampler leaking
        // its status-file descriptor into its own reported application FD count.
        uint64_t expected_fds = 0;
        DIR* fd_directory = opendir("/proc/self/fd");
        Require(fd_directory != nullptr, "POSIX fd evidence available");
        const int directory_fd = dirfd(fd_directory);
        while (dirent* entry = readdir(fd_directory)) {
            const std::string_view name(entry->d_name);
            int fd = -1;
            const auto parsed_fd = std::from_chars(name.data(), name.data() + name.size(), fd);
            if (parsed_fd.ec == std::errc{} && parsed_fd.ptr == name.data() + name.size() && fd >= 0 && fd != directory_fd) ++expected_fds;
        }
        Require(closedir(fd_directory) == 0, "reference sampler closes fd");
#endif
        const auto actual = CaptureResources();
        Require(actual.wall_ns >= 0, "real resource wall clock");
#ifndef _WIN32
        Require(actual.fd_count.observed && actual.fd_count.value == expected_fds, "only actual descriptors excluding sampler files");
#endif
        const auto boundary = CaptureCpuBoundary();
        Require(boundary.wall_ns >= 0 && boundary.cpu_ns.observed, "platform process CPU works");
        std::puts("PASS resource core: strict proc units, unavailable values, process CPU percent and real platform CPU");
        return 0;
    } catch (const std::exception& e) { std::printf("FAIL %s\n", e.what()); return 1; }
}
