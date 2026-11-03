"""Test-only fixed nonbusiness CPython child. No Goal backend or permit.

The small API harness creates only this exact file through the pinned Python
executable. Production identity/read/drain methods are shared, not reimplemented.
"""
import sys,os,time
if __name__=='__main__':
    if len(sys.argv)!=3 or sys.argv[1]!='--nonbusiness-child' or sys.argv[2] not in ('normal','sleep'):raise SystemExit(78)
    os.write(1,b'KNOWN_CHILD_STDOUT\n');os.write(2,b'KNOWN_CHILD_STDERR\n')
    if sys.argv[2]=='sleep':time.sleep(2)
    raise SystemExit(0)

import ctypes as C,ctypes.wintypes as W,io,subprocess,hashlib
from pathlib import Path
import goal_win32_owned as win
from goal_handle_lifecycle import HandleBinding
win.api(win.K,'WriteFile',W.BOOL,[W.HANDLE,W.LPCVOID,W.DWORD,C.POINTER(W.DWORD),W.LPVOID])

PYTHON=Path('C:/Users/17715/anaconda3/python.exe')
PYTHON_SHA='7679e53fa969789309e81fdad0d52b8cda5f83c9abf7cb31a3c58bf24b31e264'
OBSERVATIONS=[]
HOLD={'active':False,'reason':None}

