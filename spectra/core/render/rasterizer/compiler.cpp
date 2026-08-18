module;

#include "../shaders/shader_semantics.h"

module spectra.render.rasterizer.compiler;

import spectra.render.shader_abi;
import std;
import vulkan;

namespace spectra::render {
    static_assert(std::to_underlying(scene::SpectrumColorSpace::Srgb) == shader_semantics::color_space_srgb);
    static_assert(std::to_underlying(scene::SpectrumColorSpace::Rec2020) == shader_semantics::color_space_rec2020);
    static_assert(std::to_underlying(scene::SpectrumColorSpace::Aces2065_1) == shader_semantics::color_space_aces2065_1);

    [[nodiscard]] math::Float3 raster_linear_srgb(const math::Float3 value, const scene::SpectrumColorSpace color_space) noexcept {
        if (color_space == scene::SpectrumColorSpace::Rec2020) return {1.660491f * value.x - 0.587641f * value.y - 0.072850f * value.z, -0.124550f * value.x + 1.132900f * value.y - 0.008349f * value.z, -0.018151f * value.x - 0.100579f * value.y + 1.118730f * value.z};
        if (color_space == scene::SpectrumColorSpace::Aces2065_1) return {2.521686f * value.x - 1.134130f * value.y - 0.387556f * value.z, -0.276479f * value.x + 1.372719f * value.y - 0.096240f * value.z, -0.015378f * value.x - 0.152975f * value.y + 1.168353f * value.z};
        return value;
    }

    [[nodiscard]] math::Float3 raster_output_rgb(const math::Float3 value, const scene::SpectrumColorSpace color_space) noexcept {
        if (color_space == scene::SpectrumColorSpace::Rec2020) return {0.627404f * value.x + 0.329283f * value.y + 0.043313f * value.z, 0.069097f * value.x + 0.919540f * value.y + 0.011362f * value.z, 0.016391f * value.x + 0.088013f * value.y + 0.895595f * value.z};
        if (color_space == scene::SpectrumColorSpace::Aces2065_1) return {0.439701f * value.x + 0.382978f * value.y + 0.177335f * value.z, 0.089792f * value.x + 0.813423f * value.y + 0.096762f * value.z, 0.017544f * value.x + 0.111544f * value.y + 0.870704f * value.z};
        return value;
    }

    [[nodiscard]] math::Float3 raster_spectrum_rgb(const scene::SpectrumParameter& parameter) {
        if (parameter.encoding == scene::SpectrumEncoding::RgbAlbedo || parameter.encoding == scene::SpectrumEncoding::RgbUnbounded || parameter.encoding == scene::SpectrumEncoding::RgbIlluminant) {
            return raster_linear_srgb(parameter.value, parameter.color_space);
        }
        if (parameter.encoding == scene::SpectrumEncoding::Constant) return {parameter.scalar, parameter.scalar, parameter.scalar};
        if (parameter.encoding == scene::SpectrumEncoding::Blackbody) {
            const scene::BlackbodySpectrum spectrum{parameter.temperature};
            return {spectrum.evaluate(610.0f), spectrum.evaluate(550.0f), spectrum.evaluate(460.0f)};
        }
        const scene::PiecewiseLinearSpectrum spectrum{parameter.wavelengths, parameter.samples};
        return {spectrum.evaluate(610.0f), spectrum.evaluate(550.0f), spectrum.evaluate(460.0f)};
    }

    [[nodiscard]] std::uint32_t raster_texture_source_index(const scene::ResolvedSceneView scene, const scene::TextureId id) {
        return static_cast<std::uint32_t>(std::ranges::find(scene.resources.textures, id, &scene::Texture::id) - scene.resources.textures.begin());
    }

