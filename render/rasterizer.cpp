module spectra.rasterizer;

import std;

namespace spectra::rasterizer {
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
    static_assert(sizeof(RasterAreaLight) == 16);
    static_assert(sizeof(RasterSceneBindings) == 128);
    static_assert(sizeof(GpuPickPrimitive) == 32);
    static_assert(sizeof(RasterVolume) == 256);
    static_assert(sizeof(RasterVolumeLight) == 80);

    namespace {
        constexpr std::uint32_t invalid_raster_index = std::numeric_limits<std::uint32_t>::max();
        constexpr float cie_y_integral               = 106.856895f;

        [[nodiscard]] scene::Float3 raster_linear_srgb(const scene::Float3 value, const scene::SpectrumColorSpace color_space) noexcept {
            if (color_space == scene::SpectrumColorSpace::Rec2020) return {1.660491f * value.x - 0.587641f * value.y - 0.072850f * value.z, -0.124550f * value.x + 1.132900f * value.y - 0.008349f * value.z, -0.018151f * value.x - 0.100579f * value.y + 1.118730f * value.z};
            if (color_space == scene::SpectrumColorSpace::Aces2065_1) return {2.521686f * value.x - 1.134130f * value.y - 0.387556f * value.z, -0.276479f * value.x + 1.372719f * value.y - 0.096240f * value.z, -0.015378f * value.x - 0.152975f * value.y + 1.168353f * value.z};
            return value;
        }

        [[nodiscard]] scene::Float3 raster_spectrum_rgb(const scene::SpectrumParameter& parameter) {
            if (parameter.encoding == scene::SpectrumEncoding::RgbAlbedo || parameter.encoding == scene::SpectrumEncoding::RgbUnbounded || parameter.encoding == scene::SpectrumEncoding::RgbIlluminant) {
                scene::Float3 rgb = raster_linear_srgb(parameter.value, parameter.color_space);
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

        [[nodiscard]] RasterTextureCompilation compile_raster_textures(const render::GpuScene& assets, const scene::SceneView scene) {
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
                            local_index               = static_cast<std::uint32_t>(result.constants.size());
                            const scene::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? scene::Float3{data.scalar, data.scalar, data.scalar} : raster_spectrum_rgb(data.spectrum);
                            result.constants.push_back(RasterConstantTexture{{value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalar : 1.0f}});
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                            kind                                   = 1;
                            local_index                            = static_cast<std::uint32_t>(result.images.size());
                            const render::GpuTextureImage& runtime = assets.texture_image(texture, texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat);
                            result.images.push_back(RasterImageTexture{runtime.image_descriptor, runtime.sampler_descriptor, {compile_raster_mapping(result.mappings, data.mapping), static_cast<std::uint32_t>(data.channel), static_cast<std::uint32_t>(data.filter), data.invert ? 1u : 0u}, {data.scale, static_cast<float>(std::to_underlying(texture.color_space)), data.maximum_anisotropy, static_cast<float>(std::to_underlying(data.wrap))}});
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
                                const scene::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? scene::Float3{data.scalars[corner], data.scalars[corner], data.scalars[corner]} : raster_spectrum_rgb(data.spectra[corner]);
                                compiled.values[corner]   = {value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalars[corner] : 1.0f};
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
                const scene::Float3 rgb = raster_spectrum_rgb(parameter);
                value                   = {rgb.x, rgb.y, rgb.z, 0.0f};
                data[0]                 = texture_handle(parameter.texture);
            };
            const auto material_index = [scene](const scene::MaterialId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, id, &scene::MaterialResource::id) - scene.resources.materials.begin()); };
            std::vector<RasterMaterial> result{};
            result.reserve(scene.resources.materials.size());
            for (const scene::MaterialResource& material : scene.resources.materials) {
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

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(Spectra& runtime, const std::span<const Element> elements, const vk::BufferUsageFlags usage) {
            GpuBuffer staging = runtime.create_buffer(elements.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, elements.data(), elements.size_bytes());
            GpuBuffer destination = runtime.create_buffer(elements.size_bytes(), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            runtime.immediate([&staging, &destination, usage](const vk::raii::CommandBuffer& command_buffer) {
                command_buffer.copyBuffer(*staging.buffer, *destination.buffer, vk::BufferCopy{0, 0, staging.size});
                const vk::BufferMemoryBarrier2 upload_dependency{
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
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &upload_dependency});
            });
            return destination;
        }

        template <class Sample>
        [[nodiscard]] std::vector<float> build_raster_majorant(const scene::UInt3 resolution, Sample&& sample) {
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
        [[nodiscard]] GpuBuffer upload_buffer(Spectra& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage) {
            GpuBuffer destination       = runtime.create_buffer(elements.size_bytes(), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload = runtime.stage_upload(std::as_bytes(elements));
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


        [[nodiscard]] std::vector<RasterTransform> compile_raster_transforms(const render::GpuScene& assets, const scene::SceneView scene) {
            std::vector<RasterTransform> transforms{};
            transforms.reserve(assets.draws.size());
            for (const render::GpuDraw& draw : assets.draws) {
                const scene::Instance& instance   = scene.resources.instances[draw.scene_instance_index];
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                const scene::Transform transform  = instance.transform * prototype.primitives[draw.prototype_primitive_index].transform;
                transforms.push_back(RasterTransform{{transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]}, {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]}, {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]}, {transform.matrix[12], transform.matrix[13], transform.matrix[14], transform.matrix[15]}});
            }
            return transforms;
        }

    } // namespace

