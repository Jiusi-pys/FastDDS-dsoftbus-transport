# Maintained host backend derived from frozen readiness source aa6f06d56660f338a9394c7e7d54e69e52576972f272366045804f47851c46b8
# No executable entry, approval, registry or native payload is added here.
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
        self.stream_bytes={};self.capture_failed=False;self.root_samples={};self.epochs=0;self.samples=0;self.last_sample=0;self.captured={};self.cache={};self.raw_notice=[];self.pins=[];self.decoded=0
        need(not RUN_ROOT.exists(),'UNKNOWN_PRIOR_RUN')
        for parent in [RUN_ROOT.parent,*RUN_ROOT.parent.parents]:need(not parent.lstat().st_file_attributes&0x400,'RUN_REPARSE')
        ctx=w.context();need(ctx['sid']==w.SID and Path(ctx['profile'])==Path(w.PROFILE) and ctx['appcontainer']==0,'CALLER_CHANGED')
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
        self.marker=Marker(self.notice_event,cases=tuple(grant.registry),max_total=LIMITS['server_capture']+2*LIMITS['client_capture'])
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
        need(time.monotonic()<deadline,'WORK_DEADLINE_BEFORE_CREATE')
        if step.kind!='server':need(self.server_ok(),'SERVER_NOT_READY_BEFORE_CREATE')
        self.journal.case=step.id;root.goal_started=time.monotonic()
        need(root.goal_started<deadline,'WORK_DEADLINE_AFTER_SERVER_CHECK')
        duration=LIMITS['long_ms'] if step.kind=='long' else LIMITS['total_ms'] if step.kind=='server' else 110000 if step.kind=='controller' else LIMITS['client_ms']
        identity_deadline=min(deadline,root.goal_started+duration/1000)
        root.identity_deadline=min(identity_deadline,root.goal_started+LIMITS['bind_ms']/1000)
        root.create();sample=self.observe(root,'PRE_RESUME')
        if step.kind=='server':self.baseline=sample['system']['commit_total_bytes']
        need(aggregate_reason(sample['system'],list(self.latest.values()),self.baseline,start=True) is None,'PRE_RESUME_RESOURCE')
        need(not self.journal.failed,'PRE_RESUME_RECORD_FAILED')
        need(time.monotonic()<deadline,'WORK_DEADLINE_BEFORE_RESUME')
        need(time.monotonic()-root.goal_started<=LIMITS['bind_ms']/1000,'BIND_DEADLINE_BEFORE_RESUME')
        root.resume();need(time.monotonic()-root.goal_started<=LIMITS['bind_ms']/1000,'BIND_DEADLINE')
        need(time.monotonic()<deadline,'WORK_DEADLINE_AFTER_RESUME')
        root.identity_deadline=identity_deadline
        self.grant.mark_started(step.id,root.receipt)
    def observe(self,root,phase):
        self.journal.case=root.case
        self.root_samples[root.case]=self.root_samples.get(root.case,0)+1;need(self.root_samples[root.case]<=(LIMITS['server_samples'] if root.case=='server' else LIMITS['client_samples']),'ROOT_SAMPLE_CAP');self.samples+=1;need(self.samples<=LIMITS['process_samples'],'PROCESS_SAMPLE_CAP');self.epochs+=1;need(self.epochs<=LIMITS['system_epochs'],'SYSTEM_SAMPLE_CAP')
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
        previous=self.journal.case;self.journal.case=server.case
        try:
            need(server.state()=='LIVE','SERVER_DIED');server.identity(require_live=True);rows=self.w.tcp_table();selected=[r for r in rows if r['port']==8710]
        finally:self.journal.case=previous
        need(all(r['pid']==server.owner['pid'] and r['address'] in ('127.0.0.1','::ffff:127.0.0.1') for r in selected),'ENDPOINT_REPLACED')
        need(all(r['port']==8710 and r['address'] in ('127.0.0.1','::ffff:127.0.0.1') for r in rows if r['pid']==server.owner['pid']),'UNEXPECTED_SERVER_LISTENER')
        return bool(selected)
    def notice_gate(self):
        report=self.marker.report();events=report.get('events',[])
        # Driver-specific policy is explicit in the new effects/approval. It
        # never erases the original UNKNOWN or upgrades it to a false diagnostic.
        if not self.marker.notice:return
        if report.get('classification')=='COMMAND_FAILURE':
            need(False,self.marker.reason())
        permitted=self.grant.server_unknown_continue is True and events and all(e.get('case')=='server' and e.get('rule_id')=='AUTH_CONTEXT_UNKNOWN' for e in events)
        need(permitted and not report.get('record_failed') and not report.get('overflow'),'NOTICE_REQUEST_DENIED_OR_UNKNOWN')
    def observe_chunk(self,root,stream,data,before,after,phase):
        need(not self.capture_failed and stream in ('stdout','stderr'),'CAPTURE_STATE')
        key=(root.case,stream);self.stream_bytes[key]=self.stream_bytes.get(key,0)+len(data)
        cap=LIMITS['server_capture'] if root.case=='server' else LIMITS['stream_capture']
        if self.stream_bytes[key]>cap:
            self.capture_failed=True;raise ContractError('STREAM_OUTPUT_CAP')
        self.marker.feed(root.case,stream,data,phase,before,after)
    def observe_all(self,roots,deadline):
        need(time.monotonic()<deadline,'GLOBAL_DEADLINE');now=time.monotonic()
        need(not (RUN_ROOT/'CANCEL.request').exists(),'OPERATOR_CANCEL')
        known={r.owner['pid'] for r in self.roots if r.owner is not None and not r.closed}
        selected=self.w.snapshot(known);need(not any(r['pid'] not in known or r['parent'] in known for r in selected),'UNKNOWN_HDC_OR_DESCENDANT')
        if any(r.case!='server' for r in roots):need(self.server_ok(),'SERVER_NOT_READY')
        for root in roots:
            self.journal.case=root.case
            cap=LIMITS['server_capture'] if root.case=='server' else LIMITS['client_capture']
            def observer(stream,data,before,after,root=root):self.observe_chunk(root,stream,data,before,after,'MONITOR')
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
        self.journal.case=root.case
        if step.kind=='server' and root.state()=='LIVE':
            if not self.server_ok():
                need(time.monotonic()-root.goal_started<LIMITS['server_ready_ms']/1000,'SERVER_LISTENER_DEADLINE');return None
            need(time.monotonic()<min(root.identity_deadline,root.goal_started+LIMITS['server_ready_ms']/1000),'SERVER_ACCEPT_DEADLINE')
            self.grant.mark_server_ready(root.receipt,True)
            return {'step':step.id,'accepted':True,'terminal':False,'listener_owned':True}
        if root.state()!='TERMINAL' or not root.receipt['EOF']:return None
        for stream in ('stdout','stderr'):self.marker.eof(root.case,stream)
        self.marker.advance();root.identity();need(self.server_ok(),'SERVER_NOT_READY_AT_ACCEPT');self.grant.mark_terminal(step.id,root.receipt);self.notice_gate()
        from goal_handle_lifecycle import terminal_receipt_qualified
        accepted=(root.receipt['native_exit']==0 and not root.receipt['cleanup_errors']
                  and root.receipt['stop_attempts']==0 and terminal_receipt_qualified(root.receipt))
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
        accepted=accepted and terminal_receipt_qualified(root.receipt) and not root.receipt['cleanup_errors'] and not self.journal.failed
        # This exclusive per-root file is a pre-accept observation. Publication
        # or the final server check can cross a deadline. Only the existing
        # batch accepted ledger is authoritative for the later grant commit.
        root.receipt.update(content_evidence_qualified=accepted,accepted_evidence=False,
                            step_acceptance='PENDING' if accepted else 'REJECTED')
        self.w.put(root.dest/'RESULT.json',root.receipt)
        need(not self.journal.failed,'ROOT_FINAL_RECORD_FAILED')
        if accepted:
            try:
                need(self.server_ok(),'SERVER_CHANGED_BEFORE_ACCEPT')
                need(time.monotonic()<root.identity_deadline,'STEP_ACCEPT_DEADLINE')
                root.receipt['accepted_evidence']=True
                self.grant.accept(step.id,root.receipt)
                root.receipt['step_acceptance']='ACCEPTED'
            except BaseException:
                root.receipt.update(accepted_evidence=False,step_acceptance='REJECTED')
                raise
        return {'step':step.id,'accepted':accepted,'terminal':True,'native_exit':root.receipt['native_exit'],'evidence':evaluated,'cleanup_errors':list(root.receipt['cleanup_errors'])}
    def storage(self):
        from goal_storage import scan
        result=scan(RUN_ROOT,self.grant.registry,self.cache);self.cache=result['cache_state']
        from goal_authorization import active_inventory
        active=active_inventory(self.grant.pin_rows);need(active['bytes']+result['bytes']<=LIMITS['total_bytes'],'TOTAL_BYTE_CAP')
    def cleanup_notice(self,errors):
        """Policy still blocks business; it is not an OS-close failure by itself."""
        blocked=False
        try:
            report=self.marker.report()
            if report.get('record_failed') is not False or report.get('overflow') is not False:
                errors.append('NOTICE_RECORD_UNRELIABLE')
            # Only a healthy, recognized policy observation is separable from
            # OS cleanup. Clock/binding/encoding/sink/cap and unknown future
            # rule/type combinations remain observation/cleanup UNKNOWN.
            policy_pairs={('UNKNOWN','AUTH_CONTEXT_UNKNOWN'),
                ('REQUEST_TEXT','CODE_E000002'),('REQUEST_TEXT','REQUEST_SENTENCE'),
                ('DENIED_TEXT','CODE_E000003'),
                ('COMMAND_FAILURE','CODE_E000004_CHANNEL_ESTABLISHING'),
                ('COMMAND_FAILURE','CODE_DEVICE_SELECTION_FAILURE'),
                ('DIAGNOSTIC','DIAG_FIELD'),('DIAGNOSTIC','DIAG_PATH')}
            events=report.get('events')
            reliable=(type(events) is list and (not self.marker.notice or bool(events))
                and type(report.get('failure_rules')) is list and not report['failure_rules']
                and type(report.get('emergency_events')) is list and not report['emergency_events']
                and all(type(e) is dict and e.get('complete_record') is True
                        and (e.get('event_type'),e.get('rule_id')) in policy_pairs
                        for e in events))
            if not reliable:errors.append('NOTICE_OBSERVATION_UNRELIABLE')
            try:self.notice_gate()
            except ContractError:blocked=True
        except BaseException:
            errors.append('NOTICE_CHECK_FAILED');blocked=True
        return blocked

    def cleanup(self,root,deadline):
        errors=[];blocked=False;phase='SET_CASE'
        try:
            self.journal.case=root.case
            if not root.closed:
                phase='STATE_OR_IDENTITY'
                if root.state()=='LIVE':
                    # A revoked signal capability is never recreated merely
                    # to finish cleanup. Read/drain and prove terminal only.
                    if root.receipt.get('signal_authority') is True:
                        phase='STOP';root.stop(deadline)
                    else:root.receipt['stop_skipped']='NO_SIGNAL_AUTHORITY'
                phase='DRAIN'
                need(not self.capture_failed,'CAPTURE_ALREADY_FAILED_NO_DRAIN')
                drained=root.drain(deadline,max(0,(LIMITS['server_capture'] if root.case=='server' else LIMITS['client_capture'])-self.captured[root.case]),lambda stream,data,b,a:self.observe_chunk(root,stream,data,b,a,'CLEANUP'))
                self.captured[root.case]+=drained['bytes']
                phase='TERMINAL_IDENTITY';root.state();root.identity()
        except BaseException:
            errors.append(phase+'_FAILED')
        finally:
            # Notifications cannot skip closing an owned handle or publishing
            # the independently observed root facts. Each failure is retained.
            if not root.closed:
                for stream in ('stdout','stderr'):
                    try:
                        if next((r['eof'] for r in root.reads if r['name']==stream),False):
                            self.marker.eof(root.case,stream,'CLEANUP')
                    except BaseException:errors.append('NOTICE_EOF_FAILED')
                try:self.marker.advance()
                except BaseException:errors.append('NOTICE_ADVANCE_FAILED')
                blocked=self.cleanup_notice(errors)
                try:root.close()
                except BaseException:errors.append('ROOT_CLOSE_FAILED')
                try:self.w.put(root.dest/'RESULT.json',root.receipt)
                except BaseException:errors.append('ROOT_RESULT_RECORD_FAILED')
        # Never turn an incomplete observation into success. In particular an
        # identity failure has no fallback PID open, signal, or retry here.
        receipt=root.receipt
        if receipt.get('cleanup_errors'):errors.append('ROOT_REPORTED_CLEANUP_ERROR')
        if self.journal.failed:errors.append('JOURNAL_FAILED')
        qualified=(root.closed and receipt.get('terminal') is True
                   and receipt.get('terminal_binding_verified') is True
                   and receipt.get('EOF') is True
                   and ('drain' not in receipt or receipt['drain'].get('status')=='EOF'))
        return {'step':root.case,'state':'TERMINAL' if qualified and not errors else 'UNKNOWN',
                'EOF':receipt.get('EOF'),'native_exit':receipt.get('native_exit'),
                'stop_attempts':receipt.get('stop_attempts'),'drain':receipt.get('drain'),
                'terminal_binding_verified':receipt.get('terminal_binding_verified'),
                'business_blocked':blocked,'cleanup_errors':errors,
                'remote_shell_termination':'UNKNOWN','closed_handle_not_reused':True}

    def complete_cleanup(self):
        errors=[]
        try:self.marker.close()
        except BaseException:errors.append('NOTICE_FINALIZE_FAILED')
        blocked=self.cleanup_notice(errors)
        try:
            self.journal.emit({'key_metadata_before':self.keys_before,'key_metadata_after':self.w.key_metadata(),'content_or_hash':False,'UI_device_trust_restoration':'UNKNOWN'})
        except BaseException:errors.append('KEY_METADATA_OR_RECORD_FAILED')
        # Detach the owned pin list first. Every original handle is attempted
        # at most once, even if one close or its API journal record fails.
        pins=self.pins;self.pins=[]
        for handle in reversed(pins):
            try:
                if not self.w.K.CloseHandle(handle):errors.append('PIN_CLOSE_FAILED')
            except BaseException:errors.append('PIN_CLOSE_UNKNOWN')
        try:self.journal.close()
        except BaseException:errors.append('JOURNAL_CLOSE_FAILED')
        finally:self.w._journal=None
        if self.journal.failed:errors.append('JOURNAL_FAILED')
        return {'state':'UNKNOWN' if errors else 'COMPLETE','business_blocked':blocked,
                'cleanup_errors':errors,'remote_UI_trust_restoration':'UNKNOWN'}
