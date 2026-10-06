"""Adversarial replay of saved actual ROS stdout; no device or constructed PASS rows."""
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('ros_analysis', ROOT / 'scripts/public_socket/analyze_ros2.py')
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)
FIXTURE = Path(__file__).with_name('fixtures')
RUN = json.loads((FIXTURE / 'PROVENANCE.json').read_text())['run']


class ActualReplay(unittest.TestCase):
    def setUp(self):
        self.texts = {s: (FIXTURE / (s + '.stdout')).read_text() for s in 'AB'}

    def check_failure(self):
        self.assertFalse(MOD.analyze(self.texts, RUN, ['reliable', 'best_effort'])['pass'])

    def test_actual_complete(self):
        result = MOD.analyze(self.texts, RUN, ['reliable', 'best_effort'])
        self.assertTrue(result['pass'], result['errors'])
        self.assertEqual((result['observed_writes'], result['observed_takes']), (20, 20))

    def test_application_failure_is_not_shutdown_failure(self):
        self.texts['B'] = self.texts['B'].replace('side=B result=0','side=B result=4 result_scope=APPLICATION_EXIT_STATUS shutdown_return=1')
        result = MOD.analyze(self.texts, RUN, ['reliable', 'best_effort'])
        self.assertFalse(result['pass'])
        self.assertIn('application_exit:B', result['errors'])
        self.assertNotIn('context_close:B', result['errors'])

    def test_joined_delete_keeps_close_status_and_failure(self):
        self.texts['A'] = self.texts['A'].replace('ROS_CONTEXT_CLOSED ', 'Failed to delete participantROS_CONTEXT_CLOSED ')
        # The known joined form uses the actual current context-close schema.
        self.texts['A'] = self.texts['A'].replace('side=A result=0', 'side=A result=0 result_scope=APPLICATION_EXIT_STATUS shutdown_return=1')
        result = MOD.analyze(self.texts, RUN, ['reliable', 'best_effort'])
        self.assertFalse(result['pass'])
        self.assertIn('participant_delete_failed:A', result['errors'])
        self.assertNotIn('context_close:A', result['errors'])

    def test_joined_close_malformed_does_not_recover_identity(self):
        text = 'Failed to delete participantROS_CONTEXT_CLOSED run='+RUN+' side=A result=4 result_scope=APPLICATION_EXIT_STATUS shutdown_return=1 extra=1'
        with self.assertRaises(ValueError):
            MOD.rows(text, 'ROS_CONTEXT_CLOSED')

    def test_missing_take(self):
        self.texts['A'] = '\n'.join(x for x in self.texts['A'].splitlines() if not (x.startswith('ROS_TAKE ') and 'ordinal=3 ' in x and 'phase=reliable' in x))
        self.check_failure()

    def test_duplicate_take(self):
        self.texts['A'] += next(x for x in self.texts['A'].splitlines() if x.startswith('ROS_TAKE ')) + '\n'
        self.check_failure()

    def test_wrong_writer(self):
        line = next(x for x in self.texts['A'].splitlines() if x.startswith('ROS_TAKE '))
        gid = MOD.rows(line, 'ROS_TAKE')[0]['gid']
        self.texts['A'] = self.texts['A'].replace(gid, 'f' * len(gid), 1)
        self.check_failure()

    def test_wrong_run(self):
        self.texts['A'] = self.texts['A'].replace('ROS_WRITE run=' + RUN, 'ROS_WRITE run=' + '0' * 16, 1)
        self.check_failure()

    def test_wrong_topic(self):
        self.texts['A'] = self.texts['A'].replace('topic=/fdds_' + RUN + '/reliable/from_A', 'topic=/wrong/from_A', 1)
        self.check_failure()

    def test_phase_reallocation_same_total(self):
        self.texts['A'] = self.texts['A'].replace('phase=best_effort', 'phase=reliable')
        self.check_failure()

    def test_missing_context_close(self):
        self.texts['B'] = '\n'.join(x for x in self.texts['B'].splitlines() if not x.startswith('ROS_CONTEXT_CLOSED '))
        self.check_failure()

    def test_malformed_duplicate_field(self):
        self.texts['A'] = self.texts['A'].replace('ROS_TAKE ', 'ROS_TAKE ordinal=5 ', 1)
        self.check_failure()

    def test_empty_null_truncated(self):
        for value in ('', None, self.texts['B'][:100]):
            with self.subTest(value=repr(value)[:30]):
                self.assertFalse(MOD.analyze({'A': self.texts['A'], 'B': value}, RUN, ['reliable', 'best_effort'])['pass'])

    def test_wrong_service_request(self):
        line = next(x for x in self.texts['B'].splitlines() if x.startswith('ROS_SERVICE_REQUEST '))
        identifier = MOD.rows(line, 'ROS_SERVICE_REQUEST')[0]['request_id']
        self.texts['B'] = self.texts['B'].replace(line, line.replace('request_id=' + identifier, 'request_id=999'))
        self.check_failure()

    def test_extra_service_wrong_side(self):
        self.texts['A'] += next(x for x in self.texts['B'].splitlines() if x.startswith('ROS_SERVICE_REQUEST ')) + '\n'
        self.check_failure()


if __name__ == '__main__':
    unittest.main()
