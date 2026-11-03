"""Offline-only DDS composition. Imports/assemble/freeze cannot run HDC.

Reuse fixed Native binaries by hash, not by copying a full archive. The old
consumed package and its controls are immutable historical inputs, not grants.
"""
import ast
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('_dds_offline_helpers', HERE/'prepare_probe_pair.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)
REPO = h.REPO
BASE = Path('E:/Codex-FastDDS-stage1-directdds-reasons-package-20261031-d01')
BASE_SHA = 'f989dfac19d45f13e2c6388f06ea8e8866e0562848c7815dc46077c7a897be4a'
PACKAGE = REPO/'build/directdds-df-contract-package-20261103-d01'
ACTIVE = PACKAGE/'active'
RUN = Path('E:/Codex-FastDDS-directdds-once-20261103_c2d7a641')
DEVICE = '/data/local/tmp/mdds_direct_20261103_c2d7a641'
SCOPE = 'ONE_NEW_CURRENT_STOCK_DDS_AB_DF_CONTRACT_240S_D03'
NONCE = 'C2D7A641_DIRECT_DDS_240S_D03'
PROBE_RUN = 'c2d7a64131d0aa60'
DATA_RUN = 'c2d7a64131d0aa61'
CARD_PATH = 'docs/DIRECT_DDS_DF_EXECUTION.md'
spec = importlib.util.spec_from_file_location('_dds_resource_query', HERE/'resource_query.py')
resource_query = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resource_query)
SOURCES = ('goal_contract.py', 'goal_authorization.py', 'goal_driver.py',
           'goal_windows_backend.py', 'goal_win32_owned.py', 'goal_handle_lifecycle.py',
           'goal_memory_trace.py', 'goal_notice.py', 'goal_storage.py', 'goal_evidence.py',
           'goal_policy.py', 'goal_records.py', 'goal_workflow.py', 'goal_entry.py', 'dds_observation.py', 'dds_notice.py', 'resource_receipt.py')
CODE_PATHS = ('scripts/hdc_goal/prepare_directdds.py', 'scripts/hdc_goal/dds_observation.py',
              'scripts/hdc_goal/dds_notice.py', 'scripts/hdc_goal/goal_driver.py', 'scripts/hdc_goal/goal_notice.py',
              'scripts/hdc_goal/goal_windows_backend.py', 'scripts/hdc_goal/prepare_probe_pair.py',
              'tests/test_hdc_directdds_composition.py', 'docs/DIRECT_DDS_OBSERVATION_EXECUTION.md')
CODE_PATHS=(*CODE_PATHS,'scripts/hdc_goal/resource_receipt.py','tests/test_hdc_resource_receipt.py',
            'docs/HDC_RESOURCE_RECEIPT_COMPATIBILITY.md','scripts/hdc_goal/resource_query.py',
            'tests/test_hdc_resource_query.py','docs/HDC_DF_RECEIPT_CONTRACT.md',CARD_PATH,
            'scripts/hdc_goal/goal_handle_lifecycle.py','tests/test_hdc_handle_lifecycle.py',
            'docs/HDC_TERMINAL_IDENTITY.md')
CONTROLS = h.CONTROLS


def replace_method(text, name, source, class_name='GoalWindowsBackend'):
    def find(value):
        classes = [n for n in ast.parse(value).body if isinstance(n, ast.ClassDef) and n.name == class_name]
        h.need(len(classes) == 1, 'BACKEND_CLASS')
        return classes[0],next((n for n in classes[0].body if isinstance(n, ast.FunctionDef) and n.name == name), None)
    old_class,old = find(text)
    _,new = find(source)
    h.need(new is not None, 'METHOD_SOURCE')
    lines = source.splitlines(keepends=True)
    replacement = ''.join(lines[new.lineno-1:new.end_lineno])+'\n'
    lines = text.splitlines(keepends=True)
    if old is None:
        return ''.join(lines[:old_class.end_lineno])+'\n'+replacement+''.join(lines[old_class.end_lineno:])
    return ''.join(lines[:old.lineno-1])+replacement+''.join(lines[old.end_lineno:])


