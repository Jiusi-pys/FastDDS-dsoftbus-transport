"""Reuse D04's monitored observation window and one bounded target snapshot."""
import time
import readonly_evidence as evidence

SCOPE='ONE_NEW_D04_PREPARED_AB_READONLY_EVIDENCE_60S_D02'
NONCE='F6A1842D_D04_PREPARED_READONLY_60S_D02'
CASES=('server','targets','read.B','read.A')


class TargetPair:
    """Same bounded list, two fixed-target projections, no device fields out."""
    def __init__(self,targets,emit=None):
        from fixed_target_listing import Listing
        self.parsers={s:Listing(targets[s]) for s in ('A','B')};self.rows={};self.emit=emit or (lambda r:None)
    def feed(self,data):
        for parser in self.parsers.values():parser.feed(data)
        evidence.require(not any(p.failed for p in self.parsers.values()),'TARGET_LIST_FORMAT')
        evidence.require(not any(p.negative for p in self.parsers.values()),'TARGET_UNAUTHORIZED')
    def finish(self):
        try:rows={s:p.finish() for s,p in self.parsers.items()}
        except ValueError:
            self.emit({'prepared_target_rejection':{'reason':'TARGET_LIST_FORMAT'}})
            raise evidence.EvidenceUnknown('TARGET_LIST_FORMAT') from None
        snapshot={s:{'present':r['present'],'unique':r['target_unique'],
          'state':r['device_state'] if r['present'] else 'ABSENT'} for s,r in rows.items()}
        self.emit({'prepared_target_snapshot':{'fixed_targets':snapshot,'accepted':False,
          'meaning':'COMPLETE_LIST_BEFORE_TERMINAL_CLOSE_ACCEPTANCE','handshake_proved':False}})
        if not all(r['present'] and r['device_state']=='CONNECTED' for r in rows.values()):
            self.emit({'prepared_target_rejection':{'reason':'FIXED_TARGET_NOT_CONNECTED'}})
        evidence.require(all(r['present'] and r['target_unique'] and r['format_valid']
          and r['complete_list'] and r['device_state']=='CONNECTED' for r in rows.values()),'FIXED_TARGET_NOT_CONNECTED')
        return {'source_bytes':0,'listing_bytes':max(p.total for p in self.parsers.values()),
          'fixed_targets':{s:{'present':True,'unique':True,'state':'CONNECTED'} for s in rows},
          'list_complete':True,'handshake_proved':False,'future_ready_proved':False}
    def erase(self):
        for p in self.parsers.values():p.erase()


def make_types(driver_module,backend_module,qualified):
    D,B=evidence.make_types(driver_module,backend_module,qualified)
    need=driver_module.need
    class ReadonlyBackend(B):
        def register(self,step):
            if step.id!='targets':return super().register(step)
            from goal_contract import TARGETS
            root=backend_module.GoalWindowsBackend.register(self,step)
            self.parsers[step.id]=TargetPair(TARGETS,self.journal.emit);return root
        def create_and_resume(self,root,step,deadline):
            if step.id!='server':
                duration=5 if step.id=='targets' else 15
                need(time.monotonic()+duration<=min(deadline,self.grant.work_deadline),'FULL_QUERY_BUDGET')
                deadline=min(deadline,time.monotonic()+duration)
            return super().create_and_resume(root,step,deadline)
        def observe_all(self,roots,deadline):
            for root in roots:
                if root.case=='targets':need(time.monotonic()<root.identity_deadline,'TARGETS_QUERY_DEADLINE')
            result=super().observe_all(roots,deadline)
            for root in roots:
                if root.case=='targets':need(time.monotonic()<root.identity_deadline,'TARGETS_QUERY_DEADLINE')
            return result
        def outcome(self,root,step):
            if step.id=='server' and 'server' in self.grant.accepted:
                # Keep monitoring an already accepted server; startup's ten
                # seconds applies only to its first ready observation.
                need(time.monotonic()<min(self.grant.work_deadline,root.identity_deadline),'SERVER_MONITOR_DEADLINE')
                root.identity(require_live=True);need(self.server_ok(),'SERVER_LISTENER_CHANGED')
                self.notice_gate();return None
            return super().outcome(root,step)
    class ReadonlyDriver(D):
        def run_readonly(self):
            self.start('server');self.wait_accepted('server')
            start=self.clock();end=start+10
            while self.clock()<end:
                self.pump();self.backend.pause(min(.05,max(0,end-self.clock())))
            self.observation_window={'seconds':10,'start_monotonic':start,'end_monotonic':self.clock(),
                                     'handshake_proved':False}
            for name,remaining in [('targets',35),('read.B',30),('read.A',15)]:
                self.check_time();need(self.clock()+remaining<=self.deadline,'REMAINING_QUERY_BUDGET')
                self.start(name);self.wait(name)
            return self.summary()
        def summary(self):
            row=super().summary();row['prequery_observation_window']=getattr(self,'observation_window',None)
            row['connected_snapshot_is_not_handshake']=True;return row
    return ReadonlyDriver,ReadonlyBackend


def actual_driver(grant):
    import goal_authorization as a
    import goal_contract as c
    c.need(grant in a._grants and c.SCOPE==SCOPE and c.NONCE==NONCE
      and getattr(grant,'fixed_readonly_evidence',False) is True,'NEW_PREPARED_READONLY_GRANT')
    c.validate_registry(list(grant.registry.values()))
    import goal_driver,goal_windows_backend
    from goal_handle_lifecycle import terminal_receipt_qualified
    D,B=make_types(goal_driver,goal_windows_backend,terminal_receipt_qualified)
    return D(grant.registry,B(grant))


def observation_complete(driver,cleanup):
    return evidence.observation_complete(driver,cleanup)
