// Linked behind the frozen IdentityStartup main: no Python initialization here
// runs until the selected native identity has passed token-first admission.
#include <Python.h>
#include "TeardownDiagnostics.hpp"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>

extern "C" int FastddsDsoftbusFactoryCaptureTeardown(fastdds_dsoftbus::ros2_teardown::Snapshot*) noexcept;
extern "C" void FastddsDsoftbusTeardownCheckpoint(const char*) noexcept;
extern "C" int FastddsDsoftbusTeardownResult() noexcept;

namespace {
constexpr const char* runtime = "/data/python312-rk3588a-verify-c646982b346f/usr";
// The selected OHOS loader filters extension relocations on DF_1_GLOBAL.
// This is an explicitly derived owned copy, not an alternate Python runtime.
bool owned_python_provider(const std::filesystem::path& prefix, const char* phase)
{
    namespace fs = std::filesystem;
    const auto selected = prefix / "lib/libpython3.12.so.1.0";
    const char* names[] = {"Py_InitializeFromConfig", "PyModuleDef_Init"};
    const void* linked[] = {reinterpret_cast<const void*>(&Py_InitializeFromConfig),
                           reinterpret_cast<const void*>(&PyModuleDef_Init)};
    for (std::size_t index = 0; index != 2; ++index) {
        Dl_info info{};
        std::error_code error;
        if (dlsym(RTLD_DEFAULT, names[index]) != linked[index] || !dladdr(linked[index], &info) || !info.dli_fname ||
                !fs::path(info.dli_fname).is_absolute() || fs::canonical(info.dli_fname, error) != selected || error) {
            std::fprintf(stderr, "ROS_PYTHON_PROVIDER_REJECTED phase=%s reason=linked_or_global_provider symbol=%s\n", phase, names[index]);
            return false;
        }
    }
    std::ifstream maps("/proc/self/maps");
    if (!maps) return false;
    std::set<std::string> providers;
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("/libpython3.12.so") == std::string::npos) continue;
        std::istringstream fields(line);
        std::string address, permissions, offset, device, inode, path;
        if (!(fields >> address >> permissions >> offset >> device >> inode)) return false;
        std::getline(fields >> std::ws, path);
        if (path != selected.string()) {
            std::fprintf(stderr, "ROS_PYTHON_PROVIDER_REJECTED phase=%s reason=unexpected_python_mapping\n", phase);
            return false;
        }
        providers.insert(path);
    }
    if (maps.bad() || providers.size() != 1) return false;
    std::fprintf(stdout, "ROS_PYTHON_PROVIDER phase=%s owned=1 unique=1 path=%s\n", phase, selected.c_str());
    std::fflush(stdout);
    return true;
}

constexpr const char* bootstrap = R"PY(
import hashlib
import json
import os
from pathlib import Path
import sys

prefix = Path(sys.path[0]).parent
manifest = json.loads((prefix / 'LAUNCHER_PACKAGE.json').read_text())
if manifest['base_package_sha256'] != 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f':
    raise RuntimeError('Unselected project Python slice')
if sys.version_info[:3] != (3, 12, 7) or sys.prefix != '/data/python312-rk3588a-verify-c646982b346f/usr':
    raise RuntimeError('Unexpected CPython runtime')
for name, expected in manifest['files'].items():
    path = prefix / name
    if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Launcher package member changed: ' + name)
if hashlib.sha256(Path('/data/python312-rk3588a-verify-c646982b346f/usr/lib/libpython3.12.so.1.0').read_bytes()).hexdigest() != '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a':
    raise RuntimeError('Canonical CPython source library changed')
owned_python = prefix / 'lib/libpython3.12.so.1.0'
if hashlib.sha256(owned_python.read_bytes()).hexdigest() != '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25':
    raise RuntimeError('Owned CPython visibility derivative changed')
provider = manifest['python_provider']
if provider['baseline_sha256'] != '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a' or provider['owned_sha256'] != '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25' or provider['flags_1'] != 3:
    raise RuntimeError('Unexpected Python provider provenance')
import math
stdlib = Path('/data/python312-rk3588a-verify-c646982b346f/usr/lib/python3.12')
if not Path(math.__file__).resolve().is_relative_to(stdlib / 'lib-dynload') or not Path(hashlib.__file__).resolve().is_relative_to(stdlib) or math.sqrt(4.0) != 2.0:
    raise RuntimeError('Unexpected original CPython standard library')
print('ROS_PYTHON_RUNTIME imports=math,hashlib owned_provider=1 original_stdlib=1', flush=True)
if os.environ.get('LD_LIBRARY_PATH') != str(prefix / 'lib') + ':/data/python312-rk3588a-verify-c646982b346f/usr/lib':
    raise RuntimeError('Unexpected native library search path')
