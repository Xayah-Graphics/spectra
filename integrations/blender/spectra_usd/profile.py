from __future__ import annotations

from array import array
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path

import bpy
from mathutils import Matrix
from pxr import Gf, Sdf, Tf, Usd, UsdGeom, UsdLux, UsdRender, UsdShade, Vt


PROFILE_VERSION = 2
import_requested = False
export_requested = False
last_error: str | None = None


MATERIAL_KINDS = {
    "SpectraDiffuse": "DIFFUSE",
    "SpectraDiffuseTransmission": "DIFFUSE_TRANSMISSION",
    "SpectraConductor": "CONDUCTOR",
    "SpectraDielectric": "DIELECTRIC",
    "SpectraThinDielectric": "THIN_DIELECTRIC",
    "SpectraCoatedDiffuse": "COATED_DIFFUSE",
    "SpectraCoatedConductor": "COATED_CONDUCTOR",
    "SpectraMix": "MIX",
    "SpectraInterface": "INTERFACE",
}


@dataclass(slots=True)
class PrimitiveExport:
    blender_object: bpy.types.Object
    mesh_prim: Usd.Prim
    transform: Gf.Matrix4d


@dataclass(slots=True)
class PrototypeExport:
    key: int
    name: str
    revision_content: int
    revision_topology: int
    primitives: list[PrimitiveExport]


@dataclass(slots=True)
class InstanceExport:
    blender_object: bpy.types.Object
    usd_prim: Usd.Prim
    prototype: PrototypeExport


def _attribute(prim: Usd.Prim, name: str):
    return prim.GetAttribute(name).Get()


def _relationship_target(prim: Usd.Prim, name: str) -> Sdf.Path | None:
    targets = prim.GetRelationship(name).GetTargets()
    return targets[0] if targets else None


def _set_attribute(prim: Usd.Prim, name: str, value_type: Sdf.ValueTypeName, value) -> None:
    prim.CreateAttribute(name, value_type, custom=True).Set(value)


def _set_relationship(prim: Usd.Prim, name: str, target: Sdf.Path) -> None:
    prim.CreateRelationship(name, custom=True).SetTargets([target])


def _set_name(prim: Usd.Prim, name: str) -> None:
    prim.SetDisplayName(name)
    _set_attribute(prim, "spectra:name", Sdf.ValueTypeNames.String, name)


def _set_revision(prim: Usd.Prim, content: int, topology: int) -> None:
    _set_attribute(prim, "spectra:revision:content", Sdf.ValueTypeNames.UInt64, content)
    _set_attribute(prim, "spectra:revision:topology", Sdf.ValueTypeNames.UInt64, topology)


def _display_name(prim: Usd.Prim) -> str:
    value = _attribute(prim, "spectra:name")
    return value if value else prim.GetDisplayName() or prim.GetName()


def _identifier(name: str, default_identifier: str, used: set[str]) -> str:
    identifier = Tf.MakeValidIdentifier(name)
    if not identifier or identifier == "_":
        identifier = default_identifier
    base = identifier
    suffix = 2
    while identifier in used:
        identifier = f"{base}_{suffix}"
        suffix += 1
    used.add(identifier)
    return identifier


def _blender_id(prim_map: dict, path: Sdf.Path, expected_type: type):
    return next((value for value in prim_map.get(path, ()) if isinstance(value, expected_type)), None)


def _requires_preserved_profile(stage: Usd.Stage) -> bool:
    for material in stage.GetPrimAtPath("/Spectra/Materials").GetChildren():
        shader = material.GetChild("SpectraSurface")
        if str(_attribute(shader, "info:id")) != "SpectraDiffuse":
            return True
    for texture in stage.GetPrimAtPath("/Spectra/Textures").GetChildren():
        if str(_attribute(texture, "info:id")) != "SpectraConstantTexture":
            return True
    for geometry in stage.GetPrimAtPath("/Spectra/Geometry").GetChildren():
        if str(_attribute(geometry, "spectra:geometryType")) != "triangleMesh":
            return True
    if stage.GetPrimAtPath("/World/Lights").GetChildren() or stage.GetPrimAtPath("/World/Volumes").GetChildren() or stage.GetPrimAtPath("/Spectra/Media").GetChildren():
        return True
    for prototype in stage.GetPrimAtPath("/Spectra/Prototypes").GetChildren():
        for primitive in prototype.GetAllChildren():
            if any(relationship.GetName().startswith("spectra:faceMaterial:") for relationship in primitive.GetRelationships()):
                return True
            area_light_path = _relationship_target(primitive, "spectra:areaLight")
            if area_light_path and stage.GetPrimAtPath(area_light_path).HasRelationship("spectra:emissionTexture"):
                return True
    return False


def _property_group_fingerprint(digest, settings, excluded: set[str]) -> None:
    for prop in settings.bl_rna.properties:
        if prop.identifier == "rna_type" or prop.identifier in excluded:
            continue
        digest.update(prop.identifier.encode())
        digest.update(repr(getattr(settings, prop.identifier)).encode())


def scene_fingerprint(scene: bpy.types.Scene) -> str:
    digest = hashlib.sha256()
    digest.update(scene.camera.name.encode() if scene.camera else b"")
    digest.update(repr((scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage)).encode())
    _property_group_fingerprint(digest, scene.spectra_usd, {"source_path", "source_layer_text", "source_fingerprint"})
    for blender_object in sorted(scene.objects, key=lambda value: value.name_full):
        digest.update(repr((blender_object.name_full, blender_object.type, blender_object.parent.name_full if blender_object.parent else "", blender_object.instance_collection.name_full if blender_object.instance_collection else "", blender_object.hide_render, blender_object.hide_viewport)).encode())
        digest.update(array("d", (value for row in blender_object.matrix_world for value in row)).tobytes())
        _property_group_fingerprint(digest, blender_object.spectra_usd, set())
        if blender_object.type == "MESH":
            mesh = blender_object.data
            values = array("f", [0.0]) * (len(mesh.vertices) * 3)
            mesh.vertices.foreach_get("co", values)
            digest.update(values.tobytes())
            indices = array("i", [0]) * len(mesh.loops)
            mesh.loops.foreach_get("vertex_index", indices)
            digest.update(indices.tobytes())
            polygon_data = array("i", [0]) * (len(mesh.polygons) * 3)
            for index, polygon in enumerate(mesh.polygons):
                polygon_data[index * 3:index * 3 + 3] = array("i", (polygon.loop_start, polygon.loop_total, polygon.material_index))
            digest.update(polygon_data.tobytes())
            for uv_layer in mesh.uv_layers:
                digest.update(uv_layer.name.encode())
                coordinates = array("f", [0.0]) * (len(uv_layer.data) * 2)
                uv_layer.data.foreach_get("uv", coordinates)
                digest.update(coordinates.tobytes())
            digest.update(repr(tuple((slot.link, slot.material.name_full if slot.material else "") for slot in blender_object.material_slots)).encode())
            _property_group_fingerprint(digest, mesh.spectra_usd, set())
        elif blender_object.type == "CAMERA":
            camera = blender_object.data
            digest.update(repr((camera.type, camera.lens, camera.sensor_width, camera.sensor_height, camera.clip_start, camera.clip_end, camera.dof.focus_distance, camera.dof.aperture_fstop)).encode())
            _property_group_fingerprint(digest, camera.spectra_usd, set())
        elif blender_object.type == "LIGHT":
            light = blender_object.data
            digest.update(repr((light.type, tuple(light.color), light.energy, getattr(light, "shape", ""), getattr(light, "size", 0.0), getattr(light, "size_y", 0.0), getattr(light, "spot_size", 0.0), getattr(light, "spot_blend", 0.0))).encode())
        elif blender_object.type == "VOLUME":
            filepath = blender_object.data.filepath
            resolved = Path(bpy.path.abspath(filepath)).resolve() if filepath else None
            digest.update((str(resolved) if resolved and resolved != Path(resolved.anchor) else "").encode())
    for material in sorted((value for value in bpy.data.materials if value.users or value.spectra_usd.material), key=lambda value: value.name_full):
        digest.update(material.name_full.encode())
        _property_group_fingerprint(digest, material.spectra_usd, set())
        if not material.node_tree:
            continue
        for node in sorted(material.node_tree.nodes, key=lambda value: value.name):
            digest.update(repr((node.name, node.bl_idname)).encode())
            if node.type == "TEX_IMAGE":
                digest.update((str(Path(bpy.path.abspath(node.image.filepath)).resolve()) if node.image else "").encode())
            for socket in node.inputs:
                if hasattr(socket, "default_value"):
                    digest.update(repr(socket.default_value[:] if hasattr(socket.default_value, "__len__") else socket.default_value).encode())
        for link in sorted(material.node_tree.links, key=lambda value: (value.from_node.name, value.from_socket.name, value.to_node.name, value.to_socket.name)):
            digest.update(repr((link.from_node.name, link.from_socket.name, link.to_node.name, link.to_socket.name)).encode())
    return digest.hexdigest()


