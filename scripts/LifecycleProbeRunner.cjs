'use strict';

// Requiring this file performs no IO. Production execution is the exact CLI
// below; injected adapters always label their observations HOST_HARNESS_ONLY.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {TextDecoder} = require('node:util');
const {spawn} = require('node:child_process');
const {validateLifecycleRunBinding} = require('./LifecycleRunBinding.cjs');
const {runLifecycleProbeHarness} = require('./LifecycleProbeHarness.cjs');

const freeze = object => { if (object && typeof object === 'object' && !Object.isFrozen(object)) {
  Object.values(object).forEach(freeze); Object.freeze(object); } return object; };
const ensure = (ok, reason) => { if (!ok) throw Error(reason); };
const protection = () => ({pathProtection: 'DESCRIPTOR_BYTES_WITH_PRE_POST_IDENTITY_CHECKS',
  atomicParentNoFollow: 'UNAVAILABLE_ON_THIS_NODE_WINDOWS', inputTreeExclusion: 'EXTERNAL_MAIN_REQUIRED'});
const realClock = () => ({wallNowMs: () => Date.now(), monotonicNowMs: () => Number(process.hrtime.bigint()) / 1e6,
  setTimer: (fn, ms) => setTimeout(fn, ms), clearTimer: id => clearTimeout(id)});

function newResult(scope) {
  return {schemaVersion: 1, scope, profile: null, runId: null, activeBoards: [],
    inputReceipt: {expected: null, evidence: null, files: [], ...protection()},
    timing: {startedAt: null, finishedAt: null, elapsedMs: 0, bindingExpiresAt: null, admissions: []},
    budgets: {runMs: 7000, closeMs: 3000, sendMs: 1000, holdMs: 500, quietMs: 200,
      externalWatchdogMs: 10000, externalKillGraceMs: 2000, overallMs: 45000},
    boards: {}, executionObservation: 'NOT_RUN',
    raceCoverage: {status: 'NOT_EXERCISED', reason: 'SDK_OVERLAP_NOT_EVALUATED_IN_THIS_SLICE'},
    sdkQuiescence: 'NOT_VERIFIED', permissionRestoration: 'EXTERNAL_REQUIRED', permissionOperations: 0,
    transport: 'NOT_VERIFIED', errors: []};
}

function identity(stat) {
  const out = {};
  for (const key of ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs']) {
    ensure(typeof stat[key] === 'bigint' && stat[key] >= 0n, 'required exact file identity unavailable: ' + key);
    out[key] = stat[key].toString();
  }
  return out;
}
const sameIdentity = (a, b) => ['dev', 'ino', 'size', 'mtimeNs', 'ctimeNs'].every(key => a[key] === b[key]);

