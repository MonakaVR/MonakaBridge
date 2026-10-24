import bpy,pathlib,json
D=pathlib.Path(__file__).parent
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.wm.stl_import(filepath=str(D/'PICO_Tracker_Detailed_APPROX_Rev3_mm.stl'))
o=bpy.context.object;o.name='PICO_Tracker_APPROX';o.scale=(.001,.001,.001)
bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
# CAD Z axis -> provisional OpenVR Y axis. This is NOT calibrated tracker alignment.
o.rotation_euler=(1.5707963267948966,0,0);bpy.ops.object.transform_apply(location=False,rotation=True,scale=False)
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project();bpy.ops.object.mode_set(mode='OBJECT')
m=bpy.data.materials.new('approx_white');m.use_nodes=True
img=bpy.data.images.new('approx_white',width=16,height=16);img.generated_color=(.8,.8,.8,1);img.filepath_raw=str(D/'approx_white.png');img.file_format='PNG';img.save()
t=m.node_tree.nodes.new('ShaderNodeTexImage');t.image=img;m.node_tree.links.new(t.outputs['Color'],m.node_tree.nodes.get('Principled BSDF').inputs['Base Color']);o.data.materials.clear();o.data.materials.append(m)
bpy.ops.wm.obj_export(filepath=str(D/'PICO_Tracker_Detailed_APPROX_Rev3_m.obj'),export_selected_objects=True,forward_axis='Y',up_axis='Z',export_triangulated_mesh=True,path_mode='STRIP')
(D/'steamvr_mesh_validation.json').write_text(json.dumps({'vertices':len(o.data.vertices),'faces':len(o.data.polygons),'uv_layers':len(o.data.uv_layers),'materials':len(o.data.materials),'objects':1,'units':'meters','alignment':'provisional, not tracking-origin calibrated','steamvr_runtime_test':'NOT_RUN'},indent=2))