    RasterScene::RasterScene(Spectra& runtime, const render::GpuScene& assets, const scene::SceneView scene, const std::filesystem::path& shader_directory) : runtime(&runtime), material_descriptor(runtime.allocate_resource_descriptor()), face_material_descriptor(runtime.allocate_resource_descriptor()), area_light_descriptor(runtime.allocate_resource_descriptor()), volume_zero_descriptor(runtime.allocate_resource_descriptor()), uploaded_revision(scene.revision), gpu_scene(assets), primitives_descriptor(runtime.allocate_resource_descriptor()), transforms_descriptor(runtime.allocate_resource_descriptor()), pick_primitives_descriptor(runtime.allocate_resource_descriptor()), bindings_descriptor(runtime.allocate_resource_descriptor()), volumes_descriptor(runtime.allocate_resource_descriptor()), volume_lights_descriptor(runtime.allocate_resource_descriptor()), camera(scene.camera), film_exposure(scene.film.exposure) {
        for (DescriptorHandle& descriptor : this->texture_descriptors) descriptor = runtime.allocate_resource_descriptor();
        const std::vector<std::uint32_t> code = render::load_spirv(shader_directory / "path_volume_majorant.spv");
        this->volume_majorant_shader          = vk::raii::ShaderEXT{runtime.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), "build_majorant"}};
        const std::array<scene::Float3, 1> zero{};
        this->volume_zero_buffer = upload_buffer(runtime, std::span<const scene::Float3>{zero}, vk::BufferUsageFlagBits::eStorageBuffer);
        runtime.write_buffer(this->volume_zero_descriptor, vk::DescriptorType::eStorageBuffer, this->volume_zero_buffer);
        this->upload(scene);
    }

    RasterScene::~RasterScene() {
        this->runtime->release_resource_descriptor(this->primitives_descriptor);
        this->runtime->release_resource_descriptor(this->transforms_descriptor);
        this->runtime->release_resource_descriptor(this->material_descriptor);
        this->runtime->release_resource_descriptor(this->face_material_descriptor);
        this->runtime->release_resource_descriptor(this->area_light_descriptor);
        this->runtime->release_resource_descriptor(this->pick_primitives_descriptor);
        this->runtime->release_resource_descriptor(this->volumes_descriptor);
        this->runtime->release_resource_descriptor(this->volume_lights_descriptor);
        for (const VolumeGpuData& volume : this->volume_gpu_data) this->runtime->release_resource_descriptor(volume.majorant_descriptor);
        this->runtime->release_resource_descriptor(this->volume_zero_descriptor);
        for (const DescriptorHandle descriptor : this->texture_descriptors) this->runtime->release_resource_descriptor(descriptor);
        this->runtime->release_resource_descriptor(this->bindings_descriptor);
    }

    void RasterScene::upload(const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer) {
        RasterTextureCompilation textures     = compile_raster_textures(this->gpu_scene, scene);
        this->texture_count                   = static_cast<std::uint32_t>(scene.resources.textures.size());
        this->texture_stack_size              = textures.maximum_stack_size;
        std::vector<RasterMaterial> materials = compile_raster_materials(scene, textures);

        std::vector<RasterAreaLight> area_lights{};
        area_lights.reserve(scene.resources.lights.size());
        for (const scene::Light& light : scene.resources.lights) {
            scene::Float3 emission{};
            if (const scene::DiffuseAreaLight* area = std::get_if<scene::DiffuseAreaLight>(&light.data)) {
                emission = raster_spectrum_rgb(area->radiance);
                emission = {emission.x * area->scale, emission.y * area->scale, emission.z * area->scale};
            }
            area_lights.push_back(RasterAreaLight{{emission.x, emission.y, emission.z, 0.0f}});
        }
        if (area_lights.empty()) area_lights.emplace_back();

        std::vector<RasterPrimitive> primitives{};
        std::vector<RasterTransform> transforms = compile_raster_transforms(this->gpu_scene, scene);
        std::vector<std::uint32_t> face_materials{};
        std::vector<GpuPickPrimitive> pick_primitives{};
        primitives.reserve(this->gpu_scene.draws.size());
        for (const render::GpuDraw& draw : this->gpu_scene.draws) {
            const scene::Instance& instance        = scene.resources.instances[draw.scene_instance_index];
            const scene::Prototype& prototype      = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive      = prototype.primitives[draw.prototype_primitive_index];
            const bool particle_draw               = draw.kind == render::GpuDrawKind::ParticleSet;
            const scene::ParticleSet* particle_set = particle_draw ? &*std::ranges::find(scene.resources.particle_sets, primitive.particles, &scene::ParticleSet::id) : nullptr;
            if (!particle_draw) {
                GpuPickPrimitive pick_primitive{};
                const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
                std::visit(
                    [&pick_primitive](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphereGeometry>) {
                            pick_primitive.metadata[0] = 1;
                            pick_primitive.parameters  = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiskGeometry>) {
                            pick_primitive.metadata[0] = 2;
                            pick_primitive.parameters  = {data.height, data.radius, data.inner_radius, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CylinderGeometry>) {
                            pick_primitive.metadata[0] = 3;
                            pick_primitive.parameters  = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        }
                    },
                    geometry.data);
                pick_primitives.push_back(pick_primitive);
            }
            const std::uint32_t face_material_offset = static_cast<std::uint32_t>(face_materials.size());
            for (const scene::MaterialId face_material : particle_draw ? std::span<const scene::MaterialId>{} : std::span<const scene::MaterialId>{primitive.face_materials}) {
                const std::vector<scene::MaterialResource>::const_iterator resource = std::ranges::find(scene.resources.materials, face_material, &scene::MaterialResource::id);
                face_materials.push_back(static_cast<std::uint32_t>(resource - scene.resources.materials.begin()));
            }
            const std::vector<scene::MaterialResource>::const_iterator material = std::ranges::find(scene.resources.materials, particle_draw ? particle_set->material : primitive.material, &scene::MaterialResource::id);
            std::uint32_t area_light                                            = std::numeric_limits<std::uint32_t>::max();
            if (primitive.area_light.value != 0) {
                const std::vector<scene::Light>::const_iterator light = std::ranges::find(scene.resources.lights, primitive.area_light, &scene::Light::id);
                area_light                                            = static_cast<std::uint32_t>(light - scene.resources.lights.begin());
            }
            const std::uint32_t transform_index = static_cast<std::uint32_t>(primitives.size());
            primitives.push_back(RasterPrimitive{transform_index, static_cast<std::uint32_t>(material - scene.resources.materials.begin()), area_light, primitive.reverse_orientation ? 1u : 0u, face_material_offset, static_cast<std::uint32_t>(particle_draw ? 0 : primitive.face_materials.size()), {}});
        }
        if (primitives.empty()) {
            primitives.emplace_back();
            transforms.emplace_back();
        }
        if (face_materials.empty()) face_materials.emplace_back();
        if (pick_primitives.empty()) pick_primitives.emplace_back();

        GpuBuffer new_primitives         = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const RasterPrimitive>{primitives}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const RasterPrimitive>{primitives}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_transforms         = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const RasterTransform>{transforms}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const RasterTransform>{transforms}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_materials          = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const RasterMaterial>{materials}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const RasterMaterial>{materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_face_materials     = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const std::uint32_t>{face_materials}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const std::uint32_t>{face_materials}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_area_lights        = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const RasterAreaLight>{area_lights}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const RasterAreaLight>{area_lights}, vk::BufferUsageFlagBits::eStorageBuffer);
        GpuBuffer new_pick_primitives    = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const GpuPickPrimitive>{pick_primitives}, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, std::span<const GpuPickPrimitive>{pick_primitives}, vk::BufferUsageFlagBits::eStorageBuffer);
        const auto upload_texture_buffer = [this, command_buffer]<typename Element>(const std::span<const Element> elements) { return command_buffer ? upload_buffer(*this->runtime, *command_buffer, elements, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, elements, vk::BufferUsageFlagBits::eStorageBuffer); };
        std::vector<RasterVolume> raster_volumes{};
        std::vector<VolumeGpuData> new_volume_gpu_data{};
        raster_volumes.reserve(scene.resources.volumes.size());
        new_volume_gpu_data.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) {
            const render::GpuVolume& shared = *std::ranges::find(this->gpu_scene.volumes, volume.id, &render::GpuVolume::id);
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
            const scene::Transform inverse = volume.transform.inverse();
            record.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            record.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            record.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
            if (medium) {
                const scene::Float3 sigma_a  = raster_spectrum_rgb(medium->sigma_a);
                const scene::Float3 sigma_s  = raster_spectrum_rgb(medium->sigma_s);
                const scene::Float3 emission = raster_spectrum_rgb(medium->emission);
                record.sigma_a               = {sigma_a.x, sigma_a.y, sigma_a.z, 0.0f};
                record.sigma_s               = {sigma_s.x, sigma_s.y, sigma_s.z, 0.0f};
                record.emission              = {emission.x, emission.y, emission.z, 0.0f};
                record.scales                = {medium->density_scale, medium->emission_scale, medium->anisotropy, medium->temperature_scale};
                record.temperature           = {medium->temperature_offset, medium->minimum_emission_temperature, medium->blackbody_emission ? 1.0f : 0.0f, 0.0f};
            }
            VolumeGpuData gpu_data{};
            gpu_data.id                  = volume.id;
            gpu_data.revision            = shared.revision;
            gpu_data.majorant_descriptor = this->runtime->allocate_resource_descriptor();
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
                        record.majorant_metadata = {16, 16, 16, 0};
                        majorant                 = build_raster_majorant(data.resolution, [&data](const std::size_t index) {
                            const scene::Float3 absorption = data.sigma_a.empty() ? scene::Float3{1.0f, 1.0f, 1.0f} : data.sigma_a[index];
                            const scene::Float3 scattering = data.sigma_s.empty() ? scene::Float3{1.0f, 1.0f, 1.0f} : data.sigma_s[index];
                            return std::max({absorption.x + scattering.x, absorption.y + scattering.y, absorption.z + scattering.z});
                        });
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ProceduralCloudVolume>) {
                        record.metadata          = {2, 0, 0, 0};
                        record.majorant_metadata = {16, 16, 16, 0};
                        record.scales[0] *= data.density;
                        record.temperature[3] = data.frequency;
                        std::ranges::fill(majorant, 1.0f);
                    } else
                        throw std::runtime_error("Rasterizer requires dense or procedural Volume data; NanoVDB is currently a Path-only resource");
                },
                volume.data);
            gpu_data.majorant = upload_texture_buffer(std::span<const float>{majorant});
            this->runtime->write_buffer(gpu_data.majorant_descriptor, vk::DescriptorType::eStorageBuffer, gpu_data.majorant);
            const auto field = [this, &shared](const render::GpuVolumeField field) {
                const std::size_t index = std::to_underlying(field);
                return shared.present[index] ? shared.descriptors[index] : this->volume_zero_descriptor;
            };
            record.density              = field(render::GpuVolumeField::Density);
            record.temperature_field    = field(render::GpuVolumeField::Temperature);
            record.sigma_a_field        = field(render::GpuVolumeField::SigmaA);
            record.sigma_s_field        = field(render::GpuVolumeField::SigmaS);
            record.emission_scale_field = field(render::GpuVolumeField::EmissionScale);
            record.emission_field       = field(render::GpuVolumeField::Emission);
            record.majorant             = gpu_data.majorant_descriptor;
            raster_volumes.push_back(record);
            new_volume_gpu_data.push_back(std::move(gpu_data));
        }
        this->volumes_count = static_cast<std::uint32_t>(raster_volumes.size());
        if (raster_volumes.empty()) raster_volumes.emplace_back();

        std::vector<RasterVolumeLight> volume_lights{};
        for (const scene::Light& source : scene.resources.lights) {
            if (std::holds_alternative<scene::DiffuseAreaLight>(source.data)) continue;
            RasterVolumeLight light{};
            std::visit(
                [&](const auto& data) {
                    const scene::Float3 radiance = raster_spectrum_rgb([&]() -> const scene::SpectrumParameter& {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>)
                            return data.intensity;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>)
                            return data.radiance;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                            return data.radiance;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                            return data.environment.radiance;
                        else
                            return data.radiance;
                    }());
                    const float scale            = [&]() {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                            return data.environment.scale;
                        else
                            return data.scale;
                    }();
                    light.radiance = {radiance.x * scale, radiance.y * scale, radiance.z * scale, 0.0f};
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) {
                        const std::array<float, 16>& transform = data.transform.matrix;
                        light.metadata[0]                      = std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> ? 0u : 1u;
                        light.position                         = {transform[3], transform[7], transform[11], 1.0f};
                        light.direction                        = {-transform[2], -transform[6], -transform[10], 0.0f};
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) light.parameters = {std::cos(data.cone_angle * std::numbers::pi_v<float> / 180.0f), std::cos((data.cone_angle - data.cone_delta) * std::numbers::pi_v<float> / 180.0f), 0.0f, 0.0f};
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>) {
                        light.metadata[0]                      = 2;
                        const std::array<float, 16>& transform = data.transform.matrix;
                        light.direction                        = {-transform[2], -transform[6], -transform[10], 0.0f};
                    } else
                        light.metadata[0] = 4;
                },
                source.data);
            volume_lights.push_back(light);
        }
        for (const render::GpuDraw& draw : this->gpu_scene.draws) {
            const scene::Instance& instance   = scene.resources.instances[draw.scene_instance_index];
            const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive = prototype.primitives[draw.prototype_primitive_index];
            if (primitive.area_light.value == 0) continue;
            if (draw.kind != render::GpuDrawKind::Geometry) throw std::runtime_error("Rasterizer does not bind a Diffuse Area Light to a Particle Set");
            const scene::Light& source                = *std::ranges::find(scene.resources.lights, primitive.area_light, &scene::Light::id);
            const scene::DiffuseAreaLight& area_light = std::get<scene::DiffuseAreaLight>(source.data);
            const scene::Geometry& geometry           = *std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
            const scene::Transform transform          = instance.transform * primitive.transform;
            scene::Float3 position                    = transform.transform_point(scene::geometry_bounds(geometry).center());
            scene::Float3 normal                      = transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
            float area                                = scene::surface_area(geometry);
            if (const scene::TriangleMeshGeometry* mesh = std::get_if<scene::TriangleMeshGeometry>(&geometry.data)) {
                scene::Float3 weighted_position{};
                scene::Float3 weighted_normal{};
                area = 0.0f;
                for (std::size_t index = 0; index < mesh->indices.size(); index += 3) {
                    const scene::Float3 first  = transform.transform_point(mesh->positions[mesh->indices[index]]);
                    const scene::Float3 second = transform.transform_point(mesh->positions[mesh->indices[index + 1]]);
                    const scene::Float3 third  = transform.transform_point(mesh->positions[mesh->indices[index + 2]]);
                    const scene::Float3 cross  = (second - first).cross(third - first);
                    const float triangle_area  = cross.length() * 0.5f;
                    weighted_position          = weighted_position + (first + second + third) * (triangle_area / 3.0f);
                    weighted_normal            = weighted_normal + cross;
                    area += triangle_area;
                }
                if (area > 0.0f) position = weighted_position / area;
                if (weighted_normal.length() > 0.0f) normal = weighted_normal.normalized();
            } else {
                const scene::Float3 x = transform.transform_vector({1.0f, 0.0f, 0.0f});
                const scene::Float3 y = transform.transform_vector({0.0f, 1.0f, 0.0f});
                const scene::Float3 z = transform.transform_vector({0.0f, 0.0f, 1.0f});
                const float scale     = std::holds_alternative<scene::RectangleGeometry>(geometry.data) || std::holds_alternative<scene::DiskGeometry>(geometry.data) ? x.cross(y).length() : (x.cross(y).length() + x.cross(z).length() + y.cross(z).length()) / 3.0f;
                area *= scale;
            }
            if (primitive.reverse_orientation) normal = -normal;
            const scene::Float3 radiance = raster_spectrum_rgb(area_light.radiance) * area_light.scale;
            RasterVolumeLight light{};
            light.metadata   = {3, area_light.sidedness == scene::EmissionSidedness::Both ? 1u : 0u, 0, 0};
            light.position   = {position.x, position.y, position.z, 1.0f};
            light.direction  = {normal.x, normal.y, normal.z, 0.0f};
            light.radiance   = {radiance.x, radiance.y, radiance.z, 0.0f};
            light.parameters = {area, 0.0f, 0.0f, 0.0f};
            volume_lights.push_back(light);
        }
        this->volume_lights_count = static_cast<std::uint32_t>(volume_lights.size());
        if (volume_lights.empty()) volume_lights.emplace_back();
        GpuBuffer new_volume_buffer       = upload_texture_buffer(std::span<const RasterVolume>{raster_volumes});
        GpuBuffer new_volume_light_buffer = upload_texture_buffer(std::span<const RasterVolumeLight>{volume_lights});
        this->runtime->write_buffer(this->volumes_descriptor, vk::DescriptorType::eStorageBuffer, new_volume_buffer);
        this->runtime->write_buffer(this->volume_lights_descriptor, vk::DescriptorType::eStorageBuffer, new_volume_light_buffer);
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
        if (command_buffer) {
            const DescriptorHandle primitives_descriptor      = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle transforms_descriptor      = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle material_descriptor        = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle face_material_descriptor   = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle area_light_descriptor      = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle pick_primitives_descriptor = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            this->runtime->write_buffer(transforms_descriptor, vk::DescriptorType::eStorageBuffer, new_transforms);
            this->runtime->write_buffer(material_descriptor, vk::DescriptorType::eStorageBuffer, new_materials);
            this->runtime->write_buffer(face_material_descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
            this->runtime->write_buffer(area_light_descriptor, vk::DescriptorType::eStorageBuffer, new_area_lights);
            this->runtime->write_buffer(pick_primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_pick_primitives);
            std::array<DescriptorHandle, 9> texture_descriptors{};
            for (std::size_t index = 0; index != texture_descriptors.size(); ++index) {
                texture_descriptors[index] = this->runtime->allocate_resource_descriptor();
                this->runtime->write_buffer(texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_texture_buffers[index]);
            }
            const RasterSceneBindings bindings{primitives_descriptor, transforms_descriptor, material_descriptor, face_material_descriptor, area_light_descriptor, texture_descriptors[0], texture_descriptors[1], texture_descriptors[2], texture_descriptors[3], texture_descriptors[4], texture_descriptors[5], texture_descriptors[6], texture_descriptors[7], texture_descriptors[8], {this->texture_count, this->texture_stack_size, 0, 0}};
            new_binding_buffer                         = upload_texture_buffer(std::span{&bindings, 1});
            const DescriptorHandle bindings_descriptor = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(bindings_descriptor, vk::DescriptorType::eStorageBuffer, new_binding_buffer);
            this->runtime->release_resource_descriptor(this->primitives_descriptor);
            this->runtime->release_resource_descriptor(this->transforms_descriptor);
            this->runtime->release_resource_descriptor(this->material_descriptor);
            this->runtime->release_resource_descriptor(this->face_material_descriptor);
            this->runtime->release_resource_descriptor(this->area_light_descriptor);
            this->runtime->release_resource_descriptor(this->pick_primitives_descriptor);
            for (const DescriptorHandle descriptor : this->texture_descriptors) this->runtime->release_resource_descriptor(descriptor);
            this->runtime->release_resource_descriptor(this->bindings_descriptor);
            this->runtime->defer([primitives = std::move(this->primitive_buffer), transforms = std::move(this->transform_buffer), materials = std::move(this->material_buffer), face_materials = std::move(this->face_material_buffer), area_lights = std::move(this->area_light_buffer), pick_primitives = std::move(this->pick_primitive_buffer)]() mutable {});
            this->runtime->defer([texture_buffers = std::move(this->texture_buffers), bindings = std::move(this->binding_buffer)]() mutable {});
            this->primitives_descriptor      = primitives_descriptor;
            this->transforms_descriptor      = transforms_descriptor;
            this->material_descriptor        = material_descriptor;
            this->face_material_descriptor   = face_material_descriptor;
            this->area_light_descriptor      = area_light_descriptor;
            this->pick_primitives_descriptor = pick_primitives_descriptor;
            this->texture_descriptors        = texture_descriptors;
            this->bindings_descriptor        = bindings_descriptor;
        } else {
            this->runtime->write_buffer(this->primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            this->runtime->write_buffer(this->transforms_descriptor, vk::DescriptorType::eStorageBuffer, new_transforms);
            this->runtime->write_buffer(this->material_descriptor, vk::DescriptorType::eStorageBuffer, new_materials);
            this->runtime->write_buffer(this->face_material_descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
            this->runtime->write_buffer(this->area_light_descriptor, vk::DescriptorType::eStorageBuffer, new_area_lights);
            this->runtime->write_buffer(this->pick_primitives_descriptor, vk::DescriptorType::eStorageBuffer, new_pick_primitives);
            for (std::size_t index = 0; index != this->texture_descriptors.size(); ++index) this->runtime->write_buffer(this->texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_texture_buffers[index]);
            const RasterSceneBindings bindings{this->primitives_descriptor, this->transforms_descriptor, this->material_descriptor, this->face_material_descriptor, this->area_light_descriptor, this->texture_descriptors[0], this->texture_descriptors[1], this->texture_descriptors[2], this->texture_descriptors[3], this->texture_descriptors[4], this->texture_descriptors[5], this->texture_descriptors[6], this->texture_descriptors[7], this->texture_descriptors[8], {this->texture_count, this->texture_stack_size, 0, 0}};
            new_binding_buffer = upload_texture_buffer(std::span{&bindings, 1});
            this->runtime->write_buffer(this->bindings_descriptor, vk::DescriptorType::eStorageBuffer, new_binding_buffer);
        }
        this->primitive_buffer      = std::move(new_primitives);
        this->transform_buffer      = std::move(new_transforms);
        this->material_buffer       = std::move(new_materials);
        this->face_material_buffer  = std::move(new_face_materials);
        this->area_light_buffer     = std::move(new_area_lights);
        this->pick_primitive_buffer = std::move(new_pick_primitives);
        for (const VolumeGpuData& volume : this->volume_gpu_data) this->runtime->release_resource_descriptor(volume.majorant_descriptor);
        if (!this->volume_gpu_data.empty()) this->runtime->defer([volumes = std::move(this->volume_gpu_data), records = std::move(this->volume_buffer), lights = std::move(this->volume_light_buffer)]() mutable {});
        this->volume_gpu_data     = std::move(new_volume_gpu_data);
        this->volume_buffer       = std::move(new_volume_buffer);
        this->volume_light_buffer = std::move(new_volume_light_buffer);
        this->texture_buffers     = std::move(new_texture_buffers);
        this->binding_buffer      = std::move(new_binding_buffer);
    }

    void RasterScene::update_transforms(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        std::vector<RasterTransform> transforms = compile_raster_transforms(this->gpu_scene, scene);
        if (transforms.empty()) transforms.emplace_back();
        GpuBuffer new_transforms                     = upload_buffer(*this->runtime, command_buffer, std::span<const RasterTransform>{transforms}, vk::BufferUsageFlagBits::eStorageBuffer);
        const DescriptorHandle transforms_descriptor = this->runtime->allocate_resource_descriptor();
        this->runtime->write_buffer(transforms_descriptor, vk::DescriptorType::eStorageBuffer, new_transforms);
        const RasterSceneBindings bindings{this->primitives_descriptor, transforms_descriptor, this->material_descriptor, this->face_material_descriptor, this->area_light_descriptor, this->texture_descriptors[0], this->texture_descriptors[1], this->texture_descriptors[2], this->texture_descriptors[3], this->texture_descriptors[4], this->texture_descriptors[5], this->texture_descriptors[6], this->texture_descriptors[7], this->texture_descriptors[8], {this->texture_count, this->texture_stack_size, 0, 0}};
        GpuBuffer new_bindings                     = upload_buffer(*this->runtime, command_buffer, std::span{&bindings, 1}, vk::BufferUsageFlagBits::eStorageBuffer);
        const DescriptorHandle bindings_descriptor = this->runtime->allocate_resource_descriptor();
        this->runtime->write_buffer(bindings_descriptor, vk::DescriptorType::eStorageBuffer, new_bindings);
        this->runtime->release_resource_descriptor(this->transforms_descriptor);
        this->runtime->release_resource_descriptor(this->bindings_descriptor);
        this->runtime->defer([transforms = std::move(this->transform_buffer), bindings = std::move(this->binding_buffer)]() mutable {});
        this->transform_buffer      = std::move(new_transforms);
        this->binding_buffer        = std::move(new_bindings);
        this->transforms_descriptor = transforms_descriptor;
        this->bindings_descriptor   = bindings_descriptor;
    }

    void RasterScene::update_volumes(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.resources.volumes.size() != this->volume_gpu_data.size()) {
            this->upload(scene, &command_buffer);
            return;
        }
        for (std::size_t index = 0; index != scene.resources.volumes.size(); ++index) {
            const scene::Volume& source     = scene.resources.volumes[index];
            VolumeGpuData& volume           = this->volume_gpu_data[index];
            const render::GpuVolume& shared = this->gpu_scene.volumes[index];
            if (source.id != volume.id || shared.id != volume.id) {
                this->upload(scene, &command_buffer);
                return;
            }
            if (shared.revision.content == volume.revision.content) continue;
            if (shared.revision.topology != volume.revision.topology || !shared.dirty_region) {
                this->upload(scene, &command_buffer);
                return;
            }
            const auto descriptor = [this, &shared](const render::GpuVolumeField field) {
                const std::size_t field_index = std::to_underlying(field);
                return shared.present[field_index] ? shared.descriptors[field_index] : this->volume_zero_descriptor;
            };
            DescriptorHandle density = descriptor(render::GpuVolumeField::Density);
            DescriptorHandle sigma_a = descriptor(render::GpuVolumeField::SigmaA);
            DescriptorHandle sigma_s = descriptor(render::GpuVolumeField::SigmaS);
            std::uint32_t majorant_mode{};
            std::uint32_t majorant_flags{};
            const scene::UInt3 resolution = shared.resolution;
            if (std::holds_alternative<scene::RgbGridVolume>(source.data)) {
                majorant_mode = 1;
                if (shared.present[std::to_underlying(render::GpuVolumeField::SigmaA)]) majorant_flags |= 1u;
                if (shared.present[std::to_underlying(render::GpuVolumeField::SigmaS)]) majorant_flags |= 2u;
            } else if (!std::holds_alternative<scene::DensityGridVolume>(source.data)) {
                this->upload(scene, &command_buffer);
                return;
            }
            const scene::VolumeRegion expanded{{shared.dirty_region->minimum.x > 0 ? shared.dirty_region->minimum.x - 1 : 0, shared.dirty_region->minimum.y > 0 ? shared.dirty_region->minimum.y - 1 : 0, shared.dirty_region->minimum.z > 0 ? shared.dirty_region->minimum.z - 1 : 0}, {std::min(resolution.x, shared.dirty_region->maximum.x + 1), std::min(resolution.y, shared.dirty_region->maximum.y + 1), std::min(resolution.z, shared.dirty_region->maximum.z + 1)}};
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
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->volume_majorant_shader);
            this->runtime->bind_descriptor_heaps(command_buffer);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            const std::uint32_t brick_count = (brick_maximum[0] - brick_minimum[0]) * (brick_maximum[1] - brick_minimum[1]) * (brick_maximum[2] - brick_minimum[2]);
            command_buffer.dispatch((brick_count + 63u) / 64u, 1, 1);
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
            volume.revision = shared.revision;
        }
    }

    void RasterScene::synchronize(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.value == this->uploaded_revision.value) return;
        bool recompiled = (scene.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium)) != scene::SceneChange::None;
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None)
            for (const scene::Instance& instance : scene.resources.instances) {
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                if (std::ranges::any_of(prototype.primitives, [](const scene::Primitive& primitive) { return primitive.area_light.value != 0; })) {
                    recompiled = true;
                    break;
                }
            }
        if (recompiled) this->upload(scene, &command_buffer);
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(scene, command_buffer);
        if (!recompiled && (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) this->update_transforms(scene, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None) this->camera = scene.camera;
        if ((scene.revision.changes & scene::SceneChange::Film) != scene::SceneChange::None) this->film_exposure = scene.film.exposure;
        this->uploaded_revision = scene.revision;
    }

    Rasterizer::Rasterizer(Spectra& runtime, const RasterScene& scene, const std::filesystem::path& shader_directory) : runtime(&runtime), scene(&scene), sampled_output_descriptor(runtime.allocate_resource_descriptor()), storage_output_descriptor(runtime.allocate_resource_descriptor()), sampled_depth_descriptor(runtime.allocate_resource_descriptor()) {
        this->create_shaders(shader_directory);
    }

    Rasterizer::~Rasterizer() {
        this->runtime->release_resource_descriptor(this->sampled_output_descriptor);
        this->runtime->release_resource_descriptor(this->storage_output_descriptor);
        this->runtime->release_resource_descriptor(this->sampled_depth_descriptor);
    }

    void Rasterizer::create_shaders(const std::filesystem::path& shader_directory) {
        const std::vector<std::uint32_t> mesh_code     = render::load_spirv(shader_directory / "raster_mesh.spv");
        const std::vector<std::uint32_t> particle_code = render::load_spirv(shader_directory / "raster_particles.spv");
        const std::vector<std::uint32_t> fragment_code = render::load_spirv(shader_directory / "raster_fragment.spv");
        const std::array create_infos{
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
        this->shaders                                = vk::raii::ShaderEXTs{this->runtime->device, create_infos};
        this->particle_shader                        = vk::raii::ShaderEXT{this->runtime->device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader, vk::ShaderStageFlagBits::eMeshEXT, {}, vk::ShaderCodeTypeEXT::eSpirv, particle_code.size() * sizeof(std::uint32_t), particle_code.data(), "raster_particles"}};
        const std::vector<std::uint32_t> volume_code = render::load_spirv(shader_directory / "raster_volume.spv");
        vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
        acceleration_structure_source.pushAddressOffset = 0;
        const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{0, 0, 1, vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure, vk::DescriptorMappingSourceEXT::ePushAddress, acceleration_structure_source};
        const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
        vk::ShaderCreateInfoEXT volume_create_info{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, volume_code.size() * sizeof(std::uint32_t), volume_code.data(), "raster_volume"};
        volume_create_info.pNext = &mapping;
        this->volume_shader      = vk::raii::ShaderEXT{this->runtime->device, volume_create_info};
    }

    void Rasterizer::create_output(const vk::Extent2D extent) {
        this->output_image = this->runtime->create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage);
        this->depth_image  = this->runtime->create_image_2d(extent, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eDepth);
        this->runtime->write_sampled_image(this->sampled_output_descriptor, this->output_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->runtime->write_storage_image(this->storage_output_descriptor, this->output_image, vk::ImageLayout::eGeneral);
        this->runtime->write_sampled_image(this->sampled_depth_descriptor, this->depth_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->output_layout = vk::ImageLayout::eUndefined;
        this->depth_layout  = vk::ImageLayout::eUndefined;
    }

    void Rasterizer::prepare(const vk::Extent2D extent) {
        if (!*this->output_image.image || this->output_image.extent != extent) this->create_output(extent);
    }

    std::array<float, 16> Rasterizer::view_projection() const noexcept {
        return this->scene->camera.matrices().view_projection;
    }

    void Rasterizer::record(const vk::raii::CommandBuffer& command_buffer) {
        const std::array barriers{
            vk::ImageMemoryBarrier2{
                this->output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone
                : this->output_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader
                                                                   : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                this->output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{}
                : this->output_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite
                                                                   : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->output_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->depth_layout == vk::ImageLayout::eUndefined               ? vk::PipelineStageFlagBits2::eNone
                : this->depth_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eComputeShader
                                                                                : vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->depth_layout == vk::ImageLayout::eUndefined               ? vk::AccessFlags2{}
                : this->depth_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead
                                                                                : vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->depth_image.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()), barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            *this->output_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.01f, 0.015f, 0.025f, 1.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->depth_image.view,
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
            vk::Rect2D{{0, 0}, this->output_image.extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });

        const vk::Viewport viewport{
            0.0f,
            static_cast<float>(this->output_image.extent.height),
            static_cast<float>(this->output_image.extent.width),
            -static_cast<float>(this->output_image.extent.height),
            0.0f,
            1.0f,
        };
        const vk::Rect2D scissor{{0, 0}, this->output_image.extent};
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
        constexpr vk::Bool32 blend_enable = vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{*this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->runtime->bind_descriptor_heaps(command_buffer);

        struct alignas(16) RasterPushData {
            DescriptorHandle positions;
            DescriptorHandle normals;
            DescriptorHandle tangents;
            DescriptorHandle texture_coordinates;
            DescriptorHandle indices;
            DescriptorHandle bindings;
            std::uint32_t instance_index;
            std::uint32_t element_count;
            std::array<std::uint32_t, 2> attributes;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> camera_position;
            std::array<float, 4> camera_right;
            std::array<float, 4> camera_up;
        };
        static_assert(sizeof(RasterPushData) == 176);
        const std::array<float, 16> view_projection = this->view_projection();
        const scene::CameraFrame camera             = this->scene->camera.frame();
        for (const render::GpuDraw& draw : this->scene->gpu_scene.draws) {
            if (draw.kind != render::GpuDrawKind::Geometry) continue;
            const render::GpuGeometry& mesh = this->scene->gpu_scene.geometries[draw.resource_index];
            const RasterPushData push_data{
                mesh.positions_descriptor,
                mesh.normals_descriptor,
                mesh.tangents_descriptor,
                mesh.texture_coordinates_descriptor,
                mesh.indices_descriptor,
                this->scene->bindings_descriptor,
                draw.instance_index,
                mesh.index_count / 3u,
                {mesh.attribute_flags, 0},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                {camera.position.x, camera.position.y, camera.position.z, 1.0f},
                {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                {camera.up.x, camera.up.y, camera.up.z, 0.0f},
            };
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        }
        const std::array particle_shader_handles{*this->particle_shader, *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, particle_shader_handles);
        for (const render::GpuDraw& draw : this->scene->gpu_scene.draws) {
            if (draw.kind != render::GpuDrawKind::ParticleSet) continue;
            const render::GpuParticleSet& particles = this->scene->gpu_scene.particle_sets[draw.resource_index];
            const RasterPushData push_data{
                particles.positions_descriptor,
                particles.radii_descriptor,
                particles.colors_descriptor,
                particles.temperatures_descriptor,
                particles.materials_descriptor,
                this->scene->bindings_descriptor,
                draw.instance_index,
                particles.particle_count,
                {particles.attribute_flags, 0},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
                {camera.position.x, camera.position.y, camera.position.z, 1.0f},
                {camera.right.x, camera.right.y, camera.right.z, 0.0f},
                {camera.up.x, camera.up.y, camera.up.z, 0.0f},
            };
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        }
        command_buffer.endRendering();
        if (this->scene->volumes_count == 0) {
            this->output_layout = vk::ImageLayout::eColorAttachmentOptimal;
            this->depth_layout  = vk::ImageLayout::eDepthAttachmentOptimal;
            return;
        }
        const std::array volume_barriers{
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->output_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eDepthAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->depth_image.image, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}},
        };
        const vk::MemoryBarrier2 acceleration_structure_barrier{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &acceleration_structure_barrier, 0, nullptr, static_cast<std::uint32_t>(volume_barriers.size()), volume_barriers.data()});
        struct alignas(16) VolumePushData {
            vk::DeviceAddress acceleration_structure_address;
            DescriptorHandle output;
            DescriptorHandle depth;
            DescriptorHandle volumes;
            DescriptorHandle lights;
            std::array<std::uint32_t, 2> reserved;
            std::array<std::uint32_t, 4> metadata;
            std::array<float, 4> inverse_view_projection_row_0;
            std::array<float, 4> inverse_view_projection_row_1;
            std::array<float, 4> inverse_view_projection_row_2;
            std::array<float, 4> inverse_view_projection_row_3;
        };
        static_assert(sizeof(VolumePushData) == 128);
        const std::array<float, 16> inverse_view_projection = this->scene->camera.matrices().inverse_view_projection;
        const VolumePushData volume_push_data{this->scene->gpu_scene.top_level_acceleration_structure.address, this->storage_output_descriptor, this->sampled_depth_descriptor, this->scene->volumes_descriptor, this->scene->volume_lights_descriptor, {}, {this->output_image.extent.width, this->output_image.extent.height, this->scene->volumes_count, this->scene->volume_lights_count}, {inverse_view_projection[0], inverse_view_projection[1], inverse_view_projection[2], inverse_view_projection[3]}, {inverse_view_projection[4], inverse_view_projection[5], inverse_view_projection[6], inverse_view_projection[7]}, {inverse_view_projection[8], inverse_view_projection[9], inverse_view_projection[10], inverse_view_projection[11]}, {inverse_view_projection[12], inverse_view_projection[13], inverse_view_projection[14], inverse_view_projection[15]}};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->volume_shader);
        this->runtime->bind_descriptor_heaps(command_buffer);
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&volume_push_data, 1}));
        command_buffer.dispatch((this->output_image.extent.width + 7) / 8, (this->output_image.extent.height + 7) / 8, 1);
        this->output_layout = vk::ImageLayout::eGeneral;
        this->depth_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    render::RenderOutput Rasterizer::output() const noexcept {
        return {
            this->output_image,
            this->sampled_output_descriptor,
            this->output_layout,
            this->output_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            this->output_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite : vk::AccessFlagBits2::eColorAttachmentWrite,
            this->scene->film_exposure,
        };
    }
} // namespace spectra::rasterizer
