'use strict';

// Read-only diagnostic trigger parsing. This is not a native-pressure oracle.
function createNativeWaitObserver({onObservation, onError = () => {}, observeFirstSend = false}) {
  if (typeof onObservation !== 'function') throw new TypeError('onObservation required');
  let pending = '', discard = false;
  const seen = new Set();
  const maximum = 65536;
  function line(text) {
    const fields = Object.create(null);
    const pieces = text.trim().split(/\s+/);
    const event = pieces[0];
    if (event !== 'event=selected_request' &&
        !(observeFirstSend && event === 'event=first_send_preparing')) return;
    for (const piece of pieces) {
      const split = piece.indexOf('=');
      if (split < 1) { onError('MALFORMED_TRIGGER'); return; }
      const key = piece.slice(0, split);
      if (Object.hasOwn(fields, key)) { onError('DUPLICATE_TRIGGER_FIELD'); return; }
      fields[key] = piece.slice(split + 1);
    }
    const pid = Number(fields.pid);
    const tidText = fields.event === 'selected_request' ? fields.sender_tid : fields.tid;
    const tid = Number(tidText);
    const validId = (raw, number) => typeof raw === 'string' && /^[0-9]+$/.test(raw) &&
      Number.isInteger(number) && number > 0 && number <= 2147483647;
    if (!validId(fields.pid, pid) || !validId(tidText, tid)) { onError('INVALID_PROCESS_ID'); return; }
    const key = `${fields.event}:${pid}:${tid}`;
    if (seen.has(key)) return;
    if (seen.size === 4) { onError('TRIGGER_BUDGET_EXCEEDED'); return; }
    seen.add(key);
    onObservation(Object.freeze({event: fields.event, pid, tid, runId: fields.run_id,
      invocationId: fields.invocation_id, profile: fields.profile}));
  }
  function push(role, text) {
    if (role !== 'B') return;
    if (typeof text !== 'string') throw new TypeError('decoded stdout text required');
    let offset = 0;
    while (offset < text.length) {
      const newline = text.indexOf('\n', offset);
      const end = newline < 0 ? text.length : newline;
      if (discard) {
        if (newline >= 0) discard = false;
      } else if (pending.length + end - offset > maximum) {
        pending = ''; discard = newline < 0; onError('OVERSIZED_TRIGGER_LINE');
      } else {
        pending += text.slice(offset, end);
        if (newline >= 0) { line(pending); pending = ''; }
      }
      offset = newline < 0 ? text.length : newline + 1;
    }
  }
  return {push, finish() { if (pending) line(pending); pending = ''; discard = false; }};
}
module.exports = {createNativeWaitObserver};
