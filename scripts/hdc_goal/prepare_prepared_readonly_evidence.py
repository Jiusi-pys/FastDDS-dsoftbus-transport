"""Offline successor; preserve the consumed package and reuse its generator."""
import ast,importlib.util,json,sys
from pathlib import Path

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('_prepared_parent',HERE/'prepare_readonly_evidence.py')
parent=importlib.util.module_from_spec(spec);spec.loader.exec_module(parent)
b=parent.b
original=parent.source_buffers
OLD_SCOPE=parent.SCOPE;OLD_NONCE=parent.NONCE
parent.PACKAGE=b.REPO/'build/d04-prepared-readonly-package-20261103-d01'
parent.ACTIVE=parent.PACKAGE/'active'
parent.RUN=Path('E:/Codex-FastDDS-d04-prepared-readonly-once-20261103_f6a1842d')
parent.SCOPE='ONE_NEW_D04_PREPARED_AB_READONLY_EVIDENCE_60S_D02'
parent.NONCE='F6A1842D_D04_PREPARED_READONLY_60S_D02'
parent.DOC='docs/D04_PREPARED_READONLY_EVIDENCE_EXECUTION.md'
NEW_PATHS=['scripts/hdc_goal/prepared_readonly_evidence.py','scripts/hdc_goal/prepare_prepared_readonly_evidence.py',
 'tests/test_hdc_prepared_readonly_evidence.py',parent.DOC]
parent.CODE_PATHS=[*parent.CODE_PATHS,*NEW_PATHS]
parent.SOURCES=(*parent.SOURCES,'fixed_target_listing.py','prepared_readonly_evidence.py')
CASES=('server','targets','read.B','read.A')


def source_buffers():
    buffers,old=original()
    for name in ('readonly_evidence.py',):
        text=buffers[name].decode().replace(OLD_SCOPE,parent.SCOPE).replace(OLD_NONCE,parent.NONCE)
        text=b.replace_assignment(text,'CASES',repr(CASES))
        text=b.replace_once(text,"set(b.observations)!={'read.B','read.A'}","set(b.observations)!={'targets','read.B','read.A'}")
        text=b.replace_once(text,'len(b.roots)!=3','len(b.roots)!=4')
        buffers[name]=text.encode()
    listing=ast.parse(b.read_safe(HERE/'state_samples.py'))
    nodes=[n for n in listing.body if isinstance(n,ast.ClassDef) and n.name=='Listing'
      or isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='STATES' for t in n.targets)]
    b.need(len(nodes)==2,'EXACT_LISTING_REUSE')
    # D04's accepted RO-1 parser used whitespace-separated four-column rows,
    # without demanding a double-TAB or hdc suffix. Retain Listing's bounded
    # framing but adapt only row(), with explicit failed-state projections.
    row=ast.parse('''
def row(self,line):
 self.rows+=1
 if self.rows>128:raise ValueError('FORMAT_UNKNOWN')
 if not line.strip():return
 if line=='[Empty]\\r\\thdc':
  if self.rows!=1:raise ValueError('FORMAT_UNKNOWN')
  self.empty=True;return
 if self.empty:raise ValueError('FORMAT_UNKNOWN')
 m=re.fullmatch(r'([A-Za-z0-9_.:\\-]{1,32})[ \\t]+(USB|TCP)[ \\t]+(Unknown|Ready|Connected|Offline|Unauthorized)[ \\t]+([^\\x00-\\x08\\x0a-\\x1f\\x7f]{1,256})',line)
 if not m:raise ValueError('FORMAT_UNKNOWN')
 if m[1]==self.target:
  if self.match is not None:raise ValueError('FORMAT_UNKNOWN')
  self.match=STATES[m[3]];self.negative=self.match=='UNAUTHORIZED'
''').body[0]
    cls=next(n for n in nodes if isinstance(n,ast.ClassDef))
    cls.body=[row if isinstance(n,ast.FunctionDef) and n.name=='row' else n for n in cls.body]
    buffers['fixed_target_listing.py']=('import re\n'+ast.unparse(ast.Module(body=nodes,type_ignores=[]))+'\n').encode()
    buffers['prepared_readonly_evidence.py']=b.read_safe(HERE/'prepared_readonly_evidence.py')
    text=buffers['goal_contract.py'].decode().replace('clients=2,','clients=3,')
    text=b.replace_assignment(text,'SOURCE_NAMES',repr(parent.SOURCES))
    text=b.replace_function(text,'build_registry',"""
def build_registry(commands):
 expected=[('server',(HDC,'-s',ENDPOINT,'-m')),('targets',(HDC,'-p','-s',ENDPOINT,'list','targets','-v'))]
 expected += [('read.'+s,(HDC,'-p','-s',ENDPOINT,'-t',TARGETS[s],'shell',body(s))) for s in ('B','A')]
 need(type(commands.get('commands')) is list and [(r.get('id'),tuple(r.get('argv',[]))) for r in commands['commands']]==expected,'EXACT_PREPARED_COMMANDS')
 return {n:GoalStep(n,'listener' if n=='server' else 'readonly','' if n in ('server','targets') else n[-1],a[1:],
  'server' if n=='server' else 'readonly',requires=() if n=='server' else ('server',) if n=='targets' else ('server','targets') if n=='read.B' else ('server','targets','read.B')) for n,a in expected}
""")
    text=b.replace_function(text,'validate_registry',"""
def validate_registry(rows):
 expected=build_registry({'commands':[{'id':'server','argv':[HDC,'-s',ENDPOINT,'-m']},
 {'id':'targets','argv':[HDC,'-p','-s',ENDPOINT,'list','targets','-v']},
 *[{'id':'read.'+s,'argv':[HDC,'-p','-s',ENDPOINT,'-t',TARGETS[s],'shell',body(s)]} for s in ('B','A')]]})
 need(rows==list(expected.values()),'REGISTRY_MUTATION');return dict(expected)
""")
    buffers['goal_contract.py']=text.encode()
    for name in ('goal_storage.py','goal_notice.py'):
        buffers[name]=b.replace_assignment(buffers[name].decode(),'CASES',repr(CASES)).encode()
    text=buffers['goal_authorization.py'].decode()
    text=b.replace_once(text,"grant.fixed_readonly_evidence=approval['effects']['fixed_D04_records_and_B_proc_bounded_read']",
      "grant.fixed_readonly_evidence=approval['effects']['fixed_D04_records_and_B_proc_bounded_read']\n grant.prepared_readonly=True")
    tree=ast.parse(text);effects=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
    effects.add('one_fixed_AB_target_list_after_monitored_window')
    buffers['goal_authorization.py']=b.replace_assignment(text,'EFFECTS','{'+','.join(repr(s) for s in sorted(effects))+'}').encode()
    for name in ('goal_driver.py','goal_entry.py'):
        buffers[name]=buffers[name].replace(b'from readonly_evidence import actual_driver',b'from prepared_readonly_evidence import actual_driver')
    buffers['goal_entry.py']=buffers['goal_entry.py'].replace(b"'max_fixed_queries':2",b"'max_fixed_queries':3")
    for name,data in buffers.items():ast.parse(data,filename=name)
    return buffers,old


