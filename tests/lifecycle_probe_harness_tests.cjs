'use strict';
// Fabricated native-shaped HOST transcripts; this file never starts HDC.
const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const crypto = require('node:crypto');
const {runLifecycleProbeHarness} = require('../scripts/LifecycleProbeHarness.cjs');
const {validateLifecycleRunBinding} = require('../scripts/LifecycleRunBinding.cjs');
const {lifecycleBindingFixture} = require('./run_binding_fixtures.cjs');
const BOOT = '12345678-1234-1234-1234-123456789abc';

class FakeClock {
  constructor() { this.time = 0; this.wall = Date.parse('2026-09-14T04:30:30.000Z'); this.next = 0; this.timers = new Map(); }
  wallNowMs = () => this.wall + this.time;
  monotonicNowMs = () => this.time;
  setTimer = (fn, ms) => { const id = ++this.next; this.timers.set(id, {at: this.time + ms, fn}); return id; };
  clearTimer = id => this.timers.delete(id);
  async drive(promise) {
    let done = false, value, failure;
    promise.then(v => { value = v; done = true; }, e => { failure = e; done = true; });
    for (let count = 0; !done && count < 3000; ++count) {
      // Drain microtasks and pending local fixture writes before advancing time.
      for (let i = 0; i < 8 && !done; ++i) await new Promise(setImmediate);
      if (done) break;
      const pending = [...this.timers].sort((a, b) => a[1].at - b[1].at || a[0] - b[0]);
      assert(pending.length, 'unresolved promise without a tracked timer');
      const [id, timer] = pending[0]; this.timers.delete(id); this.time = timer.at; timer.fn();
    }
    assert(done, 'harness must resolve within finite fake timer steps');
    if (failure) throw failure;
    return value;
  }
}

