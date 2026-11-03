"""New Goal scope; no D05 grants, serialized bools or fixture authority reuse.

The operator must independently establish a genuine contemporaneous user reply.
These records bind that decision; they are not an OS grant or proof of consent.
"""
from pathlib import Path
from dataclasses import dataclass,field
import json,os,time,weakref,stat,re
from goal_contract import SCOPE,NONCE,RUN_ROOT,ACTIVE_ROOT,HDC,GoalStep,need,sha,canonical,registry_digest,validate_registry
from types import MappingProxyType

_grants=weakref.WeakSet();_permits=weakref.WeakSet()
SOURCE_NAMES=('goal_contract.py','goal_authorization.py','goal_driver.py','goal_windows_backend.py','goal_win32_owned.py','goal_handle_lifecycle.py','goal_memory_trace.py','goal_notice.py','goal_storage.py','goal_evidence.py','goal_policy.py','goal_records.py','goal_workflow.py','goal_entry.py')
PAYLOAD_TARGETS={'app':'own_goal_runner','launcher':'own_goal_launcher','supervisor':'goal_supervisor','readonly_frontend':'readonly_frontend','product':'lib/libfastdds_dsoftbus_stock_214.so','stockdds':'lib/libfastrtps.so.2.14','foonathan':'lib/libfoonathan_memory-0.7.4.so','tinyxml':'lib/libtinyxml2.so.6','fastcdr':'lib/libfastcdr.so.2','sdkcpp':'lib/libc++_shared.so','identity_bridge':'lib/libstage1_actionbag_identity.so'}
REQUIRED_ROLES=frozenset({'tool.hdc','tool.python','host.controller','schema.inputs','evidence.final','evidence.review',*{'payload.'+n for n in PAYLOAD_TARGETS},*{'source.'+n for n in SOURCE_NAMES}})

def safe_input(path,cap):
    path=Path(path);before=path.lstat()
    need(stat.S_ISREG(before.st_mode) and not before.st_file_attributes&0x400 and before.st_nlink==1 and 0<before.st_size<=cap,'INPUT_METADATA')
    for parent in path.parents:need(not parent.lstat().st_file_attributes&0x400,'INPUT_ANCESTOR_REPARSE')
    # Read from the opened descriptor and compare descriptor/path identities.
    # Actual execution also holds Windows no-write/no-delete pins for the fixed
    # active closure. This helper alone is not a cross-process path lock.
    with path.open('rb') as stream:
        opened=os.fstat(stream.fileno());data=stream.read(cap+1);finished=os.fstat(stream.fileno())
    need((opened.st_dev,opened.st_ino,opened.st_size,opened.st_mtime_ns)==(before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)==(finished.st_dev,finished.st_ino,finished.st_size,finished.st_mtime_ns) and len(data)==before.st_size,'OPEN_INPUT_CHANGED')
    after=path.lstat()
    need((before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns)==(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns),'INPUT_CHANGED')
    return data

