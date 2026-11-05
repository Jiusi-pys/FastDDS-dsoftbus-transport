#!/usr/bin/env python3
"""Deploy one Fast DDS line (2.14.5 or 3.6.2) to two OpenHarmony boards and run the cross-board
device suite over DSoftBus, or the UDP/TCP content-filter race reproducer.

    python scripts/release/run_cross_board.py --version 3.6.2 --work E:/fdds-rel --a <hdc target A> --b <hdc target B>
        [--groups all|core,qos,feature,security,statistics] [--route 0|1|2] [--stress wsf0|wsf1]
        [--cft-race udp|tcp --race-mode filtered|unfiltered|nochurn|wsf0 --seconds 60] [--no-deploy] [--out DIR]

Each version is deployed to its own directory (<board root>/<version>/{bin,lib,work}); nothing else on
the boards is modified. The process gets the DATASYNC-capable native token named by --token-process
(looked up in /data/service/el0/access_token/nativetoken.json) through SetSelfTokenID.
Exit code 0 only when every case on both boards passed and both Runtimes closed.
"""
import argparse
import json
import os
import re
import secrets
import subprocess
import sys
import threading
import time
from pathlib import Path

SDK = Path(os.environ.get('OHOS_NATIVE', Path.home() / 'AppData/Local/OpenHarmony/Sdk/23/native'))
HDC = os.environ.get('HDC', str(SDK.parent / 'toolchains' / 'hdc.exe'))
READELF = str(SDK / 'llvm' / 'bin' / 'llvm-readelf.exe')
CXX_SHARED = SDK / 'llvm' / 'lib' / 'aarch64-linux-ohos' / 'libc++_shared.so'


def sh(target, cmd, timeout=60):
    r = subprocess.run([HDC, '-t', target, 'shell', cmd], capture_output=True, timeout=timeout)
    return r.stdout.decode('utf-8', 'replace').replace('\r', '')


def send(target, local, remote):
    r = subprocess.run([HDC, '-t', target, 'file', 'send', str(local), remote], capture_output=True, text=True, timeout=300)
    # hdc exits 0 even when it prints [Fail]; require the explicit success line.
    if 'FileTransfer finish' not in r.stdout or '[Fail]' in r.stdout:
        sys.exit('send failed %s -> %s: %s' % (local, remote, r.stdout))


def soname(path):
    out = subprocess.run([READELF, '-d', str(path)], capture_output=True, text=True).stdout
    m = re.search(r'\(SONAME\)\s+Library soname: \[([^\]]+)\]', out)
    return m.group(1) if m else None


def deploy(target, root, work, version):
    prefix, inst = Path(work) / version / 'prefix', Path(work) / version / 'transport-install'
    libs = {}
    for f in list((prefix / 'lib').glob('*.so*')) + list((inst / 'lib').glob('*.so')):
        if f.is_file() and not f.is_symlink():
            name = soname(f)
            if name and name not in libs:
                libs[name] = f
    libs['libc++_shared.so'] = CXX_SHARED
    sh(target, 'mkdir -p %s/lib %s/bin %s/work' % (root, root, root))
    for name, f in sorted(libs.items()):
        send(target, f, '%s/lib/%s' % (root, name))
    for exe in ('dsb_suite', 'cft_race'):
        send(target, inst / 'bin' / exe, '%s/bin/%s' % (root, exe))
    sh(target, 'chmod 755 %s/bin/*' % root)
    return sorted(libs)


def token_of(target, process):
    text = sh(target, 'cat /data/service/el0/access_token/nativetoken.json')
    m = re.search(r'"processName":"%s","APL":\d+,"version":\d+,"tokenId":(\d+)' % re.escape(process), text)
    if not m:
        sys.exit('native token %s not provisioned on %s (see docs/FASTDDS_VERSIONS_AND_RELEASE.md)' % (process, target))
    return m.group(1)


def ipv4(target):
    return [a for a in re.findall(r'inet (?:addr:)?(\d+\.\d+\.\d+\.\d+)', sh(target, 'ifconfig 2>/dev/null || ip -4 addr')) if not a.startswith('127.')]


