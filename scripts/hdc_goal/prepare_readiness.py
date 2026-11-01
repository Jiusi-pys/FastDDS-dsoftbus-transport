"""Fixed OFFLINE successor builder. Never imports an executor or live backend.

Only --assemble and --freeze <published-code-commit> are supported. Neither
creates approval/admission/freshness/once/run controls or runs a tool binary.
"""
import ast
import hashlib
import types
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

REPO=Path(__file__).resolve().parents[2]
BASE=Path('E:/Codex-FastDDS-hdc-readiness-package-20261031-d01')
PACKAGE=Path('E:/Codex-FastDDS-readiness-trigger-package-20261102-d01')
ACTIVE=PACKAGE/'active'
RUN=Path('E:/Codex-FastDDS-hdc-ready-once-20261102_6d2b9a73')
OLD_SCOPE='ONE_NEW_FIXED_AB_PRINTF_READINESS_60S_D01'
SCOPE='ONE_NEW_FIXED_AB_PRINTF_TRIGGER_FAMILY_60S_D02'
NONCE='6D2B9A73_READY_60S_D02'
SOURCES=('goal_contract.py','goal_policy.py','goal_authorization.py','goal_driver.py',
 'goal_windows_backend.py','goal_win32_owned.py','goal_handle_lifecycle.py','goal_memory_trace.py',
 'goal_notice.py','goal_storage.py','goal_evidence.py','goal_workflow.py','goal_entry.py','preflight.py','verify_offline.py')
MAINTAINED=('goal_driver.py','goal_windows_backend.py','goal_notice.py')
DYNAMIC=('EXECUTION_AUTHORIZATION_D01.json','MAIN_ADMISSION_D01.json','FRESH_PREFLIGHT_D01.json',
 'ONCE_RESERVED_D01.json','ENTRY_RECEIPT_D01.json')
F7DD_PINS={'goal_driver.py':'0647e817d753e7580712338600fedc7da477d41f015a5f50da117b4eab2cfc75',
 'goal_windows_backend.py':'152994e9df5897a1660ee0cf76c422992fae7fad05a18229b6759f1195c12923'}

def need(value,code):
    if not value:raise ValueError(code)

def digest(data):return hashlib.sha256(data).hexdigest()

def read_safe(path,cap=65536):
    path=Path(path);s=path.lstat()
    need(stat.S_ISREG(s.st_mode) and s.st_nlink==1 and hasattr(s,'st_file_attributes')
         and not s.st_file_attributes&1024 and 0<s.st_size<=cap,'SAFE_FILE_METADATA')
    for parent in path.parents:need(not parent.lstat().st_file_attributes&1024,'SAFE_ANCESTOR')
    with path.open('rb') as f:
        opened=os.fstat(f.fileno());data=f.read(cap+1);finished=os.fstat(f.fileno())
    after=path.lstat()
    need(all((x.st_dev,x.st_ino,x.st_size,x.st_mtime_ns)==(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns)
             for x in (opened,finished,after)) and len(data)==s.st_size,'SAFE_FILE_CHANGED')
    return data

def write_new(path,data):
    with Path(path).open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())

def json_bytes(value):return (json.dumps(value,indent=2,ensure_ascii=True)+'\n').encode('utf-8')

def relocate(data):
    text=data.decode('utf-8-sig')
    for old,new in ((str(BASE).replace('\\','/'),str(PACKAGE).replace('\\','/')),
                    ('E:/Codex-FastDDS-hdc-ready-once-20261031_c84f6d21',str(RUN).replace('\\','/')),
                    ('AGENTIC_REVIEW_CLOSURE_D03.json','AGENTIC_REVIEW_CLOSURE_D04.json'),
                    (OLD_SCOPE,SCOPE),('C84F6D21_READY_60S_D01',NONCE),('C84F6D21','6D2B9A73')):
        text=text.replace(old,new)
    need('C84F6D21' not in text and 'readiness-package-20261031' not in text,'OLD_RUNTIME_LITERAL')
    return text.encode('utf-8')

def checked_contract_bytes(data,expected):
    need(data==expected,'CONTRACT_EXACT_BUFFER');return data

def checked_card_bytes(data):
    need(digest(data)=='79efda4237a1e16e3c76208830a34ff8357b2dd4b7a5d1d854d109774197c2af','BASE_CARD_PIN')
    return data