function inputTree(io, root) {
  const guards = new Map();
  function canonical(value) {
    ensure(typeof value === 'string' && value.length > 0 && value.length < 4096 && !value.includes('\0'), 'invalid input path');
    ensure(!/^(\\\\|\/\/)/.test(value), 'UNC/device path is forbidden');
    const absolute = path.isAbsolute(value);
    if (!absolute) ensure(!value.includes('\\'), 'relative path must use canonical slashes');
    const stripped = absolute ? value.replace(/^[A-Za-z]:/, '') : value;
    ensure(!stripped.includes(':'), 'ADS/colon forbidden');
    const lexical = stripped.split(/[\\/]/).filter(Boolean);
    for (const part of lexical) ensure(part !== '.' && part !== '..' && !/[. ]$/.test(part) &&
      !/^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part), 'noncanonical path component');
    const abs = absolute ? path.normalize(value) : path.join(root, ...value.split('/'));
    const relative = path.relative(root, abs).split(path.sep).join('/');
    ensure(relative.startsWith('.artifacts/') && !relative.includes('../') && !path.isAbsolute(relative), 'input escapes real .artifacts tree');
    ensure(absolute || relative === value, 'path normalization/alias forbidden');
    return {abs, relative};
  }
  function walk(abs, leafIsFile = true) {
    const parsed = path.parse(abs), components = abs.slice(parsed.root.length).split(path.sep).filter(Boolean);
    let current = parsed.root; const entries = [];
    for (let index = -1; index < components.length; ++index) {
      if (index >= 0) current = path.join(current, components[index]);
      const stat = io.lstatSync(current, {bigint: true});
      ensure(!stat.isSymbolicLink(), 'observed link/junction forbidden: ' + current);
      const last = index === components.length - 1;
      ensure(last && leafIsFile ? stat.isFile() : stat.isDirectory(), 'nonregular leaf or nondirectory parent');
      ensure(io.realpathSync(current) === current, 'resolved path differs (link/case alias): ' + current);
      entries.push({path: current, identity: identity(stat), mode: stat.mode.toString(), leaf: last});
    }
    return entries;
  }
  function compareWalk(before, after, file = true) {
    ensure(before.length === after.length, 'parent chain changed');
    before.forEach((entry, i) => {
      const next = after[i]; ensure(entry.path === next.path && entry.mode === next.mode &&
        entry.identity.dev === next.identity.dev && entry.identity.ino === next.identity.ino, 'parent/path identity changed');
      // Parent timestamps naturally change when output files are created. Their
      // stable identity/type/link checks remain; leaf bytes use full metadata.
      if (entry.leaf && file) ensure(sameIdentity(entry.identity, next.identity), 'file metadata changed');
    });
  }
  function read(value, pin, kind = 'UTF8', json = false) {
    const c = canonical(value), normalizedPin = pin == null ? null : String(pin).toLowerCase();
    ensure(normalizedPin === null || /^[a-f0-9]{64}$/.test(normalizedPin), 'invalid hash pin');
    const existing = guards.get(c.relative);
    if (existing) {
      ensure(existing.kind === kind && (normalizedPin === null || existing.receipt.sha256 === normalizedPin), 'conflicting pin/kind for shared path');
      return {receipt: existing.receipt, value: json ? JSON.parse(existing.text) : existing.text};
    }
    ensure(guards.size < 32, 'distinct read-set/open guard cap32');
    const beforeWalk = walk(c.abs), before = beforeWalk.at(-1).identity;
    let fd;
    try {
      fd = io.openSync(c.abs, 'r'); const opened = io.fstatSync(fd, {bigint: true});
      ensure(opened.isFile() && sameIdentity(before, identity(opened)), 'held descriptor/path identity mismatch');
      const size = Number(opened.size), cap = kind === 'ELF' ? 64 * 1024 * 1024 : 128 * 1024;
      ensure(Number.isSafeInteger(size) && size <= cap, 'input byte cap exceeded');
      const hash = crypto.createHash('sha256'), chunks = [], buffer = Buffer.alloc(65536);
      let count = 0; const magic = Buffer.alloc(4);
      while (count < size) {
        const n = io.readSync(fd, buffer, 0, Math.min(buffer.length, size - count), count);
        ensure(Number.isInteger(n) && n > 0 && n <= Math.min(buffer.length, size - count), 'short/invalid descriptor read');
        const part = buffer.subarray(0, n);
        if (count < 4) part.copy(magic, count, 0, Math.min(n, 4 - count));
        hash.update(part); if (kind === 'UTF8') chunks.push(Buffer.from(part)); count += n;
      }
      ensure(io.readSync(fd, buffer, 0, 1, size) === 0, 'extra descriptor bytes');
      const sha256 = hash.digest('hex'); ensure(normalizedPin === null || sha256 === normalizedPin, 'actual byte hash differs from independent pin');
      let text = null;
      if (kind === 'ELF') ensure(count >= 4 && magic.equals(Buffer.from([127, 69, 76, 70])), 'local probe has no ELF magic');
      else { const bytes = Buffer.concat(chunks); ensure(!bytes.subarray(0, 3).equals(Buffer.from([239, 187, 191])), 'UTF8 BOM forbidden');
        text = new TextDecoder('utf-8', {fatal: true}).decode(bytes); }
      const after = identity(io.fstatSync(fd, {bigint: true})), afterWalk = walk(c.abs);
      ensure(sameIdentity(before, after), 'descriptor changed while reading'); compareWalk(beforeWalk, afterWalk);
      const receipt = {path: c.relative, sha256, bytes: count, kind, identityBefore: before, identityAfter: after};
      const guard = {fd, c, kind, text, receipt, beforeWalk}; guards.set(c.relative, guard); fd = undefined;
      return {receipt, value: json ? JSON.parse(text) : text};
    } finally { if (fd !== undefined) io.closeSync(fd); }
  }
  function recheck() {
    for (const g of guards.values()) {
      const stat = io.fstatSync(g.fd, {bigint: true});
      ensure(stat.isFile() && sameIdentity(g.receipt.identityAfter, identity(stat)), 'held input descriptor changed');
      compareWalk(g.beforeWalk, walk(g.c.abs));
    }
  }
  return {canonical, walk, compareWalk, read, recheck, guards,
    close: () => { for (const guard of guards.values()) { try { io.closeSync(guard.fd); } catch (_) { /* read-only handle cleanup */ } } }};
}

