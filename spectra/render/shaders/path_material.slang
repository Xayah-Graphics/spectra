module path_material;

import path_bxdf;
import path_layered;
import path_random;
import path_scene;
import path_spectrum;

struct SurfaceFrame {
    float3 tangent;
    float3 bitangent;
    float3 normal;
}

public struct MaterialGeometry {
    public TextureEvaluationContext texture;
    public float3 geometric_normal;
    public float3 dpdu;
    public float3 dpdv;
    public float3 dndu;
    public float3 dndv;
}

struct MaterialSurface {
    TextureEvaluationContext texture;
    SurfaceFrame frame;
}

[ForceInline]
PathSpectrumParameter spectrum_parameter(
    uint4 parameter) {
    return {
        parameter.x,
        parameter.y,
        parameter.zw,
    };
}

[ForceInline]
PathFloatParameter float_parameter(
    float value,
    uint texture) {
    return {
        value,
        texture,
        uint2(0),
    };
}

public struct MaterialEvaluation {
    public float4 throughput;
    public float pdf;
}

public struct MaterialSample {
    public float4 throughput;
    public float3 wi;
    public float3 shading_normal;
    public float sampling_pdf;
    public float mis_pdf;
    public float eta;
    public uint flags;
    public bool valid;
}

SurfaceFrame make_surface_frame(
    float3 normal,
    float3 tangent) {
    SurfaceFrame result;
    result.tangent = tangent;
    result.bitangent =
        cross(
            normal,
            tangent);
    result.normal = normal;
    return result;
}

float3 to_local(
    SurfaceFrame frame,
    float3 direction) {
    return float3(
        dot(
            direction,
            frame.tangent),
        dot(
            direction,
            frame.bitangent),
        dot(
            direction,
            frame.normal));
}

float3 to_world(
    SurfaceFrame frame,
    float3 direction) {
    return
        direction.x * frame.tangent +
        direction.y * frame.bitangent +
        direction.z * frame.normal;
}

[ForceInline]
float4 material_texture_value(
    PathSceneBindings bindings,
    TextureEvaluationContext texture_context,
    uint texture,
    uint role,
    uint owner) {
    StructuredBuffer<PathMaterialTextureRequest>
        requests =
            bindings.material_texture_requests;
    RWStructuredBuffer<float4> evaluated_values =
        texture_context
            .material_texture_values;
    [loop]
    for (uint request_index = 0;
         request_index !=
             texture_context
                 .texture_requests.y;
         ++request_index) {
        const PathMaterialTextureRequest request =
            requests[
                texture_context
                    .texture_requests.x +
                request_index];
        if (
            request.data.x == texture &&
            request.data.y == role &&
            request.data.z == owner)
            return evaluated_values[
                texture_context
                    .material_texture_value_base +
                request_index];
    }
    return evaluated_values[
        texture_context
            .material_texture_value_base];
}

[ForceInline]
float4 evaluate_parameter(
    PathSpectrumParameter parameter,
    PathSceneBindings bindings,
    StructuredBuffer<PathSpectrum> spectra,
    StructuredBuffer<float2> piecewise_spectra,
    ByteAddressBuffer cie_spectra,
    TextureEvaluationContext texture_context,
    float4 wavelengths) {
    if (parameter.texture != 0xffffffff)
        return material_texture_value(
            bindings,
            texture_context,
            parameter.texture,
            material_texture_parameter,
            0xffffffff);
    return evaluate_spectrum(
        spectra[parameter.spectrum],
        piecewise_spectra,
        cie_spectra,
        wavelengths);
}

[ForceInline]
float evaluate_parameter(
    PathFloatParameter parameter,
    PathSceneBindings bindings,
    TextureEvaluationContext texture_context,
    float4 wavelengths) {
    if (parameter.texture != 0xffffffff)
        return material_texture_value(
            bindings,
            texture_context,
            parameter.texture,
            material_texture_parameter,
            0xffffffff)
            .x;
    return parameter.value;
}

