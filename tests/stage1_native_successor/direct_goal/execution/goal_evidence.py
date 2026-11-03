"""Pure fixed evidence parsers; no process/API or private-stream access."""
import re
from enum import Enum
from goal_contract import ContractError,TARGETS,DEVICE_ROOT,OWN,RUN_ROOT,LIMITS

# Source-controlled predicate names only. Never recover a reason by reading
# an exception's args, text, repr, input, decoder details or arbitrary class name.
EvidenceReason=Enum('EvidenceReason',{name:name for name in '''
BOARD_DISK_MARGIN BOARD_MEMORY_MARGIN CLIENT_LOCAL_EXIT CLIENT_NATIVE_NONZERO
CONTROLLER_STDIO_UNKNOWN CURRENT_CONTROLLER_RESULT DATA_RESULT_NOT_COMPLETE
DF_HEADER_UNKNOWN DF_RECORD_UNKNOWN ENTRY_NOT_RESTORED_NORMAL EXEC_PROBE_FAILED
FIXED_OWN_CPP_ABI NATIVE_CREATED_CHILD_BINDING NATIVE_NOT_NORMAL_ZERO
NATIVE_POSTWAIT_RSS NATIVE_REMOTE_NONZERO NATIVE_SUPERVISION_RECORDS
NATIVE_UNEXPLAINED_ERRNO ORDERED_RESOURCE_SENTINELS OWN_FIXED_MAPPING
OWN_QUERY_CALLER_CHANGED OWN_QUERY_COUNT OWN_QUERY_DUPLICATE OWN_QUERY_UNAVAILABLE
OWN_SINGLE_PERMISSION_NOT_QUALIFIED OWN_TYPE_APL PARENT_PATH_METADATA_UNKNOWN
PROVIDER_BYTES_DIFFER_FROM_CACHED_NATIVE_INPUT PROVIDER_FORMAT PROVIDER_HASH_FORMAT
PROVIDER_RECORD_COUNT PROVIDER_STAT PROVIDER_STDERR_UNKNOWN RECORD_DUPLICATE
RECORD_FIELDS RECORD_FRAME RECORD_LINE RECORD_NUMERIC RECORD_SCHEMA RECORD_TEXT
REMOTE_BASE_RC_MISSING_OR_NONZERO REMOTE_DENIED REMOTE_DUPLICATE_MARKER
REMOTE_HASH_FORMAT REMOTE_STEP_RC RESOLVED_ROOT_UNKNOWN RESOURCE_FULL_RECORD_COUNT
ROOT_ABSENCE_NOT_PROVED_ENOENT ROOT_STAT_MARKER STEP_STDERR_UNKNOWN STEP_STDOUT_BOUND
TARGETS_NOT_EXACT_A_B TARGET_FORMAT_UNKNOWN TARGET_STDERR_UNKNOWN TEST_ROOT_ALREADY_EXISTS
TRANSFER_BYTES_MISMATCH UNEXPECTED_STEP_BODY UNIQUE_ORDERED_MEMORY_FIELDS
TEXT_DECODE_FAILED UNCLASSIFIED_EXCEPTION UNCLASSIFIED_EVIDENCE
'''.split()})

class EvidenceRejected(ContractError):
    def __init__(self,code):
        self.reason=(EvidenceReason.__members__.get(code,EvidenceReason.UNCLASSIFIED_EVIDENCE)
                     if type(code) is str else EvidenceReason.UNCLASSIFIED_EVIDENCE)
        super().__init__('EVIDENCE_REJECTED')

def need(ok,code):
    if not ok:raise EvidenceRejected(code)

