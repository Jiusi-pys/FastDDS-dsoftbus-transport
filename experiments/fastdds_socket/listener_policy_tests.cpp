#include "ListenerPolicy.hpp"
#include <cstdio>
int main() {
    using eprosima::fastdds::dds::StatusMask;
    const auto mask=mdds::experiment::ParticipantListenerMask();
    const bool intercepts=(mask & StatusMask::data_on_readers()).any();
    const bool data_available=(mask & StatusMask::data_available()).any();
    const bool matched=(mask & StatusMask::subscription_matched()).any() &&
        (mask & StatusMask::publication_matched()).any();
    std::printf("participant_listener data_on_readers_intercepts=%d data_available=%d matched=%d\n",
        intercepts,data_available,matched);
    return !intercepts && data_available && matched ? 0 : 1;
}