[ForceInline]
MaterialSurface make_material_surface(
    PathSceneBindings bindings,
    uint material_index,
    PathMaterialHeader material,
    MaterialGeometry geometry,
    float4 wavelengths) {
    float3 dpdu = geometry.dpdu;
    float3 dpdv = geometry.dpdv;
    float3 normal = geometry.texture.normal;
    if (material.normal_map != 0xffffffff) {
        const float3 tangent_normal =
            normalize(
                2.0 *
                    material_texture_value(
                        bindings,
                        geometry.texture,
                        material.normal_map,
                        material_texture_normal,
                        material_index)
                        .xyz -
                1.0);
        const float3 frame_tangent =
            normalize(dpdu);
        const float3 frame_bitangent =
            cross(
                normal,
                frame_tangent);
        const float3 mapped_normal =
            normalize(
                tangent_normal.x *
                    frame_tangent +
                tangent_normal.y *
                    frame_bitangent +
                tangent_normal.z *
                    normal);
        const float u_length =
            length(dpdu);
        const float v_length =
            length(dpdv);
        dpdu =
            normalize(
                dpdu -
                dot(
                    dpdu,
                    mapped_normal) *
                    mapped_normal) *
            u_length;
        dpdv =
            normalize(
                cross(
                    mapped_normal,
                    dpdu)) *
            v_length;
        normal =
            normalize(
                cross(
                    dpdu,
                    dpdv));
    }
    else if (material.bump_map != 0xffffffff) {
        const float du_candidate =
            0.5 *
            (
                abs(
                    geometry.texture
                        .texture_coordinates_dx.x) +
                abs(
                    geometry.texture
                        .texture_coordinates_dy.x)
            );
        const float dv_candidate =
            0.5 *
            (
                abs(
                    geometry.texture
                        .texture_coordinates_dx.y) +
                abs(
                    geometry.texture
                        .texture_coordinates_dy.y)
            );
        const float du =
            du_candidate == 0.0
                ? 0.0005
                : du_candidate;
        const float dv =
            dv_candidate == 0.0
                ? 0.0005
                : dv_candidate;
        const float u_displacement =
            material_texture_value(
                bindings,
                geometry.texture,
                material.bump_map,
                material_texture_bump_u,
                material_index)
                .x;
        const float v_displacement =
            material_texture_value(
                bindings,
                geometry.texture,
                material.bump_map,
                material_texture_bump_v,
                material_index)
                .x;
        const float displacement =
            material_texture_value(
                bindings,
                geometry.texture,
                material.bump_map,
                material_texture_bump,
                material_index)
                .x;
        dpdu +=
            (
                u_displacement -
                displacement
            ) / du * normal +
            displacement *
                geometry.dndu;
        dpdv +=
            (
                v_displacement -
                displacement
            ) / dv * normal +
            displacement *
                geometry.dndv;
        normal =
            normalize(
                cross(
                    dpdu,
                    dpdv));
    }
    if (
        dot(
            normal,
            geometry.geometric_normal) < 0.0)
        normal = -normal;
    MaterialSurface result;
    result.texture = geometry.texture;
    result.texture.normal = normal;
    result.frame =
        make_surface_frame(
            normal,
            normalize(dpdu));
    return result;
}

[ForceInline]
TrowbridgeReitzDistribution
material_distribution(
    PathFloatParameter u_roughness,
    PathFloatParameter v_roughness,
    PathSceneBindings bindings,
    TextureEvaluationContext texture_context,
    float4 wavelengths,
    bool remap,
    bool regularize) {
    float alpha_x =
        evaluate_parameter(
            u_roughness,
            bindings,
            texture_context,
            wavelengths);
    float alpha_y =
        evaluate_parameter(
            v_roughness,
            bindings,
            texture_context,
            wavelengths);
    if (remap) {
        alpha_x =
            roughness_to_alpha(alpha_x);
        alpha_y =
            roughness_to_alpha(alpha_y);
    }
    if (regularize) {
        if (alpha_x < 0.3)
            alpha_x =
                clamp(
                    2.0 * alpha_x,
                    0.1,
                    0.3);
        if (alpha_y < 0.3)
            alpha_y =
                clamp(
                    2.0 * alpha_y,
                    0.1,
                    0.3);
    }
    return make_trowbridge_reitz(
        alpha_x,
        alpha_y);
}

