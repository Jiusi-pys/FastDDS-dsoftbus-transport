"""Fixed inline diagnostic projections. No process, device or file reads here.

The registered remote command owns the postlude. Nothing restarts a halted
driver. Native return codes and the original DDS evaluator remain authoritative.
"""
from readonly_evidence import EvidenceUnknown,Projection,query,require

SPECIAL={'readonly.4':('B','process'),'data.a.launch':('A','cause'),'data.b.launch':('B','cause')}
REASONS=frozenset(('INLINE_TRUNCATED_FRAME','INLINE_PARTIAL_FRAME','INLINE_SINK_PENDING','INLINE_TAIL_WRITE',
 'INLINE_QUERY_FAILED_OR_UNKNOWN','INLINE_DUPLICATE_FRAME','INLINE_UNEXPECTED_FRAME','INLINE_PENDING_CAP',
 'PID_REUSED_OR_CHANGED','OWNER_MISMATCH','PROCESS_CHANGED_DURING_READ','PROCESS_PARENT_CHANGED',
 'REMOTE_BOUND_OR_IDENTITY_UNKNOWN','OPEN_FILE_CHANGED_OR_READ_FAILED','QUERY_EOF_INCOMPLETE',
 'QUERY_BEGIN','QUERY_INCOMPLETE','READ_COMMAND_NONZERO_OR_UNKNOWN','SOURCE_COUNT_OR_BOUNDARY_UNKNOWN',
 'ITEM_CAP','SOURCE_TOTAL_CAP','CLIENT_ENVELOPE_CAP','LINE_FORMAT','LINE_BOUND','HEX_FORMAT','ITEM_HEADER',
 'ITEM_ORDER','ITEM_ABSENCE','EMPTY_ITEM','TRAILING_DATA','NUMBER_UNKNOWN','NUMBER_RANGE','STREAM_STATE'))

def fixed_reason(error):
    return error.args[0] if type(error) is EvidenceUnknown and len(error.args)==1 and type(error.args[0]) is str and error.args[0] in REASONS else 'INLINE_FINALIZATION_UNKNOWN'

def postlude(side,expected,mode,root):
    require((side,mode) in (('B','process'),('A','cause'),('B','cause')),'INLINE_FIXED_MODE')
    # The query contains exec/exit/variable assignments: isolate all of them.
    return ("printf '\\nS1DIAG_BEGIN "+side+' '+mode+"\\n'\n(\n"+
            query(side,expected,mode=mode,root=root)+"\n)\ndiag_rc=$?\n"+
            "if test \"$diag_rc\" = 0; then printf 'S1DIAG_END "+side+" OK\\n'; "+
            "else printf 'S1DIAG_END "+side+" FAILED\\n'; fi\n")

class Frame:
    """Hold undecided line prefixes; never forward a diagnostic wire byte."""
    def __init__(self,side,expected,mode):
        self.side=side;self.mode=mode;self.parser=Projection(side,expected,mode=mode)
        self.pending=bytearray();self.phase='BEFORE';self.projected=None;self.finished=False
    def feed(self,data):
        require(not self.finished and type(data) is bytes and 0<len(data)<=4096,'INLINE_CHUNK')
        self.pending.extend(data);out=bytearray()
        while b'\n' in self.pending:
            n=self.pending.index(10)+1;raw=bytes(self.pending[:n]);del self.pending[:n]
            line=raw[:-1]
            if line.endswith(b'\r\r'):line=line[:-2]
            elif line.endswith(b'\r'):line=line[:-1]
            begin=('S1DIAG_BEGIN '+self.side+' '+self.mode).encode()
            end=('S1DIAG_END '+self.side+' OK').encode()
            if self.phase=='QUERY':
                if line.startswith(b'S1DIAG_END '):
                    require(line==end,'INLINE_QUERY_FAILED_OR_UNKNOWN')
                    self.projected=self.parser.finish();self.parser.erase();self.phase='AFTER'
                else:self.parser.feed(raw)
            elif line==begin:
                require(self.phase=='BEFORE','INLINE_DUPLICATE_FRAME');self.phase='QUERY'
            else:
                require(not line.startswith((b'S1DIAG_',b'S1RO_')),'INLINE_UNEXPECTED_FRAME')
                out.extend(raw)
        require(len(self.pending)<=(256 if self.phase=='QUERY' else 65536),'INLINE_PENDING_CAP')
        return bytes(out)
    def finish(self):
        require(not self.finished and self.phase!='QUERY','INLINE_TRUNCATED_FRAME')
        pending=bytes(self.pending)
        require(not pending or not any(pending.startswith(magic) or magic.startswith(pending)
                    for magic in (b'S1DIAG_',b'S1RO_')),'INLINE_PARTIAL_FRAME')
        tail=bytes(self.pending);self.pending.clear();self.finished=True
        if self.projected is None:
            self.projected={'side':self.side,'record':'NOT_OBSERVED','process':'UNKNOWN',
                            'SDK_quiet':'UNKNOWN','remote_descendants':'UNKNOWN'}
        return tail
    def erase(self):self.pending.clear();self.parser.erase()

class Capture:
    def __init__(self,adapter,original):self.a=adapter;self.original=original
    def write(self,raw):
        require(self.a.ready is not None and not self.a.written,'INLINE_CAPTURE_ORDER')
        value=self.original.write(self.a.ready)
        require(value==len(self.a.ready),'INLINE_CAPTURE_SHORT_WRITE')
        self.a.written=True;return len(raw)
    def flush(self):return self.original.flush()
    def close(self):
        try:self.a.before_capture_close()
        finally:self.original.close()

class Buffer(bytearray):
    def __init__(self,adapter):super().__init__();self.a=adapter
    def extend(self,raw):
        require(self.a.ready is not None and self.a.written,'INLINE_BUFFER_ORDER')
        super().extend(self.a.ready);self.a.ready=None;self.a.written=False

