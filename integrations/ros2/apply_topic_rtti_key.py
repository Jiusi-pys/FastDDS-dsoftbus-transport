"""Give the existing TopicDescription destructor one 2.14.5 library definition.

No layout, virtual order, deletion algorithm, RMW or SDK implementation changes.
Only a fresh isolated copy of the exact upstream inputs is accepted.
"""
import argparse,hashlib,json,stat
from pathlib import Path

HERE=Path(__file__).resolve().parent
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def unlinked_member(source,path):
    path=Path(path)
    if not path.resolve().is_relative_to(source):raise ValueError('RTTI member path escape')
    for part in [path,*path.parents]:
        if part.exists() or part.is_symlink():
            info=part.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
                raise ValueError('RTTI linked member or parent rejected')
            if stat.S_ISREG(info.st_mode) and info.st_nlink!=1:
                raise ValueError('RTTI hardlinked member rejected')
        if part==source:break
def apply(source):
    original=Path(source).absolute();source=original.resolve();repo=HERE.parents[1]
    if not source.is_relative_to(repo/'build'):
        raise ValueError('RTTI patch requires isolated repository build source')
    if original!=source:raise ValueError('RTTI linked source root rejected')
    rows=json.loads((HERE/'TOPIC_RTTI_UPSTREAM.json').read_text())['files']
    receipt=source/'FDDST_TOPIC_RTTI_PATCH.json'
    for path in [source/row['path'] for row in rows]+[receipt]:unlinked_member(source,path)
    if receipt.exists():raise ValueError('RTTI receipt already exists')
    for row in rows:
        if sha(source/row['path'])!=row['base_sha256']:
            raise ValueError('Topic RTTI exact upstream input mismatch: '+row['path'])
    header=source/rows[0]['path'];cpp=source/rows[1]['path']
    text=header.read_text();body=cpp.read_text()
    old='    virtual ~TopicDescription()\n    {\n    }'
    if text.count(old)!=1 or body.count('Topic::Topic(')!=2:
        raise ValueError('Topic RTTI key-function anchor mismatch')
    text=text.replace('#include <string>', '#include <string>\n#include <fastrtps/fastrtps_dll.h>')
    text=text.replace(old,'    RTPS_DllAPI virtual ~TopicDescription();')
    marker='namespace dds {\n\n'
    if body.count(marker)!=1:raise ValueError('Topic definition namespace anchor mismatch')
    body=body.replace(marker,marker+'// Canonical key function for cross-DSO TopicDescription RTTI.\n'
                      'TopicDescription::~TopicDescription()\n{\n}\n\n',1)
    header.write_text(text,encoding='utf-8',newline='\n');cpp.write_text(body,encoding='utf-8',newline='\n')
    result={'scope':'EXACT_TOPICDESCRIPTION_RTTI_KEY_FUNCTION_PATCH_NOT_RUNTIME_PROOF',
            'upstream_commit':'ebcb5c05e9da9e262041e8616bbaff61ab40a296',
            'files':[dict(row,after_sha256=sha(source/row['path'])) for row in rows]}
    receipt.write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args()
    print(json.dumps(apply(a.source)))
