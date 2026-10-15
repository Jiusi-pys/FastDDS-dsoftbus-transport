"""Synthetic CMake dependency-contract controls, not binary qualification.

Run independently with --cmake ABSOLUTE_EXE --work NEW_EMPTY_DIRECTORY.
All fake headers/import files stay below --work; real prefixes are untouched.
Add --project-entry for separate actual-project install-path controls on Windows
with Visual Studio 17 2022. This configures only; it never builds or installs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


def matches_configure_result(exit_code, stdout, stderr, expected_error):
    if expected_error is None:
        return exit_code == 0
    # CMake wraps long diagnostics according to its output width. Preserve the
    # complete expected message and nonzero exit requirement; normalize only
    # whitespace, independently in each stream, never words or return codes.
    expected = " ".join(expected_error.split())
    return exit_code != 0 and bool(expected) and any(
        expected in " ".join(stream.split()) for stream in (stdout, stderr))


def diagnostic_matcher_controls():
    expected = "Historical Host Fast CDR runtime DLL content differs from compile dependency"
    # Exact diagnostic body wrapping from the preserved FIX1 CMake failure.
    wrapped = "  Historical Host Fast CDR runtime DLL content differs from compile\n  dependency\n"
    cases = [("wrapped-specific-diagnostic", 1, wrapped, True),
             ("wrong-diagnostic", 1, "Historical Host Fast CDR runtime DLL file unavailable\n", False),
             ("zero-exit-with-diagnostic", 0, wrapped, False),
             ("truncated-diagnostic", 1, "Historical Host Fast CDR runtime DLL content differs from compile\n", False)]
    results = []
    for name, code, stderr, wanted in cases:
        actual = matches_configure_result(code, "", stderr, expected)
        results.append({"case": name, "exit": code, "stderr": stderr,
                        "expected_error": expected, "expected_match": wanted,
                        "actual_match": actual, "pass": actual == wanted,
                        "scope": "PURE_DIAGNOSTIC_MATCHER_MODEL"})
    return results


def run_case(cmake, work, module, name, expected_error):
    case = work / name
    case.mkdir()
    dds, cdr = case / "dds", case / "cdr"
    dds_header = "#define FASTRTPS_VERSION_MAJOR 2\n#define FASTRTPS_VERSION_MINOR 14\n#define FASTRTPS_VERSION_MICRO 7\n#define HAVE_SECURITY 1\n#define FASTDDS_STATISTICS\n"
    cdr_header = "#define FASTCDR_VERSION_MAJOR 2\n#define FASTCDR_VERSION_MINOR 2\n#define FASTCDR_VERSION_MICRO 8\n"
    if name == "wrong-dds-version":
        dds_header = dds_header.replace("MICRO 7", "MICRO 5")
    elif name == "wrong-cdr-version":
        cdr_header = cdr_header.replace("MICRO 8", "MICRO 7")
    elif name == "security-off":
        dds_header = dds_header.replace("HAVE_SECURITY 1", "HAVE_SECURITY 0")
    elif name == "statistics-off":
        dds_header = dds_header.replace("#define FASTDDS_STATISTICS", "/* #undef FASTDDS_STATISTICS */")
    elif name == "commented-statistics":
        dds_header = dds_header.replace("#define FASTDDS_STATISTICS", "// #define FASTDDS_STATISTICS")
    for prefix, stem, header, text in ((dds, "fastrtps-2.14", "fastrtps", dds_header),
                                     (cdr, "fastcdr-2.2", "fastcdr", cdr_header)):
        for subdir in (f"include/{header}", "lib", "bin"):
            (prefix / subdir).mkdir(parents=True, exist_ok=True)
        (prefix / "include" / header / "config.h").write_text(text, encoding="utf-8")
        (prefix / "lib" / f"{stem}.lib").write_text("SYNTHETIC_IMPORT_NOT_LINKABLE", encoding="ascii")
        (prefix / "bin" / f"{stem}.dll").write_text("SYNTHETIC_DLL_NOT_LOADABLE", encoding="ascii")
    (dds / "share/fastrtps/cmake").mkdir(parents=True)
    (cdr / "lib/cmake/fastcdr").mkdir(parents=True)
    other = case / "other"
    other.mkdir()
    (other / "fake.lib").write_text("SYNTHETIC", encoding="ascii")
    (other / "fake.dll").write_text("SYNTHETIC", encoding="ascii")
    runtime = case / "runtime.dll"
    runtime.write_bytes((cdr / "bin/fastcdr-2.2.dll").read_bytes())
    runtime_argument = runtime.as_posix()
    if name == "runtime-missing":
        runtime_argument = ""
    elif name == "runtime-relative":
        runtime_argument = "runtime.dll"
    elif name == "runtime-nonexistent":
        runtime_argument = (case / "nonexistent.dll").as_posix()
    elif name == "runtime-content-mismatch":
        runtime.write_text("DIFFERENT_SYNTHETIC_DLL_NOT_LOADABLE", encoding="ascii")
    cmake_text = f'''cmake_minimum_required(VERSION 3.22)
project(ProfileContractFixture NONE)
set(fastrtps_DIR "{dds.as_posix()}/share/fastrtps/cmake")
set(fastcdr_DIR "{cdr.as_posix()}/lib/cmake/fastcdr")
add_library(fastrtps SHARED IMPORTED)
add_library(fastcdr SHARED IMPORTED)
set_target_properties(fastrtps PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "{dds.as_posix()}/include" IMPORTED_IMPLIB_RELEASE "{dds.as_posix()}/lib/fastrtps-2.14.lib" IMPORTED_LOCATION_RELEASE "{dds.as_posix()}/bin/fastrtps-2.14.dll")
set_target_properties(fastcdr PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "{cdr.as_posix()}/include" IMPORTED_IMPLIB_RELEASE "{cdr.as_posix()}/lib/fastcdr-2.2.lib" IMPORTED_LOCATION_RELEASE "{cdr.as_posix()}/bin/fastcdr-2.2.dll")
include("{module.as_posix()}")
'''
    mutations = {
        "mixed-package-root": f'set(fastrtps_DIR "{other.as_posix()}")',
        "mixed-header-root": f'set_property(TARGET fastrtps PROPERTY INTERFACE_INCLUDE_DIRECTORIES "{other.as_posix()}")',
        "mixed-import-root": f'set_property(TARGET fastrtps PROPERTY IMPORTED_IMPLIB_RELEASE "{other.as_posix()}/fake.lib")',
        "mixed-dll-root": f'set_property(TARGET fastrtps PROPERTY IMPORTED_LOCATION_RELEASE "{other.as_posix()}/fake.dll")',
        "multiple-header-roots": f'set_property(TARGET fastrtps PROPERTY INTERFACE_INCLUDE_DIRECTORIES "{dds.as_posix()}/include;{other.as_posix()}")',
        "mixed-cdr-root": f'set_property(TARGET fastcdr PROPERTY INTERFACE_INCLUDE_DIRECTORIES "{other.as_posix()}")',
    }
    cmake_text += mutations.get(name, "") + "\n"
    cmake_text += f'fdds_host_historical_dependencies("{dds.as_posix()}" "{cdr.as_posix()}")\n'
    cmake_text += f'fdds_host_historical_runtime_cdr("{cdr.as_posix()}/bin/fastcdr-2.2.dll" "{runtime_argument}")\n'
    if name == "positive":
        cmake_text += f'''if(NOT _fdds_cdr_path STREQUAL "{runtime.as_posix()}" OR
    NOT _fdds_cdr_compile_path STREQUAL "{cdr.as_posix()}/bin/fastcdr-2.2.dll")
    message(FATAL_ERROR "Runtime and compile paths were not kept distinct")
endif()
file(SHA256 "{runtime.as_posix()}" expected_sha)
if(NOT _fdds_profile_fastcdr_runtime_sha STREQUAL expected_sha)
    message(FATAL_ERROR "Runtime hash metadata mismatch")
endif()
'''
    (case / "CMakeLists.txt").write_text(cmake_text, encoding="utf-8")
    command = [str(cmake), "-S", str(case), "-B", str(case / "build")]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (case / "stdout.txt").write_text(result.stdout, encoding="utf-8")
    (case / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    passed = matches_configure_result(result.returncode, result.stdout, result.stderr, expected_error)
    return {"case": name, "argv": command, "exit": result.returncode,
            "expected_error": expected_error, "pass": passed, "scope": "SYNTHETIC_CONFIGURE_CONTRACT"}


def project_fixture(case, default_profile):
    """Non-linkable packages for the real project configure entry point."""
    inputs = []

    def write(path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        inputs.append(path)

    dds, cdr = case / "dds", case / "cdr"
    dds_version = "2.14.5" if default_profile else "2.14.7"
    dds_header = ("#define FASTRTPS_VERSION_MAJOR 2\n#define FASTRTPS_VERSION_MINOR 14\n"
                  f"#define FASTRTPS_VERSION_MICRO {5 if default_profile else 7}\n"
                  f"#define HAVE_SECURITY {0 if default_profile else 1}\n")
    if not default_profile:
        dds_header += "#define FASTDDS_STATISTICS\n"
    cdr_header = "#define FASTCDR_VERSION_MAJOR 2\n#define FASTCDR_VERSION_MINOR 2\n#define FASTCDR_VERSION_MICRO 8\n"
    for prefix, package, version, subdir, stem, header in (
            (dds, "fastrtps", dds_version, "share/fastrtps/cmake", "fastrtps-2.14", dds_header),
            (cdr, "fastcdr", "2.2.8", "lib/cmake/fastcdr", "fastcdr-2.2", cdr_header)):
        write(prefix / "include" / package / "config.h", header)
        write(prefix / "lib" / f"{stem}.lib", "SYNTHETIC_IMPORT_NOT_LINKABLE")
        write(prefix / "bin" / f"{stem}.dll", "SYNTHETIC_DLL_NOT_LOADABLE")
        write(prefix / subdir / f"{package}-config.cmake", f'''
get_filename_component(_fixture_prefix "${{CMAKE_CURRENT_LIST_DIR}}/../../.." ABSOLUTE)
add_library({package} SHARED IMPORTED)
set_target_properties({package} PROPERTIES
 INTERFACE_INCLUDE_DIRECTORIES "${{_fixture_prefix}}/include"
 IMPORTED_IMPLIB_RELEASE "${{_fixture_prefix}}/lib/{stem}.lib"
 IMPORTED_LOCATION_RELEASE "${{_fixture_prefix}}/bin/{stem}.dll")
''')
        write(prefix / subdir / f"{package}-config-version.cmake", f'''
set(PACKAGE_VERSION "{version}")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
 set(PACKAGE_VERSION_EXACT TRUE)
endif()
''')
    write(case / "runtime.dll", "SYNTHETIC_DLL_NOT_LOADABLE")
    write(case / "softbus/interfaces/kits/transport/socket.h", "// SYNTHETIC_CONFIGURE_ONLY\n")
    return dds, cdr, inputs


def create_fixture_junction(case, target):
    """Fixed program text; paths are data, never interpolated shell syntax."""
    alias = case / "junction"
    powershell = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    environment = os.environ.copy()
    environment["FDDS_TEST_JUNCTION_LINK"] = str(alias)
    environment["FDDS_TEST_JUNCTION_TARGET"] = str(target)
    command = [str(powershell), "-NoProfile", "-NonInteractive", "-Command",
               "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
               "-Path $env:FDDS_TEST_JUNCTION_LINK -Target $env:FDDS_TEST_JUNCTION_TARGET | Out-Null"]
    (case / "JUNCTION_INTENT.json").write_text(json.dumps(
        {"argv": command, "link": str(alias), "target": str(target)}, indent=2), encoding="utf-8")
    result = subprocess.run(command, env=environment, capture_output=True, text=True, timeout=30)
    (case / "junction.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (case / "junction.stderr.txt").write_text(result.stderr, encoding="utf-8")
    samefile = result.returncode == 0 and alias.exists() and os.path.samefile(alias, target)
    reparse = samefile and bool(alias.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)
    evidence = {"argv": command, "exit": result.returncode, "link": str(alias),
                "target": str(target), "samefile": samefile, "reparse_point": reparse}
    (case / "JUNCTION.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    if not samefile or not reparse:
        raise OSError("Junction fixture unavailable; this negative cannot be counted as PASS")
    return alias


def project_entry_controls(cmake, work):
    source = Path(__file__).resolve().parents[2] / "integrations/ros2"
    alias_error = "Historical Host profile refuses symlink or junction prefix ancestry:"
    containment_error = "Historical Host install must not modify a dependency prefix"
    cases = [("direct-dependency", containment_error), ("uppercase-dependency", containment_error),
             ("junction-dds", alias_error), ("junction-cdr", alias_error),
             ("ancestor-existing", alias_error), ("ancestor-missing", alias_error),
             ("reverse-dds-alias", alias_error), ("reverse-cdr-alias", alias_error),
             ("ordinary-existing", None), ("ordinary-new", None), ("component-boundary", None),
             ("default-junction", None),
             ("existing-file", "Historical Host profile prefix ancestry must contain directories:")]
    results = []
    for name, expected in cases:
        case = work / name
        case.mkdir()
        result = {"case": name, "expected_error": expected, "pass": False,
                  "status": "BLOCKED", "scope": "SYNTHETIC_ACTUAL_PROJECT_CONFIGURE"}
        try:
            if os.name != "nt":
                raise OSError("Windows project-entry controls require Windows")
            default_profile = name == "default-junction"
            dds, cdr, inputs = project_fixture(case, default_profile)
            declared_dds, declared_cdr = dds, cdr
            install = case / "ordinary-install"
            if name in ("direct-dependency", "uppercase-dependency"):
                install = dds if name == "direct-dependency" else Path(str(dds).upper())
            elif name.startswith("junction-") or name.startswith("ancestor-") or name.startswith("reverse-") or default_profile:
                target = cdr if name in ("junction-cdr", "reverse-cdr-alias") else dds
                alias = create_fixture_junction(case, target)
                if not default_profile:
                    expected = f"{alias_error} {alias.as_posix()}"
                install = alias
                if name == "ancestor-existing":
                    (target / "existing-child").mkdir()
                    install = alias / "existing-child"
                elif name == "ancestor-missing":
                    install = alias / "missing" / "child"
                elif name == "reverse-dds-alias":
                    declared_dds, install = alias, dds
                elif name == "reverse-cdr-alias":
                    declared_cdr, install = alias, cdr
            elif name == "ordinary-existing":
                install.mkdir()
            elif name == "ordinary-new":
                install = install / "missing" / "child"
            elif name == "component-boundary":
                install = case / "dds-independent"
            elif name == "existing-file":
                install.write_text("NOT_A_DIRECTORY", encoding="ascii")
                expected = f"{expected} {install.as_posix()}"
            result["expected_error"] = expected
            before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
            command = [str(cmake), "-S", str(source), "-B", str(case / "build"),
                       "-G", "Visual Studio 17 2022", "-A", "x64",
                       "-DCMAKE_CONFIGURATION_TYPES=Release", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL",
                       f"-DCMAKE_INSTALL_PREFIX={install}", "-DBUILD_TESTING=OFF",
                       f"-Dfastrtps_DIR={declared_dds}/share/fastrtps/cmake",
                       f"-Dfastcdr_DIR={declared_cdr}/lib/cmake/fastcdr",
                       f"-DDSOFTBUS_SOURCE_DIR={case}/softbus",
                       "-DFASTDDS_DSOFTBUS_HOST_PROFILE=" + ("default-2145" if default_profile else "historical-2147-security-statistics")]
            if not default_profile:
                command += [f"-DFASTDDS_DSOFTBUS_HISTORICAL_FASTDDS_PREFIX={declared_dds}",
                            f"-DFASTDDS_DSOFTBUS_HISTORICAL_FASTCDR_PREFIX={declared_cdr}",
                            f"-DFASTDDS_DSOFTBUS_HISTORICAL_FASTCDR_RUNTIME_DLL={case}/runtime.dll"]
            result.update({"argv": command, "install_prefix": str(install), "input_hashes_before": before})
            configured = subprocess.run(command, capture_output=True, text=True, timeout=120)
            (case / "stdout.txt").write_text(configured.stdout, encoding="utf-8")
            (case / "stderr.txt").write_text(configured.stderr, encoding="utf-8")
            after = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
            passed = before == after and matches_configure_result(
                configured.returncode, configured.stdout, configured.stderr, expected)
            result.update({"exit": configured.returncode, "input_hashes_after": after,
                           "inputs_unchanged": before == after, "pass": passed,
                           "status": "PASS" if passed else "FAIL"})
        except subprocess.TimeoutExpired as error:
            for stream_name, output in (("stdout", error.stdout), ("stderr", error.stderr)):
                if isinstance(output, bytes):
                    output = output.decode("utf-8", errors="replace")
                (case / f"timeout.{stream_name}.txt").write_text(output or "", encoding="utf-8")
            result["reason"] = str(error)
        except OSError as error:
            result["reason"] = str(error)
        results.append(result)
        (work / "PROJECT_ENTRY_RESULT.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        print(json.dumps(result))
    return 0 if all(result["pass"] for result in results) else (
        2 if any(result["status"] == "BLOCKED" for result in results) else 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--project-entry", action="store_true",
                        help="Run separate actual-project install-prefix controls; no build/install")
    args = parser.parse_args()
    if not args.cmake.is_absolute() or not args.cmake.is_file():
        parser.error("--cmake must name an existing absolute executable")
    work = args.work.resolve()
    if any(character in str(work) for character in (';', '"', '$', '\n', '\r')):
        parser.error("--work contains unsupported CMake fixture path characters")
    work.mkdir(parents=True, exist_ok=False)
    if args.project_entry:
        return project_entry_controls(args.cmake, work)
    module = Path(__file__).resolve().parents[2] / "integrations/ros2/HostProfile.cmake"
    cases = [("positive", None), ("wrong-dds-version", "requires Fast DDS 2.14.7 headers"),
             ("wrong-cdr-version", "requires Fast CDR 2.2.8 headers"),
             ("security-off", "requires SECURITY ON and STATISTICS ON headers"),
             ("statistics-off", "requires SECURITY ON and STATISTICS ON headers"),
             ("commented-statistics", "requires SECURITY ON and STATISTICS ON headers"),
             ("mixed-package-root", "mixed dependency root"),
             ("mixed-header-root", "mixed dependency root"),
             ("mixed-import-root", "mixed dependency root"),
             ("mixed-dll-root", "mixed dependency root"),
             ("multiple-header-roots", "requires one declared fastrtps include root"),
             ("mixed-cdr-root", "mixed dependency root"),
             ("runtime-missing", "requires explicit existing absolute DLL files"),
             ("runtime-relative", "requires explicit existing absolute DLL files"),
             ("runtime-nonexistent", "requires explicit existing absolute DLL files"),
             ("runtime-content-mismatch", "Historical Host Fast CDR runtime DLL content differs from compile dependency")]
    matcher_results = diagnostic_matcher_controls()
    (work / "MATCHER_RESULT.json").write_text(json.dumps(matcher_results, indent=2), encoding="utf-8")
    results = [run_case(args.cmake, work, module, name, error) for name, error in cases]
    (work / "RESULT.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    for result in results:
        print(json.dumps(result))
    for result in matcher_results:
        print(json.dumps(result))
    return 0 if all(result["pass"] for result in results + matcher_results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
