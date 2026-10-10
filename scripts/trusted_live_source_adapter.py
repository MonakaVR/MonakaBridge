"""Real producer -> existing A2 inherited continuous control pipe. Default startup is inert.
No fixture/replay import, implicit prepare/authorize/activate, listener or state mutation.
"""
import argparse
import json
import queue
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

def strict_json(text):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise ValueError('duplicate key')
            value[key] = item
        return value
    return json.loads(text, object_pairs_hook=pairs)

class ContinuousA2Adapter:
    """Serial, bounded handoff to A2's production commands; no mutable authority seam."""
    def __init__(self, publisher, request, restart):
        self.publisher = publisher
        self.request = request
        self.restart = restart
        self.prepared = False
        self.authorized = False
        self.intent = None
        self.await_restart = False
        self.session = None
        self.last_handoff = None
        self.dropped = 0
        self.rejected = 0
        self.last_failure = None
        self.producer_snapshot = None
        self.last_event = None

    def operator(self, operation):
        if operation not in ('prepare', 'source-authorize'):
            raise ValueError('explicit source operation required')
        if (operation == 'prepare') == self.prepared:
            raise ValueError('source authorization lifecycle')
        # Any pre-approval emitted events were intentionally discarded. Start a new
        # publication so the explicitly authorized stream is contiguous from seq0.
        self.intent = operation
        self.await_restart = True
        self.authorized = False
        self.restart()

    def disconnect(self, reason):
        self.last_failure = reason if reason in ('stream-failure', 'backpressure', 'invalid-export') else self.last_failure
        self.authorized = False
        if self.prepared:
            self.request('source-disconnect', None)

    def event(self, value):
        self.producer_snapshot = value.get('snapshot', self.producer_snapshot)
        event = value.get('event')
        self.last_event = event
        if event != 'envelope':
            if event in ('disconnect', 'orderly-shutdown', 'stream-failure', 'backpressure', 'invalid-export'):
                self.disconnect(event)
                if event == 'disconnect':
                    self.await_restart = False
            return
        envelope = value['envelope']
        if self.await_restart:
            self.dropped += 1
            return
        if envelope.get('publisher_id') != self.publisher:
            self.rejected += 1
            self.disconnect('invalid-export')
            raise ValueError('canonical publisher mismatch')
        session = envelope['session_id']
        if session != self.session:
            if envelope.get('type') != 'source_authority' or envelope.get('sequence') != '0':
                raise ValueError('fresh stream bootstrap required')
            self.session = session
            self.authorized = False
            if self.prepared:
                self.request('source-reconnect', None)  # fresh lease/generation; NO authorization
            if self.intent is None:
                self.dropped += 1
                return
            response = self.request(self.intent, envelope)
            self.intent = None
            if not response.get('accepted'):
                self.rejected += 1
                self.disconnect('stream-failure')
                return
            self.prepared = True
            self.authorized = True
        elif self.authorized:
            response = self.request('input', envelope)
            if not response.get('accepted'):
                self.rejected += 1
                self.disconnect('stream-failure')
                self.restart()
                return
        else:
            self.dropped += 1
            return
        after = response['after']
        self.last_handoff = {
            'publisher_id': self.publisher, 'session_id': session,
            'sequence': envelope['sequence'], 'result': response['result'],
            'runtime_instance': after['runtime_instance'],
            'source_generation': after['sender']['source_generation'],
            'source_lease': after['source_lease'], 'observed_at_monotonic_ns': time.monotonic_ns(),
        }

    def snapshot(self):
        value = dict(self.producer_snapshot or {})
        value.update(source_publisher_id=self.publisher,
                     last_successful_a2_handoff=self.last_handoff,
                     adapter_drop_count=self.dropped, adapter_reject_count=self.rejected,
                     adapter_last_stream_failure=self.last_failure,
                     a2_source_authorized=self.authorized)
        return value

class BoundedReader:
    def __init__(self, stream, capacity, max_line):
        self.items = queue.Queue(capacity)
        self.failed = threading.Event()
        self.eof = threading.Event()
        def read():
            try:
                while True:
                    line = stream.readline(max_line + 1)
                    if not line:
                        self.eof.set()
                        return
                    if len(line) > max_line or not line.endswith('\n'):
                        raise ValueError('bounded stream framing')
                    self.items.put_nowait(strict_json(line))
            except Exception:
                self.failed.set()
        self.thread = threading.Thread(target=read, daemon=True)
        self.thread.start()

