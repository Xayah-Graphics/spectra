module spectra.render.rasterizer;

import spectra.runtime.shaders;
import std;
import vulkan;

namespace spectra {
    static_assert(sizeof(RasterPrimitive) == 32);
    static_assert(sizeof(RasterTransform) == 64);
    static_assert(sizeof(RasterMaterial) == 224);
    static_assert(sizeof(RasterTextureHeader) == 32);
    static_assert(sizeof(RasterTextureMapping) == 112);
    static_assert(sizeof(RasterConstantTexture) == 16);
    static_assert(sizeof(RasterImageTexture) == 48);
    static_assert(sizeof(RasterCompositeTexture) == 16);
    static_assert(sizeof(RasterDirectionMixTexture) == 32);
    static_assert(sizeof(RasterBilerpTexture) == 80);
    static_assert(sizeof(RasterAreaLight) == 32);
    static_assert(sizeof(RasterLight) == 176);
    static_assert(sizeof(RasterRayPrimitive) == 80);
    static_assert(sizeof(RasterSceneBindings) == 144);
    static_assert(sizeof(RasterVolume) == 272);

    namespace {
        constexpr std::uint32_t invalid_raster_index = std::numeric_limits<std::uint32_t>::max();
        constexpr float cie_y_integral               = 106.856895f;

        [[nodiscard]] math::Float3 raster_linear_srgb(const math::Float3 value, const scene::SpectrumColorSpace color_space) noexcept {
            if (color_space == scene::SpectrumColorSpace::Rec2020) return {1.660491f * value.x - 0.587641f * value.y - 0.072850f * value.z, -0.124550f * value.x + 1.132900f * value.y - 0.008349f * value.z, -0.018151f * value.x - 0.100579f * value.y + 1.118730f * value.z};
            if (color_space == scene::SpectrumColorSpace::Aces2065_1) return {2.521686f * value.x - 1.134130f * value.y - 0.387556f * value.z, -0.276479f * value.x + 1.372719f * value.y - 0.096240f * value.z, -0.015378f * value.x - 0.152975f * value.y + 1.168353f * value.z};
            return value;
        }

