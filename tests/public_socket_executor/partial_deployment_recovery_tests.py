"""Actual recovery classifier/reconciliation, bounded UNIT objects; no device."""
import importlib.util,tempfile,unittest,json,copy
from unittest.mock import patch
from pathlib import Path
SOURCE=Path(__file__).resolve().parents[2]/'scripts/public_socket/pregrant_recovery.py'
spec=importlib.util.spec_from_file_location('recovery_partial',SOURCE);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Partial(unittest.TestCase):
 def test_transfer_layers(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=Path(folder)/'input';source.write_bytes(b'x');(root/'exit-code.txt').write_text('0')
   for value,wanted in [('FileTransfer finish, Size:1, File count = 1, time:1ms rate:1.00kB/s','TRANSFER_REPORTED_SUCCESS'),('[Fail]Error opening file: no such file or directory, path:'+str(source),'HOST_FILE_OPEN_FAILED')]:
    (root/'stdout.log').write_text(value);self.assertEqual(m.transfer_classification(root,source),wanted)
   for value in ('','[Fail]some other failure','FileTransfer finish'):
    (root/'stdout.log').write_text(value);self.assertRaises(ValueError,m.transfer_classification,root,source)
   (root/'stdout.log').write_text('');self.assertEqual(m.transfer_classification(root,source,True),'LOCAL_FAKE_TRANSFER_REPORTED_SUCCESS')
 def test_positive_missing_transfer_evidence_is_required(self):
  expected=[dict(name='delivered',sha256='a'*64,transfer_class='TRANSFER_REPORTED_SUCCESS'),dict(name='failed-open',sha256='b'*64,transfer_class='HOST_FILE_OPEN_FAILED')]
  observations=[];absence=[]
  result=m.reconcile_owned(expected,['delivered'],lambda i,e:observations.append(e['name']) or {'mode':'644'},lambda i,e:absence.append(e['name']))
  self.assertEqual(observations,['delivered']);self.assertEqual(absence,['failed-open']);self.assertEqual(result[1]['disposition'],'ATTEMPTED_ABSENT_CONFIRMED');self.assertIsNone(result[1]['actual_file_sha256'])
  for names in ([],['foreign'],['delivered','delivered']):self.assertRaises(ValueError,m.reconcile_owned,expected,names,lambda i,e:{},lambda i,e:None)
 def test_present_mismatch_cannot_be_excused_as_failed_transfer(self):
  def mismatch(i,e):raise ValueError('OWNED_FILE_HASH_MISMATCH')
  self.assertRaises(ValueError,m.reconcile_owned,[dict(name='failed',transfer_class='HOST_FILE_OPEN_FAILED')],['failed'],mismatch,lambda i,e:None)
 def test_strict_old_reader_projection_does_not_rewrite_evidence(self):
  legacy='6e9fcdd0f89527ee69c05fed6604c6ba38c41125d21c172df4a6f3ddd7110bc8'
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);window=root/'window';window.mkdir();aux=['capture-load.sh','native_permission_probe','required-load-paths.txt','ros.conf']
   cfg=dict(auxiliary_sha256={s:dict.fromkeys(aux,'a'*64) for s in 'AB'});(window/'window.json').write_text(json.dumps(cfg))
   current=dict(window=str(window),sides={s:dict(retained_owned_artifacts=[dict(name='unit',sha256='a'*64,transfer_class='TRANSFER_REPORTED_SUCCESS')]) for s in 'AB'})
   old=copy.deepcopy(current)
   for s in 'AB':old['sides'][s]['retained_owned_artifacts'][0].pop('transfer_class')
   tool=str(root/'recovery/pregrant_recovery.py');selection=dict(facts=old,tools_root=str(root),tools_sha256={tool:legacy});snapshot=json.dumps(selection,sort_keys=True)
   with patch.object(m,'inspect_window',return_value=current),patch.object(m,'sha',return_value=legacy):
    m.validate_original_facts(selection);self.assertEqual(json.dumps(selection,sort_keys=True),snapshot)
    bad=copy.deepcopy(selection);bad['tools_sha256'][tool]='0'*64;self.assertRaises(ValueError,m.validate_original_facts,bad)
    bad=copy.deepcopy(selection);bad['facts']['sides']['A']['retained_owned_artifacts'][0]['name']='changed';self.assertRaises(ValueError,m.validate_original_facts,bad)
    current['sides']['A']['retained_owned_artifacts'][0]['transfer_class']='HOST_FILE_OPEN_FAILED';self.assertRaises(ValueError,m.validate_original_facts,selection)
if __name__=='__main__':unittest.main()