def verify_package(package,manifest_sha,binding_sha,registry):
    manifest_bytes=safe_input(package/'GOAL_PACKAGE_FINAL_D01.json',4*1048576)
    binding_bytes=safe_input(package/'FINAL_EXECUTION_BINDING_D01.json',65536)
    need(sha(manifest_bytes)==manifest_sha and sha(binding_bytes)==binding_sha,'FROZEN_PACKAGE_BINDING')
    manifest=json.loads(manifest_bytes.decode('utf-8-sig'));binding=json.loads(binding_bytes.decode('utf-8-sig'))
    need(manifest.get('scope')==SCOPE and manifest.get('nonce')==NONCE and manifest.get('registry_sha256')==registry_digest(list(registry.values())),'FIXED_STEP_REGISTRY')
    need(binding.get('scope')==SCOPE and binding.get('nonce')==NONCE and binding.get('manifest_sha256')==manifest_sha and binding.get('run_root')==str(RUN_ROOT),'FINAL_BINDING_CONTENT')
    need(binding.get('full_executor_final') is True and binding.get('actual_execution_authorized') is False,'PREPARATION_FINAL_REQUIRED')
    pins=manifest.get('active_pins',[]);need(isinstance(pins,list) and 10<=len(pins)<=256,'ACTIVE_CLOSURE_ROWS')
    seen=set();total=0;roles=set();by_role={}
    for row in pins:
        need(set(row)=={'path','sha256','bytes','purpose','role'} and row['purpose'] in ('source','schema','tool','payload','final_evidence') and row['role'] in REQUIRED_ROLES and row['role'] not in roles,'ACTIVE_CLOSURE_ROW');roles.add(row['role']);by_role[row['role']]=row
        path=Path(row['path']);need(path.is_absolute() and str(path).casefold() not in seen and re.fullmatch('[0-9a-f]{64}',row['sha256']),'ACTIVE_PATH_OR_HASH');seen.add(str(path).casefold())
        need(not any(part.casefold() in ('raw','tmp','log','logs','cache','key','keys','.harmony') for part in path.parts) and not path.name.casefold().endswith(('.raw','.log','.pid','.pem','.pub','.key')),'PRIVATE_INPUT_FORBIDDEN')
        need(type(row['bytes']) is int and 0<row['bytes']<=32*1048576 and total+row['bytes']<=32*1048576,'ACTIVE_INPUT_SIZE')
        data=safe_input(path,row['bytes']);need(len(data)==row['bytes'] and sha(data)==row['sha256'],'ACTIVE_INPUT_DRIFT');total+=len(data)
    need(total<=32*1048576 and binding.get('active_closure_bytes')==total,'ACTIVE_CLOSURE_BUDGET')
    need(roles==REQUIRED_ROLES and binding.get('required_roles')==sorted(REQUIRED_ROLES),'EXACT_ACTIVE_ROLE_CLOSURE')
    need(manifest.get('limits')==__import__('goal_contract').LIMITS and binding.get('registry_sha256')==manifest['registry_sha256'],'EXACT_POLICY_BINDING')
    for name in SOURCE_NAMES:need(Path(by_role['source.'+name]['path'])==package/name,'EXACT_SOURCE_ROLE_PATH')
    need(Path(by_role['schema.inputs']['path'])==package/'GOAL_INPUTS_D01.json','EXACT_INPUT_SCHEMA_PATH')
    inputs=json.loads(safe_input(package/'GOAL_INPUTS_D01.json',65536).decode('utf-8-sig'))
    validate_app_fsize(inputs)
    need(set(inputs)=={'payloads','readonly_commands','controller','artifacts'},'FIXED_INPUT_FIELDS')
    need(len(inputs['payloads'])==len(PAYLOAD_TARGETS),'EXACT_PAYLOAD_COUNT')
    for role,target in PAYLOAD_TARGETS.items():
        matches=[row for row in inputs['payloads'] if row.get('target')==target]
        need(len(matches)==1,'EXACT_PAYLOAD_TARGET');row=matches[0];pin=by_role['payload.'+role]
        need(Path(row['source'])==Path(pin['path']) and row['sha256']==pin['sha256'] and row['bytes']==pin['bytes'],'PAYLOAD_ROLE_ASSOCIATION')
    from goal_contract import HDC_SHA
    need(Path(by_role['tool.hdc']['path'])==Path(HDC) and by_role['tool.hdc']['sha256']==HDC_SHA,'FIXED_HDC_ROLE')
    need(Path(by_role['tool.python']['path'])==Path('C:/Users/17715/anaconda3/python.exe'),'FIXED_PYTHON_ROLE')
    controllers=[s for s in registry.values() if s.kind=='controller']
    need(len(controllers)==1 and Path(by_role['host.controller']['path'])==Path(controllers[0].executable) and by_role['host.controller']['sha256']==controllers[0].executable_sha256,'FIXED_CONTROLLER_ROLE')
    from goal_contract import build_registry
    need(registry_digest(build_registry(inputs['payloads'],inputs['readonly_commands'],inputs['controller']))==manifest['registry_sha256'],'REGISTRY_INPUT_ASSOCIATION')
    return manifest,binding

def validate_app_fsize(inputs):
    from goal_contract import LIMITS
    app=[row for row in inputs.get('payloads',[]) if row.get('target')==PAYLOAD_TARGETS['app']]
    need(len(app)==1 and type(app[0].get('bytes')) is int and 0<app[0]['bytes']<=LIMITS['native_fsize'],'APP_EXCEEDS_NATIVE_SEALED_IMAGE_FSIZE')

