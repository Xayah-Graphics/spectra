from __future__ import annotations

import bpy
from bpy_extras.io_utils import ExportHelper, ImportHelper

from . import profile


class SpectraSceneSettings(bpy.types.PropertyGroup):
    source_path: bpy.props.StringProperty(name="Source USD")
    profile_mode: bpy.props.EnumProperty(
        name="Profile Mode",
        items=(
            ("AUTHORING", "Authoring", "Spectra features are authored from Blender data"),
            ("PRESERVED", "Preserved", "The complete Spectra layer is preserved for lossless exchange"),
        ),
        default="AUTHORING",
    )
    source_layer_text: bpy.props.StringProperty(name="Preserved Layer")
    source_fingerprint: bpy.props.StringProperty(name="Imported Scene Fingerprint")
    scene_name: bpy.props.StringProperty(name="Scene Name", default="Scene")
    film_name: bpy.props.StringProperty(name="Film", default="Film")
    film_revision_content: bpy.props.IntProperty(name="Film Content Revision", default=1, min=0)
    film_revision_topology: bpy.props.IntProperty(name="Film Topology Revision", default=1, min=0)
    exposure: bpy.props.FloatProperty(name="Exposure", default=0.0)
    iso: bpy.props.FloatProperty(name="ISO", default=100.0, min=0.0)
    color_space: bpy.props.EnumProperty(
        name="Color Space",
        items=(("sRGB", "sRGB", ""), ("Rec2020", "Rec. 2020", ""), ("ACES2065-1", "ACES 2065-1", "")),
        default="sRGB",
    )
    sensor_to_output_rgb: bpy.props.FloatVectorProperty(
        name="Sensor to Output RGB",
        size=9,
        default=(3.240479, -1.53715, -0.498535, -0.969256, 1.875991, 0.041556, 0.055648, -0.204043, 1.057311),
    )
    filter_kind: bpy.props.EnumProperty(
        name="Filter",
        items=(("box", "Box", ""), ("gaussian", "Gaussian", ""), ("mitchell", "Mitchell", ""), ("sinc", "Sinc", ""), ("triangle", "Triangle", "")),
        default="box",
    )
    filter_radius: bpy.props.FloatVectorProperty(name="Filter Radius", size=2, default=(0.5, 0.5), min=0.0)
    filter_sigma: bpy.props.FloatProperty(name="Filter Sigma", default=0.5)
    filter_b: bpy.props.FloatProperty(name="Filter B", default=1.0 / 3.0)
    filter_c: bpy.props.FloatProperty(name="Filter C", default=1.0 / 3.0)
    filter_tau: bpy.props.FloatProperty(name="Filter Tau", default=3.0)
    maximum_component_enabled: bpy.props.BoolProperty(name="Limit Maximum Component", default=False)
    maximum_component_value: bpy.props.FloatProperty(name="Maximum Component", default=1.0)
    gbuffer: bpy.props.BoolProperty(name="GBuffer", default=False)
    gbuffer_camera_space: bpy.props.BoolProperty(name="Camera-space GBuffer", default=True)
    sampler_name: bpy.props.StringProperty(name="Sampler", default="Sampler")
    sampler_revision_content: bpy.props.IntProperty(name="Sampler Content Revision", default=1, min=0)
    sampler_revision_topology: bpy.props.IntProperty(name="Sampler Topology Revision", default=1, min=0)
    sampler_kind: bpy.props.EnumProperty(
        name="Sampler",
        items=(
            ("independent", "Independent", ""),
            ("stratified", "Stratified", ""),
            ("halton", "Halton", ""),
            ("sobol", "Sobol", ""),
            ("paddedSobol", "Padded Sobol", ""),
            ("zSobol", "Z Sobol", ""),
            ("pmj02bn", "PMJ02BN", ""),
        ),
        default="independent",
    )
    samples_per_pixel: bpy.props.IntProperty(name="Samples", default=64, min=1)
    seed: bpy.props.IntProperty(name="Seed", default=0, min=0)
    jitter: bpy.props.BoolProperty(name="Jitter", default=True)
    x_strata: bpy.props.IntProperty(name="X Strata", default=1, min=1)
    y_strata: bpy.props.IntProperty(name="Y Strata", default=1, min=1)
    randomization: bpy.props.EnumProperty(
        name="Randomization",
        items=(("none", "None", ""), ("permuteDigits", "Permute Digits", ""), ("fastOwen", "Fast Owen", ""), ("owen", "Owen", "")),
        default="owen",
    )
    maximum_depth: bpy.props.IntProperty(name="Maximum Depth", default=8, min=0)
    light_sampler: bpy.props.EnumProperty(
        name="Light Sampler",
        items=(("uniform", "Uniform", ""), ("power", "Power", ""), ("bvh", "BVH", "")),
        default="bvh",
    )
    regularize: bpy.props.BoolProperty(name="Regularize", default=False)


