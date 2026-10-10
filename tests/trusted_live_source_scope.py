"""5AI-S is additive: unchanged authority/store/ingress/A2/sender/receiver/codec algorithms."""
from pathlib import Path
import io
import subprocess
import tarfile
import re
BASE='a2a5df9640ffff5dcfac67faf845cc880286142f'
FILES={
 'CMakeLists.txt','tests/publisher_identity_scope.py','tests/receiver_scope.py',
 'include/monaka_bridge/trusted_live_source.hpp','src/source/trusted_live_source.cpp','src/source/trusted_live_source_main.cpp',
 'tests/trusted_live_source_tests.cpp','tests/trusted_live_source_scope.py','tests/trusted_live_adapter_tests.py',
 'tests/trusted_live_source_host_tests.py','tests/trusted_live_receiver_identity.py',
 'scripts/trusted_live_source_adapter.py','scripts/trusted_live_source_artifact.py',
 'deployment/trusted-live-source.inactive.json','docs/trusted-live-source-a2-adapter-v2.1.md'}
def approved_live_source(root):return FILES
def historical_source(text):
    text=re.sub(r'(?ms)^[ \t]*# BEGIN 5AI-S LIVE SOURCE SCOPE\n.*?^[ \t]*# END 5AI-S LIVE SOURCE SCOPE\n','',text)
    return text.replace('PATHS|approved_live_source(root)|','PATHS|')
def verify(root):
    def git(*args):return subprocess.check_output(['git','-C',str(root),*args])
    changed=set(git('diff','--name-only',BASE).decode().splitlines())|set(git('ls-files','--others','--exclude-standard').decode().splitlines())
    assert changed<=FILES,('5AI-S scope',changed-FILES)
    with tarfile.open(fileobj=io.BytesIO(git('archive',BASE))) as tree:
        for member in tree:
            if not member.isfile():continue
            old=tree.extractfile(member).read();current=(root/member.name).read_bytes()
            if member.name in {'CMakeLists.txt','tests/publisher_identity_scope.py','tests/receiver_scope.py'}:
                current=historical_source(current.decode().replace('\r\n','\n')).encode()
                old=old.replace(b'\r\n',b'\n')
                # Removing an appended block also removes its intentional separating newline.
                if member.name=='CMakeLists.txt':current=current.rstrip()+b'\n';old=old.rstrip()+b'\n'
            assert current==old,'R2 production/test baseline changed: '+member.name
    core=(root/'src/source/trusted_live_source.cpp').read_text()
    adapter=(root/'scripts/trusted_live_source_adapter.py').read_text()
    assert 'w::EncodeEnvelope' in core and 'config_.sourcePublisherId' in core
    for name in ['commitWorldTransition','acquireAuthorization','enablePublisher','ownerApproved=']:
        assert name not in core+adapter,name
    assert "self.request('source-reconnect', None)" in adapter and 'self.intent is None' in adapter
    assert 'queue.Queue(capacity)' in adapter and 'put_nowait' in adapter
    print('PASS additive 5AI-S scope; all R2 authority/store/ingress/A2/sender/receiver/codec and historical behavioral tests byte-exact')
if __name__=='__main__':verify(Path(__file__).resolve().parents[1])