class Run:
    def __init__(self, target, cmd, log):
        self.lines, self.log, self.target = [], log, target
        self.p = subprocess.Popen([HDC, '-t', target, 'shell', cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in iter(self.p.stdout.readline, b''):
            line = raw.decode('utf-8', 'replace').rstrip('\r\n')
            self.lines.append(line)
            self.log.write('[%s] %s\n' % (self.target[-6:], line)); self.log.flush()

    def seen(self, prefix):
        return any(l.startswith(prefix) for l in self.lines)


def wait_until(pred, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.2)
    return pred()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True, choices=['2.14.5', '2.14.5-ros', '3.6.2', '3.2.5'])
    ap.add_argument('--work', required=True)
    ap.add_argument('--a', required=True); ap.add_argument('--b', required=True)
    ap.add_argument('--board-root', default='/data/local/tmp/fdds_dsb')
    ap.add_argument('--token-process', default='mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16')
    ap.add_argument('--groups', default='all'); ap.add_argument('--route', default='0'); ap.add_argument('--stress', default='')
    ap.add_argument('--domain', default='151')
    ap.add_argument('--cft-race', choices=['udp', 'tcp']); ap.add_argument('--race-mode', default='filtered',
        choices=['filtered', 'unfiltered', 'nochurn', 'wsf0']); ap.add_argument('--seconds', default='60')
    ap.add_argument('--no-deploy', action='store_true'); ap.add_argument('--out')
    a = ap.parse_args()
    run = secrets.token_hex(8)
    out = Path(a.out or Path(a.work) / 'runs' / ('%s-%s' % (a.version, run))); out.mkdir(parents=True, exist_ok=True)
    log = open(out / 'run.log', 'w', encoding='utf-8')
    root = '%s/%s' % (a.board_root, a.version)
    boards = {'A': a.a, 'B': a.b}
    if not a.no_deploy:
        for side, t in boards.items():
            print('deploy', side, deploy(t, root, a.work, a.version))
    env = 'cd %s && LD_LIBRARY_PATH=%s/lib' % (root, root)

    if a.cft_race:
        ips = {s: ipv4(t) for s, t in boards.items()}
        pair = next(((x, y) for x in ips['A'] for y in ips['B'] if x.rsplit('.', 1)[0] == y.rsplit('.', 1)[0]), None)
        if not pair:
            sys.exit('no common IPv4 subnet between the boards: %s' % ips)
        local, readers, wsf = ('1', '6', '1') if a.race_mode in ('filtered', 'wsf0') else ('1', '-6', '1')
        if a.race_mode == 'nochurn': local = '0'
        if a.race_mode == 'wsf0': wsf = '0'
        ra = Run(a.a, '%s ./bin/cft_race A %s %s %s %s %s %s; echo CFTRACE_EXIT=$?' % (env, a.cft_race, pair[1], a.seconds, local, readers, wsf), log)
        time.sleep(2)
        rb = Run(a.b, '%s ./bin/cft_race B %s %s %s 0 %s %s; echo CFTRACE_EXIT=$?' % (env, a.cft_race, pair[0], a.seconds, readers, wsf), log)
        ra.p.wait(); rb.p.wait()
        exit_a = next((l for l in ra.lines if l.startswith('CFTRACE_EXIT=')), 'CFTRACE_EXIT=?')
        done_a = next((l for l in ra.lines if l.startswith('CFTRACE_DONE')), '')
        result = {'version': a.version, 'transport': a.cft_race, 'mode': a.race_mode, 'seconds': a.seconds, 'ips': pair,
                  'A_exit': exit_a.split('=', 1)[1], 'A_done': done_a, 'A_survived': bool(done_a) and exit_a.endswith('=0')}
        (out / 'result.json').write_text(json.dumps(result, indent=1))
        print(json.dumps(result, indent=1)); return 0 if result['A_survived'] else 1

    tokens = {s: token_of(t, a.token_process) for s, t in boards.items()}
    nodes = {}
    for s, t in boards.items():
        text = sh(t, '%s ./bin/dsb_suite node token=%s' % (env, tokens[s]))
        m = re.search(r'DSB_NODE network=(\S+) udid=(\S+)', text)
        if not m:
            sys.exit('node query failed on %s: %s' % (s, text))
        nodes[s] = {'network': m.group(1), 'udid': m.group(2)}
    work = '%s/work/%s' % (root, run)
    runs = {}
    for idx, (s, t) in enumerate(boards.items()):
        peer = nodes['B' if s == 'A' else 'A']
        sh(t, 'mkdir -p %s' % work)
        cmd = ('%s ./bin/dsb_suite run %d %s token=%s run=%s domain=%s local_udid=%s peer_udid=%s peer_network=%s groups=%s route=%s%s; echo DSB_EXIT=$?'
               % (env, idx, work, tokens[s], run, a.domain, nodes[s]['udid'], peer['udid'], peer['network'], a.groups, a.route,
                  (' stress=' + a.stress) if a.stress else ''))
        runs[s] = Run(t, cmd, log)
    if not wait_until(lambda: all(r.seen('DSB_LISTENING') for r in runs.values()), 120):
        print('listeners not ready', {s: r.lines[-3:] for s, r in runs.items()})
    for t in boards.values():
        sh(t, 'touch %s/go' % work)
    if a.route != '0':   # drop all IP traffic from A's addresses on B, 15 s each
        ips_a = ipv4(a.a)
        if wait_until(lambda: runs['B'].seen('DSB_ROUTE_WINDOW_BEGIN'), 900):
            add = '; '.join('iptables -I INPUT -s %s -j DROP; iptables -I OUTPUT -d %s -j DROP' % (p, p) for p in ips_a)
            rm = '; '.join('iptables -D INPUT -s %s -j DROP; iptables -D OUTPUT -d %s -j DROP' % (p, p) for p in ips_a)
            t0 = time.time()   # outages start 6 s (one) or 5 s and 29 s (two) into the window
            for start in ([6] if a.route == '1' else [5, 29]):
                time.sleep(max(0.0, t0 + start - time.time()))
                log.write('route outage start %.1f\n' % (time.time() - t0)); sh(a.b, add); time.sleep(15); sh(a.b, rm)
                log.write('route outage end %.1f\n' % (time.time() - t0))
    for r in runs.values():
        r.p.wait()
    cases, summary = [], {}
    for s, r in runs.items():
        for l in r.lines:
            if l.startswith('DSB_CASE'):
                f = dict(x.split('=', 1) for x in l.split()[1:] if '=' in x); f['board'] = s; f['line'] = l; cases.append(f)
            elif l.startswith('DSB_SUMMARY') or l.startswith('DSB_ABORT') or l.startswith('DSB_EXIT'):
                summary.setdefault(s, []).append(l)
    failed = [c for c in cases if c.get('pass') == '0']
    ok = not failed and all(any('DSB_EXIT=0' in l for l in summary.get(s, [])) for s in boards)
    result = {'version': a.version, 'run': run, 'groups': a.groups, 'route': a.route, 'stress': a.stress, 'nodes': nodes,
              'passed': sum(1 for c in cases if c.get('pass') == '1'), 'failed': len(failed),
              'not_applicable_or_not_run': sorted({c['case'] + ':' + c['pass'] for c in cases if c.get('pass') not in ('0', '1')}),
              'summary': summary, 'ok': ok, 'cases': cases}
    (out / 'result.json').write_text(json.dumps(result, indent=1))
    for c in cases:
        if c.get('pass') in ('0', '1'):
            print('%s %-5s %-10s %-45s %s' % (c['board'], 'PASS' if c['pass'] == '1' else 'FAIL', c.get('group'), c.get('case'), ''))
    print('result:', 'PASS' if ok else 'FAIL', 'passed=%d failed=%d' % (result['passed'], result['failed']), 'out=%s' % out)
    for s in boards:
        print(s, summary.get(s))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