def _archive_source_layer(stage: Usd.Stage, scene: bpy.types.Scene) -> None:
    name = f"Spectra USD — {scene.spectra_usd.scene_name}"
    if name in bpy.data.texts:
        bpy.data.texts.remove(bpy.data.texts[name])
    text = bpy.data.texts.new(name)
    text.write(stage.GetRootLayer().ExportToString())
    scene.spectra_usd.source_layer_text = text.name


def validate_preserved_export(scene: bpy.types.Scene, output_path: str | Path) -> bpy.types.Text:
    if scene_fingerprint(scene) != scene.spectra_usd.source_fingerprint:
        raise RuntimeError("This preserved Spectra profile was modified in Blender; the changed advanced feature set cannot yet be exported without losing semantics")
    source_directory = Path(scene.spectra_usd.source_path).resolve().parent
    output_directory = Path(output_path).resolve().parent
    if output_directory != source_directory:
        raise RuntimeError(f"Preserved Spectra profiles must be exported beside their asset directory: {source_directory}")
    text = bpy.data.texts.get(scene.spectra_usd.source_layer_text)
    if text is None:
        raise RuntimeError("The preserved Spectra source layer is missing from the Blender file")
    return text


def _export_preserved_profile(stage: Usd.Stage, scene: bpy.types.Scene) -> bool:
    text = validate_preserved_export(scene, stage.GetRootLayer().realPath)
    if not stage.GetRootLayer().ImportFromString(text.as_string()):
        raise RuntimeError("The preserved Spectra source layer could not be restored")
    return True


def _principled(material: bpy.types.Material) -> bpy.types.ShaderNodeBsdfPrincipled:
    material.use_nodes = True
    nodes = material.node_tree.nodes
    principled = next((node for node in nodes if node.type == "BSDF_PRINCIPLED"), None)
    if principled:
        return principled
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    material.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    return principled


def _set_preview_material(material: bpy.types.Material, color: tuple[float, float, float]) -> None:
    principled = _principled(material)
    principled.inputs["Base Color"].default_value = (*color, 1.0)
    principled.inputs["Metallic"].default_value = 0.0
    principled.inputs["Roughness"].default_value = 0.5
    principled.inputs["Transmission Weight"].default_value = 0.0
    principled.inputs["Alpha"].default_value = 1.0
    material.diffuse_color = (*color, 1.0)


def _set_emissive_preview(material: bpy.types.Material, radiance: tuple[float, float, float], scale: float) -> None:
    principled = _principled(material)
    maximum = max(radiance)
    color = tuple(component / maximum for component in radiance) if maximum else (0.0, 0.0, 0.0)
    principled.inputs["Emission Color"].default_value = (*color, 1.0)
    principled.inputs["Emission Strength"].default_value = maximum * scale


def _image_texture_node(stage: Usd.Stage, material: bpy.types.Material, texture: Usd.Prim, non_color: bool) -> bpy.types.ShaderNodeTexImage:
    asset = _attribute(texture, "inputs:file")
    resolved = Path(asset.resolvedPath) if asset.resolvedPath else Path(stage.GetRootLayer().realPath).parent / asset.path
    source_color_space = str(_attribute(texture, "inputs:sourceColorSpace"))
    color_space = "Non-Color" if non_color else {"sRGB": "sRGB", "linear": "Linear Rec.709", "Rec2020": "Rec.2020", "ACES2065-1": "ACES2065-1"}[source_color_space]
    resolved = resolved.resolve()
    image = next((candidate for candidate in bpy.data.images if candidate.filepath and Path(bpy.path.abspath(candidate.filepath)).resolve() == resolved and candidate.colorspace_settings.name == color_space), None)
    if image is None:
        image = bpy.data.images.load(str(resolved), check_existing=False)
        image.colorspace_settings.name = color_space
    node = material.node_tree.nodes.new("ShaderNodeTexImage")
    node.name = _display_name(texture)
    node.label = _display_name(texture)
    node.image = image
    wrap = str(_attribute(texture, "spectra:wrap"))
    node.extension = {"repeat": "REPEAT", "clamp": "EXTEND", "black": "CLIP"}[wrap]
    coordinate = material.node_tree.nodes.new("ShaderNodeTexCoord")
    mapping = material.node_tree.nodes.new("ShaderNodeMapping")
    mapping_type = str(_attribute(texture, "spectra:mapping:type"))
    if mapping_type == "uv":
        scale = _attribute(texture, "spectra:mapping:scale")
        offset = _attribute(texture, "spectra:mapping:offset")
        mapping.inputs["Scale"].default_value = (float(scale[0]), float(scale[1]), 1.0)
        mapping.inputs["Location"].default_value = (float(offset[0]), float(offset[1]), 0.0)
        material.node_tree.links.new(coordinate.outputs["UV"], mapping.inputs["Vector"])
    else:
        node.projection = {"planar": "FLAT", "spherical": "SPHERE", "cylindrical": "TUBE"}[mapping_type]
        material.node_tree.links.new(coordinate.outputs["Generated"], mapping.inputs["Vector"])
    material.node_tree.links.new(mapping.outputs["Vector"], node.inputs["Vector"])
    return node


def _set_spectra_preview(stage: Usd.Stage, material: bpy.types.Material, shader: Usd.Prim, shader_id: str, color: tuple[float, float, float]) -> None:
    _set_preview_material(material, color)
    principled = _principled(material)
    if shader_id == "SpectraConductor":
        principled.inputs["Metallic"].default_value = 1.0
        principled.inputs["Roughness"].default_value = float(_attribute(shader, "spectra:distribution:roughness:value"))
    elif shader_id == "SpectraDiffuseTransmission":
        principled.inputs["Transmission Weight"].default_value = 0.5
    elif shader_id in {"SpectraDielectric", "SpectraThinDielectric"}:
        principled.inputs["Transmission Weight"].default_value = 1.0
        principled.inputs["IOR"].default_value = float(_attribute(shader, "spectra:eta:scalar"))
        if shader.HasAttribute("spectra:distribution:roughness:value"):
            principled.inputs["Roughness"].default_value = float(_attribute(shader, "spectra:distribution:roughness:value"))
    elif shader_id == "SpectraCoatedDiffuse":
        principled.inputs["Coat Weight"].default_value = 1.0
        principled.inputs["Coat Roughness"].default_value = float(_attribute(shader, "spectra:interface:roughness:value"))
        principled.inputs["Coat IOR"].default_value = float(_attribute(shader, "spectra:eta:scalar"))
    elif shader_id == "SpectraCoatedConductor":
        principled.inputs["Metallic"].default_value = 1.0
        principled.inputs["Roughness"].default_value = float(_attribute(shader, "spectra:conductor:roughness:value"))
        principled.inputs["Coat Weight"].default_value = 1.0
        principled.inputs["Coat Roughness"].default_value = float(_attribute(shader, "spectra:interface:roughness:value"))
    reflectance_path = _relationship_target(shader, "spectra:reflectance:texture")
    if reflectance_path:
        texture = stage.GetPrimAtPath(reflectance_path)
        if str(_attribute(texture, "info:id")) == "UsdUVTexture":
            image = _image_texture_node(stage, material, texture, False)
            material.node_tree.links.new(image.outputs["Color"], principled.inputs["Base Color"])
    bump_path = _relationship_target(shader, "spectra:bumpMap")
    if bump_path:
        texture = stage.GetPrimAtPath(bump_path)
        if str(_attribute(texture, "info:id")) == "UsdUVTexture":
            image = _image_texture_node(stage, material, texture, True)
            bump = material.node_tree.nodes.new("ShaderNodeBump")
            bump.inputs["Distance"].default_value = float(_attribute(texture, "spectra:scale"))
            material.node_tree.links.new(image.outputs["Color"], bump.inputs["Height"])
            material.node_tree.links.new(bump.outputs["Normal"], principled.inputs["Normal"])
    normal_path = _relationship_target(shader, "spectra:normalMap")
    if normal_path:
        texture = stage.GetPrimAtPath(normal_path)
        if str(_attribute(texture, "info:id")) == "UsdUVTexture":
            image = _image_texture_node(stage, material, texture, True)
            normal = material.node_tree.nodes.new("ShaderNodeNormalMap")
            material.node_tree.links.new(image.outputs["Color"], normal.inputs["Color"])
            material.node_tree.links.new(normal.outputs["Normal"], principled.inputs["Normal"])
    roughness_path = _relationship_target(shader, "spectra:distribution:roughness:texture")
    if roughness_path:
        texture = stage.GetPrimAtPath(roughness_path)
        if str(_attribute(texture, "info:id")) == "UsdUVTexture":
            image = _image_texture_node(stage, material, texture, True)
            material.node_tree.links.new(image.outputs["Color"], principled.inputs["Roughness"])
    if shader_id == "SpectraMix":
        first = stage.GetPrimAtPath(_relationship_target(shader, "spectra:first")).GetChild("PreviewSurface")
        second = stage.GetPrimAtPath(_relationship_target(shader, "spectra:second")).GetChild("PreviewSurface")
        mix = material.node_tree.nodes.new("ShaderNodeMixRGB")
        mix.inputs[1].default_value = (*tuple(float(component) for component in _attribute(first, "inputs:diffuseColor")), 1.0)
        mix.inputs[2].default_value = (*tuple(float(component) for component in _attribute(second, "inputs:diffuseColor")), 1.0)
        amount_path = _relationship_target(shader, "spectra:amount:texture")
        if amount_path:
            texture = stage.GetPrimAtPath(amount_path)
            if str(_attribute(texture, "info:id")) == "UsdUVTexture":
                image = _image_texture_node(stage, material, texture, True)
                material.node_tree.links.new(image.outputs["Color"], mix.inputs[0])
        else:
            mix.inputs[0].default_value = float(_attribute(shader, "spectra:amount:value"))
        material.node_tree.links.new(mix.outputs["Color"], principled.inputs["Base Color"])