[ForceInline]
void conductor_optics(
    PathConductorMaterial material,
    PathSceneBindings bindings,
    StructuredBuffer<PathSpectrum> spectra,
    StructuredBuffer<float2> piecewise_spectra,
    ByteAddressBuffer cie_spectra,
    TextureEvaluationContext texture_context,
    float4 wavelengths,
    out float4 eta,
    out float4 k) {
    if (material.metadata.x == 0) {
        eta =
            evaluate_parameter(
                spectrum_parameter(
                    material.eta_or_reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                texture_context,
                wavelengths);
        k =
            evaluate_parameter(
                spectrum_parameter(material.k),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                texture_context,
                wavelengths);
    }
    else {
        const float4 reflectance =
            clamp(
                evaluate_parameter(
                    spectrum_parameter(
                        material.eta_or_reflectance),
                    bindings,
                    spectra,
                    piecewise_spectra,
                    cie_spectra,
                    texture_context,
                    wavelengths),
                float4(0.0),
                float4(0.9999));
        eta = float4(1.0);
        k =
            2.0 *
            sqrt(reflectance) /
            sqrt(
                max(
                    float4(1.0) -
                        reflectance,
                    float4(1.0e-8)));
    }
}

[ForceInline]
bool spectrum_parameter_terminates_secondary(
    PathSpectrumParameter parameter,
    PathSceneBindings bindings) {
    if (parameter.texture != 0xffffffff)
        return true;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    return
        spectra[parameter.spectrum]
            .metadata.x != 1;
}

[ForceInline]
bool leaf_terminates_secondary(
    PathSceneBindings bindings,
    PathMaterialHeader header) {
    if (header.kind == material_dielectric) {
        StructuredBuffer<PathDielectricMaterial>
            materials =
                bindings.dielectric_materials;
        return
            spectrum_parameter_terminates_secondary(
                spectrum_parameter(
                    materials[
                        header.local_index]
                        .eta),
                bindings);
    }
    if (
        header.kind ==
        material_thin_dielectric) {
        StructuredBuffer<
            PathThinDielectricMaterial>
            materials =
                bindings
                    .thin_dielectric_materials;
        return
            spectrum_parameter_terminates_secondary(
                spectrum_parameter(
                    materials[
                        header.local_index]
                        .eta),
                bindings);
    }
    if (
        header.kind ==
        material_coated_diffuse) {
        StructuredBuffer<
            PathCoatedDiffuseMaterial>
            materials =
                bindings
                    .coated_diffuse_materials;
        return
            spectrum_parameter_terminates_secondary(
                spectrum_parameter(
                    materials[
                        header.local_index]
                        .eta),
                bindings);
    }
    if (
        header.kind ==
        material_coated_conductor) {
        StructuredBuffer<
            PathCoatedConductorMaterial>
            materials =
                bindings
                    .coated_conductor_materials;
        return
            spectrum_parameter_terminates_secondary(
                spectrum_parameter(
                    materials[
                        header.local_index]
                        .interface_eta),
                bindings);
    }
    return false;
}

[ForceInline]
BxdfEvaluation evaluate_diffuse_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths) {
    StructuredBuffer<PathDiffuseMaterial>
        materials =
            bindings.diffuse_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDiffuseMaterial material =
        materials[local_index];
    return evaluate_diffuse(
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths),
            float4(0.0),
            float4(1.0)),
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_diffuse_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float2 direction_sample) {
    StructuredBuffer<PathDiffuseMaterial>
        materials =
            bindings.diffuse_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDiffuseMaterial material =
        materials[local_index];
    return sample_diffuse(
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths),
            float4(0.0),
            float4(1.0)),
        outgoing,
        direction_sample);
}

[ForceInline]
BxdfEvaluation
evaluate_diffuse_transmission_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths) {
    StructuredBuffer<
        PathDiffuseTransmissionMaterial>
        materials =
            bindings
                .diffuse_transmission_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDiffuseTransmissionMaterial material =
        materials[local_index];
    return evaluate_diffuse_transmission(
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths) *
                material.scales.x,
            float4(0.0),
            float4(1.0)),
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.transmittance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths) *
                material.scales.y,
            float4(0.0),
            float4(1.0)),
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_diffuse_transmission_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample) {
    StructuredBuffer<
        PathDiffuseTransmissionMaterial>
        materials =
            bindings
                .diffuse_transmission_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDiffuseTransmissionMaterial material =
        materials[local_index];
    return sample_diffuse_transmission(
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths) *
                material.scales.x,
            float4(0.0),
            float4(1.0)),
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.transmittance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths) *
                material.scales.y,
            float4(0.0),
            float4(1.0)),
        outgoing,
        component_sample,
        direction_sample);
}

