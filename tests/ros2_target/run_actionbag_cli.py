"""Fixed ordinary CLI windows; local prepare/analyze, explicitly selected paired run. No provisioning."""
import argparse
import ast
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
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

import yaml

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[2]
ROOT = REPO.parents[2]
NATIVE_PATH = Path(__file__).with_name('run_actionbag_native.py')
NATIVE_SHA = 'f9fa38d1a1629ccd976b5ba57f36fcea205cc03a544276eee01c3c7977ac6712'
if hashlib.sha256(NATIVE_PATH.read_bytes()).hexdigest() != NATIVE_SHA:
    raise RuntimeError('Reviewed native pure helper source changed')
_SPEC = importlib.util.spec_from_file_location('ordinary_cli_frozen_native', NATIVE_PATH)
NATIVE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(NATIVE)
CASES = ('imports_cdr_pair', 'single_node_pair', 'node_graph_AB', 'node_graph_BA',
         'topic_graph_AB', 'topic_graph_BA', 'topic_pub_AB', 'topic_pub_BA',
         'topic_echo_AB', 'topic_echo_BA', 'action_send_goal_AB', 'action_send_goal_BA')
PLAN_SHA = 'b423dfd6949e45af4dffce4cdd9487dc018d0868464902556640efe4d533cebb'
SOURCE_SHA = 'd8a2c2b2f409de9f9cccaabb93680f4f4b2b55bd6730fd7e0bfa9c6e0e5c616d'
ADDENDUM_SHA = 'b6b2115f740a5da413734b3a0b16b892dd7afcd29393acdcf2a54230af92e3b0'
CLOCK_SHA = 'bd5d3491c23e19b06b00e98edcd3a60515c26fe5682dace3586fcb87148746a6'
QUALIFICATION_SHA = '173911172ddf24fe8fa50a1f3be26aca0be8b7d08bddc3f53433db800389a8b6'
PACKAGE_EDGE_SHA = 'ddaaa95b902a322439af7a1e51552bd27ba5fda6efa9df405faf250d4a446b32'
OWNED_PYTHON_SHA = '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25'
PROBE = REPO / 'integrations/ros2/target/actionbag_runtime_probe.py'
RUNTIME = '/data/python312-rk3588a-verify-c646982b346f/usr'
FIBONACCI = [0, 1, 1, 2, 3, 5, 8]
ERROR_PAIR = ('[RTPS_PARTICIPANT Error] No unicast locators to create unique flows -> Function createAndAssociateReceiverswithEndpoint',
              '[DATA_READER Error] Problem creating associated Reader -> Function enable')


def ordinary_unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('Duplicate JSON key: ' + key)
        result[key] = value
    return result


