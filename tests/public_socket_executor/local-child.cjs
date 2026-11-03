'use strict';
// Local process/stdio simulator only. No HDC, SDK, network or device fallback.
const mode=process.argv[2];
if(mode==='tail')process.stdout.write('last-stage');
else if(mode==='stop'){process.stdout.write('STOP\n');setTimeout(()=>process.exit(0),15);}
else if(mode==='hang')setInterval(()=>{},1000);
else if(mode==='control')process.stdout.write('CONTROL_REMOTE_EXIT=0');
else if(mode==='error'){process.stderr.write('local failure\n');process.exitCode=7;}
else if(mode==='empty'){}
else if(mode==='load')process.stdout.write('LOAD_OBSERVER_REMOTE_EXIT=0');
else if(mode==='pair-success')process.stdout.write('REMOTE_EXIT=0');
else if(mode==='early-summary'){
    process.stdout.write('SHARED_RESULT run=0123456789abcdef side=0 cycles=10 q_deleted=0 success=0 first_failure=LOCAL_FAILURE\n');
    setImmediate(()=>process.stdout.write('SHARED_DELETE run=0123456789abcdef side=0 cycle=6 phase=FINAL group=Q life=1 entities_rc=0 participant_rc=0 egress_closed=1 egress_drained=1\n'));
}
else if(mode==='control-pipe'){
    require('node:child_process').spawn(process.execPath,[__filename,'pipe-holder'],{stdio:['ignore',process.stdout,'ignore'],windowsHide:true}).unref();
    process.stdout.write('CONTROL_REMOTE_EXIT=0\n');
}
else if(mode==='pipe'){
    require('node:child_process').spawn(process.execPath,[__filename,'pipe-holder'],{stdio:['ignore',process.stdout,'ignore'],windowsHide:true}).unref();
    process.stdout.write('DDS_MAIN run=0123456789abcdef side=A pid=10 mode=dds headers=LOCAL_SIM\nREMOTE_EXIT=0\n');
}
else if(mode==='pipe-holder')setTimeout(()=>{process.stdout.write('late descendant\n');},450);
else if(mode==='transcript'){
    const rows=JSON.parse(require('node:fs').readFileSync(process.argv[3],'utf8'));
    const side=process.argv[4];let index=0,stage,waitTimer;
    const emit=()=>{
        const block=rows[index++];if(!block)throw Error('Local transcript exhausted');
        for(const row of block){const b=Buffer.from(row+'\n');for(let i=0;i<b.length;i+=13)process.stdout.write(b.subarray(i,i+13));}
        stage=/stage=([^ ]+)$/.exec(block.at(-1))[1];process.stderr.write('local simulation diagnostic\n');
        if(stage.endsWith('DDS_DELETED')){process.stdout.write('REMOTE_EXIT=0');process.stdin.destroy();}
        else waitTimer=setTimeout(()=>{process.stderr.write('LOCAL_SIMULATED_STAGE_TIMEOUT '+stage+'\n');process.stdout.write('REMOTE_EXIT=5');process.stdin.destroy();},1500);
    };
    process.stdout.write(`DDS_MAIN run=0123456789abcdef side=${side} pid=${side==='A'?10:20} mode=dds headers=LOCAL_CONTROL_SIMULATION\n`);
    require('node:readline').createInterface({input:process.stdin}).on('line',line=>{if(line!==stage)throw Error('Wrong local phase release');clearTimeout(waitTimer);emit();});emit();
}
else throw Error('Unknown local-only mode');