[ForceInline]
BxdfEvaluation evaluate_conductor_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths,
    bool regularize) {
    StructuredBuffer<PathConductorMaterial>
        materials =
            bindings.conductor_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathConductorMaterial material =
        materials[local_index];
    float4 eta;
    float4 k;
    conductor_optics(
        material,
        bindings,
        spectra,
        piecewise_spectra,
        cie_spectra,
        surface.texture,
        wavelengths,
        eta,
        k);
    return evaluate_conductor(
        material_distribution(
            float_parameter(
                material.roughness.x,
                material.roughness_textures.x),
            float_parameter(
                material.roughness.y,
                material.roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.y != 0,
            regularize),
        eta,
        k,
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_conductor_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float2 direction_sample,
    bool regularize) {
    StructuredBuffer<PathConductorMaterial>
        materials =
            bindings.conductor_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathConductorMaterial material =
        materials[local_index];
    float4 eta;
    float4 k;
    conductor_optics(
        material,
        bindings,
        spectra,
        piecewise_spectra,
        cie_spectra,
        surface.texture,
        wavelengths,
        eta,
        k);
    return sample_conductor(
        material_distribution(
            float_parameter(
                material.roughness.x,
                material.roughness_textures.x),
            float_parameter(
                material.roughness.y,
                material.roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.y != 0,
            regularize),
        eta,
        k,
        outgoing,
        direction_sample);
}

[ForceInline]
BxdfEvaluation evaluate_dielectric_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths,
    bool regularize) {
    StructuredBuffer<PathDielectricMaterial>
        materials =
            bindings.dielectric_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDielectricMaterial material =
        materials[local_index];
    float eta =
        evaluate_parameter(
            spectrum_parameter(material.eta),
            bindings,
            spectra,
            piecewise_spectra,
            cie_spectra,
            surface.texture,
            wavelengths)
            .x;
    if (eta == 0.0) eta = 1.0;
    return evaluate_dielectric(
        material_distribution(
            float_parameter(
                material.roughness.x,
                material.roughness_textures.x),
            float_parameter(
                material.roughness.y,
                material.roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.x != 0,
            regularize),
        eta,
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_dielectric_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample,
    bool regularize) {
    StructuredBuffer<PathDielectricMaterial>
        materials =
            bindings.dielectric_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathDielectricMaterial material =
        materials[local_index];
    float eta =
        evaluate_parameter(
            spectrum_parameter(material.eta),
            bindings,
            spectra,
            piecewise_spectra,
            cie_spectra,
            surface.texture,
            wavelengths)
            .x;
    if (eta == 0.0) eta = 1.0;
    return sample_dielectric(
        material_distribution(
            float_parameter(
                material.roughness.x,
                material.roughness_textures.x),
            float_parameter(
                material.roughness.y,
                material.roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.x != 0,
            regularize),
        eta,
        outgoing,
        component_sample,
        direction_sample);
}

[ForceInline]
BxdfSample sample_thin_dielectric_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float component_sample) {
    StructuredBuffer<PathThinDielectricMaterial>
        materials =
            bindings.thin_dielectric_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathThinDielectricMaterial material =
        materials[local_index];
    float eta =
        evaluate_parameter(
            spectrum_parameter(material.eta),
            bindings,
            spectra,
            piecewise_spectra,
            cie_spectra,
            surface.texture,
            wavelengths)
            .x;
    if (eta == 0.0) eta = 1.0;
    return sample_thin_dielectric(
        eta,
        outgoing,
        component_sample);
}

[ForceInline]
LayeredBxdf coated_diffuse_bxdf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float4 wavelengths,
    bool regularize) {
    StructuredBuffer<PathCoatedDiffuseMaterial>
        materials =
            bindings.coated_diffuse_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathCoatedDiffuseMaterial material =
        materials[local_index];
    LayeredBxdf result;
    result.interface_distribution =
        material_distribution(
            float_parameter(
                material.parameters.x,
                material.parameter_textures.x),
            float_parameter(
                material.parameters.y,
                material.parameter_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.x != 0,
            regularize);
    result.conductor_distribution =
        make_trowbridge_reitz(
            0.0,
            0.0);
    result.interface_eta =
        evaluate_parameter(
            spectrum_parameter(material.eta),
            bindings,
            spectra,
            piecewise_spectra,
            cie_spectra,
            surface.texture,
            wavelengths)
            .x;
    if (result.interface_eta == 0.0)
        result.interface_eta = 1.0;
    result.diffuse_reflectance =
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths),
            float4(0.0),
            float4(1.0));
    result.conductor_eta = float4(0.0);
    result.conductor_k = float4(0.0);
    result.thickness =
        max(
            evaluate_parameter(
                float_parameter(
                    material.parameters.z,
                    material.parameter_textures.z),
                bindings,
                surface.texture,
                wavelengths),
            1.175494351e-38);
    result.albedo =
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.albedo),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths),
            float4(0.0),
            float4(1.0));
    result.anisotropy =
        clamp(
            evaluate_parameter(
                float_parameter(
                    material.parameters.w,
                    material.parameter_textures.w),
                bindings,
                surface.texture,
                wavelengths),
            -1.0,
            1.0);
    result.max_depth = material.metadata.y;
    result.sample_count = material.metadata.z;
    result.bottom_kind =
        layered_bottom_diffuse;
    return result;
}

