"""Actual materialized ROS configuration preparation; local Fake query only.

The existing local-flow fixture supplies its own synthetic identities, claim and
recovery. No data process is launched. This tests configuration, not SDK policy
enforcement or ROS/device communication.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

REPO = Path(__file__).resolve().parents[2]
ROOT = Path(os.environ.get('ROS_SERVICE_NAMES_TEST_ROOT', str(
    REPO/'.artifacts/ros2-device-acceptance/local-negative-20261001-ros-service_names'))).resolve()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


class RosServiceNames(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if ROOT.exists():
            raise ValueError('Fresh isolated test root required')
        tool = Path(__file__).parent/'local_flow/run.py'
        for phase in ('init', 'query'):
            subprocess.run([sys.executable, str(tool), '--root', str(ROOT),
                            '--scenario', 'ROS2_DSOFTBUS', '--phase', phase,
                            '--window', 'query-2'], cwd=REPO, check=True)
        subprocess.run([sys.executable, str(ROOT/'executor/prepare-window.py'),
                        'ROS2_DSOFTBUS', '--build', str(ROOT/'build'),
                        '--destination', str(ROOT/'windows/data-2'),
                        '--preflight', str(ROOT/'preflight.local.json'),
                        '--route', str(ROOT/'windows/query-2/PRIVATE_ROUTE_BINDINGS.json'),
                        '--contract', str(ROOT/'DEVICE_CONTRACT.json')], cwd=REPO, check=True)
        cls.cfg = read(ROOT/'windows/data-2/window.json')
        cls.profile = read(ROOT/'PRIVATE_CONFIG.json')
        cls.configs = {}
        for side in ('A', 'B'):
            lines = (ROOT/'windows/data-2'/side/'aux/ros.conf').read_text().splitlines()
            cls.configs[side] = dict(line.split('=', 1) for line in lines)

    def test_emitted_prefix_contains_authorized_package_and_stem(self):
        expected = self.profile['ros2_package_name'] + '.' + self.profile['ros2_service_prefix']
        for side, config in self.configs.items():
            with self.subTest(side=side):
                self.assertEqual(config['service_prefix'], expected)
                self.assertEqual(config['authorized_package'], self.profile['ros2_package_name'])
                self.assertEqual(config['run'], self.cfg['run'])
        # The ledger retains the original stem; only the target configuration
        # expands it. The installed consumer already expands its own argv stem.
        self.assertEqual(self.cfg['service_prefix'], self.profile['ros2_service_prefix'])

    def test_all_target_roles_fit_selected_namespace(self):
        # The target appends .run.side.role to the emitted full prefix. These
        # synthetic dots-only patterns have the same match semantics as the
        # documented POSIX ERE namespace rule; this is not an SDK substitute.
        pattern = re.escape(self.profile['ros2_package_name']) + r'\..*'
        names = set()
        for config in self.configs.values():
            for role in ('listener', 'outgoing'):
                name = '.'.join((config['service_prefix'], config['run'], config['side'], role))
                self.assertRegex(name, pattern)
                self.assertLessEqual(len(name), 255)
                self.assertNotIn(name, names)
                names.add(name)
        self.assertEqual(len(names), 4)
        old_name = self.profile['ros2_service_prefix'] + '.' + self.cfg['run'] + '.0.listener'
        self.assertIsNone(re.search(pattern, old_name))

    def test_formal_query_restored_without_data_or_device_authority(self):
        spec = importlib.util.spec_from_file_location('name_test_contract', ROOT/'executor/task_contract.py')
        contract = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(contract)
        contract.require_restored(ROOT/'windows/query-2')
        frozen = read(ROOT/'DEVICE_CONTRACT.json')
        self.assertIs(frozen['local_contract_test'], True)
        self.assertIs(frozen['device_authorized'], False)
        self.assertIs(self.cfg['simulated'], True)
        self.assertFalse(list(ROOT.rglob('DATA_ATTEMPT_STARTED')))
        self.assertFalse((ROOT/'windows/data-2/EXECUTION_STARTED').exists())

    def test_reject_package_qualified_stem_and_overlength_fields(self):
        spec = importlib.util.spec_from_file_location('name_test_private', ROOT/'executor/private_config.py')
        private = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(private)
        qualified = dict(self.profile, ros2_service_prefix=self.profile['ros2_package_name']+'.x')
        with self.assertRaises(ValueError):
            private.validate_profile(qualified, ROOT, REPO)
        oversized = dict(self.profile, ros2_service_prefix='s'*41)
        with self.assertRaises(ValueError):
            private.validate_profile(oversized, ROOT, REPO)
        # Every field at its admitted limit still produces a complete name
        # below the source SDK maximum of 255 bytes; no truncation is needed.
        maximum = '.'.join(('p'*64, 's'*40, '0123456789abcdef', '1', 'outgoing'))
        self.assertLessEqual(len(maximum), 255)


if __name__ == '__main__':
    unittest.main()
