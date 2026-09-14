'use strict';

// Pure, fabricated HOST FIXTURES. Hash strings (except frozen manager identity)
// and times below are test data, not captures from a current board experiment.
const assert = require('node:assert/strict');
const {validateRecordRunBinding} = require('../scripts/RecordRunBinding.cjs');

function fixture() {
  const startedAt = '2026-09-14T04:30:00.000Z';
  const at = '2026-09-14T04:30:10.000Z';
  const managerAt = '2026-09-14T04:30:20.000Z';
  const auditAt = '2026-09-14T04:30:21.000Z';
  const snapshotAt = '2026-09-14T04:30:22.000Z';
  const guardAt = '2026-09-14T04:30:23.000Z';
  const expected = {runId: '20260914T043000000Z-fixture', startedAt, maxAgeMs: 120000, boards: {}};
  const evidence = {schemaVersion: 1, runId: expected.runId, startedAt, boards: {}};
  for (const [board, token, target, ownHex, peerHex] of [
    ['A', 671793191, '3e01ff55454d202020104033bf453b00', 'a', 'b'],
    ['B', 671320465, '3e01ff55454d202020104433991c3b00', 'b', 'a']
  ]) {
    const record = {path: `/data/local/tmp/${expected.runId}_${board}/softbus_record_probe`, sha256: 'c'.repeat(64)};
    const manager = {path: `/data/local/tmp/fastdds_dsoftbus_record_timing_20260914T014946Z_${board}/softbus_record_probe`,
      sha256: 'd01365fc20d70206a1ddeb32c5c278642125f5c1eb49f80754b2d1c8350a172b'};
    const origin = {path: `.artifacts/fixtures/${board}-origin.json`, sha256: '1'.repeat(64)};
    const ownership = {path: `.artifacts/fixtures/${board}-ownership.json`, sha256: '2'.repeat(64)};
    const freshBefore = {path: `.artifacts/fixtures/${expected.runId}/${board}-before.json`, sha256: '3'.repeat(64)};
    const currentPath = `.artifacts/fixtures/${expected.runId}/${board}-current.json`;
    const metadata = {kind: 'regular', isSymlink: false, mode: '640', uid: 0, gid: 3020,
      selinux: 'u:object_r:accesstoken_data_file:s0', size: 96628, inode: 14,
      mtimeSeconds: 1789350000, ctimeSeconds: 1789350000};
    const localNetworkId = ownHex.repeat(64), peerNetworkId = peerHex.repeat(64);
    const launchArgv = board === 'A' ? [record.path, '--server', '--local-name', 'com.kaihong.mdds.listen.a',
      '--expected-remote-initiator-name', 'com.kaihong.mdds.dial.b', '--expected-peer-network-id', peerNetworkId] :
      [record.path, '--client', '--local-name', 'com.kaihong.mdds.dial.b', '--remote-listen-name',
        'com.kaihong.mdds.listen.a', '--peer-network-id', peerNetworkId, '--expected-peer-network-id', peerNetworkId];
    launchArgv.push('--expected-token', String(token), '--preflight-confirmed');
    expected.boards[board] = {target, token, record, manager, origin, ownership, freshBefore,
      currentPath, metadata, localNetworkId, peerNetworkId, launchArgv};
    const passed = phase => ({Passed: true, Code: 'PASS', ProcessName: 'fastdds_dsoftbus_probe', TokenId: token, Phase: phase});
    const pid = board === 'A' ? 10001 : 10002;
    evidence.boards[board] = {board, target, token, record: {...record, checkedAt: at},
      manager: {...manager, startedAt: at, endedAt: managerAt, hdcExit: 0, remoteExit: 0, pid,
        argv: [manager.path, '--list-nodes', '--expected-token', String(token), '--preflight-confirmed'],
        pidAudit: {pid, gone: true, checkedAt: auditAt},
        identity: {lookupToken: token, tokenType: 'TOKEN_NATIVE', nativeInfoRc: 0,
          processName: 'fastdds_dsoftbus_probe', apl: 'normal', permissionRc: 0,
          permissions: [{name: 'ohos.permission.DISTRIBUTED_DATASYNC', state: 'GRANTED'}]},
        localNetworkId, peerNetworkIds: [peerNetworkId]},
      snapshots: {origin: {...origin}, ownership: {...ownership}, freshBefore: {...freshBefore},
        current: {path: currentPath, sha256: '4'.repeat(64),
          remotePath: '/data/service/el0/access_token/nativetoken.json', remoteSha256: '4'.repeat(64),
          capturedAt: snapshotAt, remoteReadAt: snapshotAt}},
      metadata: {checkedAt: snapshotAt, value: {...metadata}},
      guard: {runId: expected.runId, checkedAt: guardAt, exitCode: 0,
        originSha256: origin.sha256, ownershipSha256: ownership.sha256,
        result: {Passed: true, Board: board, Target: target, Token: token, Phase: 'Granted',
          Ownership: passed('Ownership'), Origin: passed('Granted'), FreshTransition: passed('Granted'),
          FreshBeforeSha256: freshBefore.sha256, CurrentSha256: '4'.repeat(64)}},
      launchArgv: [...launchArgv]};
  }
  return {expected, evidence, nowMs: Date.parse('2026-09-14T04:30:30.000Z')};
}

