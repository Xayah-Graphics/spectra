module;

#include "../shaders/shader_semantics.h"

module spectra.render.pathtracer.scene;

import spectra.render.pathtracer.abi;
import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr std::uint32_t table_resolution    = 64;
        constexpr std::size_t coefficient_count     = 3ull * table_resolution * table_resolution * table_resolution * 3ull;
        constexpr std::uint64_t expected_table_size = 16ull + table_resolution * sizeof(float) + coefficient_count * sizeof(float);
        constexpr std::size_t table_word_count      = expected_table_size / sizeof(std::uint32_t);

        [[nodiscard]] float lerp(const float value, const float first, const float second) noexcept {
            return std::fma(value, second - first, first);
        }

    } // namespace

    float RgbSigmoidPolynomial::evaluate(const float wavelength) const noexcept {
        const float value = std::fma(std::fma(this->c0, wavelength, this->c1), wavelength, this->c2);
        if (std::isinf(value)) return value > 0.0f ? 1.0f : 0.0f;
        return 0.5f + value / (2.0f * std::sqrt(1.0f + value * value));
    }

    RgbToSpectrumTable::RgbToSpectrumTable(const std::span<const std::uint32_t> data) {
        const std::array<char, 8> magic{'S', 'P', 'R', 'G', 'B', '0', '0', '1'};
        if (data.size() != table_word_count || std::memcmp(data.data(), magic.data(), magic.size()) != 0 || data[2] != 1 || data[3] != table_resolution) throw std::runtime_error("Invalid RGB-to-spectrum table header");
        for (std::size_t index = 0; index != this->scale.size(); ++index) this->scale[index] = std::bit_cast<float>(data[index + 4]);
        this->coefficients = data.subspan(4 + table_resolution, coefficient_count);
    }

    RgbSigmoidPolynomial RgbToSpectrumTable::polynomial(const math::Float3 rgb) const noexcept {
        const std::array components{rgb.x, rgb.y, rgb.z};
        if (components[0] == components[1] && components[1] == components[2]) {
            if (components[0] <= 0.0f) return {0.0f, 0.0f, -std::numeric_limits<float>::infinity()};
            if (components[0] >= 1.0f) return {0.0f, 0.0f, std::numeric_limits<float>::infinity()};
            return {0.0f, 0.0f, (components[0] - 0.5f) / std::sqrt(components[0] * (1.0f - components[0]))};
        }
        const std::uint32_t maximum_component             = components[0] > components[1] ? (components[0] > components[2] ? 0u : 2u) : (components[1] > components[2] ? 1u : 2u);
        const float z                                     = components[maximum_component];
        const float x                                     = components[(maximum_component + 1u) % 3u] * static_cast<float>(table_resolution - 1u) / z;
        const float y                                     = components[(maximum_component + 2u) % 3u] * static_cast<float>(table_resolution - 1u) / z;
        const std::uint32_t x_index                       = std::min(static_cast<std::uint32_t>(x), table_resolution - 2u);
        const std::uint32_t y_index                       = std::min(static_cast<std::uint32_t>(y), table_resolution - 2u);
        const std::array<float, 64>::const_iterator upper = std::ranges::lower_bound(this->scale, z);
        const std::uint32_t z_index                       = std::clamp(static_cast<std::uint32_t>(upper - this->scale.begin()) - 1u, 0u, table_resolution - 2u);
        const float x_offset                              = x - static_cast<float>(x_index);
        const float y_offset                              = y - static_cast<float>(y_index);
        const float z_offset                              = (z - this->scale[z_index]) / (this->scale[z_index + 1u] - this->scale[z_index]);
        std::array<float, 3> result{};
        for (std::uint32_t coefficient = 0; coefficient != 3; ++coefficient) {
            const auto value = [this, maximum_component, x_index, y_index, z_index, coefficient](const std::uint32_t x_delta, const std::uint32_t y_delta, const std::uint32_t z_delta) {
                const std::size_t index = ((((maximum_component * table_resolution + z_index + z_delta) * table_resolution + y_index + y_delta) * table_resolution + x_index + x_delta) * 3u + coefficient);
                return std::bit_cast<float>(this->coefficients[index]);
            };
            result[coefficient] = lerp(z_offset, lerp(y_offset, lerp(x_offset, value(0, 0, 0), value(1, 0, 0)), lerp(x_offset, value(0, 1, 0), value(1, 1, 0))), lerp(y_offset, lerp(x_offset, value(0, 0, 1), value(1, 0, 1)), lerp(x_offset, value(0, 1, 1), value(1, 1, 1))));
        }
        return {result[0], result[1], result[2]};
    }

    std::span<const std::uint32_t> RgbToSpectrumTables::checked_data(const std::span<const std::uint32_t> data) {
        if (data.size() != table_word_count * 3) throw std::runtime_error("Invalid RGB-to-spectrum table collection");
        return data;
    }

    RgbToSpectrumTables::RgbToSpectrumTables(const std::span<const std::uint32_t> data) : srgb(checked_data(data).first(table_word_count)), rec2020(data.subspan(table_word_count, table_word_count)), aces2065_1(data.last(table_word_count)) {}

    const RgbToSpectrumTable& RgbToSpectrumTables::table_for(const scene::SpectrumColorSpace color_space) const noexcept {
        switch (color_space) {
        case scene::SpectrumColorSpace::Srgb: return this->srgb;
        case scene::SpectrumColorSpace::Rec2020: return this->rec2020;
        case scene::SpectrumColorSpace::Aces2065_1: return this->aces2065_1;
        }
        std::unreachable();
    }

    CompiledSpectrum compile_spectrum(const scene::SpectrumParameter& spectrum, const RgbToSpectrumTables& tables, std::vector<math::Float2>& piecewise_samples) {
        if (spectrum.texture.value != 0) throw std::runtime_error("A textured Spectrum must be compiled by the Texture Program");
        switch (spectrum.encoding) {
        case scene::SpectrumEncoding::RgbAlbedo:
        case scene::SpectrumEncoding::RgbUnbounded:
        case scene::SpectrumEncoding::RgbIlluminant:
            {
                math::Float3 rgb = spectrum.value;
                float scale      = 1.0f;
                if (spectrum.encoding != scene::SpectrumEncoding::RgbAlbedo) {
                    const float maximum = std::max(rgb.x, std::max(rgb.y, rgb.z));
                    scale               = 2.0f * maximum;
                    if (scale != 0.0f) {
                        rgb.x /= scale;
                        rgb.y /= scale;
                        rgb.z /= scale;
                    }
                }
                const RgbSigmoidPolynomial polynomial = tables.table_for(spectrum.color_space).polynomial(rgb);
                CompiledIlluminant illuminant         = CompiledIlluminant::None;
                if (spectrum.encoding == scene::SpectrumEncoding::RgbIlluminant) illuminant = spectrum.color_space == scene::SpectrumColorSpace::Aces2065_1 ? CompiledIlluminant::D60 : CompiledIlluminant::D65;
                return {{polynomial.c0, polynomial.c1, polynomial.c2, scale}, {std::to_underlying(CompiledSpectrumKind::Rgb), std::to_underlying(illuminant), 0, 0}};
            }
        case scene::SpectrumEncoding::Constant: return {{spectrum.scalar, 0.0f, 0.0f, 0.0f}, {std::to_underlying(CompiledSpectrumKind::Constant), 0, 0, 0}};
        case scene::SpectrumEncoding::Blackbody:
            {
                const scene::BlackbodySpectrum blackbody{spectrum.temperature};
                return {{spectrum.temperature, blackbody.normalization, 0.0f, 0.0f}, {std::to_underlying(CompiledSpectrumKind::Blackbody), 0, 0, 0}};
            }
        case scene::SpectrumEncoding::PiecewiseLinear:
            {
                if (spectrum.wavelengths.size() != spectrum.samples.size() || spectrum.wavelengths.size() < 2) throw std::runtime_error("Piecewise-linear Spectrum requires equal wavelength/sample arrays with at least two entries");
                for (std::size_t index = 1; index != spectrum.wavelengths.size(); ++index)
                    if (spectrum.wavelengths[index] <= spectrum.wavelengths[index - 1]) throw std::runtime_error("Piecewise-linear Spectrum wavelengths must be strictly increasing");
                const std::uint32_t offset = static_cast<std::uint32_t>(piecewise_samples.size());
                for (std::size_t index = 0; index != spectrum.wavelengths.size(); ++index) piecewise_samples.push_back({spectrum.wavelengths[index], spectrum.samples[index]});
                return {{}, {std::to_underlying(CompiledSpectrumKind::PiecewiseLinear), 0, offset, static_cast<std::uint32_t>(spectrum.wavelengths.size())}};
            }
        }
        std::unreachable();
    }


    scene::SceneResources snapshot_path_scene_resources(const scene::SceneResources& source) {
        scene::SceneResources result{};
        result.geometries.reserve(source.geometries.size());
        for (const scene::Geometry& geometry : source.geometries) {
            scene::Geometry copy{geometry.id, geometry.name, geometry.revision};
            std::visit(
                [&copy](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::TriangleMeshGeometry>)
                        copy.data = scene::TriangleMeshGeometry{.asset = data.asset, .source = data.source};
                    else
                        copy.data = data;
                },
                geometry.data);
            result.geometries.push_back(std::move(copy));
        }
        result.sphere_sets.reserve(source.sphere_sets.size());
        for (const scene::SphereSet& spheres : source.sphere_sets) result.sphere_sets.emplace_back(spheres.id, spheres.name, spheres.revision, spheres.asset);
        result.volumes    = source.volumes;
        result.textures   = source.textures;
        result.materials  = source.materials;
        result.media      = source.media;
        result.lights     = source.lights;
        result.prototypes = source.prototypes;
        result.instances  = source.instances;
        return result;
    }

    namespace {
        enum class PathMaterialKind : std::uint32_t {
            Interface           = shader_semantics::material_interface,
            Diffuse             = shader_semantics::material_diffuse,
            DiffuseTransmission = shader_semantics::material_diffuse_transmission,
            Conductor           = shader_semantics::material_conductor,
            Dielectric          = shader_semantics::material_dielectric,
            ThinDielectric      = shader_semantics::material_thin_dielectric,
            CoatedDiffuse       = shader_semantics::material_coated_diffuse,
            CoatedConductor     = shader_semantics::material_coated_conductor,
            Mix                 = shader_semantics::material_mix,
        };

        enum class PathTextureKind : std::uint32_t {
            Constant     = shader_semantics::texture_constant,
            Image        = shader_semantics::texture_image,
            Checkerboard = shader_semantics::texture_checkerboard,
            Scale        = shader_semantics::texture_scale,
            Mix          = shader_semantics::texture_mix,
            DirectionMix = shader_semantics::texture_direction_mix,
            Bilerp       = shader_semantics::texture_bilerp,
        };

        enum class PathLightKind : std::uint32_t {
            Point          = shader_semantics::light_point,
            Spot           = shader_semantics::light_spot,
            Distant        = shader_semantics::light_distant,
            DiffuseArea    = shader_semantics::light_diffuse_area,
            Infinite       = shader_semantics::light_infinite,
            PortalInfinite = shader_semantics::light_portal_infinite,
        };

        enum class PathMediumKind : std::uint32_t {
            Homogeneous = shader_semantics::medium_homogeneous,
            Volume      = shader_semantics::medium_volume,
        };

        enum class PathVolumeKind : std::uint32_t {
            DensityGrid     = shader_semantics::volume_density_grid,
            RgbGrid         = shader_semantics::volume_rgb_grid,
            NanoVdb         = shader_semantics::volume_nanovdb,
            ProceduralCloud = shader_semantics::volume_procedural_cloud,
        };

        enum class PathMaterialTextureRole : std::uint32_t {
            Parameter,
            Normal,
            Bump,
            BumpU,
            BumpV,
        };

        constexpr std::uint32_t invalid_path_index = std::numeric_limits<std::uint32_t>::max();
        constexpr float path_pi                    = std::numbers::pi_v<float>;

        [[nodiscard]] float evaluate_compiled_spectrum(const CompiledSpectrum& spectrum, const std::span<const math::Float2> piecewise_spectra, const std::span<const float> cie_spectra, const float wavelength) noexcept {
            float value{};
            switch (static_cast<CompiledSpectrumKind>(spectrum.metadata[0])) {
            case CompiledSpectrumKind::Rgb:
                {
                    const float polynomial = (spectrum.parameters[0] * wavelength + spectrum.parameters[1]) * wavelength + spectrum.parameters[2];
                    value                  = 0.5f + polynomial / (2.0f * std::sqrt(1.0f + polynomial * polynomial));
                    if (std::isinf(polynomial)) value = polynomial > 0.0f ? 1.0f : 0.0f;
                    value *= spectrum.parameters[3];
                    if (spectrum.metadata[1] != std::to_underlying(CompiledIlluminant::None)) {
                        const std::uint32_t curve = spectrum.metadata[1] == std::to_underlying(CompiledIlluminant::D65) ? 3u : 4u;
                        const float position      = std::clamp(wavelength, 360.0f, 830.0f) - 360.0f;
                        const std::uint32_t lower = std::min(static_cast<std::uint32_t>(position), 469u);
                        value *= std::lerp(cie_spectra[curve * 471u + lower], cie_spectra[curve * 471u + lower + 1u], position - static_cast<float>(lower));
                    }
                    break;
                }
            case CompiledSpectrumKind::Constant: value = spectrum.parameters[0]; break;
            case CompiledSpectrumKind::Blackbody:
                {
                    constexpr double speed_of_light     = 299792458.0;
                    constexpr double planck_constant    = 6.62606957e-34;
                    constexpr double boltzmann_constant = 1.3806488e-23;
                    const double wavelength_meters      = static_cast<double>(wavelength) * 1.0e-9;
                    value                               = spectrum.parameters[0] <= 0.0f ? 0.0f : static_cast<float>((2.0 * planck_constant * speed_of_light * speed_of_light) / (std::pow(wavelength_meters, 5.0) * (std::exp((planck_constant * speed_of_light) / (wavelength_meters * boltzmann_constant * spectrum.parameters[0])) - 1.0))) * spectrum.parameters[1];
                    break;
                }
            case CompiledSpectrumKind::PiecewiseLinear:
                {
                    const std::uint32_t offset = spectrum.metadata[2];
                    const std::uint32_t count  = spectrum.metadata[3];
                    if (wavelength < piecewise_spectra[offset].x || wavelength > piecewise_spectra[offset + count - 1u].x) return 0.0f;
                    std::uint32_t upper = 1;
                    while (upper + 1u < count && piecewise_spectra[offset + upper].x < wavelength) ++upper;
                    const math::Float2 first  = piecewise_spectra[offset + upper - 1u];
                    const math::Float2 second = piecewise_spectra[offset + upper];
                    value                     = std::lerp(first.y, second.y, (wavelength - first.x) / (second.x - first.x));
                    break;
                }
            }
            return value;
        }

        [[nodiscard]] float spectrum_power_sample(const CompiledSpectrum& spectrum, const std::span<const math::Float2> piecewise_spectra, const std::span<const float> cie_spectra) noexcept {
            float result{};
            for (std::uint32_t index = 0; index != 4; ++index) {
                float sample = 0.5f + static_cast<float>(index) / 4.0f;
                if (sample > 1.0f) sample -= 1.0f;
                const float wavelength = std::clamp(538.0f - 138.888889f * std::atanh(0.85691062f - 1.82750197f * sample), 360.0f, 830.0f);
                const float hyperbolic = std::cosh(0.0072f * (wavelength - 538.0f));
                const float pdf        = 0.0039398042f / (hyperbolic * hyperbolic);
                result += evaluate_compiled_spectrum(spectrum, piecewise_spectra, cie_spectra, wavelength) / pdf;
            }
            return result / 4.0f;
        }

        [[nodiscard]] float spectrum_maximum(const CompiledSpectrum& spectrum, const std::span<const math::Float2> piecewise_spectra, const std::span<const float> cie_spectra) noexcept {
            float result{};
            for (std::uint32_t wavelength = 360; wavelength <= 830; ++wavelength) result = std::max(result, evaluate_compiled_spectrum(spectrum, piecewise_spectra, cie_spectra, static_cast<float>(wavelength)));
            return result;
        }

        [[nodiscard]] std::uint32_t compile_light_distribution(const std::span<const float> weights, const std::uint32_t width, const std::uint32_t height, std::vector<pathtracer::PathLightDistribution>& distributions, std::vector<float>& data) {
            const std::uint32_t function_offset = static_cast<std::uint32_t>(data.size());
            data.insert(data.end(), weights.begin(), weights.end());
            const std::uint32_t conditional_offset = static_cast<std::uint32_t>(data.size());
            data.resize(data.size() + static_cast<std::size_t>(height) * (width + 1u));
            std::vector<float> row_integrals(height);
            for (std::uint32_t row = 0; row != height; ++row) {
                const std::uint32_t cdf = conditional_offset + row * (width + 1u);
                data[cdf]               = 0.0f;
                for (std::uint32_t column = 0; column != width; ++column) data[cdf + column + 1u] = data[cdf + column] + std::abs(weights[row * width + column]) / static_cast<float>(width);
                row_integrals[row] = data[cdf + width];
                if (row_integrals[row] == 0.0f)
                    for (std::uint32_t column = 1; column <= width; ++column) data[cdf + column] = static_cast<float>(column) / static_cast<float>(width);
                else
                    for (std::uint32_t column = 1; column <= width; ++column) data[cdf + column] /= row_integrals[row];
            }
            const std::uint32_t marginal_offset = static_cast<std::uint32_t>(data.size());
            data.resize(data.size() + height + 1u);
            data[marginal_offset] = 0.0f;
            for (std::uint32_t row = 0; row != height; ++row) data[marginal_offset + row + 1u] = data[marginal_offset + row] + row_integrals[row] / static_cast<float>(height);
            const float integral = data[marginal_offset + height];
            if (integral == 0.0f)
                for (std::uint32_t row = 1; row <= height; ++row) data[marginal_offset + row] = static_cast<float>(row) / static_cast<float>(height);
            else
                for (std::uint32_t row = 1; row <= height; ++row) data[marginal_offset + row] /= integral;
            const std::uint32_t summed_area_offset = static_cast<std::uint32_t>(data.size());
            data.resize(data.size() + static_cast<std::size_t>(width) * height);
            for (std::uint32_t row = 0; row != height; ++row)
                for (std::uint32_t column = 0; column != width; ++column) {
                    float sum = weights[row * width + column];
                    if (column != 0) sum += data[summed_area_offset + row * width + column - 1u];
                    if (row != 0) sum += data[summed_area_offset + (row - 1u) * width + column];
                    if (column != 0 && row != 0) sum -= data[summed_area_offset + (row - 1u) * width + column - 1u];
                    data[summed_area_offset + row * width + column] = sum;
                }
            const std::uint32_t index = static_cast<std::uint32_t>(distributions.size());
            distributions.push_back(pathtracer::PathLightDistribution{{width, height, function_offset, conditional_offset}, {marginal_offset, summed_area_offset, 0, 0}, {integral, std::accumulate(weights.begin(), weights.end(), 0.0f) / static_cast<float>(weights.size()), 0.0f, 0.0f}});
            return index;
        }

        [[nodiscard]] math::Float2 equal_area_sphere_to_square(const math::Float3 direction) noexcept {
            const float x      = std::abs(direction.x);
            const float y      = std::abs(direction.y);
            const float z      = std::abs(direction.z);
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z));
            const float a      = std::max(x, y);
            float b            = std::min(x, y);
            b                  = a == 0.0f ? 0.0f : b / a;
            float phi          = 0.4067585662467884896e-5f + b * (0.6362265452740161349f + b * (0.6157201789828021349e-2f + b * (-0.2473337332812689442f + b * (0.8817706647753162947e-1f + b * (0.4190388180291657359e-1f + b * -0.2513909723434835093e-1f)))));
            if (x < y) phi = 1.0f - phi;
            float v = phi * radius;
            float u = radius - v;
            if (direction.z < 0.0f) {
                std::swap(u, v);
                u = 1.0f - u;
                v = 1.0f - v;
            }
            u = std::copysign(u, direction.x);
            v = std::copysign(v, direction.y);
            return {0.5f * (u + 1.0f), 0.5f * (v + 1.0f)};
        }

        [[nodiscard]] math::Float4 image_texel_octahedral(const scene::ImageTexture& image, int x, int y) noexcept {
            const int width  = static_cast<int>(image.width);
            const int height = static_cast<int>(image.height);
            if (x < 0) {
                x = -x;
                y = height - 1 - y;
            } else if (x >= width) {
                x = 2 * width - 1 - x;
                y = height - 1 - y;
            }
            if (y < 0) {
                x = width - 1 - x;
                y = -y;
            } else if (y >= height) {
                x = width - 1 - x;
                y = 2 * height - 1 - y;
            }
            return image.texels[static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)];
        }

        [[nodiscard]] math::Float4 image_bilerp_octahedral(const scene::ImageTexture& image, const math::Float2 uv) noexcept {
            const float x  = uv.x * static_cast<float>(image.width) - 0.5f;
            const float y  = uv.y * static_cast<float>(image.height) - 0.5f;
            const int x0   = static_cast<int>(std::floor(x));
            const int y0   = static_cast<int>(std::floor(y));
            const float dx = x - static_cast<float>(x0);
            const float dy = y - static_cast<float>(y0);
            const math::Float4 values[4]{image_texel_octahedral(image, x0, y0), image_texel_octahedral(image, x0 + 1, y0), image_texel_octahedral(image, x0, y0 + 1), image_texel_octahedral(image, x0 + 1, y0 + 1)};
            return {std::lerp(std::lerp(values[0].x, values[1].x, dx), std::lerp(values[2].x, values[3].x, dx), dy), std::lerp(std::lerp(values[0].y, values[1].y, dx), std::lerp(values[2].y, values[3].y, dx), dy), std::lerp(std::lerp(values[0].z, values[1].z, dx), std::lerp(values[2].z, values[3].z, dx), dy), 1.0f};
        }


        static_assert(sizeof(CompiledSpectrum) == 32);
        struct PathBufferArena {
            template <class Element>
            [[nodiscard]] PathBufferSlice append(const std::span<const Element> elements) {
                const vk::DeviceSize alignment = std::max<vk::DeviceSize>(16u, alignof(Element));
                const vk::DeviceSize offset    = (this->data.size() + alignment - 1u) & ~(alignment - 1u);
                const vk::DeviceSize size      = std::max<vk::DeviceSize>(elements.size_bytes(), sizeof(Element));
                this->data.resize(offset + size);
                if (!elements.empty()) std::memcpy(this->data.data() + offset, elements.data(), elements.size_bytes());
                return {{}, offset, size};
            }

            std::vector<std::byte> data{};
        };

        template <class Sample>
        [[nodiscard]] std::vector<float> build_grid_majorant(const math::UInt3 resolution, Sample&& sample) {
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
                        for (std::int32_t gz = z0; gz <= z1; ++gz)
                            for (std::int32_t gy = y0; gy <= y1; ++gy)
                                for (std::int32_t gx = x0; gx <= x1; ++gx) maximum = std::max(maximum, sample((static_cast<std::size_t>(gz) * resolution.y + static_cast<std::size_t>(gy)) * resolution.x + static_cast<std::size_t>(gx)));
                        majorant[(z * 16u + y) * 16u + x] = maximum;
                    }
            return majorant;
        }

        [[nodiscard]] float rgb_spectrum_maximum(const math::Float3 rgb, const scene::SpectrumColorSpace color_space, const RgbToSpectrumTables& tables) {
            const float maximum = std::max(rgb.x, std::max(rgb.y, rgb.z));
            if (maximum == 0.0f) return 0.0f;
            const float scale                     = 2.0f * maximum;
            const RgbSigmoidPolynomial polynomial = tables.table_for(color_space)
                                                        .polynomial({
                                                            rgb.x / scale,
                                                            rgb.y / scale,
                                                            rgb.z / scale,
                                                        });
            float result = std::max(polynomial.evaluate(360.0f), polynomial.evaluate(830.0f));
            if (polynomial.c0 != 0.0f) {
                const float wavelength = -polynomial.c1 / (2.0f * polynomial.c0);
                if (wavelength >= 360.0f && wavelength <= 830.0f) result = std::max(result, polynomial.evaluate(wavelength));
            }
            return scale * result;
        }

        [[nodiscard]] std::vector<float> build_density_majorant(const scene::DensityGridVolume& volume) {
            return build_grid_majorant(volume.resolution, [&volume](const std::size_t index) { return volume.density[index]; });
        }

        [[nodiscard]] std::vector<float> build_rgb_majorant(const scene::RgbGridVolume& volume, const RgbToSpectrumTables& tables) {
            return build_grid_majorant(volume.resolution, [&volume, &tables](const std::size_t index) {
                const float absorption = volume.sigma_a.empty() ? 1.0f : rgb_spectrum_maximum(volume.sigma_a[index], volume.color_space, tables);
                const float scattering = volume.sigma_s.empty() ? 1.0f : rgb_spectrum_maximum(volume.sigma_s[index], volume.color_space, tables);
                return absorption + scattering;
            });
        }

        struct CompiledPathFloat {
            float value{};
            std::uint32_t texture{};
        };

        [[nodiscard]] std::array<std::uint32_t, 4> compile_path_spectrum(const scene::SpectrumParameter& parameter, const RgbToSpectrumTables& tables, std::vector<CompiledSpectrum>& spectra, std::vector<math::Float2>& piecewise_spectra, const std::function<std::uint32_t(scene::TextureId)>& texture_handle) {
            const std::uint32_t spectrum                  = static_cast<std::uint32_t>(spectra.size());
            scene::SpectrumParameter untextured_parameter = parameter;
            untextured_parameter.texture                  = {};
            spectra.push_back(compile_spectrum(untextured_parameter, tables, piecewise_spectra));
            return {spectrum, parameter.texture.value == 0 ? std::numeric_limits<std::uint32_t>::max() : texture_handle(parameter.texture), 0, 0};
        }

        [[nodiscard]] CompiledPathFloat compile_path_float(const scene::FloatParameter& parameter, const std::function<std::uint32_t(scene::TextureId)>& texture_handle) {
            return {parameter.value, parameter.texture.value == 0 ? std::numeric_limits<std::uint32_t>::max() : texture_handle(parameter.texture)};
        }

    } // namespace

    PathSceneGpuSnapshot snapshot_path_scene_gpu(GpuScene& source, const scene::SceneView scene) {
        PathSceneGpuSnapshot snapshot{};
        const GpuSceneView gpu_scene = source.view();
        snapshot.textures.resize(scene.resources.textures.size());
        for (std::size_t index = 0; index != scene.resources.textures.size(); ++index) {
            const scene::Texture& texture = scene.resources.textures[index];
            if (!std::holds_alternative<scene::ImageTexture>(texture.data)) continue;
            const GpuTextureImage& image = source.texture_image(texture, texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat);
            snapshot.textures[index]     = {image.image_descriptor, image.sampler_descriptor};
        }
        snapshot.volumes.reserve(gpu_scene.volumes.size());
        for (const GpuVolume& volume : gpu_scene.volumes) {
            std::array<DescriptorHandle, static_cast<std::size_t>(GpuVolumeField::Count)> descriptors{};
            for (std::size_t index = 0; index != descriptors.size(); ++index) descriptors[index] = volume.descriptors[index];
            snapshot.volumes.push_back({volume.volume_id, volume.revision, volume.resolution, descriptors, volume.field_present});
        }
        snapshot.geometries.reserve(gpu_scene.geometries.size());
        for (const GpuGeometry& geometry : gpu_scene.geometries) snapshot.geometries.push_back({geometry.positions_descriptor, geometry.indices_descriptor, geometry.normals_descriptor, geometry.tangents_descriptor, geometry.texture_coordinates_descriptor, geometry.vertex_count, geometry.index_count, geometry.vertex_capacity, geometry.index_capacity, geometry.attribute_mask});
        snapshot.sphere_sets.reserve(gpu_scene.sphere_sets.size());
        for (const GpuSphereSet& spheres : gpu_scene.sphere_sets) snapshot.sphere_sets.push_back({spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, spheres.sphere_capacity});
        snapshot.primitives.assign(gpu_scene.primitives.begin(), gpu_scene.primitives.end());
        snapshot.acceleration_primitive_indices.assign(gpu_scene.acceleration_primitive_indices.begin(), gpu_scene.acceleration_primitive_indices.end());
        snapshot.structure_revision = gpu_scene.structure_revision;
        return snapshot;
    }


    std::unique_ptr<PreparedPathFilter> prepare_path_filter(const scene::Film& film, const PathTracerResources& resources) {
        const scene::Filter& filter = film.filter;
        std::array<std::uint32_t, 2> resolution{};
        std::vector<float> distribution{};
        float absolute_integral{};
        if (filter.kind != scene::FilterKind::Box && filter.kind != scene::FilterKind::Triangle) {
            resolution                             = {std::max(1u, static_cast<std::uint32_t>(std::ceil(32.0f * filter.radius.x))), std::max(1u, static_cast<std::uint32_t>(std::ceil(32.0f * filter.radius.y)))};
            const std::uint32_t value_count        = resolution[0] * resolution[1];
            const std::uint32_t conditional_offset = value_count;
            const std::uint32_t marginal_offset    = conditional_offset + resolution[1] * (resolution[0] + 1);
            distribution.resize(marginal_offset + resolution[1] + 1);
            const auto evaluate_1d = [&filter](const float coordinate) {
                if (filter.kind == scene::FilterKind::Gaussian) return std::exp(-coordinate * coordinate / (2.0f * filter.sigma * filter.sigma)) / std::sqrt(2.0f * std::numbers::pi_v<float> * filter.sigma * filter.sigma);
                if (filter.kind == scene::FilterKind::Mitchell) {
                    const float x = std::abs(coordinate);
                    if (x <= 1.0f) return ((12.0f - 9.0f * filter.b - 6.0f * filter.c) * x * x * x + (-18.0f + 12.0f * filter.b + 6.0f * filter.c) * x * x + 6.0f - 2.0f * filter.b) / 6.0f;
                    if (x <= 2.0f) return ((-filter.b - 6.0f * filter.c) * x * x * x + (6.0f * filter.b + 30.0f * filter.c) * x * x + (-12.0f * filter.b - 48.0f * filter.c) * x + 8.0f * filter.b + 24.0f * filter.c) / 6.0f;
                    return 0.0f;
                }
                return 0.0f;
            };
            const float cell_width  = 2.0f * filter.radius.x / static_cast<float>(resolution[0]);
            const float cell_height = 2.0f * filter.radius.y / static_cast<float>(resolution[1]);
            std::vector<float> row_integrals(resolution[1]);
            for (std::uint32_t y = 0; y != resolution[1]; ++y) {
                const float position_y = std::lerp(-filter.radius.y, filter.radius.y, (static_cast<float>(y) + 0.5f) / static_cast<float>(resolution[1]));
                float row_integral{};
                distribution[conditional_offset + y * (resolution[0] + 1)] = 0.0f;
                for (std::uint32_t x = 0; x != resolution[0]; ++x) {
                    const float position_x = std::lerp(-filter.radius.x, filter.radius.x, (static_cast<float>(x) + 0.5f) / static_cast<float>(resolution[0]));
                    float value{};
                    if (filter.kind == scene::FilterKind::Gaussian) {
                        const float edge_x = evaluate_1d(filter.radius.x);
                        const float edge_y = evaluate_1d(filter.radius.y);
                        value              = std::max(0.0f, evaluate_1d(position_x) - edge_x) * std::max(0.0f, evaluate_1d(position_y) - edge_y);
                    } else if (filter.kind == scene::FilterKind::Mitchell)
                        value = evaluate_1d(2.0f * position_x / filter.radius.x) * evaluate_1d(2.0f * position_y / filter.radius.y);
                    else {
                        const auto windowed_sinc = [&filter](const float coordinate, const float radius) {
                            if (std::abs(coordinate) > radius) return 0.0f;
                            const auto sinc = [](const float value) {
                                if (value == 0.0f) return 1.0f;
                                const float angle = std::numbers::pi_v<float> * value;
                                return std::sin(angle) / angle;
                            };
                            return sinc(coordinate) * sinc(coordinate / filter.tau);
                        };
                        value = windowed_sinc(position_x, filter.radius.x) * windowed_sinc(position_y, filter.radius.y);
                    }
                    distribution[y * resolution[0] + x] = value;
                    row_integral += std::abs(value) * cell_width;
                    distribution[conditional_offset + y * (resolution[0] + 1) + x + 1] = row_integral;
                }
                row_integrals[y] = row_integral;
                if (row_integral != 0.0f)
                    for (std::uint32_t x = 1; x != resolution[0] + 1; ++x) distribution[conditional_offset + y * (resolution[0] + 1) + x] /= row_integral;
                else
                    for (std::uint32_t x = 1; x != resolution[0] + 1; ++x) distribution[conditional_offset + y * (resolution[0] + 1) + x] = static_cast<float>(x) / static_cast<float>(resolution[0]);
            }
            distribution[marginal_offset] = 0.0f;
            for (std::uint32_t y = 0; y != resolution[1]; ++y) {
                absolute_integral += row_integrals[y] * cell_height;
                distribution[marginal_offset + y + 1] = absolute_integral;
            }
            if (!(absolute_integral > 0.0f) || !std::isfinite(absolute_integral)) throw std::runtime_error("Film filter has no finite nonzero sampling distribution");
            for (std::uint32_t y = 1; y != resolution[1] + 1; ++y) distribution[marginal_offset + y] /= absolute_integral;
        }
        if (distribution.empty()) distribution.push_back(0.0f);
        const std::span<const float> sensor_response = film.sensor_response.empty() ? std::span<const float>{resources.cie_samples}.first(3u * 471u) : std::span<const float>{film.sensor_response};
        if (sensor_response.size() != 3u * 471u) throw std::runtime_error("Film sensor response must contain three 471-wavelength channels");
        std::unique_ptr<PreparedPathFilter> prepared = std::make_unique<PreparedPathFilter>();
        prepared->film                               = film;
        prepared->distribution                       = std::move(distribution);
        prepared->sensor_response.assign(sensor_response.begin(), sensor_response.end());
        prepared->resolution        = resolution;
        prepared->absolute_integral = absolute_integral;
        return prepared;
    }


    std::unique_ptr<PreparedPathSampler> prepare_path_sampler(const scene::Sampler& sampler, const PathTracerResources& resources) {
        if (sampler.samples_per_pixel == 0) throw std::runtime_error("Sampler samples_per_pixel must be positive");
        if (sampler.kind == scene::SamplerKind::Stratified && (sampler.x_strata == 0 || sampler.y_strata == 0 || sampler.x_strata * sampler.y_strata != sampler.samples_per_pixel)) throw std::runtime_error("Stratified sampler requires samples_per_pixel == x_strata * y_strata");
        if (sampler.kind == scene::SamplerKind::Halton && sampler.randomization == scene::SamplerRandomization::FastOwen) throw std::runtime_error("Halton sampler does not support Fast Owen randomization");
        if (sampler.kind == scene::SamplerKind::ZSobol && !std::has_single_bit(sampler.samples_per_pixel)) throw std::runtime_error("ZSobol sampler requires a power-of-two sample count");
        if (sampler.kind == scene::SamplerKind::Pmj02bn && sampler.samples_per_pixel > 65'536) throw std::runtime_error("PMJ02BN sampler supports at most 65536 samples per pixel");

        std::uint32_t pixel_tile_size{1};
        std::vector<math::Float2> pixel_samples(1);
        if (sampler.kind == scene::SamplerKind::Pmj02bn) {
            std::uint32_t rounded_samples{1};
            std::uint32_t log_4_samples{};
            while (rounded_samples < sampler.samples_per_pixel) {
                rounded_samples *= 4;
                ++log_4_samples;
            }
            pixel_tile_size = 1u << (8u - log_4_samples);
            pixel_samples.assign(static_cast<std::size_t>(pixel_tile_size) * pixel_tile_size * sampler.samples_per_pixel, {});
            std::vector<std::uint32_t> stored(static_cast<std::size_t>(pixel_tile_size) * pixel_tile_size);
            const std::uint32_t pmj_offset = resources.sampling_table_data[12];
            for (std::uint32_t index = 0; index != 65'536; ++index) {
                const double x                 = static_cast<double>(resources.sampling_table_data[pmj_offset + index * 2]) * 0x1p-32 * pixel_tile_size;
                const double y                 = static_cast<double>(resources.sampling_table_data[pmj_offset + index * 2 + 1]) * 0x1p-32 * pixel_tile_size;
                const std::uint32_t pixel_x    = static_cast<std::uint32_t>(x);
                const std::uint32_t pixel_y    = static_cast<std::uint32_t>(y);
                const std::size_t pixel_offset = pixel_x + static_cast<std::size_t>(pixel_y) * pixel_tile_size;
                if (stored[pixel_offset] == sampler.samples_per_pixel) continue;
                const std::size_t sample_offset = pixel_offset * sampler.samples_per_pixel + stored[pixel_offset];
                pixel_samples[sample_offset]    = {static_cast<float>(x - std::floor(x)), static_cast<float>(y - std::floor(y))};
                ++stored[pixel_offset];
            }
            if (!std::ranges::all_of(stored, [&sampler](const std::uint32_t count) { return count == sampler.samples_per_pixel; })) throw std::runtime_error("PMJ02BN pixel sample table is incomplete");
        }

        std::unique_ptr<PreparedPathSampler> prepared = std::make_unique<PreparedPathSampler>();
        prepared->sampler                             = sampler;
        prepared->pixel_samples                       = std::move(pixel_samples);
        prepared->pixel_tile_size                     = pixel_tile_size;
        return prepared;
    }


    std::unique_ptr<PreparedPathScene> prepare_path_scene(const scene::SceneView scene, const PathSceneGpuSnapshot& gpu, const PathTracerResources& resources, PathTracerPreparationState* progress) {
        const RgbToSpectrumTables spectrum_tables{resources.rgb_to_spectrum_table_data};
        std::vector<CompiledSpectrum> spectra{};
        std::vector<math::Float2> piecewise_spectra{};
        const std::size_t texture_count     = scene.resources.textures.size();
        const std::uint32_t invalid_texture = std::numeric_limits<std::uint32_t>::max();
        std::vector<scene::TextureId> normal_maps{};
        std::vector<scene::TextureId> bump_maps{};
        for (const scene::Material& material : scene.resources.materials)
            std::visit(
                [&](const auto& data) {
                    if constexpr (requires {
                                      data.normal_map;
                                      data.bump_map;
                                  }) {
                        if (data.normal_map.value != 0) normal_maps.push_back(data.normal_map);
                        if (data.bump_map.value != 0) bump_maps.push_back(data.bump_map);
                    }
                },
                material.data);
        std::vector<std::uint32_t> texture_order{};
        std::vector<std::uint32_t> texture_handles(texture_count, invalid_texture);
        std::vector<std::uint8_t> texture_marks(texture_count);
        const auto source_texture_index = [&scene](const scene::TextureId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.textures, id, &scene::Texture::id) - scene.resources.textures.begin()); };
        std::function<void(std::uint32_t)> visit_texture;
        visit_texture = [&](const std::uint32_t index) {
            if (texture_marks[index] == 2) return;
            if (texture_marks[index] == 1) throw std::runtime_error(std::format("Texture dependency cycle contains {}", scene.resources.textures[index].name));
            texture_marks[index] = 1;
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        visit_texture(source_texture_index(data.first));
                        visit_texture(source_texture_index(data.second));
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        visit_texture(source_texture_index(data.first));
                        visit_texture(source_texture_index(data.second));
                        visit_texture(source_texture_index(data.amount));
                    }
                },
                scene.resources.textures[index].data);
            texture_marks[index]   = 2;
            texture_handles[index] = static_cast<std::uint32_t>(texture_order.size());
            texture_order.push_back(index);
        };
        for (std::uint32_t index = 0; index != texture_count; ++index) visit_texture(index);
        std::vector<std::uint32_t> texture_stack_sizes(texture_count, 1);
        std::uint32_t maximum_texture_stack_size{1};
        for (const std::uint32_t index : texture_order) {
            std::uint32_t stack_size = 1;
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        stack_size = std::max(texture_stack_sizes[source_texture_index(data.first)], 1 + texture_stack_sizes[source_texture_index(data.second)]);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        stack_size = std::max({texture_stack_sizes[source_texture_index(data.first)], 1 + texture_stack_sizes[source_texture_index(data.second)], 2 + texture_stack_sizes[source_texture_index(data.amount)]});
                    }
                },
                scene.resources.textures[index].data);
            texture_stack_sizes[index] = stack_size;
            maximum_texture_stack_size = std::max(maximum_texture_stack_size, stack_size);
        }
        const std::function<std::uint32_t(scene::TextureId)> texture_handle = [&](const scene::TextureId id) { return texture_handles[source_texture_index(id)]; };
        std::vector<pathtracer::PathTextureHeader> texture_headers{};
        std::vector<pathtracer::PathTextureMapping> texture_mappings{};
        std::vector<pathtracer::PathConstantTexture> constant_textures{};
        std::vector<pathtracer::PathImageTexture> image_textures{};
        std::vector<pathtracer::PathCheckerboardTexture> checkerboard_textures{};
        std::vector<pathtracer::PathScaleTexture> scale_textures{};
        std::vector<pathtracer::PathMixTexture> mix_textures{};
        std::vector<pathtracer::PathDirectionMixTexture> direction_mix_textures{};
        std::vector<pathtracer::PathBilerpTexture> bilerp_textures{};
        const auto compile_mapping = [&texture_mappings](const scene::TextureMapping& mapping) {
            pathtracer::PathTextureMapping result{};
            result.transform_row_0 = {1.0f, 0.0f, 0.0f, 0.0f};
            result.transform_row_1 = {0.0f, 1.0f, 0.0f, 0.0f};
            result.transform_row_2 = {0.0f, 0.0f, 1.0f, 0.0f};
            result.transform_row_3 = {0.0f, 0.0f, 0.0f, 1.0f};
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::UvTextureMapping>) {
                        result.metadata[0] = shader_semantics::mapping_uv;
                        result.parameter_0 = {data.scale.x, data.scale.y, data.offset.x, data.offset.y};
                    } else {
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
            const std::uint32_t index = static_cast<std::uint32_t>(texture_mappings.size());
            texture_mappings.push_back(result);
            return index;
        };
        const auto compile_checkerboard_mapping = [&texture_mappings, &compile_mapping](const scene::CheckerboardMapping& mapping) {
            if (const scene::TextureMapping* two_dimensional = std::get_if<scene::TextureMapping>(&mapping.data)) return compile_mapping(*two_dimensional);
            const scene::TextureMapping3D& three_dimensional = std::get<scene::TextureMapping3D>(mapping.data);
            const std::array<float, 16>& transform           = three_dimensional.texture_from_render.matrix;
            pathtracer::PathTextureMapping result{{shader_semantics::mapping_checkerboard_3d, 0, 0, 0}, {transform[0], transform[1], transform[2], transform[3]}, {transform[4], transform[5], transform[6], transform[7]}, {transform[8], transform[9], transform[10], transform[11]}, {transform[12], transform[13], transform[14], transform[15]}};
            const std::uint32_t index = static_cast<std::uint32_t>(texture_mappings.size());
            texture_mappings.push_back(result);
            return index;
        };
        const auto texture_spectrum = [&](const scene::SpectrumParameter& parameter) {
            const std::uint32_t index = static_cast<std::uint32_t>(spectra.size());
            spectra.push_back(compile_spectrum(parameter, spectrum_tables, piecewise_spectra));
            return index;
        };
        texture_headers.reserve(texture_count);
        for (const std::uint32_t source_index : texture_order) {
            const scene::Texture& texture = scene.resources.textures[source_index];
            const bool normal_map         = std::ranges::contains(normal_maps, texture.id);
            const bool bump_map           = std::ranges::contains(bump_maps, texture.id);
            if (normal_map && (texture.value_kind != scene::TextureValueKind::Float || !std::holds_alternative<scene::ImageTexture>(texture.data))) throw std::runtime_error(std::format("Normal Map {} must reference a Float Image Texture", texture.name));
            if (bump_map && texture.value_kind != scene::TextureValueKind::Float) throw std::runtime_error(std::format("Bump Map {} must reference a Float Texture", texture.name));
            PathTextureKind kind{};
            std::uint32_t local_index{};
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConstantTexture>) {
                        kind        = PathTextureKind::Constant;
                        local_index = static_cast<std::uint32_t>(constant_textures.size());
                        constant_textures.push_back(pathtracer::PathConstantTexture{data.scalar, texture_spectrum(data.spectrum)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                        kind        = PathTextureKind::Image;
                        local_index = static_cast<std::uint32_t>(image_textures.size());
                        image_textures.push_back(pathtracer::PathImageTexture{gpu.textures[source_index].image, gpu.textures[source_index].sampler, {compile_mapping(data.mapping), static_cast<std::uint32_t>(data.channel), static_cast<std::uint32_t>(data.filter), data.invert ? 1u : 0u}, {data.scale, texture.color_space == scene::TextureColorSpace::Rec2020 ? 1.0f : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? 2.0f : 0.0f, data.maximum_anisotropy, static_cast<float>(std::to_underlying(data.wrap))}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture>) {
                        kind        = PathTextureKind::Checkerboard;
                        local_index = static_cast<std::uint32_t>(checkerboard_textures.size());
                        checkerboard_textures.push_back(pathtracer::PathCheckerboardTexture{{texture_handle(data.first), texture_handle(data.second), compile_checkerboard_mapping(data.mapping), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture>) {
                        kind        = PathTextureKind::Scale;
                        local_index = static_cast<std::uint32_t>(scale_textures.size());
                        scale_textures.push_back(pathtracer::PathScaleTexture{{texture_handle(data.first), texture_handle(data.second), 0, 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        kind        = PathTextureKind::Mix;
                        local_index = static_cast<std::uint32_t>(mix_textures.size());
                        mix_textures.push_back(pathtracer::PathMixTexture{{texture_handle(data.first), texture_handle(data.second), texture_handle(data.amount), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        kind        = PathTextureKind::DirectionMix;
                        local_index = static_cast<std::uint32_t>(direction_mix_textures.size());
                        direction_mix_textures.push_back(pathtracer::PathDirectionMixTexture{{texture_handle(data.first), texture_handle(data.second), 0, 0}, {data.direction.x, data.direction.y, data.direction.z, 0.0f}});
                    } else {
                        kind        = PathTextureKind::Bilerp;
                        local_index = static_cast<std::uint32_t>(bilerp_textures.size());
                        pathtracer::PathBilerpTexture compiled{};
                        compiled.scalars = data.scalars;
                        for (std::uint32_t corner = 0; corner != 4; ++corner) compiled.spectra[corner] = texture_spectrum(data.spectra[corner]);
                        compiled.data[0] = compile_mapping(data.mapping);
                        bilerp_textures.push_back(compiled);
                    }
                },
                texture.data);
            texture_headers.push_back(pathtracer::PathTextureHeader{static_cast<std::uint32_t>(kind), local_index, static_cast<std::uint32_t>(texture.value_kind), static_cast<std::uint32_t>(texture.spectrum_type), {}, {}});
            if (progress) progress->report(PathTracerPreparationStage::CompilingTextures, static_cast<std::uint32_t>(texture_headers.size()), static_cast<std::uint32_t>(texture_count));
        }
        const std::uint32_t texture_root_count = static_cast<std::uint32_t>(texture_headers.size());
        std::vector<pathtracer::PathTextureHeader> texture_program{};
        std::function<void(std::uint32_t)> emit_texture_program;
        emit_texture_program = [&](const std::uint32_t source_index) {
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        emit_texture_program(source_texture_index(data.first));
                        emit_texture_program(source_texture_index(data.second));
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        emit_texture_program(source_texture_index(data.first));
                        emit_texture_program(source_texture_index(data.second));
                        emit_texture_program(source_texture_index(data.amount));
                    }
                },
                scene.resources.textures[source_index].data);
            texture_program.push_back(texture_headers[texture_handles[source_index]]);
        };
        for (std::uint32_t handle = 0; handle != texture_root_count; ++handle) {
            pathtracer::PathTextureHeader& root = texture_headers[handle];
            root.program[0]                     = texture_root_count + static_cast<std::uint32_t>(texture_program.size());
            emit_texture_program(texture_order[handle]);
            root.program[1] = texture_root_count + static_cast<std::uint32_t>(texture_program.size()) - root.program[0];
        }
        texture_headers.insert(texture_headers.end(), texture_program.begin(), texture_program.end());
        if (texture_headers.empty()) texture_headers.emplace_back();
        if (texture_mappings.empty()) texture_mappings.emplace_back();
        if (constant_textures.empty()) constant_textures.emplace_back();
        if (image_textures.empty()) image_textures.emplace_back();
        if (checkerboard_textures.empty()) checkerboard_textures.emplace_back();
        if (scale_textures.empty()) scale_textures.emplace_back();
        if (mix_textures.empty()) mix_textures.emplace_back();
        if (direction_mix_textures.empty()) direction_mix_textures.emplace_back();
        if (bilerp_textures.empty()) bilerp_textures.emplace_back();
        std::vector<pathtracer::PathMaterialHeader> material_headers{};
        std::vector<pathtracer::PathDiffuseMaterial> diffuse_materials{};
        std::vector<pathtracer::PathDiffuseTransmissionMaterial> diffuse_transmission_materials{};
        std::vector<pathtracer::PathConductorMaterial> conductor_materials{};
        std::vector<pathtracer::PathDielectricMaterial> dielectric_materials{};
        std::vector<pathtracer::PathThinDielectricMaterial> thin_dielectric_materials{};
        std::vector<pathtracer::PathCoatedDiffuseMaterial> coated_diffuse_materials{};
        std::vector<pathtracer::PathCoatedConductorMaterial> coated_conductor_materials{};
        std::vector<pathtracer::PathMixMaterial> mix_materials{};
        material_headers.reserve(scene.resources.materials.size());
        if (progress) progress->report(PathTracerPreparationStage::CompilingMaterials, 0, static_cast<std::uint32_t>(scene.resources.materials.size()));
        const auto material_index = [&scene](const scene::MaterialId id) {
            const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene.resources.materials, id, &scene::Material::id);
            if (material == scene.resources.materials.end()) throw std::runtime_error(std::format("Material graph references unknown Material id {}", id.value));
            return static_cast<std::uint32_t>(material - scene.resources.materials.begin());
        };
        std::vector<std::uint8_t> material_visit_state(scene.resources.materials.size());
        std::function<void(std::uint32_t)> visit_material_graph;
        visit_material_graph = [&](const std::uint32_t index) {
            if (material_visit_state[index] == 1) throw std::runtime_error(std::format("Material dependency cycle reaches {}", scene.resources.materials[index].name));
            if (material_visit_state[index] == 2) return;
            material_visit_state[index] = 1;
            if (const scene::MixMaterialData* mix = std::get_if<scene::MixMaterialData>(&scene.resources.materials[index].data)) {
                visit_material_graph(material_index(mix->first));
                visit_material_graph(material_index(mix->second));
            }
            material_visit_state[index] = 2;
        };
        for (std::uint32_t index = 0; index != scene.resources.materials.size(); ++index) visit_material_graph(index);
        for (const scene::Material& material : scene.resources.materials) {
            PathMaterialKind kind{};
            std::uint32_t local_index{};
            scene::TextureId normal_map{};
            scene::TextureId bump_map{};
            std::visit(
                [&](const auto& data) {
                    if constexpr (requires {
                                      data.normal_map;
                                      data.bump_map;
                                  }) {
                        normal_map = data.normal_map;
                        bump_map   = data.bump_map;
                    }
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>)
                        kind = PathMaterialKind::Interface;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseMaterialData>) {
                        kind        = PathMaterialKind::Diffuse;
                        local_index = static_cast<std::uint32_t>(diffuse_materials.size());
                        diffuse_materials.push_back(pathtracer::PathDiffuseMaterial{compile_path_spectrum(data.reflectance, spectrum_tables, spectra, piecewise_spectra, texture_handle)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                        kind        = PathMaterialKind::DiffuseTransmission;
                        local_index = static_cast<std::uint32_t>(diffuse_transmission_materials.size());
                        diffuse_transmission_materials.push_back(pathtracer::PathDiffuseTransmissionMaterial{compile_path_spectrum(data.reflectance, spectrum_tables, spectra, piecewise_spectra, texture_handle), compile_path_spectrum(data.transmittance, spectrum_tables, spectra, piecewise_spectra, texture_handle), {data.scale, data.scale, 0.0f, 0.0f}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                        kind        = PathMaterialKind::Conductor;
                        local_index = static_cast<std::uint32_t>(conductor_materials.size());
                        pathtracer::PathConductorMaterial compiled{};
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.eta, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.k                  = compile_path_spectrum(optics.k, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                } else {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.reflectance, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.metadata[0]        = 1;
                                }
                            },
                            data.optics);
                        const CompiledPathFloat u_roughness = compile_path_float(data.distribution.u_roughness.value_or(data.distribution.roughness), texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.distribution.v_roughness.value_or(data.distribution.roughness), texture_handle);
                        compiled.roughness                  = {u_roughness.value, v_roughness.value, 0.0f, 0.0f};
                        compiled.roughness_textures         = {u_roughness.texture, v_roughness.texture, 0, 0};
                        compiled.metadata[1]                = data.remap_roughness ? 1u : 0u;
                        conductor_materials.push_back(compiled);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DielectricMaterialData>) {
                        kind                                = PathMaterialKind::Dielectric;
                        local_index                         = static_cast<std::uint32_t>(dielectric_materials.size());
                        const CompiledPathFloat u_roughness = compile_path_float(data.distribution.u_roughness.value_or(data.distribution.roughness), texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.distribution.v_roughness.value_or(data.distribution.roughness), texture_handle);
                        dielectric_materials.push_back(pathtracer::PathDielectricMaterial{compile_path_spectrum(data.eta, spectrum_tables, spectra, piecewise_spectra, texture_handle), {u_roughness.value, v_roughness.value, 0.0f, 0.0f}, {u_roughness.texture, v_roughness.texture, 0, 0}, {data.remap_roughness ? 1u : 0u, 0, 0, 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                        kind        = PathMaterialKind::ThinDielectric;
                        local_index = static_cast<std::uint32_t>(thin_dielectric_materials.size());
                        thin_dielectric_materials.push_back(pathtracer::PathThinDielectricMaterial{compile_path_spectrum(data.eta, spectrum_tables, spectra, piecewise_spectra, texture_handle)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                        kind                                = PathMaterialKind::CoatedDiffuse;
                        local_index                         = static_cast<std::uint32_t>(coated_diffuse_materials.size());
                        const CompiledPathFloat u_roughness = compile_path_float(data.interface.u_roughness.value_or(data.interface.roughness), texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.interface.v_roughness.value_or(data.interface.roughness), texture_handle);
                        const CompiledPathFloat thickness   = compile_path_float(data.coating.thickness, texture_handle);
                        const CompiledPathFloat g           = compile_path_float(data.coating.g, texture_handle);
                        coated_diffuse_materials.push_back(pathtracer::PathCoatedDiffuseMaterial{
                            compile_path_spectrum(data.reflectance, spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            compile_path_spectrum(data.eta, spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            compile_path_spectrum(data.coating.albedo, spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            {u_roughness.value, v_roughness.value, thickness.value, g.value},
                            {u_roughness.texture, v_roughness.texture, thickness.texture, g.texture},
                            {data.remap_roughness ? 1u : 0u, static_cast<std::uint32_t>(data.coating.max_depth), static_cast<std::uint32_t>(data.coating.sample_count), 0},
                        });
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                        kind        = PathMaterialKind::CoatedConductor;
                        local_index = static_cast<std::uint32_t>(coated_conductor_materials.size());
                        pathtracer::PathCoatedConductorMaterial compiled{};
                        compiled.interface_eta = compile_path_spectrum(data.interface_eta, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.eta, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.k                  = compile_path_spectrum(optics.k, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                } else {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.reflectance, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.metadata[0]        = 1;
                                }
                            },
                            data.optics);
                        compiled.albedo                               = compile_path_spectrum(data.coating.albedo, spectrum_tables, spectra, piecewise_spectra, texture_handle);
                        const CompiledPathFloat interface_u_roughness = compile_path_float(data.interface.u_roughness.value_or(data.interface.roughness), texture_handle);
                        const CompiledPathFloat interface_v_roughness = compile_path_float(data.interface.v_roughness.value_or(data.interface.roughness), texture_handle);
                        compiled.interface_roughness                  = {interface_u_roughness.value, interface_v_roughness.value, 0.0f, 0.0f};
                        compiled.interface_roughness_textures         = {interface_u_roughness.texture, interface_v_roughness.texture, 0, 0};
                        const CompiledPathFloat conductor_u_roughness = compile_path_float(data.conductor.u_roughness.value_or(data.conductor.roughness), texture_handle);
                        const CompiledPathFloat conductor_v_roughness = compile_path_float(data.conductor.v_roughness.value_or(data.conductor.roughness), texture_handle);
                        compiled.conductor_roughness                  = {conductor_u_roughness.value, conductor_v_roughness.value, 0.0f, 0.0f};
                        compiled.conductor_roughness_textures         = {conductor_u_roughness.texture, conductor_v_roughness.texture, 0, 0};
                        const CompiledPathFloat thickness             = compile_path_float(data.coating.thickness, texture_handle);
                        const CompiledPathFloat g                     = compile_path_float(data.coating.g, texture_handle);
                        compiled.coating                              = {thickness.value, g.value, 0.0f, 0.0f};
                        compiled.coating_textures                     = {thickness.texture, g.texture, 0, 0};
                        compiled.metadata[1]                          = data.remap_roughness ? 1u : 0u;
                        compiled.metadata[2]                          = static_cast<std::uint32_t>(data.coating.max_depth);
                        compiled.metadata[3]                          = static_cast<std::uint32_t>(data.coating.sample_count);
                        coated_conductor_materials.push_back(compiled);
                    } else {
                        kind                           = PathMaterialKind::Mix;
                        local_index                    = static_cast<std::uint32_t>(mix_materials.size());
                        const CompiledPathFloat amount = compile_path_float(data.amount, texture_handle);
                        mix_materials.push_back(pathtracer::PathMixMaterial{{material_index(data.first), material_index(data.second), 0, 0}, amount.value, amount.texture, {}});
                    }
                },
                material.data);
            pathtracer::PathMaterialHeader material_header{static_cast<std::uint32_t>(kind), local_index, normal_map.value == 0 ? invalid_texture : texture_handle(normal_map), bump_map.value == 0 ? invalid_texture : texture_handle(bump_map)};
            material_header.stable_id = {
                static_cast<std::uint32_t>(material.id.value),
                static_cast<std::uint32_t>(material.id.value >> 32),
            };
            material_headers.push_back(material_header);
            if (progress) progress->report(PathTracerPreparationStage::CompilingMaterials, static_cast<std::uint32_t>(material_headers.size()), static_cast<std::uint32_t>(scene.resources.materials.size()));
        }
        if (material_headers.empty()) throw std::runtime_error("Path rendering requires at least one Material");
        std::vector<pathtracer::PathMaterialTextureRequest> material_texture_requests{};
        for (std::uint32_t root = 0; root != scene.resources.materials.size(); ++root) {
            const std::uint32_t request_offset = static_cast<std::uint32_t>(material_texture_requests.size());
            std::vector<std::uint8_t> visited(scene.resources.materials.size());
            const auto add_texture_request = [&](const scene::TextureId texture, const PathMaterialTextureRole role, const std::uint32_t owner) {
                if (texture.value == 0) return;
                const std::uint32_t handle = texture_handle(texture);
                for (std::uint32_t request = request_offset; request != material_texture_requests.size(); ++request) {
                    const std::array<std::uint32_t, 4>& data = material_texture_requests[request].data;
                    if (data[0] == handle && data[1] == static_cast<std::uint32_t>(role) && data[2] == owner) return;
                }
                material_texture_requests.push_back(pathtracer::PathMaterialTextureRequest{{handle, static_cast<std::uint32_t>(role), owner, 0}});
            };
            std::function<void(std::uint32_t)> visit_material;
            visit_material = [&](const std::uint32_t index) {
                if (visited[index] != 0) return;
                visited[index]                  = 1;
                const scene::Material& material = scene.resources.materials[index];
                std::visit(
                    [&](const auto& data) {
                        const auto add_parameter    = [&](const auto& parameter) { add_texture_request(parameter.texture, PathMaterialTextureRole::Parameter, std::numeric_limits<std::uint32_t>::max()); };
                        const auto add_surface_maps = [&] {
                            if constexpr (requires {
                                              data.normal_map;
                                              data.bump_map;
                                          }) {
                                add_texture_request(data.normal_map, PathMaterialTextureRole::Normal, index);
                                if (data.normal_map.value == 0) {
                                    add_texture_request(data.bump_map, PathMaterialTextureRole::Bump, index);
                                    add_texture_request(data.bump_map, PathMaterialTextureRole::BumpU, index);
                                    add_texture_request(data.bump_map, PathMaterialTextureRole::BumpV, index);
                                }
                            }
                        };
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InterfaceMaterialData>)
                            return;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.reflectance);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.reflectance);
                            add_parameter(data.transmittance);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                            add_surface_maps();
                            std::visit(
                                [&](const auto& optics) {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                        add_parameter(optics.eta);
                                        add_parameter(optics.k);
                                    } else
                                        add_parameter(optics.reflectance);
                                },
                                data.optics);
                            add_parameter(data.distribution.u_roughness.value_or(data.distribution.roughness));
                            add_parameter(data.distribution.v_roughness.value_or(data.distribution.roughness));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DielectricMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.eta);
                            add_parameter(data.distribution.u_roughness.value_or(data.distribution.roughness));
                            add_parameter(data.distribution.v_roughness.value_or(data.distribution.roughness));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.eta);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.reflectance);
                            add_parameter(data.eta);
                            add_parameter(data.interface.u_roughness.value_or(data.interface.roughness));
                            add_parameter(data.interface.v_roughness.value_or(data.interface.roughness));
                            add_parameter(data.coating.thickness);
                            add_parameter(data.coating.albedo);
                            add_parameter(data.coating.g);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                            add_surface_maps();
                            add_parameter(data.interface_eta);
                            add_parameter(data.interface.u_roughness.value_or(data.interface.roughness));
                            add_parameter(data.interface.v_roughness.value_or(data.interface.roughness));
                            std::visit(
                                [&](const auto& optics) {
                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                        add_parameter(optics.eta);
                                        add_parameter(optics.k);
                                    } else
                                        add_parameter(optics.reflectance);
                                },
                                data.optics);
                            add_parameter(data.conductor.u_roughness.value_or(data.conductor.roughness));
                            add_parameter(data.conductor.v_roughness.value_or(data.conductor.roughness));
                            add_parameter(data.coating.thickness);
                            add_parameter(data.coating.albedo);
                            add_parameter(data.coating.g);
                        } else {
                            add_parameter(data.amount);
                            visit_material(material_index(data.first));
                            visit_material(material_index(data.second));
                        }
                    },
                    material.data);
            };
            visit_material(root);
            material_headers[root].texture_requests = {request_offset, static_cast<std::uint32_t>(material_texture_requests.size()) - request_offset};
        }
        std::uint32_t maximum_material_texture_requests{1};
        for (const pathtracer::PathMaterialHeader& header : material_headers) maximum_material_texture_requests = std::max(maximum_material_texture_requests, header.texture_requests[1]);
        if (material_texture_requests.empty()) material_texture_requests.emplace_back();
        if (diffuse_materials.empty()) diffuse_materials.emplace_back();
        if (diffuse_transmission_materials.empty()) diffuse_transmission_materials.emplace_back();
        if (conductor_materials.empty()) conductor_materials.emplace_back();
        if (dielectric_materials.empty()) dielectric_materials.emplace_back();
        if (thin_dielectric_materials.empty()) thin_dielectric_materials.emplace_back();
        if (coated_diffuse_materials.empty()) coated_diffuse_materials.emplace_back();
        if (coated_conductor_materials.empty()) coated_conductor_materials.emplace_back();
        if (mix_materials.empty()) mix_materials.emplace_back();

        std::vector<pathtracer::PathMedium> media{};
        std::vector<pathtracer::PathVolume> volumes{};
        std::vector<PreparedPathVolume> prepared_volume_resources{};
        const std::uint32_t medium_volume_count = static_cast<std::uint32_t>(scene.resources.media.size() + scene.resources.volumes.size());
        std::uint32_t compiled_medium_volume_count{};
        if (progress) progress->report(PathTracerPreparationStage::CompilingMedia, 0, medium_volume_count);
        const auto compile_medium_spectrum = [&](const scene::SpectrumParameter& parameter) {
            const std::uint32_t index = static_cast<std::uint32_t>(spectra.size());
            spectra.push_back(compile_spectrum(parameter, spectrum_tables, piecewise_spectra));
            return index;
        };
        const auto volume_index = [&scene](const scene::VolumeId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.volumes, id, &scene::Volume::id) - scene.resources.volumes.begin()); };
        media.reserve(scene.resources.media.size());
        for (const scene::Medium& medium : scene.resources.media) {
            std::visit(
                [&](const auto& data) {
                    pathtracer::PathMedium result{};
                    result.spectra = {compile_medium_spectrum(data.sigma_a), compile_medium_spectrum(data.sigma_s), compile_medium_spectrum(data.emission), invalid_path_index};
                    result.scales  = {data.density_scale, data.emission_scale, data.anisotropy, 1.0f};
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::HomogeneousMedium>)
                        result.metadata[0] = std::to_underlying(PathMediumKind::Homogeneous);
                    else {
                        result.metadata[0] = std::to_underlying(PathMediumKind::Volume);
                        result.spectra[3]  = volume_index(data.volume);
                        result.scales[3]   = data.temperature_scale;
                        result.temperature = {data.temperature_offset, data.minimum_emission_temperature, data.blackbody_emission ? 1.0f : 0.0f, 0.0f};
                    }
                    media.push_back(result);
                },
                medium.data);
            if (progress) progress->report(PathTracerPreparationStage::CompilingMedia, ++compiled_medium_volume_count, medium_volume_count);
        }

        volumes.reserve(scene.resources.volumes.size());
        prepared_volume_resources.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) {
            const PathSceneGpuSnapshot::Volume& shared = *std::ranges::find(gpu.volumes, volume.id, &PathSceneGpuSnapshot::Volume::id);
            PreparedPathVolume prepared_volume{};
            prepared_volume.id       = volume.id;
            prepared_volume.revision = shared.revision;
            pathtracer::PathVolume result{};
            result.density                 = resources.zero_volume_field_descriptor;
            result.temperature             = resources.zero_volume_field_descriptor;
            result.emission_scale          = resources.zero_volume_field_descriptor;
            result.sigma_a                 = resources.zero_volume_field_descriptor;
            result.sigma_s                 = resources.zero_volume_field_descriptor;
            result.emission                = resources.zero_volume_field_descriptor;
            result.nanovdb_density         = resources.zero_volume_field_descriptor;
            result.nanovdb_temperature     = resources.zero_volume_field_descriptor;
            result.majorant                = resources.zero_volume_field_descriptor;
            result.bounds_minimum          = {volume.bounds.minimum.x, volume.bounds.minimum.y, volume.bounds.minimum.z, 0.0f};
            result.bounds_maximum          = {volume.bounds.maximum.x, volume.bounds.maximum.y, volume.bounds.maximum.z, 0.0f};
            const math::Transform inverse  = volume.transform.inverse();
            result.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            result.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            result.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
            const auto field               = [&resources, &shared](const GpuVolumeField field) -> DescriptorHandle {
                const std::size_t index = std::to_underlying(field);
                return shared.field_present[index] ? shared.descriptors[index] : resources.zero_volume_field_descriptor;
            };
            const auto prepare_majorant = [&](const auto values) -> DescriptorHandle {
                prepared_volume.majorant.assign(values.begin(), values.end());
                return resources.zero_volume_field_descriptor;
            };
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                        prepared_volume.resolution        = data.resolution;
                        result.metadata                   = {std::to_underlying(PathVolumeKind::DensityGrid), data.resolution.x, data.resolution.y, data.resolution.z};
                        result.majorant_metadata          = {16, 16, 16, (data.temperature.empty() ? 0u : 1u) | (data.emission_scale.empty() ? 0u : 2u)};
                        result.density                    = field(GpuVolumeField::Density);
                        result.temperature                = field(GpuVolumeField::Temperature);
                        result.emission_scale             = field(GpuVolumeField::EmissionScale);
                        const std::vector<float> majorant = build_density_majorant(data);
                        result.majorant                   = prepare_majorant(std::span<const float>{majorant});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                        prepared_volume.resolution        = data.resolution;
                        result.metadata                   = {std::to_underlying(PathVolumeKind::RgbGrid), data.resolution.x, data.resolution.y, data.resolution.z};
                        result.majorant_metadata          = {16, 16, 16, (data.sigma_a.empty() ? 0u : 1u) | (data.sigma_s.empty() ? 0u : 2u) | (data.emission.empty() ? 0u : 4u) | (std::to_underlying(data.color_space) << 8)};
                        result.sigma_a                    = field(GpuVolumeField::SigmaA);
                        result.sigma_s                    = field(GpuVolumeField::SigmaS);
                        result.emission                   = field(GpuVolumeField::Emission);
                        const std::vector<float> majorant = build_rgb_majorant(data, spectrum_tables);
                        result.majorant                   = prepare_majorant(std::span<const float>{majorant});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::NanoVdbVolume>) {
                        result.metadata[0]         = std::to_underlying(PathVolumeKind::NanoVdb);
                        result.majorant_metadata   = {data.majorant_resolution.x, data.majorant_resolution.y, data.majorant_resolution.z, data.temperature_data.empty() ? 0u : 1u};
                        result.nanovdb_density     = field(GpuVolumeField::NanoVdbDensity);
                        result.nanovdb_temperature = field(GpuVolumeField::NanoVdbTemperature);
                        result.majorant            = prepare_majorant(std::span<const float>{data.majorant});
                    } else {
                        result.metadata[0]           = std::to_underlying(PathVolumeKind::ProceduralCloud);
                        result.procedural_parameters = {data.density, data.wispiness, data.frequency, 0.0f};
                        const std::array<float, 1> majorant{1.0f};
                        result.majorant = prepare_majorant(std::span<const float>{majorant});
                    }
                },
                volume.data);
            volumes.push_back(result);
            prepared_volume_resources.push_back(std::move(prepared_volume));
            if (progress) progress->report(PathTracerPreparationStage::CompilingMedia, ++compiled_medium_volume_count, medium_volume_count);
        }
        if (media.empty()) media.emplace_back();
        if (volumes.empty()) volumes.emplace_back();

        const math::Bounds3 bounds      = scene.bounds();
        const math::Float3 scene_center = bounds.center();
        const float scene_radius        = bounds.radius();
        std::vector<pathtracer::PathLight> lights{};
        std::vector<pathtracer::PathLightDistribution> light_distributions{};
        std::vector<float> light_distribution_data{};
        std::vector<pathtracer::PathPortal> portals{};
        std::vector<pathtracer::PathLightBvhNode> light_bvh_nodes{};
        std::vector<std::uint32_t> light_bvh_bit_trails{};
        std::uint32_t light_bvh_infinite_count{};
        std::uint32_t light_bvh_node_count{};
        std::vector<std::uint32_t> area_spectra(scene.resources.lights.size(), invalid_path_index);
        std::vector<std::uint32_t> area_textures(scene.resources.lights.size(), invalid_path_index);
        std::vector<float> area_scales(scene.resources.lights.size());
        std::vector<float> area_powers(scene.resources.lights.size(), -1.0f);
        std::vector<float> area_texture_luminance(scene.resources.lights.size(), 1.0f);
        std::vector<float> area_texture_power(scene.resources.lights.size(), 1.0f);
        std::vector<float> area_texture_channel_average(scene.resources.lights.size(), 1.0f);
        std::vector<bool> area_two_sided(scene.resources.lights.size());
        const auto compile_light_spectrum = [&](const scene::SpectrumParameter& parameter) {
            const std::uint32_t index = static_cast<std::uint32_t>(spectra.size());
            spectra.push_back(compile_spectrum(parameter, spectrum_tables, piecewise_spectra));
            return index;
        };
        const auto image_texture = [&](const scene::TextureId id) -> const scene::Texture& {
            const std::vector<scene::Texture>::const_iterator found = std::ranges::find(scene.resources.textures, id, &scene::Texture::id);
            if (found == scene.resources.textures.end()) throw std::runtime_error(std::format("Light references unknown Image Texture id {}", id.value));
            if (found->value_kind != scene::TextureValueKind::Spectrum || found->spectrum_type != scene::TextureSpectrumType::Illuminant || !std::holds_alternative<scene::ImageTexture>(found->data)) throw std::runtime_error(std::format("Light Texture {} must be an Illuminant Spectrum Image Texture", found->name));
            return *found;
        };
        struct LightImageStatistics {
            float luminance{};
            float spectral_power{};
            float channel_average{};
        };
        const auto light_image_statistics = [&](const scene::Texture& texture) {
            const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
            math::Float3 luminance{0.212671f, 0.715160f, 0.072169f};
            if (texture.color_space == scene::TextureColorSpace::Rec2020)
                luminance = {0.262700f, 0.677998f, 0.059302f};
            else if (texture.color_space == scene::TextureColorSpace::Aces2065_1)
                luminance = {0.3439664498f, 0.7281660966f, -0.0721325464f};
            const scene::SpectrumColorSpace color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
            float luminance_sum{};
            float spectral_power_sum{};
            float channel_sum{};
            std::vector<math::Float2> unused_piecewise_samples{};
            const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
            for (std::size_t index = 0; index != pixel_count; ++index) {
                const math::Float3 rgb{std::max(0.0f, image.texels[index].x), std::max(0.0f, image.texels[index].y), std::max(0.0f, image.texels[index].z)};
                luminance_sum += rgb.x * luminance.x + rgb.y * luminance.y + rgb.z * luminance.z;
                channel_sum += (rgb.x + rgb.y + rgb.z) / 3.0f;
                const CompiledSpectrum compiled = compile_spectrum(scene::SpectrumParameter{{rgb.x * image.scale, rgb.y * image.scale, rgb.z * image.scale}, {}, scene::SpectrumEncoding::RgbIlluminant, color_space}, spectrum_tables, unused_piecewise_samples);
                spectral_power_sum += spectrum_power_sample(compiled, unused_piecewise_samples, resources.cie_samples);
            }
            return LightImageStatistics{luminance_sum / static_cast<float>(pixel_count) * image.scale, spectral_power_sum / static_cast<float>(pixel_count), channel_sum / static_cast<float>(pixel_count) * image.scale};
        };
        const auto initialize_light_transform = [](pathtracer::PathLight& target, const math::Transform& transform) {
            const math::Transform inverse  = transform.inverse();
            target.transform_row_0         = {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]};
            target.transform_row_1         = {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]};
            target.transform_row_2         = {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]};
            target.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            target.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            target.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
        };
        if (progress) progress->report(PathTracerPreparationStage::CompilingLights, 0, static_cast<std::uint32_t>(scene.resources.lights.size()));
        for (std::uint32_t source_index = 0; source_index != scene.resources.lights.size(); ++source_index) {
            const scene::Light& resource = scene.resources.lights[source_index];
            std::visit(
                [&](const auto& light) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::DiffuseAreaLight>) {
                        area_spectra[source_index]   = compile_light_spectrum(light.radiance);
                        area_scales[source_index]    = light.scale;
                        area_powers[source_index]    = light.power.value_or(-1.0f);
                        area_two_sided[source_index] = light.sidedness == scene::EmissionSidedness::Both;
                        if (light.emission_texture.value != 0) {
                            const scene::Texture& texture              = image_texture(light.emission_texture);
                            area_textures[source_index]                = texture_handle(light.emission_texture);
                            const LightImageStatistics statistics      = light_image_statistics(texture);
                            area_texture_luminance[source_index]       = statistics.luminance;
                            area_texture_power[source_index]           = statistics.spectral_power;
                            area_texture_channel_average[source_index] = statistics.channel_average;
                        }
                    } else {
                        pathtracer::PathLight compiled{};
                        compiled.references     = {invalid_path_index, invalid_path_index, invalid_path_index, invalid_path_index};
                        compiled.identifiers[3] = invalid_path_index;
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::PointLight>) {
                            compiled.identifiers        = {std::to_underlying(PathLightKind::Point), 1u, compile_light_spectrum(light.intensity), invalid_path_index};
                            const math::Float3 position = light.transform.transform_point({});
                            compiled.position_and_scale = {position.x, position.y, position.z, light.scale};
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = 4.0f * path_pi * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            compiled.selection[3] = 4.0f * path_pi * light.scale * spectrum_maximum(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            lights.push_back(compiled);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::SpotLight>) {
                            compiled.identifiers        = {std::to_underlying(PathLightKind::Spot), 1u, compile_light_spectrum(light.intensity), invalid_path_index};
                            const math::Float3 position = light.transform.transform_point({});
                            compiled.position_and_scale = {position.x, position.y, position.z, light.scale};
                            compiled.parameters[0]      = std::cos((light.cone_angle - light.cone_delta) * path_pi / 180.0f);
                            compiled.parameters[1]      = std::cos(light.cone_angle * path_pi / 180.0f);
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = 2.0f * path_pi * (1.0f - compiled.parameters[0] + (compiled.parameters[0] - compiled.parameters[1]) / 2.0f) * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            compiled.selection[3] = 4.0f * path_pi * light.scale * spectrum_maximum(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            lights.push_back(compiled);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::DistantLight>) {
                            compiled.identifiers         = {std::to_underlying(PathLightKind::Distant), 2u, compile_light_spectrum(light.radiance), invalid_path_index};
                            const math::Float3 direction = light.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                            compiled.position_and_scale  = {direction.x, direction.y, direction.z, light.scale};
                            compiled.parameters          = {scene_center.x, scene_center.y, scene_center.z, scene_radius};
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = path_pi * scene_radius * scene_radius * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            lights.push_back(compiled);
                        } else {
                            const scene::InfiniteLight& environment = [&]() -> const scene::InfiniteLight& {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::InfiniteLight>)
                                    return light;
                                else
                                    return light.environment;
                            }();
                            compiled.identifiers        = {std::to_underlying(std::same_as<std::remove_cvref_t<decltype(light)>, scene::InfiniteLight> ? PathLightKind::Infinite : PathLightKind::PortalInfinite), 4u, compile_light_spectrum(environment.radiance), invalid_path_index};
                            compiled.position_and_scale = {scene_center.x, scene_center.y, scene_center.z, environment.scale};
                            compiled.parameters[3]      = scene_radius;
                            initialize_light_transform(compiled, environment.transform);
                            float average_power = spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, resources.cie_samples);
                            if (environment.emission_texture.value != 0) {
                                const scene::Texture& texture    = image_texture(environment.emission_texture);
                                const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
                                if (image.invert) throw std::runtime_error(std::format("Light Texture {} cannot use inversion", texture.name));
                                if (image.width != image.height) throw std::runtime_error(std::format("Infinite Light Texture {} must use a square equal-area projection", texture.name));
                                const std::uint32_t handle = texture_handle(environment.emission_texture);
                                compiled.identifiers[1] |= 16u;
                                compiled.identifiers[3] = texture_headers[handle].local_index;
                                std::vector<float> weights(static_cast<std::size_t>(image.width) * image.height);
                                for (std::size_t pixel = 0; pixel != weights.size(); ++pixel) weights[pixel] = (image.texels[pixel].x + image.texels[pixel].y + image.texels[pixel].z) / 3.0f;
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::InfiniteLight>) {
                                    compiled.references[1] = compile_light_distribution(weights, image.width, image.height, light_distributions, light_distribution_data);
                                    const float average    = std::accumulate(weights.begin(), weights.end(), 0.0f) / static_cast<float>(weights.size());
                                    for (float& weight : weights) weight = std::max(weight - average, 0.0f);
                                    if (std::ranges::all_of(weights, [](const float weight) { return weight == 0.0f; })) std::ranges::fill(weights, 1.0f);
                                    compiled.references[2] = compile_light_distribution(weights, image.width, image.height, light_distributions, light_distribution_data);
                                }
                                average_power = light_image_statistics(texture).spectral_power;
                            }
                            compiled.selection[0] = 4.0f * path_pi * path_pi * scene_radius * scene_radius * environment.scale * average_power;
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::InfiniteLight>)
                                lights.push_back(compiled);
                            else {
                                if (environment.emission_texture.value == 0) throw std::runtime_error("Portal Infinite Light requires an Environment Image Texture");
                                const scene::Texture& texture                      = image_texture(environment.emission_texture);
                                const scene::ImageTexture& image                   = std::get<scene::ImageTexture>(texture.data);
                                const math::Transform light_from_render            = environment.transform.inverse();
                                const scene::SpectrumColorSpace portal_color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
                                const std::uint32_t portal_illuminant_curve        = portal_color_space == scene::SpectrumColorSpace::Aces2065_1 ? 4u : 3u;
                                for (const std::array<math::Float3, 4>& points : light.portals) {
                                    const math::Float3 frame_x       = (points[3] - points[0]).normalized();
                                    const math::Float3 frame_y       = (points[1] - points[0]).normalized();
                                    const math::Float3 frame_z       = frame_x.cross(frame_y).normalized();
                                    const std::uint32_t portal_index = static_cast<std::uint32_t>(portals.size());
                                    portals.push_back(pathtracer::PathPortal{{points[0].x, points[0].y, points[0].z, 0.0f}, {points[2].x, points[2].y, points[2].z, 0.0f}, {frame_x.x, frame_x.y, frame_x.z, 0.0f}, {frame_y.x, frame_y.y, frame_y.z, 0.0f}, {frame_z.x, frame_z.y, frame_z.z, 0.0f}});
                                    std::vector<float> weights(static_cast<std::size_t>(image.width) * image.height);
                                    std::vector<CompiledSpectrum> rectified_spectra(weights.size());
                                    float portal_power_sum{};
                                    std::vector<math::Float2> unused_piecewise_samples{};
                                    for (std::uint32_t y = 0; y != image.height; ++y)
                                        for (std::uint32_t x = 0; x != image.width; ++x) {
                                            const math::Float2 uv{(static_cast<float>(x) + 0.5f) / static_cast<float>(image.width), (static_cast<float>(y) + 0.5f) / static_cast<float>(image.height)};
                                            const float alpha                      = -path_pi / 2.0f + uv.x * path_pi;
                                            const float beta                       = -path_pi / 2.0f + uv.y * path_pi;
                                            const math::Float3 local               = math::Float3{std::tan(alpha), std::tan(beta), 1.0f}.normalized();
                                            const math::Float3 render_direction    = frame_x * local.x + frame_y * local.y + frame_z * local.z;
                                            const math::Float3 light_direction     = light_from_render.transform_vector(render_direction).normalized();
                                            const math::Float4 value               = image_bilerp_octahedral(image, equal_area_sphere_to_square(light_direction));
                                            const float jacobian                   = path_pi * path_pi * (1.0f - local.x * local.x) * (1.0f - local.y * local.y) / local.z;
                                            weights[y * image.width + x]           = (value.x + value.y + value.z) / 3.0f * jacobian;
                                            const CompiledSpectrum rectified       = compile_spectrum(scene::SpectrumParameter{{std::max(0.0f, value.x), std::max(0.0f, value.y), std::max(0.0f, value.z)}, {}, scene::SpectrumEncoding::RgbIlluminant, portal_color_space}, spectrum_tables, unused_piecewise_samples);
                                            rectified_spectra[y * image.width + x] = rectified;
                                            portal_power_sum += spectrum_power_sample(rectified, unused_piecewise_samples, resources.cie_samples) * image.scale / jacobian;
                                        }
                                    pathtracer::PathLight portal_light                     = compiled;
                                    portal_light.references[1]                             = compile_light_distribution(weights, image.width, image.height, light_distributions, light_distribution_data);
                                    pathtracer::PathLightDistribution& portal_distribution = light_distributions[portal_light.references[1]];
                                    portal_distribution.offsets[2]                         = static_cast<std::uint32_t>(light_distribution_data.size());
                                    portal_distribution.offsets[3]                         = portal_illuminant_curve;
                                    portal_distribution.parameters[2]                      = image.scale;
                                    for (const CompiledSpectrum& spectrum : rectified_spectra) light_distribution_data.insert(light_distribution_data.end(), spectrum.parameters.begin(), spectrum.parameters.end());
                                    portal_light.references[3] = portal_index;
                                    const float area           = (points[1] - points[0]).length() * (points[3] - points[0]).length();
                                    portal_light.selection[0]  = environment.scale * area * portal_power_sum / static_cast<float>(weights.size());
                                    lights.push_back(portal_light);
                                }
                            }
                        }
                    }
                },
                resource.data);
            if (progress) progress->report(PathTracerPreparationStage::CompilingLights, source_index + 1u, static_cast<std::uint32_t>(scene.resources.lights.size()));
        }

        std::vector<pathtracer::PathPrimitive> primitives{};
        std::vector<pathtracer::PathLightShape> light_shapes{};
        std::vector<PathDynamicAreaLightRange> dynamic_area_lights{};
        std::vector<std::uint32_t> face_materials{};
        const auto medium_index = [&scene](const scene::MediumId id) {
            if (id.value == 0) return invalid_path_index;
            return static_cast<std::uint32_t>(std::ranges::find(scene.resources.media, id, &scene::Medium::id) - scene.resources.media.begin());
        };
        const std::uint32_t camera_medium_index = medium_index(scene.camera.medium);
        const auto add_area_light               = [&](pathtracer::PathLightShape shape, const std::uint32_t source_index, const float area, const bool delta_position) {
            const std::uint32_t shape_index = static_cast<std::uint32_t>(light_shapes.size());
            const std::uint32_t light_index = static_cast<std::uint32_t>(lights.size());
            shape.identifiers[0]            = light_index;
            light_shapes.push_back(shape);
            float scale = area_scales[source_index];
            if (area_powers[source_index] > 0.0f) scale *= area_powers[source_index] / ((area_two_sided[source_index] ? 2.0f : 1.0f) * area * path_pi * area_texture_luminance[source_index]);
            pathtracer::PathLight light{};
            light.identifiers = {std::to_underlying(PathLightKind::DiffuseArea), (area_two_sided[source_index] ? 8u : 0u) | (delta_position ? 1u : 0u) | 32u | 64u, area_spectra[source_index], area_textures[source_index]};
            if (area_textures[source_index] != invalid_path_index) light.identifiers[1] |= 16u;
            light.references            = {shape_index, invalid_path_index, invalid_path_index, invalid_path_index};
            light.position_and_scale[3] = scale;
            const float spectral_power  = area_textures[source_index] == invalid_path_index ? spectrum_power_sample(spectra[area_spectra[source_index]], piecewise_spectra, resources.cie_samples) : area_texture_power[source_index];
            light.selection[0]          = path_pi * (area_two_sided[source_index] ? 2.0f : 1.0f) * area * scale * spectral_power;
            light.selection[3]          = path_pi * area * scale * (area_textures[source_index] == invalid_path_index ? spectrum_maximum(spectra[area_spectra[source_index]], piecewise_spectra, resources.cie_samples) : area_texture_channel_average[source_index]);
            lights.push_back(light);
        };
        primitives.reserve(gpu.acceleration_primitive_indices.size());
        for (const std::uint32_t scene_primitive_index : gpu.acceleration_primitive_indices) {
            const GpuScenePrimitive& gpu_primitive   = gpu.primitives[scene_primitive_index];
            const scene::Instance& instance          = scene.resources.instances[gpu_primitive.scene_instance_index];
            const scene::Prototype& prototype        = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive        = prototype.primitives[gpu_primitive.prototype_primitive_index];
            const std::uint32_t face_material_offset = static_cast<std::uint32_t>(face_materials.size());
            for (const scene::MaterialId face_material : primitive.face_materials) {
                const std::vector<scene::Material>::const_iterator resource = std::ranges::find(scene.resources.materials, face_material, &scene::Material::id);
                face_materials.push_back(static_cast<std::uint32_t>(resource - scene.resources.materials.begin()));
            }
            const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::Material::id);
            const std::uint32_t material_index                          = static_cast<std::uint32_t>(material - scene.resources.materials.begin());
            std::uint32_t alpha                                         = invalid_texture;
            bool zero_alpha_area_light{};
            if (primitive.alpha.value != 0) {
                const scene::Texture& texture = *std::ranges::find(scene.resources.textures, primitive.alpha, &scene::Texture::id);
                if (texture.value_kind != scene::TextureValueKind::Float) throw std::runtime_error(std::format("Primitive Alpha {} must reference a Float Texture", texture.name));
                alpha = texture_handle(primitive.alpha);
                if (const scene::ConstantTexture* constant = std::get_if<scene::ConstantTexture>(&texture.data)) zero_alpha_area_light = constant->scalar == 0.0f;
            }
            std::uint32_t area_light = std::numeric_limits<std::uint32_t>::max();
            if (primitive.area_light.value != 0) {
                const std::vector<scene::Light>::const_iterator light = std::ranges::find(scene.resources.lights, primitive.area_light, &scene::Light::id);
                area_light                                            = static_cast<std::uint32_t>(light - scene.resources.lights.begin());
            }
            const std::uint32_t first_light_shape = static_cast<std::uint32_t>(lights.size());
            const bool sphere_set                 = gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet;
            const scene::Geometry* geometry{};
            const scene::TriangleMeshGeometry* mesh{};
            if (!sphere_set) {
                geometry = &*std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
                mesh     = std::get_if<scene::TriangleMeshGeometry>(&geometry->data);
            }
            std::uint32_t geometry_kind = sphere_set ? shader_semantics::geometry_sphere_set : shader_semantics::geometry_triangle;
            std::array<float, 4> geometry_parameters{};
            if (!sphere_set)
                std::visit(
                    [&geometry_kind, &geometry_parameters](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphereGeometry>) {
                            geometry_kind       = shader_semantics::geometry_sphere;
                            geometry_parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiskGeometry>) {
                            geometry_kind       = shader_semantics::geometry_disk;
                            geometry_parameters = {data.height, data.radius, data.inner_radius, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CylinderGeometry>) {
                            geometry_kind       = shader_semantics::geometry_cylinder;
                            geometry_parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::BoxGeometry>)
                            geometry_kind = shader_semantics::geometry_box;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RectangleGeometry>)
                            geometry_kind = shader_semantics::geometry_rectangle;
                    },
                    geometry->data);
            if (area_light != std::numeric_limits<std::uint32_t>::max()) {
                const math::Transform transform = instance.transform * primitive.transform;
                if (mesh) {
                    const PathSceneGpuSnapshot::Geometry& gpu_geometry = gpu.geometries[gpu_primitive.resource_index];
                    const std::uint32_t capacity                       = gpu_geometry.index_capacity / 3u;
                    for (std::uint32_t triangle = 0; triangle != capacity; ++triangle) add_area_light(pathtracer::PathLightShape{{0.0f, 0.0f, 0.0f, 1.0f}, {}, {}, {0, primitive.reverse_orientation ? 1u : 0u, geometry_kind, zero_alpha_area_light ? invalid_texture : alpha}, {}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}, area_light, 1.0f, zero_alpha_area_light);
                    dynamic_area_lights.push_back({gpu_primitive.scene_primitive_index, gpu_primitive.resource_index, first_light_shape, capacity, 0, geometry_kind, primitive.reverse_orientation ? 1u : 0u, zero_alpha_area_light ? invalid_texture : alpha, gpu_geometry.attribute_mask, geometry_parameters, {area_scales[area_light], area_powers[area_light], area_two_sided[area_light] ? 2.0f : 1.0f, area_texture_luminance[area_light]}, {area_textures[area_light] == invalid_path_index ? spectrum_power_sample(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_power[area_light], area_textures[area_light] == invalid_path_index ? spectrum_maximum(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_channel_average[area_light], 0.0f, 0.0f}});
                } else if (sphere_set) {
                    const PathSceneGpuSnapshot::SphereSet& gpu_spheres = gpu.sphere_sets[gpu_primitive.resource_index];
                    for (std::uint32_t sphere = 0; sphere != gpu_spheres.capacity; ++sphere) add_area_light(pathtracer::PathLightShape{{0.0f, 0.0f, 0.0f, 1.0f}, {}, {}, {0, primitive.reverse_orientation ? 1u : 0u, 1u, zero_alpha_area_light ? invalid_texture : alpha}, {1.0f, -1.0f, 1.0f, 2.0f * path_pi}, {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]}, {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]}, {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]}}, area_light, 1.0f, zero_alpha_area_light);
                    dynamic_area_lights.push_back({gpu_primitive.scene_primitive_index, gpu_primitive.resource_index, first_light_shape, gpu_spheres.capacity, 1, 1, primitive.reverse_orientation ? 1u : 0u, zero_alpha_area_light ? invalid_texture : alpha, 0, {}, {area_scales[area_light], area_powers[area_light], area_two_sided[area_light] ? 2.0f : 1.0f, area_texture_luminance[area_light]}, {area_textures[area_light] == invalid_path_index ? spectrum_power_sample(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_power[area_light], area_textures[area_light] == invalid_path_index ? spectrum_maximum(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_channel_average[area_light], 0.0f, 0.0f}});
                } else {
                    const float object_area = scene::surface_area(*geometry);
                    const float world_area  = scene::surface_area(*geometry, transform);
                    std::array<float, 4> position_0{0.0f, 0.0f, 0.0f, object_area};
                    std::array<float, 4> position_1{};
                    if (const scene::BoxGeometry* box = std::get_if<scene::BoxGeometry>(&geometry->data)) {
                        position_0[0] = box->bounds.minimum.x;
                        position_0[1] = box->bounds.minimum.y;
                        position_0[2] = box->bounds.minimum.z;
                        position_1    = {box->bounds.maximum.x, box->bounds.maximum.y, box->bounds.maximum.z, 0.0f};
                    } else if (const scene::RectangleGeometry* rectangle = std::get_if<scene::RectangleGeometry>(&geometry->data)) {
                        position_0[0] = rectangle->minimum.x;
                        position_0[1] = rectangle->minimum.y;
                        position_1    = {rectangle->maximum.x, rectangle->maximum.y, 0.0f, 0.0f};
                    }
                    add_area_light(pathtracer::PathLightShape{position_0, position_1, {}, {0, primitive.reverse_orientation ? 1u : 0u, geometry_kind, zero_alpha_area_light ? invalid_texture : alpha}, geometry_parameters, {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]}, {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]}, {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]}}, area_light, world_area, zero_alpha_area_light);
                    dynamic_area_lights.push_back({gpu_primitive.scene_primitive_index, gpu_primitive.resource_index, first_light_shape, 1, 2, geometry_kind, primitive.reverse_orientation ? 1u : 0u, zero_alpha_area_light ? invalid_texture : alpha, 0, geometry_parameters, {area_scales[area_light], area_powers[area_light], area_two_sided[area_light] ? 2.0f : 1.0f, area_texture_luminance[area_light]}, {area_textures[area_light] == invalid_path_index ? spectrum_power_sample(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_power[area_light], area_textures[area_light] == invalid_path_index ? spectrum_maximum(spectra[area_spectra[area_light]], piecewise_spectra, resources.cie_samples) : area_texture_channel_average[area_light], 0.0f, 0.0f}});
                }
            }
            if (sphere_set) {
                const PathSceneGpuSnapshot::SphereSet& gpu_spheres = gpu.sphere_sets[gpu_primitive.resource_index];
                primitives.push_back(pathtracer::PathPrimitive{material_index, area_light, first_light_shape, primitive.reverse_orientation ? 1u : 0u, gpu_spheres.radii, gpu_spheres.positions, {}, {}, {0, alpha, geometry_kind, 0}, geometry_parameters, {face_material_offset, static_cast<std::uint32_t>(primitive.face_materials.size()), 0, 0}, {medium_index(primitive.media.inside), medium_index(primitive.media.outside), 0, 0}, {static_cast<std::uint32_t>(instance.id.value), static_cast<std::uint32_t>(instance.id.value >> 32)}});
            } else {
                const PathSceneGpuSnapshot::Geometry& gpu_geometry = gpu.geometries[gpu_primitive.resource_index];
                primitives.push_back(pathtracer::PathPrimitive{material_index, area_light, first_light_shape, primitive.reverse_orientation ? 1u : 0u, gpu_geometry.indices, gpu_geometry.normals, gpu_geometry.tangents, gpu_geometry.texture_coordinates, {gpu_geometry.attribute_mask, alpha, geometry_kind, 0}, geometry_parameters, {face_material_offset, static_cast<std::uint32_t>(primitive.face_materials.size()), 0, 0}, {medium_index(primitive.media.inside), medium_index(primitive.media.outside), 0, 0}, {static_cast<std::uint32_t>(instance.id.value), static_cast<std::uint32_t>(instance.id.value >> 32)}});
            }
            if (progress) progress->report(PathTracerPreparationStage::CompilingGeometry, static_cast<std::uint32_t>(primitives.size()), static_cast<std::uint32_t>(gpu.acceleration_primitive_indices.size()));
        }
        const std::uint32_t light_count = static_cast<std::uint32_t>(lights.size());
        if (!lights.empty()) {
            if (scene.transport.light_sampler == scene::LightSamplerKind::Uniform) {
                const float probability = 1.0f / static_cast<float>(lights.size());
                float cumulative{};
                for (pathtracer::PathLight& light : lights) {
                    light.selection[1] = probability;
                    cumulative += probability;
                    light.selection[2] = cumulative;
                }
                lights.back().selection[2] = 1.0f;
            } else if (scene.transport.light_sampler == scene::LightSamplerKind::Power) {
                float total           = std::transform_reduce(lights.begin(), lights.end(), 0.0f, std::plus{}, [](const pathtracer::PathLight& light) { return light.selection[0]; });
                const bool zero_power = total == 0.0f;
                if (zero_power) total = static_cast<float>(lights.size());
                float cumulative{};
                for (pathtracer::PathLight& light : lights) {
                    light.selection[1] = zero_power ? 1.0f / total : light.selection[0] / total;
                    cumulative += light.selection[1];
                    light.selection[2] = cumulative;
                }
                lights.back().selection[2] = 1.0f;
            } else {
                if (progress) progress->report(PathTracerPreparationStage::BuildingLightBvh);
                struct LightBounds {
                    math::Float3 minimum{};
                    math::Float3 maximum{};
                    math::Float3 direction{0.0f, 0.0f, 1.0f};
                    float phi{};
                    float cos_theta_o{-1.0f};
                    float cos_theta_e{};
                    bool two_sided{};
                };
                const auto include = [](math::Float3& minimum, math::Float3& maximum, const math::Float3 point) {
                    minimum.x = std::min(minimum.x, point.x);
                    minimum.y = std::min(minimum.y, point.y);
                    minimum.z = std::min(minimum.z, point.z);
                    maximum.x = std::max(maximum.x, point.x);
                    maximum.y = std::max(maximum.y, point.y);
                    maximum.z = std::max(maximum.z, point.z);
                };
                const std::function<LightBounds(const LightBounds&, const LightBounds&)> union_bounds = [](const LightBounds& first, const LightBounds& second) {
                    if (first.phi == 0.0f) return second;
                    if (second.phi == 0.0f) return first;
                    LightBounds result{{std::min(first.minimum.x, second.minimum.x), std::min(first.minimum.y, second.minimum.y), std::min(first.minimum.z, second.minimum.z)}, {std::max(first.maximum.x, second.maximum.x), std::max(first.maximum.y, second.maximum.y), std::max(first.maximum.z, second.maximum.z)}, {}, first.phi + second.phi, {}, std::min(first.cos_theta_e, second.cos_theta_e), first.two_sided || second.two_sided};
                    const float theta_first     = std::acos(std::clamp(first.cos_theta_o, -1.0f, 1.0f));
                    const float theta_second    = std::acos(std::clamp(second.cos_theta_o, -1.0f, 1.0f));
                    const float theta_direction = std::acos(std::clamp(first.direction.dot(second.direction), -1.0f, 1.0f));
                    if (std::min(theta_direction + theta_second, path_pi) <= theta_first) {
                        result.direction   = first.direction;
                        result.cos_theta_o = first.cos_theta_o;
                        return result;
                    }
                    if (std::min(theta_direction + theta_first, path_pi) <= theta_second) {
                        result.direction   = second.direction;
                        result.cos_theta_o = second.cos_theta_o;
                        return result;
                    }
                    const float theta = (theta_first + theta_direction + theta_second) / 2.0f;
                    if (theta >= path_pi) {
                        result.direction   = {0.0f, 0.0f, 1.0f};
                        result.cos_theta_o = -1.0f;
                        return result;
                    }
                    const math::Float3 axis = first.direction.cross(second.direction);
                    const float axis_length = axis.length();
                    if (axis_length == 0.0f) {
                        result.direction   = {0.0f, 0.0f, 1.0f};
                        result.cos_theta_o = -1.0f;
                        return result;
                    }
                    const math::Float3 normalized_axis = axis / axis_length;
                    const float rotation               = theta - theta_first;
                    const math::Float3 rotated         = first.direction * std::cos(rotation) + normalized_axis.cross(first.direction) * std::sin(rotation);
                    result.direction                   = (rotated + normalized_axis * normalized_axis.dot(first.direction) * (1.0f - std::cos(rotation))).normalized();
                    result.cos_theta_o                 = std::cos(theta);
                    return result;
                };
                std::vector<std::pair<std::uint32_t, LightBounds>> bounded_lights{};
                for (std::uint32_t light_index = 0; light_index != lights.size(); ++light_index) {
                    const pathtracer::PathLight& light = lights[light_index];
                    const PathLightKind kind           = static_cast<PathLightKind>(light.identifiers[0]);
                    if (kind == PathLightKind::Distant || kind == PathLightKind::Infinite || kind == PathLightKind::PortalInfinite) {
                        ++light_bvh_infinite_count;
                        continue;
                    }
                    LightBounds light_bounds{};
                    light_bounds.minimum = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                    light_bounds.maximum = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
                    light_bounds.phi     = std::max(light.selection[3], 0.0f);
                    if (kind == PathLightKind::Point || kind == PathLightKind::Spot) {
                        const math::Float3 position{light.position_and_scale[0], light.position_and_scale[1], light.position_and_scale[2]};
                        light_bounds.minimum = position;
                        light_bounds.maximum = position;
                        if (kind == PathLightKind::Spot) {
                            light_bounds.direction   = math::Float3{light.transform_row_0[2], light.transform_row_1[2], light.transform_row_2[2]}.normalized();
                            light_bounds.cos_theta_o = light.parameters[0];
                            light_bounds.cos_theta_e = std::cos(std::acos(std::clamp(light.parameters[1], -1.0f, 1.0f)) - std::acos(std::clamp(light.parameters[0], -1.0f, 1.0f)));
                        }
                    } else {
                        const pathtracer::PathLightShape& shape = light_shapes[light.references[0]];
                        const std::uint32_t geometry            = shape.identifiers[2];
                        if (geometry == 0) {
                            const math::Float3 points[3]{{shape.position_0_and_area[0], shape.position_0_and_area[1], shape.position_0_and_area[2]}, {shape.position_1[0], shape.position_1[1], shape.position_1[2]}, {shape.position_2[0], shape.position_2[1], shape.position_2[2]}};
                            for (const math::Float3 point : points) include(light_bounds.minimum, light_bounds.maximum, point);
                            light_bounds.direction = (points[1] - points[0]).cross(points[2] - points[0]).normalized();
                            if (shape.identifiers[1] != 0) light_bounds.direction = -light_bounds.direction;
                            light_bounds.cos_theta_o = 1.0f;
                        } else {
                            math::Float3 local_minimum{};
                            math::Float3 local_maximum{};
                            if (geometry == 1) {
                                local_minimum = {-shape.geometry_parameters[0], -shape.geometry_parameters[0], shape.geometry_parameters[1]};
                                local_maximum = {shape.geometry_parameters[0], shape.geometry_parameters[0], shape.geometry_parameters[2]};
                            } else if (geometry == 2) {
                                local_minimum = {-shape.geometry_parameters[1], -shape.geometry_parameters[1], shape.geometry_parameters[0]};
                                local_maximum = {shape.geometry_parameters[1], shape.geometry_parameters[1], shape.geometry_parameters[0]};
                            } else if (geometry == 3) {
                                local_minimum = {-shape.geometry_parameters[0], -shape.geometry_parameters[0], shape.geometry_parameters[1]};
                                local_maximum = {shape.geometry_parameters[0], shape.geometry_parameters[0], shape.geometry_parameters[2]};
                            } else if (geometry == 4) {
                                local_minimum = {shape.position_0_and_area[0], shape.position_0_and_area[1], shape.position_0_and_area[2]};
                                local_maximum = {shape.position_1[0], shape.position_1[1], shape.position_1[2]};
                            } else {
                                local_minimum = {shape.position_0_and_area[0], shape.position_0_and_area[1], 0.0f};
                                local_maximum = {shape.position_1[0], shape.position_1[1], 0.0f};
                            }
                            for (const float x : {local_minimum.x, local_maximum.x})
                                for (const float y : {local_minimum.y, local_maximum.y})
                                    for (const float z : {local_minimum.z, local_maximum.z}) include(light_bounds.minimum, light_bounds.maximum, {math::Float3{shape.transform_row_0[0], shape.transform_row_0[1], shape.transform_row_0[2]}.dot({x, y, z}) + shape.transform_row_0[3], math::Float3{shape.transform_row_1[0], shape.transform_row_1[1], shape.transform_row_1[2]}.dot({x, y, z}) + shape.transform_row_1[3], math::Float3{shape.transform_row_2[0], shape.transform_row_2[1], shape.transform_row_2[2]}.dot({x, y, z}) + shape.transform_row_2[3]});
                            if (geometry == 2 || geometry == 5) {
                                const math::Float3 x_axis{shape.transform_row_0[0], shape.transform_row_1[0], shape.transform_row_2[0]};
                                const math::Float3 y_axis{shape.transform_row_0[1], shape.transform_row_1[1], shape.transform_row_2[1]};
                                light_bounds.direction = x_axis.cross(y_axis).normalized();
                                if (shape.identifiers[1] != 0) light_bounds.direction = -light_bounds.direction;
                                light_bounds.cos_theta_o = 1.0f;
                            }
                        }
                        light_bounds.two_sided = (light.identifiers[1] & 8u) != 0;
                    }
                    bounded_lights.emplace_back(light_index, light_bounds);
                }
                light_bvh_bit_trails.assign(lights.size(), invalid_path_index);
                if (!bounded_lights.empty()) {
                    const auto surface_area = [](const LightBounds& bounds) {
                        const math::Float3 diagonal = bounds.maximum - bounds.minimum;
                        return 2.0f * (diagonal.x * diagonal.y + diagonal.x * diagonal.z + diagonal.y * diagonal.z);
                    };
                    const auto cost = [&](const LightBounds& light, const LightBounds& bounds, const std::uint32_t dimension) {
                        if (light.phi == 0.0f) return 0.0f;
                        const float theta_o         = std::acos(std::clamp(light.cos_theta_o, -1.0f, 1.0f));
                        const float theta_e         = std::acos(std::clamp(light.cos_theta_e, -1.0f, 1.0f));
                        const float theta_w         = std::min(theta_o + theta_e, path_pi);
                        const float sin_theta_o     = std::sqrt(std::max(0.0f, 1.0f - light.cos_theta_o * light.cos_theta_o));
                        const float measure         = 2.0f * path_pi * (1.0f - light.cos_theta_o) + path_pi / 2.0f * (2.0f * theta_w * sin_theta_o - std::cos(theta_o - 2.0f * theta_w) - 2.0f * theta_o * sin_theta_o + light.cos_theta_o);
                        const math::Float3 diagonal = bounds.maximum - bounds.minimum;
                        const float components[3]{diagonal.x, diagonal.y, diagonal.z};
                        const float ratio = std::max({diagonal.x, diagonal.y, diagonal.z}) / components[dimension];
                        return light.phi * measure * ratio * surface_area(light);
                    };
                    std::function<std::pair<std::uint32_t, LightBounds>(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)> build;
                    build = [&](const std::uint32_t start, const std::uint32_t end, const std::uint32_t bit_trail, const std::uint32_t depth) -> std::pair<std::uint32_t, LightBounds> {
                        if (end - start == 1) {
                            const std::uint32_t node_index  = static_cast<std::uint32_t>(light_bvh_nodes.size());
                            const std::uint32_t light_index = bounded_lights[start].first;
                            const LightBounds& light_bounds = bounded_lights[start].second;
                            light_bvh_nodes.push_back(pathtracer::PathLightBvhNode{{light_bounds.minimum.x, light_bounds.minimum.y, light_bounds.minimum.z, light_bounds.phi}, {light_bounds.maximum.x, light_bounds.maximum.y, light_bounds.maximum.z, light_bounds.cos_theta_o}, {light_bounds.direction.x, light_bounds.direction.y, light_bounds.direction.z, light_bounds.cos_theta_e}, {light_index, 1u, light_bounds.two_sided ? 1u : 0u, 0u}});
                            light_bvh_bit_trails[light_index] = bit_trail;
                            return {node_index, light_bounds};
                        }
                        LightBounds bounds{};
                        LightBounds centroid_bounds{};
                        bounds.minimum          = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                        bounds.maximum          = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
                        centroid_bounds.minimum = bounds.minimum;
                        centroid_bounds.maximum = bounds.maximum;
                        for (std::uint32_t index = start; index != end; ++index) {
                            bounds                      = union_bounds(bounds, bounded_lights[index].second);
                            const math::Float3 centroid = (bounded_lights[index].second.minimum + bounded_lights[index].second.maximum) * 0.5f;
                            include(centroid_bounds.minimum, centroid_bounds.maximum, centroid);
                        }
                        constexpr std::uint32_t bucket_count = 12;
                        float minimum_cost                   = std::numeric_limits<float>::infinity();
                        int split_bucket                     = -1;
                        int split_dimension                  = -1;
                        for (std::uint32_t dimension = 0; dimension != 3; ++dimension) {
                            const float minimum[3]{centroid_bounds.minimum.x, centroid_bounds.minimum.y, centroid_bounds.minimum.z};
                            const float maximum[3]{centroid_bounds.maximum.x, centroid_bounds.maximum.y, centroid_bounds.maximum.z};
                            if (maximum[dimension] == minimum[dimension]) continue;
                            std::array<LightBounds, bucket_count> buckets{};
                            for (std::uint32_t index = start; index != end; ++index) {
                                const LightBounds& light_bounds = bounded_lights[index].second;
                                const math::Float3 centroid     = (light_bounds.minimum + light_bounds.maximum) * 0.5f;
                                const float values[3]{centroid.x, centroid.y, centroid.z};
                                const std::uint32_t bucket = std::min(static_cast<std::uint32_t>(bucket_count * (values[dimension] - minimum[dimension]) / (maximum[dimension] - minimum[dimension])), bucket_count - 1u);
                                buckets[bucket]            = union_bounds(buckets[bucket], light_bounds);
                            }
                            for (std::uint32_t split = 0; split + 1u < bucket_count; ++split) {
                                LightBounds first{};
                                LightBounds second{};
                                for (std::uint32_t bucket = 0; bucket <= split; ++bucket) first = union_bounds(first, buckets[bucket]);
                                for (std::uint32_t bucket = split + 1u; bucket != bucket_count; ++bucket) second = union_bounds(second, buckets[bucket]);
                                const float value = cost(first, bounds, dimension) + cost(second, bounds, dimension);
                                if (split != 0 && value > 0.0f && value < minimum_cost) {
                                    minimum_cost    = value;
                                    split_bucket    = static_cast<int>(split);
                                    split_dimension = static_cast<int>(dimension);
                                }
                            }
                        }
                        std::uint32_t middle = (start + end) / 2u;
                        if (split_dimension != -1) {
                            const float minimum[3]{centroid_bounds.minimum.x, centroid_bounds.minimum.y, centroid_bounds.minimum.z};
                            const float maximum[3]{centroid_bounds.maximum.x, centroid_bounds.maximum.y, centroid_bounds.maximum.z};
                            const auto partition = std::partition(bounded_lights.begin() + start, bounded_lights.begin() + end, [&](const auto& entry) {
                                const math::Float3 centroid = (entry.second.minimum + entry.second.maximum) * 0.5f;
                                const float values[3]{centroid.x, centroid.y, centroid.z};
                                const std::uint32_t bucket = std::min(static_cast<std::uint32_t>(bucket_count * (values[split_dimension] - minimum[split_dimension]) / (maximum[split_dimension] - minimum[split_dimension])), bucket_count - 1u);
                                return bucket <= static_cast<std::uint32_t>(split_bucket);
                            });
                            middle               = static_cast<std::uint32_t>(partition - bounded_lights.begin());
                            if (middle == start || middle == end) middle = (start + end) / 2u;
                        }
                        const std::uint32_t node_index = static_cast<std::uint32_t>(light_bvh_nodes.size());
                        light_bvh_nodes.emplace_back();
                        const auto first            = build(start, middle, bit_trail, depth + 1u);
                        const auto second           = build(middle, end, bit_trail | (1u << depth), depth + 1u);
                        const LightBounds combined  = union_bounds(first.second, second.second);
                        light_bvh_nodes[node_index] = pathtracer::PathLightBvhNode{{combined.minimum.x, combined.minimum.y, combined.minimum.z, combined.phi}, {combined.maximum.x, combined.maximum.y, combined.maximum.z, combined.cos_theta_o}, {combined.direction.x, combined.direction.y, combined.direction.z, combined.cos_theta_e}, {second.first, 0u, combined.two_sided ? 1u : 0u, 0u}};
                        return {node_index, combined};
                    };
                    build(0, static_cast<std::uint32_t>(bounded_lights.size()), 0, 0);
                    for (pathtracer::PathLightBvhNode& node : light_bvh_nodes) node.metadata[3] = invalid_path_index;
                    for (std::uint32_t node_index = 0; node_index != light_bvh_nodes.size(); ++node_index) {
                        const pathtracer::PathLightBvhNode& node = light_bvh_nodes[node_index];
                        if (node.metadata[1] != 0) continue;
                        light_bvh_nodes[node_index + 1u].metadata[3]  = node_index;
                        light_bvh_nodes[node.metadata[0]].metadata[3] = node_index;
                    }
                }
                if (light_bvh_nodes.empty()) light_bvh_nodes.emplace_back();
                light_bvh_node_count = bounded_lights.empty() ? 0u : static_cast<std::uint32_t>(light_bvh_nodes.size());
            }
        } else
            lights.emplace_back();
        if (light_shapes.empty()) light_shapes.emplace_back();
        if (light_distributions.empty()) light_distributions.emplace_back();
        if (light_distribution_data.empty()) light_distribution_data.emplace_back();
        if (portals.empty()) portals.emplace_back();
        if (light_bvh_nodes.empty()) light_bvh_nodes.emplace_back();
        if (light_bvh_bit_trails.empty()) light_bvh_bit_trails.emplace_back();
        if (face_materials.empty()) face_materials.emplace_back();
        if (spectra.empty()) spectra.emplace_back();
        if (piecewise_spectra.empty()) piecewise_spectra.emplace_back();

        if (progress) progress->report(PathTracerPreparationStage::AssemblingScene);
        PathBufferArena arena{};
        PathBufferSlice new_primitives = arena.append(std::span<const pathtracer::PathPrimitive>{primitives});
        std::array<PathBufferSlice, static_cast<std::size_t>(PathMaterialTable::Count)> new_materials{};
        new_materials[static_cast<std::size_t>(PathMaterialTable::Header)]              = arena.append(std::span<const pathtracer::PathMaterialHeader>{material_headers});
        new_materials[static_cast<std::size_t>(PathMaterialTable::Diffuse)]             = arena.append(std::span<const pathtracer::PathDiffuseMaterial>{diffuse_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::DiffuseTransmission)] = arena.append(std::span<const pathtracer::PathDiffuseTransmissionMaterial>{diffuse_transmission_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::Conductor)]           = arena.append(std::span<const pathtracer::PathConductorMaterial>{conductor_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::Dielectric)]          = arena.append(std::span<const pathtracer::PathDielectricMaterial>{dielectric_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::ThinDielectric)]      = arena.append(std::span<const pathtracer::PathThinDielectricMaterial>{thin_dielectric_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::CoatedDiffuse)]       = arena.append(std::span<const pathtracer::PathCoatedDiffuseMaterial>{coated_diffuse_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::CoatedConductor)]     = arena.append(std::span<const pathtracer::PathCoatedConductorMaterial>{coated_conductor_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::Mix)]                 = arena.append(std::span<const pathtracer::PathMixMaterial>{mix_materials});
        new_materials[static_cast<std::size_t>(PathMaterialTable::TextureRequest)]      = arena.append(std::span<const pathtracer::PathMaterialTextureRequest>{material_texture_requests});
        std::array<PathBufferSlice, static_cast<std::size_t>(PathTextureTable::Count)> new_textures{};
        new_textures[static_cast<std::size_t>(PathTextureTable::Header)]       = arena.append(std::span<const pathtracer::PathTextureHeader>{texture_headers});
        new_textures[static_cast<std::size_t>(PathTextureTable::Mapping)]      = arena.append(std::span<const pathtracer::PathTextureMapping>{texture_mappings});
        new_textures[static_cast<std::size_t>(PathTextureTable::Constant)]     = arena.append(std::span<const pathtracer::PathConstantTexture>{constant_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::Image)]        = arena.append(std::span<const pathtracer::PathImageTexture>{image_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::Checkerboard)] = arena.append(std::span<const pathtracer::PathCheckerboardTexture>{checkerboard_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::Scale)]        = arena.append(std::span<const pathtracer::PathScaleTexture>{scale_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::Mix)]          = arena.append(std::span<const pathtracer::PathMixTexture>{mix_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::DirectionMix)] = arena.append(std::span<const pathtracer::PathDirectionMixTexture>{direction_mix_textures});
        new_textures[static_cast<std::size_t>(PathTextureTable::Bilerp)]       = arena.append(std::span<const pathtracer::PathBilerpTexture>{bilerp_textures});
        PathBufferSlice new_lights                                             = arena.append(std::span<const pathtracer::PathLight>{lights});
        PathBufferSlice new_light_shapes                                       = arena.append(std::span<const pathtracer::PathLightShape>{light_shapes});
        PathBufferSlice new_light_distributions                                = arena.append(std::span<const pathtracer::PathLightDistribution>{light_distributions});
        PathBufferSlice new_light_distribution_data                            = arena.append(std::span<const float>{light_distribution_data});
        PathBufferSlice new_portals                                            = arena.append(std::span<const pathtracer::PathPortal>{portals});
        PathBufferSlice new_light_bvh_nodes                                    = arena.append(std::span<const pathtracer::PathLightBvhNode>{light_bvh_nodes});
        PathBufferSlice new_light_bvh_bit_trails                               = arena.append(std::span<const std::uint32_t>{light_bvh_bit_trails});
        PathBufferSlice new_face_materials                                     = arena.append(std::span<const std::uint32_t>{face_materials});
        PathBufferSlice new_media                                              = arena.append(std::span<const pathtracer::PathMedium>{media});
        PathBufferSlice new_volumes                                            = arena.append(std::span<const pathtracer::PathVolume>{volumes});
        PathBufferSlice new_spectra                                            = arena.append(std::span<const CompiledSpectrum>{spectra});
        PathBufferSlice new_piecewise_spectra                                  = arena.append(std::span<const math::Float2>{piecewise_spectra});

        std::unique_ptr<PreparedPathScene> prepared = std::make_unique<PreparedPathScene>();
        prepared->arena                             = std::move(arena.data);
        prepared->primitives                        = std::move(new_primitives);
        prepared->materials                         = std::move(new_materials);
        prepared->textures                          = std::move(new_textures);
        prepared->light_table                       = std::move(new_lights);
        prepared->light_shapes                      = std::move(new_light_shapes);
        prepared->light_distribution                = std::move(new_light_distributions);
        prepared->light_distribution_data           = std::move(new_light_distribution_data);
        prepared->portals                           = std::move(new_portals);
        prepared->light_bvh_nodes                   = std::move(new_light_bvh_nodes);
        prepared->light_bvh_bit_trails              = std::move(new_light_bvh_bit_trails);
        prepared->face_materials                    = std::move(new_face_materials);
        prepared->media                             = std::move(new_media);
        prepared->volumes                           = std::move(new_volumes);
        prepared->spectra                           = std::move(new_spectra);
        prepared->piecewise_spectra                 = std::move(new_piecewise_spectra);
        prepared->compiled_volumes                  = std::move(volumes);
        prepared->volume_resources                  = std::move(prepared_volume_resources);
        prepared->texture_count                     = static_cast<std::uint32_t>(texture_count);
        prepared->texture_stack_size                = maximum_texture_stack_size;
        prepared->material_texture_value_count      = maximum_material_texture_requests;
        prepared->light_count                       = light_count;
        prepared->light_bvh_node_count              = light_bvh_node_count;
        prepared->light_bvh_infinite_count          = light_bvh_infinite_count;
        prepared->camera_medium_index               = camera_medium_index;
        prepared->light_sampler                     = scene.transport.light_sampler;
        prepared->bounds                            = {bounds.minimum, bounds.maximum};
        prepared->instance_transforms.reserve(scene.resources.instances.size());
        for (const scene::Instance& instance : scene.resources.instances) prepared->instance_transforms.push_back(instance.transform);
        prepared->revision               = scene.revision;
        prepared->gpu_structure_revision = gpu.structure_revision;
        prepared->dynamic_area_lights    = std::move(dynamic_area_lights);
        return prepared;
    }

} // namespace spectra
