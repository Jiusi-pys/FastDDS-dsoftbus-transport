"""Marked NONBUSINESS Windows file fixture for the real controller CLI.

All event producers/token/PID/board fields below are SYNTHETIC. No DDS, HDC,
SDK, Native image or identity API is run. The outer qualified test monitor
contains this Python process and its one positively-created Windows child.
"""
import pathlib,json,hashlib,struct,subprocess,sys,time,os,stat,ctypes
from ctypes import wintypes as W

class FixtureFailure(Exception):
    pass

def require(value,code):
    if not value:
        raise FixtureFailure(code)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def regular(path,cap):
    s=path.lstat()
    require(stat.S_ISREG(s.st_mode) and not s.st_file_attributes&0x400 and s.st_nlink==1 and 0<s.st_size<=cap,'FILE_METADATA')
    data=path.read_bytes();after=path.lstat()
    require((s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns)==(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns),'FILE_CHANGED')
    return data

def atomic(path,data,replace=False):
    require(not path.exists() or replace,'OUTPUT_EXISTS')
    temporary=path.with_name(path.name+'.fixture-writing')
    with temporary.open('xb') as f:
        f.write(data);f.flush();os.fsync(f.fileno())
    os.replace(temporary,path)

def text(value):
    data=value.encode('ascii');return struct.pack('>H',len(data))+data

class LockedImage:
    def __init__(self,path):
        self.kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        self.kernel.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,ctypes.c_void_p,W.DWORD,W.DWORD,W.HANDLE];self.kernel.CreateFileW.restype=W.HANDLE
        self.kernel.CloseHandle.argtypes=[W.HANDLE];self.kernel.CloseHandle.restype=W.BOOL
        # Hold before hashing until the known child has exited. Read-sharing
        # permits the Windows image loader, while write/delete sharing is denied.
        self.handle=self.kernel.CreateFileW(str(path),0x80000000,1,None,3,0x00200000,None)
        require(self.handle not in (None,ctypes.c_void_p(-1).value),'IMAGE_LOCK')
    def close(self):
        if self.handle is not None:
            require(self.kernel.CloseHandle(self.handle),'IMAGE_LOCK_CLOSE');self.handle=None