def checked_source_rows(source):
    names=(*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')
    rows=source.get('sources')
    need(type(rows) is list and len(rows)==len(names),'EXACT_SOURCE_ROW_COUNT')
    for name,x in zip(names,rows):
        need(type(x) is dict and set(x)=={'path','bytes','sha256'}
             and type(x['path']) is str and Path(x['path'])==ACTIVE/name
             and type(x['bytes']) is int and 0<x['bytes']<=65536
             and type(x['sha256']) is str and re.fullmatch('[0-9a-f]{64}',x['sha256']),
             'EXACT_SOURCE_ROW_BEFORE_READ')
    return rows

def checked_budget(owned_bytes,tool_bytes,code_bytes,owned_files,external_files=8):
    # Future run16MiB is separate. Reserve four not-yet-written metadata files,
    # five max64KiB controls and the unchanged256KiB closing space here.
    reserve=4*65536+5*65536+262144
    need(all(type(x) is int and x>=0 for x in (owned_bytes,tool_bytes,code_bytes,owned_files,external_files)), 'BUDGET_TYPES')
    need(owned_bytes+tool_bytes+code_bytes+reserve<=8388608,'OWNED_PREPARATION_AND_ACTIVE_BUDGET')
    need(owned_files+external_files+4+5+32<=128+32,'OWNED_FILE_RESERVE')
    return {'owned_bytes':owned_bytes,'direct_tools_bytes':tool_bytes,'preparation_code_bytes':code_bytes,
            'reserved_bytes':reserve,'upper_bound_bytes':owned_bytes+tool_bytes+code_bytes+reserve,
            'owned_files':owned_files,'external_files':external_files,'reserved_files':41,'file_limit':160,
            'limit':8388608,'future_run_limit':16777216,'combined_limit':25165824}

def owned_metadata():
    todo=[PACKAGE];total=0;files=0;dirs=0
    while todo:
        path=todo.pop();x=path.lstat()
        need(not x.st_file_attributes&1024,'OWNED_REPARSE')
        if stat.S_ISDIR(x.st_mode):
            dirs+=1;need(dirs<=32,'OWNED_DIRECTORY_CAP');todo.extend(path.iterdir())
        else:
            need(stat.S_ISREG(x.st_mode) and x.st_nlink==1,'OWNED_FILE_TYPE')
            total+=x.st_size;files+=1;need(files<=160 and total<=8388608,'OWNED_METADATA_CAP')
    return total,files

def load_contract():
    # Execute the exact verified buffer; never reopen a path through a loader.
    original=read_safe(BASE/'active/goal_contract.py')
    need(digest(original)=='d6e78324403ad485694cf14ee1cfe2067a9f2f447e346546f016487995573700','BASE_CONTRACT_PIN')
    data=checked_contract_bytes(read_safe(ACTIVE/'goal_contract.py'),relocate(original))
    name='_offline_prepared_readiness_contract';value=types.ModuleType(name)
    prior=sys.modules.get(name);sys.modules[name]=value
    try:exec(compile(data,str(ACTIVE/'goal_contract.py'),'exec'),value.__dict__)
    finally:
        if prior is None:sys.modules.pop(name,None)
        else:sys.modules[name]=prior
    need(tuple(value.SOURCE_NAMES)==SOURCES and value.SCOPE==SCOPE and value.NONCE==NONCE
         and value.ACTIVE_ROOT==ACTIVE and value.RUN_ROOT==RUN,'CONTRACT_BINDING')
    return value


def row(path,role=None,cap=65536):
    data=read_safe(path,cap);value={'path':str(path),'bytes':len(data),'sha256':digest(data)}
    if role is not None:value['role']=role
    return value

def assemble():
    need(not os.path.lexists(ACTIVE) and not os.path.lexists(RUN),'OUTPUT_ALREADY_OR_UNKNOWN')
    for parent in (PACKAGE,*PACKAGE.parents):need(not parent.lstat().st_file_attributes&1024,'OUTPUT_ANCESTOR')
    raw=read_safe(BASE/'active/PACKAGE_FINAL_D01.json')
    need(digest(raw)=='4a26857ca902b57762e5d3970672f3d152acbcb3c55818e0b7974e0df1c3bb5d','BASE_MANIFEST')
    baseline=json.loads(raw.decode('utf-8-sig'));pins={x['role']:x for x in baseline['active_pins']}
    payload={};provenance=[]
    for name in SOURCES:
        origin=REPO/'scripts/hdc_goal'/name if name in MAINTAINED else BASE/'active'/name
        data=read_safe(origin)
        if name in F7DD_PINS:need(digest(data)==F7DD_PINS[name],'F7DD_SOURCE_DRIFT')
        if name not in MAINTAINED:need(digest(data)==pins['source.'+name]['sha256'],'BASE_SOURCE_DRIFT')
        payload[name]=data if name in MAINTAINED else relocate(data)
        ast.parse(payload[name],filename=name)
        provenance.append(dict(name=name,origin=str(origin),origin_sha256=digest(data),candidate_sha256=digest(payload[name])))
    schema=json.loads(read_safe(BASE/'active/INPUT_SCHEMAS_D01.json').decode('utf-8-sig'))
    schema.update(scope=SCOPE,nonce=NONCE)
    ACTIVE.mkdir()
    for name,data in payload.items():write_new(ACTIVE/name,data)
    c=load_contract()
    commands={'status':'PROPOSAL_ONLY_NOT_AUTHORITY','scope':SCOPE,'nonce':NONCE,'commands':[
      {'id':'server','argv':[c.HDC,'-s',c.ENDPOINT,'-m']},
      *[{'id':'ready.'+side,'argv':[c.HDC,'-p','-s',c.ENDPOINT,'-t',c.TARGETS[side],'shell',c.body(side)]} for side in ('A','B')]]}
    c.validate_registry(list(c.build_registry(commands).values()))
    write_new(ACTIVE/'COMMANDS_D01.json',json_bytes(commands))
    write_new(ACTIVE/'INPUT_SCHEMAS_D01.json',json_bytes(schema))
    registry=c.build_registry(commands)
    source={'scope':SCOPE,'nonce':NONCE,'sources':[row(ACTIVE/n) for n in (*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')],
      'registry_sha256':c.registry_digest(list(registry.values())),'limits':c.LIMITS,'actual_execution_authorized':False}
    write_new(ACTIVE/'SOURCE_BINDING_D01.json',json_bytes(source))
    write_new(PACKAGE/'prepare/ASSEMBLY_PROVENANCE_D01.json',json_bytes({'f7dd_source_commit':'f7dd076985bfb76961267c4806aa44e4ac85e3f1','provenance':provenance,'SOURCE_BINDING_SHA':digest(read_safe(ACTIVE/'SOURCE_BINDING_D01.json')),'HDC_calls':0}))
    return source

def freeze(commit):
    need(re.fullmatch('[0-9a-f]{40}',commit) is not None,'COMMIT_SHA')
    c=load_contract();need(not os.path.lexists(RUN) and all(not os.path.lexists(ACTIVE/x) for x in DYNAMIC),'DYNAMIC_MUST_BE_ABSENT')
    code_paths=['scripts/hdc_goal/'+n for n in (*MAINTAINED,'prepare_readiness.py')]
    code_paths+=['tests/test_hdc_notice_family.py','docs/HDC_READINESS_TRIGGER_PACKAGE.md']
    for path in code_paths:
        result=subprocess.run(['git','show',commit+':'+path],cwd=REPO,capture_output=True)
        need(result.returncode==0 and result.stdout==read_safe(REPO/path),'PUBLISHED_SOURCE_BYTE_BINDING')
    source=json.loads(read_safe(ACTIVE/'SOURCE_BINDING_D01.json').decode('utf-8-sig'));source_sha=digest(read_safe(ACTIVE/'SOURCE_BINDING_D01.json'))
    for x in checked_source_rows(source):need(row(Path(x['path']))==x,'CANDIDATE_SOURCE_DRIFT')
    for n in MAINTAINED:need(read_safe(ACTIVE/n)==read_safe(REPO/'scripts/hdc_goal'/n),'MAINTAINED_COPY_DRIFT')
    for name in ('FINAL_TEST_RESULT_D01.json','AGENTIC_REVIEW_CLOSURE_D04.json'):
        value=json.loads(read_safe(PACKAGE/'prepare'/name).decode('utf-8-sig'))
        need(value['source_binding_sha256']==source_sha,'EVIDENCE_SOURCE_BINDING')
        if name.startswith('FINAL'):need(value['native_exit']==value['outer_controller_exit']==0,'FINAL_FAILED')
        else:need(value['blocking_findings_open']==0,'REVIEW_OPEN')
    rows=[row(ACTIVE/n,'source.'+n) for n in SOURCES]
    rows +=[row(ACTIVE/n,role) for role,n in (('commands','COMMANDS_D01.json'),('schema','INPUT_SCHEMAS_D01.json'),('source_binding','SOURCE_BINDING_D01.json'))]
    tools=(('tool.hdc',c.HDC,c.HDC_SHA),('tool.python','C:/Users/17715/anaconda3/python.exe','7679e53fa969789309e81fdad0d52b8cda5f83c9abf7cb31a3c58bf24b31e264'))
    for role,path,expected in tools:
        value=row(Path(path),role,6291456);need(value['sha256']==expected,'TOOL_DRIFT');rows.append(value)
    rows +=[row(PACKAGE/'prepare'/n,role) for role,n in (('evidence.final','FINAL_TEST_RESULT_D01.json'),('evidence.review','AGENTIC_REVIEW_CLOSURE_D04.json'))]
    total=sum(x['bytes'] for x in rows);need(total<c.LIMITS['active_bytes'] and len(rows)<=c.LIMITS['active_files'],'ACTIVE_BUDGET')
    owned_bytes,owned_files=owned_metadata()
    budget=checked_budget(owned_bytes,sum(x['bytes'] for x in rows if x['role'].startswith('tool.')),
                          sum(len(read_safe(REPO/path)) for path in code_paths),owned_files,2+len(code_paths))
    original_card=checked_card_bytes(read_safe(BASE/'EXECUTION_CARD_D01.md'))
    m={'format':'READINESS_TRIGGER_PACKAGE2','scope':SCOPE,'nonce':NONCE,'registry_sha256':source['registry_sha256'],'limits':c.LIMITS,'active_pins':rows,
       'actual_execution_authorized':False,'published_source_commit':commit,'preparation_budget':budget,'runtime_platform':'Installed Windows/CPython stdlib,pyd,dll separately accounted; exact loader closure UNKNOWN'}
    mraw=json_bytes(m);write_new(ACTIVE/'PACKAGE_FINAL_D01.json',mraw)
    b={'format':'READINESS_TRIGGER_BINDING2','scope':SCOPE,'nonce':NONCE,'manifest_sha256':digest(mraw),'registry_sha256':source['registry_sha256'],
       'run_root':str(RUN),'active_closure_bytes':total,'full_executor_final':True,'actual_execution_authorized':False,'published_source_commit':commit,
       'qualification':'OFFLINE_FAKE_AND_METADATA_ONLY_NOT_DEVICE'}
    braw=json_bytes(b);write_new(ACTIVE/'FINAL_EXECUTION_BINDING_D01.json',braw)
    card=relocate(original_card).decode('utf-8')
    card=card.replace('4a26857ca902b57762e5d3970672f3d152acbcb3c55818e0b7974e0df1c3bb5d',digest(mraw)).replace('2c734a61e269a9b36a16888e1b10680abeb4b499d78e4a11188aa1a60073f152',digest(braw))
    card+='\n## 本后继版本差异与批准边界\n\n代码提交：'+commit+'；清理修复来源f7dd076985bfb76961267c4806aa44e4ac85e3f1。新包实际采用维护backend/driver，旧冻结包未改。NOTICE_EVENT_V2只增加trigger_family：AUTH_WORD_ONLY/ECODE_PREFIX_ONLY/BOTH/NOT_APPLICABLE。它不证明原句、具体错误码、权限、UI或误报。去重区分family但96上限不增；未知仍阻断。清理OS证据与业务阻断分栏，观测/记录/EOF/identity等失败仍UNKNOWN。仅server日志AUTH_CONTEXT_UNKNOWN原例外可在未来具体批准后继续，client未知、明确请求拒绝/E4等仍停。\n\n本阶段实际HDC/板端/真实信号0，不存在真实AUTH/MAIN/FRESH/ONCE/run。未来必须新单次批准同时绑定本卡、Manifest、Binding；主体/资源/端点/无重复/JIT条件留到当次鲜核。本卡不申请部署或DDS业务，不含新的配对永久信任或权限网络设置。\n'
    write_new(PACKAGE/'EXECUTION_CARD_D01.md',card.encode('utf-8'))
    summary={'manifest_sha256':digest(mraw),'binding_sha256':digest(braw),'card_sha256':digest(card.encode('utf-8')),'source_binding_sha256':source_sha,'source_commit':commit,'active_bytes':total,'EXECUTION_NOT_AUTHORIZED':True}
    write_new(PACKAGE/'prepare/FINAL_HASHES_D01.json',json_bytes(summary));return summary

def main(args):
    try:
        if args==['--assemble']:assemble();print('OFFLINE_ASSEMBLED_HDC0_NO_AUTH');return 0
        if len(args)==2 and args[0]=='--freeze':print(json.dumps(freeze(args[1])));return 0
        print('OFFLINE_ONLY_BAD_ARGUMENTS');return 78
    except BaseException:print('OFFLINE_BUILD_FAILED_PRESERVE_PARTIAL_NO_RETRY');return 1

if __name__=='__main__':raise SystemExit(main(sys.argv[1:]))
