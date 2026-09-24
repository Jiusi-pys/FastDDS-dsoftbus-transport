#include "Resources.hpp"
#include <charconv>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <dirent.h>
#include <time.h>
#endif

namespace mdds::experiment::perf {
namespace {
ObservedU64 ParseValue(const std::string& text, bool kilobytes)
{
    std::istringstream line(text);
    std::string number, unit, extra;
    if (!(line >> number) || number.empty() || number[0] == '-') return {};
    uint64_t value = 0;
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) return {};
    if (kilobytes && (!(line >> unit) || unit != "kB")) return {};
    if (line >> extra) return {};
    if (kilobytes) { if (value > UINT64_MAX / 1024) return {}; value *= 1024; }
    return {true, value};
}
ObservedU64 ProcessCpuNs()
{
#ifdef _WIN32
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return {};
    const uint64_t k = (uint64_t{kernel.dwHighDateTime} << 32) | kernel.dwLowDateTime;
    const uint64_t u = (uint64_t{user.dwHighDateTime} << 32) | user.dwLowDateTime;
    uint64_t total = k;
    if (!CheckedAdd(total, u) || total > UINT64_MAX / 100) return {};
    return {true, total * 100};
#else
    timespec value{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0 || value.tv_nsec >= 1000000000) return {};
    const auto seconds = static_cast<uint64_t>(value.tv_sec);
    if (seconds > UINT64_MAX / 1000000000) return {};
    uint64_t result = seconds * 1000000000;
    if (!CheckedAdd(result, static_cast<uint64_t>(value.tv_nsec))) return {};
    return {true, result};
#endif
}
}
ResourceSnapshot ParseProcStatus(const std::string& status)
{
    ResourceSnapshot result;
    std::istringstream stream(status);
    std::string line;
    bool rss_seen = false, hwm_seen = false, threads_seen = false;
    while (std::getline(stream, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const auto key = line.substr(0, colon), value = line.substr(colon + 1);
        if (key == "VmRSS") { result.rss_bytes = rss_seen ? ObservedU64{} : ParseValue(value, true); rss_seen = true; }
        else if (key == "VmHWM") { result.hwm_bytes = hwm_seen ? ObservedU64{} : ParseValue(value, true); hwm_seen = true; }
        else if (key == "Threads") { result.threads = threads_seen ? ObservedU64{} : ParseValue(value, false); threads_seen = true; }
    }
    return result;
}
ResourceSnapshot CaptureResources()
{
    ResourceSnapshot result;
#ifndef _WIN32
    // /proc files are bounded here; an unexpected giant read is unavailable.
    std::ifstream status("/proc/self/status", std::ios::binary);
    if (status) {
        char buffer[16385]{};
        status.read(buffer, 16384);
        if (status.eof()) result = ParseProcStatus(std::string(buffer, static_cast<std::size_t>(status.gcount())));
    }
    status.close();
    if (DIR* directory = opendir("/proc/self/fd")) {
        const int own_fd = dirfd(directory);
        uint64_t count = 0;
        bool complete = own_fd >= 0;
        errno = 0;
        while (dirent* entry = readdir(directory)) {
            const std::string_view name(entry->d_name);
            uint64_t descriptor = 0;
            const auto parsed = std::from_chars(name.data(), name.data() + name.size(), descriptor);
            if (!name.empty() && parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
                    descriptor != static_cast<uint64_t>(own_fd) && !CheckedAdd(count, 1)) complete = false;
            errno = 0;
        }
        if (errno != 0) complete = false;
        if (closedir(directory) != 0) complete = false;
        if (complete) result.fd_count = {true, count};
    }
#endif
    // Windows /proc metrics remain unavailable; do not substitute handles for FDs.
    result.cpu_ns = ProcessCpuNs();
    result.wall_ns = ClockNowNs();
    return result;
}
CpuBoundary CaptureCpuBoundary()
{
    CpuBoundary result;
    result.cpu_ns = ProcessCpuNs();
    result.wall_ns = ClockNowNs();
    return result;
}
Rate CpuPercent(const CpuBoundary& begin, const CpuBoundary& end)
{
    if (!begin.cpu_ns.observed || !end.cpu_ns.observed || begin.wall_ns < 0 || end.wall_ns <= begin.wall_ns ||
            end.cpu_ns.value < begin.cpu_ns.value) return {};
    return {true, static_cast<double>(100.0L * (end.cpu_ns.value - begin.cpu_ns.value) / (end.wall_ns - begin.wall_ns))};
}
}