def rejection_record(step,error):
    # The caller is a fixed registry step; still refuse arbitrary identifiers in
    # the exported diagnostic. This is a local evaluation stage, not remote/UI time.
    identifier=step.id
    fixed=('targets','readonly.1','readonly.2','readonly.3','readonly.4')
    identifier=identifier if type(identifier) is str and identifier in fixed else 'OTHER_FIXED_STEP'
    reason=EvidenceReason.UNCLASSIFIED_EXCEPTION;predicate='EVALUATOR_EXCEPTION'
    if type(error) is EvidenceRejected and type(error.reason) is EvidenceReason:
        reason=error.reason;predicate='SOURCE_REQUIREMENT'
    elif type(error) is UnicodeDecodeError:
        # Both strict UTF8 query and strict ASCII controller decoders exist.
        # Do not claim the encoding or inspect the decoder's potentially private data.
        reason=EvidenceReason.TEXT_DECODE_FAILED;predicate='STRICT_TEXT'
    return {'format':'EVIDENCE_REJECTION1','step':identifier,'stage':'EVIDENCE_EVALUATION',
            'predicate':predicate,'reason':reason.value,'accepted':False}

A=TARGETS["A"];B=TARGETS["B"]
# Exact cached board interpreter bytes used by the sealed current Native link.
# A fresh mismatch is a dependency HOLD before staging/Native; not an ABI verdict.
INTERPRETER_SHA='d3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86'

SCHEMAS={
 'S1GOALBOOT1':set('format side pid parent birth caller uid euid gid egid wall mono image image_sha'.split()),
 'S1GOALNODE1':set('format run side pid parent target network udid image_sha'.split()),
 'S1GOALPROBE1':set('format run pid entry_selected one_owned_worker_selected all_SDK_workers project_business_calls'.split()),
 'S1GOALEXIT1':set('format run pid status restored_entry restore_rc_valid restore_rc worker_parent_restoration SDK_quiet phase native_rc_valid native_rc'.split()),
 'S1GOALRESULT1':set('format status result_status phase writes takes local_control_drained owners_retained SDK_quiescence_unproven destroy_rc primary_phase native_rc_valid native_rc'.split()),
}
def safe_records(body):
    need(0<len(body)<=16384 and body.endswith('\n'),'RECORD_FRAME')
    records=[];current=None
    for line in body.splitlines():
        if not line:continue
        need('=' in line and len(line)<=1024,'RECORD_LINE');key,value=line.split('=',1)
        need(re.fullmatch(r'[A-Za-z0-9_]+',key) and value and all(33<=ord(c)<=126 for c in value),'RECORD_TEXT')
        if key=='format':
            if current is not None:records.append(current)
            need(value in SCHEMAS,'RECORD_SCHEMA');current={'format':value}
        else:need(current is not None and key not in current,'RECORD_DUPLICATE');current[key]=value
    if current is not None:records.append(current)
    for record in records:
        need(set(record)==SCHEMAS[record['format']],'RECORD_FIELDS')
        for key,value in record.items():
            if key in ('format','side','image','image_sha','network','udid','all_SDK_workers','worker_parent_restoration','SDK_quiet'):continue
            need(re.fullmatch(r'-?\d+',value) is not None,'RECORD_NUMERIC')
    return records

def step_stdout(step,data):
    need(len(data)<=65536,'STEP_STDOUT_BOUND')
    text=data.decode('utf-8','strict').replace('\r\n','\n')
    marker='S1_GOAL_STEP:'+step.id+':'
    matches=re.findall('^'+re.escape(marker)+r'(-?\d+)$',text,re.M)
    need(len(matches)==1 and matches[0]==str(step.expected_remote) and text.count(marker)==1,'REMOTE_STEP_RC')
    return text