@dataclass(eq=False)
class GoalGrant:
    manifest_sha:str
    binding_sha:str
    registry:dict
    deadline:float
    seen:set=field(default_factory=set)
    accepted:set=field(default_factory=set)
    live:set=field(default_factory=set)
    terminal:set=field(default_factory=set)
    stopped:bool=False
    def reserve_step(self,identifier):
        need(self in _grants and not self.stopped and time.monotonic()<self.deadline,'NO_LIVE_GOAL_GRANT')
        need(identifier in self.registry and identifier not in self.seen,'STEP_NOT_ALLOWED_OR_USED')
        step=self.registry[identifier];need(set(step.requires)<=self.accepted,'STEP_PRECONDITION')
        need(set(step.requires_started)<=(self.live|self.terminal) and set(step.requires_terminal)<=self.terminal,'STEP_LIFECYCLE_PRECONDITION')
        # A failed/unknown Create does not refund this step or its opportunity.
        self.seen.add(identifier);permit=StepPermit(self,step);_permits.add(permit);return permit
    def accept(self,identifier,receipt):
        need(self in _grants and identifier in self.seen and not self.stopped,'STEP_NOT_STARTED')
        from goal_handle_lifecycle import terminal_receipt_qualified
        need(receipt.get('case')==identifier and terminal_receipt_qualified(receipt),'ROOT_TERMINAL_BINDING_AND_EOF')
        need(receipt.get('tool_sha256')==self.registry[identifier].executable_sha256,'ROOT_TOOL_RECEIPT')
        need(receipt.get('stop_attempts')==0 and receipt.get('native_exit')==0 and not receipt.get('cleanup_errors'),'ROOT_NOT_NORMAL')
        need(receipt.get('accepted_evidence') is True,'EVIDENCE_NOT_ACCEPTED')
        self.accepted.add(identifier)
    def halt(self):self.stopped=True
    def mark_started(self,identifier,receipt):
        need(self in _grants and identifier in self.seen and receipt.get('case')==identifier and receipt.get('created') is True and receipt.get('bound') is True and receipt.get('resumed') is True,'ROOT_NOT_STARTED')
        self.live.add(identifier)
    def mark_terminal(self,identifier,receipt):
        need(self in _grants and identifier in self.seen and receipt.get('case')==identifier and receipt.get('terminal') is True,'ROOT_NOT_TERMINAL')
        self.live.discard(identifier);self.terminal.add(identifier)
    def mark_server_ready(self,receipt,listener_owned):
        need(self in _grants and 'server' in self.live and receipt.get('case')=='server' and receipt.get('bound') is True and receipt.get('resumed') is True and receipt.get('terminal') is False and listener_owned is True,'SERVER_NOT_OWNED_READY')
        self.accepted.add('server')

@dataclass(eq=False)
class StepPermit:
    grant:GoalGrant
    step:GoalStep
    bound:bool=False
    created:bool=False

def validate_approval_shape(approval,manifest_sha,binding_sha):
    need(isinstance(approval,dict) and approval.get('scope')==SCOPE and approval.get('nonce')==NONCE,'NEW_GOAL_SCOPE_REQUIRED')
    need(approval.get('manifest_sha256')==manifest_sha and approval.get('binding_sha256')==binding_sha,'APPROVAL_ARTIFACT_BINDING')
    source=approval.get('source',{})
    need(source.get('kind')=='trusted_direct_user_reply' and source.get('reply_id') and source.get('request_id') and source.get('reply_to_request_verified') is True,'TRUSTED_REPLY_REQUIRED')
    need(source.get('time_precision') in ('minute','second') and source.get('sent_time_original'),'MESSAGE_TIME_PROVENANCE')
    need(approval.get('operator_verified_context') is True and approval.get('fixture') is False,'NOT_OPERATOR_OR_FIXTURE')
    effects=approval.get('effects',{})
    required={'hostkey','automatic_USB_auth_state','existing_peer_Socket_auth','endpoint_write_misdelivery_residual','opaque_external_Job_residual','entry_select_and_restore','Native_project_execution','server_auth_context_unknown_continue'}
    need(set(k for k,v in effects.items() if v is True)==required,'EXACT_EFFECTS_ACK')
    return True # Shape only; never returns a process permit.

