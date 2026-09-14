// Executes the concrete probe entry/worker/exit path against an explicitly host-only boundary.
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const exe = process.argv[2];
if (!exe) throw new Error('Usage: node tests/softbus_lifecycle_subprocess_tests.cjs <host-fixture-exe>');
const common = ['--run-id','host-tdd','--local-name','com.kaihong.mdds.local',
  '--expected-peer-network-id','b'.repeat(64),'--expected-token','671793191','--preflight-confirmed'];
function run(fixture, mode='alias', extras=[]) {
  const role = mode==='alias' ? ['--single','--remote-listen-name','com.kaihong.mdds.peer','--peer-network-id','b'.repeat(64)] :
    ['--server','--expected-remote-initiator-name','com.kaihong.mdds.peer'];
  const r=spawnSync(exe,[...common,'--case',mode,...role,'--host-fixture',fixture,...extras],{encoding:'utf8',timeout:10000,windowsHide:true});
  assert.equal(r.error,undefined,`subprocess did not terminate: ${r.error}`);
  assert.equal(r.signal,null,'watchdog is not normal termination');
  return {rc:r.status,log:r.stdout+r.stderr};
}
const cases=[
  ['alias ownership uses actual call boundary',()=>{const r=run('normal');assert.equal(r.rc,0,r.log);assert.match(r.log,/socket_calls=2 shutdown_calls=1/);assert.match(r.log,/aliases=1/);assert.match(r.log,/scope=HOST_FIXTURE_NOT_DEVICE/);assert.match(r.log,/token_restored=1/);} ],
  ['different returned ID is not alias PASS',()=>{const r=run('different-id');assert.equal(r.rc,2,r.log);assert.match(r.log,/socket_calls=2 shutdown_calls=2/);assert.match(r.log,/NOT_EXERCISED/);} ],
  ['actual callback is active across close request',()=>{const r=run('normal','callback-close');assert.equal(r.rc,0,r.log);assert.match(r.log,/event=CallbackHeld/);assert.match(r.log,/event=CallbackReleased/);assert.match(r.log,/event=ShutdownStarted .*tid=[1-9][0-9]* .*socket=2/);assert.match(r.log,/active_callbacks=0/);assert.match(r.log,/sdk_race=REQUIRES_EXTERNAL_LOGS/);assert(r.log.indexOf('event=CallbackHeld')<r.log.indexOf('event=CloseRequested'));assert(r.log.indexOf('event=CloseRequested')<r.log.indexOf('event=CallbackReleased'));} ],
  ['worker token mismatch performs no Socket',()=>{const r=run('worker-token');assert.equal(r.rc,6,r.log);assert.match(r.log,/socket_calls=0 shutdown_calls=0/);assert.match(r.log,/reason=worker_token_mismatch/);assert.match(r.log,/token_restored=1/);} ],
  ['real blocked Shutdown survives close expiry and restores before nonzero _Exit',()=>{const r=run('blocked-close');assert.equal(r.rc,70,r.log);assert.match(r.log,/application_workers_joinable=1/);assert.match(r.log,/token_restored=1/);assert.match(r.log,/termination=NONZERO_PROCESS_EXIT_RETAINED/);assert(r.log.indexOf('stage=restore_token')<r.log.indexOf('termination=NONZERO_PROCESS_EXIT_RETAINED'));} ],
  ['thread creation failure after first owner still closes owner',()=>{const r=run('thread-failure');assert.equal(r.rc,6,r.log);assert.match(r.log,/socket_calls=1 shutdown_calls=1/);assert.match(r.log,/token_restored=1/);} ],
  ['close-thread creation failure while callback active retains state before exit',()=>{const r=run('close-thread-failure','callback-close');assert.equal(r.rc,70,r.log);assert.match(r.log,/reason=close_thread_creation_failed/);assert.match(r.log,/token_restored=1/);assert.match(r.log,/cleanup=UNCONFIRMED/);} ],
  ['event ledger overflow cannot produce observation success',()=>{const r=run('event-full','callback-close');assert.equal(r.rc,6,r.log);assert.match(r.log,/events=512/);assert.match(r.log,/reason=event_ledger_full/);assert.match(r.log,/active_callbacks=0/);assert.match(r.log,/token_restored=1/);} ],
  ['callback exception decrements active and closes',()=>{const r=run('callback-copy','callback-close');assert.equal(r.rc,6,r.log);assert.match(r.log,/active_callbacks=0/);assert.match(r.log,/reason=callback_exception/);assert.match(r.log,/token_restored=1/);} ],
  ['untracked callback cannot become successful cleanup',()=>{const r=run('unknown-callback','callback-close');assert.equal(r.rc,70,r.log);assert.match(r.log,/cleanup=UNCONFIRMED/);assert.match(r.log,/untracked_id=99/);assert.match(r.log,/token_restored=1/);} ],
  ['CLI duplicate rejected before identity selection',()=>{const r=run('normal','alias',['--expected-token','671793191']);assert.equal(r.rc,64,r.log);assert.doesNotMatch(r.log,/stage=select_token|socket_calls=/);} ],
];
let failed=0;for(const [name,test] of cases){try{test();console.log(`PASS ${name}`);}catch(e){++failed;console.error(`FAIL ${name}: ${e.stack}`);}}
console.log(`tests=${cases.length} failed=${failed} scope=HOST_SUBPROCESS_NOT_DEVICE`);process.exitCode=failed?1:0;
