"""Native per-process checks adapted from the frozen actionbag oracle.

All three original logs retain their own run/side/PID identity; no log rewriting.
Common checks are unchanged; matching is separately required for A-B0 and A-B1.
"""
import re
from process_restart_oracle import fields as actionbag_fields


def native_capture(texts, identities, *, instances=('A', 'B0', 'B1')):
    if instances not in (('A', 'B0', 'B1'), ('A', 'B0', 'B1', 'B2')) or set(texts) != set(instances) or set(identities) != set(instances):
        raise ValueError('Exact finite process set required')
    frames, result = {}, {}
    for instance in instances:
        run, pid = identities[instance]['run'], identities[instance]['pid']
        side = instance[0]
        text = texts[instance]
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
        frames[instance] = {'SDK_SEND': set(), 'SDK_INGRESS': set()}
        for entry in data:
            if set(entry) != {'run', 'side', 'observation', 'direction', 'socket', 'writer', 'sequence_hi', 'sequence_lo', 'scope'} or entry['run'] != run or entry['side'] != side or entry['scope'] != 'STRUCTURAL_APP_OBSERVER' or entry['direction'] not in frames[instance] or not re.fullmatch('[0-9a-f]{32}', entry['writer']) or not re.fullmatch('[1-9][0-9]*', entry['observation']) or not re.fullmatch('[0-9]+', entry['socket']) or not re.fullmatch('-?[0-9]+', entry['sequence_hi']) or not re.fullmatch('[0-9]+', entry['sequence_lo']):
                raise ValueError('Native data schema/identity')
            high, low = int(entry['sequence_hi']), int(entry['sequence_lo'])
            if not -2147483648 <= high <= 2147483647 or not 0 <= low <= 4294967295 or int(entry['socket']) > 2147483647:
                raise ValueError('Native data range')
            frames[instance][entry['direction']].add((entry['writer'], high, low))
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
        if not teardown or any(x.get('run') != run or x.get('side') != side or x.get('pid') != str(pid) or x.get('completeness') != 'CAPTURED' or x.get('overflow') != '0' or x.get('output_errors') != '0' for x in teardown):
            raise ValueError('Teardown capture incomplete')
        deletion = actionbag_fields(text, 'ROS_TEARDOWN_EVENT')
        if any(x.get('run') != run or x.get('side') != side or x.get('pid') != str(pid) or x.get('schema') != '1' for x in deletion):
            raise ValueError('Teardown event identity mismatch')
        if not any(x.get('operation') == 'Factory_delete_participant' and x.get('returned') == '1' and x.get('value_type') == 'PUBLIC_RETURN_CODE' and x.get('value') == '0' for x in deletion):
            raise ValueError('DDS public delete return missing')
        if re.search(r'ROS_FAILURE|ACTION_EXCEPTION|INIT_FAILED|RUNTIME_CLOSE_FAILURE|DIAGNOSTIC_CAPTURE.*INCOMPLETE|failed to send response[^\r\n]*timeout', text, re.I):
            raise ValueError('Native/business failure diagnostic')
        result[instance] = {'native_records': len(data), 'send_enter': int(row['send_enter']), 'ingress': int(row['ingress'])}
    for peer in instances[1:]:
        for source, destination in [('A', peer), (peer, 'A')]:
            matched = frames[source]['SDK_SEND'] & frames[destination]['SDK_INGRESS']
            if not matched:
                raise ValueError('Each process needs bidirectional actual RTPS evidence')
            result[source]['matched_to_' + destination] = len(matched)
    guids = {}
    for instance in instances:
        run, side = identities[instance]['run'], instance[0]
        entries = actionbag_fields(texts[instance], 'ROS_V1_SPDP')
        local = set()
        for row in entries:
            if row.get('run') != run or row.get('side') != side:
                raise ValueError('SPDP process identity differs')
            if row.get('direction') == 'SDK_SEND':
                if not re.fullmatch('[0-9a-f]{32}', row.get('writer', '')):
                    raise ValueError('Invalid local SPDP writer')
                local.add(row['writer'][:24])
        if len(local) != 1:
            raise ValueError('One local Participant prefix required per process')
        guids[instance] = next(iter(local))
    if len(set(guids.values())) != len(instances):
        raise ValueError('Every process must create a distinct Participant')
    return {'instances': result, 'participant_prefixes': guids, 'sdk_epoch': 'NOT_PROVEN',
            'payload_to_rtps_sequence': 'NOT_DIRECTLY_MAPPED'}