function nativeTranscript(binding, board, options = {}) {
  const b = binding.boards[board], pid = board === 'A' ? '1001' : '1002';
  const original = '4294967297', token = String(b.token), lines = [], events = [];
  let active = 0, closing = 0, sequence = 0, nextTid = 40;
  const event = (kind, socket = 0, code = 0, bytes = 0, tid = 0) => {
    if (kind === 'CallbackEnter') ++active;
    if (kind === 'CallbackExit') --active;
    if (kind === 'CloseRequested') closing = 1;
    events.push(`event=${kind} run_id=${binding.runId} pid=${pid} tid=${tid} seq=${++sequence} steady_us=${sequence * 10} wall_us=${1000000 + sequence * 10} socket=${socket} code=${code} bytes=${bytes} active=${active} closing=${closing}` +
      (kind === 'ShutdownReturned' ? ' return_kind=VOID quiescence=NOT_VERIFIED' : ''));
  };
  const call = (operation, socket, rc, bytes = 0, during = () => {}) => {
    const tid = ++nextTid; event('WorkerToken', 0, 0, token, tid); event('CallBegin', socket, operation, bytes, tid);
    during(); event('CallReturn', socket, rc, bytes, tid); event('WorkerJoined');
  };
  const shutdown = socket => {
    const tid = ++nextTid; event('ShutdownClaimed', socket); event('WorkerToken', 0, 0, token, tid);
    event('ShutdownStarted', socket, 0, 0, tid); event('ShutdownReturned', socket, 0, 0, tid); event('WorkerJoined');
  };
  lines.push(`LC_WRAPPER_START run_id=${binding.runId} board=${board} pid=${pid} boot_id=${BOOT} start_ticks=12345`,
    'scope=NATIVE_SOCKET_LIFECYCLE',
    `run_id=${binding.runId} pid=${pid} process_name=fastdds_dsoftbus_probe package=com.kaihong.mdds writes=NONE external_preflight=OPERATOR_ASSERTED run_ms=7000 close_ms=3000 send_ms=1000 hold_ms=500 quiet_ms=200`,
    `stage=original_token pid=${pid} tid=${pid} first=${original} second=${token} rc=0 scope=CURRENT_MAIN_THREAD_ONLY`,
    `stage=select_token pid=${pid} tid=${pid} first=${token} second=${token} rc=0 scope=CURRENT_MAIN_THREAD_ONLY`,
    `stage=identity phase=after_select lookup=${token} type=1 info_rc=0 query_rc=0 state_count=1`);
  call(1, 0, 1, 0, () => event('Created', 1));
  let owned = 1, aliases = 0, calls = 1;
  if (b.role === 'single') {
    ++calls; const other = options.different ? 2 : 1; owned = options.different ? 2 : 1; aliases = options.different ? 0 : 1;
    call(1, 0, other, 0, () => event(options.different ? 'Created' : 'Alias', other));
    event('CloseRequested'); if (owned === 2) shutdown(2); shutdown(1);
  } else if (b.role === 'server') {
    call(2, 1, 0); lines.push(...events.splice(0));
    lines.push(`stage=Listen action=RETURN rc=0 run_id=${binding.runId}`);
    event('CallbackEnter', 2, 0, 0, 90); event('Created', 2); ++owned;
    event('Accepted', 2); event('Ready', 2); event('CallbackExit', 2, 0, 0, 90);
    event('CallbackEnter', 2, 0, 0, 91); event('Bytes', 2, 0, 60000); event('CallbackHeld', 2, 0, 60000, 91);
    event('CloseRequested'); event('ShutdownClaimed', 2); event('WorkerToken', 0, 0, token, 92);
    event('ShutdownStarted', 2, 0, 0, 92); event('CallbackReleased', 2, 0, 60000, 91);
    event('CallbackExit', 2, 0, 0, 91); event('ShutdownReturned', 2, 0, 0, 92); event('WorkerJoined'); shutdown(1);
  } else {
    call(3, 1, 0); event('Ready', 1); call(4, 1, 0, 60000);
    event('CallbackEnter', 1, 0, 0, 93); event('PeerShutdown', 1); event('CloseRequested');
    event('CallbackExit', 1, 0, 0, 93); shutdown(1);
  }
  lines.push(...events, `stage=restore_token pid=${pid} tid=${pid} first=${original} second=${original} rc=0 scope=CURRENT_MAIN_THREAD_ONLY`);
  const rc = options.different ? 2 : 0;
  lines.push(`result=${rc ? 'NOT_EXERCISED' : 'OBSERVATION_COMPLETE'} socket_calls=${calls} shutdown_calls=${owned} owned=${owned} aliases=${aliases} shutdown_returned=${owned} active_callbacks=0 events=${sequence} application_workers_joinable=0 untracked_id=0 reason=none token_restored=1 cleanup=OBSERVED_KNOWN_HANDLES_ONLY sdk_race=REQUIRES_EXTERNAL_LOGS quiescence=NOT_VERIFIED transport=NOT_IMPLEMENTED`,
    `LC_REMOTE_EXIT run_id=${binding.runId} board=${board} rc=${rc}`);
  return lines.join('\n') + '\n';
}

