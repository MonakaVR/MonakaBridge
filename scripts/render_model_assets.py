"""Offline Rev3 mesh/package validation. No SteamVR or device access."""
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib
from itertools import product

MODEL = 'pico_motion_tracker'
PROPERTY = '{monaka_bridge}pico_motion_tracker'
STEP_SHA = '436509dd665aeca53d80fa33c35615d4d35834073a9481dcc876447dc65d55cd'
NAMES = {MODEL+'.obj', MODEL+'.mtl', MODEL+'.png'}

def require(ok, why):
    if not ok:
        raise ValueError(why)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])

def mesh(path,model=MODEL,material="approx_white",expected_extent=(.0385,.0152,.0385)):
    vertices=[];uv=[];normals=[];faces=[];objects=[];libs=[];materials=[]
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        row=line.split()
        if not row or row[0].startswith('#'):continue
        kind,values=row[0],row[1:]
        if kind in ('v','vn','vt'):
            require(len(values)==(2 if kind=='vt' else 3),'vector dimension')
            value=tuple(map(float,values));require(all(math.isfinite(x) for x in value),'nonfinite vector')
            {'v':vertices,'vn':normals,'vt':uv}[kind].append(value)
        elif kind=='f':
            require(len(values)==3,'triangles required')
            face=[]
            for value in values:
                indexes=value.split('/');require(len(indexes)==3 and all(indexes),'UV/normal index required')
                indexes=tuple(map(int,indexes));require(all(x>0 for x in indexes),'positive indices required')
                face.append(indexes)
            faces.append(face)
        elif kind=='o':objects.append(' '.join(values))
        elif kind=='mtllib':libs+=values
        elif kind=='usemtl':materials+=values
        else:require(kind=='s','unsupported OBJ statement '+kind)
    require(len(objects)==1 and objects[0] and vertices and faces,'one nonempty object required')
    require(len(vertices)<65000 and uv and normals,'vertex or UV/normal constraints')
    require(libs==[model+'.mtl'] and set(materials)=={material},'OBJ material reference')
    corners=set();triangles=[]
    for face in faces:
        for vi,ti,ni in face:
            require(vi<=len(vertices) and ti<=len(uv) and ni<=len(normals),'index out of range')
            corners.add((vi,ti,ni))
        a,b,c=[vertices[i[0]-1] for i in face]
        n=cross(tuple(b[i]-a[i] for i in range(3)),tuple(c[i]-a[i] for i in range(3)))
        length=math.sqrt(sum(x*x for x in n));require(length>1e-18,'degenerate triangle')
        avg=tuple(sum(normals[corner[2]-1][i] for corner in face) for i in range(3))
        require(sum(avg[i]*n[i] for i in range(3))>=-1e-14,'normal/winding mismatch')
        triangles.append((a,b,c))
    require(len(corners)<65000,'expanded corner vertex limit')
    for n in normals:require(abs(sum(x*x for x in n)-1)<.002,'unit normal required')
    lo=[min(v[i] for v in vertices) for i in range(3)]
    hi=[max(v[i] for v in vertices) for i in range(3)]
    extent=[hi[i]-lo[i] for i in range(3)]
    if expected_extent is not None:require(all(abs(a-b)<=2e-6 for a,b in zip(extent,expected_extent)),'mm/m or axis/bbox mismatch')
    return dict(vertices=len(vertices),uv=len(uv),normals=len(normals),faces=len(faces),
                expanded_vertices=len(corners),bounds_m=dict(min=lo,max=hi,extent=extent)),triangles

def png(path):
    data=Path(path).read_bytes();require(data[:8]==b'\x89PNG\r\n\x1a\n','PNG signature')
    offset=8;parts=[];dims=None;ended=False
    while offset<len(data):
        require(offset+12<=len(data),'PNG chunk truncated')
        size=struct.unpack('>I',data[offset:offset+4])[0];kind=data[offset+4:offset+8]
        body=data[offset+8:offset+8+size]
        require(len(body)==size and offset+12+size<=len(data),'PNG chunk length')
        crc=struct.unpack('>I',data[offset+8+size:offset+12+size])[0]
        require(zlib.crc32(kind+body)&0xffffffff==crc,'PNG CRC')
        if kind==b'IHDR':
            require(size==13,'PNG header');w,h,depth,color,comp,filt,interlace=struct.unpack('>IIBBBBB',body)
            require(0<w<=4096 and 0<h<=4096 and depth==8 and color in (2,6) and comp==filt==interlace==0,'PNG format')
            dims=(w,h,3 if color==2 else 4)
        elif kind==b'IDAT':parts.append(body)
        elif kind==b'IEND':require(size==0,'PNG end');ended=True
        offset+=12+size
    require(ended and dims and parts,'PNG closure')
    decoded=zlib.decompress(b''.join(parts));require(len(decoded)==dims[1]*(1+dims[0]*dims[2]),'PNG decoded length')
    return dims[:2]

