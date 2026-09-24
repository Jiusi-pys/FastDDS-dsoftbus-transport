#pragma once
#include "softbus_record_contract.hpp"
#include <array>
#include <mutex>

namespace mdds_probe {
enum class LocalSide { Listener, Client };
// Single-run diagnostic only: two explicitly different callback tables, one
// listener, one client, one accepted ID. No reconnect or native-ID reuse claim.
class LocalPairProbeContract {
public:
    LocalPairProbeContract(std::string listener_name, std::string client_name,
            std::string local_network, int data_type);
    bool Created(LocalSide side, int id);
    bool BeginAccepted(int id);
    bool CompleteAccepted(int id, RecordPeer peer);
    bool Accepted(int id, RecordPeer peer);
    SoftbusRecordContract& Channel(LocalSide side);
    RecordShutdownTargets ClaimShutdownTargets();
    bool Failed() const;
private:
    SoftbusRecordContract server_, client_;
    mutable std::mutex mutex_;
    std::array<int, 3> ids_{};
    bool failed_ = false;
};
}
