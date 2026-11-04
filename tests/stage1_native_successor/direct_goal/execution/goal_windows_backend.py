"""Goal-only live backend. Construction requires the new opaque actual grant.
Never imported by plan/fixture modes. Original handle operations are inherited
from the explicitly adapted qualified source; no Job management is introduced.
"""
import json,time,os,shutil
from pathlib import Path
from goal_contract import LIMITS,RUN_ROOT,ContractError,need
from goal_driver import aggregate_reason

class Journal:
    def __init__(self,path):
        self.file=path.open('xb');self.failed=False;self.bytes=0;self.case='UNBOUND';self.resource=False
    def emit(self,row):
        if self.failed:raise ContractError('JOURNAL_FAILED')
        data=json.dumps(row,sort_keys=True,separators=(',',':')).encode('ascii')+b'\n'
        if self.bytes+len(data)>LIMITS['api_journal_bytes']:self.failed=True;raise ContractError('JOURNAL_CAP')
        try:self.file.write(data);self.file.flush();self.bytes+=len(data)
        except BaseException:self.failed=True;raise
    def event(self,name,before,after,result,error,exception):
        # Trace.api_times records these same successful/false-return observations
        # in one bounded sample. Exceptions remain critical immediate records.
        if self.resource and exception is None:return
        self.emit({'case':self.case,'api':name,'before_ns':before,'after_ns':after,'result':result,'win32_error':error,'exception_enum':'API_EXCEPTION' if exception else None})
    def batch(self,kind,before,after,count,selected,complete,error):
        self.emit({'case':self.case,'batch':kind,'before_ns':before,'after_ns':after,'count':count,'selected':selected,'complete':complete,'error_enum':'QUERY_ERROR' if error else None})
    def rejection(self,row):
        # A rejection must survive even if later root cleanup cannot write its
        # exclusive RESULT. Do not create RESULT early or retry a failed write.
        try:
            self.emit({'evidence_rejection':row})
            os.fsync(self.file.fileno())
        except BaseException:
            self.failed=True
            raise
    def close(self):self.file.close()

