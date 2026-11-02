"""Fixed A-only observation policy; never authentication or UI approval."""
import probe_pair as strict

SCOPE='ONE_NEW_FIXED_A_SCOPED_SERVER_TELEMETRY_60S_D01'
NONCE='B6F13D90_OBSERVE_A_60S_D01'


def telemetry(event):
    return (event.get('case')=='server' and event.get('stream')=='stdout'
            and event.get('phase') in ('MONITOR','CLEANUP')
            and event.get('event_type')=='UNKNOWN'
            and event.get('rule_id')=='AUTH_CONTEXT_UNKNOWN'
            and event.get('trigger_family')=='AUTH_WORD_ONLY'
            and event.get('complete_record') is True)


def make_types(driver_module,backend_module,terminal_qualified):
    PairDriver,PairBackend=strict.make_types(driver_module,backend_module,terminal_qualified)
    need=driver_module.need

    class ObservationBackend(PairBackend):
        qualified_receipt=staticmethod(terminal_qualified)

        def __init__(self,grant):
            need(getattr(grant,'scoped_server_keyword_observation',False) is True,
                 'SCOPED_OBSERVATION_EFFECT_REQUIRED')
            super().__init__(grant)  # broad server_unknown_continue remains False

        def failure_events(self):
            need(getattr(self.grant,'scoped_server_keyword_observation',False) is True
                 and self.grant.server_unknown_continue is False,'SCOPED_OBSERVATION_EFFECT_REQUIRED')
            report=self.marker.report();events=report.get('events')
            need(report.get('schema')=='NOTICE_EVIDENCE_V2'
                 and report.get('record_failed') is False and report.get('overflow') is False
                 and report.get('failure_rules')==[] and report.get('emergency_events')==[]
                 and type(events) is list,'OBSERVATION_NOTICE_UNRELIABLE')
            failures=[];unknown=False;diagnostic=False
            for event in events:
                need(type(event) is dict and event.get('schema')=='NOTICE_EVENT_V2'
                     and event.get('case') in strict.CASES
                     and event.get('stream') in ('stdout','stderr')
                     and event.get('complete_record') is True,'OBSERVATION_EVENT_BINDING')
                if telemetry(event):
                    unknown=True
                    continue
                kind,rule=event.get('event_type'),event.get('rule_id')
                if (kind=='DIAGNOSTIC' and rule in ('DIAG_FIELD','DIAG_PATH')
                        and event.get('trigger_family')=='NOT_APPLICABLE'):
                    diagnostic=True
                    continue
                # Inspect every retained event. An earlier UNKNOWN must not
                # conceal a later request, denial, malformed record or client event.
                need(kind=='COMMAND_FAILURE' and rule in strict.RULES
                     and event.get('case')==strict.FIRST and event.get('stream')=='stdout'
                     and event.get('trigger_family')=='NOT_APPLICABLE','OBSERVATION_BLOCKING_EVENT')
                failures.append(event)
            expected='UNKNOWN' if unknown else 'COMMAND_FAILURE' if failures else 'DIAGNOSTIC' if diagnostic else 'NONE'
            need(report.get('classification')==expected,'OBSERVATION_CLASSIFICATION_MISMATCH')
            return failures

        def cleanup_notice(self,errors):
            blocked=super().cleanup_notice(errors)
            # OS close may complete, but unexplained authentication semantics
            # and the failed A1 history are never upgraded to business success.
            return blocked or any(telemetry(e) for e in self.marker.report().get('events',[]))

    class ObservationDriver(PairDriver):
        def summary(self):
            result=super().summary()
            result.update(observation_only=True,authentication_semantics='UNKNOWN_NOT_ATTESTED',
                          retained_server_keyword_unknown_events=sum(
                              telemetry(e) for e in self.backend.marker.report().get('events',[])))
            return result

    return ObservationDriver,ObservationBackend


def actual_driver(grant):
    # All checks precede import/construction of the backend and native API module.
    import goal_authorization as authorization
    import goal_contract as contract
    contract.need(grant in authorization._grants and contract.SCOPE==SCOPE
                  and contract.NONCE==NONCE
                  and getattr(grant,'scoped_server_keyword_observation',False) is True,
                  'NEW_SCOPED_OBSERVATION_GRANT_REQUIRED')
    contract.validate_registry(list(grant.registry.values()))
    import goal_driver,goal_windows_backend
    from goal_handle_lifecycle import terminal_receipt_qualified
    Driver,Backend=make_types(goal_driver,goal_windows_backend,terminal_receipt_qualified)
    return Driver(grant.registry,Backend(grant))


def observation_complete(driver,cleanup):
    """Roundtrip observation only, with the full final evidence matrix intact."""
    try:
        b=driver.backend;b.failure_events()
        status=driver.summary()['diagnostic_outcome']
        first=status=='FIRST_SAMPLE_SUCCESS'
        if not first and status!='OBSERVED_FAILURE_THEN_RECOVERY':return False
        cases={'server',strict.FIRST} if first else set(strict.CASES)
        accepted={'server',strict.FIRST if first else strict.SECOND}
        if set(driver.used)!=cases or set(driver.accepted)!=accepted:return False
        if set(cleanup['used'])!=cases or set(cleanup['accepted'])!=accepted:return False
        if len(b.roots)!=len(cases) or {r.case for r in b.roots}!=cases:return False
        for root in b.roots:
            if (not root.closed or not b.qualified_receipt(root.receipt)
                    or root.receipt.get('EOF') is not True or root.receipt.get('cleanup_errors')):return False
            if root.case!='server':
                if root.receipt.get('native_exit')!=0 or root.receipt.get('stop_attempts')!=0:return False
                if root.receipt.get('accepted_evidence') is not (root.case in accepted):return False
        if not first:
            r=b.first_root
            if (b.first_failure is None or b.first_failure['accepted'] is not False
                    or b.first_failure['cleanup']['business_blocked'] is not True
                    or r.receipt.get('diagnostic_sample_failure')!=strict.PAIR
                    or not strict.exact_failure_pair(bytes(r.output[0]),bytes(r.output[1]))):return False
        elif b.first_failure is not None:return False
        blocked=not first or any(telemetry(e) for e in b.marker.report()['events'])
        final=cleanup['finalization'];rows=cleanup['cleanup']
        if (final.get('state')!='COMPLETE' or final.get('cleanup_errors')!=[]
                or final.get('business_blocked') is not blocked):return False
        if len(rows)!=1 or rows[0].get('step')!='server':return False
        row=rows[0]
        if (row.get('state')!='TERMINAL' or row.get('EOF') is not True
                or row.get('terminal_binding_verified') is not True
                or row.get('cleanup_errors')!=[] or row.get('business_blocked') is not blocked):return False
        expected='CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN' if blocked else None
        return cleanup['failure']==expected and not b.journal.failed
    except BaseException:return False
