#pragma once
#include "Measurement.hpp"
#include <string>
namespace fastdds_dsoftbus::experiment::perf {
struct ObservedU64 { bool observed = false; uint64_t value = 0; };
struct CpuBoundary { int64_t wall_ns = kUnobservedNs; ObservedU64 cpu_ns; };
struct ResourceSnapshot {
    int64_t wall_ns = kUnobservedNs;
    ObservedU64 cpu_ns, rss_bytes, hwm_bytes, threads, fd_count;
};
ResourceSnapshot ParseProcStatus(const std::string& status);
// Heavy /proc reads: boundaries only, never between RTT t0/t1.
ResourceSnapshot CaptureResources();
// Only process CPU + steady wall; capture start after heavy reads, end before reads.
CpuBoundary CaptureCpuBoundary();
Rate CpuPercent(const CpuBoundary& begin, const CpuBoundary& end);
}
