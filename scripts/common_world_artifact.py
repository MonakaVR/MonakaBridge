"""Local build/package/integrity only. Never invokes activation or deployment."""
from pathlib import Path
import argparse, hashlib, json, re, struct, subprocess, sys

ROOT = Path(__file__).resolve().parents[1]

def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def build_input():
    names = sorted(set(git('ls-files').splitlines()) | set(git('ls-files', '--others', '--exclude-standard').splitlines()))
    h = hashlib.sha256()
    for name in names:
        path = ROOT / name
        if not path.is_file():
            raise ValueError('source file missing: ' + name)
        h.update(name.encode('utf-8') + b'\0' + bytes.fromhex(digest(path)))
    return h.hexdigest()

def strict_json(path):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError('duplicate JSON field')
            out[key] = value
        return out
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=pairs)

def pe_imports(path):
    data = Path(path).read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('PE executable required')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x8664:
        raise ValueError('x64 PE required')
    count, opt_size = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    opt = pe+24
    if struct.unpack_from('<H', data, opt)[0] != 0x20b:
        raise ValueError('PE32+ required')
    sections = []
    for i in range(count):
        at = opt+opt_size+i*40
        size, va, raw_size, raw_at = struct.unpack_from('<IIII', data, at+8)
        sections.append((va, max(size, raw_size), raw_at))
    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va+size:
                return raw+rva-va
        raise ValueError('invalid PE RVA')
    rva = struct.unpack_from('<I', data, opt+112+8)[0]
    out = set()
    if rva:
        at = offset(rva)
        while any(data[at:at+20]):
            name_at = offset(struct.unpack_from('<I', data, at+12)[0])
            end = data.index(b'\0', name_at)
            out.add(data[name_at:end].decode('ascii').lower()); at += 20
    if any(n.startswith(('vcruntime', 'msvcp', 'api-ms-win-crt')) or n == 'ucrtbase.dll' for n in out):
        raise ValueError('dynamic CRT dependency forbidden in static production artifact')
    return out

def validate(artifact):
    artifact = Path(artifact).resolve(strict=True)
    manifest = strict_json(artifact / 'artifact-manifest.json')
    if manifest['schema'] != 'monaka-common-world-artifact-1' or manifest['activation_default'] is not False:
        raise ValueError('invalid artifact contract')
    if (artifact / 'artifact-manifest.sha256').read_text() != digest(artifact / 'artifact-manifest.json') + '\n':
        raise ValueError('manifest hash mismatch')
    listed = set()
    for row in manifest['files']:
        name = row['path']
        if re.search(r'(^|/)(state|logs|reports|config)(/|$)|\.(rev|lock|tmp)$', name, re.I):
            raise ValueError('mutable data must be outside artifact')
        p = artifact / name
        if not name or '\\' in name or ':' in name or p.is_symlink() or not p.resolve().is_relative_to(artifact) or name in listed:
            raise ValueError('unsafe artifact path')
        for ancestor in [p, *p.parents]:
            if ancestor.is_symlink() or (hasattr(ancestor, 'is_junction') and ancestor.is_junction()):
                raise ValueError('reparse artifact path')
        if p.stat().st_size != row['size'] or digest(p) != row['sha256']:
            raise ValueError('artifact file mismatch: ' + name)
        listed.add(name)
    actual = {p.relative_to(artifact).as_posix() for p in artifact.rglob('*') if p.is_file()}
    if actual != listed | {'artifact-manifest.json', 'artifact-manifest.sha256'}:
        raise ValueError('artifact inventory mismatch')
    for name in ('bin/monaka_common_world_runtime.exe', 'bin/monaka_common_world_preflight.exe',
                 'templates/production.activation.json', 'dependency-inventory.json'):
        if name not in listed:
            raise ValueError('artifact dependency missing')
    if strict_json(artifact / 'templates/production.activation.json')['enabled'] is not False:
        raise ValueError('template activation must be OFF')
    return manifest

