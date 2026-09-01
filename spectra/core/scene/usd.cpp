module;

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/blackbody.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/usd/usdLux/meshLightAPI.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdRender/product.h>
#include <pxr/usd/usdRender/settings.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdVol/openVDBAsset.h>
#include <pxr/usd/usdVol/tokens.h>
#include <pxr/usd/usdVol/volume.h>

#if defined(interface)
#undef interface
#endif

module spectra.scene.usd;

import std;

namespace spectra::scene {
    namespace {
        constexpr std::uint32_t profile_version = 2u;

        struct Paths {
            std::unordered_map<std::uint64_t, pxr::SdfPath> geometries{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> sphere_sets{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> particle_sets{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> volumes{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> neural_fields{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> textures{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> materials{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> media{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> lights{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> cameras{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> films{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> samplers{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> prototypes{};
            std::unordered_map<std::uint64_t, pxr::SdfPath> instances{};
        };

        [[nodiscard]] pxr::TfToken token(const std::string_view value) {
            return pxr::TfToken{std::string{value}};
        }

        [[nodiscard]] pxr::SdfPath child_path(const pxr::SdfPath& parent, const std::string& name, const std::string_view default_identifier, std::unordered_set<std::string>& identifiers) {
            std::string identifier = pxr::TfMakeValidIdentifier(name);
            if (identifier.empty() || identifier == "_") identifier = std::string{default_identifier};
            const std::string base = identifier;
            for (std::uint32_t suffix = 2u; identifiers.contains(identifier); ++suffix) identifier = std::format("{}_{}", base, suffix);
            identifiers.emplace(identifier);
            return parent.AppendChild(pxr::TfToken{identifier});
        }

        template <typename Type>
        void set_attribute(const pxr::UsdPrim& prim, const std::string_view name, const pxr::SdfValueTypeName& type, const Type& value) {
            prim.CreateAttribute(token(name), type, true).Set(value);
        }

        void set_relationship(const pxr::UsdPrim& prim, const std::string_view name, const pxr::SdfPath& target) {
            prim.CreateRelationship(token(name), true).SetTargets({target});
        }

        void set_optional_relationship(const pxr::UsdPrim& prim, const std::string_view name, const std::uint64_t id, const std::unordered_map<std::uint64_t, pxr::SdfPath>& paths) {
            if (id != 0u) set_relationship(prim, name, paths.at(id));
        }

        void set_name(const pxr::UsdPrim& prim, const std::string& name) {
            prim.SetDisplayName(name);
            set_attribute(prim, "spectra:name", pxr::SdfValueTypeNames->String, name);
        }

        [[nodiscard]] pxr::GfVec2f usd(const math::Float2 value) {
            return {value.x, value.y};
        }

        [[nodiscard]] pxr::GfVec3f usd(const math::Float3 value) {
            return {value.x, value.y, value.z};
        }

        [[nodiscard]] pxr::GfVec4f usd(const math::Float4 value) {
            return {value.x, value.y, value.z, value.w};
        }

        [[nodiscard]] pxr::GfVec3i usd(const math::UInt3 value) {
            return {static_cast<int>(value.x), static_cast<int>(value.y), static_cast<int>(value.z)};
        }

        [[nodiscard]] pxr::GfMatrix4d usd(const math::Transform& transform) {
            pxr::GfMatrix4d result{};
            for (std::uint32_t row = 0u; row != 4u; ++row)
                for (std::uint32_t column = 0u; column != 4u; ++column) result[row][column] = transform.matrix[column * 4u + row];
            return result;
        }

        [[nodiscard]] pxr::VtArray<pxr::GfVec3f> usd(const std::vector<math::Float3>& values) {
            pxr::VtArray<pxr::GfVec3f> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = usd(values[index]);
            return result;
        }

        [[nodiscard]] pxr::VtArray<pxr::GfVec2f> usd(const std::vector<math::Float2>& values) {
            pxr::VtArray<pxr::GfVec2f> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = usd(values[index]);
            return result;
        }

        [[nodiscard]] pxr::VtArray<int> usd_indices(const std::vector<std::uint32_t>& values) {
            pxr::VtArray<int> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = static_cast<int>(values[index]);
            return result;
        }

        [[nodiscard]] pxr::VtArray<unsigned int> usd_uints(const std::vector<std::uint32_t>& values) {
            return {values.begin(), values.end()};
        }

        [[nodiscard]] pxr::VtArray<float> usd_floats(const std::vector<float>& values) {
            return {values.begin(), values.end()};
        }

        void set_transform(const pxr::UsdPrim& prim, const math::Transform& transform) {
            pxr::UsdGeomXformable{prim}.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble).Set(usd(transform));
            set_attribute(prim, "spectra:transform", pxr::SdfValueTypeNames->Matrix4d, usd(transform));
        }

        void set_directional_light_transform(const pxr::UsdPrim& prim, const math::Transform& transform) {
            const math::Transform reverse_z{{-1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
            pxr::UsdGeomXformable{prim}.AddTransformOp(pxr::UsdGeomXformOp::PrecisionDouble).Set(usd(transform * reverse_z));
            set_attribute(prim, "spectra:transform", pxr::SdfValueTypeNames->Matrix4d, usd(transform));
        }

        void set_revision(const pxr::UsdPrim& prim, const ResourceRevision revision) {
            set_attribute(prim, "spectra:revision:content", pxr::SdfValueTypeNames->UInt64, revision.content);
            set_attribute(prim, "spectra:revision:topology", pxr::SdfValueTypeNames->UInt64, revision.topology);
        }

        template <typename Enum, std::size_t Size>
        [[nodiscard]] pxr::TfToken enum_token(const Enum value, const std::array<std::string_view, Size>& names) {
            return token(names[std::to_underlying(value)]);
        }

        constexpr std::array<std::string_view, 6> spectrum_encodings{"rgbAlbedo", "rgbUnbounded", "rgbIlluminant", "constant", "blackbody", "piecewiseLinear"};
        constexpr std::array<std::string_view, 3> spectrum_color_spaces{"sRGB", "Rec2020", "ACES2065-1"};
        constexpr std::array<std::string_view, 2> texture_value_kinds{"float", "spectrum"};
        constexpr std::array<std::string_view, 3> texture_spectrum_types{"albedo", "unbounded", "illuminant"};
        constexpr std::array<std::string_view, 4> texture_color_spaces{"linear", "sRGB", "ACES2065-1", "Rec2020"};
        constexpr std::array<std::string_view, 3> texture_wrap_modes{"repeat", "clamp", "black"};
        constexpr std::array<std::string_view, 6> texture_channels{"red", "green", "blue", "alpha", "average", "luminance"};
        constexpr std::array<std::string_view, 4> texture_filters{"point", "bilinear", "trilinear", "ewa"};
        constexpr std::array<std::string_view, 2> volume_sampling{"cell", "vertex"};
        constexpr std::array<std::string_view, 3> vector_spaces{"grid", "local", "world"};
        constexpr std::array<std::string_view, 6> field_kinds{"float", "float2", "float3", "float4", "uint32", "macFloat3"};
        constexpr std::array<std::string_view, 9> diagnostic_modes{"off", "slice", "cells", "rayMarch", "maximumIntensityProjection", "isosurface", "glyphs", "streamlines", "lic"};
        constexpr std::array<std::string_view, 8> field_mappings{"value", "magnitude", "x", "y", "z", "divergence", "curlMagnitude", "qCriterion"};
        constexpr std::array<std::string_view, 3> depth_modes{"tested", "xRay", "overlay"};
        constexpr std::array<std::string_view, 4> color_maps{"viridis", "turbo", "coolWarm", "grayscale"};
        constexpr std::array<std::string_view, 3> particle_displays{"points", "discs", "spheres"};
        constexpr std::array<std::string_view, 5> filter_kinds{"box", "gaussian", "mitchell", "sinc", "triangle"};
        constexpr std::array<std::string_view, 7> sampler_kinds{"independent", "stratified", "halton", "sobol", "paddedSobol", "zSobol", "pmj02bn"};
        constexpr std::array<std::string_view, 4> sampler_randomizations{"none", "permuteDigits", "fastOwen", "owen"};
        constexpr std::array<std::string_view, 3> light_sampler_kinds{"uniform", "power", "bvh"};
        constexpr std::array<std::string_view, 3> color_sources{"element", "uniform", "scalar"};
        constexpr std::array<std::string_view, 2> composition_domains{"sceneLinear", "displayReferred"};
        constexpr std::array<std::string_view, 5> parameter_kinds{"boolean", "integer", "float", "float3", "enumeration"};
        constexpr std::array<std::string_view, 4> derived_mesh_modes{"wireframe", "vertices", "vertexNormals", "faceNormals"};

        void set_spectrum(const pxr::UsdPrim& prim, const std::string_view name, const SpectrumParameter& spectrum, const Paths& paths) {
            const std::string prefix{name};
            set_attribute(prim, prefix + ":value", pxr::SdfValueTypeNames->Color3f, usd(spectrum.value));
            set_attribute(prim, prefix + ":encoding", pxr::SdfValueTypeNames->Token, enum_token(spectrum.encoding, spectrum_encodings));
            set_attribute(prim, prefix + ":colorSpace", pxr::SdfValueTypeNames->Token, enum_token(spectrum.color_space, spectrum_color_spaces));
            set_attribute(prim, prefix + ":scalar", pxr::SdfValueTypeNames->Float, spectrum.scalar);
            set_attribute(prim, prefix + ":temperature", pxr::SdfValueTypeNames->Float, spectrum.temperature);
            set_attribute(prim, prefix + ":wavelengths", pxr::SdfValueTypeNames->FloatArray, usd_floats(spectrum.wavelengths));
            set_attribute(prim, prefix + ":samples", pxr::SdfValueTypeNames->FloatArray, usd_floats(spectrum.samples));
            set_optional_relationship(prim, prefix + ":texture", spectrum.texture.value, paths.textures);
        }

        void set_float_parameter(const pxr::UsdPrim& prim, const std::string_view name, const FloatParameter parameter, const Paths& paths) {
            const std::string prefix{name};
            set_attribute(prim, prefix + ":value", pxr::SdfValueTypeNames->Float, parameter.value);
            set_optional_relationship(prim, prefix + ":texture", parameter.texture.value, paths.textures);
        }

        void set_texture_mapping(const pxr::UsdPrim& prim, const std::string_view name, const TextureMapping& mapping) {
            const std::string prefix{name};
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, UvTextureMapping>) {
                        set_attribute(prim, prefix + ":type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"uv"});
                        set_attribute(prim, prefix + ":scale", pxr::SdfValueTypeNames->Float2, usd(data.scale));
                        set_attribute(prim, prefix + ":offset", pxr::SdfValueTypeNames->Float2, usd(data.offset));
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PlanarTextureMapping>) {
                        set_attribute(prim, prefix + ":type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"planar"});
                        set_attribute(prim, prefix + ":firstAxis", pxr::SdfValueTypeNames->Vector3f, usd(data.first_axis));
                        set_attribute(prim, prefix + ":secondAxis", pxr::SdfValueTypeNames->Vector3f, usd(data.second_axis));
                        set_attribute(prim, prefix + ":offset", pxr::SdfValueTypeNames->Float2, usd(data.offset));
                        set_attribute(prim, prefix + ":textureFromRender", pxr::SdfValueTypeNames->Matrix4d, usd(data.texture_from_render));
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphericalTextureMapping>) {
                        set_attribute(prim, prefix + ":type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"spherical"});
                        set_attribute(prim, prefix + ":textureFromRender", pxr::SdfValueTypeNames->Matrix4d, usd(data.texture_from_render));
                    } else {
                        set_attribute(prim, prefix + ":type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"cylindrical"});
                        set_attribute(prim, prefix + ":textureFromRender", pxr::SdfValueTypeNames->Matrix4d, usd(data.texture_from_render));
                    }
                },
                mapping.data);
        }

        void set_checker_mapping(const pxr::UsdPrim& prim, const CheckerboardMapping& mapping) {
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TextureMapping>) set_texture_mapping(prim, "spectra:mapping", data);
                    else {
                        set_attribute(prim, "spectra:mapping:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"threeDimensional"});
                        set_attribute(prim, "spectra:mapping:textureFromRender", pxr::SdfValueTypeNames->Matrix4d, usd(data.texture_from_render));
                    }
                },
                mapping.data);
        }

        void set_roughness(const pxr::UsdPrim& prim, const std::string_view name, const MaterialRoughness& roughness, const Paths& paths) {
            const std::string prefix{name};
            set_float_parameter(prim, prefix + ":roughness", roughness.roughness, paths);
            set_attribute(prim, prefix + ":anisotropic", pxr::SdfValueTypeNames->Bool, roughness.u_roughness.has_value());
            if (roughness.u_roughness) set_float_parameter(prim, prefix + ":uRoughness", *roughness.u_roughness, paths);
            if (roughness.v_roughness) set_float_parameter(prim, prefix + ":vRoughness", *roughness.v_roughness, paths);
        }

        void set_coating(const pxr::UsdPrim& prim, const CoatingLayer& coating, const Paths& paths) {
            set_float_parameter(prim, "spectra:coating:thickness", coating.thickness, paths);
            set_spectrum(prim, "spectra:coating:albedo", coating.albedo, paths);
            set_float_parameter(prim, "spectra:coating:g", coating.g, paths);
            set_attribute(prim, "spectra:coating:maxDepth", pxr::SdfValueTypeNames->Int, coating.max_depth);
            set_attribute(prim, "spectra:coating:sampleCount", pxr::SdfValueTypeNames->Int, coating.sample_count);
        }

        void set_optics(const pxr::UsdPrim& prim, const std::variant<ConductorEtaK, ConductorReflectance>& optics, const Paths& paths) {
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConductorEtaK>) {
                        set_attribute(prim, "spectra:optics:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"etaK"});
                        set_spectrum(prim, "spectra:optics:eta", data.eta, paths);
                        set_spectrum(prim, "spectra:optics:k", data.k, paths);
                    } else {
                        set_attribute(prim, "spectra:optics:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"reflectance"});
                        set_spectrum(prim, "spectra:optics:reflectance", data.reflectance, paths);
                    }
                },
                optics);
        }

        void create_mesh_data(pxr::UsdGeomMesh mesh, const TriangleMeshGeometry& data) {
            mesh.CreatePointsAttr().Set(usd(data.positions));
            pxr::VtArray<int> counts(data.indices.size() / 3u, 3);
            mesh.CreateFaceVertexCountsAttr().Set(counts);
            mesh.CreateFaceVertexIndicesAttr().Set(usd_indices(data.indices));
            mesh.CreateSubdivisionSchemeAttr().Set(pxr::UsdGeomTokens->none);
            if (!data.normals.empty()) {
                mesh.CreateNormalsAttr().Set(usd(data.normals));
                mesh.SetNormalsInterpolation(pxr::UsdGeomTokens->vertex);
            }
            if (!data.texture_coordinates.empty()) pxr::UsdGeomPrimvarsAPI{mesh}.CreatePrimvar(pxr::TfToken{"st"}, pxr::SdfValueTypeNames->TexCoord2fArray, pxr::UsdGeomTokens->vertex).Set(usd(data.texture_coordinates));
            if (!data.tangents.empty()) set_attribute(mesh.GetPrim(), "primvars:tangents", pxr::SdfValueTypeNames->Vector3fArray, usd(data.tangents));
            if (!data.source.empty()) set_attribute(mesh.GetPrim(), "spectra:source", pxr::SdfValueTypeNames->Asset, pxr::SdfAssetPath{data.source});
        }

        void write_geometries(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Geometry"});
            std::unordered_set<std::string> identifiers{};
            for (const Geometry& geometry : resources.geometries) {
                const pxr::SdfPath path = child_path(pxr::SdfPath{"/Spectra/Geometry"}, geometry.name, "Geometry", identifiers);
                paths.geometries.emplace(geometry.id.value, path);
                const pxr::UsdPrim prim = stage->CreateClassPrim(path);
                set_name(prim, geometry.name);
                set_revision(prim, geometry.revision);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                            prim.SetTypeName(pxr::TfToken{"Mesh"});
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"triangleMesh"});
                            create_mesh_data(pxr::UsdGeomMesh{prim}, data);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) {
                            prim.SetTypeName(pxr::TfToken{"Sphere"});
                            const pxr::UsdGeomSphere sphere{prim};
                            sphere.CreateRadiusAttr().Set(static_cast<double>(data.radius));
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"sphere"});
                            set_attribute(prim, "spectra:zMin", pxr::SdfValueTypeNames->Float, data.z_min);
                            set_attribute(prim, "spectra:zMax", pxr::SdfValueTypeNames->Float, data.z_max);
                            set_attribute(prim, "spectra:phiMax", pxr::SdfValueTypeNames->Float, data.phi_max);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                            prim.SetTypeName(pxr::TfToken{"Cube"});
                            pxr::UsdGeomCube{prim}.CreateSizeAttr().Set(1.0);
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"box"});
                            set_attribute(prim, "spectra:boundsMinimum", pxr::SdfValueTypeNames->Point3f, usd(data.bounds.minimum));
                            set_attribute(prim, "spectra:boundsMaximum", pxr::SdfValueTypeNames->Point3f, usd(data.bounds.maximum));
                            const math::Float3 diagonal = data.bounds.diagonal();
                            pxr::UsdGeomXformable{prim}.AddScaleOp().Set(pxr::GfVec3f{diagonal.x, diagonal.y, diagonal.z});
                            pxr::UsdGeomXformable{prim}.AddTranslateOp().Set(pxr::GfVec3d{data.bounds.center().x, data.bounds.center().y, data.bounds.center().z});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) {
                            prim.SetTypeName(pxr::TfToken{"Mesh"});
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"rectangle"});
                            set_attribute(prim, "spectra:minimum", pxr::SdfValueTypeNames->Float2, usd(data.minimum));
                            set_attribute(prim, "spectra:maximum", pxr::SdfValueTypeNames->Float2, usd(data.maximum));
                            create_mesh_data(pxr::UsdGeomMesh{prim}, TriangleMeshGeometry{.positions = {{data.minimum.x, data.minimum.y, 0.0f}, {data.maximum.x, data.minimum.y, 0.0f}, {data.maximum.x, data.maximum.y, 0.0f}, {data.minimum.x, data.maximum.y, 0.0f}}, .normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}, .texture_coordinates = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}, .indices = {0u, 1u, 2u, 0u, 2u, 3u}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) {
                            prim.SetTypeName(pxr::TfToken{"Cylinder"});
                            const pxr::UsdGeomCylinder cylinder{prim};
                            cylinder.CreateRadiusAttr().Set(static_cast<double>(data.radius));
                            cylinder.CreateHeightAttr().Set(0.0);
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"disk"});
                            set_attribute(prim, "spectra:height", pxr::SdfValueTypeNames->Float, data.height);
                            set_attribute(prim, "spectra:innerRadius", pxr::SdfValueTypeNames->Float, data.inner_radius);
                            set_attribute(prim, "spectra:phiMax", pxr::SdfValueTypeNames->Float, data.phi_max);
                        } else {
                            prim.SetTypeName(pxr::TfToken{"Cylinder"});
                            const pxr::UsdGeomCylinder cylinder{prim};
                            cylinder.CreateRadiusAttr().Set(static_cast<double>(data.radius));
                            cylinder.CreateHeightAttr().Set(static_cast<double>(data.z_max - data.z_min));
                            set_attribute(prim, "spectra:geometryType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"cylinder"});
                            set_attribute(prim, "spectra:zMin", pxr::SdfValueTypeNames->Float, data.z_min);
                            set_attribute(prim, "spectra:zMax", pxr::SdfValueTypeNames->Float, data.z_max);
                            set_attribute(prim, "spectra:phiMax", pxr::SdfValueTypeNames->Float, data.phi_max);
                        }
                    },
                    geometry.data);
            }
        }

        void write_sphere_sets(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/SphereSets"});
            std::unordered_set<std::string> identifiers{};
            for (const SphereSet& spheres : resources.sphere_sets) {
                const pxr::SdfPath path = child_path(pxr::SdfPath{"/Spectra/SphereSets"}, spheres.name, "SphereSet", identifiers);
                paths.sphere_sets.emplace(spheres.id.value, path);
                const pxr::UsdPrim prim = stage->CreateClassPrim(path);
                prim.SetTypeName(pxr::TfToken{"Points"});
                set_name(prim, spheres.name);
                set_revision(prim, spheres.revision);
                const pxr::UsdGeomPoints points{prim};
                points.CreatePointsAttr().Set(usd(spheres.positions));
                pxr::VtArray<float> widths(spheres.radii.size());
                for (std::size_t index = 0u; index != spheres.radii.size(); ++index) widths[index] = spheres.radii[index] * 2.0f;
                points.CreateWidthsAttr().Set(widths);
                if (!spheres.source.empty()) set_attribute(prim, "spectra:source", pxr::SdfValueTypeNames->Asset, pxr::SdfAssetPath{spheres.source});
            }
        }

