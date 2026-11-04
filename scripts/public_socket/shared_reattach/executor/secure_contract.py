"""Exact task-scoped secure configuration binding; no device or policy operations."""
from pathlib import Path
import hashlib,json
AUTH='TASK_SCOPED_SECURE_CANDIDATE_DEVICE_VALIDATION'
TASK='SECURE_CANDIDATE_TARGET_VALIDATION_FROM_FD7F65D'
BASE='fd7f65d34d499d9fd9f830342aac8755d98db5b1'
FILES=('cert.pem','key.pem','identity_ca.cert.pem','permissions_ca.cert.pem','permissions.p7s','governance.p7s')
INVALID_GOVERNANCE=b'INVALID TEST-ONLY GOVERNANCE SIGNATURE\r\n'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def mode(frozen):return frozen.get('authorization')==AUTH
def topics_for(run):
    namespace='fdds_'+run
    topics=['ros_discovery_info','rt/rosout','rt/parameter_events']
    for qos in ('reliable','best_effort'):
        topics += [f'rt/{namespace}/{qos}/from_{side}' for side in 'AB']
        topics += [f'{prefix}/{namespace}/{qos}/add{suffix}' for prefix,suffix in [('rq','Request'),('rr','Reply')]]
    for side in 'AB':
        for method in ('describe_parameters','get_parameter_types','get_parameters','list_parameters','set_parameters','set_parameters_atomically','get_type_description'):
            topics += [f'{prefix}/{namespace}/transport_{side}/{method}{suffix}' for prefix,suffix in [('rq','Request'),('rr','Reply')]]
    return topics
def case_spec(case):
    if case not in ('S1','S1_FRAGMENT','S2','S3'):raise ValueError('Unknown bounded secure scene')
    return dict(case=case,enabled={'A':True,'B':case!='S2'},fragment_bytes=70000 if case=='S1_FRAGMENT' else 0,
        discovery='SIGN',data_protection='NONE',allow_unauthenticated=False,nonproduction=True)
def material(root):
    root=Path(root).resolve();rows={}
    for side in 'AB':
        rows[side]={}
        for name in FILES:
            file=root/'enclaves'/side/name
            if not file.is_file() or file.is_symlink() or file.resolve()!=file:
                raise ValueError('Missing or linked secure input: '+side+'/'+name)
            rows[side][name]=digest(file)
    return rows
def verify_material(root,expected):
    if material(root)!=expected:raise ValueError('Secure material digest drift')
def recovery_commands(config,side):
    root=config['remote'][side];leaf=root+'/security/enclaves/'+side
    commands={'stat -c %a '+root:('READ_ONLY',None),
        'mkdir -p '+leaf+' && chmod 700 '+root+'/security '+root+'/security/enclaves '+leaf:('SECURE_DIRECTORY_SETUP',None)}
    for name in FILES:
        commands['sha256sum '+leaf+'/'+name]=('READ_ONLY',None)
        commands['cp '+root+'/security_'+name+' '+leaf+'/'+name+' && chmod 600 '+root+'/security_'+name+' '+leaf+'/'+name]=('SECURE_FILE_COPY',name)
    return commands
def recovery_tree(config,side,required,shell):
    """Observe only this run's fixed PKI tree; no deletion, grant or SDK call."""
    root=config['remote'][side]
    if shell('secure-root-mode','stat -c %a '+root).strip()!='700':raise ValueError('Secure retained root is not private')
    directories=[];present=set();leaves=[]
    for index,(relative,allowed) in enumerate([('security',{'enclaves'}),('security/enclaves',{side}),('security/enclaves/'+side,set(FILES))]):
        path=root+'/'+relative
        command='if test ! -e '+path+' && test ! -L '+path+'; then echo __ABSENT__; else test -d '+path+' && test ! -L '+path+' && ls -1A '+path+'; fi'
        output=shell('secure-tree-'+str(index),command).splitlines()
        if output==['__ABSENT__']:continue
        if len(output)!=len(set(output)) or not set(output)<=allowed:raise ValueError('Unowned secure recovery member')
        directories.append(dict(path=path,members=output));present.add(relative)
        if index==2:leaves=output
    if ('security/enclaves' in present and 'security' not in present) or ('security/enclaves/'+side in present and 'security/enclaves' not in present):
        raise ValueError('Inconsistent secure recovery directories')
    if not set(required)<=set(leaves):raise ValueError('Confirmed secure copy missing')
    files=[]
    for index,name in enumerate(sorted(leaves)):
        path=root+'/security/enclaves/'+side+'/'+name
        command='test -f '+path+' && test ! -L '+path+' && sha256sum '+path+" && stat -c 'FILE=%F|%a|%u|%g|%s' "+path
        output=shell('secure-file-'+str(index),command)
        expected=config['secure_candidate']['material']['sha256'][side][name]
        import re
        match=re.search(r'^FILE=regular file\|([0-9]+)\|0\|([0-9]+)\|([0-9]+)$',output,re.M)
        if not re.search(r'^'+expected+r'\s+'+re.escape(path)+r'$',output,re.M) or not match:
            raise ValueError('Secure retained file hash/owner mismatch')
        files.append(dict(path=path,sha256=expected,mode=match[1],gid=match[2],bytes=int(match[3])))
    return dict(root_mode='700',directories=directories,files=files,disposition='RETAINED_CONTROLLED_NONPRODUCTION_PKI')
