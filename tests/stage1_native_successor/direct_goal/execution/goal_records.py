"""Pure current-run BOOT/OWN/node -> LF/no-BOM input production.
Records bind the operator's approved run; they do not grant OS identity rights.
"""
import re
from goal_contract import need,DEVICE_ROOT,TARGETS,OWN,PACKAGE,sha

def number(value,maximum=18446744073709551615,positive=True):
    need(isinstance(value,(str,int)) and re.fullmatch(r'0|[1-9]\d*',str(value)),'CANONICAL_NUMBER')
    result=int(value);need((result>0 or not positive) and result<=maximum,'NUMBER_RANGE');return result

def boot_context(boot,side,created,artifacts):
    need(boot.get('format')=='S1GOALBOOT1' and boot.get('side')==side and side in TARGETS,'BOOT_SIDE')
    pid=number(boot['pid'],4294967295);parent=number(boot['parent'],4294967295);birth=number(boot['birth'])
    need(pid==created['created_child'] and parent==created['created_parent'] and birth==created['birth'],'BOOT_CREATED_PROCESS')
    need(boot['image']==DEVICE_ROOT+'/own_goal_launcher' and boot['image_sha']==artifacts['launcher']['sha256'],'BOOT_LAUNCHER_IMAGE')
    for name in ['uid','euid','gid','egid']:number(boot[name],4294967295,False)
    return {'pid':pid,'parent':parent,'birth':birth,'caller':number(boot['caller']),'wall':number(boot['wall']),'mono':number(boot['mono'])}

def admission(boot,own,side,created,artifacts,run,manifest_sha,intent):
    need(intent in ('own','traffic') and re.fullmatch('[0-9a-f]{16}',run) and run!='0'*16,'RUN_OR_INTENT')
    need(re.fullmatch('[0-9a-f]{64}',manifest_sha),'MANIFEST_HASH')
    ctx=boot_context(boot,side,created,artifacts)
    need(own.get('Native') is True and own.get('normalAPL') is True and own.get('single_DATASYNC')=='GRANTED','OWN_NOT_QUALIFIED')
    target=number(own['target'],4294967295)
    namespace='md_'+manifest_sha;prefix=PACKAGE+'.dds.'+run
    fields=[('format','S1ENTRY1'),('intent',intent),('run',run),('side',side),('board',TARGETS[side]),('own',OWN),('caller',str(ctx['caller'])),('target',str(target)),('issued',str(ctx['wall'])),('until',str(ctx['wall']+120)),('frontend',DEVICE_ROOT+'/own_goal_launcher'),('frontend_sha',artifacts['launcher']['sha256']),('bridge',DEVICE_ROOT+'/lib/libstage1_actionbag_identity.so'),('bridge_sha',artifacts['bridge']['sha256']),('consumer',DEVICE_ROOT+'/own_goal_runner'),('consumer_sha',artifacts['app']['sha256']),('package',PACKAGE),('namespace',namespace),('prefix',prefix),('domain','151')]
    data=''.join(k+'='+v+'\n' for k,v in fields).encode('ascii');need(len(data)<=4096,'ADMISSION_SIZE')
    return data

