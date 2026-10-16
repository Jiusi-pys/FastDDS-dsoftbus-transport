"""Install a relocatable POSIX entry for an explicitly declared flat ELF bundle.

Usage on target: /path/to/bundle/run EXECUTABLE_BASENAME [ARG ...]
Arguments and caller cwd are preserved. Only AMENT_PREFIX_PATH and
LD_LIBRARY_PATH are replaced in the child environment; other values, including
RMW_IMPLEMENTATION, are inherited. This is not a general ROS setup script.
Invoke the physical entry by a pathname: entry-file symlinks are unsupported.
Runtime prefix names containing colon or newline are unsupported.
Deployment on a POSIX target must preserve/set the generated run file's 0755 mode.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex


def install_entry(bundle, executables, required_resources=()):
    """Create fresh run/receipt files after declared executable/resources exist."""
    bundle = Path(bundle)
    executables, required_resources = list(executables), list(required_resources)
    if not bundle.is_dir():
        raise ValueError('Existing bundle directory required')
    if not executables or len(set(executables)) != len(executables):
        raise ValueError('Nonempty unique executable list required')
    for name in executables:
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.+-]*', name) or not (bundle/'lib'/name).is_file():
            raise ValueError('Declared executable basename missing or invalid: '+name)
    if len(set(required_resources)) != len(required_resources):
        raise ValueError('Duplicate required resource')
    for relative in required_resources:
        path = PurePosixPath(relative)
        if (not relative or path.is_absolute() or str(path) != relative or
                any(part in ('.', '..') for part in path.parts) or
                any(char in relative for char in ('\\', ':', '\r', '\n')) or
                not (bundle/relative).is_file()):
            raise ValueError('Declared relative resource missing or invalid: '+relative)
    entry, receipt_path = bundle/'run', bundle/'RUNTIME_ENTRY.json'
    if entry.exists() or entry.is_symlink() or receipt_path.exists() or receipt_path.is_symlink():
        raise ValueError('Fresh entry and receipt paths required')
    choices = '|'.join(shlex.quote(name) for name in executables)
    script = '''#!/bin/sh
# Usage: /path/to/bundle/run EXECUTABLE_BASENAME [ARG ...]
# Replaces only AMENT_PREFIX_PATH/LD_LIBRARY_PATH; preserves cwd and other env.
# No sourcing, eval, general ROS setup, or entry-file symlink invocation.
set -eu
fail() { printf '%s\\n' "runtime entry: $2" >&2; exit "$1"; }
case "$0" in
  */*) ;;
  *) fail 64 'invoke the entry by its pathname' ;;
esac
[ ! -L "$0" ] || fail 64 'entry-file symlinks are unsupported'
case "$0" in
  *:*|*'
'*) fail 64 'entry pathname cannot contain colon or newline' ;;
esac
runtime_entry_resolve() (
# cd changes PWD only in this subshell. Normalize relative operands, including '-'.
case "$1" in
  '') CDPATH= cd -P / || exit 66 ;;
  /*) CDPATH= cd -P "$1" || exit 66 ;;
  *) CDPATH= cd -P "./$1" || exit 66 ;;
esac
# A final non-newline sentinel preserves every physical PWD byte in $(...).
printf '%s.' "$PWD"
)
runtime_entry_run() {
# Positional parameters avoid changing other inherited/exported variable names.
case "$1" in
  /*.) ;;
  *) fail 66 'cannot resolve bundle directory' ;;
esac
case "$1" in
  *:*|*'
'*) fail 64 'bundle prefix cannot contain colon or newline' ;;
esac
[ "$#" -ge 2 ] || fail 64 'usage: run EXECUTABLE_BASENAME [ARG ...]'
case "$2" in
  @CHOICES@) ;;
  *) fail 64 'executable basename is not declared in this bundle' ;;
esac
[ -f "${1%.}/lib/$2" ] && [ -x "${1%.}/lib/$2" ] || fail 66 'declared executable missing or not executable'
@RESOURCES@
AMENT_PREFIX_PATH=${1%.}
LD_LIBRARY_PATH=${1%.}/lib
export AMENT_PREFIX_PATH LD_LIBRARY_PATH
case "$2" in
@EXEC_BRANCHES@
esac
}
runtime_entry_run "$(runtime_entry_resolve "${0%/*}")" "$@"
'''
    checks = '\n'.join(
        '[ -f "${1%.}"/'+shlex.quote(p)+' ] && [ -r "${1%.}"/'+shlex.quote(p)+
        ' ] || fail 66 '+shlex.quote('required resource missing or unreadable: '+p)
        for p in required_resources)
    branches = '\n'.join('  '+shlex.quote(name)+') shift 2; exec "$LD_LIBRARY_PATH"/'+
                         shlex.quote(name)+' "$@" ;;' for name in executables)
    replacements = {'@CHOICES@': choices, '@RESOURCES@': checks, '@EXEC_BRANCHES@': branches}
    # Substitute only matches in the original template, never markers in inserted filenames.
    script = re.sub(r'@(?:CHOICES|RESOURCES|EXEC_BRANCHES)@',
                    lambda match: replacements[match.group(0)], script)
    # write_bytes is intentional: Windows text newline translation must not alter sh.
    data = script.encode('utf-8')
    with entry.open('xb') as output:
        output.write(data)
    entry.chmod(0o755)
    receipt = dict(entry='run', sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                   requested_posix_mode='0755', executables=executables,
                   required_resources=required_resources,
                   usage='run EXECUTABLE_BASENAME [ARG ...]', argv_policy='forward unchanged after executable name',
                   cwd_policy='preserve caller cwd',
                   environment_policy={'AMENT_PREFIX_PATH': 'replace with physical bundle prefix',
                                       'LD_LIBRARY_PATH': 'replace with physical bundle prefix/lib',
                                       'other_variables': 'inherit unchanged'},
                   symlink_entry='unsupported; rejected',
                   scope='POSIX_BUNDLE_ENTRY_NOT_ROS_SETUP_OR_ABI_ADMISSION')
    with receipt_path.open('x', encoding='utf-8', newline='\n') as output:
        output.write(json.dumps(receipt, indent=2)+'\n')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--executable', action='append', required=True,
                        help='Declared basename in bundle/lib; repeat for multiple executables')
    parser.add_argument('--require-resource', action='append', default=[],
                        help='Required file relative to bundle; repeat as needed')
    args = parser.parse_args()
    install_entry(args.bundle, args.executable, args.require_resource)


if __name__ == '__main__':
    main()
