'use strict';

// Only runs caller-supplied, explicit HDC record commands. It performs no
// permission preflight/grant/revoke itself. Those remain a separate gated step.
function runRecordPair({spawn = require('node:child_process').spawn, targets, commands,
  env = process.env, timeoutMs = 15000, drainGraceMs = 250,
  onOutput = () => {}, onTransition = () => {}}) {
  if (!targets?.A || !targets?.B || !commands?.A || !commands?.B ||
      !Number.isSafeInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 14400000 ||
      !Number.isSafeInteger(drainGraceMs) || drainGraceMs < 1 || drainGraceMs > 1000) {
    return Promise.reject(new Error('invalid explicit record run configuration'));
  }
  const childEnv = Object.fromEntries(Object.entries(env).filter(([k]) => k.toLowerCase() !== 'path'));
  childEnv.Path = env.Path || env.PATH || '';
  return new Promise(resolve => {
    const states = {};
    let done = false, timedOut = false, timer, drainTimer;
    function complete(force = false) {
      if (done || (!force && (!states.A?.closed || (states.B && !states.B.closed)))) return;
      done = true; clearTimeout(timer); clearTimeout(drainTimer);
      const valid = s => s && !s.error && s.hdcExit === 0 && s.remoteExits.length === 1 && s.remoteExits[0] === 0;
      const summary = s => s ? {hdcExit:s.hdcExit, signal:s.signal,
        remoteExit:s.remoteExits.length === 1 ? s.remoteExits[0] : null,
        remoteExitCount:s.remoteExits.length, error:s.error || null} : null;
      const activeCollectors = Object.entries(states).filter(([,s]) => !s.closed)
        .map(([role,s]) => ({role, pid:s.child?.pid ?? null, child:s.child}));
      resolve({pass:!timedOut && !!states.A.ready && valid(states.A) && valid(states.B),
        timedOut, A:summary(states.A), B:summary(states.B), activeCollectors,
        collectionCleanup:activeCollectors.length ? 'UNCONFIRMED' : 'HDC_STREAMS_CLOSED',
        remoteTermination:'NOT_INFERRED_FROM_HDC'});
    }
    function start(role) {
      const state = states[role] = {closed:false, exited:false, ready:false, pending:'', remoteExits:[]};
      onTransition({event:'START', role});
      let child;
      try {
        child = spawn('hdc', ['-t', targets[role], 'shell', commands[role]],
          {env:childEnv, shell:false, windowsHide:true});
      } catch (error) {
        state.error = String(error); state.closed = true; complete(); return;
      }
      state.child = child;
      function line(text) {
        const exit = /^REMOTE_EXIT=(\d+)$/.exec(text);
        if (exit) state.remoteExits.push(Number(exit[1]));
        if (role === 'A' && !state.closed && !state.exited && !state.error && !states.B && !timedOut &&
            /^stage=Listen action=RETURN\b.*\brc=0(?:\s|$)/.test(text)) {
          state.ready = true;
          onTransition({event:'A_READY_WHILE_HDC_OPEN', role:'A'});
          start('B');
        }
      }
      child.stdout.on('data', data => {
        onOutput(role, 'stdout', data);
        state.pending += data.toString();
        for (;;) {
          const end = state.pending.indexOf('\n');
          if (end < 0) break;
          line(state.pending.slice(0,end).replace(/\r$/, ''));
          state.pending = state.pending.slice(end+1);
        }
        // A record probe emits small diagnostic lines. Unbounded/malformed
        // output is a harness failure, never an invitation to allocate forever.
        if (state.pending.length > 65536) { state.error='oversized unterminated output'; state.pending=''; }
      });
      child.stderr.on('data', data => onOutput(role, 'stderr', data));
      child.on('error', error => { state.error=String(error); });
      child.on('exit', (code, signal) => {
        state.exited=true;
        onTransition({event:'HDC_EXITED', role, code, signal});
      });
      // close, not exit: stdout/stderr must have finished before parsing final RC.
      child.on('close', (code, signal) => {
        state.closed = true;
        if (state.pending) line(state.pending.replace(/\r$/, ''));
        state.hdcExit=code; state.signal=signal;
        onTransition({event:'HDC_CLOSED', role, code, signal});
        complete();
      });
    }
    // Explicit whole-run collection safety limit, including functional
    // multi-cycle experiments. Never evidence of a native cancellation bound.
    timer=setTimeout(() => {
      timedOut=true;
      onTransition({event:'HOST_COLLECTION_TIMEOUT'});
      // This stops only the local HDC collection processes. Caller commands
      // MUST separately bound remote Socket execution; no grant/revoke here.
      drainTimer=setTimeout(() => complete(true), drainGraceMs);
      for (const state of Object.values(states)) if (!state.closed) {
        try { state.killRequested=state.child?.kill(); }
        catch (error) { state.error=String(error); }
      }
      complete();
    }, timeoutMs);
    start('A');
  });
}

module.exports = {runRecordPair};
