"""Validate actual 5AD durable-store streams with the unchanged pinned ordering oracle."""
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(root / 'third_party/monaka-protocol-v2/tools'))
from ordered_stream_reference import Reference

expected = {
    'durable-lifecycle': ['world', 'mapping', 'pose', 'mapping', 'pose', 'revoked', 'mapping', 'pose', 'revoked'],
    'durable-restart': ['world', 'mapping', 'pose', 'revoked', 'world', 'mapping', 'pose'],
    'durable-replacement': ['world', 'mapping', 'pose', 'revoked', 'world', 'mapping', 'pose'],
}
directory = Path(sys.argv[1])
manifest = {}
for name, outcomes in expected.items():
    path = directory / (name + '.jsonl')
    events = [json.loads(line) for line in path.read_text().splitlines()]
    reference = Reference()
    actual = [reference.accept(event) for event in events]
    assert actual == outcomes, (name, actual, outcomes)
    for event in events:
        assert event['timestamp_kind'] == 'receive'
        assert event['timestamp_ns'] == event['sent_at_ns']
        if event['type'] == 'common_pose':
            assert event['common_position'] == event['source_position']
            assert event['common_orientation'] == event['source_orientation']
    manifest[path.name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'events': len(events), 'outcomes': actual}
    print('PASS durable stream', path.name, len(events), 'events')
(directory / 'generated-stream-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