def _import_materials(stage: Usd.Stage, prim_map: dict) -> dict[str, bpy.types.Material]:
    materials: dict[str, bpy.types.Material] = {}
    root = stage.GetPrimAtPath("/Spectra/Materials")
    for material_prim in root.GetChildren():
        path = str(material_prim.GetPath())
        material = _blender_id(prim_map, material_prim.GetPath(), bpy.types.Material)
        if material is None:
            material = bpy.data.materials.new(_display_name(material_prim))
        shader = material_prim.GetChild("SpectraSurface")
        shader_id = str(_attribute(shader, "info:id"))
        if shader_id not in MATERIAL_KINDS:
            raise RuntimeError(f"Unsupported Spectra material {path}: {shader_id}")
        preview = material_prim.GetChild("PreviewSurface")
        preview_color = _attribute(preview, "inputs:diffuseColor")
        value = tuple(float(component) for component in (_attribute(shader, "spectra:reflectance:value") if shader.HasAttribute("spectra:reflectance:value") else preview_color))
        settings = material.spectra_usd
        settings.material = True
        settings.source_path = path
        settings.display_name = _display_name(material_prim)
        settings.revision_content = int(_attribute(material_prim, "spectra:revision:content"))
        settings.revision_topology = int(_attribute(material_prim, "spectra:revision:topology"))
        settings.kind = MATERIAL_KINDS[shader_id]
        settings.shader_id = shader_id
        if shader.HasAttribute("spectra:reflectance:encoding"):
            settings.reflectance_encoding = str(_attribute(shader, "spectra:reflectance:encoding"))
            settings.reflectance_color_space = str(_attribute(shader, "spectra:reflectance:colorSpace"))
            settings.reflectance_scalar = float(_attribute(shader, "spectra:reflectance:scalar"))
        texture_path = _relationship_target(shader, "spectra:reflectance:texture")
        if texture_path:
            texture = stage.GetPrimAtPath(texture_path)
            settings.texture = True
            settings.texture_source_path = str(texture_path)
            settings.texture_name = _display_name(texture)
            settings.texture_revision_content = int(_attribute(texture, "spectra:revision:content"))
            settings.texture_revision_topology = int(_attribute(texture, "spectra:revision:topology"))
            settings.texture_type = str(_attribute(texture, "spectra:textureType"))
        _set_spectra_preview(stage, material, shader, shader_id, value)
        materials[path] = material
    return materials


def _assign_material(blender_object: bpy.types.Object, material: bpy.types.Material) -> None:
    mesh = blender_object.data
    if not mesh.materials:
        mesh.materials.append(material)
    while len(mesh.materials) > 1:
        mesh.materials.pop(index=len(mesh.materials) - 1)
    blender_object.material_slots[0].link = "OBJECT"
    blender_object.material_slots[0].material = material


