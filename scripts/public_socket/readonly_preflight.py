"""Declared two-board read-only preflight. No directory API query/deploy/grant.

Requires the final static admission review before running the selected real tool.
Every command/exit and token-file transfer is retained; no historical baseline is
promoted to current evidence. Existing platform hashes are only version binding.
"""
import argparse, hashlib, importlib.util, json, re, subprocess, sys
from pathlib import Path
repo = Path.cwd().resolve()
base = repo / '.artifacts/ros2-device-acceptance/run-20260930-local'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_text(encoding='utf-8-sig'))
def need(value, reason):
    if not value:
        raise ValueError(reason)

p = argparse.ArgumentParser()
p.add_argument('--selection', type=Path, required=True)
p.add_argument('--review', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
s = read(a.selection)
review = read(a.review)
out = a.output.resolve()
need(Path(s['preflight_output_parent']).resolve() == out.parent and out.parent.is_relative_to(repo / '.artifacts') and (not out.exists()), 'Preflight constraint failed')
need(review['verdict'] == 'PASS' and review['selection_sha256'] == sha(a.selection) and (review['allow_readonly_preflight'] is True), 'Preflight constraint failed')
need(sha(__file__) == s['preflight_tool_sha256'], 'Preflight constraint failed')
need(sha(s['platform_reference']) == s['platform_reference_sha256'], 'Declared platform reference changed; no device dispatch')
for row in s['source_files']:
    need(sha(row['path']) == row['sha256'], 'Current source drift')
observer=Path(__file__).resolve().with_name('pregrant_recovery.py')
need(any(Path(e['path']).resolve()==observer and e['sha256']==sha(observer) for e in s['source_files']),'Preflight process observer not bound')
spec=importlib.util.spec_from_file_location('bound_process_observer',observer);observer_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer_module)
process_identity=observer_module.process_identity
hdc = s['device_driver']['path']
local=s.get('local_contract_test') is True
need(s.get('local_contract_test') in (None,False,True) and type(s.get('local_contract_test',False)) is bool,'Preflight mode must be boolean')
need(Path(hdc).name.lower() == ('fake-hdc.exe' if local else 'hdc.exe') and sha(hdc) == s['device_driver']['sha256'], 'Preflight constraint failed')
if local:need(s['targets']=={'A':'SIM-A','B':'SIM-B'},'Local preflight targets must be synthetic')
out.mkdir()
receipts = []

def command(tag, argv):
    pref = out / tag
    r = dict(tag=tag, argv=argv, cwd=str(repo), scope='REAL_READ_ONLY_PREFLIGHT_NO_DIRECTORY_QUERY', status='DISPATCH_INTENT')
    pref.with_suffix('.command.json').write_text(json.dumps(r, indent=2))
    try:
        child = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=repo)
        pref.with_suffix('.spawn.json').write_text(json.dumps(dict(pid=child.pid,identity=process_identity(child.pid))))
        child.stdout_data,child.stderr_data,timed_out,complete=observer_module.collect_child(child,30)
        r.update(exit_code=child.poll(),timed_out=timed_out,capture_complete=complete,drain_budget_seconds=2)
    except OSError:
        r.update(exit_code=None,timed_out=False,capture_complete=False)
        pref.with_suffix('.result.json').write_text(json.dumps(r));raise
    pref.with_suffix('.stdout').write_bytes(child.stdout_data)
    pref.with_suffix('.stderr').write_bytes(child.stderr_data)
    r.update(stdout_sha256=sha(pref.with_suffix('.stdout')), stderr_sha256=sha(pref.with_suffix('.stderr')))
    pref.with_suffix('.result.json').write_text(json.dumps(r, indent=2))
    receipts.append(r)
    need(not r['timed_out'] and r['capture_complete'],'Read-only capture timeout/incomplete')
    if child.returncode:
        raise RuntimeError('Preflight command failed ' + tag)
    return child.stdout_data.decode('utf-8-sig').replace('\r\n', '\n')