[ForceInline]
LayeredBxdf coated_conductor_bxdf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float4 wavelengths,
    bool regularize) {
    StructuredBuffer<
        PathCoatedConductorMaterial>
        materials =
            bindings
                .coated_conductor_materials;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    const PathCoatedConductorMaterial material =
        materials[local_index];
    LayeredBxdf result;
    result.interface_distribution =
        material_distribution(
            float_parameter(
                material.interface_roughness.x,
                material
                    .interface_roughness_textures.x),
            float_parameter(
                material.interface_roughness.y,
                material
                    .interface_roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.y != 0,
            regularize);
    result.conductor_distribution =
        material_distribution(
            float_parameter(
                material.conductor_roughness.x,
                material
                    .conductor_roughness_textures.x),
            float_parameter(
                material.conductor_roughness.y,
                material
                    .conductor_roughness_textures.y),
            bindings,
            surface.texture,
            wavelengths,
            material.metadata.y != 0,
            regularize);
    result.interface_eta =
        evaluate_parameter(
            spectrum_parameter(
                material.interface_eta),
            bindings,
            spectra,
            piecewise_spectra,
            cie_spectra,
            surface.texture,
            wavelengths)
            .x;
    if (result.interface_eta == 0.0)
        result.interface_eta = 1.0;
    result.diffuse_reflectance = float4(0.0);
    if (material.metadata.x == 0) {
        result.conductor_eta =
            evaluate_parameter(
                spectrum_parameter(
                    material
                        .eta_or_reflectance),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths);
        result.conductor_k =
            evaluate_parameter(
                spectrum_parameter(material.k),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths);
    }
    else {
        const float4 reflectance =
            clamp(
                evaluate_parameter(
                    spectrum_parameter(
                        material
                            .eta_or_reflectance),
                    bindings,
                    spectra,
                    piecewise_spectra,
                    cie_spectra,
                    surface.texture,
                    wavelengths),
                float4(0.0),
                float4(0.9999));
        result.conductor_eta = float4(1.0);
        result.conductor_k =
            2.0 *
            sqrt(reflectance) /
            sqrt(
                max(
                    float4(1.0) -
                        reflectance,
                    float4(
                        1.175494351e-38)));
    }
    result.conductor_eta /=
        result.interface_eta;
    result.conductor_k /=
        result.interface_eta;
    result.thickness =
        max(
            evaluate_parameter(
                float_parameter(
                    material.coating.x,
                    material.coating_textures.x),
                bindings,
                surface.texture,
                wavelengths),
            1.175494351e-38);
    result.albedo =
        clamp(
            evaluate_parameter(
                spectrum_parameter(
                    material.albedo),
                bindings,
                spectra,
                piecewise_spectra,
                cie_spectra,
                surface.texture,
                wavelengths),
            float4(0.0),
            float4(1.0));
    result.anisotropy =
        clamp(
            evaluate_parameter(
                float_parameter(
                    material.coating.y,
                    material.coating_textures.y),
                bindings,
                surface.texture,
                wavelengths),
            -1.0,
            1.0);
    result.max_depth = material.metadata.z;
    result.sample_count = material.metadata.w;
    result.bottom_kind =
        layered_bottom_conductor;
    return result;
}

[ForceInline]
BxdfEvaluation evaluate_coated_diffuse_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths,
    bool regularize) {
    return evaluate_layered(
        coated_diffuse_bxdf(
            bindings,
            local_index,
            surface,
            wavelengths,
            regularize),
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_coated_diffuse_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample,
    bool regularize) {
    return sample_layered(
        coated_diffuse_bxdf(
            bindings,
            local_index,
            surface,
            wavelengths,
            regularize),
        outgoing,
        component_sample,
        direction_sample);
}

[ForceInline]
BxdfEvaluation evaluate_coated_conductor_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float3 incident,
    float4 wavelengths,
    bool regularize) {
    return evaluate_layered(
        coated_conductor_bxdf(
            bindings,
            local_index,
            surface,
            wavelengths,
            regularize),
        outgoing,
        incident);
}