function fakePorts(binding, options = {}, clock = new FakeClock()) {
  const starts = [], children = [], sinks = [], admissions = [];
  let pendingServer = null;
  const processPort = {start(request) {
    starts.push({...request});
    if (options.spawnThrow === request.kind) throw new Error('fixture spawn throw');
    if (request.kind === 'probe' && request.board === 'B' && pendingServer)
      clock.setTimer(() => { const resume = pendingServer; pendingServer = null; resume?.(); }, 2);
    const child = new EventEmitter(); child.stdout = new EventEmitter(); child.stderr = new EventEmitter();
    child.pid = 500 + starts.length; child.closed = false; children.push(child);
    child.kill = () => {
      if (options.unkillable) return false;
      clock.setTimer(() => finish(null, 'SIGTERM'), 0); return true;
    };
    const output = (data, stream = 'stdout') => child[stream].emit('data', Buffer.isBuffer(data) ? data : Buffer.from(data));
    const finish = (code = 0, signal = null) => {
      if (child.closed) return;
      child.emit('exit', code, signal);
      if (options.noClose && request.kind === 'probe') return;
      child.stdout.emit('end'); child.stderr.emit('end'); child.closed = true; child.emit('close', code, signal);
    };
    clock.setTimer(() => {
      if (request.kind === 'collector') {
        const id = request.board === 'A' ? '2001' : '2002';
        const text = `LC_COLLECTOR_START run_id=${binding.runId} board=${request.board} pid=${id} boot_id=${BOOT} start_ticks=12345\nLC_COLLECTOR_READY run_id=${binding.runId} board=${request.board}\n`;
        output(options.collectorWrong ? text.replace('LC_COLLECTOR_READY', 'LC_COLLECTOR_READY_BAD') : text);
        if (options.collectorHang) return;
        clock.setTimer(() => { output(`opaque SDK-like Shutdown# socket=2\nLC_COLLECTOR_REMOTE_EXIT run_id=${binding.runId} board=${request.board} rc=124\n`); finish(); }, 20);
      } else if (request.kind === 'probe') {
        let text = nativeTranscript(binding, request.board, options);
        if (options.mutate) text = options.mutate(text, request.board);
        if (options.probeError) child.emit('error', new Error('fixture process error'));
        if (options.exitBeforeData) child.emit('exit', 0, null);
        if (options.invalidUtf8) output(Buffer.from([0xc3, 0x28]));
        if (options.overflow) output(Buffer.alloc(options.overflow, 120));
        const sendText = value => {
          if (options.split) {
            const bytes = Buffer.from('diagnostic 中文\n' + value); const index = bytes.indexOf(Buffer.from('中')) + 1;
            output(bytes.subarray(0, index)); output(bytes.subarray(index));
          } else output(value, options.stderrOnly ? 'stderr' : 'stdout');
        };
        const cut = text.indexOf('event=CallbackEnter');
        if (binding.boards[request.board].role === 'server' && cut >= 0 && !options.stderrOnly) {
          // Real A cannot emit its received payload or terminal before B starts.
          // All already-observable duplicate Listen/header lines stay in prefix.
          pendingServer = () => {
            sendText(text.slice(cut));
            if (!options.probeHang) clock.setTimer(() => finish(options.hdcExit ?? 0), 2);
          };
          sendText(text.slice(0, cut));
          clock.setTimer(() => {
            if (pendingServer && !starts.some(r => r.kind === 'probe' && r.board === 'B')) {
              pendingServer = null;
              output(`LC_REMOTE_EXIT run_id=${binding.runId} board=${request.board} rc=6\n`); finish();
            }
          }, 40);
          return;
        }
        sendText(text);
        if (options.probeHang) return;
        clock.setTimer(() => finish(options.hdcExit ?? 0), 2);
      } else if (request.kind === 'audit') {
        if (options.auditHang) return;
        const subject = request.command.includes('kind=collector') ? 'collector' : 'probe';
        const pid = (subject === 'collector' ? '200' : '100') + (request.board === 'A' ? '1' : '2');
        const boot = options.bootChanged ? BOOT.replace('12345678', '87654321') : BOOT;
        const state = options.alive || options.reused ? 'PRESENT' : 'ABSENT';
        const ticks = options.reused ? '12346' : options.alive ? '12345' : '0';
        output(`LC_AUDIT run_id=${binding.runId} board=${request.board} kind=${subject} pid=${pid} boot_id=${boot} state=${state} start_ticks=${ticks}\nLC_AUDIT_REMOTE_EXIT run_id=${binding.runId} board=${request.board} kind=${subject} rc=0\n`);
        finish();
      } else throw new Error('unexpected process kind');
    }, 1);
    return child;
  }};
  const capturePort = {open(spec) {
    const parts = []; let count = 0;
    const sink = {spec, append(bytes) {
      if (options.sinkError) throw new Error('fixture sink error');
      parts.push(Buffer.from(bytes)); count += bytes.length;
    }, finish() {
      if (options.sinkHang) return new Promise(() => {});
      const content = Buffer.concat(parts);
      return Promise.resolve({path: `.artifacts/fixture/${spec.board}-${spec.kind}-${spec.stream}.log`,
        sha256: crypto.createHash('sha256').update(content).digest('hex'), bytes: count,
        complete: !options.sinkIncomplete, error: options.sinkIncomplete ? 'fixture incomplete' : null});
    }}; sinks.push(sink); return sink;
  }};
  const recheckAdmission = request => {
    admissions.push({...request});
    if (options.refuse?.(request, admissions.length)) return {ok: false, reason: 'fixture admission refusal'};
    return {ok: true, checkedAt: new Date(clock.wallNowMs()).toISOString(), remainingMs: options.remaining ?? 80000};
  };
  return {processPort, capturePort, recheckAdmission, clock, starts, children, sinks, admissions};
}