class SpectraObjectSettings(bpy.types.PropertyGroup):
    role: bpy.props.EnumProperty(
        name="Role",
        items=(("NONE", "None", ""), ("INSTANCE", "Instance", ""), ("PRIMITIVE", "Prototype Primitive", "")),
        default="NONE",
    )
    source_path: bpy.props.StringProperty(name="Source Prim")
    prototype_path: bpy.props.StringProperty(name="Prototype Prim")
    geometry_path: bpy.props.StringProperty(name="Geometry Prim")
    material_path: bpy.props.StringProperty(name="Material Prim")
    revision_content: bpy.props.IntProperty(name="Content Revision", default=1, min=0)
    revision_topology: bpy.props.IntProperty(name="Topology Revision", default=1, min=0)
    reverse_orientation: bpy.props.BoolProperty(name="Reverse Orientation", default=False)
    area_light: bpy.props.BoolProperty(name="Diffuse Area Light", default=False)
    area_light_path: bpy.props.StringProperty(name="Area Light Prim")
    area_light_name: bpy.props.StringProperty(name="Area Light Name", default="Area Light")
    area_light_revision_content: bpy.props.IntProperty(name="Light Content Revision", default=1, min=0)
    area_light_revision_topology: bpy.props.IntProperty(name="Light Topology Revision", default=1, min=0)
    radiance: bpy.props.FloatVectorProperty(name="Radiance", size=3, subtype="COLOR", default=(1.0, 1.0, 1.0), min=0.0)
    radiance_encoding: bpy.props.EnumProperty(
        name="Radiance Encoding",
        items=(("rgbIlluminant", "RGB Illuminant", ""), ("constant", "Constant", ""), ("blackbody", "Blackbody", "")),
        default="rgbIlluminant",
    )
    radiance_color_space: bpy.props.EnumProperty(
        name="Radiance Color Space",
        items=(("sRGB", "sRGB", ""), ("Rec2020", "Rec. 2020", ""), ("ACES2065-1", "ACES 2065-1", "")),
        default="sRGB",
    )
    radiance_scalar: bpy.props.FloatProperty(name="Radiance Scalar", default=0.0)
    radiance_temperature: bpy.props.FloatProperty(name="Temperature", default=6500.0)
    light_scale: bpy.props.FloatProperty(name="Scale", default=1.0, min=0.0)
    light_sidedness: bpy.props.EnumProperty(name="Sidedness", items=(("front", "Front", ""), ("both", "Both", "")), default="front")
    light_power_enabled: bpy.props.BoolProperty(name="Specify Power", default=False)
    light_power: bpy.props.FloatProperty(name="Power", default=1.0, min=0.0)


class SpectraCollectionSettings(bpy.types.PropertyGroup):
    prototype: bpy.props.BoolProperty(name="Spectra Prototype", default=False)
    source_path: bpy.props.StringProperty(name="Source Prim")
    display_name: bpy.props.StringProperty(name="Name", default="Prototype")
    revision_content: bpy.props.IntProperty(name="Content Revision", default=1, min=0)
    revision_topology: bpy.props.IntProperty(name="Topology Revision", default=1, min=0)


