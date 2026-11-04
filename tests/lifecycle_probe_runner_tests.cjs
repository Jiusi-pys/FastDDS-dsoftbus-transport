'use strict';
// Actual local fixture bytes plus injected process ports. Never launches HDC.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {runLifecycleProbe} = require('../scripts/LifecycleProbeRunner.cjs');
const {validateLifecycleRunBinding} = require('../scripts/LifecycleRunBinding.cjs');
const {lifecycleBindingFixture} = require('./run_binding_fixtures.cjs');
const {fakePorts, FakeClock} = require('./lifecycle_probe_harness_tests.cjs');
const root = path.resolve(__dirname, '..');
const stamp = `${process.pid}-${Date.now()}`;
let serial = 0;
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const absolute = file => path.join(root, ...file.split('/'));

function bundle() {
  const base = `.artifacts/tdd/lc-runner-fixtures/${stamp}/${++serial}`;
  const initial = lifecycleBindingFixture('lifecycle-alias-v1', 'A');
  const id = `20260914T043000000Z-runner-${process.pid}-${serial}`;
  const f = JSON.parse(JSON.stringify(initial).split(initial.expected.runId).join(id));
  const e = f.expected.boards.A, a = f.evidence.boards.A;
  const write = (name, data) => {
    const file = `${base}/${name}`; fs.mkdirSync(path.dirname(absolute(file)), {recursive: true});
    fs.writeFileSync(absolute(file), data, {flag: 'wx'}); return {path: file, sha256: hash(data)};
  };
  for (const key of ['origin', 'ownership', 'freshBefore']) {
    e[key] = write(`${key}.json`, Buffer.from(JSON.stringify({fixture: true, key})));
    a.snapshots[key] = {...e[key]};
  }
  const current = write('current.json', Buffer.from('{"fixture":"current"}'));
  e.currentPath = current.path; a.snapshots.current.path = current.path;
  a.snapshots.current.sha256 = a.snapshots.current.remoteSha256 = current.sha256;
  a.guard.originSha256 = e.origin.sha256; a.guard.ownershipSha256 = e.ownership.sha256;
  a.guard.result.CurrentSha256 = current.sha256; a.guard.result.FreshBeforeSha256 = e.freshBefore.sha256;
  for (const key of Object.keys(e.rawCaptures)) {
    e.rawCaptures[key] = write(`${key}.raw.log`, Buffer.from(`HOST FIXTURE ${key}\n`));
    Object.assign(a.rawCaptures[key], e.rawCaptures[key]);
  }
  const elf = Buffer.alloc(2048); elf.set([0x7f, 0x45, 0x4c, 0x46]);
  const local = write('softbus_lifecycle_probe', elf);
  const manifest = write('build.json', Buffer.from('{"fixture":"build","NEEDED":["libfixture.so"]}'));
  for (const p of [e.probe, a.probe]) {
    p.localPath = local.path; p.localSha256 = p.sha256 = local.sha256; p.buildManifest = {...manifest};
  }
  a.probe.remoteSha256 = local.sha256;
  const expectedPath = `${base}/expected.json`, evidencePath = `${base}/evidence.json`;
  function persist() {
    const exp = Buffer.from(JSON.stringify(f.expected)); const evidence = Buffer.from(JSON.stringify(f.evidence));
    fs.writeFileSync(absolute(expectedPath), exp); fs.writeFileSync(absolute(evidencePath), evidence);
    return {expectedPath, evidencePath, expectedSha256: hash(exp)};
  }
  const options = persist();
  const checked = validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs); assert.equal(checked.ok, true, JSON.stringify(checked));
  const ports = fakePorts(checked.binding);
  return {base, f, e, a, options, persist, ports, write};
}

