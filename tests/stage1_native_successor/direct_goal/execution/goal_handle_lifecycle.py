"""Leaf lifecycle contracts for original owned Win32 handles. No process factory."""
from dataclasses import dataclass
import time

@dataclass(frozen=True)
class HandleBinding:
    process: int
    image_pin: int
    pid: int
    birth: int
    image: str
    file_id: tuple

class LifecycleError(Exception):
    def __init__(self,code):self.code=code;super().__init__(code)

class ImageUnavailable(LifecycleError):
    def __init__(self,error):self.error=error;super().__init__('QUERY_IMAGE_FAILED')

def require(value,code):
    if not value:raise LifecycleError(code)

def observe_binding(binding,ops):
    """Read only the already-owned process and image-pin objects.

    A failed live image read is not ignored: only a new positive terminal
    observation of this exact original object permits the terminal proof.
    """
    require(type(binding) is HandleBinding,'BINDING_TYPE')
    require(all(type(v) is int and v>0 for v in [binding.process,binding.image_pin,binding.pid,binding.birth]) and isinstance(binding.image,str) and bool(binding.image),'BINDING_FIELDS')
    require(type(binding.file_id) is tuple and len(binding.file_id)==3 and all(type(v) is int and v>=0 for v in binding.file_id),'BINDING_FILE_ID')
    def anchored():
        require(ops.pid_birth(binding.process)==(binding.pid,binding.birth),'PROCESS_BINDING_CHANGED')
        require(tuple(ops.file_id(binding.image_pin))==binding.file_id,'IMAGE_PIN_CHANGED')
    def terminal(transition=None):
        require(ops.wait(binding.process)==0,'TERMINAL_WAIT_NOT_SIGNALED')
        anchored();code=ops.exit_code(binding.process)
        require(type(code) is int and 0<=code<=0xffffffff,'TERMINAL_EXIT_UNKNOWN')
        return {'phase':'TERMINAL','exit_code':code,'transition_error':transition,
                'proof':{'method':'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN',
                         'hProcess':binding.process,'pid':binding.pid,'birth':binding.birth,
                         'image':binding.image,'FileID':list(binding.file_id),
                         'required_apis_ok':True,'image_pin_retained_at_check':True,
                         'full_image_was_verified_while_live':True}}
    anchored();state=ops.wait(binding.process);require(state in (0,258),'WAIT_UNKNOWN')
    if state==0:return terminal()
    try:image=ops.image(binding.process)
    except ImageUnavailable as failure:
        if ops.wait(binding.process)!=0:raise
        return terminal({'api':'QueryFullProcessImageNameW','error':failure.error,
                         'classification':'LIVE_TO_TERMINAL_IMAGE_UNAVAILABLE'})
    require(image==binding.image,'LIVE_IMAGE_CHANGED')
    anchored();state=ops.wait(binding.process);require(state in (0,258),'WAIT_UNKNOWN')
    if state==0:return terminal()
    return {'phase':'LIVE','image_checked':True}

def terminal_receipt_qualified(receipt):
    proof=receipt.get('terminal_proof') or {};owner=receipt.get('owner') or {}
    return (all(type(proof.get(k)) is int and proof[k]>0 for k in ['hProcess','pid','birth'])
            and isinstance(proof.get('image'),str) and bool(proof['image'])
            and isinstance(proof.get('FileID'),list) and len(proof['FileID'])==3
            and receipt.get('created') is True and receipt.get('bound') is True
            and receipt.get('terminal') is True and receipt.get('EOF') is True
            and receipt.get('terminal_binding_verified') is True
            and receipt.get('signal_authority') is False
            and ('drain' not in receipt or receipt['drain'].get('status')=='EOF')
            and proof.get('method')=='ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN'
            and proof.get('hProcess')==receipt.get('hProcess')
            and proof.get('pid')==owner.get('pid') and proof.get('birth')==owner.get('birth')
            and proof.get('image')==owner.get('image') and proof.get('FileID')==receipt.get('FileID')
            and proof.get('required_apis_ok') is True
            and proof.get('image_pin_retained_at_check') is True
            and proof.get('full_image_was_verified_while_live') is True)

def bounded_drain(read_once,eof,*,deadline,remaining,clock=time.monotonic,
                  pause=time.sleep,cancelled=lambda:False,max_polls=4096):
    require(type(remaining) is int and remaining>=0,'DRAIN_CAP_INVALID')
    used=0;polls=0;start=clock();last=start
    def result(status):
        finished=clock();require(finished>=last,'DRAIN_CLOCK_REGRESSION')
        if cancelled():status='CANCELLED'
        elif status=='EOF' and finished>=deadline:status='DEADLINE'
        return {'status':status,'EOF':bool(eof()),'bytes':used,'polls':polls,'started':start,'finished':finished,'deadline':deadline}
    while True:
        now=clock();require(now>=last,'DRAIN_CLOCK_REGRESSION');last=now
        if cancelled():return result('CANCELLED')
        if now>=deadline:return result('DEADLINE')
        if eof():return result('EOF')
        require(polls<max_polls,'DRAIN_POLL_CAP');polls+=1
        chunks=read_once(remaining-used);amount=sum(len(data) for _,data in chunks)
        require(used+amount<=remaining,'DRAIN_OUTPUT_CAP');used+=amount
        if not eof():pause(min(.005,max(0,deadline-clock())))
