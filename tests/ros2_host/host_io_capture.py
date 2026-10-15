"""Strict Host IO completeness check, independent of the DDS business verdict."""
import re


def validate_capture(text, run, side, pid):
    rows = [line for line in text.splitlines() if line.startswith('HOST_IO_')]
    events, captures = [], []
    try:
        for line in rows:
            tokens = line.split()
            pairs = [token.split('=', 1) for token in tokens[1:]]
            if any(len(pair) != 2 for pair in pairs):
                raise ValueError('syntax')
            row = dict(pairs)
            if len(row) != len(pairs) or row.get('schema') != '1' or row.get('run') != run or row.get('side') != side:
                raise ValueError('identity')
            if tokens[0] == 'HOST_IO_EVENT':
                if row.get('pid') != str(pid):
                    raise ValueError('pid')
                expected = {'schema','run','side','pid','seq','begin','last_call_begin','thread','kind','phase','termination','send','socket','epoch','handle','requested','result_known','result','wsa_known','wsa','calls','partial'}
                if set(row) != expected:
                    raise ValueError('event fields')
                for key in expected-{'schema','run','side','kind','phase','termination'}:
                    if not re.fullmatch(r'-?(0|[1-9][0-9]*)', row[key]):
                        raise ValueError('event numeric field')
                if row['result_known'] not in ('0','1') or row['wsa_known'] not in ('0','1'):
                    raise ValueError('event flags')
                events.append(row)
            elif tokens[0] == 'HOST_IO_CAPTURE':
                if set(row) != {'schema','run','side','capacity','reserved','published','overflow','unpublished','complete','ordering','runtime_owner','successful_syscalls','sdk_epoch'}:
                    raise ValueError('capture fields')
                captures.append(row)
            else:
                raise ValueError('unknown record')
        if len(captures) != 1:
            raise ValueError('capture count')
        capture = captures[0]
        def number(row, key):
            value = row.get(key, '')
            if not re.fullmatch(r'0|[1-9][0-9]*', value):
                raise ValueError(key)
            return int(value)
        reserved = number(capture, 'reserved')
        if (number(capture, 'capacity') != 16384 or reserved == 0 or reserved > 16384
                or number(capture, 'published') != reserved or len(events) != reserved
                or number(capture, 'overflow') != 0 or number(capture, 'unpublished') != 0
                or capture.get('complete') != '1'
                or capture.get('ordering') != 'PROCESS_OBSERVATION_ONLY'
                or capture.get('runtime_owner') != 'NOT_OBSERVED'
                or capture.get('successful_syscalls') != 'AGGREGATED'
                or capture.get('sdk_epoch') != 'NOT_PROVEN'):
            raise ValueError('incomplete')
        seqs = [number(row, 'seq') for row in events]
        if len(set(seqs)) != len(seqs) or min(seqs) == 0:
            raise ValueError('sequence')
        ordered = sorted(events, key=lambda row: number(row, 'seq'))
        if ordered[-1].get('kind') != 'LINK_JOIN_RETURN':
            raise ValueError('join boundary')
        return {'complete': True, 'records': reserved, 'error': None}
    except (ValueError, KeyError):
        return {'complete': False, 'records': len(events), 'error': 'missing_malformed_or_incomplete_host_io_capture'}
