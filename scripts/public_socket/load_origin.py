"""Read-only validation of one live /proc capture. Static ELF is not substituted."""
import argparse
import hashlib
import json
import re
from pathlib import Path, PurePosixPath


def analyze(directory, config, side):
    directory = Path(directory)
    errors = []
    evidence = {}
    def read(name):
        try:
            data = (directory/name).read_bytes()
            evidence[name] = hashlib.sha256(data).hexdigest()
            return data.decode('utf-8', errors='strict')
        except (OSError, UnicodeError) as error:
            errors.append(f'{name}: {type(error).__name__}')
            return ''
    def check(condition, reason):
        if not condition: errors.append(reason)
    status = {}
    for line in read('load-status.txt').splitlines():
        if '=' in line:
            key, value = line.split('=',1)
            check(key not in status, 'DUPLICATE_STATUS_KEY')
            status[key] = value
    modeled = config.get('simulated') is True
    check(status.get('scope') == 'LOCAL_MODELED_CAPTURE' if modeled else
          status.get('scope') != 'LOCAL_MODELED_CAPTURE', 'LOAD_CAPTURE_MODE_SCOPE_MISMATCH')
    remote = config['remote'][side]
    installed = config.get('scenario') == 'INSTALLED_RUNTIME'
    ros = config.get('scenario') == 'ROS2_DSOFTBUS'
    dds214 = config.get('scenario') == 'DDS214_LAYER_BASELINE'
    member = config.get('main_member') if installed or ros or dds214 else 'public_socket_dds_experiment'
    check(not installed or member == 'mdds_installed_consumer', 'INSTALLED_MAIN_MEMBER_MISMATCH')
    check(not ros or member == 'ordinary_ros2_transport_consumer', 'ROS_MAIN_MEMBER_MISMATCH')
    check(not dds214 or member == 'fastdds_dsoftbus_dds214_baseline','DDS214_MAIN_MEMBER_MISMATCH')
    expected_exe = remote+'/'+str(member)
    check(status.get('status') == 'COMPLETE', 'SAMPLER_NOT_COMPLETE')
    check(status.get('run') == config['run'], 'SAMPLER_RUN_MISMATCH')
    try: collector=json.loads(read('load-collector.json'))
    except (ValueError,TypeError): collector={};errors.append('COLLECTOR_JSON_INVALID')
    if not isinstance(collector,dict):collector={};errors.append('COLLECTOR_JSON_TYPE')
    check(collector.get('run')==config['run'] and collector.get('side')==side,'COLLECTOR_RUN_SIDE_MISMATCH')
    check(str(collector.get('target_pid'))==status.get('pid'),'COLLECTOR_APP_PID_MISMATCH')
    check(collector.get('pass') is True and collector.get('remoteExit')==0 and collector.get('hdcExit')==0,'COLLECTOR_INCOMPLETE')
    check(collector.get('collectorClosed') is True and collector.get('signal') is None,'COLLECTOR_NOT_CLOSED_OR_CANCELLED')
    output=collector.get('output_bytes')
    check(isinstance(output,dict) and output.get('complete') is True and output.get('overflow')==0,'COLLECTOR_OUTPUT_INCOMPLETE')
    check(bool(re.fullmatch(r'[1-9][0-9]*',status.get('pid',''))),'PID_INVALID')
    check(read('load-exe.txt').strip() == expected_exe,'EXE_PATH_MISMATCH')
    check(read('load-cwd.txt').strip() == remote,'CWD_MISMATCH')
    vector_format=status.get('vector_format','LEGACY_NEWLINE')
    check(vector_format in {'NUL_V1','LEGACY_NEWLINE'},'UNKNOWN_PROC_VECTOR_FORMAT')
    def vector(name):
        value=read(name)
        if vector_format=='NUL_V1':
            check(bool(value) and value.endswith('\0'),'UNTERMINATED_PROC_VECTOR:'+name)
            parts=value[:-1].split('\0') if value.endswith('\0') else []
            check(all(parts),'EMPTY_PROC_VECTOR_ELEMENT:'+name)
            return parts
        check('\0' not in value,'UNDECLARED_NUL_VECTOR:'+name)
        return value.splitlines()
    environment = {}
    for line in vector('load-environ.txt'):
        if '=' in line:
            key,value=line.split('=',1)
            check(key not in environment,'DUPLICATE_ENVIRONMENT_KEY')
            environment[key]=value
    check(environment.get('PATH') == '/system/bin:/system/xbin','PATH_MISMATCH')
    check(environment.get('LD_LIBRARY_PATH') == remote+':'+':'.join(config['system_library_dirs']),'LD_LIBRARY_PATH_MISMATCH')
    allowed_environment={'PATH','LD_LIBRARY_PATH','SKIP_DEFAULT_XML','PWD','OLDPWD','SHLVL','_'}
    if ros:
        expected_ros_environment={
            'ROS_LOG_DIR':remote+'/ros-log',
            'RMW_IMPLEMENTATION':'rmw_fastrtps_cpp','ROS_DOMAIN_ID':'201',
            'ROS_AUTOMATIC_DISCOVERY_RANGE':'SUBNET',
            'FASTDDS_DSOFTBUS_REQUIRED_CONFIG':remote+'/ros.conf',
            'FASTDDS_DSOFTBUS_NATIVE_TOKEN':config['token'][side],
            'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED':'1',
            'FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX':remote+'/control'}
        if config.get('bounded_native_reaccept'):
            expected_ros_environment['FDDST_BOUNDED_NATIVE_SCENE']='BOUNDED_NORMAL_ONCE'
        allowed_environment.update(expected_ros_environment)
        for name,value in expected_ros_environment.items():
            check(environment.get(name)==value,'ROS_ENVIRONMENT_MISMATCH:'+name)
    if dds214:
        expected_dds_environment={'FASTDDS_DSOFTBUS_REQUIRED_CONFIG':remote+'/dds214.conf','FASTDDS_DSOFTBUS_NATIVE_TOKEN':config['token'][side],
            'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED':'1','FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX':remote+'/control'}
        allowed_environment.update(expected_dds_environment)
        for name,value in expected_dds_environment.items():check(environment.get(name)==value,'DDS214_ENVIRONMENT_MISMATCH:'+name)
    check(set(environment).issubset(allowed_environment),'UNDECLARED_ENVIRONMENT_KEY')
    check(environment.get('SKIP_DEFAULT_XML') == '1','DEFAULT_XML_NOT_DISABLED')
    command = vector('load-cmdline.txt')
    check(bool(command) and command[0] == expected_exe,'ARGV0_MISMATCH')
    if installed:
        expected=[expected_exe,'0' if side=='A' else '1',config['peer'][side],config['package_name'],config['service_prefix'],config['run'],config['token'][side],remote+'/control']
        check(command == expected, 'INSTALLED_ARGV_MISMATCH')
    elif ros:
        check(command==[expected_exe,side,config['run'],'dual'],'ROS_ARGV_MISMATCH')
    elif dds214:
        check(command==[expected_exe,side,config['run'],'normal'],'DDS214_ARGV_MISMATCH')
    else:
        check(command.count('--run') == 1 and '--run' in command and
              command[command.index('--run')+1:command.index('--run')+2] == [config['run']], 'ARGV_RUN_MISMATCH')
    def stat_identity(text):
        end = text.rfind(')')
        try: return (text.split(' ',1)[0],text[end+2:].split()[19])
        except (IndexError,ValueError): return None
    before = stat_identity(read('load-stat-before.txt'))
    after = stat_identity(read('load-stat-after.txt'))
    check(before is not None and before == after == (status.get('pid'),status.get('starttime')), 'PROC_IDENTITY_CHANGED')
    uptime = read('load-uptime.txt').split()
    check(bool(uptime) and re.fullmatch(r'[0-9]+(?:\.[0-9]+)?',uptime[0]) is not None,'MONOTONIC_TIMESTAMP_MISSING')
    mapped = set()
    for line in read('load-maps.txt').splitlines():
        columns = line.split(None,5)
        if len(columns) == 6 and columns[5].startswith('/'):
            mapped.add(columns[5])
    package_paths = [remote+'/'+item['name'] for item in config['package_artifacts']]
    package_names = {item['name'] for item in config['package_artifacts']}
    for expected in package_paths+config['required_platform_paths']:
        check(expected in mapped, 'MISSING_MAPPING:'+expected)
    for actual in sorted(mapped):
        name = PurePosixPath(actual.removesuffix(' (deleted)')).name
        if name in package_names:
            check(actual in package_paths, 'WRONG_PACKAGE_MAPPING:'+actual)
        if actual.startswith('/data/'):
            check(actual in package_paths, 'UNDECLARED_DATA_MAPPING:'+actual)
    return {'schema':1,'complete':not errors,'status':'LOAD_CAPTURE_FAILED' if errors else 'COMPLETE','run':config['run'],'board':side,
            'pid':status.get('pid'),'starttime':status.get('starttime'),
            'monotonic_uptime_seconds':uptime[0] if uptime else None,
            'environment':environment,
            'proc_vector_format':vector_format,
            'collector':collector,
            'scope':'LOCAL_MODELED_CAPTURE_NOT_DEVICE_EVIDENCE' if modeled else 'LIVE_PROC_SNAPSHOT_NOT_FULL_SDK_MODULE_CLOSURE',
            'expected_package_paths':package_paths,'mapped_paths':sorted(mapped),
            'errors':errors,'evidence_sha256':evidence}


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('window',type=Path)
    parser.add_argument('side',choices=['A','B'])
    parser.add_argument('--output',type=Path,help='Fresh derived replay result inside this window; originals are never overwritten')
    args=parser.parse_args()
    config=json.loads((args.window/'window.json').read_text(encoding='utf-8-sig'))
    result=analyze(args.window/args.side/'load',config,args.side)
    target=args.output if args.output else args.window/args.side/'actual-load-origin.json'
    if args.output and not target.resolve().is_relative_to(args.window.resolve()):
        parser.error('Derived replay output must belong to this window')
    with target.open('x',encoding='utf-8') as stream: json.dump(result,stream,indent=2)
    print(json.dumps({'board':args.side,'complete':result['complete'],'errors':result['errors']}))
    raise SystemExit(0 if result['complete'] else 1)
