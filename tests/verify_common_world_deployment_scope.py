"""Historical 5AH-A observer constraints plus explicitly audited A2 lifecycle/observation scope."""
from pathlib import Path
import io, json, re, subprocess, tarfile
root=Path(__file__).resolve().parents[1]
base='8abd5eb3e60e2e37592039488e72c77ecf63b6d1'
allowed=set(json.loads((root/'deployment/phase-scope.json').read_text()))
extension='# 5AH-A adds only the isolated deployment observer/host/tooling.\nallow.update(__import__("json").loads((root/"deployment/phase-scope.json").read_text()))\n'
gates={'tests/verify_common_world_activation_scope.py','tests/verify_common_world_live_sender_scope.py','tests/verify_common_world_live_ingress_dormancy.py'}
# BEGIN 5AI-P VERSIONED IDENTITY SCOPE
from publisher_identity_scope import approved, historical_gate
allowed.update(approved(root))
# END 5AI-P VERSIONED IDENTITY SCOPE
archive=subprocess.check_output(['git','-C',str(root),'archive',base])
with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
 for entry in tree:
  if not entry.isfile(): continue
  old=tree.extractfile(entry).read()
  if entry.name in gates:
   current=(root/entry.name).read_text().replace(extension,'')
   if entry.name.endswith('activation_scope.py'):
    current=current.replace(" current=current[:current.index('# 5AH-A adds')]+current[current.index('archive=subprocess'):]\n",'')
   assert historical_gate(current)==old.decode(),'earlier scope assertion changed: '+entry.name
  elif entry.name not in allowed:
   assert (root/entry.name).read_bytes()==old,'frozen 5AG behavioral file changed: '+entry.name
changes=set(subprocess.check_output(['git','-C',str(root),'diff','--name-only',base],text=True).splitlines())
untracked=set(subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard'],text=True).splitlines())
assert changes|untracked<=allowed, 'unexpected phase file'
code=(root/'src/deployment/common_world_deployment_readiness.cpp').read_text()
assert 'storage->atomicWrite' not in code and 'storage->readCommitted' in code
assert 'dry.commitSource' in code and 'std::make_unique<VolatileStorage>(recoveredBytes)' in code
assert 'preflight transport cannot connect' in code and 'preflight transport cannot submit' in code
assert 'j["enabled"]=true' in code and 'config.enabled=false' in code
for pattern in (r'\b(?:sendto|socket|recvfrom|getaddrinfo|VR_Init|CreateService|RegSetValue|SetEnvironmentVariable)\s*\(', r'\b(?:getenv|thread|jthread)\s*\('):
 assert not re.search(pattern,code),pattern
header=(root/'include/monaka_bridge/common_world_deployment_readiness.hpp').read_text()
observer=header[header.index('class CommonWorldDeploymentReadiness {'):header.index('// Diagnostics')]
assert not re.search(r'\bactivate\s*\(',observer) and 'CommonWorldDurableReceipt' not in observer
assert code.index('r.revokeOwner(at)')<code.index('r.shutdown(at)')
host=(root/'src/deployment/common_world_hil_runtime_main.cpp').read_text()
assert '--enable-production' in host and '--emergency-stop-file' in host and 'bytes(path)!=frozen' in host
cmake=(root/'CMakeLists.txt').read_text()
assert not re.search(r'target_link_libraries\(monaka_bridge_service[^\n]*(?:readiness|activation)',cmake)
assert json.loads((root/'deployment/production.activation.json').read_text())['enabled'] is False
print('PASS 5AH-A scope: exact 5AG behavior/tests and all prior assertions; observer has no activation/receipt/network/persistent-write path; separate packaged host and no legacy/global OS changes')
