"""A2 scope: unchanged validators/authority machines and existing behavioral tests."""
from pathlib import Path
import json, subprocess, re, io, tarfile
root=Path(__file__).resolve().parents[1]; base='2cf9a3d530de85fccf6b5e200946b3bd81b73572'
old=set(json.loads(subprocess.check_output(['git','-C',str(root),'show',base+':deployment/phase-scope.json'])))
extra={'include/monaka_bridge/common_world_revision_store.hpp','src/authority/common_world_revision_store.cpp','include/monaka_bridge/common_world_activation_config.hpp','include/monaka_bridge/common_world_production_activation.hpp','src/activation/common_world_activation_config.cpp','src/activation/common_world_production_activation.cpp','include/monaka_bridge/trusted_hmd_source_stream.hpp','include/monaka_bridge/persistent_common_world_runtime.hpp','src/authority/persistent_common_world_runtime.cpp','include/monaka_bridge/common_world_live_sender.hpp','src/publication/common_world_live_sender.cpp','src/deployment/common_world_control_host.cpp','tests/common_world_control_tests.cpp','tests/common_world_control_host_tests.py','tests/verify_common_world_control_scope.py','scripts/capture_common_world_fault.py','tests/common_world_fault_tooling_tests.py','docs/common-world-hil-control-observability-live-input-v2.1.md'}
assert set(json.loads((root/'deployment/phase-scope.json').read_text()))==old|extra
mutable={'include/monaka_bridge/common_world_revision_store.hpp','src/authority/common_world_revision_store.cpp','CMakeLists.txt','deployment/hil-evidence-template.json','deployment/phase-scope.json','include/monaka_bridge/common_world_activation_config.hpp','include/monaka_bridge/common_world_deployment_readiness.hpp','include/monaka_bridge/common_world_live_sender.hpp','include/monaka_bridge/common_world_production_activation.hpp','include/monaka_bridge/persistent_common_world_runtime.hpp','src/activation/common_world_activation_config.cpp','src/activation/common_world_production_activation.cpp','src/authority/persistent_common_world_runtime.cpp','src/publication/common_world_live_sender.cpp','src/deployment/common_world_deployment_readiness.cpp','src/deployment/common_world_hil_runtime_main.cpp','scripts/common_world_artifact.py','tests/verify_common_world_deployment_scope.py'}
# BEGIN 5AI-P VERSIONED IDENTITY SCOPE
from publisher_identity_scope import approved, historical_gate
mutable.update(approved(root))
# END 5AI-P VERSIONED IDENTITY SCOPE
archive=subprocess.check_output(['git','-C',str(root),'archive',base])
with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
 for member in tree:
  if member.isfile() and member.name not in mutable:
   assert (root/member.name).read_bytes()==tree.extractfile(member).read(),'frozen baseline: '+member.name
before=subprocess.check_output(['git','-C',str(root),'show',base+':tests/verify_common_world_deployment_scope.py'],text=True)
after=(root/'tests/verify_common_world_deployment_scope.py').read_text()
assert historical_gate(after)==before.replace('"""5AH-A preserves every 5AG behavioral file and every earlier assertion."""','"""Historical 5AH-A observer constraints plus explicitly audited A2 lifecycle/observation scope."""').replace("elif entry.name!='CMakeLists.txt':","elif entry.name not in allowed:")
# All existing behavioral regression coverage remains byte-exact. Only audited A2
# lifecycle/diagnostic files leave historical byte-freezes; validation machines do not.
code=(root/'src/publication/common_world_live_sender.cpp').read_text()
assert code.count('s->transport->submit(bytes)')==1 and 'acceptCurrentCommit(a.plan_->receipt' in code
assert code.index('SEND LINEARIZATION POINT')<code.index('s->lastSubmission=')<code.index('s->transport->submit(bytes)')
activation=(root/'src/activation/common_world_production_activation.cpp').read_text()
assert 'session_->sender->startSource(at)' in activation and 'session_->sender->authorizeSource' in activation and 'current->sender->commitSource' in activation
assert 'configurationCurrent' in activation and 'current-readiness-required' in activation and 'stale-control-session' in activation
host=(root/'src/deployment/common_world_control_host.cpp').read_text()
assert 'FILE_TYPE_PIPE' in host and 'PeekNamedPipe' in host and 'runtime.control(request' in host
assert not re.search(r'\b(?:sendto|socket|bind|listen|RegSetValue|CreateService|VR_Init)\s*\(',host)
print('PASS A2 scope: fixed authority/source validator/codec/store state machines, every prior behavioral test and legacy route unchanged; explicit lifecycle and observation extension; sole guarded send boundary; local pipe only')
