#!/usr/bin/env python3
"""Host checks for scripts/release/run_cross_board.py: route-outage cleanup and scenario verdicts.

The board is an in-memory iptables model; nothing here touches hdc or a device.
"""
import importlib.util
import io
import re
import subprocess
import sys
import unittest
from collections import Counter
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).resolve().parent.parent / 'scripts' / 'release' / 'run_cross_board.py'
spec = importlib.util.spec_from_file_location('run_cross_board', SCRIPT)
rcb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcb)

IPS = ['192.0.2.1']
RULES = rcb.drop_rules(IPS)


class Board:
    """Answers the shell commands the script sends, against a model rule table."""

    def __init__(self):
        self.rules = Counter()
        self.adds = self.removes = 0
        self.fail_remove_calls = 0    # sh raises this many times instead of removing
        self.fail_with = subprocess.TimeoutExpired('hdc', 60)
        self.keep_rules = False       # removal runs but cannot delete anything
        self.drop_add = False         # install runs but does not take effect
        self.garble_check = False     # iptables -C prints nothing usable

    def sh(self, _target, cmd, timeout=60):
        if 'iptables -I' in cmd:
            self.adds += 1
            if not self.drop_add:
                self.rules.update(re.findall(r'iptables -I ([^;]+)', cmd))
            return ''
        if 'iptables -D' in cmd:
            self.removes += 1
            if self.fail_remove_calls:
                self.fail_remove_calls -= 1
                raise self.fail_with
            if not self.keep_rules:
                for rule in re.findall(r'iptables -D (.+?) 2>/dev/null', cmd):
                    self.rules.pop(rule, None)
            return ''
        if self.garble_check:
            return 'hdc: connection lost\n'
        return ''.join('ROUTE_RULE_RC=%d\n' % (0 if self.rules[r] else 1)
                       for r in re.findall(r'iptables -C (.+?) 2>/dev/null', cmd))


class RouteOutageTests(unittest.TestCase):
    def setUp(self):
        self.board = Board()
        patches = [mock.patch.object(rcb, 'sh', self.board.sh), mock.patch.object(rcb.time, 'sleep')]
        self.sleep = patches[1].start()
        patches[0].start()
        for p in patches:
            self.addCleanup(p.stop)
        self.log = io.StringIO()

    def outage(self, route='1'):
        return rcb.run_route_outages('B', IPS, route, self.log)

    def test_each_outage_installs_then_removes(self):
        self.assertEqual(self.outage('2'), 2)
        self.assertEqual((self.board.adds, self.board.removes), (2, 2))
        self.assertFalse(self.board.rules)

    def test_interrupt_during_outage_still_removes_rules(self):
        self.sleep.side_effect = [None, KeyboardInterrupt]   # the schedule wait, then the 15 s outage
        with self.assertRaises(KeyboardInterrupt):
            self.outage()
        self.assertEqual((self.board.adds, self.board.removes), (1, 1))
        self.assertFalse(self.board.rules)

    def test_exception_during_outage_still_removes_rules(self):
        self.sleep.side_effect = [None, RuntimeError('host fault')]
        with self.assertRaisesRegex(RuntimeError, 'host fault'):
            self.outage()
        self.assertFalse(self.board.rules)

    def test_interrupt_while_installing_still_removes_rules(self):
        real = self.board.sh

        def interrupted_after_add(target, cmd, timeout=60):
            out = real(target, cmd, timeout)
            if 'iptables -I' in cmd:
                raise KeyboardInterrupt   # the rule landed but hdc never returned
            return out
        with mock.patch.object(rcb, 'sh', interrupted_after_add), self.assertRaises(KeyboardInterrupt):
            self.outage()
        self.assertFalse(self.board.rules)

    def test_second_interrupt_cannot_abandon_cleanup(self):
        self.sleep.side_effect = [None, KeyboardInterrupt] + [None] * rcb.CLEANUP_ATTEMPTS
        self.board.fail_remove_calls, self.board.fail_with = 1, KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):
            self.outage()
        self.assertEqual(self.board.removes, 2)
        self.assertFalse(self.board.rules)

    def test_transient_cleanup_failure_is_retried(self):
        self.board.fail_remove_calls = 2
        self.assertEqual(self.outage(), 1)
        self.assertEqual(self.board.removes, 3)
        self.assertFalse(self.board.rules)

    def test_retries_wait_for_hdc_to_recover(self):
        self.board.fail_remove_calls = 1
        self.outage()
        self.sleep.assert_any_call(rcb.CLEANUP_RETRY_SECONDS)

    def test_rule_that_will_not_go_is_reported_with_the_fix(self):
        self.board.keep_rules = True
        with self.assertRaisesRegex(rcb.RouteCleanupError, 'iptables -D INPUT -s 192.0.2.1'):
            self.outage()
        self.assertEqual(self.board.removes, rcb.CLEANUP_ATTEMPTS)

    def test_unverifiable_removal_is_not_called_clean(self):
        def garbled_after_add(target, cmd, timeout=60):
            self.board.garble_check = 'iptables -D' in cmd or self.board.garble_check
            return self.board.sh(target, cmd, timeout)
        with mock.patch.object(rcb, 'sh', garbled_after_add), self.assertRaises(rcb.RouteCleanupError):
            self.outage()

    def test_cleanup_failure_does_not_hide_an_interrupt_silently(self):
        self.sleep.side_effect = [None, KeyboardInterrupt] + [None] * rcb.CLEANUP_ATTEMPTS
        self.board.keep_rules = True
        with self.assertRaises(rcb.RouteCleanupError) as caught:
            self.outage()
        self.assertIsInstance(caught.exception.__context__, KeyboardInterrupt)

    def test_outage_that_did_not_apply_is_an_error(self):
        self.board.drop_add = True
        with self.assertRaises(rcb.RouteOutageError):
            self.outage()
        self.assertEqual(self.board.removes, 1)

    def test_no_peer_address_is_an_error_not_a_silent_success(self):
        with self.assertRaises(rcb.RouteOutageError):
            rcb.run_route_outages('B', [], '1', self.log)
        self.assertEqual(self.board.adds, 0)


