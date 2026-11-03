"""Strict cancellation receipts through the formal cycle text entry."""
import copy
import unittest
import subprocess
import sys
import tempfile
from pathlib import Path
from public_socket_cycle_analysis_tests import fixture,render,a

def receipts():
    rows=fixture()
    for tag,r in rows:
        if tag=='SHARED_SAMPLING':r['send_policy']='selected_retirement_v1'
        if tag=='SHARED_ID':r['token']=r['receiver']
    for side in range(2):
        old=next(r for t,r in rows if t=='SHARED_ID' and r['side']==side and r['group']=='P' and r['life']==1)
        rows.append(('SHARED_SEND_CANCELLED',dict(run=old['run'],side=side,cycle=1,phase='GAP',group='P',life=1,app=old['app'],receiver=old['receiver'],token=old['token'],status=29,outcome=0,code_present=0,selected=1,view_closing=1,admission_closed=1,registration_ok=1,shared_live=1,count=1)))
        rows.append(('SHARED_SEND_SUMMARY',dict(run=old['run'],side=side,cycle=2,cancelled=1,control_ok=1)))
    return rows

class Cancellation(unittest.TestCase):
    def test_positive(self):a.analyze('HOST_CYCLE',[render(receipts())],2)
    def test_wrong_receipts(self):
        for key,value in dict(status=0,outcome=1,code_present=1,selected=0,view_closing=0,admission_closed=0,registration_ok=0,shared_live=0,app=999,receiver=999,token=999,cycle=2,phase='FINAL',group='Q',life=2,count=2).items():
            with self.subTest(key=key):
                rows=receipts();next(r for t,r in rows if t=='SHARED_SEND_CANCELLED')[key]=value
                with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[render(rows)],2)
    def test_missing_or_extra(self):
        for tag in ('SHARED_SEND_SUMMARY','SHARED_SEND_CANCELLED'):
            for duplicate in (False,True):
                with self.subTest(tag=tag,duplicate=duplicate):
                    rows=receipts();index=next(i for i,(t,r) in enumerate(rows) if t==tag)
                    if duplicate:rows.append(copy.deepcopy(rows[index]))
                    else:rows.pop(index)
                    with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[render(rows)],2)
    def test_undeclared(self):
        rows=receipts();next(r for t,r in rows if t=='SHARED_SAMPLING').pop('send_policy')
        with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[render(rows)],2)
    def test_legacy(self):a.analyze('HOST_CYCLE',[render(fixture())],2)
    def test_compensated_host_duplicate(self):
        for count in (2,5):
            with self.subTest(count=count):
                rows=receipts();original=next(r for t,r in rows if t=='SHARED_SEND_CANCELLED' and r['side']==0)
                rows.extend(('SHARED_SEND_CANCELLED',dict(original,count=i)) for i in range(2,count+1))
                next(r for t,r in rows if t=='SHARED_SEND_SUMMARY' and r['side']==0)['cancelled']=count
                with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[render(rows)],2)
    def test_required_policy_cli(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'stdout';output=Path(folder)/'result.json'
            command=[sys.executable,a.__file__,'--case','HOST_CYCLE','--cycles','2','--a',str(source),'--output',str(output),'--require-send-policy','selected_retirement_v1']
            for rows,code in ((receipts(),0),(fixture(),1)):
                source.write_text(render(rows),encoding='utf-8')
                self.assertEqual(subprocess.run(command,capture_output=True).returncode,code)
    def test_target_receipts(self):
        from public_socket_device_cycle_analysis_tests import target
        boards=target()
        for side,rows in enumerate(boards):
            next(r for t,r in rows if t=='SHARED_SAMPLING')['send_policy']='selected_retirement_v1'
            own=next(r for t,r in rows if t=='SHARED_ID' and r['group']=='P' and str(r['life'])=='1')
            cancelled=next(copy.deepcopy(r) for t,r in receipts() if t=='SHARED_SEND_CANCELLED' and r['side']==side)
            cancelled['token']=own['token']
            rows.append(('SHARED_SEND_CANCELLED',cancelled))
            rows.append(('SHARED_SEND_SUMMARY',dict(run=own['run'],side=side,cycle=2,cancelled=1,control_ok=1)))
        a.analyze('SHARED_W1',[render(rows) for rows in boards],2)
        # Compensate every aggregate, so only exact retired-lifetime uniqueness
        # (not a stale count) can reject this second observation of the same View.
        duplicate=copy.deepcopy(boards)
        original=next(r for t,r in duplicate[1] if t=='SHARED_SEND_CANCELLED')
        duplicate[1].append(('SHARED_SEND_CANCELLED',dict(original,count=2)))
        next(r for t,r in duplicate[1] if t=='SHARED_SEND_SUMMARY')['cancelled']=2
        with self.assertRaises((ValueError,KeyError)):a.analyze('SHARED_W1',[render(rows) for rows in duplicate],2)
        next(r for t,r in boards[1] if t=='SHARED_SEND_CANCELLED')['token']='999'
        with self.assertRaises((ValueError,KeyError)):a.analyze('SHARED_W1',[render(rows) for rows in boards],2)
if __name__=='__main__':unittest.main()