const tests = [];
function test(name, body) { tests.push([name, body]); }
function reject(name, mutate) {
  test(name, () => {
    const f = fixture();
    mutate(f, f.evidence.boards.A, f.expected.boards.A);
    const launched = [];
    const result = validateRecordRunBinding(f.evidence, f.expected, f.nowMs);
    // Caller composition oracle only. No real process/device is launched here.
    if (result.ok) launched.push(result.binding);
    assert.equal(result.ok, false, name);
    assert.equal(launched.length, 0, name);
    assert(Array.isArray(result.errors) && result.errors.length > 0, name);
    assert.equal('binding' in result, false, name);
  });
}

test('valid exact pair returns detached deeply frozen launch bindings', () => {
  const f = fixture();
  const result = validateRecordRunBinding(f.evidence, f.expected, f.nowMs);
  assert.equal(result.ok, true, JSON.stringify(result));
  assert.deepEqual(result.errors, []);
  assert.equal(result.binding.runId, f.expected.runId);
  assert.equal(result.binding.validatedAt, new Date(f.nowMs).toISOString());
  for (const board of ['A', 'B']) {
    assert.deepEqual(result.binding.boards[board].launchArgv, f.expected.boards[board].launchArgv);
    assert.equal(result.binding.boards[board].target, f.expected.boards[board].target);
    assert.deepEqual(result.binding.boards[board].record, f.expected.boards[board].record);
    assert(Object.isFrozen(result.binding.boards[board]));
    assert(Object.isFrozen(result.binding.boards[board].record));
    assert(Object.isFrozen(result.binding.boards[board].launchArgv));
  }
  assert(Object.isFrozen(result)); assert(Object.isFrozen(result.errors));
  assert(Object.isFrozen(result.binding)); assert(Object.isFrozen(result.binding.boards));
  f.evidence.boards.A.launchArgv[0] = '/changed'; f.expected.boards.A.launchArgv[0] = '/changed';
  assert.notEqual(result.binding.boards.A.launchArgv[0], '/changed');
  assert.throws(() => { result.binding.boards.A.launchArgv[0] = '/changed'; }, TypeError);
});
test('explicit freshness inclusive boundary and canonical case-insensitive SHA comparison', () => {
  const f = fixture(); f.nowMs = Date.parse(f.expected.startedAt) + f.expected.maxAgeMs;
  f.evidence.boards.A.record.sha256 = f.evidence.boards.A.record.sha256.toUpperCase();
  f.evidence.boards.A.guard.result.CurrentSha256 = f.evidence.boards.A.guard.result.CurrentSha256.toUpperCase();
  assert.equal(validateRecordRunBinding(f.evidence, f.expected, f.nowMs).ok, true);
});

