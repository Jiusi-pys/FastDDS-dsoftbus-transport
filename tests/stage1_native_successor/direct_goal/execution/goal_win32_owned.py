"""Narrow reviewed Win32 primitives; no HDC entry or old runner import."""
import ctypes as C, ctypes.wintypes as W, datetime, hashlib, ipaddress, json, os, pathlib, socket, struct, sys, time
from goal_policy import HDC, HDC_SHA, SID, PROFILE, ROOT, LIMITS, Halt
from goal_memory_trace import Trace, fingerprint, ObservationError
from goal_handle_lifecycle import HandleBinding,ImageUnavailable,LifecycleError,observe_binding,bounded_drain
K=C.WinDLL('kernel32',use_last_error=True);A=C.WinDLL('advapi32',use_last_error=True)
P=C.WinDLL('psapi',use_last_error=True);I=C.WinDLL('iphlpapi',use_last_error=True);U=C.WinDLL('userenv',use_last_error=True)
_journal=None

def mark_record_failure():
 if _journal is not None:
  try:_journal.failed=True
  except BaseException:pass

def safe_clock():
 try:return time.perf_counter_ns()
 except BaseException:mark_record_failure();return None

def safe_last_error():
 try:return C.get_last_error()
 except BaseException:mark_record_failure();return None

def api(d,n,r,args):
 f=getattr(d,n);f=getattr(f,'_raw_winapi',f);f.restype=r;f.argtypes=args
 def call(*values):
  before=safe_clock()
  try:
   result=f(*values);error=safe_last_error()
  except BaseException as e:
   try:
    if _journal:_journal.event(n,before,safe_clock(),None,safe_last_error(),type(e).__name__)
   except BaseException:mark_record_failure()
   raise
  try:
   if _journal:_journal.event(n,before,safe_clock(),result if isinstance(result,(int,bool)) else None,error,None)
  except BaseException:mark_record_failure()
  try:
   if error is not None:C.set_last_error(error)
  except BaseException:mark_record_failure()
  return result
 call._raw_winapi=f;setattr(d,n,call);return call