if os.environ.get('LD_PRELOAD') or os.environ.get('RMW_IMPLEMENTATION') != 'rmw_fastrtps_cpp':
    raise RuntimeError('Unexpected native preload or RMW provider')
if os.environ.get('AMENT_PREFIX_PATH') != str(prefix):
    raise RuntimeError('Unexpected ament resource prefix')

# Ordinary CLI plugins may request a daemon/subprocess. Such a request cannot
# inherit this admission by assumption; fail before starting another process.
def process_boundary(event, args):
    if event in ('subprocess.Popen', 'os.fork', 'os.forkpty', 'os.posix_spawn', 'os.exec', 'os.system'):
        raise RuntimeError('Token-first launcher requires one process; daemon/child execution refused')
sys.addaudithook(process_boundary)
sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL)
mode, arguments = sys.argv[1], sys.argv[2:]
print('ROS_PYTHON_ENTRY mode=' + mode + ' participant_lifecycles_max=1 daemon=REFUSED', flush=True)
try:
    if mode == 'ros2':
        sys.argv = ['ros2'] + arguments
        from ros2cli.cli import main
        raise SystemExit(main(argv=arguments))
    import runpy
    script = Path(arguments[0]).resolve(strict=True)
    relative = script.relative_to(prefix).as_posix()
    if not script.is_relative_to(prefix / 'scripts') or relative not in manifest['files']:
        raise RuntimeError('Script is not a sealed owned package member')
    sys.argv = [str(script)] + arguments[1:]
    runpy.run_path(str(script), run_name='__main__')
finally:
    # This is a shutdown attempt, not a claim that a command succeeded or that
    # an arbitrary native extension drained every worker. Finalize follows.
    if 'rclpy' in sys.modules:
        sys.modules['rclpy'].try_shutdown()
        print('ROS_PYTHON_DEFAULT_CONTEXT shutdown_returned=1', flush=True)
)PY";

// BEGIN EMBED_RETURN_CONTROL
// These helpers are also compiled unchanged by the real host CPython control.
// No exception-printer API is used: its SystemExit branch can terminate C++.
int python_error_status()
{
    PyObject* error = PyErr_GetRaisedException(); // New normalized exception; clears the indicator.
    if (!error) return 1;
    int result = 1;
    if (PyErr_GivenExceptionMatches(error, PyExc_SystemExit)) {
        PyObject* code = PyObject_GetAttrString(error, "code");
        if (code == Py_None) result = 0;
        else if (code && PyLong_Check(code)) {
            // Match CPython's integer SystemExit conversion, including overflow.
            const long value = PyLong_AsLong(code);
            result = PyErr_Occurred() ? -1 : static_cast<int>(value);
        } else if (code) {
            PyObject* text = PyObject_Str(code);
            const char* utf8 = text ? PyUnicode_AsUTF8(text) : nullptr;
            std::fprintf(stderr, "%s\n", utf8 ? utf8 : "SystemExit: noninteger code could not be formatted");
            Py_XDECREF(text);
        }
        Py_XDECREF(code);
        PyErr_Clear(); // Formatting/conversion failure must also return to C++.
    } else {
        if (PyErr_GivenExceptionMatches(error, PyExc_KeyboardInterrupt)) result = 130;
        PyObject* module = PyImport_ImportModule("traceback");
        PyObject* printer = module ? PyObject_GetAttrString(module, "print_exception") : nullptr;
        PyObject* printed = printer ? PyObject_CallFunctionObjArgs(printer, error, nullptr) : nullptr;
        if (!printed) std::fputs("ROS_PYTHON_EXCEPTION traceback_formatting_failed\n", stderr);
        Py_XDECREF(printed);
        Py_XDECREF(printer);
        Py_XDECREF(module);
        PyErr_Clear(); // Even a formatter raising SystemExit is never printed by PyErr_Print.
    }
    Py_DECREF(error);
    return result;
}

int python_evaluate_and_finalize(const char* command)
{
    PyObject* module = PyImport_AddModule("__main__"); // Borrowed; interpreter owns main globals.
    PyObject* globals = module ? PyModule_GetDict(module) : nullptr;
    PyObject* value = globals ? PyRun_StringFlags(command, Py_file_input, globals, globals, nullptr) : nullptr;
    const int execution = value ? 0 : python_error_status();
    Py_XDECREF(value);
    const int finalization = Py_FinalizeEx(); // Exactly once after successful initialization.
    std::fprintf(stdout, "ROS_PYTHON_INTERPRETER_RETURN execution_rc=%d finalize_rc=%d\n", execution, finalization);
    std::fflush(stdout);
    // CPython uses 120 for failures while flushing/finalizing, even after exit0.
    return finalization < 0 ? 120 : execution;
}
// END EMBED_RETURN_CONTROL