class GoalWindowsBackend:
    evidence_kind='ACTUAL_NEW_GOAL_SCOPE_ONLY'
    def __init__(self,grant):
        from goal_authorization import _grants
        need(grant in _grants,'NO_ACTUAL_GRANT')
        import goal_win32_owned as w
        from goal_notice import Marker
        self.w=w;self.grant=grant;self.roots=[];self.latest={};self.baseline=None;self.stopped=False
        self.epochs=0;self.samples=0;self.last_sample=0;self.captured={};self.cache={};self.raw_notice=[];self.pins=[];self.decoded=0
        need(not RUN_ROOT.exists(),'UNKNOWN_PRIOR_RUN')
        for parent in [RUN_ROOT.parent,*RUN_ROOT.parent.parents]:need(not parent.lstat().st_file_attributes&0x400,'RUN_REPARSE')
        ctx=w.context();need(ctx['sid']==w.SID and ctx['profile']==w.PROFILE and ctx['appcontainer']==0,'CALLER_CHANGED')
        need(not w.snapshot() and not any(r['port']==8710 for r in w.tcp_table()),'EXISTING_HDC_OR_ENDPOINT')
        w.private_directory(RUN_ROOT);(RUN_ROOT/'cases').mkdir();(RUN_ROOT/'evidence').mkdir()
        self.journal=Journal(RUN_ROOT/'evidence/API.jsonl');w._journal=self.journal
        # Deny concurrent write/delete on each fixed active input through the
        # whole batch. Rehash only the explicit safe executable/source closure.
        try:
            for row in grant.pin_rows:
                handle=w.K.CreateFileW(row['path'],0x80000000,1,None,3,0x00200000,None)
                need(handle and handle!=w.INVALID,'ACTIVE_PIN_OPEN');self.pins.append(handle)
                need(w.digest(row['path'])==row['sha256'],'ACTIVE_LOCKED_HASH')
                self.journal.emit({'active_role':row['role'],'FileID':w.file_id(handle),'locked_no_write_delete':True})
        except BaseException:
            for handle in reversed(self.pins):w.K.CloseHandle(handle)
            self.pins.clear();self.journal.close();w._journal=None;raise
        self.trace=w.Trace(w.MEM,w.K,w.P,w.api)
        self.keys_before=w.key_metadata()
        self.marker=Marker(self.notice_event,cases=tuple(grant.registry),max_total=12*1048576)
    def notice_event(self,event):
        self.raw_notice.append(dict(event));self.journal.emit({'notice_event':event});return True
    def halt(self):self.stopped=True;self.grant.halt()
    def pause(self,seconds):time.sleep(max(0,min(seconds,.1)))
    def system(self):
        self.epochs+=1;need(self.epochs<=LIMITS['system_epochs'],'SYSTEM_SAMPLE_CAP');times=[]
        self.journal.resource=True
        try:sample=self.trace.system(times)
        finally:self.journal.resource=False
        self.journal.emit({'system':sample,'api_times':times});return sample
    def precreate(self,step,deadline):
        self.marker.advance();self.notice_gate()
        need(not self.stopped and time.monotonic()<deadline,'HALTED_OR_EXPIRED')
        need(all(shutil.disk_usage(d).free>=LIMITS['disk_min'] for d in ['C:/','E:/']),'DISK_MARGIN')
        sample=self.system();reason=aggregate_reason(sample,list(self.latest.values()),self.baseline,start=True);need(reason is None,reason or 'RESOURCE')
        self.storage();need(not self.journal.failed,'PRECREATE_RECORD_FAILED')
    def register(self,step):
        permit=self.grant.reserve_step(step.id);self.journal.case=step.id
        root=self.w.OwnedRoot(step.id,step.executable,list(step.args),step.executable_sha256,RUN_ROOT/'cases'/step.id,permit)
        self.roots.append(root);self.captured[step.id]=0;return root
    def create_and_resume(self,root,step,deadline):
        if step.kind!='server':need(self.server_ok(),'SERVER_NOT_READY_BEFORE_CREATE')
        self.journal.case=step.id;root.create();sample=self.observe(root,'PRE_RESUME')
        if step.kind=='server':self.baseline=sample['system']['commit_total_bytes']
        need(aggregate_reason(sample['system'],list(self.latest.values()),self.baseline,start=True) is None,'PRE_RESUME_RESOURCE')
        need(not self.journal.failed,'PRE_RESUME_RECORD_FAILED');root.resume();root.goal_started=time.monotonic()
        self.grant.mark_started(step.id,root.receipt)
    def observe(self,root,phase):
        self.samples+=1;need(self.samples<=LIMITS['process_samples'],'PROCESS_SAMPLE_CAP');self.epochs+=1;need(self.epochs<=LIMITS['system_epochs'],'SYSTEM_SAMPLE_CAP')
        if root.identity(require_live=phase=='PRE_RESUME')=='TERMINAL':
            self.latest.pop(root.case,None);return None
        self.journal.case=root.case;self.journal.resource=True
        try:sample=self.trace.observe(root.pi.hProcess,phase)
        except self.w.ObservationError as error:
            self.journal.resource=False
            self.journal.emit({'case':root.case,'resource_API_error':error.details})
            if phase=='RUNNING' and error.details['api']=='GetProcessMemoryInfo(root)' and root.identity()=='TERMINAL':
                self.latest.pop(root.case,None);return None
            raise
        finally:self.journal.resource=False
        fp=self.w.fingerprint(sample['jobs']);old=root.receipt.get('job_fingerprint');need(old is None or old==fp,'JOB_LAYER_CHANGED');root.receipt['job_fingerprint']=fp
        layer=sample['jobs'].get('caller_visible_layer') or {}
        need(layer.get('process_memory_limit') is None or layer['process_memory_limit']>=LIMITS['private'],'VISIBLE_PROCESS_LIMIT')
        need(layer.get('job_memory_limit') is None or layer['job_memory_limit']>=LIMITS['aggregate_private'],'VISIBLE_JOB_LIMIT')
        self.latest[root.case]=sample['process'];self.journal.emit({'case':root.case,'resource':sample});return sample
    def server_ok(self):
        servers=[r for r in self.roots if r.case=='server'];need(len(servers)==1,'SERVER_ROOT_COUNT');server=servers[0]
        need(server.state()=='LIVE','SERVER_DIED');server.identity(require_live=True);rows=self.w.tcp_table();selected=[r for r in rows if r['port']==8710]
        need(all(r['pid']==server.owner['pid'] and r['address'] in ('127.0.0.1','::ffff:127.0.0.1') for r in selected),'ENDPOINT_REPLACED')
        need(all(r['port']==8710 and r['address'] in ('127.0.0.1','::ffff:127.0.0.1') for r in rows if r['pid']==server.owner['pid']),'UNEXPECTED_SERVER_LISTENER')
        return bool(selected)
    def notice_gate(self):
        report=self.marker.report();events=report.get('events',[])
        # Driver-specific policy is explicit in the new effects/approval. It
        # never erases the original UNKNOWN or upgrades it to a false diagnostic.
        if not self.marker.notice:return
        need(report.get('classification')!='COMMAND_FAILURE','HDC_CHANNEL_ESTABLISHING_FAILURE')
        permitted=events and all(e.get('case')=='server' and e.get('rule_id')=='AUTH_CONTEXT_UNKNOWN' for e in events)
        need(permitted and not report.get('record_failed') and not report.get('overflow'),'NOTICE_REQUEST_DENIED_OR_UNKNOWN')
    def observe_all(self,roots,deadline):
        need(time.monotonic()<deadline,'GLOBAL_DEADLINE');now=time.monotonic()
        need(not (RUN_ROOT/'CANCEL.request').exists(),'OPERATOR_CANCEL')
        known={r.owner['pid'] for r in self.roots if r.owner is not None and not r.closed}
        selected=self.w.snapshot(known);need(not any(r['pid'] not in known or r['parent'] in known for r in selected),'UNKNOWN_HDC_OR_DESCENDANT')
        if any(r.case!='server' for r in roots):need(self.server_ok(),'SERVER_NOT_READY')
        for root in roots:
            self.journal.case=root.case
            cap=LIMITS['server_capture'] if root.case=='server' else LIMITS['client_capture']
            def observer(stream,data,before,after,root=root):self.marker.feed(root.case,stream,data,'MONITOR',before,after)
            chunks=root.read(cap-self.captured[root.case],observer);self.captured[root.case]+=sum(len(data) for _,data in chunks)
            state=root.state()
            step=self.grant.registry[root.case];duration=LIMITS['long_ms'] if step.kind=='long' else LIMITS['total_ms'] if step.kind=='server' else 110000 if step.kind=='controller' else LIMITS['client_ms']
            observed=time.monotonic();need(observed-root.goal_started<duration/1000,'ROOT_DEADLINE')
            if state=='LIVE':
                if now-self.last_sample>=.1:
                    sample=self.observe(root,'RUNNING')
                    if sample is not None:need(aggregate_reason(sample['system'],list(self.latest.values()),self.baseline) is None,'RESOURCE_LIMIT')
            elif state=='TERMINAL':
                self.latest.pop(root.case,None)
                need(observed<min(deadline,root.terminal_at+LIMITS['stop_ms']/1000),'TERMINAL_EOF_DEADLINE')
            else:raise ContractError('ROOT_STATE_UNKNOWN')
        if now-self.last_sample>=.1:self.last_sample=now
        self.notice_gate();self.storage();need(not self.journal.failed,'RECORD_FAILED')
    def outcome(self,root,step):
        if step.kind=='server' and root.state()=='LIVE':
            if not self.server_ok():return None
            self.grant.mark_server_ready(root.receipt,True)
            return {'step':step.id,'accepted':True,'terminal':False,'listener_owned':True}
        if root.state()!='TERMINAL' or not root.receipt['EOF']:return None
        for stream in ('stdout','stderr'):self.marker.eof(root.case,stream)
        self.marker.advance();root.identity();self.grant.mark_terminal(step.id,root.receipt);self.notice_gate()
        accepted=root.receipt['native_exit']==0 and not root.receipt['cleanup_errors'] and root.receipt['stop_attempts']==0
        # The finite step evaluator must supply explicit content/remote or byte
        # evidence; local exit0 alone is deliberately never acceptance.
        from goal_evidence import evaluate_step,rejection_record
        evaluated={'accepted':False}
        if accepted:
            self.decoded+=len(root.output[0])+len(root.output[1]);need(self.decoded<=LIMITS['decoded'],'DECODED_TOTAL_CAP')
            try:
                evaluated={'accepted':True,'parsed':evaluate_step(step,bytes(root.output[0]),bytes(root.output[1]),root.receipt['native_exit'])}
            except BaseException as error:
                row=rejection_record(step,error)
                root.receipt['accepted_evidence']=False
                root.receipt['evidence_rejection']=row
                root.receipt['evidence_rejection_recorded']=False
                try:
                    self.journal.rejection(row)
                    need(not self.journal.failed,'EVIDENCE_REJECTION_RECORD_FAILED')
                except BaseException:
                    raise ContractError('EVIDENCE_REJECTION_RECORD_FAILED') from None
                root.receipt['evidence_rejection_recorded']=True
                # Original error still propagates. No new accept/close/signal
                # authority; normal cleanup owns the one RESULT publication.
                raise
        accepted=accepted and evaluated.get('accepted') is True
        # Close can append late capture/handle errors without throwing. Final
        # acceptance and its durable receipt must come AFTER that boundary.
        root.close()
        accepted=accepted and not root.receipt['cleanup_errors'] and not self.journal.failed
        root.receipt['accepted_evidence']=accepted
        self.w.put(root.dest/'RESULT.json',root.receipt)
        need(not self.journal.failed,'ROOT_FINAL_RECORD_FAILED')
        if accepted:self.grant.accept(step.id,root.receipt)
        return {'step':step.id,'accepted':accepted,'terminal':True,'native_exit':root.receipt['native_exit'],'evidence':evaluated,'cleanup_errors':list(root.receipt['cleanup_errors'])}
    def storage(self):
        from goal_storage import scan
        result=scan(RUN_ROOT,self.grant.registry,self.cache);self.cache=result['cache_state']
    def cleanup(self,root,deadline):
        self.journal.case=root.case
        if root.closed:
            # No use of a closed/recycled handle and no second close attempt.
            incomplete=root.receipt.get('cleanup_errors') or root.receipt.get('EOF') is not True or root.receipt.get('terminal') is not True or ('drain' in root.receipt and root.receipt['drain'].get('status')!='EOF')
            return {'step':root.case,'state':'UNKNOWN' if incomplete else 'TERMINAL','EOF':root.receipt.get('EOF'),'drain':root.receipt.get('drain'),'cleanup_errors':list(root.receipt.get('cleanup_errors',[])),'closed_handle_not_reused':True}
        try:
            if root.state()=='LIVE':root.stop(deadline)
            drained=root.drain(deadline,max(0,(LIMITS['server_capture'] if root.case=='server' else LIMITS['client_capture'])-self.captured[root.case]),lambda stream,data,b,a:self.marker.feed(root.case,stream,data,'CLEANUP',b,a))
            self.captured[root.case]+=drained['bytes']
            state=root.state()
        finally:
            try:
                for stream in ('stdout','stderr'):
                    if next((r['eof'] for r in root.reads if r['name']==stream),False):self.marker.eof(root.case,stream,'CLEANUP')
                self.marker.advance();self.notice_gate()
            finally:
                try:root.close()
                finally:self.w.put(root.dest/'RESULT.json',root.receipt)
        need(not root.receipt['cleanup_errors'] and not self.journal.failed,'ROOT_CLEANUP_FAILED')
        return {'step':root.case,'state':state if root.receipt['EOF'] and drained['status']=='EOF' else 'UNKNOWN','process_state':state,'native_exit':root.receipt['native_exit'],'stop_attempts':root.receipt['stop_attempts'],'EOF':root.receipt['EOF'],'drain':drained,'cleanup_errors':[],'remote_tree_and_identity_restoration':'UNKNOWN_UNLESS_EXPLICIT_NATIVE_RECEIPTS'}
    def complete_cleanup(self):
        try:self.marker.close();self.notice_gate()
        finally:
            try:
                self.journal.emit({'key_metadata_before':self.keys_before,'key_metadata_after':self.w.key_metadata(),'content_or_hash':False,'UI_device_trust_restoration':'UNKNOWN'})
            finally:
                try:
                    # APIs still record into an OPEN critical journal here.
                    closed=True
                    for handle in reversed(self.pins):
                        if not self.w.K.CloseHandle(handle):closed=False
                    self.pins.clear()
                    need(closed and not self.journal.failed,'FINAL_PIN_OR_RECORD_FAILURE')
                finally:
                    try:self.journal.close()
                    finally:self.w._journal=None