def native_supervision(text):
    created=re.findall(r'^DIRECT_SUP_CREATED parent=(\d+) child=(\d+) generation=UNVERIFIED no_signal_authority=1$',text,re.M)
    begin=re.findall(r'^DIRECT_SUP_BEGIN parent=(\d+) child=(\d+) birth=(\d+) scope=DIRECT_CHILD_ONLY descendants=UNKNOWN$',text,re.M)
    result=re.findall(r'^DIRECT_SUP_RESULT (.+)$',text,re.M)
    need(len(created)==len(begin)==len(result)==1,'NATIVE_SUPERVISION_RECORDS')
    need(created[0]==begin[0][:2] and all(int(v)>0 for v in begin[0]),'NATIVE_CREATED_CHILD_BINDING')
    values=dict(item.split('=',1) for item in result[0].split())
    expected={'child':created[0][1],'phase':'3','status_valid':'1','raw_status':'0','signal_sent':'0','timeout':'0','cancelled':'0','late':'0','root_reaped':'1','descendants':'UNKNOWN','no_group_signal':'1'}
    need(all(values.get(k)==v for k,v in expected.items()),'NATIVE_NOT_NORMAL_ZERO')
    need(0<int(values['peak_rss_kib'])<=524288,'NATIVE_POSTWAIT_RSS')
    need(int(values['errno']) in (0,4),'NATIVE_UNEXPLAINED_ERRNO')
    return {'created_parent':int(created[0][0]),'created_child':int(created[0][1]),'birth':int(begin[0][2]),'RSS_kib':int(values['peak_rss_kib']),'descendants':'UNKNOWN'}

def own_query(text):
    rows=re.findall(r'^OWN_QUERY (.+)$',text,re.M);need(len(rows)==1,'OWN_QUERY_COUNT')
    pairs=rows[0].split();need(len(set(p.split('=',1)[0] for p in pairs))==len(pairs),'OWN_QUERY_DUPLICATE')
    values=dict(p.split('=',1) for p in pairs)
    need(values.get('name')==OWN and values.get('bridge_cpp')=='__h','FIXED_OWN_CPP_ABI')
    for name in ['raw_rc','info_rc','query_rc']:need(values.get(name)=='0','OWN_QUERY_UNAVAILABLE')
    need(values.get('type')==values.get('native_type') and values.get('apl')==values.get('normal_apl'),'OWN_TYPE_APL')
    need(values.get('query_count')=='1' and values.get('granted')=='1' and int(values.get('called','0'))&31==31,'OWN_SINGLE_PERMISSION_NOT_QUALIFIED')
    need(values.get('caller_before')==values.get('caller_bridge')==values.get('caller_after') and int(values['caller_before'])>0,'OWN_QUERY_CALLER_CHANGED')
    need(values.get('no_identity_setter')=='1' and 0<int(values['lookup'])<=4294967295,'OWN_FIXED_MAPPING')
    return {'target':int(values['lookup']),'caller':int(values['caller_before']),'Native':True,'normalAPL':True,'single_DATASYNC':'GRANTED','full_permission_set':'NOT_ENUMERATED'}

