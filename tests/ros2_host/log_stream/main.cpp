#include <fastdds/dds/log/Log.hpp>
#include <fastrtps/config.h>
#include <iostream>

int main()
{
    using eprosima::fastdds::dds::Log;
    // Deliberately flush asynchronous DDS logging between fragments of one
    // application record. This reproduces the observed device stream corruption.
    std::cout << "APPLICATION_RECORD run=";
    EPROSIMA_LOG_INFO(SECURITY, "TEST_ONLY_SECURITY_INITIALIZED");
    EPROSIMA_LOG_INFO(SECURITY_CRYPTO, "TEST_ONLY_CRYPTO_INITIALIZED");
    EPROSIMA_LOG_WARNING(RTPS_PARTICIPANT, "TEST_ONLY_READER_WARNING_RETAINED");
    EPROSIMA_LOG_ERROR(RTPS_PARTICIPANT, "TEST_ONLY_READER_ERROR_RETAINED");
    Log::Flush();
    std::cout << "test count=1" << std::endl;
    Log::KillThread();
    return 0;
}
