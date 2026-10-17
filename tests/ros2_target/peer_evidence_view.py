"""Lossless host-only view of one completed frozen public peer-query protocol.

Only observation_0 ATTEMPT placement changes. No process/device API is imported.
Views are local evidence, never a release; old run64e76c8f2f9b75fc is offline only.
"""
import argparse
import ast
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import stat

TARGETS = {'A': '3e01ff55454d202020104033bf453b00', 'B': '3e01ff55454d202020104433991c3b00'}
TOKENS = {'A': 672006291, 'B': 672042405}
LABELS = ('targets', 'external_hashes', 'directory_absence', 'directory_create',
          'send_actionbag_peer_query', 'send_libmdds_actionbag_token_bridge_so', 'send_libc++_shared_so',
          'deployed_hashes', 'deployed_modes', 'query_launch_once', 'query_exit_observation_0',
          'processes_exited', 'original_raw_hashes_before_receive', 'receive_query_stdout',
          'receive_query_stderr', 'receive_query_exit', 'original_raw_hashes_after_receive',
          'private_identity_stat_hash', 'private_identity_receive', 'private_identity_stat_hash_after_receive')
FROZEN = {'query_stage.py': '51ff5ba76389cb6d26ae3477b9f235ce6bd2ef38cc9c6f6b9dbf429d60d688ef',
          'execute.py': 'b8a55630fd401b22df8af8c58bfd08066757529d603e6ccdc193d1e8b940cd82',
          'run_actionbag_native.py': 'f9fa38d1a1629ccd976b5ba57f36fcea205cc03a544276eee01c3c7977ac6712',
          'run_actionbag_cli.py': '3d348d26157311d5fd5d1f05248d4454ca238296ae8f0154936fac061a03c40e'}
PRODUCTS = {'actionbag_peer_query': 'b37c36c8c2744ee7990cfa2cd3a95c690ab532ad9497383564d883c436ce7a92',
            'libmdds_actionbag_token_bridge.so': 'd7e503a01c8d5d86fd49858bb7e375f49e71585081d87f450cf0521f7611ee57',
            'libc++_shared.so': 'd1a368b58e7cb80a7422b8bd344ec9328a43884d62f409af6e9ec161c73a2f4d'}
FAILURE_SEAL = '19c5ab4eadd06702b5daf842031897f17ec9da3be112107007e9d6b8e7176190'
REPORTS = ('MAP.json', 'AUDIT.json', 'SOURCE_SEAL.json', 'VIEW_SEAL.json', 'RESULT.json')


