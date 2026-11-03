// Host-only orchestration tests. No HDC, SDK or permission operation is run.
'use strict';
const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const {runRecordPair} = require('../scripts/RecordPairHarness.cjs');

function scenario(mode, timeoutMs = 100) {
  const children = {}, events = [], raw = [];
  let aLiveAtB = false;
  const spawn = (_exe, args, options) => {
    const role = args[1] === 'board-a' ? 'A' : 'B';
    assert.equal(options.shell, false);
    assert.equal(options.windowsHide, true);
    assert.equal(Object.keys(options.env).filter(k => k.toLowerCase() === 'path').length, 1);
    if (role === 'B') aLiveAtB = !children.A.closed;
    const c = new EventEmitter();
    c.stdout = new EventEmitter(); c.stderr = new EventEmitter();
    c.pid = role === 'A' ? 501 : 502;
    c.kill = () => {
      if (mode === 'unkillable') return false;
      c.closed = true; c.emit('close', null, 'SIGTERM'); return true;
    };
    children[role] = c;
    const output = text => c.stdout.emit('data', Buffer.from(text));
    const finish = () => {
      if (c.closed) return;
      output(`result=${mode === 'bind_fail' && role === 'B' ? 'FAIL' : 'PASS'}\n`);
      if (mode !== 'missing_rc') output(`REMOTE_EXIT=${mode === 'bind_fail' && role === 'B' ? 6 : 0}\n`);
      if (mode === 'duplicate_rc') output('REMOTE_EXIT=0\n');
      c.closed = true; c.emit('close', 0, null);
    };
    setImmediate(() => {
      if (mode === 'spawn_error' && role === 'A') {
        c.emit('error', new Error('simulated spawn error')); c.closed = true; c.emit('close', -2, null); return;
      }
      output(`pid=${role === 'A' ? 101 : 102}\n`);
      if (role === 'A') {
        if (mode === 'a_fail') { output('stage=Listen action=RETURN at_ms=1 rc=-1\n'); finish(); return; }
        if (mode === 'false_ready') { output('stage=Listen action=RETURN at_ms=1 rc=01\n'); finish(); return; }
        if (mode === 'late_ready') c.emit('exit', 0, null);
        // Readiness split across chunks, no assumption about HDC packet boundaries.
        output('stage=Listen action=RET'); output('URN at_ms=1 rc=0 elapsed_ms=0\r\n');
        if (mode === 'hang' || mode === 'unkillable') return;
        setTimeout(finish, 15);
      } else {
        c.stderr.emit('data', Buffer.from('diagnostic only\n'));
        if (mode !== 'hang' && mode !== 'unkillable') setTimeout(finish, 2);
      }
    });
    return c;
  };
  return runRecordPair({spawn, targets: {A:'board-a', B:'board-b'},
    commands: {A:'SIMULATED-A', B:'SIMULATED-B'}, env: {Path:'one', PATH:'two'},
    timeoutMs, drainGraceMs: 20, onOutput: (r,s,b) => raw.push([r,s,b.toString()]),
    onTransition: e => events.push(e)}).then(result => ({result, children, aLiveAtB, events, raw}));
}

(async () => {
  if (!process.argv[2] || process.argv[2] === 'functional_cycle_budget') {
    // A long functional scenario may need more than a minute; its successful
    // collectors still finish immediately here. This is not an SDK deadline.
    assert.equal((await scenario('success', 14400000)).result.pass, true);
    let spawned = false;
    for (const timeoutMs of [0, -1, 14400001, Infinity, 1.5]) {
      await assert.rejects(runRecordPair({spawn: () => {spawned = true;},
        targets: {A:'board-a', B:'board-b'}, commands: {A:'A', B:'B'}, timeoutMs}),
        /invalid explicit record run configuration/);
    }
    assert.equal(spawned, false, 'invalid budgets must fail before a subprocess');
    console.log('functional_cycle_budget PASS scope=HOST_HARNESS_ONLY');
  }
  for (const mode of ['success','a_fail','bind_fail','missing_rc','duplicate_rc','false_ready','spawn_error','hang','late_ready','unkillable']) {
    if (process.argv[2] && process.argv[2] !== mode) continue;
    let watchdog;
    let s;
    try {
      s = await Promise.race([scenario(mode), new Promise((_, reject) => {
        watchdog = setTimeout(() => reject(new Error('harness remained unresolved')), 500);
      })]);
    } finally { clearTimeout(watchdog); }
    assert.equal(s.result.pass, mode === 'success', mode);
    const startsB = !['a_fail','false_ready','spawn_error','late_ready'].includes(mode);
    assert.equal(!!s.children.B, startsB, mode);
    if (startsB) assert.equal(s.aLiveAtB, true, 'B must start before A closes');
    if (mode === 'bind_fail') {
      assert.equal(s.result.B.hdcExit, 0);
      assert.equal(s.result.B.remoteExit, 6);
    }
    if (mode === 'success') assert(s.raw.some(x => x[0] === 'B' && x[1] === 'stderr'));
    if (mode === 'hang') assert.equal(s.result.timedOut, true);
    if (mode === 'unkillable') {
      assert.equal(s.result.collectionCleanup, 'UNCONFIRMED');
      assert.deepEqual(s.result.activeCollectors.map(x => x.pid), [501,502]);
    }
    console.log(`${mode} PASS scope=HOST_HARNESS_ONLY`);
  }
})().catch(e => { console.error(e); process.exitCode=1; });