def package(build, output):
    if git('status', '--porcelain'):
        raise ValueError('packaging requires clean committed source')
    build, output = Path(build).resolve(), Path(output).absolute()
    if output.exists():
        raise ValueError('fresh artifact directory required; never overwrite a release')
    cache = (build / 'CMakeCache.txt').read_text()
    if 'CMAKE_MSVC_RUNTIME_LIBRARY:UNINITIALIZED=MultiThreaded' not in cache:
        raise ValueError('production build requires static MultiThreaded CRT')
    identity = build / 'Release/monaka_common_world_preflight.exe'
    runtime = build / 'Release/monaka_common_world_hil_runtime.exe'
    commit = git('rev-parse', 'HEAD')
    for binary in (identity, runtime):
        if subprocess.check_output([str(binary), '--build-identity'], text=True).strip() != commit:
            raise ValueError('binary commit mismatch; configure/build current commit')
    imports = sorted(pe_imports(identity) | pe_imports(runtime))
    header = (build / 'deployment-generated/common_world_build_identity.hpp').read_text()
    source_hash = build_input()
    if source_hash not in header:
        raise ValueError('binary build input mismatch')
    files = {
        'bin/monaka_common_world_runtime.exe': runtime.read_bytes(),
        'bin/monaka_common_world_preflight.exe': identity.read_bytes(),
        'templates/production.activation.json': (ROOT / 'deployment/production.activation.json').read_bytes(),
        'templates/hil-evidence.json': (ROOT / 'deployment/hil-evidence-template.json').read_bytes(),
        'docs/deployment-contract.md': (ROOT / 'docs/common-world-deployment-hil-readiness-v2.1.md').read_bytes(),
        'scripts/rollback.ps1': (ROOT / 'scripts/rollback_common_world_artifact.ps1').read_bytes(),
        'scripts/new_hil_report.py': (ROOT / 'scripts/new_common_world_hil_report.py').read_bytes(),
        'scripts/common_world_artifact.py': Path(__file__).read_bytes(),
        'licenses/protocol-NOTICE': (ROOT / 'third_party/monaka-protocol-v2/NOTICE').read_bytes(),
        'licenses/nlohmann-LICENSE.MIT': (ROOT / 'third_party/monaka-protocol-v2/cpp/third_party/nlohmann/LICENSE.MIT').read_bytes(),
    }
    dependencies = {'schema': 'monaka-common-world-dependencies-1', 'codec': 'monaka-wire-2.1',
                    'linkage': 'static MSVC CRT; Bridge/Owner/Store/Publisher/Sender/activation/codec static libraries',
                    'system_dlls': imports,
                    'pe_imports': {p.name: sorted(pe_imports(p)) for p in (identity, runtime)},
                    'operator_tools': ['Python 3.13+ for report/integrity tooling', 'PowerShell 5.1+ for rollback'],
                    'protocol_lock_sha256': digest(ROOT / 'third_party/monaka-protocol-v2/protocol.lock.json'),
                    'source_dependencies': {str(p.relative_to(ROOT)).replace('\\', '/'): digest(p)
                                            for p in sorted((ROOT / 'dependencies').glob('*')) if p.is_file()},
                    'hardware_runtime_required_for_preflight': False}
    files['dependency-inventory.json'] = (json.dumps(dependencies, indent=2) + '\n').encode()
    if json.loads(files['templates/production.activation.json'])['enabled'] is not False:
        raise ValueError('default OFF template required')
    # The artifact has no mutable config/state/log directories and no live endpoint.
    for name, data in files.items():
        p = output / name
        p.parent.mkdir(parents=True, exist_ok=True)
        with p.open('xb') as f:
            f.write(data)
    manifest = {'schema': 'monaka-common-world-artifact-1', 'commit': commit,
                'branch': git('branch', '--show-current'), 'build_input_sha256': source_hash,
                'build_configuration': 'Release', 'architecture': 'x64', 'runtime': 'windows-local-ntfs',
                'target_framework': 'native C++17 / MSVC', 'activation_default': False,
                'delivery_semantics': 'ordered best-effort local submission',
                'mutable_directories': 'external config, state, logs, reports; never inside artifact',
                'files': [{'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
                          for name, data in sorted(files.items())]}
    (output / 'artifact-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (output / 'artifact-manifest.sha256').write_bytes((digest(output / 'artifact-manifest.json') + '\n').encode('ascii'))
    validate(output)
    print(json.dumps({'artifact': str(output), 'commit': commit, 'manifest_sha256': digest(output / 'artifact-manifest.json'),
                      'files': len(files), 'activation_default': False}, indent=2))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('build-input')
    p = sub.add_parser('package'); p.add_argument('--build', required=True); p.add_argument('--output', required=True)
    p = sub.add_parser('verify'); p.add_argument('artifact')
    args = parser.parse_args()
    if args.command == 'build-input':
        print(build_input())
    elif args.command == 'package':
        package(args.build, args.output)
    else:
        m = validate(args.artifact)
        print(json.dumps({'result': 'PASS', 'commit': m['commit'], 'files': len(m['files']), 'activation': False}))

if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        print('FAIL: ' + str(error), file=sys.stderr)
        sys.exit(1)