def _import_instances(stage: Usd.Stage, prim_map: dict, materials: dict[str, bpy.types.Material]) -> tuple[list[bpy.types.Object], set[bpy.types.Collection]]:
    world = stage.GetPrimAtPath("/World")
    bpy.context.view_layer.update()
    geometry_meshes: dict[str, bpy.types.Mesh] = {}
    processed_prototypes: set[str] = set()
    instances: list[bpy.types.Object] = []
    prototype_collections: set[bpy.types.Collection] = set()
    preserved = bpy.context.scene.spectra_usd.profile_mode == "PRESERVED"
    instance_prims = [prim for prim in Usd.PrimRange(world, Usd.PrimAllPrimsPredicate) if _relationship_target(prim, "spectra:prototype")]
    imported = {str(prim.GetPath()): _blender_id(prim_map, prim.GetPath(), bpy.types.Object) for prim in instance_prims}
    for prim in instance_prims:
        prototype_path = _relationship_target(prim, "spectra:prototype")
        blender_object = imported[str(prim.GetPath())]
        if blender_object is None and str(_attribute(prim, "visibility")) == "invisible":
            visible = next((candidate for candidate in instance_prims if _relationship_target(candidate, "spectra:prototype") == prototype_path and imported[str(candidate.GetPath())] is not None), None)
            if visible:
                blender_object = bpy.data.objects.new(_display_name(prim), None)
                bpy.context.scene.collection.objects.link(blender_object)
                blender_object.instance_type = "COLLECTION"
                blender_object.instance_collection = imported[str(visible.GetPath())].instance_collection
                transform = UsdGeom.Xformable(prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
                matrix = Matrix(tuple(tuple(float(value) for value in row) for row in transform)).transposed()
                if UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y:
                    matrix = Matrix.Rotation(math.radians(90.0), 4, "X") @ matrix
                blender_object.matrix_world = matrix
                blender_object.hide_render = True
                blender_object.hide_viewport = True
        if blender_object is None:
            raise RuntimeError(f"Spectra instance {prim.GetPath()} was not imported as a Blender Object")
        prototype = stage.GetPrimAtPath(prototype_path)
        source_primitives = list(prototype.GetAllChildren())
        if blender_object.instance_collection is None:
            standard_mesh_prims = [candidate for candidate in Usd.PrimRange(prim) if UsdGeom.Mesh(candidate)]
            blender_primitives = [_blender_id(prim_map, candidate.GetPath(), bpy.types.Object) for candidate in standard_mesh_prims]
            if len(source_primitives) == 1 and len(standard_mesh_prims) == 1 and blender_object.type == "MESH" and blender_primitives == [None]:
                projected_mesh = blender_object
                owners = list(projected_mesh.users_collection)
                matrix = projected_mesh.matrix_world.copy()
                parent = projected_mesh.parent
                blender_object = bpy.data.objects.new(_display_name(prim), None)
                for owner in owners:
                    owner.objects.link(blender_object)
                blender_object.parent = parent
                blender_object.matrix_world = matrix
                blender_primitives = [projected_mesh]
            if len(source_primitives) != len(blender_primitives) or any(candidate is None for candidate in blender_primitives):
                raise RuntimeError(f"Unsupported Spectra instance {prim.GetPath()}: its standard projection does not match prototype {prototype_path}")
            collection = bpy.data.collections.new(_display_name(prototype))
            inverse_instance = blender_object.matrix_world.inverted()
            for candidate in blender_primitives:
                local_matrix = inverse_instance @ candidate.matrix_world
                candidate.parent = None
                for owner in list(candidate.users_collection):
                    owner.objects.unlink(candidate)
                collection.objects.link(candidate)
                candidate.matrix_world = local_matrix
            blender_object.instance_type = "COLLECTION"
            blender_object.instance_collection = collection
        else:
            collection = blender_object.instance_collection
        settings = blender_object.spectra_usd
        settings.role = "INSTANCE"
        settings.source_path = str(prim.GetPath())
        settings.prototype_path = str(prototype_path)
        settings.revision_content = int(_attribute(prim, "spectra:revision:content"))
        settings.revision_topology = int(_attribute(prim, "spectra:revision:topology"))
        instances.append(blender_object)
        prototype_collections.add(collection)
        if str(prototype_path) in processed_prototypes:
            continue
        processed_prototypes.add(str(prototype_path))
        collection.name = _display_name(prototype)
        collection_settings = collection.spectra_usd
        collection_settings.prototype = True
        collection_settings.source_path = str(prototype_path)
        collection_settings.display_name = _display_name(prototype)
        collection_settings.revision_content = int(_attribute(prototype, "spectra:revision:content"))
        collection_settings.revision_topology = int(_attribute(prototype, "spectra:revision:topology"))
        blender_primitives = [candidate for candidate in collection.all_objects if candidate.type == "MESH"]
        if len(source_primitives) != len(blender_primitives):
            raise RuntimeError(f"Unsupported Spectra prototype {prototype_path}: expected {len(source_primitives)} Blender mesh primitives, imported {len(blender_primitives)}")
        for index, (source, candidate) in enumerate(zip(source_primitives, blender_primitives, strict=True), 1):
            geometry_path = _relationship_target(source, "spectra:geometry")
            material_path = _relationship_target(source, "spectra:material")
            if not geometry_path or not material_path:
                raise RuntimeError(f"Unsupported Spectra primitive {source.GetPath()}: geometry and material are required by the Blender bridge")
            object_settings = candidate.spectra_usd
            object_settings.role = "PRIMITIVE"
            object_settings.source_path = str(source.GetPath())
            object_settings.geometry_path = str(geometry_path)
            object_settings.material_path = str(material_path)
            object_settings.reverse_orientation = bool(_attribute(source, "spectra:reverseOrientation"))
            candidate.name = f"{collection_settings.display_name} Primitive {index:03}"
            geometry = stage.GetPrimAtPath(geometry_path)
            if str(geometry_path) in geometry_meshes:
                previous = candidate.data
                candidate.data = geometry_meshes[str(geometry_path)]
                if previous.users == 0:
                    bpy.data.meshes.remove(previous)
            else:
                geometry_meshes[str(geometry_path)] = candidate.data
                candidate.data.name = _display_name(geometry)
                mesh_settings = candidate.data.spectra_usd
                mesh_settings.geometry = True
                mesh_settings.source_path = str(geometry_path)
                mesh_settings.display_name = _display_name(geometry)
                mesh_settings.revision_content = int(_attribute(geometry, "spectra:revision:content"))
                mesh_settings.revision_topology = int(_attribute(geometry, "spectra:revision:topology"))
            material = materials[str(material_path)]
            if not preserved or not candidate.data.materials:
                _assign_material(candidate, material)
            alpha_path = _relationship_target(source, "spectra:alpha")
            if preserved and alpha_path:
                texture = stage.GetPrimAtPath(alpha_path)
                preview_material = material.copy()
                preview_material.name = f"{material.name} — Alpha"
                _assign_material(candidate, preview_material)
                if str(_attribute(texture, "info:id")) == "UsdUVTexture":
                    image = _image_texture_node(stage, preview_material, texture, True)
                    preview_material.node_tree.links.new(image.outputs["Color"], _principled(preview_material).inputs["Alpha"])
                    preview_material.surface_render_method = "DITHERED"
            area_light_path = _relationship_target(source, "spectra:areaLight")
            if area_light_path:
                light = stage.GetPrimAtPath(area_light_path)
                if str(_attribute(light, "spectra:lightType")) != "diffuseArea":
                    raise RuntimeError(f"Unsupported Spectra area light {area_light_path}")
                object_settings.area_light = True
                object_settings.area_light_path = str(area_light_path)
                object_settings.area_light_name = _display_name(light)
                object_settings.area_light_revision_content = int(_attribute(light, "spectra:revision:content"))
                object_settings.area_light_revision_topology = int(_attribute(light, "spectra:revision:topology"))
                object_settings.radiance = tuple(float(component) for component in _attribute(light, "spectra:radiance:value"))
                object_settings.radiance_encoding = str(_attribute(light, "spectra:radiance:encoding"))
                object_settings.radiance_color_space = str(_attribute(light, "spectra:radiance:colorSpace"))
                object_settings.radiance_scalar = float(_attribute(light, "spectra:radiance:scalar"))
                object_settings.radiance_temperature = float(_attribute(light, "spectra:radiance:temperature"))
                object_settings.light_scale = float(_attribute(light, "spectra:scale"))
                object_settings.light_sidedness = str(_attribute(light, "spectra:sidedness"))
                object_settings.light_power_enabled = bool(_attribute(light, "spectra:hasPower"))
                if object_settings.light_power_enabled:
                    object_settings.light_power = float(_attribute(light, "spectra:power"))
                preview_material = material
                if preserved:
                    preview_material = material.copy()
                    preview_material.name = f"{material.name} — {object_settings.area_light_name}"
                    _assign_material(candidate, preview_material)
                _set_emissive_preview(preview_material, tuple(object_settings.radiance), object_settings.light_scale)
                emission_path = _relationship_target(light, "spectra:emissionTexture")
                if emission_path:
                    texture = stage.GetPrimAtPath(emission_path)
                    if str(_attribute(texture, "info:id")) == "UsdUVTexture":
                        image = _image_texture_node(stage, preview_material, texture, False)
                        principled = _principled(preview_material)
                        maximum = max(object_settings.radiance)
                        color = tuple(component / maximum for component in object_settings.radiance) if maximum else (0.0, 0.0, 0.0)
                        multiply = preview_material.node_tree.nodes.new("ShaderNodeMixRGB")
                        multiply.blend_type = "MULTIPLY"
                        multiply.inputs[0].default_value = 1.0
                        multiply.inputs[1].default_value = (*color, 1.0)
                        preview_material.node_tree.links.new(image.outputs["Color"], multiply.inputs[2])
                        preview_material.node_tree.links.new(multiply.outputs["Color"], principled.inputs["Emission Color"])
    return instances, prototype_collections


def _import_render_settings(stage: Usd.Stage, prim_map: dict, scene: bpy.types.Scene) -> bpy.types.Object:
    settings_prim = stage.GetPrimAtPath("/Render/Settings")
    film_path = _relationship_target(settings_prim, "spectra:film")
    sampler_path = _relationship_target(settings_prim, "spectra:sampler")
    camera_targets = settings_prim.GetRelationship("camera").GetTargets()
    film = stage.GetPrimAtPath(film_path)
    sampler = stage.GetPrimAtPath(sampler_path)
    camera_prim = stage.GetPrimAtPath(camera_targets[0])
    camera_object = _blender_id(prim_map, camera_prim.GetPath(), bpy.types.Object)
    camera_data = _blender_id(prim_map, camera_prim.GetPath(), bpy.types.Camera)
    if camera_object is None and camera_data is not None:
        camera_object = next((candidate for candidate in bpy.data.objects if candidate.data == camera_data), None)
    camera_parent_path = camera_prim.GetPath().GetParentPath()
    while camera_object is None and camera_parent_path != Sdf.Path.absoluteRootPath:
        candidate = _blender_id(prim_map, camera_parent_path, bpy.types.Object)
        if candidate is not None and candidate.type == "CAMERA":
            camera_object = candidate
        camera_parent_path = camera_parent_path.GetParentPath()
    if camera_object is None or camera_object.type != "CAMERA":
        raise RuntimeError(f"Spectra camera {camera_prim.GetPath()} was not imported as a Blender Camera")
    camera_settings = camera_object.data.spectra_usd
    camera_settings.camera = True
    camera_settings.source_path = str(camera_prim.GetPath())
    camera_settings.display_name = _display_name(camera_prim)
    camera_settings.revision_content = int(_attribute(camera_prim, "spectra:revision:content"))
    camera_settings.revision_topology = int(_attribute(camera_prim, "spectra:revision:topology"))
    camera_settings.exposure_time = float(_attribute(camera_prim, "spectra:exposureTime"))
    camera_type = str(_attribute(camera_prim, "spectra:cameraType"))
    if camera_type != "perspective":
        raise RuntimeError(f"Unsupported Spectra camera {camera_prim.GetPath()}: the Cornell Box feature set accepts perspective cameras")
    camera_settings.vertical_fov = float(_attribute(camera_prim, "spectra:verticalFov"))
    camera_settings.lens_radius = float(_attribute(camera_prim, "spectra:lensRadius"))
    camera_object.data.sensor_fit = "VERTICAL"
    camera_object.data.lens = camera_object.data.sensor_height / (2.0 * math.tan(math.radians(camera_settings.vertical_fov) * 0.5))
    scene.camera = camera_object
    resolution = _attribute(film, "resolution")
    scene.render.resolution_x = int(resolution[0])
    scene.render.resolution_y = int(resolution[1])
    scene.render.resolution_percentage = 100
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.length_unit = "METERS"
    bridge = scene.spectra_usd
    bridge.film_name = _display_name(film)
    bridge.film_revision_content = int(_attribute(film, "spectra:revision:content"))
    bridge.film_revision_topology = int(_attribute(film, "spectra:revision:topology"))
    bridge.exposure = float(_attribute(film, "spectra:exposure"))
    bridge.iso = float(_attribute(film, "spectra:iso"))
    bridge.color_space = str(_attribute(film, "spectra:colorSpace"))
    sensor_response = _attribute(film, "spectra:sensorResponse")
    if sensor_response:
        raise RuntimeError(f"Unsupported Spectra film {film_path}: sensor response curves are not in the Cornell Box bridge feature set")
    sensor_matrix = _attribute(film, "spectra:sensorToOutputRGB")
    bridge.sensor_to_output_rgb = tuple(float(sensor_matrix[row][column]) for row in range(3) for column in range(3))
    bridge.filter_kind = str(_attribute(film, "spectra:filter:kind"))
    bridge.filter_radius = tuple(float(value) for value in _attribute(film, "spectra:filter:radius"))
    bridge.filter_sigma = float(_attribute(film, "spectra:filter:sigma"))
    bridge.filter_b = float(_attribute(film, "spectra:filter:b"))
    bridge.filter_c = float(_attribute(film, "spectra:filter:c"))
    bridge.filter_tau = float(_attribute(film, "spectra:filter:tau"))
    bridge.maximum_component_enabled = bool(_attribute(film, "spectra:hasMaximumComponentValue"))
    if bridge.maximum_component_enabled:
        bridge.maximum_component_value = float(_attribute(film, "spectra:maximumComponentValue"))
    bridge.gbuffer = bool(_attribute(film, "spectra:gbuffer"))
    bridge.gbuffer_camera_space = bool(_attribute(film, "spectra:gbufferCameraSpace"))
    bridge.sampler_name = _display_name(sampler)
    bridge.sampler_revision_content = int(_attribute(sampler, "spectra:revision:content"))
    bridge.sampler_revision_topology = int(_attribute(sampler, "spectra:revision:topology"))
    bridge.sampler_kind = str(_attribute(sampler, "spectra:kind"))
    bridge.samples_per_pixel = int(_attribute(sampler, "spectra:samplesPerPixel"))
    bridge.seed = int(_attribute(sampler, "spectra:seed"))
    bridge.jitter = bool(_attribute(sampler, "spectra:jitter"))
    bridge.x_strata = int(_attribute(sampler, "spectra:xStrata"))
    bridge.y_strata = int(_attribute(sampler, "spectra:yStrata"))
    bridge.randomization = str(_attribute(sampler, "spectra:randomization"))
    bridge.maximum_depth = int(_attribute(settings_prim, "spectra:transport:maximumDepth"))
    bridge.light_sampler = str(_attribute(settings_prim, "spectra:transport:lightSampler"))
    bridge.regularize = bool(_attribute(settings_prim, "spectra:transport:regularize"))
    return camera_object


def _import_volumes(stage: Usd.Stage, prim_map: dict) -> None:
    collection = None
    for prim in stage.GetPrimAtPath("/World/Volumes").GetChildren():
        blender_object = _blender_id(prim_map, prim.GetPath(), bpy.types.Object)
        volume = _blender_id(prim_map, prim.GetPath(), bpy.types.Volume)
        if volume is None and blender_object and blender_object.type == "VOLUME":
            volume = blender_object.data
        if blender_object:
            blender_object.spectra_usd.source_path = str(prim.GetPath())
        field = prim.GetChild("FieldAsset")
        if volume and field:
            asset = _attribute(field, "filePath")
            resolved = Path(asset.resolvedPath) if asset.resolvedPath else Path(stage.GetRootLayer().realPath).parent / asset.path
            volume.filepath = str(resolved.resolve())
        if blender_object:
            if collection is None:
                collection = bpy.data.collections.new("Spectra Volume Domains")
                bpy.context.scene.collection.children.link(collection)
            minimum = _attribute(prim, "spectra:domainMinimum")
            maximum = _attribute(prim, "spectra:domainMaximum")
            vertices = [
                (x, y, z)
                for x in (float(minimum[0]), float(maximum[0]))
                for y in (float(minimum[1]), float(maximum[1]))
                for z in (float(minimum[2]), float(maximum[2]))
            ]
            mesh = bpy.data.meshes.new(f"{_display_name(prim)} Domain")
            mesh.from_pydata(vertices, (), ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)))
            proxy = bpy.data.objects.new(f"{_display_name(prim)} Domain", mesh)
            collection.objects.link(proxy)
            proxy.matrix_world = blender_object.matrix_world
            proxy.display_type = "WIRE"
            proxy.show_in_front = True
            proxy.hide_render = True
            proxy.spectra_usd.source_path = str(prim.GetPath())


