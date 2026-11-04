'use strict';

// FABRICATED HOST FIXTURES, not a board capture, permission or launch authority.
// Kept separate so the original 130 record tests remain byte-for-byte unchanged.
function recordBindingFixture() {
  const startedAt = '2026-09-14T04:30:00.000Z';
  const at = '2026-09-14T04:30:10.000Z', managerAt = '2026-09-14T04:30:20.000Z';
  const auditAt = '2026-09-14T04:30:21.000Z', snapshotAt = '2026-09-14T04:30:22.000Z';
  const guardAt = '2026-09-14T04:30:23.000Z';
  const expected = {runId: '20260914T043000000Z-fixture', startedAt, maxAgeMs: 120000, boards: {}};
  const evidence = {schemaVersion: 1, runId: expected.runId, startedAt, boards: {}};
  for (const [board, token, target, own, peer] of [
    ['A', 671793191, '3e01ff55454d202020104033bf453b00', 'a', 'b'],
    ['B', 671320465, '3e01ff55454d202020104433991c3b00', 'b', 'a']
  ]) {
    const record = {path: `/data/local/tmp/${expected.runId}_${board}/softbus_record_probe`, sha256: 'c'.repeat(64)};
    const manager = {path: `/data/local/tmp/fastdds_dsoftbus_record_timing_20260914T014946Z_${board}/softbus_record_probe`,
      sha256: 'd01365fc20d70206a1ddeb32c5c278642125f5c1eb49f80754b2d1c8350a172b'};
    const origin = {path: `.artifacts/fixtures/${board}-origin.json`, sha256: '1'.repeat(64)};
    const ownership = {path: `.artifacts/fixtures/${board}-ownership.json`, sha256: '2'.repeat(64)};
    const freshBefore = {path: `.artifacts/fixtures/${expected.runId}/${board}-before.json`, sha256: '3'.repeat(64)};
    const currentPath = `.artifacts/fixtures/${expected.runId}/${board}-current.json`;
    const metadata = {kind: 'regular', isSymlink: false, mode: '640', uid: 0, gid: 3020,
      selinux: 'u:object_r:accesstoken_data_file:s0', size: 96628, inode: 14,
      mtimeSeconds: 1789350000, ctimeSeconds: 1789350000};
    const localNetworkId = own.repeat(64), peerNetworkId = peer.repeat(64);
    const launchArgv = board === 'A' ? [record.path, '--server', '--local-name', 'com.kaihong.mdds.listen.a',
      '--expected-remote-initiator-name', 'com.kaihong.mdds.dial.b', '--expected-peer-network-id', peerNetworkId] :
      [record.path, '--client', '--local-name', 'com.kaihong.mdds.dial.b', '--remote-listen-name',
        'com.kaihong.mdds.listen.a', '--peer-network-id', peerNetworkId, '--expected-peer-network-id', peerNetworkId];
    launchArgv.push('--expected-token', String(token), '--preflight-confirmed');
    expected.boards[board] = {target, token, record, manager, origin, ownership, freshBefore,
      currentPath, metadata, localNetworkId, peerNetworkId, launchArgv};
    const passed = phase => ({Passed: true, Code: 'PASS', ProcessName: 'fastdds_dsoftbus_probe', TokenId: token, Phase: phase});
    const pid = board === 'A' ? 10001 : 10002;
    evidence.boards[board] = {board, target, token, record: {...record, checkedAt: at},
      manager: {...manager, startedAt: at, endedAt: managerAt, hdcExit: 0, remoteExit: 0, pid,
        argv: [manager.path, '--list-nodes', '--expected-token', String(token), '--preflight-confirmed'],
        pidAudit: {pid, gone: true, checkedAt: auditAt},
        identity: {lookupToken: token, tokenType: 'TOKEN_NATIVE', nativeInfoRc: 0,
          processName: 'fastdds_dsoftbus_probe', apl: 'normal', permissionRc: 0,
          permissions: [{name: 'ohos.permission.DISTRIBUTED_DATASYNC', state: 'GRANTED'}]},
        localNetworkId, peerNetworkIds: [peerNetworkId]},
      snapshots: {origin: {...origin}, ownership: {...ownership}, freshBefore: {...freshBefore},
        current: {path: currentPath, sha256: '4'.repeat(64), remoteSha256: '4'.repeat(64),
          remotePath: '/data/service/el0/access_token/nativetoken.json', capturedAt: snapshotAt, remoteReadAt: snapshotAt}},
      metadata: {checkedAt: snapshotAt, value: {...metadata}},
      guard: {runId: expected.runId, checkedAt: guardAt, exitCode: 0,
        originSha256: origin.sha256, ownershipSha256: ownership.sha256,
        result: {Passed: true, Board: board, Target: target, Token: token, Phase: 'Granted',
          Ownership: passed('Ownership'), Origin: passed('Granted'), FreshTransition: passed('Granted'),
          FreshBeforeSha256: freshBefore.sha256, CurrentSha256: '4'.repeat(64)}},
      launchArgv: [...launchArgv]};
  }
  return {expected, evidence, nowMs: Date.parse('2026-09-14T04:30:30.000Z')};
}

