#ifndef MDDS_APPLICATION_ATTACHMENTS_TEST_STACK_HPP
#define MDDS_APPLICATION_ATTACHMENTS_TEST_STACK_HPP
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")
#include <iostream>
inline void PrintAttachmentFailureStack()
{
    const auto process = GetCurrentProcess();
    if (!SymInitialize(process, nullptr, TRUE)) return;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    void* frames[40]{};
    const auto count = CaptureStackBackTrace(0, 40, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 511;
        DWORD64 delta = 0; const auto address = reinterpret_cast<DWORD64>(frames[i]);
        if (SymFromAddr(process, address, &delta, symbol)) std::cerr << symbol->Name;
        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(line); DWORD offset = 0;
        if (SymGetLineFromAddr64(process, address, &offset, &line)) std::cerr << " " << line.FileName << ':' << line.LineNumber;
        std::cerr << '\n';
    }
    SymCleanup(process);
}
#else
inline void PrintAttachmentFailureStack() {}
#endif
#endif
