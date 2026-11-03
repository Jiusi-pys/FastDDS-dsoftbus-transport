"""Required real-emitter replay. Missing/incomplete supplied logs fail, never skip."""
import argparse
from pathlib import Path
import re
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/public_socket'))
import analyze_shared as a
p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--cycles',type=int,required=True)
args=p.parse_args();text=args.log.read_text(encoding='utf-8-sig');n=args.cycles
class Replay(unittest.TestCase):
    def reject(self,changed):
        self.assertNotEqual(text,changed,'mutation must alter real text')
        with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[changed],n)
    def change(self,tag,old,new,predicate=lambda line:True):
        return '\n'.join(line.replace(old,new) if line.startswith(tag+' ') and predicate(line) else line for line in text.splitlines())
    def test_real_emitter(self):
        result=a.analyze('HOST_CYCLE',[text],n)
        self.assertEqual(result['reader_samples'],sum(line.startswith('SHARED_TAKE ') for line in text.splitlines()))
        self.assertEqual(result['reader_samples'],30*n)
    def test_missing_round(self):self.reject('\n'.join(line for line in text.splitlines() if not(line.startswith('SHARED_TAKE ') and f' cycle={n} ' in line)))
    def test_duplicate_round(self):self.reject(text+'\n'+'\n'.join(line for line in text.splitlines() if line.startswith('SHARED_TAKE ') and ' cycle=1 ' in line))
    def test_compensated_side(self):self.reject(self.change('SHARED_TAKE','side=1','side=0',lambda line:f' cycle={n} ' in line))
    def test_count_creates_as_rebuilds(self):self.reject(self.change('SHARED_RESULT',f'p_created={n+1}',f'p_created={n}'))
    def test_q_rebuild(self):self.reject(self.change('SHARED_TAKE','life=1','life=2',lambda line:' group=Q ' in line))
    def test_wrong_run(self):self.reject(self.change('SHARED_MATCH','run=','run=f'))
    def test_previous_round_match(self):
        if n==1:self.reject(self.change('SHARED_MATCH','cycle=1','cycle=0'))
        else:self.reject(self.change('SHARED_MATCH',f'cycle={n}',f'cycle={n-1}'))
    def test_wrong_phase(self):self.reject(self.change('SHARED_TAKE','phase=INITIAL','phase=GAP'))
    def test_wrong_group(self):self.reject(self.change('SHARED_TAKE','group=P','group=Q'))
    def test_false_full_detail_claim(self):self.reject(self.change('SHARED_SAMPLING','detail_scope=per_sample_frames','detail_scope=all_frames'))
    def test_final_delete_missing(self):self.reject('\n'.join(line for line in text.splitlines() if not(line.startswith('SHARED_DELETE ') and ' phase=FINAL ' in line)))
    def test_overflow_cannot_be_omission(self):self.reject(self.change('TRACE_SUMMARY','overflow=0','overflow=1'))
    def test_must_record_loss(self):self.reject(self.change('TRACE_SUMMARY','required_lost=0','required_lost=1'))
    def test_duplicate_field(self):self.reject(self.change('SHARED_TAKE','phase=INITIAL','phase=GAP phase=INITIAL'))
    def test_match_phase(self):self.reject(self.change('SHARED_MATCH','phase=INITIAL','phase=GAP'))
    def test_frame_run(self):self.reject(self.change('HOST_MODEL','run=','run=999'))
    def test_frame_cycle(self):self.reject(self.change('HOST_MODEL','cycle=1','cycle=999'))
    def test_detail_rows(self):self.reject(re.sub(r'stored_detail_rows=\d+','stored_detail_rows=0',text))
    def test_write_identity_missing(self):self.reject('\n'.join(re.sub(r' (app|receiver)=\d+','',line) if line.startswith('SHARED_WRITE ') else line for line in text.splitlines()))
    def test_delete_identity_missing(self):self.reject('\n'.join(re.sub(r' (app|receiver)=\d+','',line) if line.startswith('SHARED_DELETE ') else line for line in text.splitlines()))
    def test_intermediate_records(self):self.reject('\n'.join(re.sub(r' records=\d+',' records=999999',line) if line.startswith('SHARED_NATIVE ') and 'stage=FINAL' not in line else line for line in text.splitlines()))
    def test_intermediate_bytes(self):self.reject('\n'.join(re.sub(r' bytes=\d+',' bytes=-1',line) if line.startswith('SHARED_NATIVE ') and 'stage=FINAL' not in line else line for line in text.splitlines()))
    def test_fake_actual_count(self):self.reject('\n'.join(re.sub(r' sends=\d+',' sends=0',line) if line.startswith('SHARED_LINK ') else line for line in text.splitlines()))
    def test_fake_queue_growth(self):self.reject('\n'.join(re.sub(r' record_peak=\d+',' record_peak=65',line) if line.startswith('SHARED_LINK ') else line for line in text.splitlines()))
unittest.main(argv=[sys.argv[0]])
