"""5AD production isolation and byte-exact preservation of frozen 5AC contracts."""
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
base = 'eacb84580ce59666dafdbdb706daba1a302e1be3'
core = [
    'include/monaka_bridge/common_world_authority.hpp',
    'include/monaka_bridge/common_world_publisher.hpp',
    'include/monaka_bridge/trusted_hmd_common_world_coordinator.hpp',
    'src/authority/common_world_authority.cpp',
    'src/authority/common_world_publisher.cpp',
    'src/authority/trusted_hmd_common_world_coordinator.cpp',
    'src/authority/common_world_facts.hpp',
    'tests/common_world_runtime_tests.cpp',
    'tests/verify_common_world_dormancy.py',
    'tests/CommonWorldGeneratedReplay.java',
]
# BEGIN 5AI-P VERSIONED IDENTITY SCOPE
from publisher_identity_scope import approved, historical_gate
# END 5AI-P VERSIONED IDENTITY SCOPE
for name in core:
    expected = subprocess.check_output(['git', '-C', str(root), 'show', base + ':' + name])
    assert name in approved(root) or (root / name).read_bytes() == expected, 'frozen 5AC file changed: ' + name
names = ('CommonWorldRevisionStore', 'PersistentCommonWorldRuntime', 'makeCommonWorldFileStorage')
paths = ('tools', 'outputs', 'ui', 'config', 'src/routing', 'src/observation', 'src/calibration', 'src/mapping')
for directory in paths:
    for p in (root / directory).rglob('*'):
        if p.is_file() and p.suffix in ('.cpp', '.hpp', '.cs', '.json', '.ps1'):
            code = p.read_text(encoding='utf-8-sig')
            assert not any(name in code for name in names), 'production wiring: ' + str(p)
code = '\n'.join((root / p).read_text() for p in (
    'include/monaka_bridge/common_world_revision_store.hpp',
    'include/monaka_bridge/persistent_common_world_runtime.hpp',
    'src/authority/common_world_revision_store.cpp',
    'src/authority/persistent_common_world_runtime.cpp'))
for pattern in (
    r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|VR_Init|VRChaperone|GetRawZeroPoseToStandingAbsoluteTrackingPose)\b',
    r'Fanout\s*::\s*put', r'\b(?:thread|jthread)\b\s*[(<]',
    r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge))',
):
    assert not re.search(pattern, code), pattern
print('PASS 5AD: frozen 5AC core/tests byte exact; no production wiring, live source/sender/transport, thread, Fanout or driver/config integration')
