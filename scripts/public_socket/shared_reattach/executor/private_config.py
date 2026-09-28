"""Explicit local identity inputs; contains no authorization or device driver."""
from pathlib import Path
import re
def validate_profile(x,task,repo):
 task=Path(task).resolve();repo=Path(repo).resolve()
 if not isinstance(x,dict):raise ValueError('Private configuration object required')
 if x.get('schema')!=1 or Path(x.get('task_root','')).resolve()!=task or Path(x.get('repo_root','')).resolve()!=repo:raise ValueError('Explicit task/repository identity required')
 if set(x.get('targets',{}))!={'A','B'} or len(set(x['targets'].values()))!=2 or not all(isinstance(v,str) and re.fullmatch(r'[A-Za-z0-9_.:-]{1,128}',v) for v in x['targets'].values()):raise ValueError('Two distinct explicit board identities required')
 if set(x.get('tokens',{}))!={'A','B'} or not all(isinstance(v,str) and re.fullmatch(r'[1-9][0-9]*',v) for v in x['tokens'].values()):raise ValueError('Explicit numeric token identities required')
 if set(x.get('permission_baseline_sha256',{}))!={'A','B'} or not all(re.fullmatch('[0-9a-f]{64}',v) for v in x['permission_baseline_sha256'].values()):raise ValueError('Explicit permission baseline hashes required')
 if not all(isinstance(x.get(k),str) and Path(x[k]).is_absolute() for k in ('hdc_path','query_helper_path')):raise ValueError('Explicit absolute tool/helper paths required; no PATH fallback')
 return x
