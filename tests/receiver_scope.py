"""5AI-R2 adds a standalone receiver only; every sender/codec baseline byte is frozen."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile
from p2r_runtime_scope import approved_runtime,runtime_bytes
# BEGIN WS01A EXACT AGENTS IMPORT
from control_foundation_scope import verify_archive_agents
# END WS01A EXACT AGENTS IMPORT
BASE='f2f0a46199af711f91c7e575c6f9229bdff51614'
FILES={'CMakeLists.txt','tests/publisher_identity_scope.py','tests/receiver_scope.py','tests/receiver_ctest.py',
 'scripts/import_receiver_core.py','scripts/receiver_build.py','scripts/receiver_artifact.py',
 'deployment/receiver.inactive.json','docs/common-world-production-receiver-wire-v2.2.md',
 'receiver/settings.gradle.kts','receiver/build.gradle.kts','receiver/.gitignore','receiver/NOTICE.md','receiver/provenance.json',
 'receiver/libs/kotlinx-serialization-core-jvm-1.10.0.jar','receiver/libs/LICENSE-APACHE',
 'receiver/src/Recovery.kt','receiver/src/ReceiverStore.kt','receiver/src/ReceiverRuntime.kt','receiver/src/ReceiverMain.kt',
 'receiver/tests/ReceiverTests.kt','receiver/upstream/LICENSE-MIT','receiver/upstream/LICENSE-APACHE',
 'receiver/upstream/io/github/axisangles/ktmath/LICENSE-MIT','receiver/upstream/io/github/axisangles/ktmath/LICENSE-APACHE'}
FILES|={'receiver/upstream/dev/monaka/tracking/'+n+'.kt' for n in ['CommonWorldAuthority','TrustedHmdCommonWorldStream']}
FILES|={'receiver/upstream/io/github/axisangles/ktmath/'+n+'.kt' for n in ['Vector3','Quaternion','Matrix3','EulerAngles','Transform']}
def approved_receiver(root):return FILES|approved_runtime(root)
def strip(text,start,end):
    a=text.index(start);b=text.index(end,a)+len(end);return text[:a]+text[b:]
def digest(b):return hashlib.sha256(b).hexdigest()
def verify(root):
    root=Path(root)
    def git(*a):return subprocess.check_output(['git','-C',str(root),*a])
    changes=set(git('diff','--name-only',BASE).decode().splitlines())|set(git('ls-files','--others','--exclude-standard').decode().splitlines())
    # BEGIN 5AI-S LIVE SOURCE SCOPE
    from trusted_live_source_scope import approved_live_source,historical_source
    verify_live_allow=approved_live_source(root)
    changes-=verify_live_allow-FILES
    # END 5AI-S LIVE SOURCE SCOPE
    assert changes<=FILES|approved_runtime(root),('receiver scope',changes-FILES)
    assert all((root/n).is_file() for n in FILES)
    with tarfile.open(fileobj=io.BytesIO(git('archive',BASE))) as tree:
        for member in tree:
            if not member.isfile():continue
            old=tree.extractfile(member).read();current=runtime_bytes(root,member.name,(root/member.name).read_bytes())
            # BEGIN 5AI-S LIVE SOURCE SCOPE
            if member.name in {'CMakeLists.txt','tests/publisher_identity_scope.py'}:
                current=historical_source(current.decode().replace('\r\n','\n')).encode()
                old=old.replace(b'\r\n',b'\n')
                if member.name=='CMakeLists.txt':current=current.rstrip()+b'\n';old=old.rstrip()+b'\n'
            # END 5AI-S LIVE SOURCE SCOPE
            if member.name=='CMakeLists.txt':
                current=strip(current.decode().replace('\r\n','\n'),'# BEGIN 5AI-R2 RECEIVER SCOPE\n','# END 5AI-R2 RECEIVER SCOPE\n').encode()
                old=old.replace(b'\r\n',b'\n')
            elif member.name=='tests/publisher_identity_scope.py':
                current=current.decode().replace('\r\n','\n')
                current=strip(current,'    # BEGIN 5AI-R2 RECEIVER SCOPE\n','    # END 5AI-R2 RECEIVER SCOPE\n').replace('PATHS|approved_receiver(root)|','PATHS|').encode()
                old=old.replace(b'\r\n',b'\n')
            # BEGIN WS01A EXACT AGENTS COMPARISON
            if member.name=='AGENTS.md':
                verify_archive_agents(root,BASE,old,current)
            else:
                assert current==old,'5AI-P production/test baseline changed: '+member.name
            # END WS01A EXACT AGENTS COMPARISON
    provenance=json.loads((root/'receiver/provenance.json').read_text())
    for entry in provenance['files']:
        raw=(root/entry['path']).read_bytes().replace(b'\r\n',b'\n')
        if '/dev/monaka/tracking/' in entry['path']:
            text=raw.decode();start='\t// BEGIN 5AI-R2 RECOVERY API\n';end='\t// END 5AI-R2 RECOVERY API\n'
            text=strip(text,start,end)
            raw=text.encode()
        assert digest(raw)==entry['git_blob_sha256'],'upstream algorithm/dependency changed: '+entry['path']
    assert digest((root/'receiver/libs/kotlinx-serialization-core-jvm-1.10.0.jar').read_bytes())=='14d6f27ce28f61ebc4a516d562f911b7bc01cfbe5397fb884c45ea0db044c635'
    production='\n'.join((root/p).read_text() for p in FILES if p.startswith('receiver/src/') and p.endswith('.kt'))
    assert 'MonakaCodec.decodeEnvelope(bytes)' in production
    assert 'DatagramChannel.open(StandardProtocolFamily.INET)' in production
    assert 'ByteBuffer.allocate(4097)' in production
    assert 'SourcePublisherMismatch' in production and 'StaleSocketCompletion' in production
    for forbidden in ['test://sink','ReceiverTestsKt','TrackerObservation(','VR_Init','ProcessBuilder','Runtime.getRuntime','System.getenv']:
        assert forbidden not in production,forbidden
    print('PASS R2 receiver-only scope; 5AI-P sender/Protocol/2.1 fixtures exact; original Core algorithms exact outside recovery APIs')
if __name__=='__main__':verify(Path(__file__).resolve().parents[1])
