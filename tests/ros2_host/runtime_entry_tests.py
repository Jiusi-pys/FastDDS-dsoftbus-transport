"""Real POSIX shell entry controls with dummy consumers, not ROS/target evidence.

Run: python -B tests/ros2_host/runtime_entry_tests.py --shell /path/to/sh
The shell is explicit on Windows (for example Git's bin/sh.exe). POSIX-only
direct-execution/PID controls report SKIP on Windows; board evidence is separate.
Host command programs enter the selected POSIX shell on stdin. A POSIX outer
supervisor records the entry child's real $? in a temporary sidecar; native
Windows shell process codes are logged separately, never accepted as POSIX codes.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import shlex
import signal
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2]/'integrations/ros2/target/runtime_entry.py'
SPEC = importlib.util.spec_from_file_location('runtime_entry_under_test', SOURCE)
ENTRY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ENTRY)
SHELL = None


class RuntimeEntryControls(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='runtime_entry_host_')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bundle = self.root/'original bundle'
        (self.bundle/'lib').mkdir(parents=True)
        (self.bundle/'share/example').mkdir(parents=True)
        self.resource = self.bundle/'share/example/package.xml'
        self.resource.write_bytes(b'<package><name>example</name></package>\n')
        self.child = self.bundle/'lib/consumer'
        self.write_child('''printf '%s\\000' "$PWD" "$AMENT_PREFIX_PATH" "$LD_LIBRARY_PATH" "${ENTRY_OTHER-}" "${RMW_IMPLEMENTATION-}" "$@"
''')
        self.cwd = self.root/'unrelated cwd'
        self.cwd.mkdir()

    def write_child(self, body):
        self.child.write_bytes(('#!/bin/sh\n'+body).encode('utf-8'))
        self.child.chmod(0o755)

    def install(self):
        return ENTRY.install_entry(self.bundle, ['consumer'], ['share/example/package.xml'])

    def invoke(self, args=(), env=None, *, entry=None, cwd=None, explicit_shell=False):
        status_path = Path(tempfile.mkdtemp(prefix='entry-status-', dir=self.root))/'status.txt'
        self.assertFalse(status_path.exists())
        entry_path = self.shell_path(self.bundle/'run') if entry is None else entry
        argv = [entry_path, *args]
        if explicit_shell:
            argv = [self.shell_path(Path(SHELL).resolve()), '--', *argv]
        command = shlex.join(argv)
        # Only the shell executable crosses Windows native argv parsing. The
        # entry's POSIX argv is constructed by this stdin program with real quoting.
        # The outer shell waits for the child and writes its real POSIX status.
        program = (command+' </dev/null\nprintf \'%s\\n\' "$?" > '+shlex.quote(self.shell_path(status_path))+
                   ' || exit 125\nexit 0\n')
        native = subprocess.run([SHELL], input=program.encode('utf-8'), cwd=self.cwd if cwd is None else cwd,
                                env=env, capture_output=True, timeout=10, shell=False)
        raw_status = status_path.read_bytes() if status_path.is_file() else b''
        print('HOST_POSIX_ENTRY_ADAPTER '+json.dumps(dict(
            native_returncode=native.returncode, posix_status_raw_hex=raw_status.hex(),
            sidecar=str(status_path), posix_argv=argv)),
              file=sys.stderr, flush=True)
        self.assertEqual(native.returncode, 0, native.stderr)
        self.assertRegex(raw_status, rb'\A(?:0|[1-9][0-9]{0,2})\n\Z')
        status = int(raw_status)
        self.assertLessEqual(status, 255)
        result = subprocess.CompletedProcess(argv, status,
                                             native.stdout, native.stderr)
        result.native_returncode = native.returncode
        return result

    def shell_path(self, path):
        if os.name != 'nt':
            return str(path)
        # Ask the selected Git POSIX environment; do not guess Windows drive mapping.
        # POSIX quoting and stdin also avoid Windows quote decoding for this path.
        program = 'cygpath -u -- '+shlex.quote(str(path))+'\n'
        result = subprocess.run([SHELL], input=program,
                                cwd=self.cwd, capture_output=True, text=True, encoding='utf-8',
                                check=True, timeout=10, shell=False)
        converted = result.stdout.rstrip('\r\n')
        self.assertTrue(converted.startswith('/'), result.stdout)
        return converted

    def shell_pwd(self, directory):
        return subprocess.run([SHELL], input=b'pwd -P\n', cwd=directory,
                              capture_output=True, check=True, timeout=10).stdout.rstrip(b'\r\n')

    def test_relocation_space_cwd_and_arguments(self):
        self.install()
        moved = self.root/'relocated bundle with spaces'
        self.bundle.rename(moved)
        self.bundle = moved
        args = ['consumer', '', 'two words', "quote'\"", '$HOME;*?', '中文🙂', '--flag', 'line1\nline2']
        result = self.invoke(args)
        self.assertEqual(result.returncode, 0, result.stderr)
        fields = result.stdout.split(b'\0')
        prefix = self.shell_pwd(moved)
        self.assertEqual(fields[:3], [self.shell_pwd(self.cwd), prefix, prefix+b'/lib'])
        self.assertEqual(fields[5:-1], [a.encode('utf-8') for a in args[1:]])
        self.assertEqual(fields[-1], b'')

    def test_inherited_prefixes_replaced_other_environment_preserved(self):
        self.write_child('''printf '%s\\000' "$PWD" "$AMENT_PREFIX_PATH" "$LD_LIBRARY_PATH" "${ENTRY_OTHER-}" "${RMW_IMPLEMENTATION-}" "${prefix-}" "${name-}" "${entry_path-}" "${entry_dir-}" "${resource-}"
''')
        self.install()
        for inherited in [None, '', '/foreign/prefix', ':/foreign/prefix:']:
            with self.subTest(inherited=inherited):
                env = dict(os.environ)
                for key in ('AMENT_PREFIX_PATH', 'LD_LIBRARY_PATH'):
                    env.pop(key, None)
                    if inherited is not None:
                        env[key] = inherited
                env.update(ENTRY_OTHER='keep spaces;$*', RMW_IMPLEMENTATION='explicit_rmw')
                for key in ('prefix', 'name', 'entry_path', 'entry_dir', 'resource'):
                    env[key] = 'unchanged_'+key
                result = self.invoke(['consumer'], env)
                self.assertEqual(result.returncode, 0, result.stderr)
                fields = result.stdout.split(b'\0')
                prefix = self.shell_pwd(self.bundle)
                self.assertEqual(fields[1:5], [prefix, prefix+b'/lib', b'keep spaces;$*', b'explicit_rmw'])
                self.assertEqual(fields[5:-1], [b'unchanged_'+key.encode() for key in
                                              ('prefix', 'name', 'entry_path', 'entry_dir', 'resource')])

    def test_child_exit_status_preserved(self):
        self.write_child('exit 37\n')
        self.install()
        self.assertEqual(self.invoke(['consumer']).returncode, 37)

    def test_child_signal_status_preserved(self):
        self.write_child('kill -TERM "$$"\nexit 99\n')
        self.install()
        self.assertIn(self.invoke(['consumer']).returncode, [-signal.SIGTERM, 128+signal.SIGTERM])

    @unittest.skipUnless(os.name == 'posix', 'Direct POSIX executable/PID check; separately required on target')
    def test_direct_entry_exec_keeps_process_pid(self):
        self.write_child('printf "%s\\n" "$$"\n')
        self.install()
        process = subprocess.Popen([str(self.bundle/'run'), 'consumer'], cwd=self.cwd,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            stdout, stderr = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        self.assertEqual(process.returncode, 0, stderr)
        self.assertEqual(int(stdout.strip()), process.pid)

    def test_invalid_usage_and_executable_paths_stop_before_child(self):
        self.install()
        for args in [[], ['undeclared'], ['../consumer'], ['/bin/sh'], ['consumer;echo injected']]:
            with self.subTest(args=args):
                result = self.invoke(args)
                self.assertEqual(result.returncode, 64)
                self.assertEqual(result.stdout, b'')
                self.assertIn(b'runtime entry:', result.stderr)

    def test_missing_executable_stops_before_child(self):
        self.install()
        self.child.unlink()
        result = self.invoke(['consumer'])
        self.assertEqual(result.returncode, 66)
        self.assertEqual(result.stdout, b'')

    @unittest.skipUnless(os.name == 'posix', 'Windows does not preserve POSIX executable bits')
    def test_nonexecutable_consumer_rejected(self):
        self.install()
        self.child.chmod(0o644)
        self.assertEqual(self.invoke(['consumer']).returncode, 66)

    def test_missing_resource_stops_before_child(self):
        self.install()
        self.resource.unlink()
        result = self.invoke(['consumer'])
        self.assertEqual(result.returncode, 66)
        self.assertEqual(result.stdout, b'')
        self.assertIn(b'share/example/package.xml', result.stderr)

    @unittest.skipUnless(os.name == 'posix', 'POSIX pathname and symlink controls')
    def test_symlink_entry_and_colon_prefix_rejected(self):
        self.install()
        alias = self.bundle/'alias'
        alias.symlink_to('run')
        result = subprocess.run([SHELL, str(alias), 'consumer'], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 64)
        moved = self.root/'colon:prefix'
        self.bundle.rename(moved)
        self.bundle = moved
        self.assertEqual(self.invoke(['consumer']).returncode, 64)

    def test_generated_bytes_receipt_and_freshness(self):
        receipt = self.install()
        data = (self.bundle/'run').read_bytes()
        self.assertTrue(data.startswith(b'#!/bin/sh\n'))
        self.assertNotIn(b'\r', data)
        self.assertNotIn(str(self.root).encode(), data)
        self.assertEqual(receipt['sha256'], hashlib.sha256(data).hexdigest())
        self.assertEqual(json.loads((self.bundle/'RUNTIME_ENTRY.json').read_text()), receipt)
        with self.assertRaisesRegex(ValueError, 'Fresh entry'):
            self.install()

    def test_explicit_shell_relative_leading_dash_and_parent_paths(self):
        self.write_child('''printf '%s\\000' "$PWD" "$AMENT_PREFIX_PATH" "$LD_LIBRARY_PATH" "${ENTRY_OTHER-}" "${RMW_IMPLEMENTATION-}" "$@"
exit 37
''')
        self.install()
        args = ['consumer', 'relative arg', '', "quote'\""]
        for name in ['ordinary bundle', '-bundle', '-']:
            moved = self.root/name
            self.bundle.rename(moved)
            self.bundle = moved
            for entry, cwd in [(name+'/run', self.root), ('./'+name+'/run', self.root),
                               ('../'+name+'/run', self.cwd), (self.shell_path(moved/'run'), self.cwd)]:
                with self.subTest(entry=entry):
                    result = self.invoke(args, entry=entry, cwd=cwd, explicit_shell=True)
                    self.assertEqual(result.returncode, 37, result.stderr)
                    fields = result.stdout.split(b'\0')
                    prefix = self.shell_pwd(moved)
                    self.assertEqual(fields[:3], [self.shell_pwd(cwd), prefix, prefix+b'/lib'])
                    self.assertEqual(fields[5:-1], [a.encode('utf-8') for a in args[1:]])

    @unittest.skipUnless(os.name == 'posix', 'POSIX physical LF directory/parent-symlink cases required on target')
    def test_physical_trailing_newlines_never_execute_trimmed_sibling(self):
        self.install()
        for count in (1, 2):
            sibling = self.root/('physical bundle '+str(count))
            shutil.copytree(self.bundle, sibling)
            wrong_child = sibling/'lib/consumer'
            wrong_child.write_bytes(b'#!/bin/sh\nprintf "WRONG_SIBLING\\n"\nexit 37\n')
            wrong_child.chmod(0o755)
            physical = Path(str(sibling)+'\n'*count)
            shutil.copytree(self.bundle, physical)
            alias = self.root/('physical alias '+str(count))
            alias.symlink_to(physical, target_is_directory=True)
            for entry, cwd in [('./run', physical), (str(alias/'run'), self.cwd)]:
                with self.subTest(newlines=count, entry=entry):
                    result = self.invoke(['consumer'], entry=entry, cwd=cwd, explicit_shell=True)
                    self.assertEqual(result.returncode, 64, result.stderr)
                    self.assertEqual(result.stdout, b'')
                    self.assertIn(b'bundle prefix cannot contain colon or newline', result.stderr)

    def test_resource_markers_quotes_spaces_and_metacharacters_are_literal(self):
        resources = ['share/@EXEC_BRANCHES@', 'share/@CHOICES@', 'share/@RESOURCES@',
                     "share/quote' space; $(touch entry_injected) &",
                     'share/backtick `touch entry_injected_backtick`']
        for relative in resources:
            (self.bundle/relative).write_bytes(b'original resource\n')
        receipt = ENTRY.install_entry(self.bundle, ['consumer'], resources)
        self.assertEqual(receipt['required_resources'], resources)
        result = self.invoke(['consumer', "literal arg'\" ; $HOME"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.split(b'\0')[5:-1], [b'literal arg\'" ; $HOME'])
        self.assertFalse((self.cwd/'entry_injected').exists())
        self.assertFalse((self.cwd/'entry_injected_backtick').exists())
        # The exact literal resource must still be required; checks cannot be dropped.
        for relative in resources:
            with self.subTest(missing=relative):
                (self.bundle/relative).unlink()
                result = self.invoke(['consumer'])
                self.assertEqual(result.returncode, 66)
                self.assertEqual(result.stdout, b'')
                (self.bundle/relative).write_bytes(b'original resource\n')

    def test_generator_rejects_bad_or_missing_declarations(self):
        for executables, resources in [([], []), (['../consumer'], []), (['missing'], []),
                                       (['consumer', 'consumer'], []),
                                       (['consumer'], ['../escape']), (['consumer'], ['/absolute']),
                                       (['consumer'], ['share/missing']),
                                       (['consumer'], ['share/example/package.xml']*2)]:
            with self.subTest(executables=executables, resources=resources):
                with self.assertRaises(ValueError):
                    ENTRY.install_entry(self.bundle, executables, resources)
        self.assertFalse((self.bundle/'run').exists())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, add_help=False)
    parser.add_argument('--shell', default=shutil.which('sh'))
    options, remainder = parser.parse_known_args()
    if not options.shell:
        raise SystemExit('A real POSIX shell is required; pass --shell /path/to/sh')
    SHELL = options.shell
    print('HOST_POSIX_ENTRY_CONTROLS_WITH_DUMMY_CONSUMERS; not AArch64/ROS/device evidence', flush=True)
    unittest.main(argv=[sys.argv[0], *remainder])
