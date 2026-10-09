"""Read generated codec-roundtripped streams through the pinned C2.1 test-only oracle."""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True  # fixed kit is read-only, including its member inventory
sys.path.insert(0, str(ROOT / 'third_party/monaka-protocol-v2/tools'))
from ordered_stream_reference import Reference

expected = {
    'normal': ['world', 'mapping', 'pose'],
    'unavailable-recovery': ['world', 'mapping', 'pose', 'unavailable', 'pose'],
    'mapping-update': ['world', 'mapping', 'pose', 'mapping', 'pose'],
    'mapping-revoke': ['world', 'mapping', 'pose', 'revoked'],
    'mapping-reestablish': ['world', 'mapping', 'pose', 'revoked', 'mapping', 'pose'],
    'world-replace': ['world', 'mapping', 'pose', 'revoked', 'world', 'mapping', 'pose'],
    'world-revoke-no-pose': ['world', 'mapping', 'pose', 'revoked'],
    'publisher-reconnect': ['world', 'mapping', 'pose', 'world', 'mapping', 'pose'],
    'explicit-world-replace': ['world', 'mapping', 'pose', 'revoked', 'world', 'mapping', 'pose'],
}
directory = Path(sys.argv[1])
manifest = {}
for name, outcomes in expected.items():
    path = directory / (name + '.jsonl')
    events = [json.loads(line) for line in path.read_text().splitlines()]
    reference = Reference()
    actual = [reference.accept(e) for e in events]
    assert actual == outcomes, (name, actual, outcomes)
    session = None
    sequence = -1
    for i, event in enumerate(events):
        if event['session_id'] != session:
            session = event['session_id']
            sequence = -1
            assert event['type'] == 'world_authority'
            assert events[i + 1]['type'] == 'mapping_publication'
        assert int(event['sequence']) == sequence + 1
        sequence += 1
        assert event['timestamp_kind'] == 'receive'
        assert event['timestamp_ns'] == event['sent_at_ns']
        if event['type'] == 'common_pose':
            assert event['source_position'] == event['common_position']
            assert event['source_orientation'] == event['common_orientation']
    manifest[path.name] = {'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'events': len(events), 'outcomes': actual}
    print('PASS', path.name, len(events), 'ordered events')
(directory / 'generated-stream-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