async function execute(b, overrides = {}) {
  const result = await b.ports.clock.drive(runLifecycleProbe(b.options, {
    clock: b.ports.clock, processPort: b.ports.processPort, capturePort: b.ports.capturePort,
    ...(b.fsOverride ? {fs: b.fsOverride} : {}), ...overrides}));
  assert.equal(result.result.permissionOperations, 0);
  assert.equal(result.result.permissionRestoration, 'EXTERNAL_REQUIRED');
  assert.equal(result.result.sdkQuiescence, 'NOT_VERIFIED');
  assert.equal(result.result.raceCoverage.status, 'NOT_EXERCISED');
  assert.equal(result.result.scope, 'HOST_HARNESS_ONLY');
  return result;
}
const cases = [];
const test = (name, body) => cases.push([name, body]);
function reject(name, mutate) {
  test(name, async () => {
    const b = bundle(); await mutate(b);
    const r = await execute(b); assert.equal(r.exitCode, 64, JSON.stringify(r));
    assert.equal(b.ports.starts.length, 0); assert.equal(r.result.executionObservation, 'NOT_RUN');
  });
}
test('valid actual-byte bundle gives only frozen HOST execution observation', async () => {
  const b = bundle(), r = await execute(b);
  assert.equal(r.exitCode, 0, JSON.stringify(r.result.errors)); assert.equal(r.result.executionObservation, 'COMPLETE');
  assert(r.resultPath.endsWith(`/${b.f.expected.runId}/result.json`));
  const saved = JSON.parse(fs.readFileSync(absolute(r.resultPath), 'utf8'));
  assert.deepEqual(saved, r.result); assert.equal(saved.inputReceipt.expected.sha256, b.options.expectedSha256);
  assert(saved.inputReceipt.files.some(x => x.kind === 'ELF' && x.bytes === 2048));
  assert.equal(saved.inputReceipt.pathProtection, 'DESCRIPTOR_BYTES_WITH_PRE_POST_IDENTITY_CHECKS');
  assert.equal(saved.inputReceipt.atomicParentNoFollow, 'UNAVAILABLE_ON_THIS_NODE_WINDOWS');
  assert.equal(saved.inputReceipt.inputTreeExclusion, 'EXTERNAL_MAIN_REQUIRED');
});
test('absolute canonical paths use same actual descriptor bytes', async () => {
  const b = bundle(); b.options.expectedPath = absolute(b.options.expectedPath); b.options.evidencePath = absolute(b.options.evidencePath);
  assert.equal((await execute(b)).exitCode, 0);
});
test('expected uppercase SHA pin compares complete actual bytes', async () => {
  const b = bundle(); b.options.expectedSha256 = b.options.expectedSha256.toUpperCase(); assert.equal((await execute(b)).exitCode, 0);
});
reject('independently wrong expected SHA', b => { b.options.expectedSha256 = '0'.repeat(64); });
reject('wrong evidence guard rejected before process', b => { b.a.guard.result.Passed = false; b.options = b.persist(); });
reject('wrong profile rejected before process', b => { b.f.expected.profile = b.f.evidence.profile = 'lifecycle-arbitrary-v1'; b.options = b.persist(); });
reject('wrong argv/token cannot be fixed by preflight-confirmed', b => { b.a.launchArgv.push('--arbitrary'); b.options = b.persist(); });
reject('local raw mismatch despite matching JSON DTOs', b => fs.appendFileSync(absolute(b.e.rawCaptures.manager.path), 'altered'));
reject('local ELF mismatch', b => fs.appendFileSync(absolute(b.e.probe.localPath), Buffer.from([1])));
reject('invalid expected UTF8', b => {
  const bytes = Buffer.from([0xc3, 0x28]); fs.writeFileSync(absolute(b.options.expectedPath), bytes); b.options.expectedSha256 = hash(bytes);
});
reject('BOM expected JSON is not silently stripped', b => {
  const bytes = Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), fs.readFileSync(absolute(b.options.expectedPath))]);
  fs.writeFileSync(absolute(b.options.expectedPath), bytes); b.options.expectedSha256 = hash(bytes);
});
reject('malformed evidence JSON', b => fs.writeFileSync(absolute(b.options.evidencePath), '{bad json'));
reject('raw invalid UTF8 cannot hide behind correct hash', b => {
  const bytes = Buffer.from([0xff]); const raw = b.e.rawCaptures.manager; fs.writeFileSync(absolute(raw.path), bytes);
  raw.sha256 = b.a.rawCaptures.manager.sha256 = hash(bytes); b.options = b.persist();
});
test('exact128KiB raw UTF8 accepted', async () => {
  const b = bundle(), bytes = Buffer.alloc(131072, 120), raw = b.e.rawCaptures.manager;
  fs.writeFileSync(absolute(raw.path), bytes); raw.sha256 = b.a.rawCaptures.manager.sha256 = hash(bytes); b.options = b.persist();
  assert.equal((await execute(b)).exitCode, 0);
});
reject('over128KiB raw rejected', b => {
  const bytes = Buffer.alloc(131073, 120), raw = b.e.rawCaptures.manager;
  fs.writeFileSync(absolute(raw.path), bytes); raw.sha256 = b.a.rawCaptures.manager.sha256 = hash(bytes); b.options = b.persist();
});
test('ELF has separate binary ceiling, not UTF8 ceiling', async () => {
  const b = bundle(), bytes = Buffer.alloc(200000); bytes.set([127, 69, 76, 70]);
  fs.writeFileSync(absolute(b.e.probe.localPath), bytes);
  for (const p of [b.e.probe, b.a.probe]) p.sha256 = p.localSha256 = hash(bytes);
  b.a.probe.remoteSha256 = hash(bytes); b.options = b.persist(); assert.equal((await execute(b)).exitCode, 0);
});
reject('over64MiB ELF refused before hash allocation', b => fs.truncateSync(absolute(b.e.probe.localPath), 64 * 1024 * 1024 + 1));
reject('non ELF magic rejected with otherwise matching pin', b => {
  const bytes = Buffer.from('not an ELF'); fs.writeFileSync(absolute(b.e.probe.localPath), bytes);
  for (const p of [b.e.probe, b.a.probe]) p.sha256 = p.localSha256 = hash(bytes);
  b.a.probe.remoteSha256 = hash(bytes); b.options = b.persist();
});
for (const value of ['../expected.json', '.artifacts/../expected.json', '.artifacts/x/./expected.json',
  '.artifacts/x/expected.json:ads', '.artifacts/CON/file', '.artifacts/x./file', '.artifacts/x /file',
  '\\\\server\\share\\file', '\\\\?\\C:\\file', '.ARTIFACTS/file', '.artifacts/a\0b'])
  reject(`noncanonical path ${JSON.stringify(value)}`, b => { b.options.expectedPath = value; });
