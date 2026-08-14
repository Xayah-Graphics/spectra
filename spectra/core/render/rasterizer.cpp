module;

#include <cstddef>
#include "shaders/shader_semantics.h"

module spectra.render.rasterizer;

import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr std::uint32_t invalid_raster_index = std::numeric_limits<std::uint32_t>::max();

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

        struct RasterTextureCompilation {
            std::vector<RasterTextureHeader> headers{};
            std::vector<RasterTextureMapping> mappings{};
            std::vector<RasterConstantTexture> constants{};
            std::vector<RasterImageTexture> images{};
            std::vector<RasterCompositeTexture> checkerboards{};
            std::vector<RasterCompositeTexture> scales{};
            std::vector<RasterCompositeTexture> mixes{};
            std::vector<RasterDirectionMixTexture> direction_mixes{};
            std::vector<RasterBilerpTexture> bilerps{};
            std::vector<std::uint32_t> handles{};
        };

        [[nodiscard]] std::uint32_t raster_texture_source_index(const scene::SceneView scene, const scene::TextureId id) {
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
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::UvTextureMapping>)
                        result.parameter_0 = {data.scale.x, data.scale.y, data.offset.x, data.offset.y};
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

        [[nodiscard]] RasterTextureCompilation compile_raster_textures(GpuScene& gpu_scene, const scene::SceneView scene) {
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

        struct RasterMaterialCompilation {
            std::vector<RasterMaterial> materials{};
            std::vector<RasterMaterialRange> ranges{};
            std::vector<RasterMaterialTerm> terms{};
            std::vector<RasterMaterialFactor> factors{};
        };

        [[nodiscard]] RasterMaterialCompilation compile_raster_materials(const scene::SceneView scene, const RasterTextureCompilation& textures) {
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
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>)
                            compiled.metadata[0] = shader_semantics::raster_material_interface;
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

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_raster_buffer(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage) {
            GpuBuffer destination       = runtime.resources.create_buffer(elements.size_bytes(), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(elements));
            command_buffer.copyBuffer(upload.buffer, *destination.buffer,
                vk::BufferCopy{
                    upload.offset,
                    0,
                    upload.size,
                });
            const vk::BufferMemoryBarrier2 dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlagBits2::eAllCommands,
                static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *destination.buffer,
                0,
                destination.size,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{
                {},
                0,
                nullptr,
                1,
                &dependency,
            });
            return destination;
        }

        [[nodiscard]] math::Float3 raster_emission_texture_average(const scene::SceneView scene, const scene::TextureId id) {
            if (id.value == 0) return {1.0f, 1.0f, 1.0f};
            const scene::Texture& texture     = *std::ranges::find(scene.resources.textures, id, &scene::Texture::id);
            const scene::ImageTexture* image = std::get_if<scene::ImageTexture>(&texture.data);
            if (!image) return {1.0f, 1.0f, 1.0f};
            const std::size_t pixel_count = static_cast<std::size_t>(image->width) * image->height;
            const scene::SpectrumColorSpace color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
            math::Float3 average{};
            for (std::size_t index = 0; index != pixel_count; ++index) {
                math::Float3 value{image->texels[index].x * image->scale, image->texels[index].y * image->scale, image->texels[index].z * image->scale};
                if (image->invert) value = {std::max(0.0f, 1.0f - value.x), std::max(0.0f, 1.0f - value.y), std::max(0.0f, 1.0f - value.z)};
                average = average + raster_linear_srgb(value, color_space);
            }
            return average / static_cast<float>(pixel_count);
        }

        void compile_raster_emitters(const GpuSceneView gpu_scene, const scene::SceneView scene, const RasterTextureCompilation& textures, std::vector<RasterAreaLight>& surface_lights, std::vector<RasterAreaEmitterRange>& area_emitters) {
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
                if (gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet)
                    range_kind = shader_semantics::area_source_sphere_set;
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

    } // namespace

    Rasterizer::Rasterizer(VulkanRuntime& runtime, GpuScene& gpu_scene, const scene::SceneView scene_view, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {
        this->initialize_scene(scene_view);
        this->initialize_renderer();
    }

    Rasterizer::~Rasterizer() {
        this->destroy();
    }

    void Rasterizer::invalidate(const scene::SceneChange changes, const GpuSceneUpdate gpu_update) noexcept {
        this->renderer.pending_changes     = this->renderer.pending_changes | changes;
        this->renderer.pending_gpu_changes = this->renderer.pending_gpu_changes | gpu_update.gpu_changes;
    }

    void Rasterizer::prepare(scene::SceneView scene_view, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        if (this->renderer.pending_changes != scene::SceneChange::None) {
            scene_view.revision.changes = this->renderer.pending_changes;
            this->synchronize_scene(scene_view, command_buffer);
            this->renderer.pending_changes = scene::SceneChange::None;
        }
        bool gpu_recompiled{};
        if ((this->renderer.pending_gpu_changes & GpuSceneChange::Structure) != GpuSceneChange::None) {
            this->upload_scene(scene_view, command_buffer);
            gpu_recompiled = true;
        }
        if (!gpu_recompiled && (this->renderer.pending_gpu_changes & (GpuSceneChange::Geometry | GpuSceneChange::Transform)) != GpuSceneChange::None) this->update_area_emitters(command_buffer);
        this->renderer.pending_gpu_changes = GpuSceneChange::None;
        this->renderer.camera              = view.camera;
        if (!*this->renderer.output_image.image || this->renderer.output_image.extent != view.extent) this->create_output(view.extent);
    }

    void Rasterizer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t) {
        this->record_commands(command_buffer);
    }

    RenderOutput Rasterizer::output() const noexcept {
        return {
            this->renderer.output_image,
            this->renderer.sampled_output_descriptor,
            this->renderer.output_layout,
            this->renderer.output_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            this->renderer.output_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite : vk::AccessFlagBits2::eColorAttachmentWrite,
            this->renderer.film_color_space,
            this->renderer.film_exposure + std::log2(this->renderer.camera.exposure_time * this->renderer.film_iso / 100.0f),
        };
    }

    DepthBufferView Rasterizer::depth_buffer() noexcept {
        return {this->renderer.depth_image, this->renderer.sampled_depth_descriptor, this->renderer.depth_layout};
    }

    void Rasterizer::set_display_mode(const RasterDisplayMode mode) noexcept {
        this->renderer.display_mode = mode;
    }

    void Rasterizer::initialize_scene(const scene::SceneView scene) {
        this->context.runtime.frames.retire_frame();
        this->scene.zero_volume_field_descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.uploaded_revision                       = scene.revision;
        this->renderer.camera                               = scene.camera;
        this->renderer.film_exposure                        = scene.film.exposure;
        this->renderer.film_iso                             = scene.film.iso;
        this->renderer.film_color_space                     = scene.film.color_space;
        this->renderer.film_maximum_component_value         = scene.film.maximum_component_value;
        this->renderer.film_resolution                      = scene.film.resolution;
        this->renderer.film_pixel_minimum                   = scene.film.pixel_minimum;
        this->renderer.film_pixel_maximum                   = scene.film.pixel_maximum;
        const std::vector<std::uint32_t> area_emission_code = load_spirv(this->context.shader_directory / "raster_area_emission.spv");
        this->scene.area_emission_shader                    = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, area_emission_code.size() * sizeof(std::uint32_t), area_emission_code.data(), "update_raster_area_emission"}};
        const std::array<math::Float3, 1> zero{};
        this->context.runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            this->scene.zero_volume_field_buffer = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const math::Float3>{zero}, vk::BufferUsageFlagBits::eStorageBuffer);
            this->context.runtime.resources.write_buffer_descriptor(this->scene.zero_volume_field_descriptor, vk::DescriptorType::eStorageBuffer, this->scene.zero_volume_field_buffer);
            this->upload_scene(scene, command_buffer);
        });
    }

    void Rasterizer::upload_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        RasterTextureCompilation textures   = compile_raster_textures(this->context.gpu_scene, scene);
        RasterMaterialCompilation materials = compile_raster_materials(scene, textures);

        std::vector<RasterAreaLight> area_lights{};
        std::vector<RasterAreaEmitterRange> area_emitters{};
        compile_raster_emitters(this->context.gpu_scene.view(), scene, textures, area_lights, area_emitters);
        if (area_lights.empty()) area_lights.emplace_back();

        std::vector<RasterLight> lights{};
        math::Float3 environment_radiance{};
        for (const scene::Light& source : scene.resources.lights)
            std::visit(
                [&lights, &environment_radiance, &scene](const auto& light) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::DiffuseAreaLight>)
                        return;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::PointLight>) {
                        const math::Float3 radiance = raster_spectrum_rgb(light.intensity) * light.scale;
                        const math::Float3 position = light.transform.transform_point({});
                        RasterLight record{};
                        record.metadata[0]          = shader_semantics::light_point;
                        record.radiance             = {radiance.x, radiance.y, radiance.z, 0.0f};
                        record.position             = {position.x, position.y, position.z, 0.0f};
                        lights.push_back(record);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::SpotLight>) {
                        const math::Float3 radiance  = raster_spectrum_rgb(light.intensity) * light.scale;
                        const math::Float3 position  = light.transform.transform_point({});
                        const math::Float3 direction = light.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                        RasterLight record{};
                        record.metadata[0]           = shader_semantics::light_spot;
                        record.radiance              = {radiance.x, radiance.y, radiance.z, 0.0f};
                        record.position              = {position.x, position.y, position.z, 0.0f};
                        record.direction             = {direction.x, direction.y, direction.z, std::cos((light.cone_angle - light.cone_delta) * std::numbers::pi_v<float> / 180.0f)};
                        record.parameters[0]         = std::cos(light.cone_angle * std::numbers::pi_v<float> / 180.0f);
                        lights.push_back(record);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::DistantLight>) {
                        const math::Float3 radiance  = raster_spectrum_rgb(light.radiance) * light.scale;
                        const math::Float3 direction = light.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                        RasterLight record{};
                        record.metadata[0]           = shader_semantics::light_distant;
                        record.radiance              = {radiance.x, radiance.y, radiance.z, 0.0f};
                        record.direction             = {direction.x, direction.y, direction.z, 0.0f};
                        lights.push_back(record);
                    } else {
                        const scene::InfiniteLight& environment = [&light]() -> const scene::InfiniteLight& {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::InfiniteLight>) return light;
                            else return light.environment;
                        }();
                        math::Float3 radiance              = raster_spectrum_rgb(environment.radiance) * environment.scale;
                        const math::Float3 texture_average = raster_emission_texture_average(scene, environment.emission_texture);
                        radiance                           = {radiance.x * texture_average.x, radiance.y * texture_average.y, radiance.z * texture_average.z};
                        RasterLight record{};
                        record.metadata[0]         = shader_semantics::light_infinite;
                        record.radiance            = {radiance.x, radiance.y, radiance.z, 0.0f};
                        lights.push_back(record);
                        environment_radiance = environment_radiance + radiance;
                    }
                },
                source.data);
        const std::uint32_t light_count = static_cast<std::uint32_t>(lights.size());
        if (lights.empty()) lights.emplace_back();

        std::vector<RasterPrimitive> primitives{};
        std::vector<std::uint32_t> face_materials{};
        primitives.reserve(this->context.gpu_scene.view().primitives.size());
        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
            const scene::Instance& instance          = scene.resources.instances[gpu_primitive.scene_instance_index];
            const scene::Prototype& prototype        = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive        = prototype.primitives[gpu_primitive.prototype_primitive_index];
            const bool sphere_draw                   = gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet;
            const std::uint32_t face_material_offset = static_cast<std::uint32_t>(face_materials.size());
            for (const scene::MaterialId face_material : sphere_draw ? std::span<const scene::MaterialId>{} : std::span<const scene::MaterialId>{primitive.face_materials}) {
                const std::vector<scene::Material>::const_iterator resource = std::ranges::find(scene.resources.materials, face_material, &scene::Material::id);
                face_materials.push_back(static_cast<std::uint32_t>(resource - scene.resources.materials.begin()));
            }
            const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::Material::id);
            std::uint32_t area_light                                    = std::numeric_limits<std::uint32_t>::max();
            if (primitive.area_light.value != 0) area_light = static_cast<std::uint32_t>(primitives.size());
            const std::uint32_t transform_index = static_cast<std::uint32_t>(primitives.size());
            const std::uint32_t alpha_texture   = primitive.alpha.value == 0 ? invalid_raster_index : textures.handles[raster_texture_source_index(scene, primitive.alpha)];
            primitives.push_back(RasterPrimitive{transform_index, static_cast<std::uint32_t>(material - scene.resources.materials.begin()), area_light, primitive.reverse_orientation ? 1u : 0u, face_material_offset, static_cast<std::uint32_t>(sphere_draw ? 0 : primitive.face_materials.size()), alpha_texture, 0});
        }
        if (primitives.empty()) primitives.emplace_back();
        if (face_materials.empty()) face_materials.emplace_back();

        GpuBuffer new_primitives         = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterPrimitive>{primitives}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_materials          = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterMaterial>{materials.materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_material_ranges    = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterMaterialRange>{materials.ranges}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_material_terms     = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterMaterialTerm>{materials.terms}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_material_factors   = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterMaterialFactor>{materials.factors}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_face_materials     = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const std::uint32_t>{face_materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_area_lights        = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterAreaLight>{area_lights}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_lights             = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterLight>{lights}, vk::BufferUsageFlagBits::eStorageBuffer);
        const auto upload_texture_buffer = [this, &command_buffer]<typename Element>(const std::span<const Element> elements) { return upload_raster_buffer(this->context.runtime, command_buffer, elements, vk::BufferUsageFlagBits::eStorageBuffer); };
        std::vector<RasterVolume> raster_volumes{};
        raster_volumes.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) {
            if (!volume.visible) continue;
            const GpuVolume& shared = *std::ranges::find(this->context.gpu_scene.view().volumes, volume.id, &GpuVolume::volume_id);
            const scene::VolumeRendering& rendering = volume.rendering;
            RasterVolume record{};
            record.bounds_minimum          = {volume.domain.minimum.x, volume.domain.minimum.y, volume.domain.minimum.z, 0.0f};
            record.bounds_maximum          = {volume.domain.maximum.x, volume.domain.maximum.y, volume.domain.maximum.z, 0.0f};
            const math::Transform inverse  = volume.transform.inverse();
            record.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            record.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            record.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
            const math::Float3 sigma_a  = raster_spectrum_rgb(rendering.sigma_a);
            const math::Float3 sigma_s  = raster_spectrum_rgb(rendering.sigma_s);
            const math::Float3 emission = raster_spectrum_rgb(rendering.emission);
            record.sigma_a              = {sigma_a.x, sigma_a.y, sigma_a.z, 0.0f};
            record.sigma_s              = {sigma_s.x, sigma_s.y, sigma_s.z, 0.0f};
            record.emission             = {emission.x, emission.y, emission.z, 0.0f};
            record.scales               = {rendering.density_scale, rendering.emission_scale, rendering.anisotropy, rendering.temperature_scale};
            record.temperature          = {rendering.temperature_offset, rendering.minimum_emission_temperature, rendering.blackbody_emission ? 1.0f : 0.0f, 0.0f};
            const auto field = [this, &shared](const std::string_view id) -> DescriptorHandle {
                const std::vector<GpuVolumeField>::const_iterator found = std::ranges::find(shared.fields, id, &GpuVolumeField::id);
                return found == shared.fields.end() ? this->scene.zero_volume_field_descriptor : found->descriptors.front();
            };
            if (const auto* data = std::get_if<scene::GridVolume>(&volume.data)) {
                const auto vertex_sampled = [data](const std::string_view id) {
                    const std::vector<scene::VolumeField>::const_iterator found = std::ranges::find(data->fields, id, &scene::VolumeField::id);
                    return found != data->fields.end() && found->sampling == scene::VolumeFieldSampling::Vertex ? 1u : 0u;
                };
                const bool rgb                   = rendering.density_field.empty() && (!rendering.sigma_a_field.empty() || !rendering.sigma_s_field.empty() || !rendering.emission_field.empty());
                record.metadata                 = {rgb ? 1u : 0u, data->resolution.x, data->resolution.y, data->resolution.z};
                const std::uint32_t field_flags = (rgb ? (rendering.sigma_a_field.empty() ? 0u : 1u) | (rendering.sigma_s_field.empty() ? 0u : 2u) | (rendering.emission_field.empty() ? 0u : 4u) | (std::to_underlying(rendering.field_color_space) << 8) : (!rendering.temperature_field.empty() ? 1u : 0u) | (!rendering.emission_scale_field.empty() ? 2u : 0u))
                    | (vertex_sampled(rendering.density_field) << 16u)
                    | (vertex_sampled(rendering.temperature_field) << 17u)
                    | (vertex_sampled(rendering.emission_scale_field) << 18u)
                    | (vertex_sampled(rendering.sigma_a_field) << 19u)
                    | (vertex_sampled(rendering.sigma_s_field) << 20u)
                    | (vertex_sampled(rendering.emission_field) << 21u);
                record.procedural_parameters[3] = std::bit_cast<float>(field_flags);
            } else {
                const scene::ProceduralCloudVolume& cloud = std::get<scene::ProceduralCloudVolume>(volume.data);
                record.metadata                           = {2, 0, 0, 0};
                record.procedural_parameters              = {cloud.density, cloud.wispiness, cloud.frequency, 0.0f};
            }
            record.density              = field(rendering.density_field);
            record.temperature_field    = field(rendering.temperature_field);
            record.sigma_a_field        = field(rendering.sigma_a_field);
            record.sigma_s_field        = field(rendering.sigma_s_field);
            record.emission_scale_field = field(rendering.emission_scale_field);
            record.emission_field       = field(rendering.emission_field);
            raster_volumes.push_back(record);
        }
        const std::uint32_t volume_count = static_cast<std::uint32_t>(raster_volumes.size());
        if (raster_volumes.empty()) raster_volumes.emplace_back();

        GpuBuffer new_volume_buffer = upload_texture_buffer(std::span<const RasterVolume>{raster_volumes});
        std::array<GpuBuffer, 9> new_texture_buffers{};
        new_texture_buffers[0] = upload_texture_buffer(std::span<const RasterTextureHeader>{textures.headers});
        new_texture_buffers[1] = upload_texture_buffer(std::span<const RasterTextureMapping>{textures.mappings});
        new_texture_buffers[2] = upload_texture_buffer(std::span<const RasterConstantTexture>{textures.constants});
        new_texture_buffers[3] = upload_texture_buffer(std::span<const RasterImageTexture>{textures.images});
        new_texture_buffers[4] = upload_texture_buffer(std::span<const RasterCompositeTexture>{textures.checkerboards});
        new_texture_buffers[5] = upload_texture_buffer(std::span<const RasterCompositeTexture>{textures.scales});
        new_texture_buffers[6] = upload_texture_buffer(std::span<const RasterCompositeTexture>{textures.mixes});
        new_texture_buffers[7] = upload_texture_buffer(std::span<const RasterDirectionMixTexture>{textures.direction_mixes});
        new_texture_buffers[8] = upload_texture_buffer(std::span<const RasterBilerpTexture>{textures.bilerps});
        GpuBuffer new_binding_buffer{};
        DescriptorLease primitives_descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease material_descriptor        = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease material_range_descriptor  = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease material_term_descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease material_factor_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease face_material_descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease area_light_descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease light_descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease volumes_descriptor         = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
        this->context.runtime.resources.write_buffer_descriptor(material_descriptor, vk::DescriptorType::eStorageBuffer, new_materials);
        this->context.runtime.resources.write_buffer_descriptor(material_range_descriptor, vk::DescriptorType::eStorageBuffer, new_material_ranges);
        this->context.runtime.resources.write_buffer_descriptor(material_term_descriptor, vk::DescriptorType::eStorageBuffer, new_material_terms);
        this->context.runtime.resources.write_buffer_descriptor(material_factor_descriptor, vk::DescriptorType::eStorageBuffer, new_material_factors);
        this->context.runtime.resources.write_buffer_descriptor(face_material_descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
        this->context.runtime.resources.write_buffer_descriptor(area_light_descriptor, vk::DescriptorType::eStorageBuffer, new_area_lights);
        this->context.runtime.resources.write_buffer_descriptor(light_descriptor, vk::DescriptorType::eStorageBuffer, new_lights);
        this->context.runtime.resources.write_buffer_descriptor(volumes_descriptor, vk::DescriptorType::eStorageBuffer, new_volume_buffer);
        std::array<DescriptorLease, 9> texture_descriptors{};
        for (std::size_t index = 0; index != texture_descriptors.size(); ++index) {
            texture_descriptors[index] = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_texture_buffers[index]);
        }
        const RasterSceneBindings bindings{primitives_descriptor, this->context.gpu_scene.view().primitive_transforms, material_descriptor, material_range_descriptor, material_term_descriptor, material_factor_descriptor, face_material_descriptor, area_light_descriptor, light_descriptor, texture_descriptors[0], texture_descriptors[1], texture_descriptors[2], texture_descriptors[3], texture_descriptors[4], texture_descriptors[5], texture_descriptors[6], texture_descriptors[7], texture_descriptors[8]};
        new_binding_buffer                  = upload_texture_buffer(std::span{&bindings, 1});
        DescriptorLease bindings_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(bindings_descriptor, vk::DescriptorType::eStorageBuffer, new_binding_buffer);
        this->context.runtime.frames.defer_destruction([primitives = std::move(this->scene.primitive_buffer), materials = std::move(this->scene.material_buffer), material_ranges = std::move(this->scene.material_range_buffer), material_terms = std::move(this->scene.material_term_buffer), material_factors = std::move(this->scene.material_factor_buffer), face_materials = std::move(this->scene.face_material_buffer), area_lights = std::move(this->scene.area_light_buffer), fixed_lights = std::move(this->scene.light_buffer)]() mutable {});
        this->context.runtime.frames.defer_destruction([texture_buffers = std::move(this->scene.texture_buffers), bindings = std::move(this->scene.scene_binding_buffer)]() mutable {});
        this->scene.primitives_descriptor      = std::move(primitives_descriptor);
        this->scene.material_descriptor        = std::move(material_descriptor);
        this->scene.material_range_descriptor  = std::move(material_range_descriptor);
        this->scene.material_term_descriptor   = std::move(material_term_descriptor);
        this->scene.material_factor_descriptor = std::move(material_factor_descriptor);
        this->scene.face_material_descriptor   = std::move(face_material_descriptor);
        this->scene.area_light_descriptor      = std::move(area_light_descriptor);
        this->scene.light_descriptor           = std::move(light_descriptor);
        this->scene.volumes_descriptor         = std::move(volumes_descriptor);
        this->scene.texture_descriptors        = std::move(texture_descriptors);
        this->scene.bindings_descriptor        = std::move(bindings_descriptor);
        this->scene.primitive_buffer           = std::move(new_primitives);
        this->scene.material_buffer            = std::move(new_materials);
        this->scene.material_range_buffer      = std::move(new_material_ranges);
        this->scene.material_term_buffer       = std::move(new_material_terms);
        this->scene.material_factor_buffer     = std::move(new_material_factors);
        this->scene.face_material_buffer       = std::move(new_face_materials);
        this->scene.area_light_buffer          = std::move(new_area_lights);
        this->scene.light_buffer               = std::move(new_lights);
        this->context.runtime.frames.defer_destruction([volumes = std::move(this->scene.volume_buffer)]() mutable {});
        this->scene.volume_buffer        = std::move(new_volume_buffer);
        this->scene.texture_buffers      = std::move(new_texture_buffers);
        this->scene.scene_binding_buffer = std::move(new_binding_buffer);
        this->scene.light_count          = light_count;
        this->scene.volume_count         = volume_count;
        this->scene.environment_radiance = environment_radiance;
        this->scene.area_emitters        = std::move(area_emitters);
        this->update_area_emitters(command_buffer);
    }

    void Rasterizer::update_area_emitters(const vk::raii::CommandBuffer& command_buffer) {
        if (this->scene.area_emitters.empty()) return;
        struct alignas(16) RasterAreaEmissionPushData {
            DescriptorHandle positions{};
            DescriptorHandle indices{};
            DescriptorHandle radii{};
            DescriptorHandle transforms{};
            DescriptorHandle area_lights{};
            std::uint64_t reserved{};
            std::array<std::uint32_t, 4> range{};
            std::array<float, 4> geometry_parameters{};
            std::array<float, 4> emission_parameters{};
            std::array<float, 4> source_radiance{};
        };
        static_assert(sizeof(RasterAreaEmissionPushData) == 112);
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->scene.area_emission_shader);
        for (const RasterAreaEmitterRange& range : this->scene.area_emitters) {
            RasterAreaEmissionPushData push{};
            push.transforms          = gpu_scene.primitive_transforms;
            push.area_lights         = this->scene.area_light_descriptor;
            push.range               = {range.scene_primitive_index, 1, range.kind, range.geometry_kind};
            push.geometry_parameters = range.geometry_parameters;
            push.emission_parameters = range.emission_parameters;
            push.source_radiance     = range.radiance;
            if (range.kind == shader_semantics::area_source_triangle) {
                const GpuGeometry& geometry = gpu_scene.geometries[range.resource_index];
                push.positions              = geometry.positions_descriptor;
                push.indices                = geometry.indices_descriptor;
                push.range[1]               = geometry.index_count / 3u;
            } else if (range.kind == shader_semantics::area_source_sphere_set) {
                const GpuSphereSet& spheres = gpu_scene.sphere_sets[range.resource_index];
                push.radii                  = spheres.radii_descriptor;
                push.range[1]               = spheres.sphere_count;
            }
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch(1, 1, 1);
        }
        const vk::MemoryBarrier2 render_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &render_dependency});
    }

    void Rasterizer::synchronize_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.number == this->scene.uploaded_revision.number) return;
        const bool recompiled = (scene.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Transform | scene::SceneChange::Volume)) != scene::SceneChange::None;
        if (recompiled) this->upload_scene(scene, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None) this->renderer.camera = scene.camera;
        if ((scene.revision.changes & scene::SceneChange::Film) != scene::SceneChange::None) {
            this->renderer.film_exposure                = scene.film.exposure;
            this->renderer.film_iso                     = scene.film.iso;
            this->renderer.film_color_space             = scene.film.color_space;
            this->renderer.film_maximum_component_value = scene.film.maximum_component_value;
            this->renderer.film_resolution              = scene.film.resolution;
            this->renderer.film_pixel_minimum           = scene.film.pixel_minimum;
            this->renderer.film_pixel_maximum           = scene.film.pixel_maximum;
        }
        this->scene.uploaded_revision = scene.revision;
    }

    void Rasterizer::initialize_renderer() {
        this->renderer.sampled_output_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->renderer.storage_output_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->renderer.sampled_depth_descriptor  = this->context.runtime.frames.allocate_resource_descriptor();
        this->create_shaders();
    }

    void Rasterizer::create_shaders() {
        const std::vector<std::uint32_t> mesh_code     = load_spirv(this->context.shader_directory / "raster_mesh.spv");
        const std::vector<std::uint32_t> sphere_code   = load_spirv(this->context.shader_directory / "raster_spheres.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "raster_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                mesh_code.size() * sizeof(std::uint32_t),
                mesh_code.data(),
                "raster_mesh",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "raster_fragment",
            },
        };
        this->renderer.shaders                       = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
        this->renderer.sphere_shader                 = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, sphere_code.size() * sizeof(std::uint32_t), sphere_code.data(), "raster_spheres"}};
        const std::vector<std::uint32_t> volume_code = load_spirv(this->context.shader_directory / "raster_volume.spv");
        this->renderer.volume_shader                 = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, volume_code.size() * sizeof(std::uint32_t), volume_code.data(), "raster_volume"}};
    }

    void Rasterizer::create_output(const vk::Extent2D extent) {
        GpuImage next_output           = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
        GpuImage next_depth            = this->context.runtime.resources.create_image_2d(extent, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eDepth);
        const bool replacing           = *this->renderer.output_image.image;
        DescriptorLease sampled_output = replacing ? this->context.runtime.frames.allocate_resource_descriptor() : std::move(this->renderer.sampled_output_descriptor);
        DescriptorLease storage_output = replacing ? this->context.runtime.frames.allocate_resource_descriptor() : std::move(this->renderer.storage_output_descriptor);
        DescriptorLease sampled_depth  = replacing ? this->context.runtime.frames.allocate_resource_descriptor() : std::move(this->renderer.sampled_depth_descriptor);
        this->context.runtime.resources.write_sampled_image_descriptor(sampled_output, next_output, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->context.runtime.resources.write_storage_image_descriptor(storage_output, next_output, vk::ImageLayout::eGeneral);
        this->context.runtime.resources.write_sampled_image_descriptor(sampled_depth, next_depth, vk::ImageLayout::eShaderReadOnlyOptimal);
        if (replacing) this->context.runtime.frames.defer_destruction([output = std::move(this->renderer.output_image), depth = std::move(this->renderer.depth_image)]() mutable {});
        this->renderer.output_image              = std::move(next_output);
        this->renderer.depth_image               = std::move(next_depth);
        this->renderer.sampled_output_descriptor = std::move(sampled_output);
        this->renderer.storage_output_descriptor = std::move(storage_output);
        this->renderer.sampled_depth_descriptor  = std::move(sampled_depth);
        this->renderer.output_layout             = vk::ImageLayout::eUndefined;
        this->renderer.depth_layout              = vk::ImageLayout::eUndefined;
    }

    void Rasterizer::record_commands(const vk::raii::CommandBuffer& command_buffer) {
        const bool wireframe_only = this->renderer.display_mode == RasterDisplayMode::Wireframe;
        const float imaging_ratio = this->renderer.camera.exposure_time * this->renderer.film_iso / 100.0f;
        const float maximum_component_value = this->renderer.film_maximum_component_value.value_or(0.0f) / imaging_ratio;
        math::Float3 background = this->scene.environment_radiance;
        const float background_maximum = std::max(background.x, std::max(background.y, background.z));
        if (this->renderer.film_maximum_component_value && background_maximum > maximum_component_value) background = background * (maximum_component_value / background_maximum);
        background = raster_output_rgb(background, this->renderer.film_color_space);
        const std::array<std::uint32_t, 4> film_bounds{
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(this->renderer.film_pixel_minimum[0]) * this->renderer.output_image.extent.width / this->renderer.film_resolution[0]),
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(this->renderer.film_pixel_minimum[1]) * this->renderer.output_image.extent.height / this->renderer.film_resolution[1]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(this->renderer.film_pixel_maximum[0]) * this->renderer.output_image.extent.width + this->renderer.film_resolution[0] - 1u) / this->renderer.film_resolution[0]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(this->renderer.film_pixel_maximum[1]) * this->renderer.output_image.extent.height + this->renderer.film_resolution[1] - 1u) / this->renderer.film_resolution[1]),
        };
        const std::array barriers{
            vk::ImageMemoryBarrier2{
                this->renderer.output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone
                : this->renderer.output_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader
                                                                            : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                this->renderer.output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{}
                : this->renderer.output_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite
                                                                            : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->renderer.output_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->renderer.output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->renderer.depth_layout == vk::ImageLayout::eUndefined               ? vk::PipelineStageFlagBits2::eNone
                : this->renderer.depth_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eComputeShader
                                                                                         : vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->renderer.depth_layout == vk::ImageLayout::eUndefined               ? vk::AccessFlags2{}
                : this->renderer.depth_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead
                                                                                         : vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->renderer.depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->renderer.depth_image.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()), barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            *this->renderer.output_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{background.x, background.y, background.z, 1.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->renderer.depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, this->renderer.output_image.extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });

        const vk::Viewport viewport{
            0.0f,
            static_cast<float>(this->renderer.output_image.extent.height),
            static_cast<float>(this->renderer.output_image.extent.width),
            -static_cast<float>(this->renderer.output_image.extent.height),
            0.0f,
            1.0f,
        };
        const vk::Rect2D scissor{{static_cast<std::int32_t>(film_bounds[0]), static_cast<std::int32_t>(film_bounds[1])}, {film_bounds[2] - film_bounds[0], film_bounds[3] - film_bounds[1]}};
        command_buffer.setViewportWithCount(viewport);
        command_buffer.setScissorWithCount(scissor);
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::True);
        command_buffer.setDepthWriteEnable(vk::True);
        command_buffer.setDepthCompareOp(vk::CompareOp::eLess);
        set_basic_graphics_state(command_buffer);
        const vk::Bool32 blend_enable = wireframe_only ? vk::True : vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        if (wireframe_only)
            command_buffer.setColorBlendEquationEXT(0, vk::ColorBlendEquationEXT{
                                                           vk::BlendFactor::eSrcAlpha,
                                                           vk::BlendFactor::eOneMinusSrcAlpha,
                                                           vk::BlendOp::eAdd,
                                                           vk::BlendFactor::eOne,
                                                           vk::BlendFactor::eOneMinusSrcAlpha,
                                                           vk::BlendOp::eAdd,
                                                       });
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{*this->renderer.shaders[0], vk::ShaderEXT{}, *this->renderer.shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);

        struct alignas(16) RasterPushData {
            DescriptorHandle positions;
            DescriptorHandle normals;
            DescriptorHandle tangents;
            DescriptorHandle texture_coordinates;
            DescriptorHandle indices;
            DescriptorHandle bindings;
            std::uint32_t scene_primitive_index;
            std::uint32_t element_count;
            std::array<std::uint32_t, 4> attributes;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> camera_position;
            std::array<float, 4> camera_right;
            std::array<float, 4> camera_up;
            std::array<float, 4> camera_forward;
            std::array<float, 4> film;
        };
        static_assert(sizeof(RasterPushData) == 224);
        static_assert(offsetof(RasterPushData, attributes) == 56);
        static_assert(offsetof(RasterPushData, view_projection_row_0) == 72);
        static_assert(offsetof(RasterPushData, film) == 200);
        const std::array<float, 16> view_projection = this->renderer.camera.matrices().view_projection;
        const scene::CameraFrame camera             = this->renderer.camera.frame();
        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
            if (gpu_primitive.kind != GpuScenePrimitiveKind::Geometry) continue;
            const GpuGeometry& mesh = this->context.gpu_scene.view().geometries[gpu_primitive.resource_index];
            const RasterPushData push_data{
                mesh.positions_descriptor,
                mesh.normals_descriptor,
                mesh.tangents_descriptor,
                mesh.texture_coordinates_descriptor,
                mesh.indices_descriptor,
                this->scene.bindings_descriptor,
                gpu_primitive.scene_primitive_index,
                mesh.index_count / 3u,
                {mesh.attribute_mask, static_cast<std::uint32_t>(this->renderer.display_mode), 0, std::to_underlying(this->renderer.film_color_space)},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                {camera.position.x, camera.position.y, camera.position.z, std::holds_alternative<scene::PerspectiveCameraData>(this->renderer.camera.data) ? 1.0f : 0.0f},
                {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                {camera.up.x, camera.up.y, camera.up.z, 0.0f},
                {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f},
                {maximum_component_value, this->renderer.film_maximum_component_value.has_value() ? 1.0f : 0.0f, 0.0f, 0.0f},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        }
        if (!wireframe_only) {
            const std::array sphere_shader_handles{*this->renderer.sphere_shader, vk::ShaderEXT{}, *this->renderer.shaders[1]};
            command_buffer.bindShadersEXT(stages, sphere_shader_handles);
            for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
                if (gpu_primitive.kind != GpuScenePrimitiveKind::SphereSet) continue;
                const GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                const RasterPushData push_data{
                    spheres.positions_descriptor,
                    spheres.radii_descriptor,
                    {},
                    {},
                    {},
                    this->scene.bindings_descriptor,
                    gpu_primitive.scene_primitive_index,
                    spheres.sphere_count,
                    {0, static_cast<std::uint32_t>(this->renderer.display_mode), 0, std::to_underlying(this->renderer.film_color_space)},
                    {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                    {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                    {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                    {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                    {camera.position.x, camera.position.y, camera.position.z, std::holds_alternative<scene::PerspectiveCameraData>(this->renderer.camera.data) ? 1.0f : 0.0f},
                    {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                    {camera.up.x, camera.up.y, camera.up.z, 0.0f},
                    {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f},
                    {maximum_component_value, this->renderer.film_maximum_component_value.has_value() ? 1.0f : 0.0f, 0.0f, 0.0f},
                };
                this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
            }
        }
        command_buffer.endRendering();
        if (wireframe_only || this->scene.volume_count == 0) {
            this->renderer.output_layout = vk::ImageLayout::eColorAttachmentOptimal;
            this->renderer.depth_layout  = vk::ImageLayout::eDepthAttachmentOptimal;
            return;
        }
        const std::array volume_barriers{
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.output_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.depth_image.image, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(volume_barriers.size()), volume_barriers.data()});
        struct alignas(16) VolumePushData {
            DescriptorHandle output;
            DescriptorHandle depth;
            DescriptorHandle volumes;
            DescriptorHandle bindings;
            std::array<std::uint32_t, 4> metadata;
            std::array<std::uint32_t, 4> film_bounds;
            std::array<float, 4> film;
            std::array<float, 4> inverse_view_projection_row_0;
            std::array<float, 4> inverse_view_projection_row_1;
            std::array<float, 4> inverse_view_projection_row_2;
            std::array<float, 4> inverse_view_projection_row_3;
        };
        static_assert(sizeof(VolumePushData) == 144);
        const std::array<float, 16> inverse_view_projection = this->renderer.camera.matrices().inverse_view_projection;
        const VolumePushData volume_push_data{this->renderer.storage_output_descriptor, this->renderer.sampled_depth_descriptor, this->scene.volumes_descriptor, this->scene.bindings_descriptor, {this->renderer.output_image.extent.width, this->renderer.output_image.extent.height, this->scene.volume_count, this->scene.light_count}, film_bounds, {maximum_component_value, this->renderer.film_maximum_component_value.has_value() ? 1.0f : 0.0f, static_cast<float>(std::to_underlying(this->renderer.film_color_space)), 0.0f}, {inverse_view_projection[0], inverse_view_projection[1], inverse_view_projection[2], inverse_view_projection[3]}, {inverse_view_projection[4], inverse_view_projection[5], inverse_view_projection[6], inverse_view_projection[7]}, {inverse_view_projection[8], inverse_view_projection[9], inverse_view_projection[10], inverse_view_projection[11]}, {inverse_view_projection[12], inverse_view_projection[13], inverse_view_projection[14], inverse_view_projection[15]}};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.volume_shader);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&volume_push_data, 1}));
        command_buffer.dispatch((this->renderer.output_image.extent.width + 7) / 8, (this->renderer.output_image.extent.height + 7) / 8, 1);
        this->renderer.output_layout = vk::ImageLayout::eGeneral;
        this->renderer.depth_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    void Rasterizer::destroy() noexcept {
        this->context.runtime.frames.defer_destruction([primitive_buffer = std::move(this->scene.primitive_buffer), material_buffer = std::move(this->scene.material_buffer), material_range_buffer = std::move(this->scene.material_range_buffer), material_term_buffer = std::move(this->scene.material_term_buffer), material_factor_buffer = std::move(this->scene.material_factor_buffer), face_material_buffer = std::move(this->scene.face_material_buffer), area_light_buffer = std::move(this->scene.area_light_buffer), light_buffer = std::move(this->scene.light_buffer), volume_buffer = std::move(this->scene.volume_buffer), zero_volume_field_buffer = std::move(this->scene.zero_volume_field_buffer), texture_buffers = std::move(this->scene.texture_buffers), binding_buffer = std::move(this->scene.scene_binding_buffer), area_emission_shader = std::move(this->scene.area_emission_shader), output_image = std::move(this->renderer.output_image), depth_image = std::move(this->renderer.depth_image), shaders = std::move(this->renderer.shaders),
                                                           sphere_shader = std::move(this->renderer.sphere_shader), volume_shader = std::move(this->renderer.volume_shader)]() mutable {});
    }

} // namespace spectra