function productionProcess(io, commands, transitions) {
  const env = {}; let pathValue = null;
  for (const [key, value] of Object.entries(process.env)) {
    if (key.toLowerCase() === 'path') { if (pathValue === null) pathValue = value; } else env[key] = value;
  }
  env.Path = pathValue || '';
  let executable = null;
  for (const directory of env.Path.split(path.delimiter)) {
    if (!directory || !path.isAbsolute(directory)) continue;
    const candidate = path.join(directory, process.platform === 'win32' ? 'hdc.exe' : 'hdc');
    try { if (io.statSync(candidate).isFile()) { executable = io.realpathSync(candidate); break; } } catch (_) { /* next PATH entry */ }
  }
  ensure(executable, 'trusted HDC executable not found on host PATH');
  const record = (request, event, extra = {}) => { if (transitions.length < 128) transitions.push({kind: request.kind, board: request.board, event, ...extra}); };
  return {metadata: {executable, version: 'NOT_OBSERVED'}, start(request) {
    ensure(commands.length < 8, 'fixed process count exceeded'); commands.push({...request});
    const child = spawn(executable, ['-t', request.target, 'shell', request.command], {shell: false, windowsHide: true, env,
      stdio: ['ignore', 'pipe', 'pipe']});
    record(request, 'START_RETURNED', {hostPid: child.pid ?? null});
    child.on('error', e => record(request, 'ERROR', {detail: e.message.slice(0, 512)}));
    child.on('exit', (code, signal) => record(request, 'EXIT', {code, signal}));
    child.on('close', (code, signal) => record(request, 'CLOSE', {code, signal}));
    return child;
  }};
}

function productionCapture(io, outAbs, outRelative) {
  const sinks = [];
  return {sinks, open({board, kind, stream}) {
    ensure(['A', 'B'].includes(board) && ['probe', 'collector', 'probe-audit', 'collector-audit'].includes(kind) && ['stdout', 'stderr'].includes(stream), 'invalid capture enum');
    const name = `${board}-${kind}-${stream}.log`, file = path.join(outAbs, name), relative = outRelative + '/' + name;
    const fd = io.openSync(file, 'wx'); const hash = crypto.createHash('sha256');
    let accepted = 0, persisted = 0, pending = 0, ending = false, closed = false, failure = null, tail = Promise.resolve();
    const setFailure = e => { if (!failure) failure = String(e.message || e).slice(0, 512); };
    function append(bytes) {
      ensure(!ending && !closed && !failure, failure || 'capture already ending');
      if (pending + bytes.length > 256 * 1024) { setFailure('pending write queue exceeds256KiB'); throw Error(failure); }
      const copy = Buffer.from(bytes); pending += copy.length; accepted += copy.length;
      tail = tail.then(() => new Promise(resolve => {
        if (failure) { pending -= copy.length; resolve(); return; }
        let offset = 0;
        const write = () => io.write(fd, copy, offset, copy.length - offset, null, (err, written) => {
          if (err || !Number.isInteger(written) || written <= 0) { setFailure(err || 'short asynchronous write'); pending -= copy.length - offset; resolve(); return; }
          hash.update(copy.subarray(offset, offset + written)); persisted += written; pending -= written; offset += written;
          if (offset < copy.length) write(); else resolve();
        });
        try { write(); } catch (e) { setFailure(e); pending -= copy.length - offset; resolve(); }
      }));
    }
    let finishPromise;
    function finish() {
      if (finishPromise) return finishPromise; ending = true;
      finishPromise = tail.then(() => new Promise(resolve => {
        io.close(fd, err => { closed = true; if (err) setFailure(err); resolve({path: relative, sha256: hash.digest('hex'), bytes: persisted,
          complete: failure === null && persisted === accepted, error: failure}); });
      }));
      return finishPromise;
    }
    const sink = {append, finish}; sinks.push(sink); return sink;
  }};
}