reject('directory leaf rejected', b => { b.options.expectedPath = b.base; });
reject('file identity changed observable at fstat', b => {
  b.fsOverride = new Proxy(fs, {get(target, key) {
    if (key === 'fstatSync') return (...args) => {
      const stat = fs.fstatSync(...args); return new Proxy(stat, {get(s, k) { return k === 'ino' ? s.ino + 1n : Reflect.get(s, k); }});
    };
    return Reflect.get(target, key);
  }});
  const original = b.ports; b.ports = {...original};
});
test('short descriptor read rejects before process', async () => {
  const b = bundle(), io = new Proxy(fs, {get(target, key) { return key === 'readSync' ? () => 0 : Reflect.get(target, key); }});
  const r = await execute(b, {fs: io}); assert.equal(r.exitCode, 64); assert.equal(b.ports.starts.length, 0);
});
test('held fd and path mismatch rejects before process', async () => {
  const b = bundle();
  const io = new Proxy(fs, {get(target, key) {
    if (key === 'fstatSync') return (...args) => {
      const stat = fs.fstatSync(...args); return new Proxy(stat, {get(s, k) { return k === 'ino' ? s.ino + 1n : Reflect.get(s, k); }});
    };
    return Reflect.get(target, key);
  }});
  assert.equal((await execute(b, {fs: io})).exitCode, 64); assert.equal(b.ports.starts.length, 0);
});
reject('conflicting pin for one canonical raw path', b => {
  b.e.rawCaptures.provider.path = b.a.rawCaptures.provider.path = b.e.rawCaptures.manager.path;
  b.e.rawCaptures.provider.sha256 = b.a.rawCaptures.provider.sha256 = '1'.repeat(64); b.options = b.persist();
});
reject('existing output refuses to overwrite evidence', b => {
  fs.mkdirSync(absolute(`.artifacts/verification/lifecycle-runner/${b.f.expected.runId}`), {recursive: true});
});
test('wall rollback after collector start refuses probe without rebudgeting', async () => {
  const b = bundle(), original = b.ports.processPort.start;
  b.ports.processPort.start = r => { const child = original(r); if (r.kind === 'collector') b.ports.clock.wall -= 5000; return child; };
  const result = await execute(b); assert.equal(result.exitCode, 1);
  assert.equal(b.ports.starts.some(r => r.kind === 'probe'), false);
});
test('mutating held input after collector start refuses probe', async () => {
  const b = bundle(), original = b.ports.processPort.start;
  b.ports.processPort.start = r => { const child = original(r); if (r.kind === 'collector') fs.appendFileSync(absolute(b.options.evidencePath), ' '); return child; };
  assert.equal((await execute(b)).exitCode, 1); assert.equal(b.ports.starts.some(r => r.kind === 'probe'), false);
});
test('injected deps without explicit fake process port can never invoke HDC', async () => {
  const b = bundle(); const r = await runLifecycleProbe(b.options, {clock: b.ports.clock}); assert.equal(r.exitCode, 64);
});
test('CLI rejects every nonexact invocation without executing HDC', () => {
  for (const argv of [[], ['--dry-success'], ['--execute'], ['--execute', 'a', 'b', 'c', 'extra'], ['--skip-guard']]) {
    const r = spawnSync(process.execPath, [path.join(root, 'scripts/LifecycleProbeRunner.cjs'), ...argv],
      {encoding: 'utf8', timeout: 3000, windowsHide: true});
    assert.equal(r.error, undefined); assert.equal(r.status, 64, r.stdout + r.stderr);
    assert.match(r.stdout + r.stderr, /NOT_RUN/);
  }
});

