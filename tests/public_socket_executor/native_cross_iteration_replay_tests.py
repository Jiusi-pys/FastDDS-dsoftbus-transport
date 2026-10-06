"""Controlled file-only replay tests; no device, grant, claim or fixture mutation."""
import argparse,ast,importlib.util,json,re,unittest
from pathlib import Path
from unittest.mock import patch

def run(task,source):
 repo=Path(__file__).resolve().parents[2];p=repo/'scripts/public_socket/shared_reattach/executor/route_binding.py'
 spec=importlib.util.spec_from_file_location('working_replay',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
 # Test new function against immutable selected inputs; materialized replay is a separate gate.
 m.__file__=str(task/'executor/route_binding.py')
 value=m.completed_query_replay(source);assert value['query_pair_consumed'] is True
 assert value['original_verifier_closure_sha256'] and value['current_replay_closure_sha256']
 real=Path.read_text;real_bytes=Path.read_bytes
 def altered(target,change):
  def read(path,*a,**k):
   text=real(path,*a,**k)
   if path.resolve()==target.resolve():
    obj=json.loads(text);change(obj);return json.dumps(obj)
   return text
  def read_bytes(path):
   data=real_bytes(path)
   if path.resolve()==target.resolve():
    obj=json.loads(data);change(obj);return json.dumps(obj).encode()
   return data
  with patch.object(Path,'read_text',read),patch.object(Path,'read_bytes',read_bytes):
   try:m.completed_query_replay(source)
   except (ValueError,KeyError,FileNotFoundError):return
   raise AssertionError('Changed replay input accepted')
 altered(task/'DEVICE_CONTRACT.json',lambda x:x.update(task_authority_root='FOREIGN_AUTHORITY'))
 altered(task/'PRIVATE_CONFIG.json',lambda x:x['targets'].update(A='OTHER_BOARD'))
 altered(task/'DEVICE_CONTRACT.json',lambda x:x.update(bounded_native_authorization={'sha256':'0'*64}))
 attempt=json.loads((source/'window.json').read_text())['ros_attempt']
 claim=Path(json.loads((task/'DEVICE_CONTRACT.json').read_text())['task_authority_root'])/'ros2/task-ledger'/f'query-{attempt}.json'
 altered(claim,lambda x:x.update(contract_sha256='0'*64))
 altered(source/'window-summary.json',lambda x:x.update(cleanup_failures=['NOT_RESTORED']))
 digest=m.digest;dependency=source.parent.parent/'executor/task_contract.py'
 with patch.object(m,'digest',lambda path:'0'*64 if Path(path).resolve()==dependency else digest(path)):
  try:m.completed_query_replay(source)
  except ValueError:pass
  else:raise AssertionError('Modified original dependency accepted')
 try:m.completed_query_replay(source.parent.parent.parent.parent/'windows/query-18')
 except ValueError:pass
 else:raise AssertionError('Other authority accepted')
 print('PASS 8 controlled replay cases; ORIGINAL_QUERY_CONSUMED; NO_DEVICE_OPERATIONS')

class RepairWindowName(unittest.TestCase):
 def test_exact_cross_iteration_guard_name_literal(self):
  source=Path(__file__).resolve().parents[2]/'scripts/public_socket/shared_reattach/executor/route_binding.py'
  tree=ast.parse(source.read_text(encoding='utf-8-sig'))
  function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='completed_query_replay')
  patterns=[n.args[0].value for n in ast.walk(function) if isinstance(n,ast.Call) and isinstance(n.func,ast.Attribute) and n.func.attr=='fullmatch' and n.args and isinstance(n.args[0],ast.Constant) and str(n.args[0].value).startswith('query-')]
  self.assertEqual(len(patterns),1)
  for name in ('query-1','query-21','query-21-r2','query-21-r10','query-21-r100'):
   with self.subTest(name=name):self.assertIsNotNone(re.fullmatch(patterns[0],name))
  for name in ('query-0','query-01','query-21-r0','query-21-r1','query-21-r02','query-21-r2-extra','query-21-r2/other','foreign-query-21-r2'):
   with self.subTest(name=name):self.assertIsNone(re.fullmatch(patterns[0],name))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--task',type=Path,required=True);p.add_argument('--source',type=Path,required=True);a=p.parse_args();run(a.task.resolve(),a.source.resolve())
