"""Paired runtime evidence, exclusive creation, no receiver/delivery inference."""
from pathlib import Path
import argparse, hashlib, json, subprocess, uuid

def save_fault_pair(response, output):
    if response.get('schema') != 'monaka-common-world-control-2':
        raise ValueError('control response schema required')
    before, after = response['before'], response['after']
    if not response.get('operation_id') or not response.get('operation'):
        raise ValueError('operation identity required')
    if before.get('schema') != 'monaka-common-world-runtime-snapshot-2' or after.get('schema') != before['schema']:
        raise ValueError('stable snapshot schema required')
    if not before.get('runtime_instance') or before['runtime_instance'] != after['runtime_instance'] or before['control_session'] != after['control_session']:
        raise ValueError('cross-runtime pair rejected')
    if before['activation_generation'] > after['activation_generation']:
        raise ValueError('generation regression')
    output = Path(output).absolute()
    output.mkdir(parents=False, exist_ok=False)  # never overwrite any blocked/prior evidence
    rows = []
    for name, value in [('before.json', before), ('operation.json', response), ('after.json', after)]:
        data = (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode('utf-8')
        with (output/name).open('xb') as f:
            f.write(data)
        rows.append({'path': name, 'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
    manifest = {'schema': 'monaka-common-world-fault-pair-2', 'operation_id': response['operation_id'],
                'operation': response['operation'], 'runtime_instance': before['runtime_instance'],
                'control_session': before['control_session'], 'result': response['result'],
                'accepted': response['accepted'], 'snapshots': rows,
                'delivery_semantics': 'ordered best-effort local submission', 'HIL': 'NOT_INFERRED',
                'local_submission_is_peer_receipt': False}
    with (output/'pair.json').open('x', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2); f.write('\n')
    return manifest

class LocalControlClient:
    """Explicit same-process pipe client; it never automatically enables publication."""
    def __init__(self, runtime, config, emergency_stop):
        self.process = subprocess.Popen([str(runtime), '--control', str(config), '--emergency-stop-file', str(emergency_stop)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
        greeting = self.read()
        if greeting.get('event') != 'created':
            self.process.wait(timeout=10)
            raise ValueError('control host unavailable: ' + greeting.get('result', 'unknown'))
        self.snapshot = greeting['snapshot']
    def read(self):
        line = self.process.stdout.readline()
        if not line:
            raise ValueError('control host disconnected')
        response = json.loads(line)
        if "after" in response: self.snapshot = response["after"]
        elif "snapshot" in response: self.snapshot = response["snapshot"]
        return response
    def command(self, operation, **arguments):
        request = {'operation_id': str(uuid.uuid4()), 'operation': operation,
                   'runtime_instance': self.snapshot['runtime_instance'], 'control_session': self.snapshot['control_session']}
        request.update(arguments)
        self.process.stdin.write(json.dumps(request) + '\n'); self.process.stdin.flush()
        response = self.read()
        if 'after' in response:
            self.snapshot = response['after']
        return response
    def close(self):
        if self.process.poll() is None:
            try:
                if not self.snapshot.get('shutdown'): self.command('shutdown')
            except (BrokenPipeError, ValueError):
                pass  # terminal sentinel/EOF may already have retired the process
            finally: self.process.stdin.close()
        self.process.wait(timeout=10)
        self.process.stdout.close(); self.process.stderr.close()

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--response', required=True, help='saved machine response from the retained local control process')
    p.add_argument('--output', required=True, help='new exclusive evidence directory with existing parent')
    args = p.parse_args()
    print(json.dumps(save_fault_pair(json.loads(Path(args.response).read_text(encoding='utf-8')), args.output), indent=2))
