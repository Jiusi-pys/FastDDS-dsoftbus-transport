# Maintained scheduler derived from frozen readiness source 92b4da231f832ac382d70a8d837a44a9f6a1d2be42622f704591ab6c551abfcb
"""Finite fixed-step scheduler shared by actual and explicitly fake backends.

No backend is chosen from an input bool. Actual construction first consumes
the new Goal authorization; importing this module cannot create a process.
"""
import time
from goal_contract import LIMITS,ContractError,need

class GoalDriver:
    def __init__(self,registry,backend,*,clock=time.monotonic):
        self.registry=registry;self.backend=backend;self.clock=clock
        self.started=clock();self.hard_deadline=self.started+LIMITS['total_ms']/1000;self.deadline=self.started+LIMITS['work_ms']/1000
        if hasattr(backend,'grant'):
            self.hard_deadline=min(self.hard_deadline,backend.grant.deadline)
            self.deadline=min(self.deadline,self.hard_deadline-4)
            backend.grant.work_deadline=self.deadline
        self.used=set();self.accepted=set();self.live={};self.terminal=set()
        self.failure=None;self.receipts=[];self.client_creates=0;self.controller_creates=0
        self._finish_started=False;self._finish_result=None;self._finish_reentered=False
    def halt(self,reason):
        if self.failure is None:self.failure=reason
        self.backend.halt()
    def check_time(self):
        need(not self._finish_started,'BATCH_FINISHED')
        need(self.failure is None,'BATCH_ALREADY_STOPPED')
        if self.clock()>=self.deadline:self.halt('GLOBAL_DEADLINE');raise ContractError('GLOBAL_DEADLINE')
    def start(self,identifier):
        self.check_time();need(identifier in self.registry and identifier not in self.used,'STEP_REPLAY_OR_UNKNOWN')
        step=self.registry[identifier]
        need(set(step.requires)<=self.accepted,'DEPENDENCY_NOT_ACCEPTED')
        need(set(step.requires_started)<=(set(self.live)|self.terminal) and set(step.requires_terminal)<=self.terminal,'ROOT_PHASE_NOT_READY')
        if step.kind not in ('server','controller'):
            need(self.client_creates<LIMITS['clients'],'CLIENT_CREATION_CAP')
            clients=sum(self.registry[s].kind not in ('server','controller') for s in self.live)
            need(clients<LIMITS['concurrent_clients'],'CONCURRENT_CLIENT_CAP')
        if step.kind=='server':need(not any(self.registry[s].kind=='server' for s in self.used),'SERVER_ONCE')
        if step.kind=='controller':need(self.controller_creates==0,'CONTROLLER_ONCE')
        try:
            self.backend.precreate(step,self.deadline)
            self.check_time()
        except BaseException:
            self.halt(self.failure or 'PRECREATE_FAILED_OR_UNKNOWN');raise
        # Register the opportunity BEFORE a possibly partially successful create.
        self.used.add(identifier)
        if step.kind not in ('server','controller'):self.client_creates+=1
        if step.kind=='controller':self.controller_creates+=1
        try:
            root=self.backend.register(step);self.live[identifier]=root
            self.check_time()
            self.backend.create_and_resume(root,step,self.deadline)
        except BaseException:
            self.halt('CREATE_OR_BIND_UNKNOWN');raise
        return root
    def pump(self):
        self.check_time()
        try:
            self.backend.observe_all(tuple(self.live.values()),self.deadline)
            for identifier,root in list(self.live.items()):
                outcome=self.backend.outcome(root,self.registry[identifier])
                if outcome is None:continue
                if outcome.get('accepted') is not True:
                    self.halt('STEP_FAILED_OR_UNKNOWN');raise ContractError('STEP_FAILED_OR_UNKNOWN')
                if outcome.get('terminal') is False:
                    if identifier not in self.accepted:self.receipts.append(outcome)
                    self.accepted.add(identifier);continue
                self.terminal.add(identifier);self.live.pop(identifier)
                self.receipts.append(outcome)
                self.accepted.add(identifier)
        except BaseException:
            self.halt(self.failure or 'OBSERVATION_OR_RECEIPT_UNKNOWN');raise
    def wait(self,identifier):
        while identifier not in self.terminal:
            self.pump();self.backend.pause(.05)
        return next(r for r in reversed(self.receipts) if r['step']==identifier)
    def wait_accepted(self,identifier):
        while identifier not in self.accepted:
            self.pump();self.backend.pause(.05)
    def finish(self):
        # This is a one-shot teardown, not a retry mechanism. A reentrant call
        # has no effects and cannot publish an in-progress result as success.
        if self._finish_result is not None:return self._finish_result
        if self._finish_started:
            self._finish_reentered=True
            return {'failure':self.failure or 'CLEANUP_REENTRY_UNKNOWN',
                    'cleanup':[{'state':'UNKNOWN','reason':'CLEANUP_IN_PROGRESS'}]}
        self._finish_started=True
        cleanup=[];finalization=None
        try:self.backend.halt()
        except BaseException:
            cleanup.append({'state':'UNKNOWN','reason':'BACKEND_HALT_FAILED'})
            if self.failure is None:self.failure='RESOURCE_CLEANUP_UNKNOWN'
        order=sorted(self.live,key=lambda s:self.registry[s].kind in ('server','controller'))
        for identifier in order:
            root=self.live[identifier]
            try:
                row=self.backend.cleanup(root,min(self.hard_deadline,self.clock()+2))
                need(type(row) is dict and row.get('step')==identifier
                     and row.get('state') in ('TERMINAL','UNKNOWN'),'CLEANUP_RECEIPT')
            except BaseException:
                row={'step':identifier,'state':'UNKNOWN','authority_not_expanded':True,
                     'reason':'ROOT_CLEANUP_FAILED_OR_UNKNOWN'}
            cleanup.append(row)
            if row.get('state')!='TERMINAL' or row.get('EOF') is not True:
                if self.failure is None:self.failure='RESOURCE_CLEANUP_UNKNOWN'
            if row.get('business_blocked') is True and self.failure is None:
                self.failure='CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN'
        try:
            finalization=self.backend.complete_cleanup()
            need(type(finalization) is dict and finalization.get('state') in ('COMPLETE','UNKNOWN'),
                 'FINAL_CLEANUP_RECEIPT')
        except BaseException:
            finalization={'state':'UNKNOWN','cleanup_errors':['FINAL_CLEANUP_FAILED_OR_UNKNOWN']}
        if finalization.get('state')!='COMPLETE':
            if self.failure is None:self.failure='RESOURCE_CLEANUP_UNKNOWN'
        if finalization.get('business_blocked') is True and self.failure is None:
            self.failure='CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN'
        if self._finish_reentered:
            if self.failure is None:self.failure='CLEANUP_REENTRY_UNKNOWN'
        self._finish_result={'failure':self.failure,'used':sorted(self.used),
            'accepted':sorted(self.accepted),'receipts':self.receipts,'cleanup':cleanup,
            'finalization':finalization,'backend_evidence':self.backend.evidence_kind,'DDS_verified':False}
        return self._finish_result


