'use strict';
const assert = require('node:assert/strict');
const api = require('../scripts/RecordRunBinding.cjs');
const {recordBindingFixture} = require('./run_binding_fixtures.cjs');
const tests = [];
const test = (name, body) => tests.push([name, body]);
const validate = f => api.validateRecordRunBinding(f.evidence, f.expected, f.nowMs);

test('original export, name and arity', () => {
  assert.deepEqual(Object.keys(api), ['validateRecordRunBinding']);
  assert.equal(api.validateRecordRunBinding.name, 'validateRecordRunBinding');
  assert.equal(api.validateRecordRunBinding.length, 3);
});
test('exact successful DTO and key order without provider fields', () => {
  const f = recordBindingFixture(), result = validate(f), boards = {};
  for (const b of ['A', 'B']) {
    const e = f.expected.boards[b];
    boards[b] = {target: e.target, token: e.token, record: {...e.record}, launchArgv: [...e.launchArgv]};
  }
  const expected = {ok: true, errors: [], binding: {runId: f.expected.runId,
    validatedAt: new Date(f.nowMs).toISOString(), boards}};
  assert.equal(JSON.stringify(result), JSON.stringify(expected));
  assert.deepEqual(result, expected);
});
test('v1 run suffix 80 remains accepted and age limit remains inclusive', () => {
  const f = recordBindingFixture(), id = '20260914T043000000Z-' + 'x'.repeat(80);
  f.expected.runId = f.evidence.runId = id;
  for (const a of Object.values(f.evidence.boards)) a.guard.runId = id;
  f.nowMs = Date.parse(f.expected.startedAt) + 120000;
  assert.equal(validate(f).ok, true);
  ++f.nowMs;
  assert.deepEqual(validate(f), {ok: false, errors: ['time.runFreshness']});
});
test('empty headers retain exact ordered errors and early return', () => {
  assert.deepEqual(api.validateRecordRunBinding({}, {}, 0), {ok: false, errors: [
    'time.now/start', 'expected.maxAgeMs', 'time.runFreshness', 'expected.runId',
    'run.binding', 'expected.boards', 'evidence.boards']});
});
test('per-board error ordering and pair error remain stable', () => {
  const f = recordBindingFixture();
  f.evidence.boards.A.token = 0;
  f.evidence.boards.A.record.sha256 = '0'.repeat(64);
  f.evidence.boards.A.manager.remoteExit = 1;
  f.evidence.boards.A.guard.result.Ownership.Code = 'FAIL';
  f.evidence.boards.B.token = 0;
  f.expected.boards.B.localNetworkId = f.evidence.boards.B.manager.localNetworkId = 'e'.repeat(64);
  assert.deepEqual(validate(f), {ok: false, errors: ['A.identityBinding', 'A.record', 'A.manager.exit',
    'A.guard.Ownership', 'B.identityBinding', 'pair.routeCrossCheck']});
});
test('frozen manager mismatch keeps specific original error', () => {
  const f = recordBindingFixture();
  f.evidence.boards.A.manager.sha256 = f.expected.boards.A.manager.sha256 = 'e'.repeat(64);
  assert.deepEqual(validate(f), {ok: false, errors: ['A.expected.frozenManager']});
});
test('historic snapshot time does not gain a freshness check', () => {
  const f = recordBindingFixture();
  for (const a of Object.values(f.evidence.boards)) for (const key of ['origin', 'ownership', 'freshBefore'])
    a.snapshots[key].capturedAt = '2020-01-01T00:00:00.000Z';
  assert.equal(validate(f).ok, true);
});
test('normal v1 unknown inert fields remain ignored', () => {
  const f = recordBindingFixture(); f.expected.note = f.evidence.note = {historical: true};
  assert.equal(validate(f).ok, true);
});
test('getter error remains malformedInput after preceding errors', () => {
  const f = recordBindingFixture(); f.evidence.boards.A.token = 0;
  Object.defineProperty(f.evidence.boards.A.record, 'path', {get() { throw new Error('fixture'); }});
  assert.deepEqual(validate(f), {ok: false, errors: ['A.identityBinding', 'malformedInput']});
});
test('detached recursively frozen results and lowercase hashes', () => {
  const f = recordBindingFixture(); f.expected.boards.A.record.sha256 = 'C'.repeat(64);
  const r = validate(f);
  const frozen = x => { if (x && typeof x === 'object') { assert(Object.isFrozen(x)); Object.values(x).forEach(frozen); } };
  frozen(r); assert.equal(r.binding.boards.A.record.sha256, 'c'.repeat(64));
  f.expected.boards.A.record.path = '/changed'; f.evidence.boards.A.launchArgv[0] = '/changed';
  assert.notEqual(r.binding.boards.A.record.path, '/changed');
  assert.notEqual(r.binding.boards.A.launchArgv[0], '/changed');
});

// Main-approved deliberate narrowing, separate from the compatibility cases.
for (const side of ['expected', 'evidence']) for (const profile of ['lifecycle-alias-v1', 'lifecycle-callback-close-v1', 'lifecycle-unknown']) {
  test(`deliberate narrowing: v1 refuses ${side} ${profile}`, () => {
    const f = recordBindingFixture(); f[side].profile = profile;
    assert.deepEqual(validate(f), {ok: false, errors: ['run.binding']});
  });
}

let failed = 0;
for (const [name, body] of tests) try { body(); console.log(`PASS ${name}`); }
catch (error) { ++failed; console.error(`FAIL ${name}: ${error.stack}`); }
console.log(`RECORD_V1_COMPATIBILITY tests=${tests.length} passed=${tests.length - failed} failed=${failed} scope=HOST_FIXTURES_ONLY`);
process.exitCode = failed ? 1 : 0;