async function runLifecycleProbe(options, deps) {
  const injected = deps !== undefined, io = deps?.fs || fs, clock = deps?.clock || realClock();
  let result = newResult(injected ? 'HOST_HARNESS_ONLY' : 'LC_NATIVE_EXECUTION_OBSERVATION'), resultPath = null;
  let tree, output, binding, expected, evidence, processPort, begun = false, startMono = null;
  const commands = [], transitions = [];
  const fail = (code, stage, reason) => { result.errors.push({code, board: null, stage, detail: String(reason).slice(0, 512)});
    result.executionObservation = begun ? 'FAIL' : 'NOT_RUN'; };
  try {
    ensure(options && Object.keys(options).sort().join(',') === 'evidencePath,expectedPath,expectedSha256', 'exact runner arguments required');
    ensure(typeof options.expectedSha256 === 'string' && /^[a-fA-F0-9]{64}$/.test(options.expectedSha256), 'independent expected SHA256 required');
    ensure(!injected || deps.processPort?.start, 'injected execution requires explicit fake processPort');
    const root = path.resolve(__dirname, '..'); tree = inputTree(io, root);
    const exp = tree.read(options.expectedPath, options.expectedSha256, 'UTF8', true); expected = freeze(exp.value);
    const ev = tree.read(options.evidencePath, null, 'UTF8', true); evidence = freeze(ev.value);
    result.inputReceipt.expected = {path: exp.receipt.path, sha256: exp.receipt.sha256, bytes: exp.receipt.bytes};
    result.inputReceipt.evidence = {path: ev.receipt.path, sha256: ev.receipt.sha256, bytes: ev.receipt.bytes};
    const initialWall = clock.wallNowMs(), initialMono = clock.monotonicNowMs();
    ensure(Number.isFinite(initialWall) && Number.isFinite(initialMono) && initialWall >= 0 && initialMono >= 0, 'invalid initial clock');
    const validated = validateLifecycleRunBinding(evidence, expected, initialWall);
    ensure(validated.ok, 'frozen LC binding refused: ' + JSON.stringify(validated.errors).slice(0, 384)); binding = validated.binding;
    result.profile = binding.profile; result.runId = binding.runId; result.activeBoards = [...binding.activeBoards];
    result.timing.bindingExpiresAt = binding.expiresAt;
    for (const board of binding.activeBoards) {
      const e = expected.boards[board], a = evidence.boards[board];
      for (const key of ['origin', 'ownership', 'freshBefore']) tree.read(e[key].path, e[key].sha256, 'UTF8', true);
      tree.read(a.snapshots.current.path, a.snapshots.current.sha256, 'UTF8', true);
      for (const key of ['manager', 'pidAudit', 'metadata', 'probe', 'provider']) tree.read(e.rawCaptures[key].path, e.rawCaptures[key].sha256);
      tree.read(e.probe.buildManifest.path, e.probe.buildManifest.sha256, 'UTF8', true);
      tree.read(e.probe.localPath, e.probe.localSha256, 'ELF');
    }
    result.inputReceipt.files = [...tree.guards.values()].map(g => g.receipt);
    const outRelative = `.artifacts/verification/lifecycle-runner/${binding.runId}`, outAbs = path.join(root, ...outRelative.split('/'));
    // Only these two fixed parent directories can be created. Each existing
    // component is checked; the per-run leaf must be exclusively new.
    let parent = path.join(root, '.artifacts'); tree.walk(parent, false);
    for (const name of ['verification', 'lifecycle-runner']) {
      parent = path.join(parent, name);
      try { io.mkdirSync(parent); } catch (e) { if (e.code !== 'EEXIST') throw e; }
      tree.walk(parent, false);
    }
    io.mkdirSync(outAbs); output = {abs: outAbs, relative: outRelative, guard: tree.walk(outAbs, false)};
    ensure(![...tree.guards.values()].some(g => g.c.abs.startsWith(outAbs + path.sep)), 'input/output overlap');
    const writeExclusive = (name, value) => {
      tree.compareWalk(output.guard, tree.walk(outAbs, false), false);
      io.writeFileSync(path.join(outAbs, name), JSON.stringify(value, null, 2) + '\n', {flag: 'wx'});
    };
    writeExclusive('input-receipt.json', result.inputReceipt); writeExclusive('validation.json', validated);
    const port = injected ? deps.processPort : productionProcess(io, commands, transitions);
    const capturePort = injected ? deps.capturePort : productionCapture(io, outAbs, outRelative);
    ensure(capturePort?.open, 'capture port required');
    if (!injected) writeExclusive('host.json', port.metadata);
    processPort = {start(request) { if (injected) commands.push({...request}); const child = port.start(request); begun = true;
      if (injected) transitions.push({kind: request.kind, board: request.board, event: 'START_RETURNED', hostPid: child.pid ?? null}); return child; }};
    let lastWall = initialWall, lastMono = initialMono, anchorMono = null, anchorRemaining = null;
    const expires = Date.parse(binding.expiresAt);
    const recheckAdmission = () => {
      try {
        tree.recheck(); tree.compareWalk(output.guard, tree.walk(outAbs, false), false);
        // No IO, waiting, or hashing after these samples and before start().
        const wall = clock.wallNowMs(), mono = clock.monotonicNowMs();
        ensure(Number.isFinite(wall) && Number.isFinite(mono) && wall >= lastWall && mono >= lastMono, 'observed invalid/backwards clock');
        lastWall = wall; lastMono = mono;
        const checked = validateLifecycleRunBinding(evidence, expected, wall);
        ensure(checked.ok, 'original LC evidence no longer fresh/valid');
        const wallRemaining = Math.floor(expires - wall);
        const monoRemaining = anchorMono === null ? wallRemaining : Math.floor(anchorRemaining - (mono - anchorMono));
        const remainingMs = Math.min(wallRemaining, monoRemaining);
        ensure(remainingMs >= 10000, 'original dual-clock remaining reserve below10000ms');
        if (anchorMono === null) { anchorMono = mono; anchorRemaining = wallRemaining; }
        return {ok: true, checkedAt: new Date(wall).toISOString(), remainingMs};
      } catch (e) { return {ok: false, reason: e.message}; }
    };
    startMono = clock.monotonicNowMs();
    const observed = await runLifecycleProbeHarness({binding, recheckAdmission, processPort, capturePort, clock});
    result = {...observed, scope: result.scope, inputReceipt: result.inputReceipt};
    // Persistence has no new timing budget. Synchronous OS filesystem calls are
    // not hard-real-time cancellable; reject if the original host budget elapsed.
    ensure(clock.monotonicNowMs() - startMono < 45000, 'overall budget exhausted before final persistence');
    writeExclusive('commands.json', commands); writeExclusive('transitions.json', transitions);
    ensure(clock.monotonicNowMs() - startMono < 45000, 'overall budget exhausted during final persistence');
    writeExclusive('result.json', result); resultPath = outRelative + '/result.json';
  } catch (e) {
    // A harness DTO is deeply frozen; keep immutable observations intact while
    // adding an explicit local persistence/admission failure to a new result.
    result = {...result, errors: [...result.errors]}; fail('RUNNER_REFUSED', output ? 'execution/persistence' : 'local-admission', e.message);
  } finally { tree?.close(); }
  const exitCode = result.executionObservation === 'COMPLETE' ? 0 : result.executionObservation === 'NOT_EXERCISED' ? 2 : begun ? 1 : 64;
  return freeze({exitCode, resultPath, result});
}

module.exports = {runLifecycleProbe};

if (require.main === module) {
  const args = process.argv.slice(2);
  if (args.length !== 4 || args[0] !== '--execute') {
    const result = newResult('LC_NATIVE_EXECUTION_OBSERVATION');
    result.errors.push({code: 'CLI_USAGE', board: null, stage: 'cli', detail: 'expected exactly --execute evidencePath expectedPath expectedSha256'});
    process.stdout.write(JSON.stringify({exitCode: 64, resultPath: null, result}) + '\n'); process.exitCode = 64;
  } else runLifecycleProbe({evidencePath: args[1], expectedPath: args[2], expectedSha256: args[3]}).then(outcome => {
    process.stdout.write(JSON.stringify(outcome) + '\n'); process.exitCode = outcome.exitCode;
  }).catch(error => { process.stderr.write('NOT_RUN unexpected runner rejection: ' + error.message.slice(0, 512) + '\n'); process.exitCode = 64; });
}
