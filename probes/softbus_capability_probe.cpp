// Standalone S2 preflight, not a transport or a communication test.
#include <socket.h>
#include <chrono>
#include <iostream>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace {
void usage()
{
    std::cerr << "Usage: softbus_capability_probe --library <explicit-path> [--socket-check]\n"
              << "Socket check uses only com.kaihong.mdds; it does not change access tokens or permissions.\n";
}

// Keep the provider loaded for the lifetime of this short-lived process. SDK
// global threads are not ours to dlclose. This is NOT proof of SDK quiescence.
class Provider
{
public:
    explicit Provider(const std::string& path)
    {
#ifdef _WIN32
        handle_ = LoadLibraryA(path.c_str());
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    }
    bool loaded() const { return handle_ != nullptr; }
    template<typename T> T symbol(const char* name) const
    {
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(handle_, name));
#else
        return reinterpret_cast<T>(dlsym(handle_, name));
#endif
    }
private:
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    std::string path;
    bool socket_check = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg(argv[i]);
        if (arg == "--library" && path.empty() && i + 1 < argc)
            path = argv[++i];
        else if (arg == "--socket-check" && !socket_check)
            socket_check = true;
        else { usage(); return 64; }
    }
    if (path.empty()) { usage(); return 64; }
    Provider provider(path);
    if (!provider.loaded())
    {
        std::cerr << "stage=load result=FAIL library=" << path << '\n';
#ifdef _WIN32
        std::cerr << "loader_error=" << GetLastError() << '\n';
#else
        const char* error = dlerror();
        std::cerr << "loader_error=" << (error ? error : "unknown") << '\n';
#endif
        return 2;
    }
    std::cout << "stage=load result=PASS library=" << path << '\n';
    bool complete = true;
    for (const char* name : {"Socket", "Listen", "Bind", "SendBytes", "Shutdown"})
    {
        const bool present = provider.symbol<void (*)()>(name) != nullptr;
        std::cout << "symbol=" << name << " required=true present=" << (present ? "true" : "false") << '\n';
        complete = complete && present;
    }
    for (const char* name : {"BindAsync", "SendBytesAsync"})
        std::cout << "symbol=" << name << " required=false present="
                  << (provider.symbol<void (*)()>(name) ? "true" : "false") << '\n';
    if (!complete)
    {
        std::cout << "stage=symbols result=FAIL communication=NOT_RUN\n";
        return 2;
    }
    std::cout << "stage=symbols result=PASS communication=NOT_RUN\n";
    if (!socket_check) return 0;

    char package[] = "com.kaihong.mdds";
#ifdef _WIN32
    const auto process_id = GetCurrentProcessId();
#else
    const auto process_id = getpid();
#endif
    // Own unique name prevents reusing a live socket of an unrelated caller.
    std::string name = "com.kaihong.mdds.probe." + std::to_string(process_id) + "." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    SocketInfo info{};
    info.name = name.data();
    info.pkgName = package;
    info.dataType = DATA_TYPE_BYTES;
    const auto create = provider.symbol<decltype(&Socket)>("Socket");
    const auto shutdown = provider.symbol<decltype(&Shutdown)>("Shutdown");
    std::cout << "stage=Socket result=START name=" << name << " package=" << package << '\n';
    const auto start = std::chrono::steady_clock::now();
    const int32_t id = create(info);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (id <= 0)
    {
        std::cout << "stage=Socket result=FAIL code=" << id << " elapsed_ms=" << elapsed << '\n';
        return 3;
    }
    std::cout << "stage=Socket result=PASS id=" << id << " elapsed_ms=" << elapsed << '\n';
    std::cout << "stage=Shutdown result=START id=" << id << '\n';
    const auto close_start = std::chrono::steady_clock::now();
    shutdown(id);
    std::cout << "stage=Shutdown result=RETURNED elapsed_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - close_start).count()
              << " quiescence=NOT_VERIFIED communication=NOT_RUN\n";
    return 0;
}
