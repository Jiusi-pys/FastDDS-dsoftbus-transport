"""D04 fixed-path diagnosis. Importing builds no process and reads no evidence.

Wire bytes are private even when hex encoded. Only public projections leave
the streaming decoder. No remote cancellation or identity API is included.
"""
import re
import time

SCOPE='ONE_NEW_D04_FIXED_AB_READONLY_EVIDENCE_60S_D01'
NONCE='E31B704C_D04_READONLY_60S_D01'
CASES=('server','read.B','read.A')
ROOT='/data/local/tmp/mdds_direct_20261103_d5a29f38/data'
CAPS={'failure':1024,'result':1024,'exit':1024,'stage':1024,'P0':4096,'status':8192,'P1':4096}
SOURCE_CAP=25600


class EvidenceUnknown(Exception):
    """Fixed reason only; never wrap a private input or exception message."""


def require(ok,reason):
    if not ok:raise EvidenceUnknown(reason)


def number(text,lo=0,hi=18446744073709551615):
    require(type(text) is str and re.fullmatch(r'0|-?[1-9][0-9]{0,19}',text) is not None,'NUMBER_UNKNOWN')
    n=int(text);require(lo<=n<=hi,'NUMBER_RANGE');return n


def typed(label,data,expected):
    """Exact producer grammars. Identity values are comparison-only locals."""
    require(data and len(data)<=1024 and data.endswith(b'\n') and b'\r' not in data,'RECORD_INCOMPLETE')
    try:text=data.decode('ascii')
    except UnicodeError:raise EvidenceUnknown('RECORD_ENCODING') from None
    rows=text[:-1].split('\n');fields={}
    for row in rows:
        require(row.count('=')==1,'RECORD_GRAMMAR');k,v=row.split('=')
        require(k not in fields,'RECORD_DUPLICATE');fields[k]=v
    diag=('phase','native_rc_valid','native_rc')
    shapes={
      'failure':('format',*diag,'origin_bound','restored_entry','restore_rc_valid','restore_rc','SDK_or_data_success'),
      'stage':diag,
      'exit':('format','run','pid','status','restored_entry','restore_rc_valid','restore_rc','worker_parent_restoration','SDK_quiet',*diag),
      'result':('format','status','result_status','phase','writes','takes','local_control_drained','owners_retained','SDK_quiescence_unproven','destroy_rc','primary_phase','native_rc_valid','native_rc')}
    require(tuple(fields)==shapes[label],'RECORD_FIELDS')
    formats={'failure':'S1GOALFAIL1','exit':'S1GOALEXIT1','result':'S1GOALRESULT1'}
    if label in formats:require(fields['format']==formats[label],'RECORD_FORMAT')
    result={'record':'VALID','association':'FIXED_PATH_ASSOCIATED_ONLY'}
    flags={'native_rc_valid','origin_bound','restored_entry','restore_rc_valid','local_control_drained','owners_retained','SDK_quiescence_unproven'}
    errors={'native_rc','restore_rc','status','result_status','destroy_rc'}
    for key,value in fields.items():
        if key in flags:result[key]=bool(number(value,0,1))
        elif key in errors:result[key]=number(value,-2147483648,2147483647)
        elif key in ('phase','primary_phase'):result[key]=number(value,0,4 if label=='result' and key=='phase' else 22)
        elif key in ('writes','takes'):
            n=number(value,0,4294967295)
            result[key]='ZERO' if n==0 else 'ONE_TO_TWO' if n<3 else 'THREE' if n==3 else 'OTHER_VALID'
    if label=='failure':require(fields['SDK_or_data_success']=='0','SUCCESS_CONTRADICTION')
    if label=='exit':
        result['run_matches']=number(fields['run'])==expected['run']
        result['pid_matches']=number(fields['pid'])==expected['pid']
        require(result['run_matches'] and result['pid_matches'],'EXIT_ASSOCIATION_MISMATCH')
        require(fields['worker_parent_restoration']==fields['SDK_quiet']=='UNKNOWN','QUIET_CONTRACT_UNKNOWN')
        result.update(association='RUN_PID_MATCH_PATH_ASSOCIATION',worker_parent_restoration='UNKNOWN',SDK_quiet='UNKNOWN')
    for valid,code in [('native_rc_valid','native_rc'),('restore_rc_valid','restore_rc')]:
        if valid in result and code in result:require(result[valid] or result[code]==0,'INVALID_RC_VALIDITY')
    return result


