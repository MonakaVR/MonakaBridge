"""Color-only revision. Python 3 + numpy + Pillow. No Blender needed for export.
Usage: python colorize_rev3.py /path/to/original.obj /output/directory
Preserves original v, vn, vertex face order and normal references verbatim.
"""
import sys,json,hashlib
from pathlib import Path
from collections import Counter
import numpy as np
from PIL import Image,ImageDraw
src=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
lines=src.read_text().splitlines();vs=[];fs=[];ft=[]
for l in lines:
 if l.startswith('v '):vs.append(list(map(float,l.split()[1:4])))
 if l.startswith('f '):
  ft.append(l.split()[1:]);fs.append([int(t.split('/')[0])-1 for t in l.split()[1:]])
v=np.array(vs)*1000;f=np.array(fs);p=v[f];c=p.mean(1)
parent=list(range(len(v)))
def root(i):
 while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
 return i
for face in f:
 a=root(face[0])
 for b in face[1:]:parent[root(b)]=a
components={}
for i in range(len(v)):components.setdefault(root(i),[]).append(i)
component_type={}
for k,ids in components.items():
 low=v[ids].min(0);high=v[ids].max(0)
 if len(ids)>1000:typ='housing'
 elif high[0]<-15:typ='usb'
 elif high[1]<0:typ='led'
 else:typ='button'
 component_type[k]=typ
colors={'housing':(221,224,229),'logo':(13,16,21),'seam':(28,34,43),'led':(0,112,255),'usb':(125,138,153),'cavity':(63,74,89),'button':(183,192,204)}
N=2048;img=Image.new('RGB',(N,N),colors['housing']);d=ImageDraw.Draw(img)
# Back-panel region: x [-16,+16] -> u [0.025,0.775], -z [-16,+16] -> v [.025,.775].
def back_uv(x,z):return (.025+.75*(x+16)/32,.025+.75*(-z+16)/32)
def px(x,z):
 u,vv=back_uv(x,z);return (round(u*N),round((1-vv)*N))
# Directional mark is geometry-free, intentionally large and unambiguous at low resolution.
arrow=[(-2.1,8),(2.1,8),(2.1,-2),(6,-2),(0,-10),(-6,-2),(-2.1,-2)]
d.polygon([px(x,z) for x,z in arrow],fill=(12,20,33))
swatches={}
for j,(name,col) in enumerate(colors.items()):
 u=.875;vv=.08+j*.125;swatches[name]=(u,vv)
 box=[int((u-.065)*N),int((1-vv-.05)*N),int((u+.065)*N),int((1-vv+.05)*N)]
 d.rectangle(box,fill=col)
img.save(out/'pico_rev3_identification_atlas.png')
normal=np.cross(p[:,1]-p[:,0],p[:,2]-p[:,0]);normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-30)
rad=np.linalg.norm(c[:,[0,2]],axis=1);dot=np.abs((normal[:,0]*c[:,0]+normal[:,2]*c[:,2])/np.maximum(rad,1e-9))
labels=[];uvs=[];newfaces=[]
for i,face in enumerate(f):
 typ=component_type[root(face[0])]
 if typ=='housing':
  if np.all(abs(p[i,:,1]-7.6)<1e-5):typ='back'
  elif c[i,1]<-7.24999 and not np.all(abs(p[i,:,1]+7.6)<1e-5) and np.linalg.norm(c[i,[0,2]])<12:typ='logo'
  elif abs(c[i,1]-.76)<.10001 and rad[i]>19.1:typ='seam'
  elif c[i,0]<-15 and abs(c[i,2])<4.61 and 1.34<c[i,1]<4.66 and (rad[i]<18.66 or dot[i]<.8):typ='cavity'
 labels.append(typ);ids=[]
 for j,vid in enumerate(face):
  if typ=='back':uv=back_uv(v[vid,0],v[vid,2])
  else:
   u,vv=swatches[typ];du,dv=[(-.002,-.002),(.002,-.002),(0,.002)][j];uv=(u+du,vv+dv)
  uvs.append('vt %.9f %.9f'%uv);ids.append(len(uvs))
 tokens=[]
 for j,t in enumerate(ft[i]):
  a=t.split('/');tokens.append(a[0]+'/'+str(ids[j])+('/'+a[2] if len(a)>2 else ''))
 newfaces.append('f '+' '.join(tokens))
# Retain every original vertex and normal string. Write just one material and same face order.
output=['# PICO Rev3 identification colors. Geometry unchanged.','mtllib pico_tracker_rev3.mtl','o pico_tracker_rev3']
output += [l for l in lines if l.startswith('v ') or l.startswith('vn ')]
output += uvs+['usemtl pico_rev3_identification','s 1']+newfaces
obj=out/'pico_tracker_rev3.obj';obj.write_text('\n'.join(output)+'\n')
(out/'pico_tracker_rev3.mtl').write_text('newmtl pico_rev3_identification\nKa 0.000000 0.000000 0.000000\nKd 1.000000 1.000000 1.000000\nKs 0.000000 0.000000 0.000000\nNs 50.000000\nd 1.000000\nillum 2\nmap_Kd pico_rev3_identification_atlas.png\n')
r={'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'vertices':len(v),'triangles':len(f),'face_categories':dict(Counter(labels)),'positions_preserved_verbatim':True,'normals_preserved_verbatim':True,'face_vertex_and_normal_indices_preserved':True,'bounds_m':[np.min(v,axis=0).tolist(),np.max(v,axis=0).tolist()],'bounds_note':'Values above in mm; exported OBJ units are meters','materials':1,'atlas_pixels':[N,N],'back_normal':[0,1,0],'arrow_direction':[0,0,-1],'led_button_direction':[0,0,-1],'usb_direction':[-1,0,0],'seam_y_m':.00076,'logo_side_to_back_side_ratio':'5.5 : 4.5','geometry_modified':False,'runtime_test':'NOT RUN; assets only, no registration or installation'}
r['bounds_mm']=r.pop('bounds_m');r.pop('bounds_note')
(out/'color_validation.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
