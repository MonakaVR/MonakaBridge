# Japanese + HIL H1-R1 operator adapter; original launcher preserved unchanged.
# Adapted from: C:\Users\nynyp\Downloads\6Dof\task-work\ws01b4-review-fix-20261018\launchers\operator_gui_launcher.py
# Adapted source SHA256: d3f94cb064e9fe10d2e50a9c2e9d77566fe7ec27d8012f6fd8639f89384ec627
# Launcher-only adaptation from C:\Users\nynyp\Downloads\6Dof\task-work\bridge-operator-entry-calibration-prep-20261017\bridge\scripts\operator_gui_launcher.py
# Original source SHA256: dea9a6529d92daf031b4d855d13676046ca8b78ed8c5d7026549e4f04992007b
"""Open the actual Bridge configuration GUI; never save or control a service."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import time
sys.dont_write_bytecode = True


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def checked(profile_path, mode):
    if mode not in ('normal', 'diagnostic'):
        raise ValueError('Unknown GUI mode')
    p = read(profile_path)
    paths = {key: Path(p[key]) for key in ['gui', 'helper', 'calibration_helper', 'sdk', 'package_manifest', 'config', 'health']}
    if any(not path.is_absolute() for path in paths.values()):
        raise ValueError('Every GUI target must be an absolute path')
    if len({str(path.resolve()).casefold() for path in paths.values()}) != len(paths):
        raise ValueError('GUI/config/health/helper paths must be distinct')
    for key in ['gui', 'helper', 'calibration_helper', 'sdk', 'package_manifest']:
        if sha(paths[key]) != p[key + '_sha256']:
            raise ValueError(key + ' SHA256 differs from the reviewed tool')
    manifest = read(paths['package_manifest'])
    # This adapter is for the reviewed, uncommitted Japanese + HIL H1-R1 candidate.
    # The base commit is provenance, not a claim that the candidate is a committed release.
    if (manifest['schema'] != 'monaka.bridge.hil-preview-candidate.v1'
            or manifest['base_commit'] != p['gui_source_base_commit']
            or manifest['revision'] != p['candidate_revision']
            or p['candidate_revision'] != 'H1-R1'
            or manifest['source_is_uncommitted_candidate'] is not True
            or p['source_is_uncommitted_candidate'] is not True):
        raise ValueError('Uncommitted H1-R1 candidate/base identity differs from reviewed profile')
    package = paths['gui'].parent.resolve()
    if paths['package_manifest'].resolve() != package / 'candidate-manifest.json':
        raise ValueError('Candidate manifest must belong to the selected package')
    for key, product in [('gui', 'monaka_bridge_control.exe'), ('helper', 'monaka_bridge_config.exe'),
                         ('calibration_helper', 'monaka_bridge_calibrator.exe'), ('sdk', 'openvr_api.dll')]:
        if paths[key].resolve() != package / product or manifest['files_sha256'][product] != p[key + '_sha256']:
            raise ValueError('Candidate product/profile association drift: ' + key)
    actual = {file.relative_to(package).as_posix() for file in package.rglob('*') if file.is_file()} - {'candidate-manifest.json'}
    if actual != set(manifest['files_sha256']):
        raise ValueError('Finished package inventory drift')
    for relative, expected in manifest['files_sha256'].items():
        file = (package / relative).resolve()
        if not file.is_relative_to(package) or sha(file) != expected:
            raise ValueError('Finished package content drift: ' + relative)
    config = read(paths['config'])
    if config.get('namespace') != 'monaka.bridge' or config.get('version') != 2:
        raise ValueError('Expected the existing Bridge v2 configuration')
    selected = [m for m in config.get('mappings', []) if all(m.get(k) == v for k, v in p['selected_mapping'].items())]
    # GUI/Discovery must remain available after mapping or publisher changes.
    # Native profile validation still guards measurement, saving and approval.
    p['selection_matches'] = len(selected) == 1 and config.get('bridge_id') == p['publisher']
    p['selection_warning'] = None if p['selection_matches'] else 'Saved mapping/publisher changed; select and review the current target in Bridge Control'
    operator_root = Path(p['operator_root'])
    if not operator_root.is_absolute() or not operator_root.is_dir():
        raise ValueError('Existing absolute operator root is unavailable')
    root = str(operator_root.resolve())
    args = [str(paths['gui']), root, '--control-window-only', '--installation-name', 'Current PICO Bridge',
            '--config', str(paths['config']), '--health', str(paths['health']),
            '--config-helper', str(paths['helper']), '--helper-sha256', p['helper_sha256'],
            '--allow-write' if mode == 'normal' else '--read-only']
    return p, args


def inspect(pid):
    # Integer-only PID interpolation. No shell/file mutation or UI automation.
    command = "[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false);$p=Get-CimInstance Win32_Process -Filter 'ProcessId=" + str(int(pid)) + "';if($p){$w=Get-Process -Id $p.ProcessId;$p|Select-Object ProcessId,ExecutablePath,CommandLine,@{N='Title';E={$w.MainWindowTitle}},@{N='Responding';E={$w.Responding}},@{N='CreationUtc';E={$_.CreationDate.ToUniversalTime().ToString('o')}}|ConvertTo-Json -Compress}"
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
                            capture_output=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise RuntimeError('Could not inspect the existing GUI; no duplicate started')
    return json.loads(result.stdout.decode('utf-8-sig')) if result.stdout.strip() else None


def launch(profile_path, mode):
    import msvcrt
    p, args = checked(profile_path, mode)
    here = Path(p['operator_root']).resolve()
    record = here / ('gui-' + mode + '.json')
    with (here / 'gui-launch.lock').open('a+b') as lock:
        if lock.seek(0, os.SEEK_END) == 0:
            lock.write(b'0'); lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            if record.exists():
                old = read(record); row = inspect(old['pid'])
                if row and row['CreationUtc'] == old['creation_utc'] and row['ExecutablePath'].casefold() == p['gui'].casefold() and subprocess.list2cmdline(args[1:]) in row['CommandLine']:
                    print(json.dumps(dict(result='already running', mode=mode, pid=old['pid']))); return
            run = here / 'gui-runs' / (datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + mode)
            run.mkdir(parents=True)
            startup = subprocess.STARTUPINFO(); startup.dwFlags = subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow = 1
            with (run / 'stdout.log').open('xb') as out, (run / 'stderr.log').open('xb') as err:
                child = subprocess.Popen(args, cwd=here, stdin=subprocess.DEVNULL, stdout=out, stderr=err, startupinfo=startup)
            title = 'Monaka Bridge | Current PICO Bridge' + (' | READ ONLY' if mode == 'diagnostic' else '')
            row = None
            for _ in range(10):
                if child.poll() is not None:
                    break
                row = inspect(child.pid)
                if row and row['Title'] == title and row['Responding']:
                    break
                time.sleep(.2)
            if child.poll() is not None or not row or row['Title'] != title or not row['Responding']:
                raise RuntimeError('GUI startup unconfirmed; inspect ' + str(run))
            state = dict(pid=child.pid, creation_utc=row['CreationUtc'], mode=mode, args=args,
                         config=p['config'], health=p['health'], helper=p['helper'],
                         configuration_saved_by_launcher=False, services_started=False)
            record.write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
            (run / 'started.json').write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
            print(json.dumps(state))
        finally:
            lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['check', 'launch'])
    parser.add_argument('--mode', choices=['normal', 'diagnostic'], required=True)
    parser.add_argument('--profile', type=Path, required=True)
    args = parser.parse_args()
    p, command = checked(args.profile, args.mode)
    if args.action == 'check':
        print(json.dumps(dict(ready=True, mode=args.mode, command=command,
            config=p['config'], health=p['health'], helper=p['helper'],
            base_commit=p["gui_source_base_commit"], source_is_uncommitted_candidate=True, candidate_revision="H1-R1",
            selection_matches=p["selection_matches"], selection_warning=p["selection_warning"],
            service_controls=False, calibration_gui=True, default_profile_schema="v1", explicit_v2_save_as_required=True, config_writes=0, services_started=False)))
    else:
        launch(args.profile, args.mode)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print('Bridge GUI entry blocked: ' + str(error), file=sys.stderr); raise SystemExit(2)