def source_buffers():
    raw = h.read_safe(BASE/'active/GOAL_PACKAGE_FINAL_D01.json', 4*1048576)
    h.need(h.sha(raw) == BASE_SHA, 'HISTORICAL_MANIFEST_DRIFT')
    manifest = json.loads(raw)
    pins = {r['role']: r for r in manifest['active_pins']}
    buffers = {}
    # Read only the explicit public source/schema closure, never old controls,
    # run output, private streams, cache, keys or historical reference bodies.
    for name in SOURCES[:-3]:
        data = h.read_safe(BASE/'active'/name)
        h.need(h.sha(data) == pins['source.'+name]['sha256'], 'HISTORICAL_SOURCE_DRIFT')
        buffers[name] = data
    data = h.read_safe(BASE/'active/GOAL_INPUTS_D01.json')
    h.need(h.sha(data) == pins['schema.inputs']['sha256'], 'INPUT_SCHEMA_DRIFT')
    inputs = json.loads(data)
    old_contract = h.load_buffer(buffers['goal_contract.py'], '_dds_old_contract', BASE/'active/goal_contract.py')
    # Resource/root-absence queries must qualify the NEW staging root. Payload
    # source paths and bytes remain exact historical binary references.
    for row in inputs['readonly_commands']:
        row['argv'] = [arg.replace(old_contract.DEVICE_ROOT, DEVICE) for arg in row['argv']]
        if row['argv'][-1].startswith(resource_query.MEMINFO_AWK):
            row['argv'][-1] = resource_query.replace_meminfo_query(row['argv'][-1])
    h.need(sum(row['argv'][-1].startswith(resource_query.meminfo_records())
               for row in inputs['readonly_commands']) == 2, 'TWO_FIXED_RESOURCE_QUERIES')
    for name in buffers:
        text = buffers[name].decode('utf-8-sig')
        for old, new in ((str(BASE/'active').replace('\\', '/'), str(ACTIVE).replace('\\', '/')),
                         (str(old_contract.RUN_ROOT).replace('\\', '/'), str(RUN).replace('\\', '/')),
                         (old_contract.DEVICE_ROOT, DEVICE), (old_contract.SCOPE, SCOPE),
                         (old_contract.NONCE, NONCE), ('9a72c6e431d0aa60', PROBE_RUN),
                         ('9a72c6e431d0aa61', DATA_RUN)):
            text = text.replace(old, new)
        buffers[name] = text.encode('utf-8')
    contract = buffers['goal_contract.py'].decode()
    contract = h.replace_once(contract, 'total_ms=240000,', 'total_ms=240000,work_ms=236000,')
    buffers['goal_contract.py'] = contract.encode()
    buffers['goal_driver.py'] = h.read_safe(HERE/'goal_driver.py')
    buffers['goal_notice.py'] = h.read_safe(HERE/'goal_notice.py')
    buffers['dds_observation.py'] = h.read_safe(HERE/'dds_observation.py')
    buffers['dds_notice.py'] = h.read_safe(HERE/'dds_notice.py')
    buffers['resource_receipt.py'] = h.read_safe(HERE/'resource_receipt.py')
    buffers['goal_handle_lifecycle.py'] = h.read_safe(HERE/'goal_handle_lifecycle.py')
    # Adapt only observation methods. The creator, permit, retained image pin,
    # hash binding, resume and stop mechanisms remain the reviewed originals.
    win32 = buffers['goal_win32_owned.py'].decode()
    win32 = h.replace_once(win32, 'observe_binding,bounded_drain',
                           'observe_binding,bounded_drain,observe_owned_binding')
    win32 = replace_method(win32, 'identity', '''class OwnedRoot:
 def identity(self,require_live=False):
  return observe_owned_binding(self,IdentityOps(),require_live)
''', 'OwnedRoot')
    win32 = replace_method(win32, 'wait_terminal', '''class IdentityOps:
 def wait_terminal(self,h,timeout_ms):
  need(type(timeout_ms) is int and 0<timeout_ms<=50,'TRANSITION_WAIT_BOUND')
  value=K.WaitForSingleObject(h,timeout_ms)
  need(value in (0,258),'TRANSITION_WAIT_UNKNOWN')
  return value
''', 'IdentityOps')
    buffers['goal_win32_owned.py'] = win32.encode()
    evidence=buffers['goal_evidence.py'].decode().replace('\r\n','\n')
    evidence=h.replace_once(evidence,"text=data.decode('utf-8','strict').replace('\\r\\n','\\n')",
                            "from resource_receipt import receipt_text\n    text=receipt_text(data,need)")
    evidence=h.replace_once(evidence,"text = stdout.decode('utf-8', errors='strict').replace('\\r\\n', '\\n')\n    err = stderr.decode('utf-8', errors='strict')",
                            "from resource_receipt import receipt_text,parse_resource\n    if case in ('RO-A-resource-path','RO-B-resource-path'):\n        return parse_resource(stdout,stderr,native_exit,DEVICE_ROOT,LIMITS,need)\n    text = receipt_text(stdout,need)\n    err = receipt_text(stderr,need)")
    buffers['goal_evidence.py']=evidence.encode()
    storage=buffers['goal_storage.py'].decode()
    storage=h.replace_once(storage,"('evidence','RESULT.json')]", "('evidence','RESULT.json'),('evidence','RESULT.pending.json')]")
    buffers['goal_storage.py']=storage.encode()
    backend = buffers['goal_windows_backend.py'].decode()
    maintained = h.read_safe(HERE/'goal_windows_backend.py').decode()
    for name in ('create_and_resume', 'server_ok', 'observe_chunk', 'cleanup_notice', 'cleanup', 'complete_cleanup', 'outcome'):
        backend = replace_method(backend, name, maintained)
    backend = h.replace_once(backend, "    def observe(self,root,phase):\n",
                             "    def observe(self,root,phase):\n        self.journal.case=root.case\n")
    backend = h.replace_once(backend, "self.epochs=0;self.samples=0;", "self.stream_bytes={};self.capture_failed=False;self.epochs=0;self.samples=0;")
    backend = h.replace_once(backend, 'from goal_notice import Marker', 'from dds_notice import DDSMarker')
    backend = h.replace_once(backend, 'self.marker=Marker(self.notice_event,cases=tuple(grant.registry),max_total=12*1048576)',
                             'self.marker=DDSMarker(self.notice_event,registry=grant.registry,cases=tuple(grant.registry),max_total=12*1048576)')
    backend = h.replace_once(backend, "else LIMITS['stream_capture']", "else LIMITS['client_capture']")
    # Preserve the DDS baseline's total sample/output/storage caps. Do not copy
    # the probe's extra per-root fields or its missing active_inventory import.
    backend = h.replace_once(backend, "if not self.server_ok():\n                need(time.monotonic()-root.goal_started<LIMITS['server_ready_ms']/1000,'SERVER_LISTENER_DEADLINE');return None", "if not self.server_ok():return None")
    # DDS has an original work/server lifetime deadline, not the readiness
    # probe's separate listener cap. Do not introduce a nonexistent LIMITS key.
    backend = h.replace_once(backend,
        "time.monotonic()<min(root.identity_deadline,root.goal_started+LIMITS['server_ready_ms']/1000),'SERVER_ACCEPT_DEADLINE'",
        "time.monotonic()<root.identity_deadline,'SERVER_ACCEPT_DEADLINE'")
    backend = h.replace_once(backend, "self.marker.feed(root.case,stream,data,'MONITOR',before,after)", "self.observe_chunk(root,stream,data,before,after,'MONITOR')")
    policy = '''
    def notice_gate(self):
        from dds_observation import check_notice
        check_notice(self.marker,self.grant.registry,
                     getattr(self.grant,'scoped_server_keyword_observation',False),need)
'''
    backend = replace_method(backend, 'notice_gate', 'class GoalWindowsBackend:\n'+policy)
    backend = h.replace_once(backend, 'return blocked\n', "from dds_observation import server_telemetry\n        return blocked or any(server_telemetry(e) for e in self.marker.report().get('events',[]))\n")
    backend = h.replace_once(backend, "('DIAGNOSTIC','DIAG_FIELD'),('DIAGNOSTIC','DIAG_PATH')}",
                             "('DIAGNOSTIC','DIAG_FIELD'),('DIAGNOSTIC','DIAG_PATH'),\n                ('DIAGNOSTIC','DIAG_NATIVE_CREATED'),('DIAGNOSTIC','DIAG_FIXED_TRANSFER_HASH'),\n                ('DIAGNOSTIC','DIAG_FIXED_STEP_STATUS')}")
    buffers['goal_windows_backend.py'] = backend.encode()
    auth = buffers['goal_authorization.py'].decode()
    auth = h.replace_assignment(auth, 'SOURCE_NAMES', repr(SOURCES))
    auth = auth.replace("'server_auth_context_unknown_continue'", "'server_stdout_auth_word_only_unknown_observation'")
    auth = h.replace_once(auth, '_grants.add(grant);return grant', "grant.scoped_server_keyword_observation=approval['effects']['server_stdout_auth_word_only_unknown_observation']\n    _grants.add(grant);return grant")
    buffers['goal_authorization.py'] = auth.encode()
    workflow=buffers['goal_workflow.py'].decode()
    workflow=h.replace_once(workflow,"text=bytes(root.output[0]).decode('utf-8','strict').replace('\\r\\n','\\n')",
                             "from resource_receipt import receipt_text\n        text=receipt_text(bytes(root.output[0]),need)")
    workflow=h.replace_once(workflow,"    def run(self):\n        self.driver.start('server');self.driver.wait_accepted('server');self.step('targets')",'''    def observe_fixed_window(self):
        # Experimental timing, not a readiness claim or a retry. The same
        # resource/notice/deadline checks remain active throughout this window.
        start=self.driver.clock()
        end=start+10.0
        while self.driver.clock()<end:
            self.driver.pump()
            self.driver.backend.pause(min(.05,max(0,end-self.driver.clock())))
        return {'seconds':10,'start_monotonic':start,'end_monotonic':self.driver.clock(),
                'handshake_proved':False}

    def run(self):
        self.driver.start('server');self.driver.wait_accepted('server')
        self.observation_window=self.observe_fixed_window()
        self.step('targets')''')
    workflow=h.replace_once(workflow,"'procedure':'FIRST_CURRENT_STOCK_DDS_AB'", "'procedure':'FIRST_CURRENT_STOCK_DDS_AB','prequery_observation_window':self.observation_window")
    buffers['goal_workflow.py']=workflow.encode()
    entry = buffers['goal_entry.py'].decode()
    publication='''def publish_result(path,result):
    # Only the final name is authoritative. Never publish a success record
    # until its complete bytes have passed flush/fsync. A failed pending file
    # is retained as evidence, not promoted or retried.
    pending=path.with_name('RESULT.pending.json')
    need(not path.exists() and not pending.exists(),'RESULT_ALREADY_EXISTS')
    with pending.open('x',encoding='utf-8',newline='\\n') as out:
        json.dump(result,out,sort_keys=True);out.flush();os.fsync(out.fileno())
    os.replace(pending,path)


'''
    entry=h.replace_once(entry,'def main(argv=None):',publication+'def main(argv=None):')
    entry=h.replace_once(entry,"with path.open('x',encoding='utf-8') as out:json.dump(result,out,sort_keys=True);out.flush();os.fsync(out.fileno())",'publish_result(path,result)')
    start, end = entry.index('    clean=cleanup'), entry.index('    if driver:\n        try:')
    entry = entry[:start]+'''    from dds_observation import qualify
    qualification={'DDS_data_observed':False,'overall_unconditional_pass':False}
    if driver:
        try:
            qualification=qualify(procedure,cleanup,driver.backend.marker,driver.registry,
                                  driver.backend.grant.scoped_server_keyword_observation,need)
        except BaseException:
            failure=failure or 'DDS_QUALIFICATION_UNKNOWN'
    passed=failure is None and qualification['overall_unconditional_pass'] is True
    status=('PASS' if passed else 'DDS_DATA_OBSERVED_AUTH_UNKNOWN'
            if failure is None and qualification.get('DDS_data_observed') else 'FAIL_OR_HOLD')
    result={'scope':'CURRENT_STOCK_DDS_AB','status':status,'failure':failure,
            'procedure':procedure,'cleanup':cleanup,'qualification':qualification,
            'Native_SDK_callback_quiet':'UNKNOWN','worker_parent_identity_restore':'UNKNOWN','no_retry':True}
'''+entry[end:]
    buffers['goal_entry.py'] = entry.encode()
    for name, raw in buffers.items():
        ast.parse(raw, filename=name)
        h.need(b'9a72c6e4' not in raw and b'REJECTION_EVIDENCE_D04' not in raw, 'OLD_SCOPE_LITERAL')
    return buffers, inputs, manifest


