"""Formal live-capture validator with explicit local modeled input, no device I/O."""
import copy
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ros_load', REPO/'scripts/public_socket/load_origin.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class RosLoad(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        run = '1234567890abcdef'
        remote = '/data/local/tmp/mdds_dds_'+run+'_A'
        self.config = dict(scenario='ROS2_DSOFTBUS',simulated=True,run=run,
            remote={'A':remote},system_library_dirs=['/system/lib64/platformsdk','/system/lib64'],
            main_member='ordinary_ros2_transport_consumer',token={'A':'671088739'},
            package_artifacts=[{'name':'ordinary_ros2_transport_consumer'},{'name':'libfastrtps.so.2.14'}],
            required_platform_paths=['/system/lib64/platformsdk/libsoftbus_client.z.so'])
        env = dict(PATH='/system/bin:/system/xbin',LD_LIBRARY_PATH=remote+':/system/lib64/platformsdk:/system/lib64',
            SKIP_DEFAULT_XML='1',ROS_LOG_DIR=remote+'/ros-log',RMW_IMPLEMENTATION='rmw_fastrtps_cpp',ROS_DOMAIN_ID='201',
            ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET',FASTDDS_DSOFTBUS_REQUIRED_CONFIG=remote+'/ros.conf',
            FASTDDS_DSOFTBUS_NATIVE_TOKEN='671088739',FASTDDS_DSOFTBUS_IDENTITY_ADMITTED='1',
            FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=remote+'/control')
        self.env=env
        self.write_env()
        self.put('load-status.txt',f'status=COMPLETE\nrun={run}\npid=101\nstarttime=123\nvector_format=NUL_V1\nscope=LOCAL_MODELED_CAPTURE\n')
        self.put('load-exe.txt',remote+'/ordinary_ros2_transport_consumer\n')
        self.put('load-cwd.txt',remote+'\n')
        self.put('load-cmdline.txt','\0'.join([remote+'/ordinary_ros2_transport_consumer','A',run,'dual'])+'\0')
        for name in ['load-stat-before.txt','load-stat-after.txt']:
            self.put(name,'101 (local_ros_model) S '+'0 '*18+'123 0\n')
        self.put('load-uptime.txt','10.0 0\n')
        self.put('load-maps.txt','\n'.join('1000-2000 r-xp 0 00:01 1 '+p for p in [remote+'/'+x['name'] for x in self.config['package_artifacts']]+self.config['required_platform_paths'])+'\n')
        self.put('load-collector.json',json.dumps(dict(run=run,side='A',target_pid=101,pass_=True,
            remoteExit=0,hdcExit=0,collectorClosed=True,signal=None,output_bytes=dict(complete=True,overflow=0))).replace('"pass_"','"pass"'))
    def put(self,name,value):
        (self.directory/name).write_text(value,encoding='utf-8')
    def write_env(self):
        self.put('load-environ.txt','\0'.join(k+'='+v for k,v in self.env.items())+'\0')
    def check(self):
        return m.analyze(self.directory,self.config,'A')
    def test_exact_ros_argv_and_environment(self):
        r=self.check();self.assertTrue(r['complete'],r['errors'])
    def test_wrong_or_missing_configuration(self):
        for name in ('ROS_LOG_DIR','RMW_IMPLEMENTATION','ROS_DOMAIN_ID','FASTDDS_DSOFTBUS_REQUIRED_CONFIG','FASTDDS_DSOFTBUS_NATIVE_TOKEN','FASTDDS_DSOFTBUS_IDENTITY_ADMITTED','FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX','SKIP_DEFAULT_XML'):
            with self.subTest(name=name):
                old=self.env.pop(name);self.write_env();self.assertFalse(self.check()['complete']);self.env[name]=old
        self.env['RMW_IMPLEMENTATION']='rmw_cyclonedds_cpp';self.write_env();self.assertFalse(self.check()['complete'])
    def test_wrong_logging_directory(self):
        self.env['ROS_LOG_DIR']='/tmp/unrelated/log';self.write_env();self.assertFalse(self.check()['complete'])
    def test_wrong_side_run_or_mode(self):
        remote=self.config['remote']['A']
        for args in [('B',self.config['run'],'dual'),('A','0'*16,'dual'),('A',self.config['run'],'reliable')]:
            self.put('load-cmdline.txt','\0'.join([remote+'/'+self.config['main_member'],*args])+'\0');self.assertFalse(self.check()['complete'])
    def test_modeled_capture_is_never_device_evidence(self):
        self.config['simulated']=False;self.assertFalse(self.check()['complete'])
    def test_unapproved_environment(self):
        self.env['FASTRTPS_DEFAULT_PROFILES_FILE']='/tmp/bypass.xml';self.write_env();self.assertFalse(self.check()['complete'])
    def test_native_scene_exact_environment(self):
        self.config['bounded_native_reaccept']={'contract':'BOUNDED_NORMAL_ONCE'}
        self.env['FDDST_BOUNDED_NATIVE_SCENE']='BOUNDED_NORMAL_ONCE';self.write_env()
        r=self.check();self.assertTrue(r['complete'],r['errors'])
        self.env['FDDST_BOUNDED_NATIVE_SCENE']='ARBITRARY';self.write_env();self.assertFalse(self.check()['complete'])
        self.env.pop('FDDST_BOUNDED_NATIVE_SCENE');self.write_env();self.assertFalse(self.check()['complete'])
    def test_ordinary_scene_refuses_native_environment(self):
        self.env['FDDST_BOUNDED_NATIVE_SCENE']='BOUNDED_NORMAL_ONCE';self.write_env();self.assertFalse(self.check()['complete'])

if __name__=='__main__':unittest.main()
