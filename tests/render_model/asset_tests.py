"""Real files, mesh geometry, archive closure and destructive-copy negative fixtures."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid

root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
import render_model_assets as m
p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);a=p.parse_args()
directory=root/'outputs/steamvr/package/resources/rendermodels'/m.MODEL
qc=m.validate(directory)
original=root/'assets/pico-rev3-source';pins=json.loads((original/'SHA256.json').read_text())
for name in ['PICO_Tracker_Detailed_APPROX_Rev3_mm.step','PICO_Tracker_Detailed_APPROX_Rev3_mm.stl']:
    m.require(m.sha(original/name)==pins[name],'source original hash')
m.require(m.sha(original/'PICO_Tracker_Detailed_APPROX_Rev3_mm.step')==m.STEP_SHA,'original STEP identity')
_,triangles=m.mesh(directory/(m.MODEL+'.obj'))
match=m.compare_stl(original/'PICO_Tracker_Detailed_APPROX_Rev3_mm.stl',triangles)
pack=a.package/'resources/rendermodels'/m.MODEL
m.require(m.validate(pack)==qc,'built package assets differ')
for path in directory.iterdir():m.require(path.read_bytes()==(pack/path.name).read_bytes(),'built asset bytes')
m.require((a.package/'bin/win64/driver_monaka_bridge.dll').is_file(),'production DLL not packaged')
driver=json.loads((a.package/'driver.vrdrivermanifest').read_text());m.require(driver['name']=='monaka_bridge','driver name')
scratch=root/'build/d2-asset-mutants'/uuid.uuid4().hex;scratch.mkdir(parents=True)
rejected=[]

def mutant(name,change):
    target=scratch/name;shutil.copytree(directory,target)
    change(target)
    # Re-pin bytes for structural mutants so validation actually reaches the
    # OBJ/MTL/PNG oracle, rather than passing because only the checksum failed.
    if name not in ['bad-manifest','missing-obj','wrong-model','traversal-inventory']:
        value=json.loads((target/'assets-manifest.json').read_text())
        value['files']={n:m.sha(target/n) for n in m.NAMES}
        (target/'assets-manifest.json').write_text(json.dumps(value))
    try:m.validate(target)
    except (ValueError,OSError,KeyError):rejected.append(name)
    else:raise RuntimeError('asset mutant accepted: '+name)

def obj_change(fn):
    def change(d):
        path=d/(m.MODEL+'.obj');path.write_text(fn(path.read_text()),encoding='utf-8')
    return change
mutant('missing-obj',lambda d:(d/(m.MODEL+'.obj')).unlink())
mutant('bad-manifest',lambda d:(d/'assets-manifest.json').write_text('{}'))
mutant('multiple-objects',obj_change(lambda s:s+'\no second\n'))
mutant('zero-uv',obj_change(lambda s:'\n'.join(line for line in s.splitlines() if not line.startswith('vt '))))
mutant('vertex-overflow',obj_change(lambda s:s+'\nv 0 0 0\n'*65000))
mutant('nonfinite-position',obj_change(lambda s:s.replace('v 0.019250','v NaN',1)))
mutant('invalid-index',obj_change(lambda s:s.replace('f ','f 999999/1/1 ',1)))
mutant('bad-mtl-reference',obj_change(lambda s:s.replace('mtllib pico_motion_tracker.mtl','mtllib ../outside.mtl')))
mutant('mm-unit-error',obj_change(lambda s:'\n'.join('v '+' '.join(str(float(x)*1000) for x in line.split()[1:]) if line.startswith('v ') else line for line in s.splitlines())))
mutant('inverted-winding',obj_change(lambda s:'\n'.join('f '+' '.join(reversed(line.split()[1:])) if line.startswith('f ') else line for line in s.splitlines())))
mutant('bad-texture-reference',lambda d:(d/(m.MODEL+'.mtl')).write_text('newmtl approx_white\nmap_Kd ../outside.png\n'))
mutant('absolute-texture-reference',lambda d:(d/(m.MODEL+'.mtl')).write_text('newmtl approx_white\nmap_Kd C:/outside.png\n'))
mutant('corrupt-png',lambda d:(d/(m.MODEL+'.png')).write_bytes(b'\x89PNG\r\n\x1a\nbroken'))
def wrong_model(d):
    p=d/'assets-manifest.json';v=json.loads(p.read_text());v['property']='{monaka_bridge}/rendermodels/pico_motion_tracker';p.write_text(json.dumps(v))
mutant('wrong-model',wrong_model)
def traversal(d):
    p=d/'assets-manifest.json';v=json.loads(p.read_text());v['files']['../outside.obj']='0'*64;p.write_text(json.dumps(v))
mutant('traversal-inventory',traversal)
result=dict(result='PASS',qc=qc,step_sha256=m.STEP_SHA,geometry=match,negative_fixtures=rejected,
            generated_original='FreeCAD/Blender prior verified export; runtime generation not claimed',
            real_steamvr_render='NOT RUN')
(scratch/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
