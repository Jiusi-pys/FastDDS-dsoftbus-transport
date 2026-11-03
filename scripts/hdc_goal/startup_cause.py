"""Pure decoder for future fixed cause.A/B records; no file/device access."""
KEYS=('format','available','entered_stage','first_reason','failure_stage','gate_entered_stage','gate_reason','gate_failure_stage')
STAGES=('NONE','EXECUTE_GUARD','EXECUTE_CHECKS','SESSION_CONSTRUCTION','CONFIGURATION','RUNTIME_CONSTRUCTION',
 'GATE_REGISTRATION','DESCRIPTOR_CREATION','FACTORY_LOOKUP','XML_LOAD','XML_MODE','WORKER_START','PARTICIPANT_CREATION','AFTER_PARTICIPANT')
REASONS=('NO_RECORDED_FAILURE','LOCK_BUSY','HANDLE_REUSED','GATE_REGISTRATION_REJECTED','DESCRIPTOR_NULL',
 'XML_LOAD_REJECTED','XML_MODE_MISMATCH','PARTICIPANT_NULL','STATUS_THROWN','UNEXPECTED_EXCEPTION')
GATE_STAGES=('NONE','OBSERVE_LISTENER','CHECK_LISTENER','EMIT_EVENT','WAIT_RELEASE','COMPLETED')
GATE_REASONS=('NO_RECORDED_FAILURE','LISTENER_REJECTED','EMIT_FAILED','WAIT_FAILED','UNEXPECTED_EXCEPTION')
# Fixed producer contract. Generic catches can retain any entered main stage;
# gate catches use their own explicit local stage, never the shared main one.
FAILURE_STAGES=({0},{1},{1},{6},{7},{9},{10},{12},set(range(1,14)),set(range(1,14)))
GATE_FAILURE_STAGES=({0},{2},{3},{4},set(range(1,5)))

def decode(data):
    if type(data) is not bytes or not 0<len(data)<=512 or not data.endswith(b'\n') or b'\r' in data:raise ValueError('CAUSE_FRAME')
    try:rows=[line.split('=',1) for line in data[:-1].decode('ascii').split('\n')]
    except UnicodeError:raise ValueError('CAUSE_ENCODING') from None
    if any(len(r)!=2 for r in rows) or tuple(r[0] for r in rows)!=KEYS or rows[0][1]!='S1GOALCAUSE1':raise ValueError('CAUSE_SCHEMA')
    values=[]
    for _,text in rows[1:]:
        if not text or len(text)>10 or not text.isascii() or not text.isdigit() or str(int(text))!=text:raise ValueError('CAUSE_NUMBER')
        values.append(int(text))
    available,stage,reason,failed,gate_stage,gate_reason,gate_failed=values
    if not(available<=1 and stage<len(STAGES) and failed<len(STAGES) and reason<len(REASONS)
      and gate_stage<len(GATE_STAGES) and gate_failed<len(GATE_STAGES) and gate_reason<len(GATE_REASONS)):raise ValueError('CAUSE_ENUM')
    if (reason==0)!=(failed==0) or (gate_reason==0)!=(gate_failed==0) or (not available and any(values[1:])):raise ValueError('CAUSE_CONTRADICTION')
    if failed not in FAILURE_STAGES[reason] or gate_failed not in GATE_FAILURE_STAGES[gate_reason]:raise ValueError('CAUSE_STAGE_REASON')
    return {'record':'VALID','available':bool(available),'association':'FIXED_PATH_ONLY',
      'entered_stage':STAGES[stage],'first_reason':REASONS[reason],'failure_stage':STAGES[failed],
      'gate_entered_stage':GATE_STAGES[gate_stage],'gate_reason':GATE_REASONS[gate_reason],'gate_failure_stage':GATE_STAGES[gate_failed],
      'SDK_quiet':'UNKNOWN','producer_contract':'NS_ABI_STATUS_PHASE_UNCHANGED_BY_DESIGN','success_inferred':False}
