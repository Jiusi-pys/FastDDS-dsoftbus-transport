'use strict';
// Every value is a fabricated HOST fixture. No device, SDK or permission IO.
const assert = require('node:assert/strict');
const {validateLifecycleRunBinding} = require('../scripts/LifecycleRunBinding.cjs');
const {validateRecordRunBinding} = require('../scripts/RecordRunBinding.cjs');
const {lifecycleBindingFixture, recordBindingFixture} = require('./run_binding_fixtures.cjs');
const tests = [];
const test = (name, body) => tests.push([name, body]);
const validate = f => validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs);
function reject(name, mutate, profile, board) {
  test(name, () => {
    const f = lifecycleBindingFixture(profile, board), key = Object.keys(f.expected.boards)[0];
    mutate(f, f.evidence.boards[key], f.expected.boards[key]);
    const result = validate(f);
    assert.equal(result.ok, false, JSON.stringify(result));
    assert(result.errors.length > 0); assert.equal('binding' in result, false);
    assert(Object.isFrozen(result)); assert(Object.isFrozen(result.errors));
  });
}
for (const [profile, selected] of [['lifecycle-alias-v1', 'A'], ['lifecycle-alias-v1', 'B'], ['lifecycle-callback-close-v1', undefined]]) {
  test(`${profile} ${selected || 'pair'} exact detached frozen output`, () => {
    const f = lifecycleBindingFixture(profile, selected), r = validate(f);
    assert.equal(r.ok, true, JSON.stringify(r));
    assert.equal(r.binding.profile, profile); assert.equal(r.binding.runId, f.expected.runId);
    assert.equal(r.binding.startedAt, f.expected.startedAt);
    assert.equal(r.binding.validatedAt, new Date(f.nowMs).toISOString());
    assert.equal(r.binding.expiresAt, '2026-09-14T04:32:00.000Z');
    assert.deepEqual(r.binding.activeBoards, selected ? [selected] : ['A', 'B']);
    assert.deepEqual(r.binding.budgets, {runMs: 7000, closeMs: 3000, sendMs: 1000,
      holdMs: 500, quietMs: 200, externalWatchdogMs: 10000});
    for (const key of r.binding.activeBoards) {
      const out = r.binding.boards[key], e = f.expected.boards[key];
      assert.equal(out.role, e.role); assert.equal(out.target, e.target); assert.equal(out.token, e.token);
      assert.deepEqual(out.probe, e.probe); assert.deepEqual(out.provider, e.provider);
      assert.deepEqual(out.loader, e.loader); assert.deepEqual(out.launchArgv, e.launchArgv);
      assert.deepEqual(out.rawCaptures, e.rawCaptures); assert.equal('record' in out, false);
    }
    const checkFrozen = x => { if (x && typeof x === 'object') { assert(Object.isFrozen(x)); Object.values(x).forEach(checkFrozen); } };
    checkFrozen(r);
    const key = r.binding.activeBoards[0];
    f.expected.boards[key].provider.identity.inode = 999;
    f.evidence.boards[key].launchArgv[0] = '/changed';
    assert.notEqual(r.binding.boards[key].provider.identity.inode, 999);
    assert.notEqual(r.binding.boards[key].launchArgv[0], '/changed');
    assert.equal(validateRecordRunBinding(f.evidence, f.expected, f.nowMs).ok, false);
  });
}
test('v1 evidence is never a LC launch even with matching expected', () => {
  const f = recordBindingFixture(); assert.equal(validate(f).ok, false);
});
test('inclusive freshness boundary without claiming a future runner reserve', () => {
  const f = lifecycleBindingFixture(); f.nowMs = Date.parse(f.expected.startedAt) + 120000;
  assert.equal(validate(f).ok, true); ++f.nowMs; assert.equal(validate(f).ok, false);
});
test('LC runId exact64 boundary is accepted unchanged', () => {
  const initial = lifecycleBindingFixture(), id = '20260914T043000000Z-' + 'x'.repeat(44);
  assert.equal(id.length, 64);
  const f = JSON.parse(JSON.stringify(initial).split(initial.expected.runId).join(id));
  assert.equal(validate(f).ok, true);
});
test('valid hash case is normalized in every detached artifact', () => {
  const f = lifecycleBindingFixture();
  for (const e of Object.values(f.expected.boards)) {
    e.probe.sha256 = e.probe.localSha256 = 'C'.repeat(64);
    e.probe.buildManifest.sha256 = 'D'.repeat(64); e.provider.sha256 = 'E'.repeat(64);
    for (const v of Object.values(e.rawCaptures)) v.sha256 = 'F'.repeat(64);
  }
  const r = validate(f); assert.equal(r.ok, true);
  assert.equal(r.binding.boards.A.probe.sha256, 'c'.repeat(64));
  assert.equal(r.binding.boards.A.probe.buildManifest.sha256, 'd'.repeat(64));
  assert.equal(r.binding.boards.A.provider.sha256, 'e'.repeat(64));
  assert.equal(r.binding.boards.A.rawCaptures.manager.sha256, 'f'.repeat(64));
});
test('independently pinned provider resolution need not equal requested path', () => {
  const f = lifecycleBindingFixture();
  for (const side of [f.expected, f.evidence]) {
    side.boards.A.provider.resolvedPath = '/system/lib64/chipset-sdk/libsoftbus_client.z.so';
    side.boards.A.provider.identity.kind = 'symlink'; side.boards.A.provider.identity.isSymlink = true;
  }
  assert.equal(validate(f).ok, true);
});
test('jointly fabricated expected and evidence can pass consistency, not authentication', () => {
  const f = lifecycleBindingFixture();
  for (const side of [f.expected, f.evidence]) side.boards.A.provider.sha256 = '9'.repeat(64);
  assert.equal(validate(f).ok, true); // Demonstrates the explicitly documented trust limit.
});
test('historical origin and ownership never acquire an invented age limit', () => {
  const f = lifecycleBindingFixture();
  for (const a of Object.values(f.evidence.boards)) for (const key of ['origin', 'ownership', 'freshBefore'])
    a.snapshots[key].capturedAt = '2020-01-01T00:00:00.000Z';
  assert.equal(validate(f).ok, true);
});
for (const profile of ['record-v1', 'lifecycle-alias-v2', 'lifecycle-reuse-v1', '', null])
  reject(`closed unsupported profile ${profile}`, f => { f.expected.profile = f.evidence.profile = profile; });
