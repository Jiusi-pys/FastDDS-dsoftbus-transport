#include <cstdlib>
#if defined(_WIN32)
#define HOOK_CONFIG_EXPORT __declspec(dllexport)
#else
#define HOOK_CONFIG_EXPORT __attribute__((visibility("default")))
#endif
// Loader contract fixture only, not a Runtime, provider or SDK success model.
extern "C" HOOK_CONFIG_EXPORT unsigned FastddsDsoftbusHookVersion() { return 21405; }
extern "C" HOOK_CONFIG_EXPORT int FastddsDsoftbusCreate(const char*, unsigned, void*, void** out) {
    *out = std::malloc(1);
    return *out ? 0 : 1;
}
extern "C" HOOK_CONFIG_EXPORT int FastddsDsoftbusClose(void* owner) { std::free(owner); return 0; }
