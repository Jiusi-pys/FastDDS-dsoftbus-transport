"""Bounded local text classification; never a device/UI/permission attestation."""
import re
import threading
import time

CASES = ('server', 'ready.A', 'ready.B')
STREAMS = ('stdout', 'stderr')
PHASES = ('MONITOR', 'CLEANUP', 'EOF', 'ADVANCE', 'CLOSE')
MAX_LINE = 512
MAX_CHUNK = 4096
MAX_TOTAL = 4194304 + 327680
MAX_EVENTS = 96
TRIGGER_FAMILIES = frozenset(('NOT_APPLICABLE', 'AUTH_WORD_ONLY', 'ECODE_PREFIX_ONLY', 'BOTH'))
RULES = {
    'CODE_E000002', 'CODE_E000003', 'REQUEST_SENTENCE', 'DIAG_FIELD',
    'DIAG_PATH', 'AUTH_CONTEXT_UNKNOWN', 'UTF8_INVALID', 'RECORD_TOO_LONG',
    'PARTIAL_RECORD', 'BINDING_INVALID', 'CLOCK_INVALID', 'CHUNK_INVALID',
    'TOTAL_CAP', 'THREAD_INVALID', 'AFTER_EOF', 'AFTER_CLOSE',
    'CLASSIFIER_ERROR', 'SINK_FAILURE', 'EVENT_CAP',
    'CODE_E000004_CHANNEL_ESTABLISHING',
    'CODE_DEVICE_SELECTION_FAILURE',
}