class SA(C.Structure):_fields_=[('nLength',W.DWORD),('lpSecurityDescriptor',W.LPVOID),('bInheritHandle',W.BOOL)]
class SI(C.Structure):_fields_=[('cb',W.DWORD),('lpReserved',W.LPWSTR),('lpDesktop',W.LPWSTR),('lpTitle',W.LPWSTR),('dwX',W.DWORD),('dwY',W.DWORD),('dwXSize',W.DWORD),('dwYSize',W.DWORD),('dwXCountChars',W.DWORD),('dwYCountChars',W.DWORD),('dwFillAttribute',W.DWORD),('dwFlags',W.DWORD),('wShowWindow',W.WORD),('cbReserved2',W.WORD),('lpReserved2',W.LPVOID),('hStdInput',W.HANDLE),('hStdOutput',W.HANDLE),('hStdError',W.HANDLE)]
class SIX(C.Structure):_fields_=[('StartupInfo',SI),('lpAttributeList',W.LPVOID)]
class PI(C.Structure):_fields_=[('hProcess',W.HANDLE),('hThread',W.HANDLE),('dwProcessId',W.DWORD),('dwThreadId',W.DWORD)]
class FT(C.Structure):_fields_=[('lo',W.DWORD),('hi',W.DWORD)]
class FI(C.Structure):_fields_=[('attributes',W.DWORD),('created',FT),('access',FT),('written',FT),('volume',W.DWORD),('sizehi',W.DWORD),('sizelo',W.DWORD),('links',W.DWORD),('indexhi',W.DWORD),('indexlo',W.DWORD)]
class MEM(C.Structure):_fields_=[('cb',W.DWORD),('faults',W.DWORD)]+[(n,C.c_size_t) for n in ['peak','working','qpp','qp','qnp','qn','page','peakpage','private']]
class PE(C.Structure):_fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),('module',W.DWORD),('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),('flags',W.DWORD),('name',W.WCHAR*260)]
class TCP4(C.Structure):_fields_=[(n,W.DWORD) for n in ['state','addr','port','remote','remoteport','pid']]
class TCP6(C.Structure):_fields_=[('addr',C.c_ubyte*16),('scope',W.DWORD),('port',W.DWORD),('remote',C.c_ubyte*16),('rscope',W.DWORD),('rport',W.DWORD),('state',W.DWORD),('pid',W.DWORD)]
api(K,'GetCurrentProcess',W.HANDLE,[]);api(K,'CloseHandle',W.BOOL,[W.HANDLE]);api(K,'LocalFree',W.LPVOID,[W.LPVOID])
api(K,'GetFileInformationByHandle',W.BOOL,[W.HANDLE,C.POINTER(FI)])
api(K,'CreateFileW',W.HANDLE,[W.LPCWSTR,W.DWORD,W.DWORD,C.POINTER(SA),W.DWORD,W.DWORD,W.HANDLE])
api(K,'ReadFile',W.BOOL,[W.HANDLE,W.LPVOID,W.DWORD,C.POINTER(W.DWORD),W.LPVOID])
api(K,'CreatePipe',W.BOOL,[C.POINTER(W.HANDLE),C.POINTER(W.HANDLE),C.POINTER(SA),W.DWORD])
api(K,'SetHandleInformation',W.BOOL,[W.HANDLE,W.DWORD,W.DWORD]);api(K,'PeekNamedPipe',W.BOOL,[W.HANDLE,W.LPVOID,W.DWORD,C.POINTER(W.DWORD),C.POINTER(W.DWORD),C.POINTER(W.DWORD)])
api(K,'InitializeProcThreadAttributeList',W.BOOL,[W.LPVOID,W.DWORD,W.DWORD,C.POINTER(C.c_size_t)])
api(K,'UpdateProcThreadAttribute',W.BOOL,[W.LPVOID,W.DWORD,C.c_size_t,W.LPVOID,C.c_size_t,W.LPVOID,W.LPVOID]);api(K,'DeleteProcThreadAttributeList',None,[W.LPVOID])
api(K,'CreateProcessW',W.BOOL,[W.LPCWSTR,W.LPWSTR,W.LPVOID,W.LPVOID,W.BOOL,W.DWORD,W.LPVOID,W.LPCWSTR,C.POINTER(SIX),C.POINTER(PI)])
api(K,'ResumeThread',W.DWORD,[W.HANDLE]);api(K,'GetProcessId',W.DWORD,[W.HANDLE]);api(K,'GetProcessTimes',W.BOOL,[W.HANDLE,C.POINTER(FT),C.POINTER(FT),C.POINTER(FT),C.POINTER(FT)])
api(K,'QueryFullProcessImageNameW',W.BOOL,[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)])
api(K,'WaitForSingleObject',W.DWORD,[W.HANDLE,W.DWORD]);api(K,'GetExitCodeProcess',W.BOOL,[W.HANDLE,C.POINTER(W.DWORD)]);api(K,'TerminateProcess',W.BOOL,[W.HANDLE,W.UINT])
api(K,'CreateToolhelp32Snapshot',W.HANDLE,[W.DWORD,W.DWORD]);api(K,'Process32FirstW',W.BOOL,[W.HANDLE,C.POINTER(PE)]);api(K,'Process32NextW',W.BOOL,[W.HANDLE,C.POINTER(PE)])
api(P,'GetProcessMemoryInfo',W.BOOL,[W.HANDLE,C.POINTER(MEM),W.DWORD]);api(I,'GetExtendedTcpTable',W.DWORD,[W.LPVOID,C.POINTER(W.DWORD),W.BOOL,W.ULONG,C.c_int,W.ULONG])
api(A,'OpenProcessToken',W.BOOL,[W.HANDLE,W.DWORD,C.POINTER(W.HANDLE)]);api(A,'GetTokenInformation',W.BOOL,[W.HANDLE,C.c_int,W.LPVOID,W.DWORD,C.POINTER(W.DWORD)])
api(A,'ConvertSidToStringSidW',W.BOOL,[W.LPVOID,C.POINTER(W.LPWSTR)]);api(U,'GetUserProfileDirectoryW',W.BOOL,[W.HANDLE,W.LPWSTR,C.POINTER(W.DWORD)])
api(A,'ConvertStringSecurityDescriptorToSecurityDescriptorW',W.BOOL,[W.LPCWSTR,W.DWORD,C.POINTER(W.LPVOID),C.POINTER(W.DWORD)])
api(K,'CreateDirectoryW',W.BOOL,[W.LPCWSTR,C.POINTER(SA)])
INVALID=C.c_void_p(-1).value
class Fault(Exception):pass
def need(ok,name):
 if not ok:raise Fault(name+';last_error_raw_not_always_cause='+str(C.get_last_error()))
