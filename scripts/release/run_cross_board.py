#!/usr/bin/env python3
"""Deploy one Fast DDS line (2.14.5 or 3.6.2) to two OpenHarmony boards and run the cross-board
device suite over DSoftBus, or the UDP/TCP content-filter race reproducer.

    python scripts/release/run_cross_board.py --version 3.6.2 --work E:/fdds-rel --a <hdc target A> --b <hdc target B>
        [--groups all|core,qos,feature,security,statistics] [--route 0|1|2] [--stress wsf0|wsf1]
        [--cft-race udp|tcp --race-mode filtered|unfiltered|nochurn|wsf0 --seconds 60] [--no-deploy] [--out DIR]

Each version is deployed to its own directory (<board root>/<version>/{bin,lib,work}); nothing else on
the boards is modified. The process gets the DATASYNC-capable native token named by --token-process
(looked up in /data/service/el0/access_token/nativetoken.json) through SetSelfTokenID.
Exit code 0 only when every case on both boards passed, both Runtimes closed, and both boards recorded
the selected scenario (each selected group, and the route window or stress case when requested). Unknown
--groups names are refused before anything is deployed. Exit code 3 means a route-outage DROP rule could
not be proven removed from board B; the message names the commands to run by hand.
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
GROUPS = ('core', 'qos', 'feature', 'security', 'statistics')   # dsb_suite group_on() names
OUTAGE_SECONDS = 15
OUTAGE_STARTS = {'1': [6], '2': [5, 29]}   # seconds into the route window
CLEANUP_ATTEMPTS = 3
CLEANUP_RETRY_SECONDS = 2


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


def parse_groups(text):
    """Return the selected suite groups. dsb_suite skips any name it does not know, so a typo here would
    otherwise run nothing and report success."""
    if text == 'all':
        return list(GROUPS)
    names = text.split(',')
    unknown = [n for n in names if n not in GROUPS]
    if unknown:
        raise ValueError("unknown --groups %s; use 'all' or a comma list of %s" % (unknown, ','.join(GROUPS)))
    return list(dict.fromkeys(names))


class RouteOutageError(RuntimeError):
    """The outage was not applied; board traffic was not interrupted as requested."""


class RouteCleanupError(RuntimeError):
    """A DROP rule may still be blocking the two boards."""


def drop_rules(ips):
    return [r for p in ips for r in ('INPUT -s %s -j DROP' % p, 'OUTPUT -d %s -j DROP' % p)]


def rule_codes(target, rules):
    """iptables -C per rule: 0 present, 1 absent, anything else (or a missing line) proves nothing."""
    out = sh(target, '; '.join('iptables -C %s 2>/dev/null; echo ROUTE_RULE_RC=$?' % r for r in rules))
    return re.findall(r'^ROUTE_RULE_RC=(\d+)$', out, re.M)


def install_route_rules(target, rules):
    sh(target, '; '.join('iptables -I %s' % r for r in rules))
    codes = rule_codes(target, rules)
    if codes != ['0'] * len(rules):
        raise RouteOutageError('DROP rules not installed on %s: %s' % (target, codes))


def remove_route_rules(target, rules, log):
    """Remove every copy of the rules and prove they are gone. A Ctrl+C during cleanup does not stop it;
    it is raised again once the board is known to be clean."""
    # Each rule loops on its own, so one failure cannot leave the next rule (or a duplicate) in place.
    remove = '; '.join('while iptables -D %s 2>/dev/null; do :; done' % r for r in rules)
    interrupted, last = None, 'not attempted'
    for attempt in range(1, CLEANUP_ATTEMPTS + 1):
        try:
            if attempt > 1:
                time.sleep(CLEANUP_RETRY_SECONDS)   # hdc may need a moment after an interrupt reached it
            sh(target, remove)
            codes = rule_codes(target, rules)
            if codes == ['1'] * len(rules):
                log.write('route rules removed attempt=%d\n' % attempt); log.flush()
                last = None
                break
            last = 'iptables -C returned %s' % codes
        except KeyboardInterrupt as e:
            interrupted, last = e, 'interrupted'
        except (OSError, subprocess.SubprocessError) as e:
            last = repr(e)
        log.write('route rule cleanup attempt=%d failed: %s\n' % (attempt, last)); log.flush()
    if last is not None:
        raise RouteCleanupError('DROP rules may remain on %s (%s). Remove them with: hdc -t %s shell "%s"'
                                % (target, last, target, remove))
    if interrupted:
        raise interrupted


def run_route_outages(target, ips, route, log):
    """Apply the scheduled outages on target; returns how many ran. The rules are removed on every exit
    path, including an interrupt, and the finally starts before the first iptables -I."""
    if not ips:
        raise RouteOutageError('no IPv4 address known for the peer board')
    rules = drop_rules(ips)
    t0 = time.time()
    for start in OUTAGE_STARTS[route]:
        time.sleep(max(0.0, t0 + start - time.time()))
        log.write('route outage start %.1f\n' % (time.time() - t0)); log.flush()
        try:
            install_route_rules(target, rules)
            time.sleep(OUTAGE_SECONDS)
        finally:
            remove_route_rules(target, rules, log)
        log.write('route outage end %.1f\n' % (time.time() - t0)); log.flush()
    return len(OUTAGE_STARTS[route])


def collect_records(lines):
    """Split one board's output into its DSB_CASE records and its summary/exit lines."""
    cases, summary = [], []
    for l in lines:
        if l.startswith('DSB_CASE'):
            f = dict(x.split('=', 1) for x in l.split()[1:] if '=' in x); f['line'] = l; cases.append(f)
        elif l.startswith('DSB_SUMMARY') or l.startswith('DSB_ABORT') or l.startswith('DSB_EXIT'):
            summary.append(l)
    return cases, summary


