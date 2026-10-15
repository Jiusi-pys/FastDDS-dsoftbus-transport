// Adapt the existing malloc/free hook-config fixture to the original factory's
// security-capability handshake. It remains a synthetic hook, never transport.
#include <fastrtps/config.h>
extern "C" __declspec(dllexport) unsigned FastddsDsoftbusHookSecurity()
{
    return HAVE_SECURITY;
}