function lifecycleBindingFixture(profile = 'lifecycle-callback-close-v1', selectedBoard) {
  const f = recordBindingFixture(), {expected, evidence} = f;
  expected.profile = evidence.profile = profile;
  expected.scope = evidence.scope = 'NATIVE';
  evidence.schemaVersion = 2;
  if (profile === 'lifecycle-alias-v1') {
    expected.selectedBoard = evidence.selectedBoard = selectedBoard;
    for (const board of ['A', 'B']) if (board !== selectedBoard) {
      delete expected.boards[board]; delete evidence.boards[board];
    }
  }
  for (const board of Object.keys(expected.boards)) {
    const e = expected.boards[board], a = evidence.boards[board];
    delete e.record; delete a.record;
    e.role = a.role = profile === 'lifecycle-alias-v1' ? 'single' : board === 'A' ? 'server' : 'client';
    e.probe = {path: `/data/local/tmp/${expected.runId}_${board}/softbus_lifecycle_probe`, sha256: 'c'.repeat(64),
      localPath: '.artifacts/fixtures/build/softbus_lifecycle_probe', localSha256: 'c'.repeat(64),
      buildManifest: {path: '.artifacts/fixtures/build/manifest.json', sha256: 'd'.repeat(64)}};
    a.probe = {...structuredClone(e.probe), remoteSha256: 'c'.repeat(64), checkedAt: '2026-09-14T04:30:24.000Z'};
    e.provider = {path: '/system/lib64/platformsdk/libsoftbus_client.z.so',
      resolvedPath: '/system/lib64/platformsdk/libsoftbus_client.z.so', sha256: 'e'.repeat(64),
      identity: {kind: 'regular', isSymlink: false, mode: '644', uid: 0, gid: 0,
        selinux: 'u:object_r:system_lib_file:s0', size: 1000, inode: 5, mtimeSeconds: 1, ctimeSeconds: 1}};
    a.provider = {...structuredClone(e.provider), checkedAt: '2026-09-14T04:30:24.000Z'};
    e.loader = {LD_LIBRARY_PATH: '/system/lib64/chipset-sdk-sp:/system/lib64/platformsdk:/system/lib64/chipset-sdk', LD_PRELOAD: ''};
    a.loader = {...e.loader};
    e.rawCaptures = {}; a.rawCaptures = {};
    for (const key of ['manager', 'pidAudit', 'metadata', 'probe', 'provider']) {
      e.rawCaptures[key] = {path: `.artifacts/fixtures/${expected.runId}/${board}-${key}.log`, sha256: 'f'.repeat(64)};
      a.rawCaptures[key] = {...e.rawCaptures[key], runId: expected.runId, checkedAt: '2026-09-14T04:30:25.000Z'};
    }
    a.manager.remoteExitCount = 1;
    let argv = [e.probe.path, '--case', profile === 'lifecycle-alias-v1' ? 'alias' : 'callback-close',
      `--${e.role}`, '--run-id', expected.runId];
    if (profile === 'lifecycle-alias-v1') argv.push('--local-name', `com.kaihong.mdds.lc3a.dial.${board.toLowerCase()}`,
      '--remote-listen-name', 'com.kaihong.mdds.lc3a.peer', '--peer-network-id', e.peerNetworkId,
      '--expected-peer-network-id', e.peerNetworkId);
    else if (board === 'A') argv.push('--local-name', 'com.kaihong.mdds.lc2.listen.a',
      '--expected-remote-initiator-name', 'com.kaihong.mdds.lc2.dial.b', '--expected-peer-network-id', e.peerNetworkId);
    else argv.push('--local-name', 'com.kaihong.mdds.lc2.dial.b', '--remote-listen-name', 'com.kaihong.mdds.lc2.listen.a',
      '--peer-network-id', e.peerNetworkId, '--expected-peer-network-id', e.peerNetworkId);
    argv.push('--expected-token', String(e.token), '--preflight-confirmed');
    e.launchArgv = argv; a.launchArgv = [...argv];
  }
  return f;
}

module.exports = {recordBindingFixture, lifecycleBindingFixture};
