#include "api.hpp"
#include <fastdds/dds/topic/Topic.hpp>
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
int main(){
    assert(create_fixture());
    const auto* original=positive_description();assert(original);
    const auto* converted=dynamic_cast<const eprosima::fastdds::dds::Topic*>(original);
    assert(converted&&converted->get_name()=="rtti_positive");
    assert(dynamic_cast<const eprosima::fastdds::dds::Topic*>(unrelated_description())==nullptr);
    assert(*producer_description_type()==typeid(eprosima::fastdds::dds::TopicDescription));
    bool caught=false;
    try{throw_fixture_exception();}catch(const std::runtime_error& error){caught=std::string(error.what())=="owned cross-DSO exception";}
    assert(caught);
    assert(destroy_fixture()==0);
    std::cout<<"RTTI_CROSS_DSO positive=PASS unrelated=PASS exception=PASS explicit_delete=PASS scope=HOST_ABI_ONLY\n";
}