def external_rows(manifest):
    return [dict(row) for row in manifest['active_pins']
            if row['role'].startswith(('payload.', 'tool.')) or row['role'] == 'host.controller']


def verify_external(manifest):
    rows = external_rows(manifest)
    h.need(len(rows) == 14, 'EXTERNAL_ROLE_COUNT')
    for row in rows:
        actual = h.row(Path(row['path']), cap=32*1048576)
        h.need(all(actual[k] == row[k] for k in ('path', 'bytes', 'sha256')), 'EXTERNAL_INPUT_DRIFT')
    return rows


def assemble():
    h.need(not PACKAGE.exists() and not RUN.exists(), 'NEW_PACKAGE_AND_RUN_REQUIRED')
    buffers, inputs, base = source_buffers()
    rows = verify_external(base)
    contract = h.load_buffer(buffers['goal_contract.py'], '_dds_composition_contract', ACTIVE/'goal_contract.py')
    steps = contract.build_registry(inputs['payloads'], inputs['readonly_commands'], inputs['controller'])
    registry = contract.validate_registry(steps)
    h.need(len(registry) == 123, 'DDS_REGISTRY_SIZE')
    h.directory(ACTIVE);h.directory(PACKAGE/'prepare')
    for name, raw in buffers.items():h.write_new(ACTIVE/name, raw)
    h.write_new(ACTIVE/'GOAL_INPUTS_D01.json', h.encoded(inputs))
    from dataclasses import asdict
    h.write_new(PACKAGE/'COMMANDS_D01.json', h.encoded([asdict(step) for step in steps]))
    source = {'scope':SCOPE,'nonce':NONCE,'registry_sha256':contract.registry_digest(steps),
              'sources':[h.row(ACTIVE/n) for n in (*SOURCES,'GOAL_INPUTS_D01.json')],
              'external_inputs':rows,'base_manifest_sha256':BASE_SHA,
              'limits':contract.LIMITS,'actual_execution_authorized':False}
    h.write_new(ACTIVE/'SOURCE_BINDING_D01.json', h.encoded(source))
    h.write_new(PACKAGE/'prepare/REUSE_VERIFICATION_D01.json', h.encoded({
        'external_input_count':len(rows),'external_input_bytes':sum(r['bytes'] for r in rows),
        'all_rehashed_equal':True,'Native_rebuilt':False,'binary_copies':0,'actual_HDC_calls':0,
        'consumed_controls_reused':False,'registry_count':len(steps),
        'normal_client_steps':sum(s.kind not in ('server','controller') and '.cancel.' not in s.id for s in steps),
        'cancel_client_steps':sum('.cancel.' in s.id for s in steps)}))
    return source


