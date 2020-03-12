import bpy
import bmesh
import sys
import zipfile
import os

# Setting the right atlas and lightmappare values
file_name = sys.argv[2]
atlas_size = int(sys.argv[6])
lightmapper_samples = int(sys.argv[7])
bpy.data.scenes["Scene"].cycles.samples = lightmapper_samples
bpy.data.scenes["Scene"].cycles.use_square_samples = False

# Starting the bake process
print("# Starting bake process")
for ob in bpy.data.objects:
    if ob.type == 'MESH':
        ob.select = True
        bpy.context.scene.objects.active = ob
    else:
        ob.select = False

# Joining Meshes
print("# Joining meshes")
bpy.ops.object.join()

# Creating texture atlas
print("# Creating texture atlas")
atlas = bpy.data.images.new("atlas", atlas_size, atlas_size)

# Switching to Cycled rendering engine
print("# Switching to Cycles rendering engine")
bpy.context.scene.render.engine = 'CYCLES'

# Getting all materials in the scene
print("# Getting all materials")
selected_mesh = bpy.context.selected_objects
all_mat = bpy.data.materials
all_mat_count = len(all_mat)
print("# Found " + str(all_mat_count) + " materials to bake")

# Generating Lightmap UV on channel 2
print ("# Generating Lightmap UV on channel 2")
#bpy.ops.mesh.uv_texture_add()
lm = selected_mesh[0].data.uv_textures.new("lightmap")
lm.active = True

# Unwrapping LightmapUVs using Lightmap pack
print ("# Unwrapping LightmapUVs using Lightmap pack")
bpy.ops.object.editmode_toggle()
bm = bmesh.from_edit_mesh(selected_mesh[0].data)
for f in bm.faces:
    f.select = True
bpy.ops.uv.lightmap_pack(
    PREF_IMG_PX_SIZE=2048,
    PREF_BOX_DIV=48,
    PREF_MARGIN_DIV=0.3
)
bpy.ops.object.editmode_toggle()

# Switching to nodes
print ("# Switching materials to nodes")
for mat in all_mat:
    mat.use_nodes = True
    matnodes = mat.node_tree.nodes
    #new texture
    tex = matnodes.new('ShaderNodeTexImage')
    tex.image = atlas
    tex.select = True
    mat.node_tree.nodes.active = tex

# Starting bake
print("# Starting bake")
bpy.ops.object.bake(type='COMBINED')

print("# Packing and saving image")
atlas.pack(as_png=True)
atlas_name = "".join(["./tmp/", os.path.basename(file_name), ".png"])
atlas.filepath = atlas_name
atlas.file_format = "PNG"
atlas.save()

print("# Saving blend file as new one")
zip_file_name = "".join(["./tmp/", os.path.basename(file_name), ".zip"])
file_name = "".join(["./tmp/", os.path.basename(file_name)])
bpy.ops.wm.save_as_mainfile(filepath=file_name)

# Compressing the files after the bake
zip_file = zipfile.ZipFile(zip_file_name, "w")
zip_file.write(file_name, compress_type=zipfile.ZIP_DEFLATED)
zip_file.write(atlas_name, compress_type=zipfile.ZIP_DEFLATED)
zip_file.close()

# Removing temp files
os.remove(file_name)
os.remove(atlas_name)