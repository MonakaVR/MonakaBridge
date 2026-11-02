"""Acquire and verify the public SDK in this clone's Build/Dependencies only."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def prepare_openvr(root=None):
    root = Path(root or ROOT).resolve()
    sdk = root / 'Build/Dependencies/OpenVR'
    lock = json.loads((root / 'dependencies/calibration-openvr-sdk.json').read_text(encoding='utf-8'))
    git = shutil.which('git')
    if not git:
        raise FileNotFoundError('Git is required to acquire and verify the pinned public OpenVR SDK.')
    if not sdk.exists():
        sdk.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([git, 'clone', '--depth', '1', '--branch', lock['version'],
                        'https://github.com/ValveSoftware/openvr.git', str(sdk)], check=True)
    head = subprocess.check_output([git, '--no-optional-locks', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if head != lock['commit']:
        raise ValueError('Existing Build/Dependencies/OpenVR has the wrong commit; review it explicitly. No old SDK fallback.')
    for relative, expected in lock['files'].items():
        if hashlib.sha256((sdk / relative).read_bytes()).hexdigest() != expected:
            raise ValueError('Pinned OpenVR artifact differs: ' + relative)
    return sdk


if __name__ == '__main__':
    print(prepare_openvr())