class SpectraMeshSettings(bpy.types.PropertyGroup):
    geometry: bpy.props.BoolProperty(name="Spectra Geometry", default=False)
    source_path: bpy.props.StringProperty(name="Source Prim")
    display_name: bpy.props.StringProperty(name="Name", default="Geometry")
    revision_content: bpy.props.IntProperty(name="Content Revision", default=1, min=0)
    revision_topology: bpy.props.IntProperty(name="Topology Revision", default=1, min=0)


class SpectraMaterialSettings(bpy.types.PropertyGroup):
    material: bpy.props.BoolProperty(name="Spectra Material", default=False)
    source_path: bpy.props.StringProperty(name="Source Prim")
    display_name: bpy.props.StringProperty(name="Name", default="Material")
    revision_content: bpy.props.IntProperty(name="Content Revision", default=1, min=0)
    revision_topology: bpy.props.IntProperty(name="Topology Revision", default=1, min=0)
    kind: bpy.props.EnumProperty(
        name="Material",
        items=(
            ("DIFFUSE", "Diffuse", ""),
            ("DIFFUSE_TRANSMISSION", "Diffuse Transmission", ""),
            ("CONDUCTOR", "Conductor", ""),
            ("DIELECTRIC", "Dielectric", ""),
            ("THIN_DIELECTRIC", "Thin Dielectric", ""),
            ("COATED_DIFFUSE", "Coated Diffuse", ""),
            ("COATED_CONDUCTOR", "Coated Conductor", ""),
            ("MIX", "Mix", ""),
            ("INTERFACE", "Interface", ""),
        ),
        default="DIFFUSE",
    )
    shader_id: bpy.props.StringProperty(name="Spectra Shader")
    reflectance_encoding: bpy.props.EnumProperty(
        name="Reflectance Encoding",
        items=(("rgbAlbedo", "RGB Albedo", ""), ("constant", "Constant", "")),
        default="rgbAlbedo",
    )
    reflectance_color_space: bpy.props.EnumProperty(
        name="Reflectance Color Space",
        items=(("sRGB", "sRGB", ""), ("Rec2020", "Rec. 2020", ""), ("ACES2065-1", "ACES 2065-1", "")),
        default="sRGB",
    )
    reflectance_scalar: bpy.props.FloatProperty(name="Reflectance", default=0.0, min=0.0)
    texture: bpy.props.BoolProperty(name="Constant Texture", default=False)
    texture_source_path: bpy.props.StringProperty(name="Texture Prim")
    texture_name: bpy.props.StringProperty(name="Texture Name", default="Texture")
    texture_revision_content: bpy.props.IntProperty(name="Texture Content Revision", default=1, min=0)
    texture_revision_topology: bpy.props.IntProperty(name="Texture Topology Revision", default=1, min=0)
    texture_type: bpy.props.StringProperty(name="Texture Type")


class SpectraCameraSettings(bpy.types.PropertyGroup):
    camera: bpy.props.BoolProperty(name="Spectra Camera", default=False)
    source_path: bpy.props.StringProperty(name="Source Prim")
    display_name: bpy.props.StringProperty(name="Name", default="Camera")
    revision_content: bpy.props.IntProperty(name="Content Revision", default=1, min=0)
    revision_topology: bpy.props.IntProperty(name="Topology Revision", default=1, min=0)
    exposure_time: bpy.props.FloatProperty(name="Exposure Time", default=1.0, min=0.0)
    vertical_fov: bpy.props.FloatProperty(name="Vertical Field of View", default=45.0, min=0.001, max=179.0)
    lens_radius: bpy.props.FloatProperty(name="Lens Radius", default=0.0, min=0.0)


