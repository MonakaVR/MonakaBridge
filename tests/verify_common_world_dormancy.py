"""Static foundation boundaries and unchanged 5AB/legacy/protocol source gates."""
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
base = 'be97d2279ffd51ba6e66bd9bc19526164309691d'
files = [root / 'include/monaka_bridge' / name for name in (
    'common_world_authority.hpp', 'common_world_publisher.hpp',
    'trusted_hmd_common_world_coordinator.hpp')]
files += [root / 'src/authority' / name for name in (
    'common_world_authority.cpp', 'common_world_publisher.cpp',
    'common_world_facts.hpp', 'trusted_hmd_common_world_coordinator.cpp')]
code = '\n'.join(p.read_text() for p in files)
for pattern in (
    r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|MoveFileEx|VR_Init|VRChaperone|GetRawZeroPoseToStandingAbsoluteTrackingPose)\b',
    r'Fanout\s*::\s*put', r'\b(?:fstream|ofstream|filesystem|thread|jthread)\b',
    r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge))',
):
    assert not re.search(pattern, code), pattern
service = (root / 'tools/bridge_service.cpp').read_text()
for name in ('CommonWorldAuthorityOwner', 'CommonWorldPublisher', 'TrustedHmdCommonWorldCoordinator'):
    assert name not in service, 'runtime wiring: ' + name
protected = ['third_party', 'dependencies', 'config', 'outputs', 'ui', 'tools',
             'src/calibration', 'src/mapping', 'src/routing', 'src/observation',
             'src/authority/trusted_hmd_source_stream.cpp',
             'include/monaka_bridge/trusted_hmd_source_stream.hpp',
             'include/monaka_bridge/calibration.hpp', 'include/monaka_bridge/config.hpp',
             'include/monaka_bridge/world_calibration.hpp',
             'include/monaka_bridge/trajectory_calibration.hpp', 'include/monaka_bridge/bridge.hpp']
changes = subprocess.check_output(['git', '-C', str(root), 'diff', '--name-only', base, '--', *protected], text=True)
# BEGIN 5AI-P VERSIONED IDENTITY SCOPE
from publisher_identity_scope import approved, historical_gate
changes='\n'.join(n for n in changes.splitlines() if n not in approved(root))
# END 5AI-P VERSIONED IDENTITY SCOPE
assert not changes.strip(), 'protected changes: ' + changes
subprocess.run([sys.executable, str(root / 'scripts/verify_protocol_v21.py')], check=True)
print('PASS dormant owner/publisher: no transport/thread/Fanout/persistence/OpenVR/runtime wiring; protected legacy/5AB/kit unchanged')
