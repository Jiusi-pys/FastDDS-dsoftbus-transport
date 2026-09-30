#include <cstdlib>
#include <cstdio>
// A link-time import binds ordinary application consumption to the actual
// patched 2.14 DLL. This performs no Runtime/Socket/thread work in static init.
extern "C" __declspec(dllimport) unsigned FastddsDsoftbusFactoryHookAbi();
namespace {
struct AbiCheck {
    AbiCheck(){if(FastddsDsoftbusFactoryHookAbi()!=21405){std::fputs("FDDST_FACTORY_ABI_MISMATCH\n",stderr);std::abort();}}
} abi_check;
}
