"""Metadata-only exact new-run grammar. No private file content/hash API."""
import os,stat
from pathlib import Path
from goal_contract import LIMITS,need

CASE_FILES={'INTENT.json','IDENTITY.json','RESULT.json','stdout.raw','stderr.raw'}
CONTROL_FILES={'pair','events.A','events.B','cancel','cancel.A','cancel.B','controller-result',
               *{'admission.'+s for s in ['A','B']},
               *{'release.'+s+'.'+str(i) for s in ['A','B'] for i in range(1,5)},
               *{'delivered.'+str(i) for i in range(1,5)}}

def scan(root,cases,previous):
    files=0;total=0;cache=dict(previous);dirs=0
    def enumeration_failed(error):raise RuntimeError('RUN_ENUMERATION_UNKNOWN') from None
    for directory,names,leaves in os.walk(root,followlinks=False,onerror=enumeration_failed):
        relative=Path(directory).relative_to(root).parts;dirs+=1
        need(dirs<=512 and len(relative)<=4,'DIRECTORY_CAP')
        for name in names:
            path=Path(directory)/name;info=path.lstat();parts=path.relative_to(root).parts
            need(stat.S_ISDIR(info.st_mode) and not info.st_file_attributes&0x400,'DIRECTORY_REPARSE')
            valid=parts in [('cases',),('evidence',),('control',),('snapshots',)] or len(parts)==2 and parts[0]=='cases' and parts[1] in cases or len(parts)==3 and parts[0]=='cases' and parts[1] in cases and parts[2]=='tmp' or len(parts)==2 and parts[0]=='control' and parts[1] in ('probe','data')
            need(valid,'UNKNOWN_DIRECTORY')
        for name in leaves:
            path=Path(directory)/name;info=path.lstat();parts=path.relative_to(root).parts
            need(stat.S_ISREG(info.st_mode) and not info.st_file_attributes&0x400 and info.st_nlink==1,'FILE_REPARSE_OR_LINK')
            valid=parts in [('CANCEL.request',),('evidence','API.jsonl'),('evidence','RESULT.json')]
            valid=valid or len(parts)==3 and parts[0]=='cases' and parts[1] in cases and parts[2] in CASE_FILES
            is_cache=len(parts)==4 and parts[0]=='cases' and parts[1] in cases and parts[2]=='tmp' and parts[3]=='.hdc.cache.log'
            valid=valid or is_cache or parts in [('cases','server','tmp','.HDCServer.pid'),('cases','server','tmp','hdc.log')]
            valid=valid or len(parts)==3 and parts[0]=='control' and parts[1] in ('probe','data') and (parts[2] in CONTROL_FILES or parts[2].endswith('.incoming') and parts[2][:-9] in CONTROL_FILES or parts[2].endswith('.writing') and parts[2][:-8] in CONTROL_FILES)
            valid=valid or len(parts)==2 and parts[0]=='snapshots' and parts[1] in {'events.'+s+'.'+str(i) for s in ['A','B'] for i in range(12)}
            need(valid,'UNKNOWN_RUN_FILE');files+=1;total+=info.st_size
            if is_cache:
                key='/'.join(parts);identity=(info.st_dev,info.st_ino);old=cache.get(key)
                need(old is None or old[0]==identity and info.st_size>=old[1],'CACHE_IDENTITY_OR_SHRINK')
                cache[key]=(identity,info.st_size,max(info.st_size,old[2] if old else 0))
    need(files<=LIMITS['files'] and total<=LIMITS['run_bytes']-LIMITS['closing_reserve'],'RUN_CAP')
    need(sum(row[2] for row in cache.values())<=LIMITS['cache_peak'],'CUMULATIVE_CACHE_PEAK')
    return {'files':files,'bytes':total,'cache_state':cache,'content_read_or_hash':False}
