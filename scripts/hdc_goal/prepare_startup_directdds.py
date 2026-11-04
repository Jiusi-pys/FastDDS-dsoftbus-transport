"""Offline-only successor: two Native replacements and fixed inline evidence."""
import ast,importlib.util,json,sys
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('_startup_dds_parent',HERE/'prepare_directdds.py')
parent=importlib.util.module_from_spec(spec);spec.loader.exec_module(parent)
h=parent.h
OLD_SOURCES=parent.SOURCES
original_buffers=parent.source_buffers
original_external=parent.external_rows
parent.PACKAGE=h.REPO/'build/directdds-startup-cause-package-20261103-d01'
parent.ACTIVE=parent.PACKAGE/'active'
parent.RUN=Path('E:/Codex-FastDDS-directdds-once-20261103_f47c9e20')
parent.DEVICE='/data/local/tmp/mdds_direct_20261103_f47c9e20'
parent.SCOPE='ONE_NEW_CURRENT_STOCK_DDS_AB_STARTUP_CAUSE_240S_D05'
parent.NONCE='F47C9E20_DIRECT_DDS_240S_D05'
parent.PROBE_RUN='f47c9e2031d0aa60';parent.DATA_RUN='f47c9e2031d0aa61'
parent.CARD_PATH='docs/DIRECT_DDS_STARTUP_CAUSE_EXECUTION.md'
parent.SOURCES=(*OLD_SOURCES,'readonly_evidence.py','startup_cause.py','dds_startup_evidence.py','goal_read_context.py')
parent.CODE_PATHS=(*parent.CODE_PATHS,'scripts/hdc_goal/prepare_startup_directdds.py',
 'scripts/hdc_goal/readonly_evidence.py','scripts/hdc_goal/startup_cause.py',
 'scripts/hdc_goal/dds_startup_evidence.py','tests/test_hdc_startup_integration.py',parent.CARD_PATH)
NATIVE=Path('E:/Codex-FastDDS-native-startup-diagnostics-20261103-d01/initial-d05')
REPLACEMENTS={'payload.app':('own_goal_runner',305392,'ec733e0cd2a7d53d8178d7082c002b7f867bc2ceb6a2e6ed185b583cfde0c393'),
 'payload.launcher':('own_goal_launcher',91928,'cbd24f3f45eaefba1cb711c80a56a5defa4df85baff3cf725fe60b1b863da363')}
CONTEXT=h.REPO/'build/d04-prepared-readonly-package-20261103-d01/active'
CONTEXT_MANIFEST_SHA='5105205afed0b44ceb11c02f6d1d1acf8c658da2cdb557e2cf70adbd300beb34'

def external_rows(manifest):
    rows=original_external(manifest)
    for row in rows:
        if row['role'] in REPLACEMENTS:
            name,size,digest=REPLACEMENTS[row['role']]
            actual=h.row(NATIVE/name,cap=1048576)
            h.need(actual['bytes']==size and actual['sha256']==digest,'BOUND_NATIVE_REPLACEMENT')
            row.update(actual)
    h.need({r['role'] for r in rows}&set(REPLACEMENTS)==set(REPLACEMENTS),'TWO_NATIVE_REPLACEMENTS')
    return rows

def legacy_context():
    raw=h.read_safe(CONTEXT/'PACKAGE_FINAL_D01.json',4*1048576)
    h.need(h.sha(raw)==CONTEXT_MANIFEST_SHA,'LEGACY_CONTEXT_MANIFEST')
    rows=[r for r in json.loads(raw)['active_pins'] if r['role']=='source.goal_read_context.py']
    h.need(len(rows)==1,'LEGACY_CONTEXT_PIN')
    raw=h.read_safe(CONTEXT/'goal_read_context.py')
    h.need(h.sha(raw)==rows[0]['sha256'],'LEGACY_CONTEXT_DRIFT')
    tree=ast.parse(raw)
    if tree.body and isinstance(tree.body[0],ast.Expr) and isinstance(tree.body[0].value,ast.Constant) and type(tree.body[0].value.value) is str:
        tree.body.pop(0)
    h.need(len(tree.body)==1 and isinstance(tree.body[0],ast.Assign)
           and len(tree.body[0].targets)==1 and isinstance(tree.body[0].targets[0],ast.Name)
           and tree.body[0].targets[0].id=='EXPECTED','LEGACY_CONTEXT_LITERAL')
    expected=ast.literal_eval(tree.body[0].value)['B']
    h.need(set(expected)=={'pid','birth','parent','uid','euid','gid','egid','run'}
           and all(type(v) is int and 0<=v<=18446744073709551615 for v in expected.values())
           and all(expected[k]>0 for k in ('pid','birth','parent')),'LEGACY_CONTEXT_FIELDS')
    return ('EXPECTED='+repr({'B':expected})+'\n').encode()