reject('profile disagreement', f => { f.evidence.profile = 'lifecycle-alias-v1'; });
reject('schema1 cannot enter LC', f => { f.evidence.schemaVersion = 1; });
reject('native scope required', f => { f.evidence.scope = 'HOST_FIXTURES_ONLY'; });
reject('matching fake scope is not native', f => { f.expected.scope = f.evidence.scope = 'HOST_FIXTURES_ONLY'; });
reject('alias missing selected board', f => { delete f.expected.selectedBoard; }, 'lifecycle-alias-v1', 'A');
reject('alias selection mismatch', f => { f.evidence.selectedBoard = 'B'; }, 'lifecycle-alias-v1', 'A');
reject('alias zero boards', f => { f.expected.boards = f.evidence.boards = {}; }, 'lifecycle-alias-v1', 'A');
reject('alias two boards', f => {
  const other = lifecycleBindingFixture(); f.expected.boards.B = other.expected.boards.B; f.evidence.boards.B = other.evidence.boards.B;
}, 'lifecycle-alias-v1', 'A');
reject('pair missing B', f => { delete f.evidence.boards.B; });
reject('pair cannot carry selectedBoard', f => { f.expected.selectedBoard = 'A'; });
reject('pair extra C', f => { f.evidence.boards.C = f.evidence.boards.A; });
reject('swapped role', (_f, a, e) => { a.role = e.role = 'client'; });
reject('runId65', f => { f.evidence.runId = f.expected.runId = '20260914T043000000Z-' + 'x'.repeat(45); });
reject('runId timestamp mismatch', f => { f.expected.runId = f.evidence.runId = '20260914T043001000Z-fixture'; });
reject('old run', f => { f.evidence.runId += '-old'; });
reject('future current time', f => { f.nowMs = Date.parse(f.expected.startedAt) - 1; });
for (const age of [0, -1, 120001, Infinity, NaN, '120000']) reject(`bad maxAge ${age}`, f => { f.expected.maxAgeMs = age; });
for (const key of ['customArgv', 'skipGuard', 'allowSingle', 'acceptAnyHash', 'policy']) {
  reject(`no caller policy ${key}`, f => { f.expected[key] = true; });
  reject(`no evidence policy ${key}`, f => { f.evidence[key] = true; });
}
for (const key of ['target', 'token', 'board']) reject(`fixed ${key}`, (_f, a) => { a[key] = 'wrong'; });
for (const key of ['remoteExit', 'hdcExit']) reject(`manager ${key} fails`, (_f, a) => { a.manager[key] = 1; });
for (const value of [undefined, 0, 2, '1']) reject(`manager count ${value}`, (_f, a) => { a.manager.remoteExitCount = value; });
reject('manager missing exit', (_f, a) => { delete a.manager.remoteExit; });
reject('manager alive', (_f, a) => { a.manager.pidAudit.gone = false; });
reject('manager different audited PID', (_f, a) => { ++a.manager.pidAudit.pid; });
reject('audit before manager exit', (_f, a) => { a.manager.pidAudit.checkedAt = a.manager.startedAt; });
reject('unrecognized identity', (_f, a) => { a.manager.identity = {Passed: true}; });
reject('wrong native token', (_f, a) => { a.manager.identity.lookupToken = 1; });
reject('broadened native identity', (_f, a) => { a.manager.identity.apl = 'system_core'; });
reject('denied permission', (_f, a) => { a.manager.identity.permissions[0].state = 'DENIED'; });
reject('missing permission', (_f, a) => { a.manager.identity.permissions = []; });
for (const key of ['Ownership', 'Origin', 'FreshTransition']) for (const type of ['missing', 'failed', 'phase'])
  reject(`guard ${key} ${type}`, (_f, a) => {
    if (type === 'missing') delete a.guard.result[key];
    else if (type === 'failed') a.guard.result[key].Code = 'FAIL';
    else a.guard.result[key].Phase = 'Revoked';
  });
