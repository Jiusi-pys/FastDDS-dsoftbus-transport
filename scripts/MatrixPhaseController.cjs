'use strict';
// Host control metadata only. This module never transports samples or RTPS.
function createMatrixController({run,caseId,release,abort=async()=>{},record=()=>{}}){
  if(!/^[0-9a-f]{16}$/.test(run)||!/^[A-Z][A-Z0-9_]*$/.test(caseId)||typeof release!=='function')throw Error('Invalid matrix control identity');
  const phases=['INITIAL','PRE','CHANGE','POST','OBSERVE','CLEANUP'];
  const gates=['PRE','CHANGE','POST','OBSERVE','CLEANUP'];
  const done={A:-1,B:-1},pending={A:'',B:''},pids={},orders={};
  const cleanupFailures=[],flight=[];
  let firstFailure=null,issued=-1,busy=false,aborted=false;
  function emit(event){try{record({...event,run,case:caseId});}catch(error){if(!firstFailure)firstFailure='CONTROL_RECORD '+String(error);}}
  function fail(reason){
    if(!firstFailure){firstFailure=reason;emit({event:'CONTROL_FIRST_FAILURE',reason});}
    if(!aborted){
      aborted=true;
      for(const side of ['A','B']){
        const task=Promise.resolve().then(()=>abort(side,`MATRIX_ABORT ${run} ${caseId}\n`)).catch(error=>{cleanupFailures.push('ABORT '+side+' '+String(error));});
        flight.push(task);
      }
    }
  }
  function pump(){
    if(firstFailure||busy||issued===gates.length-1)return;
    const next=issued+1;
    if(done.A<next||done.B<next)return;
    busy=true;issued=next;
    const phase=gates[next];
    emit({event:'CONTROL_RELEASE_BEGIN',phase,prerequisite:phases[next]});
    if(firstFailure){busy=false;fail(firstFailure);return;}
    const task=Promise.allSettled(['A','B'].map(side=>Promise.resolve().then(async()=>{
      await release(side,phase,`MATRIX_RELEASE ${run} ${caseId} ${phase}\n`);
      emit({event:'CONTROL_RELEASE_RETURN',side,phase,result:'PASS'});
    }))).then(results=>{
      for(let i=0;i<results.length;i++)if(results[i].status==='rejected')fail('CONTROL_RELEASE '+['A','B'][i]+' '+phase+' '+String(results[i].reason));
      busy=false;pump();
    });
    flight.push(task);
  }
  function consume(side,line){
    if(!line.startsWith('MATRIX '))return;
    const fields={};
    for(const token of line.slice(7).split(' ')){
      const at=token.indexOf('=');if(at<1)continue;
      const key=token.slice(0,at);if(Object.hasOwn(fields,key))throw Error('duplicate field '+key);
      fields[key]=token.slice(at+1);
    }
    if(!['PHASE_DONE','FIRST_FAILURE','CLEANUP_FAILURE'].includes(fields.event))return;
    if(fields.run!==run||fields.case!==caseId||fields.side!==side)throw Error('wrong run/case/side');
    if(!/^[1-9][0-9]*$/.test(fields.pid||''))throw Error('invalid PID');
    if(pids[side]&&pids[side]!==fields.pid)throw Error('PID changed');
    pids[side]=fields.pid;
    if(fields.event==='FIRST_FAILURE'){fail('BUSINESS '+side+' '+(fields.stage||'UNKNOWN')+' '+(fields.predicate||'UNKNOWN'));return;}
    if(fields.event==='CLEANUP_FAILURE'){cleanupFailures.push('BUSINESS_CLEANUP '+side+' '+(fields.predicate||'UNKNOWN'));return;}
    if(firstFailure)return; // Cleanup output cannot erase an earlier business failure.
    const index=phases.indexOf(fields.phase),order=Number(fields.order);
    if(index!==done[side]+1||index>issued+1)throw Error('phase order violation');
    if(!Number.isSafeInteger(order)||order<1||(orders[side]&&order<=orders[side]))throw Error('record order violation');
    orders[side]=order;done[side]=index;
    emit({event:'CONTROL_PHASE_OBSERVED',side,phase:fields.phase,pid:fields.pid,order});
    pump();
  }
  return {
    output(side,stream,data){
      if(stream!=='stdout'||!Object.hasOwn(pending,side))return;
      pending[side]+=data.toString();
      for(;;){const end=pending[side].indexOf('\n');if(end<0)break;
        const line=pending[side].slice(0,end).replace(/\r$/,'');pending[side]=pending[side].slice(end+1);
        try{consume(side,line);}catch(error){fail('CONTROL_INPUT '+side+' '+String(error));}
      }
      if(pending[side].length>65536){pending[side]='';fail('CONTROL_INPUT oversized unterminated output');}
    },
    async finish(){
      // New tasks may be scheduled while an earlier release finishes.
      for(let i=0;i<flight.length;i++)await flight[i];
      const complete=done.A===5&&done.B===5&&issued===4&&!busy;
      const pass=!firstFailure&&!cleanupFailures.length&&complete;
      return {pass,first_failure:firstFailure||(complete?null:'INCOMPLETE_MATRIX_PHASES'),cleanup_failures:cleanupFailures,
        observed_phases:done,pids,issued_gate_count:issued+1,control_scope:'METADATA_ONLY_NOT_DDS_PAYLOAD'};
    }
  };
}
module.exports={createMatrixController};
