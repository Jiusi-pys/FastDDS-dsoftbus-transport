"""LLDB-side, read-only original RMW instruction observations. No inferior calls."""
import json,time,hashlib
from pathlib import Path
import lldb

# Exact reviewed ELF instruction tables, not version-name or wildcard matching.
LAYOUTS={
 'dceb78b7b38f1ef6c6bc2970d52cb74fb7e9be7ad24cafd2b29031f0578ff7ec':{
  'got':0x85eb0,'cast_return':0x7cea8,'points':{
   0x7ce68:'REMOVE_ENTER',0x7cea4:'CAST_CALL',0x7cea8:'CAST_RETURN',
   0x3c8c4:'MAP_ENTER',0x3c924:'MAP_LOOKUP',0x3c934:'COUNT_BEFORE',
   0x3c93c:'COUNT_AFTER',0x3c958:'DDS_DELETE_CALL',0x3c95c:'DDS_DELETE_RETURN'}},
 '651953ff2844f792a49258a5cb03b091759ebce1d8c2d58399aea58d06a8d8b7':{
  'got':0x860c0,'cast_return':0x7d0ac,'points':{
   0x7d06c:'REMOVE_ENTER',0x7d0a8:'CAST_CALL',0x7d0ac:'CAST_RETURN',
   0x3cae4:'MAP_ENTER',0x3cb44:'MAP_LOOKUP',0x3cb54:'COUNT_BEFORE',
   0x3cb5c:'COUNT_AFTER',0x3cb78:'DDS_DELETE_CALL',0x3cb7c:'DDS_DELETE_RETURN'}},
 '155140e25c64ed95a69e206c2556f3bd5af4ba05dc4b2bd3d3e101c8ef4135a9':{
  'got':0x86090,'cast_return':0x7d094,'points':{
   0x7d054:'REMOVE_ENTER',0x7d090:'CAST_CALL',0x7d094:'CAST_RETURN',
   0x3cacc:'MAP_ENTER',0x3cb2c:'MAP_LOOKUP',0x3cb3c:'COUNT_BEFORE',
   0x3cb44:'COUNT_AFTER',0x3cb60:'DDS_DELETE_CALL',0x3cb64:'DDS_DELETE_RETURN'}}}
def instruction_layout(digest):
    if digest not in LAYOUTS:raise RuntimeError('RMW_INSTRUCTION_TABLE_HASH_MISMATCH')
    return LAYOUTS[digest]
SESSION=None
RTTI_FASTDDS_PROFILE='0ab1972136307f84ba55ca353c533708175aaa0bfbc60ab8762c49257e4e6a5e'
RTTI_FASTDDS_PROFILES=(RTTI_FASTDDS_PROFILE,'d4083991a41ee991a81466b575bffb7c68f1b05e2110a8e394440e54d7be1434')
RTTI_NAMES=('N8eprosima7fastdds3dds16TopicDescriptionE','N8eprosima7fastdds3dds5TopicE',
            'N8eprosima7fastdds3dds12DomainEntityE')

