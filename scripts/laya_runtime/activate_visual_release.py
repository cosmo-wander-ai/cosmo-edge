#!/usr/bin/env python3
"""Install an engine/web/typed-worker release, restoring it on readiness failure.

This is the private managed deployment route, not the product OTA protocol.
Database contents are retained across rollback; no stale database is restored.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from prepare_visual_release import digest

APP = Path('/appfs/cosmo_wander/cwai_data')
ROOT = Path('/data/cosmo-laya')
UNITS = Path('/etc/systemd/system')
DROPIN = '99-laya-visual-release.conf'
STATUS = Path('/run/cosmo-visual-decision/status.json')


def command(*args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True)


def save(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def read_status():
    try:
        return json.loads(STATUS.read_text())
    except (OSError, ValueError):
        return {}


def active(unit):
    return command('systemctl', 'is-active', unit, check=False).returncode == 0


def web_identity(root):
    return {str(path.relative_to(root)): digest(path) for path in sorted(root.rglob('*')) if path.is_file()}


def wait_ready(engine_sha, manifest_sha, services, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        worker = read_status()
        good = all(active(name) == state for name, state in services.items())
        if services['cosmo-laya']:
            good = good and worker.get('state') == 'ready' and worker.get('manifest_sha256') == manifest_sha
            good = good and 0 <= time.time() - worker.get('updated_at', 0) < 5
        engine = None
        if services['cosmo']:
            pids = command('pgrep', '-x', 'cosmo-engine', check=False).stdout.split()
            if len(pids) == 1:
                try:
                    pid = pids[0]
                    engine = {'pid': int(pid), 'sha256': digest('/proc/' + pid + '/exe'),
                              'start_ticks': int(Path('/proc/' + pid + '/stat').read_text().split(') ', 1)[1].split()[19])}
                except OSError:
                    pass
            good = good and engine is not None and engine['sha256'] == engine_sha
        if good:
            return {'engine': engine, 'worker': worker, 'services': services,
                    'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip()}
        time.sleep(1)
    raise RuntimeError('installed_runtime_readiness_failed')


def install_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.visual-next')
    shutil.copy2(source, temporary)
    temporary.replace(target)


def replace_web(source):
    target = APP / 'web'
    temporary = APP / 'web.visual-next'
    previous = APP / 'web.visual-previous'
    if temporary.exists() or previous.exists():
        raise RuntimeError('unfinished_web_install')
    shutil.copytree(source, temporary)
    for path in [temporary, *temporary.rglob('*')]:
        path.chmod(0o755 if path.is_dir() else 0o644)
    target.rename(previous)
    try:
        temporary.rename(target)
    except BaseException:
        previous.rename(target)
        raise
    shutil.rmtree(previous)


def restore(backup, timeout):
    old = json.loads((backup / 'original.json').read_text())
    command('systemctl', 'stop', 'cosmo', 'cosmo-laya')
    install_file(backup / 'cosmo-engine', APP / 'bin/cosmo-engine')
    replace_web(backup / 'web')
    if web_identity(APP / 'web') != old['web_identity']:
        raise RuntimeError('restored_web_identity_mismatch')
    for service in ['cosmo', 'cosmo-laya']:
        path = UNITS / (service + '.service.d') / DROPIN
        saved = backup / (service + '.conf')
        if saved.exists():
            install_file(saved, path)
        else:
            path.unlink(missing_ok=True)
    command('systemctl', 'daemon-reload')
    for service in ['cosmo-laya', 'cosmo']:
        if old['services'][service]:
            command('systemctl', 'start', service)
    return wait_ready(old['engine_sha256'], old['manifest_sha256'], old['services'], timeout)


def activate(release, receipt_path, timeout=90, exercise_rollback=False):
    if os.geteuid() != 0:
        raise PermissionError('device_administrator_required')
    os.umask(0o077)
    release = release.resolve()
    if release.parent != ROOT / 'visual-releases' or receipt_path.exists():
        raise ValueError('managed_release_and_new_receipt_required')
    record = json.loads((release / 'release.json').read_text())
    for entry in record['files']:
        path = (release / entry['path']).resolve()
        if not path.is_relative_to(release) or digest(path) != entry['sha256']:
            raise ValueError('release_identity_mismatch:' + entry['path'])
    if digest(release / 'manifest.json') != record['manifest_sha256']:
        raise ValueError('manifest_identity_mismatch')
    receipt_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    backup = receipt_path.parent / (receipt_path.stem + '-backup')
    backup.mkdir(mode=0o700)
    old = {'engine_sha256': digest(APP / 'bin/cosmo-engine'),
           'web_identity': web_identity(APP / 'web'),
           'manifest_sha256': read_status().get('manifest_sha256'),
           'services': {s: active(s) for s in ['cosmo', 'cosmo-laya']},
           'database_policy': 'preserve live database and WAL; no database rollback'}
    shutil.copy2(APP / 'bin/cosmo-engine', backup / 'cosmo-engine')
    shutil.copytree(APP / 'web', backup / 'web')
    for service in old['services']:
        path = UNITS / (service + '.service.d') / DROPIN
        if path.exists():
            shutil.copy2(path, backup / (service + '.conf'))
    save(backup / 'original.json', old)
    report = {'status': 'INSTALLING', 'release': str(release), 'backup': str(backup),
              'engine_sha256': record['engine_sha256'], 'manifest_sha256': record['manifest_sha256'],
              'standard_ota': False, 'exercise_rollback': exercise_rollback}
    save(receipt_path, report)
    try:
        state = ROOT / 'visual-state' / release.name
        state.mkdir(parents=True, exist_ok=True, mode=0o700)
        environment = state / 'runtime.env'
        values = {'COSMO_VISUAL_MANIFEST': release / 'manifest.json',
                  'COSMO_VISUAL_MANIFEST_SHA256': record['manifest_sha256'],
                  'COSMO_VISUAL_QUESTION_CACHE': state / 'cache', 'COSMO_VISUAL_PYTHON': '/usr/bin/python3',
                  'COSMO_VISUAL_SOCKET': '/run/cosmo-visual-decision/worker.sock',
                  'PYTHONPATH': release / 'pydeps', 'PYTHONDONTWRITEBYTECODE': '1',
                  'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1'}
        environment.write_text(''.join(k + '=' + str(v) + '\n' for k, v in values.items()))
        command('systemctl', 'stop', 'cosmo', 'cosmo-laya')
        install_file(release / 'bin/cosmo-engine', APP / 'bin/cosmo-engine')
        replace_web(release / 'web')
        common = '[Service]\nEnvironmentFile=\nEnvironmentFile=' + str(environment) + '\n'
        for service in old['services']:
            path = UNITS / (service + '.service.d') / DROPIN
            path.parent.mkdir(parents=True, exist_ok=True)
            content = common
            if service == 'cosmo':
                # Preserve the base unit's optional runtime path configuration.
                content = '[Service]\nEnvironmentFile=\nEnvironmentFile=-' + str(APP / 'share/cosmo/runtime-paths.env') + '\nEnvironmentFile=' + str(environment) + '\n'
            else:
                content += ('ExecStart=\nExecStart=/usr/bin/python3 ' + str(release / 'runtime/dynamic_worker.py')
                            + ' --manifest ' + str(release / 'manifest.json') + ' --manifest-sha256 '
                            + record['manifest_sha256'] + ' --cache ' + str(state / 'cache')
                            + ' --logs ' + str(state / 'logs') + '\nRuntimeDirectory=\nRuntimeDirectory=cosmo-visual-decision\n')
            path.write_text(content)
        command('systemctl', 'daemon-reload')
        command('systemctl', 'start', 'cosmo-laya')
        command('systemctl', 'start', 'cosmo')
        report['installed_runtime'] = wait_ready(record['engine_sha256'], record['manifest_sha256'],
                                                 {'cosmo': True, 'cosmo-laya': True}, timeout)
        if exercise_rollback:
            raise RuntimeError('injected_post_install_acceptance_failure')
        report['status'] = 'ACTIVE'
    except BaseException as error:
        report['failure'] = str(error)
        try:
            report['restored_runtime'] = restore(backup, timeout)
            report['status'] = 'ROLLED_BACK'
        except BaseException as recovery:
            report.update(status='RECOVERY_REQUIRED', recovery_error=str(recovery))
        save(receipt_path, report)
        if not exercise_rollback or report['status'] != 'ROLLED_BACK':
            raise
    save(receipt_path, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', required=True, type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    parser.add_argument('--timeout', default=90, type=float)
    parser.add_argument('--exercise-rollback', action='store_true',
                        help='inject acceptance failure after installation and verify full restoration')
    args = parser.parse_args()
    report = activate(args.release, args.receipt, args.timeout, args.exercise_rollback)
    print(json.dumps({k: report[k] for k in ['status', 'engine_sha256', 'manifest_sha256']}))


if __name__ == '__main__':
    main()