[ForceInline]
BxdfSample sample_coated_conductor_leaf(
    PathSceneBindings bindings,
    uint local_index,
    MaterialSurface surface,
    float3 outgoing,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample,
    bool regularize) {
    return sample_layered(
        coated_conductor_bxdf(
            bindings,
            local_index,
            surface,
            wavelengths,
            regularize),
        outgoing,
        component_sample,
        direction_sample);
}

[ForceInline]
BxdfEvaluation evaluate_leaf(
    PathSceneBindings bindings,
    PathMaterialHeader header,
    MaterialSurface surface,
    float3 outgoing_world,
    float3 incident_world,
    float4 wavelengths,
    bool regularize) {
    const float3 outgoing =
        to_local(
            surface.frame,
            outgoing_world);
    const float3 incident =
        to_local(
            surface.frame,
            incident_world);
    if (outgoing.z == 0.0)
        return empty_bxdf_evaluation();
    if (header.kind == material_diffuse)
        return evaluate_diffuse_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths);
    if (
        header.kind ==
        material_diffuse_transmission)
        return evaluate_diffuse_transmission_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths);
    if (header.kind == material_conductor)
        return evaluate_conductor_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths,
            regularize);
    if (header.kind == material_dielectric)
        return evaluate_dielectric_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths,
            regularize);
    if (
        header.kind ==
        material_coated_diffuse)
        return evaluate_coated_diffuse_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths,
            regularize);
    if (
        header.kind ==
        material_coated_conductor)
        return evaluate_coated_conductor_leaf(
            bindings,
            header.local_index,
            surface,
            outgoing,
            incident,
            wavelengths,
            regularize);
    return empty_bxdf_evaluation();
}
[ForceInline]
BxdfSample sample_leaf(
    PathSceneBindings bindings,
    PathMaterialHeader header,
    MaterialSurface surface,
    float3 outgoing_world,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample,
    bool regularize) {
    const float3 outgoing =
        to_local(
            surface.frame,
            outgoing_world);
    if (outgoing.z == 0.0)
        return invalid_bxdf_sample();
    BxdfSample result =
        invalid_bxdf_sample();
    if (header.kind == material_interface) {
        result.wi = -outgoing;
        result.f =
            float4(
                1.0 /
                absolute_cosine(
                    result.wi));
        result.pdf = 1.0;
        result.eta = 1.0;
        result.flags =
            bxdf_transmission |
            bxdf_specular;
        result.valid = true;
    }
    else if (header.kind == material_diffuse)
        result =
            sample_diffuse_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                direction_sample);
    else if (
        header.kind ==
        material_diffuse_transmission)
        result =
            sample_diffuse_transmission_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                component_sample,
                direction_sample);
    else if (header.kind == material_conductor)
        result =
            sample_conductor_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                direction_sample,
                regularize);
    else if (header.kind == material_dielectric)
        result =
            sample_dielectric_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                component_sample,
                direction_sample,
                regularize);
    else if (
        header.kind ==
        material_thin_dielectric)
        result =
            sample_thin_dielectric_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                component_sample);
    else if (
        header.kind ==
        material_coated_diffuse)
        result =
            sample_coated_diffuse_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                component_sample,
                direction_sample,
                regularize);
    else if (
        header.kind ==
        material_coated_conductor)
        result =
            sample_coated_conductor_leaf(
                bindings,
                header.local_index,
                surface,
                outgoing,
                wavelengths,
                component_sample,
                direction_sample,
                regularize);
    if (
        !result.valid ||
        result.pdf == 0.0 ||
        result.wi.z == 0.0 ||
        !any(
            result.f !=
            float4(0.0)))
        return invalid_bxdf_sample();
    result.wi =
        to_world(
            surface.frame,
            result.wi);
    return result;
}
[ForceInline]
uint resolve_material(
    PathSceneBindings bindings,
    uint material_index,
    TextureEvaluationContext texture_context,
    float3 outgoing_world,
    float4 wavelengths) {
    StructuredBuffer<PathMaterialHeader>
        headers =
            bindings.material_headers;
    StructuredBuffer<PathMixMaterial>
        mixes =
            bindings.mix_materials;
    [loop]
    while (
        headers[material_index].kind ==
        material_mix) {
        const PathMixMaterial material =
            mixes[
                headers[material_index]
                    .local_index];
        const float amount =
            evaluate_parameter(
                float_parameter(
                    material.amount,
                    material.amount_texture),
                bindings,
                texture_context,
                wavelengths);
        if (amount <= 0.0) {
            material_index =
                material.materials.x;
            continue;
        }
        if (amount >= 1.0) {
            material_index =
                material.materials.y;
            continue;
        }
        const PathMaterialHeader first =
            headers[material.materials.x];
        const PathMaterialHeader second =
            headers[material.materials.y];
        const float choice =
            hash_float_material_choice(
                texture_context.position,
                outgoing_world,
                first.stable_id,
                second.stable_id);
        material_index =
            amount < choice
                ? material.materials.x
                : material.materials.y;
    }
    return material_index;
}

