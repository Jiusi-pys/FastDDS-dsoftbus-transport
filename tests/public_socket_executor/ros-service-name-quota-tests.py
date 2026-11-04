"""New one-query/one-data ROS grant against the actual formal validator.

Uses a byte-identical saved predecessor ledger inside an isolated task root.
It cannot dispatch a driver or create a real device claim.
"""
import hashlib
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py'
PRIOR = REPO/'.artifacts/ros2-device-acceptance/observed-device-20261002/FINAL_ACTUAL_CONSUMPTION_LEDGER.json'
AUTH = 'ONE_ROS2_SERVICE_NAME_DEVICE_VALIDATION_AND_SUCCESSFUL_LOCAL_COMMIT'
PRIOR_SHA = '00ca1a2928ca8d082c7bcf9bb1ddc18fa21596e2fd34e934f82d9dafbce8f00c'
QUOTA = 'ROS2_SERVICE_NAME_20261002_1'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2)+'\n', encoding='utf-8')


class ExactNewGrant(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        parent = Path(tmp.name)
        self.root = parent/'exec-service-name-20261002'
        (self.root/'executor').mkdir(parents=True)
        shutil.copyfile(SOURCE, self.root/'executor/task_contract.py')
        write(self.root/'PRIVATE_CONFIG.json', {'targets': {'A': 'UNIT_A', 'B': 'UNIT_B'},
                                                 'tokens': {'A': '1', 'B': '2'}})
        spec = importlib.util.spec_from_file_location('new_ros_quota', self.root/'executor/task_contract.py')
        self.contract = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.contract)
        self.prior = parent/'observed-device-20261002/FINAL_ACTUAL_CONSUMPTION_LEDGER.json'
        self.prior.parent.mkdir()
        shutil.copyfile(PRIOR, self.prior)
        self.assertEqual(digest(self.prior), PRIOR_SHA)
        self.grant_path = self.root/'AUTHORIZATION.json'
        self.grant = {'authorization': AUTH, 'work_package': 'ROS2_SERVICE_NAME_DEVICE_VALIDATION',
                      'task_root': str(self.root), 'quota_id': QUOTA,
                      'new_limits': {'query_pair': 1, 'data_attempt': 1, 'repair_retry': 0},
                      'historical_ledger': {'path': str(self.prior), 'sha256': PRIOR_SHA},
                      'historical_used': {'query_pair': 4, 'data_attempt': 3},
                      'historical_remaining': 0, 'refund': False}
        self.freeze = {'scope': self.contract.ROS_SCOPE, 'authorization': AUTH,
                       'limits': self.contract.ROS_LIMITS_ONCE, 'new_quota_id': QUOTA}
        self.save()

    def save(self):
        write(self.grant_path, self.grant)
        self.freeze['authorization_record'] = {'path': str(self.grant_path),
                                                'sha256': digest(self.grant_path)}
        write(self.root/'DEVICE_CONTRACT.json', self.freeze)

    def verify(self):
        self.contract.verify_ros_authorization(self.freeze)
        self.contract.verify_ros_observed_grant(self.freeze)

    def test_exact_new_grant_and_fresh_attempt(self):
        self.verify()
        self.assertEqual(self.contract.ros_next_attempt(), 1)

    def test_specific_predecessor_path_required_even_when_bytes_match(self):
        other = self.root.parent/'other-ledger.json'
        shutil.copyfile(self.prior, other)
        self.grant['historical_ledger']['path'] = str(other)
        self.save()
        with self.assertRaises(ValueError):
            self.verify()

    def test_older_or_rewritten_ledger_rejected(self):
        old = REPO/'.artifacts/ros2-device-acceptance/run-20260930-local/FINAL_ACTUAL_CONSUMPTION_LEDGER.json'
        self.grant['historical_ledger'] = {'path': str(old), 'sha256': digest(old)}
        self.grant['historical_used'] = {'query_pair': 3, 'data_attempt': 2}
        self.save()
        with self.assertRaises(ValueError):
            self.verify()
        altered = json.loads(self.prior.read_text(encoding='utf-8'))
        altered['new_remaining']['data_attempt'] = 1
        write(self.prior, altered)
        self.grant['historical_ledger'] = {'path': str(self.prior), 'sha256': digest(self.prior)}
        self.grant['historical_used'] = {'query_pair': 4, 'data_attempt': 3}
        self.save()
        with self.assertRaises(ValueError):
            self.verify()

    def test_reused_quota_and_expanded_or_noninteger_limits_rejected(self):
        self.grant['quota_id'] = json.loads(self.prior.read_text(encoding='utf-8'))['quota_id']
        self.freeze['new_quota_id'] = self.grant['quota_id']
        self.save()
        with self.assertRaises(ValueError):
            self.verify()
        self.grant['quota_id'] = QUOTA
        self.freeze['new_quota_id'] = QUOTA
        for key, value in [('total_query_pairs', 2), ('total_attempts', 2),
                           ('repair_retries', 1), ('total_attempts', 1.0),
                           ('canonical_windows', True)]:
            with self.subTest(key=key, value=value):
                self.freeze['limits'] = dict(self.contract.ROS_LIMITS_ONCE, **{key: value})
                self.save()
                with self.assertRaises(ValueError):
                    self.verify()

    def test_first_data_dispatch_exhausts_new_attempt(self):
        self.verify()
        path = self.root/'ros2/attempt-ledger/data-1.json'
        write(path, {'scope': self.contract.ROS_SCOPE, 'attempt': 1})
        with self.assertRaisesRegex(ValueError, 'quota'):
            self.contract.ros_next_attempt()

    def test_direct_attempt_cannot_skip_new_original_grant(self):
        del self.freeze['authorization_record']
        write(self.root/'DEVICE_CONTRACT.json', self.freeze)
        with self.assertRaises(ValueError):
            self.contract.ros_next_attempt()

    def test_real_query_claim_carries_distinct_quota(self):
        self.verify()
        self.freeze['materialization_sha256'] = 'a'*64
        self.freeze['build_binding'] = {'source_state_sha256': 'b'*64}
        self.save()
        cfg = {'run': '0123456789abcdef', 'window_root': str(self.root/'windows/query-1'),
               'targets': {'A': 'UNIT_A', 'B': 'UNIT_B'}, 'ros_attempt': 1,
               'task_contract': {'freeze_path': str(self.root/'DEVICE_CONTRACT.json'),
                                 'freeze_sha256': digest(self.root/'DEVICE_CONTRACT.json')}}
        row = self.contract.ros_query_claim_row(cfg, False)
        self.assertEqual(row['quota_id'], QUOTA)
        self.assertEqual(row['authorization'], AUTH)
        self.assertEqual(row['purpose'], self.contract.ROS_SCENARIO)

    def test_old_observed_and_original_grants_preserved(self):
        self.contract.verify_ros_authorization({'authorization': self.contract.ROS_OBSERVED_AUTHORIZATION,
                                                'limits': self.contract.ROS_LIMITS_ONCE,
                                                'new_quota_id': 'ROS2_OBSERVED_20261002_1'})
        self.contract.verify_ros_authorization({'authorization': self.contract.ROS_AUTHORIZATION,
                                                'limits': self.contract.ROS_LIMITS})


if __name__ == '__main__':
    unittest.main()
