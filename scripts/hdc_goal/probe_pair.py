"""One fixed A-pair diagnostic policy; no tool or native imports on import.

Fixture construction is not authority. The actual factory requires the new
opaque grant before constructing the existing live backend.
"""
import copy
import time

SCOPE='ONE_NEW_SAME_SERVER_A_PAIR_60S_D01'
NONCE='A8D3F641_A_PAIR_60S_D01'
FIRST='probe.A1'
SECOND='probe.A2'
CASES=('server',FIRST,SECOND)
RULES=('CODE_E000004_CHANNEL_ESTABLISHING','CODE_DEVICE_SELECTION_FAILURE')
PAIR='EXACT_PUBLIC_BINDING_FAILURE_PAIR'
START_WINDOW=2.0
PUBLIC_LINES=(b'[Fail][E000004]:The communication channel is being established.',
              b'Please wait for several seconds and try again.',
              b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info')

def exact_failure_pair(stdout,stderr):
    """Whole public EchoClient envelope only; no strip, substring or hash."""
    return (type(stdout) is bytes and type(stderr) is bytes and stderr==b''
            and any(stdout==ending.join(PUBLIC_LINES)+ending
                    for ending in (b'\n',b'\r\n',b'\r\r\n')))

def make_types(driver_module,backend_module,terminal_qualified):
    """Bind the maintained driver/backend; fixtures supply no actual grant."""
    need=driver_module.need
    limits=driver_module.LIMITS

    class PairBackend(backend_module.GoalWindowsBackend):
        def __init__(self,grant):
            need(grant.pair_collection_continue is True and grant.pair_second_sample is True
                 and grant.server_unknown_continue is False,'PAIR_EFFECTS_NOT_GRANTED')
            super().__init__(grant)
            self.init_pair_state()

        def init_pair_state(self,clock=time.monotonic):
            self.pair_clock=clock;self.first_failure=None;self.first_closed_at=None
            self.first_history=None;self.server_token=None;self.server_root=None
            self.first_root=None
            self.first_qualification_attempted=False
            self.original_work_deadline=self.grant.work_deadline

        def server_same(self):
            need(self.server_root is not None and self.server_ok(),'PAIR_SERVER_NOT_OWNED')
            root=self.server_root
            token=(root.pi.hProcess,copy.deepcopy(root.owner),copy.deepcopy(root.receipt['FileID']))
            need(token==self.server_token,'PAIR_SERVER_CHANGED')

        def failure_events(self):
            report=self.marker.report();events=report.get('events')
            need(report.get('record_failed') is False and report.get('overflow') is False
                 and report.get('failure_rules')==[] and report.get('emergency_events')==[],
                 'PAIR_NOTICE_RECORD_UNRELIABLE')
            need(report.get('classification') in ('NONE','DIAGNOSTIC','COMMAND_FAILURE')
                 and type(events) is list,'PAIR_AUTH_OR_UNKNOWN')
            failures=[]
            for event in events:
                need(event.get('case') in CASES and event.get('complete_record') is True,
                     'PAIR_NOTICE_INCOMPLETE')
                kind,rule=event.get('event_type'),event.get('rule_id')
                if kind=='DIAGNOSTIC' and rule in ('DIAG_FIELD','DIAG_PATH'):continue
                need(kind=='COMMAND_FAILURE' and rule in RULES and event.get('case')==FIRST
                     and event.get('stream')=='stdout' and event.get('trigger_family')=='NOT_APPLICABLE',
                     'PAIR_NEW_BLOCKING_EVENT')
                failures.append(event)
            need(bool(failures)==(report.get('classification')=='COMMAND_FAILURE'),
                 'PAIR_NOTICE_CLASSIFICATION')
            return failures

        def notice_gate(self):
            failures=self.failure_events()
            if not failures:return
            if self.first_history is not None:
                need(failures==self.first_history,'PAIR_FAILURE_HISTORY_CHANGED')
                return
            roots=[r for r in self.roots if r.case==FIRST]
            need(len(roots)==1,'PAIR_FIRST_NOT_STARTED');root=roots[0]
            need(self.pair_clock()<root.goal_started+limits['client_ms']/1000,
                 'PAIR_FIRST_COLLECTION_DEADLINE')
            need([e['rule_id'] for e in failures] in ([RULES[0]],list(RULES)),
                 'PAIR_FAILURE_ORDER')
            if root.receipt.get('EOF') is True:
                need([e['rule_id'] for e in failures]==list(RULES)
                     and exact_failure_pair(bytes(root.output[0]),bytes(root.output[1])),
                     'PAIR_FIRST_ENVELOPE_NOT_EXACT')

        def cleanup_notice(self,errors):
            blocked=super().cleanup_notice(errors)
            # Collection permission does not turn the failed sample into
            # successful business or erase its historical blocking facts.
            if any(e.get('event_type')=='COMMAND_FAILURE' for e in self.marker.report().get('events',[])):
                blocked=True
            return blocked

        def precreate(self,step,deadline):
            if step.id==SECOND:self.check_second_window()
            super().precreate(step,deadline)
            if step.id==SECOND:self.check_second_window()

        def check_second_window(self):
            need(not self.stopped and not self.grant.stopped,'PAIR_HALTED')
            need(self.first_failure is not None and self.first_closed_at is not None,
                 'PAIR_FIRST_NOT_QUALIFIED')
            self.notice_gate();self.server_same();now=self.pair_clock()
            need(now<self.first_closed_at+START_WINDOW,'PAIR_SECOND_START_WINDOW')
            need(now+limits['client_ms']/1000<=self.original_work_deadline,
                 'PAIR_INSUFFICIENT_CLIENT_BUDGET')

        def create_and_resume(self,root,step,deadline):
            if step.id==SECOND:
                self.check_second_window()
                # The original native-boundary checks use this deadline. It
                # is only tightened and never restored or extended.
                deadline=min(deadline,self.grant.work_deadline)
            return super().create_and_resume(root,step,deadline)

        def outcome(self,root,step):
            need(not self.stopped and not self.grant.stopped,'PAIR_HALTED')
            if step.id=='server':
                result=super().outcome(root,step)
                if result and result.get('accepted') is True:
                    token=(root.pi.hProcess,copy.deepcopy(root.owner),copy.deepcopy(root.receipt['FileID']))
                    if self.server_token is None:self.server_root=root;self.server_token=token
                    else:need(root is self.server_root and token==self.server_token,'PAIR_SERVER_REPLACED')
                return result
            if step.id==FIRST and self.first_failure is not None:
                need(root is self.first_root and root.closed,'PAIR_FIRST_REENTRY')
                return copy.deepcopy(self.first_failure)
            if step.id==FIRST:
                need(not self.first_qualification_attempted,'PAIR_FIRST_QUALIFICATION_ALREADY_USED')
            if (step.id!=FIRST or root.state()!='TERMINAL' or root.receipt.get('EOF') is not True
                    or not exact_failure_pair(bytes(root.output[0]),bytes(root.output[1]))):
                return super().outcome(root,step)
            self.first_qualification_attempted=True
            self.notice_gate();self.server_same()
            for stream in ('stdout','stderr'):self.marker.eof(root.case,stream)
            self.marker.advance();root.identity()
            need(terminal_qualified(root.receipt) and root.receipt.get('native_exit')==0
                 and root.receipt.get('stop_attempts')==0 and not root.receipt.get('cleanup_errors'),
                 'PAIR_FIRST_TERMINAL_NOT_NORMAL')
            self.decoded+=len(root.output[0])+len(root.output[1])
            need(self.decoded<=limits['decoded'],'DECODED_TOTAL_CAP')
            self.grant.mark_terminal(step.id,root.receipt)
            root.receipt['accepted_evidence']=False
            root.receipt['diagnostic_sample_failure']=PAIR
            closed=super().cleanup(root,min(self.original_work_deadline,self.pair_clock()+2))
            need(closed.get('state')=='TERMINAL' and closed.get('EOF') is True
                 and closed.get('terminal_binding_verified') is True and not closed.get('cleanup_errors')
                 and root.closed and terminal_qualified(root.receipt) and not self.journal.failed,
                 'PAIR_FIRST_CLOSE_OR_RECORD_UNKNOWN')
            self.first_closed_at=self.pair_clock()
            self.first_history=copy.deepcopy(self.failure_events())
            need([e['rule_id'] for e in self.first_history]==list(RULES),'PAIR_FIRST_RULES')
            self.server_same()
            observed={'step':FIRST,'accepted':False,'terminal':True,'observed_failure':PAIR,
                      'cleanup':closed,'first_failed':True}
            # Existing durable rejection journal; no new logger or raw output.
            self.journal.rejection({'step':FIRST,'stage':'PAIR_FIRST_SAMPLE',
                                    'reason':PAIR,'accepted':False})
            need(not self.journal.failed,'PAIR_FIRST_RECORD_FAILED')
            need(self.pair_clock()<self.first_closed_at+START_WINDOW,'PAIR_SECOND_START_WINDOW')
            self.grant.work_deadline=min(self.grant.work_deadline,self.first_closed_at+START_WINDOW,
                self.original_work_deadline-limits['client_ms']/1000)
            self.first_root=root
            self.first_failure=copy.deepcopy(observed)
            return observed

    class PairDriver(driver_module.GoalDriver):
        def start(self,identifier):
            self.check_time()
            need(identifier in CASES,'PAIR_UNKNOWN_STEP')
            if identifier==SECOND:
                try:
                    need(FIRST in self.terminal and FIRST not in self.accepted
                         and self.backend.first_failure is not None,'PAIR_SECOND_NOT_ELIGIBLE')
                    self.backend.check_second_window()
                except BaseException:
                    self.halt('PAIR_SECOND_GATE_FAILED');raise
            return super().start(identifier)

        def pump(self):
            self.check_time()
            try:
                self.backend.observe_all(tuple(self.live.values()),self.deadline)
                for identifier,root in list(self.live.items()):
                    outcome=self.backend.outcome(root,self.registry[identifier])
                    if outcome is None:continue
                    if (identifier==FIRST and outcome.get('observed_failure')==PAIR
                            and outcome.get('accepted') is False and outcome.get('terminal') is True
                            and outcome==self.backend.first_failure):
                        self.terminal.add(identifier);self.live.pop(identifier)
                        self.receipts.append(outcome);continue
                    if outcome.get('accepted') is not True:
                        self.halt('STEP_FAILED_OR_UNKNOWN');raise driver_module.ContractError('STEP_FAILED_OR_UNKNOWN')
                    if outcome.get('terminal') is False:
                        if identifier not in self.accepted:self.receipts.append(outcome)
                        self.accepted.add(identifier);continue
                    self.terminal.add(identifier);self.live.pop(identifier)
                    self.receipts.append(outcome);self.accepted.add(identifier)
            except BaseException:
                self.halt(self.failure or 'OBSERVATION_OR_RECEIPT_UNKNOWN');raise

        def run_pair(self):
            self.start('server');self.wait_accepted('server')
            self.start(FIRST);self.wait(FIRST)
            if FIRST not in self.accepted:
                self.start(SECOND);self.wait(SECOND)
            return self.summary()

        def summary(self):
            if FIRST in self.accepted:outcome='FIRST_SAMPLE_SUCCESS'
            elif self.backend.first_failure is not None and SECOND in self.accepted:
                outcome='OBSERVED_FAILURE_THEN_RECOVERY'
            elif self.backend.first_failure is not None:outcome='NO_SECOND_SUCCESS_OBSERVED'
            else:outcome='STOPPED_OR_UNKNOWN'
            return {'diagnostic_outcome':outcome,'first_accepted':FIRST in self.accepted,
                    'second_accepted':SECOND in self.accepted,'samples_created':self.client_creates,
                    'first_failure':copy.deepcopy(self.backend.first_failure),
                    'first_failure_not_accepted':self.backend.first_failure is not None,
                    'handshake_bit':'NOT_OBSERVED','cold_start_cause':'NOT_PROVED',
                    'continuous_or_permanent_failure':'NOT_PROVED','DDS':'NOT_RUN'}

    return PairDriver,PairBackend

def actual_driver(grant):
    # No native module or live backend before the new opaque scope grant.
    import goal_authorization as authorization
    import goal_contract as contract
    contract.need(grant in authorization._grants and contract.SCOPE==SCOPE and contract.NONCE==NONCE,
                  'NEW_PAIR_GRANT_REQUIRED')
    contract.validate_registry(list(grant.registry.values()))
    import goal_driver,goal_windows_backend
    from goal_handle_lifecycle import terminal_receipt_qualified
    Driver,Backend=make_types(goal_driver,goal_windows_backend,terminal_receipt_qualified)
    return Driver(grant.registry,Backend(grant))

def observation_complete(driver,cleanup):
    """Diagnostic completion only; never clear a failed sample or failure."""
    try:
        status=driver.summary()['diagnostic_outcome']
        if status not in ('FIRST_SAMPLE_SUCCESS','OBSERVED_FAILURE_THEN_RECOVERY'):return False
        driver.backend.failure_events() # late auth/UNKNOWN still disqualifies
        if cleanup['finalization']['state']!='COMPLETE' or cleanup['finalization']['cleanup_errors']:
            return False
        if (len(cleanup['cleanup'])!=1 or any(r.get('state')!='TERMINAL' or r.get('EOF') is not True
                or r.get('cleanup_errors') for r in cleanup['cleanup'])):return False
        if status=='FIRST_SAMPLE_SUCCESS':return cleanup['failure'] is None
        return (cleanup['failure']=='CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN'
                and driver.backend.first_failure['accepted'] is False
                and driver.backend.first_failure['cleanup']['business_blocked'] is True)
    except BaseException:return False