[ForceInline]
public bool material_terminates_secondary(
    PathSceneBindings bindings,
    uint material_index,
    TextureEvaluationContext texture_context,
    float3 outgoing_world,
    float4 wavelengths) {
    StructuredBuffer<PathMaterialHeader>
        headers =
            bindings.material_headers;
    material_index =
        resolve_material(
            bindings,
            material_index,
            texture_context,
            outgoing_world,
            wavelengths);
    return leaf_terminates_secondary(
        bindings,
        headers[material_index]);
}

[ForceInline]
public MaterialEvaluation evaluate_material(
    PathSceneBindings bindings,
    uint material_index,
    MaterialGeometry geometry,
    float3 outgoing_world,
    float3 incident_world,
    float4 wavelengths,
    bool regularize) {
    StructuredBuffer<PathMaterialHeader>
        headers =
            bindings.material_headers;
    material_index =
        resolve_material(
            bindings,
            material_index,
            geometry.texture,
            outgoing_world,
            wavelengths);
    const PathMaterialHeader header =
        headers[material_index];
    const MaterialSurface surface =
        make_material_surface(
            bindings,
            material_index,
            header,
            geometry,
            wavelengths);
    const BxdfEvaluation evaluation =
        evaluate_leaf(
            bindings,
            header,
            surface,
            outgoing_world,
            incident_world,
            wavelengths,
            regularize);
    MaterialEvaluation result;
    result.throughput =
        evaluation.f *
        abs(
            dot(
                surface.frame.normal,
                incident_world));
    result.pdf = evaluation.pdf;
    return result;
}

