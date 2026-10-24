"""Check actual OBJ resource closure and lossless UV compaction, not a visual HIL claim."""
from pathlib import Path
import argparse,hashlib,json,sys,tempfile,shutil
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'scripts'))
import render_model_assets as m
p=argparse.ArgumentParser();p.add_argument('--package',type=Path,required=True);a=p.parse_args()
prod='pico_tracker_rev3_identification';debug='pico_tracker_rev3_axis_debug'
source=root/'assets/pico-rev3-identification-source'
base=root/'outputs/steamvr/package/resources/rendermodels'
def parse(path):
 rows=path.read_text().splitlines();uv=[tuple(map(float,l.split()[1:])) for l in rows if l.startswith('vt ')]
 faces=[[(int(c.split('/')[0]),uv[int(c.split('/')[1])-1],int(c.split('/')[2])) for c in l.split()[1:]] for l in rows if l.startswith('f ')]
 return [l for l in rows if l.startswith('v ')],[l for l in rows if l.startswith('vn ')],faces
original=source/'resources/rendermodels/pico_tracker_rev3/pico_tracker_rev3.obj'
assert m.sha(original)=='e477879904c8a5b3055f0a1d8cca63a889d1a6914ef71e2c07f742ba80cd9b6d'
old_v,old_n,old_f=parse(original)
v,n,f=parse(base/prod/(prod+'.obj'));assert (v,n,f)==(old_v,old_n,old_f),'geometry, normal, face order or UV sampling changed'
assert m.sha(base/prod/(prod+'.png'))=='7e9922800e0255be6b3bbb2d4720cafa7851e454b4096cd2b3e771da1a58735e'
dv,dn,df=parse(base/debug/(debug+'.obj'));assert dv[:len(v)]==v and dn[:len(n)]==n
expected=[[(vi,(u,.5+.5*t),ni) for vi,(u,t),ni in face] for face in f]
for x,y in zip(df,expected):
 for (vi,uv,ni),(vj,tv,nj) in zip(x,y):assert vi==vj and ni==nj and all(abs(t-s)<1e-11 for t,s in zip(uv,tv))
assert len(df)==len(f)+120,'unexpected diagnostic geometry'
results={}
def validate(d,model):
 manifest=json.loads((d/'assets-manifest.json').read_text());assert manifest['model']==model and manifest['property']=='{monaka_bridge}'+model
 assert set(manifest['files'])=={model+x for x in ['.obj','.mtl','.png']}
 for name,h in manifest['files'].items():assert m.sha(d/name)==h
 assert (d/(model+'.mtl')).read_text().split('map_Kd ')[1].strip()==model+'.png'
 value,_=m.mesh(d/(model+'.obj'),model,'identification',None if model==debug else (.0385,.0152,.0385))
 assert value['expanded_vertices']<65000
 assert m.png(d/(model+'.png'))==((2048,4096) if model==debug else (2048,2048))
 return value
for model in [prod,debug]:
 results[model]=validate(base/model,model)
 assert validate(a.package/'resources/rendermodels'/model,model)==results[model]
 for p in (base/model).iterdir():assert p.read_bytes()==(a.package/'resources/rendermodels'/model/p.name).read_bytes()
# Structural mutants bypass only checksum comparison, then must fail the mesh oracle.
rejected=[]
with tempfile.TemporaryDirectory() as t:
 d=Path(t);p=d/(prod+'.obj');text=(base/prod/(prod+'.obj')).read_text()
 changes={'missing-uv':'\n'.join(l for l in text.splitlines() if not l.startswith('vt ')),
 'wrong-material':text.replace('mtllib '+prod+'.mtl','mtllib ../outside.mtl'),
 'mm-scale':'\n'.join('v '+' '.join(str(float(x)*1000) for x in l.split()[1:]) if l.startswith('v ') else l for l in text.splitlines()),
 'inverted-winding':'\n'.join('f '+' '.join(reversed(l.split()[1:])) if l.startswith('f ') else l for l in text.splitlines()),
 'uncompacted-uv':original.read_text().replace('pico_tracker_rev3.mtl',prod+'.mtl').replace('usemtl pico_rev3_identification','usemtl identification')}
 for name,value in changes.items():
  p.write_text(value)
  try:m.mesh(p,prod,'identification')
  except (ValueError,IndexError):rejected.append(name)
  else:raise AssertionError('accepted '+name)
print(json.dumps(dict(result='PASS',geometry_uv_unchanged=True,resources=results,negative_cases=rejected,hardware='NOT_RUN')))