def _detach(blender_object: bpy.types.Object) -> None:
    matrix = blender_object.matrix_world.copy()
    blender_object.parent = None
    blender_object.matrix_world = matrix


def _clean_import(instances: list[bpy.types.Object], camera: bpy.types.Object, prototype_collections: set[bpy.types.Collection], imported_objects: set[int]) -> None:
    retained = [blender_object for blender_object in bpy.context.scene.objects if blender_object.type != "EMPTY" and blender_object.as_pointer() in imported_objects]
    for blender_object in (*instances, camera, *retained):
        _detach(blender_object)
    keep = {value.as_pointer() for value in (*instances, camera, *retained)}
    for blender_object in list(bpy.data.objects):
        if blender_object.type == "EMPTY" and blender_object.spectra_usd.role == "NONE" and blender_object.as_pointer() in imported_objects and blender_object.as_pointer() not in keep:
            bpy.data.objects.remove(blender_object, do_unlink=True)
    prototype_parent = next((collection for collection in bpy.data.collections if prototype_collections and all(child.name in collection.children for child in prototype_collections)), None)
    if prototype_parent:
        bpy.data.collections.remove(prototype_parent)


def import_profile(import_context) -> bool:
    stage = import_context.get_stage()
    spectra = stage.GetPrimAtPath("/Spectra")
    if not spectra or not spectra.HasAttribute("spectra:profileVersion"):
        raise RuntimeError("The USD file is not a Spectra USD Profile")
    version = int(_attribute(spectra, "spectra:profileVersion"))
    if version != PROFILE_VERSION:
        raise RuntimeError(f"Unsupported Spectra USD Profile {version}; expected {PROFILE_VERSION}")
    scene = bpy.context.scene
    scene_settings = scene.spectra_usd
    scene_settings.source_path = str(Path(stage.GetRootLayer().realPath))
    scene_settings.scene_name = str(_attribute(spectra, "spectra:sceneName"))
    scene_settings.profile_mode = "PRESERVED" if _requires_preserved_profile(stage) else "AUTHORING"
    if scene_settings.profile_mode == "PRESERVED":
        _archive_source_layer(stage, scene)
    prim_map = import_context.get_prim_map()
    imported_objects = {value.as_pointer() for values in prim_map.values() for value in values if isinstance(value, bpy.types.Object)}
    materials = _import_materials(stage, prim_map)
    instances, prototype_collections = _import_instances(stage, prim_map, materials)
    camera = _import_render_settings(stage, prim_map, scene)
    bpy.context.view_layer.update()
    _clean_import(instances, camera, prototype_collections, imported_objects)
    _import_volumes(stage, prim_map)
    scene.render.engine = "CYCLES"
    return True


def _reverse_prim_map(export_context) -> dict[int, list[Sdf.Path]]:
    result: dict[int, list[Sdf.Path]] = {}
    for path, ids in export_context.get_prim_map().items():
        for value in ids:
            result.setdefault(value.as_pointer(), []).append(path)
    return result


def _object_prim(stage: Usd.Stage, reverse_map: dict[int, list[Sdf.Path]], blender_object: bpy.types.Object) -> Usd.Prim:
    paths = reverse_map.get(blender_object.as_pointer(), ())
    prims = [stage.GetPrimAtPath(path) for path in paths]
    prim = next((candidate for candidate in prims if candidate and not candidate.IsPrototype()), None)
    if prim is None:
        raise RuntimeError(f"Blender object {blender_object.name} was not exported to the standard USD projection")
    return prim


def _descendant_meshes(prim: Usd.Prim) -> list[Usd.Prim]:
    return [candidate for candidate in Usd.PrimRange(prim) if UsdGeom.Mesh(candidate)]


def _material_for_primitive(blender_object: bpy.types.Object) -> bpy.types.Material:
    materials = [slot.material for slot in blender_object.material_slots if slot.material]
    if len(materials) != 1:
        raise RuntimeError(f"Unsupported Blender object {blender_object.name}: Spectra prototype primitives require exactly one material")
    return materials[0]


