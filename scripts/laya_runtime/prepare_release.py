#!/usr/bin/env python3
"""Prepare an offline versioned runtime. Does not start services or change current."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-manifest',required=True,type=Path)
    parser.add_argument('--source-sha256',required=True)
    parser.add_argument('--release',required=True,type=Path)
    parser.add_argument('--python-deps',required=True,type=Path)
    parser.add_argument('--reserve-bytes',type=int,default=2*1024**3)
    args=parser.parse_args()
    if digest(args.source_manifest)!=args.source_sha256:raise ValueError('source_manifest_mismatch')
    if not args.release.is_absolute() or args.release.exists():raise ValueError('release_must_be_new_absolute_directory')
    args.release.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    manifest=json.loads(args.source_manifest.read_text())
    root=args.source_manifest.parent
    source=Path(__file__).resolve().parent
    inputs=[]
    def add(spec,destination):
        path=(root/spec['path']).resolve()
        if digest(path)!=spec['sha256']:raise ValueError('input_identity_mismatch_'+destination)
        inputs.append((path,destination))
        return dict(spec,path=destination)
    for role,spec in manifest['models'].items():manifest['models'][role]=add(spec,'models/'+role+'.bmodel')
    for key in ('library','question_pack','cpu_helper'):manifest[key]=add(manifest[key], 'cpu/laya_cpu.py' if key=='cpu_helper' else Path(manifest[key]['path']).name)
    pack=json.loads((root/json.loads(args.source_manifest.read_text())['question_pack']['path']).read_text())
    for name,sha in pack['cpu_files'].items():
        add({'path':str(Path(manifest['cpu_directory'])/name),'sha256':sha},'cpu/'+name)
    manifest['cpu_directory']='cpu'
    for name,spec in manifest['implementation'].items():
        # Refuse accidental changes to the frozen numerical runner during packaging.
        if digest(source/name)!=spec['sha256']:raise ValueError('implementation_changed_'+name)
        inputs.append((source/name,name));manifest['implementation'][name]=dict(spec,path=name)
    for name in ('managed_worker.py','cosmo-laya.service.in','prepare_release.py','activate_release.py'):
        inputs.append((source/name,name))
    if not args.python_deps.is_dir():raise ValueError('missing_private_python_dependencies')
    for file in args.python_deps.rglob('*'):
        if file.is_symlink():raise ValueError('symlink_in_private_dependencies')
        if file.is_file() and '__pycache__' not in file.parts and file.suffix!='.pyc':
            inputs.append((file,'pydeps/'+str(file.relative_to(args.python_deps))))
    # Hardlink immutable local model assets on the same filesystem. Copy otherwise.
    device=args.release.parent.stat().st_dev
    copy_bytes=sum(p.stat().st_size for p,_ in inputs if p.stat().st_dev!=device or p.suffix not in (".bmodel", ".npy", ".npz"))
    available=shutil.disk_usage(args.release.parent).free
    required=args.reserve_bytes+256*1024**2+copy_bytes
    if available<required:raise RuntimeError(f'insufficient_storage_headroom available={available} required={required}')
    os.umask(0o077);args.release.mkdir(mode=0o700)
    inventory=[]
    for path,name in inputs:
        target=args.release/name;target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
        if path.stat().st_dev==device and path.suffix in (".bmodel", ".npy", ".npz"):os.link(path,target)
        else:shutil.copyfile(path,target)
        inventory.append({'path':name,'sha256':digest(target),'bytes':target.stat().st_size})
    # Resolve CPU directory against the final release, including symlink activation.
    manifest['cpu_directory']=str(args.release/'cpu')
    (args.release/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    manifest_sha=digest(args.release/'manifest.json')
    (args.release/'runtime.env').write_text('LAYA_MANIFEST_SHA256='+manifest_sha+'\nPYTHONPATH='+str(args.release/'pydeps')+'\nPYTHONDONTWRITEBYTECODE=1\n')
    receipt={'schema':1,'release':args.release.name,'manifest_sha256':manifest_sha,
             'source_manifest_sha256':args.source_sha256,'storage_before':available,'required':required,
             'numeric_exception_retained':True,'business_qualified':False,'files':inventory}
    (args.release/'release.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({'release':str(args.release),'manifest_sha256':manifest_sha,'files':len(inventory)}))

if __name__=='__main__':main()
