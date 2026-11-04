'use strict';
const assert = require('node:assert/strict');
const {createNativeWaitObserver} = require('../scripts/NativeWaitObserver.cjs');
const selected = 'event=selected_request run_id=wait_bytes6k profile=bytes pid=6126 invocation_id=wait_bytes6k:6126:190 sequence=190 send_enter_us=945122 shutdown_request_us=1145235 sender_tid=6135 shutdown_tid=6134 still_in_flight_at_selection=1 clock_scope=LOCAL_ONLY\r\n';
const preparing = 'event=first_send_preparing run_id=wait_raw profile=raw role=client pid=6164 tid=6178 steady_us=38700 socket=1 code=0 sequence=0\n';
function state(first = false) {
  const seen = [], errors = [];
  return {seen, errors, parser: createNativeWaitObserver({onObservation: x => seen.push(x),
    onError: x => errors.push(x), observeFirstSend: first})};
}
const tests = [
  ['OBS-01 real selected line uses word fields, not escaped regexp literals', () => {
    const s = state(); s.parser.push('B', selected);
    assert.equal(s.seen.length, 1); assert.equal(s.seen[0].pid, 6126); assert.equal(s.seen[0].tid, 6135);
    assert.equal(s.seen[0].event, 'selected_request');
  }],
  ['OBS-02 every possible HDC split is reconstructed', () => {
    for (let i = 1; i < selected.length; ++i) {
      const s = state(); s.parser.push('B', selected.slice(0,i)); s.parser.push('B', selected.slice(i));
      assert.equal(s.seen.length, 1, `split ${i}`);
    }
  }],
  ['OBS-03 RAW sampling starts without cancellation selection', () => {
    const s = state(true); for (const character of preparing) s.parser.push('B', character);
    assert.equal(s.seen.length, 1); assert.equal(s.seen[0].tid, 6178);
    assert.equal(s.seen[0].event, 'first_send_preparing');
  }],
  ['OBS-04 duplicate and wrong-role events do not schedule extra captures', () => {
    const s = state(true); s.parser.push('A', preparing); s.parser.push('B', preparing + preparing);
    assert.equal(s.seen.length, 1);
    const b = state(); b.parser.push('B', preparing); assert.equal(b.seen.length, 0);
  }],
  ['OBS-05 malformed process identifiers never become shell arguments', () => {
    for (const bad of ['0', '-1', '6126;id', '2147483648', '1e3']) {
      const s = state(); s.parser.push('B', selected.replace('pid=6126 ', `pid=${bad} `));
      assert.equal(s.seen.length, 0); assert.equal(s.errors.length, 1);
    }
  }],
  ['OBS-06 oversized/truncated evidence is reported, not silently discarded', () => {
    const s = state(); s.parser.push('B', 'x'.repeat(65537)); assert.equal(s.errors.length, 1);
    s.parser.push('B', '\n' + selected.trimEnd()); s.parser.finish(); assert.equal(s.seen.length, 1);
  }],
  ['OBS-07 ordinary diagnostics do not generate a sampling event', () => {
    const s = state(true); s.parser.push('B', 'result=NOT_EXERCISED pid=6126 sender_tid=6135\n');
    assert.equal(s.seen.length, 0); assert.equal(s.errors.length, 0);
  }],
  ['OBS-08 noisy input cannot request unbounded native captures', () => {
    const s = state();
    for (let pid = 100; pid < 110; ++pid) s.parser.push('B', selected.replace('pid=6126 ', `pid=${pid} `));
    assert.equal(s.seen.length, 4); assert.equal(s.errors.length, 6);
  }]
];
let failures = 0;
for (const [name, action] of tests) try { action(); console.log('PASS ' + name); }
  catch (error) { ++failures; console.log('FAIL ' + name + ': ' + error.message); }
process.exitCode = failures ? 1 : 0;