def verify_pki(scene,task):
    binding=scene.get('pki_inputs',{});file=Path(binding.get('path','')).resolve()
    if not file.is_relative_to(Path(task).resolve()) or not file.is_file() or digest(file)!=binding.get('sha256'):
        raise ValueError('PKI generation receipt missing or changed')
    inputs=json.loads(file.read_text(encoding='utf-8'))
    expected=dict(run=scene['run'],domain=201,default='DENY',plugins=['PKI-DH','Access-Permissions','AES-GCM-GMAC'],
        discovery='SIGN',allow_unauthenticated=False,join_access_control=True,data_protection='NONE',production=False)
    if any(inputs.get(k)!=v for k,v in expected.items()):raise ValueError('PKI permission/protection identity mismatch')
    topics=inputs.get('granted_dds_topics')
    if topics!=topics_for(scene['run']):raise ValueError('Exact run-specific DDS topic permissions changed')
    baseline=scene.get('legitimate_material',{})
    original=Path(baseline.get('root','')).resolve()
    if original!=file.parent:raise ValueError('PKI receipt and legitimate enclave root differ')
    verify_material(original,baseline.get('sha256'))
    selected=material(scene['material']['root'])
    for side in 'AB':
        for name in FILES:
            if scene['case']=='S3' and name=='governance.p7s':
                bad=Path(scene['material']['root'])/'enclaves'/side/name
                if bad.read_bytes()!=INVALID_GOVERNANCE:raise ValueError('S3 corruption differs from frozen Host case')
            elif selected[side][name]!=baseline['sha256'][side][name]:
                raise ValueError('Secure configuration differs beyond declared governance corruption')
def authority(frozen,private,task):
    task=Path(task).resolve();binding=frozen.get('authorization_record',{})
    file=Path(binding.get('path','')).resolve()
    if file!=task/'AUTHORIZATION.json' or not file.is_file() or digest(file)!=binding.get('sha256'):
        raise ValueError('Secure task authority drift')
    grant=json.loads(file.read_text(encoding='utf-8'))
    if not mode(frozen) or grant.get('authorization')!=AUTH or grant.get('task_id')!=TASK or grant.get('base_commit')!=BASE:
        raise ValueError('Secure authority purpose mismatch')
    if grant.get('task_root')!=str(task) or grant.get('targets')!=private['targets'] or grant.get('tokens')!=private['tokens']:
        raise ValueError('Registered secure device identity changed')
    if grant.get('platform_policy_change') is not False or grant.get('production_qualification') is not False:
        raise ValueError('Secure authorization may not widen policy/qualification')
    if any(frozen.get(key) is True for key in ('simulated','local_contract_test')):
        raise ValueError('Local driver cannot claim secure device authority')
    return grant
def window(config,frozen,private,task):
    authority(frozen,private,task)
    scene=config.get('secure_candidate')
    if scene!=frozen.get('secure_candidate'):raise ValueError('Secure case omitted or changed from freeze')
    if not isinstance(scene,dict) or scene.get('spec')!=case_spec(scene.get('case')):
        raise ValueError('Secure scene activation/protection drift')
    binding=scene.get('material',{});root=Path(binding.get('root','')).resolve()
    if not root.is_relative_to(Path(task).resolve()):raise ValueError('Foreign secure material root')
    verify_material(root,binding.get('sha256'))
    verify_pki(scene,task)
    if config.get('main_member')!='ordinary_ros2_transport_consumer' or config.get('main_prefix')!='ROS_MAIN':
        raise ValueError('Secure application/capture identity mismatch')
    if scene.get('run')!=config.get('run'):raise ValueError('Secure run/topic permission identity drift')
    manifest=json.loads(Path(config['build_manifest']).read_text(encoding='utf-8'))
    if manifest.get('abi',{}).get('fastrtps')!='2.14.7' or manifest.get('security_mode')!='ON':
        raise ValueError('Secure candidate build mode mismatch')
    if config.get('timeout_seconds')!=300 or config.get('max_record_bytes')!=8192:
        raise ValueError('Secure window bounds drift')
    return scene
