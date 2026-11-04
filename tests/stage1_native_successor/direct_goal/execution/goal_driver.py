"""Finite fixed-step scheduler shared by actual and explicitly fake backends.

No backend is chosen from an input bool. Actual construction first consumes
the new Goal authorization; importing this module cannot create a process.
"""
import time
from goal_contract import LIMITS,ContractError,need

class GoalDriver:
    def __init__(self,registry,backend,*,clock=time.monotonic):
        self.registry=registry;self.backend=backend;self.clock=clock
        self.started=clock();self.deadline=self.started+LIMITS['total_ms']/1000
        self.used=set();self.accepted=set();self.live={};self.terminal=set()
        self.failure=None;self.receipts=[];self.client_creates=0;self.controller_creates=0
    def halt(self,reason):
        if self.failure is None:self.failure=reason
        self.backend.halt()
    def check_time(self):
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
        self.backend.precreate(step,self.deadline)
        # Register the opportunity BEFORE a possibly partially successful create.
        self.used.add(identifier)
        if step.kind not in ('server','controller'):self.client_creates+=1
        if step.kind=='controller':self.controller_creates+=1
        try:
            root=self.backend.register(step);self.live[identifier]=root
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
        # No new command is issued by cleanup. Existing original handles only.
        # Clients before controller/server; unknown authority is never invented.
        order=sorted(self.live,key=lambda s:self.registry[s].kind in ('server','controller'))
        cleanup=[]
        for identifier in order:
            root=self.live[identifier]
            try:cleanup.append(self.backend.cleanup(root,min(self.deadline,self.clock()+2)))
            except BaseException:cleanup.append({'step':identifier,'state':'UNKNOWN','authority_not_expanded':True})
        close=getattr(self.backend,'complete_cleanup',None)
        if close:
            try:close()
            except BaseException:cleanup.append({'state':'UNKNOWN','reason':'FINAL_NOTICE_OR_RECORD_FAILURE'})
        return {'failure':self.failure,'used':sorted(self.used),'accepted':sorted(self.accepted),'receipts':self.receipts,'cleanup':cleanup,'backend_evidence':self.backend.evidence_kind,'DDS_verified':False}

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
    return GoalDriver(registry,GoalWindowsBackend(grant))