def proc_stat(data,expected):
    require(0<len(data)<4096 and data.endswith(b'\n') and b'\x00' not in data,'PROC_STAT_BOUNDARY_UNKNOWN')
    end=data.rfind(b') ');start=data.find(b' (')
    require(start>0 and end>start,'PROC_STAT_GRAMMAR')
    try:pid=number(data[:start].decode('ascii'));tail=data[end+2:-1].decode('ascii').split(' ')
    except UnicodeError:raise EvidenceUnknown('PROC_STAT_GRAMMAR') from None
    require(len(tail)>=22 and len(tail)<=64 and re.fullmatch('[RSDZTWtXxIKP]',tail[0]) is not None,'PROC_STAT_GRAMMAR')
    for v in tail[1:]:number(v,-9223372036854775808,18446744073709551615)
    birth=number(tail[19]);parent=number(tail[1],0,4294967295)
    require(pid==expected['pid'] and birth==expected['birth'],'PID_REUSED_OR_CHANGED')
    return {'state':'ZOMBIE' if tail[0]=='Z' else 'DEAD_SNAPSHOT' if tail[0] in ('X','x') else 'RUNNING_SNAPSHOT',
      'parent_matches':parent==expected['parent']}


def proc_status(data,expected):
    require(0<len(data)<8192 and data.endswith(b'\n') and b'\x00' not in data,'PROC_STATUS_BOUNDARY_UNKNOWN')
    selected={}
    for line in data.split(b'\n')[:-1]:
        require(b':' in line,'PROC_STATUS_GRAMMAR');k,v=line.split(b':',1)
        if k in (b'Pid',b'PPid',b'Uid',b'Gid'):
            require(k not in selected,'PROC_STATUS_DUPLICATE')
            try:selected[k]=[number(x.decode('ascii'),0,4294967295) for x in v.split()]
            except UnicodeError:raise EvidenceUnknown('PROC_STATUS_GRAMMAR') from None
    require(set(selected)=={b'Pid',b'PPid',b'Uid',b'Gid'} and len(selected[b'Pid'])==len(selected[b'PPid'])==1
      and len(selected[b'Uid'])==len(selected[b'Gid'])==4,'PROC_STATUS_FIELDS')
    require(selected[b'Pid'][0]==expected['pid'],'PROC_STATUS_PID_CHANGED')
    owner=(selected[b'Uid'][:2]==[expected['uid'],expected['euid']]
      and selected[b'Gid'][:2]==[expected['gid'],expected['egid']])
    require(owner,'OWNER_MISMATCH')
    return {'owner_matches':True,'parent_matches':selected[b'PPid'][0]==expected['parent']}