async function scenario(options = {}, profile = 'lifecycle-callback-close-v1', selected) {
  const f = lifecycleBindingFixture(profile, selected), checked = validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs);
  assert.equal(checked.ok, true); const ports = fakePorts(checked.binding, options);
  const result = await ports.clock.drive(runLifecycleProbeHarness({binding: checked.binding, ...ports}));
  assert.equal(result.raceCoverage.status, 'NOT_EXERCISED'); assert.equal(result.sdkQuiescence, 'NOT_VERIFIED');
  assert.equal(result.permissionRestoration, 'EXTERNAL_REQUIRED'); assert.equal(result.permissionOperations, 0);
  assert.equal(result.scope, 'HOST_HARNESS_ONLY'); assert.equal(result.transport, 'NOT_VERIFIED');
  assert(ports.clock.time <= 45250, 'all outstanding work has a fixed overall envelope');
  assert(Object.isFrozen(result));
  return {...ports, result};
}

const cases = [];
const test = (name, body) => cases.push([name, body]);
for (const board of ['A', 'B']) test(`alias ${board} only selected board, COMPLETE`, async () => {
  const s = await scenario({}, 'lifecycle-alias-v1', board);
  assert.equal(s.result.executionObservation, 'COMPLETE', JSON.stringify(s.result.errors));
  assert(s.starts.every(x => x.board === board)); assert.equal(s.starts.filter(x => x.kind === 'probe').length, 1);
});
test('pair native ledger and exact live readiness', async () => {
  const s = await scenario(); assert.equal(s.result.executionObservation, 'COMPLETE', JSON.stringify(s.result.errors));
  assert.deepEqual(s.starts.filter(x => x.kind === 'probe').map(x => x.board), ['A', 'B']);
  assert.deepEqual(s.admissions.map(x => `${x.kind}-${x.board}`), ['collector-A', 'collector-B', 'probe-A', 'probe-B']);
});
test('alias different IDs is NOT_EXERCISED, never COMPLETE', async () => {
  assert.equal((await scenario({different: true}, 'lifecycle-alias-v1', 'A')).result.executionObservation, 'NOT_EXERCISED');
});
test('fixed commands quote token argv, timeout, proc last-parenthesis and clear LD_PRELOAD', async () => {
  const s = await scenario();
  for (const r of s.starts) {
    assert.match(r.target, /^3e01ff/); assert.doesNotMatch(r.command, /pkill|killall|SetSelfToken|grant|revoke|nativetoken\.json/);
    if (r.kind === 'probe') {
      assert.match(r.command, /timeout -k 2s 10s sh -c/); assert.match(r.command, /exec env/);
      assert.match(r.command, /LD_PRELOAD=/); assert.match(r.command, /LC_WRAPPER_START/);
      assert(r.command.includes('##*) '), 'proc stat strips through last ) space');
      assert(r.command.includes('shift 19'), 'start_ticks is field20 of remainder');
    }
  }
});
test('fatal UTF8 decoder preserves split valid UTF8', async () => assert.equal((await scenario({split: true})).result.executionObservation, 'COMPLETE'));
test('first collector refusal has zero process starts', async () => {
  const s = await scenario({refuse: () => true}); assert.equal(s.starts.length, 0); assert.equal(s.result.executionObservation, 'NOT_RUN');
});
test('probe refusal after collectors is FAIL without probe', async () => {
  const s = await scenario({refuse: r => r.kind === 'probe'}); assert.equal(s.starts.filter(x => x.kind === 'probe').length, 0);
  assert.equal(s.result.executionObservation, 'FAIL');
});
test('stale B is never started after A ready', async () => {
  const s = await scenario({refuse: r => r.kind === 'probe' && r.board === 'B'});
  assert.deepEqual(s.starts.filter(x => x.kind === 'probe').map(x => x.board), ['A']); assert.equal(s.result.executionObservation, 'FAIL');
});
for (const [remaining, starts] of [[9999, 0], [10000, 2]]) test(`remaining reserve ${remaining}`, async () => {
  const s = await scenario({remaining}); assert.equal(s.starts.filter(x => x.kind === 'probe').length, starts);
});
for (const [name, options] of [
  ['wrong scope', {mutate: t => t.replace('scope=NATIVE_SOCKET_LIFECYCLE', 'scope=HOST_FIXTURE_NOT_DEVICE')}],
  ['header PID mismatch', {mutate: t => t.replace('pid=1001 process_name', 'pid=9999 process_name')}],
  ['wrong run Listen', {mutate: t => t.replace('stage=Listen action=RETURN rc=0 run_id=', 'stage=Listen action=RETURN rc=0 run_id=old-')}],
  ['negative Listen', {mutate: t => t.replace('stage=Listen action=RETURN rc=0', 'stage=Listen action=RETURN rc=-1')}],
  ['duplicate same-chunk Listen', {mutate: t => t.replace(/(stage=Listen[^\n]*\n)/, '$1$1')}],
  ['header duplicate', {mutate: t => t.replace(/(run_id=[^\n]*process_name=[^\n]*\n)/, '$1$1')}],
  ['stderr only', {stderrOnly: true}], ['exit before close/readiness', {exitBeforeData: true}],
  ['reentrant error', {probeError: true}], ['bad collector readiness', {collectorWrong: true}]
]) test(`${name} prevents B`, async () => {
  const s = await scenario(options); assert.equal(s.starts.some(r => r.kind === 'probe' && r.board === 'B'), false);
  assert.notEqual(s.result.executionObservation, 'COMPLETE');
});
for (const [name, mutate] of [
  ['missing remote exit', t => t.replace(/LC_REMOTE_EXIT[^\n]*\n/, '')],
  ['duplicate remote exit', t => t.replace(/(LC_REMOTE_EXIT[^\n]*\n)/, '$1$1')],
  ['remote watchdog', t => t.replace(/(LC_REMOTE_EXIT[^\n]*rc=)0/, '$1124')],
  ['retained nonzero', t => t + 'termination=NONZERO_PROCESS_EXIT_RETAINED exit_code=70 token_restored=1\n'],
  ['terminal absent', t => t.replace(/result=OBSERVATION_COMPLETE[^\n]*\n/, '')],
  ['terminal duplicate', t => t.replace(/(result=OBSERVATION_COMPLETE[^\n]*\n)/, '$1$1')],
  ['terminal counters alone', t => t.replace(/^event=[^\n]*\n/gm, '')],
  ['owner conservation', t => t.replace(' owned=1 ', ' owned=2 ')],
  ['callback active', t => t.replace(' active_callbacks=0 ', ' active_callbacks=1 ')],
  ['worker joinable', t => t.replace(' application_workers_joinable=0 ', ' application_workers_joinable=1 ')],
  ['token restore missing', t => t.replace(/^stage=restore_token[^\n]*\n/m, '')],
  ['wrong worker token', t => t.replace(/(event=WorkerToken[^\n]*bytes=)671793191/, '$1671793192')],
  ['event sequence gap', t => t.replace(' seq=2 ', ' seq=3 ')],
  ['event time backwards', t => t.replace(' steady_us=20 ', ' steady_us=0 ')],
  ['uint64 overflow', t => t.replace(' tid=41 ', ' tid=18446744073709551616 ')],
  ['void suffix missing', t => t.replace(' return_kind=VOID quiescence=NOT_VERIFIED', '')],
  ['call return error', t => t.replace(/(event=CallReturn[^\n]*socket=0 code=)1/, '$1-1')],
  ['malformed reserved line', t => t + 'event=MadeUp success=true\n'],
  ['unterminated final record', t => t.trimEnd()]
]) test(`${name} fails observation`, async () => {
  const s = await scenario({mutate}, 'lifecycle-alias-v1', 'A'); assert.equal(s.result.executionObservation, 'FAIL');
});
for (const [name, options] of [['hdc nonzero', {hdcExit: 1}], ['invalid UTF8', {invalidUtf8: true}],
  ['line overflow', {overflow: 65537}], ['stream overflow', {overflow: 1048577}],
  ['sink error', {sinkError: true}], ['sink incomplete', {sinkIncomplete: true}], ['sink hung', {sinkHang: true}],
  ['probe local timeout', {probeHang: true}], ['unclosed stubborn child', {noClose: true, unkillable: true}],
  ['audit hung', {auditHang: true}], ['process alive', {alive: true}], ['boot changed', {bootChanged: true}]])
  test(name, async () => assert.notEqual((await scenario(options, 'lifecycle-alias-v1', 'A')).result.executionObservation, 'COMPLETE'));
