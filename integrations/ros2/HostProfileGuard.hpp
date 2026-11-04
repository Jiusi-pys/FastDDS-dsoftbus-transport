#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Keep the header self-contained without letting windows.h introduce Winsock 1
// before the Host observation headers use Winsock 2.
#include <winsock2.h>
#include <windows.h>
#include <filesystem>
#include <stdexcept>

namespace fastdds_dsoftbus::ros2_host_profile {
inline void RequireModulePath(HMODULE module, const std::filesystem::path& expected)
{
    wchar_t loaded[32768]{};
    const DWORD length = module ? GetModuleFileNameW(module, loaded, 32768) : 0;
    if (!length || length >= 32768 || !expected.is_absolute() ||
            !std::filesystem::is_regular_file(expected) ||
            !std::filesystem::equivalent(expected, std::filesystem::path(loaded)))
        throw std::runtime_error("historical Host profile loaded dependency path mismatch");
}

inline unsigned ReadFactoryMode(HMODULE module, const char* symbol)
{
    auto address = module ? GetProcAddress(module, symbol) : nullptr;
    HMODULE provider = nullptr;
    // Forwarded exports must not silently attest a different DLL.
    if (!address || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address), &provider) || provider != module)
        throw std::runtime_error("historical Host profile factory export missing or forwarded");
    return reinterpret_cast<unsigned (*)()>(address)();
}

inline void ValidateHistoricalFactory(HMODULE module,
        const std::filesystem::path& expected, unsigned security)
{
    RequireModulePath(module, expected);
    if (ReadFactoryMode(module, "FastddsDsoftbusFactoryHookAbi") != 21405)
        throw std::runtime_error("historical Host profile factory hook ABI mismatch");
    if (ReadFactoryMode(module, "FastddsDsoftbusFactorySecurityMode") != security)
        throw std::runtime_error("historical Host profile factory security mismatch");
    // The original factory exports no statistics-mode attestation. Full ABI
    // qualification still requires bound headers/libs/DLLs and loaded modules.
}
}
