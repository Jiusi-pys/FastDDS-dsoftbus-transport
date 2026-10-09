#define NOMINMAX
#include <windows.h>
#include "../../integrations/ros2/FastddsHook.hpp"
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

// Link only Fast DDS, not the bridge. prepare is the sole bridge loader-ref
// acquisition; the observation below neither increments nor pins a reference.
static bool mapped(void* address, HMODULE expected) {
    HMODULE observed=nullptr;
    const auto present=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address), &observed)!=0;
    return present && observed==expected;
}

int main(int argc,char** argv) {
    assert(argc==4);
    const auto library=std::filesystem::absolute(argv[1]);
    const auto config=std::filesystem::absolute(argv[2]);
    const std::string mode=argv[3];
    const bool normal=mode=="public-delete";
    const bool error=mode=="failed-runtime-error";
    assert(normal || error || mode=="failed");
    // The complete Host config is provided by the isolated test runner. It
    // declares actual DDS/CDR origins and the existing opaque-byte Host seam.
    assert(std::filesystem::is_regular_file(config));
    assert(_putenv_s("FASTDDS_DSOFTBUS_REQUIRED_CONFIG",config.string().c_str())==0);
    std::shared_ptr<fastdds_dsoftbus_hook::Lease> lease;
    using Descriptor=eprosima::fastdds::rtps::TransportDescriptorInterface;
    std::weak_ptr<Descriptor> surviving;
    HMODULE module=nullptr;
    void* code=nullptr;
    bool clean=false;
    {
        eprosima::fastdds::dds::DomainParticipantQos qos;
        bool admitted=false;
        lease=fastdds_dsoftbus_hook::prepare(201,&qos,admitted);
        assert(admitted && lease && lease->owner && lease->module);
        module=lease->module;
        code=fastdds_dsoftbus_hook_platform::symbol(module,"FastddsDsoftbusClose");
        char actual[MAX_PATH]{};
        assert(GetModuleFileNameA(module,actual,MAX_PATH));
        assert(std::filesystem::equivalent(library,actual));
        assert(code && mapped(code,module));
        assert(qos.transport().user_transports.size()==1);
        surviving=qos.transport().user_transports.front();
        if(!normal) {
            clean=fastdds_dsoftbus_hook::release_pending(lease);
            assert(lease->close_state==2 && !lease->owner && !lease->module);
            assert(lease->terminal_result==(error?2:0) && clean==!error);
            assert(fastdds_dsoftbus_hook::retired_callback_module()==module);
            assert(surviving.use_count()==1 && mapped(code,module));
            // Dispatch through the real bridge-defined descriptor after Close.
            assert(qos.transport().user_transports.front()->min_send_buffer_size()>0);
            bool repeated=true;
            assert(!fastdds_dsoftbus_hook::prepare(201,&qos,repeated) && !repeated);
            assert(fastdds_dsoftbus_hook::retired_callback_module()==module);
            assert(fastdds_dsoftbus_hook::release_pending(lease)==!error);
            std::cout<<"LIFETIME_AFTER_CLOSE cleanup_result="<<lease->terminal_result
                <<" return="<<clean<<" qos_holder_alive=1 mapped=1 repeat_refused=1\n";
        }
    } // Real QoS destruction releases the last strong descriptor holder.
    assert(normal ? !surviving.expired() : surviving.expired());
    assert(mapped(code,module));
    surviving.reset(); // Release the module-backed control-block deleter too.
    assert(mapped(code,module));
    if(normal) {
        // A fresh process; all external descriptor holders are gone first.
        fastdds_dsoftbus_hook::mark_created(lease);
        clean=fastdds_dsoftbus_hook::release_pending(lease,fastdds_dsoftbus_hook::CloseCause::PublicDelete);
        assert(clean && lease->terminal_result==0 && !lease->owner && !lease->module);
        assert(!fastdds_dsoftbus_hook::retired_callback_module());
        assert(!mapped(code,module));
    }
    std::cout<<"MODULE_LIFETIME_PASS mode="<<mode
        <<" cleanup_result="<<lease->terminal_result
        <<" descriptor_destroyed=1 weak_deleter_released=1 mapped="<<!normal
        <<" observation_extra_loader_refs=0\n";
}
