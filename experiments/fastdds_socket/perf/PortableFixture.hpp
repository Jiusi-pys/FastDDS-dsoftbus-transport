#pragma once
#include "../ExperimentalTransport.hpp"
#include "Measurement.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace mdds::experiment::perf {
struct Options {
    std::string run;
    bool board_a = false;
    std::string control_directory;
    Mode mode = Mode::Trace;
    unsigned repeat = 1;
    bool test_factory_preconfigured = false;
    bool host_contract = false;
    // Output-only observer; true suppresses routine tracing, never changes DDS.
    std::function<void(bool)> quiet_observer;
    // Capture into bounded root-owned storage. Only *_FLUSH/AFTER_DDS_CLOSE
    // callbacks may print; captures execute outside every request RTT interval.
    std::function<void(const char*,unsigned)> observation_snapshot;
};
class PerformanceFixture final {
public:
    explicit PerformanceFixture(Options);
    ~PerformanceFixture();
    PerformanceFixture(const PerformanceFixture&) = delete;
    PerformanceFixture& operator=(const PerformanceFixture&) = delete;
    bool OpenNative(const std::vector<Config>&, SocketApi, std::shared_ptr<ProcessRuntime>);
    bool OpenReference(std::uint16_t base_port);
    bool Run();
    bool Close();
    std::string FirstFailure() const;
    std::string CleanupFailure() const;
    std::vector<std::shared_ptr<Control>> Controls() const;
#ifdef MDDS_PERF_HOST_CONTRACT_TESTS
    static bool ContractTest(const std::string&, std::uint16_t base_port);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
