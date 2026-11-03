"""Bounded file comparison engine; import and CLI never touch a target.

Library calls require separate caller data authority. No boolean or fixture
creates that authority. Current project scope permits synthetic fixtures only.
"""
from dataclasses import dataclass
import json
import ntpath
import re
import stat
import threading

RANGES=((0,65),(115,77))
CODES=frozenset(('E000002','E000003','E000004','OTHER_OR_UNMATCHED'))
LF_TERMINATED='LF_TERMINATED'
LF_EXCLUDED='LF_EXCLUDED'
FRAMINGS=frozenset((LF_TERMINATED,LF_EXCLUDED))
# Public comparison-source 8efffcf2fa2f5d4f997f2de8eb287432959417c8:
# daemon.cpp618/643, server_for_client.cpp421-442/982/1398, server.cpp630.
TEMPLATES={
 b'[E000002]:The device unauthorized.':'PUBLIC_REQUEST_HEAD',
 b'[Fail][E000002]:The device unauthorized.':'PUBLIC_REQUEST_HEAD',
 b'[E000003]:The device unauthorized.':'PUBLIC_DENIAL_HEAD',
 b'[Fail][E000003]:The device unauthorized.':'PUBLIC_DENIAL_HEAD',
 b'Please check for a confirmation dialog on your device.':'PUBLIC_REQUEST_INSTRUCTION',
 b'The user denied the access for the device.':'PUBLIC_DENIAL_SENTENCE',
 b'[E000004]:The communication channel is being established.':'PUBLIC_CHANNEL_PENDING',
 b'[Fail][E000004]:The communication channel is being established.':'PUBLIC_CHANNEL_PENDING',
 b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info':'PUBLIC_DEVICE_SELECTION_FAILURE',
 b'response auth signture success':'PUBLIC_AUTH_DIAGNOSTIC',
}

class ControlledFailure(Exception):pass

def require(ok,code):
    if not ok:raise ControlledFailure(code)

def canonical_path(value):
    require(type(value) is str and re.match(r'^[A-Za-z]:[\\/]',value)
            and not any(ord(c)<32 for c in value),'PATH_FORMAT')
    return ntpath.normcase(ntpath.normpath(value))

@dataclass(frozen=True)
class Identity:
    dev:int;ino:int;size:int;mtime_ns:int;attrs:int;links:int
    def validate(self):
        require(all(type(x) is int for x in (self.dev,self.ino,self.size,self.mtime_ns,self.attrs,self.links)), 'IDENTITY_TYPES')
        require(self.dev>=0 and self.ino>0 and self.mtime_ns>=0 and
                self.size==193 and self.attrs==32 and self.links==1,'IDENTITY_CONSTRAINTS')

@dataclass(frozen=True)
class ReadSpec:
    path:str
    identity:Identity
    # Format metadata, not data-access authority. LF_EXCLUDED requires the
    # caller to establish complete record boundaries independently of a match.
    framing:str=LF_TERMINATED
    def validate(self):
        canonical_path(self.path);require(type(self.identity) is Identity,'IDENTITY_TYPE')
        self.identity.validate()
        require(type(self.framing) is str and self.framing in FRAMINGS,'RECORD_FRAMING')


def record_body(data,framing):
    """Accept exact LF/CRLF or conditional Windows-text CRCRLF framing.

    LF_EXCLUDED represents a complete record with only its final LF removed
    upstream. It must not be inferred from a matching prefix or window length.
    """
    if framing==LF_TERMINATED:
        if not data.endswith(b'\n'):return None
        data=data[:-1]
    if data.endswith(b'\r\r'):data=data[:-2]
    elif data.endswith(b'\r'):data=data[:-1]
    if any(c in data for c in (b'\r',b'\n',b'\x00')):return None
    return data


def classify_record(data,*,framing=LF_TERMINATED):
    require(type(data) is bytes and len(data)<=77,'RECORD_TYPE_OR_CAP')
    require(type(framing) is str and framing in FRAMINGS,'RECORD_FRAMING')
    # Strict decode only detects malformed encoding. The text is never returned.
    try:data.decode('utf-8','strict')
    except UnicodeError:return {'code':'OTHER_OR_UNMATCHED','template':'UNMATCHED','state':'UNKNOWN'}
    tokens={x[1:-1].decode('ascii').upper() for x in re.findall(rb'\[E00000[234]\]',data,re.IGNORECASE)}
    code=next(iter(tokens)) if len(tokens)==1 else 'OTHER_OR_UNMATCHED'
    require(code in CODES,'CODE_ENUM')
    body=record_body(data,framing)
    template=TEMPLATES.get(body,'UNMATCHED') if len(tokens)<=1 else 'UNMATCHED'
    return {'code':code,'template':template,'state':'PUBLIC_TEMPLATE_MATCH' if template!='UNMATCHED' else 'UNKNOWN'}


