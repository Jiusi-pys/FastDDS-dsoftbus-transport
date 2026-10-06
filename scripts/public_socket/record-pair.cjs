'use strict';

// Only runs caller-supplied, explicit HDC record commands. It performs no
// permission preflight/grant/revoke itself. Those remain a separate gated step.
function runRecordPair({spawn: injectedSpawn, executable, targets, commands,
  env = process.env, timeoutMs = 15000, drainGraceMs = 250,
  onOutput = () => {}, onTransition = () => {}, onStreamEnd = () => {}, onBeforeStart=()=>{},
  mainLineRegex = /^DDS_MAIN run=[0-9a-f]{16} side=A pid=[1-9][0-9]* mode=(dds) /}) {
  const spawn=injectedSpawn||require('node:child_process').spawn;
  // Legacy injected-event tests have no OS executable; real launches must be explicit.
  const program=executable||(injectedSpawn?'INJECTED_TEST_ONLY':null);
  if(!program||(!injectedSpawn&&!require('node:path').isAbsolute(program)))return Promise.reject(Error('Explicit collector executable required'));
  if (!targets?.A || !targets?.B || !commands?.A || !commands?.B ||
      !Number.isSafeInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 43200000 ||
      !Number.isSafeInteger(drainGraceMs) || drainGraceMs < 1 || drainGraceMs > 1000) {
    return Promise.reject(new Error('invalid explicit record run configuration'));
  }
  const childEnv = Object.fromEntries(Object.entries(env).filter(([k]) => k.toLowerCase() !== 'path'));
  childEnv.Path = env.Path || env.PATH || '';
  return new Promise(resolve => {
    const states = {};let eventSequence=0;const emit=e=>onTransition({...e,sequence:++eventSequence,observer_pid:process.pid,host_monotonic_ns:String(process.hrtime.bigint())});
    let done = false, timedOut = false, timer, drainTimer;
    function complete(force = false) {
      if (done || (!force && (!states.A?.collectionEnded || (states.B && !states.B.collectionEnded)))) return;
      done = true; clearTimeout(timer); clearTimeout(drainTimer);
      const valid = s => s && s.closed && !s.error && s.hdcExit === 0 && !s.signal && s.remoteExits.length === 1 && s.remoteExits[0] === 0;
      const summary = s => s ? {hdcExit:s.hdcExit, signal:s.signal,
        remoteExit:s.remoteExits.length === 1 ? s.remoteExits[0] : null,
        remoteExitCount:s.remoteExits.length, error:s.error || null,dispatchAttempted:s.dispatchAttempted===true,collectorPid:s.child?.pid??null,exited:s.exited,closed:s.closed,bytes:{...s.bytes},termination:s.termination?{...s.termination}:null,terminationOrigin:s.termination?'LOCAL_REQUEST_RECORDED':s.signal?'EXTERNAL_OR_UNOBSERVED_ORIGIN':'NONE_OBSERVED'} : null;
      const activeCollectors = Object.entries(states).filter(([,s]) => !s.closed)
        .map(([role,s]) => ({role, pid:s.child?.pid ?? null, child:s.child}));
      if (force) for(const [role,s] of Object.entries(states))if(!s.collectionEnded){s.collectionEnded=true;onStreamEnd(role,false);}
      for (const {child} of activeCollectors) {
        child?.unref?.(); child?.stdout?.unref?.(); child?.stderr?.unref?.();
      }
      resolve({pass:!timedOut && !!states.A.ready && valid(states.A) && valid(states.B),
        timedOut, A:summary(states.A), B:summary(states.B), activeCollectors,
        collectionCleanup:activeCollectors.length ? 'UNCONFIRMED' : 'HDC_STREAMS_CLOSED',
        remoteTermination:'NOT_INFERRED_FROM_HDC'});
    }
    function start(role) {
      const state = states[role] = {closed:false, exited:false,collectionEnded:false, ready:false, pending:'', remoteExits:[],bytes:{stdout:0,stderr:0,late:0}};
      emit({event:'START', role});
      let child;
      try {
        onBeforeStart(role,program,['-t',targets[role],'shell',commands[role]]);
        state.dispatchAttempted=true;
        child = spawn(program, ['-t', targets[role], 'shell', commands[role]],
          {env:childEnv, shell:false, windowsHide:true});
      } catch (error) {
        state.error = String(error); state.closed = true;state.collectionEnded=true;emit({event:'DISPATCH_REFUSED_OR_SPAWN_FAILED',role,error:state.error,dispatch_attempted:state.dispatchAttempted===true});onStreamEnd(role,false); complete(); return;
      }
      state.child = child;
      function line(text) {
        const exit = /^REMOTE_EXIT=(\d+)$/.exec(text);
        if (exit) {if(state.remoteExits.length<2)state.remoteExits.push(Number(exit[1]));else state.error='too many remote exit records';}
        if (role === 'A' && !state.closed && !state.exited && !state.error && !states.B && !timedOut &&
            mainLineRegex.test(text)) {
          state.ready = true;
          emit({event:'A_MAIN_WHILE_HDC_OPEN', role:'A'});
          start('B');
        }
      }
      child.stdout.on('data', data => {
        if(done||state.collectionEnded){state.bytes.late+=data.length;return;}
        state.bytes.stdout+=data.length;
        try{onOutput(role, 'stdout', data);}catch(e){if(!state.error){state.error=String(e);emit({event:'OUTPUT_ERROR',role,error:state.error});}}
        for(let chunkOffset=0;chunkOffset<data.length;chunkOffset+=4096){
        state.pending += data.subarray(chunkOffset,chunkOffset+4096).toString();
        for (;;) {
          const end = state.pending.indexOf('\n');
          if (end < 0) break;
          line(state.pending.slice(0,end).replace(/\r$/, ''));
          state.pending = state.pending.slice(end+1);
        }
        // A record probe emits small diagnostic lines. Unbounded/malformed
        // output is a harness failure, never an invitation to allocate forever.
        if (state.pending.length > 65536) { state.error='oversized unterminated output'; state.pending=''; }
        }
      });
      child.stderr.on('data', data => {if(done||state.collectionEnded){state.bytes.late+=data.length;return;}state.bytes.stderr+=data.length;try{onOutput(role,'stderr',data);}catch(e){if(!state.error){state.error=String(e);emit({event:'OUTPUT_ERROR',role,error:state.error});}}});
      child.on('error', error => {if(!state.error){state.error=String(error);emit({event:'COLLECTOR_ERROR',role,error:state.error});}});
      child.on('exit', (code, signal) => {
        state.exited=true;
        emit({event:'HDC_EXITED', role, code, signal});
        state.hdcExit=code;state.signal=signal;
        state.exitDrain=setTimeout(()=>{if(done||state.collectionEnded)return;state.error||='EXIT_DRAIN_INCOMPLETE';state.collectionEnded=true;state.pending='';onStreamEnd(role,false);emit({event:'EXIT_DRAIN_INCOMPLETE',role});complete();},drainGraceMs);
      });
      // close, not exit: stdout/stderr must have finished before parsing final RC.
      child.on('close', (code, signal) => {
        clearTimeout(state.exitDrain);
        state.closed = true;
        if(done||state.collectionEnded){emit({event:'LATE_CLOSE',role,code,signal,original_result:'NOT_UPGRADED'});return;}
        if (state.pending) line(state.pending.replace(/\r$/, ''));
        state.collectionEnded=true;onStreamEnd(role,!state.error);
        state.hdcExit=code; state.signal=signal;
        emit({event:'HDC_CLOSED', role, code, signal});
        complete();
      });
    }
    // Explicit whole-run collection safety limit, including functional
    // multi-cycle experiments. Never evidence of a native cancellation bound.
    timer=setTimeout(() => {
      timedOut=true;
      emit({event:'HOST_COLLECTION_TIMEOUT'});
      // This stops only the local HDC collection processes. Caller commands
      // MUST separately bound remote Socket execution; no grant/revoke here.
      drainTimer=setTimeout(() => complete(true), drainGraceMs);
      for (const [role,state] of Object.entries(states)) if (!state.closed) {
        state.termination={requester:'record_pair_deadline',reason:'HOST_COLLECTION_TIMEOUT',target_pid:state.child?.pid??null,deadline_ms:timeoutMs,signal_requested:'SIGTERM',requested:true,result:null};
        try{emit({event:'TERMINATION_REQUEST',role,termination:{...state.termination}});}catch(error){state.error||='TERMINATION_RECEIPT_FAILED:'+String(error);}
        try { state.killRequested=state.child?.kill('SIGTERM');state.termination.result=Boolean(state.killRequested); }
        catch (error) { state.termination.kill_error=String(error);state.error||=String(error); }
        try{emit({event:'TERMINATION_RESULT',role,termination:{...state.termination}});}catch(error){state.error||='TERMINATION_RECEIPT_FAILED:'+String(error);}
      }
      complete();
    }, timeoutMs);
    start('A');
  });
}

module.exports = {runRecordPair};
