#include "v2_client.hpp"
#include "native_ingress.hpp"
#include "parent_protocol.hpp"
#include "v2_route.hpp"
#include <cstdio>
#include <cstring>
namespace stage1_control {
int frontend_control(int argc,char** argv){
    if(argc>=2&&!std::strcmp(argv[1],"--native-fd-prepare"))return native_fd_prepare(argc,argv);
    if(argc>=2&&!std::strcmp(argv[1],"--native-os-child"))return native_os_child(argc,argv);
    if(argc==2&&!std::strcmp(argv[1],"--native-v2-denial"))return native_v2_denial();
    if(argc>=2&&!std::strcmp(argv[1],"--handoff-run")){
        std::puts("HANDOFF_EXECUTION_COMPILE_LOCKED no_handle_parse=1 no_file_open=1 no_getter=1 no_module_load=1 no_identity=1 no_business=1");return 78;
    }
    if(argc>=2&&!std::strcmp(argv[1],"--control-run")){
        std::puts("CONTROL_EXECUTION_COMPILE_LOCKED no_pipe_open=1 no_module_load=1 no_identity=1 no_business=1");return 78;
    }
    if(argc==2&&!std::strcmp(argv[1],"--control-codec-selftest")){
        const bool ok=codec_selftest();std::printf("CONTROL_CODEC_SELFTEST no_io=1 no_module_load=1 no_identity=1 result=%d\n",int(ok));return ok?0:106;
    }
    return -1;
}
}
