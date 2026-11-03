"""Narrow, explicit DDS observation policy; never an authentication action.

This policy requires a new scoped approval. It does not classify retained
server authentication semantics as benign, nor turn them into an overall PASS.
"""


def server_telemetry(event):
    return (event.get('case') == 'server' and event.get('stream') == 'stdout'
            and event.get('phase') in ('MONITOR', 'CLEANUP')
            and event.get('event_type') == 'UNKNOWN'
            and event.get('rule_id') == 'AUTH_CONTEXT_UNKNOWN'
            and event.get('trigger_family') == 'AUTH_WORD_ONLY'
            and event.get('complete_record') is True)


def check_notice(marker, registry, enabled, need):
    report = marker.report()
    need(report.get('schema') == 'NOTICE_EVIDENCE_V2'
         and report.get('record_failed') is False and report.get('overflow') is False
         and report.get('failure_rules') == [] and report.get('emergency_events') == []
         and type(report.get('events')) is list, 'DDS_NOTICE_UNRELIABLE')
    unknown = diagnostic = False
    for event in report['events']:
        need(type(event) is dict and event.get('schema') == 'NOTICE_EVENT_V2'
             and event.get('case') in registry and event.get('stream') in ('stdout', 'stderr')
             and event.get('phase') in ('MONITOR', 'CLEANUP')
             and event.get('complete_record') is True, 'DDS_NOTICE_BINDING')
        if server_telemetry(event):
            need(enabled is True, 'DDS_SCOPED_OBSERVATION_NOT_GRANTED')
            unknown = True
        else:
            need(event.get('event_type') == 'DIAGNOSTIC'
                 and event.get('rule_id') in ('DIAG_FIELD', 'DIAG_PATH',
                                             'DIAG_NATIVE_CREATED', 'DIAG_FIXED_TRANSFER_HASH',
                                             'DIAG_FIXED_STEP_STATUS')
                 and event.get('trigger_family') == 'NOT_APPLICABLE',
                 'DDS_REQUEST_DENIED_FAILURE_OR_UNKNOWN')
            diagnostic = True
    expected = 'UNKNOWN' if unknown else 'DIAGNOSTIC' if diagnostic else 'NONE'
    need(report.get('classification') == expected, 'DDS_NOTICE_CLASSIFICATION_MISMATCH')
    return unknown


def qualify(procedure, cleanup, marker, registry, enabled, need):
    """Keep data, authentication, and OS cleanup evidence as separate facts."""
    unknown = check_notice(marker, registry, enabled, need)
    data = (type(procedure) is dict
            and procedure.get('all_four_windows_and_controller_accepted') is True
            and set(procedure.get('Native_results', {})) == {'probe.A', 'probe.B', 'data.A', 'data.B'})
    final = cleanup.get('finalization', {}) if type(cleanup) is dict else {}
    rows = cleanup.get('cleanup', []) if type(cleanup) is dict else []
    clean = (type(cleanup) is dict and final.get('state') == 'COMPLETE'
             and final.get('cleanup_errors') == [] and bool(rows)
             and all(row.get('state') == 'TERMINAL' and row.get('EOF') is True
                     and row.get('terminal_binding_verified') is True
                     and row.get('cleanup_errors') == [] for row in rows))
    reason = cleanup.get('failure') if type(cleanup) is dict else 'NO_CLEANUP'
    permitted_reason = 'CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN' if unknown else None
    clean = clean and reason == permitted_reason
    return {'DDS_data_observed': data and clean,
            'owned_resource_cleanup_complete': clean,
            'authentication_semantics': 'UNKNOWN_NOT_ATTESTED' if unknown else 'NO_NOTICE_OBSERVED_NOT_PERMISSION_CERTIFICATION',
            'overall_unconditional_pass': data and clean and not unknown,
            'unknown_server_events': sum(server_telemetry(e) for e in marker.report()['events'])}