        void write_textures(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Textures"});
            std::unordered_set<std::string> identifiers{};
            for (const Texture& texture : resources.textures) {
                const pxr::SdfPath path = child_path(pxr::SdfPath{"/Spectra/Textures"}, texture.name, "Texture", identifiers);
                paths.textures.emplace(texture.id.value, path);
            }
            for (const Texture& texture : resources.textures) {
                pxr::UsdShadeShader shader = pxr::UsdShadeShader::Define(stage, paths.textures.at(texture.id.value));
                const pxr::UsdPrim prim    = shader.GetPrim();
                set_name(prim, texture.name);
                set_revision(prim, texture.revision);
                set_attribute(prim, "spectra:valueKind", pxr::SdfValueTypeNames->Token, enum_token(texture.value_kind, texture_value_kinds));
                set_attribute(prim, "spectra:spectrumType", pxr::SdfValueTypeNames->Token, enum_token(texture.spectrum_type, texture_spectrum_types));
                set_attribute(prim, "spectra:colorSpace", pxr::SdfValueTypeNames->Token, enum_token(texture.color_space, texture_color_spaces));
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConstantTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraConstantTexture"}});
                            set_attribute(prim, "spectra:scalar", pxr::SdfValueTypeNames->Float, data.scalar);
                            set_spectrum(prim, "spectra:spectrum", data.spectrum, paths);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"UsdUVTexture"}});
                            shader.CreateInput(pxr::TfToken{"file"}, pxr::SdfValueTypeNames->Asset).Set(pxr::SdfAssetPath{data.source});
                            shader.CreateInput(pxr::TfToken{"sourceColorSpace"}, pxr::SdfValueTypeNames->Token).Set(enum_token(texture.color_space, texture_color_spaces));
                            set_attribute(prim, "spectra:textureType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"image"});
                            set_texture_mapping(prim, "spectra:mapping", data.mapping);
                            set_attribute(prim, "spectra:wrap", pxr::SdfValueTypeNames->Token, enum_token(data.wrap, texture_wrap_modes));
                            set_attribute(prim, "spectra:channel", pxr::SdfValueTypeNames->Token, enum_token(data.channel, texture_channels));
                            set_attribute(prim, "spectra:filter", pxr::SdfValueTypeNames->Token, enum_token(data.filter, texture_filters));
                            set_attribute(prim, "spectra:maximumAnisotropy", pxr::SdfValueTypeNames->Float, data.maximum_anisotropy);
                            set_attribute(prim, "spectra:scale", pxr::SdfValueTypeNames->Float, data.scale);
                            set_attribute(prim, "spectra:invert", pxr::SdfValueTypeNames->Bool, data.invert);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CheckerboardTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraCheckerboardTexture"}});
                            set_relationship(prim, "spectra:first", paths.textures.at(data.first.value));
                            set_relationship(prim, "spectra:second", paths.textures.at(data.second.value));
                            set_checker_mapping(prim, data.mapping);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ScaleTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraScaleTexture"}});
                            set_relationship(prim, "spectra:first", paths.textures.at(data.first.value));
                            set_relationship(prim, "spectra:second", paths.textures.at(data.second.value));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, MixTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraMixTexture"}});
                            set_relationship(prim, "spectra:first", paths.textures.at(data.first.value));
                            set_relationship(prim, "spectra:second", paths.textures.at(data.second.value));
                            set_relationship(prim, "spectra:amount", paths.textures.at(data.amount.value));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DirectionMixTexture>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraDirectionMixTexture"}});
                            set_relationship(prim, "spectra:first", paths.textures.at(data.first.value));
                            set_relationship(prim, "spectra:second", paths.textures.at(data.second.value));
                            set_attribute(prim, "spectra:direction", pxr::SdfValueTypeNames->Vector3f, usd(data.direction));
                        } else {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraBilerpTexture"}});
                            set_attribute(prim, "spectra:scalars", pxr::SdfValueTypeNames->Float4, pxr::GfVec4f{data.scalars[0], data.scalars[1], data.scalars[2], data.scalars[3]});
                            for (std::uint32_t corner = 0u; corner != 4u; ++corner) set_spectrum(prim, std::format("spectra:corner{}", corner), data.spectra[corner], paths);
                            set_texture_mapping(prim, "spectra:mapping", data.mapping);
                        }
                    },
                    texture.data);
            }
        }

        [[nodiscard]] math::Float3 preview_color(const Material& material) {
            return std::visit(
                [](const auto& data) -> math::Float3 {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseMaterialData> || std::same_as<std::remove_cvref_t<decltype(data)>, CoatedDiffuseMaterialData>) return data.reflectance.value;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseTransmissionMaterialData>) return data.reflectance.value;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConductorMaterialData>) {
                        return std::visit(
                            [](const auto& optics) -> math::Float3 {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, ConductorReflectance>) return optics.reflectance.value;
                                else return {0.7f, 0.7f, 0.7f};
                            },
                            data.optics);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedConductorMaterialData>) {
                        return std::visit(
                            [](const auto& optics) -> math::Float3 {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, ConductorReflectance>) return optics.reflectance.value;
                                else return {0.7f, 0.7f, 0.7f};
                            },
                            data.optics);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InterfaceMaterialData>) return {0.0f, 0.0f, 0.0f};
                    else return {1.0f, 1.0f, 1.0f};
                },
                material.data);
        }

        void write_materials(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Materials"});
            std::unordered_set<std::string> identifiers{};
            for (const Material& material : resources.materials) paths.materials.emplace(material.id.value, child_path(pxr::SdfPath{"/Spectra/Materials"}, material.name, "Material", identifiers));
            for (const Material& material : resources.materials) {
                pxr::UsdShadeMaterial usd_material = pxr::UsdShadeMaterial::Define(stage, paths.materials.at(material.id.value));
                const pxr::UsdPrim material_prim   = usd_material.GetPrim();
                set_name(material_prim, material.name);
                set_revision(material_prim, material.revision);

                pxr::UsdShadeShader preview = pxr::UsdShadeShader::Define(stage, material_prim.GetPath().AppendChild(pxr::TfToken{"PreviewSurface"}));
                preview.CreateIdAttr(pxr::VtValue{pxr::TfToken{"UsdPreviewSurface"}});
                preview.CreateInput(pxr::TfToken{"diffuseColor"}, pxr::SdfValueTypeNames->Color3f).Set(usd(preview_color(material)));
                preview.CreateInput(pxr::TfToken{"roughness"}, pxr::SdfValueTypeNames->Float).Set(0.5f);
                preview.CreateOutput(pxr::TfToken{"surface"}, pxr::SdfValueTypeNames->Token);
                usd_material.CreateSurfaceOutput().ConnectToSource(preview.ConnectableAPI(), pxr::TfToken{"surface"});

                pxr::UsdShadeShader shader = pxr::UsdShadeShader::Define(stage, material_prim.GetPath().AppendChild(pxr::TfToken{"SpectraSurface"}));
                const pxr::UsdPrim prim    = shader.GetPrim();
                shader.CreateOutput(pxr::TfToken{"surface"}, pxr::SdfValueTypeNames->Token);
                usd_material.CreateSurfaceOutput(pxr::TfToken{"spectra"}).ConnectToSource(shader.ConnectableAPI(), pxr::TfToken{"surface"});
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InterfaceMaterialData>) shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraInterface"}});
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraDiffuse"}});
                            set_spectrum(prim, "spectra:reflectance", data.reflectance, paths);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseTransmissionMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraDiffuseTransmission"}});
                            set_spectrum(prim, "spectra:reflectance", data.reflectance, paths);
                            set_spectrum(prim, "spectra:transmittance", data.transmittance, paths);
                            set_attribute(prim, "spectra:scale", pxr::SdfValueTypeNames->Float, data.scale);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConductorMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraConductor"}});
                            set_optics(prim, data.optics, paths);
                            set_roughness(prim, "spectra:distribution", data.distribution, paths);
                            set_attribute(prim, "spectra:remapRoughness", pxr::SdfValueTypeNames->Bool, data.remap_roughness);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DielectricMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraDielectric"}});
                            set_spectrum(prim, "spectra:eta", data.eta, paths);
                            set_roughness(prim, "spectra:distribution", data.distribution, paths);
                            set_attribute(prim, "spectra:remapRoughness", pxr::SdfValueTypeNames->Bool, data.remap_roughness);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ThinDielectricMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraThinDielectric"}});
                            set_spectrum(prim, "spectra:eta", data.eta, paths);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedDiffuseMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraCoatedDiffuse"}});
                            set_spectrum(prim, "spectra:reflectance", data.reflectance, paths);
                            set_spectrum(prim, "spectra:eta", data.eta, paths);
                            set_roughness(prim, "spectra:interface", data.interface, paths);
                            set_coating(prim, data.coating, paths);
                            set_attribute(prim, "spectra:remapRoughness", pxr::SdfValueTypeNames->Bool, data.remap_roughness);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedConductorMaterialData>) {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraCoatedConductor"}});
                            set_spectrum(prim, "spectra:interfaceEta", data.interface_eta, paths);
                            set_roughness(prim, "spectra:interface", data.interface, paths);
                            set_optics(prim, data.optics, paths);
                            set_roughness(prim, "spectra:conductor", data.conductor, paths);
                            set_coating(prim, data.coating, paths);
                            set_attribute(prim, "spectra:remapRoughness", pxr::SdfValueTypeNames->Bool, data.remap_roughness);
                            set_optional_relationship(prim, "spectra:normalMap", data.normal_map.value, paths.textures);
                            set_optional_relationship(prim, "spectra:bumpMap", data.bump_map.value, paths.textures);
                        } else {
                            shader.CreateIdAttr(pxr::VtValue{pxr::TfToken{"SpectraMix"}});
                            set_relationship(prim, "spectra:first", paths.materials.at(data.first.value));
                            set_relationship(prim, "spectra:second", paths.materials.at(data.second.value));
                            set_float_parameter(prim, "spectra:amount", data.amount, paths);
                        }
                    },
                    material.data);
            }
        }

        void write_media(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Media"});
            std::unordered_set<std::string> identifiers{};
            for (const Medium& medium : resources.media) paths.media.emplace(medium.id.value, child_path(pxr::SdfPath{"/Spectra/Media"}, medium.name, "Medium", identifiers));
            for (const Medium& medium : resources.media) {
                const pxr::UsdPrim prim = stage->DefinePrim(paths.media.at(medium.id.value), pxr::TfToken{"Scope"});
                set_name(prim, medium.name);
                set_revision(prim, medium.revision);
                set_attribute(prim, "spectra:mediumType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"homogeneous"});
                set_spectrum(prim, "spectra:sigmaA", medium.sigma_a, paths);
                set_spectrum(prim, "spectra:sigmaS", medium.sigma_s, paths);
                set_spectrum(prim, "spectra:emission", medium.emission, paths);
                set_attribute(prim, "spectra:densityScale", pxr::SdfValueTypeNames->Float, medium.density_scale);
                set_attribute(prim, "spectra:emissionScale", pxr::SdfValueTypeNames->Float, medium.emission_scale);
                set_attribute(prim, "spectra:anisotropy", pxr::SdfValueTypeNames->Float, medium.anisotropy);
            }
        }

        void set_standard_light_spectrum(const pxr::UsdPrim& prim, const SpectrumParameter& spectrum, const float scale) {
            pxr::UsdLuxLightAPI light{prim};
            if (spectrum.encoding == SpectrumEncoding::Constant) light.CreateColorAttr().Set(pxr::GfVec3f{spectrum.scalar});
            else if (spectrum.encoding == SpectrumEncoding::Blackbody) {
                light.CreateColorAttr().Set(pxr::GfVec3f{1.0f});
                light.CreateEnableColorTemperatureAttr().Set(true);
                light.CreateColorTemperatureAttr().Set(spectrum.temperature);
            } else light.CreateColorAttr().Set(usd(spectrum.value));
            light.CreateIntensityAttr().Set(scale);
        }

        void set_light_spectrum(const pxr::UsdPrim& prim, const SpectrumParameter& spectrum, const float scale, const Paths& paths, const std::string_view name) {
            set_spectrum(prim, name, spectrum, paths);
            set_standard_light_spectrum(prim, spectrum, scale);
        }

        void write_lights(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/Lights"});
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/AreaLights"});
            std::unordered_set<std::string> world_identifiers{};
            std::unordered_set<std::string> area_identifiers{};
            for (const Light& light : resources.lights) {
                if (std::holds_alternative<DiffuseAreaLight>(light.data)) paths.lights.emplace(light.id.value, child_path(pxr::SdfPath{"/Spectra/AreaLights"}, light.name, "AreaLight", area_identifiers));
                else paths.lights.emplace(light.id.value, child_path(pxr::SdfPath{"/World/Lights"}, light.name, "Light", world_identifiers));
            }
            for (const Light& light : resources.lights) {
                const pxr::SdfPath path = paths.lights.at(light.id.value);
                std::visit(
                    [&](const auto& data) {
                        pxr::UsdPrim prim{};
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointLight>) prim = pxr::UsdLuxSphereLight::Define(stage, path).GetPrim();
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SpotLight>) prim = pxr::UsdLuxSphereLight::Define(stage, path).GetPrim();
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DistantLight>) prim = pxr::UsdLuxDistantLight::Define(stage, path).GetPrim();
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InfiniteLight> || std::same_as<std::remove_cvref_t<decltype(data)>, PortalInfiniteLight>) prim = pxr::UsdLuxDomeLight::Define(stage, path).GetPrim();
                        else prim = stage->DefinePrim(path, pxr::TfToken{"Scope"});
                        set_name(prim, light.name);
                        set_revision(prim, light.revision);
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointLight>) {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"point"});
                            pxr::UsdLuxSphereLight{prim}.CreateTreatAsPointAttr().Set(true);
                            set_transform(prim, data.transform);
                            set_light_spectrum(prim, data.intensity, data.scale, paths, "spectra:intensity");
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SpotLight>) {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"spot"});
                            pxr::UsdLuxSphereLight{prim}.CreateTreatAsPointAttr().Set(true);
                            set_directional_light_transform(prim, data.transform);
                            set_light_spectrum(prim, data.intensity, data.scale, paths, "spectra:intensity");
                            const pxr::UsdLuxShapingAPI shaping = pxr::UsdLuxShapingAPI::Apply(prim);
                            shaping.CreateShapingConeAngleAttr().Set(data.cone_angle);
                            shaping.CreateShapingConeSoftnessAttr().Set(data.cone_angle == 0.0f ? 0.0f : data.cone_delta / data.cone_angle);
                            set_attribute(prim, "spectra:coneAngle", pxr::SdfValueTypeNames->Float, data.cone_angle);
                            set_attribute(prim, "spectra:coneDelta", pxr::SdfValueTypeNames->Float, data.cone_delta);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DistantLight>) {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"distant"});
                            set_transform(prim, data.transform);
                            set_light_spectrum(prim, data.radiance, data.scale, paths, "spectra:radiance");
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseAreaLight>) {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"diffuseArea"});
                            set_spectrum(prim, "spectra:radiance", data.radiance, paths);
                            set_attribute(prim, "spectra:sidedness", pxr::SdfValueTypeNames->Token, data.sidedness == EmissionSidedness::Front ? pxr::TfToken{"front"} : pxr::TfToken{"both"});
                            set_attribute(prim, "spectra:scale", pxr::SdfValueTypeNames->Float, data.scale);
                            set_attribute(prim, "spectra:hasPower", pxr::SdfValueTypeNames->Bool, data.power.has_value());
                            if (data.power) set_attribute(prim, "spectra:power", pxr::SdfValueTypeNames->Float, *data.power);
                            set_optional_relationship(prim, "spectra:emissionTexture", data.emission_texture.value, paths.textures);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InfiniteLight>) {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"infinite"});
                            set_transform(prim, data.transform);
                            set_light_spectrum(prim, data.radiance, data.scale, paths, "spectra:radiance");
                            set_optional_relationship(prim, "spectra:emissionTexture", data.emission_texture.value, paths.textures);
                        } else {
                            set_attribute(prim, "spectra:lightType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"portalInfinite"});
                            set_transform(prim, data.environment.transform);
                            set_light_spectrum(prim, data.environment.radiance, data.environment.scale, paths, "spectra:radiance");
                            set_optional_relationship(prim, "spectra:emissionTexture", data.environment.emission_texture.value, paths.textures);
                            pxr::VtArray<pxr::GfVec3f> portals(data.portals.size() * 4u);
                            for (std::size_t portal = 0u; portal != data.portals.size(); ++portal)
                                for (std::size_t corner = 0u; corner != 4u; ++corner) portals[portal * 4u + corner] = usd(data.portals[portal][corner]);
                            set_attribute(prim, "spectra:portals", pxr::SdfValueTypeNames->Point3fArray, portals);
                        }
                    },
                    light.data);
            }
        }

        void set_volume_rendering(const pxr::UsdPrim& prim, const VolumeRendering& rendering, const Paths& paths, const bool write_field_selectors = true) {
            if (write_field_selectors) {
                set_attribute(prim, "spectra:rendering:densityField", pxr::SdfValueTypeNames->String, rendering.density_field);
                set_attribute(prim, "spectra:rendering:temperatureField", pxr::SdfValueTypeNames->String, rendering.temperature_field);
                set_attribute(prim, "spectra:rendering:emissionScaleField", pxr::SdfValueTypeNames->String, rendering.emission_scale_field);
                set_attribute(prim, "spectra:rendering:sigmaAField", pxr::SdfValueTypeNames->String, rendering.sigma_a_field);
                set_attribute(prim, "spectra:rendering:sigmaSField", pxr::SdfValueTypeNames->String, rendering.sigma_s_field);
                set_attribute(prim, "spectra:rendering:emissionField", pxr::SdfValueTypeNames->String, rendering.emission_field);
            }
            set_attribute(prim, "spectra:rendering:fieldColorSpace", pxr::SdfValueTypeNames->Token, enum_token(rendering.field_color_space, spectrum_color_spaces));
            set_spectrum(prim, "spectra:rendering:sigmaA", rendering.sigma_a, paths);
            set_spectrum(prim, "spectra:rendering:sigmaS", rendering.sigma_s, paths);
            set_spectrum(prim, "spectra:rendering:emission", rendering.emission, paths);
            set_attribute(prim, "spectra:rendering:densityScale", pxr::SdfValueTypeNames->Float, rendering.density_scale);
            set_attribute(prim, "spectra:rendering:emissionScale", pxr::SdfValueTypeNames->Float, rendering.emission_scale);
            set_attribute(prim, "spectra:rendering:anisotropy", pxr::SdfValueTypeNames->Float, rendering.anisotropy);
            set_attribute(prim, "spectra:rendering:temperatureScale", pxr::SdfValueTypeNames->Float, rendering.temperature_scale);
            set_attribute(prim, "spectra:rendering:temperatureOffset", pxr::SdfValueTypeNames->Float, rendering.temperature_offset);
            set_attribute(prim, "spectra:rendering:minimumEmissionTemperature", pxr::SdfValueTypeNames->Float, rendering.minimum_emission_temperature);
            set_attribute(prim, "spectra:rendering:blackbodyEmission", pxr::SdfValueTypeNames->Bool, rendering.blackbody_emission);
        }

        void set_volume_diagnostics(const pxr::UsdPrim& prim, const VolumeDiagnostics& diagnostics) {
            set_attribute(prim, "spectra:diagnostics:field", pxr::SdfValueTypeNames->String, diagnostics.field_id);
            set_attribute(prim, "spectra:diagnostics:mode", pxr::SdfValueTypeNames->Token, enum_token(diagnostics.mode, diagnostic_modes));
            set_attribute(prim, "spectra:diagnostics:mapping", pxr::SdfValueTypeNames->Token, enum_token(diagnostics.mapping, field_mappings));
            set_attribute(prim, "spectra:diagnostics:depthMode", pxr::SdfValueTypeNames->Token, enum_token(diagnostics.depth_mode, depth_modes));
            set_attribute(prim, "spectra:diagnostics:colorMap", pxr::SdfValueTypeNames->Token, enum_token(diagnostics.color_map, color_maps));
            set_attribute(prim, "spectra:diagnostics:color", pxr::SdfValueTypeNames->Color4f, usd(diagnostics.color));
            set_attribute(prim, "spectra:diagnostics:minimum", pxr::SdfValueTypeNames->Float, diagnostics.minimum);
            set_attribute(prim, "spectra:diagnostics:maximum", pxr::SdfValueTypeNames->Float, diagnostics.maximum);
            set_attribute(prim, "spectra:diagnostics:slicePosition", pxr::SdfValueTypeNames->Float, diagnostics.slice_position);
            set_attribute(prim, "spectra:diagnostics:opacity", pxr::SdfValueTypeNames->Float, diagnostics.opacity);
            set_attribute(prim, "spectra:diagnostics:threshold", pxr::SdfValueTypeNames->Float, diagnostics.threshold);
            set_attribute(prim, "spectra:diagnostics:scale", pxr::SdfValueTypeNames->Float, diagnostics.scale);
            set_attribute(prim, "spectra:diagnostics:width", pxr::SdfValueTypeNames->Float, diagnostics.width);
            set_attribute(prim, "spectra:diagnostics:axis", pxr::SdfValueTypeNames->UInt, diagnostics.axis);
            set_attribute(prim, "spectra:diagnostics:sampling", pxr::SdfValueTypeNames->UInt, diagnostics.sampling);
            set_attribute(prim, "spectra:diagnostics:steps", pxr::SdfValueTypeNames->UInt, diagnostics.steps);
            set_attribute(prim, "spectra:diagnostics:categoryMask", pxr::SdfValueTypeNames->UInt, diagnostics.category_mask);
        }

        void write_volumes(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/Volumes"});
            std::unordered_set<std::string> identifiers{};
            for (const Volume& volume : resources.volumes) paths.volumes.emplace(volume.id.value, child_path(pxr::SdfPath{"/World/Volumes"}, volume.name, "Volume", identifiers));
            for (const Volume& volume : resources.volumes) {
                const pxr::UsdVolVolume usd_volume = pxr::UsdVolVolume::Define(stage, paths.volumes.at(volume.id.value));
                const pxr::UsdPrim prim            = usd_volume.GetPrim();
                set_name(prim, volume.name);
                set_revision(prim, volume.revision);
                set_transform(prim, volume.transform);
                pxr::UsdGeomImageable{prim}.CreateVisibilityAttr().Set(volume.visible ? pxr::UsdGeomTokens->inherited : pxr::UsdGeomTokens->invisible);
                usd_volume.CreateExtentAttr().Set(pxr::VtArray<pxr::GfVec3f>{usd(volume.domain.minimum), usd(volume.domain.maximum)});
                if (!std::holds_alternative<OpenVdbVolume>(volume.data)) {
                    set_attribute(prim, "spectra:domainMinimum", pxr::SdfValueTypeNames->Point3f, usd(volume.domain.minimum));
                    set_attribute(prim, "spectra:domainMaximum", pxr::SdfValueTypeNames->Point3f, usd(volume.domain.maximum));
                }
                set_optional_relationship(prim, "spectra:exteriorMedium", volume.exterior_medium.value, paths.media);
                set_volume_rendering(prim, volume.rendering, paths, !std::holds_alternative<OpenVdbVolume>(volume.data));
                set_volume_diagnostics(prim, volume.diagnostics);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DenseGridVolume>) {
                            set_attribute(prim, "spectra:volumeType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"denseGrid"});
                            set_attribute(prim, "spectra:resolution", pxr::SdfValueTypeNames->Int3, usd(data.resolution));
                            const pxr::UsdPrim fields = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Fields"}), pxr::TfToken{"Scope"});
                            std::unordered_set<std::string> field_identifiers{};
                            for (const VolumeField& field : data.fields) {
                                const pxr::UsdPrim field_prim = stage->DefinePrim(child_path(fields.GetPath(), field.id, "Field", field_identifiers), pxr::TfToken{"Scope"});
                                set_attribute(field_prim, "spectra:id", pxr::SdfValueTypeNames->String, field.id);
                                set_attribute(field_prim, "spectra:name", pxr::SdfValueTypeNames->String, field.name);
                                set_attribute(field_prim, "spectra:unit", pxr::SdfValueTypeNames->String, field.unit);
                                set_attribute(field_prim, "spectra:kind", pxr::SdfValueTypeNames->Token, enum_token(field_kind(field), field_kinds));
                                set_attribute(field_prim, "spectra:sampling", pxr::SdfValueTypeNames->Token, enum_token(field_sampling(field), volume_sampling));
                                set_attribute(field_prim, "spectra:vectorSpace", pxr::SdfValueTypeNames->Token, enum_token(field_vector_space(field), vector_spaces));
                                std::visit(
                                    [&](const auto& values) {
                                        if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, ScalarVolumeField>) set_attribute(field_prim, "spectra:values", pxr::SdfValueTypeNames->FloatArray, usd_floats(values.values));
                                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, VectorVolumeField>) set_attribute(field_prim, "spectra:values", pxr::SdfValueTypeNames->Vector3fArray, usd(values.values));
                                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, CategoryVolumeField>) set_attribute(field_prim, "spectra:values", pxr::SdfValueTypeNames->UIntArray, usd_uints(values.values));
                                        else {
                                            set_attribute(field_prim, "spectra:valuesX", pxr::SdfValueTypeNames->FloatArray, usd_floats(values.values[0]));
                                            set_attribute(field_prim, "spectra:valuesY", pxr::SdfValueTypeNames->FloatArray, usd_floats(values.values[1]));
                                            set_attribute(field_prim, "spectra:valuesZ", pxr::SdfValueTypeNames->FloatArray, usd_floats(values.values[2]));
                                        }
                                    },
                                    field.data);
                            }
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, OpenVdbVolume>) {
                            std::unordered_set<std::string> field_identifiers{};
                            for (const OpenVdbField& field : data.fields) {
                                const pxr::SdfPath field_path       = child_path(prim.GetPath(), field.id, "Field", field_identifiers);
                                const pxr::UsdVolOpenVDBAsset asset = pxr::UsdVolOpenVDBAsset::Define(stage, field_path);
                                asset.CreateFilePathAttr().Set(pxr::SdfAssetPath{field.source});
                                asset.CreateFieldNameAttr().Set(field.grid_name);
                                asset.CreateFieldDataTypeAttr().Set(field.kind == FieldKind::Float ? pxr::UsdVolTokens->float_ : pxr::UsdVolTokens->float3);
                                asset.CreateFieldClassAttr().Set(pxr::UsdVolTokens->fogVolume);
                                asset.GetPrim().SetDisplayName(field.name);
                                set_attribute(asset.GetPrim(), "spectra:unit", pxr::SdfValueTypeNames->String, field.unit);
                                set_attribute(asset.GetPrim(), "spectra:vectorSpace", pxr::SdfValueTypeNames->Token, enum_token(field.vector_space, vector_spaces));
                                usd_volume.CreateFieldRelationship(token(field.id), field_path);
                            }
                        } else {
                            set_attribute(prim, "spectra:volumeType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"proceduralCloud"});
                            set_attribute(prim, "spectra:density", pxr::SdfValueTypeNames->Float, data.density);
                            set_attribute(prim, "spectra:wispiness", pxr::SdfValueTypeNames->Float, data.wispiness);
                            set_attribute(prim, "spectra:frequency", pxr::SdfValueTypeNames->Float, data.frequency);
                        }
                    },
                    volume.data);
            }
        }

        void write_particles(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/Particles"});
            std::unordered_set<std::string> identifiers{};
            for (const ParticleSet& particles : resources.particle_sets) paths.particle_sets.emplace(particles.id.value, child_path(pxr::SdfPath{"/World/Particles"}, particles.name, "ParticleSet", identifiers));
            for (const ParticleSet& particles : resources.particle_sets) {
                const pxr::UsdGeomPoints points = pxr::UsdGeomPoints::Define(stage, paths.particle_sets.at(particles.id.value));
                const pxr::UsdPrim prim         = points.GetPrim();
                set_name(prim, particles.name);
                set_revision(prim, particles.revision);
                set_transform(prim, particles.transform);
                pxr::UsdGeomImageable{prim}.CreateVisibilityAttr().Set(particles.visible ? pxr::UsdGeomTokens->inherited : pxr::UsdGeomTokens->invisible);
                set_attribute(prim, "spectra:domainMinimum", pxr::SdfValueTypeNames->Point3f, usd(particles.domain.minimum));
                set_attribute(prim, "spectra:domainMaximum", pxr::SdfValueTypeNames->Point3f, usd(particles.domain.maximum));
                set_attribute(prim, "spectra:radius", pxr::SdfValueTypeNames->Float, particles.radius);
                const pxr::UsdPrim fields = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Fields"}), pxr::TfToken{"Scope"});
                std::unordered_set<std::string> field_identifiers{};
                for (const ParticleField& field : particles.fields) {
                    const pxr::UsdPrim field_prim = stage->DefinePrim(child_path(fields.GetPath(), field.id, "Field", field_identifiers), pxr::TfToken{"Scope"});
                    set_attribute(field_prim, "spectra:id", pxr::SdfValueTypeNames->String, field.id);
                    set_attribute(field_prim, "spectra:name", pxr::SdfValueTypeNames->String, field.name);
                    set_attribute(field_prim, "spectra:unit", pxr::SdfValueTypeNames->String, field.unit);
                    set_attribute(field_prim, "spectra:kind", pxr::SdfValueTypeNames->Token, enum_token(field_kind(field), field_kinds));
                    set_attribute(field_prim, "spectra:vectorSpace", pxr::SdfValueTypeNames->Token, enum_token(field_vector_space(field), vector_spaces));
                }
                const ParticleVisualization& visualization = particles.visualization;
                set_attribute(prim, "spectra:visualization:field", pxr::SdfValueTypeNames->String, visualization.field_id);
                set_attribute(prim, "spectra:visualization:display", pxr::SdfValueTypeNames->Token, enum_token(visualization.display, particle_displays));
                set_attribute(prim, "spectra:visualization:mapping", pxr::SdfValueTypeNames->Token, enum_token(visualization.mapping, field_mappings));
                set_attribute(prim, "spectra:visualization:depthMode", pxr::SdfValueTypeNames->Token, enum_token(visualization.depth_mode, depth_modes));
                set_attribute(prim, "spectra:visualization:colorMap", pxr::SdfValueTypeNames->Token, enum_token(visualization.color_map, color_maps));
                set_attribute(prim, "spectra:visualization:color", pxr::SdfValueTypeNames->Color4f, usd(visualization.color));
                set_attribute(prim, "spectra:visualization:minimum", pxr::SdfValueTypeNames->Float, visualization.minimum);
                set_attribute(prim, "spectra:visualization:maximum", pxr::SdfValueTypeNames->Float, visualization.maximum);
                set_attribute(prim, "spectra:visualization:radiusScale", pxr::SdfValueTypeNames->Float, visualization.radius_scale);
                set_attribute(prim, "spectra:visualization:pointSize", pxr::SdfValueTypeNames->Float, visualization.point_size);
                const ParticleDiagnostics& diagnostics = particles.diagnostics;
                set_attribute(prim, "spectra:diagnostics:vectorField", pxr::SdfValueTypeNames->String, diagnostics.vector_field);
                set_attribute(prim, "spectra:diagnostics:colorMap", pxr::SdfValueTypeNames->Token, enum_token(diagnostics.color_map, color_maps));
                set_attribute(prim, "spectra:diagnostics:minimum", pxr::SdfValueTypeNames->Float, diagnostics.minimum);
                set_attribute(prim, "spectra:diagnostics:maximum", pxr::SdfValueTypeNames->Float, diagnostics.maximum);
                set_attribute(prim, "spectra:diagnostics:scale", pxr::SdfValueTypeNames->Float, diagnostics.scale);
                set_attribute(prim, "spectra:diagnostics:width", pxr::SdfValueTypeNames->Float, diagnostics.width);
                set_attribute(prim, "spectra:diagnostics:sampling", pxr::SdfValueTypeNames->UInt, diagnostics.sampling);
            }
        }

        void write_neural_fields(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/NeuralFields"});
            std::unordered_set<std::string> identifiers{};
            for (const NeuralField& field : resources.neural_fields) paths.neural_fields.emplace(field.id.value, child_path(pxr::SdfPath{"/World/NeuralFields"}, field.name, "NeuralField", identifiers));
            for (const NeuralField& field : resources.neural_fields) {
                const pxr::UsdGeomXform xform = pxr::UsdGeomXform::Define(stage, paths.neural_fields.at(field.id.value));
                const pxr::UsdPrim prim       = xform.GetPrim();
                set_name(prim, field.name);
                set_transform(prim, field.transform);
                pxr::UsdGeomImageable{prim}.CreateVisibilityAttr().Set(field.visible ? pxr::UsdGeomTokens->inherited : pxr::UsdGeomTokens->invisible);
                set_attribute(prim, "spectra:neuralField", pxr::SdfValueTypeNames->Bool, true);
                set_attribute(prim, "spectra:diagnostics:occupancyGrid", pxr::SdfValueTypeNames->Bool, field.diagnostics.occupancy_grid);
            }
        }

        void write_prototypes(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Prototypes"});
            std::unordered_set<std::string> identifiers{};
            for (const Prototype& prototype : resources.prototypes) paths.prototypes.emplace(prototype.id.value, child_path(pxr::SdfPath{"/Spectra/Prototypes"}, prototype.name, "Prototype", identifiers));
            for (const Prototype& prototype : resources.prototypes) {
                const pxr::UsdPrim root = stage->CreateClassPrim(paths.prototypes.at(prototype.id.value));
                root.SetTypeName(pxr::TfToken{"Xform"});
                set_name(root, prototype.name);
                set_revision(root, prototype.revision);
                for (std::size_t index = 0u; index != prototype.primitives.size(); ++index) {
                    const Primitive& primitive = prototype.primitives[index];
                    const pxr::UsdPrim prim    = stage->DefinePrim(root.GetPath().AppendChild(pxr::TfToken{std::format("Primitive_{:03}", index + 1u)}));
                    if (primitive.geometry.value != 0u) {
                        prim.GetReferences().AddInternalReference(paths.geometries.at(primitive.geometry.value));
                        set_relationship(prim, "spectra:geometry", paths.geometries.at(primitive.geometry.value));
                    } else {
                        prim.GetReferences().AddInternalReference(paths.sphere_sets.at(primitive.spheres.value));
                        set_relationship(prim, "spectra:spheres", paths.sphere_sets.at(primitive.spheres.value));
                    }
                    set_transform(prim, primitive.transform);
                    if (primitive.material.value != 0u) {
                        set_relationship(prim, "spectra:material", paths.materials.at(primitive.material.value));
                        pxr::UsdShadeMaterialBindingAPI::Apply(prim).Bind(pxr::UsdShadeMaterial{stage->GetPrimAtPath(paths.materials.at(primitive.material.value))});
                    }
                    if (primitive.area_light.value != 0u) {
                        set_relationship(prim, "spectra:areaLight", paths.lights.at(primitive.area_light.value));
                        const DiffuseAreaLight& light = std::get<DiffuseAreaLight>(std::ranges::find(resources.lights, primitive.area_light, &Light::id)->data);
                        pxr::UsdLuxMeshLightAPI::Apply(prim);
                        set_standard_light_spectrum(prim, light.radiance, light.scale);
                    }
                    set_optional_relationship(prim, "spectra:insideMedium", primitive.media.inside.value, paths.media);
                    set_optional_relationship(prim, "spectra:outsideMedium", primitive.media.outside.value, paths.media);
                    set_optional_relationship(prim, "spectra:alpha", primitive.alpha.value, paths.textures);
                    set_attribute(prim, "spectra:reverseOrientation", pxr::SdfValueTypeNames->Bool, primitive.reverse_orientation);
                    prim.CreateAttribute(pxr::UsdGeomTokens->orientation, pxr::SdfValueTypeNames->Token).Set(primitive.reverse_orientation ? pxr::UsdGeomTokens->leftHanded : pxr::UsdGeomTokens->rightHanded);
                    for (std::size_t face = 0u; face != primitive.face_materials.size(); ++face) set_relationship(prim, std::format("spectra:faceMaterial:f{:06}", face), paths.materials.at(primitive.face_materials[face].value));
                    std::map<std::uint64_t, std::vector<int>> material_faces{};
                    for (std::size_t face = 0u; face != primitive.face_materials.size(); ++face) material_faces[primitive.face_materials[face].value].emplace_back(static_cast<int>(face));
                    std::size_t subset_index{};
                    for (const auto& [material_id, faces] : material_faces) {
                        const pxr::UsdGeomSubset subset = pxr::UsdGeomSubset::Define(stage, prim.GetPath().AppendChild(pxr::TfToken{std::format("MaterialSubset_{:03}", ++subset_index)}));
                        subset.CreateElementTypeAttr().Set(pxr::UsdGeomTokens->face);
                        subset.CreateFamilyNameAttr().Set(pxr::TfToken{"materialBind"});
                        subset.CreateIndicesAttr().Set(pxr::VtArray<int>{faces.begin(), faces.end()});
                        pxr::UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(pxr::UsdShadeMaterial{stage->GetPrimAtPath(paths.materials.at(material_id))});
                    }
                }
            }
        }

        void write_instances(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/Instances"});
            std::unordered_set<std::string> identifiers{};
            for (const Instance& instance : resources.instances) paths.instances.emplace(instance.id.value, child_path(pxr::SdfPath{"/World/Instances"}, instance.name, "Instance", identifiers));
            for (const Instance& instance : resources.instances) {
                const pxr::UsdPrim prim = pxr::UsdGeomXform::Define(stage, paths.instances.at(instance.id.value)).GetPrim();
                prim.GetReferences().AddInternalReference(paths.prototypes.at(instance.prototype.value));
                prim.SetInstanceable(true);
                set_name(prim, instance.name);
                set_revision(prim, instance.revision);
                set_relationship(prim, "spectra:prototype", paths.prototypes.at(instance.prototype.value));
                set_transform(prim, instance.transform);
                pxr::UsdGeomImageable{prim}.CreateVisibilityAttr().Set(instance.visible ? pxr::UsdGeomTokens->inherited : pxr::UsdGeomTokens->invisible);
            }
        }

        void write_cameras(const pxr::UsdStageRefPtr& stage, const SceneResources& resources, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/World/Cameras"});
            std::unordered_set<std::string> identifiers{};
            for (const Camera& camera : resources.cameras) paths.cameras.emplace(camera.id.value, child_path(pxr::SdfPath{"/World/Cameras"}, camera.name, "Camera", identifiers));
            for (const Camera& camera : resources.cameras) {
                const pxr::UsdGeomCamera usd_camera = pxr::UsdGeomCamera::Define(stage, paths.cameras.at(camera.id.value));
                const pxr::UsdPrim prim             = usd_camera.GetPrim();
                set_name(prim, camera.name);
                set_revision(prim, camera.revision);
                set_transform(prim, camera.transform);
                set_attribute(prim, "spectra:exposureTime", pxr::SdfValueTypeNames->Float, camera.exposure_time);
                set_optional_relationship(prim, "spectra:medium", camera.medium.value, paths.media);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PerspectiveCameraData>) {
                            usd_camera.CreateProjectionAttr().Set(pxr::UsdGeomTokens->perspective);
                            const float focal_length      = 20.955f;
                            const float vertical_aperture = 2.0f * focal_length * std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                            usd_camera.CreateFocalLengthAttr().Set(focal_length);
                            usd_camera.CreateVerticalApertureAttr().Set(vertical_aperture);
                            set_attribute(prim, "spectra:cameraType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"perspective"});
                            set_attribute(prim, "spectra:verticalFov", pxr::SdfValueTypeNames->Float, data.vertical_fov);
                        } else {
                            usd_camera.CreateProjectionAttr().Set(pxr::UsdGeomTokens->orthographic);
                            usd_camera.CreateHorizontalApertureAttr().Set(data.screen_window.maximum.x - data.screen_window.minimum.x);
                            usd_camera.CreateVerticalApertureAttr().Set(data.screen_window.maximum.y - data.screen_window.minimum.y);
                            set_attribute(prim, "spectra:cameraType", pxr::SdfValueTypeNames->Token, pxr::TfToken{"orthographic"});
                        }
                        set_attribute(prim, "spectra:screenMinimum", pxr::SdfValueTypeNames->Float2, usd(data.screen_window.minimum));
                        set_attribute(prim, "spectra:screenMaximum", pxr::SdfValueTypeNames->Float2, usd(data.screen_window.maximum));
                        set_attribute(prim, "spectra:lensRadius", pxr::SdfValueTypeNames->Float, data.lens_radius);
                        set_attribute(prim, "spectra:focalDistance", pxr::SdfValueTypeNames->Float, data.focal_distance);
                        set_attribute(prim, "spectra:nearPlane", pxr::SdfValueTypeNames->Float, data.near_plane);
                        set_attribute(prim, "spectra:farPlane", pxr::SdfValueTypeNames->Float, data.far_plane);
                        usd_camera.CreateFocusDistanceAttr().Set(data.focal_distance);
                        usd_camera.CreateClippingRangeAttr().Set(pxr::GfVec2f{data.near_plane, data.far_plane});
                    },
                    camera.data);
            }
        }

        void write_render_settings(const pxr::UsdStageRefPtr& stage, const Scene& scene, Paths& paths) {
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Render"});
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Films"});
            pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra/Samplers"});
            std::unordered_set<std::string> film_identifiers{};
            std::unordered_set<std::string> sampler_identifiers{};
            for (const Film& film : scene.resources.films) paths.films.emplace(film.id.value, child_path(pxr::SdfPath{"/Spectra/Films"}, film.name, "Film", film_identifiers));
            for (const Sampler& sampler : scene.resources.samplers) paths.samplers.emplace(sampler.id.value, child_path(pxr::SdfPath{"/Spectra/Samplers"}, sampler.name, "Sampler", sampler_identifiers));
            for (const Film& film : scene.resources.films) {
                const pxr::UsdRenderProduct product = pxr::UsdRenderProduct::Define(stage, paths.films.at(film.id.value));
                const pxr::UsdPrim prim             = product.GetPrim();
                set_name(prim, film.name);
                set_revision(prim, film.revision);
                product.CreateResolutionAttr().Set(pxr::GfVec2i{static_cast<int>(film.resolution[0]), static_cast<int>(film.resolution[1])});
                set_attribute(prim, "spectra:pixelMinimum", pxr::SdfValueTypeNames->Int2, pxr::GfVec2i{static_cast<int>(film.pixel_minimum[0]), static_cast<int>(film.pixel_minimum[1])});
                set_attribute(prim, "spectra:pixelMaximum", pxr::SdfValueTypeNames->Int2, pxr::GfVec2i{static_cast<int>(film.pixel_maximum[0]), static_cast<int>(film.pixel_maximum[1])});
                set_attribute(prim, "spectra:exposure", pxr::SdfValueTypeNames->Float, film.exposure);
                set_attribute(prim, "spectra:iso", pxr::SdfValueTypeNames->Float, film.iso);
                set_attribute(prim, "spectra:colorSpace", pxr::SdfValueTypeNames->Token, enum_token(film.color_space, spectrum_color_spaces));
                set_attribute(prim, "spectra:sensorResponse", pxr::SdfValueTypeNames->FloatArray, usd_floats(film.sensor_response));
                set_attribute(prim, "spectra:sensorToOutputRGB", pxr::SdfValueTypeNames->Matrix3d, pxr::GfMatrix3d{film.sensor_to_output_rgb[0], film.sensor_to_output_rgb[1], film.sensor_to_output_rgb[2], film.sensor_to_output_rgb[3], film.sensor_to_output_rgb[4], film.sensor_to_output_rgb[5], film.sensor_to_output_rgb[6], film.sensor_to_output_rgb[7], film.sensor_to_output_rgb[8]});
                set_attribute(prim, "spectra:hasMaximumComponentValue", pxr::SdfValueTypeNames->Bool, film.maximum_component_value.has_value());
                if (film.maximum_component_value) set_attribute(prim, "spectra:maximumComponentValue", pxr::SdfValueTypeNames->Float, *film.maximum_component_value);
                set_attribute(prim, "spectra:filter:kind", pxr::SdfValueTypeNames->Token, enum_token(film.filter.kind, filter_kinds));
                set_attribute(prim, "spectra:filter:radius", pxr::SdfValueTypeNames->Float2, usd(film.filter.radius));
                set_attribute(prim, "spectra:filter:sigma", pxr::SdfValueTypeNames->Float, film.filter.sigma);
                set_attribute(prim, "spectra:filter:b", pxr::SdfValueTypeNames->Float, film.filter.b);
                set_attribute(prim, "spectra:filter:c", pxr::SdfValueTypeNames->Float, film.filter.c);
                set_attribute(prim, "spectra:filter:tau", pxr::SdfValueTypeNames->Float, film.filter.tau);
                set_attribute(prim, "spectra:gbuffer", pxr::SdfValueTypeNames->Bool, film.gbuffer);
                set_attribute(prim, "spectra:gbufferCameraSpace", pxr::SdfValueTypeNames->Bool, film.gbuffer_camera_space);
            }
            for (const Sampler& sampler : scene.resources.samplers) {
                const pxr::UsdPrim prim = stage->DefinePrim(paths.samplers.at(sampler.id.value), pxr::TfToken{"Scope"});
                set_name(prim, sampler.name);
                set_revision(prim, sampler.revision);
                set_attribute(prim, "spectra:kind", pxr::SdfValueTypeNames->Token, enum_token(sampler.kind, sampler_kinds));
                set_attribute(prim, "spectra:samplesPerPixel", pxr::SdfValueTypeNames->UInt, sampler.samples_per_pixel);
                set_attribute(prim, "spectra:seed", pxr::SdfValueTypeNames->UInt, sampler.seed);
                set_attribute(prim, "spectra:jitter", pxr::SdfValueTypeNames->Bool, sampler.jitter);
                set_attribute(prim, "spectra:xStrata", pxr::SdfValueTypeNames->UInt, sampler.x_strata);
                set_attribute(prim, "spectra:yStrata", pxr::SdfValueTypeNames->UInt, sampler.y_strata);
                set_attribute(prim, "spectra:randomization", pxr::SdfValueTypeNames->Token, enum_token(sampler.randomization, sampler_randomizations));
            }
            const pxr::UsdRenderSettings settings = pxr::UsdRenderSettings::Define(stage, pxr::SdfPath{"/Render/Settings"});
            const pxr::UsdPrim prim               = settings.GetPrim();
            settings.CreateProductsRel().SetTargets({paths.films.at(scene.active_film.value)});
            settings.CreateCameraRel().SetTargets({paths.cameras.at(scene.active_camera.value)});
            set_relationship(prim, "spectra:film", paths.films.at(scene.active_film.value));
            set_relationship(prim, "spectra:sampler", paths.samplers.at(scene.active_sampler.value));
            set_attribute(prim, "spectra:transport:maximumDepth", pxr::SdfValueTypeNames->UInt, scene.transport.maximum_depth);
            set_attribute(prim, "spectra:transport:lightSampler", pxr::SdfValueTypeNames->Token, enum_token(scene.transport.light_sampler, light_sampler_kinds));
            set_attribute(prim, "spectra:transport:regularize", pxr::SdfValueTypeNames->Bool, scene.transport.regularize);
        }

        [[nodiscard]] pxr::SdfPath simulation_resource_path(const SimulationOutputBinding& binding, const SceneResources& resources, const Paths& paths) {
            const std::uint64_t id = binding.resource_id;
            if (std::ranges::contains(resources.geometries, GeometryId{id}, &Geometry::id)) return paths.geometries.at(id);
            if (std::ranges::contains(resources.sphere_sets, SphereSetId{id}, &SphereSet::id)) return paths.sphere_sets.at(id);
            if (std::ranges::contains(resources.particle_sets, ParticleSetId{id}, &ParticleSet::id)) return paths.particle_sets.at(id);
            if (std::ranges::contains(resources.volumes, VolumeId{id}, &Volume::id)) return paths.volumes.at(id);
            if (std::ranges::contains(resources.neural_fields, NeuralFieldId{id}, &NeuralField::id)) return paths.neural_fields.at(id);
            throw std::runtime_error(std::format("Simulation output '{}' refers to unsupported resource {}", binding.output_id, id));
        }

        void write_simulation(const pxr::UsdStageRefPtr& stage, const Scene& scene, const Paths& paths) {
            if (!scene.simulation) return;
            const pxr::UsdPrim simulation = stage->DefinePrim(pxr::SdfPath{"/Spectra/Simulation"}, pxr::TfToken{"Scope"});
            set_attribute(simulation, "spectra:clock:stepSeconds", pxr::SdfValueTypeNames->Double, scene.simulation->clock.step_seconds);
            set_attribute(simulation, "spectra:clock:startStep", pxr::SdfValueTypeNames->UInt64, scene.simulation->clock.start_step);
            set_attribute(simulation, "spectra:clock:hasEndStep", pxr::SdfValueTypeNames->Bool, scene.simulation->clock.end_step.has_value());
            if (scene.simulation->clock.end_step) set_attribute(simulation, "spectra:clock:endStep", pxr::SdfValueTypeNames->UInt64, *scene.simulation->clock.end_step);
            set_attribute(simulation, "spectra:clock:loop", pxr::SdfValueTypeNames->Bool, scene.simulation->clock.loop);
            set_attribute(simulation, "spectra:seed", pxr::SdfValueTypeNames->UInt64, scene.simulation->seed);
            std::unordered_set<std::string> identifiers{};
            for (const SimulationSystem& system : scene.simulation->systems) {
                const pxr::UsdPrim prim = stage->DefinePrim(child_path(simulation.GetPath(), system.id.value, "System", identifiers), pxr::TfToken{"Scope"});
                set_attribute(prim, "spectra:id", pxr::SdfValueTypeNames->String, system.id.value);
                set_attribute(prim, "spectra:name", pxr::SdfValueTypeNames->String, system.name);
                set_attribute(prim, "spectra:provider", pxr::SdfValueTypeNames->String, system.provider_id);
                set_attribute(prim, "spectra:enabled", pxr::SdfValueTypeNames->Bool, system.enabled);
                set_attribute(prim, "spectra:visible", pxr::SdfValueTypeNames->Bool, system.visible);
                for (std::size_t index = 0u; index != system.parameters.size(); ++index) {
                    const SimulationParameterSetting& parameter = system.parameters[index];
                    const pxr::UsdPrim parameter_prim           = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{std::format("Parameter_{:03}", index + 1u)}), pxr::TfToken{"Scope"});
                    set_attribute(parameter_prim, "spectra:id", pxr::SdfValueTypeNames->String, parameter.parameter_id);
                    set_attribute(parameter_prim, "spectra:kind", pxr::SdfValueTypeNames->Token, enum_token(parameter.value.kind, parameter_kinds));
                    set_attribute(parameter_prim, "spectra:integer", pxr::SdfValueTypeNames->Int64, parameter.value.integer);
                    set_attribute(parameter_prim, "spectra:floating", pxr::SdfValueTypeNames->Double3, pxr::GfVec3d{parameter.value.floating[0], parameter.value.floating[1], parameter.value.floating[2]});
                }
                for (std::size_t index = 0u; index != system.scene_bindings.size(); ++index) {
                    const SimulationOutputBinding& binding = system.scene_bindings[index];
                    const pxr::UsdPrim binding_prim        = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{std::format("Binding_{:03}", index + 1u)}), pxr::TfToken{"Scope"});
                    set_attribute(binding_prim, "spectra:output", pxr::SdfValueTypeNames->String, binding.output_id);
                    set_relationship(binding_prim, "spectra:resource", simulation_resource_path(binding, scene.resources, paths));
                }
                for (std::size_t index = 0u; index != system.visualizations.size(); ++index) {
                    const SimulationVisualization& visualization = system.visualizations[index];
                    const pxr::UsdPrim view                      = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{std::format("Visualization_{:03}", index + 1u)}), pxr::TfToken{"Scope"});
                    set_attribute(view, "spectra:output", pxr::SdfValueTypeNames->String, visualization.output_id);
                    set_attribute(view, "spectra:name", pxr::SdfValueTypeNames->String, visualization.name);
                    set_attribute(view, "spectra:depthMode", pxr::SdfValueTypeNames->Token, enum_token(visualization.depth_mode, depth_modes));
                    set_attribute(view, "spectra:compositionDomain", pxr::SdfValueTypeNames->Token, enum_token(visualization.composition_domain, composition_domains));
                    set_optional_relationship(view, "spectra:anchor", visualization.anchor.value, paths.instances);
                    set_attribute(view, "spectra:color", pxr::SdfValueTypeNames->Color4f, usd(visualization.color));
                    set_attribute(view, "spectra:visible", pxr::SdfValueTypeNames->Bool, visualization.visible);
                    std::visit(
                        [&](const auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"points"});
                                set_attribute(view, "spectra:size", pxr::SdfValueTypeNames->Float, data.size);
                                set_attribute(view, "spectra:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                                set_attribute(view, "spectra:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                                set_attribute(view, "spectra:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                                set_attribute(view, "spectra:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"segments"});
                                set_attribute(view, "spectra:width", pxr::SdfValueTypeNames->Float, data.width);
                                set_attribute(view, "spectra:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                                set_attribute(view, "spectra:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                                set_attribute(view, "spectra:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                                set_attribute(view, "spectra:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"vectors"});
                                set_attribute(view, "spectra:width", pxr::SdfValueTypeNames->Float, data.width);
                                set_attribute(view, "spectra:scale", pxr::SdfValueTypeNames->Float, data.scale);
                                set_attribute(view, "spectra:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                                set_attribute(view, "spectra:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                                set_attribute(view, "spectra:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                                set_attribute(view, "spectra:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"image"});
                                set_attribute(view, "spectra:screenRect", pxr::SdfValueTypeNames->Float4, usd(data.screen_rect));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SurfaceVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"surface"});
                                set_attribute(view, "spectra:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                                set_attribute(view, "spectra:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                                set_attribute(view, "spectra:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                                set_attribute(view, "spectra:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DerivedMeshVisualization>) {
                                set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, enum_token(data.mode, derived_mesh_modes));
                                set_attribute(view, "spectra:width", pxr::SdfValueTypeNames->Float, data.width);
                                set_attribute(view, "spectra:scale", pxr::SdfValueTypeNames->Float, data.scale);
                            } else set_attribute(view, "spectra:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"occupancyGrid"});
                        },
                        visualization.data);
                }
            }
        }

        struct ReaderPaths {
            std::unordered_map<std::string, std::uint64_t> geometries{};
            std::unordered_map<std::string, std::uint64_t> sphere_sets{};
            std::unordered_map<std::string, std::uint64_t> particle_sets{};
            std::unordered_map<std::string, std::uint64_t> volumes{};
            std::unordered_map<std::string, std::uint64_t> neural_fields{};
            std::unordered_map<std::string, std::uint64_t> textures{};
            std::unordered_map<std::string, std::uint64_t> materials{};
            std::unordered_map<std::string, std::uint64_t> media{};
            std::unordered_map<std::string, std::uint64_t> lights{};
            std::unordered_map<std::string, std::uint64_t> cameras{};
            std::unordered_map<std::string, std::uint64_t> films{};
            std::unordered_map<std::string, std::uint64_t> samplers{};
            std::unordered_map<std::string, std::uint64_t> prototypes{};
            std::unordered_map<std::string, std::uint64_t> instances{};
        };

        template <typename Type>
        [[nodiscard]] Type get_attribute(const pxr::UsdPrim& prim, const std::string_view name) {
            Type value{};
            if (!prim.GetAttribute(token(name)).Get(&value)) throw std::runtime_error(std::format("Unsupported USD at {}.{}: required attribute is missing", prim.GetPath().GetString(), name));
            return value;
        }

        [[nodiscard]] bool has_attribute(const pxr::UsdPrim& prim, const std::string_view name) {
            return prim.HasAttribute(token(name));
        }

        [[nodiscard]] pxr::SdfPath get_relationship(const pxr::UsdPrim& prim, const std::string_view name) {
            pxr::SdfPathVector targets{};
            if (!prim.GetRelationship(token(name)).GetTargets(&targets) || targets.size() != 1u) throw std::runtime_error(std::format("Unsupported USD at {}.{}: exactly one relationship target is required", prim.GetPath().GetString(), name));
            return targets.front();
        }

        [[nodiscard]] pxr::SdfPathVector get_relationships(const pxr::UsdPrim& prim, const std::string_view name) {
            pxr::SdfPathVector targets{};
            if (!prim.GetRelationship(token(name)).GetTargets(&targets)) throw std::runtime_error(std::format("Unsupported USD at {}.{}: relationship targets are required", prim.GetPath().GetString(), name));
            return targets;
        }

        [[nodiscard]] std::uint64_t get_optional_relationship_id(const pxr::UsdPrim& prim, const std::string_view name, const std::unordered_map<std::string, std::uint64_t>& paths) {
            if (!prim.HasRelationship(token(name))) return 0u;
            const pxr::SdfPath path = get_relationship(prim, name);
            const auto resource     = paths.find(path.GetString());
            if (resource == paths.end()) throw std::runtime_error(std::format("Unsupported USD at {}.{}: target {} is not a supported composed resource", prim.GetPath().GetString(), name, path.GetString()));
            return resource->second;
        }

        [[nodiscard]] std::string get_name(const pxr::UsdPrim& prim) {
            return get_attribute<std::string>(prim, "spectra:name");
        }

        [[nodiscard]] ResourceRevision get_revision(const pxr::UsdPrim& prim) {
            return {get_attribute<std::uint64_t>(prim, "spectra:revision:content"), get_attribute<std::uint64_t>(prim, "spectra:revision:topology")};
        }

        [[nodiscard]] math::Float2 spectra(const pxr::GfVec2f value) {
            return {value[0], value[1]};
        }

        [[nodiscard]] math::Float3 spectra(const pxr::GfVec3f value) {
            return {value[0], value[1], value[2]};
        }

        [[nodiscard]] math::Float4 spectra(const pxr::GfVec4f value) {
            return {value[0], value[1], value[2], value[3]};
        }

        [[nodiscard]] math::UInt3 spectra(const pxr::GfVec3i value) {
            return {static_cast<std::uint32_t>(value[0]), static_cast<std::uint32_t>(value[1]), static_cast<std::uint32_t>(value[2])};
        }

        [[nodiscard]] math::Transform spectra(const pxr::GfMatrix4d& matrix) {
            math::Transform result{};
            for (std::uint32_t row = 0u; row != 4u; ++row)
                for (std::uint32_t column = 0u; column != 4u; ++column) result.matrix[row * 4u + column] = static_cast<float>(matrix[column][row]);
            return result;
        }

        [[nodiscard]] std::vector<math::Float3> spectra(const pxr::VtArray<pxr::GfVec3f>& values) {
            std::vector<math::Float3> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = spectra(values[index]);
            return result;
        }

        [[nodiscard]] std::vector<math::Float2> spectra(const pxr::VtArray<pxr::GfVec2f>& values) {
            std::vector<math::Float2> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = spectra(values[index]);
            return result;
        }

        [[nodiscard]] std::vector<std::uint32_t> spectra_indices(const pxr::VtArray<int>& values) {
            std::vector<std::uint32_t> result(values.size());
            for (std::size_t index = 0u; index != values.size(); ++index) result[index] = static_cast<std::uint32_t>(values[index]);
            return result;
        }

        [[nodiscard]] std::vector<float> spectra_floats(const pxr::VtArray<float>& values) {
            return {values.begin(), values.end()};
        }

        [[nodiscard]] float get_light_intensity(const pxr::UsdPrim& prim) {
            float value{};
            pxr::UsdLuxLightAPI{prim}.GetIntensityAttr().Get(&value);
            return value;
        }

        [[nodiscard]] float get_cylinder_radius(const pxr::UsdPrim& prim) {
            double value{};
            pxr::UsdGeomCylinder{prim}.GetRadiusAttr().Get(&value);
            return static_cast<float>(value);
        }

        template <typename Enum, std::size_t Size>
        [[nodiscard]] Enum get_enum(const pxr::UsdPrim& prim, const std::string_view name, const std::array<std::string_view, Size>& names) {
            const pxr::TfToken value = get_attribute<pxr::TfToken>(prim, name);
            const auto found         = std::ranges::find(names, value.GetString());
            if (found == names.end()) throw std::runtime_error(std::format("Unsupported USD at {}.{}: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), name, value.GetString()));
            return static_cast<Enum>(std::distance(names.begin(), found));
        }

        [[nodiscard]] SpectrumParameter get_spectrum(const pxr::UsdPrim& prim, const std::string_view name, const ReaderPaths& paths) {
            const std::string prefix{name};
            return {
                .value       = spectra(get_attribute<pxr::GfVec3f>(prim, prefix + ":value")),
                .texture     = {get_optional_relationship_id(prim, prefix + ":texture", paths.textures)},
                .encoding    = get_enum<SpectrumEncoding>(prim, prefix + ":encoding", spectrum_encodings),
                .color_space = get_enum<SpectrumColorSpace>(prim, prefix + ":colorSpace", spectrum_color_spaces),
                .scalar      = get_attribute<float>(prim, prefix + ":scalar"),
                .temperature = get_attribute<float>(prim, prefix + ":temperature"),
                .wavelengths = spectra_floats(get_attribute<pxr::VtArray<float>>(prim, prefix + ":wavelengths")),
                .samples     = spectra_floats(get_attribute<pxr::VtArray<float>>(prim, prefix + ":samples")),
            };
        }

        [[nodiscard]] FloatParameter get_float_parameter(const pxr::UsdPrim& prim, const std::string_view name, const ReaderPaths& paths) {
            const std::string prefix{name};
            return {get_attribute<float>(prim, prefix + ":value"), {get_optional_relationship_id(prim, prefix + ":texture", paths.textures)}};
        }

        [[nodiscard]] TextureMapping get_texture_mapping(const pxr::UsdPrim& prim, const std::string_view name) {
            const std::string prefix{name};
            const std::string type = get_attribute<pxr::TfToken>(prim, prefix + ":type").GetString();
            if (type == "uv") return TextureMapping{UvTextureMapping{spectra(get_attribute<pxr::GfVec2f>(prim, prefix + ":scale")), spectra(get_attribute<pxr::GfVec2f>(prim, prefix + ":offset"))}};
            if (type == "planar") return TextureMapping{PlanarTextureMapping{spectra(get_attribute<pxr::GfVec3f>(prim, prefix + ":firstAxis")), spectra(get_attribute<pxr::GfVec3f>(prim, prefix + ":secondAxis")), spectra(get_attribute<pxr::GfVec2f>(prim, prefix + ":offset")), spectra(get_attribute<pxr::GfMatrix4d>(prim, prefix + ":textureFromRender"))}};
            if (type == "spherical") return TextureMapping{SphericalTextureMapping{spectra(get_attribute<pxr::GfMatrix4d>(prim, prefix + ":textureFromRender"))}};
            if (type == "cylindrical") return TextureMapping{CylindricalTextureMapping{spectra(get_attribute<pxr::GfMatrix4d>(prim, prefix + ":textureFromRender"))}};
            throw std::runtime_error(std::format("Unsupported USD at {}.{}: texture mapping '{}' is not in the Spectra profile", prim.GetPath().GetString(), name, type));
        }

        [[nodiscard]] CheckerboardMapping get_checker_mapping(const pxr::UsdPrim& prim) {
            if (get_attribute<pxr::TfToken>(prim, "spectra:mapping:type") == pxr::TfToken{"threeDimensional"}) return CheckerboardMapping{TextureMapping3D{spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:mapping:textureFromRender"))}};
            return CheckerboardMapping{get_texture_mapping(prim, "spectra:mapping")};
        }

        [[nodiscard]] MaterialRoughness get_roughness(const pxr::UsdPrim& prim, const std::string_view name, const ReaderPaths& paths) {
            const std::string prefix{name};
            MaterialRoughness result{.roughness = get_float_parameter(prim, prefix + ":roughness", paths)};
            if (get_attribute<bool>(prim, prefix + ":anisotropic")) {
                result.u_roughness = get_float_parameter(prim, prefix + ":uRoughness", paths);
                result.v_roughness = get_float_parameter(prim, prefix + ":vRoughness", paths);
            }
            return result;
        }

        [[nodiscard]] CoatingLayer get_coating(const pxr::UsdPrim& prim, const ReaderPaths& paths) {
            return {
                .thickness    = get_float_parameter(prim, "spectra:coating:thickness", paths),
                .albedo       = get_spectrum(prim, "spectra:coating:albedo", paths),
                .g            = get_float_parameter(prim, "spectra:coating:g", paths),
                .max_depth    = get_attribute<std::int32_t>(prim, "spectra:coating:maxDepth"),
                .sample_count = get_attribute<std::int32_t>(prim, "spectra:coating:sampleCount"),
            };
        }

        [[nodiscard]] std::variant<ConductorEtaK, ConductorReflectance> get_optics(const pxr::UsdPrim& prim, const ReaderPaths& paths) {
            const pxr::TfToken type = get_attribute<pxr::TfToken>(prim, "spectra:optics:type");
            if (type == pxr::TfToken{"etaK"}) return ConductorEtaK{get_spectrum(prim, "spectra:optics:eta", paths), get_spectrum(prim, "spectra:optics:k", paths)};
            if (type == pxr::TfToken{"reflectance"}) return ConductorReflectance{get_spectrum(prim, "spectra:optics:reflectance", paths)};
            throw std::runtime_error(std::format("Unsupported USD at {}.spectra:optics:type: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), type.GetString()));
        }

        void index_children(const pxr::UsdPrim& parent, std::unordered_map<std::string, std::uint64_t>& paths) {
            std::uint64_t id = 1u;
            for (const pxr::UsdPrim& prim : parent.GetAllChildren()) paths.emplace(prim.GetPath().GetString(), id++);
        }

        void read_geometries(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Geometry"});
            index_children(root, paths.geometries);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                Geometry geometry{.id = {paths.geometries.at(prim.GetPath().GetString())}, .name = get_name(prim), .revision = get_revision(prim)};
                const pxr::TfToken type = get_attribute<pxr::TfToken>(prim, "spectra:geometryType");
                if (type == pxr::TfToken{"triangleMesh"}) {
                    const pxr::UsdGeomMesh mesh{prim};
                    pxr::VtArray<pxr::GfVec3f> positions{};
                    pxr::VtArray<pxr::GfVec3f> normals{};
                    pxr::VtArray<pxr::GfVec3f> tangents{};
                    pxr::VtArray<pxr::GfVec2f> texture_coordinates{};
                    pxr::VtArray<int> indices{};
                    mesh.GetPointsAttr().Get(&positions);
                    mesh.GetNormalsAttr().Get(&normals);
                    prim.GetAttribute(pxr::TfToken{"primvars:tangents"}).Get(&tangents);
                    pxr::UsdGeomPrimvarsAPI{prim}.GetPrimvar(pxr::TfToken{"st"}).Get(&texture_coordinates);
                    mesh.GetFaceVertexIndicesAttr().Get(&indices);
                    std::string source{};
                    if (has_attribute(prim, "spectra:source")) source = get_attribute<pxr::SdfAssetPath>(prim, "spectra:source").GetAssetPath();
                    geometry.data = TriangleMeshGeometry{source, spectra(positions), spectra(normals), spectra(tangents), spectra(texture_coordinates), spectra_indices(indices)};
                } else if (type == pxr::TfToken{"sphere"}) {
                    double radius{};
                    pxr::UsdGeomSphere{prim}.GetRadiusAttr().Get(&radius);
                    geometry.data = SphereGeometry{static_cast<float>(radius), get_attribute<float>(prim, "spectra:zMin"), get_attribute<float>(prim, "spectra:zMax"), get_attribute<float>(prim, "spectra:phiMax")};
                } else if (type == pxr::TfToken{"box"}) geometry.data = BoxGeometry{{spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:boundsMinimum")), spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:boundsMaximum"))}};
                else if (type == pxr::TfToken{"rectangle"}) geometry.data = RectangleGeometry{spectra(get_attribute<pxr::GfVec2f>(prim, "spectra:minimum")), spectra(get_attribute<pxr::GfVec2f>(prim, "spectra:maximum"))};
                else if (type == pxr::TfToken{"disk"}) geometry.data = DiskGeometry{get_attribute<float>(prim, "spectra:height"), get_cylinder_radius(prim), get_attribute<float>(prim, "spectra:innerRadius"), get_attribute<float>(prim, "spectra:phiMax")};
                else if (type == pxr::TfToken{"cylinder"}) {
                    double radius{};
                    pxr::UsdGeomCylinder{prim}.GetRadiusAttr().Get(&radius);
                    geometry.data = CylinderGeometry{static_cast<float>(radius), get_attribute<float>(prim, "spectra:zMin"), get_attribute<float>(prim, "spectra:zMax"), get_attribute<float>(prim, "spectra:phiMax")};
                } else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:geometryType: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), type.GetString()));
                resources.geometries.emplace_back(std::move(geometry));
            }
        }

        void read_sphere_sets(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/SphereSets"});
            index_children(root, paths.sphere_sets);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                pxr::VtArray<pxr::GfVec3f> positions{};
                pxr::VtArray<float> widths{};
                pxr::UsdGeomPoints{prim}.GetPointsAttr().Get(&positions);
                pxr::UsdGeomPoints{prim}.GetWidthsAttr().Get(&widths);
                std::vector<float> radii(widths.size());
                for (std::size_t index = 0u; index != widths.size(); ++index) radii[index] = widths[index] * 0.5f;
                std::string source{};
                if (has_attribute(prim, "spectra:source")) source = get_attribute<pxr::SdfAssetPath>(prim, "spectra:source").GetAssetPath();
                resources.sphere_sets.emplace_back(SphereSet{{paths.sphere_sets.at(prim.GetPath().GetString())}, get_name(prim), get_revision(prim), source, spectra(positions), std::move(radii)});
            }
        }

        void read_textures(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Textures"});
            index_children(root, paths.textures);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                Texture texture{
                    .id            = {paths.textures.at(prim.GetPath().GetString())},
                    .name          = get_name(prim),
                    .revision      = get_revision(prim),
                    .value_kind    = get_enum<TextureValueKind>(prim, "spectra:valueKind", texture_value_kinds),
                    .spectrum_type = get_enum<TextureSpectrumType>(prim, "spectra:spectrumType", texture_spectrum_types),
                    .color_space   = get_enum<TextureColorSpace>(prim, "spectra:colorSpace", texture_color_spaces),
                };
                pxr::TfToken id{};
                pxr::UsdShadeShader{prim}.GetIdAttr().Get(&id);
                if (id == pxr::TfToken{"SpectraConstantTexture"}) texture.data = ConstantTexture{get_attribute<float>(prim, "spectra:scalar"), get_spectrum(prim, "spectra:spectrum", paths)};
                else if (id == pxr::TfToken{"UsdUVTexture"}) {
                    pxr::SdfAssetPath source{};
                    pxr::UsdShadeShader{prim}.GetInput(pxr::TfToken{"file"}).Get(&source);
                    texture.data = ImageTexture{
                        .source             = source.GetAssetPath(),
                        .mapping            = get_texture_mapping(prim, "spectra:mapping"),
                        .wrap               = get_enum<TextureWrapMode>(prim, "spectra:wrap", texture_wrap_modes),
                        .channel            = get_enum<TextureChannel>(prim, "spectra:channel", texture_channels),
                        .filter             = get_enum<TextureFilter>(prim, "spectra:filter", texture_filters),
                        .maximum_anisotropy = get_attribute<float>(prim, "spectra:maximumAnisotropy"),
                        .scale              = get_attribute<float>(prim, "spectra:scale"),
                        .invert             = get_attribute<bool>(prim, "spectra:invert"),
                    };
                } else if (id == pxr::TfToken{"SpectraCheckerboardTexture"}) texture.data = CheckerboardTexture{{get_optional_relationship_id(prim, "spectra:first", paths.textures)}, {get_optional_relationship_id(prim, "spectra:second", paths.textures)}, get_checker_mapping(prim)};
                else if (id == pxr::TfToken{"SpectraScaleTexture"}) texture.data = ScaleTexture{{get_optional_relationship_id(prim, "spectra:first", paths.textures)}, {get_optional_relationship_id(prim, "spectra:second", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraMixTexture"}) texture.data = MixTexture{{get_optional_relationship_id(prim, "spectra:first", paths.textures)}, {get_optional_relationship_id(prim, "spectra:second", paths.textures)}, {get_optional_relationship_id(prim, "spectra:amount", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraDirectionMixTexture"}) texture.data = DirectionMixTexture{{get_optional_relationship_id(prim, "spectra:first", paths.textures)}, {get_optional_relationship_id(prim, "spectra:second", paths.textures)}, spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:direction"))};
                else if (id == pxr::TfToken{"SpectraBilerpTexture"}) {
                    const pxr::GfVec4f scalars = get_attribute<pxr::GfVec4f>(prim, "spectra:scalars");
                    BilerpTexture data{.scalars = {scalars[0], scalars[1], scalars[2], scalars[3]}, .mapping = get_texture_mapping(prim, "spectra:mapping")};
                    for (std::uint32_t corner = 0u; corner != 4u; ++corner) data.spectra[corner] = get_spectrum(prim, std::format("spectra:corner{}", corner), paths);
                    texture.data = std::move(data);
                } else throw std::runtime_error(std::format("Unsupported USD at {}.info:id: shader '{}' is not in the Spectra profile", prim.GetPath().GetString(), id.GetString()));
                resources.textures.emplace_back(std::move(texture));
            }
        }

        void read_materials(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Materials"});
            index_children(root, paths.materials);
            for (const pxr::UsdPrim& material_prim : root.GetAllChildren()) {
                const pxr::UsdPrim prim = stage->GetPrimAtPath(material_prim.GetPath().AppendChild(pxr::TfToken{"SpectraSurface"}));
                pxr::TfToken id{};
                pxr::UsdShadeShader{prim}.GetIdAttr().Get(&id);
                Material material{.id = {paths.materials.at(material_prim.GetPath().GetString())}, .name = get_name(material_prim), .revision = get_revision(material_prim)};
                if (id == pxr::TfToken{"SpectraInterface"}) material.data = InterfaceMaterialData{};
                else if (id == pxr::TfToken{"SpectraDiffuse"}) material.data = DiffuseMaterialData{get_spectrum(prim, "spectra:reflectance", paths), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraDiffuseTransmission"}) material.data = DiffuseTransmissionMaterialData{get_spectrum(prim, "spectra:reflectance", paths), get_spectrum(prim, "spectra:transmittance", paths), get_attribute<float>(prim, "spectra:scale"), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraConductor"}) material.data = ConductorMaterialData{get_optics(prim, paths), get_roughness(prim, "spectra:distribution", paths), get_attribute<bool>(prim, "spectra:remapRoughness"), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraDielectric"}) material.data = DielectricMaterialData{get_spectrum(prim, "spectra:eta", paths), get_roughness(prim, "spectra:distribution", paths), get_attribute<bool>(prim, "spectra:remapRoughness"), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraThinDielectric"}) material.data = ThinDielectricMaterialData{get_spectrum(prim, "spectra:eta", paths), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraCoatedDiffuse"}) material.data = CoatedDiffuseMaterialData{get_spectrum(prim, "spectra:reflectance", paths), get_spectrum(prim, "spectra:eta", paths), get_roughness(prim, "spectra:interface", paths), get_coating(prim, paths), get_attribute<bool>(prim, "spectra:remapRoughness"), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraCoatedConductor"}) material.data = CoatedConductorMaterialData{get_spectrum(prim, "spectra:interfaceEta", paths), get_roughness(prim, "spectra:interface", paths), get_optics(prim, paths), get_roughness(prim, "spectra:conductor", paths), get_coating(prim, paths), get_attribute<bool>(prim, "spectra:remapRoughness"), {get_optional_relationship_id(prim, "spectra:normalMap", paths.textures)}, {get_optional_relationship_id(prim, "spectra:bumpMap", paths.textures)}};
                else if (id == pxr::TfToken{"SpectraMix"}) material.data = MixMaterialData{{get_optional_relationship_id(prim, "spectra:first", paths.materials)}, {get_optional_relationship_id(prim, "spectra:second", paths.materials)}, get_float_parameter(prim, "spectra:amount", paths)};
                else throw std::runtime_error(std::format("Unsupported USD at {}.info:id: shader '{}' is not in the Spectra profile", prim.GetPath().GetString(), id.GetString()));
                resources.materials.emplace_back(std::move(material));
            }
        }

        void read_media(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Media"});
            index_children(root, paths.media);
            for (const pxr::UsdPrim& prim : root.GetAllChildren())
                resources.media.emplace_back(Medium{
                    .id             = {paths.media.at(prim.GetPath().GetString())},
                    .name           = get_name(prim),
                    .revision       = get_revision(prim),
                    .sigma_a        = get_spectrum(prim, "spectra:sigmaA", paths),
                    .sigma_s        = get_spectrum(prim, "spectra:sigmaS", paths),
                    .emission       = get_spectrum(prim, "spectra:emission", paths),
                    .density_scale  = get_attribute<float>(prim, "spectra:densityScale"),
                    .emission_scale = get_attribute<float>(prim, "spectra:emissionScale"),
                    .anisotropy     = get_attribute<float>(prim, "spectra:anisotropy"),
                });
        }

        void read_lights(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            std::vector<pxr::UsdPrim> prims{};
            const pxr::UsdPrim world_root = stage->GetPrimAtPath(pxr::SdfPath{"/World/Lights"});
            const pxr::UsdPrim area_root  = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/AreaLights"});
            for (const pxr::UsdPrim& prim : world_root.GetAllChildren()) prims.emplace_back(prim);
            for (const pxr::UsdPrim& prim : area_root.GetAllChildren()) prims.emplace_back(prim);
            std::uint64_t next_id = 1u;
            for (const pxr::UsdPrim& prim : prims) paths.lights.emplace(prim.GetPath().GetString(), next_id++);
            for (const pxr::UsdPrim& prim : prims) {
                Light light{.id = {paths.lights.at(prim.GetPath().GetString())}, .name = get_name(prim), .revision = get_revision(prim)};
                const pxr::TfToken type = get_attribute<pxr::TfToken>(prim, "spectra:lightType");
                if (type == pxr::TfToken{"point"}) light.data = PointLight{spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")), get_spectrum(prim, "spectra:intensity", paths), get_light_intensity(prim)};
                else if (type == pxr::TfToken{"spot"}) light.data = SpotLight{spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")), get_spectrum(prim, "spectra:intensity", paths), get_light_intensity(prim), get_attribute<float>(prim, "spectra:coneAngle"), get_attribute<float>(prim, "spectra:coneDelta")};
                else if (type == pxr::TfToken{"distant"}) light.data = DistantLight{spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")), get_spectrum(prim, "spectra:radiance", paths), get_light_intensity(prim)};
                else if (type == pxr::TfToken{"diffuseArea"}) {
                    DiffuseAreaLight data{
                        .radiance         = get_spectrum(prim, "spectra:radiance", paths),
                        .sidedness        = get_attribute<pxr::TfToken>(prim, "spectra:sidedness") == pxr::TfToken{"front"} ? EmissionSidedness::Front : EmissionSidedness::Both,
                        .scale            = get_attribute<float>(prim, "spectra:scale"),
                        .emission_texture = {get_optional_relationship_id(prim, "spectra:emissionTexture", paths.textures)},
                    };
                    if (get_attribute<bool>(prim, "spectra:hasPower")) data.power = get_attribute<float>(prim, "spectra:power");
                    light.data = std::move(data);
                } else if (type == pxr::TfToken{"infinite"}) light.data = InfiniteLight{get_spectrum(prim, "spectra:radiance", paths), spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")), get_light_intensity(prim), {get_optional_relationship_id(prim, "spectra:emissionTexture", paths.textures)}};
                else if (type == pxr::TfToken{"portalInfinite"}) {
                    PortalInfiniteLight data{.environment = {get_spectrum(prim, "spectra:radiance", paths), spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")), get_light_intensity(prim), {get_optional_relationship_id(prim, "spectra:emissionTexture", paths.textures)}}};
                    const pxr::VtArray<pxr::GfVec3f> portals = get_attribute<pxr::VtArray<pxr::GfVec3f>>(prim, "spectra:portals");
                    data.portals.resize(portals.size() / 4u);
                    for (std::size_t portal = 0u; portal != data.portals.size(); ++portal)
                        for (std::size_t corner = 0u; corner != 4u; ++corner) data.portals[portal][corner] = spectra(portals[portal * 4u + corner]);
                    light.data = std::move(data);
                } else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:lightType: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), type.GetString()));
                resources.lights.emplace_back(std::move(light));
            }
        }

        [[nodiscard]] VolumeRendering get_volume_rendering(const pxr::UsdPrim& prim, const ReaderPaths& paths, const bool field_relationships = false) {
            const auto field = [&prim, field_relationships](const std::string_view attribute, const std::string_view relationship) {
                if (!field_relationships) return get_attribute<std::string>(prim, attribute);
                return pxr::UsdVolVolume{prim}.HasFieldRelationship(token(relationship)) ? std::string{relationship} : std::string{};
            };
            return {
                .density_field                = field("spectra:rendering:densityField", "density"),
                .temperature_field            = field("spectra:rendering:temperatureField", "temperature"),
                .emission_scale_field         = field("spectra:rendering:emissionScaleField", "emissionScale"),
                .sigma_a_field                = field("spectra:rendering:sigmaAField", "sigmaA"),
                .sigma_s_field                = field("spectra:rendering:sigmaSField", "sigmaS"),
                .emission_field               = field("spectra:rendering:emissionField", "emission"),
                .field_color_space            = get_enum<SpectrumColorSpace>(prim, "spectra:rendering:fieldColorSpace", spectrum_color_spaces),
                .sigma_a                      = get_spectrum(prim, "spectra:rendering:sigmaA", paths),
                .sigma_s                      = get_spectrum(prim, "spectra:rendering:sigmaS", paths),
                .emission                     = get_spectrum(prim, "spectra:rendering:emission", paths),
                .density_scale                = get_attribute<float>(prim, "spectra:rendering:densityScale"),
                .emission_scale               = get_attribute<float>(prim, "spectra:rendering:emissionScale"),
                .anisotropy                   = get_attribute<float>(prim, "spectra:rendering:anisotropy"),
                .temperature_scale            = get_attribute<float>(prim, "spectra:rendering:temperatureScale"),
                .temperature_offset           = get_attribute<float>(prim, "spectra:rendering:temperatureOffset"),
                .minimum_emission_temperature = get_attribute<float>(prim, "spectra:rendering:minimumEmissionTemperature"),
                .blackbody_emission           = get_attribute<bool>(prim, "spectra:rendering:blackbodyEmission"),
            };
        }

        [[nodiscard]] OpenVdbVolume get_openvdb_volume(const pxr::UsdStageRefPtr& stage, const pxr::UsdVolVolume& volume) {
            OpenVdbVolume result{};
            for (const auto& [id, field_path] : volume.GetFieldPaths()) {
                const pxr::UsdVolOpenVDBAsset asset{stage->GetPrimAtPath(field_path)};
                if (!asset) throw std::runtime_error(std::format("Unsupported USD at {}.field:{}: target {} must be an OpenVDBAsset", volume.GetPath().GetString(), id.GetString(), field_path.GetString()));
                pxr::SdfAssetPath source{};
                pxr::TfToken grid_name{};
                pxr::TfToken data_type{};
                pxr::TfToken field_class{};
                std::string unit{};
                pxr::TfToken vector_space = token(vector_spaces[1]);
                asset.GetFilePathAttr().Get(&source);
                asset.GetFieldNameAttr().Get(&grid_name);
                asset.GetFieldDataTypeAttr().Get(&data_type);
                asset.GetFieldClassAttr().Get(&field_class);
                asset.GetPrim().GetAttribute(pxr::TfToken{"spectra:unit"}).Get(&unit);
                asset.GetPrim().GetAttribute(pxr::TfToken{"spectra:vectorSpace"}).Get(&vector_space);
                if (!field_class.IsEmpty() && field_class != pxr::UsdVolTokens->fogVolume) throw std::runtime_error(std::format("Unsupported USD at {}.fieldClass: Spectra accepts fogVolume OpenVDB fields", field_path.GetString()));
                FieldKind kind{};
                if (data_type == pxr::UsdVolTokens->float_) kind = FieldKind::Float;
                else if (data_type == pxr::UsdVolTokens->float3) kind = FieldKind::Float3;
                else throw std::runtime_error(std::format("Unsupported USD at {}.fieldDataType: Spectra accepts float and float3 OpenVDB fields", field_path.GetString()));
                const auto space = std::ranges::find(vector_spaces, vector_space.GetString());
                if (space == vector_spaces.end()) throw std::runtime_error(std::format("Unsupported USD at {}.spectra:vectorSpace: token '{}' is not in the current Spectra feature set", field_path.GetString(), vector_space.GetString()));
                result.fields.emplace_back(OpenVdbField{
                    .id              = id.GetString(),
                    .name            = asset.GetPrim().GetDisplayName().empty() ? id.GetString() : asset.GetPrim().GetDisplayName(),
                    .unit            = std::move(unit),
                    .source          = source.GetAssetPath(),
                    .resolved_source = source.GetResolvedPath(),
                    .grid_name       = grid_name.GetString(),
                    .kind            = kind,
                    .vector_space    = static_cast<VolumeVectorSpace>(space - vector_spaces.begin()),
                });
            }
            return result;
        }

        [[nodiscard]] VolumeDiagnostics get_volume_diagnostics(const pxr::UsdPrim& prim) {
            return {
                .field_id       = get_attribute<std::string>(prim, "spectra:diagnostics:field"),
                .mode           = get_enum<VolumeDiagnosticMode>(prim, "spectra:diagnostics:mode", diagnostic_modes),
                .mapping        = get_enum<FieldMapping>(prim, "spectra:diagnostics:mapping", field_mappings),
                .depth_mode     = get_enum<VisualizationDepthMode>(prim, "spectra:diagnostics:depthMode", depth_modes),
                .color_map      = get_enum<VisualizationColorMap>(prim, "spectra:diagnostics:colorMap", color_maps),
                .color          = spectra(get_attribute<pxr::GfVec4f>(prim, "spectra:diagnostics:color")),
                .minimum        = get_attribute<float>(prim, "spectra:diagnostics:minimum"),
                .maximum        = get_attribute<float>(prim, "spectra:diagnostics:maximum"),
                .slice_position = get_attribute<float>(prim, "spectra:diagnostics:slicePosition"),
                .opacity        = get_attribute<float>(prim, "spectra:diagnostics:opacity"),
                .threshold      = get_attribute<float>(prim, "spectra:diagnostics:threshold"),
                .scale          = get_attribute<float>(prim, "spectra:diagnostics:scale"),
                .width          = get_attribute<float>(prim, "spectra:diagnostics:width"),
                .axis           = get_attribute<std::uint32_t>(prim, "spectra:diagnostics:axis"),
                .sampling       = get_attribute<std::uint32_t>(prim, "spectra:diagnostics:sampling"),
                .steps          = get_attribute<std::uint32_t>(prim, "spectra:diagnostics:steps"),
                .category_mask  = get_attribute<std::uint32_t>(prim, "spectra:diagnostics:categoryMask"),
            };
        }

        void read_volumes(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/World/Volumes"});
            index_children(root, paths.volumes);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                const pxr::UsdVolVolume usd_volume{prim};
                const bool openvdb = !usd_volume.GetFieldPaths().empty();
                Volume volume{
                    .id              = {paths.volumes.at(prim.GetPath().GetString())},
                    .name            = get_name(prim),
                    .revision        = get_revision(prim),
                    .domain          = openvdb ? math::Bounds3::empty() : math::Bounds3{spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:domainMinimum")), spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:domainMaximum"))},
                    .transform       = spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")),
                    .rendering       = get_volume_rendering(prim, paths, openvdb),
                    .diagnostics     = get_volume_diagnostics(prim),
                    .exterior_medium = {get_optional_relationship_id(prim, "spectra:exteriorMedium", paths.media)},
                    .visible         = get_attribute<pxr::TfToken>(prim, "visibility") != pxr::UsdGeomTokens->invisible,
                };
                if (openvdb) {
                    volume.data = get_openvdb_volume(stage, usd_volume);
                } else {
                    const pxr::TfToken type = get_attribute<pxr::TfToken>(prim, "spectra:volumeType");
                    if (type == pxr::TfToken{"denseGrid"}) {
                        DenseGridVolume grid{.resolution = spectra(get_attribute<pxr::GfVec3i>(prim, "spectra:resolution"))};
                        const pxr::UsdPrim fields = stage->GetPrimAtPath(prim.GetPath().AppendChild(pxr::TfToken{"Fields"}));
                        for (const pxr::UsdPrim& field_prim : fields.GetAllChildren()) {
                            VolumeField field{
                                .id   = get_attribute<std::string>(field_prim, "spectra:id"),
                                .name = get_attribute<std::string>(field_prim, "spectra:name"),
                                .unit = get_attribute<std::string>(field_prim, "spectra:unit"),
                            };
                            const FieldKind kind                 = get_enum<FieldKind>(field_prim, "spectra:kind", field_kinds);
                            const VolumeFieldSampling sampling   = get_enum<VolumeFieldSampling>(field_prim, "spectra:sampling", volume_sampling);
                            const VolumeVectorSpace vector_space = get_enum<VolumeVectorSpace>(field_prim, "spectra:vectorSpace", vector_spaces);
                            if (kind == FieldKind::Float) field.data = ScalarVolumeField{sampling, spectra_floats(get_attribute<pxr::VtArray<float>>(field_prim, "spectra:values"))};
                            else if (kind == FieldKind::Float3) field.data = VectorVolumeField{sampling, vector_space, spectra(get_attribute<pxr::VtArray<pxr::GfVec3f>>(field_prim, "spectra:values"))};
                            else if (kind == FieldKind::UInt32) {
                                const pxr::VtArray<unsigned int> usd_values = get_attribute<pxr::VtArray<unsigned int>>(field_prim, "spectra:values");
                                std::vector<std::uint32_t> values{usd_values.begin(), usd_values.end()};
                                field.data = CategoryVolumeField{sampling, std::move(values)};
                            } else {
                                MacVolumeField values{.vector_space = vector_space};
                                values.values[0] = spectra_floats(get_attribute<pxr::VtArray<float>>(field_prim, "spectra:valuesX"));
                                values.values[1] = spectra_floats(get_attribute<pxr::VtArray<float>>(field_prim, "spectra:valuesY"));
                                values.values[2] = spectra_floats(get_attribute<pxr::VtArray<float>>(field_prim, "spectra:valuesZ"));
                                field.data       = std::move(values);
                            }
                            grid.fields.emplace_back(std::move(field));
                        }
                        volume.data = std::move(grid);
                    } else if (type == pxr::TfToken{"proceduralCloud"}) volume.data = ProceduralCloudVolume{get_attribute<float>(prim, "spectra:density"), get_attribute<float>(prim, "spectra:wispiness"), get_attribute<float>(prim, "spectra:frequency")};
                    else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:volumeType: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), type.GetString()));
                }
                resources.volumes.emplace_back(std::move(volume));
            }
        }

        void read_particles(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/World/Particles"});
            index_children(root, paths.particle_sets);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                ParticleSet particles{
                    .id        = {paths.particle_sets.at(prim.GetPath().GetString())},
                    .name      = get_name(prim),
                    .revision  = get_revision(prim),
                    .domain    = {spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:domainMinimum")), spectra(get_attribute<pxr::GfVec3f>(prim, "spectra:domainMaximum"))},
                    .transform = spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")),
                    .radius    = get_attribute<float>(prim, "spectra:radius"),
                    .visualization =
                        {
                            .field_id     = get_attribute<std::string>(prim, "spectra:visualization:field"),
                            .display      = get_enum<ParticleDisplayMode>(prim, "spectra:visualization:display", particle_displays),
                            .mapping      = get_enum<FieldMapping>(prim, "spectra:visualization:mapping", field_mappings),
                            .depth_mode   = get_enum<VisualizationDepthMode>(prim, "spectra:visualization:depthMode", depth_modes),
                            .color_map    = get_enum<VisualizationColorMap>(prim, "spectra:visualization:colorMap", color_maps),
                            .color        = spectra(get_attribute<pxr::GfVec4f>(prim, "spectra:visualization:color")),
                            .minimum      = get_attribute<float>(prim, "spectra:visualization:minimum"),
                            .maximum      = get_attribute<float>(prim, "spectra:visualization:maximum"),
                            .radius_scale = get_attribute<float>(prim, "spectra:visualization:radiusScale"),
                            .point_size   = get_attribute<float>(prim, "spectra:visualization:pointSize"),
                        },
                    .diagnostics =
                        {
                            .vector_field = get_attribute<std::string>(prim, "spectra:diagnostics:vectorField"),
                            .color_map    = get_enum<VisualizationColorMap>(prim, "spectra:diagnostics:colorMap", color_maps),
                            .minimum      = get_attribute<float>(prim, "spectra:diagnostics:minimum"),
                            .maximum      = get_attribute<float>(prim, "spectra:diagnostics:maximum"),
                            .scale        = get_attribute<float>(prim, "spectra:diagnostics:scale"),
                            .width        = get_attribute<float>(prim, "spectra:diagnostics:width"),
                            .sampling     = get_attribute<std::uint32_t>(prim, "spectra:diagnostics:sampling"),
                        },
                    .visible = get_attribute<pxr::TfToken>(prim, "visibility") != pxr::UsdGeomTokens->invisible,
                };
                const pxr::UsdPrim fields = stage->GetPrimAtPath(prim.GetPath().AppendChild(pxr::TfToken{"Fields"}));
                for (const pxr::UsdPrim& field_prim : fields.GetAllChildren()) {
                    ParticleField field{.id = get_attribute<std::string>(field_prim, "spectra:id"), .name = get_attribute<std::string>(field_prim, "spectra:name"), .unit = get_attribute<std::string>(field_prim, "spectra:unit")};
                    const FieldKind kind = get_enum<FieldKind>(field_prim, "spectra:kind", field_kinds);
                    if (kind == FieldKind::Float) field.data = ScalarParticleField{};
                    else if (kind == FieldKind::Float3) field.data = VectorParticleField{get_enum<VolumeVectorSpace>(field_prim, "spectra:vectorSpace", vector_spaces)};
                    else if (kind == FieldKind::UInt32) field.data = CategoryParticleField{};
                    else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:kind: MAC fields do not apply to particles", field_prim.GetPath().GetString()));
                    particles.fields.emplace_back(std::move(field));
                }
                resources.particle_sets.emplace_back(std::move(particles));
            }
        }

        void read_neural_fields(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/World/NeuralFields"});
            index_children(root, paths.neural_fields);
            for (const pxr::UsdPrim& prim : root.GetAllChildren())
                resources.neural_fields.emplace_back(NeuralField{
                    .id          = {paths.neural_fields.at(prim.GetPath().GetString())},
                    .name        = get_name(prim),
                    .transform   = spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")),
                    .diagnostics = {get_attribute<bool>(prim, "spectra:diagnostics:occupancyGrid")},
                    .visible     = get_attribute<pxr::TfToken>(prim, "visibility") != pxr::UsdGeomTokens->invisible,
                });
        }

        void read_prototypes(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Prototypes"});
            index_children(root, paths.prototypes);
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                Prototype prototype{.id = {paths.prototypes.at(prim.GetPath().GetString())}, .name = get_name(prim), .revision = get_revision(prim)};
                for (const pxr::UsdPrim& child : prim.GetAllChildren()) {
                    Primitive primitive{
                        .geometry            = {get_optional_relationship_id(child, "spectra:geometry", paths.geometries)},
                        .spheres             = {get_optional_relationship_id(child, "spectra:spheres", paths.sphere_sets)},
                        .material            = {get_optional_relationship_id(child, "spectra:material", paths.materials)},
                        .area_light          = {get_optional_relationship_id(child, "spectra:areaLight", paths.lights)},
                        .media               = {{get_optional_relationship_id(child, "spectra:insideMedium", paths.media)}, {get_optional_relationship_id(child, "spectra:outsideMedium", paths.media)}},
                        .alpha               = {get_optional_relationship_id(child, "spectra:alpha", paths.textures)},
                        .reverse_orientation = get_attribute<bool>(child, "spectra:reverseOrientation"),
                        .transform           = spectra(get_attribute<pxr::GfMatrix4d>(child, "spectra:transform")),
                    };
                    for (std::size_t face = 0u;; ++face) {
                        const std::string relationship = std::format("spectra:faceMaterial:f{:06}", face);
                        if (!child.HasRelationship(token(relationship))) break;
                        primitive.face_materials.emplace_back(MaterialId{get_optional_relationship_id(child, relationship, paths.materials)});
                    }
                    prototype.primitives.emplace_back(std::move(primitive));
                }
                resources.prototypes.emplace_back(std::move(prototype));
            }
        }

        void read_instances(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            std::vector<pxr::UsdPrim> instances{};
            for (const pxr::UsdPrim& prim : pxr::UsdPrimRange{stage->GetPrimAtPath(pxr::SdfPath{"/World"})})
                if (prim.HasRelationship(pxr::TfToken{"spectra:prototype"})) {
                    paths.instances.emplace(prim.GetPath().GetString(), paths.instances.size() + 1u);
                    instances.emplace_back(prim);
                }
            for (const pxr::UsdPrim& prim : instances)
                resources.instances.emplace_back(Instance{
                    .id        = {paths.instances.at(prim.GetPath().GetString())},
                    .name      = get_name(prim),
                    .revision  = get_revision(prim),
                    .prototype = {get_optional_relationship_id(prim, "spectra:prototype", paths.prototypes)},
                    .transform = spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")),
                    .visible   = get_attribute<pxr::TfToken>(prim, "visibility") != pxr::UsdGeomTokens->invisible,
                });
        }

        void read_cameras(const pxr::UsdStageRefPtr& stage, SceneResources& resources, ReaderPaths& paths) {
            std::vector<pxr::UsdPrim> cameras{};
            for (const pxr::UsdPrim& prim : pxr::UsdPrimRange{stage->GetPrimAtPath(pxr::SdfPath{"/World"})})
                if (pxr::UsdGeomCamera{prim} && prim.HasAttribute(pxr::TfToken{"spectra:cameraType"})) {
                    paths.cameras.emplace(prim.GetPath().GetString(), paths.cameras.size() + 1u);
                    cameras.emplace_back(prim);
                }
            for (const pxr::UsdPrim& prim : cameras) {
                Camera camera{
                    .id            = {paths.cameras.at(prim.GetPath().GetString())},
                    .name          = get_name(prim),
                    .revision      = get_revision(prim),
                    .transform     = spectra(get_attribute<pxr::GfMatrix4d>(prim, "spectra:transform")),
                    .exposure_time = get_attribute<float>(prim, "spectra:exposureTime"),
                    .medium        = {get_optional_relationship_id(prim, "spectra:medium", paths.media)},
                };
                const ScreenWindow screen{spectra(get_attribute<pxr::GfVec2f>(prim, "spectra:screenMinimum")), spectra(get_attribute<pxr::GfVec2f>(prim, "spectra:screenMaximum"))};
                const float lens_radius    = get_attribute<float>(prim, "spectra:lensRadius");
                const float focal_distance = get_attribute<float>(prim, "spectra:focalDistance");
                const float near_plane     = get_attribute<float>(prim, "spectra:nearPlane");
                const float far_plane      = get_attribute<float>(prim, "spectra:farPlane");
                const pxr::TfToken type    = get_attribute<pxr::TfToken>(prim, "spectra:cameraType");
                if (type == pxr::TfToken{"perspective"}) camera.data = PerspectiveCameraData{get_attribute<float>(prim, "spectra:verticalFov"), screen, lens_radius, focal_distance, near_plane, far_plane};
                else if (type == pxr::TfToken{"orthographic"}) camera.data = OrthographicCameraData{screen, lens_radius, focal_distance, near_plane, far_plane};
                else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:cameraType: token '{}' is not in the Spectra profile", prim.GetPath().GetString(), type.GetString()));
                resources.cameras.emplace_back(std::move(camera));
            }
        }

        void read_render_settings(const pxr::UsdStageRefPtr& stage, Scene& scene, ReaderPaths& paths) {
            const pxr::UsdPrim film_root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Films"});
            index_children(film_root, paths.films);
            for (const pxr::UsdPrim& prim : film_root.GetAllChildren()) {
                const pxr::GfVec2i resolution       = get_attribute<pxr::GfVec2i>(prim, "resolution");
                const pxr::GfVec2i pixel_min        = get_attribute<pxr::GfVec2i>(prim, "spectra:pixelMinimum");
                const pxr::GfVec2i pixel_max        = get_attribute<pxr::GfVec2i>(prim, "spectra:pixelMaximum");
                const pxr::GfMatrix3d sensor_matrix = get_attribute<pxr::GfMatrix3d>(prim, "spectra:sensorToOutputRGB");
                Film film{
                    .id                   = {paths.films.at(prim.GetPath().GetString())},
                    .name                 = get_name(prim),
                    .revision             = get_revision(prim),
                    .resolution           = {static_cast<std::uint32_t>(resolution[0]), static_cast<std::uint32_t>(resolution[1])},
                    .pixel_minimum        = {static_cast<std::uint32_t>(pixel_min[0]), static_cast<std::uint32_t>(pixel_min[1])},
                    .pixel_maximum        = {static_cast<std::uint32_t>(pixel_max[0]), static_cast<std::uint32_t>(pixel_max[1])},
                    .exposure             = get_attribute<float>(prim, "spectra:exposure"),
                    .iso                  = get_attribute<float>(prim, "spectra:iso"),
                    .color_space          = get_enum<SpectrumColorSpace>(prim, "spectra:colorSpace", spectrum_color_spaces),
                    .sensor_response      = spectra_floats(get_attribute<pxr::VtArray<float>>(prim, "spectra:sensorResponse")),
                    .sensor_to_output_rgb = {static_cast<float>(sensor_matrix[0][0]), static_cast<float>(sensor_matrix[0][1]), static_cast<float>(sensor_matrix[0][2]), static_cast<float>(sensor_matrix[1][0]), static_cast<float>(sensor_matrix[1][1]), static_cast<float>(sensor_matrix[1][2]), static_cast<float>(sensor_matrix[2][0]), static_cast<float>(sensor_matrix[2][1]), static_cast<float>(sensor_matrix[2][2])},
                    .filter =
                        {
                            .kind   = get_enum<FilterKind>(prim, "spectra:filter:kind", filter_kinds),
                            .radius = spectra(get_attribute<pxr::GfVec2f>(prim, "spectra:filter:radius")),
                            .sigma  = get_attribute<float>(prim, "spectra:filter:sigma"),
                            .b      = get_attribute<float>(prim, "spectra:filter:b"),
                            .c      = get_attribute<float>(prim, "spectra:filter:c"),
                            .tau    = get_attribute<float>(prim, "spectra:filter:tau"),
                        },
                    .gbuffer              = get_attribute<bool>(prim, "spectra:gbuffer"),
                    .gbuffer_camera_space = get_attribute<bool>(prim, "spectra:gbufferCameraSpace"),
                };
                if (get_attribute<bool>(prim, "spectra:hasMaximumComponentValue")) film.maximum_component_value = get_attribute<float>(prim, "spectra:maximumComponentValue");
                scene.resources.films.emplace_back(std::move(film));
            }
            const pxr::UsdPrim sampler_root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Samplers"});
            index_children(sampler_root, paths.samplers);
            for (const pxr::UsdPrim& prim : sampler_root.GetAllChildren())
                scene.resources.samplers.emplace_back(Sampler{
                    .id                = {paths.samplers.at(prim.GetPath().GetString())},
                    .name              = get_name(prim),
                    .revision          = get_revision(prim),
                    .kind              = get_enum<SamplerKind>(prim, "spectra:kind", sampler_kinds),
                    .samples_per_pixel = get_attribute<std::uint32_t>(prim, "spectra:samplesPerPixel"),
                    .seed              = get_attribute<std::uint32_t>(prim, "spectra:seed"),
                    .jitter            = get_attribute<bool>(prim, "spectra:jitter"),
                    .x_strata          = get_attribute<std::uint32_t>(prim, "spectra:xStrata"),
                    .y_strata          = get_attribute<std::uint32_t>(prim, "spectra:yStrata"),
                    .randomization     = get_enum<SamplerRandomization>(prim, "spectra:randomization", sampler_randomizations),
                });
            const pxr::UsdPrim settings = stage->GetPrimAtPath(pxr::SdfPath{"/Render/Settings"});
            scene.active_camera         = {get_optional_relationship_id(settings, "camera", paths.cameras)};
            scene.active_film           = {get_optional_relationship_id(settings, "spectra:film", paths.films)};
            scene.active_sampler        = {get_optional_relationship_id(settings, "spectra:sampler", paths.samplers)};
            scene.transport             = {
                            .maximum_depth = get_attribute<std::uint32_t>(settings, "spectra:transport:maximumDepth"),
                            .light_sampler = get_enum<LightSamplerKind>(settings, "spectra:transport:lightSampler", light_sampler_kinds),
                            .regularize    = get_attribute<bool>(settings, "spectra:transport:regularize"),
            };
        }

        [[nodiscard]] std::uint64_t get_simulation_resource_id(const pxr::UsdPrim& prim, const ReaderPaths& paths) {
            const pxr::SdfPath path = get_relationship(prim, "spectra:resource");
            const std::array maps{&paths.geometries, &paths.sphere_sets, &paths.particle_sets, &paths.volumes, &paths.neural_fields};
            for (const auto* map : maps) {
                const auto resource = map->find(path.GetString());
                if (resource != map->end()) return resource->second;
            }
            throw std::runtime_error(std::format("Unsupported USD at {}.spectra:resource: target {} is not a simulation resource", prim.GetPath().GetString(), path.GetString()));
        }

        void read_simulation(const pxr::UsdStageRefPtr& stage, Scene& scene, const ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra/Simulation"});
            if (!root) return;
            SimulationSetup setup{
                .clock =
                    {
                        .step_seconds = get_attribute<double>(root, "spectra:clock:stepSeconds"),
                        .start_step   = get_attribute<std::uint64_t>(root, "spectra:clock:startStep"),
                        .loop         = get_attribute<bool>(root, "spectra:clock:loop"),
                    },
                .seed = get_attribute<std::uint64_t>(root, "spectra:seed"),
            };
            if (get_attribute<bool>(root, "spectra:clock:hasEndStep")) setup.clock.end_step = get_attribute<std::uint64_t>(root, "spectra:clock:endStep");
            for (const pxr::UsdPrim& prim : root.GetAllChildren()) {
                SimulationSystem system{
                    .id          = {get_attribute<std::string>(prim, "spectra:id")},
                    .name        = get_attribute<std::string>(prim, "spectra:name"),
                    .provider_id = get_attribute<std::string>(prim, "spectra:provider"),
                    .enabled     = get_attribute<bool>(prim, "spectra:enabled"),
                    .visible     = get_attribute<bool>(prim, "spectra:visible"),
                };
                for (const pxr::UsdPrim& child : prim.GetChildren()) {
                    if (child.GetName().GetString().starts_with("Parameter_")) {
                        const pxr::GfVec3d floating = get_attribute<pxr::GfVec3d>(child, "spectra:floating");
                        system.parameters.emplace_back(SimulationParameterSetting{
                            .parameter_id = get_attribute<std::string>(child, "spectra:id"),
                            .value        = {get_enum<SimulationParameterKind>(child, "spectra:kind", parameter_kinds), get_attribute<std::int64_t>(child, "spectra:integer"), {floating[0], floating[1], floating[2]}},
                        });
                    } else if (child.GetName().GetString().starts_with("Binding_")) system.scene_bindings.emplace_back(SimulationOutputBinding{.output_id = get_attribute<std::string>(child, "spectra:output"), .resource_id = get_simulation_resource_id(child, paths)});
                    else if (child.GetName().GetString().starts_with("Visualization_")) {
                        SimulationVisualization visualization{
                            .id                 = child.GetName().GetString(),
                            .output_id          = get_attribute<std::string>(child, "spectra:output"),
                            .name               = get_attribute<std::string>(child, "spectra:name"),
                            .depth_mode         = get_enum<VisualizationDepthMode>(child, "spectra:depthMode", depth_modes),
                            .composition_domain = get_enum<VisualizationCompositionDomain>(child, "spectra:compositionDomain", composition_domains),
                            .anchor             = {get_optional_relationship_id(child, "spectra:anchor", paths.instances)},
                            .color              = spectra(get_attribute<pxr::GfVec4f>(child, "spectra:color")),
                            .visible            = get_attribute<bool>(child, "spectra:visible"),
                        };
                        const pxr::TfToken type = get_attribute<pxr::TfToken>(child, "spectra:type");
                        if (type == pxr::TfToken{"points"}) visualization.data = PointVisualization{get_attribute<float>(child, "spectra:size"), get_attribute<float>(child, "spectra:scalarMinimum"), get_attribute<float>(child, "spectra:scalarMaximum"), get_enum<VisualizationColorSource>(child, "spectra:colorSource", color_sources), get_enum<VisualizationColorMap>(child, "spectra:colorMap", color_maps)};
                        else if (type == pxr::TfToken{"segments"}) visualization.data = SegmentVisualization{get_attribute<float>(child, "spectra:width"), get_attribute<float>(child, "spectra:scalarMinimum"), get_attribute<float>(child, "spectra:scalarMaximum"), get_enum<VisualizationColorSource>(child, "spectra:colorSource", color_sources), get_enum<VisualizationColorMap>(child, "spectra:colorMap", color_maps)};
                        else if (type == pxr::TfToken{"vectors"}) visualization.data = VectorVisualization{get_attribute<float>(child, "spectra:width"), get_attribute<float>(child, "spectra:scale"), get_attribute<float>(child, "spectra:scalarMinimum"), get_attribute<float>(child, "spectra:scalarMaximum"), get_enum<VisualizationColorSource>(child, "spectra:colorSource", color_sources), get_enum<VisualizationColorMap>(child, "spectra:colorMap", color_maps)};
                        else if (type == pxr::TfToken{"image"}) visualization.data = ImageVisualization{spectra(get_attribute<pxr::GfVec4f>(child, "spectra:screenRect"))};
                        else if (type == pxr::TfToken{"surface"}) visualization.data = SurfaceVisualization{get_attribute<float>(child, "spectra:scalarMinimum"), get_attribute<float>(child, "spectra:scalarMaximum"), get_enum<VisualizationColorSource>(child, "spectra:colorSource", color_sources), get_enum<VisualizationColorMap>(child, "spectra:colorMap", color_maps)};
                        else if (type == pxr::TfToken{"occupancyGrid"}) visualization.data = NeuralFieldVisualization{};
                        else if (const auto mode = std::ranges::find(derived_mesh_modes, type.GetString()); mode != derived_mesh_modes.end()) visualization.data = DerivedMeshVisualization{static_cast<DerivedMeshVisualizationMode>(mode - derived_mesh_modes.begin()), get_attribute<float>(child, "spectra:width"), get_attribute<float>(child, "spectra:scale")};
                        else throw std::runtime_error(std::format("Unsupported USD at {}.spectra:type: visualization '{}' is not in the Spectra profile", child.GetPath().GetString(), type.GetString()));
                        system.visualizations.emplace_back(std::move(visualization));
                    }
                }
                setup.systems.emplace_back(std::move(system));
            }
            scene.simulation = std::move(setup);
        }

        [[nodiscard]] Scene read_spectra_profile(const pxr::UsdStageRefPtr& stage) {
            const pxr::UsdPrim spectra_prim = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra"});
            const std::uint32_t version     = get_attribute<std::uint32_t>(spectra_prim, "spectra:profileVersion");
            if (version != profile_version) throw std::runtime_error(std::format("Unsupported Spectra USD Profile {} at /Spectra.spectra:profileVersion; expected {}", version, profile_version));
            Scene scene{get_attribute<std::string>(spectra_prim, "spectra:sceneName")};
            ReaderPaths paths{};
            read_geometries(stage, scene.resources, paths);
            read_sphere_sets(stage, scene.resources, paths);
            read_textures(stage, scene.resources, paths);
            read_materials(stage, scene.resources, paths);
            read_media(stage, scene.resources, paths);
            read_lights(stage, scene.resources, paths);
            read_volumes(stage, scene.resources, paths);
            read_particles(stage, scene.resources, paths);
            read_neural_fields(stage, scene.resources, paths);
            read_prototypes(stage, scene.resources, paths);
            read_instances(stage, scene.resources, paths);
            read_cameras(stage, scene.resources, paths);
            read_render_settings(stage, scene, paths);
            read_simulation(stage, scene, paths);
            return scene;
        }

        [[nodiscard]] std::string dcc_name(const pxr::UsdPrim& prim) {
            const std::string display_name = prim.GetDisplayName();
            return display_name.empty() ? prim.GetName().GetString() : display_name;
        }

        template <typename Type>
        [[nodiscard]] Type shader_input(const pxr::UsdShadeShader& shader, const std::string_view name, const Type& standard_default) {
            const pxr::UsdShadeInput input = shader.GetInput(token(name));
            if (!input) return standard_default;
            Type value = standard_default;
            input.Get(&value);
            return value;
        }

        [[nodiscard]] math::Transform dcc_basis(const pxr::UsdStageRefPtr& stage) {
            const float scale          = static_cast<float>(pxr::UsdGeomGetStageMetersPerUnit(stage));
            const pxr::TfToken up_axis = pxr::UsdGeomGetStageUpAxis(stage);
            if (up_axis == pxr::UsdGeomTokens->z) return math::Transform{{scale, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, -scale, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
            if (up_axis != pxr::UsdGeomTokens->y) throw std::runtime_error(std::format("Unsupported USD Stage upAxis '{}': Spectra accepts Y-up and Z-up stages", up_axis.GetString()));
            return math::Transform{{scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
        }

        [[nodiscard]] math::Transform dcc_transform(const pxr::UsdStageRefPtr& stage, const pxr::UsdPrim& prim, const math::Transform& basis) {
            return basis * spectra(pxr::UsdGeomXformable{prim}.ComputeLocalToWorldTransform(pxr::UsdTimeCode::Default()));
        }

        [[nodiscard]] math::Transform dcc_frame_transform(const pxr::UsdStageRefPtr& stage, const pxr::UsdPrim& prim, const math::Transform& basis) {
            math::Transform transform = dcc_transform(stage, prim, basis);
            for (std::uint32_t column = 0u; column != 3u; ++column) {
                const float length = std::sqrt(transform.matrix[column * 4u] * transform.matrix[column * 4u] + transform.matrix[column * 4u + 1u] * transform.matrix[column * 4u + 1u] + transform.matrix[column * 4u + 2u] * transform.matrix[column * 4u + 2u]);
                transform.matrix[column * 4u] /= length;
                transform.matrix[column * 4u + 1u] /= length;
                transform.matrix[column * 4u + 2u] /= length;
            }
            return transform;
        }

        void reject_time_samples(const pxr::UsdStageRefPtr& stage) {
            for (const pxr::UsdPrim& prim : pxr::UsdPrimRange::Stage(stage, pxr::UsdTraverseInstanceProxies()))
                for (const pxr::UsdAttribute& attribute : prim.GetAttributes())
                    if (attribute.ValueMightBeTimeVarying()) throw std::runtime_error(std::format("Unsupported USD at {}.{}: time-sampled DCC scenes are not in the current Spectra feature set", prim.GetPath().GetString(), attribute.GetName().GetString()));
        }

        void read_dcc_materials(const pxr::UsdStageRefPtr& stage, SceneResources& resources, std::unordered_map<std::string, MaterialId>& materials) {
            for (const pxr::UsdPrim& prim : stage->Traverse()) {
                const pxr::UsdShadeMaterial usd_material{prim};
                if (!usd_material) continue;
                const MaterialId id{resources.materials.size() + 1u};
                materials.emplace(prim.GetPath().GetString(), id);
                pxr::UsdShadeShader shader = usd_material.ComputeSurfaceSource();
                if (!shader) shader = usd_material.ComputeSurfaceSource(pxr::TfToken{"mtlx"});
                if (!shader) throw std::runtime_error(std::format("Unsupported USD at {}: material has no universal or MaterialX surface shader", prim.GetPath().GetString()));
                for (const pxr::UsdShadeInput& input : shader.GetInputs())
                    if (input.GetAttr().HasAuthoredConnections()) throw std::runtime_error(std::format("Unsupported USD at {}.inputs:{}: connected shader inputs are not in the current Spectra feature set", shader.GetPath().GetString(), input.GetBaseName().GetString()));
                pxr::TfToken shader_id{};
                shader.GetIdAttr().Get(&shader_id);
                Material material{.id = id, .name = dcc_name(prim)};
                if (shader_id == pxr::TfToken{"UsdPreviewSurface"}) {
                    const math::Float3 color = spectra(shader_input(shader, "diffuseColor", pxr::GfVec3f{0.18f, 0.18f, 0.18f}));
                    const float metallic     = shader_input(shader, "metallic", 0.0f);
                    const float roughness    = shader_input(shader, "roughness", 0.5f);
                    const float opacity      = shader_input(shader, "opacity", 1.0f);
                    if (opacity != 1.0f) throw std::runtime_error(std::format("Unsupported USD at {}.inputs:opacity: only opaque UsdPreviewSurface is in the current Spectra feature set", shader.GetPath().GetString()));
                    if (metallic == 0.0f) material.data = DiffuseMaterialData{SpectrumParameter{.value = color}};
                    else if (metallic == 1.0f) material.data = ConductorMaterialData{ConductorReflectance{SpectrumParameter{.value = color}}, MaterialRoughness{{roughness, {}}}};
                    else throw std::runtime_error(std::format("Unsupported USD at {}.inputs:metallic: fractional metallic values are not in the current Spectra feature set", shader.GetPath().GetString()));
                } else if (shader_id == pxr::TfToken{"ND_open_pbr_surface_surfaceshader"}) {
                    const math::Float3 color = spectra(shader_input(shader, "base_color", pxr::GfVec3f{0.8f, 0.8f, 0.8f}));
                    const float metallic     = shader_input(shader, "base_metalness", 0.0f);
                    const float roughness    = shader_input(shader, "specular_roughness", 0.3f);
                    const float transmission = shader_input(shader, "transmission_weight", 0.0f);
                    if (transmission == 1.0f && metallic == 0.0f) material.data = DielectricMaterialData{SpectrumParameter{.encoding = SpectrumEncoding::Constant, .scalar = shader_input(shader, "specular_ior", 1.5f)}, MaterialRoughness{{roughness, {}}}};
                    else if (transmission == 0.0f && metallic == 0.0f) material.data = DiffuseMaterialData{SpectrumParameter{.value = color}};
                    else if (transmission == 0.0f && metallic == 1.0f) material.data = ConductorMaterialData{ConductorReflectance{SpectrumParameter{.value = color}}, MaterialRoughness{{roughness, {}}}};
                    else throw std::runtime_error(std::format("Unsupported USD at {}: OpenPBR blends between material lobes that are discrete in the current Spectra feature set", shader.GetPath().GetString()));
                } else if (shader_id == pxr::TfToken{"ND_standard_surface_surfaceshader"}) {
                    const math::Float3 color = spectra(shader_input(shader, "base_color", pxr::GfVec3f{0.8f, 0.8f, 0.8f}));
                    const float metallic     = shader_input(shader, "metalness", 0.0f);
                    const float roughness    = shader_input(shader, "specular_roughness", 0.2f);
                    const float transmission = shader_input(shader, "transmission", 0.0f);
                    if (transmission == 1.0f && metallic == 0.0f) material.data = DielectricMaterialData{SpectrumParameter{.encoding = SpectrumEncoding::Constant, .scalar = shader_input(shader, "specular_IOR", 1.5f)}, MaterialRoughness{{roughness, {}}}};
                    else if (transmission == 0.0f && metallic == 0.0f) material.data = DiffuseMaterialData{SpectrumParameter{.value = color}};
                    else if (transmission == 0.0f && metallic == 1.0f) material.data = ConductorMaterialData{ConductorReflectance{SpectrumParameter{.value = color}}, MaterialRoughness{{roughness, {}}}};
                    else throw std::runtime_error(std::format("Unsupported USD at {}: MaterialX Standard Surface blends between material lobes that are discrete in the current Spectra feature set", shader.GetPath().GetString()));
                } else throw std::runtime_error(std::format("Unsupported USD at {}.info:id: shader '{}' is not in the current Spectra feature set", shader.GetPath().GetString(), shader_id.GetString()));
                resources.materials.emplace_back(std::move(material));
            }
        }

        [[nodiscard]] MaterialId dcc_material(const pxr::UsdPrim& prim, const std::unordered_map<std::string, MaterialId>& materials) {
            const pxr::UsdShadeMaterial material = pxr::UsdShadeMaterialBindingAPI{prim}.ComputeBoundMaterial();
            if (!material) throw std::runtime_error(std::format("Unsupported USD at {}: every renderable prim requires a material binding", prim.GetPath().GetString()));
            const auto found = materials.find(material.GetPath().GetString());
            if (found == materials.end()) throw std::runtime_error(std::format("Unsupported USD at {}: bound material {} is outside the composed Stage", prim.GetPath().GetString(), material.GetPath().GetString()));
            return found->second;
        }

        struct DccLightValues {
            SpectrumParameter spectrum{};
            float scale{};
        };

        [[nodiscard]] DccLightValues dcc_light_values(const pxr::UsdLuxLightAPI& light) {
            pxr::GfVec3f color{1.0f};
            float intensity{1.0f};
            float exposure{};
            bool enable_color_temperature{};
            float color_temperature{6500.0f};
            light.GetColorAttr().Get(&color);
            light.GetIntensityAttr().Get(&intensity);
            light.GetExposureAttr().Get(&exposure);
            light.GetEnableColorTemperatureAttr().Get(&enable_color_temperature);
            light.GetColorTemperatureAttr().Get(&color_temperature);
            if (enable_color_temperature) {
                const pxr::GfVec3f blackbody = pxr::UsdLuxBlackbodyTemperatureAsRgb(color_temperature);
                color                        = {color[0] * blackbody[0], color[1] * blackbody[1], color[2] * blackbody[2]};
            }
            return {SpectrumParameter{.value = spectra(color), .encoding = SpectrumEncoding::RgbIlluminant}, intensity * std::exp2(exposure)};
        }

        [[nodiscard]] TriangleMeshGeometry dcc_mesh(const pxr::UsdPrim& prim) {
            const pxr::UsdGeomMesh mesh{prim};
            pxr::TfToken subdivision{};
            mesh.GetSubdivisionSchemeAttr().Get(&subdivision);
            if (!subdivision.IsEmpty() && subdivision != pxr::UsdGeomTokens->none) throw std::runtime_error(std::format("Unsupported USD at {}.subdivisionScheme: subdivision surfaces are not in the current Spectra feature set", prim.GetPath().GetString()));
            pxr::VtArray<int> counts{};
            mesh.GetFaceVertexCountsAttr().Get(&counts);
            if (std::ranges::any_of(counts, [](const int count) { return count != 3; })) throw std::runtime_error(std::format("Unsupported USD at {}.faceVertexCounts: only triangle topology is in the current Spectra feature set", prim.GetPath().GetString()));
            pxr::VtArray<pxr::GfVec3f> positions{};
            pxr::VtArray<pxr::GfVec3f> normals{};
            pxr::VtArray<pxr::GfVec2f> texture_coordinates{};
            pxr::VtArray<int> indices{};
            mesh.GetPointsAttr().Get(&positions);
            mesh.GetFaceVertexIndicesAttr().Get(&indices);
            mesh.GetNormalsAttr().Get(&normals);
            const pxr::UsdGeomPrimvar st = pxr::UsdGeomPrimvarsAPI{prim}.GetPrimvar(pxr::TfToken{"st"});
            if (st) st.ComputeFlattened(&texture_coordinates);
            const pxr::TfToken normal_interpolation  = mesh.GetNormalsInterpolation();
            const pxr::TfToken texture_interpolation = st ? st.GetInterpolation() : pxr::TfToken{};
            const auto value_index                   = [&prim](const pxr::TfToken interpolation, const std::size_t corner, const std::uint32_t point) {
                if (interpolation == pxr::UsdGeomTokens->vertex || interpolation == pxr::UsdGeomTokens->varying) return static_cast<std::size_t>(point);
                if (interpolation == pxr::UsdGeomTokens->faceVarying) return corner;
                if (interpolation == pxr::UsdGeomTokens->uniform) return corner / 3u;
                if (interpolation == pxr::UsdGeomTokens->constant) return 0uz;
                throw std::runtime_error(std::format("Unsupported USD at {}: interpolation '{}' is not in the current Spectra feature set", prim.GetPath().GetString(), interpolation.GetString()));
            };
            TriangleMeshGeometry result{
                .positions           = spectra(positions),
                .normals             = normals.empty() ? std::vector<math::Float3>{} : std::vector<math::Float3>(positions.size()),
                .texture_coordinates = texture_coordinates.empty() ? std::vector<math::Float2>{} : std::vector<math::Float2>(positions.size()),
                .indices             = std::vector<std::uint32_t>(indices.size()),
            };
            std::vector<std::vector<std::uint32_t>> variants(positions.size());
            for (std::size_t corner = 0u; corner != indices.size(); ++corner) {
                const std::uint32_t point             = static_cast<std::uint32_t>(indices[corner]);
                const math::Float3 normal             = normals.empty() ? math::Float3{} : spectra(normals[value_index(normal_interpolation, corner, point)]);
                const math::Float2 texture_coordinate = texture_coordinates.empty() ? math::Float2{} : spectra(texture_coordinates[value_index(texture_interpolation, corner, point)]);
                std::uint32_t vertex{};
                bool found{};
                for (const std::uint32_t candidate : variants[point])
                    if ((normals.empty() || (std::abs(result.normals[candidate].x - normal.x) < 0.00001f && std::abs(result.normals[candidate].y - normal.y) < 0.00001f && std::abs(result.normals[candidate].z - normal.z) < 0.00001f)) && (texture_coordinates.empty() || result.texture_coordinates[candidate] == texture_coordinate)) {
                        vertex = candidate;
                        found  = true;
                        break;
                    }
                if (!found) {
                    if (variants[point].empty()) vertex = point;
                    else {
                        vertex = static_cast<std::uint32_t>(result.positions.size());
                        result.positions.emplace_back(result.positions[point]);
                        if (!normals.empty()) result.normals.emplace_back();
                        if (!texture_coordinates.empty()) result.texture_coordinates.emplace_back();
                    }
                    if (!normals.empty()) result.normals[vertex] = normal;
                    if (!texture_coordinates.empty()) result.texture_coordinates[vertex] = texture_coordinate;
                    variants[point].emplace_back(vertex);
                }
                result.indices[corner] = vertex;
            }
            return result;
        }

        void read_dcc_volumes(const pxr::UsdStageRefPtr& stage, SceneResources& resources, const math::Transform& basis) {
            for (const pxr::UsdPrim& prim : pxr::UsdPrimRange::Stage(stage, pxr::UsdTraverseInstanceProxies())) {
                const pxr::UsdVolVolume usd_volume{prim};
                if (!usd_volume) continue;
                const pxr::UsdVolVolume::FieldMap fields = usd_volume.GetFieldPaths();
                VolumeRendering rendering{
                    .density_field        = fields.contains(pxr::TfToken{"density"}) ? "density" : "",
                    .temperature_field    = fields.contains(pxr::TfToken{"temperature"}) ? "temperature" : "",
                    .emission_scale_field = fields.contains(pxr::TfToken{"emissionScale"}) ? "emissionScale" : "",
                    .sigma_a_field        = fields.contains(pxr::TfToken{"sigmaA"}) ? "sigmaA" : "",
                    .sigma_s_field        = fields.contains(pxr::TfToken{"sigmaS"}) ? "sigmaS" : "",
                    .emission_field       = fields.contains(pxr::TfToken{"emission"}) ? "emission" : "",
                    .sigma_a              = SpectrumParameter{.encoding = SpectrumEncoding::RgbUnbounded},
                    .sigma_s              = SpectrumParameter{.value = {1.0f, 1.0f, 1.0f}, .encoding = SpectrumEncoding::RgbUnbounded},
                    .emission             = SpectrumParameter{.encoding = SpectrumEncoding::RgbIlluminant},
                };
                if (has_attribute(prim, "spectra:rendering:densityScale")) rendering.density_scale = get_attribute<float>(prim, "spectra:rendering:densityScale");
                if (has_attribute(prim, "spectra:rendering:emissionScale")) rendering.emission_scale = get_attribute<float>(prim, "spectra:rendering:emissionScale");
                if (has_attribute(prim, "spectra:rendering:anisotropy")) rendering.anisotropy = get_attribute<float>(prim, "spectra:rendering:anisotropy");
                if (has_attribute(prim, "spectra:rendering:temperatureScale")) rendering.temperature_scale = get_attribute<float>(prim, "spectra:rendering:temperatureScale");
                if (has_attribute(prim, "spectra:rendering:temperatureOffset")) rendering.temperature_offset = get_attribute<float>(prim, "spectra:rendering:temperatureOffset");
                if (has_attribute(prim, "spectra:rendering:minimumEmissionTemperature")) rendering.minimum_emission_temperature = get_attribute<float>(prim, "spectra:rendering:minimumEmissionTemperature");
                if (has_attribute(prim, "spectra:rendering:blackbodyEmission")) rendering.blackbody_emission = get_attribute<bool>(prim, "spectra:rendering:blackbodyEmission");
                resources.volumes.emplace_back(Volume{
                    .id        = {resources.volumes.size() + 1u},
                    .name      = dcc_name(prim),
                    .domain    = math::Bounds3::empty(),
                    .transform = dcc_transform(stage, prim, basis),
                    .data      = get_openvdb_volume(stage, usd_volume),
                    .rendering = std::move(rendering),
                    .visible   = pxr::UsdGeomImageable{prim}.ComputeVisibility() != pxr::UsdGeomTokens->invisible,
                });
            }
        }

        void read_dcc_geometry(const pxr::UsdStageRefPtr& stage, SceneResources& resources, const std::unordered_map<std::string, MaterialId>& materials, const math::Transform& basis, ReaderPaths& paths) {
            for (const pxr::UsdPrim& prim : pxr::UsdPrimRange::Stage(stage, pxr::UsdTraverseInstanceProxies())) {
                const pxr::UsdGeomImageable imageable{prim};
                if (imageable) {
                    const pxr::TfToken purpose = imageable.ComputePurpose();
                    if (purpose == pxr::UsdGeomTokens->guide || purpose == pxr::UsdGeomTokens->proxy) continue;
                }
                if (pxr::UsdGeomPointInstancer{prim}) throw std::runtime_error(std::format("Unsupported USD at {}: PointInstancer is not in the current Spectra feature set", prim.GetPath().GetString()));
                if (pxr::UsdVolVolume{prim}) continue;
                if (pxr::UsdGeomPoints{prim}) {
                    pxr::VtArray<pxr::GfVec3f> positions{};
                    pxr::VtArray<float> widths{};
                    const pxr::UsdGeomPoints points{prim};
                    points.GetPointsAttr().Get(&positions);
                    points.GetWidthsAttr().Get(&widths);
                    if (widths.size() != 1u && widths.size() != positions.size()) throw std::runtime_error(std::format("Unsupported USD at {}.widths: Points widths must be constant or vertex-varying", prim.GetPath().GetString()));
                    SphereSet spheres{.id = {resources.sphere_sets.size() + 1u}, .name = dcc_name(prim), .positions = spectra(positions), .radii = std::vector<float>(positions.size())};
                    for (std::size_t index = 0u; index != spheres.radii.size(); ++index) spheres.radii[index] = widths[widths.size() == 1u ? 0u : index] * 0.5f;
                    resources.sphere_sets.emplace_back(std::move(spheres));
                    LightId area_light{};
                    if (prim.HasAPI<pxr::UsdLuxMeshLightAPI>()) {
                        const DccLightValues values = dcc_light_values(pxr::UsdLuxLightAPI{prim});
                        area_light                  = {resources.lights.size() + 1u};
                        resources.lights.emplace_back(Light{.id = area_light, .name = std::format("{} Emitter", dcc_name(prim)), .data = DiffuseAreaLight{.radiance = values.spectrum, .scale = values.scale}});
                    }
                    paths.sphere_sets.emplace(prim.GetPath().GetString(), spheres.id.value);
                    const PrototypeId prototype_id{resources.prototypes.size() + 1u};
                    resources.prototypes.emplace_back(Prototype{.id = prototype_id, .name = std::format("{} Prototype", dcc_name(prim)), .primitives = {Primitive{.spheres = resources.sphere_sets.back().id, .material = dcc_material(prim, materials), .area_light = area_light}}});
                    const InstanceId instance_id{resources.instances.size() + 1u};
                    resources.instances.emplace_back(Instance{.id = instance_id, .name = dcc_name(prim), .prototype = prototype_id, .transform = dcc_transform(stage, prim, basis), .visible = imageable.ComputeVisibility() != pxr::UsdGeomTokens->invisible});
                    paths.prototypes.emplace(prim.GetPath().GetString(), prototype_id.value);
                    paths.instances.emplace(prim.GetPath().GetString(), instance_id.value);
                    continue;
                }
                Geometry geometry{};
                bool renderable{};
                if (pxr::UsdGeomMesh{prim}) {
                    geometry.data = dcc_mesh(prim);
                    renderable    = true;
                } else if (pxr::UsdGeomSphere{prim}) {
                    double radius{};
                    pxr::UsdGeomSphere{prim}.GetRadiusAttr().Get(&radius);
                    geometry.data = SphereGeometry{static_cast<float>(radius), static_cast<float>(-radius), static_cast<float>(radius), 360.0f};
                    renderable    = true;
                } else if (pxr::UsdGeomCube{prim}) {
                    double size{};
                    pxr::UsdGeomCube{prim}.GetSizeAttr().Get(&size);
                    const float extent = static_cast<float>(size * 0.5);
                    geometry.data      = BoxGeometry{{{-extent, -extent, -extent}, {extent, extent, extent}}};
                    renderable         = true;
                } else if (pxr::UsdGeomCylinder{prim}) {
                    pxr::TfToken axis{};
                    double radius{};
                    double height{};
                    pxr::UsdGeomCylinder cylinder{prim};
                    cylinder.GetAxisAttr().Get(&axis);
                    cylinder.GetRadiusAttr().Get(&radius);
                    cylinder.GetHeightAttr().Get(&height);
                    if (axis != pxr::UsdGeomTokens->z) throw std::runtime_error(std::format("Unsupported USD at {}.axis: Spectra cylinders currently require Z axis", prim.GetPath().GetString()));
                    geometry.data = CylinderGeometry{static_cast<float>(radius), static_cast<float>(height * -0.5), static_cast<float>(height * 0.5), 360.0f};
                    renderable    = true;
                }
                if (!renderable) {
                    if (pxr::UsdGeomGprim{prim}) throw std::runtime_error(std::format("Unsupported USD at {}: geometry schema '{}' is not in the current Spectra feature set", prim.GetPath().GetString(), prim.GetTypeName().GetString()));
                    continue;
                }
                geometry.id   = {resources.geometries.size() + 1u};
                geometry.name = dcc_name(prim);
                paths.geometries.emplace(prim.GetPath().GetString(), geometry.id.value);
                const std::vector<pxr::UsdGeomSubset> subsets = pxr::UsdGeomMesh{prim} ? pxr::UsdGeomSubset::GetGeomSubsets(pxr::UsdGeomImageable{prim}, pxr::UsdGeomTokens->face, pxr::TfToken{"materialBind"}) : std::vector<pxr::UsdGeomSubset>{};
                const pxr::UsdShadeMaterial overall_material  = pxr::UsdShadeMaterialBindingAPI{prim}.ComputeBoundMaterial();
                if (!overall_material && subsets.empty()) throw std::runtime_error(std::format("Unsupported USD at {}: every renderable prim requires a material binding", prim.GetPath().GetString()));
                const MaterialId material = overall_material ? dcc_material(prim, materials) : dcc_material(subsets.front().GetPrim(), materials);
                Primitive primitive{.geometry = geometry.id, .material = material, .reverse_orientation = get_attribute<pxr::TfToken>(prim, "orientation") == pxr::UsdGeomTokens->leftHanded};
                if (!subsets.empty()) {
                    primitive.face_materials.assign(std::get<TriangleMeshGeometry>(geometry.data).indices.size() / 3u, material);
                    for (const pxr::UsdGeomSubset& subset : subsets) {
                        pxr::VtArray<int> faces{};
                        subset.GetIndicesAttr().Get(&faces);
                        const MaterialId subset_material = dcc_material(subset.GetPrim(), materials);
                        for (const int face : faces) primitive.face_materials[static_cast<std::size_t>(face)] = subset_material;
                    }
                }
                resources.geometries.emplace_back(std::move(geometry));
                LightId area_light{};
                if (prim.HasAPI<pxr::UsdLuxMeshLightAPI>()) {
                    const DccLightValues values = dcc_light_values(pxr::UsdLuxLightAPI{prim});
                    area_light                  = {resources.lights.size() + 1u};
                    resources.lights.emplace_back(Light{.id = area_light, .name = std::format("{} Emitter", dcc_name(prim)), .data = DiffuseAreaLight{.radiance = values.spectrum, .scale = values.scale}});
                }
                primitive.area_light = area_light;
                const PrototypeId prototype_id{resources.prototypes.size() + 1u};
                resources.prototypes.emplace_back(Prototype{.id = prototype_id, .name = std::format("{} Prototype", dcc_name(prim)), .primitives = {std::move(primitive)}});
                const InstanceId instance_id{resources.instances.size() + 1u};
                resources.instances.emplace_back(Instance{.id = instance_id, .name = dcc_name(prim), .prototype = prototype_id, .transform = dcc_transform(stage, prim, basis), .visible = imageable.ComputeVisibility() != pxr::UsdGeomTokens->invisible});
                paths.prototypes.emplace(prim.GetPath().GetString(), prototype_id.value);
                paths.instances.emplace(prim.GetPath().GetString(), instance_id.value);
            }
        }

        void read_dcc_lights(const pxr::UsdStageRefPtr& stage, SceneResources& resources, const math::Transform& basis) {
            const math::Transform reverse_z{{-1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
            for (const pxr::UsdPrim& prim : stage->Traverse()) {
                if (prim.HasAPI<pxr::UsdLuxMeshLightAPI>()) continue;
                const pxr::UsdLuxLightAPI usd_light{prim};
                if (!usd_light) continue;
                if (pxr::UsdGeomImageable{prim}.ComputeVisibility() == pxr::UsdGeomTokens->invisible) continue;
                const DccLightValues values = dcc_light_values(usd_light);
                Light light{.id = {resources.lights.size() + 1u}, .name = dcc_name(prim)};
                if (pxr::UsdLuxSphereLight{prim}) {
                    bool treat_as_point{};
                    pxr::UsdLuxSphereLight{prim}.GetTreatAsPointAttr().Get(&treat_as_point);
                    if (!treat_as_point) throw std::runtime_error(std::format("Unsupported USD at {}.treatAsPoint: finite sphere lights are not in the current Spectra feature set", prim.GetPath().GetString()));
                    if (prim.HasAPI<pxr::UsdLuxShapingAPI>()) {
                        float angle{30.0f};
                        float softness{};
                        pxr::UsdLuxShapingAPI{prim}.GetShapingConeAngleAttr().Get(&angle);
                        pxr::UsdLuxShapingAPI{prim}.GetShapingConeSoftnessAttr().Get(&softness);
                        light.data = SpotLight{dcc_frame_transform(stage, prim, basis) * reverse_z, values.spectrum, values.scale, angle, angle * softness};
                    } else light.data = PointLight{dcc_frame_transform(stage, prim, basis), values.spectrum, values.scale};
                } else if (pxr::UsdLuxDistantLight{prim}) light.data = DistantLight{dcc_frame_transform(stage, prim, basis), values.spectrum, values.scale};
                else if (pxr::UsdLuxDiskLight{prim}) {
                    float radius{};
                    pxr::UsdLuxDiskLight{prim}.GetRadiusAttr().Get(&radius);
                    light.data = DiffuseAreaLight{.radiance = values.spectrum, .sidedness = EmissionSidedness::Both, .scale = values.scale};
                    const GeometryId geometry_id{resources.geometries.size() + 1u};
                    resources.geometries.emplace_back(Geometry{.id = geometry_id, .name = std::format("{} Geometry", dcc_name(prim)), .data = DiskGeometry{0.0f, radius, 0.0f, 360.0f}});
                    const PrototypeId prototype_id{resources.prototypes.size() + 1u};
                    resources.prototypes.emplace_back(Prototype{.id = prototype_id, .name = std::format("{} Prototype", dcc_name(prim)), .primitives = {Primitive{.geometry = geometry_id, .area_light = light.id}}});
                    resources.instances.emplace_back(Instance{.id = {resources.instances.size() + 1u}, .name = dcc_name(prim), .prototype = prototype_id, .transform = dcc_transform(stage, prim, basis)});
                } else if (pxr::UsdLuxDomeLight{prim}) {
                    pxr::SdfAssetPath texture{};
                    pxr::UsdLuxDomeLight{prim}.GetTextureFileAttr().Get(&texture);
                    TextureId emission_texture{};
                    if (!texture.GetAssetPath().empty()) {
                        emission_texture = {resources.textures.size() + 1u};
                        resources.textures.emplace_back(Texture{
                            .id            = emission_texture,
                            .name          = std::format("{} Texture", dcc_name(prim)),
                            .value_kind    = TextureValueKind::Spectrum,
                            .spectrum_type = TextureSpectrumType::Illuminant,
                            .color_space   = TextureColorSpace::Linear,
                            .data          = ImageTexture{.source = texture.GetAssetPath(), .mapping = TextureMapping{SphericalTextureMapping{}}},
                        });
                    }
                    light.data = InfiniteLight{values.spectrum, dcc_frame_transform(stage, prim, basis), values.scale, emission_texture};
                } else throw std::runtime_error(std::format("Unsupported USD at {}: light schema '{}' is not in the current Spectra feature set", prim.GetPath().GetString(), prim.GetTypeName().GetString()));
                resources.lights.emplace_back(std::move(light));
            }
        }

        void read_dcc_cameras(const pxr::UsdStageRefPtr& stage, SceneResources& resources, const math::Transform& basis, ReaderPaths& paths) {
            const float meters = static_cast<float>(pxr::UsdGeomGetStageMetersPerUnit(stage));
            for (const pxr::UsdPrim& prim : stage->Traverse()) {
                const pxr::UsdGeomCamera usd_camera{prim};
                if (!usd_camera) continue;
                pxr::TfToken projection{};
                float horizontal_aperture{};
                float vertical_aperture{};
                float focal_length{};
                float focus_distance{};
                float f_stop{};
                pxr::GfVec2f clipping{};
                usd_camera.GetProjectionAttr().Get(&projection);
                usd_camera.GetHorizontalApertureAttr().Get(&horizontal_aperture);
                usd_camera.GetVerticalApertureAttr().Get(&vertical_aperture);
                usd_camera.GetFocalLengthAttr().Get(&focal_length);
                usd_camera.GetFocusDistanceAttr().Get(&focus_distance);
                usd_camera.GetFStopAttr().Get(&f_stop);
                usd_camera.GetClippingRangeAttr().Get(&clipping);
                const float aspect = horizontal_aperture / vertical_aperture;
                Camera camera{
                    .id            = {resources.cameras.size() + 1u},
                    .name          = dcc_name(prim),
                    .transform     = dcc_frame_transform(stage, prim, basis),
                    .exposure_time = 1.0f,
                };
                if (projection == pxr::UsdGeomTokens->perspective) camera.data = PerspectiveCameraData{2.0f * std::atan(vertical_aperture / (2.0f * focal_length)) * 180.0f / std::numbers::pi_v<float>, {{-aspect, -1.0f}, {aspect, 1.0f}}, f_stop == 0.0f ? 0.0f : focal_length * meters / (20.0f * f_stop), focus_distance * meters, clipping[0] * meters, clipping[1] * meters};
                else if (projection == pxr::UsdGeomTokens->orthographic) camera.data = OrthographicCameraData{{{-horizontal_aperture * meters / 20.0f, -vertical_aperture * meters / 20.0f}, {horizontal_aperture * meters / 20.0f, vertical_aperture * meters / 20.0f}}, 0.0f, focus_distance * meters, clipping[0] * meters, clipping[1] * meters};
                else throw std::runtime_error(std::format("Unsupported USD at {}.projection: camera projection '{}' is not in the current Spectra feature set", prim.GetPath().GetString(), projection.GetString()));
                paths.cameras.emplace(prim.GetPath().GetString(), camera.id.value);
                resources.cameras.emplace_back(std::move(camera));
            }
        }

        [[nodiscard]] std::uint64_t physica_resource_id(const pxr::UsdPrim& prim, const ReaderPaths& paths) {
            const pxr::SdfPath path = get_relationship(prim, "physica:target");
            const std::array maps{&paths.geometries, &paths.sphere_sets, &paths.particle_sets, &paths.volumes, &paths.neural_fields};
            for (const auto* map : maps) {
                const auto resource = map->find(path.GetString());
                if (resource != map->end()) return resource->second;
            }
            throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:target: {} is not a supported simulation resource", prim.GetPath().GetString(), path.GetString()));
        }

        void read_physica_resources(const pxr::UsdStageRefPtr& stage, Scene& scene, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Physica/Resources"});
            if (!root) return;
            for (const pxr::UsdPrim& prim : root.GetChildren()) {
                const pxr::TfToken kind = get_attribute<pxr::TfToken>(prim, "physica:kind");
                if (kind != pxr::TfToken{"hashGridRadianceField"}) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:kind: resource kind '{}' is not in the current feature set", prim.GetPath().GetString(), kind.GetString()));
                NeuralField field{
                    .id        = {scene.resources.neural_fields.size() + 1u},
                    .name      = get_attribute<std::string>(prim, "physica:name"),
                    .transform = spectra(get_attribute<pxr::GfMatrix4d>(prim, "physica:transform")),
                    .visible   = get_attribute<bool>(prim, "physica:visible"),
                };
                paths.neural_fields.emplace(prim.GetPath().GetString(), field.id.value);
                scene.resources.neural_fields.emplace_back(std::move(field));
            }
        }

        [[nodiscard]] SimulationMeshInput read_physica_mesh_input(const pxr::UsdStageRefPtr& stage, const pxr::UsdPrim& prim, const Scene& scene, const ReaderPaths& paths) {
            const pxr::SdfPath source_path = get_relationship(prim, "physica:source");
            const auto geometry            = paths.geometries.find(source_path.GetString());
            if (geometry == paths.geometries.end()) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:source: {} is not a supported Mesh", prim.GetPath().GetString(), source_path.GetString()));
            const auto instance = paths.instances.find(source_path.GetString());
            if (instance == paths.instances.end()) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:source: {} has no renderable instance", prim.GetPath().GetString(), source_path.GetString()));
            const Instance& scene_instance = *std::ranges::find(scene.resources.instances, InstanceId{instance->second}, &Instance::id);
            SimulationMeshInput input{
                .id        = get_attribute<std::string>(prim, "physica:id"),
                .prim_path = source_path.GetString(),
                .geometry  = {geometry->second},
                .transform = scene_instance.transform,
            };
            if (!prim.HasRelationship(token("physica:selections"))) return input;
            for (const pxr::SdfPath& selection_path : get_relationships(prim, "physica:selections")) {
                const pxr::UsdGeomSubset subset{stage->GetPrimAtPath(selection_path)};
                if (!subset) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:selections: {} is not a UsdGeomSubset", prim.GetPath().GetString(), selection_path.GetString()));
                pxr::TfToken element_type{};
                subset.GetElementTypeAttr().Get(&element_type);
                if (element_type != pxr::UsdGeomTokens->point) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.elementType: mesh input selections require point subsets", selection_path.GetString()));
                pxr::VtArray<int> indices{};
                subset.GetIndicesAttr().Get(&indices);
                input.selections.emplace_back(SimulationIndexSelection{dcc_name(subset.GetPrim()), selection_path.GetString(), spectra_indices(indices)});
            }
            return input;
        }

        [[nodiscard]] SimulationVisualization read_physica_visualization(const pxr::UsdPrim& prim, const ReaderPaths& paths) {
            SimulationVisualization visualization{
                .id                 = get_attribute<std::string>(prim, "physica:id"),
                .output_id          = get_attribute<std::string>(prim, "physica:source"),
                .name               = get_attribute<std::string>(prim, "physica:name"),
                .depth_mode         = get_enum<VisualizationDepthMode>(prim, "physica:depthMode", depth_modes),
                .composition_domain = get_enum<VisualizationCompositionDomain>(prim, "physica:compositionDomain", composition_domains),
                .color              = spectra(get_attribute<pxr::GfVec4f>(prim, "physica:color")),
                .visible            = get_attribute<bool>(prim, "physica:visible"),
            };
            if (prim.HasRelationship(token("physica:anchor"))) {
                const pxr::SdfPath anchor = get_relationship(prim, "physica:anchor");
                visualization.anchor_path = anchor.GetString();
                const auto instance       = paths.instances.find(visualization.anchor_path);
                if (instance == paths.instances.end()) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:anchor: {} has no renderable instance", prim.GetPath().GetString(), visualization.anchor_path));
                visualization.anchor = {instance->second};
            }
            const pxr::TfToken type = get_attribute<pxr::TfToken>(prim, "physica:type");
            if (type == pxr::TfToken{"points"}) visualization.data = PointVisualization{get_attribute<float>(prim, "physica:size"), get_attribute<float>(prim, "physica:scalarMinimum"), get_attribute<float>(prim, "physica:scalarMaximum"), get_enum<VisualizationColorSource>(prim, "physica:colorSource", color_sources), get_enum<VisualizationColorMap>(prim, "physica:colorMap", color_maps)};
            else if (type == pxr::TfToken{"segments"}) visualization.data = SegmentVisualization{get_attribute<float>(prim, "physica:width"), get_attribute<float>(prim, "physica:scalarMinimum"), get_attribute<float>(prim, "physica:scalarMaximum"), get_enum<VisualizationColorSource>(prim, "physica:colorSource", color_sources), get_enum<VisualizationColorMap>(prim, "physica:colorMap", color_maps)};
            else if (type == pxr::TfToken{"vectors"}) visualization.data = VectorVisualization{get_attribute<float>(prim, "physica:width"), get_attribute<float>(prim, "physica:scale"), get_attribute<float>(prim, "physica:scalarMinimum"), get_attribute<float>(prim, "physica:scalarMaximum"), get_enum<VisualizationColorSource>(prim, "physica:colorSource", color_sources), get_enum<VisualizationColorMap>(prim, "physica:colorMap", color_maps)};
            else if (type == pxr::TfToken{"image"}) visualization.data = ImageVisualization{spectra(get_attribute<pxr::GfVec4f>(prim, "physica:screenRect"))};
            else if (type == pxr::TfToken{"surface"}) visualization.data = SurfaceVisualization{get_attribute<float>(prim, "physica:scalarMinimum"), get_attribute<float>(prim, "physica:scalarMaximum"), get_enum<VisualizationColorSource>(prim, "physica:colorSource", color_sources), get_enum<VisualizationColorMap>(prim, "physica:colorMap", color_maps)};
            else if (type == pxr::TfToken{"occupancyGrid"}) visualization.data = NeuralFieldVisualization{};
            else {
                const auto mode = std::ranges::find(derived_mesh_modes, type.GetString());
                if (mode == derived_mesh_modes.end()) throw std::runtime_error(std::format("Unsupported Physica sidecar at {}.physica:type: visualization type '{}' is not in the current feature set", prim.GetPath().GetString(), type.GetString()));
                visualization.data = DerivedMeshVisualization{static_cast<DerivedMeshVisualizationMode>(mode - derived_mesh_modes.begin()), get_attribute<float>(prim, "physica:width"), get_attribute<float>(prim, "physica:scale")};
            }
            return visualization;
        }

        void read_physica(const pxr::UsdStageRefPtr& stage, Scene& scene, ReaderPaths& paths) {
            const pxr::UsdPrim root = stage->GetPrimAtPath(pxr::SdfPath{"/Physica"});
            if (!root) return;
            const std::uint32_t version = get_attribute<std::uint32_t>(root, "physica:version");
            if (version != 2u) throw std::runtime_error(std::format("Unsupported Physica sidecar version {} at /Physica.physica:version; expected 2", version));
            read_physica_resources(stage, scene, paths);
            SimulationSetup setup{
                .clock =
                    {
                        .step_seconds = get_attribute<double>(root, "physica:clock:stepSeconds"),
                        .start_step   = get_attribute<std::uint64_t>(root, "physica:clock:startStep"),
                        .loop         = get_attribute<bool>(root, "physica:clock:loop"),
                    },
                .seed = get_attribute<std::uint64_t>(root, "physica:seed"),
            };
            if (has_attribute(root, "physica:clock:endStep")) setup.clock.end_step = get_attribute<std::uint64_t>(root, "physica:clock:endStep");
            const pxr::UsdPrim systems = stage->GetPrimAtPath(pxr::SdfPath{"/Physica/Systems"});
            if (!systems) throw std::runtime_error("Unsupported Physica sidecar: /Physica/Systems is required");
            for (const pxr::UsdPrim& prim : systems.GetChildren()) {
                SimulationSystem system{
                    .id          = {get_attribute<std::string>(prim, "physica:id")},
                    .name        = get_attribute<std::string>(prim, "physica:name"),
                    .provider_id = get_attribute<std::string>(prim, "physica:module"),
                    .enabled     = get_attribute<bool>(prim, "physica:enabled"),
                    .visible     = get_attribute<bool>(prim, "physica:visible"),
                };
                for (const pxr::UsdPrim& child : prim.GetChild(pxr::TfToken{"Parameters"}).GetChildren()) {
                    const pxr::GfVec3d floating = get_attribute<pxr::GfVec3d>(child, "physica:floating");
                    system.parameters.emplace_back(SimulationParameterSetting{
                        .parameter_id = get_attribute<std::string>(child, "physica:id"),
                        .value        = {get_enum<SimulationParameterKind>(child, "physica:valueKind", parameter_kinds), get_attribute<std::int64_t>(child, "physica:integer"), {floating[0], floating[1], floating[2]}},
                    });
                }
                for (const pxr::UsdPrim& child : prim.GetChild(pxr::TfToken{"Inputs"}).GetChildren()) system.mesh_inputs.emplace_back(read_physica_mesh_input(stage, child, scene, paths));
                for (const pxr::UsdPrim& child : prim.GetChild(pxr::TfToken{"Bindings"}).GetChildren()) {
                    const pxr::SdfPath target = get_relationship(child, "physica:target");
                    system.scene_bindings.emplace_back(SimulationOutputBinding{get_attribute<std::string>(child, "physica:id"), target.GetString(), physica_resource_id(child, paths)});
                }
                for (const pxr::UsdPrim& child : prim.GetChild(pxr::TfToken{"Views"}).GetChildren()) system.visualizations.emplace_back(read_physica_visualization(child, paths));
                setup.systems.emplace_back(std::move(system));
            }
            scene.simulation = std::move(setup);
        }

        [[nodiscard]] Scene read_dcc_stage(const pxr::UsdStageRefPtr& stage, const std::filesystem::path& path) {
            reject_time_samples(stage);
            Scene scene{path.stem().string()};
            std::unordered_map<std::string, MaterialId> materials{};
            ReaderPaths paths{};
            const math::Transform basis = dcc_basis(stage);
            read_dcc_materials(stage, scene.resources, materials);
            read_dcc_volumes(stage, scene.resources, basis);
            read_dcc_geometry(stage, scene.resources, materials, basis, paths);
            read_dcc_lights(stage, scene.resources, basis);
            read_dcc_cameras(stage, scene.resources, basis, paths);
            if (scene.resources.cameras.empty()) throw std::runtime_error("Unsupported USD Stage: a renderable DCC scene requires a Camera");
            pxr::UsdPrim settings{};
            for (const pxr::UsdPrim& prim : stage->Traverse())
                if (pxr::UsdRenderSettings{prim}) {
                    if (settings) throw std::runtime_error("Unsupported USD Stage: Spectra accepts one UsdRenderSettings prim");
                    settings = prim;
                }
            if (settings) {
                const pxr::SdfPath camera_path = get_relationship(settings, "camera");
                const auto camera              = paths.cameras.find(camera_path.GetString());
                if (camera == paths.cameras.end()) throw std::runtime_error(std::format("Unsupported USD at {}.camera: target {} is not a supported Camera", settings.GetPath().GetString(), camera_path.GetString()));
                scene.active_camera = {camera->second};
                if (has_attribute(settings, "spectra:transport:maximumDepth")) scene.transport.maximum_depth = get_attribute<std::uint32_t>(settings, "spectra:transport:maximumDepth");
            } else if (scene.resources.cameras.size() == 1u) scene.active_camera = scene.resources.cameras.front().id;
            else throw std::runtime_error("Unsupported USD Stage: multiple Cameras require /Render/Settings.camera");
            Film film{.id = {1u}, .name = "DCC Render Product"};
            if (settings) {
                pxr::SdfPathVector products{};
                pxr::UsdRenderSettings{settings}.GetProductsRel().GetTargets(&products);
                if (products.size() > 1u) throw std::runtime_error(std::format("Unsupported USD at {}.products: Spectra accepts one UsdRenderProduct", settings.GetPath().GetString()));
                if (!products.empty()) {
                    const pxr::UsdRenderProduct product{stage->GetPrimAtPath(products.front())};
                    if (!product) throw std::runtime_error(std::format("Unsupported USD at {}.products: target {} is not a UsdRenderProduct", settings.GetPath().GetString(), products.front().GetString()));
                    pxr::GfVec2i resolution{};
                    product.GetResolutionAttr().Get(&resolution);
                    film.name          = dcc_name(product.GetPrim());
                    film.resolution    = {static_cast<std::uint32_t>(resolution[0]), static_cast<std::uint32_t>(resolution[1])};
                    film.pixel_maximum = film.resolution;
                }
            }
            scene.resources.films.emplace_back(std::move(film));
            Sampler sampler{.id = {1u}, .name = "DCC Sampler", .samples_per_pixel = 64u};
            if (settings && has_attribute(settings, "spectra:sampler:samplesPerPixel")) sampler.samples_per_pixel = get_attribute<std::uint32_t>(settings, "spectra:sampler:samplesPerPixel");
            scene.resources.samplers.emplace_back(std::move(sampler));
            scene.active_film    = {1u};
            scene.active_sampler = {1u};
            read_physica(stage, scene, paths);
            return scene;
        }
    } // namespace

    Scene load_usd(const std::filesystem::path& path) {
        std::filesystem::path stage_path = path;
        stage_path.replace_extension(".physica.usda");
        if (!std::filesystem::exists(stage_path)) stage_path = path;
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(stage_path.string(), pxr::UsdStage::LoadAll);
        if (!stage) throw std::runtime_error(std::format("Failed to open USD Stage {}", stage_path.string()));
        const pxr::UsdPrim profile = stage->GetPrimAtPath(pxr::SdfPath{"/Spectra"});
        if (profile && profile.HasAttribute(pxr::TfToken{"spectra:profileVersion"})) return read_spectra_profile(stage);
        return read_dcc_stage(stage, path);
    }

    void save_usd(const Scene& scene, const std::filesystem::path& path) {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(path.string());
        if (!stage) throw std::runtime_error(std::format("Failed to create USD Stage {}", path.string()));
        pxr::UsdGeomSetStageMetersPerUnit(stage, pxr::UsdGeomLinearUnits::meters);
        pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomTokens->y);
        const pxr::UsdGeomXform world = pxr::UsdGeomXform::Define(stage, pxr::SdfPath{"/World"});
        stage->SetDefaultPrim(world.GetPrim());
        const pxr::UsdGeomScope spectra = pxr::UsdGeomScope::Define(stage, pxr::SdfPath{"/Spectra"});
        set_attribute(spectra.GetPrim(), "spectra:profileVersion", pxr::SdfValueTypeNames->UInt, profile_version);
        set_attribute(spectra.GetPrim(), "spectra:sceneName", pxr::SdfValueTypeNames->String, scene.name);

        Paths paths{};
        write_geometries(stage, scene.resources, paths);
        write_sphere_sets(stage, scene.resources, paths);
        write_textures(stage, scene.resources, paths);
        write_materials(stage, scene.resources, paths);
        write_media(stage, scene.resources, paths);
        write_lights(stage, scene.resources, paths);
        write_volumes(stage, scene.resources, paths);
        write_particles(stage, scene.resources, paths);
        write_neural_fields(stage, scene.resources, paths);
        write_prototypes(stage, scene.resources, paths);
        write_instances(stage, scene.resources, paths);
        write_cameras(stage, scene.resources, paths);
        write_render_settings(stage, scene, paths);
        write_simulation(stage, scene, paths);
        stage->GetRootLayer()->Save();
    }

    void save_physica_usd(const Scene& scene, const std::filesystem::path& path, const std::filesystem::path& base_scene_path) {
        const pxr::UsdStageRefPtr stage = pxr::UsdStage::CreateNew(path.string());
        if (!stage) throw std::runtime_error(std::format("Failed to create Physica USD sidecar {}", path.string()));
        const pxr::UsdStageRefPtr base_stage = pxr::UsdStage::Open(base_scene_path.string(), pxr::UsdStage::LoadNone);
        if (!base_stage) throw std::runtime_error(std::format("Failed to open Physica base USD Stage {}", base_scene_path.string()));
        stage->GetRootLayer()->SetSubLayerPaths({std::format("./{}", base_scene_path.filename().generic_string())});
        pxr::UsdGeomSetStageMetersPerUnit(stage, pxr::UsdGeomGetStageMetersPerUnit(base_stage));
        pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomGetStageUpAxis(base_stage));

        const SimulationSetup& setup = *scene.simulation;
        const pxr::UsdPrim root      = stage->DefinePrim(pxr::SdfPath{"/Physica"}, pxr::TfToken{"Scope"});
        set_attribute(root, "physica:version", pxr::SdfValueTypeNames->UInt, 2u);
        set_attribute(root, "physica:clock:stepSeconds", pxr::SdfValueTypeNames->Double, setup.clock.step_seconds);
        set_attribute(root, "physica:clock:startStep", pxr::SdfValueTypeNames->UInt64, setup.clock.start_step);
        if (setup.clock.end_step) set_attribute(root, "physica:clock:endStep", pxr::SdfValueTypeNames->UInt64, *setup.clock.end_step);
        set_attribute(root, "physica:clock:loop", pxr::SdfValueTypeNames->Bool, setup.clock.loop);
        set_attribute(root, "physica:seed", pxr::SdfValueTypeNames->UInt64, setup.seed);

        stage->DefinePrim(root.GetPath().AppendChild(pxr::TfToken{"Resources"}), pxr::TfToken{"Scope"});
        for (const NeuralField& field : scene.resources.neural_fields) {
            std::string target_path{};
            for (const SimulationSystem& system : setup.systems) {
                const auto binding = std::ranges::find(system.scene_bindings, field.id.value, &SimulationOutputBinding::resource_id);
                if (binding != system.scene_bindings.end()) {
                    target_path = binding->target_path;
                    break;
                }
            }
            const pxr::UsdPrim prim = stage->DefinePrim(pxr::SdfPath{target_path}, pxr::TfToken{"Scope"});
            set_attribute(prim, "physica:kind", pxr::SdfValueTypeNames->Token, pxr::TfToken{"hashGridRadianceField"});
            set_attribute(prim, "physica:name", pxr::SdfValueTypeNames->String, field.name);
            set_attribute(prim, "physica:transform", pxr::SdfValueTypeNames->Matrix4d, usd(field.transform));
            set_attribute(prim, "physica:visible", pxr::SdfValueTypeNames->Bool, field.visible);
        }

        const pxr::UsdPrim systems = stage->DefinePrim(root.GetPath().AppendChild(pxr::TfToken{"Systems"}), pxr::TfToken{"Scope"});
        std::unordered_set<std::string> system_identifiers{};
        for (const SimulationSystem& system : setup.systems) {
            const pxr::UsdPrim prim = stage->DefinePrim(child_path(systems.GetPath(), system.id.value, "System", system_identifiers), pxr::TfToken{"Scope"});
            set_attribute(prim, "physica:id", pxr::SdfValueTypeNames->String, system.id.value);
            set_attribute(prim, "physica:name", pxr::SdfValueTypeNames->String, system.name);
            set_attribute(prim, "physica:module", pxr::SdfValueTypeNames->String, system.provider_id);
            set_attribute(prim, "physica:enabled", pxr::SdfValueTypeNames->Bool, system.enabled);
            set_attribute(prim, "physica:visible", pxr::SdfValueTypeNames->Bool, system.visible);

            const pxr::UsdPrim inputs = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Inputs"}), pxr::TfToken{"Scope"});
            std::unordered_set<std::string> input_identifiers{};
            for (const SimulationMeshInput& input : system.mesh_inputs) {
                const pxr::UsdPrim input_prim = stage->DefinePrim(child_path(inputs.GetPath(), input.id, "Mesh", input_identifiers), pxr::TfToken{"Scope"});
                set_attribute(input_prim, "physica:id", pxr::SdfValueTypeNames->String, input.id);
                set_relationship(input_prim, "physica:source", pxr::SdfPath{input.prim_path});
                pxr::SdfPathVector selections{};
                for (const SimulationIndexSelection& selection : input.selections) {
                    const pxr::UsdGeomSubset subset = pxr::UsdGeomSubset::Define(stage, pxr::SdfPath{selection.prim_path});
                    subset.GetPrim().SetDisplayName(selection.id);
                    subset.CreateElementTypeAttr().Set(pxr::UsdGeomTokens->point);
                    subset.CreateIndicesAttr().Set(usd_indices(selection.indices));
                    selections.emplace_back(selection.prim_path);
                }
                if (!selections.empty()) input_prim.CreateRelationship(pxr::TfToken{"physica:selections"}, true).SetTargets(selections);
            }

            const pxr::UsdPrim parameters = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Parameters"}), pxr::TfToken{"Scope"});
            std::unordered_set<std::string> parameter_identifiers{};
            for (const SimulationParameterSetting& parameter : system.parameters) {
                const pxr::UsdPrim parameter_prim = stage->DefinePrim(child_path(parameters.GetPath(), parameter.parameter_id, "Parameter", parameter_identifiers), pxr::TfToken{"Scope"});
                set_attribute(parameter_prim, "physica:id", pxr::SdfValueTypeNames->String, parameter.parameter_id);
                set_attribute(parameter_prim, "physica:valueKind", pxr::SdfValueTypeNames->Token, enum_token(parameter.value.kind, parameter_kinds));
                set_attribute(parameter_prim, "physica:integer", pxr::SdfValueTypeNames->Int64, parameter.value.integer);
                set_attribute(parameter_prim, "physica:floating", pxr::SdfValueTypeNames->Double3, pxr::GfVec3d{parameter.value.floating[0], parameter.value.floating[1], parameter.value.floating[2]});
            }

            const pxr::UsdPrim bindings = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Bindings"}), pxr::TfToken{"Scope"});
            std::unordered_set<std::string> binding_identifiers{};
            for (const SimulationOutputBinding& binding : system.scene_bindings) {
                const pxr::UsdPrim binding_prim = stage->DefinePrim(child_path(bindings.GetPath(), binding.output_id, "Binding", binding_identifiers), pxr::TfToken{"Scope"});
                set_attribute(binding_prim, "physica:id", pxr::SdfValueTypeNames->String, binding.output_id);
                set_relationship(binding_prim, "physica:target", pxr::SdfPath{binding.target_path});
            }

            const pxr::UsdPrim views = stage->DefinePrim(prim.GetPath().AppendChild(pxr::TfToken{"Views"}), pxr::TfToken{"Scope"});
            std::unordered_set<std::string> view_identifiers{};
            for (const SimulationVisualization& visualization : system.visualizations) {
                const pxr::UsdPrim view = stage->DefinePrim(child_path(views.GetPath(), visualization.id, "View", view_identifiers), pxr::TfToken{"Scope"});
                set_attribute(view, "physica:id", pxr::SdfValueTypeNames->String, visualization.id);
                set_attribute(view, "physica:source", pxr::SdfValueTypeNames->String, visualization.output_id);
                set_attribute(view, "physica:name", pxr::SdfValueTypeNames->String, visualization.name);
                set_attribute(view, "physica:depthMode", pxr::SdfValueTypeNames->Token, enum_token(visualization.depth_mode, depth_modes));
                set_attribute(view, "physica:compositionDomain", pxr::SdfValueTypeNames->Token, enum_token(visualization.composition_domain, composition_domains));
                if (!visualization.anchor_path.empty()) set_relationship(view, "physica:anchor", pxr::SdfPath{visualization.anchor_path});
                set_attribute(view, "physica:color", pxr::SdfValueTypeNames->Color4f, usd(visualization.color));
                set_attribute(view, "physica:visible", pxr::SdfValueTypeNames->Bool, visualization.visible);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"points"});
                            set_attribute(view, "physica:size", pxr::SdfValueTypeNames->Float, data.size);
                            set_attribute(view, "physica:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                            set_attribute(view, "physica:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                            set_attribute(view, "physica:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                            set_attribute(view, "physica:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"segments"});
                            set_attribute(view, "physica:width", pxr::SdfValueTypeNames->Float, data.width);
                            set_attribute(view, "physica:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                            set_attribute(view, "physica:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                            set_attribute(view, "physica:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                            set_attribute(view, "physica:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"vectors"});
                            set_attribute(view, "physica:width", pxr::SdfValueTypeNames->Float, data.width);
                            set_attribute(view, "physica:scale", pxr::SdfValueTypeNames->Float, data.scale);
                            set_attribute(view, "physica:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                            set_attribute(view, "physica:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                            set_attribute(view, "physica:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                            set_attribute(view, "physica:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"image"});
                            set_attribute(view, "physica:screenRect", pxr::SdfValueTypeNames->Float4, usd(data.screen_rect));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SurfaceVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"surface"});
                            set_attribute(view, "physica:scalarMinimum", pxr::SdfValueTypeNames->Float, data.scalar_minimum);
                            set_attribute(view, "physica:scalarMaximum", pxr::SdfValueTypeNames->Float, data.scalar_maximum);
                            set_attribute(view, "physica:colorSource", pxr::SdfValueTypeNames->Token, enum_token(data.color_source, color_sources));
                            set_attribute(view, "physica:colorMap", pxr::SdfValueTypeNames->Token, enum_token(data.color_map, color_maps));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DerivedMeshVisualization>) {
                            set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, enum_token(data.mode, derived_mesh_modes));
                            set_attribute(view, "physica:width", pxr::SdfValueTypeNames->Float, data.width);
                            set_attribute(view, "physica:scale", pxr::SdfValueTypeNames->Float, data.scale);
                        } else set_attribute(view, "physica:type", pxr::SdfValueTypeNames->Token, pxr::TfToken{"occupancyGrid"});
                    },
                    visualization.data);
            }
        }
        stage->GetRootLayer()->Save();
    }
} // namespace spectra::scene