class Projection:
    """One bounded stream; no raw or field digest is emitted or persisted."""
    def __init__(self,side,expected):
        require(side in ('A','B'),'SIDE');self.side=side;self.expected=expected
        self.labels=(('P0',) if side=='B' else ())+('failure','result','exit','stage')+(('status','P1') if side=='B' else ())
        self.pending=bytearray();self.payload=bytearray();self.total=0;self.source_bytes=0;self.index=0
        self.current=None;self.begun=False;self.done=False;self.failed=False;self.reason=None;self.rows={};self.eol=None

    def reject(self,reason):
        self.failed=True;self.reason=reason;self.erase();raise EvidenceUnknown(reason)

    def feed(self,data):
        try:
            require(not self.failed and not self.done and type(data) is bytes and 0<len(data)<=4096,'STREAM_STATE')
            self.total+=len(data);require(self.total<=65536,'CLIENT_ENVELOPE_CAP')
            self.pending.extend(data)
            while b'\n' in self.pending:
                i=self.pending.index(10);raw=bytes(self.pending[:i]);del self.pending[:i+1]
                ending=2 if raw.endswith(b'\r\r') else 1 if raw.endswith(b'\r') else 0
                if self.eol is None:self.eol=ending
                require(self.eol==ending and len(raw)<=256,'LINE_FORMAT')
                self.line(raw[:-ending] if ending else raw)
            require(len(self.pending)<=256,'LINE_BOUND')
        except EvidenceUnknown as e:self.reject(str(e))

    def line(self,line):
        require(not self.done,'TRAILING_DATA')
        if not self.begun:
            require(line==('S1RO_BEGIN '+self.side).encode(),'QUERY_BEGIN');self.begun=True;return
        if self.current:
            if line.startswith(b'S1RO_END '):
                require(line==('S1RO_END '+self.current[0]+' OK').encode(),'OPEN_FILE_CHANGED_OR_READ_FAILED')
                self.finish_item();return
            require(re.fullmatch(rb' *(?:[0-9a-f]{2} *)+',line) is not None,'HEX_FORMAT')
            self.payload.extend(bytes.fromhex(line.decode('ascii')))
            require(len(self.payload)<=CAPS[self.current[0]]+32,'ITEM_CAP');return
        if line.startswith(b'S1RO_ABORT '):raise EvidenceUnknown('REMOTE_BOUND_OR_IDENTITY_UNKNOWN')
        if line==('S1RO_DONE '+self.side+' 0').encode():
            require(self.index==len(self.labels),'QUERY_INCOMPLETE');self.done=True;return
        m=re.fullmatch(rb'S1RO_ITEM ([A-Za-z0-9]+) (DATA|MISSING|NOT_VISIBLE) ([0-9]{1,5})',line)
        require(m is not None and self.index<len(self.labels),'ITEM_HEADER')
        label=m[1].decode();require(label==self.labels[self.index],'ITEM_ORDER');n=number(m[3].decode(),0,CAPS[label])
        state=m[2].decode()
        if state!='DATA':
            require(n==0 and (state=='MISSING' and label in ('failure','result','exit','stage')
              or state=='NOT_VISIBLE' and label in ('P0','status','P1')),'ITEM_ABSENCE')
            self.rows[label]={'record':state};self.index+=1;return
        require(n>0,'EMPTY_ITEM');self.current=(label,n);self.payload.clear()

    def finish_item(self):
        label,n=self.current;wire=bytes(self.payload);footer=b'\x00MDDS_READ_RC=0\x00'
        require(wire.endswith(footer),'READ_COMMAND_NONZERO_OR_UNKNOWN');data=wire[:-len(footer)]
        require(len(data)==n if label in ('failure','result','exit','stage') else 0<len(data)<CAPS[label],'SOURCE_COUNT_OR_BOUNDARY_UNKNOWN')
        self.source_bytes+=len(data);require(self.source_bytes<=SOURCE_CAP,'SOURCE_TOTAL_CAP')
        if label in ('P0','P1'):row={'record':'VALID',**proc_stat(data,self.expected)}
        elif label=='status':row={'record':'VALID',**proc_status(data,self.expected)}
        else:row=typed(label,data,self.expected)
        row['source_bytes']=len(data);self.rows[label]=row
        self.payload.clear();self.current=None;self.index+=1

    def finish(self):
        require(not self.failed and self.done and not self.pending and self.current is None,'QUERY_EOF_INCOMPLETE')
        process='NOT_CHECKED_A'
        if self.side=='B':
            p,q,s=(self.rows[k] for k in ('P0','P1','status'))
            if p['record']==q['record']==s['record']=='NOT_VISIBLE':process='PID_NOT_VISIBLE_AT_TWO_SNAPSHOTS'
            else:
                require(p['record']==q['record']==s['record']=='VALID','PROCESS_CHANGED_DURING_READ')
                require(p['state']==q['state'],'PROCESS_CHANGED_DURING_READ')
                process={'ZOMBIE':'SAME_STARTTIME_ZOMBIE','DEAD_SNAPSHOT':'SAME_STARTTIME_DEAD_SNAPSHOT',
                  'RUNNING_SNAPSHOT':'SAME_STARTTIME_RUNNING_SNAPSHOT'}[p['state']]
                require(all(x['parent_matches'] for x in (p,q,s)),'PROCESS_PARENT_CHANGED')
        return {'side':self.side,'files':{k:v for k,v in self.rows.items() if k in ('failure','result','exit','stage')},
          'process':process,'source_bytes':self.source_bytes,'exe':'EXE_LINK_NOT_READ_BOUNDED_INTERFACE_UNAVAILABLE',
          'image_bytes':'UNVERIFIED','identity_restored_all_workers':'UNKNOWN','SDK_quiet':'UNKNOWN','remote_descendants':'UNKNOWN'}

    def erase(self):self.pending.clear();self.payload.clear()


