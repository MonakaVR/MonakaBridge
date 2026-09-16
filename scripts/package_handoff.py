"""Package this repository's committed source, actual built artifacts and validation evidence."""
import datetime,hashlib,json,os,subprocess,zipfile
from pathlib import Path

if __name__ == "__main__":
    raise SystemExit("Historical wire-v1 report is retired at current v2 HEAD. Use python scripts/release_v2.py; see docs/release-v2.md. Historical supplied artifacts remain immutable provenance.")

root=Path(__file__).resolve().parents[1];os.chdir(root)
def git(*args):return subprocess.check_output(['git',*args],text=True).strip()
def sha(data):return hashlib.sha256(data).hexdigest()
def read(path):return json.loads((root/path).read_text(encoding='utf-8'))
assert not git('status','--porcelain'),'Commit reviewable Task4 changes before packaging'
head=git('rev-parse','HEAD');prepared='6bcb0135bf086ddf233c8c01fd43f3df773f4301';audited='686392ead354292cab02422c942bf0236dee4255'
subprocess.run(['git','merge-base','--is-ancestor',prepared,head],check=True)
subprocess.run([os.sys.executable,'scripts/import_upstream.py'],check=True)
subprocess.run([os.sys.executable,'scripts/record_provenance.py','--check'],check=True)
validation=read('build/validation-results.json');assert validation['status']=='PASS'
cache=(root/'build/CMakeCache.txt').read_text(encoding='utf-8')
cmake=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_COMMAND:INTERNAL='))
for f,digest in validation['binary_sha256'].items():assert sha((root/'build/Release'/f).read_bytes())==digest,'Validation binary changed: '+f
for f,digest in validation['output_sha256'].items():assert sha((root/f).read_bytes())==digest,'Validated output changed: '+f
payload={}
def add(name,path):payload[name]=(root/path).read_bytes()
for f in git('ls-files').splitlines():payload['source/'+f]=subprocess.check_output(['git','show',head+':'+f])
for f in ['monaka_bridge_service.exe','monaka_bridge_config.exe','monaka_bridge_calibrator.exe','openvr_api.dll']:
 add('runtime/build/Release/'+f,'build/Release/'+f)
add('runtime/build-gui/monaka_bridge_control.exe','build-gui/monaka_bridge_control.exe')
add('runtime/config/bridge.json','config/bridge.example.json')
for f in (root/'build/steamvr/monaka_bridge').rglob('*'):
 if f.is_file():payload['runtime/steamvr/monaka_bridge/'+f.relative_to(root/'build/steamvr/monaka_bridge').as_posix()]=f.read_bytes()
add('runtime/README.md','README.md')
for f in (root/'docs').glob('*.md'):payload['runtime/docs/'+f.name]=f.read_bytes()
for f in ['validation-results.json','performance-results.json','protocol-interop-results.json','udp-regression-results.json']:
 add('evidence/'+f,'build/'+f)
for f in (root/'build/validation').glob('*.log'):payload['evidence/logs/'+f.name]=f.read_bytes()
internal={'repository':'MonakaVR/MonakaBridge','source_commit':head,'hardware':'NOT RUN','files':[{'path':f,'sha256':sha(data),'size':len(data)} for f,data in sorted(payload.items())]}
payload['handoff-content-manifest.json']=(json.dumps(internal,indent=2)+'\n').encode()
dist=root/'dist';dist.mkdir(exist_ok=True);zip_path=dist/'monaka-bridge-handoff.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED) as z:
 for f,data in sorted(payload.items()):z.writestr(f,data)
# Verify every actual ZIP entry after closing it.
with zipfile.ZipFile(zip_path) as z:
 assert z.testzip() is None
 for f,data in payload.items():assert sha(z.read(f))==sha(data),f
artifact={'filename':zip_path.name,'sha256':sha(zip_path.read_bytes()),'source_commit':head}
external={'repository':'MonakaVR/MonakaBridge','source_commit':head,'artifact':artifact,'content_manifest_sha256':sha(payload['handoff-content-manifest.json']),'validation':validation,'phase_status':'implementation complete / hardware cutover pending'}
manifest_path=dist/'monaka-bridge-handoff.handoff.json';manifest_path.write_text(json.dumps(external,indent=2)+'\n',encoding='utf-8')
def reason(f):
 if f.startswith('third_party/'):return 'Exact supplied Task1 kit bytes; fixed contract/codec/fixtures/licenses, hash verified'
 if f.startswith('dependencies/'):return 'Actual upstream artifacts, manifests, reports, provenance or pinned dependency hashes'
 if f.startswith('src/observation/'):return 'Common bounded multi-source/session registry with fixed local sample age'
 if f.startswith('src/calibration/'):return 'PICO compatibility and generic shared world/mount transforms and derivatives'
 if f.startswith('src/mapping/') or f.startswith('config/'):return 'Persistent identities, profile approvals, revision validation and legacy migration backups'
 if f.startswith('src/routing/'):return 'Common calibrated cache, bounded independent C1 fanout, loopback transport and thin MTP consumer'
 if f.startswith('include/'):return 'Generic vendor-independent Bridge interfaces using fixed C1 models'
 if f.startswith('outputs/steamvr/'):return 'Task2-derived common thin SteamVR output and one driver package'
 if f.startswith('ui/'):return 'Task2-derived common WPF/tray configuration, migration and health'
 if f.startswith('tools/'):return 'Common service/config commands or migrated shared SteamVR calibrator'
 if f.startswith('tests/'):return 'Actual fixture regression, isolated old driver harness, caught null-read reproduction, UDP/interop/performance evidence'
 if f.startswith('scripts/'):return 'Reproducible build, strict provenance/verification or non-circular source/binary handoff packaging'
 if f=='AGENTS.md' or f.startswith('docs/codex/'):return 'Prepared Task4 instructions and upstream audit metadata; not implementation-generated'
 if f.startswith('docs/') or f=='README.md':return 'Architecture, configuration, crash investigation, validation limits and staged cutover documentation'
 return 'Build configuration or generated-artifact/text handling rules'
