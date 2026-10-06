#pragma once
#include "../ExperimentalTransport.hpp"
#include <memory>
#include <string>
#include <vector>
namespace fastdds_dsoftbus::experiment::matrix {
struct MatrixOptions {
    std::string run,case_id;
    bool board_a=false;
    std::string control_directory;
};
class MatrixFixture final {
public:
    explicit MatrixFixture(MatrixOptions);
    ~MatrixFixture();
    MatrixFixture(const MatrixFixture&)=delete;
    MatrixFixture& operator=(const MatrixFixture&)=delete;
    bool OpenNative(const std::vector<Config>&,SocketApi,std::shared_ptr<ProcessRuntime>);
    bool OpenReference(std::uint16_t base_port);
    bool Run();
    bool Close();
    std::string FirstFailure() const;
    std::string CleanupFailure() const;
    std::vector<std::shared_ptr<Control>> Controls() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
