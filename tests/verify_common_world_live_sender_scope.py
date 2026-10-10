"""5AF only extends the prior scope allowlist; all frozen contracts stay byte-exact."""
from pathlib import Path
import io,re,subprocess,tarfile
root=Path(__file__).resolve().parents[1]; base='55f6259a20a6afbb22889e20e513d83f0307ccf8'
new={'include/monaka_bridge/common_world_live_sender.hpp','src/publication/common_world_live_sender.cpp',
 'tests/common_world_live_sender_tests.cpp','tests/verify_common_world_live_sender_scope.py',
 'tests/validate_live_sender_streams.py','tests/CommonWorldLiveSenderReplay.java','docs/common-world-ordered-publisher-live-transport-v2.1.md'}
allow=new|{'CMakeLists.txt','tests/verify_common_world_live_ingress_dormancy.py'}
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
   assert (root/member.name).read_bytes()==tree.extractfile(member).read(),'frozen 5AE changed: '+member.name
prior=subprocess.check_output(['git','-C',str(root),'show',base+':tests/verify_common_world_live_ingress_dormancy.py']).decode()
current=historical_gate((root/'tests/verify_common_world_live_ingress_dormancy.py').read_text())
extension=current[current.index('# 5AF adds'):current.index('archive=subprocess')]
assert historical_gate(current).replace(extension,'')==prior, 'prior scope checks weakened'
assert 'allow.update' in extension and all(n in extension for n in new)
changes=set(subprocess.check_output(['git','-C',str(root),'diff','--name-only',base],text=True).splitlines())
untracked=set(subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard'],text=True).splitlines())
assert (changes|untracked)<=allow,'out of scope: '+str((changes|untracked)-allow)
for folder in ('tools','outputs','ui','config','src/routing','src/observation','src/calibration','src/mapping','src/authority'):
 for p in (root/folder).rglob('*'):
  if p.is_file():
   code=p.read_text(encoding='utf-8-sig',errors='replace')
   assert not any(n in code for n in ('CommonWorldLiveSender','common_world_live_sender')),'production/source wiring: '+str(p)
code=(root/'src/publication/common_world_live_sender.cpp').read_text()
assert code.count('s->transport->submit(bytes)')==1 and 'acceptCurrentCommit(a.plan_->receipt' in code
for pattern in (r'\b(?:sendto|socket|saveConfig|VR_Init|VRChaperone|thread|jthread)\s*\(',r'Fanout',r'condition_variable'):
 assert not re.search(pattern,code),pattern
cmake=(root/'CMakeLists.txt').read_text()
assert not re.search(r'target_link_libraries\(monaka_bridge_service[^\n]*monaka_common_world_sender',cmake)
assert 'if(!port || (port>=29810 && port<=29813))' in code
print('PASS 5AF scope: frozen 5AD/5AE/Owner/Publisher/kit byte-exact; prior gates retained; sole guarded transport call; separate non-production library; loopback-only endpoint')
