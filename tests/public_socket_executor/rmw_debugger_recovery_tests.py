"""Actual debugger source, readonly closure and generated shell boundaries."""
import hashlib,importlib.util,json,os,subprocess,tempfile,unittest
import types
from pathlib import Path
from unittest.mock import patch

REPO=Path(__file__).resolve().parents[2]
SOURCE=REPO/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
s=importlib.util.spec_from_file_location('recovery_test_debugger',SOURCE)
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

class Recovery(unittest.TestCase):
 def fixture(self,folder):
  repo=Path(folder);(repo/'.git').mkdir();task=repo/'authority/iterations/iteration-1';window=task/'windows/data';root=window/'A/debugger';root.mkdir(parents=True)
  original=task/'executor/rmw_debugger.py';original.parent.mkdir();original.write_bytes(SOURCE.read_bytes())
  run='a'*16;pid=123;freeze=task/'contract.json'
  freeze.write_text(json.dumps({'execution_files':[{'path':original.relative_to(repo).as_posix(),'sha256':m.sha(original)}]}))
  cfg=dict(run=run,window_root=str(window),task_contract=dict(task_root=str(task),freeze_path=str(freeze),freeze_sha256=m.sha(freeze)),
      targets={'A':'UNIT_NOT_DEVICE'},hdc_path='INERT',remote={'A':'/owned'},main_member='main',rmw_debugger=True)
  (window/'window.json').write_text(json.dumps(cfg))
  (window/'DATA_ATTEMPT_STARTED').write_text(json.dumps(dict(run=run,window=str(window),kind='DATA_DISPATCH_INTENT',simulated=False)))
  names=['load-status.txt','load-exe.txt','load-cwd.txt','load-cmdline.txt','load-environ.txt','load-stat-before.txt','load-stat-after.txt','load-uptime.txt','load-maps.txt']
  (window/'A/load').mkdir();rows=[];hashes={}
  for name in names:
   p=window/'A/load'/name;p.write_bytes(b'UNIT_RAW_NOT_DEVICE');hashes[name]=m.sha(p)
   rows.append(dict(name=name,pass_=True,present=True,closed=True,sha256=m.sha(p)))
   rows[-1]['pass']=rows[-1].pop('pass_')
  analysis=window/'A/load-pre-release-analysis.json'
  analysis.write_text(json.dumps(dict(complete=True,scope='LIVE_PROC_SNAPSHOT_NOT_FULL_SDK_MODULE_CLOSURE',run=run,board='A',pid=pid,starttime='987',errors=[],evidence_sha256=hashes)))
  summary=dict(run=run,side='A',host_collector_test=False,formal_complete=True,formal_exit=0,formal_child_closed=True,collection_cleanup='CHILD_STREAMS_CLOSED',formal_report_sha256=m.sha(analysis),transfer=rows)
  summary['pass']=True;(window/'A/load-pre-release-summary.json').write_text(json.dumps(summary))
  for stream in ('stdout','stderr'):(window/('A/debugger-manager.'+stream)).write_bytes(b'captured failure')
  row=dict(run=run,side='A',pid=pid,closed=True,overflow=False,code=1,signal=None,error='READ_FAILED',argv=[str(original),'--window',str(window),'--side','A','--pid',str(pid)])
  (window/'result.json').write_text(json.dumps(dict(debugger={'sides':{'A':row}},timedOut=False,collectionCleanup='HDC_STREAMS_CLOSED')))
  argv=['INERT','-t','UNIT_NOT_DEVICE','shell',m.process_command(pid)]
  for suffix,entry in [('intent',dict(argv=argv,cwd=str(root),purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS')),
        ('started',dict(argv=argv,cwd=str(root),purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS',local_pid=987))]:
   (root/('pid-before-attach.'+suffix+'.json')).write_text(json.dumps(entry))
  for stream in ('stdout','stderr'):(root/('pid-before-attach.'+stream)).write_bytes(b'failure\n')
  result=dict(argv=argv,local_pid=987,exit_code=1,timed_out=False,capture_complete=True,overflow=False,termination=None,
      stdout_sha256=m.sha(root/'pid-before-attach.stdout'),stderr_sha256=m.sha(root/'pid-before-attach.stderr'))
  (root/'pid-before-attach.result.json').write_text(json.dumps(result))
  return window,root
 def inspect(self,window):
  return m.readonly_failure_facts(window,'A')
 def test_recorded_read_failure_closes_only_support(self):
  with tempfile.TemporaryDirectory() as f:
   w,r=self.fixture(f);facts=self.inspect(w)
   self.assertEqual(facts['labels'],['pid-before-attach']);self.assertTrue(facts['normal_guards_required'])
   self.assertEqual(facts['observation_result'],'NOT_OBSERVED');self.assertFalse((r/'RESULT.json').exists())
 def test_unknown_or_mutation_intent_blocks(self):
  for label in ('forward','server','client','unknown'):
   with self.subTest(label=label),tempfile.TemporaryDirectory() as f:
    w,r=self.fixture(f);(r/(label+'.intent.json')).write_text('{}')
    self.assertRaises(ValueError,self.inspect,w)
 def test_missing_tampered_open_and_foreign_blocks(self):
  for case in ('missing','tampered','timeout','overflow','unclosed','wrongrun','wrongargv','extra'):
   with self.subTest(case=case),tempfile.TemporaryDirectory() as f:
    w,r=self.fixture(f)
    if case=='missing':(r/'pid-before-attach.started.json').unlink()
    elif case=='tampered':(r/'pid-before-attach.stdout').write_bytes(b'changed')
    elif case in ('timeout','overflow'):
     p=r/'pid-before-attach.result.json';row=json.loads(p.read_text());row['timed_out' if case=='timeout' else 'overflow']=True;p.write_text(json.dumps(row))
    elif case=='unclosed':
     p=w/'result.json';row=json.loads(p.read_text());row['debugger']['sides']['A']['closed']=False;p.write_text(json.dumps(row))
    elif case=='wrongrun':
     p=w/'DATA_ATTEMPT_STARTED';row=json.loads(p.read_text());row['run']='b'*16;p.write_text(json.dumps(row))
    elif case=='wrongargv':
     p=r/'pid-before-attach.intent.json';row=json.loads(p.read_text());row['argv'][-1]='mutation';p.write_text(json.dumps(row))
    else:(r/'unrecorded.stdout').write_bytes(b'')
    self.assertRaises(ValueError,self.inspect,w)
 def test_generated_command_in_actual_linux_shell(self):
  command=m.process_command(987654).replace('/proc/987654','/proc/$p')
  # A shell forks before exec; $! alone is not executable readiness. Wait for
  # this exact controlled child's observed exec, not an arbitrary timeout.
  script='set -eu\n/bin/sleep 20 &\np=$!\ntrap \'kill "$p" 2>/dev/null || true; wait "$p" 2>/dev/null || true\' EXIT\nexpected=$(readlink -f /bin/sleep)\ni=0\nwhile [ "$(readlink /proc/$p/exe)" != "$expected" ]; do i=$((i+1)); [ "$i" -lt 128 ] || exit 9; kill -0 "$p" || exit 10; done\necho UNIT_PID=$p\n'+command+'\n'
  result=subprocess.run(['wsl','-d','Ubuntu-20.04','--','sh','-s'],input=script.encode(),capture_output=True,timeout=15)
  self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'));text=result.stdout.decode()
  pid=int(next(line.split('=')[1] for line in text.splitlines() if line.startswith('UNIT_PID=')))
  observed=m.process_fields(text,pid);self.assertEqual(Path(observed['exe']).name,'sleep');self.assertEqual(observed['argv'],['/bin/sleep','20'])
  self.assertEqual(text.count('MDDS_REMOTE_EXIT=0'),1)
 def test_missing_proc_read_is_observed_absence_not_identity(self):
  result=subprocess.run(['wsl','-d','Ubuntu-20.04','--','sh','-c',m.process_command(2147483647)],capture_output=True,timeout=15)
  text=result.stdout.decode();self.assertEqual(result.returncode,0);self.assertIn('MDDS_DEBUG_PROCESS_ABSENT=1',text)
  self.assertRaises(RuntimeError,m.process_identity,text,2147483647,'/any','1')

 def materialized_recovery(self,w):
  task=w.parent.parent;current=task.parent/'iteration-2';(current/'executor').mkdir(parents=True)
  reader=current/'executor/rmw_debugger.py';reader.write_bytes(SOURCE.read_bytes())
  (current/'MATERIALIZATION.json').write_text(json.dumps({'files':[dict(destination='executor/rmw_debugger.py',source='scripts/public_socket/shared_reattach/executor/rmw_debugger.py',sha256=m.sha(reader))]}))
  spec=importlib.util.spec_from_file_location('unit_bound_reader',reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  module.append_readonly_failure_closure(w,'A');p=w/'A/debugger/recovery/attempt-1/READ_ONLY_FAILURE_CLOSURE.json';facts=json.loads(p.read_text())
  review=current/'REVIEW.json';data=dict(scope='INDEPENDENT_DEBUGGER_READ_ONLY_RECOVERY_ACCEPTANCE',verdict='PASS',run=facts['run'],window=str(w),recovery_tool_sha256=m.sha(reader),normal_guards_pass=True,
      sides={'A':dict(closure_sha256=m.sha(p),decision='ACCEPTED_SUPPORT_READ_ONLY_CLOSURE')})
  review.write_text(json.dumps(data))
  acceptance=p.parent/'INDEPENDENT_ACCEPTANCE.json'
  receipt=dict(decision='ACCEPTED_SUPPORT_READ_ONLY_CLOSURE',run=facts['run'],side='A',closure_sha256=m.sha(p),recovery_tool_sha256=m.sha(reader),review_path=str(review),review_sha256=m.sha(review))
  acceptance.write_text(json.dumps(receipt));return current,p,review,acceptance

 def test_append_verify_and_future_reader_change(self):
  with tempfile.TemporaryDirectory() as f:
   w,r=self.fixture(f);current,p,review,acceptance=self.materialized_recovery(w)
   self.assertEqual(m.verify_bound_readonly_failure_closure(w,'A')['labels'],['pid-before-attach'])
   changed=current/'new-reader.py';changed.write_bytes(SOURCE.read_bytes()+b'\n# Next revision\n')
   spec=importlib.util.spec_from_file_location('new_unit_reader',changed);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   self.assertEqual(module.verify_bound_readonly_failure_closure(w,'A')['labels'],['pid-before-attach'])
   self.assertFalse((r/'RESULT.json').exists())

 def test_fail_foreign_and_missing_review_reject_even_matching_hash(self):
  for case in ('fail','foreignrun','foreignside','wrongtool','missing'):
   with self.subTest(case=case),tempfile.TemporaryDirectory() as f:
    w,r=self.fixture(f);current,p,review,acceptance=self.materialized_recovery(w)
    data=json.loads(review.read_text())
    if case=='fail':data['verdict']='FAIL'
    elif case=='foreignrun':data['run']='b'*16
    elif case=='foreignside':data['sides']={'B':data['sides']['A']}
    elif case=='wrongtool':data['recovery_tool_sha256']='0'*64
    if case=='missing':acceptance.unlink()
    else:
     review.write_text(json.dumps(data));receipt=json.loads(acceptance.read_text());receipt['review_sha256']=m.sha(review);acceptance.write_text(json.dumps(receipt))
    self.assertRaises((ValueError,FileNotFoundError),m.verify_bound_readonly_failure_closure,w,'A')

 def test_formal_gate_keeps_normal_guards_required(self):
  with tempfile.TemporaryDirectory() as f:
   w,r=self.fixture(f);current,p,review,acceptance=self.materialized_recovery(w)
   contract=current/'executor/task_contract.py';contract.write_bytes((SOURCE.parent/'task_contract.py').read_bytes())
   (current/'PRIVATE_CONFIG.json').write_text(json.dumps({'targets':{},'tokens':{},'repo_root':f}))
   spec=importlib.util.spec_from_file_location('unit_formal_contract',contract);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   module.require_debugger_restored(w,json.loads((w/'window.json').read_text()))
   self.assertRaises(FileNotFoundError,module._require_normal_restored,w)
   (w/'window-summary.json').write_text(json.dumps({'cleanup_failures':[]}))
   for side in ('A','B'):
    (w/side).mkdir(exist_ok=True)
    for name in ('baseline.json','after.json'):(w/side/name).write_bytes(b'UNIT_IDENTICAL')
    guard=dict(Passed=True,byte_match=True,metadata_match=True,process_absent=True,service_permission='DENIED',metadata_before={'evidence':str(w/side/'metadata-before')},metadata_after={'evidence':str(w/side/'metadata-after')})
    (w/side/'final-guard.json').write_text(json.dumps(guard))
    for name in ('ownership-guard.json','final-snapshot-guard.json'):(w/side/name).write_text(json.dumps({'Passed':True}))
   module._require_normal_restored(w)
   (w/'B/final-guard.json').unlink();self.assertRaises(FileNotFoundError,module._require_normal_restored,w)

if __name__=='__main__':unittest.main()