def revise(revision_name='revision-d03'):
    """One pre-freeze correction; retain prior binding and a textual delta."""
    import difflib
    import os
    h.need(not RUN.exists() and all(not (ACTIVE/n).exists() for n in CONTROLS)
           and not (ACTIVE/'GOAL_PACKAGE_FINAL_D01.json').exists(), 'UNFROZEN_CANDIDATE_ONLY')
    buffers, inputs, base = source_buffers();verify_external(base)
    old = h.read_safe(ACTIVE/'SOURCE_BINDING_D01.json')
    h.need(revision_name in ('revision-d02','revision-d03','revision-d04'),'FIXED_REVISION_LABEL')
    revision = PACKAGE/'prepare'/revision_name;h.directory(revision)
    saved=revision/'OLD_SOURCE_BINDING_D01.json'
    if saved.exists():h.need(h.read_safe(saved)==old,'REVISION_HISTORY_CHANGED')
    else:h.write_new(saved,old)
    current = {**buffers,'GOAL_INPUTS_D01.json':h.encoded(inputs)};delta=[]
    old_rows={Path(row['path']).name:row for row in json.loads(old)['sources']}
    for name,data in current.items():
        previous=h.read_safe(ACTIVE/name) if (ACTIVE/name).exists() else b''
        h.need((not previous and name not in old_rows)
               or h.sha(previous) in (old_rows.get(name,{}).get('sha256'),h.sha(data)), 'REVISION_INPUT_DRIFT')
        delta.extend(difflib.unified_diff(previous.decode('utf-8-sig').splitlines(True),
                    data.decode('utf-8-sig').splitlines(True),fromfile=name+'.before',tofile=name+'.after'))
        if previous!=data:
            temp=ACTIVE/(name+'.preparation-incoming');h.write_new(temp,data);os.replace(temp,ACTIVE/name)
    c=h.load_buffer(buffers['goal_contract.py'],'_dds_revision_contract',ACTIVE/'goal_contract.py')
    steps=c.build_registry(inputs['payloads'],inputs['readonly_commands'],inputs['controller']);c.validate_registry(steps)
    source=json.loads(old);source['sources']=[h.row(ACTIVE/n) for n in (*SOURCES,'GOAL_INPUTS_D01.json')]
    source['registry_sha256']=c.registry_digest(steps)
    from dataclasses import asdict
    for path,data in ((ACTIVE/'SOURCE_BINDING_D01.json',h.encoded(source)),
                      (PACKAGE/'COMMANDS_D01.json',h.encoded([asdict(s) for s in steps]))):
        temp=path.with_name(path.name+'.preparation-incoming');h.write_new(temp,data);os.replace(temp,path)
    h.write_new(revision/'SOURCE_DELTA.patch',''.join(delta).encode('utf-8'))
    h.write_new(revision/'REVISION_MANIFEST_D01.json',h.encoded({
        'prior_binding_sha256':h.sha(old),'current_sources':source['sources'],
        'partial_preparation_resume_allowed_only_when_each_input_is_old_or_new_hash':True,
        'actual_execution':False}))
    return source


