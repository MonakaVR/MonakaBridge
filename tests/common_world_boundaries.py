"""Current responsibility checks, independent of historical source-byte seals.

These are the behavior/boundary portions of the three historical dormancy
scripts. Their original byte/scope checks remain unchanged in the Audit lane.
"""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]

def code(paths):
    return '\n'.join((root/path).read_text(encoding='utf-8-sig') for path in paths)

def excludes(text,patterns):
    for pattern in patterns:
        assert not re.search(pattern,text),pattern

foundation=code([
    'include/monaka_bridge/common_world_authority.hpp',
    'include/monaka_bridge/common_world_publisher.hpp',
    'include/monaka_bridge/trusted_hmd_common_world_coordinator.hpp',
    'src/authority/common_world_authority.cpp',
    'src/authority/common_world_publisher.cpp',
    'src/authority/common_world_facts.hpp',
    'src/authority/trusted_hmd_common_world_coordinator.cpp',
])
excludes(foundation,[
    r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|MoveFileEx|VR_Init|VRChaperone|GetRawZeroPoseToStandingAbsoluteTrackingPose)\b',
    r'Fanout\s*::\s*put',r'\b(?:fstream|ofstream|filesystem|thread|jthread)\b',
    r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge))',
])
service=(root/'tools/bridge_service.cpp').read_text()
for name in ('CommonWorldAuthorityOwner','CommonWorldPublisher','TrustedHmdCommonWorldCoordinator'):
    assert name not in service,'runtime wiring: '+name

persistence=code([
    'include/monaka_bridge/common_world_revision_store.hpp',
    'include/monaka_bridge/persistent_common_world_runtime.hpp',
    'src/authority/common_world_revision_store.cpp',
    'src/authority/persistent_common_world_runtime.cpp',
])
excludes(persistence,[
    r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|VR_Init|VRChaperone|GetRawZeroPoseToStandingAbsoluteTrackingPose)\b',
    r'Fanout\s*::\s*put',r'\b(?:thread|jthread)\b\s*[(<]',
    r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge))',
])
ingress=code(['include/monaka_bridge/common_world_live_ingress.hpp','src/authority/common_world_live_ingress.cpp'])
excludes(ingress,[
    r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|VR_Init|VRChaperone|Fanout|thread|jthread)\b',
    r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge|network))',
])
assert 'CommonWorldPublisher' not in ingress and 'store_->commit' not in ingress
paths=('tools','outputs','ui','config','src/routing','src/observation','src/calibration','src/mapping')
for directory in paths:
    for path in (root/directory).rglob('*'):
        if path.is_file():
            text=path.read_text(encoding='utf-8-sig',errors='replace')
            assert 'CommonWorldLiveIngress' not in text and 'common_world_live_ingress' not in text,'production ingress: '+str(path)
            if path.suffix in ('.cpp','.hpp','.cs','.json','.ps1'):
                assert not any(name in text for name in ('CommonWorldRevisionStore','PersistentCommonWorldRuntime','makeCommonWorldFileStorage')),'production persistence: '+str(path)
print('PASS current CommonWorld boundaries: no forbidden transport, worker, Fanout, persistence, OpenVR or production wiring; historical seals are separate Audit checks')
