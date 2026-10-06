#pragma once
#include <fastdds/dds/core/status/StatusMask.hpp>
namespace fastdds_dsoftbus::experiment {
inline eprosima::fastdds::dds::StatusMask ParticipantListenerMask() {
    // DATA_ON_READERS at a parent takes precedence over the Reader listener.
    // Our parent only observes discovery; its inherited callback is a no-op.
    auto mask=eprosima::fastdds::dds::StatusMask::all();
    mask >> eprosima::fastdds::dds::StatusMask::data_on_readers();
    return mask;
}
}
