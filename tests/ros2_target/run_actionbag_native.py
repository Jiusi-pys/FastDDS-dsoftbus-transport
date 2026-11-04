"""One fixed two-board native Action window. Never provisions identities.

prepare is local-only. run requires fresh independently captured peer receipts
and admitted fixed identities; analyze checks the unchanged Action oracle plus
native evidence. Existing application/bridge deadlines remain unchanged.
"""
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import runpy
import secrets
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

sys.dont_write_bytecode = True  # Never import the Host orchestration runner.
REPO = Path(__file__).resolve().parents[2]
TARGETS = {'A': '3e01ff55454d202020104033bf453b00', 'B': '3e01ff55454d202020104433991c3b00'}
IDENTITY = 'mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16'
PLATFORM = {'/system/lib64/platformsdk/libsoftbus_client.z.so': '9b431d2976616becbbe5b3b18f3c9507ca42f7a15c965c9aa86457f4ff0e6087',
    '/system/lib64/chipset-sdk/libaccesstoken_sdk.z.so': '646e02a51b8e6f38c16ae194d46a7973d59b788e02ddc3414fadbeaef0edec4e',
    '/system/lib64/platformsdk/libtokensetproc_shared.z.so': '80b49bfb66f5ccff8a92bcf9c9759c1537409ae0119629b4b62162a3bd163e4d',
    '/system/lib64/chipset-sdk-sp/libc++.so': '9466fb0d933689533bdf4b4962907f3e0c0970be278bd6ccf703ffa2aea838a6',
    '/system/lib/ld-musl-aarch64.so.1': 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86'}
HDCPATH = r'C:\Users\17715\AppData\Local\OpenHarmony\Sdk\23\toolchains\hdc.exe'


def actionbag_fields(text, prefix):
    result = []
    for line in text.splitlines():
        if not line.startswith(prefix + ' '):
            continue
        pairs = [token.split('=', 1) for token in line[len(prefix) + 1:].split()]
        if any(len(pair) != 2 for pair in pairs) or len({pair[0] for pair in pairs}) != len(pairs):
            raise ValueError('Malformed or duplicate ' + prefix + ' fields')
        result.append(dict(pairs))
    return result


def actionbag_proc_stat(text):
    first, last = text.find(' ('), text.rfind(') ')
    if first < 1 or last <= first or not text[:first].isdigit():
        raise ValueError('Malformed owned process stat')
    tail = text[last + 2:].split()
    if len(tail) < 20 or len(tail[0]) != 1 or not tail[1].isdigit() or not tail[19].isdigit():
        raise ValueError('Incomplete owned process stat')
    return {'pid': int(text[:first]), 'ppid': int(tail[1]), 'starttime': int(tail[19]), 'state': tail[0]}


def actionbag_parse_peer(text):
    tasks = actionbag_fields(text, 'PEER_TASKS')
    tokens = actionbag_fields(text, 'PEER_TOKEN')
    if len(tasks) != 1 or set(tasks[0]) != {'pid', 'tid', 'count', 'complete'} or not re.fullmatch('[1-9][0-9]*', tasks[0]['pid']) or tasks[0]['tid'] != tasks[0]['pid'] or tasks[0]['count'] != '1' or tasks[0]['complete'] != '1':
        raise ValueError('Sole-main query identity missing')
    if len(tokens) != 3 or tokens[0].get('stage') != 'original_token' or tokens[0].get('rc') != '0' or tokens[0].get('matches') not in ('0', '1') or tokens[1:] != [{'stage': 'select_token', 'matches': '1', 'rc': '0'}, {'stage': 'restore_token', 'matches': '1', 'rc': '0'}]:
        raise ValueError('Token-first selection and restoration receipt missing')
    if not text.index('PEER_TASKS ') < text.index('PEER_TOKEN stage=select_token ') < text.index('PEER_IDENTITY ') < text.index('PEER_LOCAL ') < text.index('PEER_TOKEN stage=restore_token '):
        raise ValueError('Token-first admission order changed')
    final = actionbag_fields(text, 'PEER_QUERY_FINAL')
    if final != [{'success': '1', 'admitted': '1', 'restored': '1', 'socket_calls': '0', 'provisioning_calls': '0'}]:
        raise ValueError('Peer query/restoration not confirmed')
    local = re.findall(r'^route_kind=local network_id=([0-9a-fA-F]{64})\r?$', text, re.M)
    remotes = re.findall(r'^route_kind=remote index=([0-9]+) network_id=([0-9a-fA-F]{64})\r?$', text, re.M)
    directory = actionbag_fields(text, 'PEER_DIRECTORY')
    udid = actionbag_fields(text, 'PEER_UDID')
    if len(local) != 1 or not 1 <= len(remotes) <= 32 or [int(x[0]) for x in remotes] != list(range(len(remotes))):
        raise ValueError('Peer directory cardinality/index')
    ids = [x[1] for x in remotes]
    if len(set(ids)) != len(ids) or local[0] in ids or directory != [{'valid': '1', 'remotes': str(len(ids)), 'free_returned': '1', 'cap': '32', 'communication': 'NOT_RUN'}]:
        raise ValueError('Peer directory incomplete/duplicate')
    if actionbag_fields(text, 'PEER_BUFFER') != [{'released': '1', 'allocation': '1'}]:
        raise ValueError('Native directory buffer release missing')
    if actionbag_fields(text, 'PEER_LOCAL') != [{'rc': '0'}] or actionbag_fields(text, 'PEER_ONLINE') != [{'rc': '0', 'count': str(len(ids)), 'allocation': '1'}] or actionbag_fields(text, 'PEER_IDENTITY') != [{'bridge': '0', 'called': '15', 'granted': '1'}]:
        raise ValueError('Native directory/identity calls not verified')
    if len(udid) != 1 or any(udid[0].get(k) != v for k, v in {'rc': '0', 'terminated': '1', 'valid': '1', 'exported': '1', 'mode': '0600', 'identity_bytes': 'NOT_LOGGED'}.items()) or not re.fullmatch(r'[1-9][0-9]*', udid[0].get('bytes', '')) or int(udid[0]['bytes']) > 64:
        raise ValueError('Local private identity not exported')
    return {'local': local[0], 'remotes': ids, 'udid_bytes': int(udid[0]['bytes'])}


def actionbag_native_capture(texts, run, pids):
    frames, result = {}, {}
    for side in 'AB':
        text = texts[side]
        summary = actionbag_fields(text, 'ROS_NATIVE_SUMMARY')
        data = actionbag_fields(text, 'ROS_NATIVE_DATA')
        if len(summary) != 1:
            raise ValueError('One native summary required: ' + side)
        row = summary[0]
        keys = {'run', 'side', 'send_enter', 'send_return', 'send_error', 'ingress', 'detailed_stored', 'metadata_limit', 'overflow', 'malformed', 'sdk_epoch'}
        if set(row) != keys or row['run'] != run or row['side'] != side or row['sdk_epoch'] != 'NOT_PROVEN' or any(not re.fullmatch(r'[0-9]+', row[k]) for k in keys - {'run', 'side', 'sdk_epoch'}):
            raise ValueError('Native summary schema/identity')
        if int(row['send_enter']) < 1 or row['send_enter'] != row['send_return'] or row['send_error'] != '0' or int(row['ingress']) < 1 or row['overflow'] != '0' or row['malformed'] != '0' or row['metadata_limit'] != '8192' or int(row['detailed_stored']) != len(data) or len(data) > 8192:
            raise ValueError('Native summary incomplete/error')
        frames[side] = {'SDK_SEND': set(), 'SDK_INGRESS': set()}
        for entry in data:
            if set(entry) != {'run', 'side', 'observation', 'direction', 'socket', 'writer', 'sequence_hi', 'sequence_lo', 'scope'} or entry['run'] != run or entry['side'] != side or entry['scope'] != 'STRUCTURAL_APP_OBSERVER' or entry['direction'] not in frames[side] or not re.fullmatch('[0-9a-f]{32}', entry['writer']) or not re.fullmatch('[1-9][0-9]*', entry['observation']) or not re.fullmatch('[0-9]+', entry['socket']) or not re.fullmatch('-?[0-9]+', entry['sequence_hi']) or not re.fullmatch('[0-9]+', entry['sequence_lo']):
                raise ValueError('Native data schema/identity')
            high, low = int(entry['sequence_hi']), int(entry['sequence_lo'])
            if not -2147483648 <= high <= 2147483647 or not 0 <= low <= 4294967295 or int(entry['socket']) > 2147483647:
                raise ValueError('Native data range')
            frames[side][entry['direction']].add((entry['writer'], high, low))
        capture = actionbag_fields(text, 'ROS_STARTUP_CAPTURE')
        if len(capture) != 1 or any(capture[0].get(k) != v for k, v in {'run': run, 'side': side, 'source': 'REAL_TARGET', 'completeness': 'CAPTURED', 'overflow': '0', 'output_errors': '0', 'text_truncated': '0', 'first_observation_seq': '0', 'failure_ref': '0'}.items()):
            raise ValueError('Real startup capture incomplete')
        operations = actionbag_fields(text, 'ROS_STARTUP_OPERATION')
        if len(operations) != 5 or {x.get('operation') for x in operations} != {'Socket', 'Listen', 'Bind', 'SendBytes', 'Shutdown'}:
            raise ValueError('Startup operation set')
        for op in operations:
            wanted = 'VOID_NO_RC' if op['operation'] == 'Shutdown' else 'HANDLE_OR_ERROR' if op['operation'] == 'Socket' else 'SDK_RC'
            if any(op.get(k) != v for k, v in {'run': run, 'side': side, 'source': 'REAL_TARGET', 'state': 'RETURN_OBSERVED', 'return_semantics': wanted}.items()) or not re.fullmatch('[1-9][0-9]*', op.get('entered', '')) or op.get('returned') != op['entered']:
                raise ValueError('Native operation unbalanced/semantics')
        events = actionbag_fields(text, 'ROS_STARTUP_EVENT')
        if not events or capture[0].get('stored') != str(len(events)) or capture[0].get('limit') != '256' or len(events) > 256 or any(x.get('run') != run or x.get('side') != side or x.get('source') != 'REAL_TARGET' or x.get('truncated') != '0' for x in events):
            raise ValueError('Selected native event incomplete')
        index = str('AB'.index(side))
        pre = actionbag_fields(text, 'FDDST_ROS_PRE_PARTICIPANT')
        if pre != [{'run': run, 'side': index, 'builtin': '0', 'intraprocess': '0', 'mode': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL'}]:
            raise ValueError('Custom-only native participant required')
        endpoints = actionbag_fields(text, 'FDDST_ENDPOINT')
        if not endpoints or any(x.get('data_sharing') != 'OFF' for x in endpoints):
            raise ValueError('Endpoint shared-memory exclusion missing')
        real = actionbag_fields(text, 'FDDST_REAL_SOCKET')
        closed = actionbag_fields(text, 'FDDST_ROS_CLOSED')
        identity = actionbag_fields(text, 'ROS_IDENTITY_FINAL')
        if len(real) != 1 or any(real[0].get(k) != v for k, v in {'run': run, 'side': index, 'fake_sdk': '0'}.items()) or closed != [{'run': run, 'side': index, 'pump_joined': '1', 'runtime_closed': '1', 'local_only': '1'}] or identity != [{'restored': '1', 'business_rc': '0'}]:
            raise ValueError('Native close/identity incomplete')
        teardown = actionbag_fields(text, 'ROS_TEARDOWN_CAPTURE')
        if not teardown or any(x.get('run') != run or x.get('side') != side or x.get('pid') != str(pids[side]) or x.get('completeness') != 'CAPTURED' or x.get('overflow') != '0' or x.get('output_errors') != '0' for x in teardown):
            raise ValueError('Teardown capture incomplete')
        deletion = actionbag_fields(text, 'ROS_TEARDOWN_EVENT')
        if any(x.get('run') != run or x.get('side') != side or x.get('pid') != str(pids[side]) or x.get('schema') != '1' for x in deletion):
            raise ValueError('Teardown event identity mismatch')
        if not any(x.get('operation') == 'Factory_delete_participant' and x.get('returned') == '1' and x.get('value_type') == 'PUBLIC_RETURN_CODE' and x.get('value') == '0' for x in deletion):
            raise ValueError('DDS public delete return missing')
        if re.search(r'ROS_FAILURE|ACTION_EXCEPTION|INIT_FAILED|RUNTIME_CLOSE_FAILURE|DIAGNOSTIC_CAPTURE.*INCOMPLETE|failed to send response[^\r\n]*timeout', text, re.I):
            raise ValueError('Native/business failure diagnostic')
        result[side] = {'native_records': len(data), 'send_enter': int(row['send_enter']), 'ingress': int(row['ingress'])}
    for side, peer in [('A', 'B'), ('B', 'A')]:
        matched = frames[side]['SDK_SEND'] & frames[peer]['SDK_INGRESS']
        if not matched:
            raise ValueError('Bidirectional structural native RTPS chain missing')
        result[side]['peer_matched_writer_sequences'] = len(matched)
    return {'sides': result, 'sdk_epoch': 'NOT_PROVEN', 'action_uuid_to_rtps_sequence': 'NOT_DIRECTLY_MAPPED'}


def actionbag_verify_maps(text, prefix, files):
    paths = set()
    for line in text.splitlines():
        parts = line.split(None, 5)
        if len(parts) < 5:
            raise ValueError('Malformed process map')
        if len(parts) == 6:
            path = parts[5]
            if '.so' in path and not path.startswith('['):
                if not path.startswith('/') or path.endswith(' (deleted)'):
                    raise ValueError('Unresolved/deleted native map')
                project = path.startswith(prefix + '/') and path[len(prefix) + 1:] in files
                system = path.startswith(('/system/lib/', '/system/lib64/'))
                reserved = re.search(r'/(?:libfastrtps|libfastcdr|librmw|librcl|librosidl|libfastdds_dsoftbus|libmdds_|libc\+\+_shared)', path)
                if not project and (not system or reserved):
                    raise ValueError('Unselected mapped DSO: ' + path)
                paths.add(path)
    for name in ('libfastrtps.so.2.14', 'libfastcdr.so.2', 'librmw_fastrtps_cpp.so', 'libfastdds_dsoftbus_ros2_target_bridge.so', 'libmdds_actionbag_token_bridge.so', 'libc++_shared.so'):
        if prefix + '/' + name not in paths:
            raise ValueError('Critical project library not mapped: ' + name)
    if '/system/lib64/platformsdk/libsoftbus_client.z.so' not in paths:
        raise ValueError('Real DSoftBus provider missing')
    return sorted(paths)


def actionbag_prepare_package(args):
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if output.exists():
        raise ValueError('Fresh local package output required')
    output.mkdir(parents=True)
    identity = args.identity_build.resolve()
    query = args.query_build.resolve()
    receipt = json.loads((identity / 'BUILD_RESULT.json').read_text())
    query_receipt = json.loads((query / 'BUILD_RESULT.json').read_text())
    if receipt.get('identity') != IDENTITY or receipt.get('status') != 'BUILDS' or query_receipt.get('status') != 'BUILDS':
        raise ValueError('Reviewed new-identity build receipts required')
    for directory, build in [(identity, receipt), (query, query_receipt)]:
        inputs = directory / 'INPUTS.json'
        if hashlib.sha256(inputs.read_bytes()).hexdigest() != build['input_manifest_sha256']:
            raise ValueError('Build input manifest changed')
        for path, expected in json.loads(inputs.read_text())['sha256'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != expected:
                raise ValueError('Build input changed: ' + path)
    for path, expected in {**receipt['artifacts'], **query_receipt['artifacts']}.items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != expected:
            raise ValueError('Built candidate changed: ' + path)
    catalog_file = root / 'build/r25-native-actionbag/native-harness-design/R18_DEPENDENCY_CANDIDATES.json'
    catalog = json.loads(catalog_file.read_text())
    members = {}
    for row in catalog['members']:
        if row['name'] in ('ros2_action_acceptance', 'libmdds_runtime_token_bridge.so'):
            continue
        path = Path(row['source'])
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
            raise ValueError('Historical dependency candidate changed: ' + str(path))
        members[row['name']] = path
    selection = json.loads((root / 'build/ros2-target/r2/BUILD_SELECTION.json').read_text())
    elf_module = REPO / 'integrations/ros2/target/package_elf.py'
    elf = runpy.run_path(str(elf_module), run_name='r25_elf_selection')
    external = ['libaccesstoken_sdk.z.so', 'libtokensetproc_shared.z.so', 'libc++.so', 'libc.so', 'libsoftbus_client.z.so']
    package = output / 'package'
    argv = ['--readelf', str(Path(selection['sdk']) / 'llvm/bin/llvm-readelf.exe'), '--main', str(identity / 'action/ros2_action_acceptance'),
        '--bridge', str(members['libfastdds_dsoftbus_ros2_target_bridge.so']), '--library', str(identity / 'libmdds_actionbag_token_bridge.so'), '--output', str(package)]
    for path in members.values():
        argv += ['--library', str(path)]
    for name, path in members.items():
        if '__rosidl_typesupport_fastrtps_cpp.so' in name or name in ('librmw_fastrtps_cpp.so', 'librcl_logging_spdlog.so'):
            argv += ['--dynamic-root', str(path)]
    for name in external:
        argv += ['--external', name]
    elf['main'](argv)
    shutil.copyfile(query / 'actionbag_peer_query', package / 'actionbag_peer_query')
    selected = json.loads((package / 'PACKAGE_INPUTS.json').read_text())
    query_elf = elf['inspect'](Path(selection['sdk']) / 'llvm/bin/llvm-readelf.exe', query / 'actionbag_peer_query')
    if not set(query_elf['needed']) <= ({x['name'] for x in selected['members']} | set(external)):
        raise ValueError('Query requires an unselected native provider')
    resources = {}
    prefixes = [root / 'build/ros2-target/r2/ros/install'] + [Path(row['path']) for row in selection['dependency_prefixes']]
    required = ('example_interfaces', 'action_msgs', 'builtin_interfaces', 'service_msgs', 'unique_identifier_msgs',
                'rcl_interfaces', 'rosgraph_msgs', 'statistics_msgs', 'type_description_interfaces')
    for name in required:
        source = next((p / 'share' / name for p in prefixes if (p / 'share' / name / 'package.xml').is_file()), None)
        if source is None:
            raise ValueError('Missing explicit project package resources: ' + name)
        for path in source.rglob('*'):
            if path.is_symlink():
                raise ValueError('Resource symlink not selected')
            if path.is_file():
                dest = package / 'share' / name / path.relative_to(source)
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, dest)
                resources[dest.relative_to(package).as_posix()] = {'source': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    for prefix in prefixes:
        index = prefix / 'share/ament_index/resource_index'
        if not index.is_dir():
            continue
        for path in index.rglob('*'):
            if path.is_file():
                dest = package / 'share/ament_index/resource_index' / path.relative_to(index)
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() != digest:
                    raise ValueError('Conflicting selected project resource: ' + str(path.relative_to(index)))
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, dest)
                resources[dest.relative_to(package).as_posix()] = {'source': str(path), 'sha256': digest}
    for name in required:
        if not (package / 'share/ament_index/resource_index/packages' / name).is_file():
            raise ValueError('Project package index missing: ' + name)
    files = {}
    for path in package.rglob('*'):
        if path.is_symlink():
            raise ValueError('Package symlink')
        if path.is_file():
            files[path.relative_to(package).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    sources = [Path(__file__), Path(__file__).with_name('test_actionbag_native.py'), elf_module, REPO / 'tests/ros2_host/run_action_acceptance.py', REPO / 'scripts/public_socket/analyze_v1_locators.py']
    manifest = {'scope': 'STATIC_PROJECT_NATIVE_ACTION_PACKAGE_NOT_DEVICE_QUALIFICATION', 'files': files,
        'resources': resources, 'external': PLATFORM, 'identity': IDENTITY,
        'source_sha256': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources},
        'input_receipts': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in (catalog_file, identity / 'BUILD_RESULT.json', query / 'BUILD_RESULT.json', identity / 'INPUTS.json', query / 'INPUTS.json')}}
    (package / 'PACKAGE.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PACKAGE_SELECTED', package, len(files))


def actionbag_peer_receipt(args):
    """Adapt the independently audited V3 peer-query evidence, without device calls."""
    evidence = args.peer_evidence.resolve()
    plan_file = evidence / 'PLAN_V3.json'
    plan = json.loads(plan_file.read_text())
    stage_result = json.loads((evidence / 'RESULT.json').read_text())
    if stage_result.get('status') != 'PASS':
        raise ValueError('Independent peer evidence stage must be PASS')
    run = plan['run']
    if not re.fullmatch('[0-9a-f]{16}', run):
        raise ValueError('Peer run invalid')
    bindings = {str(plan_file): hashlib.sha256(plan_file.read_bytes()).hexdigest()}
    bindings[str(evidence / 'RESULT.json')] = hashlib.sha256((evidence / 'RESULT.json').read_bytes()).hexdigest()
    for item in plan['artifacts']:
        path = Path(item['local'])
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Peer artifact changed')
        bindings[str(path)] = item['sha256']
    selected = {r['name']: r['sha256'] for r in plan['artifacts']}
    if selected != {'actionbag_peer_query': 'b37c36c8c2744ee7990cfa2cd3a95c690ab532ad9497383564d883c436ce7a92',
            'libmdds_actionbag_token_bridge.so': 'd7e503a01c8d5d86fd49858bb7e375f49e71585081d87f450cf0521f7611ee57',
            'libc++_shared.so': 'd1a368b58e7cb80a7422b8bd344ec9328a43884d62f409af6e9ec161c73a2f4d'}:
        raise ValueError('Only the frozen reviewed query products are accepted')
    sides, times, parsed = {}, [], {}
    for side in 'AB':
        board = next(x for x in plan['board_plans'] if x['side'] == side)
        directory = evidence / ('execution-' + side)
        dispatch = json.loads((directory / 'DISPATCH_ONCE.json').read_text())
        result = stage_result['boards'][side]
        expected_remote = '/data/local/tmp/mdds_dds_' + run + '_' + side
        expected_argv = [expected_remote + '/actionbag_peer_query', '--list-nodes', '--check-identities', '--expected-token', str({'A': 672006291, 'B': 672042405}[side]), '--preflight-confirmed', '--identity-output', expected_remote + '/local-udid.bin']
        if board['target'] != TARGETS[side] or board['remote'] != expected_remote or board['native_query_argv'] != expected_argv or dispatch['query_argv'] != expected_argv or dispatch['target'] != TARGETS[side] or dispatch['plan_sha256'] != bindings[str(plan_file)]:
            raise ValueError('Fixed fresh peer launch contract mismatch')
        if result['status'] != 'PASS_NATIVE_PEER_QUERY_ONLY' or result['side'] != side or result['target'] != TARGETS[side] or result['query_exit'] != 0 or not all(result[k] is True for k in ('both_exited', 'token_restored', 'sdk_node_buffer_released')):
            raise ValueError('Peer query not completed/restored')
        if result.get('udid_mode') != '0600' or result.get('directory_mode') != '0700':
            raise ValueError('Peer private export modes not confirmed')
        for name in ('processes_exited', 'private_identity_stat_hash', 'receive_query_stdout', 'receive_query_stderr', 'receive_query_exit', 'private_identity_receive'):
            stem = directory / name
            record = json.loads(stem.with_suffix('.json').read_text())
            stdout, stderr = stem.with_suffix('.stdout.bin').read_bytes(), stem.with_suffix('.stderr.bin').read_bytes()
            if record['exit_code'] != 0 or record['timed_out'] or record['argv'][:3] != [HDCPATH, '-t', TARGETS[side]] or hashlib.sha256(stdout).hexdigest() != record['stdout_sha256'] or hashlib.sha256(stderr).hexdigest() != record['stderr_sha256'] or b'[Fail]' in stdout + stderr:
                raise ValueError('Peer capture/process-exit receipt failed')
            if 'shell' in record['argv']:
                if not re.search(rb'R25_PEER_REMOTE_EXIT=0\r?\n\Z', stdout):
                    raise ValueError('Peer remote completion missing')
                if name == 'processes_exited' and not record['argv'][4].startswith('test ! -d /proc/' + str(result['supervisor_pid']) + ' && test ! -d /proc/' + str(result['child_pid']) + '; peer_rc=$?;'):
                    raise ValueError('Exit proof does not cover the exact query/supervisor PIDs')
            elif b'filetransfer finish' not in stdout.lower():
                raise ValueError('Peer transfer completion missing')
            for path in (stem.with_suffix('.json'), stem.with_suffix('.stdout.bin'), stem.with_suffix('.stderr.bin')):
                bindings[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        private = evidence / 'private'
        stdout_file = private / (side + '-query.stdout')
        udid_file = private / (side + '-local-udid.bin')
        raw = stdout_file.read_bytes()
        parsed[side] = actionbag_parse_peer(raw.decode('utf-8'))
        if actionbag_fields(raw.decode('utf-8'), 'PEER_TASKS')[0]['pid'] != str(result['child_pid']):
            raise ValueError('Native helper PID does not match supervised query PID')
        if (private / (side + '-query.stderr')).read_bytes() or (private / (side + '-query.exit')).read_text().strip() != '0':
            raise ValueError('Peer raw stderr/exit mismatch')
        udid = udid_file.read_bytes()
        if len(udid) != parsed[side]['udid_bytes'] or not 1 <= len(udid) <= 64 or any(c in udid for c in (b'\0', b'\r', b'\n')) or hashlib.sha256(udid).hexdigest() != result['udid_sha256']:
            raise ValueError('Private local UDID binding mismatch')
        udid.decode('utf-8')
        observations = list(directory.glob('query_exit_observation_*.json'))
        confirmed = False
        for path in observations:
            record = json.loads(path.read_text())
            observed = path.with_suffix('.stdout.bin').read_bytes()
            if record['exit_code'] != 0 or record['timed_out'] or hashlib.sha256(observed).hexdigest() != record['stdout_sha256']:
                continue
            text = observed.decode().replace('\r\n', '\n')
            header = '0\n' + str(result['supervisor_pid']) + '\n' + str(result['child_pid']) + '\n'
            suffix = '\nR25_PEER_REMOTE_EXIT=0\n'
            if text.startswith(header) and text.endswith(suffix) and text[len(header):-len(suffix)].rstrip('\n') == raw.decode().replace('\r\n', '\n').rstrip('\n'):
                confirmed = True
                bindings[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
                bindings[str(path.with_suffix('.stdout.bin'))] = hashlib.sha256(observed).hexdigest()
        if not confirmed:
            raise ValueError('Raw peer stdout not bound to successful native observation')
        times.append(datetime.fromisoformat(dispatch['utc']))
        sides[side] = {'target': TARGETS[side], 'host_exit': 0, 'remote_exit': 0, 'stdout_file': str(stdout_file), 'stdout_sha256': hashlib.sha256(raw).hexdigest(),
            'udid_file': str(udid_file), 'udid_sha256': hashlib.sha256(udid).hexdigest(), 'query_run': run, 'known_query_and_supervisor_exited': True}
    if parsed['B']['local'] not in parsed['A']['remotes'] or parsed['A']['local'] not in parsed['B']['remotes']:
        raise ValueError('Fresh pair not cross-confirmed')
    if any(t.utcoffset() is None for t in times):
        raise ValueError('Query UTC timestamp required')
    value = {'observed_utc': min(times).isoformat(), 'sides': sides, 'bindings': bindings, 'cross_confirmed': True,
        'freshness_policy': 'Run requires oldest query dispatch <=300s at validation and launch; local conservative harness policy, not official DSoftBus API expiry',
        'device_calls_by_adapter': 0, 'identity_mutations': 0}
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
    print('PEER_RECEIPT_BOUND_NO_DEVICE_CALLS', args.output)


class ActionbagNativeRunner:
    """A single owned pair; failures retain remote evidence, never retry a window."""
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.owns_output = False
        self.output.mkdir(parents=True, exist_ok=False)
        self.owns_output = True
        self.run = secrets.token_hex(8)
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.sequence = 0
        self.owned = {}
        self.launch_pending = {}
        self.roots = {}
        self.errors = []
        self.blocked = []
        self.stage = 'INPUTS'
        self.stage_deadline = None
        self.package = args.package.resolve()
        self.manifest = json.loads((self.package / 'PACKAGE.json').read_text())
        self.files = dict(self.manifest['files'])
        self.files['PACKAGE.json'] = hashlib.sha256((self.package / 'PACKAGE.json').read_bytes()).hexdigest()
        for name, digest in self.files.items():
            relative = PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts or not re.fullmatch(r'[A-Za-z0-9_./+\-]+', name):
                raise ValueError('Unsafe package member')
            path = self.package.joinpath(*relative.parts)
            if path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError('Package member changed: ' + name)
        actual = {p.relative_to(self.package).as_posix() for p in self.package.rglob('*') if p.is_file()}
        if actual != set(self.files) or any(p.is_symlink() for p in self.package.rglob('*')):
            raise ValueError('Package inventory changed')
        for path, digest in self.manifest['source_sha256'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
                raise ValueError('Frozen package source changed: ' + path)
        for path, digest in self.manifest['input_receipts'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
                raise ValueError('Frozen package build receipt changed: ' + path)
        identity = json.loads(args.identity_result.read_text())
        if identity['main_execution_result'] != 'PASS_IDENTITY_READINESS_ONLY':
            raise ValueError('Identity readiness missing')
        self.tokens = {}
        for row in identity['boards']:
            side = row['side']
            if side not in TARGETS or row['target'] != TARGETS[side] or row['new_identity'] != IDENTITY or not all(row[k] is True for k in ('native_normal_manager_granted', 'original_process_token_restored', 'wrapper_exit_confirmed', 'legacy_exact_observations_unchanged')) or row['helper_exit'] != 0:
                raise ValueError('Identity receipt incomplete')
            self.tokens[side] = row['new_token']
        if self.tokens != {'A': 672006291, 'B': 672042405}:
            raise ValueError('Only the exact existing approved tokens are permitted')
        routes = json.loads(args.routes.read_text())
        observed = datetime.fromisoformat(routes['observed_utc'])
        if observed.utcoffset() is None:
            raise ValueError('Peer UTC timestamp required')
        age = (datetime.now(timezone.utc) - observed).total_seconds()
        if observed.utcoffset() is None or not 0 <= age <= 300:
            raise ValueError('Fresh peer receipts (at most 300 seconds) required')
        if routes.get('cross_confirmed') is not True or not routes.get('bindings'):
            raise ValueError('Use the fixed routes adapter receipt')
        for path, digest in routes['bindings'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != digest:
                raise ValueError('Peer command evidence changed')
        self.routes, self.udids = {}, {}
        self.observed = observed
        for side in 'AB':
            row = routes['sides'][side]
            if row['target'] != TARGETS[side] or row['host_exit'] != 0 or row['remote_exit'] != 0:
                raise ValueError('Peer query command did not succeed')
            for kind in ('stdout', 'udid'):
                path = Path(row[kind + '_file'])
                data = path.read_bytes()
                if hashlib.sha256(data).hexdigest() != row[kind + '_sha256']:
                    raise ValueError('Peer receipt changed')
                if kind == 'stdout':
                    self.routes[side] = actionbag_parse_peer(data.decode('utf-8-sig'))
                else:
                    if not 1 <= len(data) <= 64 or any(c in data for c in (b'\x00', b'\r', b'\n')):
                        raise ValueError('Private local UDID invalid')
                    self.udids[side] = data.decode('utf-8')
            if len(self.udids[side].encode()) != self.routes[side]['udid_bytes']:
                raise ValueError('Private export length disagrees')
        if self.udids['A'] == self.udids['B'] or any(self.routes[p]['local'] not in self.routes[s]['remotes'] for s, p in [('A', 'B'), ('B', 'A')]):
            raise ValueError('Pair not cross-confirmed by current public directories')
        self.write('INPUTS.json', {'run': self.run, 'package': str(self.package), 'package_sha256': self.files['PACKAGE.json'],
            'routes_sha256': hashlib.sha256(args.routes.read_bytes()).hexdigest(), 'identity_result_sha256': hashlib.sha256(args.identity_result.read_bytes()).hexdigest(),
            'namespace': 'mdds.r25.action', 'domain': 201, 'slots': {'A': 1, 'B': 2}, 'targets': TARGETS,
            'limits_seconds': {'listener': 60, 'business': 55, 'close': 60, 'exit': 20, 'grace': 10, 'term': 5, 'kill': 2},
            'platform_transitives': 'Only actual mapped system providers are observed and hash recorded; primary providers pinned separately'})

    def write(self, name, value):
        path = self.output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def command(self, side, argv, timeout=15):
        if self.stage_deadline is not None:
            timeout = min(timeout, self.stage_deadline - time.monotonic())
            if timeout <= 0:
                raise ValueError('Declared native stage deadline exceeded before HDC dispatch')
        self.sequence += 1
        base = self.output / 'commands' / ('%04d-%s' % (self.sequence, side))
        base.parent.mkdir(parents=True, exist_ok=True)
        record = {'argv': [str(self.args.hdc), '-t', TARGETS[side], *argv], 'start_utc': datetime.now(timezone.utc).isoformat(), 'timeout_seconds': timeout, 'host_exit': None}
        began = time.monotonic()
        try:
            result = subprocess.run(record['argv'], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=False)
            out, err = result.stdout, result.stderr
            record['host_exit'] = result.returncode
        except subprocess.TimeoutExpired as exc:
            out, err = exc.stdout or b'', exc.stderr or b''
            record['timed_out'] = True
        record.update(end_utc=datetime.now(timezone.utc).isoformat(), elapsed_seconds=time.monotonic() - began)
        base.with_suffix('.stdout').write_bytes(out)
        base.with_suffix('.stderr').write_bytes(err)
        self.write(str(base.relative_to(self.output)) + '.json', record)
        if record['host_exit'] != 0 or b'[Fail]' in out + err or len(out) + len(err) > 20 * 1024 * 1024:
            raise OSError('HDC infrastructure/capture failed; see ' + str(base))
        return out

    def shell(self, side, script, timeout=15):
        marker = 'R25_RC_' + secrets.token_hex(8)
        wrapped = '(' + script + '); _r25_rc=$?; printf "\\n' + marker + '=%s\\n" "$_r25_rc"'
        raw = self.command(side, ['shell', 'sh -c ' + shlex.quote(wrapped)], timeout)
        match = re.search(rb'\r?\n' + marker.encode() + rb'=([0-9]+)\r?\n?$', raw)
        if match is None:
            raise OSError('Remote exit marker missing')
        remote = int(match[1])
        self.write('remote/%04d-%s.json' % (self.sequence, side), {'remote_exit': remote, 'marker': marker})
        if remote != 0:
            raise OSError('Remote setup/capture command exit ' + str(remote))
        return raw[:match.start()]

    def send(self, side, local, remote):
        raw = self.command(side, ['file', 'send', str(Path(local)), remote], 30)
        if b'transfer finish' not in raw.lower():
            raise OSError('HDC transfer completion missing')
        expected = hashlib.sha256(Path(local).read_bytes()).hexdigest()
        got = self.shell(side, 'sha256sum ' + shlex.quote(remote)).decode().split()
        if not got or got[0] != expected:
            raise OSError('Remote transfer hash mismatch')

    def publish(self, side, name, data):
        prefix = self.prefix[side]
        if name not in ('control.LISTENER_READY', 'control.ACTION_RELEASE', 'control.RUNTIME_CLOSE'):
            raise ValueError('Unknown release')
        self.guard(side)
        target = prefix + '/' + name
        self.root_guard(side)
        self.shell(side, 'umask 077; set -C; test ! -e ' + shlex.quote(target) + ' && test ! -L ' + shlex.quote(target) + ' && test ! -e ' + shlex.quote(target + '.tmp') + ' && test ! -L ' + shlex.quote(target + '.tmp') + ' && printf %s ' + shlex.quote(data) + ' > ' + shlex.quote(target + '.tmp') + ' && mv ' + shlex.quote(target + '.tmp') + ' ' + shlex.quote(target))
        self.write('releases/%s-%s.json' % (side, name), {'utc': datetime.now(timezone.utc).isoformat(), 'contents': data, 'owned': self.owned[side]})

    def snapshot(self, side, pid):
        if type(pid) is not int or pid <= 1:
            raise ValueError('Invalid owned PID')
        root = '/proc/' + str(pid)
        raw = self.shell(side, 'if test -d ' + root + '; then printf "STAT\\n"; cat ' + root + '/stat; printf "\\nEXE\\n"; readlink ' + root + '/exe; printf "CMD\\n"; od -An -tx1 ' + root + '/cmdline; else printf "ABSENT\\n"; fi').decode('utf-8')
        if raw.strip() == 'ABSENT':
            return None
        match = re.fullmatch(r'STAT\r?\n([^\r\n]+)\r?\n\r?\nEXE\r?\n([^\r\n]+)\r?\nCMD\r?\n([0-9a-fA-F \r\n]+)', raw)
        if not match:
            raise OSError('Owned process identity capture incomplete')
        result = actionbag_proc_stat(match[1])
        result.update(exe=match[2], argv=bytes.fromhex(match[3]).decode('utf-8').rstrip('\x00').split('\x00'))
        return result

    def guard(self, side, role='child', absent_ok=False):
        expected = self.owned[side][role]
        actual = self.snapshot(side, expected['pid'])
        if actual is None and absent_ok:
            return False
        keys = ('pid', 'starttime', 'exe', 'argv', 'ppid') if role == 'child' else ('pid', 'starttime', 'exe', 'argv')
        if actual is None or any(actual[k] != expected[k] for k in keys):
            raise OSError('Owned PID identity changed; refusing action')
        return True

    def root_guard(self, side):
        prefix = self.prefix[side]
        actual = self.shell(side, 'test ! -L ' + prefix + ' && stat -c "%d:%i:%u:%a" ' + prefix).decode().strip()
        if actual != self.roots[side]:
            raise OSError('Fresh run directory identity changed')

    def deploy(self):
        self.stage = 'DEPLOY'
        approved_temp = Path(os.environ['LOCALAPPDATA']) / 'Temp'
        temp = Path(tempfile.mkdtemp(prefix='r25-', dir=approved_temp)).resolve()
        if temp.parent != approved_temp.resolve() or not temp.name.startswith('r25-'):
            raise OSError('Local owned archive directory escaped approved Temp')
        self.write('LOCAL_TEMP.json', {'directory': str(temp), 'parent': str(approved_temp), 'platform_limit': 'Windows native HDC local path spelling and explicit approved Temp required'})
        archive = temp / 'p.tar'
        try:
            with tarfile.open(archive, 'w', format=tarfile.USTAR_FORMAT) as tar:
                for name in sorted(self.files):
                    data = (self.package / name).read_bytes()
                    if hashlib.sha256(data).hexdigest() != self.files[name]:
                        raise ValueError('Package changed during archive creation')
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.uid, entry.gid, entry.mtime = len(data), 0, 0, 0
                    entry.mode = 0o700 if name in ('ros2_action_acceptance', 'actionbag_peer_query') else 0o600
                    entry.tobuf(format=tarfile.USTAR_FORMAT)  # Fail if any canonical name cannot be represented.
                    tar.addfile(entry, io.BytesIO(data))
            self.write('ARCHIVE.json', {'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'files': self.files, 'format': 'USTAR_REGULAR_ONLY'})
            for side in 'AB':
                p = self.prefix[side]
                upload = p + '.upload.tar'
                raw = self.shell(side, 'umask 077; test ! -e ' + p + ' && test ! -e ' + upload + ' && mkdir -m 700 ' + p + ' && stat -c "%d:%i:%u:%a" ' + p)
                self.roots[side] = raw.decode().strip()
                if not re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+:700', self.roots[side]):
                    raise OSError('Fresh private directory mode not confirmed')
                self.send(side, archive, upload)
                self.root_guard(side)
                self.shell(side, 'tar -xf ' + upload + ' -C ' + p + ' && rm ' + upload)
                self.verify_files(side)
                native = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(n) for n in PLATFORM)).decode()
                if {line.split()[1]: line.split()[0] for line in native.splitlines()} != PLATFORM:
                    raise OSError('Primary platform provider preflight mismatch')
                self.configure(side)
        finally:
            # Only this exact fresh local directory and its known archive are ours.
            if temp.parent == approved_temp.resolve() and temp.is_dir() and not temp.is_symlink():
                if archive.is_file() and not archive.is_symlink():
                    archive.unlink()
                temp.rmdir()

    def verify_files(self, side):
        p = self.prefix[side]
        self.root_guard(side)
        script = 'cd ' + p + ' && find . -type l -print && find . -type f -exec sha256sum {} \\;'
        raw = self.shell(side, script, 30).decode()
        found = {}
        for line in raw.splitlines():
            match = re.fullmatch(r'([0-9a-f]{64})  \./(.+)', line)
            if not match or match[2] in found:
                raise OSError('Remote inventory noncanonical or symlink')
            found[match[2]] = match[1]
        # Mutable run outputs are added only after the first exact package check.
        if not set(self.files) <= set(found) or any(found[n] != d for n, d in self.files.items()):
            raise OSError('Remote package inventory/hash mismatch')
        if side not in self.owned and set(found) != set(self.files):
            raise OSError('Unexpected file before launch')
        self.write('inventory/%04d-%s.json' % (self.sequence, side), found)
        return found

    def configure(self, side):
        peer = 'B' if side == 'A' else 'A'
        p = self.prefix[side]
        service = 'com.kaihong.mdds.r25.' + self.run
        fields = {'bridge_library': p + '/libfastdds_dsoftbus_ros2_target_bridge.so', 'fastdds_module': p + '/libfastrtps.so.2.14',
            'fastcdr_module': p + '/libfastcdr.so.2', 'softbus_module': '/system/lib64/platformsdk/libsoftbus_client.z.so',
            'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL', 'run': self.run, 'side': str('AB'.index(side)), 'domain': '201',
            'authorized_package': 'com.kaihong.mdds', 'peer_network_id': self.routes[peer]['local'], 'service_prefix': service,
            'connect_release': p + '/control.LISTENER_READY', 'locator_version': 'V1', 'namespace': 'mdds.r25.action',
            'local_udid': self.udids[side], 'peer_udid': self.udids[peer], 'service_slot': str('AB'.index(side) + 1), 'peer_slot': str('AB'.index(peer) + 1),
            'profile': 'BYTES_NATIVE', 'native_name_contract': 'PACKAGE_SCOPED_V1', 'local_initiator_name': service + '.' + side + '.tx',
            'peer_initiator_name': service + '.' + peer + '.tx', 'runtime_close_release': p + '/control.RUNTIME_CLOSE'}
        config = self.output / (side + '.private.conf')
        config.write_text(''.join(k + '=' + v + '\n' for k, v in fields.items()), encoding='utf-8', newline='\n')
        self.send(side, config, p + '/ros.conf')
        env = {'PATH': '/system/bin:/system/xbin', 'LD_LIBRARY_PATH': p + ':/system/lib64/platformsdk:/system/lib64/chipset-sdk:/system/lib64/chipset-sdk-sp:/system/lib64:/system/lib',
            'AMENT_PREFIX_PATH': p, 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': '201', 'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET',
            'ROS_LOG_DIR': p + '/logs', 'RCUTILS_COLORIZED_OUTPUT': '0', 'RCUTILS_LOGGING_USE_STDOUT': '0',
            'FASTDDS_DSOFTBUS_REQUIRED_CONFIG': p + '/ros.conf', 'FASTDDS_DSOFTBUS_NATIVE_TOKEN': str(self.tokens[side]),
            'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED': '1', 'FASTDDS_DSOFTBUS_NATIVE_IDENTITY': IDENTITY,
            'FDDST_ACTION_RELEASE': p + '/control.ACTION_RELEASE', 'FASTDDS_ENVIRONMENT_FILE': '', 'SKIP_DEFAULT_XML': '1'}
        command = 'env -i ' + ' '.join(shlex.quote(k + '=' + v) for k, v in env.items()) + ' ' + p + '/ros2_action_acceptance ' + side + ' ' + self.run
        script = 'umask 077\ncd ' + p + ' || exit 91\nmkdir logs || exit 92\n' + command + ' >stdout 2>stderr &\nchild=$!\nprintf "%s\\n" "$child" > child.pid.tmp\nmv child.pid.tmp child.pid\nwait "$child"\nrc=$?\nprintf "R25_CHILD_EXIT run=' + self.run + ' side=' + side + ' pid=%s rc=%s\\n" "$child" "$rc" > exit.tmp\nmv exit.tmp exit\nexit "$rc"\n'
        supervisor = self.output / (side + '.supervisor.sh')
        supervisor.write_text(script, encoding='utf-8', newline='\n')
        self.send(side, supervisor, p + '/supervisor.sh')
        self.write(side + '.environment.json', env)

    def launch(self, side):
        p = self.prefix[side]
        if (datetime.now(timezone.utc) - self.observed).total_seconds() > 300:
            raise OSError('Peer receipts expired during deployment; no business launch')
        self.launch_pending[side] = {'directory': p, 'state': 'LAUNCH_DISPATCHED_OWNERSHIP_NOT_YET_CONFIRMED'}
        self.write('LAUNCH_PENDING.json', self.launch_pending)
        raw = self.shell(side, 'cd ' + p + ' || exit 91; nohup sh ' + p + '/supervisor.sh </dev/null >' + p + '/supervisor.stdout 2>' + p + '/supervisor.stderr & printf "%s\\n" "$!"')
        supervisor_pid = int(raw.decode().strip())
        self.launch_pending[side]['supervisor_pid'] = supervisor_pid
        self.write('LAUNCH_PENDING.json', self.launch_pending)
        supervisor = self.snapshot(side, supervisor_pid)
        if supervisor is None or supervisor['argv'] != ['sh', p + '/supervisor.sh'] or supervisor['exe'] not in ('/system/bin/sh', '/system/bin/toybox', '/bin/sh'):
            raise OSError('Supervisor ownership not established')
        self.owned[side] = {'supervisor': supervisor}
        self.write(side + '.ownership.json', self.owned[side])
        deadline = time.monotonic() + 5
        child = None
        while time.monotonic() < deadline:
            raw = self.shell(side, 'if test -f ' + p + '/child.pid; then cat ' + p + '/child.pid; fi').decode().strip()
            if raw:
                child = self.snapshot(side, int(raw))
                break
            time.sleep(0.1)
        if child is None or child['argv'] != [p + '/ros2_action_acceptance', side, self.run] or child['exe'] != p + '/ros2_action_acceptance' or child['ppid'] != supervisor_pid:
            raise OSError('Business child ownership not established')
        self.owned[side] = {'child': child, 'supervisor': supervisor}
        self.launch_pending[side]['state'] = 'OWNERSHIP_CONFIRMED'
        self.write('LAUNCH_PENDING.json', self.launch_pending)
        self.write(side + '.ownership.json', self.owned[side])

    def poll(self, side):
        p = self.prefix[side]
        raw = self.shell(side, 'test -f ' + p + '/stdout && test -f ' + p + '/stderr && tail -c 65536 ' + p + '/stdout && printf "\\nR25_STDERR\\n" && tail -c 65536 ' + p + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        if re.search(r'ROS_FAILURE|ACTION_EXCEPTION|INIT_FAILED|RUNTIME_CLOSE_FAILURE', text):
            raise ValueError('Executed native process reported failure')
        status = self.shell(side, 'if test -f ' + p + '/exit; then cat ' + p + '/exit; fi').decode().strip()
        return text, status

    def barrier(self, stage, seconds):
        self.stage = stage
        start = time.monotonic()
        self.stage_deadline = start + seconds
        self.write('stages/' + stage + '-start.json', {'utc': datetime.now(timezone.utc).isoformat(), 'limit_seconds': seconds})
        captured_late_maps = set()
        while time.monotonic() - start < seconds:
            ready = {}
            for side in 'AB':
                text, status = self.poll(side)
                pid = str(self.owned[side]['child']['pid'])
                if status:
                    raise ValueError('Business exited before paired ' + stage + ': ' + status)
                if stage == 'LISTENER':
                    rows = actionbag_fields(text, 'ROS_LISTENER_READY')
                    ready[side] = len(rows) == 1 and all(rows[0].get(k) == v for k, v in {'run': self.run, 'side': side, 'pid': pid, 'fake_sdk': '0'}.items()) and re.fullmatch('[0-9]+', rows[0].get('listener', '')) is not None and int(rows[0]['listener']) <= 2147483647
                elif stage == 'BUSINESS':
                    connect = actionbag_fields(text, 'ROS_CONNECT_ALLOWED')
                    if len(connect) != 1 or any(connect[0].get(k) != v for k, v in {'run': self.run, 'side': side, 'pid': pid}.items()):
                        ready[side] = False
                        continue
                    events = actionbag_fields(text, 'ACTION_ACCEPT')
                    done = [x for x in events if x.get('event') == 'BUSINESS_DONE']
                    ready[side] = len(done) == 1 and all(done[0].get(k) == v for k, v in {'run': self.run, 'side': side, 'pid': pid, 'requested': '5', 'accepted': '4', 'rejected': '1', 'terminals': '4', 'pending': '0'}.items())
                    if side not in captured_late_maps and not done and any(x.get('event') in ('SERVER_READY', 'CLIENT_READY') for x in events):
                        self.maps(side, 'business')
                        captured_late_maps.add(side)
                else:
                    rows = actionbag_fields(text, 'ROS_RUNTIME_CLOSE_READY')
                    wanted = {'schema': '1', 'run': self.run, 'side': side, 'pid': pid, 'stage': 'RUNTIME_CLOSE', 'stopping': '1', 'sealed': '1', 'active_pumps': '0', 'pump_joined': '1'}
                    ready[side] = rows == [wanted]
            if all(ready.values()):
                if stage == 'LISTENER':
                    for side in 'AB':
                        self.maps(side, 'listener')
                    for side in 'AB':
                        self.publish(side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + self.run + ' side=' + side + ' stage=LISTENER_READY\n')
                elif stage == 'BUSINESS':
                    for side in 'AB':
                        self.publish(side, 'control.ACTION_RELEASE', 'RELEASE ' + self.run + '\n')
                else:
                    for side in 'AB':
                        self.close_release(side, 'NORMAL_COMPLETION')
                self.write('stages/' + stage + '-end.json', {'elapsed_seconds': time.monotonic() - start, 'ready': ready})
                self.stage_deadline = None
                return
            time.sleep(0.15)
        raise ValueError('Native paired ' + stage + ' deadline exceeded')

    def close_release(self, side, reason):
        pid = self.owned[side]['child']['pid']
        self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side + ' pid=' + str(pid) + ' stage=RUNTIME_CLOSE reason=' + reason + '\n')

    def maps(self, side, label):
        self.guard(side)
        pid = self.owned[side]['child']['pid']
        raw = self.shell(side, 'cat /proc/' + str(pid) + '/maps')
        (self.output / (side + '.' + label + '.maps')).write_bytes(raw)
        paths = actionbag_verify_maps(raw.decode(), self.prefix[side], self.files)
        hashes = {}
        # Query only providers actually observed in this owned process; no system inventory.
        raw_hashes = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(p) for p in paths), 15).decode()
        for line in raw_hashes.splitlines():
            match = re.fullmatch(r'([0-9a-f]{64})  (/.+)', line)
            if not match or match[2] in hashes:
                raise OSError('Mapped-provider hash capture malformed')
            hashes[match[2]] = match[1]
        if set(hashes) != set(paths):
            raise OSError('Mapped-provider capture incomplete')
        for path, digest in hashes.items():
            expected = PLATFORM.get(path)
            if path.startswith(self.prefix[side] + '/'):
                expected = self.files[path[len(self.prefix[side]) + 1:]]
            if expected is not None and digest != expected:
                raise ValueError('Mapped provider changed: ' + path)
        self.write(side + '.' + label + '.providers.json', hashes)

    def wait_exit(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            statuses = {}
            for side in 'AB':
                p = self.prefix[side]
                status = self.shell(side, 'if test -f ' + p + '/exit; then cat ' + p + '/exit; fi').decode().strip()
                statuses[side] = status
                if status:
                    expected = 'R25_CHILD_EXIT run=' + self.run + ' side=' + side + ' pid=' + str(self.owned[side]['child']['pid']) + ' rc=0'
                    if status != expected:
                        raise ValueError('Executed child exit failure: ' + status)
            if all(statuses.values()):
                self.write('CHILD_EXITS.json', statuses)
                return
            time.sleep(0.2)
        raise ValueError('Owned child exit not observed within deadline')

    def collect(self):
        texts = {}
        for side in 'AB':
            p = self.prefix[side]
            for name in ('stdout', 'stderr', 'supervisor.stdout', 'supervisor.stderr', 'exit'):
                size = int(self.shell(side, 'stat -c %s ' + p + '/' + name).decode().strip())
                if not 0 <= size <= 16 * 1024 * 1024:
                    raise ValueError('Complete evidence exceeds declared size bound')
                target = self.output / (side + '.raw.' + name + '.log')
                raw = self.command(side, ['file', 'recv', p + '/' + name, str(target)], 30)
                if b'transfer finish' not in raw.lower() or not target.is_file() or target.stat().st_size != size:
                    raise OSError('Raw log transfer incomplete')
                digest = self.shell(side, 'sha256sum ' + p + '/' + name).decode().split()[0]
                if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                    raise OSError('Raw log transfer hash mismatch')
            texts[side] = (self.output / (side + '.raw.stdout.log')).read_bytes().decode('utf-8') + '\n' + (self.output / (side + '.raw.stderr.log')).read_bytes().decode('utf-8')
        return texts

    def analyze(self, texts):
        pids = {s: self.owned[s]['child']['pid'] for s in 'AB'}
        oracle = REPO / 'tests/ros2_host/run_action_acceptance.py'
        tree = ast.parse(oracle.read_text(encoding='utf-8'))
        node = next(x for x in tree.body if isinstance(x, ast.FunctionDef) and x.name == 'analyze')
        namespace = {'re': re}
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(oracle), 'exec'), namespace)
        business = namespace['analyze'](texts, self.run, pids)
        native = actionbag_native_capture(texts, self.run, pids)
        self.write('window.json', {'run': self.run})
        locator = runpy.run_path(str(REPO / 'scripts/public_socket/analyze_v1_locators.py'), run_name='r25_locator_oracle')
        expected = {s: locator['derive']({'namespace': 'mdds.r25.action', 'domain': 201, 'udid': self.udids[s]}, 'AB'.index(s) + 1) for s in 'AB'}
        locators = locator['analyze'](self.output, expected)
        self.write('ORACLES.json', {'business': business, 'business_oracle_reuse': 'Unchanged pure Action function retains its historical Host scope label; these inputs come from the native owned children, native path proof below is separate', 'native': native, 'locators': locators})

    def cleanup(self, failed):
        result = {'directories_retained': True, 'reason': 'Explicit retention of exact fresh run evidence; persistent identity directories never touched', 'sides': {}}
        for side in self.owned:
            status = {'forced_signals': [], 'absent': False}
            try:
                if 'child' not in self.owned[side]:
                    raise OSError('Partial launch: child ownership unresolved; no signal or directory removal permitted')
                if failed and self.guard(side, absent_ok=True):
                    for name, content in [('control.ACTION_RELEASE', 'RELEASE ' + self.run + '\n'), ('control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side + ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=WINDOW_FAILED\n')]:
                        exists = self.shell(side, 'if test -e ' + self.prefix[side] + '/' + name + '; then printf 1; else printf 0; fi').decode().strip()
                        if exists == '0':
                            self.publish(side, name, content)
                for signal, budget in [(None, 10), ('TERM', 5), ('KILL', 2)]:
                    alive = self.guard(side, absent_ok=True)
                    if not alive:
                        break
                    if signal:
                        self.guard(side)
                        self.shell(side, 'kill -' + signal + ' ' + str(self.owned[side]['child']['pid']))
                        status['forced_signals'].append(signal)
                    end = time.monotonic() + budget
                    while time.monotonic() < end and self.guard(side, absent_ok=True):
                        time.sleep(0.2)
                child_absent = not self.guard(side, absent_ok=True)
                end = time.monotonic() + 3
                parent = self.snapshot(side, self.owned[side]['supervisor']['pid'])
                while parent is not None and time.monotonic() < end:
                    self.guard(side, role='supervisor')
                    time.sleep(0.2)
                    parent = self.snapshot(side, self.owned[side]['supervisor']['pid'])
                status['supervisor_snapshot'] = parent
                parent_absent = parent is None
                status['absent'] = child_absent and parent_absent
                if not status['absent']:
                    raise OSError('Owned process cleanup unconfirmed')
                inventory = self.verify_files(side)
                raw = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(n) for n in PLATFORM)).decode()
                if {line.split()[1]: line.split()[0] for line in raw.splitlines()} != PLATFORM:
                    raise OSError('Primary platform provider postflight mismatch')
                if not failed and not self.errors and not self.blocked:
                    p = self.prefix[side]
                    special = self.shell(side, 'cd ' + p + ' && find . ! -type f ! -type d -print').decode().strip()
                    if special:
                        raise OSError('Unknown remote object prevents cleanup')
                    directories = self.shell(side, 'cd ' + p + ' && find . -type d -print').decode().splitlines()
                    if not directories or '.' not in directories:
                        raise OSError('Owned directory inventory incomplete')
                    for name in [*inventory, *(x[2:] for x in directories if x != '.')]:
                        if not re.fullmatch(r'[A-Za-z0-9_./+\-]+', name) or '..' in PurePosixPath(name).parts or PurePosixPath(name).is_absolute():
                            raise OSError('Noncanonical remote cleanup member')
                    names = sorted(inventory)
                    for start in range(0, len(names), 20):
                        self.root_guard(side)
                        self.shell(side, 'rm -- ' + ' '.join(shlex.quote(p + '/' + n) for n in names[start:start + 20]))
                    for directory in sorted((x[2:] for x in directories if x != '.'), key=lambda x: (x.count('/'), x), reverse=True):
                        self.root_guard(side)
                        self.shell(side, 'rmdir ' + shlex.quote(p + '/' + directory))
                    self.root_guard(side)
                    self.shell(side, 'rmdir ' + p + ' && test ! -e ' + p)
                    status['directory_removed'] = True
            except (OSError, ValueError) as exc:
                status['error'] = str(exc)
                self.blocked.append('CLEANUP ' + side + ': ' + str(exc))
            if status['forced_signals']:
                self.errors.append('Forced owned termination: ' + side)
            result['sides'][side] = status
        self.write('CLEANUP.json', result)
        for side in self.launch_pending:
            if side not in self.owned:
                self.blocked.append('LAUNCH ' + side + ': dispatch result unresolved; exact pending receipt retained, no signal issued')
        result['directories_retained'] = any(not x.get('directory_removed', False) for x in result['sides'].values()) or any(s not in self.owned for s in self.roots)
        self.write('CLEANUP.json', result)

    def execute(self):
        try:
            self.deploy()
            for side in 'AB':
                self.launch(side)
            self.barrier('LISTENER', 60)
            self.barrier('BUSINESS', 55)
            self.barrier('CLOSE', 60)
            self.stage = 'EXIT'
            self.wait_exit(20)
            self.analyze(self.collect())
        except ValueError as exc:
            self.errors.append(self.stage + ': ' + str(exc))
        except (OSError, subprocess.SubprocessError) as exc:
            self.blocked.append(self.stage + ': ' + str(exc))
        except (Exception, KeyboardInterrupt) as exc:
            self.blocked.append(self.stage + ': unexpected capture/control exception ' + repr(exc))
        finally:
            self.stage_deadline = None
            self.cleanup(bool(self.errors or self.blocked))
            if self.errors or self.blocked:
                try:
                    if self.owned:
                        self.collect()
                except (OSError, ValueError) as exc:
                    self.blocked.append('FAILURE_CAPTURE: ' + str(exc))
            result = {'run': self.run, 'result': 'FAIL' if self.errors else 'BLOCKED' if self.blocked else 'PASS', 'failures': self.errors, 'blocked': self.blocked,
                'scope': 'BESPOKE_NATIVE_ACTION_READINESS_QUALIFIED_PAIR_NOT_ORDINARY_CLI', 'sdk_epoch': 'NOT_PROVEN',
                'identity_mutations': 0, 'device_window': self.prefix, 'source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
            self.write('RESULT.json', result)
        return 0 if result['result'] == 'PASS' else 1


def actionbag_native_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='mode', required=True)
    prepare = commands.add_parser('prepare')
    prepare.add_argument('--inputs-root', type=Path, required=True)
    prepare.add_argument('--identity-build', type=Path, required=True)
    prepare.add_argument('--query-build', type=Path, required=True)
    prepare.add_argument('--output', type=Path, required=True)
    routes = commands.add_parser('routes', help='Local adapter for the audited independent V3 fresh peer-query evidence layout')
    routes.add_argument('--peer-evidence', type=Path, required=True)
    routes.add_argument('--output', type=Path, required=True)
    run = commands.add_parser('run')
    run.add_argument('--package', type=Path, required=True)
    run.add_argument('--routes', type=Path, required=True, help='Private fresh peer receipt JSON; sides A/B contain target, host_exit, remote_exit, stdout_file/sha256 and udid_file/sha256; observed_utc UTC')
    run.add_argument('--identity-result', type=Path, required=True)
    run.add_argument('--output', type=Path, required=True)
    run.add_argument('--hdc', type=Path, default=Path(HDCPATH))
    args = parser.parse_args(argv)
    if args.mode == 'prepare':
        actionbag_prepare_package(args)
        return 0
    if args.mode == 'routes':
        actionbag_peer_receipt(args)
        return 0
    if str(args.hdc.resolve()).lower() != str(Path(HDCPATH).resolve()).lower():
        parser.error('Only the selected SDK23 HDC is supported')
    runner = ActionbagNativeRunner.__new__(ActionbagNativeRunner)
    try:
        runner.__init__(args)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        if getattr(runner, 'owns_output', False):
            path = args.output / 'INPUT_FAILURE.json'
            if not path.exists():
                path.write_text(json.dumps({'result': 'BLOCKED', 'stage': 'INPUTS', 'reason': str(exc), 'device_execution': 'NOT_DISPATCHED_BY_INPUT_VALIDATION'}, indent=2) + '\n')
        raise
    return runner.execute()


if __name__ == '__main__':
    raise SystemExit(actionbag_native_main())