test('reused PID proves only original absence and never signals replacement', async () => {
  const s = await scenario({reused: true}, 'lifecycle-alias-v1', 'A');
  assert.equal(s.result.boards.A.probe.termination.status, 'GONE_PID_REUSED'); assert.equal(s.result.executionObservation, 'COMPLETE');
});

// Five bounded self-found implementation gaps. Original53 assertions above
// remain unchanged; these ports are still host-only and never start HDC.
async function adjustedPorts(customize, options = {}) {
  const f = lifecycleBindingFixture('lifecycle-alias-v1', 'A');
  const {binding} = validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs);
  const ports = fakePorts(binding, options); customize(ports);
  const result = await ports.clock.drive(runLifecycleProbeHarness({binding, ...ports}));
  assert.equal(result.scope, 'HOST_HARNESS_ONLY'); assert.equal(result.permissionOperations, 0);
  return {...ports, result};
}
for (const [name, options, partialOpen] of [
  ['first admission refuses', {refuse: () => true}, false],
  ['spawn throws', {spawnThrow: 'collector'}, false],
  ['second capture open throws', {}, true]
]) test(`preopened capture finish when ${name}`, async () => {
  const finished = [], opened = [];
  const s = await adjustedPorts(ports => {
    const original = ports.capturePort.open;
    ports.capturePort.open = spec => {
      if (partialOpen && opened.length === 1) throw Error('second capture fixture open failure');
      const sink = original(spec), id = opened.length; opened.push(id);
      return {append: sink.append, finish() { finished.push(id); return sink.finish(); }};
    };
  }, options);
  assert.equal(s.result.executionObservation, 'NOT_RUN');
  assert.deepEqual(finished.sort(), opened, 'every successfully opened sink finishes exactly once');
});
test('preopened sink finish rejection/hang stays bounded and explicitly unknown', async () => {
  for (const mode of ['reject', 'hang']) {
    const s = await adjustedPorts(ports => {
      ports.capturePort.open = () => ({append() {}, finish() {
        if (mode === 'reject') return Promise.reject(Error('finish fixture failure'));
        return new Promise(() => {});
      }});
    }, {refuse: () => true});
    const receipt = s.result.boards.A.collector.captures.stdout;
    assert.deepEqual([receipt?.path, receipt?.sha256, receipt?.bytes], [null, null, null]);
    assert.equal(receipt.complete, false); assert(receipt.error);
    assert(s.clock.time <= 45000);
  }
});
test('identity prelude records fixed wrapper error before failure exit', async () => {
  const s = await scenario({}, 'lifecycle-alias-v1', 'A');
  for (const request of s.starts.filter(r => r.kind !== 'audit')) {
    assert.match(request.command, /LC_WRAPPER_ERROR/);
    assert.match(request.command, /stage=IDENTITY rc=71/);
    assert.doesNotMatch(request.command, /\|\| exit 71/, 'identity failures use the recorded error path');
  }
});
test('incomplete capture keeps actual persisted prefix, never accepted-byte hash', async () => {
  const prefix = Buffer.from('disk'), digest = crypto.createHash('sha256').update(prefix).digest('hex');
  const s = await adjustedPorts(ports => {
    const original = ports.capturePort.open;
    ports.capturePort.open = spec => spec.kind === 'collector' && spec.stream === 'stdout' ? {
      append() {}, finish: async () => ({path: '.artifacts/fixture/persisted-prefix.log', sha256: digest,
        bytes: prefix.length, complete: false, error: 'fixture ENOSPC after prefix'})
    } : original(spec);
  });
  const actual = s.result.boards.A.collector.captures.stdout;
  assert.equal(actual.path, '.artifacts/fixture/persisted-prefix.log'); assert.equal(actual.sha256, digest);
  assert.equal(actual.bytes, 4); assert.equal(actual.complete, false); assert.equal(s.result.executionObservation, 'FAIL');
});
test('missing or malformed sink receipt never claims a persisted byte count/hash', async () => {
  for (const mode of ['hang', 'malformed']) {
    const s = await adjustedPorts(ports => {
      const original = ports.capturePort.open;
      ports.capturePort.open = spec => spec.kind === 'collector' && spec.stream === 'stdout' ? {
        append() {}, finish: () => mode === 'hang' ? new Promise(() => {}) : Promise.resolve({path: '.artifacts/fixture/bad', sha256: 'not-hash', bytes: -1, complete: false, error: 'broken'})
      } : original(spec);
    });
    const actual = s.result.boards.A.collector.captures.stdout;
    assert.deepEqual([actual.path, actual.sha256, actual.bytes], [null, null, null]);
    assert.equal(actual.complete, false); assert(actual.error); assert.equal(s.result.executionObservation, 'FAIL');
  }
});
test('completed workers may reuse a TID with a new checked WorkerToken', async () => {
  const s = await scenario({mutate: t => t.replace(/tid=(41|42|43) /g, 'tid=41 ')}, 'lifecycle-alias-v1', 'A');
  assert.equal(s.result.executionObservation, 'COMPLETE', JSON.stringify(s.result.errors));
});
function renumberEvents(text) {
  let count = 0;
  text = text.replace(/^event=[^\n]*$/gm, line => line.replace(/ seq=[0-9]+ /, ` seq=${++count} `)
    .replace(/ steady_us=[0-9]+ /, ` steady_us=${count * 10} `));
  return text.replace(/ events=[0-9]+ /, ` events=${count} `);
}
test('an active TID cannot receive another WorkerToken', async () => {
  const s = await scenario({mutate: t => {
    const tokenLine = t.match(/^event=WorkerToken[^\n]*\n/m)[0];
    return renumberEvents(t.replace(/(^event=CallBegin[^\n]*\n)/m, '$1' + tokenLine));
  }}, 'lifecycle-alias-v1', 'A');
  assert.equal(s.result.executionObservation, 'FAIL');
});
test('reused TID cannot reuse an old WorkerToken for its next operation', async () => {
  const s = await scenario({mutate: t => {
    t = t.replace(/tid=(41|42|43) /g, 'tid=41 ');
    let count = 0; t = t.replace(/^event=WorkerToken[^\n]*\n/gm, line => ++count === 2 ? '' : line);
    return renumberEvents(t);
  }}, 'lifecycle-alias-v1', 'A');
  assert.equal(s.result.executionObservation, 'FAIL');
});

