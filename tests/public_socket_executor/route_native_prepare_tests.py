"""Actual prepare() with inert local boundaries; no device/permission authority."""
import ast
import hashlib
import importlib.util
import json
from pathlib import Path
import secrets
import shutil
import tempfile
from types import SimpleNamespace
import unittest

REPO=Path(__file__).resolve().parents[2]
EXECUTOR=REPO/'scripts/public_socket/shared_reattach/executor'


class RoutePreparation(unittest.TestCase):
    def test_real_formatter_freezes_two_sided_route_flag_and_separate_ledger(self):
        scratch=REPO/'build/route-lifecycle-native-20261006/contract-fixtures'
        scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            task=Path(temporary);here=task/'executor';here.mkdir();(task/'windows').mkdir()
            for source in EXECUTOR.iterdir():
                if source.is_file():shutil.copyfile(source,here/source.name)
            for name in ('bounded-output.cjs','record-pair.cjs','observe-native.cjs','load_origin.py','capture-load.sh'):
                shutil.copyfile(EXECUTOR.parent.parent/name,here/name)
            (here/'route_binding.py').write_text('def load_route_binding(path): return {"peer":{"A":"fixture-B","B":"fixture-A"}}\n')
            (here/'route-network-capture.py').write_text('# inert fixture; never invoked\n')
            (task/'recovery').mkdir();(task/'recovery/pregrant_recovery.py').write_text('# inert collector; never invoked\n')
            sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
            private=dict(repo_root=str(REPO),hdc_path='INERT_NO_DEVICE',permission_baseline_sha256={},ros2_service_prefix='fixture',ros2_package_name='fixture.package')
            (task/'PRIVATE_CONFIG.json').write_text('{}')
            mode=dict(contract='A_ETH1_ONE_RETURN',trigger=dict(side='A',interface='eth1',cycles=1,max_offline_seconds=90),offline_ordinal=1001,offline_total=4,authorization=dict(path='inert-local-record',sha256='a'*64))
            frozen=dict(scope='ROS_SCOPE',route_lifecycle_native=mode,bounded_native_reaccept=dict(order='outgoing-first'),header_sha256={},local_contract_test=True)
            freeze=task/'DEVICE_CONTRACT.json';freeze.write_text(json.dumps(frozen))
            build=task/'build';package=build/'package';package.mkdir(parents=True)
            main=package/'ordinary_ros2_transport_consumer';main.write_bytes(b'INERT_LOCAL_PACKAGE_NOT_ELF')
            state=build/'source-state.json';state.write_text('{}')
            (build/'build-manifest.json').write_text(json.dumps(dict(bridgeExit=0,configureExit=0,buildExit=0,main=main.name,artifacts=[dict(name=main.name,sha256=sha(main),path=str(main))],sourceStateSha256=sha(state))))
            helper=task/'inert-helper';helper.write_bytes(b'NOT_EXECUTABLE')
            preflight=task/'preflight.json';preflight.write_text(json.dumps(dict(eligible_for_executor_gate=True,permission_helper=dict(path=str(helper)),platform={},required_platform_paths=['/fixture/libsoftbus_client.z.so'])))
            route=task/'route.json';route.write_text('{}')
            checked=[]
            contract=SimpleNamespace(PRIVATE=private,INSTALLED_SCENARIO='INSTALLED',ROS_SCENARIO='ROS',ROS_SCOPE='ROS_SCOPE',CASES=['ROS'],TARGETS=dict(A='fixture-A',B='fixture-B'),TOKENS=dict(A='1',B='2'),LIBDIRS=[],ROS_STAGES=['legacy'],ROS_NATIVE_STAGES=['native'],ROS_ROUTE_NATIVE_STAGES=['LISTENER_READY','PRE_READY','PRE_DONE','OFFLINE_READY','OFFLINE_DONE','REACCEPT_DONE','POST_DONE','TEARDOWN_READY','DDS_DELETED'],OBSERVED_SCOPE='OBSERVED',load=lambda p:dict(freeze_path=str(freeze)),layered=lambda:SimpleNamespace(SCENARIO='DDS'),verify_route_native_authorization=lambda f:checked.append(f),ros_task_mode=lambda f:False,verify_window=lambda c:None)
            contract.route_network_capture_inputs=lambda f:dict(helper_sha256=sha(here/'route-network-capture.py'),collector_sha256=sha(task/'recovery/pregrant_recovery.py'))
            source=EXECUTOR/'prepare-window.py';tree=ast.parse(source.read_text(encoding='utf-8-sig'))
            node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='prepare')
            env=dict(Path=Path,json=json,hashlib=hashlib,secrets=secrets,shutil=shutil,importlib=__import__('importlib'),contract=contract,digest=sha,__file__=str(here/'prepare-window.py'))
            exec(compile(ast.Module(body=[node],type_ignores=[]),str(source),'exec'),env)
            destination=task/'windows/data-fixture'
            env['prepare']('ROS',build,destination,preflight,route,contract_path=freeze)
            cfg=json.loads((destination/'window.json').read_text())
            self.assertEqual(cfg['route_lifecycle_native'],mode);self.assertEqual(len(cfg['stages']),9)
            self.assertEqual(cfg['expected_writes'],dict(A=10,B=10));self.assertEqual(cfg['record_count'],20)
            self.assertEqual(len(checked),1)
            self.assertEqual(cfg['route_network_capture']['helper_sha256'],sha(destination/'route-network-capture.py'))
            for side in 'AB':
                values=dict(line.split('=',1) for line in (destination/side/'aux/ros.conf').read_text().splitlines())
                self.assertEqual(values['route_lifecycle'],'1');self.assertEqual(values['backend'],'OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL')
            self.assertFalse(list(task.rglob('DATA_ATTEMPT_STARTED')))


if __name__=='__main__':unittest.main()