def utc():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def digest(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(65536),b''):h.update(b)
 return h.hexdigest()
def put(p,value):
 with open(p,'x',encoding='utf-8') as f:json.dump(value,f,ensure_ascii=False,separators=(',',':'));f.flush();os.fsync(f.fileno())
def ident(h):
 ts=[FT() for _ in range(4)];need(K.GetProcessTimes(h,*[C.byref(x) for x in ts]),'GetProcessTimes')
 b=C.create_unicode_buffer(32768);n=W.DWORD(32768);need(K.QueryFullProcessImageNameW(h,0,b,C.byref(n)),'QueryFullProcessImageName')
 return {'pid':K.GetProcessId(h),'birth':(ts[0].hi<<32)|ts[0].lo,'image':str(pathlib.Path(b.value).resolve())}
def file_id(h):
 i=FI();need(K.GetFileInformationByHandle(h,C.byref(i)),'file identity');return [i.volume,i.indexhi,i.indexlo]

class IdentityOps:
 def wait(self,h):
  value=K.WaitForSingleObject(h,0)
  if value not in (0,258):raise LifecycleError('WAIT_UNKNOWN')
  return value
 def pid_birth(self,h):
  times=[FT() for _ in range(4)]
  if not K.GetProcessTimes(h,*[C.byref(x) for x in times]):raise LifecycleError('PROCESS_TIMES_FAILED')
  pid=K.GetProcessId(h)
  if not pid:raise LifecycleError('PROCESS_ID_FAILED')
  return int(pid),(times[0].hi<<32)|times[0].lo
 def image(self,h):
  buf=C.create_unicode_buffer(32768);size=W.DWORD(32768)
  if not K.QueryFullProcessImageNameW(h,0,buf,C.byref(size)):raise ImageUnavailable(C.get_last_error())
  return str(pathlib.Path(buf.value).resolve())
 def file_id(self,h):return tuple(file_id(h))
 def exit_code(self,h):
  code=W.DWORD()
  if not K.GetExitCodeProcess(h,C.byref(code)):raise LifecycleError('EXIT_QUERY_FAILED')
  return int(code.value)
def context():
 t=W.HANDLE();need(A.OpenProcessToken(K.GetCurrentProcess(),8,C.byref(t)),'query token')
 try:
  n=W.DWORD();A.GetTokenInformation(t,1,None,0,C.byref(n));b=C.create_string_buffer(n.value);need(A.GetTokenInformation(t,1,b,n.value,C.byref(n)),'TokenUser')
  s=W.LPWSTR();need(A.ConvertSidToStringSidW(C.cast(b,C.POINTER(W.LPVOID))[0],C.byref(s)),'SID');sid=s.value;K.LocalFree(C.cast(s,W.LPVOID))
  flags={}
  for key,kind in [('appcontainer',29),('has_restrictions',21)]:
   v=W.DWORD();need(A.GetTokenInformation(t,kind,C.byref(v),4,C.byref(n)),key);flags[key]=v.value
  count=W.DWORD(32768);p=C.create_unicode_buffer(count.value);need(U.GetUserProfileDirectoryW(t,p,C.byref(count)),'profile')
  return dict(sid=sid,profile=p.value,environment={k:os.environ.get(k) for k in ['USERPROFILE','HOME','HOMEDRIVE','HOMEPATH']},**flags)
 finally:K.CloseHandle(t)
