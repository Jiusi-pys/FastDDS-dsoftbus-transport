#include "softbus_local_pair.hpp"
#include <utility>
namespace fastdds_dsoftbus_probe {
LocalPairProbeContract::LocalPairProbeContract(std::string listen, std::string dial,
        std::string network, int type)
    : server_(true, std::move(dial), network, type),
      client_(false, std::move(listen), std::move(network), type) {}
bool LocalPairProbeContract::Created(LocalSide side, int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const unsigned at = side == LocalSide::Listener ? 0 : 1;
    if (id <= 0 || ids_[at] != 0 || ids_[1-at] == id || ids_[2] == id) {
        failed_ = true; return false;
    }
    ids_[at] = id;
    return Channel(side).RegisterCreated(id);
}
bool LocalPairProbeContract::BeginAccepted(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id <= 0 || id == ids_[0] || id == ids_[1] || (ids_[2] && id != ids_[2])) {
        failed_ = true; return false;
    }
    ids_[2] = id;
    return server_.BeginBind(id);
}
bool LocalPairProbeContract::CompleteAccepted(int id, RecordPeer peer) {
    server_.CompleteBind(id, std::move(peer));
    return !server_.Snapshot().failed;
}
bool LocalPairProbeContract::Accepted(int id, RecordPeer peer) {
    return BeginAccepted(id) && CompleteAccepted(id, std::move(peer));
}
SoftbusRecordContract& LocalPairProbeContract::Channel(LocalSide side)
{ return side == LocalSide::Listener ? server_ : client_; }
RecordShutdownTargets LocalPairProbeContract::ClaimShutdownTargets() {
    std::lock_guard<std::mutex> lock(mutex_);
    RecordShutdownTargets result;
    for (auto* channel : {&client_, &server_})
        for (int id : channel->ClaimShutdownTargets()) result.values[result.count++] = id;
    return result;
}
bool LocalPairProbeContract::Failed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_ || server_.Snapshot().failed || client_.Snapshot().failed;
}
}
