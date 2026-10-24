"""Deterministic model-only preparation; never contacts a runtime or changes Pose."""
from pathlib import Path
import hashlib,json,math
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'assets/pico-rev3-identification-source/resources/rendermodels/pico_tracker_rev3'
OUT=ROOT/'outputs/steamvr/package/resources/rendermodels'
PROD='pico_tracker_rev3_identification';DEBUG='pico_tracker_rev3_axis_debug'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,t):p.write_text(t,encoding='utf-8',newline='\n')
def jsonwrite(p,v):write(p,json.dumps(v,indent=2)+'\n')
raw=(SOURCE/'pico_tracker_rev3.obj').read_text().splitlines()
vs=[l for l in raw if l.startswith('v ')];ns=[l for l in raw if l.startswith('vn ')]
old_uv=[tuple(map(float,l.split()[1:])) for l in raw if l.startswith('vt ')]
uv=[];lookup={};faces=[]
for l in raw:
 if l.startswith('f '):
  face=[]
  for s in l.split()[1:]:
   v,t,n=map(int,s.split('/'));value=old_uv[t-1]
   if value not in lookup:lookup[value]=len(uv)+1;uv.append(value)
   face.append((v,lookup[value],n))
  faces.append(face)
def export(model,vertices,normals,texture,triangles,png):
 directory=OUT/model;directory.mkdir(parents=True,exist_ok=True)
 text=['# Identification geometry preserved; no model-local rotation.','mtllib '+model+'.mtl','o '+model]+vertices+normals
 text+=['vt %.12g %.12g'%t for t in texture]+['usemtl identification','s 1']
 text+=['f '+' '.join('/'.join(map(str,c)) for c in f) for f in triangles]
 write(directory/(model+'.obj'),'\n'.join(text)+'\n')
 write(directory/(model+'.mtl'),'newmtl identification\nKa 0 0 0\nKd 1 1 1\nKs 0 0 0\nd 1\nillum 1\nmap_Kd '+model+'.png\n')
 (directory/(model+'.png')).write_bytes(png)
 manifest=dict(model=model,property='{monaka_bridge}'+model,files={model+ext:sha(directory/(model+ext)) for ext in ['.obj','.mtl','.png']},source_step_sha256='436509dd665aeca53d80fa33c35615d4d35834073a9481dcc876447dc65d55cd')
 jsonwrite(directory/'assets-manifest.json',manifest)
 return dict(vertices=len(vertices),normals=len(normals),uv=len(texture),triangles=len(triangles),expanded_vertices=len({c for f in triangles for c in f}),manifest_sha256=sha(directory/'assets-manifest.json'))
production=export(PROD,vs,ns,uv,faces,(SOURCE/'pico_rev3_identification_atlas.png').read_bytes())
# Preserve the supplied atlas in the upper half of a new diagnostic atlas.
# OpenVR +Z is backward; the requested +Z / FORWARD text is only a local
# diagnostic axis name, explicitly documented, never a tracking convention.
atlas=Image.new('RGB',(2048,4096),(18,22,30));atlas.paste(Image.open(SOURCE/'pico_rev3_identification_atlas.png'),(0,0))
draw=ImageDraw.Draw(atlas);font=ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf',74)
labels=[('+X / RIGHT',(230,45,45)),('+Y / UP',(35,200,75)),('+Z / FORWARD',(45,125,255)),('FRONT: LOGO -Y',(245,185,35)),('TOP: BACK +Y',(40,210,210)),('USB-C: -X',(210,90,230))]
regions=[]
for i,(label,color) in enumerate(labels):
 y=2110+i*260;draw.rounded_rectangle((64,y,1984,y+210),radius=24,fill=color)
 draw.text((110,y+55),label,font=font,fill=(8,12,19))
 regions.append([(64/2048,1-(y+210)/4096),(1984/2048,1-(y+210)/4096),(1984/2048,1-y/4096),(64/2048,1-y/4096)])
