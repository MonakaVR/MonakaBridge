"""Build the fixed local runtime. Never start products or change configuration."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys
from runtime_dependencies import prepare_openvr

ROOT = Path(__file__).resolve().parents[1]


def find_cmake():
    found = shutil.which('cmake.exe') or shutil.which('cmake')
    if found:
        return found
    for key in ('ProgramFiles', 'ProgramFiles(x86)'):
        if not os.environ.get(key):
            continue
        folder = Path(os.environ[key])
        candidates = [folder / 'CMake/bin/cmake.exe']
        candidates += sorted(folder.glob('Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'), reverse=True)
        for candidate in candidates:
            if candidate.is_file():
                return str(candidate)
    raise FileNotFoundError('CMake was not found. Add CMake to PATH or install Visual Studio CMake support.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args()
    sdk = prepare_openvr(ROOT)
    cmake = find_cmake()
    build = ROOT / 'Build/runtime'
    env = os.environ.copy()
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['MSBUILDDISABLENODEREUSE'] = '1'
    # Windows environment names are case-insensitive; avoid duplicate Path/PATH
    # entries reaching MSBuild when called from tools with mixed casing.
    for key in list(env):
        if key.lower() == 'path' and key != 'PATH':
            env['PATH'] = env.pop(key)
    commands = [
        [cmake, '-S', str(ROOT), '-B', str(build), '-A', 'x64',
         '-DBUILD_TESTING=OFF', '-DMB_BUILD_CONTROL_GUI=ON', '-DMB_BUILD_STEAMVR=ON',
         '-DPython3_EXECUTABLE=' + sys.executable, '-DOPENVR_SDK=' + str(sdk.resolve())],
        [cmake, '--build', str(build), '--config', 'Release', '--parallel', '2', '--target',
         'monaka_bridge_service', 'monaka_bridge_config', 'monaka_bridge_calibrator', 'monaka_bridge_control'],
    ]
    for command in commands:
        print(subprocess.list2cmdline(command), flush=True)
        result = subprocess.run(command, cwd=ROOT, env=env)
        if result.returncode:
            return result.returncode
    print('Built repo/Build/runtime. Configuration, services and driver registration were not changed.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except OSError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