def scenario_issues(selected, route, stress, lines_by_board, cases, outages, route_error=None):
    """What the run was asked to do but cannot show it did. A clean exit proves nothing on its own: a
    board that skipped every case still initializes, closes and exits 0."""
    issues = [route_error] if route_error else []
    expected = ['stress'] if stress else selected
    for side, lines in lines_by_board.items():
        own = [c for c in cases if c['board'] == side]
        seen = {c.get('group') for c in own}
        issues += ['%s: selected group %s has no execution record' % (side, g) for g in expected if g not in seen]
        if not any(c.get('pass') == '1' for c in own):
            issues.append('%s: no case passed' % side)
        if route != '0' and not any(l.startswith('DSB_ROUTE_WINDOW_BEGIN') for l in lines):
            issues.append('%s: route window never began' % side)
    if route != '0' and outages != len(OUTAGE_STARTS[route]):
        issues.append('route outages applied %d of %d' % (outages, len(OUTAGE_STARTS[route])))
    return issues


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', required=True, choices=['2.14.5', '2.14.5-ros', '3.6.2', '3.2.5'])
    ap.add_argument('--work', required=True)
    ap.add_argument('--a', required=True); ap.add_argument('--b', required=True)
    ap.add_argument('--board-root', default='/data/local/tmp/fdds_dsb')
    ap.add_argument('--token-process', default='mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16')
    ap.add_argument('--groups', default='all'); ap.add_argument('--route', default='0', choices=['0', *OUTAGE_STARTS])
    ap.add_argument('--stress', default='', choices=['', 'wsf0', 'wsf1'])
    ap.add_argument('--domain', default='151')
    ap.add_argument('--cft-race', choices=['udp', 'tcp']); ap.add_argument('--race-mode', default='filtered',
        choices=['filtered', 'unfiltered', 'nochurn', 'wsf0']); ap.add_argument('--seconds', default='60')
    ap.add_argument('--no-deploy', action='store_true'); ap.add_argument('--out')
    a = ap.parse_args()
    try:
        selected = parse_groups(a.groups)
    except ValueError as e:
        ap.error(str(e))
    if a.route != '0' and (a.stress or 'core' not in selected):   # the route window is part of core_cases
        ap.error('--route needs the core group and cannot be combined with --stress')
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
    outages, route_error = 0, None
    if a.route != '0':   # drop all IP traffic from A's addresses on B, 15 s each
        ips_a = ipv4(a.a)   # before the window: the outage schedule starts when it opens
        if wait_until(lambda: runs['B'].seen('DSB_ROUTE_WINDOW_BEGIN'), 900):
            try:
                outages = run_route_outages(a.b, ips_a, a.route, log)
            except RouteOutageError as e:
                route_error = str(e)
    for r in runs.values():
        r.p.wait()
    cases, summary = [], {}
    for s, r in runs.items():
        own, summary[s] = collect_records(r.lines)
        cases += [dict(c, board=s) for c in own]
    failed = [c for c in cases if c.get('pass') == '0']
    issues = scenario_issues(selected, a.route, a.stress, {s: r.lines for s, r in runs.items()}, cases, outages, route_error)
    ok = not failed and not issues and all(any('DSB_EXIT=0' in l for l in summary.get(s, [])) for s in boards)
    result = {'version': a.version, 'run': run, 'groups': a.groups, 'route': a.route, 'route_outages': outages, 'stress': a.stress, 'nodes': nodes,
              'passed': sum(1 for c in cases if c.get('pass') == '1'), 'failed': len(failed),
              'not_applicable_or_not_run': sorted({c['case'] + ':' + c['pass'] for c in cases if c.get('pass') not in ('0', '1')}),
              'scenario_issues': issues, 'summary': summary, 'ok': ok, 'cases': cases}
    (out / 'result.json').write_text(json.dumps(result, indent=1))
    for c in cases:
        if c.get('pass') in ('0', '1'):
            print('%s %-5s %-10s %-45s %s' % (c['board'], 'PASS' if c['pass'] == '1' else 'FAIL', c.get('group'), c.get('case'), ''))
    print('result:', 'PASS' if ok else 'FAIL', 'passed=%d failed=%d' % (result['passed'], result['failed']), 'out=%s' % out)
    for issue in issues:
        print('scenario:', issue)
    for s in boards:
        print(s, summary.get(s))
    return 0 if ok else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RouteCleanupError as e:
        print('ROUTE CLEANUP FAILED:', e, file=sys.stderr)
        sys.exit(3)
