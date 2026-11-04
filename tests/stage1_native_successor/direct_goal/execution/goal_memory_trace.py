"""Read-only process/system/visible-Job observations and pure trace policy."""
import ctypes as C,ctypes.wintypes as W,os,time
PRIVATE=1355161600;WS=268435456;START_COMMIT=8589934592;START_PHYS=4294967296
STOP_COMMIT=4294967296;STOP_COMMIT_DELTA=2147483648;STOP_PHYS=2147483648
class PERF(C.Structure):_fields_=[('cb',W.DWORD)]+[(n,C.c_size_t) for n in ['CommitTotal','CommitLimit','CommitPeak','PhysicalTotal','PhysicalAvailable','SystemCache','KernelTotal','KernelPaged','KernelNonpaged','PageSize']]+[(n,W.DWORD) for n in ['HandleCount','ProcessCount','ThreadCount']]
class BASIC(C.Structure):_fields_=[('proc_time',C.c_longlong),('job_time',C.c_longlong),('flags',W.DWORD),('minws',C.c_size_t),('maxws',C.c_size_t),('active',W.DWORD),('affinity',C.c_size_t),('priority',W.DWORD),('schedule',W.DWORD)]
class IO(C.Structure):_fields_=[(n,C.c_ulonglong) for n in ['r','w','o','rb','wb','ob']]
class JOB(C.Structure):_fields_=[('basic',BASIC),('io',IO),('process_limit',C.c_size_t),('job_limit',C.c_size_t),('peak_process',C.c_size_t),('peak_job',C.c_size_t)]
class ObservationError(Exception):
 def __init__(self,api,error,times,injected=False):
  self.details={'api':api,'win32_error':error,'times':list(times),'injected_NONHDC_only':injected};super().__init__('observation failed: '+api)
def resource_reason(s,baseline=None,start=False):
 p=s['process'];g=s['system']
 if p['private_bytes']>PRIVATE:return 'PRIVATE_LIMIT'
 if p['working_set_bytes']>WS:return 'WORKING_SET_LIMIT'
 if g['commit_available_bytes']<(START_COMMIT if start else STOP_COMMIT):return 'SYSTEM_COMMIT_MARGIN'
 if g['physical_available_bytes']<(START_PHYS if start else STOP_PHYS):return 'PHYSICAL_MARGIN'
 if baseline is not None and g['commit_total_bytes']-baseline['system']['commit_total_bytes']>STOP_COMMIT_DELTA:return 'SYSTEM_COMMIT_DELTA'
 return None
def fingerprint(j):
 v=j.get('caller_visible_layer') or {}
 return {'parent_any_job':j['parent_any_job'],'child_any_job':j['child_any_job'],'parent_visible_flags':v.get('flags'),'parent_process_limit':v.get('process_memory_limit'),'parent_job_limit':v.get('job_memory_limit')}
def job_decision(j,ack,fixture=False):
 visible=j.get('caller_visible_layer') or {}
 if any(v is not None and v<PRIVATE for v in [visible.get('process_memory_limit'),visible.get('job_memory_limit')]):return {'allow':False,'reason':'VISIBLE_JOB_MEMORY_LIMIT_CONFLICT_NOT_ASSUMED_CHILD_OWNERSHIP'}
 expected='MARKED_NONBUSINESS_FIXTURE_JOB_ACK' if fixture else 'NEW_USER_TRACE_JOB_RESIDUAL_ACCEPTANCE'
 if not isinstance(ack,dict) or ack.get('kind')!=expected:return {'allow':False,'reason':'JOB_RISK_SCOPE_MISSING'}
 if ack.get('fingerprint')!=fingerprint(j):return {'allow':False,'reason':'JOB_VISIBLE_FINGERPRINT_CHANGED'}
 if not all(ack.get(k) is True for k in ['opaque_owner_and_outer_layers_accepted','external_normal_abnormal_lifecycle_risk_accepted','no_job_control_authority']):return {'allow':False,'reason':'JOB_RESIDUAL_DECISION_INCOMPLETE'}
 return {'allow':True,'reason':'FIXTURE_ONLY_ACK' if fixture else 'NEW_SCOPED_USER_DECISION_BOUND','same_job_object_proven':False,'job_management_authority':False}
