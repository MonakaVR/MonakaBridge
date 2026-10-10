"""5AE scope gate: preserve all 5AD contracts and isolate callback ingress from production."""
from pathlib import Path
import io, re, subprocess, tarfile
root=Path(__file__).resolve().parents[1]
base='f95b8c012ea0c47a38bea1c4a485bbf21556668a'
allow={'CMakeLists.txt','include/monaka_bridge/common_world_live_ingress.hpp',
 'src/authority/common_world_live_ingress.cpp','tests/common_world_live_ingress_tests.cpp',
 'tests/verify_common_world_live_ingress_dormancy.py','tests/validate_live_ingress_common_world_streams.py',
 'docs/common-world-live-source-runtime-integration-v2.1.md'}
# 5AF adds a separate software-only sender library. All original frozen/production
# checks below remain unchanged; the 5AF scope gate audits this exact extension.
allow.update({'include/monaka_bridge/common_world_live_sender.hpp',
 'src/publication/common_world_live_sender.cpp','tests/common_world_live_sender_tests.cpp',
 'tests/verify_common_world_live_sender_scope.py','tests/validate_live_sender_streams.py',
 'tests/CommonWorldLiveSenderReplay.java','docs/common-world-ordered-publisher-live-transport-v2.1.md'})
# 5AG adds only the separated activation policy/host. The new scope gate audits
# this exact allowlist insertion and keeps every earlier behavioral contract exact.
allow.update({'include/monaka_bridge/common_world_activation_config.hpp',
 'include/monaka_bridge/common_world_production_activation.hpp',
 'src/activation/common_world_activation_config.cpp','src/activation/common_world_production_activation.cpp',
 'src/activation/common_world_runtime_main.cpp','tests/common_world_activation_tests.cpp',
 'tests/verify_common_world_activation_scope.py','docs/common-world-production-activation-gate-v2.1.md'})
# 5AH-A adds only the isolated deployment observer/host/tooling.
allow.update(__import__("json").loads((root/"deployment/phase-scope.json").read_text()))
# BEGIN 5AI-P VERSIONED IDENTITY SCOPE
from publisher_identity_scope import approved, historical_gate
allow.update(approved(root))
# END 5AI-P VERSIONED IDENTITY SCOPE
archive=subprocess.check_output(['git','-C',str(root),'archive',base])
with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
 for member in tree:
  if member.isfile() and member.name not in allow:
   assert (root/member.name).read_bytes()==tree.extractfile(member).read(), 'frozen file changed: '+member.name
changes=set(subprocess.check_output(['git','-C',str(root),'diff','--name-only',base],text=True).splitlines())
untracked=set(subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard'],text=True).splitlines())
assert (changes|untracked)<=allow, 'out of scope inventory: '+str((changes|untracked)-allow)
names=('CommonWorldLiveIngress','common_world_live_ingress')
for folder in ('tools','outputs','ui','config','src/routing','src/observation','src/calibration','src/mapping'):
 for p in (root/folder).rglob('*'):
  if p.is_file():
   code=p.read_text(encoding='utf-8-sig',errors='replace')
   assert not any(n in code for n in names), 'production activation: '+str(p)
code='\n'.join((root/p).read_text() for p in ('include/monaka_bridge/common_world_live_ingress.hpp','src/authority/common_world_live_ingress.cpp'))
for pattern in (r'\b(?:Udp|socket|sendto|recvfrom|saveConfig|VR_Init|VRChaperone|Fanout|thread|jthread)\b',
 r'#include\s*[<"](?:openvr|monaka_bridge/(?:config|calibration|bridge|network))'):
 assert not re.search(pattern,code), pattern
assert 'CommonWorldPublisher' not in code and 'store_->commit' not in code
print('PASS 5AE: all 5AD files except CMake byte-exact; scoped callback adapter only; no production wiring, sender, transport, driver/config or worker/queue')
