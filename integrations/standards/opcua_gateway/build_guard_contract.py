"""Exercise CLI provenance guards before any dependency read or output mutation."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
work = Path.cwd().resolve()
assert work.is_relative_to((root / 'build').resolve())
rejected = ['-Dopen62541_DIR=x', '-Dopen62541_DIR:PATH=x', '-DDDS_STANDARDS_FASTDDS=2.14.5',
            '-DB3_GENERATED_DIR=x', '-DB3_IDL:FILEPATH=x', '-DD1_UA_PREFIX=x',
            '-Bother', '-Cother', '-DUA_ENABLE_ENCRYPTION=ON']
for index, option in enumerate(rejected):
    out = work / f'd1-guard-rejected-{index}'
    assert not out.exists(), out
    command = [sys.executable, '-B', str(root / 'scripts/standards/build_ua_gateway.py'),
               '--fastdds', '3.6.2', '--out', str(out), '--ua-archive', 'archive_missing.zip',
               '--ua-source', 'source_missing', '--clang', 'clang_missing', '--cmake-arg=' + option]
    done = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
    assert done.returncode == 1 and b'unsupported --cmake-arg' in done.stdout, (command, done.returncode, done.stdout)
    assert not out.exists(), out
print(f'D1_BUILD_GUARDS PASS rejected={len(rejected)}')