reject('guard hash mismatch', (_f, a) => { a.guard.result.CurrentSha256 = 'e'.repeat(64); });
reject('guard old run', (_f, a) => { a.guard.runId += '-old'; });
reject('guard exit failure', (_f, a) => { a.guard.exitCode = 1; });
reject('snapshot before audit', (_f, a) => { a.snapshots.current.capturedAt = a.manager.startedAt; });
reject('remote snapshot hash mismatch', (_f, a) => { a.snapshots.current.remoteSha256 = 'e'.repeat(64); });
for (const key of ['origin', 'ownership', 'freshBefore']) reject(`snapshot ${key} absent`, (_f, a) => { delete a.snapshots[key]; });
for (const [key, value] of [['mode', '644'], ['uid', 1], ['gid', 0], ['isSymlink', true], ['inode', 15], ['selinux', 'wrong']])
  reject(`token metadata ${key}`, (_f, a) => { a.metadata.value[key] = value; });
reject('self route', (_f, a, e) => { e.peerNetworkId = e.localNetworkId; a.manager.peerNetworkIds = [e.peerNetworkId]; });
reject('unobserved route', (_f, a) => { a.manager.peerNetworkIds = []; });
reject('zero route is never a dummy peer', (_f, a, e) => { e.peerNetworkId = '0'.repeat(64); a.manager.peerNetworkIds = [e.peerNetworkId]; }, 'lifecycle-alias-v1', 'A');
reject('unrelated pair', f => { f.expected.boards.B.localNetworkId = f.evidence.boards.B.manager.localNetworkId = 'e'.repeat(64); });
for (const mutate of [argv => argv.push('--host-fixture', 'normal'), argv => argv.push('--single'),
  argv => argv.push('--run-id', 'other'), argv => argv.push('--extra'), argv => argv.splice(1, 1),
  argv => { delete argv[2]; }, argv => { [argv[1], argv[2]] = [argv[2], argv[1]]; },
  argv => { argv[2] = 'alias'; }, argv => { argv[7] = 'name;echo hacked'; }])
  reject(`noncanonical argv ${String(mutate)}`, (_f, a, e) => { mutate(a.launchArgv); mutate(e.launchArgv); });
reject('argv shell string', (_f, a) => { a.launchArgv = a.launchArgv.join(' '); });
for (const path of ['/data/local/tmp/../softbus_lifecycle_probe', '/data/local/tmp/old_A/softbus_lifecycle_probe',
  '/data/local/tmp/run/softbus_record_probe']) reject(`bad probe path ${path}`, (_f, a, e) => { a.probe.path = e.probe.path = path; });
reject('D013 is never LC ELF', (_f, a, e) => { a.probe.sha256 = e.probe.sha256 = e.manager.sha256; });
reject('probe mismatch hash', (_f, a) => { a.probe.sha256 = '9'.repeat(64); });
reject('probe remote hash mismatch', (_f, a) => { a.probe.remoteSha256 = '9'.repeat(64); });
reject('probe local hash mismatch', (_f, a, e) => { a.probe.localSha256 = e.probe.localSha256 = '9'.repeat(64); });
reject('probe local path mismatch', (_f, a) => { a.probe.localPath += '-old'; });
for (const field of ['probe', 'provider']) for (const side of ['expected', 'evidence'])
  reject(`${side} ${field} cannot carry a policy hook`, f => { f[side].boards.A[field].skipGuard = true; });
