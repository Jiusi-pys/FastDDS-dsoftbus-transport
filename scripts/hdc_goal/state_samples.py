"""Two fixed-time local state observations. No raw-list publication or live imports."""
import re
import time

SCOPE='ONE_NEW_FIXED_A_STATE_SAMPLES_STRICT_AUTH_60S_D01'
NONCE='C74E205B_STATE_SAMPLES_60S_D01'
CASES=('server','state.S1','state.S2')
OFFSETS={'state.S1':0.,'state.S2':10.}
STATES={'Unknown':'DEVICE_STATE_UNKNOWN','Ready':'READY','Connected':'CONNECTED',
        'Offline':'OFFLINE','Unauthorized':'UNAUTHORIZED'}


class SlotMissed(Exception):pass


class MemoryCapture:
    """Drop pipe capture bytes; never create a raw file or retain its data."""
    __slots__=()
    def write(self,data):return len(data)
    def flush(self):pass
    def close(self):pass


class Listing:
    """Reference tab grammar; other device fields die with each bounded row."""
    def __init__(self,target):
        self.target=target;self.pending=bytearray();self.total=0;self.rows=0
        self.match=None;self.empty=False;self.failed=False;self.eol=None
        self.done=False;self.negative=False;self.peak=0

    def feed(self,data):
        if self.failed:return
        if self.done or type(data) is not bytes or not 0<len(data)<=4096:
            self.failed=True;self.erase();return
        self.total+=len(data)
        if self.total>32768:self.failed=True;self.erase();return
        self.pending.extend(data);self.peak=max(self.peak,len(self.pending))
        while b'\n' in self.pending and not self.failed:
            i=self.pending.index(10);raw=bytes(self.pending[:i]);del self.pending[:i+1]
            ending=2 if raw.endswith(b'\r\r') else 1 if raw.endswith(b'\r') else 0
            if self.eol is None:self.eol=ending
            if self.eol!=ending or len(raw)>512:self.failed=True;break
            raw=raw[:-ending] if ending else raw
            try:self.row(raw.decode('utf-8','strict'))
            except (UnicodeError,ValueError):self.failed=True
        if len(self.pending)>512:self.failed=True
        if self.failed:self.erase()

    def row(self,line):
        self.rows+=1
        if self.rows>128:raise ValueError('FORMAT_UNKNOWN')
        # EchoClient adds CRLF to Empty; ListTargetsAll inserts TAB+hdc
        # before LF. Preserve that interior CR rather than stripping it.
        if line=='[Empty]\r\thdc':
            if self.rows!=1:raise ValueError('FORMAT_UNKNOWN')
            self.empty=True;return
        if self.empty:raise ValueError('FORMAT_UNKNOWN')
        m=re.fullmatch(r'([A-Za-z0-9_.:\-]{1,32})\t\t(USB|TCP|UART|BT|UNKNOWN)\t'
                       r'(Unknown|Ready|Connected|Offline|Unauthorized)\t([^\x00-\x1f\x7f]{1,256})\thdc',line)
        if not m:raise ValueError('FORMAT_UNKNOWN')
        if m[1]==self.target:
            if self.match is not None:raise ValueError('FORMAT_UNKNOWN')
            self.match=STATES[m[3]];self.negative=self.match=='UNAUTHORIZED'

    def finish(self):
        if self.failed or self.pending or not self.rows:raise ValueError('FORMAT_UNKNOWN')
        self.done=True
        return {'present':self.match is not None,'device_state':self.match or 'A_ABSENT',
                'complete_list':True,'format_valid':True,'target_unique':True}

    def erase(self):self.pending.clear()


def check_creation_slot(permit):
    """Last check before the native call; never refund an actual creation."""
    import goal_authorization as a
    a.need(permit in a._permits and permit.grant in a._grants
           and getattr(permit.grant,'strict_state_samples',False) is True,'STATE_PERMIT_REQUIRED')
    if permit.step.id=='server':return
    a.need(permit.step.id in OFFSETS,'STATE_STEP_REQUIRED')
    start,end=permit.grant.sample_windows[permit.step.id];now=time.monotonic()
    a.need(now>=start,'STATE_SLOT_EARLY')
    if now>end:raise SlotMissed('NOT_RUN_SLOT_MISSED')


