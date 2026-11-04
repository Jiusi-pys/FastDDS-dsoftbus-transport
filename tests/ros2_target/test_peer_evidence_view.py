"""Bounded host regression for the real expired run; never dispatches a device."""
import argparse
import ast
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import stat
from types import SimpleNamespace
import unittest
from unittest import mock

import peer_evidence_view as view

ARGS = None


class PeerEvidenceViewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = view.PeerEvidenceView.path(ARGS.work, exists=False)
        cls.work.mkdir(parents=True, exist_ok=False)
        cls.tool = view.PeerEvidenceView(ARGS.binding)
        cls.tool.require(cls.tool.run == '64e76c8f2f9b75fc', 'This regression uses only the explicitly expired historical run')
        cls.output = cls.work / 'canonical'
        cls.tool.convert(cls.output)

    def test_real_lossless_map_and_all_pairs(self):
        tool = self.tool
        self.assertEqual(len(tool.raw), 181)
        self.assertEqual(len(tool.audit['pairs']), 40)
        self.assertTrue(tool.audit['old_run_offline_only'])
        rows = tool.artifacts()['MAP.json']['files']
        moved = [r for r in rows if r['source_relative'] != r['view_relative']]
        self.assertEqual(len(moved), 2)
        self.assertEqual(sum(r['view_relative'].endswith('.ATTEMPT.json') for r in rows), 40)
        self.assertEqual(tool.validate(self.output)['device_calls'], 0)

    def test_private_synthetic_binding_count_gate(self):
        # Isolate the actual production count assignment and final-result check.
        # Synthetic181 dictionaries stay in memory, never become query receipts.
        syntax = ast.parse(Path(view.__file__).read_text(encoding='utf-8'))
        cls = next(n for n in syntax.body if isinstance(n, ast.ClassDef) and n.name == 'PeerEvidenceView')
        method = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == 'validate_outer')
        assignment = next(n for n in method.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'binding_field' for t in n.targets))
        check = next(n for n in method.body if isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and any(isinstance(a, ast.Constant) and a.value == 'Original final query evidence not PASS' for a in n.value.args))
        gate = compile(ast.Module(body=[assignment, check], type_ignores=[]), str(view.__file__), 'exec')
        actual = self.tool.load(self.tool.outer / 'PLAN.json')
        self.assertEqual(len(actual['bindings']), 141)
        exec(gate, {'self': self.tool, 'outer': actual, 'final': self.tool.final_query})
        synthetic = {'bindings': {'PRIVATE_SYNTHETIC_KEY_' + str(i): None for i in range(181)}}
        final = {k: v for k, v in self.tool.final_query.items() if not k.startswith('all_')}
        exec(gate, {'self': self.tool, 'outer': synthetic, 'final': {**final, 'all_181_plan_bindings': 'PASS'}})
        for label, changes in [('missing', {}), ('wrong_count', {'all_141_plan_bindings': 'PASS'}), ('failed', {'all_181_plan_bindings': 'FAIL'})]:
            with self.subTest(case=label):
                with self.assertRaisesRegex(ValueError, 'Original final query evidence not PASS'):
                    exec(gate, {'self': self.tool, 'outer': synthetic, 'final': {**final, **changes}})

    def test_required_protocol_negatives(self):
        stem = 'execution-A/query_exit_observation_0'
        def edit(c, name, fn):
            data = c.data(name);fn(data);c.raw[name] = (json.dumps(data) + '\n').encode()
        cases = {
            'orphan': lambda c: c.raw.pop(stem + '.json'),
            'missing_source': lambda c: c.raw.pop('private/B-query.stdout'),
            'first_failure': lambda c: c.raw.update({'execution-A/.FIRST_FAILURE.json': b'{}'}),
            'extra_observation': lambda c: c.raw.update({'execution-A/query_exit_observation_1.json': b'{}'}),
            'malformed_observation': lambda c: c.raw.update({'execution-A/query_exit_observation_00.json': c.raw.pop(stem + '.json')}),
            'argv': lambda c: edit(c, stem + '.ATTEMPT.json', lambda r: r['argv'].append('unexpected')),
            'timestamp': lambda c: edit(c, stem + '.json', lambda r: r.update(started_utc='2000-01-01T00:00:00+00:00')),
            'host_rc': lambda c: edit(c, stem + '.json', lambda r: r.update(exit_code=1)),
            'timeout': lambda c: edit(c, stem + '.json', lambda r: r.update(timed_out=True)),
            'raw_hash': lambda c: edit(c, stem + '.json', lambda r: r.update(stdout_sha256='0' * 64)),
            'raw_bytes': lambda c: c.raw.update({stem + '.stdout.bin': b'changed'}),
            'query_exit': lambda c: c.raw.update({'private/A-query.exit': b'1\n'}),
            'private_identity_hash': lambda c: c.raw.update({'private/A-local-udid.bin': b'x' * 64}),
        }
        for name, mutation in cases.items():
            with self.subTest(name=name):
                candidate = copy.copy(self.tool);candidate.raw = copy.deepcopy(self.tool.raw)
                mutation(candidate)
                with self.assertRaises((ValueError, KeyError)):
                    candidate.audit_source()
        candidate = copy.copy(self.tool);candidate.raw = copy.deepcopy(self.tool.raw)
        pending = candidate.raw[stem + '.stdout.bin'].replace(b'REMOTE_EXIT=0', b'REMOTE_EXIT=1')
        candidate.raw[stem + '.stdout.bin'] = pending
        edit(candidate, stem + '.json', lambda r: r.update(stdout_sha256=hashlib.sha256(pending).hexdigest()))
        with self.assertRaisesRegex(ValueError, 'pending unsupported'):
            candidate.audit_source()

    def test_view_map_extra_file_and_byte_negatives(self):
        for case in ('mapping_omission', 'extra_file', 'changed_byte'):
            with self.subTest(case=case):
                output = self.work / case;shutil.copytree(self.output, output)
                if case == 'mapping_omission':
                    data = self.tool.load(output / 'MAP.json');data['files'].pop()
                    (output / 'MAP.json').write_text(json.dumps(data))
                    result = self.tool.load(output / 'RESULT.json')
                    result['reports']['MAP.json'] = self.tool.sha(output / 'MAP.json')
                    (output / 'RESULT.json').write_text(json.dumps(result))
                elif case == 'extra_file':
                    (output / 'view/hidden-extra').write_bytes(b'extra')
                else:
                    (output / 'view/private/A-query.stdout').write_bytes(b'changed')
                with self.assertRaises(ValueError):self.tool.validate(output)
        with self.assertRaises(ValueError):self.tool.convert(self.output)

    def test_binding_path_and_source_change_rejection(self):
        binding = copy.deepcopy(self.tool.binding)
        binding['source_seal']['sha256'] = '0' * 64
        path = self.work / 'private-bad-binding.json';path.write_text(json.dumps(binding))
        with self.assertRaises(ValueError):view.PeerEvidenceView(path)
        with self.assertRaises(ValueError):self.tool.convert(self.tool.root.parent / 'outside-peer-view-owned-root')
        with mock.patch('pathlib.Path.lstat', return_value=SimpleNamespace(st_mode=stat.S_IFDIR, st_file_attributes=0x400)):
            with self.assertRaisesRegex(ValueError, 'Symlink/reparse'):self.tool.path(self.tool.source)
        changed = dict(self.tool.raw);changed['RESULT.json'] += b' '
        with mock.patch.object(self.tool, 'inventory', return_value=changed):
            with self.assertRaisesRegex(ValueError, 'Source changed'):self.tool.recheck()

    def test_unselected_ordinary_consumer_rejected(self):
        cli_file = Path(__file__).with_name('run_actionbag_cli.py').resolve()
        original_sha = view.PeerEvidenceView.sha
        for unselected in ('3d348d26157311d5fd5d1f05248d4454ca238296ae8f0154936fac061a03c40e', '0' * 64):
            with self.subTest(unselected=unselected), mock.patch.object(
                    view.PeerEvidenceView, 'sha', side_effect=lambda p: unselected if Path(p).resolve() == cli_file else original_sha(p)):
                with self.assertRaisesRegex(ValueError, 'Frozen ordinary consumer changed'):
                    view.PeerEvidenceView(ARGS.binding)

    def test_frozen_routes_and_expired_prepare_without_processes(self):
        # Read exact existing consumers; any subprocess would fail this test.
        cli_file = Path(__file__).with_name('run_actionbag_cli.py')
        self.assertEqual(self.tool.sha(cli_file), view.FROZEN[cli_file.name])
        spec = importlib.util.spec_from_file_location('peer_view_offline_cli', cli_file)
        cli = importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)
        routes = self.work / 'offline-old-time-routes.json'
        cli.NATIVE.actionbag_peer_receipt(argparse.Namespace(peer_evidence=self.output / 'view', output=routes))
        route = self.tool.load(routes)
        self.assertEqual(route['observed_utc'], '2026-10-17T12:38:56.918632+00:00')
        self.assertTrue(route['cross_confirmed'])
        self.assertEqual(route['device_calls_by_adapter'], 0)
        for side in 'AB':
            self.assertEqual(route['sides'][side]['stdout_sha256'], self.tool.files['private/' + side + '-query.stdout']['sha256'])
        checklist_file = self.tool.root / 'ordinary-providerfix2-imports-preparation/INPUT_CHECKLIST.json'
        self.assertEqual(self.tool.sha(checklist_file), 'ae6cf35990dea89910059af1bef6af26277ff46a9093117fe932f9b6c5537f9c')
        checklist = self.tool.load(checklist_file)
        ready = self.tool.root / 'main-ordinary-providerfix2-readiness/MAIN_READY.json'
        self.assertEqual(self.tool.sha(ready), 'a6eaa9f499bacf2c16f82d694db8f9a9a68d06a11c02a9198ab37e719adde6ee')
        selection = dict(checklist['non_routes_non_ready_descriptors'])
        selection.update(schema=1, case='imports_cdr_pair', package=checklist['package_directory'],
                         readiness={'path': str(ready), 'sha256': self.tool.sha(ready)},
                         routes={'path': str(routes), 'sha256': self.tool.sha(routes)})
        # Private synthetic metadata exercises the current consumer's expiry
        # gate. Historical source qualifications and receipts are not rewritten
        # or presented as qualification of this newer host consumer.
        originals = {name: copy.deepcopy(selection[name]) for name in ('ordinary_freeze', 'external', 'readiness')}
        values = {}
        for name, descriptor in originals.items():
            self.assertEqual(self.tool.sha(descriptor['path']), descriptor['sha256'])
            values[name] = self.tool.load(Path(descriptor['path']))
        fixture = self.work / 'synthetic-current-consumer-inputs'
        fixture.mkdir()
        frozen = copy.deepcopy(values['ordinary_freeze'])
        for row in frozen['files']:
            source = cli.REPO / row['path']
            row.update(sha256=self.tool.sha(source), bytes=source.stat().st_size)
        self.assertEqual({k: v for k, v in frozen.items() if k != 'files'},
                         {k: v for k, v in values['ordinary_freeze'].items() if k != 'files'})
        self.assertEqual(frozen['source_sha256'], self.tool.sha(cli.PROBE))
        self.assertEqual([r for r in frozen['files'] if r['path'].endswith('actionbag_runtime_probe.py')],
                         [r for r in values['ordinary_freeze']['files'] if r['path'].endswith('actionbag_runtime_probe.py')])
        frozen_file = fixture / 'FREEZE.json';frozen_file.write_text(json.dumps(frozen, indent=2) + '\n')
        external = copy.deepcopy(values['external'])
        external['source_freeze_sha256'] = self.tool.sha(frozen_file)
        self.assertEqual({k: v for k, v in external.items() if k != 'source_freeze_sha256'},
                         {k: v for k, v in values['external'].items() if k != 'source_freeze_sha256'})
        external_file = fixture / 'EXTERNAL.json';external_file.write_text(json.dumps(external, indent=2) + '\n')
        readiness = copy.deepcopy(values['readiness'])
        readiness.update(ordinary_source_freeze_sha256=self.tool.sha(frozen_file), external_closure_sha256=self.tool.sha(external_file))
        rebound = {'ordinary_source_freeze_sha256', 'external_closure_sha256'}
        self.assertEqual({k: v for k, v in readiness.items() if k not in rebound},
                         {k: v for k, v in values['readiness'].items() if k not in rebound})
        readiness_file = fixture / 'READINESS.json';readiness_file.write_text(json.dumps(readiness, indent=2) + '\n')
        for name, path in (('ordinary_freeze', frozen_file), ('external', external_file), ('readiness', readiness_file)):
            selection[name] = {'path': str(path), 'sha256': self.tool.sha(path)}
        (fixture / 'PROVENANCE.json').write_text(json.dumps({
            'status': 'SYNTHETIC_HOST_ONLY_NOT_DEPLOYABLE', 'originals': originals,
            'synthetic': {k: selection[k] for k in originals},
            'new_source_review_or_device_qualification_asserted': False,
            'purpose': 'Current consumer must still reject the unchanged expired route; subprocess and shell calls forbidden',
        }, indent=2) + '\n')
        selected = self.work / 'OFFLINE_ONLY_EXPIRED_SELECTION.json'
        selected.write_text(json.dumps(selection, indent=2) + '\n')
        incomplete = self.work / 'expired-prepare-retained'
        with self.assertRaisesRegex(ValueError, 'Fresh independently captured paired routes required') as error:
            cli.ordinary_prepare(argparse.Namespace(inputs=selected, output=incomplete))
        self.assertTrue((incomplete / 'PLAN.json').is_file())
        self.assertEqual(self.tool.load(routes), route)
        for descriptor in originals.values():
            self.assertEqual(self.tool.sha(descriptor['path']), descriptor['sha256'])
        (self.work / 'EXPIRED_PREPARE_REJECTION.json').write_text(json.dumps({
            'status': 'EXPECTED_LOCAL_REJECTION_NO_HDC', 'exception': str(error.exception),
            'observed_utc_unchanged': route['observed_utc'], 'incomplete_directory_retained': str(incomplete),
            'target_eligible': False, 'device_calls': 0}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binding', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    ARGS = parser.parse_args()
    output = io.StringIO()
    with mock.patch('subprocess.Popen', side_effect=AssertionError('NO_PROCESS_OR_HDC_ALLOWED')) as process, \
            mock.patch('os.system', side_effect=AssertionError('NO_SHELL_ALLOWED')) as shell:
        result = unittest.TextTestRunner(stream=output, verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(PeerEvidenceViewTests))
        assert process.call_count == shell.call_count == 0
    report = {'status': 'PASS_HOST_OFFLINE_REGRESSION' if result.wasSuccessful() else 'FAIL',
              'tests': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
              'subprocess_calls': process.call_count, 'shell_calls': shell.call_count, 'device_calls': 0,
              'source_sha256': {p.name: view.PeerEvidenceView.sha(p) for p in (Path(view.__file__), Path(__file__))},
              'old_routes_target_eligible': False, 'log': output.getvalue()}
    (ARGS.work / 'TEST_RESULT.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0 if result.wasSuccessful() else 1)
