"""HOST model negatives and optional actual-build receipt regression. No devices."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('native_binding_under_test',
    REPO / 'integrations/ros2/target/native_bridge_receipt.py')
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)
RUN = '1234567890abcdef'


def model_log():
    lines = []
    for pid in ('0031', '0032'):
        lines.append(f'ROS_V1_SPDP run={RUN} side=A observation=1 direction=SDK_SEND socket=7 '
            f'writer={"aa" * 12}000100c2 sequence_high=0 sequence_low=1 data_offset=20 pid={pid} '
            f'bytes=24 kind=40000000 address={"11" * 16} port=01000001 occurrences=3 '
            'last_observation=100 last_sequence_high=0 last_sequence_low=100 batch_size=2')
    lines += [f'ROS_V1_OBSERVATION run={RUN} side=A captured=1 adverts=2 selected=1 faults=0',
        f'ROS_V1_AGGREGATION schema=1 run={RUN} side=A mode=EXACT_ORDERED_BATCH_IDENTITY slots=256 '
        'observed=6 repeated=4 repeated_batches=2 invalid_frame=0 unsupported_frame=0 parser_capacity=0 '
        'advert_capacity=0 invalid_selection=0 selection_capacity=0 counter_capacity=0',
        f'ROS_CLOSE_COMPONENTS schema=1 run={RUN} side=A runtime_result=0 observation_complete=1']
    return '\n'.join(lines) + '\n'


class LocatorLogModels(unittest.TestCase):
    def test_complete_coherent_batch_and_counts(self):
        self.assertEqual(M.verify_locator_aggregation(model_log(), RUN, 'A'),
            {'stored': 2, 'observed': 6, 'repeated': 4, 'distinct_batches': 1,
             'repeated_batches': 2, 'normal_close': 'PASS'})

    def test_each_fault_remains_fatal(self):
        for field in ('invalid_frame', 'unsupported_frame', 'parser_capacity', 'advert_capacity',
                      'invalid_selection', 'selection_capacity', 'counter_capacity', 'faults'):
            with self.subTest(field=field), self.assertRaises(ValueError):
                M.verify_locator_aggregation(model_log().replace(field + '=0', field + '=1'), RUN, 'A')

    def test_conservation_and_batch_coherence(self):
        for old, new in [('observed=6', 'observed=5'), ('repeated=4', 'repeated=3'),
            ('repeated_batches=2', 'repeated_batches=1'), ('batch_size=2', 'batch_size=1'),
            ('occurrences=3', 'occurrences=0'), ('slots=256', 'slots=4096'),
            ('last_observation=100', 'last_observation=0')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                M.verify_locator_aggregation(model_log().replace(old, new), RUN, 'A')
        lines = model_log().splitlines()
        lines[1] = lines[1].replace('last_observation=100', 'last_observation=99')
        with self.assertRaises(ValueError):
            M.verify_locator_aggregation('\n'.join(lines), RUN, 'A')

    def test_both_close_components_required(self):
        for old, new in [('runtime_result=0', 'runtime_result=2'),
                         ('observation_complete=1', 'observation_complete=0')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                M.verify_locator_aggregation(model_log().replace(old, new), RUN, 'A')

    def test_legacy_missing_and_duplicate_fields_rejected(self):
        for text in ['\n'.join(x for x in model_log().splitlines() if not x.startswith('ROS_V1_AGGREGATION ')),
                     model_log().replace('observed=6', 'observed=6 observed=6'),
                     model_log().replace('side=A', 'side=B'),
                     model_log().replace('occurrences=3', 'occurrences=18446744073709551616')]:
            with self.subTest(text=text[:40]), self.assertRaises(ValueError):
                M.verify_locator_aggregation(text, RUN, 'A')


@unittest.skipUnless(os.environ.get('R37_NATIVE_BINDING'), 'Actual source-bound build required; model tests do not qualify a native build')
class ActualBuildBinding(unittest.TestCase):
    def setUp(self):
        self.original = Path(os.environ['R37_NATIVE_BINDING'])
        self.build = M.checked_build(self.original)
        self.temp = tempfile.TemporaryDirectory(prefix='binding-test-', dir=M.EVIDENCE)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def reject(self, value):
        target = self.root / 'BINDING.json'
        target.write_text(json.dumps(value), encoding='utf-8')
        with self.assertRaises(ValueError):
            M.checked_build(target)

    def test_actual_current_build_accepted(self):
        self.assertNotEqual(self.build['artifact']['sha256'], M.BASELINE_NATIVE_SHA)
        self.assertEqual(len(self.build['compile_units']), 23)

    def test_changed_header_and_object_rejected(self):
        value = copy.deepcopy(self.build)
        name = next(k for k in value['input_sha256'] if k.endswith('.hpp'))
        value['input_sha256'][name] = '0' * 64
        self.reject(value)
        value = copy.deepcopy(self.build)
        value['compile_units'][0]['object_sha256'] = '0' * 64
        self.reject(value)

    def test_missing_unit_foreign_source_and_boolean_schema(self):
        value = copy.deepcopy(self.build)
        value['compile_units'].pop()
        self.reject(value)
        value = copy.deepcopy(self.build)
        value['source_root'] = str(REPO.parent / 'foreign')
        self.reject(value)
        value = copy.deepcopy(self.build)
        value['schema'] = True
        self.reject(value)

    def test_failed_command_not_hidden_by_binding(self):
        value = copy.deepcopy(self.build)
        receipt = json.loads(Path(value['receipts']['build']['path']).read_text(encoding='utf-8-sig'))
        receipt['exit_code'] = 1
        path = self.root / 'FAILED_BUILD.json'
        path.write_text(json.dumps(receipt), encoding='utf-8')
        value['receipts']['build'] = {'path': str(path), 'sha256': M.sha(path)}
        self.reject(value)

    def test_wrong_architecture_rejected(self):
        value = copy.deepcopy(self.build)
        header = bytearray(64)
        header[:6] = b'\x7fELF\x02\x01'
        header[16:20] = b'\x03\x00\x3e\x00'
        path = self.root / 'negative-only-x86-fixture.so'
        path.write_bytes(header)
        value['artifact'] = {'path': str(path), 'sha256': M.sha(path)}
        self.reject(value)


if __name__ == '__main__':
    unittest.main()