class Adapter:
    def __init__(self,root,side,expected,mode):
        require(not any(root.output) and all(f.tell()==0 for f in root.captures),'INLINE_FIRST_READ_REQUIRED')
        self.root=root;self.frame=Frame(side,expected,mode);self.ready=None;self.written=False;self.done=False
        self.final_error=None
        self.capture=Capture(self,root.captures[0]);self.buffer=Buffer(self)
        root.captures[0]=self.capture;root.output[0]=self.buffer
    def feed(self,data):
        require(self.final_error is None,'INLINE_ALREADY_REJECTED')
        require(self.ready is None,'INLINE_UNCONSUMED_CHUNK')
        try:self.ready=self.frame.feed(data)
        except BaseException as error:
            self.final_error=fixed_reason(error);self.frame.erase();raise
    def finish(self):
        require(self.final_error is None,'INLINE_ALREADY_REJECTED')
        if not self.done:
            try:
                require(self.ready is None,'INLINE_SINK_PENDING')
                tail=self.frame.finish()
                if tail:
                    require(self.capture.original.write(tail)==len(tail),'INLINE_TAIL_WRITE')
                    self.capture.original.flush();bytearray.extend(self.buffer,tail)
                self.done=True
            except BaseException as error:
                self.final_error=fixed_reason(error);self.frame.erase();raise
        return self.frame.projected
    def before_capture_close(self):
        # Original close must always run, including projection/record failures.
        # Only cached EOF is used here; this adds no process/identity API call.
        if self.done or self.final_error is not None:return
        if self.root.receipt.get('EOF') is not True:self.final_error='INLINE_EOF_UNCONFIRMED';return
        try:self.finish()
        except BaseException:pass # fixed final_error retained; never hide OS close errors
    def erase(self):self.ready=None;self.frame.erase()

class StartupEvidenceMixin:
    """Only three fixed stdout streams; original ownership APIs are untouched."""
    def __init__(self,grant):
        require(getattr(grant,'startup_evidence_bound',False) is True,'STARTUP_EFFECT_NOT_GRANTED')
        self.startup_projections={};self.startup_projection_errors={};self.startup_attempted=set()
        super().__init__(grant)
    def publish_projection(self,root,phase):
        if root.case in self.startup_attempted:return
        self.startup_attempted.add(root.case)
        adapter=getattr(root,'startup_adapter',None)
        reason=adapter.final_error if adapter is not None else 'INLINE_INSTALLATION_INCOMPLETE'
        if adapter is None or not adapter.done or reason is not None:
            reason=reason or 'INLINE_CAPTURE_CLOSE_NOT_OBSERVED'
            self.startup_projection_errors[root.case]=reason
            projected={'record':'UNKNOWN','reason':reason,'SDK_quiet':'UNKNOWN'}
        else:projected=adapter.frame.projected
        # Format validity is an observation, not transport/remote attestation.
        envelope={'phase':phase,'transport_qualified':False,'observation_only':True,'projection':projected}
        try:
            self.journal.emit({'startup_evidence':{'step':root.case,**envelope}})
            require(not self.journal.failed,'INLINE_RECORD_FAILED')
        except BaseException:
            self.startup_projection_errors[root.case]='INLINE_RECORD_FAILED';raise
        self.startup_projections[root.case]=envelope
    def register(self,step):
        root=super().register(step)
        if step.id in SPECIAL:root.startup_adapter=None;root.startup_required=True
        return root
    def create_and_resume(self,root,step,deadline):
        # register() already guards failed/partially resumed creation. Cleanup
        # cannot capture raw diagnostic output before successful installation.
        super().create_and_resume(root,step,deadline)
        if step.id in SPECIAL:
            from goal_read_context import EXPECTED
            side,mode=SPECIAL[step.id]
            root.startup_adapter=Adapter(root,side,EXPECTED[side] if mode=='process' else {},mode)
    def observe_chunk(self,root,stream,data,before,after,phase):
        if root.case in SPECIAL:
            require(getattr(root,'startup_required',False) and root.startup_adapter is not None,'INLINE_INSTALLATION_INCOMPLETE')
        # Original notice and full raw-byte budget see every input byte.
        super().observe_chunk(root,stream,data,before,after,phase)
        if root.case in SPECIAL and stream=='stdout':root.startup_adapter.feed(data)
    def outcome(self,root,step):
        if step.id in SPECIAL and root.state()=='TERMINAL' and root.receipt.get('EOF') is True:
            require(step.id not in self.startup_projection_errors,'INLINE_PRIOR_REJECTION')
            require(root.startup_adapter is not None,'INLINE_INSTALLATION_INCOMPLETE')
            projected=root.startup_adapter.finish()
            self.publish_projection(root,'MONITOR_EOF')
            if step.id=='readonly.4':
                require(projected.get('process')=='PID_NOT_VISIBLE_AT_TWO_SNAPSHOTS','LEGACY_B_PRESENT_OR_UNKNOWN')
                # This permits no cleanup claim. Descendants/SDK quiet remain
                # UNKNOWN and require explicit residual-risk acceptance.
        return super().outcome(root,step)
    def cleanup(self,root,deadline):
        try:
            row=super().cleanup(root,deadline)
            if root.case in SPECIAL:
                try:self.publish_projection(root,'CLEANUP_EOF')
                except BaseException:pass # original resource close already completed
                if root.case in self.startup_projection_errors:
                    row=dict(row);row['business_blocked']=True
                    row['startup_diagnostic_error']=self.startup_projection_errors[root.case]
            return row
        finally:
            adapter=getattr(root,'startup_adapter',None)
            if adapter is not None:adapter.erase()