        [[nodiscard]] math::Float3 raster_spectrum_rgb(const scene::SpectrumParameter& parameter) {
            if (parameter.encoding == scene::SpectrumEncoding::RgbAlbedo || parameter.encoding == scene::SpectrumEncoding::RgbUnbounded || parameter.encoding == scene::SpectrumEncoding::RgbIlluminant) {
                math::Float3 rgb = raster_linear_srgb(parameter.value, parameter.color_space);
                if (parameter.encoding == scene::SpectrumEncoding::RgbIlluminant) rgb = rgb * cie_y_integral;
                return rgb;
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
            std::uint32_t maximum_stack_size{1};
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
                        result.metadata[0]                     = std::same_as<std::remove_cvref_t<decltype(data)>, scene::PlanarTextureMapping> ? 1u : std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphericalTextureMapping> ? 2u : 3u;
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
            result.metadata[0]        = 4;
            result.transform_row_0    = {transform[0], transform[1], transform[2], transform[3]};
            result.transform_row_1    = {transform[4], transform[5], transform[6], transform[7]};
            result.transform_row_2    = {transform[8], transform[9], transform[10], transform[11]};
            result.transform_row_3    = {transform[12], transform[13], transform[14], transform[15]};
            const std::uint32_t index = static_cast<std::uint32_t>(mappings.size());
            mappings.push_back(result);
            return index;
        }

        [[nodiscard]] RasterTextureCompilation compile_raster_textures(const Rasterizer& renderer, const scene::SceneView scene) {
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
            std::vector<std::uint32_t> stack_sizes(texture_count, 1);
            for (const std::uint32_t index : order) {
                std::uint32_t stack_size = 1;
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>)
                            stack_size = std::max(stack_sizes[raster_texture_source_index(scene, data.first)], 1u + stack_sizes[raster_texture_source_index(scene, data.second)]);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>)
                            stack_size = std::max({stack_sizes[raster_texture_source_index(scene, data.first)], 1u + stack_sizes[raster_texture_source_index(scene, data.second)], 2u + stack_sizes[raster_texture_source_index(scene, data.amount)]});
                    },
                    scene.resources.textures[index].data);
                stack_sizes[index]        = stack_size;
                result.maximum_stack_size = std::max(result.maximum_stack_size, stack_size);
            }
            if (result.maximum_stack_size > 32) throw std::runtime_error(std::format("Raster Texture program requires {} local values; the explicit Vulkan preview contract permits 32", result.maximum_stack_size));
            const auto handle = [&result, scene](const scene::TextureId id) {
                if (id.value == 0) return invalid_raster_index;
                return result.handles[raster_texture_source_index(scene, id)];
            };
            result.headers.reserve(texture_count);
            for (const std::uint32_t source_index : order) {
                const scene::Texture& texture = scene.resources.textures[source_index];
                std::uint32_t kind{};
                std::uint32_t local_index{};
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConstantTexture>) {
                            local_index              = static_cast<std::uint32_t>(result.constants.size());
                            const math::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? math::Float3{data.scalar, data.scalar, data.scalar} : raster_spectrum_rgb(data.spectrum);
                            result.constants.push_back(RasterConstantTexture{{value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalar : 1.0f}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                            kind                         = 1;
                            local_index                  = static_cast<std::uint32_t>(result.images.size());
                            const GpuTextureImage& image = renderer.context.gpu_scene.texture_image(texture, texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat);
                            result.images.push_back(RasterImageTexture{image.image_descriptor, image.sampler_descriptor, {compile_raster_mapping(result.mappings, data.mapping), static_cast<std::uint32_t>(data.channel), static_cast<std::uint32_t>(data.filter), data.invert ? 1u : 0u}, {data.scale, static_cast<float>(std::to_underlying(texture.color_space)), data.maximum_anisotropy, static_cast<float>(std::to_underlying(data.wrap))}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture>) {
                            kind        = 2;
                            local_index = static_cast<std::uint32_t>(result.checkerboards.size());
                            result.checkerboards.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), compile_raster_checkerboard_mapping(result.mappings, data.mapping), 0}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture>) {
                            kind        = 3;
                            local_index = static_cast<std::uint32_t>(result.scales.size());
                            result.scales.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), 0, 0}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                            kind        = 4;
                            local_index = static_cast<std::uint32_t>(result.mixes.size());
                            result.mixes.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), handle(data.amount), 0}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                            kind        = 5;
                            local_index = static_cast<std::uint32_t>(result.direction_mixes.size());
                            result.direction_mixes.push_back(RasterDirectionMixTexture{{handle(data.first), handle(data.second), 0, 0}, {data.direction.x, data.direction.y, data.direction.z, 0.0f}});
                        } else {
                            kind        = 6;
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
            std::function<void(std::uint32_t)> emit;
            emit = [&](const std::uint32_t source_index) {
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                            emit(raster_texture_source_index(scene, data.first));
                            emit(raster_texture_source_index(scene, data.second));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                            emit(raster_texture_source_index(scene, data.first));
                            emit(raster_texture_source_index(scene, data.second));
                            emit(raster_texture_source_index(scene, data.amount));
                        }
                    },
                    scene.resources.textures[source_index].data);
                program.push_back(result.headers[result.handles[source_index]]);
            };
            for (std::uint32_t texture = 0; texture != root_count; ++texture) {
                RasterTextureHeader& root = result.headers[texture];
                root.program[0]           = root_count + static_cast<std::uint32_t>(program.size());
                emit(order[texture]);
                root.program[1] = root_count + static_cast<std::uint32_t>(program.size()) - root.program[0];
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

        [[nodiscard]] std::vector<RasterMaterial> compile_raster_materials(const scene::SceneView scene, const RasterTextureCompilation& textures) {
            const auto texture_handle = [&textures, scene](const scene::TextureId id) {
                if (id.value == 0) return invalid_raster_index;
                return textures.handles[raster_texture_source_index(scene, id)];
            };
            const auto spectrum = [&texture_handle](std::array<float, 4>& value, std::array<std::uint32_t, 4>& data, const scene::SpectrumParameter& parameter) {
                const math::Float3 rgb = raster_spectrum_rgb(parameter);
                value                  = {rgb.x, rgb.y, rgb.z, 0.0f};
                data[0]                = texture_handle(parameter.texture);
            };
            const auto material_index = [scene](const scene::MaterialId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, id, &scene::Material::id) - scene.resources.materials.begin()); };
            std::vector<RasterMaterial> result{};
            result.reserve(scene.resources.materials.size());
            for (const scene::Material& material : scene.resources.materials) {
                RasterMaterial compiled{};
                compiled.metadata[1]        = invalid_raster_index;
                compiled.metadata[2]        = invalid_raster_index;
                compiled.spectrum_data_0[0] = invalid_raster_index;
                compiled.spectrum_data_1[0] = invalid_raster_index;
                compiled.spectrum_data_2[0] = invalid_raster_index;
                compiled.spectrum_data_3[0] = invalid_raster_index;
                compiled.scalar_textures_0.fill(invalid_raster_index);
                compiled.scalar_textures_1.fill(invalid_raster_index);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (requires {
                                          data.normal_map;
                                          data.bump_map;
                                      }) {
                            compiled.metadata[1] = texture_handle(data.normal_map);
                            compiled.metadata[2] = texture_handle(data.bump_map);
                        }
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>)
                            compiled.metadata[0] = 0;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseMaterialData>) {
                            compiled.metadata[0] = 1;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                            compiled.metadata[0] = 2;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                            spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.transmittance);
                            compiled.scalar_values_0[0] = data.scale;
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                            compiled.metadata[0] = 3;
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
                            compiled.metadata[0] = 4;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.eta);
                            const scene::FloatParameter& u = data.distribution.u_roughness.value_or(data.distribution.roughness);
                            const scene::FloatParameter& v = data.distribution.v_roughness.value_or(data.distribution.roughness);
                            compiled.scalar_values_0       = {u.value, v.value, 0.0f, 0.0f};
                            compiled.scalar_textures_0     = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                            compiled.metadata[0] = 5;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.eta);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                            compiled.metadata[0] = 6;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                            spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.eta);
                            spectrum(compiled.spectrum_value_2, compiled.spectrum_data_2, data.coating.albedo);
                            const scene::FloatParameter& u = data.interface.u_roughness.value_or(data.interface.roughness);
                            const scene::FloatParameter& v = data.interface.v_roughness.value_or(data.interface.roughness);
                            compiled.scalar_values_0       = {u.value, v.value, data.coating.thickness.value, data.coating.g.value};
                            compiled.scalar_textures_0     = {texture_handle(u.texture), texture_handle(v.texture), texture_handle(data.coating.thickness.texture), texture_handle(data.coating.g.texture)};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                            compiled.metadata[0] = 7;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.interface_eta);
                            std::visit(
                                [&](const auto& optics) {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.eta);
                                        spectrum(compiled.spectrum_value_2, compiled.spectrum_data_2, optics.k);
                                    } else {
                                        compiled.metadata[3] |= 2u;
                                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.reflectance);
                                    }
                                },
                                data.optics);
                            spectrum(compiled.spectrum_value_3, compiled.spectrum_data_3, data.coating.albedo);
                            const scene::FloatParameter& interface_u = data.interface.u_roughness.value_or(data.interface.roughness);
                            const scene::FloatParameter& interface_v = data.interface.v_roughness.value_or(data.interface.roughness);
                            const scene::FloatParameter& conductor_u = data.conductor.u_roughness.value_or(data.conductor.roughness);
                            const scene::FloatParameter& conductor_v = data.conductor.v_roughness.value_or(data.conductor.roughness);
                            compiled.scalar_values_0                 = {interface_u.value, interface_v.value, conductor_u.value, conductor_v.value};
                            compiled.scalar_textures_0               = {texture_handle(interface_u.texture), texture_handle(interface_v.texture), texture_handle(conductor_u.texture), texture_handle(conductor_v.texture)};
                            compiled.scalar_values_1                 = {data.coating.thickness.value, data.coating.g.value, 0.0f, 0.0f};
                            compiled.scalar_textures_1               = {texture_handle(data.coating.thickness.texture), texture_handle(data.coating.g.texture), invalid_raster_index, invalid_raster_index};
                        } else {
                            compiled.metadata[0]          = 8;
                            compiled.materials[0]         = material_index(data.first);
                            compiled.materials[1]         = material_index(data.second);
                            compiled.scalar_values_0[0]   = data.amount.value;
                            compiled.scalar_textures_0[0] = texture_handle(data.amount.texture);
                        }
                    },
                    material.data);
                result.push_back(compiled);
            }
            if (result.empty()) result.emplace_back();
            return result;
        }

        template <class Sample>
        [[nodiscard]] std::vector<float> build_raster_majorant(const math::UInt3 resolution, Sample&& sample) {
            std::vector<float> majorant(16u * 16u * 16u);
            for (std::uint32_t z = 0; z != 16; ++z)
                for (std::uint32_t y = 0; y != 16; ++y)
                    for (std::uint32_t x = 0; x != 16; ++x) {
                        const std::int32_t x0 = std::max(static_cast<std::int32_t>(std::floor(static_cast<float>(x) * resolution.x / 16.0f - 0.5f)), 0);
                        const std::int32_t y0 = std::max(static_cast<std::int32_t>(std::floor(static_cast<float>(y) * resolution.y / 16.0f - 0.5f)), 0);
                        const std::int32_t z0 = std::max(static_cast<std::int32_t>(std::floor(static_cast<float>(z) * resolution.z / 16.0f - 0.5f)), 0);
                        const std::int32_t x1 = std::min(static_cast<std::int32_t>(std::floor(static_cast<float>(x + 1u) * resolution.x / 16.0f - 0.5f)) + 1, static_cast<std::int32_t>(resolution.x) - 1);
                        const std::int32_t y1 = std::min(static_cast<std::int32_t>(std::floor(static_cast<float>(y + 1u) * resolution.y / 16.0f - 0.5f)) + 1, static_cast<std::int32_t>(resolution.y) - 1);
                        const std::int32_t z1 = std::min(static_cast<std::int32_t>(std::floor(static_cast<float>(z + 1u) * resolution.z / 16.0f - 0.5f)) + 1, static_cast<std::int32_t>(resolution.z) - 1);
                        float maximum{};
                        for (std::int32_t grid_z = z0; grid_z <= z1; ++grid_z)
                            for (std::int32_t grid_y = y0; grid_y <= y1; ++grid_y)
                                for (std::int32_t grid_x = x0; grid_x <= x1; ++grid_x) maximum = std::max(maximum, sample((static_cast<std::size_t>(grid_z) * resolution.y + static_cast<std::size_t>(grid_y)) * resolution.x + static_cast<std::size_t>(grid_x)));
                        majorant[(z * 16u + y) * 16u + x] = maximum;
                    }
            return majorant;
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


        [[nodiscard]] float raster_emission_texture_luminance(const scene::SceneView scene, const scene::TextureId id) {
            if (id.value == 0) return 1.0f;
            const scene::Texture& texture    = *std::ranges::find(scene.resources.textures, id, &scene::Texture::id);
            const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
            if (image.invert) throw std::runtime_error(std::format("Light Texture {} cannot use inversion", texture.name));
            math::Float3 luminance{};
            for (std::size_t index = 0; index != static_cast<std::size_t>(image.width) * image.height; ++index) luminance = luminance + raster_linear_srgb({image.texels[index].x, image.texels[index].y, image.texels[index].z}, texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb);
            luminance = luminance / static_cast<float>(image.width * image.height) * image.scale;
            return 0.2126f * luminance.x + 0.7152f * luminance.y + 0.0722f * luminance.z;
        }

        void set_raster_light_transform(RasterLight& light, const math::Transform& transform) noexcept {
            light.transform_row_0 = {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]};
            light.transform_row_1 = {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]};
            light.transform_row_2 = {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]};
        }

        [[nodiscard]] std::vector<RasterLight> compile_raster_lights(const Rasterizer& renderer, const scene::SceneView scene, const RasterTextureCompilation& textures, std::vector<RasterAreaLight>& surface_lights, std::vector<RasterDynamicAreaLightRange>& dynamic_area_lights) {
            std::vector<RasterLight> lights{};
            const float scene_radius  = scene.bounds().radius();
            const auto texture_handle = [&](const scene::TextureId id) { return id.value == 0 ? invalid_raster_index : textures.handles[raster_texture_source_index(scene, id)]; };
            surface_lights.reserve(renderer.context.gpu_scene.view().primitives.size());
            for (const scene::Light& source : scene.resources.lights)
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                            return;
                        else {
                            const scene::SpectrumParameter& spectrum = [&]() -> const scene::SpectrumParameter& {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>)
                                    return data.intensity;
                                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>)
                                    return data.radiance;
                                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                                    return data.environment.radiance;
                                else
                                    return data.radiance;
                            }();
                            const float scale = [&]() {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                                    return data.environment.scale;
                                else
                                    return data.scale;
                            }();
                            const math::Float3 radiance     = raster_spectrum_rgb(spectrum) * scale;
                            const float selection_luminance = 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) {
                                if (data.environment.emission_texture.value == 0) throw std::runtime_error("Portal Infinite Light requires an Environment Image Texture");
                                const math::Transform inverse = data.environment.transform.inverse();
                                for (const std::array<math::Float3, 4>& portal : data.portals) {
                                    RasterLight light{};
                                    const math::Float3 edge_0 = portal[1] - portal[0];
                                    const math::Float3 edge_1 = portal[3] - portal[0];
                                    light.metadata            = {8, 1, texture_handle(data.environment.emission_texture), 0};
                                    light.radiance            = {radiance.x, radiance.y, radiance.z, selection_luminance * raster_emission_texture_luminance(scene, data.environment.emission_texture) * edge_0.cross(edge_1).length()};
                                    light.position            = {portal[0].x, portal[0].y, portal[0].z, 0.0f};
                                    light.direction           = {edge_0.x, edge_0.y, edge_0.z, 0.0f};
                                    light.parameters          = {edge_1.x, edge_1.y, edge_1.z, edge_0.cross(edge_1).length()};
                                    set_raster_light_transform(light, inverse);
                                    lights.push_back(light);
                                }
                            } else {
                                RasterLight light{};
                                const float texture_luminance = [&]() {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InfiniteLight>)
                                        return raster_emission_texture_luminance(scene, data.emission_texture);
                                    else
                                        return 1.0f;
                                }();
                                light.radiance = {radiance.x, radiance.y, radiance.z, selection_luminance * texture_luminance};
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) {
                                    const math::Float3 position  = data.transform.transform_point({});
                                    light.metadata[0]            = std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> ? 0u : 1u;
                                    light.position               = {position.x, position.y, position.z, 1.0f};
                                    const math::Float3 direction = -data.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                                    light.direction              = {direction.x, direction.y, direction.z, 0.0f};
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) light.parameters = {std::cos(data.cone_angle * std::numbers::pi_v<float> / 180.0f), std::cos((data.cone_angle - data.cone_delta) * std::numbers::pi_v<float> / 180.0f), 0.0f, 0.0f};
                                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>) {
                                    light.metadata[0]            = 2;
                                    const math::Float3 direction = -data.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                                    light.direction              = {direction.x, direction.y, direction.z, 0.0f};
                                } else {
                                    light.metadata = {7, 0, texture_handle(data.emission_texture), 0};
                                    set_raster_light_transform(light, data.transform.inverse());
                                }
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight>)
                                    light.radiance[3] *= 4.0f * std::numbers::pi_v<float>;
                                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>)
                                    light.radiance[3] *= 2.0f * std::numbers::pi_v<float> * (1.0f - light.parameters[1] + (light.parameters[1] - light.parameters[0]) * 0.5f);
                                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>)
                                    light.radiance[3] *= std::numbers::pi_v<float> * scene_radius * scene_radius;
                                else
                                    light.radiance[3] *= 4.0f * std::numbers::pi_v<float> * std::numbers::pi_v<float> * scene_radius * scene_radius;
                                lights.push_back(light);
                            }
                        }
                    },
                    source.data);

            for (const GpuScenePrimitive& gpu_primitive : renderer.context.gpu_scene.view().primitives) {
                surface_lights.emplace_back();
                const scene::Instance& instance   = scene.resources.instances[gpu_primitive.scene_instance_index];
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
                if (primitive.area_light.value == 0) continue;
                const scene::DiffuseAreaLight& source = std::get<scene::DiffuseAreaLight>(std::ranges::find(scene.resources.lights, primitive.area_light, &scene::Light::id)->data);
                const math::Transform transform       = instance.transform * primitive.transform;
                const math::Float3 radiance          = raster_spectrum_rgb(source.radiance);
                const float selection_luminance      = 0.2126f * radiance.x + 0.7152f * radiance.y + 0.0722f * radiance.z;
                const float texture_luminance        = raster_emission_texture_luminance(scene, source.emission_texture);
                const std::uint32_t flags            = (source.sidedness == scene::EmissionSidedness::Both ? 1u : 0u) | (primitive.reverse_orientation ? 2u : 0u);
                const std::uint32_t emission_texture = texture_handle(source.emission_texture);
                surface_lights.back()                = RasterAreaLight{{radiance.x * source.scale, radiance.y * source.scale, radiance.z * source.scale, 0.0f}, {emission_texture, flags, 0, 0}};
                const std::uint32_t first_light      = static_cast<std::uint32_t>(lights.size());
                std::uint32_t capacity{};
                std::uint32_t range_kind{};
                std::uint32_t light_kind{};
                std::uint32_t attribute_mask{};
                std::array<float, 4> geometry_parameters{};
                if (gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet) {
                    const GpuSphereSet& spheres = renderer.context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                    capacity                    = spheres.sphere_capacity;
                    range_kind                  = 1;
                    light_kind                  = 4;
                    for (std::uint32_t index = 0; index != capacity; ++index) {
                        RasterLight light{};
                        light.metadata   = {4, flags, emission_texture, 3};
                        light.radiance   = {radiance.x * source.scale, radiance.y * source.scale, radiance.z * source.scale, 1.0f};
                        light.parameters = {1.0f, -1.0f, 1.0f, 2.0f * std::numbers::pi_v<float>};
                        light.selection[1] = 1.0f;
                        set_raster_light_transform(light, transform);
                        lights.push_back(light);
                    }
                } else {
                    const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
                    const GpuGeometry& gpu_geometry = renderer.context.gpu_scene.view().geometries[gpu_primitive.resource_index];
                    attribute_mask                 = gpu_geometry.attribute_mask;
                    if (const scene::SphereGeometry* sphere = std::get_if<scene::SphereGeometry>(&geometry.data)) {
                        capacity            = 1;
                        range_kind          = 2;
                        light_kind          = 4;
                        geometry_parameters = {sphere->radius, sphere->z_min, sphere->z_max, sphere->phi_max * std::numbers::pi_v<float> / 180.0f};
                    } else if (const scene::DiskGeometry* disk = std::get_if<scene::DiskGeometry>(&geometry.data)) {
                        capacity            = 1;
                        range_kind          = 2;
                        light_kind          = 5;
                        geometry_parameters = {disk->height, disk->radius, disk->inner_radius, disk->phi_max * std::numbers::pi_v<float> / 180.0f};
                    } else if (const scene::CylinderGeometry* cylinder = std::get_if<scene::CylinderGeometry>(&geometry.data)) {
                        capacity            = 1;
                        range_kind          = 2;
                        light_kind          = 6;
                        geometry_parameters = {cylinder->radius, cylinder->z_min, cylinder->z_max, cylinder->phi_max * std::numbers::pi_v<float> / 180.0f};
                    } else {
                        capacity   = gpu_geometry.index_capacity / 3u;
                        range_kind = 0;
                        light_kind = 3;
                    }
                    for (std::uint32_t element = 0; element != capacity; ++element) {
                        RasterLight light{};
                        light.metadata   = {light_kind, flags, emission_texture, 3};
                        light.radiance   = {radiance.x * source.scale, radiance.y * source.scale, radiance.z * source.scale, 1.0f};
                        light.parameters = range_kind == 0 ? std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f} : geometry_parameters;
                        light.selection[1] = 1.0f;
                        set_raster_light_transform(light, transform);
                        lights.push_back(light);
                    }
                }
                dynamic_area_lights.push_back({gpu_primitive.scene_primitive_index, gpu_primitive.resource_index, first_light, capacity, range_kind, light_kind, flags, emission_texture, attribute_mask, geometry_parameters, {source.scale, source.power.value_or(-1.0f), source.sidedness == scene::EmissionSidedness::Both ? 2.0f : 1.0f, texture_luminance}, {radiance.x, radiance.y, radiance.z, selection_luminance}});
            }
            std::erase_if(lights, [](const RasterLight& light) { return light.radiance[3] == 0.0f; });
            float total_weight{};
            for (const RasterLight& light : lights) total_weight += light.radiance[3];
            float cumulative_weight{};
            for (RasterLight& light : lights) {
                cumulative_weight += light.radiance[3];
                light.selection[0] = cumulative_weight / total_weight;
                light.selection[1] = light.radiance[3];
                light.radiance[3]  = 0.0f;
            }
            if (!lights.empty()) lights.back().selection[0] = 1.0f;
            return lights;
        }

        [[nodiscard]] std::vector<RasterRayPrimitive> compile_raster_ray_primitives(const Rasterizer& renderer, const scene::SceneView scene, const RasterTextureCompilation& textures) {
            std::vector<RasterRayPrimitive> result{};
            result.reserve(renderer.context.gpu_scene.view().acceleration_primitive_indices.size());
            for (const std::uint32_t scene_primitive_index : renderer.context.gpu_scene.view().acceleration_primitive_indices) {
                const GpuScenePrimitive& gpu_primitive = renderer.context.gpu_scene.view().primitives[scene_primitive_index];
                const scene::Instance& instance = scene.resources.instances[gpu_primitive.scene_instance_index];
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
                RasterRayPrimitive record{};
                record.metadata[1] = primitive.alpha.value == 0 ? invalid_raster_index : textures.handles[raster_texture_source_index(scene, primitive.alpha)];
                record.metadata[3] = primitive.reverse_orientation ? 1u : 0u;
                record.scene_primitive_index = gpu_primitive.scene_primitive_index;
                if (gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet) {
                    const GpuSphereSet& spheres = renderer.context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                    record.positions = spheres.positions_descriptor;
                    record.radii = spheres.radii_descriptor;
                    record.metadata[0] = 4;
                } else {
                    const GpuGeometry& gpu_geometry = renderer.context.gpu_scene.view().geometries[gpu_primitive.resource_index];
                    const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
                    record.positions = gpu_geometry.positions_descriptor;
                    record.texture_coordinates = gpu_geometry.texture_coordinates_descriptor;
                    record.indices = gpu_geometry.indices_descriptor;
                    record.metadata[2] = gpu_geometry.attribute_mask;
                    std::visit(
                        [&](const auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphereGeometry>) {
                                record.metadata[0] = 1;
                                record.parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiskGeometry>) {
                                record.metadata[0] = 2;
                                record.parameters = {data.height, data.radius, data.inner_radius, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CylinderGeometry>) {
                                record.metadata[0] = 3;
                                record.parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                            }
                        },
                        geometry.data);
                }
                result.push_back(record);
            }
            return result;
        }

    } // namespace

} // namespace spectra

