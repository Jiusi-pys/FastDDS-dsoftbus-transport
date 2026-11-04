"""Build the exact local Fast DDS 2.14.5 Git tree, without source patches.

Offline SDK23 build only: no device, permission, provisioning or legacy hook
builder. Each output is fresh; failures and actual command results are retained.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile

COMMIT = 'ebcb5c05e9da9e262041e8616bbaff61ab40a296'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p)
            for p in sorted(root.rglob('*')) if p.is_file()}


def check_members(members):
    for item in members:
        path = PurePosixPath(item.name)
        if path.is_absolute() or '..' in path.parts or not (item.isfile() or item.isdir()):
            raise ValueError('Archive must contain only contained regular stock files/directories')


def verify_source(root, expected):
    actual = inventory(root)
    if actual != expected:
        raise ValueError('Stock source bytes or membership changed')
    return actual


def build(args):
    root, output = args.inputs_root.resolve(strict=True), args.output.absolute()
    stage = root / 'build/r39-ros2-complete-matrix'
    if not output.is_relative_to(stage) or output.parent != stage or output.exists():
        raise ValueError('Require a fresh immediate R39 output directory')
    if output.resolve() != output:
        raise ValueError('Output aliases are not allowed')
    output.mkdir()
    receipts = {}

    def write(name, value):
        (output / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def run(label, argv, binary_stdout=None):
        stdout, stderr = output / (label + '.stdout'), output / (label + '.stderr')
        with (binary_stdout or stdout).open('xb') as out, stderr.open('xb') as err:
            child = subprocess.run([str(x) for x in argv], stdout=out, stderr=err, cwd=root)
        row = {'argv': [str(x) for x in argv], 'cwd': str(root), 'exit': child.returncode,
               'stdout': str(binary_stdout or stdout), 'stderr': str(stderr)}
        row.update(stdout_sha256=digest(binary_stdout or stdout), stderr_sha256=digest(stderr))
        receipts[label] = row
        write(label + '.json', row)
        if child.returncode:
            raise RuntimeError(label + ' failed: ' + str(child.returncode))

    try:
        archive, source = output / 'stock.tar', output / 'source'
        run('archive', ['git', '-C', root / 'Fast-DDS', 'archive', '--format=tar', COMMIT], archive)
        source.mkdir()
        with tarfile.open(archive) as stream:
            members = stream.getmembers()
            check_members(members)
            expected = {m.name: hashlib.sha256(stream.extractfile(m).read()).hexdigest()
                        for m in members if m.isfile()}
            stream.extractall(source, members=members, filter='data')
        verify_source(source, expected)
        write('SOURCE_INVENTORY.json', expected)
        base = json.loads((root / 'build/g2a1/target214-fastdds-configure.json').read_text(encoding='utf-8-sig'))['argv']
        install, binary = output / 'install', output / 'build'
        # Reuse the measured SDK/dependency selections, not the patched source
        # or any legacy patch/rebuild operation.
        argv = [str(args.cmake)] + base[1:]
        argv[argv.index('-S') + 1] = str(source)
        argv[argv.index('-B') + 1] = str(binary)
        argv = [('-DCMAKE_INSTALL_PREFIX=' + str(install)) if x.startswith('-DCMAKE_INSTALL_PREFIX=') else
                ('-DTHIRDPARTY_BOOST_INCLUDE_DIR=' + str(source / 'thirdparty/boost/include')) if x.startswith('-DTHIRDPARTY_BOOST_INCLUDE_DIR=') else
                '-DCOMPILE_TOOLS=OFF' if x == '-DCOMPILE_TOOLS=ON' else x for x in argv]
        platform_options = Path(__file__).absolute().parents[1] / 'target/RosTargetBuildOptions.cmake'
        argv += ['-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=' + str(platform_options)]
        run('configure', argv)
        run('build', [args.cmake, '--build', binary, '--parallel', str(args.parallel), '--verbose',
                      '--', '-d', 'keeprsp'])
        run('install', [args.cmake, '--install', binary])
        ninja = next(x.split('=', 1)[1] for x in argv if x.startswith('-DCMAKE_MAKE_PROGRAM='))
        run('dependencies', [ninja, '-C', binary, '-t', 'deps'])
        verify_source(source, expected)
        elf = install / 'lib/libfastrtps.so.2.14.5'
        if not elf.is_file():
            elf = install / 'lib/libfastrtps.so.2.14'
        if not elf.is_file():
            raise ValueError('Built installed Fast DDS ELF missing')
        toolchain = Path(next(x.split('=', 1)[1] for x in argv if x.startswith('-DCMAKE_TOOLCHAIN_FILE=')))
        run('readelf', [toolchain.parents[2] / 'llvm/bin/llvm-readelf.exe', '-h', '-d', '-n', elf])
        elf_metadata = (output / 'readelf.stdout').read_text(encoding='utf-8')
        if 'AArch64' not in elf_metadata or not re.search(r'SONAME.*\[libfastrtps\.so\.2\.14\]', elf_metadata):
            raise ValueError('AArch64 ELF with exact libfastrtps.so.2.14 SONAME required')
        commands = json.loads((binary / 'compile_commands.json').read_text(encoding='utf-8'))
        for command in commands:
            member = Path(command['file'])
            if not (member.is_relative_to(source) or member.is_relative_to(binary)):
                raise ValueError('Fast DDS translation unit outside new stock source/build')
            text = command.get('command', ' '.join(command.get('arguments', [])))
            if re.search(r'(?:^|\s)-include(?:\s|=)', text) or 'FASTDDS_DSOFTBUS_' in text:
                raise ValueError('Project hook macro or forced upper include in stock compile command')
        raw = elf.read_bytes()
        forbidden = (b'FDDST_ENDPOINT_DATASHARING_REJECTED', b'FASTDDS_DSOFTBUS_REQUIRED_CONFIG',
                     b'FastddsDsoftbusFactoryCaptureTeardown', b'OriginalDeleteParticipant')
        if any(marker in raw for marker in forbidden):
            raise ValueError('Legacy hook marker found in fresh stock ELF')
        result = dict(status='PASS_STOCK_SDK23_BUILD_ONLY', commit=COMMIT, source_unmodified=True,
                      source_members=len(expected), source_inventory_sha256=digest(output / 'SOURCE_INVENTORY.json'),
                      elf=str(elf), elf_sha256=digest(elf), archive_sha256=digest(archive),
                      platform_build_options=dict(path=str(platform_options), sha256=digest(platform_options)),
                      compile_translation_units=len(commands), exact_soname='libfastrtps.so.2.14',
                      compile_commands=str(binary / 'compile_commands.json'), receipts=receipts,
                      device_tested=False, host_execution=False, legacy_hook_markers_present=False)
    except BaseException as error:
        result = dict(status='FAIL', error=repr(error), receipts=receipts, device_tested=False)
        write('RESULT.json', result)
        raise
    write('RESULT.json', result)
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cmake', type=Path, default=Path('C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe'))
    parser.add_argument('--parallel', type=int, choices=range(1, 5), default=2)
    build(parser.parse_args())


if __name__ == '__main__':
    main()