class Trace:
 def __init__(self,MEM,K,P,bind,fault=None):
  self.MEM=MEM;self.K=K;self.P=P;self.fault=fault
  bind(P,'GetPerformanceInfo',W.BOOL,[C.POINTER(PERF),W.DWORD]);bind(K,'IsProcessInJob',W.BOOL,[W.HANDLE,W.HANDLE,C.POINTER(W.BOOL)])
  bind(K,'QueryInformationJobObject',W.BOOL,[W.HANDLE,C.c_int,W.LPVOID,W.DWORD,C.POINTER(W.DWORD)])
 def call(self,name,fn,args,times,injected=False):
  b=time.perf_counter_ns()
  if injected:raise ObservationError(name,None,times+[{'api':name,'before_ns':b,'after_ns':time.perf_counter_ns(),'ok':False,'injected_NONHDC_only':True}],True)
  ok=bool(fn(*args));err=C.get_last_error() if not ok else None;a=time.perf_counter_ns();times.append({'api':name,'before_ns':b,'after_ns':a,'ok':ok,'error_if_failed':err})
  if not ok:raise ObservationError(name,err,times)
 def system(self,times):
  g=PERF();g.cb=C.sizeof(g);self.call('GetPerformanceInfo',self.P.GetPerformanceInfo,[C.byref(g),C.sizeof(g)],times)
  if g.PageSize<=0:raise ObservationError('PageSize',None,times)
  return {'page_size_bytes':g.PageSize,'commit_total_pages':g.CommitTotal,'commit_limit_pages':g.CommitLimit,'commit_total_bytes':int(g.CommitTotal)*g.PageSize,'commit_limit_bytes':int(g.CommitLimit)*g.PageSize,'commit_available_bytes':int(g.CommitLimit-g.CommitTotal)*g.PageSize,'physical_available_bytes':int(g.PhysicalAvailable)*g.PageSize}
 def jobs(self,h,times):
  parent=W.BOOL();child=W.BOOL();self.call('IsProcessInJob(parent,ANY)',self.K.IsProcessInJob,[self.K.GetCurrentProcess(),None,C.byref(parent)],times)
  self.call('IsProcessInJob(root,ANY)',self.K.IsProcessInJob,[h,None,C.byref(child)],times)
  layer=None
  if parent.value:
   j=JOB();n=W.DWORD();self.call('QueryInformationJobObject(NULL,CALLER_VISIBLE_LAYER)',self.K.QueryInformationJobObject,[None,9,C.byref(j),C.sizeof(j),C.byref(n)],times)
   layer={'flags':j.basic.flags,'kill_on_job_close':bool(j.basic.flags&0x2000),'process_memory_limit':j.process_limit if j.basic.flags&0x100 else None,'job_memory_limit':j.job_limit if j.basic.flags&0x200 else None,'scope':'CALLER_ASSOCIATED_VISIBLE_LAYER_ONLY','returned_bytes':n.value}
  return {'parent_pid':os.getpid(),'parent_any_job':bool(parent.value),'child_any_job':bool(child.value),'caller_visible_layer':layer,'same_job_object_proven':False,'child_job_limits':'UNKNOWN_NO_JOB_HANDLE','outer_layers':'UNKNOWN_NO_ENUMERATION','external_handle_owner_lifecycle':'UNKNOWN_EXPLICIT_SCOPED_RISK_REQUIRED','opened_or_modified_or_closed_Job':False}
 def observe(self,h,phase):
  times=[];m=self.MEM();m.cb=C.sizeof(m);self.call('GetProcessMemoryInfo(root)',self.P.GetProcessMemoryInfo,[h,C.byref(m),C.sizeof(m)],times,self.fault=='BASELINE_API' and phase=='PRE_RESUME')
  system=self.system(times);jobs=self.jobs(h,times)
  return {'phase':phase,'hProcess':int(h),'process':{'cb':m.cb,'private_bytes':m.private,'working_set_bytes':m.working},'system':system,'jobs':jobs,'api_times':times,'sample_complete_ns':time.perf_counter_ns()}