def compare_once(spec,ops):
    """Two original-handle limited reads, zero retries; no data/path in output."""
    result={'schema':'BOUNDED_PUBLIC_COMPARE_V1','outcome':'UNKNOWN','error':'PRECHECK_FAILED',
            'read_calls':0,'requested_bytes':0,'full_reads':0,'opened':False,
            'closed':False,'opened_known':True,'close_error':'NONE',
            'records':[],'interpretation':'UNKNOWN','further_action_authorized':False}
    claimed=False;phase='PRECHECK_FAILED';records=[]
    try:
        require(type(spec) is ReadSpec,'SPEC_TYPE');spec.validate()
        phase='ALREADY_USED';ops.claim();claimed=True
        phase='PATH_IDENTITY_FAILED';require(ops.path_identity(spec.path)==spec.identity,'PATH_IDENTITY')
        phase='OPEN_FAILED';ops.open(spec.path)
        phase='HANDLE_IDENTITY_FAILED';require(ops.handle_identity()==spec.identity,'HANDLE_IDENTITY')
        for offset,length in RANGES:
            phase='HANDLE_IDENTITY_CHANGED';require(ops.handle_identity()==spec.identity,'HANDLE_IDENTITY')
            phase='SEEK_FAILED';ops.seek(offset)
            phase='READ_FAILED';result['read_calls']+=1;result['requested_bytes']+=length
            data=ops.read(length)
            phase='SHORT_OR_INVALID_READ';require(type(data) is bytes and len(data)==length,'SHORT_READ')
            result['full_reads']+=1
            phase='HANDLE_IDENTITY_CHANGED';require(ops.handle_identity()==spec.identity,'HANDLE_IDENTITY')
            phase='CLASSIFICATION_FAILED';records.append(classify_record(data,framing=spec.framing))
        phase='PATH_IDENTITY_CHANGED';require(ops.path_identity(spec.path)==spec.identity,'PATH_IDENTITY')
        result.update(outcome='COMPARED_METADATA_ONLY',error='NONE',records=records,
                      interpretation='PUBLIC_TEMPLATE_MATCHES_ONLY' if all(x['state']=='PUBLIC_TEMPLATE_MATCH' for x in records) else 'UNKNOWN')
    except BaseException:
        result.update(outcome='UNKNOWN',error=phase,records=[],interpretation='UNKNOWN')
    finally:
        if claimed:
            try:
                flag=ops.opened;require(type(flag) is bool,'HANDLE_STATE_TYPE');result['opened']=flag
            except BaseException:
                result['opened_known']=False;result.update(outcome='UNKNOWN',records=[],interpretation='UNKNOWN')
                if result['error']=='NONE':result['error']='HANDLE_STATE_UNKNOWN'
            try:result['closed']=ops.close() is True
            except BaseException:result['closed']=False
            if not result['closed']:
                result.update(outcome='UNKNOWN',close_error='CLOSE_UNKNOWN',records=[],interpretation='UNKNOWN')
                if result['error']=='NONE':result['error']='CLOSE_UNKNOWN'
    return result


