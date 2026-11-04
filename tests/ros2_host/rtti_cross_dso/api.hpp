#pragma once
#include <fastdds/dds/topic/TopicDescription.hpp>
#include <typeinfo>
#if defined(_WIN32)
# if defined(RTTI_PRODUCER)
#  define RTTI_API __declspec(dllexport)
# else
#  define RTTI_API __declspec(dllimport)
# endif
#else
# define RTTI_API __attribute__((visibility("default")))
#endif
extern "C" {
RTTI_API bool create_fixture();
RTTI_API const eprosima::fastdds::dds::TopicDescription* positive_description();
RTTI_API const eprosima::fastdds::dds::TopicDescription* unrelated_description();
RTTI_API const std::type_info* producer_description_type();
RTTI_API int destroy_fixture();
RTTI_API void throw_fixture_exception();
}
