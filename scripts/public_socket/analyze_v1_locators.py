"""Independently compare observed native SPDP/selection with private-derived keys."""
from pathlib import Path
import argparse,hashlib,json,re

def need(value,why):
 if not value:raise ValueError(why)
def derive(identity,slot=0):
 # Independent Python byte-level implementation; never invokes project codec.
 ns=identity['namespace'].encode('utf-8');uid=identity['udid'].encode('utf-8');domain=identity['domain']
 need(1<=len(ns)<=116 and 1<=len(uid)<=64 and b'\0' not in ns+uid and type(domain)is int and 0<=domain<=0xffffffff and type(slot)is int and 0<=slot<=0xfffffe,'Canonical private input invalid')
 raw=b'MDDS-V1-DEVICE\0'+len(ns).to_bytes(2,'big')+ns+domain.to_bytes(4,'big')+len(uid).to_bytes(2,'big')+uid
 return dict(kind='40000000',address=hashlib.sha256(raw).digest()[:16].hex(),port=f'{0x01000000|(slot+1):08x}')
def fields(line,prefix):
 tokens=line[len(prefix):].strip().split();pairs=[token.split('=',1) for token in tokens]
 need(all(len(p)==2 for p in pairs) and len({p[0] for p in pairs})==len(pairs),'Malformed/duplicate observation fields')
 return dict(pairs)
def analyze(window,expected):
 window=Path(window);cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'));run=cfg['run'];result={}
 for side,other in [('A','B'),('B','A')]:
  data=(window/(side+'.raw.stdout.log')).read_bytes();text=data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
  groups={};selected=[];summary=[]
  for line in text.splitlines():
   if line.startswith('ROS_V1_SPDP '):
    f=fields(line,'ROS_V1_SPDP ');need(f.get('run')==run and f.get('side')==side and f.get('bytes')=='24' and f.get('pid') in ('0031','0032'),'Observed SPDP identity/layout mismatch')
    need(f.get('direction') in ('SDK_SEND','SDK_INGRESS') and re.fullmatch('[0-9a-f]{24}000100c2',f.get('writer','')),'Original SPDP writer/direction mismatch')
    wanted=expected[side if f['direction']=='SDK_SEND' else other]
    need({k:f.get(k) for k in ('kind','address','port')}==wanted,'Actual advertised/observed complete locator mismatch')
    for key in ('observation','sequence_high','sequence_low','data_offset'):need(re.fullmatch('[0-9]+',f.get(key,'')),'Source DATA association invalid')
    group=tuple(f[k] for k in ('direction','observation','socket','writer','sequence_high','sequence_low','data_offset'));groups.setdefault(group,[]).append(f)
   elif line.startswith('ROS_V1_SELECTED '):
    f=fields(line,'ROS_V1_SELECTED ');need(f.get('run')==run and f.get('side')==side and f.get('scope')=='ACTUAL_SELECTOR_VALUES','Selection source identity mismatch')
    need({k:f.get(k) for k in ('kind','address','port')}==expected[other],'Actual full-locator selection mismatch');selected.append(f)
   elif line.startswith('ROS_V1_OBSERVATION '):summary.append(fields(line,'ROS_V1_OBSERVATION '))
  need(len(summary)==1 and summary[0].get('run')==run and summary[0].get('side')==side and summary[0].get('captured')=='1' and summary[0].get('faults')=='0','Bounded locator observation incomplete/faulted')
  counts={'SDK_SEND':0,'SDK_INGRESS':0}
  for group,values in groups.items():
   # Every retained duplicate was checked above; only one original DATA group
   # can establish both actual advertised user/meta parameters.
   if {f['pid'] for f in values}=={'0031','0032'}:counts[group[0]]+=1
  need(all(counts.values()) and bool(selected),'Actual same-DATA meta/user announcement, peer observation or selection missing')
  need(int(summary[0]['adverts'])==sum(len(v) for v in groups.values()) and int(summary[0]['selected'])==len(selected),'Locator observation count mismatch')
  result[side]=dict(complete_spdp_groups=counts,selected=len(selected),source_stdout_sha256=hashlib.sha256(data).hexdigest())
 return dict(scope='ACTUAL_V1_SPDP_AND_FULL_SELECTOR_ONLY_NOT_ROS_BUSINESS_OR_QUALIFICATION',run=run,**{'pass':True},sides=result)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--window',required=True);p.add_argument('--private-profile',required=True);p.add_argument('--output',required=True);a=p.parse_args()
 x=json.loads(Path(a.private_profile).read_text(encoding='utf-8-sig'))['v1_deployment'];expected={side:derive(dict(namespace=x['namespace'],domain=x['domain'],udid=x['udids'][side]),x['slots'][side]) for side in 'AB'}
 out=analyze(a.window,expected)
 with Path(a.output).open('x',encoding='utf-8') as f:json.dump(out,f,indent=2);f.write('\n')
 print(json.dumps(dict(scope=out['scope'],**{'pass':out['pass']})))