namespace spectra {
    void Rasterizer::initialize_scene(const scene::SceneView scene) {
        if (scene.camera.medium.value != 0) throw std::runtime_error("Rasterizer does not support camera media; use Path Tracer");
        for (const scene::Instance& instance : scene.resources.instances) {
            if (!instance.visible) continue;
            const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            for (const scene::Primitive& primitive : prototype.primitives)
                for (const scene::MediumId medium_id : {primitive.media.inside, primitive.media.outside}) {
                    if (medium_id.value == 0) continue;
                    const scene::Medium& medium = *std::ranges::find(scene.resources.media, medium_id, &scene::Medium::id);
                    if (std::holds_alternative<scene::HomogeneousMedium>(medium.data)) throw std::runtime_error("Rasterizer does not support homogeneous boundary media; use Path Tracer");
                }
        }
        this->scene.zero_volume_field_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->scene.uploaded_revision            = scene.revision;
        this->renderer.camera                    = scene.camera;
        this->renderer.sampler                   = scene.sampler;
        this->renderer.film_exposure             = scene.film.exposure;
        this->renderer.film_resolution           = scene.film.resolution;
        this->renderer.film_pixel_minimum        = scene.film.pixel_minimum;
        this->renderer.film_pixel_maximum        = scene.film.pixel_maximum;
        const std::vector<std::uint32_t> code = load_spirv(this->context.shader_directory / "volume_majorant.spv");
        this->scene.volume_majorant_shader    = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), "build_majorant"}};
        const auto dynamic_shader = [this](const std::string_view file, const char* entry) {
            const std::vector<std::uint32_t> shader_code = load_spirv(this->context.shader_directory / file);
            return vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, shader_code.size() * sizeof(std::uint32_t), shader_code.data(), entry}};
        };
        this->scene.dynamic_light_shapes_shader    = dynamic_shader("raster_dynamic_area_light_shapes.spv", "update_raster_dynamic_area_light_shapes");
        this->scene.dynamic_light_finalize_shader  = dynamic_shader("raster_dynamic_area_light_finalize.spv", "finalize_raster_dynamic_area_light_range");
        this->scene.dynamic_light_selection_shader = dynamic_shader("raster_dynamic_light_selection.spv", "rebuild_raster_dynamic_light_selection");
        const std::array<math::Float3, 1> zero{};
        this->context.runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            this->scene.zero_volume_field_buffer = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const math::Float3>{zero}, vk::BufferUsageFlagBits::eStorageBuffer);
            this->context.runtime.resources.write_buffer_descriptor(this->scene.zero_volume_field_descriptor, vk::DescriptorType::eStorageBuffer, this->scene.zero_volume_field_buffer);
            this->upload_scene(scene, command_buffer);
        });
    }

    void Rasterizer::upload_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        RasterTextureCompilation textures     = compile_raster_textures(*this, scene);
        const std::uint32_t texture_count      = static_cast<std::uint32_t>(scene.resources.textures.size());
        const std::uint32_t texture_stack_size = textures.maximum_stack_size;
        std::vector<RasterMaterial> materials = compile_raster_materials(scene, textures);

        std::vector<RasterAreaLight> area_lights{};
        std::vector<RasterDynamicAreaLightRange> dynamic_area_lights{};
        std::vector<RasterLight> lights = compile_raster_lights(*this, scene, textures, area_lights, dynamic_area_lights);
        if (area_lights.empty()) area_lights.emplace_back();

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
            if (primitive.area_light.value != 0) {
                area_light = static_cast<std::uint32_t>(primitives.size());
            }
            const std::uint32_t transform_index = static_cast<std::uint32_t>(primitives.size());
            const std::uint32_t alpha_texture   = primitive.alpha.value == 0 ? invalid_raster_index : textures.handles[raster_texture_source_index(scene, primitive.alpha)];
            primitives.push_back(RasterPrimitive{transform_index, static_cast<std::uint32_t>(material - scene.resources.materials.begin()), area_light, primitive.reverse_orientation ? 1u : 0u, face_material_offset, static_cast<std::uint32_t>(sphere_draw ? 0 : primitive.face_materials.size()), alpha_texture, 0});
        }
        if (primitives.empty()) {
            primitives.emplace_back();
        }
        if (face_materials.empty()) face_materials.emplace_back();

        GpuBuffer new_primitives         = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterPrimitive>{primitives}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_materials          = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterMaterial>{materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_face_materials     = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const std::uint32_t>{face_materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_area_lights        = upload_raster_buffer(this->context.runtime, command_buffer, std::span<const RasterAreaLight>{area_lights}, vk::BufferUsageFlagBits::eStorageBuffer);
        const auto upload_texture_buffer = [this, &command_buffer]<typename Element>(const std::span<const Element> elements) { return upload_raster_buffer(this->context.runtime, command_buffer, elements, vk::BufferUsageFlagBits::eStorageBuffer); };
        std::vector<RasterVolume> raster_volumes{};
        std::vector<VolumeResources> new_volume_gpu_data{};
        raster_volumes.reserve(scene.resources.volumes.size());
        new_volume_gpu_data.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) {
            const GpuVolume& shared = *std::ranges::find(this->context.gpu_scene.view().volumes, volume.id, &GpuVolume::volume_id);
            const scene::VolumeMedium* medium{};
            for (const scene::Medium& candidate : scene.resources.media) {
                const scene::VolumeMedium* candidate_volume = std::get_if<scene::VolumeMedium>(&candidate.data);
                if (candidate_volume && candidate_volume->volume == volume.id) {
                    medium = candidate_volume;
                    break;
                }
            }
            RasterVolume record{};
            record.bounds_minimum          = {volume.bounds.minimum.x, volume.bounds.minimum.y, volume.bounds.minimum.z, 0.0f};
            record.bounds_maximum          = {volume.bounds.maximum.x, volume.bounds.maximum.y, volume.bounds.maximum.z, 0.0f};
            const math::Transform inverse  = volume.transform.inverse();
            record.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            record.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            record.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
            if (medium) {
                const math::Float3 sigma_a  = raster_spectrum_rgb(medium->sigma_a);
                const math::Float3 sigma_s  = raster_spectrum_rgb(medium->sigma_s);
                const math::Float3 emission = raster_spectrum_rgb(medium->emission);
                record.sigma_a              = {sigma_a.x, sigma_a.y, sigma_a.z, 0.0f};
                record.sigma_s              = {sigma_s.x, sigma_s.y, sigma_s.z, 0.0f};
                record.emission             = {emission.x, emission.y, emission.z, 0.0f};
                record.scales               = {medium->density_scale, medium->emission_scale, medium->anisotropy, medium->temperature_scale};
                record.temperature          = {medium->temperature_offset, medium->minimum_emission_temperature, medium->blackbody_emission ? 1.0f : 0.0f, 0.0f};
            }
            VolumeResources gpu_data{};
            gpu_data.volume_id           = volume.id;
            gpu_data.revision            = shared.revision;
            gpu_data.majorant_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
            std::vector<float> majorant(16u * 16u * 16u);
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                        gpu_data.resolution      = data.resolution;
                        record.metadata          = {0, data.resolution.x, data.resolution.y, data.resolution.z};
                        record.majorant_metadata = {16, 16, 16, (data.temperature.empty() ? 0u : 1u) | (data.emission_scale.empty() ? 0u : 2u)};
                        majorant                 = build_raster_majorant(data.resolution, [&data](const std::size_t index) { return data.density[index]; });
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                        gpu_data.resolution      = data.resolution;
                        record.metadata          = {1, data.resolution.x, data.resolution.y, data.resolution.z};
                        record.majorant_metadata = {16, 16, 16, (data.sigma_a.empty() ? 0u : 1u) | (data.sigma_s.empty() ? 0u : 2u) | (data.emission.empty() ? 0u : 4u) | (std::to_underlying(data.color_space) << 8)};
                        majorant                 = build_raster_majorant(data.resolution, [&data](const std::size_t index) {
                            const math::Float3 absorption = data.sigma_a.empty() ? math::Float3{1.0f, 1.0f, 1.0f} : data.sigma_a[index];
                            const math::Float3 scattering = data.sigma_s.empty() ? math::Float3{1.0f, 1.0f, 1.0f} : data.sigma_s[index];
                            return std::max({absorption.x + scattering.x, absorption.y + scattering.y, absorption.z + scattering.z});
                        });
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ProceduralCloudVolume>) {
                        record.metadata          = {2, 0, 0, 0};
                        record.majorant_metadata = {16, 16, 16, 0};
                        record.procedural_parameters = {data.density, data.wispiness, data.frequency, 0.0f};
                        std::ranges::fill(majorant, 1.0f);
                    } else
                        throw std::runtime_error("Rasterizer requires dense or procedural Volume data; NanoVDB is currently a Path-only resource");
                },
                volume.data);
            gpu_data.majorant = upload_texture_buffer(std::span<const float>{majorant});
            this->context.runtime.resources.write_buffer_descriptor(gpu_data.majorant_descriptor, vk::DescriptorType::eStorageBuffer, gpu_data.majorant);
            const auto field = [this, &shared](const GpuVolumeField field) -> DescriptorHandle {
                const std::size_t index = std::to_underlying(field);
                return shared.field_present[index] ? shared.descriptors[index] : this->scene.zero_volume_field_descriptor;
            };
            record.density              = field(GpuVolumeField::Density);
            record.temperature_field    = field(GpuVolumeField::Temperature);
            record.sigma_a_field        = field(GpuVolumeField::SigmaA);
            record.sigma_s_field        = field(GpuVolumeField::SigmaS);
            record.emission_scale_field = field(GpuVolumeField::EmissionScale);
            record.emission_field       = field(GpuVolumeField::Emission);
            record.majorant             = gpu_data.majorant_descriptor;
            raster_volumes.push_back(record);
            new_volume_gpu_data.push_back(std::move(gpu_data));
        }
        const std::uint32_t volume_count = static_cast<std::uint32_t>(raster_volumes.size());
        if (raster_volumes.empty()) raster_volumes.emplace_back();

        const std::uint32_t light_count = static_cast<std::uint32_t>(lights.size());
        if (lights.empty()) lights.emplace_back();
        std::vector<RasterRayPrimitive> ray_primitives = compile_raster_ray_primitives(*this, scene, textures);
        if (ray_primitives.empty()) ray_primitives.emplace_back();
        GpuBuffer new_volume_buffer        = upload_texture_buffer(std::span<const RasterVolume>{raster_volumes});
        GpuBuffer new_light_buffer         = upload_texture_buffer(std::span<const RasterLight>{lights});
        GpuBuffer new_ray_primitive_buffer = upload_texture_buffer(std::span<const RasterRayPrimitive>{ray_primitives});
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
        DescriptorLease primitives_descriptor     = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease material_descriptor       = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease face_material_descriptor  = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease area_light_descriptor     = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease volumes_descriptor        = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease light_descriptor          = this->context.runtime.resources.allocate_resource_descriptor();
        DescriptorLease ray_primitives_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
        this->context.runtime.resources.write_buffer_descriptor(material_descriptor, vk::DescriptorType::eStorageBuffer, new_materials);
        this->context.runtime.resources.write_buffer_descriptor(face_material_descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
        this->context.runtime.resources.write_buffer_descriptor(area_light_descriptor, vk::DescriptorType::eStorageBuffer, new_area_lights);
        this->context.runtime.resources.write_buffer_descriptor(volumes_descriptor, vk::DescriptorType::eStorageBuffer, new_volume_buffer);
        this->context.runtime.resources.write_buffer_descriptor(light_descriptor, vk::DescriptorType::eStorageBuffer, new_light_buffer);
        this->context.runtime.resources.write_buffer_descriptor(ray_primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_ray_primitive_buffer);
        std::array<DescriptorLease, 9> texture_descriptors{};
        for (std::size_t index = 0; index != texture_descriptors.size(); ++index) {
            texture_descriptors[index] = this->context.runtime.resources.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_texture_buffers[index]);
        }
        const RasterSceneBindings bindings{primitives_descriptor, this->context.gpu_scene.view().primitive_transforms, material_descriptor, face_material_descriptor, area_light_descriptor, texture_descriptors[0], texture_descriptors[1], texture_descriptors[2], texture_descriptors[3], texture_descriptors[4], texture_descriptors[5], texture_descriptors[6], texture_descriptors[7], texture_descriptors[8], light_descriptor, ray_primitives_descriptor, {texture_count, texture_stack_size, light_count, 0}};
        new_binding_buffer = upload_texture_buffer(std::span{&bindings, 1});
        DescriptorLease bindings_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(bindings_descriptor, vk::DescriptorType::eStorageBuffer, new_binding_buffer);
        this->context.runtime.frames.defer_destruction([primitives = std::move(this->scene.primitive_buffer), materials = std::move(this->scene.material_buffer), face_materials = std::move(this->scene.face_material_buffer), area_lights = std::move(this->scene.area_light_buffer)]() mutable {});
        this->context.runtime.frames.defer_destruction([texture_buffers = std::move(this->scene.texture_buffers), bindings = std::move(this->scene.scene_binding_buffer)]() mutable {});
        this->scene.primitives_descriptor     = std::move(primitives_descriptor);
        this->scene.material_descriptor       = std::move(material_descriptor);
        this->scene.face_material_descriptor  = std::move(face_material_descriptor);
        this->scene.area_light_descriptor     = std::move(area_light_descriptor);
        this->scene.volumes_descriptor        = std::move(volumes_descriptor);
        this->scene.light_descriptor          = std::move(light_descriptor);
        this->scene.ray_primitives_descriptor = std::move(ray_primitives_descriptor);
        this->scene.texture_descriptors       = std::move(texture_descriptors);
        this->scene.bindings_descriptor       = std::move(bindings_descriptor);
        this->scene.primitive_buffer     = std::move(new_primitives);
        this->scene.material_buffer      = std::move(new_materials);
        this->scene.face_material_buffer = std::move(new_face_materials);
        this->scene.area_light_buffer    = std::move(new_area_lights);
        this->context.runtime.frames.defer_destruction([volumes = std::move(this->scene.volume_resources), records = std::move(this->scene.volume_buffer), lights = std::move(this->scene.light_buffer), ray_primitives = std::move(this->scene.ray_primitive_buffer)]() mutable {});
        this->scene.volume_resources     = std::move(new_volume_gpu_data);
        this->scene.volume_buffer        = std::move(new_volume_buffer);
        this->scene.light_buffer         = std::move(new_light_buffer);
        this->scene.ray_primitive_buffer = std::move(new_ray_primitive_buffer);
        this->scene.texture_buffers      = std::move(new_texture_buffers);
        this->scene.scene_binding_buffer = std::move(new_binding_buffer);
        this->scene.texture_count        = texture_count;
        this->scene.texture_stack_size   = texture_stack_size;
        this->scene.volume_count         = volume_count;
        this->scene.light_count          = light_count;
        this->scene.dynamic_area_lights  = std::move(dynamic_area_lights);
        this->update_dynamic_lights(command_buffer);
    }

    void Rasterizer::update_volumes(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.resources.volumes.size() != this->scene.volume_resources.size()) {
            this->upload_scene(scene, command_buffer);
            return;
        }
        for (std::size_t index = 0; index != scene.resources.volumes.size(); ++index) {
            const scene::Volume& source = scene.resources.volumes[index];
            VolumeResources& volume     = this->scene.volume_resources[index];
            const GpuVolume& shared     = this->context.gpu_scene.view().volumes[index];
            if (source.id != volume.volume_id || shared.volume_id != volume.volume_id) {
                this->upload_scene(scene, command_buffer);
                return;
            }
            if (shared.revision.content == volume.revision.content) continue;
            if (shared.revision.topology != volume.revision.topology || !shared.dirty_region) {
                this->upload_scene(scene, command_buffer);
                return;
            }
            const auto descriptor = [this, &shared](const GpuVolumeField field) -> DescriptorHandle {
                const std::size_t field_index = std::to_underlying(field);
                return shared.field_present[field_index] ? shared.descriptors[field_index] : this->scene.zero_volume_field_descriptor;
            };
            DescriptorHandle density = descriptor(GpuVolumeField::Density);
            DescriptorHandle sigma_a = descriptor(GpuVolumeField::SigmaA);
            DescriptorHandle sigma_s = descriptor(GpuVolumeField::SigmaS);
            std::uint32_t majorant_mode{};
            std::uint32_t majorant_flags{};
            const math::UInt3 resolution = shared.resolution;
            if (std::holds_alternative<scene::RgbGridVolume>(source.data)) {
                const scene::RgbGridVolume& data = std::get<scene::RgbGridVolume>(source.data);
                majorant_mode = 1;
                if (shared.field_present[std::to_underlying(GpuVolumeField::SigmaA)]) majorant_flags |= 1u;
                if (shared.field_present[std::to_underlying(GpuVolumeField::SigmaS)]) majorant_flags |= 2u;
                if (shared.field_present[std::to_underlying(GpuVolumeField::Emission)]) majorant_flags |= 4u;
                majorant_flags |= std::to_underlying(data.color_space) << 8;
            } else if (!std::holds_alternative<scene::DensityGridVolume>(source.data)) {
                this->upload_scene(scene, command_buffer);
                return;
            }
            const scene::VolumeRegion dirty_region = shared.revision.content == volume.revision.content + 1u ? *shared.dirty_region : scene::VolumeRegion{{}, resolution};
            const scene::VolumeRegion expanded{{dirty_region.minimum.x > 0 ? dirty_region.minimum.x - 1 : 0, dirty_region.minimum.y > 0 ? dirty_region.minimum.y - 1 : 0, dirty_region.minimum.z > 0 ? dirty_region.minimum.z - 1 : 0}, {std::min(resolution.x, dirty_region.maximum.x + 1), std::min(resolution.y, dirty_region.maximum.y + 1), std::min(resolution.z, dirty_region.maximum.z + 1)}};
            const std::array<std::uint32_t, 4> brick_minimum{expanded.minimum.x * 16u / resolution.x, expanded.minimum.y * 16u / resolution.y, expanded.minimum.z * 16u / resolution.z, 0};
            const std::array<std::uint32_t, 4> brick_maximum{std::min(16u, (expanded.maximum.x * 16u + resolution.x - 1u) / resolution.x), std::min(16u, (expanded.maximum.y * 16u + resolution.y - 1u) / resolution.y), std::min(16u, (expanded.maximum.z * 16u + resolution.z - 1u) / resolution.z), 0};
            struct alignas(16) MajorantPushData {
                DescriptorHandle density;
                DescriptorHandle sigma_a;
                DescriptorHandle sigma_s;
                DescriptorHandle majorant;
                std::array<std::uint32_t, 4> resolution;
                std::array<std::uint32_t, 4> brick_minimum;
                std::array<std::uint32_t, 4> brick_maximum;
                std::array<std::uint32_t, 4> metadata;
            };
            static_assert(sizeof(MajorantPushData) == 96);
            const MajorantPushData push_data{density, sigma_a, sigma_s, volume.majorant_descriptor, {resolution.x, resolution.y, resolution.z, 0}, brick_minimum, brick_maximum, {majorant_mode, majorant_flags, 0, 0}};
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->scene.volume_majorant_shader);
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            const std::uint32_t brick_count = (brick_maximum[0] - brick_minimum[0]) * (brick_maximum[1] - brick_minimum[1]) * (brick_maximum[2] - brick_minimum[2]);
            command_buffer.dispatch((brick_count + 63u) / 64u, 1, 1);
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
            volume.revision = shared.revision;
        }
    }

    void Rasterizer::update_dynamic_lights(const vk::raii::CommandBuffer& command_buffer) {
        if (this->scene.dynamic_area_lights.empty()) return;
        struct alignas(16) DynamicAreaLightPushData {
            DescriptorHandle positions{};
            DescriptorHandle indices{};
            DescriptorHandle radii{};
            DescriptorHandle texture_coordinates{};
            DescriptorHandle transforms{};
            DescriptorHandle lights{};
            DescriptorHandle area_lights{};
            DescriptorHandle reserved{};
            std::array<std::uint32_t, 4> range{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<float, 4> geometry_parameters{};
            std::array<float, 4> emission_parameters{};
            std::array<float, 4> source_radiance{};
        };
        static_assert(sizeof(DynamicAreaLightPushData) == 144);
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        const auto push_for_range = [&](const RasterDynamicAreaLightRange& range) {
            DynamicAreaLightPushData push{};
            push.transforms          = gpu_scene.primitive_transforms;
            push.lights              = this->scene.light_descriptor;
            push.area_lights         = this->scene.area_light_descriptor;
            push.geometry_parameters = range.geometry_parameters;
            push.emission_parameters = range.emission_parameters;
            push.source_radiance     = range.radiance;
            std::uint32_t active_count{1};
            if (range.kind == 0) {
                const GpuGeometry& geometry = gpu_scene.geometries[range.resource_index];
                push.positions             = geometry.positions_descriptor;
                push.indices               = geometry.indices_descriptor;
                push.texture_coordinates   = geometry.texture_coordinates_descriptor;
                active_count               = geometry.index_count / 3u;
            } else if (range.kind == 1) {
                const GpuSphereSet& spheres = gpu_scene.sphere_sets[range.resource_index];
                push.positions             = spheres.positions_descriptor;
                push.radii                 = spheres.radii_descriptor;
                active_count               = spheres.sphere_count;
            }
            push.range    = {range.scene_primitive_index, range.first_light, active_count, range.capacity};
            push.metadata = {range.kind | (range.attribute_mask << 8), range.light_kind, range.flags, range.emission_texture};
            return push;
        };
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->scene.dynamic_light_shapes_shader);
        for (const RasterDynamicAreaLightRange& range : this->scene.dynamic_area_lights) {
            const DynamicAreaLightPushData push = push_for_range(range);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch((range.capacity + 63u) / 64u, 1, 1);
        }
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->scene.dynamic_light_finalize_shader);
        for (const RasterDynamicAreaLightRange& range : this->scene.dynamic_area_lights) {
            const DynamicAreaLightPushData push = push_for_range(range);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch(1, 1, 1);
        }
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        struct alignas(16) DynamicSelectionPushData {
            DescriptorHandle lights{};
            std::uint32_t light_count{};
            std::uint32_t reserved{};
        };
        static_assert(sizeof(DynamicSelectionPushData) == 16);
        const DynamicSelectionPushData selection_push{this->scene.light_descriptor, this->scene.light_count};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->scene.dynamic_light_selection_shader);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&selection_push, 1}));
        command_buffer.dispatch(1, 1, 1);
        const vk::MemoryBarrier2 render_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &render_dependency});
    }

    void Rasterizer::synchronize_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.number == this->scene.uploaded_revision.number) return;
        bool recompiled = (scene.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium)) != scene::SceneChange::None;
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None)
            for (const scene::Instance& instance : scene.resources.instances) {
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                if (std::ranges::any_of(prototype.primitives, [](const scene::Primitive& primitive) { return primitive.area_light.value != 0; })) {
                    recompiled = true;
                    break;
                }
            }
        if (recompiled) this->upload_scene(scene, command_buffer);
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(scene, command_buffer);
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            this->upload_scene(scene, command_buffer);
            recompiled = true;
        }
        if ((scene.revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None) this->renderer.camera = scene.camera;
        if ((scene.revision.changes & scene::SceneChange::Film) != scene::SceneChange::None) {
            this->renderer.film_exposure      = scene.film.exposure;
            this->renderer.film_resolution    = scene.film.resolution;
            this->renderer.film_pixel_minimum = scene.film.pixel_minimum;
            this->renderer.film_pixel_maximum = scene.film.pixel_maximum;
        }
        if ((scene.revision.changes & scene::SceneChange::Sampler) != scene::SceneChange::None) this->renderer.sampler = scene.sampler;
        this->scene.uploaded_revision = scene.revision;
    }

    void Rasterizer::initialize_renderer() {
        this->renderer.sampled_output_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->renderer.storage_output_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->renderer.storage_sample_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->renderer.sampled_depth_descriptor  = this->context.runtime.resources.allocate_resource_descriptor();
        this->create_shaders();
    }

    void Rasterizer::create_shaders() {
        const std::vector<std::uint32_t> mesh_code     = load_spirv(this->context.shader_directory / "raster_mesh.spv");
        const std::vector<std::uint32_t> sphere_code   = load_spirv(this->context.shader_directory / "raster_spheres.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "raster_fragment.spv");
        vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
        acceleration_structure_source.pushAddressOffset = 0;
        const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{0, 0, 1, vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure, vk::DescriptorMappingSourceEXT::ePushAddress, acceleration_structure_source};
        const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
        std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader,
                vk::ShaderStageFlagBits::eMeshEXT,
                {},
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
        create_infos[1].pNext                        = &mapping;
        this->renderer.shaders                       = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
        this->renderer.sphere_shader                 = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader, vk::ShaderStageFlagBits::eMeshEXT, {}, vk::ShaderCodeTypeEXT::eSpirv, sphere_code.size() * sizeof(std::uint32_t), sphere_code.data(), "raster_spheres"}};
        const std::vector<std::uint32_t> volume_code = load_spirv(this->context.shader_directory / "raster_volume.spv");
        vk::ShaderCreateInfoEXT volume_create_info{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, volume_code.size() * sizeof(std::uint32_t), volume_code.data(), "raster_volume"};
        volume_create_info.pNext                         = &mapping;
        this->renderer.volume_shader                     = vk::raii::ShaderEXT{this->context.runtime.graphics.device, volume_create_info};
        const std::vector<std::uint32_t> background_code = load_spirv(this->context.shader_directory / "raster_background.spv");
        vk::ShaderCreateInfoEXT background_create_info{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, background_code.size() * sizeof(std::uint32_t), background_code.data(), "raster_background"};
        this->renderer.background_shader                   = vk::raii::ShaderEXT{this->context.runtime.graphics.device, background_create_info};
        const std::vector<std::uint32_t> accumulation_code = load_spirv(this->context.shader_directory / "raster_accumulate.spv");
        this->renderer.accumulation_shader                 = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, accumulation_code.size() * sizeof(std::uint32_t), accumulation_code.data(), "raster_accumulate"}};
    }

    void Rasterizer::create_output(const vk::Extent2D extent) {
        GpuImage next_sample = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eStorage);
        GpuImage next_output = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
        GpuImage next_depth  = this->context.runtime.resources.create_image_2d(extent, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eDepth);
        const bool replacing = *this->renderer.output_image.image;
        DescriptorLease sampled_output = replacing ? this->context.runtime.resources.allocate_resource_descriptor() : std::move(this->renderer.sampled_output_descriptor);
        DescriptorLease storage_output = replacing ? this->context.runtime.resources.allocate_resource_descriptor() : std::move(this->renderer.storage_output_descriptor);
        DescriptorLease storage_sample = replacing ? this->context.runtime.resources.allocate_resource_descriptor() : std::move(this->renderer.storage_sample_descriptor);
        DescriptorLease sampled_depth  = replacing ? this->context.runtime.resources.allocate_resource_descriptor() : std::move(this->renderer.sampled_depth_descriptor);
        this->context.runtime.resources.write_sampled_image_descriptor(sampled_output, next_output, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->context.runtime.resources.write_storage_image_descriptor(storage_output, next_output, vk::ImageLayout::eGeneral);
        this->context.runtime.resources.write_storage_image_descriptor(storage_sample, next_sample, vk::ImageLayout::eGeneral);
        this->context.runtime.resources.write_sampled_image_descriptor(sampled_depth, next_depth, vk::ImageLayout::eShaderReadOnlyOptimal);
        if (replacing) {
            this->context.runtime.frames.defer_destruction([sample = std::move(this->renderer.sample_image), output = std::move(this->renderer.output_image), depth = std::move(this->renderer.depth_image)]() mutable {});
        }
        this->renderer.sample_image              = std::move(next_sample);
        this->renderer.output_image              = std::move(next_output);
        this->renderer.depth_image               = std::move(next_depth);
        this->renderer.sampled_output_descriptor = std::move(sampled_output);
        this->renderer.storage_output_descriptor = std::move(storage_output);
        this->renderer.storage_sample_descriptor = std::move(storage_sample);
        this->renderer.sampled_depth_descriptor  = std::move(sampled_depth);
        this->renderer.output_layout = vk::ImageLayout::eUndefined;
        this->renderer.sample_layout = vk::ImageLayout::eUndefined;
        this->renderer.depth_layout  = vk::ImageLayout::eUndefined;
        this->renderer.sample_index  = 0;
    }

    void Rasterizer::record_commands(const vk::raii::CommandBuffer& command_buffer) {
        const bool wireframe_only = this->renderer.display_mode == RasterDisplayMode::Wireframe;
        const scene::CameraMatrices camera_matrices = this->renderer.camera.matrices();
        std::array<float, 16> view_projection = camera_matrices.view_projection;
        std::array<float, 16> inverse_view_projection = camera_matrices.inverse_view_projection;
        const std::array<std::uint32_t, 4> film_bounds{
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(this->renderer.film_pixel_minimum[0]) * this->renderer.sample_image.extent.width / this->renderer.film_resolution[0]),
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(this->renderer.film_pixel_minimum[1]) * this->renderer.sample_image.extent.height / this->renderer.film_resolution[1]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(this->renderer.film_pixel_maximum[0]) * this->renderer.sample_image.extent.width + this->renderer.film_resolution[0] - 1u) / this->renderer.film_resolution[0]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(this->renderer.film_pixel_maximum[1]) * this->renderer.sample_image.extent.height + this->renderer.film_resolution[1] - 1u) / this->renderer.film_resolution[1]),
        };
        std::array<float, 2> projection_jitter{};
        if (this->renderer.sampler.jitter && this->renderer.sample_index != 0) {
            projection_jitter = {
                (std::fmod(0.5f + static_cast<float>(this->renderer.sample_index) * 0.754877666f, 1.0f) - 0.5f) * 2.0f / static_cast<float>(this->renderer.sample_image.extent.width),
                (std::fmod(0.5f + static_cast<float>(this->renderer.sample_index) * 0.569840296f, 1.0f) - 0.5f) * 2.0f / static_cast<float>(this->renderer.sample_image.extent.height),
            };
            for (std::size_t column = 0; column != 4; ++column) {
                view_projection[column] += projection_jitter[0] * view_projection[12 + column];
                view_projection[4 + column] += projection_jitter[1] * view_projection[12 + column];
            }
            for (std::size_t row = 0; row != 4; ++row) inverse_view_projection[row * 4 + 3] -= projection_jitter[0] * inverse_view_projection[row * 4] + projection_jitter[1] * inverse_view_projection[row * 4 + 1];
        }
        const std::array barriers{
            vk::ImageMemoryBarrier2{
                this->renderer.sample_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone
                : this->renderer.sample_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader
                                                                            : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                this->renderer.sample_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{}
                : this->renderer.sample_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
                                                                            : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                this->renderer.sample_layout,
                vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->renderer.sample_image.image,
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

        struct alignas(16) BackgroundPushData {
            DescriptorHandle output;
            DescriptorHandle bindings;
            std::array<std::uint32_t, 2> extent;
            std::array<std::uint32_t, 2> reserved;
            std::array<std::uint32_t, 4> film_bounds;
            std::array<float, 4> inverse_view_projection_row_0;
            std::array<float, 4> inverse_view_projection_row_1;
            std::array<float, 4> inverse_view_projection_row_2;
            std::array<float, 4> inverse_view_projection_row_3;
        };
        static_assert(sizeof(BackgroundPushData) == 112);
        const BackgroundPushData background_push_data{this->renderer.storage_sample_descriptor, this->scene.bindings_descriptor, {this->renderer.sample_image.extent.width, this->renderer.sample_image.extent.height}, {}, film_bounds, {inverse_view_projection[0], inverse_view_projection[1], inverse_view_projection[2], inverse_view_projection[3]}, {inverse_view_projection[4], inverse_view_projection[5], inverse_view_projection[6], inverse_view_projection[7]}, {inverse_view_projection[8], inverse_view_projection[9], inverse_view_projection[10], inverse_view_projection[11]}, {inverse_view_projection[12], inverse_view_projection[13], inverse_view_projection[14], inverse_view_projection[15]}};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.background_shader);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&background_push_data, 1}));
        command_buffer.dispatch((this->renderer.sample_image.extent.width + 7) / 8, (this->renderer.sample_image.extent.height + 7) / 8, 1);
        const vk::ImageMemoryBarrier2 background_barrier{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite, vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.sample_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &background_barrier});
        this->renderer.sample_layout = vk::ImageLayout::eColorAttachmentOptimal;

        const vk::RenderingAttachmentInfo color_attachment{
            *this->renderer.sample_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
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
            vk::Rect2D{{0, 0}, this->renderer.sample_image.extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });

        const vk::Viewport viewport{
            0.0f,
            static_cast<float>(this->renderer.sample_image.extent.height),
            static_cast<float>(this->renderer.sample_image.extent.width),
            -static_cast<float>(this->renderer.sample_image.extent.height),
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
        command_buffer.setRasterizerDiscardEnable(vk::False);
        command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
        command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
        command_buffer.setAlphaToCoverageEnableEXT(vk::False);
        command_buffer.setDepthBiasEnable(vk::False);
        command_buffer.setStencilTestEnable(vk::False);
        constexpr vk::SampleMask sample_mask = 1;
        command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
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

        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{*this->renderer.shaders[0], *this->renderer.shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);

        struct alignas(16) RasterPushData {
            vk::DeviceAddress acceleration_structure_address;
            DescriptorHandle positions;
            DescriptorHandle normals;
            DescriptorHandle tangents;
            DescriptorHandle texture_coordinates;
            DescriptorHandle indices;
            DescriptorHandle bindings;
            std::uint32_t scene_primitive_index;
            std::uint32_t element_count;
            std::array<std::uint32_t, 2> attributes;
            DescriptorHandle sampling_tables;
            std::array<std::uint32_t, 4> sampler_metadata;
            std::array<std::uint32_t, 4> sampler_parameters;
            std::array<std::uint32_t, 2> extent;
            std::uint32_t sample_index;
            std::uint32_t reserved;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> camera_position;
            std::array<float, 4> camera_right;
            std::array<float, 4> camera_up;
            std::array<float, 4> camera_forward;
        };
        static_assert(sizeof(RasterPushData) == 256);
        const scene::CameraFrame camera             = this->renderer.camera.frame();
        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
            if (gpu_primitive.kind != GpuScenePrimitiveKind::Geometry) continue;
            const GpuGeometry& mesh = this->context.gpu_scene.view().geometries[gpu_primitive.resource_index];
            const RasterPushData push_data{
                this->context.gpu_scene.view().acceleration_structure,
                mesh.positions_descriptor,
                mesh.normals_descriptor,
                mesh.tangents_descriptor,
                mesh.texture_coordinates_descriptor,
                mesh.indices_descriptor,
                this->scene.bindings_descriptor,
                gpu_primitive.scene_primitive_index,
                mesh.index_count / 3u,
                {mesh.attribute_mask, static_cast<std::uint32_t>(this->renderer.display_mode)},
                this->context.sampling.tables_descriptor,
                {std::to_underlying(this->renderer.sampler.kind), std::to_underlying(this->renderer.sampler.randomization), this->renderer.sampler.samples_per_pixel, this->renderer.sampler.seed},
                {this->renderer.sampler.x_strata, this->renderer.sampler.y_strata, this->renderer.sampler.jitter ? 1u : 0u, 0},
                {this->renderer.sample_image.extent.width, this->renderer.sample_image.extent.height},
                this->renderer.sample_index,
                0,
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                {camera.position.x, camera.position.y, camera.position.z, std::holds_alternative<scene::PerspectiveCameraData>(this->renderer.camera.data) ? 1.0f : 0.0f},
                {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                {camera.up.x, camera.up.y, camera.up.z, 0.0f},
                {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        }
        if (!wireframe_only) {
            const std::array sphere_shader_handles{*this->renderer.sphere_shader, *this->renderer.shaders[1]};
            command_buffer.bindShadersEXT(stages, sphere_shader_handles);
            for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
                if (gpu_primitive.kind != GpuScenePrimitiveKind::SphereSet) continue;
                const GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                const RasterPushData push_data{
                    this->context.gpu_scene.view().acceleration_structure,
                    spheres.positions_descriptor,
                    spheres.radii_descriptor,
                    spheres.positions_descriptor,
                    spheres.radii_descriptor,
                    spheres.positions_descriptor,
                    this->scene.bindings_descriptor,
                    gpu_primitive.scene_primitive_index,
                    spheres.sphere_count,
                    {0, static_cast<std::uint32_t>(this->renderer.display_mode)},
                    this->context.sampling.tables_descriptor,
                    {std::to_underlying(this->renderer.sampler.kind), std::to_underlying(this->renderer.sampler.randomization), this->renderer.sampler.samples_per_pixel, this->renderer.sampler.seed},
                    {this->renderer.sampler.x_strata, this->renderer.sampler.y_strata, this->renderer.sampler.jitter ? 1u : 0u, 0},
                    {this->renderer.sample_image.extent.width, this->renderer.sample_image.extent.height},
                    this->renderer.sample_index,
                    0,
                    {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                    {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                    {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                    {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                    {camera.position.x, camera.position.y, camera.position.z, std::holds_alternative<scene::PerspectiveCameraData>(this->renderer.camera.data) ? 1.0f : 0.0f},
                    {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                    {camera.up.x, camera.up.y, camera.up.z, 0.0f},
                    {camera.forward.x, camera.forward.y, camera.forward.z, 0.0f},
                };
                this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
            }
        }
        command_buffer.endRendering();
        if (!wireframe_only && this->scene.volume_count != 0) {
            const std::array volume_barriers{
                vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.sample_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
                vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.depth_image.image, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}},
            };
            const vk::MemoryBarrier2 acceleration_structure_barrier{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eAccelerationStructureReadKHR};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &acceleration_structure_barrier, 0, nullptr, static_cast<std::uint32_t>(volume_barriers.size()), volume_barriers.data()});
            struct alignas(16) VolumePushData {
                vk::DeviceAddress acceleration_structure_address;
                DescriptorHandle output;
                DescriptorHandle depth;
                DescriptorHandle volumes;
                DescriptorHandle bindings;
                DescriptorHandle sampling_tables;
                std::array<std::uint32_t, 4> metadata;
                std::array<std::uint32_t, 4> sampler_metadata;
                std::array<std::uint32_t, 4> sampler_parameters;
                std::array<std::uint32_t, 4> film_bounds;
                std::uint32_t sample_index;
                std::array<std::uint32_t, 3> reserved;
                std::array<float, 4> inverse_view_projection_row_0;
                std::array<float, 4> inverse_view_projection_row_1;
                std::array<float, 4> inverse_view_projection_row_2;
                std::array<float, 4> inverse_view_projection_row_3;
            };
            static_assert(sizeof(VolumePushData) == 192);
            const VolumePushData volume_push_data{this->context.gpu_scene.view().acceleration_structure, this->renderer.storage_sample_descriptor, this->renderer.sampled_depth_descriptor, this->scene.volumes_descriptor, this->scene.bindings_descriptor, this->context.sampling.tables_descriptor, {this->renderer.sample_image.extent.width, this->renderer.sample_image.extent.height, this->scene.volume_count, this->scene.light_count}, {std::to_underlying(this->renderer.sampler.kind), std::to_underlying(this->renderer.sampler.randomization), this->renderer.sampler.samples_per_pixel, this->renderer.sampler.seed}, {this->renderer.sampler.x_strata, this->renderer.sampler.y_strata, this->renderer.sampler.jitter ? 1u : 0u, 0}, film_bounds, this->renderer.sample_index, {}, {inverse_view_projection[0], inverse_view_projection[1], inverse_view_projection[2], inverse_view_projection[3]}, {inverse_view_projection[4], inverse_view_projection[5], inverse_view_projection[6], inverse_view_projection[7]},
                {inverse_view_projection[8], inverse_view_projection[9], inverse_view_projection[10], inverse_view_projection[11]}, {inverse_view_projection[12], inverse_view_projection[13], inverse_view_projection[14], inverse_view_projection[15]}};
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.volume_shader);
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&volume_push_data, 1}));
            command_buffer.dispatch((this->renderer.sample_image.extent.width + 7) / 8, (this->renderer.sample_image.extent.height + 7) / 8, 1);
            this->renderer.sample_layout = vk::ImageLayout::eGeneral;
            this->renderer.depth_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;
            const vk::MemoryBarrier2 sample_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &sample_dependency});
        } else {
            const vk::ImageMemoryBarrier2 sample_barrier{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.sample_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &sample_barrier});
            this->renderer.sample_layout = vk::ImageLayout::eGeneral;
            this->renderer.depth_layout  = vk::ImageLayout::eDepthAttachmentOptimal;
        }
        const vk::ImageMemoryBarrier2 output_barrier{
            this->renderer.output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eComputeShader,
            this->renderer.output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            this->renderer.output_layout,
            vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->renderer.output_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, 1, &output_barrier});
        struct alignas(16) AccumulationPushData {
            DescriptorHandle sample;
            DescriptorHandle output;
            std::array<std::uint32_t, 2> extent;
            std::uint32_t sample_index;
            std::uint32_t reserved;
        };
        static_assert(sizeof(AccumulationPushData) == 32);
        const AccumulationPushData accumulation_push_data{this->renderer.storage_sample_descriptor, this->renderer.storage_output_descriptor, {this->renderer.output_image.extent.width, this->renderer.output_image.extent.height}, this->renderer.sample_index, 0};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.accumulation_shader);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&accumulation_push_data, 1}));
        command_buffer.dispatch((this->renderer.output_image.extent.width + 7) / 8, (this->renderer.output_image.extent.height + 7) / 8, 1);
        this->renderer.output_layout = vk::ImageLayout::eGeneral;
    }

    RenderOutput Rasterizer::output() const noexcept {
        return {
            this->renderer.output_image,
            this->renderer.sampled_output_descriptor,
            this->renderer.output_layout,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            scene::SpectrumColorSpace::Srgb,
            this->renderer.film_exposure,
        };
    }
    Rasterizer::Rasterizer(VulkanRuntime& runtime, GpuScene& gpu_scene, SamplingResources& sampling, const scene::SceneView scene_view, std::filesystem::path shader_directory) : context{runtime, gpu_scene, sampling, std::move(shader_directory)} {
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
        bool reset_required{};
        if (this->renderer.pending_changes != scene::SceneChange::None) {
            scene_view.revision.changes = this->renderer.pending_changes;
            this->synchronize_scene(scene_view, command_buffer);
            this->renderer.pending_changes = scene::SceneChange::None;
            reset_required                 = true;
        }
        bool gpu_recompiled{};
        if ((this->renderer.pending_gpu_changes & GpuSceneChange::Structure) != GpuSceneChange::None) {
            this->upload_scene(scene_view, command_buffer);
            reset_required = true;
            gpu_recompiled = true;
        } else if ((this->renderer.pending_gpu_changes & GpuSceneChange::Volume) != GpuSceneChange::None) {
            this->update_volumes(scene_view, command_buffer);
            reset_required = true;
        }
        if (!gpu_recompiled && (this->renderer.pending_gpu_changes & (GpuSceneChange::Geometry | GpuSceneChange::Transform)) != GpuSceneChange::None) this->update_dynamic_lights(command_buffer);
        if ((this->renderer.pending_gpu_changes & (GpuSceneChange::Geometry | GpuSceneChange::Transform)) != GpuSceneChange::None) reset_required = true;
        this->renderer.pending_gpu_changes = GpuSceneChange::None;
        if (this->renderer.camera_revision != view.camera_revision) {
            this->renderer.camera_revision = view.camera_revision;
            reset_required                 = true;
        }
        this->renderer.camera = view.camera;
        if (!*this->renderer.output_image.image || this->renderer.output_image.extent != view.extent) {
            this->create_output(view.extent);
            reset_required = false;
        }
        if (reset_required) this->reset();
    }

    void Rasterizer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t) {
        if (this->renderer.paused || this->renderer.sample_index >= this->progress().target) return;
        this->record_commands(command_buffer);
        ++this->renderer.sample_index;
    }

    RenderProgress Rasterizer::progress() const noexcept {
        return {this->renderer.sample_index, this->renderer.display_mode == RasterDisplayMode::Wireframe ? 1u : this->renderer.sampler.samples_per_pixel, this->renderer.paused};
    }

    void Rasterizer::set_paused(const bool paused) noexcept {
        this->renderer.paused = paused;
    }

    void Rasterizer::reset() noexcept {
        this->renderer.sample_index = 0;
    }

    void Rasterizer::set_display_mode(const RasterDisplayMode mode) noexcept {
        if (this->renderer.display_mode == mode) return;
        this->renderer.display_mode = mode;
        this->reset();
    }

    void Rasterizer::destroy() noexcept {
        this->context.runtime.frames.defer_destruction([
            primitive_buffer = std::move(this->scene.primitive_buffer),
            material_buffer = std::move(this->scene.material_buffer),
            face_material_buffer = std::move(this->scene.face_material_buffer),
            area_light_buffer = std::move(this->scene.area_light_buffer),
            light_buffer = std::move(this->scene.light_buffer),
            ray_primitive_buffer = std::move(this->scene.ray_primitive_buffer),
            volume_buffer = std::move(this->scene.volume_buffer),
            zero_volume_field_buffer = std::move(this->scene.zero_volume_field_buffer),
            texture_buffers = std::move(this->scene.texture_buffers),
            binding_buffer = std::move(this->scene.scene_binding_buffer),
            volume_resources = std::move(this->scene.volume_resources),
            volume_majorant_shader = std::move(this->scene.volume_majorant_shader),
            dynamic_shapes_shader = std::move(this->scene.dynamic_light_shapes_shader),
            dynamic_finalize_shader = std::move(this->scene.dynamic_light_finalize_shader),
            dynamic_selection_shader = std::move(this->scene.dynamic_light_selection_shader),
            sample_image = std::move(this->renderer.sample_image),
            output_image = std::move(this->renderer.output_image),
            depth_image = std::move(this->renderer.depth_image),
            shaders = std::move(this->renderer.shaders),
            sphere_shader = std::move(this->renderer.sphere_shader),
            volume_shader = std::move(this->renderer.volume_shader),
            background_shader = std::move(this->renderer.background_shader),
            accumulation_shader = std::move(this->renderer.accumulation_shader)
        ]() mutable {});
    }

} // namespace spectra
