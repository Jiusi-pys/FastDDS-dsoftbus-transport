"""Formal analyzer bytes -> manager proof parser. Synthetic /proc, no authority."""
import hashlib
import importlib.util
import json
import shutil
import unittest
from pathlib import Path
from ros_load_tests import RosLoad, m as analyzer

source=Path(__file__).resolve().parents[2]/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
spec=importlib.util.spec_from_file_location('prerelease_manager',source)
manager=importlib.util.module_from_spec(spec);spec.loader.exec_module(manager)

class Prerelease(unittest.TestCase):
    put=RosLoad.put
    write_env=RosLoad.write_env
    def setUp(self):
        RosLoad.setUp(self)
        self.window=self.directory/'window';self.load=self.window/'A/load';self.load.mkdir(parents=True)
        # Deliberate raw-protocol fixture, not DATA dispatch or device admission.
        self.config['simulated']=False
        status=(self.directory/'load-status.txt').read_text().replace('scope=LOCAL_MODELED_CAPTURE','scope=LIVE_PROC')
        (self.directory/'load-status.txt').write_text(status)
        for item in self.directory.glob('load-*'):shutil.copyfile(item,self.load/item.name)
        self.report=analyzer.analyze(self.load,self.config,'A')
        self.assertTrue(self.report['complete'],self.report['errors'])
        self.live=self.window/'A/load-pre-release-analysis.json'
        self.live.write_text(json.dumps(self.report))
        names=[p.name for p in self.load.glob('*.txt')]
        self.summary=dict(run=self.config['run'],side='A',host_collector_test=False,pass_=self.report['complete'],
            formal_complete=self.report['complete'],formal_exit=0,formal_child_closed=True,collection_cleanup='CHILD_STREAMS_CLOSED',
            formal_report_sha256=manager.sha(self.live),transfer=[dict(name=n,pass_=True,present=True,closed=True,sha256=manager.sha(self.load/n)) for n in names])
        self.summary['pass']=self.summary.pop('pass_')
        for row in self.summary['transfer']:row['pass']=row.pop('pass_')
        self.save()
    def save(self):
        (self.window/'A/load-pre-release-summary.json').write_text(json.dumps(self.summary))
    def consume(self):return manager.prerelease_origin(self.window,'A',101,self.config['run'])
    def test_completed_before_postrun(self):
        self.assertFalse((self.window/'A/actual-load-origin.json').exists())
        self.assertEqual(self.consume()['starttime'],'123')
    def test_wrong_or_incomplete_proof(self):
        original=json.loads(json.dumps(self.summary))
        for key,value in [('run','0'*16),('side','B'),('formal_report_sha256','0'*64),('host_collector_test',True),('formal_child_closed',False),('pass',False)]:
            self.summary=json.loads(json.dumps(original));self.summary[key]=value;self.save()
            with self.subTest(key=key):self.assertRaises(ValueError,self.consume)
    def test_wrong_pid_raw_or_model(self):
        self.assertRaises(ValueError,manager.prerelease_origin,self.window,'A',102,self.config['run'])
        (self.load/'load-maps.txt').write_text('changed');self.assertRaises(ValueError,self.consume)
    def test_postrun_cannot_replace_prerelease(self):
        shutil.copyfile(self.live,self.window/'A/actual-load-origin.json');self.live.unlink()
        self.assertRaises(FileNotFoundError,self.consume)

if __name__=='__main__':unittest.main()