class Trace:
    def __init__(self,cfg):
        self.cfg=cfg;self.start=time.monotonic();self.seq=0;self.hits=0;self.context={};self.breakpoints={};self.callees=set();self.disabled=False
        self.rtti_pairs={}
        self.fastrtps_sha=hashlib.sha256((Path(cfg['package'])/'libfastrtps.so.2.14').read_bytes()).hexdigest()
        rmw_sha=hashlib.sha256(Path(cfg['rmw_local']).read_bytes()).hexdigest();self.layout=instruction_layout(rmw_sha)
        self.output=Path(cfg['output']);self.output.mkdir(exist_ok=False)
        self.stream=(self.output/'original-rmw.jsonl').open('x',encoding='utf-8')
        self.debugger=lldb.SBDebugger.Create();self.debugger.SetAsync(True)
        self.target=self.debugger.CreateTarget(cfg['main_local'])
        if not self.target.IsValid():raise RuntimeError('LOCAL_MAIN_TARGET_INVALID')
        self.debugger.HandleCommand('settings set target.exec-search-paths '+json.dumps(cfg['package']))
        self.debugger.HandleCommand('target modules search-paths add '+json.dumps(cfg['remote'])+' '+json.dumps(cfg['package']))
        result=lldb.SBCommandReturnObject()
        self.debugger.GetCommandInterpreter().HandleCommand('gdb-remote 127.0.0.1:'+str(cfg['host_port']),result)
        (self.output/'connect.stdout').write_text(result.GetOutput() or '',encoding='utf-8')
        (self.output/'connect.stderr').write_text(result.GetError() or '',encoding='utf-8')
        self.process=self.target.GetProcess()
        if not result.Succeeded() or not self.process.IsValid():raise RuntimeError('DEBUGGER_CONNECT_FAILED')
        if self.process.GetProcessID()!=cfg['pid']:raise RuntimeError('DEBUGGER_PID_MISMATCH')
        module=next((m for m in self.target.modules if m.GetFileSpec().GetFilename()=='librmw_fastrtps_shared_cpp.so'),None)
        if module is None:raise RuntimeError('RMW_MODULE_NOT_LOADED')
        header=module.GetObjectFileHeaderAddress();load=header.GetLoadAddress(self.target);file=header.GetFileAddress()
        if load==lldb.LLDB_INVALID_ADDRESS or file==lldb.LLDB_INVALID_ADDRESS:raise RuntimeError('RMW_LOAD_SLIDE_UNAVAILABLE')
        self.slide=load-file
        for address,name in self.layout['points'].items():
            bp=self.target.BreakpointCreateByAddress(self.slide+address)
            if not bp.IsValid() or bp.GetNumLocations()!=1:raise RuntimeError('ORIGINAL_BREAKPOINT_UNRESOLVED:'+name)
            self.breakpoints[bp.GetID()]=name
        self.emit('BOUND',slide=self.slide,rmw_sha256=rmw_sha,module_uuid=module.GetUUIDString(),pid=self.process.GetProcessID())
        self.rmw_sha=rmw_sha

    def emit(self,event,**fields):
        self.seq+=1
        row={'schema':1,'run':self.cfg['run'],'side':self.cfg['side'],'pid':self.cfg['pid'],
             'seq':self.seq,'event':event,'source':'ORIGINAL_RMW_INSTRUCTION','elapsed_ms':round((time.monotonic()-self.start)*1000),**fields}
        self.stream.write(json.dumps(row,sort_keys=True)+'\n');self.stream.flush()
    def reg(self,frame,name):
        value=frame.FindRegister(name)
        if not value.IsValid() or value.GetError().Fail():return None
        return value.GetValueAsUnsigned()
    def word(self,address):
        if not address:return None
        error=lldb.SBError();data=self.process.ReadMemory(address,8,error)
        if error.Fail() or len(data)!=8:return None
        return int.from_bytes(data,'little')
    def module_at(self,address):
        if address is None:return 'NOT_OBSERVED'
        found=self.target.ResolveLoadAddress(address);module=found.GetModule()
        return module.GetFileSpec().GetFilename() if module.IsValid() else 'UNRESOLVED'
    def rtti_header(self,address):
        row={'address':address,'status':'READ_FAILURE','name_limit':128}
        if not address:return row
        vptr=self.word(address);nameptr=self.word(address+8)
        row.update(vptr=vptr,vptr_module=self.module_at(vptr),name_pointer=nameptr,
                   slot_40=self.word(vptr+40) if vptr else None)
        row['slot_40_module']=self.module_at(row['slot_40'])
        raw=b'';terminated=False
        if nameptr:
            for offset in range(0,128,16):
                error=lldb.SBError();part=self.process.ReadMemory(nameptr+offset,16,error)
                if error.Fail() or len(part)!=16:row['name_read_error']=str(error);break
                raw+=part
                if b'\0' in raw:raw=raw.split(b'\0',1)[0];terminated=True;break
        name=raw.decode('ascii',errors='replace') if terminated else None
        # Only public names relevant to this exact Topic layout are exported;
        # arbitrary pointed-to bytes never become diagnostic free text.
        row.update(name=name if name in RTTI_NAMES else None,name_truncated=bool(nameptr and not terminated and len(raw)==128),
                   name_status='KNOWN_PUBLIC_NAME' if name in RTTI_NAMES else 'NOT_IN_BOUND_NAME_PROFILE')
        if vptr and nameptr and row['slot_40'] and name in RTTI_NAMES:row['status']='CAPTURED'
        return row
    def rtti_snapshot(self,source,target,dynamic):
        if self.fastrtps_sha not in RTTI_FASTDDS_PROFILES:return {'status':'UNBOUND_PROFILE'}
        pair=(source,target,dynamic)
        if pair in self.rtti_pairs:return {'status':'REFERENCED_SNAPSHOT','snapshot':self.rtti_pairs[pair]}
        if len(self.rtti_pairs)>=4:return {'status':'SNAPSHOT_LIMIT','limit':4}
        identity=len(self.rtti_pairs)+1;self.rtti_pairs[pair]=identity
        row={'status':'LAYOUT_NOT_CONFIRMED','snapshot':identity,'limit':4,'profile_sha256':self.fastrtps_sha,
             'scope':'STOPPED_ORIGINAL_CAST_READ_ONLY_NO_INFERIOR_CALLS',
             'source':self.rtti_header(source),'target':self.rtti_header(target),'dynamic_rtti':dynamic}
        # The bound ELF's Topic RTTI is a 56-byte vmi record: two public bases,
        # flags/count at+16 and base records at+24/+40 (AArch64 pointers).
        if dynamic!=target or row['target'].get('name')!=RTTI_NAMES[1]:return row
        packed=self.word(target+16)
        if packed is None or packed>>32!=2:return row
        row.update(flags=packed&0xffffffff,base_count=2,bases=[])
        for offset in (24,40):
            base=self.word(target+offset);header=self.rtti_header(base)
            a=row['source'].get('name_pointer');b=header.get('name_pointer')
            row['bases'].append({'header':header,'offset_flags_raw':self.word(target+offset+8),
                'source_rtti_equal':base==source if base and source else None,
                'source_name_pointer_equal':a==b if a and b else None})
        complete=all(h.get('status')=='CAPTURED' for h in [row['source'],row['target']]+[b['header'] for b in row['bases']]) and all(b['offset_flags_raw'] is not None for b in row['bases'])
        row['status']='CAPTURED_BOUND_TOPIC_LAYOUT' if complete else 'CAPTURED_WITH_READ_FAILURE'
        return row
    def hit(self,frame,location):
        self.hits+=1
        thread=frame.GetThread().GetThreadID();name=self.breakpoints.get(location.GetBreakpoint().GetID())
        if self.hits>self.cfg['hit_limit'] or time.monotonic()-self.start>self.cfg['seconds']:
            if not self.disabled:
                self.emit('OBSERVATION_LIMIT',hit_limit=self.cfg['hit_limit'],seconds=self.cfg['seconds'])
                for bp in self.target.breakpoint_iter():bp.SetEnabled(False)
                self.disabled=True
            return False
        pc=frame.GetPC();row={'thread':thread,'pc':pc,'image_pc':pc-self.slide}
        if name=='REMOVE_ENTER':
            context={k:self.reg(frame,k) for k in ('x0','x1','x2','x3')};self.context[thread]=context
            row.update(participant_info=context['x0'],event_listener=context['x1'],topic_description=context['x2'])
        elif name=='CAST_CALL':
            original=self.reg(frame,'x0');src=self.reg(frame,'x1');dst=self.reg(frame,'x2');vptr=self.word(original)
            slot=self.slide+self.layout['got'];callee=self.word(slot)
            row.update(original_source=original,source_rtti=src,target_rtti=dst,hint=self.reg(frame,'x3'),
                vtable=vptr,vtable_module=self.module_at(vptr),source_rtti_module=self.module_at(src),
                target_rtti_module=self.module_at(dst),vtable_minus8_word=self.word(vptr-8) if vptr else None,
                offset_to_top_raw=self.word(vptr-16) if vptr else None,callsite_got=slot,
                got_value=callee,got_candidate_module=self.module_at(callee),binding_scope='ACTUAL_CALLSITE_SLOT_NOT_DLSYM')
            row['rtti_snapshot']=self.rtti_snapshot(src,dst,row['vtable_minus8_word'])
            if callee and callee not in self.callees and self.module_at(callee) in ('libc++.so','libc++_shared.so'):
                bp=self.target.BreakpointCreateByAddress(callee)
                self.breakpoints[bp.GetID()]='CAST_CALLEE';self.callees.add(callee)
        elif name=='CAST_CALLEE':
            if self.reg(frame,'x30')!=self.slide+self.layout['cast_return']:return False
            # A process-wide ABI breakpoint also stops unrelated dynamic_cast
            # users before this LR filter can run. Keep one actual binding
            # observation per callee, then stop perturbing unrelated calls.
            # Retaining self.callees prevents later CAST_CALL reinstallation.
            location.GetBreakpoint().SetEnabled(False)
            row.update(actual_callee=pc,actual_callee_module=self.module_at(pc),return_address=self.reg(frame,'x30'))
            row.update(capture_scope='FIRST_MATCH_PER_CALLEE',callee_breakpoint_disabled=True)
        elif name=='CAST_RETURN':row.update(original_cast_result=self.reg(frame,'x0'),original_context=self.context.get(thread),result_observed=self.reg(frame,'x0') is not None)
        elif name=='MAP_ENTER':row.update(participant_info=self.reg(frame,'x0'),topic=self.reg(frame,'x1'),event_listener=self.reg(frame,'x2'))
        elif name=='MAP_LOOKUP':
            row.update(iterator=self.reg(frame,'x0'),end_iterator=self.reg(frame,'x8'),
                       map_hit=None if self.reg(frame,'x0') is None or self.reg(frame,'x8') is None else self.reg(frame,'x0')!=self.reg(frame,'x8'),
                       mutex_scope='ORIGINAL_RMW_MAP_MUTEX_HELD')
        elif name=='COUNT_BEFORE':row.update(use_count_before=self.reg(frame,'x9'),record=self.reg(frame,'x8'),mutex_scope='ORIGINAL_RMW_MAP_MUTEX_HELD')
        elif name=='COUNT_AFTER':row.update(use_count_after=self.reg(frame,'x9'),record=self.reg(frame,'x8'),mutex_scope='ORIGINAL_RMW_MAP_MUTEX_HELD')
        elif name=='DDS_DELETE_CALL':row.update(participant=self.reg(frame,'x0'),topic=self.reg(frame,'x1'))
        elif name=='DDS_DELETE_RETURN':row.update(dds_return_register=self.reg(frame,'w0'),value_scope='ORIGINAL_DDS_RETURN_REGISTER')
        if name:self.emit(name,**row)
        return False
    def run(self):
        # This debugger has its own public event queue. Dequeuing process events
        # updates LLDB's public state and executes breakpoint stop actions.
        # GetState() alone neither does that work nor proves inferior progress.
        listener=self.debugger.GetListener()
        continued=False;ready=False;state_events=0;running_consumed=False;restart_pending=False
        def publish_ready():
            nonlocal ready
            (self.output/'READY.json').write_text(json.dumps({'run':self.cfg['run'],'side':self.cfg['side'],'pid':self.cfg['pid'],
                'status':'ORIGINAL_RMW_BREAKPOINTS_INSTALLED','slide':self.slide,'rmw_sha256':self.rmw_sha,
                'execution_state':'RUNNING_EVENT_CONSUMED'}))
            ready=True
        def drain():
            nonlocal ready,state_events,running_consumed,restart_pending
            for _ in range(64):
                event=lldb.SBEvent()
                if not listener.GetNextEvent(event):return
                if lldb.SBProcess.EventIsProcessEvent(event):
                    observed=lldb.SBProcess.GetProcessFromEvent(event)
                    if observed.GetProcessID()!=self.cfg['pid']:
                        raise RuntimeError('DEBUGGER_EVENT_PID_MISMATCH')
                    if event.GetType() & lldb.SBProcess.eBroadcastBitStateChanged:
                        state_events+=1
                        if state_events>1024:raise RuntimeError('DEBUGGER_STATE_EVENT_LIMIT')
                        state=lldb.SBProcess.GetStateFromEvent(event)
                        restarted=lldb.SBProcess.GetRestartedFromEvent(event)
                        restart_pending=state==lldb.eStateStopped and restarted
                        self.emit('PROCESS_STATE',state=state,
                                  restarted=restarted)
                        if state==lldb.eStateRunning:running_consumed=True
                        if continued and not ready and state==lldb.eStateRunning:
                            publish_ready()
            raise RuntimeError('DEBUGGER_EVENT_BATCH_OVERFLOW')
        def observe_stop():
            frames=[]
            signals=[]
            breakpoint_stop=False
            for index in range(min(self.process.GetNumThreads(),16)):
                thread=self.process.GetThreadAtIndex(index)
                if thread.GetStopReason()==lldb.eStopReasonBreakpoint:
                    count=thread.GetStopReasonDataCount()
                    if count<2 or count%2:raise RuntimeError('BREAKPOINT_STOP_DATA_INVALID')
                    for point in range(0,count,2):
                        identity=thread.GetStopReasonDataAtIndex(point)
                        if identity not in self.breakpoints:raise RuntimeError('UNOWNED_BREAKPOINT_STOP')
                        bp=self.target.FindBreakpointByID(identity)
                        location=bp.FindLocationByID(thread.GetStopReasonDataAtIndex(point+1))
                        if not location.IsValid():raise RuntimeError('BREAKPOINT_LOCATION_INVALID')
                        self.hit(thread.GetFrameAtIndex(0),location)
                        breakpoint_stop=True
                if thread.GetStopReason()==lldb.eStopReasonSignal:
                    signals.append({'thread':thread.GetThreadID(),'number':thread.GetStopReasonDataAtIndex(0) if thread.GetStopReasonDataCount() else None})
                    for frame_index in range(8):
                        frame=thread.GetFrameAtIndex(frame_index)
                        if not frame.IsValid():break
                        frames.append({'thread':thread.GetThreadID(),'pc':frame.GetPC(),'function':frame.GetFunctionName()})
            if breakpoint_stop or signals:
                if signals:
                    self.emit('SIGNAL_STOP',frames=frames,signals=signals)
                    if not frames:raise RuntimeError('SIGNAL_FRAMES_UNAVAILABLE')
                    if getattr(self,'signal_seen',False):raise RuntimeError('REPEATED_SIGNAL_STOP')
                    self.signal_seen=True
                # Preserve the original signal delivery; no successful exit
                # is manufactured. A second identical signal is a tool failure.
            else:raise RuntimeError('UNCLASSIFIED_DEBUGGER_STOP')
        drain()
        if self.process.GetState()==lldb.eStateStopped and restart_pending:
            pending_end=min(self.start+self.cfg['seconds'],time.monotonic()+5)
            while self.process.GetState()==lldb.eStateStopped and restart_pending and time.monotonic()<pending_end:
                drain();time.sleep(.02)
            if self.process.GetState()==lldb.eStateStopped and restart_pending:raise RuntimeError('INITIAL_RESTART_UNCONFIRMED')
        initial=self.process.GetState()
        if initial==lldb.eStateRunning:
            if not running_consumed:raise RuntimeError('INITIAL_RUNNING_EVENT_MISSING')
            self.emit('INITIAL_RESUME_DECISION',state=initial,entered=False,reason='ALREADY_RUNNING_AFTER_SETUP',scope='DEBUGGER_CONTROL_ONLY')
            continued=True;publish_ready()
        elif initial==lldb.eStateStopped:
            observe_stop()
            self.emit('INITIAL_RESUME_DECISION',state=initial,entered=True,reason='STOPPED_AFTER_SETUP',scope='DEBUGGER_CONTROL_ONLY')
            error=self.process.Continue()
            if error.Fail():raise RuntimeError('INITIAL_CONTINUE_FAILED:'+str(error))
            continued=True
        else:raise RuntimeError('INITIAL_PROCESS_STATE_NOT_RESUMABLE:'+str(initial))
        while self.process.GetState() not in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):
            drain()
            state=self.process.GetState()
            if state in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):break
            if time.monotonic()-self.start>=self.cfg['seconds']:
                self.emit('OBSERVATION_LIMIT',hit_limit=self.cfg['hit_limit'],seconds=self.cfg['seconds'],reason='ELAPSED_WITH_OR_WITHOUT_HITS')
                if self.process.GetState()!=lldb.eStateStopped:
                    stopped=self.process.Stop()
                    if stopped.Fail():raise RuntimeError('DEBUGGER_DEADLINE_STOP_UNCONFIRMED:'+str(stopped))
                    end=time.monotonic()+2
                    while self.process.GetState()!=lldb.eStateStopped and time.monotonic()<end:
                        drain();time.sleep(.02)
                if self.process.GetState()!=lldb.eStateStopped:raise RuntimeError('DEBUGGER_DEADLINE_STATE_UNCONFIRMED')
                break
            if self.process.GetState()==lldb.eStateStopped:
                observe_stop()
                error=self.process.Continue()
                if error.Fail():
                    self.emit('CONTINUE_FAILED',message=str(error))
                    raise RuntimeError('CONTINUE_FAILED:'+str(error))
            time.sleep(.02)
        if not ready:raise RuntimeError('INITIAL_RUNNING_NOT_OBSERVED')
        if self.process.GetState()==lldb.eStateStopped:
            # Debugger detachment resumes OS execution; it never detaches a
            # native worker or declares SDK cancellation/normal cleanup.
            # API23's shipped Python binding exposes only Detach(). LLVM15's
            # no-argument API delegates with keep_stopped=false (resume).
            error=self.process.Detach()
            self.emit('DEBUGGER_DETACH',success=error.Success(),error=str(error),scope='DEBUGGER_SESSION_ONLY')
            if error.Fail():raise RuntimeError('DEBUGGER_DETACH_FAILED:'+str(error))
        self.emit('DEBUGGER_END',process_state=self.process.GetState(),exit_status=self.process.GetExitStatus())
        (self.output/'END.json').write_text(json.dumps({'run':self.cfg['run'],'side':self.cfg['side'],'pid':self.cfg['pid'],
            'state':self.process.GetState(),'events':self.seq,'scope':'BOUNDED_ORIGINAL_INSTRUCTION_OBSERVATION_NOT_SDK_QUIESCENCE'}))
        self.stream.close()