def query(side,expected):
    """Fixed POSIX shell; source reads use dd's one-byte input blocks.

    No readlink is invoked: its internal source read has no specified cap.
    Path/open comparisons are sampled consistency, never atomic O_NOFOLLOW.
    """
    require(side in ('A','B'),'SIDE')
    for k in ('pid','birth','parent','uid','euid','gid','egid','run'):number(str(expected[k]))
    text=r'''LC_ALL=C; export LC_ALL; set -f
exec 2>/dev/null
abort() { printf 'S1RO_ABORT BOUND_OR_IDENTITY_UNKNOWN\n'; exit 122; }
for tool in stat dd od; do command -v "$tool" >/dev/null || abort; done
meta() { stat -c '%d|%i|%s|%y|%h|%f' "$1"; }
fdmeta() { stat -L -c '%d|%i|%s|%y|%h|%f' "/proc/$$/fd/3"; }
open_checked() {
 p=$1; test ! -L "$p" && test -f "$p" || abort
 before=$(meta "$p") || abort
 oldIFS=$IFS; IFS='|'; set -- $before; IFS=$oldIFS
 test "$#" -eq 6 || abort; size=$3; links=$5; mode=$6
 case "$size:$links" in *[!0-9:]*|'') abort;; esac
 case "$mode" in *[!0-9a-fA-F]*|'') abort;; esac
 test "$links" = 1 && test "$((0x$mode & 61440))" = 32768 || abort
 exec 3< "$p" || abort
 opened=$(fdmeta) || abort; test "$opened" = "$before" || abort
}
close_checked() {
 after=$(fdmeta) || abort; path_after=$(meta "$p") || abort
 test ! -L "$p" && test "$before" = "$after" && test "$before" = "$path_after" || abort
 exec 3<&- || abort
}
read_record() {
 label=$1; file=$2
 if test ! -e "$file" && test ! -L "$file"; then
  test -d "$root" && test -x "$root" || abort
  missing=$(stat -c '%i' "$file" 2>&1); missing_rc=$?
  test "$missing_rc" -ne 0 && test -d "$root" && test -x "$root" || abort
  case "$missing" in *': No such file or directory'|*': no such file or directory')
   printf 'S1RO_ITEM %s MISSING 0\n' "$label";; *) abort;; esac
  unset missing; return
 fi
 open_checked "$file"; test "$size" -gt 0 && test "$size" -le 1024 || abort
 printf 'S1RO_ITEM %s DATA %s\n' "$label" "$size"
 { dd bs=1 count="$size" <&3 2>/dev/null; rc=$?; printf '\000MDDS_READ_RC=%s\000' "$rc"; } | od -An -v -tx1 || abort
 close_checked; printf 'S1RO_END %s OK\n' "$label"
}
proc_snapshot() {
 label=$1
 if test ! -d "$proc"; then printf 'S1RO_ITEM %s NOT_VISIBLE 0\n' "$label"; visible=0; return; fi
 open_checked "$proc/stat"
 marker=$(printf '\001')
 whole=$(dd bs=1 count=4096 <&3 2>/dev/null; printf '\001%s_' "$?")
 rc=${whole##*"$marker"}; rc=${rc%_}; raw=${whole%"$marker"*}
 test "$rc" = 0 && test "${#raw}" -gt 0 && test "${#raw}" -lt 4096 || abort
 close_checked
 printf 'S1RO_ITEM %s DATA %s\n' "$label" "${#raw}"
 { printf '%s' "$raw"; printf '\000MDDS_READ_RC=0\000'; } | od -An -v -tx1 || abort
 printf 'S1RO_END %s OK\n' "$label"
 test "${raw%% *}" = "$expected_pid" || abort
 tail=${raw##*) }; test "$tail" != "$raw" || abort
 set -- $tail; test "$#" -ge 22 && test "$#" -le 64 || abort
 shift 19; test "$1" = "$expected_birth" || abort
 visible=1; unset whole raw tail
}
read_status() {
 if test "$visible" = 0; then printf 'S1RO_ITEM status NOT_VISIBLE 0\n'; return; fi
 open_checked "$proc/status"
 printf 'S1RO_ITEM status DATA 8192\n'
 { dd bs=1 count=8192 <&3 2>/dev/null; rc=$?; printf '\000MDDS_READ_RC=%s\000' "$rc"; } | od -An -v -tx1 || abort
 close_checked; printf 'S1RO_END status OK\n'
}
'''
    text+="root='"+ROOT+"'\n"
    text+="for part in /data /data/local /data/local/tmp '${root%/data}' \"$root\"; do test -d \"$part\" && test ! -L \"$part\" || abort; done\n".replace("'${root%/data}'",'"${root%/data}"')
    text+="printf 'S1RO_BEGIN "+side+"\\n'\n"
    if side=='B':
        text+="proc='/proc/"+str(expected['pid'])+"'; expected_pid="+str(expected['pid'])+"; expected_birth="+str(expected['birth'])+"\nproc_snapshot P0\n"
    for label,name in [('failure','failure'),('result','data-result'),('exit','exit'),('stage','stage')]:
        text+='read_record '+label+' "$root/'+name+'.'+side+'"\n'
    if side=='B':text+='read_status\nproc_snapshot P1\n'
    return text+"printf 'S1RO_DONE "+side+" 0\\n'; exit 0\n"


