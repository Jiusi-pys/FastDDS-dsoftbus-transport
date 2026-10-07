#!/usr/bin/env python
"""Materialized source-binding cases for the installed security preflight.

The unit suite covers these shapes with synthetic fixtures. These cases run the
real installed checker as a CLI against real files: a real bound dependency
prefix produced by an actual build and install, a real installed project
manifest, and real mutations of those artifacts.

Runtime directories are supplied, never copied: each case prepends the fastrtps
prefix it is exercising so the checker resolves the intended binary, and the
shared directories cover OpenSSL, Fast CDR, the platform runtime and the CRT.

Run from the repository root, after the bridge has been installed:

    python build/security-on-ros-closure-20261007/run_preflight_binding_cases.py ...

The checker is read-only. Nothing outside the isolated build tree is written.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

def _repo_root(start):
    """Walk up to the repository root so the driver works from any location."""
    for candidate in [start] + list(start.parents):
        if (candidate / 'AGENTS.md').is_file() and (candidate / 'AGENTS.md').parent == candidate:
            return candidate
    raise SystemExit('repository root not found above ' + str(start))


ROOT = _repo_root(Path(__file__).resolve().parent)
TOOL = ROOT / 'scripts' / 'install_preflight.py'


def run_case(name, prefix, config, dependency, shared_runtime, readobj, fastdds_include,
             work, expect_codes, expect_exit):
    runtime = [dependency / 'bin', dependency / 'lib'] + list(shared_runtime)
    command = [sys.executable, '-B', str(TOOL), '--prefix', str(prefix), '--config', str(config),
               '--readobj', str(readobj)]
    for item in runtime:
        command += ['--runtime-dir', str(item)]
    if fastdds_include is not None:
        command += ['--fastdds-include', str(fastdds_include)]
    proc = subprocess.run(command, capture_output=True, text=True)
    try:
        report = json.loads(proc.stdout)
        codes = [c['code'] for c in report.get('checks', [])]
        statuses = report.get('statuses', [])
    except json.JSONDecodeError:
        report, codes, statuses = None, [], []
    missing = [code for code in expect_codes if code not in codes]
    passed = proc.returncode == expect_exit and not missing
    (work / (name + '.json')).write_text(json.dumps(
        dict(name=name, exit_code=proc.returncode, expected_exit=expect_exit,
             expected_codes=expect_codes, missing_codes=missing,
             codes=codes, statuses=statuses, passed=passed,
             runtime_dirs=[str(x) for x in runtime],
             stderr=proc.stderr[-1000:]), indent=2) + '\n', encoding='utf-8')
    print(('PASS ' if passed else 'FAIL ') + name +
          ' exit=%d expected=%d missing=%s' % (proc.returncode, expect_exit, missing))
    return passed


def clone(source, target, drop_binding=False):
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target, symlinks=True)
    if drop_binding:
        shutil.rmtree(target / 'share/fddst-dependency-binding', ignore_errors=True)
    return target


def rewrite_config(source_config, target_config, old_prefix, new_prefix):
    """Re-point every path under old_prefix at new_prefix.

    The configuration is JSON, so backslashes are escaped in the raw text and a
    plain text substitution would silently match nothing. Substitute on the
    parsed values instead.
    """
    data = json.loads(Path(source_config).read_text(encoding='utf-8'))
    old_text = str(Path(old_prefix).resolve())
    new_text = str(Path(new_prefix).resolve())

    def substitute(value):
        if isinstance(value, str):
            return value.replace(old_text, new_text)
        if isinstance(value, dict):
            return {key: substitute(item) for key, item in value.items()}
        if isinstance(value, list):
            return [substitute(item) for item in value]
        return value

    Path(target_config).write_text(json.dumps(substitute(data), indent=2) + '\n',
                                   encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--installed-prefix', type=Path, required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--dependency-prefix', type=Path, required=True)
    parser.add_argument('--off-dependency-prefix', type=Path, required=True)
    parser.add_argument('--shared-runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--readobj', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()

    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    dep = args.dependency_prefix.resolve()
    shared = args.shared_runtime_dir
    include = dep / 'include'
    results = []

    # 1. The real bound prefix exactly as installed.
    results.append(run_case('01_bound_install_valid', args.installed_prefix, args.config, dep,
                            shared, args.readobj, include, work,
                            ['SECURITY_SOURCE_BINDING_VERIFIED', 'SECURITY_COMPILED_IN'], 0))

    # 2. Legitimate relocation: identical content at a new root stays valid.
    moved_dep = clone(dep, work / 'moved-dependency')
    moved_pkg = clone(args.installed_prefix.resolve(), work / 'moved-installation')
    moved_config = work / 'moved-config.json'
    rewrite_config(args.config, moved_config, dep, moved_dep)
    rewrite_config(moved_config, moved_config, args.installed_prefix, moved_pkg)
    results.append(run_case('02_relocated_prefix_still_valid', moved_pkg, moved_config, moved_dep,
                            shared, args.readobj, moved_dep / 'include', work,
                            ['SECURITY_SOURCE_BINDING_VERIFIED'], 0))

    # 3. A foreign include tree with byte-identical content is still not the
    #    declared input root.
    foreign = clone(include, work / 'foreign-include-tree')
    results.append(run_case('03_foreign_include_identical_refused', args.installed_prefix, args.config,
                            dep, shared, args.readobj, foreign, work,
                            ['SECURITY_SOURCE_BINDING_INCLUDE_MISMATCH'], 2))

    # 4. A modified installed configuration header inside the declared root.
    tampered = clone(dep, work / 'tampered-dependency')
    header = tampered / 'include/fastrtps/config.h'
    header.write_bytes(header.read_bytes().replace(b'#define HAVE_SECURITY 1',
                                                   b'#define HAVE_SECURITY 0'))
    tampered_config = work / 'tampered-config.json'
    rewrite_config(args.config, tampered_config, dep, tampered)
    results.append(run_case('04_modified_config_header_refused', args.installed_prefix, tampered_config,
                            tampered, shared, args.readobj, tampered / 'include', work,
                            ['SECURITY_SOURCE_BINDING_HEADER_MISMATCH'], 2))

    # 5. Same version and basename, but the actual SECURITY=OFF library.
    off = args.off_dependency_prefix.resolve()
    off_library = next(iter((off / 'bin').glob('fastrtps-*.dll')), None)
    if off_library is not None:
        results.append(run_case('05_off_library_same_version_refused', args.installed_prefix, args.config,
                                off, shared, args.readobj, include, work,
                                # The OFF prefix carries no binding record, so the refusal is
                                # reported through the binding and dependency selectors.
                                ['SECURITY_SOURCE_BINDING_STALE',
                                 'DEPENDENCY_VERSION_OR_ABI_MISMATCH'], 2))
    else:
        print('SKIP 05: no same-version OFF fastrtps binary supplied')
        results.append(None)

    # 6. A stale binding record must be refused.
    stale = clone(dep, work / 'stale-binding')
    record = stale / 'share/fddst-dependency-binding/fastrtps.json'
    data = json.loads(record.read_text(encoding='utf-8'))
    data['version'] = '0.0.0'
    record.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    stale_config = work / 'stale-config.json'
    rewrite_config(args.config, stale_config, dep, stale)
    results.append(run_case('06_stale_binding_record_refused', args.installed_prefix, stale_config,
                            stale, shared, args.readobj, stale / 'include', work,
                            ['SECURITY_SOURCE_BINDING_STALE'], 2))

    # 7. No binding record at all.
    unbound = clone(dep, work / 'unbound-dependency', drop_binding=True)
    unbound_config = work / 'unbound-config.json'
    rewrite_config(args.config, unbound_config, dep, unbound)
    results.append(run_case('07_deleted_binding_record_refused', args.installed_prefix, unbound_config,
                            unbound, shared, args.readobj, unbound / 'include', work,
                            ['SECURITY_SOURCE_BINDING_STALE'], 2))

    # 8. A manifest whose dependency declares no binding at all: the selected
    #    dependency prefix is valid, but nothing ties the include tree to it.
    naked_pkg = clone(args.installed_prefix.resolve(), work / 'unbound-installation')
    manifest_path = naked_pkg / 'share/FastDDSDSoftBusTransportRos2Host/install-manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    for entry in manifest['dependencies']:
        entry.pop('source_binding', None)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    naked_config = work / 'naked-config.json'
    rewrite_config(args.config, naked_config, args.installed_prefix, naked_pkg)
    results.append(run_case('08_manifest_without_binding_refused', naked_pkg, naked_config,
                            dep, shared, args.readobj, include, work,
                            ['SECURITY_SOURCE_BINDING_MISSING'], 2))

    ran = [x for x in results if x is not None]
    summary = dict(cases_total=len(ran), cases_passed=sum(1 for x in ran if x),
                   skipped=len(results) - len(ran))
    (work / 'PREFLIGHT_BINDING_CASES.json').write_text(
        json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary))
    return 0 if summary['cases_passed'] == summary['cases_total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