def make_types(driver_module,backend_module,terminal_qualified):
    need=driver_module.need

    class StateBackend(backend_module.GoalWindowsBackend):
        qualify=staticmethod(terminal_qualified)
        def __init__(self,grant):
            need(getattr(grant,'strict_state_samples',False) is True
                 and grant.server_unknown_continue is False,'STRICT_STATE_EFFECT_REQUIRED')
            super().__init__(grant);self.init_state()

        def init_state(self,clock=time.monotonic):
            # The base owns numeric samples/epochs resource counters. State
            # projections have separate storage and must not shadow them.
            self.state_clock=clock;self.t0=None;self.parsers={};self.state_records={};self.sample_started={}
            self.grant.sample_windows={}

        def register(self,step):
            root=super().register(step)
            if step.id!='server':
                from goal_contract import TARGETS
                self.parsers[step.id]=Listing(TARGETS['A1'])
            return root

        def notice_gate(self):
            need(self.grant.server_unknown_continue is False,'STRICT_AUTH_POLICY')
            super().notice_gate() # no server UNKNOWN continuation in this scope
            need(not any(p.failed for p in self.parsers.values()),'FORMAT_UNKNOWN')
            need(not any(p.negative for p in self.parsers.values()),'STATE_UNAUTHORIZED')

        def observe_chunk(self,root,stream,data,before,after,phase):
            super().observe_chunk(root,stream,data,before,after,phase)
            if root.case in self.parsers:
                parser=self.parsers[root.case]
                if stream!='stdout':parser.failed=True;parser.erase()
                else:parser.feed(data)
                if parser.negative and not root.receipt.get('state_negative_recorded'):
                    self.journal.emit({'state_notice':{'sample_index':CASES.index(root.case),
                        'device_state':'UNAUTHORIZED','complete_list':False}})
                    root.receipt['state_negative_recorded']=True

        def create_and_resume(self,root,step,deadline):
            if step.id!='server':self.sample_started[step.id]=self.state_clock()
            return super().create_and_resume(root,step,min(deadline,
                self.sample_started[step.id]+5) if step.id!='server' else deadline)

        def outcome(self,root,step):
            if step.id=='server':
                result=super().outcome(root,step)
                if result and result.get('accepted') is True and self.t0 is None:
                    self.t0=self.state_clock()
                    self.grant.sample_windows={k:(self.t0+v,self.t0+v+.5) for k,v in OFFSETS.items()}
                return result
            if root.state()!='TERMINAL' or root.receipt.get('EOF') is not True:return None
            for stream in ('stdout','stderr'):self.marker.eof(root.case,stream)
            self.marker.advance();root.identity();self.notice_gate()
            need(root.receipt.get('native_exit')==0 and root.receipt.get('stop_attempts')==0
                 and terminal_qualified(root.receipt) and not root.receipt.get('cleanup_errors'),
                 'STATE_TERMINAL_NOT_NORMAL')
            need(self.server_ok(),'STATE_SERVER_CHANGED')
            self.grant.mark_terminal(step.id,root.receipt)
            try:parsed=self.parsers[step.id].finish()
            except ValueError:raise driver_module.ContractError('FORMAT_UNKNOWN') from None
            need(parsed['device_state']!='UNAUTHORIZED','STATE_UNAUTHORIZED')
            row={'sample_index':CASES.index(step.id),'planned_offset_s':OFFSETS[step.id],
                 'started_monotonic':self.sample_started[step.id],'ended_monotonic':self.state_clock(),**parsed}
            root.close()
            accepted=terminal_qualified(root.receipt) and not root.receipt.get('cleanup_errors') and not self.journal.failed
            root.receipt.update(accepted_evidence=accepted,state_sample=row)
            self.w.put(root.dest/'RESULT.json',root.receipt)
            need(accepted and not self.journal.failed,'STATE_CLOSE_OR_RECORD_UNKNOWN')
            self.grant.accept(step.id,root.receipt);self.state_records[step.id]=row
            return {'step':step.id,'accepted':True,'terminal':True,'state_sample':row}

        def cleanup(self,root,deadline):
            try:return super().cleanup(root,deadline)
            finally:
                if root.case in self.parsers:self.parsers[root.case].erase()
                for buffer in root.output:buffer.clear()

    class StateDriver(driver_module.GoalDriver):
        def __init__(self,*args,**kwargs):super().__init__(*args,**kwargs);self.skipped={}

        def start_sample(self,identifier):
            self.check_time();need(identifier in OFFSETS and identifier not in self.used,'STATE_SLOT_REPLAY')
            start,end=self.backend.grant.sample_windows[identifier]
            if self.clock()>end:return self.skip(identifier)
            need(self.clock()>=start,'STATE_SLOT_EARLY')
            step=self.registry[identifier]
            need('server' in self.accepted and not any(s!='server' for s in self.live),'STATE_PRECONDITION')
            need(self.client_creates<2 and self.clock()+7<=self.deadline,'STATE_QUERY_BUDGET')
            try:
                self.backend.precreate(step,self.deadline);self.check_time()
                if self.clock()>end:return self.skip(identifier)
                self.used.add(identifier);self.client_creates+=1
                root=self.backend.register(step);self.live[identifier]=root
                try:self.backend.create_and_resume(root,step,self.deadline)
                except SlotMissed:
                    # Safe only before the native call. Never clear a halt or
                    # resurrect a permit after any possible actual creation.
                    need(root.receipt.get('created') is False and 'create_call_before_ns' not in root.receipt,
                         'STATE_CREATION_RESULT_UNKNOWN')
                    root.close();need(not root.receipt.get('cleanup_errors'),'STATE_UNUSED_CLOSE_UNKNOWN')
                    self.live.pop(identifier);self.terminal.add(identifier)
                    self.backend.w.put(root.dest/'RESULT.json',dict(root.receipt,state_sample='NOT_RUN_SLOT_MISSED'))
                    return self.skip(identifier,registered=True)
            except BaseException:self.halt('STATE_START_FAILED_OR_UNKNOWN');raise
            return root

        def skip(self,identifier,registered=False):
            if not registered:self.used.add(identifier)
            row={'sample_index':CASES.index(identifier),'status':'NOT_RUN_SLOT_MISSED'}
            self.backend.journal.emit({'state_sample':row})
            need(not self.backend.journal.failed,'STATE_SKIP_RECORD_FAILED')
            self.skipped[identifier]=row
            return None

        def run_samples(self):
            self.start('server');self.wait_accepted('server')
            for identifier in OFFSETS:
                target=self.backend.grant.sample_windows[identifier][0]
                while self.clock()<target:
                    self.pump();self.backend.pause(min(.05,target-self.clock()))
                if self.start_sample(identifier) is not None:self.wait(identifier)
            return self.summary()

        def summary(self):
            samples=[self.backend.state_records[k] for k in OFFSETS if k in self.backend.state_records]
            return {'state_samples':samples,'missed_slots':list(self.skipped.values()),
                    'comparison':'TWO_SAMPLE_WINDOWS' if len(samples)==2 else 'INSUFFICIENT_SAMPLES',
                    'handshake':'NOT_OBSERVED','continuous_state':'NOT_PROVED','DDS':'NOT_RUN'}

    return StateDriver,StateBackend


