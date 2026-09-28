import copy
import importlib.util
import tempfile
import unittest
import json
import subprocess
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION='scripts/public_socket'
spec = importlib.util.spec_from_file_location('load_origin', ROOT/VERSION/'load_origin.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LoadOriginTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name)
        self.remote = '/data/local/tmp/mdds_restore_0123456789abcdef_A'
        self.cfg = {'run':'0123456789abcdef','remote':{'A':self.remote},
                    'package_artifacts':[{'name':n,'sha256':'a'*64} for n in ('public_socket_dds_experiment','libfastdds.so.3.6')],
                    'required_platform_paths':['/system/lib64/libc.so'],
                    'system_library_dirs':['/system/lib64']}
        self.write('load-status.txt', 'status=COMPLETE\nrun=0123456789abcdef\npid=42\nstarttime=123\nattempts=1\n')
        self.write('load-exe.txt', self.remote+'/public_socket_dds_experiment\n')
        self.write('load-cwd.txt', self.remote+'\n')
        self.write('load-environ.txt', 'PATH=/system/bin:/system/xbin\nLD_LIBRARY_PATH='+self.remote+':/system/lib64\nSKIP_DEFAULT_XML=1\n')
        self.write('load-cmdline.txt', self.remote+'/public_socket_dds_experiment\n--run\n0123456789abcdef\n')
        self.write('load-stat-before.txt', '42 (app) S '+'0 '*18+'123 0\n')
        self.write('load-stat-after.txt', '42 (app) S '+'0 '*18+'123 0\n')
        self.write('load-uptime.txt','123.45 0.0\n')
        self.write('load-collector.json',json.dumps({'run':'0123456789abcdef','side':'A','target_pid':42,'pass':True,'hdcExit':0,'remoteExit':0,
                    'collectorClosed':True,'signal':None,'output_bytes':{'complete':True,'overflow':0}}))
        self.maps = '\n'.join('1000-2000 r-xp 00000000 00:01 1 '+p for p in (
            self.remote+'/public_socket_dds_experiment',self.remote+'/libfastdds.so.3.6','/system/lib64/libc.so'))+'\n'
        self.write('load-maps.txt', self.maps)
    def tearDown(self): self.temp.cleanup()
    def write(self,name,data): (self.path/name).write_text(data,encoding='utf-8')
    def test_complete_identity(self): self.assertTrue(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_collector_requires_complete_closed_evidence(self):
        good=json.loads((self.path/'load-collector.json').read_text())
        for key in ('collectorClosed','output_bytes'):
            bad=copy.deepcopy(good);bad.pop(key)
            self.write('load-collector.json',json.dumps(bad))
            self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_null_collector_is_not_empty_success(self):
        self.write('load-collector.json','null')
        result=module.analyze(self.path,self.cfg,'A')
        self.assertFalse(result['complete'])
        self.assertIn('COLLECTOR_JSON_TYPE',result['errors'])
    def test_missing_capture_never_passes(self):
        (self.path/'load-maps.txt').unlink()
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_wrong_package_directory(self):
        self.write('load-maps.txt',self.maps.replace(self.remote+'/libfastdds','/data/local/tmp/old/libfastdds'))
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_wrong_filename(self):
        self.write('load-maps.txt',self.maps.replace('libfastdds.so.3.6','libfastdds.so.3.5'))
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_old_same_name_even_with_correct_one(self):
        self.write('load-maps.txt',self.maps+'3000-4000 r-xp 0 00:01 2 /data/local/tmp/old/libfastdds.so.3.6\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_wrong_executable(self):
        self.write('load-exe.txt','/data/local/tmp/old/public_socket_dds_experiment\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_pid_reuse_snapshot_fails(self):
        self.write('load-stat-after.txt','42 (app) S '+'0 '*18+'124 0\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_partial_sampler_status_fails(self):
        self.write('load-status.txt','status=PROCESS_EXITED\nrun=0123456789abcdef\npid=42\nstarttime=123\nattempts=1\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_run_binding(self):
        self.write('load-cmdline.txt', self.remote+'/public_socket_dds_experiment\n--run\nother\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_platform_missing(self):
        self.write('load-maps.txt',self.maps.replace('/system/lib64/libc.so','/system/lib64/other.so'))
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_inherited_loader_environment_fails(self):
        self.write('load-environ.txt','PATH=/system/bin:/system/xbin\nLD_LIBRARY_PATH='+self.remote+':/system/lib64\nSKIP_DEFAULT_XML=1\nLD_PRELOAD=/tmp/wrong.so\n')
        self.assertFalse(module.analyze(self.path,self.cfg,'A')['complete'])
    def test_collector_wrong_pid_fails(self):
        collector=json.loads((self.path/'load-collector.json').read_text())
        collector['target_pid']=44
        self.write('load-collector.json',json.dumps(collector))
        result=module.analyze(self.path,self.cfg,'A')
        self.assertFalse(result['complete'])
        self.assertIn('COLLECTOR_APP_PID_MISMATCH',result['errors'])


    def test_default_xml_environment_required(self):
        self.write('load-environ.txt', 'PATH=/system/bin:/system/xbin\nLD_LIBRARY_PATH='+self.remote+':/system/lib64\n')
        result=module.analyze(self.path,self.cfg,'A')
        self.assertFalse(result['complete'])
        self.assertIn('DEFAULT_XML_NOT_DISABLED',result['errors'])

if __name__=='__main__': unittest.main(verbosity=2)
