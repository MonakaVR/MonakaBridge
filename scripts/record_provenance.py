"""Record derivatives of the actual verified Task2 extraction, plus pinned OpenVR files."""
import argparse,hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((root/'dependencies/task2-extraction-manifest.json').read_text())
upstream={x['path']:x for x in manifest['files']}
derived={
 'outputs/steamvr/driver/hmd_driver_factory.cpp':(['outputs/steamvr/driver/hmd_driver_factory.cpp'],'Common factory name; original OpenVR provider entry point retained'),
 'outputs/steamvr/driver/driver_log.hpp':(['outputs/steamvr/driver/driver_log.hpp'],'Common namespace; original driver logging wrapper retained'),
 'outputs/steamvr/driver/device_provider.hpp':(['outputs/steamvr/driver/device_provider.hpp'],'Thin common MTP feed replaces legacy receiver'),
 'outputs/steamvr/driver/device_provider.cpp':(['outputs/steamvr/driver/device_provider.cpp'],'Retain Init/Cleanup/dynamic lifecycle; remove vendor and calibration ownership'),
 'outputs/steamvr/driver/tracker_device.hpp':(['outputs/steamvr/driver/tracker_device.hpp'],'Retain object lifecycle; accept calibrated DirectSample'),
 'outputs/steamvr/driver/tracker_device.cpp':(['outputs/steamvr/driver/tracker_device.cpp'],'Retain activation/properties/submission; remove source profile and zero/calibration from driver'),
 'outputs/steamvr/driver/direct_pose.hpp':(['outputs/steamvr/driver/tracker_device.cpp'],'Extract DriverPose conversion, disconnected pose, partial validity and clamped age; no calibration'),
 'include/monaka_bridge/calibration.hpp':(['outputs/steamvr/driver/tracker_device.cpp'],'Move Hamilton/shortest-arc angular velocity and diagnostic orientation-zero into generic layer'),
 'src/calibration/calibration.cpp':(['outputs/steamvr/driver/tracker_device.cpp'],'Preserve separate PICO position and quaternion mapping; explicit shared world and mount transforms'),
 'src/mapping/config.cpp':(['outputs/steamvr/common/world_alignment_ipc.hpp','tools/steamvr_control_tray.cs'],'Replace IPC with persistent versioned common configuration; migrate legacy alignment/route aliases with backups'),
 'tools/steamvr_calibrator.cpp':(['tools/steamvr_calibrator.cpp'],'Retain actual OpenVR reference measurement; persist shared source-space transform instead of legacy IPC'),
 'ui/steamvr_control_gui_main.cs':(['tools/steamvr_control_gui_main.cs'],'Retain WPF/tray launch pattern in common namespace'),
 'ui/steamvr_control_tray.cs':(['tools/steamvr_control_tray.cs'],'Retain tray lifecycle/menu/window reuse; route and health use common configuration'),
 'ui/steamvr_control_wpf.cs':(['tools/steamvr_control_wpf.cs'],'Adapt WPF controls/error/process patterns into common source/mapping/profile/alignment UI'),
 'scripts/build_gui.ps1':(['scripts/steamvr_control_gui.ps1'],'Retain csc/.NET Framework/WPF discovery; common paths and explicit optional launch'),
 'tests/direct_regression.cpp':(['outputs/steamvr/driver/tracker_device.cpp'],'Test links original verified TrackerDevice unchanged under a standalone mock context'),
 'tests/udp_regression.py':(['tests/protocol_tests.cpp'],'Reads TestPoseRoundTrip position/quaternion/velocity fixture literals from verified extraction at runtime; sends fixed C1 stimulus')}
rows=[]
for path,(sources,reason) in derived.items():
 rows.append(dict(path=path,sha256=sha(root/path),change_reason=reason,origins=[upstream[s] for s in sources]))
record=dict(task2_extraction_sha256=sha(root/'dependencies/artifacts/pico-bridge-extraction.zip'),license_notice_handling=manifest['license_status'],files=rows,excluded='PICO private runtime/ABI/Android and VIVE raw transports; POTB is test-reference-only inside original archive')
sdk=root/'build/openvr-sdk';revision=subprocess.check_output(['git','-C',str(sdk),'rev-parse','HEAD'],text=True).strip()
assert revision=='0924064316de3effbcd1acf1e309182a2deb1c05'
sdkfiles=['headers/openvr.h','headers/openvr_driver.h','lib/win64/openvr_api.lib','bin/win64/openvr_api.dll','LICENSE']
subprocess.run(['git','-C',str(sdk),'diff','--exit-code','HEAD','--',*sdkfiles],check=True)
sdkrecord=dict(repository='https://github.com/ValveSoftware/openvr',tag='v2.15.6',revision=revision,files={f:sha(sdk/f) for f in sdkfiles})
for name,value in [('task2-derived-files.json',record),('openvr.lock.json',sdkrecord)]:
 target=root/'dependencies'/name
 if a.check:assert json.loads(target.read_text())==value,name+' provenance changed'
 else:target.write_bytes((json.dumps(value,indent=2)+'\n').encode('utf-8'))
print('PASS derived Task2 source/path/blob/hash provenance and exact OpenVR revision/files' if a.check else 'Recorded actual derived provenance and OpenVR lock')