def load_actual_grant(package,manifest_sha,binding_sha,registry):
    package=Path(package)
    validate_registry(list(registry.values()))
    # Only this new package's exact records can activate. There are intentionally
    # no such records in preparation/tests; old D05 paths/nonces cannot be passed.
    need(package.is_absolute() and package.resolve()==ACTIVE_ROOT.resolve(),'FIXED_NEW_PACKAGE')
    manifest,binding=verify_package(package,manifest_sha,binding_sha,registry)
    files={name:package/name for name in ['EXECUTION_AUTHORIZATION_D01.json','MAIN_ADMISSION_D01.json','FRESH_PREFLIGHT_D01.json']}
    need(all(f.is_file() for f in files.values()),'NOT_GRANTED')
    values={name:json.loads(safe_input(f,65536).decode('utf-8-sig')) for name,f in files.items()}
    approval=values['EXECUTION_AUTHORIZATION_D01.json'];validate_approval_shape(approval,manifest_sha,binding_sha)
    admission=values['MAIN_ADMISSION_D01.json'];fresh=values['FRESH_PREFLIGHT_D01.json']
    need(admission.get('scope')==SCOPE and admission.get('nonce')==NONCE and admission.get('manifest_sha256')==manifest_sha and admission.get('binding_sha256')==binding_sha,'ADMISSION_BINDING')
    need(admission.get('authorization_sha256')==sha(safe_input(files['EXECUTION_AUTHORIZATION_D01.json'],65536)) and admission.get('fresh_sha256')==sha(safe_input(files['FRESH_PREFLIGHT_D01.json'],65536)),'THREE_INPUT_BINDING')
    validate_fresh(fresh,manifest_sha,binding_sha)
    need(not RUN_ROOT.exists(),'UNKNOWN_PRIOR_RUN')
    reservation=package/'ONCE_RESERVED_D01.json'
    with reservation.open('x',encoding='utf-8') as f:
        json.dump({'scope':SCOPE,'nonce':NONCE,'manifest_sha256':manifest_sha,'binding_sha256':binding_sha,'rule':'never delete/reset/refund unknown outcomes'},f);f.flush();os.fsync(f.fileno())
    grant=GoalGrant(manifest_sha,binding_sha,MappingProxyType(dict(registry)),time.monotonic()+240)
    grant.pin_rows=tuple(MappingProxyType(dict(row)) for row in manifest['active_pins'])
    _grants.add(grant);return grant

def validate_fresh(fresh,manifest_sha,binding_sha):
    from goal_policy import SID,PROFILE
    from goal_contract import LIMITS
    need(set(fresh)=={'scope','nonce','observed_epoch','all_required_pass','context','resource','conflict','package_hashes','job_risk'},'FRESH_FIELD_SET')
    need(fresh['scope']==SCOPE and fresh['nonce']==NONCE and fresh['all_required_pass'] is True and type(fresh['observed_epoch']) in (int,float) and 0<=time.time()-fresh['observed_epoch']<=30,'FRESH_REQUIRED')
    need(fresh['context']=={'sid':SID,'profile':PROFILE,'appcontainer':0,'same_execution_context':True},'FRESH_CONTEXT')
    need(fresh['package_hashes']=={'manifest':manifest_sha,'binding':binding_sha} and fresh['conflict']=={'existing_HDC':False,'port8710_in_use':False,'prior_run':False,'prior_reservation':False},'FRESH_CONFLICT_BINDING')
    need(fresh['job_risk']=='OPAQUE_EXTERNAL_LIFECYCLE_ACCEPTED_BY_THIS_SCOPE','FRESH_JOB_SCOPE')
    resources=fresh['resource'];need(set(resources)=={'commit_available','physical_available','C_free','E_free'} and all(type(v) is int and v>=0 for v in resources.values()),'FRESH_RESOURCE_TYPES')
    need(resources['commit_available']>=LIMITS['start_commit'] and resources['physical_available']>=LIMITS['start_physical'] and min(resources['C_free'],resources['E_free'])>=LIMITS['disk_min'],'FRESH_RESOURCE_MARGIN')

def require_process_permission(exe,args,permit):
    need(type(permit) is StepPermit and permit in _permits and permit.grant in _grants,'NO_ACTUAL_GOAL_PERMIT')
    need(not permit.bound and not permit.grant.stopped and time.monotonic()<permit.grant.deadline,'PERMIT_BIND_ONCE')
    need(tuple([str(exe).replace('\\','/'),*args])==permit.step.argv(),'FIXED_EXECUTABLE_ARGV')
    permit.bound=True

def consume_creation(exe,args,case,dest,expected_sha,permit):
    need(type(permit) is StepPermit and permit in _permits and permit.bound and not permit.created,'CREATE_ONCE')
    need(not permit.grant.stopped and time.monotonic()<permit.grant.deadline,'GOAL_EXPIRED')
    need(tuple([str(exe).replace('\\','/'),*args])==permit.step.argv() and case==permit.step.id and expected_sha==permit.step.executable_sha256,'STEP_MUTATED')
    need(Path(dest)==RUN_ROOT/'cases'/case,'FIXED_ROOT_DESTINATION')
    permit.created=True