def run(args):
    config = strict_json(Path(args.producer_config).read_text(encoding='utf-8'))
    publisher = config['source_publisher_id']
    if not publisher:
        raise ValueError('operator-registered publisher required')
    # Windows subprocesses are hidden; this does not install/start ALVR or SteamVR.
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0
    processes = []
    try:
        runtime = subprocess.Popen([args.runtime, '--control', args.runtime_config, '--emergency-stop-file', args.emergency_stop_file],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   text=True, encoding='utf-8', creationflags=flags)
        processes.append(runtime)
        replies = BoundedReader(runtime.stdout, 8, 131072)
        created = replies.items.get(timeout=30)
        state = created['snapshot']
        if created.get('event') != 'created':
            raise ValueError('A2 host unavailable')
        def request(operation, envelope, extra=None):
            nonlocal state
            command = {'operation_id': str(uuid.uuid4()), 'operation': operation,
                       'runtime_instance': state['runtime_instance'], 'control_session': state['control_session'],
                       'source_lease': state['source_lease'],
                       'source_generation': (state.get('sender') or {}).get('source_generation', 0)}
            if envelope is not None:
                command['envelope'] = envelope
            if extra:
                command.update(extra)
            runtime.stdin.write(json.dumps(command, separators=(',', ':')) + '\n')
            runtime.stdin.flush()
            response = replies.items.get(timeout=30)
            if replies.failed.is_set() or response.get('operation_id') != command['operation_id']:
                raise ValueError('A2 response association')
            state = response.get('after', state)
            return response
        producer = subprocess.Popen([args.producer, '--run', args.producer_config],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                    text=True, encoding='utf-8', creationflags=flags)
        processes.append(producer)
        frames = BoundedReader(producer.stdout, 16, 32768)
        controls = BoundedReader(sys.stdin, 16, 8192)
        def restart():
            producer.stdin.write('{"operation":"restart-publication"}\n')
            producer.stdin.flush()
        adapter = ContinuousA2Adapter(publisher, request, restart)
        while True:
            if frames.failed.is_set() or controls.failed.is_set() or replies.failed.is_set():
                adapter.disconnect('backpressure')
                raise ValueError('bounded stream overflow/framing')
            if (frames.eof.is_set() and frames.items.empty()) or (controls.eof.is_set() and controls.items.empty()):
                adapter.disconnect('orderly-shutdown' if controls.eof.is_set() or adapter.last_event == 'orderly-shutdown' else 'stream-failure')
                request('shutdown', None)
                return
            try:
                control = controls.items.get_nowait()
                operation = control['operation']
                if operation in ('prepare', 'source-authorize'):
                    adapter.operator(operation)
                elif operation == 'snapshot':
                    print(json.dumps({'event':'snapshot', 'producer':adapter.snapshot(), 'runtime':state}), flush=True)
                elif operation == 'shutdown':
                    request('shutdown', None)
                    return
                elif operation == 'runtime-control':
                    allowed = {'readiness_token', 'explicit_enable'}
                    extra = control.get('arguments', {})
                    if set(extra) - allowed:
                        raise ValueError('runtime control field')
                    # All authorization remains in the unchanged A2 CURRENT gates.
                    response = request(control['runtime_operation'], None, extra)
                    print(json.dumps(response), flush=True)
                else:
                    raise ValueError('operator command')
            except queue.Empty:
                pass
            try:
                value = frames.items.get(timeout=.02)
                adapter.event(value)
                print(json.dumps({'event':value.get('event'), 'producer':adapter.snapshot(), 'runtime':state}), flush=True)
            except queue.Empty:
                # Temporary no-data is neither pipe EOF nor source disconnect.
                continue
    finally:
        for process in reversed(processes):
            if process.stdin:
                process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    for name in ('producer', 'producer-config', 'runtime', 'runtime-config', 'emergency-stop-file'):
        parser.add_argument('--' + name)
    args = parser.parse_args()
    if not args.run:
        print(json.dumps({'schema':'monaka-trusted-live-adapter-1', 'lifecycle':'DisabledByDefault', 'connected':False}))
        return
    if not all((args.producer, args.producer_config, args.runtime, args.runtime_config, args.emergency_stop_file)):
        parser.error('explicit producer/runtime/config/emergency paths required')
    run(args)

if __name__ == '__main__':
    try:
        main()
    except Exception:
        print(json.dumps({'event':'stream-failure', 'result':'adapter-failed-closed'}), flush=True)
        raise SystemExit(1)