[ForceInline]
public MaterialSample sample_material(
    PathSceneBindings bindings,
    uint material_index,
    MaterialGeometry geometry,
    float3 outgoing_world,
    float4 wavelengths,
    float component_sample,
    float2 direction_sample,
    bool regularize) {
    StructuredBuffer<PathMaterialHeader>
        headers =
            bindings.material_headers;
    material_index =
        resolve_material(
            bindings,
            material_index,
            geometry.texture,
            outgoing_world,
            wavelengths);
    const PathMaterialHeader header =
        headers[material_index];
    const MaterialSurface surface =
        make_material_surface(
            bindings,
            material_index,
            header,
            geometry,
            wavelengths);
    const BxdfSample leaf_sample =
        sample_leaf(
            bindings,
            header,
            surface,
            outgoing_world,
            wavelengths,
            component_sample,
            direction_sample,
            regularize);
    MaterialSample result;
    result.throughput = float4(0.0);
    result.wi = leaf_sample.wi;
    result.shading_normal =
        surface.frame.normal;
    result.sampling_pdf = 0.0;
    result.mis_pdf = 0.0;
    result.eta = leaf_sample.eta;
    result.flags = leaf_sample.flags;
    result.valid = leaf_sample.valid;
    if (!result.valid)
        return result;
    result.throughput =
        leaf_sample.f *
        abs(
            dot(
                surface.frame.normal,
                leaf_sample.wi));
    result.sampling_pdf =
        leaf_sample.pdf;
    result.mis_pdf =
        leaf_sample.pdf;
    if (
        leaf_sample
            .pdf_is_proportional)
        result.mis_pdf =
            evaluate_leaf(
                bindings,
                header,
                surface,
                outgoing_world,
                leaf_sample.wi,
                wavelengths,
                regularize)
                .pdf;
    result.valid =
        result.sampling_pdf > 0.0 &&
        result.mis_pdf > 0.0 &&
            any(
                result.throughput >
                float4(0.0));
    return result;
}

public struct MaterialAlbedo {
    public float4 spectrum;
    public float3 shading_normal;
    public bool valid;
}

[ForceInline]
public MaterialAlbedo estimate_material_albedo(
    PathSceneBindings bindings,
    uint material_index,
    MaterialGeometry geometry,
    float3 outgoing_world,
    float4 wavelengths) {
    static const float component_samples[16] = {
        0.75741637,
        0.37870818,
        0.7083487,
        0.18935409,
        0.9149363,
        0.35417435,
        0.5990858,
        0.09467703,
        0.8578725,
        0.45746812,
        0.686759,
        0.17708716,
        0.9674518,
        0.2995429,
        0.5083201,
        0.047338516,
    };
    static const float2 direction_samples[16] = {
        float2(0.855985, 0.570367),
        float2(0.381823, 0.851844),
        float2(0.285328, 0.764262),
        float2(0.733380, 0.114073),
        float2(0.542663, 0.344465),
        float2(0.127274, 0.414848),
        float2(0.964700, 0.947162),
        float2(0.594089, 0.643463),
        float2(0.095109, 0.170369),
        float2(0.825444, 0.263359),
        float2(0.429467, 0.454469),
        float2(0.244460, 0.816459),
        float2(0.756135, 0.731258),
        float2(0.516165, 0.152852),
        float2(0.180888, 0.214174),
        float2(0.898579, 0.503897),
    };
    MaterialAlbedo result;
    result.spectrum = float4(0.0);
    StructuredBuffer<PathMaterialHeader>
        headers =
            bindings.material_headers;
    material_index =
        resolve_material(
            bindings,
            material_index,
            geometry.texture,
            outgoing_world,
            wavelengths);
    result.shading_normal =
        make_material_surface(
            bindings,
            material_index,
            headers[material_index],
            geometry,
            wavelengths)
            .frame.normal;
    result.valid = true;
    float3 sampled_normal = float3(0.0);
    uint valid_samples = 0;
    [loop]
    for (uint sample_index = 0;
         sample_index != 16;
         ++sample_index) {
        const MaterialSample sample =
            sample_material(
                bindings,
                material_index,
                geometry,
                outgoing_world,
                wavelengths,
                component_samples[
                    sample_index],
                direction_samples[
                    sample_index],
                false);
        if (!sample.valid) continue;
        result.spectrum +=
            sample.throughput /
            sample.sampling_pdf;
        sampled_normal +=
            sample.shading_normal;
        ++valid_samples;
    }
    result.spectrum /= 16.0;
    if (valid_samples != 0)
        result.shading_normal =
            normalize(
                sampled_normal);
    return result;
}