def freeze(commit):
    h.need(not RUN.exists() and all(not (ACTIVE/n).exists() for n in CONTROLS), 'NO_ACTUAL_CONTROLS')
    buffers, inputs, base = source_buffers()
    for name, data in buffers.items():h.need(h.read_safe(ACTIVE/name) == data, 'SOURCE_DRIFT')
    for path in CODE_PATHS:
        p = subprocess.run(['git','show',commit+':'+path],cwd=REPO,capture_output=True)
        h.need(p.returncode == 0 and p.stdout == h.read_safe(REPO/path), 'PUBLISHED_SOURCE_BINDING')
    source_raw = h.read_safe(ACTIVE/'SOURCE_BINDING_D01.json');source = json.loads(source_raw)
    h.need(source['sources'] == [h.row(ACTIVE/n) for n in (*SOURCES,'GOAL_INPUTS_D01.json')], 'SOURCE_BINDING_DRIFT')
    for name in ('FINAL_TEST_RESULT_D02.json','REVIEW_CLOSURE_D02.json'):
        j = json.loads(h.read_safe(PACKAGE/'prepare'/name))
        h.need(j['source_binding_sha256'] == h.sha(source_raw), 'EVIDENCE_BINDING')
        h.need(j.get('commit_gate') == 'PASS' if name.startswith('FINAL') else j.get('blocking_findings_open') == 0, 'EVIDENCE_NOT_READY')
    rows = verify_external(base)
    for name in SOURCES:rows.append({**h.row(ACTIVE/name), 'purpose':'source','role':'source.'+name})
    for name, role, purpose, directory in (
            ('GOAL_INPUTS_D01.json','schema.inputs','schema',ACTIVE),
            ('FINAL_TEST_RESULT_D02.json','evidence.final','final_evidence',PACKAGE/'prepare'),
            ('REVIEW_CLOSURE_D02.json','evidence.review','final_evidence',PACKAGE/'prepare')):
        rows.append({**h.row(directory/name),'purpose':purpose,'role':role})
    total = sum(r['bytes'] for r in rows)
    h.need(total < 32*1048576, 'ACTIVE_BUDGET')
    own = list(PACKAGE.rglob('*'));files = [p for p in own if p.is_file()]
    h.need(len(files) <= 128 and sum(p.stat().st_size for p in files) < 8*1048576, 'PREPARATION_BUDGET')
    manifest = {'scope':SCOPE,'nonce':NONCE,'registry_sha256':source['registry_sha256'],
                'limits':source['limits'],'active_pins':rows,'actual_execution_authorized':False,
                'published_source_commit':commit,'historical_manifest_sha256':BASE_SHA}
    raw = h.encoded(manifest);h.write_new(ACTIVE/'GOAL_PACKAGE_FINAL_D01.json',raw)
    # Do not import the actual authorization/backend while preparing a card.
    # The independent offline verifier checks the exact role closure later.
    binding = {'scope':SCOPE,'nonce':NONCE,'manifest_sha256':h.sha(raw),
               'registry_sha256':source['registry_sha256'],'run_root':str(RUN),
               'full_executor_final':True,'actual_execution_authorized':False,
               'active_closure_bytes':total,'required_roles':sorted(r['role'] for r in rows),
               'published_source_commit':commit}
    br = h.encoded(binding);h.write_new(ACTIVE/'FINAL_EXECUTION_BINDING_D01.json',br)
    card = h.read_safe(REPO/CARD_PATH)
    c=h.load_buffer(buffers['goal_contract.py'],'_dds_private_card_contract',ACTIVE/'goal_contract.py')
    private={'fixed_targets':c.TARGETS,'existing_project_identity':c.OWN,
             'entry_argv':['C:/Users/17715/anaconda3/python.exe','-I','-B','-c',
                           "import runpy,sys;sys.path.insert(0,"+repr(str(ACTIVE))+");runpy.run_path("+repr(str(ACTIVE/'goal_entry.py'))+",run_name='__main__')",
                           '--actual-once','--manifest-sha256',h.sha(raw),'--binding-sha256',h.sha(br)],
             'requires_new_auth_admission_fresh_then_once':True}
    card += ('\n## Private fixed binding (not for Git publication)\n\nManifest SHA256: '+h.sha(raw)+'\nBinding SHA256: '+h.sha(br)+'\n\n'+json.dumps(private,indent=2)+'\n').encode()
    h.write_new(PACKAGE/'EXECUTION_CARD_D01.md',card)
    result = {'manifest_sha256':h.sha(raw),'binding_sha256':h.sha(br),'card_sha256':h.sha(card),
              'source_binding_sha256':h.sha(source_raw),'actual_execution_authorized':False,'published_source_commit':commit}
    h.write_new(PACKAGE/'prepare/FINAL_HASHES_D01.json',h.encoded(result));return result


if __name__ == '__main__':
    if sys.argv[1:] == ['--assemble']:print(json.dumps(assemble()))
    elif sys.argv[1:] == ['--revise']:print(json.dumps(revise()))
    elif len(sys.argv) == 3 and sys.argv[1] == '--freeze':print(json.dumps(freeze(sys.argv[2])))
    else:print('OFFLINE_PREPARATION_ONLY_NOT_GRANTED');raise SystemExit(78)
