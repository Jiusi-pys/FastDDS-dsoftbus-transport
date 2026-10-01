"""Strict ordinary-ROS topic/graph/service ledger. No dispatch or recovery authority."""
import argparse
import json
import re
from pathlib import Path


def rows(text, event):
    result = []
    for line in text.splitlines():
        if not line.startswith(event + ' '):
            continue
        record = {}
        for token in line.split()[1:]:
            if '=' not in token:
                raise ValueError('Malformed ' + event)
            key, value = token.split('=', 1)
            if key in record:
                raise ValueError('Duplicate field in ' + event)
            record[key] = value
        result.append(record)
    return result


def analyze(texts, run, phases):
    errors = []
    result = {'run': run, 'phases': list(phases), 'boards': {}, 'errors': errors}
    if (not isinstance(texts, dict) or any(not isinstance(texts.get(side), str) for side in ('A', 'B')) or
            not isinstance(run, str) or not re.fullmatch(r'[0-9a-f]{16}', run) or
            tuple(phases) not in (('reliable',), ('best_effort',), ('reliable', 'best_effort'))):
        errors.append('INPUT_INCOMPLETE_OR_INVALID_CONTRACT')
        result.update(observed_writes=0, observed_takes=0, **{'pass': False})
        return result
    publishers = {}
    captured = {}
    events = ('ROS_MAIN', 'ROS_CONTEXT_CLOSED', 'ROS_PUBLISHER', 'ROS_WRITE',
              'ROS_TAKE', 'ROS_GRAPH', 'ROS_BUSINESS_DONE', 'ROS_SERVICE_SENT',
              'ROS_SERVICE_REQUEST', 'ROS_SERVICE_RESPONSE')
    for side in ('A', 'B'):
        captured[side] = {}
        for event in events:
            try:
                captured[side][event] = rows(texts[side], event)
            except ValueError as error:
                errors.append('malformed:' + side + ':' + str(error))
                captured[side][event] = []
    for side, other in [('A', 'B'), ('B', 'A')]:
        text = texts[side]
        try:
            main = captured[side]['ROS_MAIN']
            if len(main) != 1 or main[0].get('run') != run or main[0].get('side') != side or not main[0].get('pid', '').isdigit():
                errors.append('main_identity:' + side)
            closed = captured[side]['ROS_CONTEXT_CLOSED']
            if len(closed) != 1 or closed[0].get('run') != run or closed[0].get('side') != side or closed[0].get('result') != '0':
                errors.append('context_close:' + side)
            board = result['boards'][side] = {'phases': {}, 'pid': main[0].get('pid') if len(main) == 1 else None}
            for phase in phases:
                board['phases'][phase] = {}
                pub = [x for x in captured[side]['ROS_PUBLISHER'] if x.get('phase') == phase]
                if len(pub) != 1 or pub[0].get('run') != run or pub[0].get('side') != side or pub[0].get('intra_process') != '0':
                    errors.append('publisher:' + side + ':' + phase)
                else:
                    publishers[(side, phase)] = pub[0].get('gid')
                    gid = pub[0].get('gid', '')
                    if not re.fullmatch(r'[0-9a-f]{32,64}', gid) or not any(x != '0' for x in gid):
                        errors.append('publisher_gid:' + side + ':' + phase)
                    topic_prefix = phase + '/' if len(phases) == 2 else ''
                    if pub[0].get('topic') != '/fdds_' + run + '/' + topic_prefix + 'from_' + side:
                        errors.append('publisher_topic:' + side + ':' + phase)
                for event in ('ROS_WRITE', 'ROS_TAKE'):
                    samples = [x for x in captured[side][event] if x.get('phase') == phase]
                    board['phases'][phase][event] = len(samples)
                    ordinals = [x.get('ordinal') for x in samples]
                    if len(samples) != 5 or set(ordinals) != set('12345'):
                        errors.append('sample_set:' + side + ':' + phase + ':' + event)
                    for sample in samples:
                        origin = side if event == 'ROS_WRITE' else other
                        prefix = run + ':' + phase + ':' if len(phases) == 2 else run + ':'
                        if sample.get('run') != run or sample.get('side') != side or sample.get('payload') != prefix + origin + ':' + sample.get('ordinal', ''):
                            errors.append('sample_identity:' + side + ':' + phase)
                        topic_prefix = phase + '/' if len(phases) == 2 else ''
                        expected_topic = '/fdds_' + run + '/' + topic_prefix + 'from_' + origin if event == 'ROS_WRITE' else topic_prefix + 'from_' + origin
                        if sample.get('topic') != expected_topic:
                            errors.append('sample_topic:' + side + ':' + phase)
                graph = [x for x in captured[side]['ROS_GRAPH'] if x.get('phase') == phase]
                if len(graph) != 1 or graph[0].get('run') != run or graph[0].get('side') != side or graph[0].get('peer') != '/fdds_' + run + '/transport_' + other or graph[0].get('visible') != '1' or graph[0].get('subscription_count') != '1':
                    errors.append('graph:' + side + ':' + phase)
                done = [x for x in captured[side]['ROS_BUSINESS_DONE'] if x.get('phase') == phase]
                if len(done) != 1 or done[0].get('run') != run or done[0].get('side') != side or done[0].get('writes') != '5' or done[0].get('takes') != '5':
                    errors.append('phase_complete:' + side + ':' + phase)
            for event in ('ROS_WRITE', 'ROS_TAKE', 'ROS_PUBLISHER', 'ROS_GRAPH', 'ROS_BUSINESS_DONE'):
                if any(x.get('phase') not in phases for x in captured[side][event]):
                    errors.append('unexpected_phase:' + side + ':' + event)
        except (ValueError, KeyError) as error:
            errors.append('malformed:' + side + ':' + str(error))
    for side, other in [('A', 'B'), ('B', 'A')]:
        for phase in phases:
            own, remote = publishers.get((side, phase)), publishers.get((other, phase))
            if not own or not remote or own == remote:
                errors.append('distinct_peer_gid:' + side + ':' + phase)
            for sample in captured[side]['ROS_TAKE']:
                if sample.get('phase') == phase and sample.get('gid') != remote:
                    errors.append('take_writer:' + side + ':' + phase)
    if captured['A']['ROS_SERVICE_REQUEST'] or captured['B']['ROS_SERVICE_SENT'] or captured['B']['ROS_SERVICE_RESPONSE']:
        errors.append('service_wrong_side')
    for side in ('A', 'B'):
        for event in ('ROS_SERVICE_SENT', 'ROS_SERVICE_REQUEST', 'ROS_SERVICE_RESPONSE'):
            if any(x.get('phase') not in phases for x in captured[side][event]):
                errors.append('service_unexpected_phase:' + side)
    for phase in phases:
        sent = [x for x in captured['A']['ROS_SERVICE_SENT'] if x.get('phase') == phase]
        handled = [x for x in captured['B']['ROS_SERVICE_REQUEST'] if x.get('phase') == phase]
        response = [x for x in captured['A']['ROS_SERVICE_RESPONSE'] if x.get('phase') == phase]
        if len(sent) != 1 or len(handled) != 1 or len(response) != 1:
            errors.append('service_set:' + phase)
            continue
        if any(x.get('run') != run for x in sent + handled + response) or sent[0].get('request_id') != handled[0].get('request_id') or sent[0].get('request_id') != response[0].get('request_id'):
            errors.append('service_request_identity:' + phase)
        if handled[0].get('a') != '3' or handled[0].get('b') != '4' or handled[0].get('sum') != '7' or response[0].get('sum') != '7' or not handled[0].get('writer'):
            errors.append('service_content:' + phase)
        if not re.fullmatch(r'[0-9a-f]{32}', handled[0].get('writer', '')) or not sent[0].get('request_id', '').isdigit():
            errors.append('service_id_format:' + phase)
    result['observed_writes'] = sum(v.get('ROS_WRITE', 0) for b in result['boards'].values() for v in b['phases'].values())
    result['observed_takes'] = sum(v.get('ROS_TAKE', 0) for b in result['boards'].values() for v in b['phases'].values())
    result['pass'] = not errors
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    parser.add_argument('--a', type=Path, required=True)
    parser.add_argument('--b', type=Path, required=True)
    parser.add_argument('--mode', choices=['reliable', 'best_effort', 'dual'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    phases = ['reliable', 'best_effort'] if args.mode == 'dual' else [args.mode]
    outcome = analyze({'A': args.a.read_text(), 'B': args.b.read_text()}, args.run, phases)
    args.output.write_text(json.dumps(outcome, indent=2) + '\n')
    raise SystemExit(0 if outcome['pass'] else 1)