targets = command('targets', [hdc, 'list', 'targets'])
need(all((t in targets.split() for t in s['targets'].values())), 'Registered peer not online')
reference = read(s['platform_reference'])
platform = {}
baselines = {}
token = '/data/service/el0/access_token/nativetoken.json'
for side, target in s['targets'].items():
    old = reference['platform'][side]
    paths = [x['path'] for x in old['files']] + [old['service_server']['path']]

    def shell(tag, core):
        text = command(side + '-' + tag, [hdc, '-t', target, 'shell', core + '; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'])
        need(re.findall('^MDDS_REMOTE_EXIT=(\\d+)\\s*$', text, re.M) == ['0'], 'Remote preflight command failure ' + tag)
        return re.sub('^MDDS_REMOTE_EXIT=0\\s*$', '', text, flags=re.M).strip()
    hashes = {}
    for i, path in enumerate(paths):
        t = shell('platform-' + str(i), 'sha256sum ' + path)
        m = re.fullmatch('([0-9a-f]{64})\\s+' + re.escape(path), t)
        need(m, 'Invalid platform hash')
        hashes[path] = m[1]
    for x in old['files'] + [old['service_server']]:
        need(hashes[x['path']] == x['sha256'], 'Platform differs from declared target ' + x['path'])
    params = []
    for i, x in enumerate(old['parameters']):
        v = shell('parameter-' + str(i), 'uname -a' if x['name'] == 'kernel' else 'param get ' + x['name'])
        need(v == x['value'], 'Platform parameter mismatch')
        params.append(dict(name=x['name'], value=v))
    processes = shell('processes', 'ps -A')
    need(not re.search('fastdds_socket|public_socket|mdds_installed|ordinary_ros2|fastdds_dsoftb|native_permiss|softbus_recor', processes), 'Existing experiment/permission process: stop without killing')
    meta = shell('metadata', f"test -f {token} && test ! -L {token} && stat -c 'META=%F|%a|%u|%g|%s|%i|%Y|%Z' {token} && ls -lZ {token} && getenforce")
    need(re.search('^META=regular file\\|640\\|0\\|3020\\|', meta, re.M) and 'u:object_r:accesstoken_data_file:s0' in meta and re.search('^Enforcing$', meta, re.M), 'Permission metadata mismatch')
    destination = out / (side + '-nativetoken.json')
    command(side + '-token-recv', [hdc, '-t', target, 'file', 'recv', token, str(destination)])
    need(destination.exists() and destination.stat().st_size > 0, 'Missing baseline transfer')
    rows = read(destination)
    need(isinstance(rows, list), 'Preflight constraint failed')
    project = [r for r in rows if r.get('processName') == 'fastdds_dsoftbus_probe']
    need(len(project) == 1 and str(project[0]['tokenId']) == s['tokens'][side] and (project[0]['permissions'] == []), 'Registered own identity/baseline not denied')
    actual = shell('token-hash', 'sha256sum ' + token)
    need(actual.split()[0] == sha(destination), 'Token snapshot changed during collection')
    baselines[side] = sha(destination)
    platform[side] = dict(files=[dict(path=x['path'], sha256=hashes[x['path']]) for x in old['files']], parameters=params, service_server=old['service_server'])
result = dict(schema='LOCAL_MODELED_PREFLIGHT' if local else 'ACTUAL_CURRENT_INSTALLED_PREFLIGHT', eligible_for_executor_gate=True, source='LOCAL_FAKE_CAPTURE' if local else 'REAL_DEVICE_READONLY_CAPTURE', platform=platform, required_platform_paths=reference['required_platform_paths'], permission_helper=s['permission_helper'], permission_baseline_sha256=baselines, source_receipts=receipts, local_contract_test=local, directory_queries=0, permission_mutations=0, data_attempts=0)
(out / 'EXECUTOR_PREFLIGHT.json').write_text(json.dumps(result, indent=2))
(out / 'COMMANDS.json').write_text(json.dumps(receipts, indent=2))
print('CURRENT_READONLY_PREFLIGHT_PASS; directory query/data/grant all NOT_RUN')