vertices=list(vs);normals=list(ns);texture=[(u,.5+.5*v) for u,v in uv];triangles=list(faces)
def sub(a,b):return [a[i]-b[i] for i in range(3)]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def add_triangle(points,coords):
 n=cross(sub(points[1],points[0]),sub(points[2],points[0]));length=math.sqrt(sum(x*x for x in n));assert length>1e-12
 normals.append('vn '+' '.join('%.12g'%(x/length) for x in n));ni=len(normals);corners=[]
 for p,t in zip(points,coords):
  vertices.append('v '+' '.join('%.12g'%x for x in p));texture.append(t);corners.append((len(vertices),len(texture),ni))
 triangles.append(corners)
def quad(points,coords):
 for ids in [(0,1,2),(0,2,3)]:add_triangle([points[i] for i in ids],[coords[i] for i in ids])
def plate(center,right,up,width,height,index):
 points=[[center[j]+sx*right[j]*width/2+sy*up[j]*height/2 for j in range(3)] for sx,sy in [(-1,-1),(1,-1),(1,1),(-1,1)]]
 coords=regions[index];normal=cross(right,up)
 # Separate front/back faces to avoid coincident surfaces and self-shadowing.
 front=[[p[j]+normal[j]*.0001 for j in range(3)] for p in points]
 back=[[points[i][j]-normal[j]*.0001 for j in range(3)] for i in [1,0,3,2]]
 quad(front,coords);quad(back,coords)
markers=[]
for axis in range(3):
 unit=[int(i==axis) for i in range(3)];u=[int(i==(axis+1)%3) for i in range(3)];v=cross(unit,u)
 color_uv=(.05,regions[axis][0][1]+.02)
 def ring(d,rad):return [[d*unit[j]+rad*(math.cos(t)*u[j]+math.sin(t)*v[j]) for j in range(3)] for t in [i*math.pi/4 for i in range(8)]]
 low=ring(.018,.0018);high=ring(.053,.0018);base=ring(.051,.006);tip=[.066*x for x in unit]
 for i in range(8):
  k=(i+1)%8;quad([low[i],low[k],high[k],high[i]],[color_uv]*4)
  add_triangle([base[i],base[k],tip],[color_uv]*3)
  add_triangle([[.051*x for x in unit],base[k],base[i]],[color_uv]*3)
 markers.append(dict(axis=['+X','+Y','+Z'][axis],direction=unit,length_m=.066,label=labels[axis][0]))
plate([.076,.02,0],[1,0,0],[0,1,0],.047,.012,0)
plate([0,.081,0],[1,0,0],[0,1,0],.047,.012,1)
plate([0,0,.081],[1,0,0],[0,0,1],.055,.012,2)
plate([0,-.012,0],[1,0,0],[0,0,1],.032,.008,3)
plate([0,.012,0],[-1,0,0],[0,0,1],.032,.008,4)
plate([-.047,0,0],[0,0,1],[0,1,0],.035,.009,5)
import io
buffer=io.BytesIO();atlas.save(buffer,format='PNG')
diagnostic=export(DEBUG,vertices,normals,texture,triangles,buffer.getvalue())
assert production['expanded_vertices']<65000 and diagnostic['expanded_vertices']<65000
jsonwrite(ROOT/'assets/pico-rev3-identification-source/preparation-manifest.json',dict(
 source_obj_sha256=sha(SOURCE/'pico_tracker_rev3.obj'),source_atlas_sha256=sha(SOURCE/'pico_rev3_identification_atlas.png'),
 production=production,diagnostic=diagnostic,markers=markers,local_correction='NONE',
 transformation='Exact duplicate UV entries merged; all original positions, normals, ordered faces and sampled UV coordinates preserved.',
 feature_directions=dict(logo=[0,-1,0],back=[0,1,0],LED_button_arrow=[0,0,-1],USB=[-1,0,0]),
 label_semantics='FRONT names the supplied logo face (-Y). TOP names local +Y/back, not a physical tracking axis. +Z / FORWARD is the requested diagnostic label; OpenVR forward remains -Z.',
 units='metres',tracking_pose_changed=False))
print(json.dumps(dict(production=production,diagnostic=diagnostic)))