class PeerEvidenceView:
    def __init__(self, binding):
        self.repo = Path(__file__).resolve().parents[2]
        self.root = self.repo.parents[2] / 'build/r25-native-actionbag'
        self.binding_path = self.path(binding)
        self.binding_sha256 = self.sha(self.binding_path)
        self.converter_sha256 = self.sha(__file__)
        self.binding = self.load(self.binding_path)
        self.require(set(self.binding) == {'schema', 'run', 'source_root', 'source_seal', 'query_result', 'adapter_failure_seal'} and self.binding['schema'] == 1, 'Unknown binding schema')
        self.source = self.path(self.binding['source_root'])
        self.require(self.source.is_relative_to(self.root) and self.source.name == 'fresh-peer', 'Source must be an explicit owned fresh-peer root')
        self.outer = self.source.parent
        self.run = self.binding['run']
        self.require(isinstance(self.run, str) and re.fullmatch('[0-9a-f]{16}', self.run), 'Canonical run required')
        self.seal_path = self.descriptor(self.binding['source_seal'])
        self.require(self.seal_path == self.outer / 'FINAL_QUERY_SEAL.json', 'Wrong source seal role')
        self.query_result_path = self.descriptor(self.binding['query_result'])
        self.require(self.query_result_path == self.outer / 'FINAL_QUERY_RESULT.json', 'Wrong query-result role')
        failure = self.descriptor(self.binding['adapter_failure_seal'])
        self.require(failure == self.root / 'ordinary-recordfix1-imports-window/FAILURE_SEAL.json' and self.sha(failure) == FAILURE_SEAL, 'Original adapter failure lineage required')
        self.sealed = self.read_seal(self.seal_path, self.outer)
        self.failure_sealed = self.read_seal(failure, failure.parent)
        for name in ('PLAN.json', 'DEVICE_PLAN_REVIEW.json', 'MAIN_RELEASE.json', 'EXECUTION_STARTED_ONCE.json',
                     'EXECUTION_RESULT.json', 'SUPPLEMENTARY_COMMANDS.json', 'execute.py', 'PREPARATION_SEAL.json', 'FINAL_QUERY_RESULT.json'):
            self.require(str(self.outer / name) in self.sealed, 'Unsealed required outer role: ' + name)
        self.require(not any('first_failure' in p.name.lower() for p in self.outer.iterdir() if p.is_file()), 'Unresolved outer first failure')
        self.raw = self.inventory(self.source)
        self.files = {n: self.digest(v) for n, v in self.raw.items()}
        self.require(all(self.sealed.get(str(self.source / n)) == d['sha256'] for n, d in self.files.items()), 'Missing/changed/unsealed source member')
        self.require({Path(p).relative_to(self.source).as_posix() for p in self.sealed if Path(p).is_relative_to(self.source)} == set(self.raw), 'Missing source member')
        self.plan = self.data('PLAN_V3.json')
        self.require(self.plan['run'] == self.run and self.sha(self.source / 'query_stage.py') == FROZEN['query_stage.py'] and self.sha(self.outer / 'execute.py') == FROZEN['execute.py'], 'Frozen driver/run mismatch')
        self.require(len(self.plan['artifacts']) == 3 and {r['name']: r['sha256'] for r in self.plan['artifacts']} == PRODUCTS, 'Unselected native query products')
        for product in self.plan['artifacts']:
            self.require(self.sha(self.path(product['local'])) == product['sha256'], 'Frozen native product changed')
        # Reuse only the two pinned pure parsers; never import the native runner
        # module, its subprocess imports, or any driver/dispatch function.
        native = self.path(Path(__file__).with_name('run_actionbag_native.py'))
        self.require(self.sha(native) == FROZEN[native.name], 'Frozen native parser changed')
        self.require(self.sha(self.path(native.with_name('run_actionbag_cli.py'))) == FROZEN['run_actionbag_cli.py'], 'Frozen ordinary consumer changed')
        syntax = ast.parse(native.read_text(encoding='utf-8'))
        self.hdc = next(ast.literal_eval(n.value) for n in syntax.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'HDCPATH' for t in n.targets))
        functions = [n for n in syntax.body if isinstance(n, ast.FunctionDef) and n.name in ('actionbag_fields', 'actionbag_parse_peer')]
        self.require(len(functions) == 2, 'Pinned pure parsers missing')
        namespace = {'re': re}
        exec(compile(ast.Module(body=functions, type_ignores=[]), str(native), 'exec'), namespace)
        self.parse_peer = namespace['actionbag_parse_peer']
        self.validate_outer()
        self.audit = self.audit_source()

    @staticmethod
    def require(condition, message):
        if not condition:
            raise ValueError('BLOCKED: ' + message)

    @staticmethod
    def sha(path):
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()

    @staticmethod
    def digest(data):
        return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)}

    @staticmethod
    def json_bytes(raw):
        def unique(pairs):
            result = {}
            for key, value in pairs:
                PeerEvidenceView.require(key not in result, 'Duplicate JSON key')
                result[key] = value
            return result
        return json.loads(raw.decode('utf-8'), object_pairs_hook=unique,
                          parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON value')))

    @classmethod
    def path(cls, value, exists=True):
        p = Path(os.path.abspath(value))
        for item in [*reversed(p.parents), p]:
            if item.exists() or item.is_symlink():
                info = item.lstat()
                cls.require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400, 'Symlink/reparse path')
        cls.require(not exists or p.exists(), 'Missing path: ' + str(p))
        return p.resolve(strict=exists)

    def load(self, path):
        return self.json_bytes(self.path(path).read_bytes())

    def data(self, name):
        return self.json_bytes(self.raw[name])

    def descriptor(self, row):
        self.require(isinstance(row, dict) and set(row) == {'path', 'sha256'} and isinstance(row['path'], str) and Path(row['path']).is_absolute() and re.fullmatch('[0-9a-f]{64}', row['sha256']), 'Exact descriptor required')
        path = self.path(row['path'])
        self.require(path.is_relative_to(self.root) and path.is_file() and self.sha(path) == row['sha256'], 'Descriptor changed/outside evidence root')
        return path

    def read_seal(self, path, root):
        rows = self.load(path)['files']
        self.require(isinstance(rows, list) and 1 <= len(rows) <= 5000, 'Bounded list seal required')
        result = {}
        for row in rows:
            self.require(isinstance(row, dict) and set(row) in ({'path', 'sha256'}, {'path', 'sha256', 'bytes'}), 'Unknown seal row fields')
            member = self.path(row['path'])
            self.require(member.is_relative_to(root) and str(member) not in result and member.is_file() and self.sha(member) == row['sha256'], 'Duplicate/changed/outside sealed member')
            self.require('bytes' not in row or member.stat().st_size == row['bytes'], 'Sealed size mismatch')
            result[str(member)] = row['sha256']
        return result

    def inventory(self, root):
        result = {}
        pending = [self.path(root)]
        while pending:
            directory = pending.pop()
            for item in sorted(directory.iterdir()):
                path = self.path(item)
                self.require(path.is_relative_to(root), 'Inventory escape')
                if path.is_dir():
                    pending.append(path)
                else:
                    self.require(stat.S_ISREG(path.stat().st_mode) and path.stat().st_size <= 16 * 1024 * 1024, 'Nonregular/oversized evidence')
                    name = path.relative_to(root).as_posix()
                    self.require(not any(n.casefold() == name.casefold() for n in result), 'Case alias')
                    result[name] = path.read_bytes()
                    self.require(len(result) <= 2048, 'Too many evidence files')
        return result

    @staticmethod
    def utc(value):
        date = datetime.fromisoformat(value)
        PeerEvidenceView.require(date.utcoffset() is not None and date.utcoffset().total_seconds() == 0, 'Timezone-aware UTC required')
        return date

    def validate_outer(self):
        outer = self.load(self.outer / 'PLAN.json')
        review = self.load(self.outer / 'DEVICE_PLAN_REVIEW.json')
        release = self.load(self.outer / 'MAIN_RELEASE.json')
        started = self.load(self.outer / 'EXECUTION_STARTED_ONCE.json')
        execution = self.load(self.outer / 'EXECUTION_RESULT.json')
        final = self.load(self.query_result_path)
        for value in (outer, review, release, started, execution):
            self.require(value['run_id'] == self.run, 'Outer run mismatch')
        self.require(review['status'] == 'PASS' and review['assessment'] == 'NO_ACTIONABLE_FINDINGS_FOUND' and review['findings'] == [], 'Unresolved plan review')
        self.require(release['status'] == 'RELEASED_EXACT_ORDINARY_PEER_QUERY' and started['state'] == 'MAY_EXECUTE_ONCE_NO_REPLAY', 'Missing exact original release/once marker')
        fields = {'PLAN.json': 'plan_sha256', 'execute.py': 'execute_sha256', 'fresh-peer/query_stage.py': 'query_stage_sha256', 'fresh-peer/PLAN_V3.json': 'peer_plan_sha256', 'SUPPLEMENTARY_COMMANDS.json': 'supplementary_commands_sha256', 'PREPARATION_SEAL.json': 'preparation_seal_sha256'}
        self.require(set(release['bindings']) == set(fields) | {'DEVICE_PLAN_REVIEW.json'}, 'Unknown release bindings')
        for name, digest in release['bindings'].items():
            self.require(self.sealed.get(str(self.outer / name)) == digest == self.sha(self.outer / name), 'Release binding mismatch')
            if name in fields:
                self.require(review[fields[name]] == digest, 'Review role/hash mismatch')
        self.require(started['plan_sha256'] == self.sha(self.outer / 'PLAN.json') and started['main_release_sha256'] == self.sha(self.outer / 'MAIN_RELEASE.json'), 'Execution/release chain mismatch')
        self.require(self.utc(review['utc']) <= self.utc(release['issued_utc']) <= self.utc(started['utc']), 'Review/release/start time order')
        for path, digest in outer['bindings'].items():
            self.require(self.sha(self.path(path)) == digest, 'Outer frozen binding changed')
        self.read_seal(self.outer / 'PREPARATION_SEAL.json', self.outer)
        self.require(execution['status'] == 'PASS_PUBLIC_QUERY_PENDING_SEAL' and execution['hdc_calls'] == 40 and execution['paired_query_result_sha256'] == self.files['RESULT.json']['sha256'], 'Original execution failed/incomplete')
        binding_field = 'all_' + str(len(outer['bindings'])) + '_plan_bindings'
        self.require(final['status'] == 'PASS_PUBLIC_QUERY_ONLY' and final['host_exit'] == final['both_board_native_exit'] == 0 and final['raw_command_count'] == 40 and final['query_per_board'] == 1 and all(final[k] is True for k in ('owned_processes_exited', 'token_restored', 'cross_confirmed')) and final['protected_guard'] == final.get(binding_field) == 'PASS', 'Original final query evidence not PASS')
        self.require(self.utc(execution['utc']) <= self.utc(final['utc']), 'Final query time order')
        for label in ('BEFORE', 'AFTER'):
            guard_path = self.outer / (label + '-GUARD.json')
            self.require(str(guard_path) in self.sealed, 'Unsealed guard role')
            guard = self.load(guard_path)
            expected = {r['name']: r for r in outer['baseline']['git_states']}
            self.require(guard['status'] == 'PASS' and len(guard['rows']) == len(expected) and {r['name'] for r in guard['rows']} == set(expected), 'Protection guard incomplete')
            for row in guard['rows']:
                baseline = expected[row['name']]
                self.require(row['host_rc'] == 0 and row['matches'] is True and row['argv'] == baseline['argv'] and row['cwd'] == baseline['cwd'], 'Protection guard failed/wrong command')
                for stream in ('stdout', 'stderr'):
                    p = self.outer / (label + '-' + row['name'] + '.' + stream)
                    self.require(self.sealed.get(str(p)) == self.sha(p) == row[stream + '_sha256'], 'Protection raw mismatch')
                self.require(row['stdout_sha256'] == baseline['stdout_sha256'] and row['stderr_sha256'] == baseline['stderr_sha256'], 'Protection output drift')
            self.require(guard['protected_files'] == len(outer['baseline']['protected_files']), 'Protection member count mismatch')
        self.execution_start = self.utc(started['utc'])
        self.execution_end = self.utc(execution['utc'])
        self.require(0 <= (self.execution_end - self.execution_start).total_seconds() <= 600, 'Original whole query budget exceeded')
        self.final_query = final

    def audit_source(self):
        expected = {'PLAN_V3.json', 'query_stage.py', 'RESULT.json'}
        for side in 'AB':
            expected |= {'execution-' + side + '/' + label + suffix for label in LABELS for suffix in ('.ATTEMPT.json', '.json', '.stdout.bin', '.stderr.bin')}
            expected |= {'execution-' + side + '/' + name for name in ('ACTUAL_EXIT_BEFORE_ACCEPTANCE.json', 'DISPATCH_ONCE.json', 'RAW_COLLECTION_COMPLETE.json', 'RESULT.json')}
            expected |= {'private/' + side + '-' + name for name in ('local-udid.bin', 'query.stdout', 'query.stderr', 'query.exit', 'routes.json')}
        self.require(set(self.raw) == expected, 'Unknown/extra/missing/orphan observation or source file; only observation_0 supported')
        stage = self.data('RESULT.json')
        self.require(stage['status'] == 'PASS' and stage['query_execution_count'] == {'A': 1, 'B': 1} and set(stage['boards']) == {'A', 'B'}, 'Incomplete paired result')
        self.require(self.utc(stage['utc']) <= self.execution_end, 'Stage/execution time order')
        self.require({r['side'] for r in self.plan['board_plans']} == {'A', 'B'} and len(self.plan['board_plans']) == 2, 'Invalid board plans')
        audits, parsed, dispatches = [], {}, []
        previous = self.execution_start
        supplementary = self.load(self.outer / 'SUPPLEMENTARY_COMMANDS.json')
        for side in 'AB':
            board = next(r for r in self.plan['board_plans'] if r['side'] == side)
            prefix = 'execution-' + side + '/'
            result = self.data(prefix + 'RESULT.json')
            self.require(result == stage['boards'][side] and result['status'] == 'PASS_NATIVE_PEER_QUERY_ONLY' and result['side'] == side and result['target'] == TARGETS[side] and result['token'] == TOKENS[side] and result['query_exit'] == 0 and all(result[k] is True for k in ('both_exited', 'token_restored', 'sdk_node_buffer_released')), 'Side result/identity/query failure')
            remote = '/data/local/tmp/mdds_dds_' + self.run + '_' + side
            argv = [remote + '/actionbag_peer_query', '--list-nodes', '--check-identities', '--expected-token', str(TOKENS[side]), '--preflight-confirmed', '--identity-output', remote + '/local-udid.bin']
            dispatch = self.data(prefix + 'DISPATCH_ONCE.json')
            self.require(board['target'] == TARGETS[side] and board['token'] == TOKENS[side] and board['remote'] == remote and board['native_query_argv'] == dispatch['query_argv'] == argv and dispatch['target'] == TARGETS[side] and dispatch['state'] == 'MAY_HAVE_RUN_NO_RETRY' and dispatch['plan_sha256'] == self.files['PLAN_V3.json']['sha256'], 'Dispatch plan/target/identity mismatch')
            dispatched = self.utc(dispatch['utc']);dispatches.append(dispatched)
            self.require(self.execution_start <= dispatched <= self.utc(stage['utc']), 'Original dispatch time order')
            supervisor, child = result['supervisor_pid'], result['child_pid']
            self.require(type(supervisor) is type(child) is int and 0 < supervisor <= 2147483647 and 0 < child <= 2147483647 and supervisor != child, 'Invalid owned PID pair')
            commands = {r['kind']: r['argv'] for r in board['commands']}
            extras = {r['kind']: r['argv'] for r in supplementary if r['side'] == side}
            self.require(len(board['commands']) == len(commands) == 12 and len(extras) == 4 and sum(r['side'] == side for r in supplementary) == 4, 'Duplicate/unknown planned command protocol')
            commands.update(extras)
            commands.update(targets=[commands['query_launch_once'][0], 'list', 'targets', '-v'])
            commands['processes_exited'] = [commands['query_launch_once'][0], '-t', TARGETS[side], 'shell', 'test ! -d /proc/' + str(supervisor) + ' && test ! -d /proc/' + str(child) + '; peer_rc=$?; printf "\\nR25_PEER_REMOTE_EXIT=%s\\n" "$peer_rc"; exit "$peer_rc"']
            for label in LABELS:
                name = label.replace('_so', '.so') if label in ('send_libc++_shared_so', 'send_libmdds_actionbag_token_bridge_so') else label
                if label == 'query_exit_observation_0': name = 'query_exit_observation'
                if label == 'original_raw_hashes_after_receive': name = 'original_raw_hashes_before_receive'
                if label == 'private_identity_stat_hash_after_receive': name = 'private_identity_stat_hash'
                attempt = self.data(prefix + label + '.ATTEMPT.json');completion = self.data(prefix + label + '.json')
                self.require(set(attempt) == {'argv', 'utc', 'timeout_seconds', 'state'} and set(completion) == {'argv', 'cwd', 'started_utc', 'elapsed', 'host_timeout_seconds', 'timed_out', 'exit_code', 'stdout_sha256', 'stderr_sha256'}, 'Unknown attempt/completion fields')
                self.require(attempt['argv'] == completion['argv'] == commands[name] and attempt['state'] == 'MAY_HAVE_EXECUTED_NO_RETRY' and Path(completion['cwd']) == self.repo.parents[2], 'Command intent/argv/state/cwd mismatch')
                self.require(completion['argv'] == [self.hdc, 'list', 'targets', '-v'] if label == 'targets' else completion['argv'][:3] == [self.hdc, '-t', TARGETS[side]], 'Wrong host tool or A/B command target')
                began = self.utc(attempt['utc']);started = self.utc(completion['started_utc'])
                timeout, elapsed = attempt['timeout_seconds'], completion['elapsed']
                self.require(type(timeout) in (int, float) and math.isfinite(timeout) and 0 < timeout <= 30 and completion['host_timeout_seconds'] == timeout and type(elapsed) in (int, float) and math.isfinite(elapsed) and 0 <= elapsed <= timeout, 'Invalid/exceeded original timeout budget')
                self.require(previous <= began <= started <= self.utc(stage['utc']), 'Command time order mismatch')
                previous = started
                self.require(type(completion['exit_code']) is int and completion['exit_code'] == 0 and completion['timed_out'] is False, 'Unresolved/failed/timed-out command')
                stdout = self.raw[prefix + label + '.stdout.bin'];stderr = self.raw[prefix + label + '.stderr.bin']
                self.require(hashlib.sha256(stdout).hexdigest() == completion['stdout_sha256'] and hashlib.sha256(stderr).hexdigest() == completion['stderr_sha256'], 'Command raw hash mismatch')
                self.require(not stderr and b'[Fail]' not in stdout and b'Error opening' not in stdout, 'Original command error output')
                if 'shell' in completion['argv']:
                    self.require(len(re.findall(rb'R25_PEER_REMOTE_EXIT=', stdout)) == 1 and re.search(rb'R25_PEER_REMOTE_EXIT=0\r?\n\Z', stdout), 'Nonzero/unknown remote operation; pending unsupported')
                elif 'file' in completion['argv']:
                    self.require(b'filetransfer finish' in stdout.lower(), 'Missing actual file-transfer completion')
                else:
                    rows = [line for line in stdout.decode('utf-8').splitlines() if TARGETS[side] in line]
                    self.require(len(rows) == 1 and re.search(r'\bConnected\b', rows[0]), 'Target connection identity missing')
                if label == 'query_launch_once':
                    self.require(began >= dispatched and re.findall(rb'^DISPATCH_SUPERVISOR_PID=([1-9][0-9]*)\r?$', stdout, re.M) == [str(supervisor).encode()], 'Launch time/supervisor differs')
                if label in ('external_hashes', 'deployed_hashes'):
                    pairs = re.findall(r'^([0-9a-f]{64})  (.+)$', stdout.decode('utf-8').replace('\r\n', '\n'), re.M)
                    hashes = {p: h for h, p in pairs}
                    expected_hashes = self.plan['external_hashes'] if label == 'external_hashes' else {remote + '/' + name: digest for name, digest in PRODUCTS.items()}
                    self.require(len(hashes) == len(pairs) and hashes == expected_hashes, 'Actual external/deployed product hash inventory differs')
                if label == 'directory_create':
                    self.require(re.search(r'^700 [0-9]+ ' + re.escape(remote) + r'\r?$', stdout.decode('utf-8'), re.M), 'Actual owned directory mode missing')
                audits.append({'side': side, 'label': label, 'attempt_utc': attempt['utc'], 'started_utc': completion['started_utc'], 'elapsed': elapsed, 'timeout_seconds': timeout, 'argv': completion['argv'], 'exit_code': completion['exit_code'], 'timed_out': completion['timed_out'], 'attempt_state_unchanged': attempt['state'], 'attempt_sha256': self.files[prefix + label + '.ATTEMPT.json']['sha256'], 'completion_sha256': self.files[prefix + label + '.json']['sha256'], 'stdout_sha256': completion['stdout_sha256'], 'stderr_sha256': completion['stderr_sha256']})
            observed = self.raw[prefix + 'query_exit_observation_0.stdout.bin']
            private = 'private/' + side + '-'
            raw = self.raw[private + 'query.stdout']
            text = raw.decode('utf-8')
            parsed[side] = self.parse_peer(text)
            header = ('0\n' + str(supervisor) + '\n' + str(child) + '\n').encode()
            self.require(observed.replace(b'\r\n', b'\n') == header + raw.replace(b'\r\n', b'\n') + b'\nR25_PEER_REMOTE_EXIT=0\n', 'Observation does not exactly contain original query raw')
            self.require(self.raw[private + 'query.stderr'] == b'' and self.raw[private + 'query.exit'] == b'0\n' and re.search(r'^PEER_TASKS pid=' + str(child) + r' tid=' + str(child) + r' count=1 complete=1\r?$', text, re.M), 'Original query exit/stderr/PID mismatch')
            actual = self.data(prefix + 'ACTUAL_EXIT_BEFORE_ACCEPTANCE.json')
            collected = self.data(prefix + 'RAW_COLLECTION_COMPLETE.json')
            self.require(actual == {'query_exit': 0, 'supervisor_pid': supervisor, 'child_pid': child, 'status': 'OBSERVED_NOT_ACCEPTED'}, 'Actual exit preacceptance mismatch')
            expected_hashes = {remote + '/' + n: hashlib.sha256(v).hexdigest() for n, v in [('query.stdout', raw), ('query.stderr', b''), ('query.exit', b'0\n'), ('query.supervisor.pid', (str(supervisor) + '\n').encode()), ('query.pid', (str(child) + '\n').encode()), ('query.launch-attempt', b'once')]}
            self.require(collected == {'query_exit': 0, 'supervisor_pid': supervisor, 'child_pid': child, 'both_exited': True, 'raw_hashes': expected_hashes, 'acceptance_started': False}, 'Original raw collection identity/hash mismatch')
            before = self.raw[prefix + 'original_raw_hashes_before_receive.stdout.bin']
            self.require(before == self.raw[prefix + 'original_raw_hashes_after_receive.stdout.bin'], 'Original raw changed across receive')
            self.require(dict((p, h) for h, p in re.findall(r'^([0-9a-f]{64})  (.+)$', before.decode('utf-8').replace('\r\n', '\n'), re.M)) == expected_hashes, 'Original six-file remote hash inventory mismatch')
            identity = self.raw[private + 'local-udid.bin']
            self.require(len(identity) == parsed[side]['udid_bytes'] == result['udid_bytes'] and 1 <= len(identity) <= 64 and not any(c in identity for c in (b'\0', b'\r', b'\n')) and hashlib.sha256(identity).hexdigest() == result['udid_sha256'], 'Private identity bytes/hash mismatch')
            identity.decode('utf-8')
            observed_stat = self.raw[prefix + 'private_identity_stat_hash.stdout.bin']
            self.require(observed_stat == self.raw[prefix + 'private_identity_stat_hash_after_receive.stdout.bin'], 'Private identity changed across receive')
            listing = observed_stat.decode('utf-8').replace('\r\n', '\n')
            dm = re.search(r'^700 ([0-9]+) [0-9]+ ' + re.escape(remote) + r'$', listing, re.M)
            fm = re.search(r'^600 ([0-9]+) ' + str(len(identity)) + ' ' + re.escape(remote) + r'/local-udid.bin$', listing, re.M)
            self.require(dm and fm and dm[1] == fm[1] and result['udid_mode'] == '0600' and result['directory_mode'] == '0700' and (hashlib.sha256(identity).hexdigest() + '  ' + remote + '/local-udid.bin\n') in listing, 'Private identity mode/owner/hash proof missing')
            self.require(self.data(private + 'routes.json') == {'local': parsed[side]['local'], 'remote': parsed[side]['remotes']} and result['peer_count'] == len(parsed[side]['remotes']), 'Private parsed routes differ')
        self.require(parsed['A']['local'] != parsed['B']['local'] and parsed['A']['local'] in parsed['B']['remotes'] and parsed['B']['local'] in parsed['A']['remotes'], 'Pair not cross-confirmed')
        earliest = min(dispatches)
        age = (self.utc(stage['utc']) - earliest).total_seconds()
        self.require(0 <= age <= 300 and stage['earliest_dispatch_age_seconds'] == age, 'Original query completion age mismatch/stale')
        self.require({r['path']: r['sha256'] for r in self.final_query['raw_receipt_checks']} == {str(self.source / ('execution-' + r['side'] + '/' + r['label'] + '.json')): r['completion_sha256'] for r in audits} and all(r['host_rc'] == 0 for r in self.final_query['raw_receipt_checks']), 'Final query audit receipt inventory mismatch')
        return {'scope': 'ALL_40_COMPLETED_COMMANDS_AUDITED_NO_STATE_REWRITE', 'run': self.run, 'pairs': audits,
                'earliest_dispatch_utc': earliest.isoformat(), 'old_run_offline_only': self.run == '64e76c8f2f9b75fc',
                'original_adapter_failure_seal': self.binding['adapter_failure_seal'], 'device_release': 'NOT_GRANTED', 'ordinary_execution': 'NOT_RUN'}

    def artifacts(self):
        mapping = []
        for name in sorted(self.files):
            target = 'audit-attempts/' + name if name in ('execution-A/query_exit_observation_0.ATTEMPT.json', 'execution-B/query_exit_observation_0.ATTEMPT.json') else name
            mapping.append({'source_relative': name, 'view_relative': target, **self.files[name]})
        return {'MAP.json': {'scope': 'LOSSLESS_ONE_TO_ONE_COPY_ONLY_TWO_PLACEMENTS_CHANGE', 'source_root': str(self.source), 'files': mapping},
                'AUDIT.json': self.audit,
                'SOURCE_SEAL.json': {'source_root': str(self.source), 'original_seal': self.binding['source_seal'], 'files': self.files},
                'VIEW_SEAL.json': {'files': {r['view_relative']: {'sha256': r['sha256'], 'bytes': r['bytes']} for r in mapping}}}

    def recheck(self):
        self.require(self.sha(self.binding_path) == self.binding_sha256 and self.sha(__file__) == self.converter_sha256, 'Binding/converter bytes changed')
        self.require(self.descriptor(self.binding['source_seal']) == self.seal_path, 'Original source seal changed')
        self.require(self.inventory(self.source) == self.raw, 'Source changed during conversion/validation')
        self.require(self.read_seal(self.seal_path, self.outer) == self.sealed and self.read_seal(self.descriptor(self.binding['adapter_failure_seal']), self.root / 'ordinary-recordfix1-imports-window') == self.failure_sealed, 'Sealed lineage changed')
        self.require(self.load(self.binding_path) == self.binding, 'Input binding changed')

    def convert(self, output):
        output = self.path(output, exists=False)
        self.require(output.is_relative_to(self.root) and not output.exists() and not output.is_relative_to(self.outer) and not self.outer.is_relative_to(output), 'Fresh exclusive nonoverlapping output required')
        output.mkdir(parents=True, exist_ok=False)
        try:
            reports = self.artifacts()
            for row in reports['MAP.json']['files']:
                target = output / 'view' / row['view_relative']
                target.parent.mkdir(parents=True, exist_ok=True)
                self.path(target, exists=False).open('xb').write(self.raw[row['source_relative']])
            for name, value in reports.items():
                (output / name).open('x', encoding='utf-8', newline='\n').write(json.dumps(value, indent=2) + '\n')
            self.recheck()
            result = {'schema': 1, 'status': 'LOCAL_LOSSLESS_VIEW_ONLY_NOT_RELEASED', 'run': self.run,
                      'binding': {'path': str(self.binding_path), 'sha256': self.sha(self.binding_path)},
                      'converter_sha256': self.sha(__file__), 'reports': {n: self.sha(output / n) for n in reports},
                      'old_run_offline_only': self.audit['old_run_offline_only'], 'device_calls': 0,
                      'release_required': 'Main must separately rehash source/gates/full original+view/map/audit/adapter/routes/failure lineage; frozen routes alone do not cover all attempts'}
            (output / 'RESULT.json').open('x', encoding='utf-8', newline='\n').write(json.dumps(result, indent=2) + '\n')
            return self.validate(output)
        except BaseException as error:
            (output / 'FAILURE.json').open('x', encoding='utf-8').write(json.dumps({'status': 'FAIL_INCOMPLETE_VIEW_RETAINED', 'error': str(error), 'device_calls': 0}) + '\n')
            raise

    def validate(self, output):
        output = self.path(output)
        reports = self.artifacts()
        result = self.load(output / 'RESULT.json')
        self.require(set(result) == {'schema', 'status', 'run', 'binding', 'converter_sha256', 'reports', 'old_run_offline_only', 'device_calls', 'release_required'} and result['schema'] == 1 and result['status'] == 'LOCAL_LOSSLESS_VIEW_ONLY_NOT_RELEASED' and result['run'] == self.run and result['device_calls'] == 0 and result['old_run_offline_only'] == self.audit['old_run_offline_only'], 'Wrong view role/status/run')
        self.require(result['binding'] == {'path': str(self.binding_path), 'sha256': self.sha(self.binding_path)} and result['converter_sha256'] == self.sha(__file__) and set(result['reports']) == set(reports), 'View source/binding/report identity changed')
        for name, expected in reports.items():
            self.require(self.sha(output / name) == result['reports'][name] and self.load(output / name) == expected, 'View map/audit/seal changed: ' + name)
        files = self.inventory(output)
        expected = set(REPORTS) | {'view/' + r['view_relative'] for r in reports['MAP.json']['files']}
        self.require(set(files) == expected, 'Unknown/extra/missing view file')
        self.require(all(files['view/' + r['view_relative']] == self.raw[r['source_relative']] for r in reports['MAP.json']['files']), 'View bytes not one-to-one original bytes')
        self.recheck()
        return result


def peer_view_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('convert', 'validate'))
    parser.add_argument('--binding', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    tool = PeerEvidenceView(args.binding)
    result = tool.convert(args.output) if args.operation == 'convert' else tool.validate(args.output)
    print(json.dumps({'status': result['status'], 'run': result['run'], 'old_run_offline_only': result['old_run_offline_only'], 'device_calls': 0}))
    return 0


if __name__ == '__main__':
    raise SystemExit(peer_view_main())