    [[nodiscard]] std::uint32_t compile_raster_mapping(std::vector<RasterTextureMapping>& mappings, const scene::TextureMapping& mapping) {
        RasterTextureMapping result{};
        result.transform_row_0 = {1.0f, 0.0f, 0.0f, 0.0f};
        result.transform_row_1 = {0.0f, 1.0f, 0.0f, 0.0f};
        result.transform_row_2 = {0.0f, 0.0f, 1.0f, 0.0f};
        result.transform_row_3 = {0.0f, 0.0f, 0.0f, 1.0f};
        std::visit(
            [&](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::UvTextureMapping>) result.parameter_0 = {data.scale.x, data.scale.y, data.offset.x, data.offset.y};
                else {
                    result.metadata[0]                     = std::same_as<std::remove_cvref_t<decltype(data)>, scene::PlanarTextureMapping> ? shader_semantics::mapping_planar : std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphericalTextureMapping> ? shader_semantics::mapping_spherical : shader_semantics::mapping_cylindrical;
                    const std::array<float, 16>& transform = data.texture_from_render.matrix;
                    result.transform_row_0                 = {transform[0], transform[1], transform[2], transform[3]};
                    result.transform_row_1                 = {transform[4], transform[5], transform[6], transform[7]};
                    result.transform_row_2                 = {transform[8], transform[9], transform[10], transform[11]};
                    result.transform_row_3                 = {transform[12], transform[13], transform[14], transform[15]};
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PlanarTextureMapping>) {
                        result.parameter_0 = {data.first_axis.x, data.first_axis.y, data.first_axis.z, data.offset.x};
                        result.parameter_1 = {data.second_axis.x, data.second_axis.y, data.second_axis.z, data.offset.y};
                    }
                }
            },
            mapping.data);
        const std::uint32_t index = static_cast<std::uint32_t>(mappings.size());
        mappings.push_back(result);
        return index;
    }

    [[nodiscard]] std::uint32_t compile_raster_checkerboard_mapping(std::vector<RasterTextureMapping>& mappings, const scene::CheckerboardMapping& mapping) {
        if (const scene::TextureMapping* two_dimensional = std::get_if<scene::TextureMapping>(&mapping.data)) return compile_raster_mapping(mappings, *two_dimensional);
        const std::array<float, 16>& transform = std::get<scene::TextureMapping3D>(mapping.data).texture_from_render.matrix;
        RasterTextureMapping result{};
        result.metadata[0]        = shader_semantics::mapping_checkerboard_3d;
        result.transform_row_0    = {transform[0], transform[1], transform[2], transform[3]};
        result.transform_row_1    = {transform[4], transform[5], transform[6], transform[7]};
        result.transform_row_2    = {transform[8], transform[9], transform[10], transform[11]};
        result.transform_row_3    = {transform[12], transform[13], transform[14], transform[15]};
        const std::uint32_t index = static_cast<std::uint32_t>(mappings.size());
        mappings.push_back(result);
        return index;
    }

    [[nodiscard]] RasterTextureCompilation compile_raster_textures(GpuScene& gpu_scene, const scene::ResolvedSceneView scene) {
        RasterTextureCompilation result{};
        const std::size_t texture_count = scene.resources.textures.size();
        result.handles.assign(texture_count, invalid_raster_index);
        std::vector<std::uint32_t> order{};
        std::vector<std::uint8_t> marks(texture_count);
        std::function<void(std::uint32_t)> visit;
        visit = [&](const std::uint32_t index) {
            if (marks[index] == 2) return;
            if (marks[index] == 1) throw std::runtime_error(std::format("Raster Texture dependency cycle contains {}", scene.resources.textures[index].name));
            marks[index] = 1;
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        visit(raster_texture_source_index(scene, data.first));
                        visit(raster_texture_source_index(scene, data.second));
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        visit(raster_texture_source_index(scene, data.first));
                        visit(raster_texture_source_index(scene, data.second));
                        visit(raster_texture_source_index(scene, data.amount));
                    }
                },
                scene.resources.textures[index].data);
            marks[index]          = 2;
            result.handles[index] = static_cast<std::uint32_t>(order.size());
            order.push_back(index);
        };
        for (std::uint32_t index = 0; index != texture_count; ++index) visit(index);
        const auto handle = [&result, scene](const scene::TextureId id) {
            if (id.value == 0) return invalid_raster_index;
            return result.handles[raster_texture_source_index(scene, id)];
        };
        result.headers.reserve(texture_count);
        for (const std::uint32_t source_index : order) {
            const scene::Texture& texture = scene.resources.textures[source_index];
            std::uint32_t kind{shader_semantics::texture_constant};
            std::uint32_t local_index{};
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConstantTexture>) {
                        local_index              = static_cast<std::uint32_t>(result.constants.size());
                        const math::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? math::Float3{data.scalar, data.scalar, data.scalar} : raster_spectrum_rgb(data.spectrum);
                        result.constants.push_back(RasterConstantTexture{{value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalar : 1.0f}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                        kind                         = shader_semantics::texture_image;
                        local_index                  = static_cast<std::uint32_t>(result.images.size());
                        const GpuTextureImage& image = gpu_scene.texture_image(texture, texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat);
                        result.images.push_back(RasterImageTexture{image.image_descriptor, image.sampler_descriptor, {compile_raster_mapping(result.mappings, data.mapping), static_cast<std::uint32_t>(data.channel), static_cast<std::uint32_t>(data.filter), data.invert ? 1u : 0u}, {data.scale, static_cast<float>(std::to_underlying(texture.color_space)), data.maximum_anisotropy, static_cast<float>(std::to_underlying(data.wrap))}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture>) {
                        kind        = shader_semantics::texture_checkerboard;
                        local_index = static_cast<std::uint32_t>(result.checkerboards.size());
                        result.checkerboards.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), compile_raster_checkerboard_mapping(result.mappings, data.mapping), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture>) {
                        kind        = shader_semantics::texture_scale;
                        local_index = static_cast<std::uint32_t>(result.scales.size());
                        result.scales.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), 0, 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        kind        = shader_semantics::texture_mix;
                        local_index = static_cast<std::uint32_t>(result.mixes.size());
                        result.mixes.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), handle(data.amount), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        kind        = shader_semantics::texture_direction_mix;
                        local_index = static_cast<std::uint32_t>(result.direction_mixes.size());
                        result.direction_mixes.push_back(RasterDirectionMixTexture{{handle(data.first), handle(data.second), 0, 0}, {data.direction.x, data.direction.y, data.direction.z, 0.0f}});
                    } else {
                        kind        = shader_semantics::texture_bilerp;
                        local_index = static_cast<std::uint32_t>(result.bilerps.size());
                        RasterBilerpTexture compiled{};
                        compiled.data[0] = compile_raster_mapping(result.mappings, data.mapping);
                        for (std::uint32_t corner = 0; corner != 4; ++corner) {
                            const math::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? math::Float3{data.scalars[corner], data.scalars[corner], data.scalars[corner]} : raster_spectrum_rgb(data.spectra[corner]);
                            compiled.values[corner]  = {value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalars[corner] : 1.0f};
                        }
                        result.bilerps.push_back(compiled);
                    }
                },
                texture.data);
            result.headers.push_back(RasterTextureHeader{{kind, local_index, static_cast<std::uint32_t>(texture.value_kind), static_cast<std::uint32_t>(texture.spectrum_type)}, {}, {}});
        }
        const std::uint32_t root_count = static_cast<std::uint32_t>(result.headers.size());
        std::vector<RasterTextureHeader> program{};
        std::uint32_t approximation_index{invalid_raster_index};
        for (std::uint32_t texture = 0; texture != root_count; ++texture) {
            const std::size_t program_begin = program.size();
            std::vector<std::uint32_t> registers(texture_count, invalid_raster_index);
            std::function<std::uint32_t(std::uint32_t)> emit;
            emit = [&](const std::uint32_t source_index) {
                if (registers[source_index] != invalid_raster_index) return registers[source_index];
                RasterTextureHeader instruction = result.headers[result.handles[source_index]];
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                            instruction.program[0] = emit(raster_texture_source_index(scene, data.first));
                            instruction.program[1] = emit(raster_texture_source_index(scene, data.second));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                            instruction.program[0]  = emit(raster_texture_source_index(scene, data.first));
                            instruction.program[1]  = emit(raster_texture_source_index(scene, data.second));
                            instruction.reserved[0] = emit(raster_texture_source_index(scene, data.amount));
                        }
                    },
                    scene.resources.textures[source_index].data);
                const std::uint32_t register_index = static_cast<std::uint32_t>(program.size() - program_begin);
                registers[source_index]            = register_index;
                program.push_back(instruction);
                return register_index;
            };
            RasterTextureHeader& root = result.headers[texture];
            root.program[0]           = root_count + static_cast<std::uint32_t>(program_begin);
            emit(order[texture]);
            root.program[1] = root_count + static_cast<std::uint32_t>(program.size()) - root.program[0];
            if (root.program[1] > 32) {
                program.resize(program_begin);
                if (approximation_index == invalid_raster_index) {
                    approximation_index = static_cast<std::uint32_t>(result.constants.size());
                    result.constants.push_back(RasterConstantTexture{{0.5f, 0.5f, 0.5f, 0.5f}});
                }
                RasterTextureHeader approximation{};
                approximation.metadata = {shader_semantics::texture_constant, approximation_index, static_cast<std::uint32_t>(scene.resources.textures[order[texture]].value_kind), static_cast<std::uint32_t>(scene.resources.textures[order[texture]].spectrum_type)};
                program.push_back(approximation);
                root.program[1] = 1;
            }
        }
        result.headers.insert(result.headers.end(), program.begin(), program.end());
        if (result.headers.empty()) result.headers.emplace_back();
        if (result.mappings.empty()) result.mappings.emplace_back();
        if (result.constants.empty()) result.constants.emplace_back();
        if (result.images.empty()) result.images.emplace_back();
        if (result.checkerboards.empty()) result.checkerboards.emplace_back();
        if (result.scales.empty()) result.scales.emplace_back();
        if (result.mixes.empty()) result.mixes.emplace_back();
        if (result.direction_mixes.empty()) result.direction_mixes.emplace_back();
        if (result.bilerps.empty()) result.bilerps.emplace_back();
        return result;
    }

    [[nodiscard]] RasterMaterialCompilation compile_raster_materials(const scene::ResolvedSceneView scene, const RasterTextureCompilation& textures) {
        const auto texture_handle = [&textures, scene](const scene::TextureId id) {
            if (id.value == 0) return invalid_raster_index;
            return textures.handles[raster_texture_source_index(scene, id)];
        };
        const auto spectrum = [&texture_handle](std::array<float, 4>& value, std::array<std::uint32_t, 4>& data, const scene::SpectrumParameter& parameter) {
            const math::Float3 rgb = raster_spectrum_rgb(parameter);
            value                  = {rgb.x, rgb.y, rgb.z, 0.0f};
            data[0]                = texture_handle(parameter.texture);
        };
        RasterMaterialCompilation result{};
        result.materials.reserve(scene.resources.materials.size());
        std::vector<std::uint32_t> leaf_handles(scene.resources.materials.size(), invalid_raster_index);
        for (std::uint32_t source_index = 0; source_index != scene.resources.materials.size(); ++source_index) {
            const scene::Material& material = scene.resources.materials[source_index];
            if (std::holds_alternative<scene::MixMaterialData>(material.data)) continue;
            leaf_handles[source_index] = static_cast<std::uint32_t>(result.materials.size());
            RasterMaterial compiled{};
            compiled.metadata[1]        = invalid_raster_index;
            compiled.metadata[2]        = invalid_raster_index;
            compiled.spectrum_data_0[0] = invalid_raster_index;
            compiled.spectrum_data_1[0] = invalid_raster_index;
            compiled.scalar_textures_0.fill(invalid_raster_index);
            std::visit(
                [&](const auto& data) {
                    if constexpr (requires {
                                      data.normal_map;
                                      data.bump_map;
                                  }) {
                        if (data.normal_map.value != 0) {
                            const scene::Texture& texture = *std::ranges::find(scene.resources.textures, data.normal_map, &scene::Texture::id);
                            if (std::holds_alternative<scene::ImageTexture>(texture.data)) compiled.metadata[1] = texture_handle(data.normal_map);
                        }
                        compiled.metadata[2] = texture_handle(data.bump_map);
                    }
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>) compiled.metadata[0] = shader_semantics::raster_material_interface;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_diffuse;
                        spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_diffuse_transmission;
                        spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.transmittance);
                        compiled.scalar_values_0[2] = data.scale;
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_conductor;
                        compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.eta);
                                    spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.k);
                                } else {
                                    compiled.metadata[3] |= 2u;
                                    spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.reflectance);
                                }
                            },
                            data.optics);
                        const scene::FloatParameter& u = data.distribution.u_roughness.value_or(data.distribution.roughness);
                        const scene::FloatParameter& v = data.distribution.v_roughness.value_or(data.distribution.roughness);
                        compiled.scalar_values_0       = {u.value, v.value, 0.0f, 0.0f};
                        compiled.scalar_textures_0     = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DielectricMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_dielectric;
                        compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.eta);
                        const scene::FloatParameter& u = data.distribution.u_roughness.value_or(data.distribution.roughness);
                        const scene::FloatParameter& v = data.distribution.v_roughness.value_or(data.distribution.roughness);
                        compiled.scalar_values_0       = {u.value, v.value, 0.0f, 0.0f};
                        compiled.scalar_textures_0     = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_dielectric;
                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.eta);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_coated_diffuse;
                        compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                        spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.eta);
                        const scene::FloatParameter& u = data.interface.u_roughness.value_or(data.interface.roughness);
                        const scene::FloatParameter& v = data.interface.v_roughness.value_or(data.interface.roughness);
                        compiled.scalar_values_0       = {u.value, v.value, 0.0f, 0.0f};
                        compiled.scalar_textures_0     = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                        compiled.metadata[0] = shader_semantics::raster_material_coated_conductor;
                        compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.eta);
                                    spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.k);
                                } else {
                                    compiled.metadata[3] |= 2u;
                                    spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.reflectance);
                                }
                            },
                            data.optics);
                        const scene::FloatParameter& conductor_u = data.conductor.u_roughness.value_or(data.conductor.roughness);
                        const scene::FloatParameter& conductor_v = data.conductor.v_roughness.value_or(data.conductor.roughness);
                        compiled.scalar_values_0                 = {conductor_u.value, conductor_v.value, 0.0f, 0.0f};
                        compiled.scalar_textures_0               = {texture_handle(conductor_u.texture), texture_handle(conductor_v.texture), invalid_raster_index, invalid_raster_index};
                    }
                },
                material.data);
            result.materials.push_back(compiled);
        }
        result.ranges.resize(scene.resources.materials.size());
        const auto material_index = [scene](const scene::MaterialId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, id, &scene::Material::id) - scene.resources.materials.begin()); };
        for (std::uint32_t root_index = 0; root_index != scene.resources.materials.size(); ++root_index) {
            const std::uint32_t first_term = static_cast<std::uint32_t>(result.terms.size());
            std::vector<RasterMaterialFactor> path{};
            std::vector<bool> active(scene.resources.materials.size());
            std::function<void(std::uint32_t)> flatten;
            flatten = [&](const std::uint32_t source_index) {
                if (active[source_index]) throw std::runtime_error(std::format("Raster Material dependency cycle contains '{}'", scene.resources.materials[source_index].name));
                active[source_index] = true;
                if (const scene::MixMaterialData* mix = std::get_if<scene::MixMaterialData>(&scene.resources.materials[source_index].data)) {
                    const std::uint32_t texture = texture_handle(mix->amount.texture);
                    path.push_back(RasterMaterialFactor{{mix->amount.value, 0.0f, 0.0f, 0.0f}, {texture, 1, 0, 0}});
                    flatten(material_index(mix->first));
                    path.back().metadata[1] = 0;
                    flatten(material_index(mix->second));
                    path.pop_back();
                } else {
                    if (result.materials[leaf_handles[source_index]].metadata[0] == shader_semantics::raster_material_interface) {
                        active[source_index] = false;
                        return;
                    }
                    const std::uint32_t factor_offset = static_cast<std::uint32_t>(result.factors.size());
                    result.factors.insert(result.factors.end(), path.begin(), path.end());
                    result.terms.push_back(RasterMaterialTerm{{leaf_handles[source_index], factor_offset, static_cast<std::uint32_t>(path.size()), 0}});
                }
                active[source_index] = false;
            };
            flatten(root_index);
            result.ranges[root_index] = RasterMaterialRange{{first_term, static_cast<std::uint32_t>(result.terms.size()) - first_term}, {}};
        }
        if (result.materials.empty()) result.materials.emplace_back();
        if (result.ranges.empty()) result.ranges.emplace_back();
        if (result.terms.empty()) result.terms.emplace_back();
        if (result.factors.empty()) result.factors.emplace_back();
        return result;
    }

    [[nodiscard]] math::Float3 raster_emission_texture_average(const scene::ResolvedSceneView scene, const scene::TextureId id) {
        if (id.value == 0) return {1.0f, 1.0f, 1.0f};
        const scene::Texture& texture    = *std::ranges::find(scene.resources.textures, id, &scene::Texture::id);
        const scene::ImageTexture* image = std::get_if<scene::ImageTexture>(&texture.data);
        if (!image) return {1.0f, 1.0f, 1.0f};
        const std::size_t pixel_count               = static_cast<std::size_t>(image->width) * image->height;
        const scene::SpectrumColorSpace color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
        math::Float3 average{};
        for (std::size_t index = 0; index != pixel_count; ++index) {
            math::Float3 value{image->texels[index].x * image->scale, image->texels[index].y * image->scale, image->texels[index].z * image->scale};
            if (image->invert) value = {std::max(0.0f, 1.0f - value.x), std::max(0.0f, 1.0f - value.y), std::max(0.0f, 1.0f - value.z)};
            average = average + raster_linear_srgb(value, color_space);
        }
        return average / static_cast<float>(pixel_count);
    }

    void compile_raster_emitters(const GpuSceneView gpu_scene, const scene::ResolvedSceneView scene, const RasterTextureCompilation& textures, std::vector<RasterAreaLight>& surface_lights, std::vector<RasterAreaEmitterRange>& area_emitters) {
        const auto texture_handle = [&](const scene::TextureId id) { return id.value == 0 ? invalid_raster_index : textures.handles[raster_texture_source_index(scene, id)]; };
        surface_lights.reserve(gpu_scene.primitives.size());
        for (const GpuScenePrimitive& gpu_primitive : gpu_scene.primitives) {
            surface_lights.emplace_back();
            const scene::Instance& instance   = scene.resources.instances[gpu_primitive.scene_instance_index];
            const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
            if (primitive.area_light.value == 0) continue;
            const scene::DiffuseAreaLight& source = std::get<scene::DiffuseAreaLight>(std::ranges::find(scene.resources.lights, primitive.area_light, &scene::Light::id)->data);
            const math::Float3 radiance           = raster_spectrum_rgb(source.radiance);
            const math::Float3 texture_average    = raster_emission_texture_average(scene, source.emission_texture);
            const float texture_luminance         = 0.2126f * texture_average.x + 0.7152f * texture_average.y + 0.0722f * texture_average.z;
            const std::uint32_t flags             = (source.sidedness == scene::EmissionSidedness::Both ? 1u : 0u) | (primitive.reverse_orientation ? 2u : 0u);
            surface_lights.back()                 = RasterAreaLight{{radiance.x * source.scale, radiance.y * source.scale, radiance.z * source.scale, 0.0f}, {texture_handle(source.emission_texture), flags, 0, 0}};
            std::uint32_t range_kind{shader_semantics::area_source_triangle};
            std::uint32_t geometry_kind{shader_semantics::geometry_triangle};
            std::array<float, 4> geometry_parameters{};
            if (gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet) range_kind = shader_semantics::area_source_sphere_set;
            else {
                const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
                if (const scene::SphereGeometry* sphere = std::get_if<scene::SphereGeometry>(&geometry.data)) {
                    range_kind          = shader_semantics::area_source_analytic;
                    geometry_kind       = shader_semantics::geometry_sphere;
                    geometry_parameters = {sphere->radius, sphere->z_min, sphere->z_max, sphere->phi_max * std::numbers::pi_v<float> / 180.0f};
                } else if (const scene::DiskGeometry* disk = std::get_if<scene::DiskGeometry>(&geometry.data)) {
                    range_kind          = shader_semantics::area_source_analytic;
                    geometry_kind       = shader_semantics::geometry_disk;
                    geometry_parameters = {disk->height, disk->radius, disk->inner_radius, disk->phi_max * std::numbers::pi_v<float> / 180.0f};
                } else if (const scene::CylinderGeometry* cylinder = std::get_if<scene::CylinderGeometry>(&geometry.data)) {
                    range_kind          = shader_semantics::area_source_analytic;
                    geometry_kind       = shader_semantics::geometry_cylinder;
                    geometry_parameters = {cylinder->radius, cylinder->z_min, cylinder->z_max, cylinder->phi_max * std::numbers::pi_v<float> / 180.0f};
                }
            }
            area_emitters.push_back({gpu_primitive.scene_primitive_index, gpu_primitive.resource_index, range_kind, geometry_kind, geometry_parameters, {source.scale, source.power.value_or(-1.0f), source.sidedness == scene::EmissionSidedness::Both ? 2.0f : 1.0f, texture_luminance}, {radiance.x, radiance.y, radiance.z, 0.0f}});
        }
    }

} // namespace spectra::render