class Marker:
    def __init__(self, sink=None, clock=None, *, cases=CASES, max_total=MAX_TOTAL):
        self.cases=tuple(cases);self.max_total=max_total
        self.sink = sink
        self.clock = clock or time.perf_counter_ns
        self.owner_thread = threading.get_ident()
        self.lock = threading.RLock()
        self.streams = {}
        self.events = []
        self.seen = set()
        self.occurrences = 0
        self.suppressed = 0
        self.total_bytes = 0
        self.closed = False
        self.level = 'NONE'
        self.record_failed = False
        self.overflow = False
        self.failure_rules = set()
        self.emergency = []

    @property
    def notice(self):
        return self.level in ('COMMAND_FAILURE', 'REQUEST_TEXT', 'DENIED_TEXT', 'UNKNOWN')

    def reason(self):
        if self.record_failed:
            return 'NOTICE_RECORD_FAILURE'
        if self.level == 'COMMAND_FAILURE':
            # Preserve final-classification precedence: an earlier E4 must
            # never override a later request, denial or UNKNOWN observation.
            if any(e['rule_id']=='CODE_E000004_CHANNEL_ESTABLISHING' for e in self.events):
                return 'HDC_CHANNEL_ESTABLISHING_FAILURE'
            return 'HDC_DEVICE_SELECTION_FAILURE'
        return {'UNKNOWN': 'NOTICE_AMBIGUOUS', 'REQUEST_TEXT': 'AUTH_REQUEST_TEXT',
                'DENIED_TEXT': 'AUTH_DENIED_TEXT'}.get(self.level)

    def _decision_time(self):
        try:
            v = self.clock()
            return v if type(v) is int and 0 <= v < 2**63 else None
        except BaseException:
            return None

    def _emit(self, case, stream, phase, kind, rule, start, end, before, after,
              complete, trigger_family='NOT_APPLICABLE'):
        assert rule in RULES
        if trigger_family not in TRIGGER_FAMILIES:
            kind, rule, trigger_family = 'UNKNOWN', 'CLASSIFIER_ERROR', 'NOT_APPLICABLE'
        rank = {'NONE': 0, 'DIAGNOSTIC': 1, 'COMMAND_FAILURE': 2,
                'REQUEST_TEXT': 3, 'DENIED_TEXT': 3, 'UNKNOWN': 4}
        if rank[kind] > rank[self.level]:
            self.level = kind
        decision = self._decision_time()
        if decision is None or after is not None and decision < after:
            kind, rule = 'UNKNOWN', 'CLOCK_INVALID'
            self.level = 'UNKNOWN'
            trigger_family = 'NOT_APPLICABLE'
        self.occurrences += 1
        key = (case, stream, phase, kind, rule, trigger_family)
        if key in self.seen:
            self.suppressed += 1
            return
        if len(self.events) >= MAX_EVENTS:
            self.level = 'UNKNOWN'
            self.record_failed = True
            self.overflow = True
            self.failure_rules.add('EVENT_CAP')
            if not self.emergency:
                self.emergency.append(dict(rule_id='EVENT_CAP', event_type='UNKNOWN',
                                           case=case if case in self.cases else 'UNBOUND',
                                           stream=stream if stream in STREAMS else 'UNBOUND',
                                           phase=phase if phase in PHASES else 'UNBOUND'))
            return
        self.seen.add(key)
        row = dict(schema='NOTICE_EVENT_V2', sequence=len(self.events)+1,
                   case=case if case in self.cases else 'UNBOUND',
                   stream=stream if stream in STREAMS else 'UNBOUND',
                   phase=phase if phase in PHASES else 'UNBOUND',
                   event_type=kind, rule_id=rule, trigger_family=trigger_family,
                   offset_start=start, offset_end=end,
                   receive_before_ns=before, receive_after_ns=after,
                   decision_ns=decision, remote_occurrence_ns=None,
                   time_scope='LOCAL_READ_CHUNK_OR_BOUNDARY',
                   observer_thread_id=threading.get_ident(),
                   complete_record=bool(complete))
        self.events.append(row)
        if self.sink is not None:
            try:
                ok = self.sink(dict(row))
                if ok is not True:
                    raise RuntimeError('controlled sink failure')
            except BaseException:
                self.record_failed = True
                self.level = 'UNKNOWN'
                self.failure_rules.add('SINK_FAILURE')
                if not self.emergency:
                    self.emergency.append(dict(rule_id='SINK_FAILURE', event_type='UNKNOWN',
                                               case=row['case'],stream=row['stream'],phase=row['phase']))

    def _unknown(self, case, stream, phase, rule, state=None, before=None,
                 after=None):
        offset = state['offset'] if state is not None else 0
        self._emit(case, stream, phase, 'UNKNOWN', rule, offset, offset,
                   before, after, False)

    def _binding(self, case, stream, phase):
        if self.closed:
            self._unknown(case, stream, phase, 'AFTER_CLOSE')
            return False
        if threading.get_ident() != self.owner_thread:
            self._unknown(case, stream, phase, 'THREAD_INVALID')
            return False
        if case not in self.cases or stream not in STREAMS or phase not in PHASES:
            self._unknown(case, stream, phase, 'BINDING_INVALID')
            return False
        return True

    @staticmethod
    def _blocking(text):
        # These are observations of text, not proof an OS dialog occurred.
        low = text.lower()
        if '[e000003]' in low:
            return 'DENIED_TEXT', 'CODE_E000003'
        if '[e000002]' in low:
            return 'REQUEST_TEXT', 'CODE_E000002'
        if re.fullmatch(r'(?:authorize this device|please (?:authorize|confirm) '
                        r'this device|please confirm authorization on '
                        r'(?:your|this) device)[.!]?', low.strip()):
            return 'REQUEST_TEXT', 'REQUEST_SENTENCE'
        return None

    def _record(self, case, stream, phase, state, data, complete, before, after):
        start = state['offset'] - len(data)
        try:
            text = data.decode('utf-8', errors='strict')
        except UnicodeDecodeError as ex:
            if not complete and ex.reason == 'unexpected end of data' and ex.end == len(data):
                text = data[:ex.start].decode('utf-8', errors='strict')
            else:
                self._emit(case, stream, phase, 'UNKNOWN', 'UTF8_INVALID',
                           start, state['offset'], before, after, complete)
                return
        signal = self._blocking(text)
        if signal:
            self._emit(case, stream, phase, *signal, start, state['offset'],
                       before, after, complete)
            return
        if not complete:
            return
        # Exact complete-record diagnosis only. Same-code variants stay unknown;
        # this is a blocking command failure, never successful ordinary output.
        # EchoClient's CRLF can become CRCRLF on Windows text stdout. feed()
        # removes only LF; accept exactly 0/1/2 trailing CR, never strip().
        command_text = text[:-2] if text.endswith('\r\r') else text.removesuffix('\r')
        failures = {
            '[Fail][E000004]:The communication channel is being established.':
                'CODE_E000004_CHANNEL_ESTABLISHING',
            '[Fail]ExecuteCommand need connect-key? please confirm a device by help info':
                'CODE_DEVICE_SELECTION_FAILURE',
        }
        rule = failures.get(command_text)
        if rule is not None:
            self._emit(case, stream, phase, 'COMMAND_FAILURE',
                       rule, start,
                       state['offset'], before, after, True)
            return
        # Local full-record grammar only, not a claimed vendor log format.
        fields = {'auth_result': {'0'}, '授权结果': {'0'},
                  'auth_status': {'authorized', 'not_requested'},
                  '授权状态': {'已授权', '未请求'}}
        key, sep, value = text.strip().partition('=')
        mixed = any(x in text.lower() for x in
                    ('please', 'confirm', 'device unauthorized', 'pair', '配对',
                     '请求', '拒绝', '确认', '[e000'))
        # Entire fixed field/value pair is the grammar, not an auth-prefix
        # exemption. In particular, not_requested is not a request sentence.
        if sep and key in fields and value in fields[key]:
            self._emit(case, stream, phase, 'DIAGNOSTIC', 'DIAG_FIELD', start,
                       state['offset'], before, after, True)
            return
        path_mixed = mixed or any(x in value.lower() for x in
                                 ('request', 'denied', 'denial', 'unauthorized'))
        if (not path_mixed and key in ('auth_path', '授权路径') and sep
                and len(value) <= 256 and re.fullmatch(r'(?:/|[a-zA-Z]:[/\\])[^\s=\x00-\x1f]*', value)):
            self._emit(case, stream, phase, 'DIAGNOSTIC', 'DIAG_PATH', start,
                       state['offset'], before, after, True)
            return
        # Mechanical fallback predicates only. No input-derived string, code,
        # substring, digest or reversible encoding enters the evidence.
        auth_word = any(x in text.lower() for x in
                        ('auth', 'confirm', 'pair', '授权', '配对', '确认'))
        ecode_prefix = '[e000' in text.lower()
        if auth_word or ecode_prefix:
            family = ('BOTH' if auth_word and ecode_prefix else
                      'AUTH_WORD_ONLY' if auth_word else 'ECODE_PREFIX_ONLY')
            self._emit(case, stream, phase, 'UNKNOWN', 'AUTH_CONTEXT_UNKNOWN',
                       start, state['offset'], before, after, True,
                       trigger_family=family)

    def feed(self, case, stream, data, phase, before, after):
        with self.lock:
            if not self._binding(case, stream, phase):
                return
            state = self.streams.setdefault((case, stream),
                                           dict(pending=b'', offset=0, eof=False, discarding=False))
            if state['eof']:
                self._unknown(case, stream, phase, 'AFTER_EOF', state)
                return
            decision = self._decision_time()
            if (type(before) is not int or type(after) is not int or decision is None
                    or not 0 <= before <= after <= decision < 2**63):
                self._unknown(case, stream, phase, 'CLOCK_INVALID', state)
                return
            if type(data) is not bytes or not 0 < len(data) <= MAX_CHUNK:
                self._unknown(case, stream, phase, 'CHUNK_INVALID', state, before, after)
                return
            self.total_bytes += len(data)
            if self.total_bytes > self.max_total:
                self._unknown(case, stream, phase, 'TOTAL_CAP', state, before, after)
                return
            try:
                parts = data.split(b'\n')
                for i, part in enumerate(parts):
                    state['offset'] += len(part)
                    complete = i < len(parts)-1
                    # An oversized physical record remains invalid until LF.
                    # Clearing a bounded buffer must not turn its next chunk
                    # into a new complete record or an allowed diagnostic.
                    if state['discarding']:
                        if complete:
                            state['discarding'] = False
                            state['offset'] += 1
                        continue
                    state['pending'] += part
                    if len(state['pending']) > MAX_LINE:
                        self._unknown(case, stream, phase, 'RECORD_TOO_LONG', state,
                                      before, after)
                        state['pending'] = b''
                        state['discarding'] = not complete
                    elif state['pending']:
                        self._record(case, stream, phase, state, state['pending'],
                                     complete, before, after)
                    if complete:
                        state['pending'] = b''
                        state['offset'] += 1
            except BaseException:
                self._unknown(case, stream, phase, 'CLASSIFIER_ERROR', state,
                              before, after)

    def eof(self, case, stream, phase='EOF'):
        with self.lock:
            if not self._binding(case, stream, phase):
                return
            state = self.streams.setdefault((case, stream),
                                           dict(pending=b'', offset=0, eof=False, discarding=False))
            if state['eof']:
                return
            if state['pending'] or state['discarding']:
                self._unknown(case, stream, phase, 'PARTIAL_RECORD', state)
                state['pending'] = b''
                state['discarding'] = False
            state['eof'] = True

    def advance(self):
        with self.lock:
            for (case, stream), state in self.streams.items():
                if state['pending'] or state['discarding']:
                    self._unknown(case, stream, 'ADVANCE', 'PARTIAL_RECORD', state)
            return self.reason()

    def close(self):
        with self.lock:
            for (case, stream), state in self.streams.items():
                if state['pending'] or state['discarding']:
                    self._unknown(case, stream, 'CLOSE', 'PARTIAL_RECORD', state)
                    state['pending'] = b''
                    state['discarding'] = False
            self.closed = True

    def report(self):
        with self.lock:
            return dict(schema='NOTICE_EVIDENCE_V2', classification=self.level,
                        blocking=self.notice, record_failed=self.record_failed,
                        overflow=self.overflow, occurrences=self.occurrences,
                        deduplicated=self.suppressed, consumed_bytes=self.total_bytes,
                        closed=self.closed, events=[dict(x) for x in self.events],
                        failure_rules=sorted(self.failure_rules),
                        emergency_events=[dict(x) for x in self.emergency],
                        remote_UI_permission_state='UNKNOWN_NOT_ATTESTED')
