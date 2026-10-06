"""Explicit local identity inputs; contains no authorization or device driver."""
from pathlib import Path
import re,hashlib,json

def validate_v1_deployment(x,repo):
 v=x.get('v1_deployment');repo=Path(repo).resolve()
 if v is None:return None
 if not isinstance(v,dict) or set(v)!={'namespace','domain','profile','slots','udids','udid_receipts','initiator_names','initiator_receipt'}:raise ValueError('Explicit complete V1 deployment required')
 if not isinstance(v['namespace'],str) or not 1<=len(v['namespace'].encode('utf-8'))<=116 or any(c in v['namespace'] for c in '\0\r\n'):raise ValueError('V1 logical namespace bytes invalid')
 if type(v['domain']) is not int or v['domain']!=201 or v['profile']!='BYTES_NATIVE':raise ValueError('This ROS device contract supports domain201 native profile1 only')
 if set(v['slots'])!={'A','B'} or any(type(n)is not int or n!=0 for n in v['slots'].values()):raise ValueError('Existing one-participant topology uses slot0 only')
 for name in ('udids','udid_receipts','initiator_names'):
  if set(v[name])!={'A','B'}:raise ValueError('V1 corresponding endpoint inputs incomplete')
 for side in 'AB':
  value=v['udids'][side]
  if not isinstance(value,str) or not 1<=len(value.encode('utf-8'))<=64 or any(c in value for c in '\0\r\n'):raise ValueError('Exact V1 identity input invalid')
  receipt=v['udid_receipts'][side];path=Path(receipt.get('path','')).resolve()
  if not path.is_relative_to(repo/'.artifacts') or hashlib.sha256(path.read_bytes()).hexdigest()!=receipt.get('sha256') or path.read_bytes()!=value.encode('utf-8'):raise ValueError('V1 UDID differs from controlled raw SDK input')
  name=v['initiator_names'][side]
  prefix=x['ros2_package_name']+'.'+x['ros2_service_prefix']+'.'
  if not isinstance(name,str) or not name.startswith(prefix) or len(name)>255 or not re.fullmatch(r'[A-Za-z0-9_.-]+',name):raise ValueError('Approved concrete initiator input invalid')
 receipt=v['initiator_receipt'];path=Path(receipt.get('path','')).resolve()
 if not path.is_relative_to(repo/'.artifacts') or hashlib.sha256(path.read_bytes()).hexdigest()!=receipt.get('sha256'):raise ValueError('Approved initiator input receipt drift')
 approved=json.loads(path.read_text(encoding='utf-8-sig'))
 if approved.get('names')!=v['initiator_names'] or approved.get('decision')!='TEMPORARILY_RETAIN_EXACT_PREVIOUSLY_APPROVED_NAMES' or approved.get('package_name')!=x['ros2_package_name'] or approved.get('service_stem')!=x['ros2_service_prefix']:raise ValueError('Initiator contract differs from approved concrete inputs')
 return v
def validate_profile(x,task,repo):
 task=Path(task).resolve();repo=Path(repo).resolve()
 if not isinstance(x,dict):raise ValueError('Private configuration object required')
 if x.get('schema')!=1 or Path(x.get('task_root','')).resolve()!=task or Path(x.get('repo_root','')).resolve()!=repo:raise ValueError('Explicit task/repository identity required')
 if set(x.get('targets',{}))!={'A','B'} or len(set(x['targets'].values()))!=2 or not all(isinstance(v,str) and re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}',v) for v in x['targets'].values()):raise ValueError('Two distinct explicit board identities required')
 if set(x.get('tokens',{}))!={'A','B'} or not all(isinstance(v,str) and re.fullmatch(r'[1-9][0-9]*',v) for v in x['tokens'].values()):raise ValueError('Explicit numeric token identities required')
 if set(x.get('permission_baseline_sha256',{}))!={'A','B'} or not all(re.fullmatch('[0-9a-f]{64}',v) for v in x['permission_baseline_sha256'].values()):raise ValueError('Explicit permission baseline hashes required')
 if not all(isinstance(x.get(k),str) and Path(x[k]).is_absolute() for k in ('hdc_path','query_helper_path')):raise ValueError('Explicit absolute tool/helper paths required; no PATH fallback')
 for k in ('installed_service_prefix','installed_package_name','ros2_service_prefix','ros2_package_name'):
  if k in x and (not isinstance(x[k],str) or not re.fullmatch(r'[a-z0-9.]{1,40}' if k.endswith('prefix') else r'[A-Za-z0-9_.]{1,64}',x[k])):raise ValueError('Explicit installed service/package identity required')
 if 'ros2_service_prefix' in x and 'ros2_package_name' in x:
  stem=x['ros2_service_prefix'];package=x['ros2_package_name']
  if stem==package or stem.startswith(package+'.'):raise ValueError('ROS service stem must not repeat authorized package')
 validate_v1_deployment(x,repo)
 return x
