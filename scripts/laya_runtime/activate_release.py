#!/usr/bin/env python3
"""Activate a verified prepared runtime with automatic rollback on readiness failure."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from prepare_release import digest

ROOT=Path('/data/cosmo-laya')
UNIT=Path('/etc/systemd/system/cosmo-laya.service')

def command(*args,check=True):
    return subprocess.run(args,check=check,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)

def switch(path):
    temporary=ROOT/'current.next'
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(path)
    temporary.replace(ROOT/'current')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release',required=True,type=Path)
    parser.add_argument('--receipt',required=True,type=Path)
    args=parser.parse_args()
    release=args.release.resolve()
    if release.parent!=ROOT/'releases':raise ValueError('release_outside_managed_root')
    receipt=json.loads((release/'release.json').read_text())
    if digest(release/'manifest.json')!=receipt['manifest_sha256']:raise ValueError('manifest_changed')
    for row in receipt['files']:
        path=(release/row['path']).resolve()
        if not path.is_relative_to(release) or digest(path)!=row['sha256']:raise ValueError('release_file_changed')
    current=ROOT/'current'
    if current.exists() and not current.is_symlink():raise ValueError('unmanaged_current_directory')
    previous=os.readlink(current) if current.is_symlink() else None
    old_unit=UNIT.read_bytes() if UNIT.exists() else None
    was_active=command('systemctl','is-active','cosmo-laya',check=False).returncode==0
    was_enabled=command('systemctl','is-enabled','cosmo-laya',check=False).returncode==0
    report={'release':str(release),'manifest_sha256':receipt['manifest_sha256'],'previous':previous,'status':'ACTIVATING'}
    args.receipt.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
    args.receipt.write_text(json.dumps(report,indent=2))
    try:
        command('systemctl','stop','cosmo-laya',check=False)
        switch(release)
        UNIT.write_bytes((release/'cosmo-laya.service.in').read_bytes())
        command('systemctl','daemon-reload');command('systemctl','reset-failed','cosmo-laya',check=False)
        command('systemctl','start','cosmo-laya')
        status=json.loads(Path('/run/cosmo-laya/status.json').read_text())
        if status['state']!='ready' or status['manifest_sha256']!=receipt['manifest_sha256'] or time.time()-status['updated_at']>3:
            raise RuntimeError('readiness_identity_mismatch')
        command('systemctl','enable','cosmo-laya')
        report.update(status='ACTIVE',runtime=status)
    except BaseException:
        command('systemctl','stop','cosmo-laya',check=False)
        if previous is not None:switch(previous)
        else:current.unlink(missing_ok=True)
        if old_unit is None:UNIT.unlink(missing_ok=True)
        else:UNIT.write_bytes(old_unit)
        command('systemctl','daemon-reload')
        if was_enabled:command('systemctl','enable','cosmo-laya',check=False)
        else:command('systemctl','disable','cosmo-laya',check=False)
        if was_active:command('systemctl','start','cosmo-laya')
        report['status']='ROLLED_BACK'
        raise
    finally:
        args.receipt.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'release':str(release)}))

if __name__=='__main__':main()
