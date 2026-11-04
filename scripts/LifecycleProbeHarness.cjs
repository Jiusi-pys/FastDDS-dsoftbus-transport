'use strict';

// Observation only. These adapters do not grant authority, restore permissions,
// establish SDK quiescence, or establish same-name Participant hot rebuilding.
const {TextDecoder} = require('node:util');
const crypto = require('node:crypto');

const U64 = (1n << 64n) - 1n;
const BOOT = '[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}';
const KINDS = new Set(('Created Alias Accepted Ready CloseRequested ShutdownClaimed ShutdownStarted ShutdownReturned ' +
  'CallbackEnter CallbackExit Bytes Discarded Failure WorkerToken CallBegin CallReturn WorkerJoined CallbackHeld ' +
  'CallbackReleased PeerShutdown PeerError NegotiateHeld NegotiateReleased').split(' '));
const freeze = value => { if (value && typeof value === 'object' && !Object.isFrozen(value)) {
  Object.values(value).forEach(freeze); Object.freeze(value); } return value; };
const quote = value => "'" + String(value).replace(/'/g, "'\\''") + "'";
const uint = value => { if (!/^(0|[1-9][0-9]*)$/.test(value) || BigInt(value) > U64) throw Error('invalid uint64'); return value; };
const small = value => { uint(value); const n = Number(value); if (!Number.isSafeInteger(n)) throw Error('unsafe counter'); return n; };
const int = value => { if (!/^(0|-?[1-9][0-9]*)$/.test(value)) throw Error('invalid int'); const n = Number(value);
  if (n < -2147483648 || n > 2147483647) throw Error('int32 range'); return n; };
const need = (condition, detail) => { if (!condition) throw Error(detail); };
const termination = () => ({status: 'NOT_STARTED', identity: null, checkedAt: null, audit: null});
const emptyCapture = error => ({path: null, sha256: crypto.createHash('sha256').digest('hex'), bytes: 0, complete: false, error});
// Shared inert handler deliberately captures no run, child or evidence state.
// A stubborn, already-reported local child can still emit 'error' later.
function absorbLateError() {}

function identityPrelude(marker, runId, board) {
  // /proc/P/stat comm can contain spaces and ')'. Field 22 is field 20 only
  // AFTER removing through the last ') '. Do not split the original stat line.
  return `lc_identity_error() { printf 'LC_WRAPPER_ERROR run_id=%s board=%s stage=IDENTITY rc=71\\n' ${quote(runId)} ${quote(board)}; exit 71; }; ` +
    'set -f; p=$$; boot=$(cat /proc/sys/kernel/random/boot_id) || lc_identity_error; ' +
    'stat=$(cat /proc/$$/stat) || lc_identity_error; rest=${stat##*) }; ' +
    '[ "$rest" != "$stat" ] || lc_identity_error; set -- $rest; [ "$#" -ge 20 ] || lc_identity_error; shift 19; ticks=$1; ' +
    'case "$p:$ticks" in *[!0-9:]*|:*|*:) lc_identity_error;; esac; [ "$p" -gt 0 ] && [ "$ticks" -gt 0 ] || lc_identity_error; ' +
    `printf '%s\\n' "$boot" | grep -Eq ${quote('^' + BOOT + '$')} || lc_identity_error; ` +
    `printf '${marker} run_id=%s board=%s pid=%s boot_id=%s start_ticks=%s\\n' ${quote(runId)} ${quote(board)} "$p" "$boot" "$ticks" || lc_identity_error; `;
}

function commandFor(binding, board, kind) {
  const b = binding.boards[board], prefix = kind === 'probe' ? 'LC_WRAPPER_START' : 'LC_COLLECTOR_START';
  const prelude = identityPrelude(prefix, binding.runId, board);
  if (kind === 'probe') {
    const inner = prelude + `exec env LD_LIBRARY_PATH=${quote(b.loader.LD_LIBRARY_PATH)} LD_PRELOAD=${quote('')} ` + b.launchArgv.map(quote).join(' ');
    return `timeout -k 2s 10s sh -c ${quote(inner)}; rc=$?; printf 'LC_REMOTE_EXIT run_id=%s board=%s rc=%s\\n' ` +
      `${quote(binding.runId)} ${quote(board)} "$rc"; exit "$rc"`;
  }
  const inner = prelude + `printf 'LC_COLLECTOR_READY run_id=%s board=%s\\n' ${quote(binding.runId)} ${quote(board)}; ` +
    'exec hilog -D d005740,d005741,d005743,d005784,d005782,d005763,d005700,d005701,d005706';
  return `timeout -k 1s 15s sh -c ${quote(inner)}; rc=$?; printf 'LC_COLLECTOR_REMOTE_EXIT run_id=%s board=%s rc=%s\\n' ` +
    `${quote(binding.runId)} ${quote(board)} "$rc"; exit 0`;
}

function auditCommand(binding, board, kind, identity) {
  const inner = 'set -f; boot=$(cat /proc/sys/kernel/random/boot_id) || exit 71; ' +
    `printf '%s\\n' "$boot" | grep -Eq ${quote('^' + BOOT + '$')} || exit 71; ` +
    `if [ ! -d /proc/${identity.pid} ]; then state=ABSENT; ticks=0; else ` +
    `stat=$(cat /proc/${identity.pid}/stat) || exit 71; rest=\${stat##*) }; ` +
    '[ "$rest" != "$stat" ] || exit 71; set -- $rest; [ "$#" -ge 20 ] || exit 71; shift 19; ticks=$1; ' +
    'case "$ticks" in ""|*[!0-9]*) exit 71;; esac; [ "$ticks" -gt 0 ] || exit 71; state=PRESENT; fi; ' +
    `printf 'LC_AUDIT run_id=%s board=%s kind=${kind} pid=%s boot_id=%s state=%s start_ticks=%s\\n' ` +
    `${quote(binding.runId)} ${quote(board)} ${quote(identity.pid)} "$boot" "$state" "$ticks"`;
  return `timeout -k 1s 2s sh -c ${quote(inner)}; rc=$?; printf 'LC_AUDIT_REMOTE_EXIT run_id=%s board=%s kind=${kind} rc=%s\\n' ` +
    `${quote(binding.runId)} ${quote(board)} "$rc"; exit "$rc"`;
}

function nativeParser(binding, board, dto) {
  const b = binding.boards[board], ledger = {scope: 0, header: 0, listen: 0, identity: 0,
    original: null, selected: false, restored: false, close: false, active: 0,
    steady: 0n, owners: new Map(), workers: new Set(), activeWorkers: new Set(), calls: new Map(), successes: [],
    callbacks: new Map(), held: new Map(), released: 0, accepted: new Set(), ready: new Set(),
    aliases: 0, joins: 0, begins: 0, starts: 0, bytes: false, peerShutdown: false};
  const owner = id => { need(ledger.owners.has(id), 'event references unknown owner'); return ledger.owners.get(id); };
  function line(text) {
    let m;
    if (text.startsWith('scope=')) { need(text === 'scope=NATIVE_SOCKET_LIFECYCLE' && ++ledger.scope === 1, 'scope mismatch/duplicate'); return; }
    if (text.startsWith('run_id=')) {
      m = /^run_id=(\S+) pid=([0-9]+) process_name=fastdds_dsoftbus_probe package=com\.kaihong\.mdds writes=NONE external_preflight=OPERATOR_ASSERTED run_ms=7000 close_ms=3000 send_ms=1000 hold_ms=500 quiet_ms=200$/.exec(text);
      need(m && ++ledger.header === 1 && ledger.scope === 1 && m[1] === binding.runId && m[2] === dto.wrapperIdentity?.pid, 'native header identity');
      uint(m[2]); dto.nativeIdentity = {pid: m[2], runId: m[1], scope: 'NATIVE_SOCKET_LIFECYCLE'}; return;
    }
    if (text.startsWith('stage=Listen')) {
      need(text === `stage=Listen action=RETURN rc=0 run_id=${binding.runId}` && ++ledger.listen === 1 && dto.nativeIdentity && !dto.probe.terminal, 'Listen identity/order/duplicate');
      need(ledger.successes.some(x => x.op === 2), 'Listen without successful SDK ledger'); return;
    }
    if (/^stage=(original_token|select_token|restore_token)/.test(text)) {
      m = /^stage=(original_token|select_token|restore_token) pid=([0-9]+) tid=([0-9]+) first=([0-9]+) second=([0-9]+) rc=(-?[0-9]+) scope=CURRENT_MAIN_THREAD_ONLY$/.exec(text);
      need(m && dto.nativeIdentity && m[2] === dto.nativeIdentity.pid && m[3] === m[2] && int(m[6]) === 0, 'token record identity/rc');
      const first = uint(m[4]), second = uint(m[5]), token = String(b.token);
      if (m[1] === 'original_token') { need(!ledger.original && !ledger.selected && first !== '0' && second === token, 'original token'); ledger.original = first; }
      if (m[1] === 'select_token') { need(ledger.original && !ledger.selected && first === token && second === token, 'selected token'); ledger.selected = true; }
      if (m[1] === 'restore_token') { need(ledger.selected && !ledger.restored && first === ledger.original && second === ledger.original, 'restore token'); ledger.restored = dto.probe.tokenRestoreObserved = true; }
      return;
    }
    if (text.startsWith('stage=identity')) {
      m = /^stage=identity phase=after_select lookup=([0-9]+) type=(-?[0-9]+) info_rc=0 query_rc=0 state_count=1$/.exec(text);
      need(m && ledger.selected && ++ledger.identity === 1 && uint(m[1]) === String(b.token), 'native short identity'); int(m[2]); return;
    }
    if (text.startsWith('stage=local_thread_checkpoint')) {
      m = /^stage=local_thread_checkpoint pid=([0-9]+) tid=([0-9]+) count=1 complete=1 single_main=1$/.exec(text);
      need(m && m[1] === dto.nativeIdentity?.pid && m[2] === m[1], 'thread checkpoint'); return;
    }
    if (text.startsWith('event=')) {
      m = /^event=(\w+) run_id=(\S+) pid=([0-9]+) tid=([0-9]+) seq=([0-9]+) steady_us=([0-9]+) wall_us=([0-9]+) socket=(-?[0-9]+) code=(-?[0-9]+) bytes=([0-9]+) active=([0-9]+) closing=([01])(.*)$/.exec(text);
      need(m && KINDS.has(m[1]) && m[2] === binding.runId && m[3] === dto.nativeIdentity?.pid && !dto.probe.terminal && ledger.identity === 1, 'event grammar/identity/order');
      const e = {event: m[1], run_id: m[2], pid: uint(m[3]), tid: uint(m[4]), seq: small(m[5]), steady_us: uint(m[6]), wall_us: uint(m[7]),
        socket: int(m[8]), code: int(m[9]), bytes: uint(m[10]), active: small(m[11]), closing: Number(m[12])};
      need(e.seq === dto.probe.events.length + 1 && e.seq <= 512 && BigInt(e.steady_us) >= ledger.steady, 'event sequence/time/cap');
      need(m[13] === (e.event === 'ShutdownReturned' ? ' return_kind=VOID quiescence=NOT_VERIFIED' : ''), 'event suffix');
      if (e.event === 'ShutdownReturned') { e.return_kind = 'VOID'; e.quiescence = 'NOT_VERIFIED'; }
      ledger.steady = BigInt(e.steady_us);
      const key = e.tid + ':' + e.socket;
      if (e.event === 'CallbackEnter') { ++ledger.active; ledger.callbacks.set(key, (ledger.callbacks.get(key) || 0) + 1); }
      if (e.event === 'CallbackExit') { need(ledger.callbacks.get(key) > 0 && !ledger.held.has(key), 'unpaired/held callback exit'); --ledger.active;
        ledger.callbacks.set(key, ledger.callbacks.get(key) - 1); }
      if (e.event === 'CloseRequested') { need(!ledger.close, 'duplicate close'); ledger.close = true; }
      need(e.active === ledger.active && e.closing === Number(ledger.close), 'active/closing ledger');
      switch (e.event) {
        case 'Created': need(e.socket > 0 && !ledger.owners.has(e.socket) && ledger.owners.size < 16, 'owner duplicate/cap'); ledger.owners.set(e.socket, {claimed: false, tid: null, returned: false}); break;
        case 'Alias': need(!owner(e.socket).claimed, 'alias after shutdown claim'); ++ledger.aliases; break;
        case 'Accepted': owner(e.socket); ledger.accepted.add(e.socket); break;
        case 'Ready': owner(e.socket); ledger.ready.add(e.socket); break;
        case 'WorkerToken': need(e.tid !== '0' && e.bytes === String(b.token) && e.code === 0 && !ledger.workers.has(e.tid) && !ledger.activeWorkers.has(e.tid), 'worker token'); ledger.workers.add(e.tid); break;
        case 'CallBegin': need(ledger.workers.has(e.tid) && !ledger.calls.has(e.tid) && [1, 2, 3, 4].includes(e.code), 'SDK call begin');
          ledger.workers.delete(e.tid); ledger.activeWorkers.add(e.tid);
          ledger.calls.set(e.tid, {op: e.code, bytes: e.bytes, socket: e.socket}); ++ledger.begins; break;
        case 'CallReturn': { const c = ledger.calls.get(e.tid); need(c && c.bytes === e.bytes && c.socket === e.socket, 'SDK return pairing');
          if (c.op === 1) owner(e.code); else need(e.code === 0, 'SDK call failed'); ledger.successes.push({...c, rc: e.code});
          ledger.calls.delete(e.tid); ledger.activeWorkers.delete(e.tid); break; }
        case 'WorkerJoined': need(e.tid === '0', 'joined record tid'); ++ledger.joins; break;
        case 'ShutdownClaimed': need(ledger.close && !owner(e.socket).claimed, 'shutdown claim'); owner(e.socket).claimed = true; break;
        case 'ShutdownStarted': { const o = owner(e.socket); need(o.claimed && o.tid === null && ledger.workers.has(e.tid), 'shutdown start');
          ledger.workers.delete(e.tid); ledger.activeWorkers.add(e.tid); o.tid = e.tid; ++ledger.starts; break; }
        case 'ShutdownReturned': { const o = owner(e.socket); need(o.tid === e.tid && !o.returned, 'shutdown return');
          o.returned = true; ledger.activeWorkers.delete(e.tid); break; }
        case 'Bytes': owner(e.socket); need(ledger.ready.has(e.socket), 'bytes before ready'); if (e.bytes === '60000') ledger.bytes = true; break;
        case 'CallbackHeld': case 'NegotiateHeld': need(ledger.callbacks.get(key) > 0 && !ledger.held.has(key), 'callback hold'); ledger.held.set(key, e.event); break;
        case 'CallbackReleased': case 'NegotiateReleased': need(ledger.held.get(key) === e.event.replace('Released', 'Held'), 'callback release'); ledger.held.delete(key); ++ledger.released; break;
        case 'PeerShutdown': owner(e.socket); ledger.peerShutdown = true; break;
        case 'Failure': case 'PeerError': throw Error('native failure event');
      }
      dto.probe.events.push(e); return;
    }
    if (text.startsWith('result=')) {
      m = /^result=(OBSERVATION_COMPLETE|NOT_EXERCISED|FAIL) socket_calls=([0-9]+) shutdown_calls=([0-9]+) owned=([0-9]+) aliases=([0-9]+) shutdown_returned=([0-9]+) active_callbacks=([0-9]+) events=([0-9]+) application_workers_joinable=([01]) untracked_id=(-?[0-9]+) reason=(\S+) token_restored=([01]) cleanup=(OBSERVED_KNOWN_HANDLES_ONLY|UNCONFIRMED) sdk_race=REQUIRES_EXTERNAL_LOGS quiescence=NOT_VERIFIED transport=NOT_IMPLEMENTED$/.exec(text);
      need(m && dto.nativeIdentity && !dto.probe.terminal, 'terminal grammar/duplicate/early failure');
      const t = {result: m[1]}; ['socket_calls', 'shutdown_calls', 'owned', 'aliases', 'shutdown_returned', 'active_callbacks', 'events', 'application_workers_joinable'].forEach((k, i) => { t[k] = small(m[i + 2]); });
      Object.assign(t, {untracked_id: int(m[10]), reason: m[11], token_restored: Number(m[12]), cleanup: m[13], sdk_race: 'REQUIRES_EXTERNAL_LOGS', quiescence: 'NOT_VERIFIED', transport: 'NOT_IMPLEMENTED'});
      dto.probe.terminal = t;
      need(t.result !== 'FAIL' && ledger.restored && t.token_restored === 1 && t.reason === 'none' && t.untracked_id === 0 &&
        t.active_callbacks === 0 && ledger.active === 0 && t.application_workers_joinable === 0 && t.cleanup === 'OBSERVED_KNOWN_HANDLES_ONLY', 'terminal cleanup');
      need(t.owned > 0 && t.owned === ledger.owners.size && t.aliases === ledger.aliases && t.events === dto.probe.events.length &&
        t.shutdown_calls === t.owned && t.shutdown_returned === t.owned && ledger.close && ledger.starts === t.owned &&
        [...ledger.owners.values()].every(o => o.returned) && ledger.calls.size === 0 && ledger.held.size === 0 &&
        ledger.workers.size === 0 && ledger.activeWorkers.size === 0 &&
        ledger.joins === ledger.begins + ledger.starts, 'terminal conservation');
      need(t.socket_calls === ledger.successes.filter(c => c.op === 1).length, 'Socket call count');
      if (b.role === 'single') need(t.socket_calls === 2 && ((t.result === 'OBSERVATION_COMPLETE' && t.owned === 1 && t.aliases === 1) ||
        (t.result === 'NOT_EXERCISED' && t.owned === 2 && t.aliases === 0)), 'alias profile observation');
      else {
        need(t.result === 'OBSERVATION_COMPLETE' && t.socket_calls === 1, 'callback profile terminal');
        if (b.role === 'server') need(ledger.listen === 1 && [...ledger.accepted].some(s => ledger.ready.has(s)) && ledger.bytes && ledger.released === 1, 'callback server observation');
        else need(ledger.successes.some(c => c.op === 3) && ledger.successes.some(c => c.op === 4 && c.bytes === '60000') && ledger.ready.size > 0 && ledger.peerShutdown, 'callback client observation');
      }
      return;
    }
    if (/^(stage=|termination=|event=|LC_)/.test(text)) throw Error('unrecognized/reserved native record');
  }
  return {line, ready: () => ledger.scope === 1 && ledger.header === 1 && ledger.listen === 1 && !dto.probe.terminal,
    finish: () => need(dto.probe.terminal && ledger.restored, 'missing native terminal/restoration')};
}

async function runLifecycleProbeHarness({binding, recheckAdmission, processPort, capturePort, clock}) {
  const startedWall = clock.wallNowMs(), startedMono = clock.monotonicNowMs();
  const stamp = () => { const n = clock.wallNowMs(); return Number.isFinite(n) ? new Date(n).toISOString() : null; };
  const result = {schemaVersion: 1, scope: 'HOST_HARNESS_ONLY', profile: binding.profile, runId: binding.runId,
    activeBoards: [...binding.activeBoards], inputReceipt: {expected: null, evidence: null, files: [],
      pathProtection: 'DESCRIPTOR_BYTES_WITH_PRE_POST_IDENTITY_CHECKS', atomicParentNoFollow: 'UNAVAILABLE_ON_THIS_NODE_WINDOWS', inputTreeExclusion: 'EXTERNAL_MAIN_REQUIRED'},
    timing: {startedAt: new Date(startedWall).toISOString(), finishedAt: null, elapsedMs: 0, bindingExpiresAt: binding.expiresAt, admissions: []},
    budgets: {...binding.budgets, externalKillGraceMs: 2000, overallMs: 45000}, boards: {}, executionObservation: 'NOT_RUN',
    raceCoverage: {status: 'NOT_EXERCISED', reason: 'SDK_OVERLAP_NOT_EVALUATED_IN_THIS_SLICE'}, sdkQuiescence: 'NOT_VERIFIED',
    permissionRestoration: 'EXTERNAL_REQUIRED', permissionOperations: 0, transport: 'NOT_VERIFIED', errors: []};
  const jobs = [], preparations = [], timers = new Set(); let finished = false, globalExpired = false, startedCount = 0;
  const later = (fn, ms) => { let id; id = clock.setTimer(() => { timers.delete(id); if (!finished) fn(); }, Math.max(0, ms)); timers.add(id); return id; };
  const clear = id => { if (timers.delete(id)) clock.clearTimer(id); };
  const error = (code, board, stage, detail) => {
    if (finished || result.errors.length >= 128) return;
    const e = {code, board, stage, detail: String(detail).slice(0, 512)}; result.errors.push(e); if (board) result.boards[board].errors.push(e);
  };
  for (const board of binding.activeBoards) {
    const b = binding.boards[board]; result.boards[board] = {target: b.target, role: b.role, token: b.token, launchArgv: [...b.launchArgv], launchStatus: 'NOT_STARTED',
      collector: {ready: false, hdcExit: null, signal: null, remoteExit: null, remoteExitCount: 0, termination: termination(), captures: {}},
      wrapperIdentity: null, nativeIdentity: null, readyAt: null,
      probe: {hdcExit: null, signal: null, remoteExit: null, remoteExitCount: 0, terminal: null, events: [], tokenRestoreObserved: false, termination: termination(), captures: {}}, errors: []};
  }
  let globalResolve;
  const globalDone = new Promise(resolve => { globalResolve = resolve; });
  later(() => {
    globalExpired = true; error('OVERALL_TIMEOUT', null, 'overall', '45000ms observation envelope; outstanding host children=' + jobs.filter(j => !j.closed).map(j => j.child?.pid ?? 'unknown').join(','));
    for (const job of jobs) job.stop('OVERALL_TIMEOUT', true); globalResolve();
  }, 45000);
  const bounded = async (promise, fallback) => {
    const value = await Promise.race([promise, globalDone.then(() => fallback)]); return value;
  };

  async function finishCapture(job, stream) {
    const acceptedHash = stream.hash.digest('hex'); let receipt;
    try { receipt = await bounded(Promise.resolve().then(() => stream.sink.finish()), null); }
    catch (e) { job.fail('CAPTURE_FINISH', e.message); }
    const shaped = receipt && typeof receipt.path === 'string' && receipt.path.length > 0 && receipt.path.length <= 4096 &&
      typeof receipt.sha256 === 'string' && /^[a-f0-9]{64}$/.test(receipt.sha256) &&
      Number.isSafeInteger(receipt.bytes) && receipt.bytes >= 0 && receipt.bytes <= stream.bytes &&
      typeof receipt.complete === 'boolean' && (receipt.error === null || typeof receipt.error === 'string');
    if (shaped && receipt.complete && receipt.error === null && receipt.bytes === stream.bytes && receipt.sha256 === acceptedHash) {
      job.record.captures[stream.name] = {path: receipt.path, sha256: receipt.sha256, bytes: receipt.bytes,
        complete: !stream.bad && stream.ended, error: stream.bad ? 'stream rejected; accepted prefix only' : null};
    } else if (shaped && receipt.complete === false) {
      // The sink owns the actual persisted-prefix receipt. Accepted bytes can
      // exceed disk bytes; never substitute their hash/length for disk evidence.
      const detail = String(receipt.error || 'incomplete final flush').slice(0, 512);
      job.record.captures[stream.name] = {path: receipt.path, sha256: receipt.sha256, bytes: receipt.bytes, complete: false, error: detail};
      job.fail('CAPTURE_INCOMPLETE', detail);
    } else {
      // No trustworthy file receipt: even an empty-file hash would invent a
      // persisted byte count. Keep the accepted count diagnostic only.
      const detail = `unconfirmed persisted capture; accepted=${stream.bytes}; missing/malformed final receipt`;
      job.record.captures[stream.name] = {path: null, sha256: null, bytes: null, complete: false, error: detail};
      job.fail('CAPTURE_INCOMPLETE', detail);
    }
  }

  function startJob(board, kind, command, subject = null) {
    const dto = result.boards[board], record = kind === 'audit' ? {hdcExit: null, signal: null, remoteExit: null, remoteExitCount: 0, captures: {}} : dto[kind];
    const job = {board, kind, command, subject, record, child: null, closed: false, exited: false, bad: false, ready: false,
      identity: null, auditRecord: null, startedMono: clock.monotonicNowMs(), streams: {}, readyResolve: null, doneResolve: null, closing: false};
    job.readyPromise = new Promise(resolve => { job.readyResolve = resolve; });
    job.done = new Promise(resolve => { job.doneResolve = resolve; });
    job.fail = (code, detail) => { job.bad = true; error(code, board, kind, detail); job.readyResolve(false); };
    const parser = kind === 'probe' ? nativeParser(binding, board, dto) : null;
    function parse(line) {
      let m;
      if (kind === 'audit') {
        if (line.startsWith('LC_AUDIT_REMOTE_EXIT')) {
          m = /^LC_AUDIT_REMOTE_EXIT run_id=(\S+) board=([AB]) kind=(probe|collector) rc=(-?[0-9]+)$/.exec(line);
          need(m && m[1] === binding.runId && m[2] === board && m[3] === subject && ++record.remoteExitCount === 1, 'audit remote exit'); record.remoteExit = int(m[4]);
        } else if (line.startsWith('LC_AUDIT ')) {
          m = new RegExp(`^LC_AUDIT run_id=(\\S+) board=([AB]) kind=(probe|collector) pid=([0-9]+) boot_id=(${BOOT}) state=(ABSENT|PRESENT) start_ticks=([0-9]+)$`).exec(line);
          need(m && m[1] === binding.runId && m[2] === board && m[3] === subject && !job.auditRecord, 'audit identity record');
          job.auditRecord = {pid: uint(m[4]), bootId: m[5], state: m[6], startTicks: uint(m[7])};
          need((m[6] === 'ABSENT') === (m[7] === '0'), 'audit state/ticks');
        } else if (/^LC_/.test(line)) throw Error('malformed reserved audit record');
        return;
      }
      const marker = kind === 'probe' ? 'LC_WRAPPER_START' : 'LC_COLLECTOR_START';
      if (line.startsWith(marker)) {
        m = new RegExp(`^${marker} run_id=(\\S+) board=([AB]) pid=([0-9]+) boot_id=(${BOOT}) start_ticks=([0-9]+)$`).exec(line);
        need(m && m[1] === binding.runId && m[2] === board && !job.identity && uint(m[3]) !== '0' && uint(m[5]) !== '0', 'wrapper identity');
        job.identity = {pid: m[3], bootId: m[4], startTicks: m[5]}; if (kind === 'probe') dto.wrapperIdentity = {...job.identity}; return;
      }
      const exitMarker = kind === 'probe' ? 'LC_REMOTE_EXIT' : 'LC_COLLECTOR_REMOTE_EXIT';
      if (line.startsWith(exitMarker)) {
        m = new RegExp(`^${exitMarker} run_id=(\\S+) board=([AB]) rc=(-?[0-9]+)$`).exec(line);
        need(m && m[1] === binding.runId && m[2] === board && ++record.remoteExitCount === 1, 'remote exit grammar/duplicate');
        need(kind !== 'probe' || record.terminal, 'remote exit precedes full native terminal'); record.remoteExit = int(m[3]); return;
      }
      if (kind === 'collector') {
        if (line.startsWith('LC_COLLECTOR_READY')) {
          need(line === `LC_COLLECTOR_READY run_id=${binding.runId} board=${board}` && job.identity && !record.ready, 'collector readiness'); record.ready = true;
        } else if (/^LC_/.test(line)) throw Error('malformed reserved collector record');
      } else {
        need(!record.remoteExitCount || !/^(scope=|run_id=|stage=|event=|result=|termination=|LC_)/.test(line), 'native record after wrapper remote exit');
        parser.line(line);
      }
    }
    function finishStream(stream) {
      if (stream.ended) return; stream.ended = true;
      try { stream.pending += stream.decoder.decode(); need(stream.pending.length === 0, 'unterminated final line'); }
      catch (e) { stream.bad = true; job.fail('STREAM_END', e.message); }
    }
    function data(stream, bytes) {
      if (job.closed || job.closing || stream.ended || stream.bad) return;
      if (!Buffer.isBuffer(bytes)) { stream.bad = true; job.fail('NON_BYTE_STREAM', 'process stream must supply bytes'); return; }
      const allowed = Math.max(0, stream.cap - stream.bytes), accepted = bytes.subarray(0, allowed);
      if (accepted.length) {
        try { stream.sink.append(accepted); stream.hash.update(accepted); stream.bytes += accepted.length; }
        catch (e) { stream.bad = true; job.fail('CAPTURE_WRITE', e.message); return; }
      }
      if (bytes.length > allowed) { stream.bad = true; job.fail('STREAM_CAP', 'accepted prefix only'); return; }
      try {
        stream.pending += stream.decoder.decode(accepted, {stream: true});
        let index; while ((index = stream.pending.indexOf('\n')) !== -1) {
          const raw = stream.pending.slice(0, index); stream.pending = stream.pending.slice(index + 1);
          need(Buffer.byteLength(raw) <= 65536, 'line byte cap');
          if (stream.name === 'stdout') { try { parse(raw.endsWith('\r') ? raw.slice(0, -1) : raw); } catch (e) { job.fail('RECORD', e.message); } }
        }
        need(Buffer.byteLength(stream.pending) <= 65536, 'pending line byte cap');
      } catch (e) { stream.bad = true; job.fail('STREAM_DECODE', e.message); }
      // Entire delivered chunk has now been inspected. No B start from a
      // substring preceding an already-delivered duplicate/terminal/error.
      if (!job.bad && !job.exited && !job.closed && !record.remoteExitCount &&
        (kind === 'collector' ? record.ready : kind === 'probe' && parser.ready())) {
        job.ready = true; if (kind === 'probe') dto.readyAt = stamp(); job.readyResolve(true);
      }
    }
    async function closeJob(forced = false) {
      if (job.closing) return; job.closing = true; job.closed = !forced || job.closed; clear(job.deadline);
      job.readyResolve(false);
      if (forced) job.fail('LOCAL_DRAIN', 'local child close/stream drain unconfirmed; host pid=' + (job.child?.pid ?? 'unknown'));
      for (const s of Object.values(job.streams)) if (!s.ended) { s.bad = true; job.fail('MISSING_STREAM_END', s.name); }
      try {
        if (kind === 'probe') { parser.finish(); need(record.remoteExitCount === 1 && record.remoteExit === (record.terminal.result === 'NOT_EXERCISED' ? 2 : 0), 'probe remote status'); }
        else if (kind === 'collector') need(record.ready && record.remoteExitCount === 1 && record.remoteExit === 124, 'collector finite capture status');
        else need(job.auditRecord && record.remoteExitCount === 1 && record.remoteExit === 0, 'audit completion');
        need(record.hdcExit === 0 && record.signal === null, 'local HDC rc/signal');
      } catch (e) { job.fail('COMPLETION', e.message); }
      const captures = Object.values(job.streams).map(s => finishCapture(job, s));
      const audited = kind === 'audit' ? Promise.resolve() : auditJob(job);
      await Promise.all([...captures, audited]); job.doneResolve(job);
    }
    job.stop = (reason, immediate = false) => {
      if (job.closing) return; job.fail(reason, 'bounded local cancellation; remote termination requires identity audit');
      try { job.child?.kill(); } catch (e) { job.fail('LOCAL_KILL', e.message); }
      if (immediate) void closeJob(true); else later(() => void closeJob(true), 250);
    };
    function discardPreparation() {
      // No remote task exists, but successfully opened local sinks still own
      // resources. Finish each once under the SAME overall budget, with no audit.
      job.readyResolve(false);
      preparations.push(Promise.all(Object.values(job.streams).map(s => {
        s.ended = true; return finishCapture(job, s);
      })));
      return null;
    }
    // Sinks open before final admission: no file work intervenes between the
    // final caller-owned guard/clock check and the actual process start.
    try {
      for (const name of ['stdout', 'stderr']) {
        job.streams[name] = {name, cap: kind === 'probe' ? 1048576 : kind === 'collector' ? 8388608 : 131072,
          bytes: 0, hash: crypto.createHash('sha256'), decoder: new TextDecoder('utf-8', {fatal: true}), pending: '', ended: false, bad: false,
          sink: capturePort.open({board, kind: kind === 'audit' ? subject + '-audit' : kind, stream: name})};
      }
    } catch (e) { job.fail('CAPTURE_OPEN', e.message); return discardPreparation(); }
    if (kind !== 'audit') {
      let admission;
      try { admission = recheckAdmission({kind, board}); } catch (e) { admission = {ok: false, reason: e.message}; }
      const ok = admission?.ok === true && Number.isFinite(admission.remainingMs) && admission.remainingMs >= 10000;
      result.timing.admissions.push({kind, board, checkedAt: admission?.checkedAt ?? null, remainingMs: admission?.remainingMs ?? null,
        outcome: ok ? 'ACCEPTED' : 'REFUSED', reason: ok ? null : String(admission?.reason || 'remaining admission reserve below10000ms').slice(0, 512)});
      if (!ok) { job.fail('ADMISSION_REFUSED', admission?.reason || 'remaining reserve'); return discardPreparation(); }
    }
    // Readiness promises are one-shot: later exit records cannot revoke them.
    // Recheck every required collector after the caller's final admission hook.
    if (kind === 'probe' && !binding.activeBoards.every(required => jobs.some(j =>
      j.kind === 'collector' && j.board === required && j.ready && !j.bad &&
      !j.closing && !j.exited && !j.closed && j.record.remoteExitCount === 0))) {
      job.fail('COLLECTOR_INACTIVE', 'required collector is no longer ready and active');
      return discardPreparation();
    }
    try { job.child = processPort.start({kind, board, target: dto.target, command}); }
    catch (e) { job.fail('SPAWN_THROW', e.message); return discardPreparation(); }
    ++startedCount; jobs.push(job); if (kind === 'probe') dto.launchStatus = 'STARTED';
    record.termination = kind === 'audit' ? undefined : {status: 'UNCONFIRMED', identity: null, checkedAt: null, audit: null};
    const child = job.child;
    for (const name of ['stdout', 'stderr']) {
      const s = job.streams[name];
      child[name].on('data', bytes => data(s, bytes)); child[name].on('end', () => finishStream(s));
      child[name].on('error', e => { s.bad = true; job.fail('STREAM_ERROR', e.message); });
    }
    child.on('error', e => { job.fail('PROCESS_ERROR', e.message); job.exited = true; });
    child.on('exit', (code, signal) => { if (job.closing) return; job.exited = true; record.hdcExit = code; record.signal = signal; job.readyResolve(false); });
    child.on('close', (code, signal) => { if (job.closing) return; job.closed = true; job.exited = true; record.hdcExit = code; record.signal = signal; void closeJob(); });
    job.deadline = later(() => job.stop('LOCAL_TIMEOUT'), kind === 'probe' ? 15000 : kind === 'collector' ? 18000 : 5000);
    return job;
  }

  async function auditJob(job) {
    const term = job.record.termination; term.identity = job.identity ? {...job.identity} : null;
    if (!job.identity || globalExpired) { term.status = 'UNCONFIRMED'; error('AUDIT_IDENTITY', job.board, job.kind, 'missing wrapper identity or overall budget'); return; }
    // On local stream failure, wait at least the original remote watchdog
    // horizon before observing absence. Neither a local kill nor its RC is proof.
    if (!job.closed) {
      const remain = (job.kind === 'probe' ? 12000 : 16000) - (clock.monotonicNowMs() - job.startedMono);
      if (remain > 0) await bounded(new Promise(resolve => later(resolve, remain)), null);
    }
    if (globalExpired) return;
    const command = auditCommand(binding, job.board, job.kind, job.identity);
    const audit = startJob(job.board, 'audit', command, job.kind);
    if (!audit) { error('AUDIT_START', job.board, job.kind, 'audit could not start'); return; }
    await audit.done; const a = audit.auditRecord;
    term.checkedAt = stamp(); term.audit = {command, hdcExit: audit.record.hdcExit, remoteExit: audit.record.remoteExit, captures: audit.record.captures};
    if (audit.bad || !a || a.pid !== job.identity.pid) term.status = 'UNCONFIRMED';
    else if (a.bootId !== job.identity.bootId) term.status = 'BOOT_CHANGED';
    else if (a.state === 'ABSENT') term.status = 'GONE';
    else if (a.startTicks !== job.identity.startTicks) term.status = 'GONE_PID_REUSED';
    else term.status = 'ALIVE';
    if (!['GONE', 'GONE_PID_REUSED'].includes(term.status)) error('TERMINATION', job.board, job.kind, term.status);
  }

  try {
    const collectors = [];
    for (const board of binding.activeBoards) { if (result.errors.length) break; const j = startJob(board, 'collector', commandFor(binding, board, 'collector')); if (j) collectors.push(j); }
    if (collectors.length === binding.activeBoards.length && !result.errors.length) {
      let readyTimer;
      const allReady = await Promise.race([Promise.all(collectors.map(j => j.readyPromise)).then(a => a.every(Boolean)),
        new Promise(resolve => { readyTimer = later(() => { error('COLLECTOR_READY_TIMEOUT', null, 'startup', '3000ms total'); resolve(false); }, 3000); })]);
      clear(readyTimer);
      if (allReady && !result.errors.length && collectors.every(j => !j.exited && !j.closed)) {
        const board = binding.activeBoards[0], a = startJob(board, 'probe', commandFor(binding, board, 'probe'));
        if (a && binding.profile === 'lifecycle-callback-close-v1') {
          const ready = await a.readyPromise;
          if (ready && !result.errors.length && !a.bad && !a.exited && !a.closed && !a.record.terminal)
            startJob('B', 'probe', commandFor(binding, 'B', 'probe'));
        }
      }
    }
    // Each non-audit job owns its bounded audit and final capture flush.
    await Promise.all(jobs.filter(j => j.kind !== 'audit').map(j => j.done));
  } catch (e) { error('HARNESS_ERROR', null, 'harness', e.message); for (const j of jobs) j.stop('HARNESS_ERROR', true);
    await bounded(Promise.all(jobs.map(j => j.done)), null); }
  // Failed audit preparation may be added while its parent task completes.
  // Await it only after all launched parent tasks have reached their endpoint.
  await Promise.all(preparations);
  result.executionObservation = result.errors.length ? (startedCount ? 'FAIL' : 'NOT_RUN') :
    Object.values(result.boards).some(b => b.probe.terminal?.result === 'NOT_EXERCISED') ? 'NOT_EXERCISED' :
      Object.values(result.boards).every(b => b.launchStatus === 'STARTED' && b.probe.terminal) ? 'COMPLETE' : (startedCount ? 'FAIL' : 'NOT_RUN');
  result.timing.finishedAt = stamp(); result.timing.elapsedMs = Math.max(0, clock.monotonicNowMs() - startedMono);
  finished = true; for (const id of timers) clock.clearTimer(id); timers.clear();
  // No unresolved child may retain live parsing callbacks into the frozen DTO.
  for (const j of jobs) { j.child?.stdout?.removeAllListeners(); j.child?.stderr?.removeAllListeners(); j.child?.removeAllListeners();
    j.child?.on('error', absorbLateError); j.child?.stdout?.on('error', absorbLateError); j.child?.stderr?.on('error', absorbLateError);
    if (!j.closed) { j.child?.stdout?.destroy?.(); j.child?.stderr?.destroy?.(); j.child?.unref?.(); } }
  return freeze(result);
}

module.exports = {runLifecycleProbeHarness};
