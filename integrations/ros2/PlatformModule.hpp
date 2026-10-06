#pragma once
// Platform loading only. No SDK invocation, Runtime, cancellation or ownership.
#include <string>
#if defined(_WIN32)
#include <windows.h>
#define FASTDDS_DSOFTBUS_FACTORY_EXPORT __declspec(dllexport)
#else
#include <dlfcn.h>
#include <sys/stat.h>
#define FASTDDS_DSOFTBUS_FACTORY_EXPORT __attribute__((visibility("default")))
#endif

namespace fastdds_dsoftbus_hook_platform {
#if defined(_WIN32)
using Module = HMODULE;
inline bool absolute_file(const std::string& path) {
    const bool rooted = (path.size()>2 && path[1]==':' && (path[2]=='\\' || path[2]=='/')) ||
            (path.size()>1 && path[0]=='\\' && path[1]=='\\');
    const auto attributes = GetFileAttributesA(path.c_str());
    return rooted && attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
inline Module load(const char* path) { return absolute_file(path) ? LoadLibraryA(path) : nullptr; }
inline void* symbol(Module module, const char* name) {
    return module ? reinterpret_cast<void*>(GetProcAddress(module,name)) : nullptr;
}
inline bool unload(Module module) { return module && FreeLibrary(module)!=0; }
#else
using Module = void*;
inline bool absolute_file(const std::string& path) {
    struct stat info{};
    return !path.empty() && path.front()=='/' && stat(path.c_str(),&info)==0 && S_ISREG(info.st_mode);
}
inline Module load(const char* path) { return absolute_file(path) ? dlopen(path,RTLD_NOW|RTLD_LOCAL) : nullptr; }
inline void* symbol(Module module, const char* name) { return module ? dlsym(module,name) : nullptr; }
inline bool unload(Module module) { return module && dlclose(module)==0; }
#endif
}