class Win32ReadOps:
    """Owns one caller-authorized exact path. No path resolution or DLL on import.

    Current callers/tests may only bind a unique newly created synthetic file.
    This lexical allowlist is a boundary, not proof of human/OS authorization.
    """
    def __init__(self,allowed_path):
        self.allowed=canonical_path(allowed_path);self.used=False;self.lock=threading.Lock()
        self.opened=False;self.closed=False;self.handle=None;self.fd=None;self.api=None
        self.read_index=0;self.position=None;self.io_failed=False
    def claim(self):
        with self.lock:
            require(not self.used,'ALREADY_USED');self.used=True
    def check_path(self,path):
        require(canonical_path(path)==self.allowed,'PATH_NOT_ALLOWED')
        return self.allowed
    def path_identity(self,path):
        # Lexical rejection precedes EVERY file-system call. Never resolve an
        # unapproved real target as a fallback to a failing synthetic fixture.
        path=self.check_path(path)
        from pathlib import Path
        import os
        p=Path(path)
        for parent in reversed(p.parents):
            s=os.lstat(parent);require(hasattr(s,'st_file_attributes') and not s.st_file_attributes&1024,'ANCESTOR_REPARSE')
        s=os.lstat(p)
        require(stat.S_ISREG(s.st_mode) and hasattr(s,'st_file_attributes')
                and not s.st_file_attributes&1024,'LEAF_NOT_REGULAR')
        return Identity(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_file_attributes,s.st_nlink)
    def open(self,path):
        path=self.check_path(path)
        require(self.used and not self.opened and not self.closed,'OPEN_ONCE')
        import ctypes as C
        from ctypes import wintypes as W
        import msvcrt,os
        k=C.WinDLL('kernel32',use_last_error=True)
        class FT(C.Structure):_fields_=[('low',W.DWORD),('high',W.DWORD)]
        class FI(C.Structure):
            _fields_=[('attrs',W.DWORD),('created',FT),('accessed',FT),('written',FT),
                      ('volume',W.DWORD),('sizehi',W.DWORD),('sizelo',W.DWORD),('links',W.DWORD),('indexhi',W.DWORD),('indexlo',W.DWORD)]
        for name,rt,args in (
            ('CreateFileW',W.HANDLE,[W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE]),
            ('GetFileInformationByHandle',W.BOOL,[W.HANDLE,C.POINTER(FI)]),
            ('SetFilePointerEx',W.BOOL,[W.HANDLE,C.c_longlong,C.POINTER(C.c_longlong),W.DWORD]),
            ('ReadFile',W.BOOL,[W.HANDLE,W.LPVOID,W.DWORD,C.POINTER(W.DWORD),W.LPVOID]),
            ('CloseHandle',W.BOOL,[W.HANDLE])):
            fn=getattr(k,name);fn.restype=rt;fn.argtypes=args
        self.api=(C,W,k,FI)
        h=k.CreateFileW(path,0x80000000,1,None,3,0x00200000,None)
        require(h and h!=C.c_void_p(-1).value,'OPEN_FAILED')
        self.handle=int(h);self.opened=True
        # fd adopts the SAME handle; ownership never doubles. On conversion
        # failure close() still disposes this original handle once.
        self.fd=msvcrt.open_osfhandle(self.handle,os.O_RDONLY|os.O_BINARY)
    def handle_identity(self):
        import os,msvcrt
        require(self.opened and not self.closed,'HANDLE_STATE')
        require(msvcrt.get_osfhandle(self.fd)==self.handle,'SAME_HANDLE')
        C,W,k,FI=self.api;info=FI();require(k.GetFileInformationByHandle(self.handle,C.byref(info)),'HANDLE_INFO')
        s=os.fstat(self.fd);require(stat.S_ISREG(s.st_mode) and not info.attrs&1024,'HANDLE_REGULAR')
        return Identity(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,int(info.attrs),int(info.links))
    def seek(self,offset):
        require(self.used and not self.closed and not self.io_failed and self.read_index<2
                and offset==RANGES[self.read_index][0],'OFFSET_ONCE')
        C,W,k,FI=self.api;position=C.c_longlong()
        ok=k.SetFilePointerEx(self.handle,offset,C.byref(position),0)
        if not ok or position.value!=offset:self.io_failed=True;raise ControlledFailure('SEEK')
        self.position=offset
    def read(self,length):
        require(self.used and not self.closed and not self.io_failed and self.read_index<2
                and (self.position,length)==RANGES[self.read_index],'READ_CAP_ONCE')
        C,W,k,FI=self.api;buf=C.create_string_buffer(length);got=W.DWORD()
        self.read_index+=1;self.position=None
        ok=k.ReadFile(self.handle,buf,length,C.byref(got),None)
        if not ok or got.value>length:self.io_failed=True;raise ControlledFailure('READ')
        if got.value!=length:self.io_failed=True
        return buf.raw[:got.value]
    def close(self):
        if self.closed:return False
        self.closed=True
        if not self.opened:return True
        import os
        if self.fd is not None:
            os.close(self.fd);return True
        return bool(self.api[2].CloseHandle(self.handle))


def main(args=None):
    # No actual target option, metadata probe, approval flag or execution path.
    # A future authorized invocation needs separately reviewed caller binding.
    print(json.dumps({'state':'ACTUAL_PRIVATE_COMPARISON_NOT_AUTHORIZED','target_access':0}))
    return 78

if __name__=='__main__':raise SystemExit(main())
