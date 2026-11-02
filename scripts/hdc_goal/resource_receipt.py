"""Pure receipt framing and fixed resource proof; no file/device access.

HDC may merge remote stderr into its stdout. Accept only the exact expected
root-ENOENT record, never discard arbitrary diagnostics to make a receipt fit.
"""
import re


def receipt_text(data, need):
    need(type(data) is bytes and len(data)<=65536,'STEP_STDOUT_BOUND')
    if not data:return ''
    need(data.endswith(b'\n'),'RECORD_FRAME')
    lines=[]
    for line in data.split(b'\n')[:-1]:
        if line.endswith(b'\r\r'):line=line[:-2]
        elif line.endswith(b'\r'):line=line[:-1]
        need(b'\r' not in line and b'\x00' not in line,'RECORD_FRAME')
        text=line.decode('utf-8','strict')
        need(all(ch=='\t' or (ord(ch)>=32 and not 127<=ord(ch)<=159
                              and ch not in ('\u2028','\u2029')) for ch in text),'RECORD_FRAME')
        lines.append(text)
    return '\n'.join(lines)+'\n'


def marker_state(lines, name):
    """Fixed diagnostic enum only; does not return arbitrary values or text."""
    tokens=sum(line.count(name+'=') for line in lines)
    matches=[re.fullmatch(re.escape(name)+r'=(0|[1-9][0-9]{0,2})',line) for line in lines]
    values=[match[1] for match in matches if match]
    if tokens==0:return 'MISSING'
    if tokens!=1:return 'DUPLICATE_OR_CONFLICTING'
    if len(values)!=1 or int(values[0])>255:return 'MALFORMED'
    return 'ZERO' if values[0]=='0' else 'NONZERO'


def parse_resource(stdout, stderr, native_exit, root, limits, need):
    need(native_exit==0,'CLIENT_NATIVE_NONZERO')
    text=receipt_text(stdout,need);error=receipt_text(stderr,need)
    need(not any(word in (text+'\n'+error).lower() for word in
                 ('permission denied','operation not permitted','unauthorized')),'REMOTE_DENIED')
    lines=[line for line in text.split('\n') if line]
    errors=[line for line in error.split('\n') if line]
    base=marker_state(lines,'__RO_BASE_RC')
    need(base!='DUPLICATE_OR_CONFLICTING','REMOTE_DUPLICATE_MARKER')
    need(base=='ZERO','REMOTE_BASE_RC_MISSING_OR_NONZERO')
    state=marker_state(lines,'__RO_ROOT_STAT_RC')
    need(state in ('ZERO','NONZERO'),'ROOT_STAT_MARKER')
    need(state!='ZERO','TEST_ROOT_ALREADY_EXISTS')
    # These exact path-bound public diagnostics are the sole permitted stderr.
    # Separate host pipes do not reconstruct remote fds merged by HDC Popen.
    absent={f'stat: {prefix}{quote}{root}{quote}: No such file or directory'
            for prefix in ('','cannot stat ') for quote in ('',"'",'"')}
    merged=[n for n,line in enumerate(lines) if line in absent]
    if errors:
        need(len(errors)==1 and errors[0] in absent and not merged,
             'ROOT_ABSENCE_NOT_PROVED_ENOENT')
    else:
        # Base marker precedes the attempted stat. Its one error must appear
        # exactly between the base marker and the root-result marker.
        need(len(merged)==1 and merged[0]==9 and len(lines)==11,
             'ROOT_ABSENCE_NOT_PROVED_ENOENT')
        lines=lines[:9]+lines[10:]
    need(len(lines)==10,'RESOURCE_FULL_RECORD_COUNT')
    total=re.fullmatch(r'MemTotal:\s+(\d+)\s+kB',lines[0])
    available=re.fullmatch(r'MemAvailable:\s+(\d+)\s+kB',lines[1])
    need(total is not None and available is not None
         and 0<int(total[1])>=int(available[1]),'UNIQUE_ORDERED_MEMORY_FIELDS')
    need(int(total[1])>=limits['board_total_kib'] and int(available[1])>=limits['board_available_kib'],'BOARD_MEMORY_MARGIN')
    need(re.fullmatch(r'Filesystem\s+1K-blocks\s+Used\s+Available\s+Use%\s+Mounted on',lines[2]) is not None,'DF_HEADER_UNKNOWN')
    disk=re.fullmatch(r'\S+\s+(\d+)\s+(\d+)\s+(\d+)\s+\d+%\s+/data(?:/local/tmp)?',lines[3])
    need(disk is not None,'DF_RECORD_UNKNOWN')
    need(int(disk[3])>=limits['board_disk_kib'],'BOARD_DISK_MARGIN')
    need(lines[4]=='/data/local/tmp','RESOLVED_ROOT_UNKNOWN')
    need(all(re.fullmatch(r'directory\|[0-7]+\|\d+\|\d+',line) for line in lines[5:8]),'PARENT_PATH_METADATA_UNKNOWN')
    final=re.fullmatch(r'__RO_ROOT_STAT_RC=([1-9][0-9]{0,2})',lines[9])
    need(lines[8]=='__RO_BASE_RC=0' and final is not None and int(final[1])<=255,'ORDERED_RESOURCE_SENTINELS')
    return {'remote_base_RC':0,'root_stat_RC':int(final[1]),'root_absence':'ENOENT_WITH_BASE_ACCESS',
            'MemTotal_KiB':int(total[1]),'MemAvailable_KiB':int(available[1]),
            'data_available_KiB':int(disk[3]),'resource_scope':'one fresh query; not a hard reservation'}
