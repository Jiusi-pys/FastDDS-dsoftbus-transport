'use strict';

// Internal closed-profile core. Public wrappers expose only record or lifecycle.
// Pure, fail-closed consistency check for Main's already captured JSON evidence.
// NO IO, process launch, clock read, hash computation or permission operation.
// This is neither authentication nor a lock: the caller must independently gather
// real raw results, pin expected inputs, exclude concurrent writers, then validate
// immediately before launch. Never use the evidence itself as trusted `expected`.
// DTO shapes: tests/record_run_binding_tests.cjs and run_binding_fixtures.cjs.
// LC schema2 has only alias-single(A|B) and callback-close(A server/B client).
// Provider identity is the requested path's lstat; resolvedPath is separately
// observed. Raw paths/hashes are compared, never read or authenticated here.
// Times are strict
// UTC ISO strings with milliseconds; nowMs is explicitly supplied epoch millis.
// maxAgeMs <= 120000 is an experimental observation budget, not a product SLA.
// Historic origin/ownership/pre-grant snapshots are hash-bound, NOT age-limited.
// Return: frozen {ok:false,errors:[field paths]} OR frozen {ok:true,errors:[],
// binding:{runId,validatedAt,boards:{A/B:{target,token,record,launchArgv}}}}.
// LC additionally returns profile/start/expiry/activeBoards/fixed observation
// budgets; its board has role/probe/provider/loader/rawCaptures, not record.
// Result contains detached copies. Malformed JSON-like DTOs return failures;
// accessors/proxies/cycles are not supported input, and exceptions fail closed.

const FIXED = Object.freeze({
  A: Object.freeze({target: '3e01ff55454d202020104033bf453b00', token: 671793191}),
  B: Object.freeze({target: '3e01ff55454d202020104433991c3b00', token: 671320465})
});
const MANAGER_SHA = 'd01365fc20d70206a1ddeb32c5c278642125f5c1eb49f80754b2d1c8350a172b';
const PROCESS_NAME = 'fastdds_dsoftbus_probe';
const PERMISSION = 'ohos.permission.DISTRIBUTED_DATASYNC';
const SOURCE_PATH = '/data/service/el0/access_token/nativetoken.json';
const METADATA_KEYS = ['kind', 'isSymlink', 'mode', 'uid', 'gid', 'selinux', 'size', 'inode', 'mtimeSeconds', 'ctimeSeconds'];
const PROVIDER_PATH = '/system/lib64/platformsdk/libsoftbus_client.z.so';
const LIBRARY_PATH = '/system/lib64/chipset-sdk-sp:/system/lib64/platformsdk:/system/lib64/chipset-sdk';
const LC_ALIAS = 'lifecycle-alias-v1', LC_PAIR = 'lifecycle-callback-close-v1';
const RAW_KEYS = ['manager', 'pidAudit', 'metadata', 'probe', 'provider'];
const LC_BUDGETS = Object.freeze({runMs: 7000, closeMs: 3000, sendMs: 1000,
  holdMs: 500, quietMs: 200, externalWatchdogMs: 10000});

