"""Actual two-endpoint Fake probe core replay, NOT a device-emitter proof.

The Host probe shares one frame store. Project its immutable global event prefix
and each delete boundary onto the known Fake socket side before testing the
common ledger core. Real device admission still requires two untouched logs and
the formal analyze(case,texts,cycles) entry, including main/stages/API summaries.
"""
import argparse,copy,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/public_socket'))
import analyze_shared as a
p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--cycles',type=int,required=True)
p.add_argument('--case',choices=('SHARED_W1','SHARED_W2'),required=True);args=p.parse_args()
b=a.parse(args.log.read_text(encoding='utf-8-sig'))
socket_side={100+i*100+j:str(i) for i in range(2) for j in range(3)}
entries={r[k]:r['side'] for r in b['SHARED_NATIVE'] for k in ('listener_entry','outgoing_entry')}
for tag in ('DDS_FRAME','PROBE_API'):
    for r in b[tag]:r['_source_side']=socket_side.get(int(r['socket']),entries.get(r.get('entry'),'unattributed'))
for side in ('0','1'):
    rows=[r for r in b['DDS_FRAME'] if r['_source_side']==side]
    events=[int(r['event']) for r in rows]
    for r in b['SHARED_DELETE']:
        if r['side']==side:r['frames']=str(sum(e<=int(r['frames']) for e in events))
    for event,r in enumerate(rows,1):r['_process_event']=r['event'];r['event']=str(event)
run=b['SHARED_SAMPLING'][0]['run']
summary={'omitted_observations':b['DDS_FRAME_SUMMARY'][0]['omitted']}
def core(rows):return a.analyze_cycle_ledgers(rows,[],[],rows['DDS_FRAME'],summary,run,args.cycles,args.case)
class ActualCore(unittest.TestCase):
    def test_actual_core(self):self.assertEqual(core(b)['reader_samples'],30*args.cycles)
    def test_wrong_creation_order_rejected(self):
        wrong='SHARED_W2' if args.case=='SHARED_W1' else 'SHARED_W1'
        with self.assertRaisesRegex(ValueError,'target creation order'):
            a.analyze_cycle_ledgers(b,[],[],b['DDS_FRAME'],summary,run,args.cycles,wrong)
    def test_delete_counter_zero(self):
        x=copy.deepcopy(b)
        for r in x['SHARED_DELETE']:r['frames']='0'
        with self.assertRaisesRegex(ValueError,'old receiver after delete'):core(x)
    def test_native_record_changed(self):
        x=copy.deepcopy(b)
        for r in x['SHARED_NATIVE']:
            if r['cycle']=='2':r['listener_record']='999';r['accepted_record']='999'
        with self.assertRaisesRegex(ValueError,'native continuity'):core(x)
    def test_duplicate_shutdown(self):
        x=copy.deepcopy(b)
        for side in ('0','1'):
            rows=[r for r in x['PROBE_API'] if r['_source_side']==side and r['name']=='Shutdown_enter']
            if len(rows)>1:rows[1]['socket']=rows[0]['socket'];break
        with self.assertRaisesRegex(ValueError,'duplicate/foreign Shutdown'):core(x)
unittest.main(argv=[sys.argv[0]])
