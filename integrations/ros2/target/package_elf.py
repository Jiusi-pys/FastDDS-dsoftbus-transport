"""Package a declared AArch64 ELF closure, without building or executing it.

The output is an input-selection receipt, never a build or device admission.
System dependencies remain external; no system DSO is copied into the package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def inspect(tool, path):
    result = subprocess.run([str(tool), '-hd', str(path)], capture_output=True,
                            text=True, check=True, shell=False, timeout=60)
    if not re.search(r'Machine:\s+AArch64\b', result.stdout):
        raise ValueError('Not an AArch64 ELF: '+str(path))
    needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', result.stdout)
    soname = re.findall(r'\(SONAME\).*?\[([^\]]+)\]', result.stdout)
    if len(soname)>1 or any('/' in n or '\\' in n or ':' in n for n in needed+soname):
        raise ValueError('Absolute or ambiguous ELF dependency: '+str(path))
    return dict(soname=soname[0] if soname else None, needed=needed,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                readelf_stdout=result.stdout, readelf_stderr=result.stderr)


def main(argv=None):
    parser=argparse.ArgumentParser()
    parser.add_argument('--readelf',type=Path,required=True)
    parser.add_argument('--main',type=Path,required=True)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--bridge',type=Path)
    mode.add_argument('--consumer-only',action='store_true',
                      help='Explicit executable mode without a transport bridge')
    parser.add_argument('--dynamic-root',type=Path,action='append',default=[],
                        help='Explicit plugin/typesupport DSO root, not inferred from DT_NEEDED')
    parser.add_argument('--library-root',type=Path,action='append',default=[])
    parser.add_argument('--library',type=Path,action='append',default=[])
    parser.add_argument('--external',action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    if args.output.exists():raise ValueError('Fresh output required; no receipt overwrite')
    external=set(args.external)
    if any(not re.fullmatch(r'lib[A-Za-z0-9_.+-]+\.so(?:\.[0-9.]+)?',n) for n in external):
        raise ValueError('Exact external library names required')
    catalog={};libraries=list(args.library)+args.dynamic_root
    for root in args.library_root:
        if not root.is_dir():raise ValueError('Declared library root missing: '+str(root))
        libraries.extend(file for file in root.rglob('*') if file.is_file()
                         and re.fullmatch(r'.+\.so(?:\.[0-9.]+)?',file.name))
    for file in libraries:
        row=inspect(args.readelf,file)
        if not row['soname']:raise ValueError('Shared library has no SONAME: '+str(file))
        old=catalog.get(row['soname'])
        if old and old[1]['sha256']!=row['sha256']:raise ValueError('Conflicting library: '+row['soname'])
        catalog[row['soname']]=(file,row)
    roots=[dict(role='main',source=str(args.main.resolve()))]
    pending=[(args.main,inspect(args.readelf,args.main))]
    if args.bridge:
        roots.append(dict(role='bridge',source=str(args.bridge.resolve())))
        pending.append((args.bridge,inspect(args.readelf,args.bridge)))
    for file in args.dynamic_root:
        row=inspect(args.readelf,file)
        if not row['soname']:raise ValueError('Dynamic root has no SONAME: '+str(file))
        roots.append(dict(role='dynamic',source=str(file.resolve()),soname=row['soname']))
        pending.append((file,row))
    chosen={}
    while pending:
        file,row=pending.pop();name=row['soname'] or file.name
        if name in external:raise ValueError('Platform library may not be bundled: '+name)
        if name in chosen:
            if chosen[name][1]['sha256']!=row['sha256']:raise ValueError('Package collision: '+name)
            continue
        chosen[name]=(file,row)
        for needed in row['needed']:
            if needed in external:continue
            if needed not in catalog:raise ValueError('Unresolved declared dependency: '+needed)
            pending.append(catalog[needed])
    args.output.mkdir(parents=True)
    members=[]
    for name,(file,row) in sorted(chosen.items()):
        shutil.copyfile(file,args.output/name)
        members.append(dict(name=name,source=str(file.resolve()),**row))
    (args.output/'PACKAGE_INPUTS.json').write_text(json.dumps(dict(
        scope='STATIC_ELF_INPUT_SELECTION_NOT_BUILD_OR_DEVICE_ADMISSION',
        main=args.main.name,external=sorted(external),members=members,roots=roots,
        readelf=str(args.readelf.resolve())),indent=2)+'\n',encoding='utf-8')
    print('AARCH64_ELF_CLOSURE_SELECTED members='+str(len(members)))

if __name__=='__main__':main()
