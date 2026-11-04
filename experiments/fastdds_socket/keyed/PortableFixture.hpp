#pragma once
#include "../ExperimentalTransport.hpp"
#include <memory>
#include <string>
#include <vector>
namespace fastdds_dsoftbus::experiment::keyed
{
struct Options
{
    std::string run, case_id;
    bool board_a = false;
    std::string control_directory;
};
class KeyedFixture final
{
  public:
    explicit KeyedFixture(Options);
    ~KeyedFixture();
    KeyedFixture(const KeyedFixture &) = delete;
    KeyedFixture &operator=(const KeyedFixture &) = delete;
    bool OpenReference(std::uint16_t base_port);
    bool OpenNative(const std::vector<Config> &, SocketApi, std::shared_ptr<ProcessRuntime>);
    bool Run();
    bool Close();
    std::string FirstFailure() const;
    std::string CleanupFailure() const;
    std::vector<std::shared_ptr<Control>> Controls() const;
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
    // Available only in host test builds; normal reference runs leave all faults disabled.
    static bool ContractTest(const std::string &test, std::uint16_t base_port);
#endif

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace fastdds_dsoftbus::experiment::keyed