def actual_driver(grant):
    import goal_authorization as a
    import goal_contract as c
    c.need(grant in a._grants and c.SCOPE==SCOPE and c.NONCE==NONCE
           and getattr(grant,'strict_state_samples',False) is True,'NEW_STRICT_STATE_GRANT_REQUIRED')
    c.validate_registry(list(grant.registry.values()))
    import goal_driver,goal_windows_backend
    from goal_handle_lifecycle import terminal_receipt_qualified
    Driver,Backend=make_types(goal_driver,goal_windows_backend,terminal_receipt_qualified)
    return Driver(grant.registry,Backend(grant))


def observation_complete(driver,cleanup):
    try:
        b=driver.backend;b.notice_gate()
        if driver.skipped or set(driver.accepted)!=set(CASES) or set(driver.used)!=set(CASES):return False
        if set(b.state_records)!=set(OFFSETS) or len(b.roots)!=3 or {r.case for r in b.roots}!=set(CASES):return False
        if cleanup['failure'] is not None or cleanup['finalization']!={'state':'COMPLETE',
              'business_blocked':False,'cleanup_errors':[],'remote_UI_trust_restoration':'UNKNOWN'}:return False
        rows=cleanup['cleanup']
        if (len(rows)!=1 or rows[0].get('step')!='server' or rows[0].get('state')!='TERMINAL'
                or rows[0].get('EOF') is not True or rows[0].get('business_blocked') is not False
                or rows[0].get('cleanup_errors')):return False
        for identifier,row in b.state_records.items():
            start,end=b.grant.sample_windows[identifier]
            if not start<=row['started_monotonic']<=end or row['ended_monotonic']<row['started_monotonic']:return False
        for root in b.roots:
            if not root.closed or not b.qualify(root.receipt) or root.receipt.get('cleanup_errors'):return False
            if root.case!='server' and (root.receipt.get('accepted_evidence') is not True
                    or root.receipt.get('native_exit')!=0 or root.receipt.get('stop_attempts')!=0):return False
        return not b.journal.failed
    except BaseException:return False
