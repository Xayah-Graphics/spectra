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
    static_assert(sizeof(GpuPickPrimitive) == 48);

    namespace {
        constexpr std::uint32_t invalid_raster_index =
            std::numeric_limits<std::uint32_t>::max();

        [[nodiscard]] scene::Float3 raster_linear_srgb(
            const scene::Float3 value,
            const scene::SpectrumColorSpace color_space) noexcept {
            if (color_space ==
                scene::SpectrumColorSpace::Rec2020)
                return {
                    1.660491f * value.x -
                        0.587641f * value.y -
                        0.072850f * value.z,
                    -0.124550f * value.x +
                        1.132900f * value.y -
                        0.008349f * value.z,
                    -0.018151f * value.x -
                        0.100579f * value.y +
                        1.118730f * value.z};
            if (color_space ==
                scene::SpectrumColorSpace::Aces2065_1)
                return {
                    2.521686f * value.x -
                        1.134130f * value.y -
                        0.387556f * value.z,
                    -0.276479f * value.x +
                        1.372719f * value.y -
                        0.096240f * value.z,
                    -0.015378f * value.x -
                        0.152975f * value.y +
                        1.168353f * value.z};
            return value;
        }

        [[nodiscard]] scene::Float3 raster_spectrum_rgb(
            const scene::SpectrumParameter& parameter) {
            if (
                parameter.encoding ==
                    scene::SpectrumEncoding::RgbAlbedo ||
                parameter.encoding ==
                    scene::SpectrumEncoding::RgbUnbounded ||
                parameter.encoding ==
                    scene::SpectrumEncoding::RgbIlluminant)
                return raster_linear_srgb(
                    parameter.value,
                    parameter.color_space);
            if (
                parameter.encoding ==
                scene::SpectrumEncoding::Constant)
                return {
                    parameter.scalar,
                    parameter.scalar,
                    parameter.scalar};
            if (
                parameter.encoding ==
                scene::SpectrumEncoding::Blackbody) {
                const scene::BlackbodySpectrum spectrum{
                    parameter.temperature};
                return {
                    spectrum.evaluate(610.0f),
                    spectrum.evaluate(550.0f),
                    spectrum.evaluate(460.0f)};
            }
            const scene::PiecewiseLinearSpectrum spectrum{
                parameter.wavelengths,
                parameter.samples};
            return {
                spectrum.evaluate(610.0f),
                spectrum.evaluate(550.0f),
                spectrum.evaluate(460.0f)};
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

        [[nodiscard]] std::uint32_t raster_texture_source_index(
            const scene::SceneView scene,
            const scene::TextureId id) {
            return static_cast<std::uint32_t>(
                std::ranges::find(
                    scene.resources.textures,
                    id,
                    &scene::Texture::id) -
                scene.resources.textures.begin());
        }

        [[nodiscard]] std::uint32_t compile_raster_mapping(
            std::vector<RasterTextureMapping>& mappings,
            const scene::TextureMapping& mapping) {
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
                        result.metadata[0] = std::same_as<std::remove_cvref_t<decltype(data)>, scene::PlanarTextureMapping> ? 1u : std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphericalTextureMapping> ? 2u : 3u;
                        const std::array<float, 16>& transform = data.texture_from_render.matrix;
                        result.transform_row_0 = {transform[0], transform[1], transform[2], transform[3]};
                        result.transform_row_1 = {transform[4], transform[5], transform[6], transform[7]};
                        result.transform_row_2 = {transform[8], transform[9], transform[10], transform[11]};
                        result.transform_row_3 = {transform[12], transform[13], transform[14], transform[15]};
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

        [[nodiscard]] std::uint32_t compile_raster_checkerboard_mapping(
            std::vector<RasterTextureMapping>& mappings,
            const scene::CheckerboardMapping& mapping) {
            if (const scene::TextureMapping* two_dimensional = std::get_if<scene::TextureMapping>(&mapping.data))
                return compile_raster_mapping(mappings, *two_dimensional);
            const std::array<float, 16>& transform = std::get<scene::TextureMapping3D>(mapping.data).texture_from_render.matrix;
            RasterTextureMapping result{};
            result.metadata[0] = 4;
            result.transform_row_0 = {transform[0], transform[1], transform[2], transform[3]};
            result.transform_row_1 = {transform[4], transform[5], transform[6], transform[7]};
            result.transform_row_2 = {transform[8], transform[9], transform[10], transform[11]};
            result.transform_row_3 = {transform[12], transform[13], transform[14], transform[15]};
            const std::uint32_t index = static_cast<std::uint32_t>(mappings.size());
            mappings.push_back(result);
            return index;
        }

        [[nodiscard]] RasterTextureCompilation compile_raster_textures(
            const render::GpuAssetCache& assets,
            const scene::SceneView scene) {
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
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                            visit(raster_texture_source_index(scene, data.first));
                            visit(raster_texture_source_index(scene, data.second));
                            visit(raster_texture_source_index(scene, data.amount));
                        }
                    },
                    scene.resources.textures[index].data);
                marks[index] = 2;
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
                            stack_size = std::max(
                                stack_sizes[raster_texture_source_index(scene, data.first)],
                                1u + stack_sizes[raster_texture_source_index(scene, data.second)]);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>)
                            stack_size = std::max({
                                stack_sizes[raster_texture_source_index(scene, data.first)],
                                1u + stack_sizes[raster_texture_source_index(scene, data.second)],
                                2u + stack_sizes[raster_texture_source_index(scene, data.amount)]});
                    },
                    scene.resources.textures[index].data);
                stack_sizes[index] = stack_size;
                result.maximum_stack_size = std::max(result.maximum_stack_size, stack_size);
            }
            if (result.maximum_stack_size > 32)
                throw std::runtime_error(std::format("Raster Texture program requires {} local values; the explicit Vulkan preview contract permits 32", result.maximum_stack_size));
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
                            local_index = static_cast<std::uint32_t>(result.constants.size());
                            const scene::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? scene::Float3{data.scalar, data.scalar, data.scalar} : raster_spectrum_rgb(data.spectrum);
                            result.constants.push_back(RasterConstantTexture{{value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalar : 1.0f}});
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                            kind = 1;
                            local_index = static_cast<std::uint32_t>(result.images.size());
                            const render::GpuTextureImage&
                                runtime =
                                    assets.texture_image(
                                        texture,
                                        texture.spectrum_type ==
                                                scene::
                                                    TextureSpectrumType::
                                                        Albedo
                                            ? vk::Format::
                                                eR16G16B16A16Sfloat
                                            : vk::Format::
                                                eR32G32B32A32Sfloat);
                            result.images.push_back(RasterImageTexture{
                                runtime.image_descriptor,
                                runtime.sampler_descriptor,
                                {
                                    compile_raster_mapping(result.mappings, data.mapping),
                                    static_cast<std::uint32_t>(data.channel),
                                    static_cast<std::uint32_t>(data.filter),
                                    data.invert ? 1u : 0u},
                                {
                                    data.scale,
                                    static_cast<float>(std::to_underlying(texture.color_space)),
                                    data.maximum_anisotropy,
                                    static_cast<float>(std::to_underlying(data.wrap))}});
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture>) {
                            kind = 2;
                            local_index = static_cast<std::uint32_t>(result.checkerboards.size());
                            result.checkerboards.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), compile_raster_checkerboard_mapping(result.mappings, data.mapping), 0}});
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture>) {
                            kind = 3;
                            local_index = static_cast<std::uint32_t>(result.scales.size());
                            result.scales.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), 0, 0}});
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                            kind = 4;
                            local_index = static_cast<std::uint32_t>(result.mixes.size());
                            result.mixes.push_back(RasterCompositeTexture{{handle(data.first), handle(data.second), handle(data.amount), 0}});
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                            kind = 5;
                            local_index = static_cast<std::uint32_t>(result.direction_mixes.size());
                            result.direction_mixes.push_back(RasterDirectionMixTexture{{handle(data.first), handle(data.second), 0, 0}, {data.direction.x, data.direction.y, data.direction.z, 0.0f}});
                        }
                        else {
                            kind = 6;
                            local_index = static_cast<std::uint32_t>(result.bilerps.size());
                            RasterBilerpTexture compiled{};
                            compiled.data[0] = compile_raster_mapping(result.mappings, data.mapping);
                            for (std::uint32_t corner = 0; corner != 4; ++corner) {
                                const scene::Float3 value = texture.value_kind == scene::TextureValueKind::Float ? scene::Float3{data.scalars[corner], data.scalars[corner], data.scalars[corner]} : raster_spectrum_rgb(data.spectra[corner]);
                                compiled.values[corner] = {value.x, value.y, value.z, texture.value_kind == scene::TextureValueKind::Float ? data.scalars[corner] : 1.0f};
                            }
                            result.bilerps.push_back(compiled);
                        }
                    },
                    texture.data);
                result.headers.push_back(RasterTextureHeader{{
                    kind,
                    local_index,
                    static_cast<std::uint32_t>(texture.value_kind),
                    static_cast<std::uint32_t>(texture.spectrum_type)}, {}, {}});
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
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
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
                root.program[0] = root_count + static_cast<std::uint32_t>(program.size());
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

        [[nodiscard]] std::vector<RasterMaterial> compile_raster_materials(
            const scene::SceneView scene,
            const RasterTextureCompilation& textures) {
            const auto texture_handle = [&textures, scene](const scene::TextureId id) {
                if (id.value == 0) return invalid_raster_index;
                return textures.handles[raster_texture_source_index(scene, id)];
            };
            const auto spectrum =
                [&texture_handle](
                    std::array<float, 4>& value,
                    std::array<std::uint32_t, 4>& data,
                    const scene::SpectrumParameter&
                        parameter) {
                const scene::Float3 rgb =
                    raster_spectrum_rgb(
                        parameter);
                value = {
                    rgb.x,
                    rgb.y,
                    rgb.z,
                    0.0f};
                data[0] =
                    texture_handle(
                        parameter.texture);
            };
            const auto material_index = [scene](const scene::MaterialId id) {
                return static_cast<std::uint32_t>(
                    std::ranges::find(scene.resources.materials, id, &scene::MaterialResource::id) -
                    scene.resources.materials.begin());
            };
            std::vector<RasterMaterial> result{};
            result.reserve(scene.resources.materials.size());
            for (const scene::MaterialResource& material : scene.resources.materials) {
                RasterMaterial compiled{};
                compiled.metadata[1] = invalid_raster_index;
                compiled.metadata[2] = invalid_raster_index;
                compiled.spectrum_data_0[0] =
                    invalid_raster_index;
                compiled.spectrum_data_1[0] =
                    invalid_raster_index;
                compiled.spectrum_data_2[0] =
                    invalid_raster_index;
                compiled.spectrum_data_3[0] =
                    invalid_raster_index;
                compiled.scalar_textures_0.fill(invalid_raster_index);
                compiled.scalar_textures_1.fill(invalid_raster_index);
                std::visit(
                    [&](const auto& data) {
                        if constexpr (requires { data.normal_map; data.bump_map; }) {
                            compiled.metadata[1] = texture_handle(data.normal_map);
                            compiled.metadata[2] = texture_handle(data.bump_map);
                        }
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>)
                            compiled.metadata[0] = 0;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseMaterialData>) {
                            compiled.metadata[0] = 1;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                            compiled.metadata[0] = 2;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                            spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.transmittance);
                            compiled.scalar_values_0[0] = data.scale;
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                            compiled.metadata[0] = 3;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            std::visit(
                                [&](const auto& optics) {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                        spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.eta);
                                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.k);
                                    }
                                    else {
                                        compiled.metadata[3] |= 2u;
                                        spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, optics.reflectance);
                                    }
                                },
                                data.optics);
                            const scene::FloatParameter& u = data.distribution.u_roughness.value_or(data.distribution.roughness);
                            const scene::FloatParameter& v = data.distribution.v_roughness.value_or(data.distribution.roughness);
                            compiled.scalar_values_0 = {u.value, v.value, 0.0f, 0.0f};
                            compiled.scalar_textures_0 = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DielectricMaterialData>) {
                            compiled.metadata[0] = 4;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.eta);
                            const scene::FloatParameter& u = data.distribution.u_roughness.value_or(data.distribution.roughness);
                            const scene::FloatParameter& v = data.distribution.v_roughness.value_or(data.distribution.roughness);
                            compiled.scalar_values_0 = {u.value, v.value, 0.0f, 0.0f};
                            compiled.scalar_textures_0 = {texture_handle(u.texture), texture_handle(v.texture), invalid_raster_index, invalid_raster_index};
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                            compiled.metadata[0] = 5;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.eta);
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                            compiled.metadata[0] = 6;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.reflectance);
                            spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, data.eta);
                            spectrum(compiled.spectrum_value_2, compiled.spectrum_data_2, data.coating.albedo);
                            const scene::FloatParameter& u = data.interface.u_roughness.value_or(data.interface.roughness);
                            const scene::FloatParameter& v = data.interface.v_roughness.value_or(data.interface.roughness);
                            compiled.scalar_values_0 = {u.value, v.value, data.coating.thickness.value, data.coating.g.value};
                            compiled.scalar_textures_0 = {texture_handle(u.texture), texture_handle(v.texture), texture_handle(data.coating.thickness.texture), texture_handle(data.coating.g.texture)};
                        }
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                            compiled.metadata[0] = 7;
                            compiled.metadata[3] = data.remap_roughness ? 1u : 0u;
                            spectrum(compiled.spectrum_value_0, compiled.spectrum_data_0, data.interface_eta);
                            std::visit(
                                [&](const auto& optics) {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                        spectrum(compiled.spectrum_value_1, compiled.spectrum_data_1, optics.eta);
                                        spectrum(compiled.spectrum_value_2, compiled.spectrum_data_2, optics.k);
                                    }
                                    else {
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
                            compiled.scalar_values_0 = {interface_u.value, interface_v.value, conductor_u.value, conductor_v.value};
                            compiled.scalar_textures_0 = {texture_handle(interface_u.texture), texture_handle(interface_v.texture), texture_handle(conductor_u.texture), texture_handle(conductor_v.texture)};
                            compiled.scalar_values_1 = {data.coating.thickness.value, data.coating.g.value, 0.0f, 0.0f};
                            compiled.scalar_textures_1 = {texture_handle(data.coating.thickness.texture), texture_handle(data.coating.g.texture), invalid_raster_index, invalid_raster_index};
                        }
                        else {
                            compiled.metadata[0] = 8;
                            compiled.materials[0] = material_index(data.first);
                            compiled.materials[1] = material_index(data.second);
                            compiled.scalar_values_0[0] = data.amount.value;
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
        [[nodiscard]] GpuBuffer upload_buffer(GpuDevice& gpu, const std::span<const Element> elements, const vk::BufferUsageFlags usage) {
            GpuBuffer staging = gpu.create_buffer(
                elements.size_bytes(),
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                true);
            std::memcpy(staging.mapped, elements.data(), elements.size_bytes());
            GpuBuffer destination = gpu.create_buffer(
                elements.size_bytes(),
                usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                false);
            gpu.immediate([&staging, &destination, usage](const vk::raii::CommandBuffer& command_buffer) {
                command_buffer.copyBuffer(*staging.buffer, *destination.buffer, vk::BufferCopy{0, 0, staging.size});
                const vk::BufferMemoryBarrier2 upload_dependency{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR)
                        ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR
                        : vk::PipelineStageFlagBits2::eAllCommands,
                    static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR)
                        ? vk::AccessFlagBits2::eAccelerationStructureReadKHR
                        : vk::AccessFlagBits2::eShaderStorageRead,
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

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(
            GpuDevice& gpu,
            const vk::raii::CommandBuffer& command_buffer,
            const std::span<const Element> elements,
            const vk::BufferUsageFlags usage) {
            GpuBuffer destination = gpu.create_buffer(
                elements.size_bytes(),
                usage |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                false);
            const GpuUploadSlice upload =
                gpu.stage_upload(std::as_bytes(elements));
            command_buffer.copyBuffer(
                upload.buffer,
                *destination.buffer,
                vk::BufferCopy{
                    upload.offset,
                    0,
                    upload.size,
                });
            const vk::BufferMemoryBarrier2 dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                static_cast<bool>(
                    usage &
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR)
                    ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR
                    : vk::PipelineStageFlagBits2::eAllCommands,
                static_cast<bool>(
                    usage &
                    vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR)
                    ? vk::AccessFlagBits2::eAccelerationStructureReadKHR
                    : vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *destination.buffer,
                0,
                destination.size,
            };
            command_buffer.pipelineBarrier2(
                vk::DependencyInfo{
                    {},
                    0,
                    nullptr,
                    1,
                    &dependency,
                });
            return destination;
        }

        [[nodiscard]] std::vector<RasterTransform> compile_raster_transforms(
            const render::GpuAssetCache& assets,
            const scene::SceneView scene) {
            std::vector<RasterTransform> transforms{};
            transforms.reserve(assets.draws.size());
            for (const render::GpuDraw& draw : assets.draws) {
                const scene::Instance& instance = scene.resources.instances[draw.scene_instance_index];
                const scene::Prototype& prototype =
                    *std::ranges::find(
                        scene.resources.prototypes,
                        instance.prototype,
                        &scene::Prototype::id);
                const scene::Transform transform = instance.transform * prototype.primitives[draw.prototype_primitive_index].transform;
                transforms.push_back(
                    RasterTransform{
                        {
                            transform.matrix[0],
                            transform.matrix[1],
                            transform.matrix[2],
                            transform.matrix[3]},
                        {
                            transform.matrix[4],
                            transform.matrix[5],
                            transform.matrix[6],
                            transform.matrix[7]},
                        {
                            transform.matrix[8],
                            transform.matrix[9],
                            transform.matrix[10],
                            transform.matrix[11]},
                        {
                            transform.matrix[12],
                            transform.matrix[13],
                            transform.matrix[14],
                            transform.matrix[15]}});
            }
            return transforms;
        }

    } // namespace

    RasterScene::RasterScene(
        GpuDevice& gpu,
        const render::GpuAssetCache& assets,
        const scene::SceneView scene)
        : gpu(&gpu),
          asset_cache(&assets),
          primitive_descriptor(
              gpu.allocate_resource_descriptor()),
          transform_descriptor(
              gpu.allocate_resource_descriptor()),
          material_descriptor(
              gpu.allocate_resource_descriptor()),
          face_material_descriptor(
              gpu.allocate_resource_descriptor()),
          area_light_descriptor(
              gpu.allocate_resource_descriptor()),
          pick_primitive_descriptor(
              gpu.allocate_resource_descriptor()),
          binding_descriptor(
              gpu.allocate_resource_descriptor()),
          scene_camera(scene.camera),
          uploaded_revision(scene.revision),
          assets(assets),
          primitives_descriptor(this->primitive_descriptor),
          transforms_descriptor(this->transform_descriptor),
          pick_primitives_descriptor(this->pick_primitive_descriptor),
          bindings_descriptor(this->binding_descriptor),
          camera(this->scene_camera) {
        for (DescriptorHandle& descriptor :
             this->texture_descriptors)
            descriptor =
                gpu.allocate_resource_descriptor();
        this->upload(scene);
    }

    RasterScene::~RasterScene() {
        this->gpu->release_resource_descriptor(
            this->primitive_descriptor);
        this->gpu->release_resource_descriptor(
            this->transform_descriptor);
        this->gpu->release_resource_descriptor(
            this->material_descriptor);
        this->gpu->release_resource_descriptor(
            this->face_material_descriptor);
        this->gpu->release_resource_descriptor(
            this->area_light_descriptor);
        this->gpu->release_resource_descriptor(
            this->pick_primitive_descriptor);
        for (const DescriptorHandle descriptor :
             this->texture_descriptors)
            this->gpu->release_resource_descriptor(
                descriptor);
        this->gpu->release_resource_descriptor(
            this->binding_descriptor);
    }

    void RasterScene::upload(
        const scene::SceneView scene,
        const vk::raii::CommandBuffer* command_buffer) {
        RasterTextureCompilation textures =
            compile_raster_textures(
                *this->asset_cache,
                scene);
        this->texture_count = static_cast<std::uint32_t>(scene.resources.textures.size());
        this->texture_stack_size = textures.maximum_stack_size;
        std::vector<RasterMaterial> materials =
            compile_raster_materials(
                scene,
                textures);

        std::vector<RasterAreaLight> area_lights{};
        area_lights.reserve(
            scene.resources.lights.size());
        for (const scene::Light& light :
             scene.resources.lights) {
            scene::Float3 emission{};
            if (const scene::DiffuseAreaLight* area =
                    std::get_if<
                        scene::DiffuseAreaLight>(
                        &light.data)) {
                emission =
                    raster_spectrum_rgb(
                        area->radiance);
                emission = {
                    emission.x *
                        area->scale,
                    emission.y *
                        area->scale,
                    emission.z *
                        area->scale};
            }
            area_lights.push_back(
                RasterAreaLight{{
                    emission.x,
                    emission.y,
                    emission.z,
                    0.0f}});
        }
        if (area_lights.empty())
            area_lights.emplace_back();

        std::vector<RasterPrimitive> primitives{};
        std::vector<RasterTransform> transforms =
            compile_raster_transforms(
                *this->asset_cache,
                scene);
        std::vector<std::uint32_t>
            face_materials{};
        std::vector<GpuPickPrimitive>
            pick_primitives{};
        primitives.reserve(this->asset_cache->draws.size());
        for (const render::GpuDraw& draw :
             this->asset_cache->draws) {
            const scene::Instance& instance =
                scene.resources.instances[
                    draw.scene_instance_index];
            const scene::Prototype& prototype =
                *std::ranges::find(
                    scene.resources.prototypes,
                    instance.prototype,
                    &scene::Prototype::id);
            const scene::Primitive& primitive =
                prototype.primitives[
                    draw.prototype_primitive_index];
            const bool particle_draw =
                draw.kind ==
                render::GpuDrawKind::ParticleSet;
            const scene::ParticleSet* particle_set =
                particle_draw
                    ? &*std::ranges::find(
                          scene.resources
                              .particle_sets,
                          primitive.particles,
                          &scene::ParticleSet::id)
                    : nullptr;
            GpuPickPrimitive pick_primitive{};
            if (particle_draw) {
                const render::GpuParticleSet&
                    gpu_particles =
                        this->asset_cache
                            ->particle_sets[
                                draw.resource_index];
                pick_primitive.metadata[0] = 6;
                pick_primitive.positions =
                    gpu_particles
                        .positions_descriptor;
                pick_primitive.radii =
                    gpu_particles
                        .radii_descriptor;
            }
            else {
                const scene::Geometry& geometry =
                    *std::ranges::find(
                        scene.resources
                            .geometries,
                        primitive.geometry,
                        &scene::Geometry::id);
                std::visit(
                    [
                        &pick_primitive
                    ](
                        const auto& data) {
                        if constexpr (
                            std::same_as<
                                std::remove_cvref_t<
                                    decltype(data)>,
                                scene::SphereGeometry>) {
                            pick_primitive
                                .metadata[0] = 1;
                            pick_primitive.parameters = {
                                data.radius,
                                data.z_min,
                                data.z_max,
                                data.phi_max *
                                    std::numbers::pi_v<
                                        float> /
                                    180.0f};
                        }
                        else if constexpr (
                            std::same_as<
                                std::remove_cvref_t<
                                    decltype(data)>,
                                scene::DiskGeometry>) {
                            pick_primitive
                                .metadata[0] = 2;
                            pick_primitive.parameters = {
                                data.height,
                                data.radius,
                                data.inner_radius,
                                data.phi_max *
                                    std::numbers::pi_v<
                                        float> /
                                    180.0f};
                        }
                        else if constexpr (
                            std::same_as<
                                std::remove_cvref_t<
                                    decltype(data)>,
                                scene::CylinderGeometry>) {
                            pick_primitive
                                .metadata[0] = 3;
                            pick_primitive.parameters = {
                                data.radius,
                                data.z_min,
                                data.z_max,
                                data.phi_max *
                                    std::numbers::pi_v<
                                        float> /
                                    180.0f};
                        }
                    },
                    geometry.data);
            }
            pick_primitives.push_back(
                pick_primitive);
            const std::uint32_t
                face_material_offset =
                    static_cast<std::uint32_t>(
                        face_materials.size());
            for (
                const scene::MaterialId
                    face_material :
                particle_draw
                    ? std::span<
                          const scene::MaterialId>{
                          particle_set
                              ->particle_materials}
                    : std::span<
                          const scene::MaterialId>{
                          primitive.face_materials}) {
                const std::vector<
                    scene::MaterialResource>::
                    const_iterator resource =
                        std::ranges::find(
                            scene.resources.materials,
                            face_material,
                            &scene::MaterialResource::id);
                face_materials.push_back(
                    static_cast<std::uint32_t>(
                        resource -
                        scene.resources
                            .materials.begin()));
            }
            const std::vector<
                scene::MaterialResource>::
                const_iterator material =
                    std::ranges::find(
                        scene.resources.materials,
                        particle_draw
                            ? particle_set->material
                            : primitive.material,
                        &scene::MaterialResource::id);
            std::uint32_t area_light =
                std::numeric_limits<
                    std::uint32_t>::max();
            if (primitive.area_light.value != 0) {
                const std::vector<scene::Light>::
                    const_iterator light =
                        std::ranges::find(
                            scene.resources.lights,
                            primitive.area_light,
                            &scene::Light::id);
                area_light =
                    static_cast<std::uint32_t>(
                        light -
                        scene.resources.lights.begin());
            }
            const std::uint32_t transform_index =
                static_cast<std::uint32_t>(
                    primitives.size());
            primitives.push_back(
                RasterPrimitive{
                    transform_index,
                    static_cast<std::uint32_t>(
                        material -
                        scene.resources.materials.begin()),
                    area_light,
                    primitive.reverse_orientation
                        ? 1u
                        : 0u,
                    face_material_offset,
                    static_cast<std::uint32_t>(
                        particle_draw
                            ? particle_set
                                  ->particle_materials
                                  .size()
                            : primitive
                                  .face_materials
                                  .size()),
                    {}});
        }
        if (primitives.empty()) {
            primitives.emplace_back();
            transforms.emplace_back();
        }
        if (face_materials.empty())
            face_materials.emplace_back();
        if (pick_primitives.empty())
            pick_primitives.emplace_back();

        GpuBuffer new_primitives =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const RasterPrimitive>{
                          primitives},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const RasterPrimitive>{
                          primitives},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        GpuBuffer new_transforms =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const RasterTransform>{
                          transforms},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const RasterTransform>{
                          transforms},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        GpuBuffer new_materials =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const RasterMaterial>{
                          materials},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const RasterMaterial>{
                          materials},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        GpuBuffer new_face_materials =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const std::uint32_t>{
                          face_materials},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const std::uint32_t>{
                          face_materials},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        GpuBuffer new_area_lights =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const RasterAreaLight>{
                          area_lights},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const RasterAreaLight>{
                          area_lights},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        GpuBuffer new_pick_primitives =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const GpuPickPrimitive>{
                          pick_primitives},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const GpuPickPrimitive>{
                          pick_primitives},
                      vk::BufferUsageFlagBits::
                          eStorageBuffer);
        const auto upload_texture_buffer =
            [this, command_buffer]<typename Element>(
                const std::span<const Element> elements) {
                return command_buffer
                    ? upload_buffer(
                          *this->gpu,
                          *command_buffer,
                          elements,
                          vk::BufferUsageFlagBits::
                              eStorageBuffer)
                    : upload_buffer(
                          *this->gpu,
                          elements,
                          vk::BufferUsageFlagBits::
                              eStorageBuffer);
            };
        std::array<GpuBuffer, 9> new_texture_buffers{};
        new_texture_buffers[0] =
            upload_texture_buffer(
                std::span<
                    const RasterTextureHeader>{
                    textures.headers});
        new_texture_buffers[1] =
            upload_texture_buffer(
                std::span<
                    const RasterTextureMapping>{
                    textures.mappings});
        new_texture_buffers[2] =
            upload_texture_buffer(
                std::span<
                    const RasterConstantTexture>{
                    textures.constants});
        new_texture_buffers[3] =
            upload_texture_buffer(
                std::span<
                    const RasterImageTexture>{
                    textures.images});
        new_texture_buffers[4] =
            upload_texture_buffer(
                std::span<
                    const RasterCompositeTexture>{
                    textures.checkerboards});
        new_texture_buffers[5] =
            upload_texture_buffer(
                std::span<
                    const RasterCompositeTexture>{
                    textures.scales});
        new_texture_buffers[6] =
            upload_texture_buffer(
                std::span<
                    const RasterCompositeTexture>{
                    textures.mixes});
        new_texture_buffers[7] =
            upload_texture_buffer(
                std::span<
                    const RasterDirectionMixTexture>{
                    textures.direction_mixes});
        new_texture_buffers[8] =
            upload_texture_buffer(
                std::span<
                    const RasterBilerpTexture>{
                    textures.bilerps});
        GpuBuffer new_binding_buffer{};
        if (command_buffer) {
            const DescriptorHandle primitive_descriptor =
                this->gpu->allocate_resource_descriptor();
            const DescriptorHandle transform_descriptor =
                this->gpu->allocate_resource_descriptor();
            const DescriptorHandle material_descriptor =
                this->gpu->allocate_resource_descriptor();
            const DescriptorHandle face_material_descriptor =
                this->gpu->allocate_resource_descriptor();
            const DescriptorHandle area_light_descriptor =
                this->gpu->allocate_resource_descriptor();
            const DescriptorHandle pick_primitive_descriptor =
                this->gpu->allocate_resource_descriptor();
            this->gpu->write_buffer(
                primitive_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_primitives);
            this->gpu->write_buffer(
                transform_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_transforms);
            this->gpu->write_buffer(
                material_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_materials);
            this->gpu->write_buffer(
                face_material_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_face_materials);
            this->gpu->write_buffer(
                area_light_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_area_lights);
            this->gpu->write_buffer(
                pick_primitive_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_pick_primitives);
            std::array<DescriptorHandle, 9>
                texture_descriptors{};
            for (std::size_t index = 0;
                 index != texture_descriptors.size();
                 ++index) {
                texture_descriptors[index] =
                    this->gpu
                        ->allocate_resource_descriptor();
                this->gpu->write_buffer(
                    texture_descriptors[index],
                    vk::DescriptorType::eStorageBuffer,
                    new_texture_buffers[index]);
            }
            const RasterSceneBindings bindings{
                primitive_descriptor,
                transform_descriptor,
                material_descriptor,
                face_material_descriptor,
                area_light_descriptor,
                texture_descriptors[0],
                texture_descriptors[1],
                texture_descriptors[2],
                texture_descriptors[3],
                texture_descriptors[4],
                texture_descriptors[5],
                texture_descriptors[6],
                texture_descriptors[7],
                texture_descriptors[8],
                {
                    this->texture_count,
                    this->texture_stack_size,
                    0,
                    0}};
            new_binding_buffer =
                upload_texture_buffer(
                    std::span{
                        &bindings,
                        1});
            const DescriptorHandle
                binding_descriptor =
                    this->gpu
                        ->allocate_resource_descriptor();
            this->gpu->write_buffer(
                binding_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_binding_buffer);
            this->gpu->release_resource_descriptor(
                this->primitive_descriptor);
            this->gpu->release_resource_descriptor(
                this->transform_descriptor);
            this->gpu->release_resource_descriptor(
                this->material_descriptor);
            this->gpu->release_resource_descriptor(
                this->face_material_descriptor);
            this->gpu->release_resource_descriptor(
                this->area_light_descriptor);
            this->gpu->release_resource_descriptor(
                this->pick_primitive_descriptor);
            for (const DescriptorHandle descriptor :
                 this->texture_descriptors)
                this->gpu
                    ->release_resource_descriptor(
                        descriptor);
            this->gpu->release_resource_descriptor(
                this->binding_descriptor);
            this->gpu->defer(
                [
                    primitives =
                        std::move(
                            this->primitive_buffer),
                    transforms =
                        std::move(
                            this->transform_buffer),
                    materials =
                        std::move(
                            this->material_buffer),
                    face_materials =
                        std::move(
                            this->face_material_buffer),
                    area_lights =
                        std::move(
                            this->area_light_buffer),
                    pick_primitives =
                        std::move(
                            this->pick_primitive_buffer)]() mutable {});
            this->gpu->defer(
                [
                    texture_buffers =
                        std::move(
                            this->texture_buffers),
                    bindings =
                        std::move(
                            this->binding_buffer)]() mutable {});
            this->primitive_descriptor =
                primitive_descriptor;
            this->transform_descriptor =
                transform_descriptor;
            this->material_descriptor =
                material_descriptor;
            this->face_material_descriptor =
                face_material_descriptor;
            this->area_light_descriptor =
                area_light_descriptor;
            this->pick_primitive_descriptor =
                pick_primitive_descriptor;
            this->texture_descriptors =
                texture_descriptors;
            this->binding_descriptor =
                binding_descriptor;
        }
        else {
            this->gpu->write_buffer(
                this->primitive_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_primitives);
            this->gpu->write_buffer(
                this->transform_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_transforms);
            this->gpu->write_buffer(
                this->material_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_materials);
            this->gpu->write_buffer(
                this->face_material_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_face_materials);
            this->gpu->write_buffer(
                this->area_light_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_area_lights);
            this->gpu->write_buffer(
                this->pick_primitive_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_pick_primitives);
            for (std::size_t index = 0;
                 index !=
                 this->texture_descriptors.size();
                 ++index)
                this->gpu->write_buffer(
                    this->texture_descriptors[index],
                    vk::DescriptorType::eStorageBuffer,
                    new_texture_buffers[index]);
            const RasterSceneBindings bindings{
                this->primitive_descriptor,
                this->transform_descriptor,
                this->material_descriptor,
                this->face_material_descriptor,
                this->area_light_descriptor,
                this->texture_descriptors[0],
                this->texture_descriptors[1],
                this->texture_descriptors[2],
                this->texture_descriptors[3],
                this->texture_descriptors[4],
                this->texture_descriptors[5],
                this->texture_descriptors[6],
                this->texture_descriptors[7],
                this->texture_descriptors[8],
                {
                    this->texture_count,
                    this->texture_stack_size,
                    0,
                    0}};
            new_binding_buffer =
                upload_texture_buffer(
                    std::span{
                        &bindings,
                        1});
            this->gpu->write_buffer(
                this->binding_descriptor,
                vk::DescriptorType::eStorageBuffer,
                new_binding_buffer);
        }
        this->primitive_buffer =
            std::move(new_primitives);
        this->transform_buffer =
            std::move(new_transforms);
        this->material_buffer =
            std::move(new_materials);
        this->face_material_buffer =
            std::move(new_face_materials);
        this->area_light_buffer =
            std::move(new_area_lights);
        this->pick_primitive_buffer =
            std::move(new_pick_primitives);
        this->texture_buffers =
            std::move(new_texture_buffers);
        this->binding_buffer =
            std::move(new_binding_buffer);
    }

    void RasterScene::update_transforms(
        const scene::SceneView scene,
        const vk::raii::CommandBuffer& command_buffer) {
        std::vector<RasterTransform> transforms =
            compile_raster_transforms(
                *this->asset_cache,
                scene);
        if (transforms.empty()) transforms.emplace_back();
        GpuBuffer new_transforms =
            upload_buffer(
                *this->gpu,
                command_buffer,
                std::span<const RasterTransform>{transforms},
                vk::BufferUsageFlagBits::eStorageBuffer);
        const DescriptorHandle transform_descriptor =
            this->gpu->allocate_resource_descriptor();
        this->gpu->write_buffer(
            transform_descriptor,
            vk::DescriptorType::eStorageBuffer,
            new_transforms);
        const RasterSceneBindings bindings{
            this->primitive_descriptor,
            transform_descriptor,
            this->material_descriptor,
            this->face_material_descriptor,
            this->area_light_descriptor,
            this->texture_descriptors[0],
            this->texture_descriptors[1],
            this->texture_descriptors[2],
            this->texture_descriptors[3],
            this->texture_descriptors[4],
            this->texture_descriptors[5],
            this->texture_descriptors[6],
            this->texture_descriptors[7],
            this->texture_descriptors[8],
            {
                this->texture_count,
                this->texture_stack_size,
                0,
                0}};
        GpuBuffer new_bindings =
            upload_buffer(
                *this->gpu,
                command_buffer,
                std::span{&bindings, 1},
                vk::BufferUsageFlagBits::eStorageBuffer);
        const DescriptorHandle binding_descriptor =
            this->gpu->allocate_resource_descriptor();
        this->gpu->write_buffer(
            binding_descriptor,
            vk::DescriptorType::eStorageBuffer,
            new_bindings);
        this->gpu->release_resource_descriptor(
            this->transform_descriptor);
        this->gpu->release_resource_descriptor(
            this->binding_descriptor);
        this->gpu->defer(
            [
                transforms = std::move(this->transform_buffer),
                bindings = std::move(this->binding_buffer)
            ]() mutable {});
        this->transform_buffer = std::move(new_transforms);
        this->binding_buffer = std::move(new_bindings);
        this->transform_descriptor = transform_descriptor;
        this->binding_descriptor = binding_descriptor;
    }
    void RasterScene::synchronize(
        const scene::SceneView scene,
        const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.value == this->uploaded_revision.value) return;
        if ((scene.revision.changes &
             (scene::SceneChange::Geometry |
              scene::SceneChange::Texture |
              scene::SceneChange::Material |
              scene::SceneChange::Light)) !=
            scene::SceneChange::None)
            this->upload(
                scene,
                &command_buffer);
        else if ((scene.revision.changes &
                  scene::SceneChange::Transform) !=
                 scene::SceneChange::None)
            this->update_transforms(
                scene,
                command_buffer);
        if ((scene.revision.changes &
             scene::SceneChange::Camera) !=
            scene::SceneChange::None)
            this->scene_camera = scene.camera;
        this->uploaded_revision = scene.revision;
    }
} // namespace spectra::rasterizer