bool set_bytes(PyConfig& config, wchar_t** field, const std::string& value)
{
    const auto status = PyConfig_SetBytesString(&config, field, value.c_str());
    if (!PyStatus_Exception(status)) return true;
    std::fprintf(stderr, "ROS_PYTHON_CONFIG_ERROR %s\n", status.err_msg ? status.err_msg : "exit requested");
    return false;
}

bool append_path(PyConfig& config, const std::string& value)
{
    wchar_t* text = Py_DecodeLocale(value.c_str(), nullptr);
    if (!text) return false;
    const auto status = PyWideStringList_Append(&config.module_search_paths, text);
    PyMem_RawFree(text);
    return !PyStatus_Exception(status);
}
}

int FastddsDsoftbusRosBusinessMain(int argc, char** argv)
{
    namespace fs = std::filesystem;
    if (argc < 2 || (std::strcmp(argv[1], "ros2") && std::strcmp(argv[1], "--script"))) {
        std::fputs("usage: actionbag_python ros2 [args] | --script /owned/package/scripts/name.py [args]\n", stderr);
        return 64;
    }
    char executable[4096];
    const auto length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(executable) - 1) return 70;
    executable[length] = '\0';
    const auto prefix = fs::canonical(executable).parent_path();
    if (!std::strcmp(argv[1], "--script")) {
        if (argc < 3 || !fs::path(argv[2]).is_absolute()) return 64;
        const auto script = fs::canonical(argv[2]);
        const auto relative = script.lexically_relative(prefix / "scripts");
        if (relative.empty() || *relative.begin() == ".." || !fs::is_regular_file(script)) return 64;
    } else if (argc > 2 && !std::strcmp(argv[2], "daemon")) {
        std::fputs("ROS_PYTHON_REJECTED daemon_requires_separate_admission\n", stderr);
        return 64;
    }
    if (!owned_python_provider(prefix, "BEFORE_INITIALIZE")) return 70;
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.parse_argv = 0;
    config.site_import = 0;
    config.write_bytecode = 0;
    config.install_signal_handlers = 1;
    config.buffered_stdio = 0;
    config.safe_path = 1;
    config.module_search_paths_set = 1;
    bool ready = set_bytes(config, &config.home, runtime) &&
        set_bytes(config, &config.program_name, std::string(runtime) + "/bin/python3.12") &&
        append_path(config, (prefix / "python").string()) &&
        append_path(config, std::string(runtime) + "/lib/python312.zip") &&
        append_path(config, std::string(runtime) + "/lib/python3.12") &&
        append_path(config, std::string(runtime) + "/lib/python3.12/lib-dynload");
    if (ready) ready = !PyStatus_Exception(PyConfig_SetBytesArgv(&config, argc, argv));
    if (!ready) { PyConfig_Clear(&config); return 70; }
    std::fputs("ROS_PYTHON_INITIALIZE after_token_admission=1 isolated=1 site=0\n", stdout);
    std::fflush(stdout);
    const auto status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        std::fprintf(stderr, "ROS_PYTHON_INITIALIZE_ERROR %s\n", status.err_msg ? status.err_msg : "exit requested");
        PyConfig_Clear(&config);
        if (Py_IsInitialized()) (void)Py_FinalizeEx();
        return 70;
    }
    PyConfig_Clear(&config);
    // Evaluation returns an exception object instead of entering CPython's
    // command-line exception/exit machinery. Finalization precedes restoration.
    const int result = python_evaluate_and_finalize(bootstrap);
    const bool finalized = !Py_IsInitialized();
    const bool provider_valid = owned_python_provider(prefix, "AFTER_FINALIZE");
    fastdds_dsoftbus::ros2_teardown::Snapshot teardown;
    const bool observed = FastddsDsoftbusFactoryCaptureTeardown(&teardown) == 0;
    int teardown_result = -1; // Help/import-only commands need not create a Participant.
    if (observed) {
        FastddsDsoftbusTeardownCheckpoint("PYTHON_FINALIZED");
        teardown_result = FastddsDsoftbusTeardownResult();
    }
    std::fprintf(stdout, "ROS_PYTHON_FINAL finalized=%d python_rc=%d teardown_observed=%d teardown_rc=%d communication=NOT_ASSERTED\n",
        int(finalized), result, int(observed), teardown_result);
    std::fflush(stdout);
    if (!finalized || !provider_valid) return 70;
    if (observed && teardown_result != 0) return 71;
    return result;
}