def validate(directory):
    directory=Path(directory)
    manifest=json.loads((directory/'assets-manifest.json').read_text())
    require(set(manifest)=={'model','property','files','source_step_sha256'},'asset manifest keys')
    require(manifest['model']==MODEL and manifest['property']==PROPERTY and manifest['source_step_sha256']==STEP_SHA,'model identity')
    require(set(manifest['files'])==NAMES,'asset inventory')
    for name,value in manifest['files'].items():
        p=directory/name
        require(p.resolve().parent==directory.resolve() and p.is_file(),'asset path containment')
        require(sha(p)==value,'asset hash '+name)
    material=(directory/(MODEL+'.mtl')).read_text()
    maps=[line.split()[1:] for line in material.splitlines() if line.startswith('map_Kd ')]
    require(maps==[[MODEL+'.png']],'texture reference closure')
    result,_=mesh(directory/(MODEL+'.obj'))
    result['texture_size']=png(directory/(MODEL+'.png'))
    return result

def compare_stl(stl, triangles):
    """Compare the reused OBJ to its pinned CAD tessellation, including winding."""
    data=Path(stl).read_bytes();count=struct.unpack('<I',data[80:84])[0]
    require(len(data)==84+count*50,'binary STL length')
    def key(triangle):
        v=[tuple(round(x,6) for x in point) for point in triangle]
        return min(tuple(v),tuple(v[1:]+v[:1]),tuple(v[2:]+v[:2]))
    # Blender applies float transforms before its six-decimal OBJ writer. A
    # nearest-vertex check avoids decimal tie-rounding differences while still
    # bounding the actual export error to one micrometre in model space.
    grid={};epsilon=1e-6
    for triangle in triangles:
        for point in triangle:
            cell=tuple(math.floor(x/epsilon) for x in point)
            grid.setdefault(cell,set()).add(point)
    max_error=0
    def nearest(point):
        nonlocal max_error
        cell=tuple(math.floor(x/epsilon) for x in point);candidates=[]
        for delta in product((-1,0,1),repeat=3):
            candidates.extend(grid.get(tuple(cell[i]+delta[i] for i in range(3)),()))
        require(bool(candidates),'STL vertex missing in OBJ')
        closest=min(candidates,key=lambda v:sum((v[i]-point[i])**2 for i in range(3)))
        error=math.sqrt(sum((closest[i]-point[i])**2 for i in range(3)))
        require(error<=epsilon,'STL/OBJ vertex distance exceeds 1um');max_error=max(max_error,error)
        return closest
    expected=Counter();collapsed=0
    for offset in range(84,len(data),50):
        row=struct.unpack('<12fH',data[offset:offset+50]);triangle=[]
        for j in (3,6,9):
            x,y,z=row[j:j+3];triangle.append((x*.001,-z*.001,y*.001))
        triangle=key([nearest(point) for point in triangle])
        a,b,c=triangle
        n=cross(tuple(b[i]-a[i] for i in range(3)),tuple(c[i]-a[i] for i in range(3)))
        if math.sqrt(sum(x*x for x in n))<=1e-18:
            collapsed+=1
        else:expected[triangle]+=1
    actual=Counter(key(t) for t in triangles)
    require(expected==actual,'CAD tessellation/OBJ geometry or winding mismatch')
    return dict(original_triangles=count,retained_triangles=len(triangles),quantized_zero_area_removed=collapsed,
                transform_mm_to_m=[[.001,0,0],[0,0,-.001],[0,.001,0]],
                position_quantization_m=1e-6,max_vertex_distance_m=max_error,geometry_and_winding='MATCH')