def private_directory(p):
 # Only CREATE a new task directory with restrictive DACL; never modify existing ACLs.
 sd=W.LPVOID();need(A.ConvertStringSecurityDescriptorToSecurityDescriptorW('D:P(A;OICI;FA;;;'+SID+')',1,C.byref(sd),None),'new-directory SDDL')
 try:sa=SA(C.sizeof(SA),sd,False);need(K.CreateDirectoryW(str(p),C.byref(sa)),'CREATE_NEW_PRIVATE_DIRECTORY')
 finally:K.LocalFree(sd)
def key_metadata():
 rows=[]
 for p in [pathlib.Path('C:/Users/17715/.harmony'),pathlib.Path('C:/Users/17715/.harmony/hdckey'),pathlib.Path('C:/Users/17715/.harmony/hdckey.pub')]:
  try:
   s=p.lstat();rows.append({'path':str(p),'status':'PRESENT','bytes':s.st_size,'attributes':s.st_file_attributes,'mtime_ns':s.st_mtime_ns,'is_directory':p.is_dir(),'contents_or_hash':False})
  except FileNotFoundError:rows.append({'path':str(p),'status':'NOT_FOUND','contents_or_hash':False})
  except OSError as e:rows.append({'path':str(p),'status':'QUERY_ERROR','winerror':e.winerror,'contents_or_hash':False})
 return rows


def snapshot(parent_ids=()):
 """One bounded passive batch; never obtains control rights over selected PIDs."""
 before=safe_clock();h=K.CreateToolhelp32Snapshot(2,0)
 need(h and h!=INVALID,'snapshot create');scanned=0;selected=[];complete=False;error=None
 try:
  e=PE();e.dwSize=C.sizeof(e)
  first=K.Process32FirstW._raw_winapi;nextrow=K.Process32NextW._raw_winapi
  ok=first(h,C.byref(e))
  while ok:
   scanned+=1;need(scanned<=16384,'snapshot truncated')
   if e.parent in parent_ids or e.name.lower().startswith('hdc'):
    selected.append({'pid':int(e.pid),'parent':int(e.parent),'name':e.name,'ownership':'PASSIVE_ONLY'})
    need(len(selected)<=64,'snapshot selected cap')
   ok=nextrow(h,C.byref(e))
  error=C.get_last_error();complete=error==18;need(complete,'snapshot incomplete')
  return selected
 finally:
  try:
   if _journal:_journal.batch('PROCESS_SNAPSHOT',before,safe_clock(),scanned,selected,complete,error)
  except BaseException:mark_record_failure()
  finally:K.CloseHandle(h)


def tcp_table():
 before=safe_clock();out=[];scanned=0;complete=False;error=None
 try:
  for family,row in [(2,TCP4),(23,TCP6)]:
   n=W.DWORD();r=I.GetExtendedTcpTable(None,C.byref(n),False,family,3,0)
   need(r in [0,122] and 4<=n.value<=1048576,'tcp size')
   b=C.create_string_buffer(n.value);r=I.GetExtendedTcpTable(b,C.byref(n),False,family,3,0);need(r==0,'tcp read')
   count=C.cast(b,C.POINTER(W.DWORD))[0];need(4+count*C.sizeof(row)<=n.value,'tcp bounds')
   scanned+=count;need(scanned<=16384,'tcp row cap')
   for j in range(count):
    x=row.from_buffer_copy(b,4+j*C.sizeof(row))
    raw=struct.pack('=I',x.addr) if family==2 else bytes(x.addr)
    out.append({'pid':int(x.pid),'address':str(ipaddress.ip_address(raw)),'port':socket.ntohs(x.port&65535)})
  complete=True;return out
 except BaseException as e:
  error=type(e).__name__;raise
 finally:
  try:
   if _journal:_journal.batch('TCP_SNAPSHOT',before,safe_clock(),scanned,
                            [x for x in out if x['port']==8710],complete,error)
  except BaseException:mark_record_failure()