def run_config(config_path):
    global SESSION
    cfg=json.loads(Path(config_path).read_text())
    if cfg['side'] not in ('A','B') or len(cfg['run'])!=16 or not isinstance(cfg['pid'],int) or cfg['pid']<1:
        raise ValueError('Debug identity invalid')
    if not 1<=cfg['hit_limit']<=256 or not 1<=cfg['seconds']<=300:raise ValueError('Debug budget invalid')
    SESSION=Trace.__new__(Trace)
    try:
        SESSION.__init__(cfg);SESSION.run()
    except Exception as error:
        process=getattr(SESSION,'process',None)
        detached=False
        if process and process.IsValid() and process.GetProcessID()==cfg['pid'] and process.GetState() not in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):
            outcome=process.Detach();detached=outcome.Success()
        output=Path(cfg['output']);output.mkdir(exist_ok=True)
        (output/'FAILED.json').write_text(json.dumps({'error':str(error),'debugger_detached':detached,'pid':cfg['pid']}))
        raise

def detach_config(config_path):
    cfg=json.loads(Path(config_path).read_text());debugger=lldb.SBDebugger.Create();debugger.SetAsync(False)
    target=debugger.CreateTarget(cfg['main_local']);result=lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand('gdb-remote 127.0.0.1:'+str(cfg['host_port']),result)
    process=target.GetProcess();valid=result.Succeeded() and process.IsValid() and process.GetProcessID()==cfg['pid']
    success=False;error='IDENTITY_OR_CONNECT_NOT_CONFIRMED'
    if valid:outcome=process.Detach();success=outcome.Success();error=str(outcome)
    Path(cfg['detach_result']).write_text(json.dumps({'run':cfg['run'],'side':cfg['side'],'pid':cfg['pid'],'debugger_detached':success,'error':error}))
    if not success:raise RuntimeError(error)