class GroupTests(unittest.TestCase):
    def test_all_expands_to_every_suite_group(self):
        self.assertEqual(rcb.parse_groups('all'), list(rcb.GROUPS))

    def test_comma_list_keeps_order_and_drops_repeats(self):
        self.assertEqual(rcb.parse_groups('qos,core,qos'), ['qos', 'core'])

    def test_names_the_board_would_skip_are_refused(self):
        for bad in ('cores', 'core,cores', 'all,core', 'core, qos', 'core,', '', 'CORE'):
            with self.subTest(bad), self.assertRaises(ValueError):
                rcb.parse_groups(bad)

    def run_cli(self, *args):
        argv = ['run_cross_board.py', '--version', '3.6.2', '--work', 'w', '--a', 'a', '--b', 'b', *args]
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(rcb, 'deploy') as deploy, \
                mock.patch('sys.stderr', io.StringIO()), self.assertRaises(SystemExit) as caught:
            rcb.main()
        deploy.assert_not_called()
        return caught.exception.code

    def test_cli_refuses_bad_selection_before_deploying(self):
        for args in (['--groups', 'cores'], ['--route', '3'], ['--stress', 'wsf2'],
                     ['--groups', 'qos', '--route', '1'], ['--stress', 'wsf1', '--route', '1']):
            with self.subTest(args):
                self.assertEqual(self.run_cli(*args), 2)


def case(side, group, name, status):
    return 'DSB_CASE side=%d fastdds=3.6.2 group=%s case=%s pass=%s reason=x' % (side, group, name, status)


def records(lines_by_board):
    cases = []
    for side, lines in lines_by_board.items():
        own, _ = rcb.collect_records(lines)
        cases += [dict(c, board=side) for c in own]
    return cases


class ScenarioTests(unittest.TestCase):
    def issues(self, lines, selected=('core',), route='0', stress='', outages=0, error=None):
        return rcb.scenario_issues(list(selected), route, stress, lines, records(lines), outages, error)

    def test_zero_case_run_is_not_a_pass(self):
        lines = {s: ['DSB_START', 'DSB_SUMMARY passed=0 failed=0 aborted=0', 'DSB_EXIT=0'] for s in 'AB'}
        found = self.issues(lines)
        for side in 'AB':
            self.assertIn('%s: selected group core has no execution record' % side, found)
            self.assertIn('%s: no case passed' % side, found)

    def test_complete_run_has_no_issues(self):
        lines = {'A': [case(0, 'core', 'c', '1')], 'B': [case(1, 'core', 'c', '1')]}
        self.assertEqual(self.issues(lines), [])

    def test_one_board_missing_a_group_is_named(self):
        lines = {'A': [case(0, 'core', 'c', '1'), case(0, 'qos', 'q', '1')], 'B': [case(1, 'core', 'c', '1')]}
        self.assertEqual(self.issues(lines, ('core', 'qos')), ['B: selected group qos has no execution record'])

    def test_not_applicable_group_counts_as_recorded_but_not_as_a_pass(self):
        na = {s: [case(i, 'security', 'dds_security', 'NOT_APPLICABLE')] for i, s in enumerate('AB')}
        self.assertEqual(self.issues(na, ('security',)), ['A: no case passed', 'B: no case passed'])
        mixed = {s: [case(i, 'core', 'c', '1'), case(i, 'security', 'dds_security', 'NOT_APPLICABLE')] for i, s in enumerate('AB')}
        self.assertEqual(self.issues(mixed, ('core', 'security')), [])

    def test_stress_expects_the_stress_record_not_the_groups(self):
        lines = {s: [case(i, 'stress', 'cft_race_stress_dsoftbus', '1')] for i, s in enumerate('AB')}
        self.assertEqual(self.issues(lines, list(rcb.GROUPS), stress='wsf1'), [])

    def test_route_run_needs_the_window_and_every_outage(self):
        base = {s: [case(i, 'core', 'c', '1')] for i, s in enumerate('AB')}
        self.assertEqual(self.issues(base, route='1'), ['A: route window never began', 'B: route window never began',
                                                        'route outages applied 0 of 1'])
        began = {s: l + ['DSB_ROUTE_WINDOW_BEGIN side=0 outages=2'] for s, l in base.items()}
        self.assertEqual(self.issues(began, route='2', outages=1), ['route outages applied 1 of 2'])
        self.assertEqual(self.issues(began, route='2', outages=2), [])

    def test_outage_error_reaches_the_verdict(self):
        lines = {s: [case(i, 'core', 'c', '1'), 'DSB_ROUTE_WINDOW_BEGIN side=0 outages=1'] for i, s in enumerate('AB')}
        self.assertIn('rules not installed', self.issues(lines, route='1', error='rules not installed'))


if __name__ == '__main__':
    unittest.main()