for (const board of ['A', 'B']) {
  reject(`${board}: wrong board-token association`, f => { f.evidence.boards[board].token = board === 'A' ? 671320465 : 671793191; });
  reject(`${board}: absent manager`, f => { delete f.evidence.boards[board].manager; });
  reject(`${board}: wrong record hash`, f => { f.evidence.boards[board].record.sha256 = 'f'.repeat(64); });
  reject(`${board}: missing guard`, f => { delete f.evidence.boards[board].guard; });
}
reject('missing board B', f => { delete f.evidence.boards.B; });
reject('extra board is not accepted', f => { f.evidence.boards.C = f.evidence.boards.A; });
reject('wrong target', (_f, a) => { a.target = 'another-board'; });
reject('wrong board label', (_f, a) => { a.board = 'B'; });
reject('matching evidence and expected cannot redefine fixed board identity', (f, a, e) => { a.token = e.token = 123; });
reject('schema missing', f => { delete f.evidence.schemaVersion; });
reject('wrong run', f => { f.evidence.runId += '-older'; });
reject('wrong start binding', f => { f.evidence.startedAt = '2026-09-14T04:29:59.000Z'; });
reject('future now boundary', f => { f.nowMs = Date.parse(f.expected.startedAt) - 1; });
reject('stale run', f => { f.nowMs = Date.parse(f.expected.startedAt) + f.expected.maxAgeMs + 1; });
for (const age of [0, -1, 120001, Infinity, NaN, '120000', 0.5]) {
  reject(`invalid explicit maxAge ${String(age)}`, f => { f.expected.maxAgeMs = age; });
}
reject('timestamp without UTC milliseconds', (_f, a) => { a.guard.checkedAt = '2026-09-14T04:30:23Z'; });
reject('impossible timestamp', (_f, a) => { a.guard.checkedAt = '2026-02-30T04:30:23.000Z'; });
reject('future guard', (_f, a) => { a.guard.checkedAt = '2026-09-14T04:30:31.000Z'; });
reject('stale guard', (_f, a) => { a.guard.checkedAt = '2026-09-14T04:29:59.000Z'; });
reject('guard from old run', (_f, a) => { a.guard.runId = '20260913T043000000Z-fixture'; });
reject('record old path despite matching hash', (_f, a) => { a.record.path = a.manager.path; });
reject('expected must not use frozen old D013 as new binary', (_f, a, e) => { a.record.sha256 = e.record.sha256 = e.manager.sha256; });
reject('matching expected/evidence cannot replace frozen manager hash', (_f, a, e) => { a.manager.sha256 = e.manager.sha256 = 'e'.repeat(64); });
reject('manager wrong path', (_f, a) => { a.manager.path = '/data/local/tmp/other/softbus_record_probe'; });
reject('artifact check stale', (_f, a) => { a.record.checkedAt = '2026-09-14T04:29:59.000Z'; });
reject('invalid artifact hash syntax', (_f, a) => { a.record.sha256 = 'g'.repeat(64); });
reject('manager altered argv', (_f, a) => { a.manager.argv.push('--client'); });
reject('manager remote failure with HDC success', (_f, a) => { a.manager.remoteExit = 6; });
reject('manager HDC failure with remote success', (_f, a) => { a.manager.hdcExit = 1; });
reject('manager missing actual remote exit', (_f, a) => { delete a.manager.remoteExit; });
reject('manager string exit is not numeric exit', (_f, a) => { a.manager.remoteExit = '0'; });
reject('manager PID invalid', (_f, a) => { a.manager.pid = 0; });
reject('manager termination not proven', (_f, a) => { a.manager.pidAudit.gone = false; });
reject('manager termination proves different PID', (_f, a) => { a.manager.pidAudit.pid += 1; });
reject('manager process audit happened before exit', (_f, a) => { a.manager.pidAudit.checkedAt = a.manager.startedAt; });
reject('manager end before start', (_f, a) => { a.manager.endedAt = '2026-09-14T04:30:01.000Z'; });
for (const [field, wrong] of [['lookupToken', 1], ['tokenType', 'TOKEN_HAP'], ['nativeInfoRc', 1],
  ['processName', 'other'], ['apl', 'system_core'], ['permissionRc', 1]]) {
  reject(`manager identity mismatch ${field}`, (_f, a) => { a.manager.identity[field] = wrong; });
}
reject('manager permission denied', (_f, a) => { a.manager.identity.permissions[0].state = 'DENIED'; });
reject('manager wrong permission', (_f, a) => { a.manager.identity.permissions[0].name = 'ohos.permission.OTHER'; });
reject('manager empty permission result', (_f, a) => { a.manager.identity.permissions = []; });
reject('manager extra permission result', (_f, a) => { a.manager.identity.permissions.push(a.manager.identity.permissions[0]); });
reject('manager boolean verdict alone is insufficient', (_f, a) => { a.manager.identity = {Passed: true}; });
for (const key of ['origin', 'ownership', 'freshBefore']) {
  reject(`missing ${key} snapshot`, (_f, a) => { delete a.snapshots[key]; });
  reject(`mismatched ${key} snapshot hash`, (_f, a) => { a.snapshots[key].sha256 = 'f'.repeat(64); });
  reject(`mismatched ${key} snapshot path`, (_f, a) => { a.snapshots[key].path += '-other'; });
}
reject('current snapshot path mismatch', (_f, a) => { a.snapshots.current.path += '-other'; });
reject('current remote source changed', (_f, a) => { a.snapshots.current.remotePath = '/tmp/nativetoken.json'; });
reject('current local and remote hash disagree', (_f, a) => { a.snapshots.current.remoteSha256 = 'e'.repeat(64); });
reject('current captured before manager termination audit', (_f, a) => { a.snapshots.current.capturedAt = a.manager.startedAt; });
reject('current remote hash read after guard', (_f, a) => { a.snapshots.current.remoteReadAt = '2026-09-14T04:30:24.000Z'; });
reject('guard failed exit', (_f, a) => { a.guard.exitCode = 1; });
reject('guard result boolean alone is insufficient', (_f, a) => { a.guard.result = {Passed: true}; });
reject('guard top-level failed', (_f, a) => { a.guard.result.Passed = false; });
for (const [field, wrong] of [['Board', 'B'], ['Target', 'other'], ['Token', 1], ['Phase', 'Revoked'],
  ['CurrentSha256', 'f'.repeat(64)], ['FreshBeforeSha256', 'f'.repeat(64)]]) {
  reject(`guard composition mismatched ${field}`, (_f, a) => { a.guard.result[field] = wrong; });
}
for (const key of ['Ownership', 'Origin', 'FreshTransition']) {
  reject(`guard ${key} missing`, (_f, a) => { delete a.guard.result[key]; });
  reject(`guard ${key} failed`, (_f, a) => { a.guard.result[key].Code = 'FAIL'; });
  reject(`guard ${key} wrong token`, (_f, a) => { a.guard.result[key].TokenId = 1; });
  reject(`guard ${key} wrong process`, (_f, a) => { a.guard.result[key].ProcessName = 'other'; });
  reject(`guard ${key} wrong phase`, (_f, a) => { a.guard.result[key].Phase = 'Revoked'; });
}
reject('guard origin input not bound', (_f, a) => { a.guard.originSha256 = 'e'.repeat(64); });
reject('guard ownership input not bound', (_f, a) => { a.guard.ownershipSha256 = 'e'.repeat(64); });
for (const [field, wrong] of [['kind', 'directory'], ['isSymlink', true], ['mode', '644'], ['uid', 1],
  ['gid', 0], ['selinux', 'u:object_r:wrong:s0'], ['size', 96629], ['inode', 15],
  ['mtimeSeconds', 1789350001], ['ctimeSeconds', 1789350001]]) {
  reject(`metadata changed ${field}`, (_f, a) => { a.metadata.value[field] = wrong; });
}
reject('matching expected/evidence cannot waive required mode', (_f, a, e) => { a.metadata.value.mode = e.metadata.mode = '777'; });
reject('metadata checked after guard', (_f, a) => { a.metadata.checkedAt = '2026-09-14T04:30:24.000Z'; });
reject('route malformed', (_f, a) => { a.manager.localNetworkId = '0'.repeat(63); });
reject('route not observed in manager peers', (_f, a) => { a.manager.peerNetworkIds = []; });
reject('route self mismatch', (_f, a, e) => { a.manager.peerNetworkIds = [e.localNetworkId]; e.peerNetworkId = e.localNetworkId; });
reject('pair route cross-check rejects unrelated B local ID', f => {
  f.expected.boards.B.localNetworkId = 'e'.repeat(64); f.evidence.boards.B.manager.localNetworkId = 'e'.repeat(64);
});
reject('launch arguments drift', (_f, a) => { a.launchArgv.push('--extra'); });
reject('launch order drift', (_f, a) => { [a.launchArgv[1], a.launchArgv[2]] = [a.launchArgv[2], a.launchArgv[1]]; });
reject('matching expected/evidence must still launch selected new binary', (_f, a, e) => {
  a.launchArgv[0] = e.launchArgv[0] = e.manager.path;
});
reject('matching expected/evidence cannot change expected token argument', (_f, a, e) => {
  const index = a.launchArgv.indexOf('--expected-token') + 1;
  a.launchArgv[index] = e.launchArgv[index] = '671320465';
});
reject('matching expected/evidence cannot inject shell syntax', (_f, a, e) => {
  a.launchArgv[3] = e.launchArgv[3] = 'name;echo hacked';
});
reject('launch args must be arrays, not shell strings', (_f, a) => { a.launchArgv = a.launchArgv.join(' '); });
reject('sparse launch argv is missing evidence, not a match', (_f, a) => { delete a.launchArgv[3]; });
reject('sparse expected argv is not canonical', (_f, _a, e) => { delete e.launchArgv[3]; });
reject('expected path traversal not canonical', (_f, a, e) => {
  a.record.path = e.record.path = '/data/local/tmp/../softbus_record_probe';
});
for (const value of [null, undefined, [], 'x', 1]) {
  test(`malformed evidence ${String(value)} returns failure without throwing`, () => {
    const f = fixture(); const result = validateRecordRunBinding(value, f.expected, f.nowMs);
    assert.equal(result.ok, false); assert(result.errors.length > 0);
  });
}
test('malformed expected and time return failure without throwing', () => {
  const f = fixture();
  for (const e of [null, undefined, [], 1]) assert.equal(validateRecordRunBinding(f.evidence, e, f.nowMs).ok, false);
  for (const now of [undefined, NaN, Infinity, 'now', -1]) assert.equal(validateRecordRunBinding(f.evidence, f.expected, now).ok, false);
});

let failures = 0;
for (const [name, body] of tests) {
  try { body(); console.log(`PASS ${name}`); }
  catch (error) { failures++; console.error(`FAIL ${name}: ${error.stack}`); }
}
console.log(`RECORD_RUN_BINDING tests=${tests.length} passed=${tests.length - failures} failed=${failures} scope=HOST_FIXTURES_ONLY`);
process.exitCode = failures ? 1 : 0;