def _collect_scene(stage: Usd.Stage, reverse_map: dict[int, list[Sdf.Path]]) -> tuple[list[InstanceExport], list[PrototypeExport]]:
    instances: list[InstanceExport] = []
    prototypes: dict[int, PrototypeExport] = {}
    scene = bpy.context.scene
    for blender_object in scene.objects:
        settings = blender_object.spectra_usd
        if settings.role == "PRIMITIVE" or blender_object.type == "CAMERA" or blender_object.hide_render:
            continue
        if settings.role == "INSTANCE":
            if blender_object.instance_collection is None or not blender_object.instance_collection.spectra_usd.prototype:
                raise RuntimeError(f"Spectra instance {blender_object.name} has no Spectra prototype collection")
            collection = blender_object.instance_collection
            key = collection.as_pointer()
            usd_instance = _object_prim(stage, reverse_map, blender_object)
            if key not in prototypes:
                usd_prototype = usd_instance.GetPrototype() if usd_instance.IsInstance() else Usd.Prim()
                mesh_prims = _descendant_meshes(usd_prototype) if usd_prototype else _descendant_meshes(usd_instance)
                blender_primitives = [candidate for candidate in collection.all_objects if candidate.type == "MESH"]
                if len(mesh_prims) != len(blender_primitives):
                    raise RuntimeError(f"Unsupported Blender prototype {collection.name}: exported {len(mesh_prims)} USD meshes for {len(blender_primitives)} Blender primitives")
                collection_settings = collection.spectra_usd
                prototypes[key] = PrototypeExport(
                    key,
                    collection_settings.display_name or collection.name,
                    collection_settings.revision_content,
                    collection_settings.revision_topology,
                    [
                        PrimitiveExport(
                            candidate,
                            mesh_prim,
                            UsdGeom.Xformable(mesh_prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default())
                            * UsdGeom.Xformable(usd_instance).ComputeLocalToWorldTransform(Usd.TimeCode.Default()).GetInverse(),
                        )
                        for candidate, mesh_prim in zip(blender_primitives, mesh_prims, strict=True)
                    ],
                )
            instances.append(InstanceExport(blender_object, usd_instance, prototypes[key]))
        elif blender_object.type == "MESH":
            key = blender_object.as_pointer()
            usd_object = _object_prim(stage, reverse_map, blender_object)
            mesh_prims = _descendant_meshes(usd_object)
            if len(mesh_prims) != 1:
                raise RuntimeError(f"Unsupported Blender object {blender_object.name}: expected one exported USD Mesh, found {len(mesh_prims)}")
            prototype = PrototypeExport(key, f"{blender_object.name} Prototype", 1, 1, [PrimitiveExport(blender_object, mesh_prims[0], Gf.Matrix4d(1.0))])
            prototypes[key] = prototype
            instances.append(InstanceExport(blender_object, usd_object, prototype))
        else:
            raise RuntimeError(f"Unsupported Blender object {blender_object.name}: type {blender_object.type} is not in the Cornell Box feature set")
    if not instances:
        raise RuntimeError("The Blender scene contains no Spectra instances or supported Mesh objects")
    return instances, list(prototypes.values())


def _interpolated_value(values, interpolation: str, face: int, corner: int, vertex: int):
    if interpolation == "constant":
        return values[0]
    if interpolation == "uniform":
        return values[face]
    if interpolation in {"vertex", "varying"}:
        return values[vertex]
    if interpolation == "faceVarying":
        return values[corner]
    raise RuntimeError(f"Unsupported USD primvar interpolation {interpolation}")


def _expanded_mesh(mesh_prim: Usd.Prim) -> tuple[list[Gf.Vec3f], list[Gf.Vec3f], list[Gf.Vec2f], list[int]]:
    mesh = UsdGeom.Mesh(mesh_prim)
    points = list(mesh.GetPointsAttr().Get())
    counts = list(mesh.GetFaceVertexCountsAttr().Get())
    source_indices = list(mesh.GetFaceVertexIndicesAttr().Get())
    if any(count != 3 for count in counts):
        raise RuntimeError(f"Unsupported exported mesh {mesh_prim.GetPath()}: triangulation did not produce triangle topology")
    normals = list(mesh.GetNormalsAttr().Get() or ())
    normal_interpolation = str(mesh.GetNormalsInterpolation())
    st = UsdGeom.PrimvarsAPI(mesh_prim).GetPrimvar("st")
    texture_coordinates = list(st.ComputeFlattened()) if st else []
    texture_interpolation = str(st.GetInterpolation()) if st else ""
    result_points: list[Gf.Vec3f] = []
    result_normals: list[Gf.Vec3f] = []
    result_texture_coordinates: list[Gf.Vec2f] = []
    result_indices: list[int] = []
    vertices: dict[tuple, int] = {}
    for corner, vertex in enumerate(source_indices):
        face = corner // 3
        normal = _interpolated_value(normals, normal_interpolation, face, corner, vertex) if normals else None
        uv = _interpolated_value(texture_coordinates, texture_interpolation, face, corner, vertex) if texture_coordinates else None
        key = (vertex, tuple(normal) if normal is not None else None, tuple(uv) if uv is not None else None)
        if key not in vertices:
            vertices[key] = len(result_points)
            result_points.append(Gf.Vec3f(points[vertex]))
            if normal is not None:
                result_normals.append(Gf.Vec3f(normal))
            if uv is not None:
                result_texture_coordinates.append(Gf.Vec2f(uv))
        result_indices.append(vertices[key])
    return result_points, result_normals, result_texture_coordinates, result_indices