// Legitimate positive short reads must accumulate the complete ELF magic. Zero
// reads, IO failures and fewer than four total bytes remain refusal conditions.
test('complete ELF magic can arrive as consecutive two-byte descriptor reads', async () => {
  const b = bundle();
  const io = new Proxy(fs, {get(target, key) {
    if (key === 'readSync') return (fd, buffer, offset, length, position) => fs.readSync(fd, buffer, offset, Math.min(2, length), position);
    return Reflect.get(target, key);
  }});
  const r = await execute(b, {fs: io}); assert.equal(r.exitCode, 0, JSON.stringify(r.result.errors));
});
reject('ELF shorter than complete four-byte magic is refused with matching pins', b => {
  const bytes = Buffer.from([127, 69, 76]); fs.writeFileSync(absolute(b.e.probe.localPath), bytes);
  for (const p of [b.e.probe, b.a.probe]) p.sha256 = p.localSha256 = hash(bytes);
  b.a.probe.remoteSha256 = hash(bytes); b.options = b.persist();
});
test('descriptor I/O error is not treated as a partial-read success', async () => {
  const b = bundle();
  const io = new Proxy(fs, {get(target, key) { return key === 'readSync' ? () => { throw Error('fixture EIO'); } : Reflect.get(target, key); }});
  const r = await execute(b, {fs: io}); assert.equal(r.exitCode, 64); assert.equal(b.ports.starts.length, 0);
});

if (require.main === module) (async () => {
  let failed = 0; for (const [name, body] of cases) try { await body(); console.log(`PASS ${name}`); }
  catch (error) { ++failed; console.error(`FAIL ${name}: ${error.stack}`); }
  console.log(`LC_RUNNER tests=${cases.length} passed=${cases.length - failed} failed=${failed} scope=HOST_HARNESS_ONLY`);
  process.exitCode = failed ? 1 : 0;
})();