class MemoryCapture:
    __slots__=()
    def write(self,data):return len(data)
    def flush(self):pass
    def close(self):pass


def telemetry(event):
    return (event.get('case')=='server' and event.get('stream')=='stdout'
      and event.get('phase') in ('MONITOR','CLEANUP') and event.get('event_type')=='UNKNOWN'
      and event.get('rule_id')=='AUTH_CONTEXT_UNKNOWN' and event.get('trigger_family')=='AUTH_WORD_ONLY'
      and event.get('complete_record') is True)


def make_types(driver_module,backend_module,qualified):
    need=driver_module.need
    class ReadonlyBackend(backend_module.GoalWindowsBackend):
        def __init__(self,grant):
            need(getattr(grant,'fixed_readonly_evidence',False) is True and grant.server_unknown_continue is False,'READONLY_EFFECT_REQUIRED')
            super().__init__(grant);self.parsers={};self.observations={}
        def register(self,step):
            from goal_read_context import EXPECTED
            root=super().register(step)
            if step.id!='server':self.parsers[step.id]=Projection(step.side,EXPECTED[step.side])
            return root
        def notice_gate(self):
            report=self.marker.report();events=report.get('events')
            need(report.get('schema')=='NOTICE_EVIDENCE_V2' and report.get('record_failed') is False
              and report.get('overflow') is False and report.get('failure_rules')==[]
              and report.get('emergency_events')==[] and type(events) is list,'READONLY_NOTICE_UNRELIABLE')
            for e in events:
                need(e.get('schema')=='NOTICE_EVENT_V2' and e.get('case') in CASES
                  and e.get('complete_record') is True,'READONLY_EVENT_BINDING')
                need(telemetry(e) or (e.get('event_type')=='DIAGNOSTIC' and e.get('rule_id') in ('DIAG_FIELD','DIAG_PATH')
                  and e.get('trigger_family')=='NOT_APPLICABLE'),'READONLY_AUTH_OR_UNKNOWN')
        def observe_chunk(self,root,stream,data,before,after,phase):
            super().observe_chunk(root,stream,data,before,after,phase)
            if root.case in self.parsers:
                parser=self.parsers[root.case];old=set(parser.rows)
                try:
                    require(stream=='stdout','CLIENT_STDERR_UNKNOWN');parser.feed(data)
                except EvidenceUnknown as error:
                    self.journal.emit({'readonly_rejection':{'step':root.case,'reason':str(error)}})
                    raise driver_module.ContractError('READONLY_EVIDENCE_REJECTED') from None
                for label in parser.rows.keys()-old:
                    self.journal.emit({'readonly_observation':{'step':root.case,'item':label,**parser.rows[label]}})
        def outcome(self,root,step):
            if step.id=='server':return super().outcome(root,step)
            if root.state()!='TERMINAL' or root.receipt.get('EOF') is not True:return None
            for stream in ('stdout','stderr'):self.marker.eof(root.case,stream)
            self.marker.advance();root.identity();self.notice_gate()
            need(root.receipt.get('native_exit')==0 and root.receipt.get('stop_attempts')==0
              and qualified(root.receipt) and not root.receipt.get('cleanup_errors'),'READONLY_TERMINAL_UNKNOWN')
            parsed=self.parsers[step.id].finish();need(self.server_ok(),'READONLY_SERVER_CHANGED')
            self.grant.mark_terminal(step.id,root.receipt);root.close()
            need(qualified(root.receipt) and not root.receipt.get('cleanup_errors') and not self.journal.failed,'READONLY_CLOSE_UNKNOWN')
            root.receipt.update(accepted_evidence=False,step_acceptance='PENDING',readonly_projection=parsed)
            self.w.put(root.dest/'RESULT.json',root.receipt)
            need(self.server_ok() and not self.journal.failed,'READONLY_FINAL_SERVER_OR_RECORD')
            need(time.monotonic()<min(self.grant.work_deadline,root.identity_deadline),'READONLY_ACCEPT_DEADLINE')
            root.receipt.update(accepted_evidence=True,step_acceptance='ACCEPTED')
            try:self.grant.accept(step.id,root.receipt)
            except BaseException:root.receipt.update(accepted_evidence=False,step_acceptance='REJECTED');raise
            self.observations[step.id]=parsed
            return {'step':step.id,'accepted':True,'terminal':True,'readonly_projection':parsed}
        def cleanup(self,root,deadline):
            try:return super().cleanup(root,deadline)
            finally:
                if root.case in self.parsers:self.parsers[root.case].erase()
                for b in root.output:b.clear()
    class ReadonlyDriver(driver_module.GoalDriver):
        def run_readonly(self):
            self.start('server');self.wait_accepted('server')
            for name in ('read.B','read.A'):self.start(name);self.wait(name)
            return self.summary()
        def summary(self):
            return {'observations':dict(self.backend.observations),'DDS':'NOT_RUN',
              'remote_signals':0,'identity_actions':0,'exe_link':'NOT_READ','remote_shell_termination':'UNKNOWN'}
    return ReadonlyDriver,ReadonlyBackend