class SPECTRA_OT_import_usd(bpy.types.Operator, ImportHelper):
    bl_idname = "spectra.import_usd"
    bl_label = "Import Spectra USD"
    bl_description = "Import a Spectra USD Profile scene for editing"
    filename_ext = ".usda"
    filter_glob: bpy.props.StringProperty(default="*.usd;*.usda;*.usdc", options={"HIDDEN"})

    def execute(self, context):
        profile.last_error = None
        profile.import_requested = True
        try:
            try:
                result = bpy.ops.wm.usd_import(
                    filepath=self.filepath,
                    import_defined_only=True,
                    support_scene_instancing=True,
                    create_collection=True,
                    import_all_materials=True,
                    import_usd_preview=True,
                    mtl_purpose="MTL_ALL_PURPOSE",
                    property_import_mode="NONE",
                    validate_meshes=False,
                    create_world_material=False,
                    apply_unit_conversion_scale=True,
                )
            except RuntimeError:
                if not profile.last_error:
                    raise
                self.report({"ERROR"}, profile.last_error)
                return {"CANCELLED"}
        finally:
            profile.import_requested = False
        if profile.last_error:
            self.report({"ERROR"}, profile.last_error)
            return {"CANCELLED"}
        if context.scene.spectra_usd.profile_mode == "PRESERVED":
            context.view_layer.update()
            context.scene.spectra_usd.source_fingerprint = profile.scene_fingerprint(context.scene)
        return result


class SPECTRA_OT_export_usd(bpy.types.Operator, ExportHelper):
    bl_idname = "spectra.export_usd"
    bl_label = "Export Spectra USD"
    bl_description = "Export the supported Blender scene as a Spectra USD Profile"
    filename_ext = ".usda"
    filter_glob: bpy.props.StringProperty(default="*.usd;*.usda;*.usdc", options={"HIDDEN"})

    def execute(self, context):
        if context.scene.spectra_usd.profile_mode == "PRESERVED":
            try:
                profile.validate_preserved_export(context.scene, self.filepath)
            except RuntimeError as error:
                self.report({"ERROR"}, str(error))
                return {"CANCELLED"}
        profile.last_error = None
        profile.export_requested = True
        try:
            try:
                result = bpy.ops.wm.usd_export(
                    filepath=self.filepath,
                    selected_objects_only=False,
                    export_animation=False,
                    export_hair=False,
                    export_uvmaps=True,
                    export_normals=True,
                    export_materials=True,
                    export_textures_mode="KEEP",
                    overwrite_textures=False,
                    export_subdivision="IGNORE",
                    use_instancing=True,
                    evaluation_mode="RENDER",
                    generate_preview_surface=True,
                    generate_materialx_network=False,
                    convert_world_material=False,
                    convert_orientation=True,
                    export_global_forward_selection="NEGATIVE_Z",
                    export_global_up_selection="Y",
                    xform_op_mode="MAT",
                    root_prim_path="/World",
                    export_custom_properties=False,
                    author_blender_name=False,
                    triangulate_meshes=True,
                    merge_parent_xform=False,
                    convert_scene_units="METERS",
                    meters_per_unit=1.0,
                )
            except RuntimeError:
                if not profile.last_error:
                    raise
                self.report({"ERROR"}, profile.last_error)
                return {"CANCELLED"}
        finally:
            profile.export_requested = False
        if profile.last_error:
            self.report({"ERROR"}, profile.last_error)
            return {"CANCELLED"}
        return result


class SpectraUsdHook(bpy.types.USDHook):
    bl_idname = "spectra_usd_profile"
    bl_label = "Spectra USD Profile"
    bl_description = "Import and export Spectra USD Profile semantics"

    @staticmethod
    def on_import(import_context):
        if not profile.import_requested:
            return True
        try:
            return profile.import_profile(import_context)
        except Exception as error:
            profile.last_error = str(error)
            raise

    @staticmethod
    def on_export(export_context):
        if not profile.export_requested:
            return True
        try:
            return profile.export_profile(export_context)
        except Exception as error:
            profile.last_error = str(error)
            raise


class SPECTRA_PT_scene(bpy.types.Panel):
    bl_label = "Spectra USD"
    bl_idname = "SPECTRA_PT_scene"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "output"

    def draw(self, context):
        settings = context.scene.spectra_usd
        layout = self.layout
        layout.prop(settings, "profile_mode")
        if settings.profile_mode == "PRESERVED":
            layout.label(text="Advanced Spectra semantics are preserved losslessly", icon="LOCKED")
        layout.prop(settings, "scene_name")
        layout.prop(settings, "samples_per_pixel")
        layout.prop(settings, "sampler_kind")
        layout.prop(settings, "maximum_depth")
        layout.prop(settings, "light_sampler")
        layout.prop(settings, "filter_kind")
        layout.prop(settings, "gbuffer")


