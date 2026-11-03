"""Read-only evidence checker for the secure dual fragmented Host scene.

Wire ranges, RMW reads and application receipts are separate observations.
Their value equality does not establish a unique physical packet identity.
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/public_socket'))
from analyze_ros2 import analyze, rows

# Exact TEST_ONLY_BOUNDED contract of the original 19/20 attempt and current
# scoped receipts. These are not production defaults or fragment geometry.
# This checker intentionally offers no override that could change that contract.
SCENE_LIMITS = (98304, 2097152, 64)


def integer(record, key):
    value = record.get(key, '')
    if not re.fullmatch(r'-?[0-9]+', value):
        raise ValueError('invalid_integer:' + key)
    return int(value)


def one(records, event):
    if len(records) != 1:
        raise ValueError('expected_one:' + event)
    return records[0]


def ranges(records, sample_limit):
    """Derive logical fragment coverage; never count overlapping calls as samples."""
    if not records:
        raise ValueError('fragment_rows_absent')
    geometry = {(integer(x, 'fragment_size'), integer(x, 'sample_size')) for x in records}
    if len(geometry) != 1:
        raise ValueError('inconsistent_fragment_geometry')
    size, sample = geometry.pop()
    if not (1 <= size <= 0xffff and 1 <= sample <= 0xffffffff):
        raise ValueError('fragment_geometry_wire_width')
    if sample_limit < 4 or sample_limit > 0xffffffff - 0xffff:
        raise ValueError('invalid_actual_sample_limit')
    if sample > sample_limit:
        raise ValueError('sample_exceeds_actual_config')
    if size <= 0 or sample <= size:
        raise ValueError('not_a_fragmented_sample')
    total = (sample + size - 1) // size
    coverage = set()
    observed = []
    for record in records:
        first, count = integer(record, 'first'), integer(record, 'count')
        if not (1 <= first <= 0xffffffff and 1 <= count <= 0xffff):
            raise ValueError('fragment_range_wire_width')
        last = first + count - 1
        if first < 1 or count < 1 or last > total:
            raise ValueError('fragment_range_out_of_sample')
        if record.get('payload_bounds_known') != '1' or record.get('key_flag') != '0':
            raise ValueError('fragment_payload_bounds_or_key_unknown')
        logical = min(sample, last * size) - (first - 1) * size
        payload = integer(record, 'payload_bytes')
        # RTPS submessage alignment may pad the last fragment by up to 3 bytes.
        if payload < logical or payload > logical + (3 if last == total else 0):
            raise ValueError('fragment_payload_length')
        coverage.update(range(first, last + 1))
        observed.append({'first': first, 'count': count,
                         'observation': integer(record, 'observation'),
                         'socket': integer(record, 'socket'), 'payload_bytes': payload})
    return {'fragment_size': size, 'sample_size': sample,
            'derived_fragment_count': total, 'coverage': sorted(coverage), 'ranges': observed}


def check(scene, texts, errors, configs, expected='positive'):
    """Check already loaded originals without changing any source or result file."""
    checks, facts = {}, {}
    result = {'expected': expected, 'checks': checks, 'facts': facts,
              'historical_cause': 'UNRESOLVED', 'physical_packet_identity': 'NOT_ESTABLISHED'}

    def require(name, condition):
        checks[name] = bool(condition)

    try:
        run = scene['run']
        require('supported_checker_mode', expected in ('positive', 'drop-last', 'late-release'))
        require('run_contract', bool(re.fullmatch(r'[0-9a-f]{16}', run)) and
                scene.get('mode') == 'dual' and scene.get('forward') == '1')
        actual_limits = {}
        for side in 'AB':
            config = configs[side]
            m, b, n = (integer(config, key) for key in ('payload_max_sample_bytes',
                       'payload_pool_budget_bytes', 'payload_max_allocations'))
            valid = (config.get('payload_guard_profile') == 'TEST_ONLY_BOUNDED' and
                     4 <= m <= 0xffffffff - 0xffff and m <= b <= 0xffffffffffffffff and
                     1 <= n <= 0xffffffff)
            require('payload_limit_contract_' + side, valid)
            if not valid:
                raise ValueError('invalid_actual_payload_config:' + side)
            require('frozen_scene_payload_contract_' + side, (m, b, n) == SCENE_LIMITS)
            if (m, b, n) != SCENE_LIMITS:
                raise ValueError('frozen_scene_payload_contract_mismatch:' + side)
            bound = rows(errors[side], 'FDDST_PAYLOAD_POOL_BOUND')
            matches = bool(bound) and all(r.get('profile') == 'TEST_ONLY_BOUNDED' and
                (integer(r, 'M'), integer(r, 'B'), integer(r, 'N')) == (m, b, n) for r in bound)
            require('actual_pool_matches_configuration_' + side, matches)
            if not matches:
                raise ValueError('actual_pool_configuration_mismatch:' + side)
            actual_limits[side] = m
        formal = analyze(texts, run, ('reliable', 'best_effort'))
        facts['formal_business'] = formal
        result['raw_scene_pass'] = scene.get('pass')
        result['raw_child_exit'] = {s: scene['processes'][s]['exit_code'] for s in 'AB'}
        require('bounded_no_timeout', scene.get('timed_out') is False)
        require('raw_children', result['raw_child_exit'] ==
                ({'A': 4, 'B': 4} if expected == 'drop-last' else {'A': 0, 'B': 0}))
        require('business_verdict', scene.get('pass') is (expected != 'drop-last') and
                formal['pass'] is (expected != 'drop-last') and
                formal['observed_writes'] == 20 and
                formal['observed_takes'] == (19 if expected == 'drop-last' else 20))
        require('stored_business_matches_originals', scene.get('ledger') == formal)
        if expected == 'drop-last':
            require('only_expected_formal_failures', set(formal['errors']) == {
                'application_exit:A', 'application_exit:B',
                'sample_set:A:best_effort:ROS_TAKE', 'phase_complete:A:best_effort'})
        require('no_sanitizer_or_unsafe_termination', not any(
            marker in texts[s] + errors[s] for s in 'AB'
            for marker in ('AddressSanitizer', 'runtime error:', 'terminate called',
                           'unexpected terminate', 'HOST_LINK_BAD_LENGTH')))
        captures, native, drops, diagnostics = {}, {}, [], {}
        for side in 'AB':
            pid = str(scene['processes'][side]['pid'])
            main = one(rows(texts[side], 'ROS_MAIN'), 'ROS_MAIN')
            require('process_identity_' + side, main.get('run') == run and
                    main.get('side') == side and main.get('pid') == pid)
            # New records that carry a PID must identify this exact child. Other
            # records remain file/run/side scoped, not reconstructed by PID.
            events = ('ROS_TARGET_WRITE', 'ROS_TARGET_TAKE', 'ROS_LEDGER_STATE',
                      'ROS_CALLBACK_VALIDATED_PENDING', 'ROS_TARGET_LEDGER_HELD',
                      'ROS_NATIVE_FRAGMENT', 'ROS_NATIVE_SUMMARY', 'HOST_FRAGMENT_DROP',
                      'ROS_SEND_ADMISSION_SUMMARY', 'ROS_RECEIVE_DECISION_SUMMARY')
            for event in events:
                parsed = rows(texts[side], event)
                require('identity_' + side + '_' + event, all(
                    r.get('run') == run and r.get('side') == side and
                    ('pid' not in r or r['pid'] == pid) for r in parsed))
            config = configs[side]
            require('guarded_host_configuration_' + side,
                    config.get('run') == run and config.get('side') == str('AB'.index(side)) and
                    config.get('backend') == 'HOST_OPAQUE_BYTES_TEST_ONLY' and
                    config.get('host_fragment_observation') == '1' and
                    config.get('payload_guard_profile') == 'TEST_ONLY_BOUNDED' and
                    all(config.get(k, '').isdigit() for k in ('payload_max_sample_bytes',
                        'payload_pool_budget_bytes', 'payload_max_allocations')))
            require('secure_manager_active_' + side,
                    'FDDST_SECURITY_LOG category=SECURITY message=Initialized security manager' in errors[side] and
                    ' authenticated' in errors[side])
            captures[side] = one(rows(errors[side], 'FDDST_HOST_FRAGMENT_CAPTURE'), 'fragment_capture')
            require('fragment_capture_complete_' + side,
                    captures[side].get('run') == run and captures[side].get('side') == side and
                    all(captures[side].get(k) == v for k, v in
                        {'flush_return': '1', 'exception': '0', 'output_error': '0', 'complete': '1'}.items()))
            native[side] = one(rows(texts[side], 'ROS_NATIVE_SUMMARY'), 'native_summary')
            require('native_calls_balanced_' + side,
                    integer(native[side], 'send_enter') > 0 and
                    native[side]['send_enter'] == native[side]['send_return'] and
                    all(native[side].get(k) == '0' for k in ('send_error', 'overflow', 'malformed')))
            cleanup = one(rows(errors[side], 'FDDST_HOST_CLEANUP'), 'host_cleanup')
            require('final_host_join_' + side, cleanup.get('threads_joined') == '1' and
                    all(cleanup.get(k) == '0' for k in ('callbacks', 'receives', 'send_entered')) and
                    cleanup.get('attempts') == cleanup.get('calls') == cleanup.get('returns'))
            closed = one(rows(errors[side], 'FDDST_ROS_CLOSED'), 'runtime_close')
            require('final_pump_and_runtime_close_' + side,
                    closed.get('run') == run and closed.get('side') == str('AB'.index(side)) and
                    closed.get('pump_joined') == '1' and closed.get('runtime_closed') == '1' and
                    not any(x in errors[side] for x in ('FDDST_CLOSE_INCOMPLETE',
                        'FDDST_PUMP_ERROR_OBSERVED', 'FDDST_UNPUBLISHED_OWNER_RETAINED_INCOMPLETE')))
            receive = one(rows(texts[side], 'ROS_RECEIVE_DECISION_SUMMARY'), 'receive_summary')
            send = one(rows(texts[side], 'ROS_SEND_ADMISSION_SUMMARY'), 'send_summary')
            diagnostics[side] = {'receive': receive, 'send': send,
                'capture_dropped_scope': 'DIAGNOSTIC_TRY_LOCK_SAMPLING_ONLY',
                'target_binding_owned_copy_queue': 'UNKNOWN_FROM_AGGREGATE_COUNTERS'}
            drops.extend(rows(texts[side], 'HOST_FRAGMENT_DROP'))
            for phase in ('reliable', 'best_effort'):
                state = one([r for r in rows(texts[side], 'ROS_LEDGER_STATE')
                             if r.get('phase') == phase], 'actual_ledger:' + side + ':' + phase)
                lost = expected == 'drop-last' and side == 'A' and phase == 'best_effort'
                target = {'callbacks': '4' if lost else '5', 'invalid_callbacks': '0', 'bad': '0',
                          'accepted': '4' if lost else '5', 'accepted_mask': '15' if lost else '31',
                          'target5_accepted': '0' if lost else '1',
                          'service_done': '1' if side == 'A' else '0',
                          'requests': '0' if side == 'A' else '1', 'source': 'ACTUAL_APPLICATION_VALUES'}
                require('actual_callback_ledger_' + side + '_' + phase,
                        all(state.get(k) == v for k, v in target.items()))
        facts['diagnostic_layers'] = diagnostics
        facts['configuration'] = configs
        facts['recorded_inputs'] = scene.get('inputs', {})
        facts['module_unmap'] = 'NOT_EXPLICITLY_OBSERVED_IN_ORDINARY_CONSUMER'
        pub = one([r for r in rows(texts['B'], 'ROS_PUBLISHER')
                   if r.get('phase') == 'best_effort'], 'B_best_effort_publisher')
        writer = pub['gid'][:32]
        write = one([r for r in rows(texts['B'], 'ROS_TARGET_WRITE')
                     if r.get('phase') == 'best_effort'], 'B_target_write')
        takes = [r for r in rows(texts['A'], 'ROS_TARGET_TAKE') if r.get('phase') == 'best_effort']
        if expected == 'drop-last':
            control = scene.get('fragment_control', {})
            require('declared_negative_fixture', control.get('mode') == 'drop-last' and
                    control.get('scope') == 'TEST_ONLY_NO_ACK_NO_RETRANSMISSION' and
                    bool(configs['B'].get('host_fragment_control')) and
                    not configs['A'].get('host_fragment_control'))
            drop = one(drops, 'one_drop')
            require('original_drop_identity', drop.get('writer') == writer and
                    drop.get('side') == 'B' and drop.get('pid') == str(scene['processes']['B']['pid']) and
                    all(drop.get(k) == v for k, v in {'schema': '1', 'api_called': '1',
                        'actual_host_write': '0', 'native_result': '0', 'expected_negative': '1',
                        'drops': '1', 'identity': 'ORIGINAL_BORROWED_ARGS'}.items()))
            high, low = integer(drop, 'sequence_hi'), integer(drop, 'sequence_lo')
            require('target_callback_absent', not takes)
            a_takes = [r for r in rows(texts['A'], 'ROS_TAKE') if r.get('phase') == 'best_effort']
            require('only_declared_target_missing', {r.get('ordinal') for r in a_takes} == set('1234') and
                    len(a_takes) == 4 and all(r.get('gid', '')[:32] == writer for r in a_takes))
            require('negative_first_failure_visible', any(
                r.get('stage') == 'LEDGER' and r.get('reason') == 'SAMPLE_SERVICE_INCOMPLETE'
                for r in rows(texts['A'], 'ROS_FAILURE')) and scene.get('cleanup_sent_after_failure') is True)
        else:
            require('no_loss_injection', not drops)
            if expected == 'positive':
                require('ordinary_positive_fixture', scene.get('fragment_control', {}).get('mode') in (None, '') and
                        not any(configs[s].get('host_fragment_control') or
                                rows(texts[s], 'ROS_CALLBACK_VALIDATED_PENDING') for s in 'AB'))
            take = one(takes, 'A_target_take')
            business_take = one([r for r in rows(texts['A'], 'ROS_TAKE')
                if r.get('phase') == 'best_effort' and r.get('ordinal') == '5'],
                'A_actual_business_target_take')
            identity_matches = (business_take.get('run') == take.get('run') == run and
                business_take.get('side') == take.get('side') == 'A' and
                business_take.get('phase') == take.get('phase') == 'best_effort' and
                business_take.get('ordinal') == take.get('ordinal') == '5' and
                take.get('pid') == str(scene['processes']['A']['pid']) and
                business_take.get('gid') == pub.get('gid') and
                integer(business_take, 'publication_sequence') == integer(take, 'publication_sequence'))
            require('target_business_receipt_identity', identity_matches)
            if not identity_matches:
                raise ValueError('target_business_receipt_identity_mismatch')
            sequence = integer(take, 'publication_sequence')
            high, low = sequence >> 32, sequence & 0xffffffff
            require('validated_payload_value_matches', write.get('bytes') == take.get('bytes') and
                    write.get('payload_hash') == take.get('payload_hash') and
                    write.get('hash_scope') == take.get('hash_scope') == 'DIAGNOSTIC_VALUE_ONLY' and
                    write.get('ordinal') == take.get('ordinal') == '5')
            require('no_business_failure', not any(rows(texts[s], 'ROS_FAILURE') for s in 'AB'))
        fragments = {}
        for side, direction in (('B', 'SDK_SEND'), ('A', 'SDK_INGRESS')):
            fragments[direction] = ranges([r for r in rows(texts[side], 'ROS_NATIVE_FRAGMENT')
                if r.get('writer') == writer and r.get('direction') == direction and
                integer(r, 'sequence_hi') == high and integer(r, 'sequence_lo') == low],
                actual_limits[side])
        sent, received = fragments['SDK_SEND'], fragments['SDK_INGRESS']
        require('same_actual_wire_geometry', all(sent[k] == received[k]
                for k in ('fragment_size', 'sample_size', 'derived_fragment_count')))
        complete = set(range(1, sent['derived_fragment_count'] + 1))
        require('target_send_range_complete', set(sent['coverage']) == complete)
        if expected == 'drop-last':
            first, count = integer(drop, 'first'), integer(drop, 'count')
            omitted = set(range(first, first + count))
            require('drop_is_exact_final_range', count > 0 and first >= 1 and
                    max(omitted) == sent['derived_fragment_count'] and
                    integer(drop, 'sample_size') == sent['sample_size'] and
                    integer(drop, 'fragment_size') == sent['fragment_size'] and
                    any(r['first'] == first and r['count'] == count and
                        r['socket'] == integer(drop, 'original_socket') for r in sent['ranges']))
            require('ingress_missing_only_declared_drop_range', set(received['coverage']) == complete - omitted)
            facts['declared_drop'] = drop
        else:
            require('target_ingress_range_complete', set(received['coverage']) == complete)
        facts['current_target'] = {'writer': writer, 'sequence_hi': high, 'sequence_lo': low,
                                   'application_write': write, 'wire': fragments}
        facts['last_seen'] = ('SDK_SEND_ENTERED_TEST_DROP_BEFORE_HOST_WRITE' if expected == 'drop-last'
                              else 'ACTUAL_RMW_CALLBACK_AND_ACCEPTED_APPLICATION_LEDGER')
        facts['uncovered'] = ['original ticket/deadline per fragment',
            'target binding/owned-copy/queue from aggregate counters',
            'individual Reader pool/reassembly internals', 'unique physical packet identity']
        if expected == 'late-release':
            pending = one(rows(texts['A'], 'ROS_CALLBACK_VALIDATED_PENDING'), 'pending_callback')
            held = one(rows(texts['A'], 'ROS_TARGET_LEDGER_HELD'), 'held_ledger')
            require('original_validated_callback_held', pending.get('source') == 'ORIGINAL_VALIDATED_RMW_CALLBACK' and
                    pending.get('exact') == '1' and pending.get('gid', '')[:32] == writer and
                    all(pending.get(k) == take.get(k) for k in ('bytes', 'payload_hash', 'publication_sequence')) and
                    held.get('phase') == 'best_effort' and held.get('ordinal') == '5' and
                    all(held.get(k) == v for k, v in {'accepted_before': '4', 'callbacks': '5',
                        'payload_validated': '1', 'retained_payload': '0'}.items()) and
                    held.get('publication_sequence') == take.get('publication_sequence'))
            control = scene.get('fragment_control', {})
            events = control.get('events', [])
            require('no_early_done_or_cleanup', control.get('mode') == 'late-release' and
                    control.get('state') == 'RELEASED' and control.get('scope') == 'TEST_ONLY_NO_ACK_NO_RETRANSMISSION' and
                    len(events) == 2 and events[0].get('event') == 'TARGET_LEDGER_RELEASE' and
                    all(events[0].get(k) is True for k in ('A_ledger_held', 'B_done_observed',
                        'no_early_completion_or_cleanup')) and events[1].get('event') == 'DONE_RELEASE' and
                    events[1].get('A_done_observed') is True and events[1].get('B_done_observed') is True and
                    events[0].get('parent_monotonic_ns', 0) < events[1].get('parent_monotonic_ns', 0))
            lines = texts['A'].splitlines()
            order = [next(i for i, line in enumerate(lines) if line.startswith(event + ' ') and
                          'phase=best_effort' in line) for event in
                     ('ROS_CALLBACK_VALIDATED_PENDING', 'ROS_TARGET_LEDGER_HELD',
                      'ROS_TARGET_TAKE', 'ROS_BUSINESS_DONE')]
            require('in_process_callback_ledger_done_order', order == sorted(order) and len(set(order)) == 4)
    except (KeyError, ValueError, TypeError, StopIteration) as error:
        checks['complete_parse'] = False
        result['parse_error'] = str(error)
    result['passed'] = bool(checks) and all(checks.values())
    result['interpretation'] = ('EXPECTED_NEGATIVE_BUSINESS_FAIL_SAFE_CLEANUP' if expected == 'drop-last'
                               else 'SUPPORTED_BUSINESS_20_OF_20')
    return result


def load_and_check(root, expected='positive'):
    scene = json.loads((root / 'result.json').read_text(encoding='utf-8'))
    texts = {s: (root / (s + '.stdout')).read_text(encoding='utf-8', errors='replace') for s in 'AB'}
    errors = {s: (root / (s + '.stderr')).read_text(encoding='utf-8', errors='replace') for s in 'AB'}
    configs = {s: dict(line.split('=', 1) for line in
                      (root / (s + '.conf')).read_text(encoding='utf-8').splitlines()) for s in 'AB'}
    return check(scene, texts, errors, configs, expected)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--expected', choices=('positive', 'drop-last', 'late-release'), default='positive')
    parser.add_argument('--output', type=Path, help='Fresh analysis output; never overwrites a prior receipt')
    args = parser.parse_args()
    outcome = load_and_check(args.root, args.expected)
    serialized = json.dumps(outcome, indent=2) + '\n'
    if args.output:
        with args.output.open('x', encoding='utf-8') as stream:
            stream.write(serialized)
    print(serialized)
    raise SystemExit(0 if outcome['passed'] else 1)
