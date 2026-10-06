"""Pure frozen-contract tests: no private inputs or device operations."""
import ast
import copy
import hashlib
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def validator():
    source = ROOT/'scripts/public_socket/shared_reattach/executor/task_contract.py'
    tree = ast.parse(source.read_text(encoding='utf-8-sig'))
    node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'validate_route_native_mode')
    env = {'need': lambda value, reason: value or (_ for _ in ()).throw(ValueError(reason))}
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), 'exec'), env)
    return env['validate_route_native_mode']


class RouteNativeContract(unittest.TestCase):
    def setUp(self):
        self.mode = dict(contract='A_ETH1_ONE_RETURN', trigger=dict(side='A', interface='eth1', cycles=1, max_offline_seconds=90), offline_ordinal=1001, offline_total=4, authorization=dict(path='public-record.json', sha256='a'*64))

    def test_exact_frozen_scene(self):
        validator()(self.mode)

    def test_reject_scope_or_ledger_changes(self):
        for key, value in [('side','B'),('interface','wlan0'),('cycles',2),('max_offline_seconds',91)]:
            mode=copy.deepcopy(self.mode); mode['trigger'][key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): validator()(mode)
        for key,value in [('offline_ordinal',1),('offline_total',20),('contract','ANY_LINK')]:
            mode=copy.deepcopy(self.mode); mode[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): validator()(mode)

    def test_no_arbitrary_callback_or_route_override(self):
        mode=copy.deepcopy(self.mode); mode['injected_network']='synthetic'
        with self.assertRaises(ValueError): validator()(mode)

    def test_network_helper_and_collector_are_frozen_before_import(self):
        scratch=ROOT/'build/route-lifecycle-native-20261006/contract-fixtures';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            root=Path(temporary);(root/'executor').mkdir();(root/'recovery').mkdir()
            helper=root/'executor/route-network-capture.py';collector=root/'recovery/pregrant_recovery.py'
            helper.write_text('# inert helper\n');collector.write_text('# inert collector\n')
            sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
            source=ROOT/'scripts/public_socket/shared_reattach/executor/task_contract.py'
            tree=ast.parse(source.read_text(encoding='utf-8-sig'))
            names=('route_network_capture_inputs','verify_route_network_capture')
            env=dict(Path=Path,re=re,TASK_ROOT=root,repo_root=lambda:root,digest=sha,need=lambda value,reason:value or (_ for _ in ()).throw(ValueError(reason)))
            exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names],type_ignores=[]),str(source),'exec'),env)
            binding=dict(helper_sha256=sha(helper),collector_sha256=sha(collector))
            frozen=dict(route_lifecycle_native=self.mode,route_network_capture=binding,execution_files=[dict(path=p.relative_to(root).as_posix(),sha256=sha(p)) for p in (helper,collector)])
            self.assertEqual(env[names[0]](frozen),binding)
            window=root/'window';window.mkdir();copy_path=window/helper.name;copy_path.write_bytes(helper.read_bytes())
            config=dict(route_lifecycle_native=self.mode,route_network_capture=binding,executor_sha256={helper.name:sha(helper)},window_root=str(window))
            env[names[1]](config,frozen)
            copy_path.write_text('# changed window helper\n')
            with self.assertRaises(ValueError):env[names[1]](config,frozen)
            collector.write_text('# changed collector\n')
            with self.assertRaises(ValueError):env[names[0]](frozen)


if __name__=='__main__': unittest.main()
