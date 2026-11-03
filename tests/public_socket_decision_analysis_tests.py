import copy
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('decision',Path(__file__).parents[1]/'scripts/public_socket/analyze_decisions.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def document():
    row=dict(event=1,side=0,ingress=7,layer='BINDING',sequence=1,record=1,role=1,socket=11,
             branch='NO_REGISTRATION',bridge_called=0,refused_receive=1,refused_receive_total=1,
             result_known=0,result=-1,reason='UNOBSERVED',stage='UNOBSERVED',phase=3,status_known=1,status=43)
    return dict(run='new',snapshots=[dict(rows=[row],branches={'NO_REGISTRATION':1},reasons={},
        summary=dict(count=1,total=1,overflow=0,unknown=0,limit=2))])

class Accounting(unittest.TestCase):
    def test_no_registration(self):
        v=m.analyze(document(),'new');self.assertEqual(v['status'],'COMPLETE_DECISION_ACCOUNTING')
        self.assertEqual(v['binding_refused_receive'],1);self.assertNotIn('harmless',v)
    def test_repeated_snapshot_not_summed(self):
        d=document();d['snapshots']*=2;self.assertEqual(m.analyze(d,'new')['binding_refused_receive'],1)
    def test_missing_reason(self):
        d=document();d['snapshots'][0]['rows'][0]['branch']='UNKNOWN'
        self.assertNotEqual(m.analyze(d,'new')['status'],'COMPLETE_DECISION_ACCOUNTING')
    def test_bridge_not_called_cannot_be_budget(self):
        d=document();d['snapshots'][0]['rows'][0]['reason']='GLOBAL_BYTES'
        with self.assertRaises(ValueError):m.analyze(d,'new')
    def test_duplicate_event(self):
        d=document();s=d['snapshots'][0];s['rows']*=2;s['summary'].update(count=2,total=2);s['branches']['NO_REGISTRATION']=2
        with self.assertRaises(ValueError):m.analyze(d,'new')
    def test_missing_event(self):
        d=document();d['snapshots'][0]['rows']=[]
        with self.assertRaises(ValueError):m.analyze(d,'new')
    def test_different_run(self):
        with self.assertRaises(ValueError):m.analyze(document(),'historical')
    def test_overflow_not_complete(self):
        d=document();s=d['snapshots'][0];s['summary'].update(total=2,overflow=1,limit=1);s['branches']['NO_REGISTRATION']=2
        self.assertEqual(m.analyze(d,'new')['status'],'DETAIL_TRUNCATED')
    def test_accepted_is_not_refused(self):
        d=document();s=d['snapshots'][0];r=s['rows'][0];r.update(branch='BRIDGE_ACCEPTED',bridge_called=1,result_known=1,result=0,reason='ACCEPTED_QUEUE')
        s['branches']={'BRIDGE_ACCEPTED':1};s['reasons']={'ACCEPTED_QUEUE':1}
        with self.assertRaises(ValueError):m.analyze(d,'new')
    def test_unknown_bridge_result(self):
        d=document();s=d['snapshots'][0];r=s['rows'][0];r.update(branch='BRIDGE_REFUSED',bridge_called=1,result_known=0)
        s['branches']={'BRIDGE_REFUSED':1};s['reasons']={'UNOBSERVED':1}
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_layers_are_not_unique_losses(self):
        d=document();s=d['snapshots'][0];r=copy.deepcopy(s['rows'][0]);r.update(event=2,layer='ADAPTER',record=0,phase=1,status=5,branch='ADAPTER_PHASE',refused_receive=0,refused_receive_total=0)
        s['rows'].append(r);s['branches']['ADAPTER_PHASE']=1;s['summary'].update(count=2,total=2)
        v=m.analyze(d,'new');self.assertEqual(v['binding_refused_receive'],1);self.assertEqual(v['adapter_rejected'],1)
        self.assertIsNone(v['unique_lost_messages'])
    def test_reader_success_is_not_cause(self):
        d=document();d['reader_all_received']=True;d['snapshots'][0]['rows'][0]['branch']='UNKNOWN'
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_no_diagnostics_incomplete(self):
        with self.assertRaises(ValueError):m.analyze(dict(run='new',snapshots=[]),'new')
    def test_unknown_enum(self):
        d=document();s=d['snapshots'][0];s['rows'][0].update(branch='BRIDGE_REFUSED',bridge_called=1,result_known=1,result=3,reason='FUTURE_ENUM')
        s['branches']={'BRIDGE_REFUSED':1};s['reasons']={'FUTURE_ENUM':1}
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_unknown_result_integer(self):
        d=document();s=d['snapshots'][0];s['rows'][0].update(branch='BRIDGE_REFUSED',bridge_called=1,result_known=1,result=999,reason='TOO_LARGE')
        s['branches']={'BRIDGE_REFUSED':1};s['reasons']={'TOO_LARGE':1}
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_wrong_side(self):
        d=document();d['side']=0;d['snapshots'][0]['rows'][0]['side']=999
        with self.assertRaises(ValueError):m.analyze(d,'new')
    def test_unmatched_sdk_reject(self):
        d=m.parse('DDS_MAIN run=new side=A\nPROBE_API event=1 name=OnBytesRejected role=1 socket=11 value=5 length=32\nPROBE_TRACE count=1 overflow=0\nRX_DECISION_SUMMARY count=0 total=0 overflow=0 unknown=0 limit=2\n')
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_adapter_branch_status(self):
        for known,status in [(1,999),(0,5),(1,0)]:
            d=document();s=d['snapshots'][0];s['rows'][0].update(layer='ADAPTER',record=0,phase=1,branch='ADAPTER_PHASE',refused_receive=0,refused_receive_total=0,status_known=known,status=status)
            s['branches']={'ADAPTER_PHASE':1}
            self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_unknown_phase(self):
        d=document();d['snapshots'][0]['rows'][0]['phase']=999
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')
    def test_unknown_stage(self):
        d=document();s=d['snapshots'][0];s['rows'][0].update(branch='BRIDGE_REFUSED',bridge_called=1,result_known=1,result=5,status=4,reason='TOO_LARGE',stage='FUTURE_STAGE')
        s['branches']={'BRIDGE_REFUSED':1};s['reasons']={'TOO_LARGE':1}
        self.assertEqual(m.analyze(d,'new')['status'],'UNKNOWN_REASON')

if __name__=='__main__':unittest.main()