// Main expanded this batch to seven gaps before production fixes: preserve
// causal wrapper exit order and make late local error events harmless only.
test('remote exit before full native terminal is not a complete observation', async () => {
  const s = await scenario({mutate: t => {
    const remote = t.match(/^LC_REMOTE_EXIT[^\n]*\n/m)[0];
    return t.replace(remote, '').replace(/^result=/m, remote + 'result=');
  }}, 'lifecycle-alias-v1', 'A');
  assert.equal(s.result.executionObservation, 'FAIL');
});
test('already-visible remote exit before Listen prevents B', async () => {
  const s = await scenario({mutate: (t, board) => {
    if (board !== 'A') return t;
    const remote = t.match(/^LC_REMOTE_EXIT[^\n]*\n/m)[0];
    return t.replace(remote, '').replace(/^stage=Listen/m, remote + 'stage=Listen');
  }});
  assert.equal(s.starts.some(r => r.kind === 'probe' && r.board === 'B'), false);
  assert.equal(s.result.executionObservation, 'FAIL');
});
for (const surface of ['child', 'stdout', 'stderr']) test(`late stubborn ${surface} error is inert after finalization`, async () => {
  const s = await scenario({noClose: true, unkillable: true}, 'lifecycle-alias-v1', 'A');
  const child = s.children.find(c => !c.closed); assert(child, 'fixture must retain an unclosed local child');
  const snapshot = JSON.stringify(s.result), captureCount = s.sinks.length;
  const emitter = surface === 'child' ? child : child[surface];
  assert.doesNotThrow(() => emitter.emit('error', Error('late host-only error')));
  child.stdout.emit('data', Buffer.from('result=OBSERVATION_COMPLETE\n'));
  child.emit('close', 0, null); child.stdout.emit('end'); child.stderr.emit('end');
  assert.equal(JSON.stringify(s.result), snapshot, 'final evidence cannot change');
  assert.equal(s.sinks.length, captureCount, 'late events must not open capture/audit work');
  assert.equal(s.result.executionObservation, 'FAIL');
});