report={
 'repository':'MonakaVR/MonakaBridge','audited_base_branch':'main','audited_base_sha':audited,
 'actual_base_branch':'refactor/monaka-layer-separation','actual_base_sha':prepared,
 'base_change_reason':'Prepared branch supplies Task4 instructions on audited main ancestor; ancestry verified; no history rewrite',
 'work_branch':git('branch','--show-current'),'HEAD_SHA':head,'protocol_version':'1.0 fixed candidate / master reconciliation pending',
 'schema_commit':'04f2d6c831c68a65edfbfb66ff8838d1c9d78535',
 'protocol_kit_sha256':'eef5b7f2bc490926385b99dabcd44dc5a374228bf2a7869beea01f9dad936729',
 'contract_c1_sha256':'3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3',
 'task2_handoff_source_commit':'c638f158516effd7fc6511daf179b9a927a099a6',
 'task2_extraction_sha256':sha((root/'dependencies/artifacts/pico-bridge-extraction.zip').read_bytes()),
 'task3_handoff_source_commit':'eaaee63fcacb76d095ff743944fbff1f1427f015',
 'task3_handoff_sha256':sha((root/'dependencies/artifacts/vive-backend-handoff.zip').read_bytes()),
 'changed_files':[{'path':f,'reason':reason(f)} for f in git('diff','--name-only',audited,head).splitlines()],
 'build_result':'PASS Windows x64 Release service/common driver/calibrator; WPF .NET Framework 4.8 build and config-reader smoke PASS; non-Windows NOT RUN',
 'toolchain':{'generator':'Visual Studio 18 2026','compiler':'MSVC 19.51.36257.0','sdk':'10.0.26100.0','python':'3.13.15','java':'Microsoft OpenJDK 17.0.20','openvr':read('dependencies/openvr.lock.json')},
 'commands':{'native_build':[os.sys.executable,'scripts/build_windows.py','--cmake',cmake],'gui_build':['./scripts/build_gui.ps1'],'verification':validation['verification_command'],'package':[os.sys.executable,'scripts/package_handoff.py'],'working_directory':str(root)},
 'unit_test_result':validation['checks']['ctest'],
 'mock_or_cross_language_result':{'udp':read('build/udp-regression-results.json'),'fixed_codec':read('build/protocol-interop-results.json')},
 'direct_regression_result':{'standalone':validation['checks']['direct-standalone'],'ctest':validation['checks']['ctest'],'separate_process_udp':validation['checks']['separate-process-udp'],'initial_failure':'Real 0xc0000005 null driver context reproduced with stack; test-only mock logger and runtime-interface guards fix standalone harness','runtime_imports':validation['checks']['direct-imports']},
 'hardware_validation':{x:'NOT RUN' for x in ['PICO five trackers','VIVE official dongle with Hub stopped','VIVE physical axes/scale/map','PICO+VIVE coexistence','SteamVR restart/registration/routes','same-point Direct vs MTP hardware','interactive WPF/tray','OpenVR calibrator measurement']},
 'compatibility_status':'Actual Task2 extraction migration, PICO fixture/legacy Direct software regression and actual Task3 C1 fixture interoperability PASS; hardware cutover pending',
 'performance_result':read('build/performance-results.json'),
 'phase_or_DoD_status':'Task4 software implementation complete / hardware cutover pending',
 'unresolved_issues':['Task1 fixed candidate master reconciliation pending','Hardware and interactive validation NOT RUN','Task5 consumer/exclusion contract not implemented by Task4','Legacy SteamVR serial-role migration requires user-reviewed hardware comparison','Concurrent config writers should be avoided; no cross-tool distributed lock','Non-Windows build NOT RUN','Task2 extraction has no root license grant at source base; original notices preserved'],
 'followup_required_in_task2':'Verify this HEAD/ZIP/manifest and compatibility evidence; satisfy hardware and Task5 gates before Phase B or old-path removal',
 'followup_required_in_task5':'Integrate fixed MTP, source/session/mapping revisions; exclude monaka-direct and own solver outputs from input; distinct solver serial namespace; verify both-route no-feedback before opt-in',
 'handoff_artifacts':[artifact,{'filename':manifest_path.name,'sha256':sha(manifest_path.read_bytes()),'source_commit':head}],
 'responsibility_split':{'Bridge':'mapping/profiles/shared calibration/common cache/MTP/Direct/routing/GUI','PICO Backend':'runtime/private ABI/Android/vendor source operations','VIVE Backend':'HID/RF/raw decode/vendor source operations','MonakaVR':'roles/fusion/fallback/IK/solver output'},
 'old_path_preservation':'Task4 changes only MonakaBridge; old backends/providers/config files remain. Migration retains backups. Hardware cutover and Task2 Phase B are gated; no automatic driver install/removal.'}
(dist/'task4-final-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'HEAD':head,'artifact':artifact,'manifest_sha256':sha(manifest_path.read_bytes()),'report':str(dist/'task4-final-report.json')},indent=2))
