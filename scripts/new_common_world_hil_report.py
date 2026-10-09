"""Create exclusive, unexecuted HIL evidence; never activate or contact an endpoint."""
from pathlib import Path
import argparse, hashlib, json, platform, sys
sys.dont_write_bytecode = True

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--artifact', required=True); p.add_argument('--output', required=True)
    args = p.parse_args()
    artifact = Path(args.artifact).resolve()
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from common_world_artifact import validate, digest
    m = validate(artifact)
    result = json.loads((artifact / 'templates/hil-evidence.json').read_text())
    result.update(branch=m['branch'], commit=m['commit'], artifact_version=m['build_input_sha256'],
                  artifact_manifest_sha256=digest(artifact / 'artifact-manifest.json'),
                  machine_runtime={'system': platform.system(), 'machine': platform.machine(), 'python': platform.python_version()})
    output = Path(args.output).absolute()
    if output.is_relative_to(artifact):
        raise ValueError('reports must be outside artifact')
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2); stream.write('\n')
    print(json.dumps({'report': str(output), 'final_result': 'NOT_RUN'}))

if __name__ == '__main__':
    main()