// Independent review R1: an already-resolved READY promise cannot authorize
// launching after a separately delivered collector remote-exit record.
async function collectorReadinessRegression(profile, selected, collectorBoard, phase, together = false) {
  const f = lifecycleBindingFixture(profile, selected);
  const {binding} = validateLifecycleRunBinding(f.evidence, f.expected, f.nowMs);
  const ports = fakePorts(binding), start = ports.processPort.start;
  let collectorEmit, injected = false;
  const terminal = Buffer.from(`LC_COLLECTOR_REMOTE_EXIT run_id=${binding.runId} board=${collectorBoard} rc=124\n`);
  const endCollector = () => {
    assert(collectorEmit, 'required collector must already exist');
    assert.equal(injected, false, 'inject exactly one remote exit');
    injected = true; collectorEmit('data', terminal);
  };
  ports.processPort.start = request => {
    const child = start(request), emit = child.stdout.emit.bind(child.stdout);
    if (request.kind === 'collector' && request.board === collectorBoard) {
      collectorEmit = emit;
      child.stdout.emit = (event, ...args) => {
        if (event === 'data') {
          const text = args[0].toString();
          // Preserve the fixture's normal later close/audit; suppress only its
          // duplicate marker, so framing alone causes the old false success.
          if (injected && text.includes('LC_COLLECTOR_REMOTE_EXIT')) return true;
          if (phase === 'ready' && text.includes('LC_COLLECTOR_READY')) {
            if (together) { injected = true; return emit(event, Buffer.concat([args[0], terminal])); }
            const result = emit(event, ...args); endCollector(); return result;
          }
        }
        return emit(event, ...args);
      };
    } else if (request.kind === 'probe' && request.board === 'A' && phase === 'listen') {
      child.stdout.emit = (event, ...args) => {
        const result = emit(event, ...args);
        if (event === 'data' && args[0].toString().includes('stage=Listen')) endCollector();
        return result;
      };
    }
    return child;
  };
  if (phase === 'admit-first' || phase === 'admit-B') {
    const recheck = ports.recheckAdmission;
    ports.recheckAdmission = request => {
      const result = recheck(request);
      if (request.kind === 'probe' && request.board === (phase === 'admit-B' ? 'B' : binding.activeBoards[0])) endCollector();
      return result;
    };
  }
  const result = await ports.clock.drive(runLifecycleProbeHarness({binding, ...ports}));
  assert(injected, 'counterexample must actually publish the remote exit');
  const blocked = phase === 'listen' || phase === 'admit-B' ? 'B' : binding.activeBoards[0];
  assert.equal(ports.starts.some(r => r.kind === 'probe' && r.board === blocked), false,
    `must not start ${blocked} after known collector-${collectorBoard} termination`);
  assert.equal(result.executionObservation, 'FAIL');
  if (!together) assert(result.errors.some(e => e.code === 'COLLECTOR_INACTIVE'), 'specific launch refusal is recorded');
  assert.equal(result.scope, 'HOST_HARNESS_ONLY'); assert.equal(result.permissionOperations, 0);
  assert(ports.clock.time <= 45250, 'no enlarged observation budget');
}
for (const board of ['A', 'B']) {
  for (const together of [false, true]) test(`R1 alias-${board} collector exit ${together ? 'same chunk' : 'split events'} blocks first probe`,
    () => collectorReadinessRegression('lifecycle-alias-v1', board, board, 'ready', together));
  test(`R1 pair collector-${board} exit before A blocks first probe`,
    () => collectorReadinessRegression('lifecycle-callback-close-v1', undefined, board, 'ready'));
  test(`R1 pair collector-${board} exit after A Listen blocks B`,
    () => collectorReadinessRegression('lifecycle-callback-close-v1', undefined, board, 'listen'));
  test(`R1 pair collector-${board} exit during B final admission blocks B`,
    () => collectorReadinessRegression('lifecycle-callback-close-v1', undefined, board, 'admit-B'));
  test(`R1 alias-${board} collector exit during first final admission blocks probe`,
    () => collectorReadinessRegression('lifecycle-alias-v1', board, board, 'admit-first'));
}

if (require.main === module) (async () => {
  let failed = 0; for (const [name, body] of cases) try { await body(); console.log(`PASS ${name}`); }
  catch (error) { ++failed; console.error(`FAIL ${name}: ${error.stack}`); }
  console.log(`LC_HARNESS tests=${cases.length} passed=${cases.length - failed} failed=${failed} scope=HOST_HARNESS_ONLY`);
  process.exitCode = failed ? 1 : 0;
})();
module.exports = {FakeClock, nativeTranscript, fakePorts};