def evaluate_step(step,stdout,stderr,native_exit):
    """Transport acceptance is not the final DDS/restore/data acceptance."""
    need(native_exit==0,'CLIENT_LOCAL_EXIT')
    if step.kind=='controller':
        from goal_authorization import safe_input
        need(not stdout and not stderr,'CONTROLLER_STDIO_UNKNOWN')
        result=safe_input(RUN_ROOT/'control/data/controller-result',512).decode('ascii')
        expected='S1CONTROLLER1\nfixture=0\nrun=5c7a9e2130d0aa61\nROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1\nNative_attestation=0\nentry_restore_and_exit_checked_separately=1\n'
        need(result==expected,'CURRENT_CONTROLLER_RESULT')
        return {'controller_protocol_complete':True,'native_attestation':False,'entry_restore_checked_separately':True}
    if step.id=='targets':return parse_client('RO-1',stdout,stderr,native_exit)
    if step.id.startswith('readonly.'):
        aliases={'readonly.1':'RO-A-provider','readonly.2':'RO-A-resource-path','readonly.3':'RO-B-provider','readonly.4':'RO-B-resource-path'}
        return parse_client(aliases[step.id],stdout,stderr,native_exit)
    if step.kind in ('send','receive'):
        # File protocol0 is only transfer completion. Staging verification and
        # native admission/full envelope checks remain mandatory successors.
        return {'file_protocol_local_exit':0,'remote_byte_association':'PENDING_VERIFICATION','remote_shell_rc':'N/A'}
    need(not stderr.strip(),'STEP_STDERR_UNKNOWN')
    text=step_stdout(step,stdout)
    if step.kind=='long':
        need(re.findall(r'^S1_GOAL_REMOTE_RC:(-?\d+)$',text,re.M)==['0'],'NATIVE_REMOTE_NONZERO')
        return {'remote_step_rc':0,'native_root':native_supervision(text)}
    if step.id.startswith('own.'):
        return {'remote_step_rc':0,'own':own_query(text),'native_root':native_supervision(text)}
    body=text[:text.index('S1_GOAL_STEP:')].strip()+'\n'
    if step.byte_source:
        from goal_authorization import safe_input
        from goal_contract import sha
        lines=[line for line in body.splitlines() if line]
        need(len(lines)==1 and re.fullmatch(r'[0-9a-fA-F]{64}\s+\*?[^\r\n]+',lines[0]),'REMOTE_HASH_FORMAT')
        actual=lines[0].split()[0].lower();expected=sha(safe_input(step.byte_source,65536))
        need(actual==expected,'TRANSFER_BYTES_MISMATCH')
        return {'remote_step_rc':0,'source_sha256':expected,'remote_sha256':actual}
    if step.id.endswith(('.boot','.node','.result')):
        records=safe_records(body)
        if step.id.endswith('.result'):
            exits=[r for r in records if r['format']=='S1GOALEXIT1'];need(len(exits)==1 and exits[0]['status']=='0' and exits[0]['restored_entry']=='1','ENTRY_NOT_RESTORED_NORMAL')
            if step.id.startswith('data.'):
                results=[r for r in records if r['format']=='S1GOALRESULT1'];need(len(results)==1 and all(results[0][k]==v for k,v in {'status':'0','result_status':'0','phase':'4','writes':'3','takes':'3','local_control_drained':'1','owners_retained':'0','destroy_rc':'0'}.items()),'DATA_RESULT_NOT_COMPLETE')
            else:
                probes=[r for r in records if r['format']=='S1GOALPROBE1'];need(len(probes)==1 and probes[0]['entry_selected']==probes[0]['one_owned_worker_selected']=='1' and probes[0]['project_business_calls']=='0','EXEC_PROBE_FAILED')
        return {'remote_step_rc':0,'records':records}
    need(not body.strip(),'UNEXPECTED_STEP_BODY')
    return {'remote_step_rc':0}