def ordinary_records(text, run, side, pid, role=None):
    rows = []
    events = {'NODE_READY', 'ENTITY_READY', 'DATA', 'PUBLISH', 'GOAL_ACCEPTED', 'FEEDBACK', 'RESULT_INTENT',
              'IMPORTS_CDR_READY', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL', 'FAIL'}
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '): continue
        row = json.loads(line[len('R25_PROBE '):], object_pairs_hook=ordinary_unique)
        if not isinstance(row, dict) or any(type(row.get(k)) is not int for k in ('schema', 'pid', 'seq')) or any(row.get(k) != v for k, v in
            {'schema': 1, 'run': run, 'side': side, 'pid': pid, 'seq': len(rows) + 1}.items()) or row.get('event') not in events:
            raise ValueError('Probe record schema/identity/order')
        if row.get('role') not in ('imports-cdr', 'hold-node', 'publisher', 'subscriber', 'action-server') or (role and row['role'] != role):
            raise ValueError('Probe role mismatch')
        if row['event'] == 'FAIL': raise ValueError('Fixture reported failure: ' + str(row.get('reason')))
        rows.append(row)
    return rows


def ordinary_cli_argv(case, run, side, prefix, plan):
    if case not in CASES or not re.fullmatch('[0-9a-f]{16}', run) or side not in ('A', 'B'):
        raise ValueError('Unsupported case/run/side; action-list is BLOCKED')
    definition = next(row for row in plan['ordered_cases'] if row['id'] == case)
    role = definition.get('boards', {}).get(side, definition.get('fixture_role') if side == definition.get('fixture_side') else None)
    values = {'${P}': prefix + '/package', '${W}': prefix, '${run}': run, '${side}': side, '${role}': role or ''}
    argv = list(definition['fixture_argv_template'] if role else definition['cli_argv_template'])
    for key, value in values.items(): argv = [arg.replace(key, value) for arg in argv]
    if any('${' in arg for arg in argv): raise ValueError('Unexpanded fixed case argument')
    return argv, role


def ordinary_yaml(text, field):
    try:
        node = yaml.compose(text, Loader=yaml.SafeLoader)
        # Inspect the mapping before construction can overwrite duplicate keys.
        # Every accepted stock message here has exactly one named field.
        if (not isinstance(node, yaml.MappingNode) or len(node.value) != 1 or
            not isinstance(node.value[0][0], yaml.ScalarNode) or
            node.value[0][0].tag != 'tag:yaml.org,2002:str' or node.value[0][0].value != field):
            raise ValueError('Expected one unique YAML field: ' + field)
        return yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ValueError('Malformed actual message YAML') from error


def ordinary_business(case, run, texts, pids, plan, final=False):
    definition = next(row for row in plan['ordered_cases'] if row['id'] == case)
    records = {}
    for side in 'AB':
        role = definition.get('boards', {}).get(side, definition.get('fixture_role') if side == definition.get('fixture_side') else None)
        records[side] = ordinary_records(texts[side], run, side, pids[side], role)
        if role is None and records[side]: raise ValueError('Stock CLI unexpectedly emitted fixture records')
    if case == 'imports_cdr_pair':
        for side in 'AB':
            ready = [r for r in records[side] if r['event'] == 'IMPORTS_CDR_READY']
            if len(ready) != 1: return False
            row = ready[0]
            expected = {'Goal': ('order', 6), 'Result': ('sequence', FIBONACCI),
                        'Feedback': ('sequence', FIBONACCI[:4]), 'String': ('data', 'payload_' + run)}
            if row.get('node_created') is not False or row.get('soabi') != 'cpython-312-aarch64-linux-ohos':
                raise ValueError('NoNode ABI/role mismatch')
            if {r['type'] for r in row['cdr']} != set(expected) or len(row['cdr']) != 4:
                raise ValueError('Incomplete CDR membership')
            for cdr in row['cdr']:
                data = base64.b64decode(cdr['base64'], validate=True)
                if (cdr['field'], cdr['value']) != expected[cdr['type']] or len(data) <= 4 or len(data) != cdr['bytes'] or hashlib.sha256(data).hexdigest() != cdr['sha256'] or data[:4].hex() != cdr['encapsulation'] or data[:4] not in (b'\0\0\0\0', b'\0\1\0\0'):
                    raise ValueError('CDR bytes/value/encapsulation mismatch')
            if any(r['event'] in ('NODE_READY', 'ENTITY_READY') for r in records[side]):
                raise ValueError('NoNode fixture initialized a Node')
        return True
    if case == 'single_node_pair':
        return all(len([r for r in records[s] if r['event'] == 'NODE_READY' and r.get('name') == '/ab_' + run + '_' + s]) == 1 and
                   len([r for r in records[s] if r['event'] == 'ENTITY_READY' and r.get('entity') == 'hold-node']) == 1 for s in 'AB')
    cli, fixture = definition['cli_side'], definition['fixture_side']
    if not any(r['event'] == 'ENTITY_READY' for r in records[fixture]): return False
    text = texts[cli].replace('\r\n', '\n')
    if re.search(r'not available|was rejected|Timed out|Failed to|Goal canceled|status: (?:ABORTED|CANCELED|UNKNOWN)', text, re.I):
        raise ValueError('Actual stock command failure/timeout')
    if '_graph_' in case:
        if not final: return True  # Stock graph text appears only after real context shutdown.
        expected = '/ab_' + run + '_' + fixture if case.startswith('node_') else '/actionbag_' + run + ' [std_msgs/msg/String]'
        if text.splitlines().count(expected) != 1 or 'WARNING:' in text:
            raise ValueError('Expected exact post-close graph line missing/duplicate')
        return True
    payload = 'payload_' + run
    if case.startswith('topic_pub_'):
        pubs = re.findall(r'^publishing #[^\n]*$', text, re.M)
        data = [r for r in records[fixture] if r['event'] == 'DATA']
        if any(r.get('data') != payload for r in data): raise ValueError('Incorrect fixture payload')
        if not pubs or not data: return False
        if pubs != ["publishing #1: std_msgs.msg.String(data='" + payload + "')"] or not any(type(r.get('matched')) is int and r['matched'] >= 1 for r in data):
            raise ValueError('Publication/reception/match oracle failed')
        return True
    if case.startswith('topic_echo_'):
        data = re.findall(r'^data:[^\n]*\n---$', text, re.M)
        all_data = re.findall(r'^data:[^\n]*$', text, re.M)
        pubs = [r for r in records[fixture] if r['event'] == 'PUBLISH']
        if any(r.get('data') != payload for r in pubs): raise ValueError('Incorrect publisher payload')
        if not data or not pubs: return False
        if len(data) != 1 or len(all_data) != 1 or ordinary_yaml(all_data[0], 'data') != {'data': payload}:
            raise ValueError('Exact CLI echo/matched publisher missing')
        if not any(type(r.get('matched')) is int and r['matched'] >= 1 for r in pubs):
            # Board snapshots are read sequentially. A valid CLI receipt can
            # precede the next publisher snapshot containing its match event.
            # Only incomplete live evidence may wait within the existing budget.
            if final or any(type(r.get('matched')) is not int or r['matched'] != 0 for r in pubs):
                raise ValueError('Exact CLI echo/matched publisher missing')
            return False
        return True
    accepted = re.findall(r'^Goal accepted with ID: ([0-9a-f]{32})$', text, re.M)
    if accepted and (len(accepted) != 1 or int(accepted[0], 16) == 0): raise ValueError('Accepted UUID invalid/duplicate')
    blocks = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if line not in ('Feedback:', 'Result:'): continue
        end = index + 1
        while end < len(lines) and lines[end] != '': end += 1
        raw = lines[index + 1:end]
        if not raw or not raw[0].startswith('    '): raise ValueError('Malformed stock action YAML block')
        raw[0] = raw[0][4:]  # Stock format indents only the first message_to_yaml line.
        value = ordinary_yaml('\n'.join(raw), 'sequence')
        if not isinstance(value, dict) or set(value) != {'sequence'} or not isinstance(value['sequence'], list) or any(type(x) is not int for x in value['sequence']):
            raise ValueError('Unexpected action YAML fields/types')
        blocks.append((line, value['sequence']))
    expected_blocks = [('Feedback:', FIBONACCI[:n]) for n in range(3, 8)] + [('Result:', FIBONACCI)]
    for actual, expected in zip(blocks, expected_blocks):
        if actual != expected: raise ValueError('Wrong/out-of-order action feedback/result')
    if len(blocks) > len(expected_blocks): raise ValueError('Extra action feedback/result')
    goals = [r for r in records[fixture] if r['event'] == 'GOAL_ACCEPTED']
    feedback = [r for r in records[fixture] if r['event'] == 'FEEDBACK']
    results = [r for r in records[fixture] if r['event'] == 'RESULT_INTENT']
    if not accepted or len(blocks) < 6 or not results: return False
    if (len(goals) != 1 or goals[0].get('uuid') != accepted[0] or goals[0].get('order') != 6 or
        [r.get('sequence') for r in feedback] != [FIBONACCI[:n] for n in range(3, 8)] or
        len(results) != 1 or results[0].get('sequence') != FIBONACCI or results[0].get('status') != 'SUCCEEDED' or
        any(r.get('uuid') != accepted[0] for r in feedback + results) or
        re.findall(r'^Goal finished with status: .*$', text, re.M) != ['Goal finished with status: SUCCEEDED']):
        raise ValueError('Actual client/server UUID/result/status mismatch')
    return True


def ordinary_error_observations(stdout, stderr, run, side, pid, role, communicating, files):
    lines = stdout.splitlines()
    errors = []
    for index, raw in enumerate(lines):
        line = re.sub(r'\x1b\[[0-9;]*m', '', raw)
        if re.search(r'\b(?:error|exception|failed)\b', line, re.I):
            errors.append((index, raw, line))
    for raw in stderr.splitlines():
        if re.search(r'\b(?:error|exception|failed)\b', re.sub(r'\x1b\[[0-9;]*m', '', raw), re.I):
            raise ValueError('Unexpected stderr diagnostic: ' + raw)
    if not communicating:
        if errors: raise ValueError('Unexpected noNode diagnostic: ' + errors[0][1])
        if NATIVE.actionbag_fields(stdout, 'FDDST_ENDPOINT') or NATIVE.actionbag_fields(stderr, 'FDDST_ENDPOINT'):
            raise ValueError('Unexpected noNode endpoint activity')
        return {'expected_pairs': 0, 'actual_pairs': 0}
    if files.get('lib/librmw_fastrtps_shared_cpp.so') != '0fc8f4a214849f4a5abe15f66baa23404448d0cebaf972607d40578200dcf369' or files.get('lib/librmw_fastrtps_cpp.so') != '6b637fe1292e664507811041e5576bad9291c9bae60b87237f7b92fa88476681':
        raise OSError('BLOCKED_TRIAGE: optional graph-context binary binding changed')
    if not errors: raise OSError('OBSERVATION_DRIFT: zero optional-flow pair; context requires independent triage')
    if len(errors) != 2 or any(not re.fullmatch(r'(?:[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} )?' + re.escape(signature), errors[i][2]) for i, signature in enumerate(ERROR_PAIR)):
        raise ValueError('Unknown/additional/reordered native Error records')
    connect = next((i for i, line in enumerate(lines) if line.startswith('ROS_CONNECT_ALLOWED ')), None)
    if role:
        records = ordinary_records(stdout, run, side, pid, role)
        first = next((i for i, line in enumerate(lines) if line.startswith('R25_PROBE ') and json.loads(line[10:]).get('event') == 'NODE_READY'), None)
    else:
        first = next((i for i, line in enumerate(lines) if line.startswith(('Waiting for ', 'publisher: beginning loop', 'data:', 'ROS_RUNTIME_CLOSE_READY '))), None)
    # The fixed endpoint guard writes stderr. This context is checked without
    # assigning its records a causal position among stdout phase observations.
    graph = [r for r in NATIVE.actionbag_fields(stderr, 'FDDST_ENDPOINT') if r.get('topic') == 'ros_discovery_info']
    if connect is None or first is None or not connect < errors[0][0] < errors[1][0] < first:
        raise OSError('BLOCKED_LOG_PHASE_TRIAGE: exact pair outside conservative observed stdout interval; async Logger does not prove causal order')
    if [r.get('role') for r in graph] != ['writer', 'reader', 'reader'] or any(r.get('data_sharing') != 'OFF' for r in graph):
        raise ValueError('Optional-flow graph context/retry evidence differs')
    return {'expected_pairs': 1, 'actual_pairs': 1, 'raw_lines': [r[1] for r in errors],
            'phase': 'CONSERVATIVE_OBSERVED_STDOUT_ORDER_NOT_CAUSAL_LOG_ORDER', 'unique_flow': 'NOT_PROVIDED', 'strict': 'UNTESTED'}


def ordinary_map_paths(text, package, files, external, communicating, derivative):
    paths, anonymous = set(), []
    for line in text.splitlines():
        fields = line.split(None, 5)
        if len(fields) < 5: raise ValueError('Malformed process maps')
        if len(fields) != 6: continue
        path = fields[5]
        if path.startswith('['): anonymous.append(path); continue
        if not path.startswith('/'):
            if '.so' in path: raise ValueError('Relative mapped library')
            continue
        if path.endswith(' (deleted)'): raise ValueError('Deleted actual mapping')
        if '.so' not in path and path != package + '/actionbag_python': continue
        if path.startswith(package + '/'):
            if path[len(package) + 1:] not in files: raise ValueError('Unselected package mapping: ' + path)
        else:
            name = PurePosixPath(path).name
            if ('libpython3.12' in name or 'libmdds_' in name or
                name.startswith(('libfastrtps.', 'libfastcdr.', 'librmw_', 'librcl.', 'libfastdds_dsoftbus_'))):
                raise ValueError('Project/owned provider mapped outside selected slice: ' + path)
            if path not in external: raise ValueError('Unselected external mapping: ' + path)
        paths.add(path)
    required = [package + '/actionbag_python', package + '/' + derivative]
    if communicating:
        required += [package + '/lib/' + n for n in ('libfastrtps.so.2.14', 'libfastcdr.so.2', 'librmw_fastrtps_cpp.so',
            'librmw_fastrtps_shared_cpp.so', 'libfastdds_dsoftbus_ros2_target_bridge.so', 'libmdds_actionbag_token_bridge.so', 'libc++_shared.so')]
        required.append('/system/lib64/platformsdk/libsoftbus_client.z.so')
    if any(p not in paths for p in required) or not any('/python/rclpy/_rclpy_pybind11.' in p for p in paths):
        raise ValueError('Required actual selected provider missing')
    return sorted(paths), anonymous


def ordinary_prepare(args):
    selection = json.loads(args.inputs.read_text(), object_pairs_hook=ordinary_unique)
    if set(selection) != {'schema', 'case', 'package', 'runtime', 'readiness', 'external', 'identity', 'routes',
                          'design_plan', 'source_evidence', 'provider_addendum', 'clock_addendum', 'qualification_design',
                          'package_provider_addendum', 'ordinary_freeze'} or selection['schema'] != 1 or selection['case'] not in CASES:
        raise ValueError('Unknown fixed qualification input schema/case')
    if args.output.exists() and not getattr(args, 'validate_only', False): raise ValueError('Fresh prepare output required')
    bindings, receipts = {}, {}
    for name in ('runtime', 'readiness', 'external', 'identity', 'routes', 'design_plan', 'source_evidence', 'provider_addendum',
                 'clock_addendum', 'qualification_design', 'package_provider_addendum', 'ordinary_freeze'):
        descriptor = selection[name]
        if set(descriptor) != {'path', 'sha256'} or not re.fullmatch('[0-9a-f]{64}', descriptor['sha256']): raise ValueError('Bad selected receipt')
        path = Path(descriptor['path']).resolve(strict=True)
        if hashlib.sha256(path.read_bytes()).hexdigest() != descriptor['sha256']: raise ValueError('Selected receipt changed: ' + name)
        bindings[str(path)] = descriptor['sha256']
        if name not in ('provider_addendum', 'clock_addendum', 'qualification_design', 'package_provider_addendum'):
            receipts[name] = json.loads(path.read_text(), object_pairs_hook=ordinary_unique)
    if (selection['design_plan']['sha256'] != PLAN_SHA or selection['source_evidence']['sha256'] != SOURCE_SHA or
        selection['provider_addendum']['sha256'] != ADDENDUM_SHA or selection['clock_addendum']['sha256'] != CLOCK_SHA or
        selection['qualification_design']['sha256'] != QUALIFICATION_SHA or selection['package_provider_addendum']['sha256'] != PACKAGE_EDGE_SHA):
        raise ValueError('Unreviewed design selection')
    package = Path(selection['package']).resolve(strict=True)
    manifest_file = package / 'LAUNCHER_PACKAGE.json'
    manifest_sha = hashlib.sha256(manifest_file.read_bytes()).hexdigest()
    manifest = json.loads(manifest_file.read_text(), object_pairs_hook=ordinary_unique)
    files = {**manifest['files'], 'LAUNCHER_PACKAGE.json': manifest_sha}
    if (receipts['runtime'].get('eligible_for_deployment') is not True or
        receipts['runtime'].get('eligible_for_bounded_acceptance') is not True or receipts['runtime'].get('production_verified') is not False or
        receipts['runtime'].get('package_inventory') != files):
        raise ValueError('Runtime selection remains BLOCKED or differs from final seal')
    if (receipts['runtime'].get('status') != 'ELIGIBLE_FOR_BOUNDED_ACCEPTANCE_NOT_RUNTIME_VERIFIED' or
        receipts['runtime'].get('eligibility_scope') != 'ORDINARY_FIXED_CASE_QUALIFICATION_ONLY' or
        any(receipts['runtime'].get(k) is not False for k in ('communication_verified', 'target_imported', 'target_runtime_provider_rechecked', 'device_tested'))):
        raise ValueError('Only the bounded first-acceptance qualification is permitted')
    for member, digest in {
        'lib/libfastrtps.so.2.14': '40261a300a35f1974381faeacf56988f3131540b18e06522e602dd5a610975fd',
        'lib/libfastcdr.so.2': '113f8ef51c667c29c729dafc5fa20d0038688c217cd5bea99d4f13e2661d6b00',
        'lib/librmw_fastrtps_shared_cpp.so': '0fc8f4a214849f4a5abe15f66baa23404448d0cebaf972607d40578200dcf369',
        'lib/librmw_fastrtps_cpp.so': '6b637fe1292e664507811041e5576bad9291c9bae60b87237f7b92fa88476681',
    }.items():
        if files.get(member) != digest: raise ValueError('Unqualified project provider: ' + member)
    for name, expected in files.items():
        relative = PurePosixPath(name)
        if (not name or relative.is_absolute() or relative.as_posix() != name or '\\' in name or ':' in name or
            any(p in ('.', '..') or p.endswith((' ', '.')) or PureWindowsPath(p).is_reserved() for p in relative.parts)):
            raise ValueError('Noncanonical package name')
        path = package / name
        if path.is_symlink() or path.resolve(strict=True) != path or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError('Changed package file: ' + name)
    if any(p.is_symlink() for p in package.rglob('*')) or {p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file()} != set(files):
        raise ValueError('Package immutable inventory mismatch')
    sources = [Path(__file__), Path(__file__).with_name('test_actionbag_cli.py'), PROBE, NATIVE_PATH]
    for path in sources: bindings[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
    if files.get('scripts/actionbag_runtime_probe.py') != bindings[str(PROBE.resolve())]: raise ValueError('New fixture not in final composed seal')
    ready = receipts['readiness']
    if ready.get('schema') != 1 or ready.get('status') != 'READY_FOR_ORDINARY_QUALIFICATION' or ready.get('original_libpython_sha256') != '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a':
        raise ValueError('Qualified fix2 readiness missing')
    if ready['composed_runtime_sha256'] != selection['runtime']['sha256'] or ready['composed_launcher_manifest_sha256'] != manifest_sha or ready['fixture_sha256'] != files['scripts/actionbag_runtime_probe.py'] or ready['external_closure_sha256'] != selection['external']['sha256'] or ready['ordinary_source_freeze_sha256'] != selection['ordinary_freeze']['sha256']:
        raise ValueError('Readiness/final composition binding mismatch')
    if files.get('actionbag_python') != ready['launcher_elf_sha256'] or files.get(ready['owned_libpython']['member']) != ready['owned_libpython']['sha256']:
        raise ValueError('Selected launcher/owned Python provider differs')
    if ready['owned_libpython'] != {'member': 'lib/libpython3.12.so.1.0', 'sha256': OWNED_PYTHON_SHA} or {Path(p).name for p in ready['launcher_source_sha256']} != {'actionbag_python_entry.cpp', 'build_actionbag_python.py'}:
        raise ValueError('Exact launcher two-source/owned-provider contract required')
    for path, sha in ready['launcher_source_sha256'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != sha: raise ValueError('Launcher source freeze changed')
        bindings[path] = sha
    for name in ('input_launcher_manifest', 'independent_initial_result', 'independent_review', 'postreview_test', 'actual_target_noNode_result'):
        row = ready[name]
        if name in ('independent_review', 'postreview_test', 'actual_target_noNode_result') and row.get('status') != 'PASS':
            raise ValueError('Independent fix2 gate missing: ' + name)
        if name == 'actual_target_noNode_result' and row.get('bothboards6cases') is not True:
            raise ValueError('Both-board six-case noNode fix2 gate missing')
        path = Path(row['path']).resolve(strict=True)
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']: raise ValueError('Readiness evidence changed')
        bindings[str(path)] = row['sha256']
    old = json.loads(Path(ready['input_launcher_manifest']['path']).read_text(), object_pairs_hook=ordinary_unique)
    if old['files'].get('actionbag_python') != files['actionbag_python'] or old['files'].get(ready['owned_libpython']['member']) != ready['owned_libpython']['sha256']:
        raise ValueError('Composition did not preserve qualified launcher/provider bytes')
    composition = json.loads((package / 'COMPOSITION.json').read_text(), object_pairs_hook=ordinary_unique)
    provider = composition['python_provider']
    if (provider != manifest['python_provider'] or provider != old['python_provider'] or
        (provider['baseline_sha256'], provider['owned_sha256'], provider['old_flags_1'], provider['flags_1'], provider['changed_byte_offsets']) !=
        (ready['original_libpython_sha256'], OWNED_PYTHON_SHA, 1, 3, [4594312]) or provider['canonical_runtime_modified'] is not False):
        raise ValueError('Owned Python derivative provenance differs')
    closure_path = Path(selection['runtime']['path']).resolve().parent / 'ELF_CLOSURE.json'
    closure_sha = receipts['runtime']['report_sha256']['ELF_CLOSURE.json']
    if hashlib.sha256(closure_path.read_bytes()).hexdigest() != closure_sha: raise ValueError('Composed actual ELF evidence changed')
    bindings[str(closure_path)] = closure_sha
    closure = json.loads(closure_path.read_text(), object_pairs_hook=ordinary_unique)
    owned_elf = closure['objects']['lib/libpython3.12.so.1.0']
    if owned_elf['sha256'] != OWNED_PYTHON_SHA or owned_elf['soname'] != ['libpython3.12.so.1.0'] or owned_elf['needed'] != ['libc.so']:
        raise ValueError('Actual owned Python SONAME/NEEDED evidence differs')
    selected_inputs = {str(Path(p).resolve()): sha for p, sha in composition['selected_inputs'].items()}
    if selected_inputs.get(str(Path(ready['input_launcher_manifest']['path']).resolve())) != ready['input_launcher_manifest']['sha256']:
        raise ValueError('Final composition does not bind qualified launcher input manifest')
    frozen = receipts['ordinary_freeze']
    expected_sources = {p.relative_to(REPO).as_posix(): bindings[str(p.resolve())] for p in sources if p != NATIVE_PATH}
    if ({row['path']: row['sha256'] for row in frozen['files']} != expected_sources or len(frozen['files']) != 3 or
        Path(frozen['source']).resolve() != PROBE.resolve() or frozen['source_sha256'] != bindings[str(PROBE.resolve())]):
        raise ValueError('Exact three-source ordinary freeze required')
    for row in frozen['files']:
        path = REPO / row['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']: raise ValueError('Ordinary source freeze changed')
    external = receipts['external']
    if external.get('schema') != 1 or external.get('selected_package_manifest_sha256') != selection['runtime']['sha256'] or external.get('selected_launcher_manifest_sha256') != manifest_sha or external.get('source_freeze_sha256') != selection['ordinary_freeze']['sha256'] or not re.fullmatch('[0-9a-f]{64}', external.get('independent_review_result_sha256', '')):
        raise ValueError('External provider closure not independently bound to selected package/source')
    for side in 'AB':
        device = external['devices'][side]
        if device['physical_side'] != side or device['device_alias'] != NATIVE.TARGETS[side]: raise ValueError('External physical device mismatch')
        for field in ('device_identity_receipt_sha256', 'loader_search_configuration_sha256'):
            if not re.fullmatch('[0-9a-f]{64}', device[field]): raise ValueError('External identity/search proof missing')
        if datetime.fromisoformat(device['captured_at_utc']).utcoffset() is None: raise ValueError('External capture UTC required')
        providers = {row['canonical_absolute_path']: row for row in device['providers']}
        if len(providers) != len(device['providers']): raise ValueError('Duplicate external provider')
        if any('libpython3.12' in PurePosixPath(p).name for p in providers): raise ValueError('Canonical/alternate Python is not an external loaded provider')
        owned_libc = closure['routes']['lib/libpython3.12.so.1.0']['libc.so']['external']
        libc = providers.get(owned_libc['target_path'])
        if libc is None or libc['sha256'] != owned_libc['sha256'] or libc['sha256'] != 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86' or libc['needed']:
            raise ValueError('Owned Python recursive libc provider closure is incomplete')
        if any(edge['consumer_canonical_path'] not in providers for edge in device['needed_edges']):
            raise ValueError('Unknown external edge consumer')
        for path, row in providers.items():
            if not path.startswith('/') or '..' in PurePosixPath(path).parts or path.endswith(' (deleted)') or row['elf_machine'] != 'AArch64' or type(row['size']) is not int or row['size'] <= 0 or not re.fullmatch('[0-9a-f]{64}', row['sha256']) or not re.fullmatch('[0-9a-f]{64}', row['origin_package_or_platform_receipt_sha256']):
                raise ValueError('Malformed external ELF provider')
            edges = [edge for edge in device['needed_edges'] if edge['consumer_canonical_path'] == path]
            if {edge['needed_name'] for edge in edges} != set(row['needed']) or len(edges) != len(row['needed']): raise ValueError('Incomplete actual external NEEDED edges')
            for edge in edges:
                if 'package_provider' in edge:
                    if set(edge) != {'consumer_canonical_path', 'needed_name', 'package_provider'} or edge['needed_name'] != 'libpython3.12.so.1.0' or edge['package_provider'] != {
                        'member': 'lib/libpython3.12.so.1.0', 'sha256': OWNED_PYTHON_SHA, 'soname': 'libpython3.12.so.1.0'}:
                        raise ValueError('Only the exact owned-Python package-provider edge is permitted')
                    continue
                if set(edge) not in ({'consumer_canonical_path', 'needed_name', 'provider_canonical_path', 'provider_sha256'},
                                     {'consumer_canonical_path', 'needed_name', 'provider_canonical_path', 'provider_sha256', 'alias_evidence_sha256'}):
                    raise ValueError('Unknown or mixed external provider edge')
                provider = providers.get(edge['provider_canonical_path'])
                if provider is None or provider['sha256'] != edge['provider_sha256']: raise ValueError('Unknown external dependency provider')
                matches = provider['soname'] == edge['needed_name'] or PurePosixPath(edge['provider_canonical_path']).name == edge['needed_name']
                if not matches and not re.fullmatch('[0-9a-f]{64}', edge.get('alias_evidence_sha256', '')):
                    raise ValueError('Unproven native loader alias')
    output = {'schema': 1, 'scope': 'FIXED_ORDINARY_CASE_PREPARED_NO_DEVICE_DISPATCH', 'run': secrets.token_hex(8),
        'case': selection['case'], 'package': str(package), 'files': files, 'inputs': selection,
        'bindings': bindings, 'case_definition': next(r for r in receipts['design_plan']['ordered_cases'] if r['id'] == selection['case']),
        'selection_file': {'path': str(args.inputs.resolve()), 'sha256': hashlib.sha256(args.inputs.read_bytes()).hexdigest()},
        'deadlines_seconds': receipts['design_plan']['deadlines_seconds'], 'sdk_epoch': 'NOT_PROVEN'}
    if getattr(args, 'validate_only', False): return output
    args.output.mkdir(parents=True)
    (args.output / 'PLAN.json').write_text(json.dumps(output, indent=2) + '\n')
    # The same constructor validates fixed identities, freshness and paired
    # public-directory evidence without calling any device primitive.
    validation = argparse.Namespace(prepared=args.output / 'PLAN.json', output=args.output / 'validation', validation_probe=True)
    OrdinaryCliRunner(validation)
    print('ORDINARY_PREPARED_NO_DEVICE ' + str(args.output / 'PLAN.json'))
    return 0


class OrdinaryCliRunner(NATIVE.ActionbagNativeRunner):
    # Only the reviewed native recording/ownership/capture primitives are inherited.
    # Every package/lifecycle/business/cleanup operation below replaces native Action control.
    def __init__(self, args):
        self.args, self.output = args, args.output.resolve()
        self.owns_output = False
        if self.output.exists(): raise ValueError('Fresh evidence output required')
        self.output.mkdir(parents=True)
        self.owns_output = True
        self.plan = json.loads(args.prepared.read_text(), object_pairs_hook=ordinary_unique)
        if self.plan['schema'] != 1 or self.plan['scope'] != 'FIXED_ORDINARY_CASE_PREPARED_NO_DEVICE_DISPATCH' or self.plan['case'] not in CASES or not re.fullmatch('[0-9a-f]{16}', self.plan['run']):
            raise ValueError('Invalid prepared fixed window')
        selected = self.plan['selection_file']
        if hashlib.sha256(Path(selected['path']).read_bytes()).hexdigest() != selected['sha256']:
            raise ValueError('Prepared selection changed')
        if not getattr(args, 'validation_probe', False):
            checked = ordinary_prepare(argparse.Namespace(inputs=Path(selected['path']), output=self.output, validate_only=True))
            if any(self.plan[k] != checked[k] for k in checked if k != 'run'):
                raise ValueError('Prepared plan differs from independently revalidated fixed inputs')
        for path, sha in self.plan['bindings'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != sha: raise ValueError('Prepared input/source changed: ' + path)
        self.run, self.case, self.package, self.files = self.plan['run'], self.plan['case'], Path(self.plan['package']), self.plan['files']
        manifest = self.package / 'LAUNCHER_PACKAGE.json'
        actual = json.loads(manifest.read_text(), object_pairs_hook=ordinary_unique)['files']
        actual['LAUNCHER_PACKAGE.json'] = hashlib.sha256(manifest.read_bytes()).hexdigest()
        if actual != self.files or {p.relative_to(self.package).as_posix() for p in self.package.rglob('*') if p.is_file()} != set(actual) or any(p.is_symlink() for p in self.package.rglob('*')):
            raise ValueError('Prepared package inventory changed')
        for name, sha in actual.items():
            if hashlib.sha256((self.package / name).read_bytes()).hexdigest() != sha: raise ValueError('Prepared package content changed')
        for path in (Path(__file__), Path(__file__).with_name('test_actionbag_cli.py'), PROBE, NATIVE_PATH):
            if self.plan['bindings'].get(str(path.resolve())) != hashlib.sha256(path.read_bytes()).hexdigest():
                raise ValueError('Prepared source binding missing')
        self.definition = self.plan['case_definition']
        self.design = json.loads(Path(self.plan['inputs']['design_plan']['path']).read_text())
        if self.definition != next(row for row in self.design['ordered_cases'] if row['id'] == self.case): raise ValueError('Prepared case changed')
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.external = json.loads(Path(self.plan['inputs']['external']['path']).read_text())
        self.readiness = json.loads(Path(self.plan['inputs']['readiness']['path']).read_text())
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.expected_argv, self.roles, self.maps_observed = {}, {}, {}
        self.finished_fixtures, self.closed = set(), set()
        for side in 'AB':
            argv, role = ordinary_cli_argv(self.case, self.run, side, self.prefix[side], self.design)
            self.expected_argv[side], self.roles[side] = [self.prefix[side] + '/package/actionbag_python', *argv[1:]], role
        identity = json.loads(Path(self.plan['inputs']['identity']['path']).read_text())
        if identity['main_execution_result'] != 'PASS_IDENTITY_READINESS_ONLY': raise ValueError('Existing identity readiness required')
        self.tokens = {}
        for row in identity['boards']:
            side = row['side']
            if side not in NATIVE.TARGETS or row['target'] != NATIVE.TARGETS[side] or row['new_identity'] != NATIVE.IDENTITY or row['helper_exit'] != 0 or not all(row[k] is True for k in ('native_normal_manager_granted', 'original_process_token_restored', 'wrapper_exit_confirmed', 'legacy_exact_observations_unchanged')):
                raise ValueError('Exact existing admitted identity proof missing')
            self.tokens[side] = row['new_token']
        if self.tokens != {'A': 672006291, 'B': 672042405}: raise ValueError('Only the approved existing identity tokens are permitted')
        routes = json.loads(Path(self.plan['inputs']['routes']['path']).read_text())
        self.observed = datetime.fromisoformat(routes['observed_utc'])
        reference = datetime.now(timezone.utc)
        if getattr(args, 'mode', '') == 'analyze':
            dispatch = json.loads((args.run_evidence / 'LAUNCH_PENDING.json').read_text())
            if set(dispatch) != {'A', 'B'}: raise ValueError('Historical paired dispatch evidence missing')
            references = [datetime.fromisoformat(row['dispatch_utc']) for row in dispatch.values()]
            if any(t.utcoffset() is None or not 0 <= (t - self.observed).total_seconds() <= 300 for t in references):
                raise ValueError('Historical dispatch did not meet local freshness policy')
            reference = max(references)
        age = (reference - self.observed).total_seconds()
        if self.observed.utcoffset() is None or not 0 <= age <= 300 or routes.get('cross_confirmed') is not True or not routes.get('bindings'):
            raise ValueError('Fresh independently captured paired routes required (local300s policy)')
        for path, sha in routes['bindings'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest() != sha: raise ValueError('Peer receipt binding changed')
        self.routes, self.udids = {}, {}
        for side in 'AB':
            row = routes['sides'][side]
            if row['target'] != NATIVE.TARGETS[side] or row['host_exit'] != 0 or row['remote_exit'] != 0: raise ValueError('Peer query unsuccessful')
            for kind in ('stdout', 'udid'):
                data = Path(row[kind + '_file']).read_bytes()
                if hashlib.sha256(data).hexdigest() != row[kind + '_sha256']: raise ValueError('Peer export changed')
                if kind == 'stdout': self.routes[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
                else:
                    if not 1 <= len(data) <= 64 or any(c in data for c in (b'\0', b'\r', b'\n')): raise ValueError('Private UDID export invalid')
                    self.udids[side] = data.decode()
            if len(self.udids[side].encode()) != self.routes[side]['udid_bytes']: raise ValueError('Private UDID length mismatch')
        if self.udids['A'] == self.udids['B'] or any(self.routes[p]['local'] not in self.routes[s]['remotes'] for s, p in [('A', 'B'), ('B', 'A')]):
            raise ValueError('Fresh pair not cross-confirmed')
        self.write('PLAN.json', self.plan)

    def publish(self, side, name, data, *, cleanup_absent_ok=False):
        if name not in ('control.LISTENER_READY', 'control.PROBE_FINISH', 'control.RUNTIME_CLOSE'):
            raise ValueError('Only the three reviewed ordinary controls are permitted')
        if cleanup_absent_ok and name == 'control.LISTENER_READY':
            raise ValueError('Cleanup absence handling is restricted to finish/close controls')
        if not self.guard(side, absent_ok=cleanup_absent_ok):
            return False
        self.root_guard(side)
        target = self.prefix[side] + '/' + name
        self.shell(side, 'umask 077; set -C; test ! -e ' + shlex.quote(target) + ' && test ! -L ' + shlex.quote(target) +
            ' && test ! -e ' + shlex.quote(target + '.tmp') + ' && test ! -L ' + shlex.quote(target + '.tmp') +
            ' && printf %s ' + shlex.quote(data) + ' > ' + shlex.quote(target + '.tmp') +
            ' && chmod 600 ' + shlex.quote(target + '.tmp') + ' && mv ' + shlex.quote(target + '.tmp') + ' ' + shlex.quote(target))
        self.write('releases/%s-%s.json' % (side, name), {'utc': datetime.now(timezone.utc).isoformat(),
            'contents': data, 'owned': self.owned[side], 'monotonic': time.monotonic()})
        return True

    def deploy(self):
        self.stage = 'DEPLOY'
        approved = Path(os.environ['LOCALAPPDATA']) / 'Temp'
        temp = Path(tempfile.mkdtemp(prefix='r25-cli-', dir=approved)).resolve()
        if temp.parent != approved.resolve() or not temp.name.startswith('r25-cli-'): raise OSError('Owned Temp escaped approved parent')
        archive = temp / 'p.tar'
        self.write('LOCAL_TEMP.json', {'directory': str(temp), 'archive': str(archive), 'platform': 'Windows native HDC local paths required'})
        try:
            with tarfile.open(archive, 'w', format=tarfile.USTAR_FORMAT) as tar:
                for name, sha in sorted(self.files.items()):
                    data = (self.package / name).read_bytes()
                    if hashlib.sha256(data).hexdigest() != sha: raise ValueError('Package changed before archive')
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.uid, entry.gid, entry.mtime = len(data), 0, 0, 0
                    entry.mode = 0o755 if name in ('actionbag_python', 'run-token-first') else 0o600
                    entry.tobuf(format=tarfile.USTAR_FORMAT)
                    tar.addfile(entry, io.BytesIO(data))
            self.write('ARCHIVE.json', {'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'files': self.files, 'format': 'USTAR_REGULAR_ONLY'})
            for side in 'AB':
                p, upload = self.prefix[side], self.prefix[side] + '.upload.tar'
                raw = self.shell(side, 'umask 077; test ! -e ' + p + ' && test ! -L ' + p + ' && test ! -e ' + upload +
                    ' && test ! -L ' + upload + ' && mkdir -m 700 ' + p + ' && mkdir -m 700 ' + p + '/package && stat -c "%d:%i:%u:%a" ' + p)
                self.roots[side] = raw.decode().strip()
                if not re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+:700', self.roots[side]): raise OSError('Fresh private root not confirmed')
                self.send(side, archive, upload)
                self.root_guard(side)
                self.shell(side, 'tar -xf ' + upload + ' -C ' + p + '/package && rm -- ' + upload, 30)
                self.verify_files(side)
                providers = {r['canonical_absolute_path']: r['sha256'] for r in self.external['devices'][side]['providers']}
                for start in range(0, len(providers), 20):
                    names = sorted(providers)[start:start + 20]
                    text = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(n) for n in names)).decode()
                    got = {line.split(maxsplit=1)[1].strip(): line.split(maxsplit=1)[0] for line in text.splitlines()}
                    if got != {n: providers[n] for n in names}: raise OSError('Explicit external provider preflight mismatch')
                self.configure(side)
        finally:
            if temp.parent == approved.resolve() and temp.is_dir() and not temp.is_symlink():
                if archive.is_file() and not archive.is_symlink(): archive.unlink()
                temp.rmdir()

    def verify_files(self, side):
        self.root_guard(side)
        p = self.prefix[side] + '/package'
        raw = self.shell(side, 'test ! -L ' + p + ' && cd ' + p + ' && find . ! -type f ! -type d -print && find . -type f -exec sha256sum {} \\;', 30).decode()
        found = {}
        for line in raw.splitlines():
            match = re.fullmatch(r'([0-9a-f]{64})  \./(.+)', line)
            if not match or match[2] in found: raise OSError('Noncanonical remote immutable inventory')
            found[match[2]] = match[1]
        if found != self.files: raise OSError('Exact remote package inventory/hash mismatch')
        self.write('inventory/%04d-%s.json' % (self.sequence, side), found)
        return found

    def configure(self, side):
        peer = 'B' if side == 'A' else 'A'
        root, package = self.prefix[side], self.prefix[side] + '/package'
        service = 'com.kaihong.mdds.r25.' + self.run
        fields = {'bridge_library': package + '/lib/libfastdds_dsoftbus_ros2_target_bridge.so',
            'fastdds_module': package + '/lib/libfastrtps.so.2.14', 'fastcdr_module': package + '/lib/libfastcdr.so.2',
            'softbus_module': '/system/lib64/platformsdk/libsoftbus_client.z.so', 'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            'run': self.run, 'side': str('AB'.index(side)), 'domain': '201', 'authorized_package': 'com.kaihong.mdds',
            'peer_network_id': self.routes[peer]['local'], 'service_prefix': service, 'connect_release': root + '/control.LISTENER_READY',
            'locator_version': 'V1', 'namespace': 'mdds.r25.action', 'local_udid': self.udids[side], 'peer_udid': self.udids[peer],
            'service_slot': str('AB'.index(side) + 1), 'peer_slot': str('AB'.index(peer) + 1), 'profile': 'BYTES_NATIVE',
            'native_name_contract': 'PACKAGE_SCOPED_V1', 'local_initiator_name': service + '.' + side + '.tx',
            'peer_initiator_name': service + '.' + peer + '.tx', 'runtime_close_release': root + '/control.RUNTIME_CLOSE'}
        config = self.output / (side + '.private.conf')
        config.write_text(''.join(k + '=' + v + '\n' for k, v in fields.items()), encoding='utf-8', newline='\n')
        env = {'PATH': '/system/bin:/system/xbin', 'LD_LIBRARY_PATH': package + '/lib:' + RUNTIME + '/lib',
            'AMENT_PREFIX_PATH': package, 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': '201',
            'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET', 'ROS_LOG_DIR': root + '/logs', 'RCUTILS_COLORIZED_OUTPUT': '0',
            'RCUTILS_LOGGING_USE_STDOUT': '0', 'FASTDDS_DSOFTBUS_REQUIRED_CONFIG': root + '/ros.conf',
            'FASTDDS_DSOFTBUS_NATIVE_TOKEN': str(self.tokens[side]), 'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED': '1',
            'FASTDDS_DSOFTBUS_NATIVE_IDENTITY': NATIVE.IDENTITY, 'FASTDDS_ENVIRONMENT_FILE': '', 'SKIP_DEFAULT_XML': '1'}
        argv, unused = ordinary_cli_argv(self.case, self.run, side, root, self.design)
        command = 'env -i ' + ' '.join(shlex.quote(k + '=' + v) for k, v in env.items()) + ' ' + shlex.join(argv)
        script = ('umask 077\nset -C\ncd ' + root + ' || exit 91\nmkdir logs || exit 92\n' + command +
            ' >stdout 2>stderr &\nchild=$!\nprintf "%s\\n" "$child" >child.pid.tmp\nmv child.pid.tmp child.pid\n' +
            'wait "$child"\nrc=$?\nprintf "R25_CHILD_EXIT run=' + self.run + ' side=' + side +
            ' pid=%s rc=%s\\n" "$child" "$rc" >exit.tmp\nmv exit.tmp exit\nexit "$rc"\n')
        supervisor = self.output / (side + '.supervisor.sh')
        supervisor.write_text(script, encoding='utf-8', newline='\n')
        self.send(side, config, root + '/ros.conf')
        self.send(side, supervisor, root + '/supervisor.sh')
        self.shell(side, 'chmod 600 ' + root + '/ros.conf ' + root + '/supervisor.sh')
        self.write(side + '.environment.json', env)
        self.write(side + '.dispatch.json', {'argv': argv, 'expected_exec_argv': self.expected_argv[side],
            'supervisor_sha256': hashlib.sha256(supervisor.read_bytes()).hexdigest(), 'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest()})

    def launch(self, side):
        if side in self.launch_pending: raise OSError('No redispatch after a known or unknown launch outcome')
        if not 0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() <= 300:
            raise OSError('Peer receipt expired before dispatch (local freshness policy)')
        p = self.prefix[side]
        self.root_guard(side)
        self.launch_pending[side] = {'state': 'LAUNCH_PENDING', 'directory': p, 'dispatch_utc': datetime.now(timezone.utc).isoformat()}
        self.write('LAUNCH_PENDING.json', self.launch_pending)
        raw = self.shell(side, 'cd ' + p + ' || exit 91; nohup sh ' + p + '/supervisor.sh </dev/null >' + p +
            '/supervisor.stdout 2>' + p + '/supervisor.stderr & printf "%s\\n" "$!"')
        pid = int(raw.decode().strip())
        self.launch_pending[side]['supervisor_pid'] = pid
        self.write('LAUNCH_PENDING.json', self.launch_pending)
        supervisor = self.snapshot(side, pid)
        if supervisor is None or supervisor['argv'] != ['sh', p + '/supervisor.sh'] or supervisor['exe'] not in ('/system/bin/sh', '/system/bin/toybox', '/bin/sh'):
            raise OSError('Supervisor identity unconfirmed')
        self.owned[side] = {'supervisor': supervisor}
        deadline = min(time.monotonic() + 5, self.stage_deadline)
        child = None
        while time.monotonic() < deadline:
            raw = self.shell(side, 'if test -f ' + p + '/child.pid; then cat ' + p + '/child.pid; fi').decode().strip()
            if raw:
                child = self.snapshot(side, int(raw))
                break
            time.sleep(0.1)
        if child is None or child['argv'] != self.expected_argv[side] or child['exe'] != p + '/package/actionbag_python' or child['ppid'] != pid:
            raise OSError('Exact exec-preserved Python child identity unconfirmed')
        self.owned[side]['child'] = child
        self.launch_pending[side]['state'] = 'OWNERSHIP_CONFIRMED'
        self.write(side + '.ownership.json', self.owned[side])
        self.write('LAUNCH_PENDING.json', self.launch_pending)

    def poll(self, side):
        p = self.prefix[side]
        raw = self.shell(side, 'test -f ' + p + '/stdout && test -f ' + p + '/stderr && test "$(stat -c %s ' + p +
            '/stdout)" -le 4194304 && test "$(stat -c %s ' + p + '/stderr)" -le 4194304 && cat ' + p +
            '/stdout && printf "\\nR25_STDERR\\n" && cat ' + p + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        if re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE|R25_PROBE.*"event":"FAIL"', text):
            raise ValueError('Executed ordinary window reported failure')
        if self.roles[side]: ordinary_records(text, self.run, side, self.owned[side]['child']['pid'], self.roles[side])
        status = self.shell(side, 'if test -f ' + p + '/exit; then cat ' + p + '/exit; fi').decode().strip()
        return text, status

    def maps(self, side, label):
        self.guard(side)
        pid = self.owned[side]['child']['pid']
        raw = self.shell(side, 'cat /proc/' + str(pid) + '/maps')
        (self.output / (side + '.' + label + '.maps')).write_bytes(raw)
        external = {r['canonical_absolute_path']: r['sha256'] for r in self.external['devices'][side]['providers']}
        package = self.prefix[side] + '/package'
        try:
            paths, anonymous = ordinary_map_paths(raw.decode(), package, self.files, external,
                self.case != 'imports_cdr_pair', self.readiness['owned_libpython']['member'])
        except ValueError as error:
            raise OSError('PROVIDER_BOUNDARY: ' + str(error)) from error
        hashes = {}
        for start in range(0, len(paths), 20):
            text = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(p) for p in paths[start:start + 20])).decode()
            for line in text.splitlines():
                match = re.fullmatch('([0-9a-f]{64})  (/.+)', line)
                if not match or match[2] in hashes: raise OSError('Mapped provider hash capture malformed')
                hashes[match[2]] = match[1]
        if set(hashes) != set(paths): raise OSError('Mapped provider hash capture incomplete')
        previous = self.maps_observed.setdefault(side, {})
        for path, sha in hashes.items():
            expected = self.files[path[len(package) + 1:]] if path.startswith(package + '/') else external[path]
            if expected != sha or (path in previous and previous[path] != sha): raise OSError('Mapped provider hash drift: ' + path)
        previous.update(hashes)
        self.write(side + '.' + label + '.providers.json', {'sha256': hashes, 'anonymous_mappings': anonymous})

    def ready(self, side, text, stage):
        prefix = 'ROS_LISTENER_READY' if stage == 'LISTENER' else 'ROS_RUNTIME_CLOSE_READY'
        rows = NATIVE.actionbag_fields(text, prefix)
        if not rows: return False
        pid = str(self.owned[side]['child']['pid'])
        if stage == 'LISTENER':
            if len(rows) != 1 or set(rows[0]) != {'run', 'side', 'pid', 'listener', 'fake_sdk'} or any(rows[0].get(k) != v for k, v in {'run': self.run, 'side': side, 'pid': pid, 'fake_sdk': '0'}.items()) or not re.fullmatch('[0-9]+', rows[0]['listener']) or int(rows[0]['listener']) > 2147483647:
                raise ValueError('Real listener checkpoint identity/cardinality')
        elif rows != [{'schema': '1', 'run': self.run, 'side': side, 'pid': pid, 'stage': 'RUNTIME_CLOSE',
                        'stopping': '1', 'sealed': '1', 'active_pumps': '0', 'pump_joined': '1'}]:
            raise ValueError('Real runtime close checkpoint incomplete/duplicate')
        return True

    def finish_fixture(self, side, reason):
        if not self.roles[side] or side in self.finished_fixtures: return
        self.publish(side, 'control.PROBE_FINISH', 'R25_PROBE_FINISH schema=1 run=' + self.run + ' side=' + side +
            ' pid=' + str(self.owned[side]['child']['pid']) + ' reason=' + reason + '\n')
        self.finished_fixtures.add(side)

    def workflow(self):
        communicating = self.case != 'imports_cdr_pair'
        self.stage = 'LAUNCH'
        first_dispatch = time.monotonic()
        self.stage_deadline = first_dispatch + 60
        for side in 'AB': self.launch(side)  # Both launches precede all Python readiness waits.
        self.stage = 'LISTENER' if communicating else 'IMPORTS'
        while True:
            texts, statuses = {}, {}
            for side in 'AB': texts[side], statuses[side] = self.poll(side)
            if time.monotonic() >= self.stage_deadline: raise ValueError('Absolute listener/import deadline exceeded')
            if any(statuses.values()): raise ValueError('Child exited before paired admission/hold')
            if communicating:
                if all(self.ready(s, texts[s], 'LISTENER') for s in 'AB'): break
            elif ordinary_business(self.case, self.run, texts, {s: self.owned[s]['child']['pid'] for s in 'AB'}, self.design): break
            if time.monotonic() >= self.stage_deadline: raise ValueError('Absolute listener/import deadline exceeded')
            time.sleep(0.1)
        for side in 'AB': self.maps(side, 'listener' if communicating else 'imports')
        if time.monotonic() >= self.stage_deadline: raise ValueError('Live maps exceeded listener/import deadline')
        if not communicating:
            for side in 'AB': self.finish_fixture(side, 'NORMAL_COMPLETION')
            self.stage = 'EXIT'
            self.stage_deadline = min(first_dispatch + 60, time.monotonic() + 20)
            self.wait_exit(min(20, self.stage_deadline - time.monotonic()))
            return
        # CLOCK_ADDENDUM: lock the only business origin BEFORE first release.
        origin = time.monotonic()
        self.stage_deadline = min(first_dispatch + 60, origin + 35)
        self.write('CLOCK.json', {'first_dispatch': first_dispatch, 'business_origin': origin, 'business_deadline': origin + 35,
            'normal_close_release_deadline': origin + 50, 'origin': 'BEFORE_FIRST_LISTENER_RELEASE'})
        for side in 'AB':
            self.publish(side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + self.run + ' side=' + side + ' stage=LISTENER_READY\n')
        if time.monotonic() >= self.stage_deadline: raise ValueError('Listener releases exceeded admission/business deadline')
        self.stage = 'BUSINESS'
        self.stage_deadline = origin + 35
        pids = {s: self.owned[s]['child']['pid'] for s in 'AB'}
        while True:
            texts, statuses = {}, {}
            for side in 'AB': texts[side], statuses[side] = self.poll(side)
            if time.monotonic() >= origin + 35: raise ValueError('Absolute35s business deadline exceeded')
            if any(statuses.values()): raise ValueError('Child exited before controlled normal close')
            business = ordinary_business(self.case, self.run, texts, pids, self.design)
            cli = self.definition.get('cli_side')
            if business and (cli is None or self.ready(cli, texts[cli], 'CLOSE')): break
            if time.monotonic() >= origin + 35: raise ValueError('Absolute35s business deadline exceeded')
            time.sleep(0.1)
        self.stage, self.stage_deadline = 'CLOSE', origin + 50
        if cli: self.maps(cli, 'close')
        for side in 'AB': self.finish_fixture(side, 'NORMAL_COMPLETION')
        while True:
            ready = {}
            for side in 'AB':
                text, status = self.poll(side)
                if status: raise ValueError('Child exited before paired close release')
                ready[side] = self.ready(side, text, 'CLOSE')
            if time.monotonic() >= origin + 50: raise ValueError('Absolute50s close deadline exceeded')
            if all(ready.values()): break
            if time.monotonic() >= origin + 50: raise ValueError('Absolute50s normal-close deadline exceeded')
            time.sleep(0.1)
        for side in 'AB': self.maps(side, 'close')
        if time.monotonic() >= origin + 50: raise ValueError('Close maps exceeded absolute50s deadline')
        for side in 'AB':
            self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side +
                ' pid=' + str(pids[side]) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
            self.closed.add(side)
        if time.monotonic() >= origin + 50: raise ValueError('Normal close releases exceeded absolute50s deadline')
        self.stage, self.stage_deadline = 'EXIT', time.monotonic() + 20
        self.wait_exit(20)

    def analyze(self, texts):
        pids = {s: self.owned[s]['child']['pid'] for s in 'AB'}
        if not ordinary_business(self.case, self.run, texts, pids, self.design, final=True):
            raise ValueError('Required actual ordinary business evidence incomplete')
        diagnostics = {}
        for side in 'AB':
            text = texts[side]
            if NATIVE.actionbag_fields(text, 'ROS_PYTHON_INTERPRETER_RETURN') != [{'execution_rc': '0', 'finalize_rc': '0'}]:
                raise ValueError('Python execution/finalization did not return success')
            expected = {'finalized': '1', 'python_rc': '0', 'teardown_observed': '0' if self.case == 'imports_cdr_pair' else '1',
                        'teardown_rc': '-1' if self.case == 'imports_cdr_pair' else '0', 'communication': 'NOT_ASSERTED'}
            if NATIVE.actionbag_fields(text, 'ROS_PYTHON_FINAL') != [expected] or NATIVE.actionbag_fields(text, 'ROS_IDENTITY_FINAL') != [{'restored': '1', 'business_rc': '0'}]:
                raise ValueError('Actual interpreter/native/identity finalization missing')
            role = self.roles[side]
            records = ordinary_records(text, self.run, side, pids[side], role)
            if role:
                if len([r for r in records if r['event'] == 'FINAL' and r.get('success') is True and r.get('node_created') is (self.case != 'imports_cdr_pair')]) != 1:
                    raise ValueError('Fixture final record missing')
                if records[-1]['event'] != 'FINAL' or [r.get('reason') for r in records if r['event'] == 'FINISH_ACCEPTED'] != ['NORMAL_COMPLETION']:
                    raise ValueError('Fixture normal finish/final order missing')
                if self.case != 'imports_cdr_pair' and (len([r for r in records if r['event'] == 'NODE_DESTROY_RETURN']) != 1 or len([r for r in records if r['event'] == 'SHUTDOWN_RETURN']) != 1):
                    raise ValueError('Public fixture cleanup returns missing')
                expected_tail = ['FINISH_ACCEPTED', 'FINAL'] if self.case == 'imports_cdr_pair' else ['FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL']
                if [r['event'] for r in records[-len(expected_tail):]] != expected_tail:
                    raise ValueError('Actual fixture teardown order differs')
            if self.case == 'imports_cdr_pair' and re.search(r'FDDST_ROS_PRE_PARTICIPANT|ROS_LISTENER_READY|ROS_RUNTIME_CLOSE_READY|ROS_NATIVE_DATA |ROS_STARTUP_OPERATION |Factory_(?:create|delete)_participant|FDDST_ROS_CLOSED', text):
                raise ValueError('Unexpected noNode factory/socket/lifecycle activity')
            stdout = (self.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8')
            stderr = (self.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8')
            # Phase proof uses anchors and errors in the same original stream.
            # An approved stdout pair never exempts a stderr error.
            diagnostics[side] = ordinary_error_observations(stdout, stderr, self.run, side, pids[side], role, self.case != 'imports_cdr_pair', self.files)
        native, locators = None, None
        if self.case != 'imports_cdr_pair':
            native = NATIVE.actionbag_native_capture(texts, self.run, pids)
            oracle = REPO / 'scripts/public_socket/analyze_v1_locators.py'
            if hashlib.sha256(oracle.read_bytes()).hexdigest() != '582971f7d631ddffde9ec59c2e677c0ad756d2271d3ce1df6204e35ed0da93ff':
                raise ValueError('Independent V1 oracle source changed')
            module = runpy.run_path(str(oracle))
            self.write('window.json', {'run': self.run})
            expected = {s: module['derive']({'namespace': 'mdds.r25.action', 'domain': 201, 'udid': self.udids[s]}, 'AB'.index(s) + 1) for s in 'AB'}
            locators = module['analyze'](self.output, expected)
        self.write('ANALYSIS.json', {'scope': 'ORDINARY_' + self.case, 'business': 'PASS', 'native': native,
            'v1_spdp': locators, 'diagnostics': diagnostics, 'sdk_epoch': 'NOT_PROVEN', 'dedicated_SEDP': 'NOT_QUALIFIED',
            'execution_qualification': 'NOT_EVALUATED_CONTENT_ONLY' if getattr(self.args, 'mode', '') == 'analyze' else 'PENDING_EXECUTION_RESULT_AND_CLEANUP'})

    def cleanup(self, failed):
        result = {'directories_retained': True, 'identity_mutations': 0, 'sides': {}, 'pending': self.launch_pending}
        for side in self.owned:
            status = {'forced_signals': [], 'absent': False}
            try:
                self.stage_deadline = time.monotonic() + 10
                if 'child' not in self.owned[side]: raise OSError('Partial launch child identity unknown; no signal permitted')
                if failed and self.guard(side, absent_ok=True):
                    for name in ('control.PROBE_FINISH', 'control.RUNTIME_CLOSE'):
                        if name == 'control.PROBE_FINISH' and (not self.roles[side] or side in self.finished_fixtures): continue
                        exists = self.shell(side, 'if test -e ' + self.prefix[side] + '/' + name + ' || test -L ' + self.prefix[side] + '/' + name + '; then printf 1; else printf 0; fi').decode().strip()
                        if exists == '0':
                            if name == 'control.PROBE_FINISH':
                                published = self.publish(side, name, 'R25_PROBE_FINISH schema=1 run=' + self.run + ' side=' + side +
                                    ' pid=' + str(self.owned[side]['child']['pid']) + ' reason=WINDOW_FAILED\n', cleanup_absent_ok=True)
                                if published: self.finished_fixtures.add(side)
                            else:
                                published = self.publish(side, name, 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side +
                                    ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=WINDOW_FAILED\n', cleanup_absent_ok=True)
                            if not published: break
                for signal, budget in [(None, 10), ('TERM', 5), ('KILL', 2)]:
                    if signal: self.stage_deadline = time.monotonic() + budget
                    if not self.guard(side, absent_ok=True): break
                    if signal:
                        self.guard(side)
                        self.shell(side, 'kill -' + signal + ' ' + str(self.owned[side]['child']['pid']))
                        status['forced_signals'].append(signal)
                    end = self.stage_deadline
                    while time.monotonic() < end and self.guard(side, absent_ok=True): time.sleep(0.1)
                self.stage_deadline = time.monotonic() + 3
                child_absent = not self.guard(side, absent_ok=True)
                deadline = self.stage_deadline
                parent = self.snapshot(side, self.owned[side]['supervisor']['pid'])
                while parent is not None and time.monotonic() < deadline:
                    self.guard(side, role='supervisor')
                    time.sleep(0.1)
                    parent = self.snapshot(side, self.owned[side]['supervisor']['pid'])
                status['absent'] = child_absent and parent is None
                if not status['absent']: raise OSError('Owned child/supervisor absence unconfirmed')
                self.stage_deadline = None  # Process wait is complete; bounded immutable postflight calls follow.
                self.verify_files(side)
                providers = self.maps_observed.get(side, {})
                for start in range(0, len(providers), 20):
                    paths = sorted(providers)[start:start + 20]
                    raw = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(p) for p in paths)).decode()
                    got = {line.split(maxsplit=1)[1].strip(): line.split(maxsplit=1)[0] for line in raw.splitlines()}
                    if got != {p: providers[p] for p in paths}: raise OSError('Observed provider postflight drift')
            except (ValueError, OSError) as error:
                status['error'] = str(error)
                self.blocked.append('CLEANUP ' + side + ': ' + str(error))
            finally:
                self.stage_deadline = None
            if status['forced_signals']: self.errors.append('Forced owned termination: ' + side)
            result['sides'][side] = status
        for side in self.launch_pending:
            if side not in self.owned or 'child' not in self.owned[side]:
                self.blocked.append('LAUNCH ' + side + ': unknown outcome retained; no redispatch/signal')
        self.write('CLEANUP.json', result)

    def execute(self):
        try:
            self.deploy()
            self.workflow()
            self.stage_deadline = None
            self.analyze(self.collect())
        except ValueError as error: self.errors.append(self.stage + ': ' + str(error))
        except (OSError, subprocess.SubprocessError) as error: self.blocked.append(self.stage + ': ' + str(error))
        except (Exception, KeyboardInterrupt) as error: self.blocked.append(self.stage + ': unexpected ' + repr(error))
        finally:
            self.cleanup(bool(self.errors or self.blocked))
            if self.errors or self.blocked:
                try:
                    if self.owned: self.collect()
                except (OSError, ValueError) as error: self.blocked.append('FAILURE_CAPTURE: ' + str(error))
            result = {'run': self.run, 'case': self.case, 'result': 'FAIL' if self.errors else 'BLOCKED' if self.blocked else 'PASS',
                'failures': self.errors, 'blocked': self.blocked, 'identity_mutations': 0, 'device_window': self.prefix,
                'directories_retained': True, 'sdk_epoch': 'NOT_PROVEN', 'source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
            self.write('RESULT.json', result)
        return 0 if result['result'] == 'PASS' else 1


def ordinary_execution_verdict(old, plan, owned):
    """Bind recorded execution failures; this never requalifies an execution."""
    run, case = plan['run'], plan['case']
    communicating = case != 'imports_cdr_pair'
    names = ['RESULT.json', 'CHILD_EXITS.json', 'CLEANUP.json']
    definition = plan['case_definition']
    for side in 'AB':
        role = definition.get('boards', {}).get(side, definition.get('fixture_role') if side == definition.get('fixture_side') else None)
        if role: names.append('releases/' + side + '-control.PROBE_FINISH.json')
        if communicating: names += ['releases/' + side + '-control.LISTENER_READY.json', 'releases/' + side + '-control.RUNTIME_CLOSE.json']
    if communicating: names.append('CLOCK.json')
    commands = sorted((old / 'commands').glob('*.json'))
    names += [p.relative_to(old).as_posix() for p in commands]
    names += [p.relative_to(old).as_posix() for p in sorted((old / 'remote').glob('*.json'))]
    records, bindings, issues = {}, {}, []
    for name in names:
        path = old / name
        try:
            if path.is_symlink() or path.resolve(strict=True) != path: raise ValueError('Noncanonical recorded evidence')
            raw = path.read_bytes()
            bindings[name] = {'path': str(path), 'sha256': hashlib.sha256(raw).hexdigest()}
            row = json.loads(raw, object_pairs_hook=ordinary_unique)
            if not isinstance(row, dict): raise ValueError('Expected recorded object')
            records[name] = row
        except (OSError, ValueError) as error: issues.append(name + ': ' + str(error))
    original = records.get('RESULT.json', {})
    status = original.get('result', 'NOT_EVALUATED')
    if status not in ('PASS', 'FAIL', 'BLOCKED') or original.get('run') != run or original.get('case') != case:
        issues.append('Original execution verdict/identity is missing or invalid')
    if not isinstance(original.get('failures'), list) or not isinstance(original.get('blocked'), list):
        issues.append('Original failure reasons are missing')
    elif status == 'PASS' and (original['failures'] or original['blocked']):
        issues.append('Original PASS contradicts latched failures/blocks')
    expected_exits = {s: 'R25_CHILD_EXIT run=' + run + ' side=' + s + ' pid=' + str(owned[s]['child']['pid']) + ' rc=0' for s in 'AB'}
    if records.get('CHILD_EXITS.json') != expected_exits: issues.append('Recorded zero child exits are incomplete/inconsistent')
    cleanup = records.get('CLEANUP.json', {})
    sides = cleanup.get('sides', {})
    if (cleanup.get('identity_mutations') != 0 or cleanup.get('directories_retained') is not True or
        not isinstance(sides, dict) or set(sides) != {'A', 'B'} or
        any(not isinstance(row, dict) or row.get('absent') is not True or row.get('forced_signals') != [] or 'error' in row for row in sides.values())):
        issues.append('Recorded cleanup/absence is incomplete, forced or failed')
    if not commands: issues.append('Recorded HDC command outcomes are missing')
    for path in commands:
        name = path.relative_to(old).as_posix()
        row = records.get(name, {})
        if row.get('host_exit') != 0 or row.get('timed_out') is True: issues.append('Failed/timed-out recorded command: ' + name)
        if isinstance(row.get('argv'), list) and len(row['argv']) > 3 and row['argv'][3] == 'shell':
            if records.get('remote/' + path.name, {}).get('remote_exit') != 0: issues.append('Missing/nonzero recorded remote exit: ' + name)
    for name, row in records.items():
        if name.startswith('remote/') and row.get('remote_exit') != 0: issues.append('Nonzero recorded remote exit: ' + name)
    clock = records.get('CLOCK.json', {})
    valid_clock = not communicating
    if communicating:
        values = [clock.get(k) for k in ('first_dispatch', 'business_origin', 'business_deadline', 'normal_close_release_deadline')]
        valid_clock = all(type(v) in (int, float) and 0 <= v < float('inf') for v in values)
        if valid_clock:
            first, origin, business, close = values
            valid_clock = first <= origin < first + 60 and business == origin + 35 and close == origin + 50 and clock.get('origin') == 'BEFORE_FIRST_LISTENER_RELEASE'
        if not valid_clock: issues.append('Recorded original clock/deadline contract is incomplete or inconsistent')
    for side in 'AB':
        for control in ('PROBE_FINISH', 'LISTENER_READY', 'RUNTIME_CLOSE'):
            name = 'releases/' + side + '-control.' + control + '.json'
            if name not in names: continue
            row = records.get(name, {})
            pid = str(owned[side]['child']['pid'])
            expected = ('R25_PROBE_FINISH schema=1 run=' + run + ' side=' + side + ' pid=' + pid + ' reason=NORMAL_COMPLETION\n' if control == 'PROBE_FINISH' else
                        'ROS_RELEASE run=' + run + ' side=' + side + ' stage=LISTENER_READY\n' if control == 'LISTENER_READY' else
                        'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + run + ' side=' + side + ' pid=' + pid + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
            if row.get('contents') != expected or row.get('owned') != owned[side]: issues.append('Missing/failed recorded normal control: ' + name)
            when = row.get('monotonic')
            if type(when) not in (int, float) or not 0 <= when < float('inf'):
                issues.append('Invalid recorded release time: ' + name)
            elif communicating and valid_clock and not origin <= when < (min(first + 60, business) if control == 'LISTENER_READY' else close):
                issues.append('Recorded release exceeded original deadline: ' + name)
    return {'result': status if status in ('FAIL', 'BLOCKED') else 'BLOCKED' if issues else 'SKIPPED',
            'scope': 'OFFLINE_CONTENT_DIAGNOSTIC', 'execution_qualification': 'NOT_EVALUATED_CONTENT_ONLY',
            'original_execution_result': status, 'original_execution_record': original,
            'execution_evidence': bindings, 'execution_evidence_issues': issues}


def ordinary_cli_runner_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    prepare = sub.add_parser('prepare', help='Validate exact selected readiness/package/source inputs; no devices')
    prepare.add_argument('--inputs', type=Path, required=True)
    prepare.add_argument('--output', type=Path, required=True)
    routes = sub.add_parser('routes', help='Local fixed adapter for independently captured V3 peer evidence')
    routes.add_argument('--peer-evidence', type=Path, required=True)
    routes.add_argument('--output', type=Path, required=True)
    for name in ('run', 'analyze'):
        command = sub.add_parser(name)
        command.add_argument('--prepared', type=Path, required=True)
        command.add_argument('--output', type=Path, required=True)
        if name == 'run': command.add_argument('--hdc', type=Path, default=Path(NATIVE.HDCPATH))
        else: command.add_argument('--run-evidence', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.mode == 'prepare': return ordinary_prepare(args)
    if args.mode == 'routes':
        NATIVE.actionbag_peer_receipt(args)
        return 0
    if args.mode == 'run' and str(args.hdc.resolve()).lower() != str(Path(NATIVE.HDCPATH).resolve()).lower():
        parser.error('Only the selected SDK23 HDC is permitted')
    runner = OrdinaryCliRunner.__new__(OrdinaryCliRunner)
    try:
        runner.__init__(args)
        if args.mode == 'run': return runner.execute()
        old = args.run_evidence.resolve()
        if json.loads((old / 'PLAN.json').read_text()) != runner.plan: raise ValueError('Run evidence/plan mismatch')
        runner.owned = {s: json.loads((old / (s + '.ownership.json')).read_text()) for s in 'AB'}
        execution = ordinary_execution_verdict(old, runner.plan, runner.owned)
        runner.write('RECORDED_EXECUTION.json', execution)
        texts = {}
        for side in 'AB':
            for kind in ('stdout', 'stderr'):
                src, dst = old / (side + '.raw.' + kind + '.log'), runner.output / (side + '.raw.' + kind + '.log')
                shutil.copyfile(src, dst)
            texts[side] = (runner.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8') + '\n' + (runner.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8')
            external = {r['canonical_absolute_path']: r['sha256'] for r in runner.external['devices'][side]['providers']}
            for label in (('imports',) if runner.case == 'imports_cdr_pair' else ('listener', 'close')):
                map_file, hashes_file = side + '.' + label + '.maps', side + '.' + label + '.providers.json'
                raw = (old / map_file).read_text()
                observed = json.loads((old / hashes_file).read_text(), object_pairs_hook=ordinary_unique)
                package = runner.prefix[side] + '/package'
                paths, anonymous = ordinary_map_paths(raw, package, runner.files, external,
                    runner.case != 'imports_cdr_pair', runner.readiness['owned_libpython']['member'])
                expected = {p: runner.files[p[len(package) + 1:]] if p.startswith(package + '/') else external[p] for p in paths}
                if observed != {'sha256': expected, 'anonymous_mappings': anonymous}:
                    raise ValueError('Recorded actual maps/provider hash proof incomplete or changed')
                shutil.copyfile(old / map_file, runner.output / map_file)
                shutil.copyfile(old / hashes_file, runner.output / hashes_file)
        runner.analyze(texts)
        runner.write('RESULT.json', {**execution, 'content_analysis': 'PASS', 'device_execution': 'NONE',
            'recorded_evidence': str(old), 'case': runner.case, 'run': runner.run})
        return 0 if execution['result'] == 'SKIPPED' else 1
    except (ValueError, OSError, KeyError, TypeError) as error:
        if getattr(runner, 'owns_output', False) and not (runner.output / 'INPUT_FAILURE.json').exists():
            runner.write('INPUT_FAILURE.json', {'result': 'BLOCKED', 'reason': str(error), 'device_execution': 'NOT_DISPATCHED_BY_INPUT_VALIDATION'})
        raise


if __name__ == '__main__':
    raise SystemExit(ordinary_cli_runner_main())
