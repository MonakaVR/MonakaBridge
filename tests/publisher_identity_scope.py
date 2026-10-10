"""Explicit 5AI-P exception to historical byte freezes, never to 5S protection.

Only the required protocol/identity path may change. All historical behavioral
tests remain exact except explicit production codec / bound-record fixture values.
Historical scope assertions strip this documented extension when comparing prior
gate text; their production-isolation assertions still execute on current code.
"""
from pathlib import Path
import io,json,subprocess,tarfile
BASE='da62afb0c356bc0dae701922770a0697230f1cb7'
GATES=['verify_common_world_activation_scope.py','verify_common_world_live_sender_scope.py','verify_common_world_live_ingress_dormancy.py','verify_common_world_deployment_scope.py','verify_common_world_control_scope.py','verify_common_world_persistence_dormancy.py','verify_common_world_dormancy.py']
PATHS={
 'CMakeLists.txt','include/monaka_bridge/common_world_authority.hpp','include/monaka_bridge/common_world_publisher.hpp',
 'include/monaka_bridge/common_world_revision_store.hpp','src/authority/common_world_authority.cpp','src/authority/common_world_publisher.cpp',
 'src/authority/common_world_facts.hpp','src/authority/common_world_revision_store.cpp','src/authority/persistent_common_world_runtime.cpp',
 'src/publication/common_world_live_sender.cpp','src/activation/common_world_activation_config.cpp','src/activation/common_world_production_activation.cpp',
 'src/deployment/common_world_deployment_readiness.cpp','deployment/production.activation.json','scripts/common_world_artifact.py',
 'scripts/verify_protocol_v22.py','dependencies/monaka-protocol-v2.2.lock.json','dependencies/artifacts/monaka-protocol-kit-v2.2.zip',
 'dependencies/reports/monaka-protocol-v2.2-handoff-manifest.json','tests/publisher_identity_scope.py','tests/common_world_publisher_identity_tests.cpp',
 'tests/common_world_activation_tests.cpp','tests/common_world_readiness_tests.cpp','docs/common-world-publisher-identity-wire-v2.2.md'}|{'tests/'+n for n in GATES}
def approved(root):
    kit=root/'third_party/monaka-protocol-v22'
    # BEGIN 5AI-S LIVE SOURCE SCOPE
    from trusted_live_source_scope import approved_live_source
    # END 5AI-S LIVE SOURCE SCOPE
    # BEGIN 5AI-R2 RECEIVER SCOPE
    from receiver_scope import approved_receiver
    # END 5AI-R2 RECEIVER SCOPE
    return PATHS|approved_live_source(root)|approved_receiver(root)|{p.relative_to(root).as_posix() for p in kit.rglob('*') if p.is_file()}

# Exact textual adaptation only for the audited minor-version exception.
REPLACEMENTS={
 "current=(root/'tests/verify_common_world_live_ingress_dormancy.py').read_text()": "current=historical_gate((root/'tests/verify_common_world_live_ingress_dormancy.py').read_text())",
 'assert (root / name).read_bytes() == expected,': 'assert name in approved(root) or (root / name).read_bytes() == expected,',
 'assert current==old.decode(),': 'assert historical_gate(current)==old.decode(),',
 "assert current.replace(extension,'')==prior,": "assert historical_gate(current).replace(extension,'')==prior,",
 'assert after==before.replace(': 'assert historical_gate(after)==before.replace(',
}
def historical_gate(text):
    start='# BEGIN 5AI-P VERSIONED IDENTITY SCOPE\n';end='# END 5AI-P VERSIONED IDENTITY SCOPE\n'
    if start in text:
        a=text.index(start);b=text.index(end,a)+len(end);text=text[:a]+text[b:]
    for old,new in REPLACEMENTS.items():text=text.replace(new,old)
    return text

def verify(root):
    root=Path(root);allow=approved(root)
    def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
    changed=set(git('diff','--name-only',BASE).decode().splitlines())|set(git('ls-files','--others','--exclude-standard').decode().splitlines())
    assert changed<=allow,'5AI-P unexpected file: '+str(changed-allow)
    with tarfile.open(fileobj=io.BytesIO(git('archive',BASE,'tests','third_party/monaka-protocol-v2','dependencies/monaka-protocol-v2.1.lock.json'))) as archive:
        original={m.name:archive.extractfile(m).read() for m in archive if m.isfile()}
    # All earlier behavioral assertions stay exact; only production fixtures opt in
    # to their required 2.2 contract. Case counts / assertion bodies are not removed.
    for n,old in original.items():
        if not n.startswith('tests/'):continue
        if n=='tests/common_world_readiness_tests.cpp':expected=old.replace(b'monaka-wire-2.1',b'monaka-wire-2.2').replace(b'{"synthetic-bridge","common-A",{},0}',b'{"synthetic-bridge","common-A",{},0,r.config["source_publisher_id"].get<std::string>()}')
        elif n=='tests/common_world_activation_tests.cpp':expected=old.replace(b'r.config["mode"]="production";',b'r.config["mode"]="production"; r.config["codec"]="monaka-wire-2.2";')
        elif n in {'tests/'+g for g in GATES}:
            assert historical_gate((root/n).read_text())==old.decode(),'historical gate assertion changed '+n
            continue
        else:expected=old
        assert (root/n).read_bytes()==expected,'behavioral test changed '+n
    # Keep the old kit and every old vector physically byte-exact.
    for n,old in original.items():
        if not n.startswith('tests/'):assert (root/n).read_bytes()==old,'historical protocol changed '+n
    print('PASS 5AI-P audited version/identity scope; historical assertions and 2.1 kit retained; no 5S baseline exception')
if __name__=='__main__':verify(Path(__file__).resolve().parents[1])