class LocalLifecycleProcess:
    """Own CreateProcess hProcess, no Root constructor/GoalGrant/Job operation."""
    identity=win.OwnedRoot.identity
    state=win.OwnedRoot.state
    read=win.OwnedRoot.read
    drain=win.OwnedRoot.drain
    stop=win.OwnedRoot.stop
    close=win.OwnedRoot.close
    def __init__(self,directory,*,mode='normal',hold_writer=False):
        if mode not in ('normal','sleep'):raise ValueError('FIXED_MODE')
        self.dest=Path(directory);self.mode=mode;self.hold_writer=hold_writer;self.writer=None
        self.pi=win.PI();self.handles=[];self.reads=[];self.captures=[io.BytesIO(),io.BytesIO()];self.attr=None;self.attr_ready=False
        self.owner=None;self.authority=False;self.closed=False;self.output=[bytearray(),bytearray()];self.binding=None;self.image_pin=None;self.terminal_at=None;self.case='MARKED_NONBUSINESS_LOCAL'
        self.receipt={'case':self.case,'created':False,'bound':False,'resumed':False,'signal_authority':False,'stop_attempts':0,'terminal':False,'EOF':False,'native_exit':None,'cleanup_errors':[],'effect':'NO_HDC_NATIVE_TOKEN_SDK','Job_operations':0}
    def start(self):
        if HOLD['active']:raise RuntimeError('FIXTURE_PRIOR_FAILURE_HOLD')
        try:return self._start_owned()
        except BaseException:
            HOLD.update(active=True,reason='FIXTURE_CREATE_OR_BIND_FAILED')
            self.receipt['fixture_failure']='CREATE_OR_BIND_FAILED';raise
    def _start_owned(self):
        self.dest.mkdir();(self.dest/'tmp').mkdir()
        pin=win.K.CreateFileW(str(PYTHON),0x80000000,1,None,3,0x00200000,None)
        win.need(pin and pin!=win.INVALID,'fixture executable pin');self.handles.append(pin);self.image_pin=pin
        win.need(win.digest(PYTHON)==PYTHON_SHA,'fixture Python drift');self.receipt['FileID']=win.file_id(pin)
        sa=win.SA(C.sizeof(win.SA),None,True);writes=[]
        for name in ('stdout','stderr'):
            read=W.HANDLE();write=W.HANDLE();win.need(win.K.CreatePipe(C.byref(read),C.byref(write),C.byref(sa),65536),'fixture pipe')
            self.handles.extend([read.value,write.value]);win.need(win.K.SetHandleInformation(read,1,0),'fixture read noninherit')
            self.reads.append({'h':read.value,'eof':False,'name':name});writes.append(write.value)
        nul=win.K.CreateFileW('NUL',0x80000000,3,C.byref(sa),3,0,None);win.need(nul and nul!=win.INVALID,'fixture stdin');self.handles.append(nul)
        size=C.c_size_t();win.K.InitializeProcThreadAttributeList(None,1,0,C.byref(size));self.attr=C.create_string_buffer(size.value)
        win.need(win.K.InitializeProcThreadAttributeList(self.attr,1,0,C.byref(size)),'fixture attributes');self.attr_ready=True
        inherited=(W.HANDLE*3)(nul,*writes);win.need(win.K.UpdateProcThreadAttribute(self.attr,0,0x20002,inherited,C.sizeof(inherited),None,None),'fixture exact inherited handles')
        info=win.SIX();info.StartupInfo.cb=C.sizeof(info);info.StartupInfo.dwFlags=0x100;info.StartupInfo.hStdInput=nul;info.StartupInfo.hStdOutput=writes[0];info.StartupInfo.hStdError=writes[1];info.lpAttributeList=C.cast(self.attr,W.LPVOID)
        argv=[str(PYTHON),'-E','-B',str(Path(__file__).resolve()),'--nonbusiness-child',self.mode]
        env=os.environ.copy();env.update(TEMP=str(self.dest/'tmp'),TMP=str(self.dest/'tmp'))
        block=C.create_unicode_buffer('\0'.join(k+'='+v for k,v in sorted(env.items(),key=lambda item:item[0].upper()))+'\0\0');cmd=C.create_unicode_buffer(subprocess.list2cmdline(argv))
        ok=win.K.CreateProcessW(str(PYTHON),cmd,None,None,True,8|4|0x400|0x80000,block,str(self.dest),C.byref(info),C.byref(self.pi));self.receipt['created']=bool(ok)
        if ok:
            self.handles.extend([self.pi.hProcess,self.pi.hThread]);self.receipt['created_pid']=int(self.pi.dwProcessId)
        win.need(ok,'fixture CreateProcess')
        self.receipt.update(hProcess=int(self.pi.hProcess),hThread=int(self.pi.hThread),argv=argv)
        for index,handle in enumerate(writes):
            if index==0 and self.hold_writer:self.writer=handle
            else:win.need(win.K.CloseHandle(handle),'fixture parent writer close');self.handles.remove(handle)
        self.owner=win.ident(self.pi.hProcess)
        win.need(self.owner['pid']==self.pi.dwProcessId and Path(self.owner['image'])==PYTHON,'fixture exact created identity')
        image=win.K.CreateFileW(self.owner['image'],0x80000000,1,None,3,0x00200000,None);win.need(image and image!=win.INVALID,'fixture image association')
        try:win.need(win.file_id(image)==self.receipt['FileID'],'fixture image FileID')
        finally:win.need(win.K.CloseHandle(image),'fixture association close')
        self.binding=HandleBinding(int(self.pi.hProcess),int(self.image_pin),self.owner['pid'],self.owner['birth'],self.owner['image'],tuple(self.receipt['FileID']))
        self.authority=True;self.receipt.update(bound=True,owner=self.owner,signal_authority=True,tool_sha256=PYTHON_SHA)
        win.need(self.identity(require_live=True)=='LIVE','fixture suspended LIVE proof')
        win.need(win.K.ResumeThread(self.pi.hThread)==1,'fixture single resume');self.receipt['resumed']=True
        return self
    def release_writer(self,tail=b''):
        if self.writer is None:return
        if tail:
            win.need(len(tail)<=32,'fixture tail bound');count=W.DWORD();buffer=C.create_string_buffer(tail)
            win.need(win.K.WriteFile(self.writer,buffer,len(tail),C.byref(count),None) and count.value==len(tail),'fixture known tail write')
        win.need(win.K.CloseHandle(self.writer),'fixture owned writer close');self.handles.remove(self.writer);self.writer=None
    def wait_terminal(self):
        end=time.monotonic()+3
        while self.state()=='LIVE':
            win.need(time.monotonic()<end,'fixture own child timeout');time.sleep(.002)
        return self.receipt['native_exit']
    def finish(self):
        unknown=False
        try:
            if self.receipt['created'] and not self.closed:
                state=self.state()
                if state=='LIVE':
                    if self.authority and self.receipt['bound']:self.stop(time.monotonic()+2)
                    else:unknown=True
                elif state!='TERMINAL':unknown=True
        except BaseException:
            unknown=True;self.receipt['fixture_failure']='STOP_OR_STATE_UNKNOWN'
        finally:
            try:
                try:self.release_writer()
                except BaseException:unknown=True;self.receipt['cleanup_errors'].append('FIXTURE_WRITER_CLOSE_FAILED')
            finally:
                try:
                    try:self.close()
                    except BaseException:unknown=True;self.receipt['cleanup_errors'].append('FIXTURE_CLOSE_FAILED')
                finally:
                    unknown=unknown or bool(self.receipt['cleanup_errors']) or self.receipt['created'] and self.receipt['terminal'] is not True
                    self.receipt['fixture_terminal_outcome']='UNKNOWN' if unknown else 'TERMINAL' if self.receipt['created'] else 'NO_PROCESS_CREATED'
                    if unknown:HOLD.update(active=True,reason='FIXTURE_UNKNOWN_CLEANUP')
                    OBSERVATIONS.append(dict(self.receipt))
        if unknown:raise RuntimeError('FIXTURE_CLEANUP_UNKNOWN_NO_FURTHER_CREATION')