def aggregate_reason(system,processes,baseline,*,start=False):
    if system['commit_available_bytes']<(LIMITS['start_commit'] if start else LIMITS['stop_commit']):return 'SYSTEM_COMMIT_MARGIN'
    if system['physical_available_bytes']<(LIMITS['start_physical'] if start else LIMITS['stop_physical']):return 'SYSTEM_PHYSICAL_MARGIN'
    if baseline is not None and system['commit_total_bytes']-baseline>LIMITS['commit_delta']:return 'SYSTEM_COMMIT_DELTA'
    if any(p['private_bytes']>LIMITS['private'] or p['working_set_bytes']>LIMITS['ws'] for p in processes):return 'ROOT_MEMORY'
    if sum(p['private_bytes'] for p in processes)>LIMITS['aggregate_private'] or sum(p['working_set_bytes'] for p in processes)>LIMITS['aggregate_ws']:return 'AGGREGATE_MEMORY'
    return None

def actual_driver(package,manifest_sha,binding_sha,registry):
    # No live module/API, process or preflight side effect before new grant.
    from goal_authorization import load_actual_grant
    grant=load_actual_grant(package,manifest_sha,binding_sha,registry)
    from goal_windows_backend import GoalWindowsBackend
    return GoalDriver(grant.registry,GoalWindowsBackend(grant))