function validateBinding(evidence, expected, nowMs, lifecycle) {
  const errors = [];
  const check = (condition, field) => { if (!condition && errors.length < 64) errors.push(field); };
  const fail = () => Object.freeze({ok: false, errors: Object.freeze([...errors])});
  const obj = value => value && typeof value === 'object' && !Array.isArray(value) ? value : {};
  const integer = value => Number.isSafeInteger(value) && value >= 0;
  const hex = value => typeof value === 'string' && /^[0-9a-fA-F]{64}$/.test(value);
  const sameHash = (a, b) => hex(a) && hex(b) && a.toLowerCase() === b.toLowerCase();
  const network = value => typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
  const sameArray = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length &&
    b.every((x, i) => Object.hasOwn(a, i) && a[i] === x);
  const remotePath = value => typeof value === 'string' && /^\/data\/local\/tmp\/[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)*$/.test(value) &&
    !value.split('/').some(x => x === '.' || x === '..');
  const localPath = value => typeof value === 'string' && /^\.artifacts\/[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)*$/.test(value) &&
    !value.split('/').some(x => x === '.' || x === '..');
  const absolutePath = value => typeof value === 'string' && /^\/[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)*$/.test(value) &&
    !value.split('/').some(x => x === '.' || x === '..');
  const exactKeys = (value, keys) => sameArray(Object.keys(obj(value)).sort(), [...keys].sort());
  const frozenArtifact = value => Object.freeze({path: value.path, sha256: value.sha256.toLowerCase()});
  const stamp = value => {
    if (typeof value !== 'string' || !/^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$/.test(value)) return NaN;
    const millis = Date.parse(value);
    return Number.isFinite(millis) && new Date(millis).toISOString() === value ? millis : NaN;
  };
  try {
    const wanted = obj(expected), actual = obj(evidence);
    const profile = lifecycle ? wanted.profile : 'record-v1';
    const activeBoards = lifecycle && profile === LC_ALIAS ? [wanted.selectedBoard] : ['A', 'B'];
    const start = stamp(wanted.startedAt);
    check(integer(nowMs) && Number.isFinite(start), 'time.now/start');
    check(integer(wanted.maxAgeMs) && wanted.maxAgeMs > 0 && wanted.maxAgeMs <= 120000, 'expected.maxAgeMs');
    check(nowMs >= start && nowMs - start <= wanted.maxAgeMs, 'time.runFreshness');
    check(typeof wanted.runId === 'string' && (lifecycle ? /^[0-9]{8}T[0-9]{9}Z-[A-Za-z0-9_-]{1,44}$/ :
      /^[0-9]{8}T[0-9]{9}Z-[A-Za-z0-9_-]{1,80}$/).test(wanted.runId) &&
      wanted.runId.startsWith(String(wanted.startedAt).replace(/[-:.]/g, '') + '-'), 'expected.runId');
    check(actual.schemaVersion === (lifecycle ? 2 : 1) && actual.runId === wanted.runId && actual.startedAt === wanted.startedAt &&
      (lifecycle || ![wanted.profile, actual.profile].some(p => typeof p === 'string' && p.startsWith('lifecycle-'))), 'run.binding');
    check(sameArray(Object.keys(obj(wanted.boards)).sort(), activeBoards), 'expected.boards');
    check(sameArray(Object.keys(obj(actual.boards)).sort(), activeBoards), 'evidence.boards');
    if (lifecycle) {
      check((profile === LC_ALIAS || profile === LC_PAIR) && actual.profile === profile, 'run.profile');
      check(wanted.scope === 'NATIVE' && actual.scope === 'NATIVE', 'run.scope');
      check(profile !== LC_ALIAS || (['A', 'B'].includes(wanted.selectedBoard) &&
        actual.selectedBoard === wanted.selectedBoard), 'run.selectedBoard');
      const extra = profile === LC_ALIAS ? ['selectedBoard'] : [];
      check(exactKeys(wanted, ['profile', 'scope', 'runId', 'startedAt', 'maxAgeMs', 'boards', ...extra]), 'expected.fields');
      check(exactKeys(actual, ['schemaVersion', 'profile', 'scope', 'runId', 'startedAt', 'boards', ...extra]), 'evidence.fields');
    }
    if (errors.length) return fail();

    const timed = (value, label) => {
      const time = stamp(value);
      check(time >= start && time <= nowMs && nowMs - time <= wanted.maxAgeMs, label);
      return time;
    };
    const artifact = (a, e, label, pathCheck) => {
      check(pathCheck(e.path) && a.path === e.path && sameHash(a.sha256, e.sha256), label);
    };
    const boards = {};
    for (const board of activeBoards) {
      const e = obj(wanted.boards[board]), a = obj(actual.boards[board]), fixed = FIXED[board];
      const artifactName = lifecycle ? 'probe' : 'record';
      const r = obj(a[artifactName]), er = obj(e[artifactName]), m = obj(a.manager), em = obj(e.manager);
      check(e.target === fixed.target && e.token === fixed.token && a.target === fixed.target &&
        a.token === fixed.token && a.board === board, `${board}.identityBinding`);
      artifact(r, er, `${board}.${artifactName}`, remotePath);
      const artifactTime = timed(r.checkedAt, `${board}.${artifactName}.checkedAt`);
      const managerPath = `/data/local/tmp/fastdds_dsoftbus_record_timing_20260914T014946Z_${board}/softbus_record_probe`;
      check(em.path === managerPath && sameHash(em.sha256, MANAGER_SHA), `${board}.expected.frozenManager`);
      artifact(m, em, `${board}.manager`, remotePath);
      check(er.path !== em.path && hex(er.sha256) && !sameHash(er.sha256, MANAGER_SHA), `${board}.${artifactName}NotOldManager`);
      check(sameArray(m.argv, [em.path, '--list-nodes', '--expected-token', String(fixed.token), '--preflight-confirmed']), `${board}.manager.argv`);
      const managerStart = timed(m.startedAt, `${board}.manager.startedAt`);
      const managerEnd = timed(m.endedAt, `${board}.manager.endedAt`);
      check(managerEnd >= managerStart && m.remoteExit === 0 && m.hdcExit === 0 && integer(m.pid) && m.pid > 0, `${board}.manager.exit`);
      if (lifecycle) check(m.remoteExitCount === 1, `${board}.manager.remoteExitCount`);
      const audit = obj(m.pidAudit), auditTime = timed(audit.checkedAt, `${board}.manager.pidAudit.checkedAt`);
      check(audit.pid === m.pid && audit.gone === true && auditTime >= managerEnd, `${board}.manager.pidAudit`);
      const identity = obj(m.identity), permission = obj(Array.isArray(identity.permissions) ? identity.permissions[0] : null);
      check(identity.lookupToken === fixed.token && identity.tokenType === 'TOKEN_NATIVE' && identity.nativeInfoRc === 0 &&
        identity.processName === PROCESS_NAME && identity.apl === 'normal' && identity.permissionRc === 0 &&
        Array.isArray(identity.permissions) && identity.permissions.length === 1 &&
        permission.name === PERMISSION && permission.state === 'GRANTED', `${board}.manager.recognizedIdentity`);

      const snapshots = obj(a.snapshots);
      for (const key of ['origin', 'ownership', 'freshBefore']) {
        artifact(obj(snapshots[key]), obj(e[key]), `${board}.snapshots.${key}`, localPath);
      }
      const current = obj(snapshots.current);
      check(localPath(e.currentPath) && current.path === e.currentPath && current.remotePath === SOURCE_PATH &&
        sameHash(current.sha256, current.remoteSha256), `${board}.snapshots.current`);
      const capturedAt = timed(current.capturedAt, `${board}.snapshots.current.capturedAt`);
      const remoteReadAt = timed(current.remoteReadAt, `${board}.snapshots.current.remoteReadAt`);
      check(capturedAt >= auditTime && remoteReadAt >= auditTime, `${board}.snapshots.afterManagerExit`);

      const metadata = obj(a.metadata), stat = obj(metadata.value), es = obj(e.metadata);
      const metadataAt = timed(metadata.checkedAt, `${board}.metadata.checkedAt`);
      check(es.kind === 'regular' && es.isSymlink === false && es.mode === '640' && es.uid === 0 && es.gid === 3020 &&
        es.selinux === 'u:object_r:accesstoken_data_file:s0' && integer(es.size) && es.size > 0 &&
        integer(es.inode) && es.inode > 0 && integer(es.mtimeSeconds) && integer(es.ctimeSeconds), `${board}.expected.metadata`);
      check(METADATA_KEYS.every(key => stat[key] === es[key]) && metadataAt >= auditTime, `${board}.metadata`);

      const guard = obj(a.guard), g = obj(guard.result);
      const guardAt = timed(guard.checkedAt, `${board}.guard.checkedAt`);
      check(guard.runId === wanted.runId && guard.exitCode === 0 && guardAt >= capturedAt && guardAt >= remoteReadAt &&
        guardAt >= metadataAt && sameHash(guard.originSha256, obj(e.origin).sha256) &&
        sameHash(guard.ownershipSha256, obj(e.ownership).sha256), `${board}.guard.inputBinding`);
      check(g.Passed === true && g.Board === board && g.Target === fixed.target && g.Token === fixed.token && g.Phase === 'Granted' &&
        sameHash(g.CurrentSha256, current.sha256) && sameHash(g.FreshBeforeSha256, obj(e.freshBefore).sha256), `${board}.guard.composition`);
      for (const key of ['Ownership', 'Origin', 'FreshTransition']) {
        const part = obj(g[key]);
        check(part.Passed === true && part.Code === 'PASS' && part.ProcessName === PROCESS_NAME && part.TokenId === fixed.token &&
          part.Phase === (key === 'Ownership' ? 'Ownership' : 'Granted'), `${board}.guard.${key}`);
      }

      check(network(e.localNetworkId) && network(e.peerNetworkId) && e.localNetworkId !== e.peerNetworkId &&
        m.localNetworkId === e.localNetworkId && Array.isArray(m.peerNetworkIds) &&
        m.peerNetworkIds.every(network) && m.peerNetworkIds.includes(e.peerNetworkId), `${board}.route`);
      if (lifecycle) check(e.localNetworkId !== '0'.repeat(64) && e.peerNetworkId !== '0'.repeat(64), `${board}.route.nonzero`);
      let canonical = board === 'A' ? [er.path, '--server', '--local-name', 'com.kaihong.mdds.listen.a',
        '--expected-remote-initiator-name', 'com.kaihong.mdds.dial.b', '--expected-peer-network-id', e.peerNetworkId] :
        [er.path, '--client', '--local-name', 'com.kaihong.mdds.dial.b', '--remote-listen-name',
          'com.kaihong.mdds.listen.a', '--peer-network-id', e.peerNetworkId, '--expected-peer-network-id', e.peerNetworkId];
      let lcBoard;
      if (lifecycle) {
        const role = profile === LC_ALIAS ? 'single' : board === 'A' ? 'server' : 'client';
        check(e.role === role && a.role === role, `${board}.role`);
        const commonExpected = ['target', 'token', 'manager', 'origin', 'ownership', 'freshBefore',
          'currentPath', 'metadata', 'localNetworkId', 'peerNetworkId', 'launchArgv'];
        const commonActual = ['board', 'target', 'token', 'manager', 'snapshots', 'metadata', 'guard', 'launchArgv'];
        const lcKeys = ['role', 'probe', 'provider', 'loader', 'rawCaptures'];
        check(exactKeys(e, [...commonExpected, ...lcKeys]), `${board}.expected.fields`);
        check(exactKeys(a, [...commonActual, ...lcKeys]), `${board}.evidence.fields`);
        const probeKeys = ['path', 'sha256', 'localPath', 'localSha256', 'buildManifest'];
        check(exactKeys(er, probeKeys) && exactKeys(r, [...probeKeys, 'remoteSha256', 'checkedAt']), `${board}.probe.fields`);
        check(er.path === `/data/local/tmp/${wanted.runId}_${board}/softbus_lifecycle_probe`, `${board}.probe.runPath`);
        check(localPath(er.localPath) && r.localPath === er.localPath && sameHash(er.localSha256, er.sha256) &&
          sameHash(r.localSha256, er.sha256) && sameHash(r.remoteSha256, er.sha256), `${board}.probe.localRemote`);
        artifact(obj(r.buildManifest), obj(er.buildManifest), `${board}.probe.buildManifest`, localPath);
        check(exactKeys(r.buildManifest, ['path', 'sha256']) && exactKeys(er.buildManifest, ['path', 'sha256']),
          `${board}.probe.buildManifest.fields`);
        const ep = obj(e.provider), ap = obj(a.provider), identity = obj(ep.identity), observed = obj(ap.identity);
        const providerKeys = ['path', 'resolvedPath', 'sha256', 'identity'];
        check(exactKeys(ep, providerKeys) && exactKeys(ap, [...providerKeys, 'checkedAt']), `${board}.provider.fields`);
        check(ep.path === PROVIDER_PATH && ap.path === PROVIDER_PATH && sameHash(ap.sha256, ep.sha256), `${board}.provider`);
        check(absolutePath(ep.resolvedPath) && ap.resolvedPath === ep.resolvedPath, `${board}.provider.resolvedPath`);
        // Identity describes the requested path's lstat observation; resolvedPath
        // binds its separately observed resolution. Do not infer vendor layout.
        check(['regular', 'symlink'].includes(identity.kind) && identity.isSymlink === (identity.kind === 'symlink') &&
          typeof identity.mode === 'string' && /^[0-7]{3,4}$/.test(identity.mode) && integer(identity.uid) && integer(identity.gid) &&
          typeof identity.selinux === 'string' && /^[A-Za-z0-9_:.-]+$/.test(identity.selinux) &&
          integer(identity.size) && identity.size > 0 && integer(identity.inode) && identity.inode > 0 &&
          integer(identity.mtimeSeconds) && integer(identity.ctimeSeconds), `${board}.expected.provider.identity`);
        check(exactKeys(identity, METADATA_KEYS) && exactKeys(observed, METADATA_KEYS) &&
          METADATA_KEYS.every(key => identity[key] === observed[key]), `${board}.provider.identity`);
        const providerTime = timed(ap.checkedAt, `${board}.provider.checkedAt`);
        const el = obj(e.loader), al = obj(a.loader);
        check(exactKeys(el, ['LD_LIBRARY_PATH', 'LD_PRELOAD']) && exactKeys(al, ['LD_LIBRARY_PATH', 'LD_PRELOAD']) &&
          el.LD_LIBRARY_PATH === LIBRARY_PATH && al.LD_LIBRARY_PATH === LIBRARY_PATH &&
          el.LD_PRELOAD === '' && al.LD_PRELOAD === '', `${board}.loader`);
        const raw = obj(a.rawCaptures), expectedRaw = obj(e.rawCaptures), frozenRaw = {};
        check(exactKeys(raw, RAW_KEYS) && exactKeys(expectedRaw, RAW_KEYS), `${board}.rawCaptures.fields`);
        const observationTimes = {manager: managerEnd, pidAudit: auditTime, metadata: metadataAt,
          probe: artifactTime, provider: providerTime};
        for (const key of RAW_KEYS) {
          const ar = obj(raw[key]), eRaw = obj(expectedRaw[key]);
          artifact(ar, eRaw, `${board}.rawCaptures.${key}`, localPath);
          check(exactKeys(eRaw, ['path', 'sha256']) && exactKeys(ar, ['path', 'sha256', 'runId', 'checkedAt']),
            `${board}.rawCaptures.${key}.fields`);
          const rawTime = timed(ar.checkedAt, `${board}.rawCaptures.${key}.checkedAt`);
          check(ar.runId === wanted.runId && rawTime >= observationTimes[key], `${board}.rawCaptures.${key}.binding`);
          if (hex(eRaw.sha256)) frozenRaw[key] = frozenArtifact(eRaw);
        }
        canonical = [er.path, '--case', profile === LC_ALIAS ? 'alias' : 'callback-close', `--${role}`, '--run-id', wanted.runId];
        if (profile === LC_ALIAS) canonical.push('--local-name', `com.kaihong.mdds.lc3a.dial.${board.toLowerCase()}`,
          '--remote-listen-name', 'com.kaihong.mdds.lc3a.peer', '--peer-network-id', e.peerNetworkId,
          '--expected-peer-network-id', e.peerNetworkId);
        else if (board === 'A') canonical.push('--local-name', 'com.kaihong.mdds.lc2.listen.a',
          '--expected-remote-initiator-name', 'com.kaihong.mdds.lc2.dial.b', '--expected-peer-network-id', e.peerNetworkId);
        else canonical.push('--local-name', 'com.kaihong.mdds.lc2.dial.b', '--remote-listen-name', 'com.kaihong.mdds.lc2.listen.a',
          '--peer-network-id', e.peerNetworkId, '--expected-peer-network-id', e.peerNetworkId);
        if (errors.length === 0) lcBoard = {target: fixed.target, token: fixed.token, role,
          probe: Object.freeze({...frozenArtifact(er), localPath: er.localPath, localSha256: er.localSha256.toLowerCase(),
            buildManifest: frozenArtifact(er.buildManifest)}),
          provider: Object.freeze({...frozenArtifact(ep), resolvedPath: ep.resolvedPath, identity: Object.freeze({...identity})}),
          loader: Object.freeze({LD_LIBRARY_PATH: LIBRARY_PATH, LD_PRELOAD: ''}), rawCaptures: Object.freeze(frozenRaw)};
      }
      canonical.push('--expected-token', String(fixed.token), '--preflight-confirmed');
      check(sameArray(e.launchArgv, canonical) && sameArray(a.launchArgv, canonical), `${board}.launchArgv`);
      boards[board] = lifecycle ? Object.freeze({...lcBoard, launchArgv: Object.freeze(canonical)}) : Object.freeze({target: fixed.target, token: fixed.token,
        record: Object.freeze({path: er.path, sha256: hex(er.sha256) ? er.sha256.toLowerCase() : ''}),
        launchArgv: Object.freeze(canonical)});
    }
    const ea = obj(wanted.boards.A), eb = obj(wanted.boards.B);
    if (!lifecycle || profile === LC_PAIR)
      check(ea.localNetworkId === eb.peerNetworkId && ea.peerNetworkId === eb.localNetworkId, 'pair.routeCrossCheck');
    if (errors.length) return fail();
    if (lifecycle) return Object.freeze({ok: true, errors: Object.freeze([]), binding: Object.freeze({profile,
      runId: wanted.runId, startedAt: wanted.startedAt, expiresAt: new Date(start + wanted.maxAgeMs).toISOString(),
      validatedAt: new Date(nowMs).toISOString(), activeBoards: Object.freeze([...activeBoards]),
      budgets: LC_BUDGETS, boards: Object.freeze(boards)})});
    return Object.freeze({ok: true, errors: Object.freeze([]), binding: Object.freeze({runId: wanted.runId,
      validatedAt: new Date(nowMs).toISOString(), boards: Object.freeze(boards)})});
  } catch (_) {
    check(false, 'malformedInput');
    return fail();
  }
}

function validateRecordRunBinding(evidence, expected, nowMs) {
  return validateBinding(evidence, expected, nowMs, false);
}

function validateLifecycleRunBinding(evidence, expected, nowMs) {
  return validateBinding(evidence, expected, nowMs, true);
}

// No generic validator/profile object/configuration callback is exported.
module.exports = {validateRecordRunBinding, validateLifecycleRunBinding};