def parse_client(case, stdout, stderr, native_exit):
    need(native_exit == 0, 'CLIENT_NATIVE_NONZERO')
    # Parse only fixed query outputs, never the server's authentication stream.
    text = stdout.decode('utf-8', errors='strict').replace('\r\n', '\n')
    err = stderr.decode('utf-8', errors='strict')
    need(not any(x in err.lower() for x in ('permission denied', 'operation not permitted', 'unauthorized')), 'REMOTE_DENIED')
    if case == 'RO-1':
        need(not err.strip(), 'TARGET_STDERR_UNKNOWN')
        rows = [x for x in text.splitlines() if x.strip()]
        found = []
        for line in rows:
            # Comparison HdcServer::BuildDaemonVisableLine: key, transport,
            # status, device name. Current binary/source association is unproved.
            m = re.fullmatch(r'(\S+)\s+(USB|TCP)\s+Connected\s+([^\r\n]+)', line)
            need(m is not None, 'TARGET_FORMAT_UNKNOWN')
            found.append(m.group(1))
        need(len(found) == 2 and set(found) == {A, B}, 'TARGETS_NOT_EXACT_A_B')
        return {'targets': found, 'native_exit': native_exit}
    marker = '__RO_RC' if case.endswith('provider') else '__RO_BASE_RC'
    matches = re.findall(r'^' + marker + r'=(-?\d+)$', text, re.M)
    need(len(matches) == 1 and matches[0] == '0', 'REMOTE_BASE_RC_MISSING_OR_NONZERO')
    need(len(re.findall(re.escape(marker) + '=', text)) == 1, 'REMOTE_DUPLICATE_MARKER')
    if case.endswith('provider'):
        need(not err.strip(), 'PROVIDER_STDERR_UNKNOWN')
        body = [x for x in text.splitlines() if x and not x.startswith('__RO_RC=')]
        need(len(body) == 7, 'PROVIDER_RECORD_COUNT')
        need(body[0].startswith('Linux ') and all(x.startswith('/') for x in body[1:3]), 'PROVIDER_FORMAT')
        need(all(re.fullmatch(r'\d+\|\d+\|\d+\|[0-7]+', x) for x in body[3:5]), 'PROVIDER_STAT')
        for x, path in zip(body[5:7], ('/lib/ld-musl-aarch64.so.1', '/system/lib/ld-musl-aarch64.so.1')):
            need(re.fullmatch(r'[0-9a-fA-F]{64}\s+\*?' + re.escape(path), x) is not None, 'PROVIDER_HASH_FORMAT')
            need(x.split()[0].lower()==INTERPRETER_SHA,'PROVIDER_BYTES_DIFFER_FROM_CACHED_NATIVE_INPUT')
        return {'remote_RC': 0, 'provider': body}
    roots = re.findall(r'^__RO_ROOT_STAT_RC=(-?\d+)$', text, re.M)
    need(len(roots) == 1 and len(re.findall('__RO_ROOT_STAT_RC=', text)) == 1, 'ROOT_STAT_MARKER')
    need(roots[0] != '0', 'TEST_ROOT_ALREADY_EXISTS')
    root_path = DEVICE_ROOT
    need(re.fullmatch(r"stat: (?:cannot stat )?['\"]?" + re.escape(root_path)
                      + r"['\"]?: No such file or directory\s*", err) is not None, 'ROOT_ABSENCE_NOT_PROVED_ENOENT')
    lines=[x for x in text.splitlines() if x]
    need(len(lines)==10,'RESOURCE_FULL_RECORD_COUNT')
    total=re.fullmatch(r'MemTotal:\s+(\d+)\s+kB',lines[0])
    available=re.fullmatch(r'MemAvailable:\s+(\d+)\s+kB',lines[1])
    need(total is not None and available is not None
         and 0<int(total[1])>=int(available[1]),'UNIQUE_ORDERED_MEMORY_FIELDS')
    need(int(total[1])>=LIMITS['board_total_kib'] and int(available[1])>=LIMITS['board_available_kib'],'BOARD_MEMORY_MARGIN')
    need(re.fullmatch(r'Filesystem\s+1K-blocks\s+Used\s+Available\s+Use%\s+Mounted on',lines[2]) is not None,'DF_HEADER_UNKNOWN')
    disk=re.fullmatch(r'\S+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+%\s+/data(?:/local/tmp)?',lines[3])
    need(disk is not None,'DF_RECORD_UNKNOWN');need(int(disk[3])>=LIMITS['board_disk_kib'],'BOARD_DISK_MARGIN')
    need(lines[4]=='/data/local/tmp','RESOLVED_ROOT_UNKNOWN')
    need(all(re.fullmatch(r'directory\|[0-7]+\|\d+\|\d+',x) for x in lines[5:8]),'PARENT_PATH_METADATA_UNKNOWN')
    need(lines[8]=='__RO_BASE_RC=0' and lines[9]=='__RO_ROOT_STAT_RC='+roots[0]
         and 1<=int(roots[0])<=255,'ORDERED_RESOURCE_SENTINELS')
    return {'remote_base_RC':0,'root_stat_RC':int(roots[0]),'root_absence':'ENOENT_WITH_BASE_ACCESS','MemTotal_KiB':int(total[1]),'MemAvailable_KiB':int(available[1]),'data_available_KiB':int(disk[3]),'resource_scope':'one fresh query; not a hard reservation'}
