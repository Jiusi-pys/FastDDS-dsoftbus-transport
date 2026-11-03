#include <windows.h>
#include "contract.h"
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    (void)instance;(void)reserved;
    if(reason==DLL_PROCESS_ATTACH)(void)dd_host_witness(DD_ATTACH);
    if(reason==DLL_PROCESS_DETACH)(void)dd_host_witness(DD_DETACH);
    return TRUE;
}
DD_EXPORT int DD_CALL intentionally_not_the_contract(void){return 1;}