def pair_record(boots,nodes,owns,created,artifacts,run,manifest_sha):
    need(set(boots)==set(nodes)==set(owns)==set(created)=={'A','B'},'TWO_FIXED_SIDES')
    need(re.fullmatch('[0-9a-f]{16}',run) and re.fullmatch('[0-9a-f]{64}',manifest_sha),'PAIR_RUN')
    rows=[('format','S1DIRECTPAIR1'),('run',run),('image_sha',artifacts['app']['sha256']),('bridge_sha',artifacts['bridge']['sha256']),('namespace','md_'+manifest_sha),('domain','151'),('package',PACKAGE),('prefix',PACKAGE+'.dds.'+run)]
    for side in ['A','B']:
        ctx=boot_context(boots[side],side,created[side],artifacts);node=nodes[side]
        need(node.get('format')=='S1GOALNODE1' and node.get('side')==side and number(node['run'])==int(run,16),'NODE_RUN_SIDE')
        need(number(node['pid'])==ctx['pid'] and number(node['parent'])==ctx['parent'] and number(node['target'],4294967295)==number(owns[side]['target'],4294967295) and node['image_sha']==artifacts['app']['sha256'],'NODE_BOOT_IDENTITY_IMAGE')
        for name in ['network','udid']:need(isinstance(node[name],str) and 0<len(node[name])<=64 and all(33<=ord(c)<=126 for c in node[name]),'NODE_IDENTIFIER')
        prefix=side.lower()+'_';values=[('pid',str(ctx['pid'])),('parent',str(ctx['parent'])),('board',TARGETS[side]),('token',str(owns[side]['target'])),('udid',node['udid']),('network',node['network']),('slot','101' if side=='A' else '102'),('initiator',PACKAGE+'.dds.'+run+'.T'+side)]
        rows.extend((prefix+k,v) for k,v in values)
    need(nodes['A']['udid']!=nodes['B']['udid'],'DISTINCT_NATIVE_NODES')
    data=''.join(k+'='+v+'\n' for k,v in rows).encode('ascii');need(len(data)<=8192,'PAIR_SIZE');return data

def delivery_receipt(run,sequence,frames,accepted_transfers):
    need(re.fullmatch('[0-9a-f]{16}',run) and sequence in (1,2,3,4) and len(frames)==2,'DELIVERY_FRAME')
    for side,frame in zip(['A','B'],frames):
        evidence=accepted_transfers[side]
        need(evidence.get('accepted') is True and evidence.get('side')==side and evidence.get('sequence')==sequence and evidence.get('source_sha256')==sha(frame) and evidence.get('remote_sha256')==sha(frame),'TWO_ACTUAL_BYTE_DELIVERIES_REQUIRED')
    return ('S1DELIVERY1\n'+run+'\n'+str(sequence)+'\n'+sha(frames[0])+'\n'+sha(frames[1])+'\n').encode('ascii')

def qualify_window_result(window,run,side,boot,created,artifacts,launch_receipt,result_receipt):
    """Current fresh window + same supervised exec PID; never infer quiet.

    S1GOALRESULT1 has no run/PID fields. Its association is explicitly the fresh
    unique window, fixed fetch path, current pair and the bound restored EXIT.
    It is not a cryptographic attestation or proof of callback quiescence.
    """
    ctx=boot_context(boot,side,created,artifacts)
    need(launch_receipt.get('accepted') is True and result_receipt.get('accepted') is True,'WINDOW_RECEIPTS_NOT_ACCEPTED')
    observed=launch_receipt['evidence']['parsed']['native_root']
    need(all(observed[k]==created[k] for k in ['created_child','created_parent','birth']),'WINDOW_SUPERVISED_PROCESS_CHANGED')
    rows=result_receipt['evidence']['parsed']['records']
    expected={'S1GOALEXIT1','S1GOALPROBE1' if window=='probe' else 'S1GOALRESULT1'}
    need(len(rows)==2 and {r['format'] for r in rows}==expected,'WINDOW_EXACT_FINAL_RECORDS')
    exit_record=next(r for r in rows if r['format']=='S1GOALEXIT1')
    need(number(exit_record['run'])==int(run,16) and number(exit_record['pid'])==ctx['pid'],'WINDOW_FINAL_RUN_PID')
    need(all(exit_record[k]==v for k,v in {'status':'0','restored_entry':'1','restore_rc_valid':'1','restore_rc':'0','worker_parent_restoration':'UNKNOWN','SDK_quiet':'UNKNOWN'}.items()),'WINDOW_RESTORE_NOT_PROVED')
    if window=='probe':
        probe=next(r for r in rows if r['format']=='S1GOALPROBE1')
        need(number(probe['run'])==int(run,16) and number(probe['pid'])==ctx['pid'],'PROBE_RUN_PID')
    return {'window':window,'side':side,'run':run,'supervised_pid':ctx['pid'],'entry_restored_readback':True,'worker_parent_restore':'UNKNOWN','SDK_quiet':'UNKNOWN','result_association':'fresh_window_fixed_path_pair_and_exit'}
