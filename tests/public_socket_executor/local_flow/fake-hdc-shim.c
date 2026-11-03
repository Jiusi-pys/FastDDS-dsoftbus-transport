/* fake-hdc.exe - argv-forwarding PE shim for the local device-command boundary.
 *
 * The formal Node seams spawn the device tool with {shell:false}, which on Windows
 * requires a real PE image. This shim is that image and does exactly one thing:
 * hand the raw command-line tail, byte for byte, to
 *     python.exe "<script>" <tail>
 * and return the child's exit code unchanged.
 *
 * It never runs a shell, never interprets the tail, and never falls back to any
 * other program. The raw tail is appended to FAKE_HDC_RAW_LOG before the child
 * starts, so the exact bytes the formal flow issued are preserved even if the
 * child fails to start.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>

static void append_raw(const wchar_t *path, const wchar_t *tail) {
    FILE *f = _wfopen(path, L"ab");
    if (!f) return;
    fwrite(tail, sizeof(wchar_t), wcslen(tail), f);   /* UTF-16LE, no BOM */
    fwrite(L"\n", sizeof(wchar_t), 1, f);
    fclose(f);
}

int wmain(void) {
    const wchar_t *raw = GetCommandLineW();
    const wchar_t *py    = _wgetenv(L"FAKE_HDC_PYTHON");
    const wchar_t *script= _wgetenv(L"FAKE_HDC_SCRIPT");
    const wchar_t *log   = _wgetenv(L"FAKE_HDC_RAW_LOG");

    if (!py || !*py || !script || !*script) {
        fputs("FAKE_HDC_SHIM_MISCONFIGURED: FAKE_HDC_PYTHON/FAKE_HDC_SCRIPT not set\n", stderr);
        return 90;
    }

    /* Skip past argv[0] in the raw command line, honouring a quoted image path. */
    const wchar_t *p = raw;
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'"') { ++p; while (*p && *p != L'"') ++p; if (*p == L'"') ++p; }
    else            { while (*p && *p != L' ' && *p != L'\t') ++p; }
    while (*p == L' ' || *p == L'\t') ++p;
    const wchar_t *tail = p;

    if (log && *log) append_raw(log, tail);

    size_t n = wcslen(py) + wcslen(script) + wcslen(tail) + 64;
    wchar_t *cmd = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, n * sizeof(wchar_t));
    if (!cmd) return 91;
    _snwprintf_s(cmd, n, _TRUNCATE, L"\"%ls\" \"%ls\" %ls", py, script, tail);

    STARTUPINFOW si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fwprintf(stderr, L"FAKE_HDC_SHIM_SPAWN_FAILED: %lu\n", GetLastError());
        HeapFree(GetProcessHeap(), 0, cmd);
        return 92;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 0;
    GetExitCodeProcess(pi.hProcess, &rc);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    HeapFree(GetProcessHeap(), 0, cmd);
    return (int)rc;
}