def source_buffers():
    new_names=parent.SOURCES
    try:
        parent.SOURCES=OLD_SOURCES
        buffers,inputs,base=original_buffers()
    finally:parent.SOURCES=new_names
    rows={r['role']:r for r in external_rows(base)}
    for role,key in (('payload.app','app'),('payload.launcher','launcher')):
        row=rows[role];name=REPLACEMENTS[role][0]
        payload=[r for r in inputs['payloads'] if r['target']==name]
        h.need(len(payload)==1 and payload[0]['executable'] is True,'EXACT_EXECUTABLE_PAYLOAD')
        payload[0].update(source=row['path'],bytes=row['bytes'],sha256=row['sha256'])
        inputs['artifacts'][key]['sha256']=row['sha256']
    for name in ('readonly_evidence.py','startup_cause.py','dds_startup_evidence.py'):
        buffers[name]=h.read_safe(HERE/name)
    buffers['goal_read_context.py']=legacy_context()
    # Load pure helpers under their fixed names, without importing Win32 or any
    # actual grant. Restore the caller's import state on every exit.
    names=('startup_cause','readonly_evidence','dds_startup_evidence','goal_read_context')
    saved={n:sys.modules.get(n) for n in names}
    try:
        for n in names:sys.modules[n]=h.load_buffer(buffers[n+'.py'],n,parent.ACTIVE/(n+'.py'))
        helper=sys.modules['dds_startup_evidence'];expected=sys.modules['goal_read_context'].EXPECTED
        selected=[r for r in inputs['readonly_commands'] if r['id']=='RO-B-resource-path']
        h.need(len(selected)==1,'EXACT_B_RESOURCE_QUERY')
        row=selected[0]
        old=row['argv'][-1]
        row['argv'][-1]='(\n'+old+'\n)\nresource_rc=$?\n'+helper.postlude('B',expected['B'],'process',parent.DEVICE+'/data')+'exit "$resource_rc"\n'
        contract=buffers['goal_contract.py'].decode()
        source='''
def launch_step(window,side):
 need(window in ('probe','data') and side in TARGETS,'WINDOW_SIDE');d=DEVICE_ROOT+'/'+window
 argv=[DEVICE_ROOT+'/goal_supervisor','120',d+'/app.out',d+'/app.err',d+'/tmp',DEVICE_ROOT+'/own_goal_launcher','--wait-owned',side,d,'15000']
 body=' '.join(shell_quote(s) for s in argv)+'; native_rc=$?; '
 if window=='data':body+=CAUSE_POSTLUDES[side]
 body+='printf "\\\\nS1_GOAL_REMOTE_RC:%s\\\\n" "$native_rc"; exit 0'
 return shell_step(window+'.'+side.lower()+'.launch',window,side,body,kind='long')
'''
        contract=h.replace_function(contract,'launch_step',source)
        contract+='\nCAUSE_POSTLUDES='+repr({s:helper.postlude(s,{},'cause',parent.DEVICE+'/data') for s in ('A','B')})+'\n'
        buffers['goal_contract.py']=contract.encode()
    finally:
        for n,m in saved.items():
            if m is None:sys.modules.pop(n,None)
            else:sys.modules[n]=m
    auth=buffers['goal_authorization.py'].decode()
    tree=ast.parse(auth)
    shape=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='validate_approval_shape')
    effect_node=next(n for n in shape.body if isinstance(n,ast.Assign)
                     and any(isinstance(t,ast.Name) and t.id=='required' for t in n.targets))
    effects=ast.literal_eval(effect_node.value)
    effects.update(('fixed_old_B_process_snapshots_no_signal','inline_current_cause_512_projection_only'))
    auth=h.replace_once(auth,ast.get_source_segment(auth,effect_node),'required={'+','.join(repr(s) for s in sorted(effects))+'}')
    auth=h.replace_assignment(auth,'SOURCE_NAMES',repr(parent.SOURCES))
    auth=h.replace_once(auth,'_grants.add(grant);return grant',
      "grant.startup_evidence_bound=(approval['effects']['fixed_old_B_process_snapshots_no_signal'] is True and approval['effects']['inline_current_cause_512_projection_only'] is True)\n    _grants.add(grant);return grant")
    buffers['goal_authorization.py']=auth.encode()
    driver=buffers['goal_driver.py'].decode()
    driver=h.replace_once(driver,'return GoalDriver(grant.registry,GoalWindowsBackend(grant))',
      'from dds_startup_evidence import StartupEvidenceMixin\n    class FixedStartupBackend(StartupEvidenceMixin,GoalWindowsBackend):pass\n    return GoalDriver(grant.registry,FixedStartupBackend(grant))')
    buffers['goal_driver.py']=driver.encode()
    entry=buffers['goal_entry.py'].decode()
    entry=h.replace_once(entry,"'worker_parent_identity_restore':'UNKNOWN','no_retry':True}",
      "'worker_parent_identity_restore':'UNKNOWN','no_retry':True,\n            'startup_projections':dict(driver.backend.startup_projections) if driver else {},\n            'startup_projection_errors':dict(driver.backend.startup_projection_errors) if driver else {}}")
    entry=h.replace_once(entry,"    passed=failure is None and qualification['overall_unconditional_pass'] is True",
      "    if driver and driver.backend.startup_projection_errors:\n        failure=failure or 'STARTUP_DIAGNOSTIC_PROTOCOL_OR_RECORD_FAILED'\n        qualification['overall_unconditional_pass']=False\n    passed=failure is None and qualification['overall_unconditional_pass'] is True")
    buffers['goal_entry.py']=entry.encode()
    for name,data in buffers.items():ast.parse(data,filename=name)
    return buffers,inputs,base

parent.source_buffers=source_buffers
parent.external_rows=external_rows

if __name__=='__main__':
    if sys.argv[1:]==['--assemble']:print(json.dumps(parent.assemble()))
    elif len(sys.argv)==3 and sys.argv[1]=='--freeze':print(json.dumps(parent.freeze(sys.argv[2])))
    else:print('OFFLINE_PREPARATION_ONLY_NOT_GRANTED');raise SystemExit(78)