class SPECTRA_PT_object(bpy.types.Panel):
    bl_label = "Spectra USD"
    bl_idname = "SPECTRA_PT_object"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"

    @classmethod
    def poll(cls, context):
        return context.object is not None

    def draw(self, context):
        settings = context.object.spectra_usd
        layout = self.layout
        layout.prop(settings, "role")
        if settings.role == "INSTANCE":
            layout.label(text=settings.prototype_path)
        if context.object.type == "MESH":
            layout.prop(settings, "reverse_orientation")
            layout.prop(settings, "area_light")
            if settings.area_light:
                layout.prop(settings, "radiance")
                layout.prop(settings, "light_scale")
                layout.prop(settings, "light_sidedness")


class SPECTRA_PT_camera(bpy.types.Panel):
    bl_label = "Spectra USD"
    bl_idname = "SPECTRA_PT_camera"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "data"

    @classmethod
    def poll(cls, context):
        return context.camera is not None

    def draw(self, context):
        settings = context.camera.spectra_usd
        layout = self.layout
        layout.prop(settings, "exposure_time")
        layout.prop(settings, "vertical_fov")
        layout.prop(settings, "lens_radius")


class SPECTRA_PT_material(bpy.types.Panel):
    bl_label = "Spectra USD"
    bl_idname = "SPECTRA_PT_material"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "material"

    @classmethod
    def poll(cls, context):
        return context.material is not None

    def draw(self, context):
        settings = context.material.spectra_usd
        layout = self.layout
        layout.prop(settings, "kind")
        if settings.shader_id:
            layout.label(text=settings.shader_id)
        layout.prop(settings, "reflectance_encoding")
        layout.prop(settings, "reflectance_color_space")
        if settings.texture:
            layout.label(text=f"Texture: {settings.texture_name} ({settings.texture_type})")


def import_menu(self, context):
    self.layout.operator(SPECTRA_OT_import_usd.bl_idname, text="Spectra USD (.usd/.usda/.usdc)")


def export_menu(self, context):
    self.layout.operator(SPECTRA_OT_export_usd.bl_idname, text="Spectra USD (.usd/.usda/.usdc)")


classes = (
    SpectraSceneSettings,
    SpectraObjectSettings,
    SpectraCollectionSettings,
    SpectraMeshSettings,
    SpectraMaterialSettings,
    SpectraCameraSettings,
    SPECTRA_OT_import_usd,
    SPECTRA_OT_export_usd,
    SpectraUsdHook,
    SPECTRA_PT_scene,
    SPECTRA_PT_object,
    SPECTRA_PT_camera,
    SPECTRA_PT_material,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    bpy.types.Scene.spectra_usd = bpy.props.PointerProperty(type=SpectraSceneSettings)
    bpy.types.Object.spectra_usd = bpy.props.PointerProperty(type=SpectraObjectSettings)
    bpy.types.Collection.spectra_usd = bpy.props.PointerProperty(type=SpectraCollectionSettings)
    bpy.types.Mesh.spectra_usd = bpy.props.PointerProperty(type=SpectraMeshSettings)
    bpy.types.Material.spectra_usd = bpy.props.PointerProperty(type=SpectraMaterialSettings)
    bpy.types.Camera.spectra_usd = bpy.props.PointerProperty(type=SpectraCameraSettings)
    bpy.types.TOPBAR_MT_file_import.append(import_menu)
    bpy.types.TOPBAR_MT_file_export.append(export_menu)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(export_menu)
    bpy.types.TOPBAR_MT_file_import.remove(import_menu)
    del bpy.types.Camera.spectra_usd
    del bpy.types.Material.spectra_usd
    del bpy.types.Mesh.spectra_usd
    del bpy.types.Collection.spectra_usd
    del bpy.types.Object.spectra_usd
    del bpy.types.Scene.spectra_usd
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