class OwnedRoot:
 def __init__(self,case,exe,args,expected_sha,dest,permit=None):
  from goal_authorization import require_process_permission
  require_process_permission(exe,args,permit)
  self.case=case;self.exe=pathlib.Path(exe);self.args=list(args);self.expected_sha=expected_sha;self.dest=dest
  self.goal_permit=permit
  self.pi=PI();self.handles=[];self.reads=[];self.captures=[];self.attr=None;self.attr_ready=False
  self.owner=None;self.authority=False;self.closed=False;self.output=[bytearray(),bytearray()];self.binding=None;self.image_pin=None;self.terminal_at=None
  self.receipt={'case':case,'argv':[str(exe),*args],'created':False,'bound':False,'resumed':False,
                'signal_authority':False,'stop_attempts':0,'terminal':False,'EOF':False,'native_exit':None,'cleanup_errors':[]}

 def create(self):
  from goal_authorization import consume_creation
  consume_creation(self.exe,self.args,self.case,self.dest,self.expected_sha,self.goal_permit)
  import subprocess
  # All partially acquired handles are recorded on this already-registered root.
  private_directory(self.dest);(self.dest/'tmp').mkdir()
  pin=K.CreateFileW(str(self.exe),0x80000000,1,None,3,0x00200000,None)
  need(pin and pin!=INVALID,'exe pin');self.handles.append(pin)
  self.image_pin=pin
  need(digest(self.exe)==self.expected_sha,'locked exe hash');self.receipt['FileID']=file_id(pin)
  sa=SA(C.sizeof(SA),None,True);writes=[]
  for name in ('stdout','stderr'):
   rd=W.HANDLE();wr=W.HANDLE();need(K.CreatePipe(C.byref(rd),C.byref(wr),C.byref(sa),0),'pipe create')
   self.handles.extend([rd.value,wr.value]);need(K.SetHandleInformation(rd,1,0),'inherit read off')
   self.reads.append({'h':rd.value,'eof':False,'name':name});writes.append(wr.value)
   self.captures.append(open(self.dest/(name+'.raw'),'xb'))
  nul=K.CreateFileW('NUL',0x80000000,3,C.byref(sa),3,0,None);need(nul and nul!=INVALID,'stdin NUL');self.handles.append(nul)
  count=C.c_size_t();K.InitializeProcThreadAttributeList(None,1,0,C.byref(count))
  self.attr=C.create_string_buffer(count.value);need(K.InitializeProcThreadAttributeList(self.attr,1,0,C.byref(count)),'attribute list');self.attr_ready=True
  inherit=(W.HANDLE*3)(nul,*writes);need(K.UpdateProcThreadAttribute(self.attr,0,0x20002,inherit,C.sizeof(inherit),None,None),'inherit allowlist')
  si=SIX();si.StartupInfo.cb=C.sizeof(si);si.StartupInfo.dwFlags=0x100
  si.StartupInfo.hStdInput=nul;si.StartupInfo.hStdOutput=writes[0];si.StartupInfo.hStdError=writes[1];si.lpAttributeList=C.cast(self.attr,W.LPVOID)
  env=os.environ.copy();env.update(TEMP=str(self.dest/'tmp'),TMP=str(self.dest/'tmp'),PYTHONDONTWRITEBYTECODE='1')
  block=C.create_unicode_buffer('\0'.join(k+'='+v for k,v in sorted(env.items(),key=lambda x:x[0].upper()))+'\0\0')
  cmd=C.create_unicode_buffer(subprocess.list2cmdline([str(self.exe),*self.args]))
  put(self.dest/'INTENT.json',{'utc':utc(),'argv':[str(self.exe),*self.args],'exe_sha256':self.expected_sha,'CWD':str(self.dest),'TEMP_TMP':str(self.dest/'tmp')})
  self.receipt['create_call_before_ns']=safe_clock()
  need(_journal is None or not _journal.failed,'PRE_CREATE_RECORD_FAILURE')
  ok=bool(K.CreateProcessW(str(self.exe),cmd,None,None,True,8|4|0x400|0x80000,block,str(self.dest),C.byref(si),C.byref(self.pi)))
  self.receipt['created']=ok
  if ok:self.handles.extend([self.pi.hProcess,self.pi.hThread])
  self.receipt['create_call_after_ns']=safe_clock()
  for h in writes:need(K.CloseHandle(h),'parent write close');self.handles.remove(h)
  need(ok,'CreateProcess false')
  self.receipt['hProcess']=int(self.pi.hProcess);self.receipt['hThread']=int(self.pi.hThread)
  self.owner=ident(self.pi.hProcess)
  need(self.owner['pid']==self.pi.dwProcessId and pathlib.Path(self.owner['image'])==self.exe,'created identity')
  h=K.CreateFileW(self.owner['image'],0x80000000,1,None,3,0x00200000,None);need(h and h!=INVALID,'image association')
  try:need(file_id(h)==self.receipt['FileID'],'image FileID changed')
  finally:need(K.CloseHandle(h),'image association close')
  self.authority=True;self.receipt.update(bound=True,signal_authority=True,owner=self.owner,tool_sha256=self.expected_sha)
  self.binding=HandleBinding(int(self.pi.hProcess),int(self.image_pin),self.owner['pid'],self.owner['birth'],self.owner['image'],tuple(self.receipt['FileID']))
  put(self.dest/'IDENTITY.json',self.receipt)

 def identity(self,require_live=False):
  try:
   need(self.authority and not self.closed and self.binding is not None,'root ownership not retained')
   need(int(self.pi.hProcess)==self.binding.process and self.image_pin==self.binding.image_pin and self.binding.process in self.handles and self.image_pin in self.handles,'original handles not retained')
   need(self.owner=={'pid':self.binding.pid,'birth':self.binding.birth,'image':self.binding.image},'creation anchor changed')
   seen=observe_binding(self.binding,IdentityOps());phase=seen['phase']
   need(not self.receipt.get('terminal') or phase=='TERMINAL','terminal state regression')
   self.receipt['identity_phase']=phase
   if phase=='TERMINAL':
    if getattr(self,'terminal_at',None) is None:self.terminal_at=time.monotonic()
    self.receipt.update(terminal=True,native_exit=seen['exit_code'],signal_authority=False,terminal_binding_verified=True,terminal_proof=seen['proof'])
    if seen.get('transition_error'):self.receipt['identity_transition_error']=seen['transition_error']
   else:self.receipt['signal_authority']=True
   need(not require_live or phase=='LIVE','expected live process has terminated')
   return phase
  except BaseException:
   self.authority=False;self.receipt['signal_authority']=False;self.receipt['terminal_binding_verified']=False;raise

 def state(self):
  if self.closed:
   self.authority=False;self.receipt['signal_authority']=False;raise LifecycleError('CLOSED_PROCESS_HANDLE')
  if not self.receipt['created']:return 'NOT_CREATED'
  try:
   v=K.WaitForSingleObject(self.pi.hProcess,0);need(v in (0,258),'root wait unknown')
   if v==0:
    code=W.DWORD();need(K.GetExitCodeProcess(self.pi.hProcess,C.byref(code)),'root exit')
    if self.terminal_at is None:self.terminal_at=time.monotonic()
    self.receipt.update(terminal=True,native_exit=int(code.value),signal_authority=False);return 'TERMINAL'
   return 'LIVE'
  except BaseException:
   self.authority=False;self.receipt['signal_authority']=False;self.receipt['terminal_binding_verified']=False;raise

 def resume(self):
  self.identity(require_live=True);self.receipt['resume_before_ns']=safe_clock()
  need(_journal is None or not _journal.failed,'PRE_RESUME_RECORD_FAILURE')
  need(K.ResumeThread(self.pi.hThread)==1,'initial suspend count')
  self.receipt['resumed']=True;self.receipt['resume_after_ns']=safe_clock()

 def read(self,remaining,observer):
  need(callable(observer),'LIVE_OBSERVER_REQUIRED')
  chunks=[]
  for i,row in enumerate(self.reads):
   if row['eof']:continue
   amount=W.DWORD();ok=K.PeekNamedPipe(row['h'],None,0,None,C.byref(amount),None)
   if not ok:
    if C.get_last_error()==109:row['eof']=True;continue
    raise Halt('PIPE_PEEK_ERROR')
   if amount.value:
    need(remaining>0,'PIPE_OUTPUT_CAP');n=min(amount.value,4096,remaining)
    b=C.create_string_buffer(n);got=W.DWORD();before=safe_clock()
    ok=K.ReadFile(row['h'],b,n,C.byref(got),None);after=safe_clock()
    if not ok:
     if C.get_last_error()==109:row['eof']=True;continue
     raise Halt('PIPE_READ_ERROR')
    data=b.raw[:got.value];remaining-=len(data)
    if not data:continue # TRUE/zero is not EOF and not a notice chunk.
    # Synchronous borrowed callback: consume/evidence before any capture write
    # or next pipe. Never retained; no raw text crosses into structured records.
    observer(row['name'],data,before,after)
    self.captures[i].write(data);self.captures[i].flush()
    if self.case!='server':self.output[i].extend(data)
    chunks.append((row['name'],data))
  self.receipt['EOF']=all(x['eof'] for x in self.reads)
  return chunks

 def drain(self,deadline,remaining,observer):
  if self.terminal_at is not None:deadline=min(deadline,self.terminal_at+2)
  self.receipt['drain']={'status':'IN_PROGRESS','EOF':False,'deadline':deadline}
  try:
   result=bounded_drain(lambda cap:self.read(cap,observer),lambda:self.receipt['EOF'],deadline=deadline,remaining=remaining)
   self.receipt['drain']=result;return result
  except BaseException:
   self.receipt['drain']['status']='ERROR';raise

 def stop(self,deadline):
  if self.state()!='LIVE':return
  if self.identity()!='LIVE':return
  if self.state()!='LIVE':return # Natural exit between checks never grants a signal.
  need(self.receipt['stop_attempts']==0,'duplicate stop forbidden')
  self.receipt['stop_attempts']=1;self.receipt['stop_utc']=utc()
  ok=bool(K.TerminateProcess(self.pi.hProcess,125));self.receipt['TerminateProcess_ok']=ok
  need(ok,'TerminateProcess failed')
  ms=max(0,min(2000,int((deadline-time.monotonic())*1000)))
  v=K.WaitForSingleObject(self.pi.hProcess,ms);self.receipt['stop_wait_ms']=ms
  need(v==0,'STOP_TERMINAL_UNKNOWN');self.state()

 def close(self):
  if self.closed:return
  self.closed=True;self.authority=False;self.receipt['signal_authority']=False
  for f in self.captures:
   try:f.close()
   except BaseException as e:self.receipt['cleanup_errors'].append('capture_close:'+type(e).__name__)
  if self.attr_ready:K.DeleteProcThreadAttributeList(self.attr)
  for h in reversed(self.handles):
   if not K.CloseHandle(h):self.receipt['cleanup_errors'].append('CloseHandle:'+str(C.get_last_error()))
  self.handles.clear()