def _write_geometry(stage: Usd.Stage, path: Sdf.Path, name: str, content: int, topology: int, mesh_prim: Usd.Prim) -> None:
    prim = stage.CreateClassPrim(path)
    prim.SetTypeName("Mesh")
    _set_name(prim, name)
    _set_revision(prim, content, topology)
    _set_attribute(prim, "spectra:geometryType", Sdf.ValueTypeNames.Token, "triangleMesh")
    points, normals, texture_coordinates, indices = _expanded_mesh(mesh_prim)
    mesh = UsdGeom.Mesh(prim)
    mesh.CreatePointsAttr(Vt.Vec3fArray(points))
    mesh.CreateFaceVertexCountsAttr(Vt.IntArray([3] * (len(indices) // 3)))
    mesh.CreateFaceVertexIndicesAttr(Vt.IntArray(indices))
    mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
    if normals:
        mesh.CreateNormalsAttr(Vt.Vec3fArray(normals))
        mesh.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
    if texture_coordinates:
        UsdGeom.PrimvarsAPI(mesh).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, UsdGeom.Tokens.vertex).Set(Vt.Vec2fArray(texture_coordinates))


def _base_color(material: bpy.types.Material) -> tuple[float, float, float]:
    principled = _principled(material)
    for name in ("Base Color", "Metallic", "Transmission Weight", "Alpha"):
        if principled.inputs[name].is_linked:
            raise RuntimeError(f"Unsupported Blender material {material.name}: connected Principled input {name}")
    if principled.inputs["Metallic"].default_value != 0.0 or principled.inputs["Transmission Weight"].default_value != 0.0 or principled.inputs["Alpha"].default_value != 1.0:
        raise RuntimeError(f"Unsupported Blender material {material.name}: the Cornell Box feature set accepts opaque non-metallic diffuse Principled materials")
    return tuple(float(component) for component in principled.inputs["Base Color"].default_value[:3])


def _set_spectrum(prim: Usd.Prim, prefix: str, value: tuple[float, float, float], encoding: str, color_space: str, scalar: float = 0.0, temperature: float = 0.0) -> None:
    _set_attribute(prim, f"{prefix}:value", Sdf.ValueTypeNames.Color3f, Gf.Vec3f(*value))
    _set_attribute(prim, f"{prefix}:encoding", Sdf.ValueTypeNames.Token, encoding)
    _set_attribute(prim, f"{prefix}:colorSpace", Sdf.ValueTypeNames.Token, color_space)
    _set_attribute(prim, f"{prefix}:scalar", Sdf.ValueTypeNames.Float, scalar)
    _set_attribute(prim, f"{prefix}:temperature", Sdf.ValueTypeNames.Float, temperature)
    _set_attribute(prim, f"{prefix}:wavelengths", Sdf.ValueTypeNames.FloatArray, Vt.FloatArray())
    _set_attribute(prim, f"{prefix}:samples", Sdf.ValueTypeNames.FloatArray, Vt.FloatArray())


def _write_texture(stage: Usd.Stage, path: Sdf.Path, material: bpy.types.Material, color: tuple[float, float, float]) -> None:
    settings = material.spectra_usd
    shader = UsdShade.Shader.Define(stage, path)
    prim = shader.GetPrim()
    shader.CreateIdAttr("SpectraConstantTexture")
    _set_name(prim, settings.texture_name)
    _set_revision(prim, settings.texture_revision_content, settings.texture_revision_topology)
    _set_attribute(prim, "spectra:valueKind", Sdf.ValueTypeNames.Token, "spectrum")
    _set_attribute(prim, "spectra:spectrumType", Sdf.ValueTypeNames.Token, "albedo")
    _set_attribute(prim, "spectra:colorSpace", Sdf.ValueTypeNames.Token, settings.reflectance_color_space)
    _set_attribute(prim, "spectra:scalar", Sdf.ValueTypeNames.Float, 0.0)
    _set_spectrum(prim, "spectra:spectrum", color, settings.reflectance_encoding, settings.reflectance_color_space, settings.reflectance_scalar)


def _write_material(stage: Usd.Stage, path: Sdf.Path, material: bpy.types.Material, texture_path: Sdf.Path | None) -> None:
    settings = material.spectra_usd
    if settings.material and settings.kind != "DIFFUSE":
        raise RuntimeError(f"Unsupported Spectra material {material.name}: {settings.kind}")
    color = _base_color(material)
    usd_material = UsdShade.Material.Define(stage, path)
    material_prim = usd_material.GetPrim()
    _set_name(material_prim, settings.display_name if settings.material else material.name)
    _set_revision(material_prim, settings.revision_content if settings.material else 1, settings.revision_topology if settings.material else 1)
    preview = UsdShade.Shader.Define(stage, path.AppendChild("PreviewSurface"))
    preview.CreateIdAttr("UsdPreviewSurface")
    preview.CreateInput("diffuseColor", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*color))
    preview.CreateInput("roughness", Sdf.ValueTypeNames.Float).Set(0.5)
    preview.CreateOutput("surface", Sdf.ValueTypeNames.Token)
    usd_material.CreateSurfaceOutput().ConnectToSource(preview.ConnectableAPI(), "surface")
    shader = UsdShade.Shader.Define(stage, path.AppendChild("SpectraSurface"))
    shader.CreateIdAttr("SpectraDiffuse")
    shader.CreateOutput("surface", Sdf.ValueTypeNames.Token)
    usd_material.CreateSurfaceOutput("spectra").ConnectToSource(shader.ConnectableAPI(), "surface")
    encoding = settings.reflectance_encoding if settings.material else "rgbAlbedo"
    color_space = settings.reflectance_color_space if settings.material else "sRGB"
    scalar = settings.reflectance_scalar if settings.material else 0.0
    _set_spectrum(shader.GetPrim(), "spectra:reflectance", color, encoding, color_space, scalar)
    if texture_path:
        _set_relationship(shader.GetPrim(), "spectra:reflectance:texture", texture_path)


def _write_area_light(stage: Usd.Stage, path: Sdf.Path, blender_object: bpy.types.Object) -> None:
    settings = blender_object.spectra_usd
    prim = stage.DefinePrim(path, "Scope")
    _set_name(prim, settings.area_light_name)
    _set_revision(prim, settings.area_light_revision_content, settings.area_light_revision_topology)
    _set_attribute(prim, "spectra:lightType", Sdf.ValueTypeNames.Token, "diffuseArea")
    _set_spectrum(prim, "spectra:radiance", tuple(settings.radiance), settings.radiance_encoding, settings.radiance_color_space, settings.radiance_scalar, settings.radiance_temperature)
    _set_attribute(prim, "spectra:sidedness", Sdf.ValueTypeNames.Token, settings.light_sidedness)
    _set_attribute(prim, "spectra:scale", Sdf.ValueTypeNames.Float, settings.light_scale)
    _set_attribute(prim, "spectra:hasPower", Sdf.ValueTypeNames.Bool, settings.light_power_enabled)
    if settings.light_power_enabled:
        _set_attribute(prim, "spectra:power", Sdf.ValueTypeNames.Float, settings.light_power)


def _write_camera(stage: Usd.Stage, reverse_map: dict[int, list[Sdf.Path]], camera_object: bpy.types.Object, resolution: tuple[int, int]) -> Sdf.Path:
    camera_paths = reverse_map.get(camera_object.as_pointer(), ()) + reverse_map.get(camera_object.data.as_pointer(), ())
    camera_prim = next((stage.GetPrimAtPath(path) for path in camera_paths if UsdGeom.Camera(stage.GetPrimAtPath(path))), None)
    if camera_prim is None:
        object_prim = _object_prim(stage, reverse_map, camera_object)
        camera_prim = next((prim for prim in Usd.PrimRange(object_prim) if UsdGeom.Camera(prim)), None)
    if camera_prim is None:
        raise RuntimeError(f"Blender camera {camera_object.name} was not exported to the standard USD projection")
    settings = camera_object.data.spectra_usd
    camera = UsdGeom.Camera(camera_prim)
    projection = camera.GetProjectionAttr().Get()
    _set_name(camera_prim, settings.display_name if settings.camera else camera_object.name)
    _set_revision(camera_prim, settings.revision_content if settings.camera else 1, settings.revision_topology if settings.camera else 1)
    _set_attribute(camera_prim, "spectra:transform", Sdf.ValueTypeNames.Matrix4d, UsdGeom.Xformable(camera_prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default()))
    _set_attribute(camera_prim, "spectra:exposureTime", Sdf.ValueTypeNames.Float, settings.exposure_time if settings.camera else 1.0)
    _set_attribute(camera_prim, "spectra:cameraType", Sdf.ValueTypeNames.Token, str(projection))
    aspect = resolution[0] / resolution[1]
    _set_attribute(camera_prim, "spectra:screenMinimum", Sdf.ValueTypeNames.Float2, Gf.Vec2f(-aspect, -1.0))
    _set_attribute(camera_prim, "spectra:screenMaximum", Sdf.ValueTypeNames.Float2, Gf.Vec2f(aspect, 1.0))
    _set_attribute(camera_prim, "spectra:lensRadius", Sdf.ValueTypeNames.Float, settings.lens_radius if settings.camera else 0.0)
    _set_attribute(camera_prim, "spectra:focalDistance", Sdf.ValueTypeNames.Float, float(camera.GetFocusDistanceAttr().Get()))
    clipping = camera.GetClippingRangeAttr().Get()
    _set_attribute(camera_prim, "spectra:nearPlane", Sdf.ValueTypeNames.Float, float(clipping[0]))
    _set_attribute(camera_prim, "spectra:farPlane", Sdf.ValueTypeNames.Float, float(clipping[1]))
    if projection == UsdGeom.Tokens.perspective:
        focal_length = float(camera.GetFocalLengthAttr().Get())
        vertical_aperture = float(camera.GetVerticalApertureAttr().Get())
        vertical_fov = settings.vertical_fov if settings.camera else math.degrees(2.0 * math.atan(vertical_aperture / (2.0 * focal_length)))
        _set_attribute(camera_prim, "spectra:verticalFov", Sdf.ValueTypeNames.Float, vertical_fov)
    return camera_prim.GetPath()


def _write_render_settings(stage: Usd.Stage, camera_path: Sdf.Path) -> None:
    scene = bpy.context.scene
    bridge = scene.spectra_usd
    film_path = Sdf.Path(f"/Spectra/Films/{Tf.MakeValidIdentifier(bridge.film_name) or 'Film'}")
    sampler_path = Sdf.Path(f"/Spectra/Samplers/{Tf.MakeValidIdentifier(bridge.sampler_name) or 'Sampler'}")
    UsdGeom.Scope.Define(stage, "/Spectra/Films")
    UsdGeom.Scope.Define(stage, "/Spectra/Samplers")
    product = UsdRender.Product.Define(stage, film_path)
    film = product.GetPrim()
    _set_name(film, bridge.film_name)
    _set_revision(film, bridge.film_revision_content, bridge.film_revision_topology)
    resolution = Gf.Vec2i(scene.render.resolution_x, scene.render.resolution_y)
    product.CreateResolutionAttr(resolution)
    _set_attribute(film, "spectra:pixelMinimum", Sdf.ValueTypeNames.Int2, Gf.Vec2i(0, 0))
    _set_attribute(film, "spectra:pixelMaximum", Sdf.ValueTypeNames.Int2, resolution)
    _set_attribute(film, "spectra:exposure", Sdf.ValueTypeNames.Float, bridge.exposure)
    _set_attribute(film, "spectra:iso", Sdf.ValueTypeNames.Float, bridge.iso)
    _set_attribute(film, "spectra:colorSpace", Sdf.ValueTypeNames.Token, bridge.color_space)
    _set_attribute(film, "spectra:sensorResponse", Sdf.ValueTypeNames.FloatArray, Vt.FloatArray())
    matrix = bridge.sensor_to_output_rgb
    _set_attribute(film, "spectra:sensorToOutputRGB", Sdf.ValueTypeNames.Matrix3d, Gf.Matrix3d(*matrix))
    _set_attribute(film, "spectra:hasMaximumComponentValue", Sdf.ValueTypeNames.Bool, bridge.maximum_component_enabled)
    if bridge.maximum_component_enabled:
        _set_attribute(film, "spectra:maximumComponentValue", Sdf.ValueTypeNames.Float, bridge.maximum_component_value)
    _set_attribute(film, "spectra:filter:kind", Sdf.ValueTypeNames.Token, bridge.filter_kind)
    _set_attribute(film, "spectra:filter:radius", Sdf.ValueTypeNames.Float2, Gf.Vec2f(*bridge.filter_radius))
    _set_attribute(film, "spectra:filter:sigma", Sdf.ValueTypeNames.Float, bridge.filter_sigma)
    _set_attribute(film, "spectra:filter:b", Sdf.ValueTypeNames.Float, bridge.filter_b)
    _set_attribute(film, "spectra:filter:c", Sdf.ValueTypeNames.Float, bridge.filter_c)
    _set_attribute(film, "spectra:filter:tau", Sdf.ValueTypeNames.Float, bridge.filter_tau)
    _set_attribute(film, "spectra:gbuffer", Sdf.ValueTypeNames.Bool, bridge.gbuffer)
    _set_attribute(film, "spectra:gbufferCameraSpace", Sdf.ValueTypeNames.Bool, bridge.gbuffer_camera_space)
    sampler = stage.DefinePrim(sampler_path, "Scope")
    _set_name(sampler, bridge.sampler_name)
    _set_revision(sampler, bridge.sampler_revision_content, bridge.sampler_revision_topology)
    _set_attribute(sampler, "spectra:kind", Sdf.ValueTypeNames.Token, bridge.sampler_kind)
    _set_attribute(sampler, "spectra:samplesPerPixel", Sdf.ValueTypeNames.UInt, bridge.samples_per_pixel)
    _set_attribute(sampler, "spectra:seed", Sdf.ValueTypeNames.UInt, bridge.seed)
    _set_attribute(sampler, "spectra:jitter", Sdf.ValueTypeNames.Bool, bridge.jitter)
    _set_attribute(sampler, "spectra:xStrata", Sdf.ValueTypeNames.UInt, bridge.x_strata)
    _set_attribute(sampler, "spectra:yStrata", Sdf.ValueTypeNames.UInt, bridge.y_strata)
    _set_attribute(sampler, "spectra:randomization", Sdf.ValueTypeNames.Token, bridge.randomization)
    settings = UsdRender.Settings.Define(stage, "/Render/Settings")
    settings.CreateProductsRel().SetTargets([film_path])
    settings.CreateCameraRel().SetTargets([camera_path])
    prim = settings.GetPrim()
    _set_relationship(prim, "spectra:film", film_path)
    _set_relationship(prim, "spectra:sampler", sampler_path)
    _set_attribute(prim, "spectra:transport:maximumDepth", Sdf.ValueTypeNames.UInt, bridge.maximum_depth)
    _set_attribute(prim, "spectra:transport:lightSampler", Sdf.ValueTypeNames.Token, bridge.light_sampler)
    _set_attribute(prim, "spectra:transport:regularize", Sdf.ValueTypeNames.Bool, bridge.regularize)


def export_profile(export_context) -> bool:
    stage = export_context.get_stage()
    scene = bpy.context.scene
    if scene.spectra_usd.profile_mode == "PRESERVED":
        return _export_preserved_profile(stage, scene)
    if scene.camera is None:
        raise RuntimeError("The Blender scene has no active camera")
    reverse_map = _reverse_prim_map(export_context)
    instances, prototypes = _collect_scene(stage, reverse_map)
    stage.RemovePrim("/Spectra")
    stage.RemovePrim("/Render")
    world = stage.GetPrimAtPath("/World")
    if not world:
        world = UsdGeom.Xform.Define(stage, "/World").GetPrim()
    stage.SetDefaultPrim(world)
    UsdGeom.SetStageMetersPerUnit(stage, UsdGeom.LinearUnits.meters)
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    for scope in ("Lights", "Volumes", "Particles", "NeuralFields"):
        UsdGeom.Scope.Define(stage, f"/World/{scope}")
    spectra = UsdGeom.Scope.Define(stage, "/Spectra").GetPrim()
    _set_attribute(spectra, "spectra:profileVersion", Sdf.ValueTypeNames.UInt, PROFILE_VERSION)
    _set_attribute(spectra, "spectra:sceneName", Sdf.ValueTypeNames.String, scene.spectra_usd.scene_name or scene.name)
    for scope in ("Geometry", "SphereSets", "Textures", "Materials", "Media", "AreaLights", "Prototypes"):
        UsdGeom.Scope.Define(stage, f"/Spectra/{scope}")
    geometry_paths: dict[int, Sdf.Path] = {}
    geometry_prims: dict[int, Usd.Prim] = {}
    material_paths: dict[int, Sdf.Path] = {}
    texture_paths: dict[int, Sdf.Path] = {}
    area_light_paths: dict[int, Sdf.Path] = {}
    prototype_paths: dict[int, Sdf.Path] = {}
    geometry_names: set[str] = set()
    material_names: set[str] = set()
    texture_names: set[str] = set()
    light_names: set[str] = set()
    prototype_names: set[str] = set()
    for prototype in prototypes:
        prototype_paths[prototype.key] = Sdf.Path("/Spectra/Prototypes").AppendChild(_identifier(prototype.name, "Prototype", prototype_names))
        for primitive in prototype.primitives:
            mesh = primitive.blender_object.data
            mesh_key = mesh.as_pointer()
            if mesh_key not in geometry_paths:
                settings = mesh.spectra_usd
                name = settings.display_name if settings.geometry else mesh.name
                geometry_paths[mesh_key] = Sdf.Path("/Spectra/Geometry").AppendChild(_identifier(name, "Geometry", geometry_names))
                geometry_prims[mesh_key] = primitive.mesh_prim
            material = _material_for_primitive(primitive.blender_object)
            material_key = material.as_pointer()
            if material_key not in material_paths:
                settings = material.spectra_usd
                name = settings.display_name if settings.material else material.name
                material_paths[material_key] = Sdf.Path("/Spectra/Materials").AppendChild(_identifier(name, "Material", material_names))
                if settings.texture:
                    texture_paths[material_key] = Sdf.Path("/Spectra/Textures").AppendChild(_identifier(settings.texture_name, "Texture", texture_names))
            object_settings = primitive.blender_object.spectra_usd
            if object_settings.area_light:
                area_light_paths[primitive.blender_object.as_pointer()] = Sdf.Path("/Spectra/AreaLights").AppendChild(_identifier(object_settings.area_light_name, "AreaLight", light_names))
    for prototype in prototypes:
        for primitive in prototype.primitives:
            mesh = primitive.blender_object.data
            key = mesh.as_pointer()
            if geometry_prims[key]:
                settings = mesh.spectra_usd
                _write_geometry(stage, geometry_paths[key], settings.display_name if settings.geometry else mesh.name, settings.revision_content if settings.geometry else 1, settings.revision_topology if settings.geometry else 1, geometry_prims[key])
                geometry_prims[key] = Usd.Prim()
            material = _material_for_primitive(primitive.blender_object)
            material_key = material.as_pointer()
            if material_paths[material_key] and not stage.GetPrimAtPath(material_paths[material_key]):
                texture_path = texture_paths.get(material_key)
                if texture_path:
                    _write_texture(stage, texture_path, material, _base_color(material))
                _write_material(stage, material_paths[material_key], material, texture_path)
            object_key = primitive.blender_object.as_pointer()
            if object_key in area_light_paths and not stage.GetPrimAtPath(area_light_paths[object_key]):
                _write_area_light(stage, area_light_paths[object_key], primitive.blender_object)
    for prototype in prototypes:
        root = stage.CreateClassPrim(prototype_paths[prototype.key])
        root.SetTypeName("Xform")
        _set_name(root, prototype.name)
        _set_revision(root, prototype.revision_content, prototype.revision_topology)
        for index, primitive in enumerate(prototype.primitives, 1):
            child = stage.DefinePrim(root.GetPath().AppendChild(f"Primitive_{index:03}"))
            geometry_path = geometry_paths[primitive.blender_object.data.as_pointer()]
            child.GetReferences().AddInternalReference(geometry_path)
            _set_relationship(child, "spectra:geometry", geometry_path)
            _set_attribute(child, "spectra:transform", Sdf.ValueTypeNames.Matrix4d, primitive.transform)
            xform = UsdGeom.Xformable(child)
            xform.AddTransformOp(UsdGeom.XformOp.PrecisionDouble).Set(primitive.transform)
            material = _material_for_primitive(primitive.blender_object)
            material_path = material_paths[material.as_pointer()]
            _set_relationship(child, "spectra:material", material_path)
            UsdShade.MaterialBindingAPI.Apply(child).Bind(UsdShade.Material(stage.GetPrimAtPath(material_path)))
            object_settings = primitive.blender_object.spectra_usd
            _set_attribute(child, "spectra:reverseOrientation", Sdf.ValueTypeNames.Bool, object_settings.reverse_orientation)
            child.CreateAttribute("orientation", Sdf.ValueTypeNames.Token).Set(UsdGeom.Tokens.leftHanded if object_settings.reverse_orientation else UsdGeom.Tokens.rightHanded)
            object_key = primitive.blender_object.as_pointer()
            if object_key in area_light_paths:
                light_path = area_light_paths[object_key]
                _set_relationship(child, "spectra:areaLight", light_path)
                UsdLux.MeshLightAPI.Apply(child)
                light_settings = primitive.blender_object.spectra_usd
                child.CreateAttribute("inputs:color", Sdf.ValueTypeNames.Color3f).Set(Gf.Vec3f(*light_settings.radiance))
                child.CreateAttribute("inputs:intensity", Sdf.ValueTypeNames.Float).Set(light_settings.light_scale)
    for instance in instances:
        prim = instance.usd_prim
        object_settings = instance.blender_object.spectra_usd
        _set_name(prim, instance.blender_object.name)
        _set_revision(prim, object_settings.revision_content if object_settings.role == "INSTANCE" else 1, object_settings.revision_topology if object_settings.role == "INSTANCE" else 1)
        _set_relationship(prim, "spectra:prototype", prototype_paths[instance.prototype.key])
        _set_attribute(prim, "spectra:transform", Sdf.ValueTypeNames.Matrix4d, UsdGeom.Xformable(prim).ComputeLocalToWorldTransform(Usd.TimeCode.Default()))
    camera_path = _write_camera(stage, reverse_map, scene.camera, (scene.render.resolution_x, scene.render.resolution_y))
    _write_render_settings(stage, camera_path)
    return True