def commands(buffers):
    saved={n:sys.modules.get(n) for n in ('readonly_evidence','goal_read_context')}
    try:
        for n in saved:sys.modules[n]=b.load_buffer(buffers[n+'.py'],n,parent.ACTIVE/(n+'.py'))
        c=b.load_buffer(buffers['goal_contract.py'],'_prepared_contract',parent.ACTIVE/'goal_contract.py')
        rows=[{'id':'server','argv':[c.HDC,'-s',c.ENDPOINT,'-m']},
              {'id':'targets','argv':[c.HDC,'-p','-s',c.ENDPOINT,'list','targets','-v']}]
        rows +=[{'id':'read.'+s,'argv':[c.HDC,'-p','-s',c.ENDPOINT,'-t',c.TARGETS[s],'shell',c.body(s)]} for s in ('B','A')]
        registry=c.build_registry({'commands':rows});c.validate_registry(list(registry.values()));return c,rows,registry
    finally:
        for n,v in saved.items():
            if v is None:sys.modules.pop(n,None)
            else:sys.modules[n]=v


parent.source_buffers=source_buffers;parent.commands=commands
if __name__=='__main__':
    try:
        if sys.argv[1:]==['--assemble']:parent.assemble();print('PREPARED_READONLY_ASSEMBLED_HDC0');raise SystemExit(0)
        if len(sys.argv)==3 and sys.argv[1]=='--freeze':print(json.dumps(parent.freeze(sys.argv[2])));raise SystemExit(0)
        print('PREPARED_READONLY_NOT_GRANTED');raise SystemExit(78)
    except SystemExit:raise
    except BaseException:print('PREPARED_READONLY_HOLD_PRESERVE_PARTIAL');raise SystemExit(1)