def actual_driver(grant):
    import goal_authorization as a
    import goal_contract as c
    c.need(grant in a._grants and c.SCOPE==SCOPE and c.NONCE==NONCE
      and getattr(grant,'fixed_readonly_evidence',False) is True,'NEW_READONLY_GRANT_REQUIRED')
    c.validate_registry(list(grant.registry.values()))
    import goal_driver,goal_windows_backend
    from goal_handle_lifecycle import terminal_receipt_qualified
    D,B=make_types(goal_driver,goal_windows_backend,terminal_receipt_qualified)
    return D(grant.registry,B(grant))


def observation_complete(driver,cleanup):
    try:
        b=driver.backend;b.notice_gate()
        if set(driver.used)!=set(CASES) or set(driver.accepted)!=set(CASES) or set(b.observations)!={'read.B','read.A'}:return False
        if sum(v['source_bytes'] for v in b.observations.values())>SOURCE_CAP:return False
        if len(b.roots)!=3 or {r.case for r in b.roots}!=set(CASES):return False
        from goal_handle_lifecycle import terminal_receipt_qualified
        for root in b.roots:
            if not root.closed or not terminal_receipt_qualified(root.receipt) or root.receipt.get('cleanup_errors'):return False
            if root.case!='server' and (root.receipt.get('native_exit')!=0 or root.receipt.get('stop_attempts')!=0):return False
        blocked=any(telemetry(e) for e in b.marker.report()['events'])
        f=cleanup['finalization'];rows=cleanup['cleanup']
        return (len(rows)==1 and rows[0].get('step')=='server' and rows[0].get('state')=='TERMINAL'
          and rows[0].get('EOF') is True and rows[0].get('cleanup_errors')==[]
          and f.get('state')=='COMPLETE' and f.get('cleanup_errors')==[]
          and f.get('business_blocked') is blocked
          and cleanup['failure']==('CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN' if blocked else None)
          and not b.journal.failed)
    except BaseException:return False