reject('build manifest cannot carry callback', (_f, a) => { a.probe.buildManifest.policy = () => true; });
reject('missing build manifest', (_f, a) => { delete a.probe.buildManifest; });
reject('build manifest hash mismatch', (_f, a) => { a.probe.buildManifest.sha256 = '9'.repeat(64); });
reject('provider path cannot be customized jointly', (_f, a, e) => { a.provider.path = e.provider.path = '/tmp/other.so'; });
reject('provider mismatched hash', (_f, a) => { a.provider.sha256 = '9'.repeat(64); });
reject('provider mismatched resolved path', (_f, a) => { a.provider.resolvedPath = '/system/lib64/other.so'; });
reject('provider resolved traversal', (_f, a, e) => { a.provider.resolvedPath = e.provider.resolvedPath = '/system/lib64/../other.so'; });
for (const key of ['inode', 'size', 'uid', 'gid', 'mtimeSeconds', 'ctimeSeconds'])
  reject(`provider identity mismatch ${key}`, (_f, a) => { ++a.provider.identity[key]; });
reject('provider nonfile identity', (_f, a, e) => { a.provider.identity.kind = e.provider.identity.kind = 'directory'; });
reject('provider fresh time missing', (_f, a) => { delete a.provider.checkedAt; });
reject('loader path alteration', (_f, a, e) => { a.loader.LD_LIBRARY_PATH = e.loader.LD_LIBRARY_PATH = '/tmp'; });
reject('preload never authorized by matching DTOs', (_f, a, e) => { a.loader.LD_PRELOAD = e.loader.LD_PRELOAD = '/tmp/hook.so'; });
for (const key of ['manager', 'pidAudit', 'metadata', 'probe', 'provider']) {
  reject(`raw ${key} missing`, (_f, a) => { delete a.rawCaptures[key]; });
  reject(`raw ${key} hash mismatch`, (_f, a) => { a.rawCaptures[key].sha256 = '9'.repeat(64); });
  reject(`raw ${key} run mismatch`, (_f, a) => { a.rawCaptures[key].runId += '-old'; });
  reject(`raw ${key} time before observation`, (_f, a) => { a.rawCaptures[key].checkedAt = '2026-09-14T04:30:00.000Z'; });
  reject(`raw ${key} extra policy`, (_f, a) => { a.rawCaptures[key].skipGuard = true; });
}
reject('unknown raw capture hook', (_f, a) => { a.rawCaptures.extra = a.rawCaptures.manager; });
for (const key of ['probe', 'provider']) for (const stamp of ['2026-09-14T04:29:59.000Z', '2026-09-14T04:30:31.000Z', 'not-a-time'])
  reject(`${key} invalid time ${stamp}`, (_f, a) => { a[key].checkedAt = stamp; });
for (const value of [null, undefined, [], 1, 'x']) test(`malformed evidence ${value}`, () => {
  const f = lifecycleBindingFixture(); assert.equal(validateLifecycleRunBinding(value, f.expected, f.nowMs).ok, false);
});
test('pure core needs no IO, subprocess, ambient clock or permission API', () => {
  const fs = require('node:fs'), vm = require('node:vm');
  const code = fs.readFileSync(require.resolve('../scripts/RunBindingCore.cjs'), 'utf8');
  class ExplicitDate extends Date {
    constructor(...args) { assert(args.length > 0, 'ambient Date read'); super(...args); }
    static now() { throw new Error('ambient clock read'); }
  }
  const sandbox = {module: {exports: {}}, Date: ExplicitDate,
    require() { throw new Error('IO/import forbidden in pure core'); },
    process: new Proxy({}, {get() { throw new Error('process access forbidden'); }})};
  vm.runInNewContext(code, sandbox);
  const f = lifecycleBindingFixture();
  assert.equal(sandbox.module.exports.validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs).ok, true);
  const r = recordBindingFixture();
  assert.equal(sandbox.module.exports.validateRecordRunBinding(r.evidence, r.expected, r.nowMs).ok, true);
});

let failed = 0;
for (const [name, body] of tests) try { body(); console.log(`PASS ${name}`); }
catch (error) { ++failed; console.error(`FAIL ${name}: ${error.stack}`); }
console.log(`LIFECYCLE_RUN_BINDING tests=${tests.length} passed=${tests.length - failed} failed=${failed} scope=HOST_FIXTURES_ONLY`);
process.exitCode = failed ? 1 : 0;