def run_controller_file_fixture(bindings):
    image=pathlib.Path(bindings['controller']['path']);expected=bindings['controller']['sha256']
    base=pathlib.Path('E:/Codex-FastDDS-stage1-directdds-mainpath-20261030-d01').resolve()
    require(image.resolve().is_relative_to(base) and image.name=='goal_controller.exe','FIXED_LOCAL_CONTROLLER_SCOPE')
    image=image.resolve();data=regular(image,4*1048576)
    require(digest(data)==expected and expected!='99043a8267aec70e2aa78c056a8d2de59505e1c8bb96ced3c9ac6aad7c834b7c','CONTROLLER_BYTES_NOT_HDC')
    require(data[:2]==b'MZ','WINDOWS_PE_ONLY')
    pe=int.from_bytes(data[60:64],'little');require(data[pe:pe+6]==b'PE\0\0\x64\x86','X64_NON_NATIVE')
    root=pathlib.Path(bindings['case_root'])
    require(root.parent.resolve().is_relative_to(base) and not root.exists(),'FRESH_OWNED_CASE')
    for parent in [root.parent,*root.parent.parents]:
        require(not parent.lstat().st_file_attributes&0x400,'NO_REPARSE_PARENT')
    root.mkdir()
    fields=[('format','S1DIRECTPAIR1'),('run','7051b9ea00000001'),('image_sha','a'*64),('bridge_sha','b'*64),('namespace','goal_fixture'),('domain','151'),('package','com.kaihong.mdds'),('prefix','com.kaihong.mdds.fixture')]
    boards=['3e01ff55454d202020104033bf453b00','3e01ff55454d202020104433991c3b00']
    for side,name in enumerate(['a','b']):
        values=[('pid',str(101+side)),('parent','77'),('board',boards[side]),('token',str(2+side)),('udid','SYNTHETIC_UDID_'+name),('network','SYNTHETIC_NET_'+name),('slot',str(1+side)),('initiator','com.kaihong.mdds.fixture.T'+name)]
        fields.extend((name+'_'+key,value) for key,value in values)
    pair=''.join(key+'='+value+'\n' for key,value in fields).encode('ascii');atomic(root/'pair',pair)
    pair_sha=digest(pair);run=int('7051b9ea00000001',16)
    def envelope(side):
        return struct.pack('>QQIII',run,2+side,side,151,101+side)+b''.join(text(s) for s in [boards[side],'a'*64,pair_sha,'goal_fixture'])
    def frame(kind,side,sequence,body):
        payload=envelope(side)+body
        return struct.pack('>4sHBBIIQ',b'S1CF',1,kind,1 if kind==1 else 2,len(payload),sequence,run)+payload
    journals=[bytearray(),bytearray()];sequences=[1,1];stages=[]
    def event(side,kind,ordinal=0):
        producer=1-side if kind==4 else side
        payload=bytes(((run>>(8*(i%8)))^producer^ordinal^i)&255 for i in range(32)) if ordinal else bytes(32)
        # Flag2 is explicitly HOST_FIXTURE_LISTEN. --coordinate (real flag1)
        # must never accept this fixture as a Native listener attestation.
        body=struct.pack('>IIQ',kind,ordinal,producer*100+ordinal if ordinal else 0)+bytes([31+producer])*16+payload+struct.pack('>iiIII',3,0,2,1,1)
        journals[side].extend(frame(1,side,sequences[side],body));sequences[side]+=1
    def publish_journals():
        for side in range(2):atomic(root/('events.'+('A' if side==0 else 'B')),bytes(journals[side]),replace=True)
    process=None;end=time.monotonic()+4;lock=LockedImage(image)
    try:
        require(digest(regular(image,4*1048576))==expected,'LOCKED_IMAGE_BYTES')
        argv=[str(image),'--fixture-coordinate',str(root),'2000']
        process=subprocess.Popen(argv,cwd=root,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,close_fds=True,creationflags=subprocess.CREATE_NO_WINDOW)
        def deliver(sequence,release):
            paths=[root/('release.'+side+'.'+str(sequence)) for side in ['A','B']]
            while not all(p.exists() for p in paths):
                require(process.poll() is None,'CHILD_EARLY_EXIT');require(time.monotonic()<end,'FIXTURE_DEADLINE');time.sleep(.001)
            values=[regular(p,1024) for p in paths]
            for side in range(2):require(values[side]==frame(2,side,sequence,struct.pack('>I',release)),'EXACT_RELEASE_FRAME')
            receipt=('S1DELIVERY1\n7051b9ea00000001\n'+str(sequence)+'\n'+digest(values[0])+'\n'+digest(values[1])+'\n').encode('ascii')
            atomic(root/('delivered.'+str(sequence)),receipt);stages.append({'sequence':sequence,'release':release,'frame_hashes':[digest(v) for v in values]})
        for side in range(2):event(side,1)
        publish_journals();deliver(1,1)
        for side in range(2):event(side,2)
        publish_journals();deliver(2,2)
        for side in range(2):
            for kind in [3,4]:
                for ordinal in range(1,4):event(side,kind,ordinal)
            event(side,5)
        publish_journals();deliver(3,4)
        for side in range(2):event(side,6)
        publish_journals();deliver(4,3)
        for side in range(2):event(side,7)
        publish_journals()
        out,err=process.communicate(timeout=max(.01,end-time.monotonic()))
        require(process.returncode==0 and not out and not err,'CONTROLLER_NORMAL_ZERO')
        report=regular(root/'controller-result',512)
        require(report==b'S1CONTROLLER1\nfixture=1\nrun=7051b9ea00000001\nROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1\nNative_attestation=0\nentry_restore_and_exit_checked_separately=1\n','MARKED_RESULT_NOT_NATIVE')
        require(sequences==[12,12] and len(stages)==4,'EXACT_TWO_ELEVEN_EVENTS')
        return {'status':'PASS_MARKED_WINDOWS_FILE_HANDSHAKE','child_pid':process.pid,'child_exit':process.returncode,'argv':argv,'source_image_sha256':expected,'release_ledger':stages,'events_each':11,'protocol_payload':'SYNTHETIC only, not DDS data','Native_HDC_identity_SDK_DDS':0,'native_attestation':False}
    finally:
        # No numeric/name kill or borrowed ownership. Unexpected live child is
        # a failure for the existing bounded task monitor, never a clean claim.
        if process is not None and process.poll() is None:
            try:process.wait(timeout=max(.01,end-time.monotonic()))
            except subprocess.TimeoutExpired:pass
        lock.close()

if __name__=='__main__':
    if sys.flags.optimize or len(sys.argv)!=3:
        raise SystemExit(64)
    config=pathlib.Path(sys.argv[1]);require(digest(regular(config,65536))==sys.argv[2],'FINAL_BINDING_HASH')
    result=run_controller_file_fixture(json.loads(config.read_text(encoding='utf-8-sig')))
    print(json.dumps(result,sort_keys=True))
