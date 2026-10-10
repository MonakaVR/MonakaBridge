"""5AG: exact legacy contracts and explicit production/test registration boundary."""
from pathlib import Path
import io,re,subprocess,tarfile
root=Path(__file__).resolve().parents[1]
base='01c73b30bf7276ca765566ffb4b131b810341a43'
new={'include/monaka_bridge/common_world_activation_config.hpp',
 'include/monaka_bridge/common_world_production_activation.hpp',
 'src/activation/common_world_activation_config.cpp','src/activation/common_world_production_activation.cpp',
 'src/activation/common_world_runtime_main.cpp','tests/common_world_activation_tests.cpp',
 'tests/verify_common_world_activation_scope.py','docs/common-world-production-activation-gate-v2.1.md'}
gates={'tests/verify_common_world_live_ingress_dormancy.py','tests/verify_common_world_live_sender_scope.py'}
allow=new|gates|{'CMakeLists.txt'}
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
   assert (root/member.name).read_bytes()==tree.extractfile(member).read(),'frozen 5AF changed: '+member.name
for name in gates:
 prior=subprocess.check_output(['git','-C',str(root),'show',base+':'+name]).decode()
 current=(root/name).read_text()
 current=current[:current.index('# 5AH-A adds')]+current[current.index('archive=subprocess'):]
 extension=current[current.index('# 5AG adds'):current.index('archive=subprocess')]
 assert historical_gate(current).replace(extension,'')==prior,'earlier checks changed: '+name
 assert set(re.findall(r"'([^']+)'",extension))==new,'unexpected scope extension'
changes=set(subprocess.check_output(['git','-C',str(root),'diff','--name-only',base],text=True).splitlines())
untracked=set(subprocess.check_output(['git','-C',str(root),'ls-files','--others','--exclude-standard'],text=True).splitlines())
assert (changes|untracked)<=allow,'out of scope: '+str((changes|untracked)-allow)
code=(root/'src/activation/common_world_production_activation.cpp').read_text()
config=(root/'src/activation/common_world_activation_config.cpp').read_text()
header=(root/'include/monaka_bridge/common_world_production_activation.hpp').read_text()
assert 'CommonWorldActivationTestRegistration::' not in code and 'DeterministicTransport' not in code
assert 'makeCommonWorldLoopbackDatagramTransport' not in code
assert code.count('sendto(')==1 and 'nonblocking' in code and 'getaddrinfo' not in code
assert 'acceptCurrentCommit(*session_->receipt' in code
assert code.index('session_->sender->disablePublisher();')<code.index('session_->sender->disconnect();')
assert 'c.mode!=CommonWorldExecutionMode::Production' in code
assert 'candidate->config.mode!=dependencies_.mode' in code
assert 'first!=127' in config and 'test://sink' in config
assert re.search(r'bool enabled = false', (root/'include/monaka_bridge/common_world_activation_config.hpp').read_text())
for pattern in (r'\b(?:getenv|SetEnvironmentVariable|RegSetValue|CreateService|VR_Init|VRChaperone|saveConfig|thread|jthread)\s*\(', r'#include\s*[<"]openvr'):
 assert not re.search(pattern,code+'\n'+config+'\n'+header),pattern
cmake=(root/'CMakeLists.txt').read_text()
assert 'add_executable(monaka_common_world_runtime src/activation/common_world_runtime_main.cpp)' in cmake
assert not re.search(r'target_link_libraries\(monaka_bridge_service[^\n]*monaka_common_world',cmake)
print('PASS 5AG: exact 5AF/5AE/5AD/Owner/Publisher/codec/legacy; prior scope assertions retained; explicit inert production registration; isolated test registration; no driver/global config/deployment changes')
