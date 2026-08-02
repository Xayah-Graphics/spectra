module spectra.pathtracer;

import spectra.pathtracer.abi;
import spectra.scene;
import std;

namespace spectra::pathtracer {
    namespace {
        constexpr std::uint32_t table_resolution    = 64;
        constexpr std::size_t coefficient_count     = 3ull * table_resolution * table_resolution * table_resolution * 3ull;
        constexpr std::uint64_t expected_table_size = 16ull + table_resolution * sizeof(float) + coefficient_count * sizeof(float);
        constexpr std::size_t table_word_count      = expected_table_size / sizeof(std::uint32_t);

        [[nodiscard]] float lerp(const float value, const float first, const float second) noexcept {
            return std::fma(value, second - first, first);
        }

        [[nodiscard]] std::vector<std::uint32_t> load_tables(const std::filesystem::path& directory) {
            constexpr std::array names{"srgb.rgb2spec", "rec2020.rgb2spec", "aces2065_1.rgb2spec"};
            std::vector<std::uint32_t> data(table_word_count * names.size());
            for (std::size_t index = 0; index != names.size(); ++index) {
                const std::filesystem::path path = directory / names[index];
                std::ifstream stream{path, std::ios::binary};
                std::error_code error{};
                if (!stream || std::filesystem::file_size(path, error) != expected_table_size || error) throw std::runtime_error(std::format("Invalid RGB-to-spectrum table size: {}", path.string()));
                stream.read(reinterpret_cast<char*>(data.data() + table_word_count * index), expected_table_size);
                if (!stream) throw std::runtime_error(std::format("Failed to read RGB-to-spectrum table: {}", path.string()));
            }
            return data;
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

    RgbSigmoidPolynomial RgbToSpectrumTable::polynomial(const scene::Float3 rgb) const noexcept {
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

    SpectrumTables::SpectrumTables(const std::span<const std::uint32_t> data) : srgb(data.first(table_word_count)), rec2020(data.subspan(table_word_count, table_word_count)), aces2065_1(data.last(table_word_count)) {
        if (data.size() != table_word_count * 3) throw std::runtime_error("Invalid RGB-to-spectrum table collection");
    }

    const RgbToSpectrumTable& SpectrumTables::rgb(const scene::SpectrumColorSpace color_space) const noexcept {
        switch (color_space) {
        case scene::SpectrumColorSpace::Srgb: return this->srgb;
        case scene::SpectrumColorSpace::Rec2020: return this->rec2020;
        case scene::SpectrumColorSpace::Aces2065_1: return this->aces2065_1;
        }
        std::unreachable();
    }

    std::vector<std::uint32_t> load_spectrum_table_data(const std::filesystem::path& directory) {
        return load_tables(directory);
    }

    CompiledSpectrum compile_spectrum(const scene::SpectrumParameter& spectrum, const SpectrumTables& tables, std::vector<scene::Float2>& piecewise_samples) {
        if (spectrum.texture.value != 0) throw std::runtime_error("A textured Spectrum must be compiled by the Texture Program");
        switch (spectrum.encoding) {
        case scene::SpectrumEncoding::RgbAlbedo:
        case scene::SpectrumEncoding::RgbUnbounded:
        case scene::SpectrumEncoding::RgbIlluminant:
            {
                scene::Float3 rgb = spectrum.value;
                float scale       = 1.0f;
                if (spectrum.encoding != scene::SpectrumEncoding::RgbAlbedo) {
                    const float maximum = std::max(rgb.x, std::max(rgb.y, rgb.z));
                    scale               = 2.0f * maximum;
                    if (scale != 0.0f) {
                        rgb.x /= scale;
                        rgb.y /= scale;
                        rgb.z /= scale;
                    }
                }
                const RgbSigmoidPolynomial polynomial = tables.rgb(spectrum.color_space).polynomial(rgb);
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

    namespace {
        enum class PathMaterialKind : std::uint32_t {
            Interface,
            Diffuse,
            DiffuseTransmission,
            Conductor,
            Dielectric,
            ThinDielectric,
            CoatedDiffuse,
            CoatedConductor,
            Mix,
        };

        enum class PathTextureKind : std::uint32_t {
            Constant,
            Image,
            Checkerboard,
            Scale,
            Mix,
            DirectionMix,
            Bilerp,
        };

        enum class PathLightKind : std::uint32_t {
            Point,
            Spot,
            Distant,
            DiffuseArea,
            Infinite,
            PortalInfinite,
        };

        enum class PathMediumKind : std::uint32_t {
            Homogeneous,
            Volume,
        };

        enum class PathVolumeKind : std::uint32_t {
            DensityGrid,
            RgbGrid,
            NanoVdb,
            ProceduralCloud,
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

        [[nodiscard]] float evaluate_compiled_spectrum(const CompiledSpectrum& spectrum, const std::span<const scene::Float2> piecewise_spectra, const std::span<const float> cie_spectra, const float wavelength) noexcept {
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
                    const scene::Float2 first  = piecewise_spectra[offset + upper - 1u];
                    const scene::Float2 second = piecewise_spectra[offset + upper];
                    value                      = std::lerp(first.y, second.y, (wavelength - first.x) / (second.x - first.x));
                    break;
                }
            }
            return value;
        }

        [[nodiscard]] float spectrum_power_sample(const CompiledSpectrum& spectrum, const std::span<const scene::Float2> piecewise_spectra, const std::span<const float> cie_spectra) noexcept {
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

        [[nodiscard]] float spectrum_maximum(const CompiledSpectrum& spectrum, const std::span<const scene::Float2> piecewise_spectra, const std::span<const float> cie_spectra) noexcept {
            float result{};
            for (std::uint32_t wavelength = 360; wavelength <= 830; ++wavelength) result = std::max(result, evaluate_compiled_spectrum(spectrum, piecewise_spectra, cie_spectra, static_cast<float>(wavelength)));
            return result;
        }

        [[nodiscard]] std::uint32_t compile_light_distribution(const std::span<const float> weights, const std::uint32_t width, const std::uint32_t height, std::vector<PathLightDistribution>& distributions, std::vector<float>& data) {
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
            distributions.push_back(PathLightDistribution{{width, height, function_offset, conditional_offset}, {marginal_offset, summed_area_offset, 0, 0}, {integral, std::accumulate(weights.begin(), weights.end(), 0.0f) / static_cast<float>(weights.size()), 0.0f, 0.0f}});
            return index;
        }

        [[nodiscard]] scene::Float2 equal_area_sphere_to_square(const scene::Float3 direction) noexcept {
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

        [[nodiscard]] scene::Float4 image_texel_octahedral(const scene::ImageTexture& image, int x, int y) noexcept {
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

        [[nodiscard]] scene::Float4 image_bilerp_octahedral(const scene::ImageTexture& image, const scene::Float2 uv) noexcept {
            const float x  = uv.x * static_cast<float>(image.width) - 0.5f;
            const float y  = uv.y * static_cast<float>(image.height) - 0.5f;
            const int x0   = static_cast<int>(std::floor(x));
            const int y0   = static_cast<int>(std::floor(y));
            const float dx = x - static_cast<float>(x0);
            const float dy = y - static_cast<float>(y0);
            const scene::Float4 values[4]{image_texel_octahedral(image, x0, y0), image_texel_octahedral(image, x0 + 1, y0), image_texel_octahedral(image, x0, y0 + 1), image_texel_octahedral(image, x0 + 1, y0 + 1)};
            return {std::lerp(std::lerp(values[0].x, values[1].x, dx), std::lerp(values[2].x, values[3].x, dx), dy), std::lerp(std::lerp(values[0].y, values[1].y, dx), std::lerp(values[2].y, values[3].y, dx), dy), std::lerp(std::lerp(values[0].z, values[1].z, dx), std::lerp(values[2].z, values[3].z, dx), dy), 1.0f};
        }


        static_assert(sizeof(CompiledSpectrum) == 32);
        [[nodiscard]] GpuBuffer create_storage_buffer(Spectra& runtime, const vk::DeviceSize size, const vk::BufferUsageFlags additional_usage = {}) {
            return runtime.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | additional_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        }

        template <class Element>
        [[nodiscard]] GpuBuffer upload_path_buffer(Spectra& runtime, const std::span<const Element> elements, const vk::raii::CommandBuffer* command_buffer) {
            GpuBuffer destination = create_storage_buffer(runtime, std::max(elements.size_bytes(), sizeof(Element)), vk::BufferUsageFlagBits::eTransferDst);
            if (elements.empty()) return destination;
            if (command_buffer) {
                const GpuUploadSlice upload = runtime.stage_upload(std::as_bytes(elements));
                command_buffer->copyBuffer(upload.buffer, *destination.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
                const vk::BufferMemoryBarrier2 dependency{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    *destination.buffer,
                    0,
                    destination.size,
                };
                command_buffer->pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &dependency});
                return destination;
            }
            GpuBuffer staging = runtime.create_buffer(elements.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, elements.data(), elements.size_bytes());
            runtime.immediate([&staging, &destination](const vk::raii::CommandBuffer& command_buffer) {
                command_buffer.copyBuffer(*staging.buffer, *destination.buffer, vk::BufferCopy{0, 0, staging.size});
                const vk::BufferMemoryBarrier2 dependency{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    *destination.buffer,
                    0,
                    destination.size,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &dependency});
            });
            return destination;
        }

        template <class Sample>
        [[nodiscard]] std::vector<float> build_grid_majorant(const scene::UInt3 resolution, Sample&& sample) {
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

        [[nodiscard]] float rgb_spectrum_maximum(const scene::Float3 rgb, const scene::SpectrumColorSpace color_space, const SpectrumTables& tables) {
            const float maximum = std::max(rgb.x, std::max(rgb.y, rgb.z));
            if (maximum == 0.0f) return 0.0f;
            const float scale                     = 2.0f * maximum;
            const RgbSigmoidPolynomial polynomial = tables.rgb(color_space)
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

        [[nodiscard]] std::vector<float> build_rgb_majorant(const scene::RgbGridVolume& volume, const SpectrumTables& tables) {
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

        [[nodiscard]] std::array<std::uint32_t, 4> compile_path_spectrum(const scene::SpectrumParameter& parameter, const std::string_view name, const SpectrumTables& tables, std::vector<CompiledSpectrum>& spectra, std::vector<scene::Float2>& piecewise_spectra, const std::function<std::uint32_t(scene::TextureId)>& texture_handle) {
            const std::uint32_t spectrum                  = static_cast<std::uint32_t>(spectra.size());
            scene::SpectrumParameter untextured_parameter = parameter;
            untextured_parameter.texture                  = {};
            spectra.push_back(compile_spectrum(untextured_parameter, tables, piecewise_spectra));
            return {spectrum, parameter.texture.value == 0 ? std::numeric_limits<std::uint32_t>::max() : texture_handle(parameter.texture), 0, 0};
        }

        [[nodiscard]] CompiledPathFloat compile_path_float(const scene::FloatParameter& parameter, const std::string_view name, const std::function<std::uint32_t(scene::TextureId)>& texture_handle) {
            return {parameter.value, parameter.texture.value == 0 ? std::numeric_limits<std::uint32_t>::max() : texture_handle(parameter.texture)};
        }

        [[nodiscard]] std::vector<std::byte> load_cie_spectra(const std::filesystem::path& path) {
            constexpr std::uint64_t expected_size = 16ull + 5ull * 471ull * sizeof(float);
            std::ifstream stream{path, std::ios::binary};
            std::error_code error{};
            if (!stream || std::filesystem::file_size(path, error) != expected_size || error) throw std::runtime_error(std::format("Invalid CIE spectral table size: {}", path.string()));
            std::vector<std::byte> bytes(expected_size);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            const std::array<char, 8> magic{'S', 'P', 'S', 'P', 'E', 'C', '0', '1'};
            std::uint32_t version{};
            std::uint32_t sample_count{};
            std::memcpy(&version, bytes.data() + 8, sizeof(version));
            std::memcpy(&sample_count, bytes.data() + 12, sizeof(sample_count));
            if (!stream || std::memcmp(bytes.data(), magic.data(), magic.size()) != 0 || version != 1 || sample_count != 471) throw std::runtime_error(std::format("Invalid CIE spectral table header: {}", path.string()));
            return bytes;
        }

        [[nodiscard]] std::vector<std::byte> load_sampling_tables(const std::filesystem::path& path) {
            constexpr std::uint64_t expected_size = 6'005'504;
            std::ifstream stream{path, std::ios::binary};
            std::error_code error{};
            if (!stream || std::filesystem::file_size(path, error) != expected_size || error) throw std::runtime_error(std::format("Invalid sampling table size: {}", path.string()));
            std::vector<std::byte> bytes(expected_size);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            const std::array<std::uint32_t, 16> expected_header{0x41535053, 0x3130504d, 1, 32, 32, 53'248, 53'280, 1'300, 55'880, 1'352, 58'584, 1'000, 59'584, 655'360, 714'944, 786'432};
            if (!stream || std::memcmp(bytes.data(), expected_header.data(), sizeof(expected_header)) != 0) throw std::runtime_error(std::format("Invalid sampling table header: {}", path.string()));
            return bytes;
        }
    } // namespace

    PathScene::PathScene(Spectra& runtime, const render::GpuScene& gpu_scene, const scene::SceneView scene, const std::filesystem::path& shader_directory)
        : runtime(&runtime), gpu_scene(&gpu_scene), spectrum_table_data(load_spectrum_table_data(SPECTRA_SPECTRAL_ASSET_DIRECTORY)), primitives(runtime.allocate_resource_descriptor()), light_table(runtime.allocate_resource_descriptor()), light_shapes(runtime.allocate_resource_descriptor()), light_distribution(runtime.allocate_resource_descriptor()), light_distribution_data(runtime.allocate_resource_descriptor()), portals(runtime.allocate_resource_descriptor()), light_bvh_nodes(runtime.allocate_resource_descriptor()), light_bvh_bit_trails(runtime.allocate_resource_descriptor()), face_materials(runtime.allocate_resource_descriptor()), media(runtime.allocate_resource_descriptor()), volumes(runtime.allocate_resource_descriptor()), volume_zero(runtime.allocate_resource_descriptor()), spectra(runtime.allocate_resource_descriptor()), piecewise_spectra(runtime.allocate_resource_descriptor()), cie_spectra(runtime.allocate_resource_descriptor()), rgb_to_spectrum(runtime.allocate_resource_descriptor()), bindings_table(runtime.allocate_resource_descriptor()), scene_camera(scene.camera),
          transport_settings(scene.transport) {
        const std::vector<std::uint32_t> majorant_code = render::load_spirv(shader_directory / "path_volume_majorant.spv");
        this->volume_majorant_shader                   = vk::raii::ShaderEXT{runtime.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, majorant_code.size() * sizeof(std::uint32_t), majorant_code.data(), "build_majorant"}};
        this->filter.distribution.descriptor           = runtime.allocate_resource_descriptor();
        this->filter.sensor_response.descriptor        = runtime.allocate_resource_descriptor();
        this->sampler.tables.descriptor                = runtime.allocate_resource_descriptor();
        this->sampler.pixel_samples.descriptor         = runtime.allocate_resource_descriptor();
        for (GpuBufferBinding& binding : this->materials) binding.descriptor = runtime.allocate_resource_descriptor();
        for (GpuBufferBinding& binding : this->textures) binding.descriptor = runtime.allocate_resource_descriptor();
        const std::vector<std::byte> cie = load_cie_spectra(std::filesystem::path{SPECTRA_SPECTRAL_ASSET_DIRECTORY} / "cie1931.spectrum");
        this->cie_samples.resize(5u * 471u);
        std::memcpy(this->cie_samples.data(), cie.data() + 16, this->cie_samples.size() * sizeof(float));
        this->cie_spectra.buffer = upload_path_buffer(runtime, std::span<const std::byte>{cie}, nullptr);
        runtime.write_buffer(this->cie_spectra.descriptor, vk::DescriptorType::eStorageBuffer, this->cie_spectra.buffer);
        constexpr std::array<std::uint32_t, 4> volume_zero{};
        this->volume_zero.buffer = upload_path_buffer(runtime, std::span<const std::uint32_t>{volume_zero}, nullptr);
        runtime.write_buffer(this->volume_zero.descriptor, vk::DescriptorType::eStorageBuffer, this->volume_zero.buffer);
        this->rgb_to_spectrum.buffer = upload_path_buffer(runtime, std::as_bytes(std::span<const std::uint32_t>{this->spectrum_table_data}), nullptr);
        runtime.write_buffer(this->rgb_to_spectrum.descriptor, vk::DescriptorType::eStorageBuffer, this->rgb_to_spectrum.buffer);
        const std::vector<std::byte> sampling_tables = load_sampling_tables(std::filesystem::path{SPECTRA_SPECTRAL_ASSET_DIRECTORY} / "sampling.tables");
        this->sampler.table_data.resize(sampling_tables.size() / sizeof(std::uint32_t));
        std::memcpy(this->sampler.table_data.data(), sampling_tables.data(), sampling_tables.size());
        this->sampler.tables.buffer = upload_path_buffer(runtime, std::span<const std::uint32_t>{this->sampler.table_data}, nullptr);
        runtime.write_buffer(this->sampler.tables.descriptor, vk::DescriptorType::eStorageBuffer, this->sampler.tables.buffer);
        this->compile_sampler(scene.sampler, nullptr);
        this->compile_filter(scene.film, nullptr);
        this->compile(scene, nullptr);
        this->compiled_revision = scene.revision;
    }

    PathScene::~PathScene() {
        this->runtime->release_resource_descriptor(this->primitives.descriptor);
        for (const GpuBufferBinding& binding : this->materials) this->runtime->release_resource_descriptor(binding.descriptor);
        for (const GpuBufferBinding& binding : this->textures) this->runtime->release_resource_descriptor(binding.descriptor);
        this->runtime->release_resource_descriptor(this->light_table.descriptor);
        this->runtime->release_resource_descriptor(this->light_shapes.descriptor);
        this->runtime->release_resource_descriptor(this->light_distribution.descriptor);
        this->runtime->release_resource_descriptor(this->light_distribution_data.descriptor);
        this->runtime->release_resource_descriptor(this->portals.descriptor);
        this->runtime->release_resource_descriptor(this->light_bvh_nodes.descriptor);
        this->runtime->release_resource_descriptor(this->light_bvh_bit_trails.descriptor);
        this->runtime->release_resource_descriptor(this->face_materials.descriptor);
        this->runtime->release_resource_descriptor(this->media.descriptor);
        this->runtime->release_resource_descriptor(this->volumes.descriptor);
        this->runtime->release_resource_descriptor(this->volume_zero.descriptor);
        for (const VolumeGpuData& volume : this->volume_gpu_data) this->runtime->release_resource_descriptor(volume.majorant.descriptor);
        this->runtime->release_resource_descriptor(this->spectra.descriptor);
        this->runtime->release_resource_descriptor(this->piecewise_spectra.descriptor);
        this->runtime->release_resource_descriptor(this->cie_spectra.descriptor);
        this->runtime->release_resource_descriptor(this->rgb_to_spectrum.descriptor);
        this->runtime->release_resource_descriptor(this->bindings_table.descriptor);
        this->runtime->release_resource_descriptor(this->filter.distribution.descriptor);
        this->runtime->release_resource_descriptor(this->filter.sensor_response.descriptor);
        this->runtime->release_resource_descriptor(this->sampler.tables.descriptor);
        this->runtime->release_resource_descriptor(this->sampler.pixel_samples.descriptor);
    }

    void PathScene::compile_filter(const scene::Film& film, const vk::raii::CommandBuffer* command_buffer) {
        const scene::Filter& filter = film.filter;
        std::array<std::uint32_t, 2> resolution{};
        std::vector<float> distribution{};
        float absolute_integral{};
        if (filter.kind != scene::FilterKind::Box && filter.kind != scene::FilterKind::Triangle) {
            resolution                             = {static_cast<std::uint32_t>(32.0f * filter.radius.x), static_cast<std::uint32_t>(32.0f * filter.radius.y)};
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
            for (std::uint32_t y = 1; y != resolution[1] + 1; ++y) distribution[marginal_offset + y] /= absolute_integral;
        }
        if (distribution.empty()) distribution.push_back(0.0f);
        GpuBuffer new_distribution                   = upload_path_buffer(*this->runtime, std::span<const float>{distribution}, command_buffer);
        const std::span<const float> sensor_response = film.sensor_response.empty() ? std::span<const float>{this->cie_samples}.first(3u * 471u) : std::span<const float>{film.sensor_response};
        if (sensor_response.size() != 3u * 471u) throw std::runtime_error("Film sensor response must contain three 471-wavelength channels");
        GpuBuffer new_sensor_response = upload_path_buffer(*this->runtime, sensor_response, command_buffer);
        if (*this->filter.distribution.buffer.buffer) {
            const DescriptorHandle distribution_descriptor = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle sensor_descriptor       = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(distribution_descriptor, vk::DescriptorType::eStorageBuffer, new_distribution);
            this->runtime->write_buffer(sensor_descriptor, vk::DescriptorType::eStorageBuffer, new_sensor_response);
            this->runtime->release_resource_descriptor(this->filter.distribution.descriptor);
            this->runtime->release_resource_descriptor(this->filter.sensor_response.descriptor);
            this->runtime->defer([distribution_buffer = std::move(this->filter.distribution.buffer), sensor_buffer = std::move(this->filter.sensor_response.buffer)]() mutable {});
            this->filter.distribution.descriptor    = distribution_descriptor;
            this->filter.sensor_response.descriptor = sensor_descriptor;
        } else {
            this->runtime->write_buffer(this->filter.distribution.descriptor, vk::DescriptorType::eStorageBuffer, new_distribution);
            this->runtime->write_buffer(this->filter.sensor_response.descriptor, vk::DescriptorType::eStorageBuffer, new_sensor_response);
        }
        this->filter.description            = film;
        this->filter.distribution.buffer    = std::move(new_distribution);
        this->filter.sensor_response.buffer = std::move(new_sensor_response);
        this->filter.resolution             = resolution;
        this->filter.absolute_integral      = absolute_integral;
    }

    void PathScene::compile_sampler(const scene::Sampler& sampler, const vk::raii::CommandBuffer* command_buffer) {
        if (sampler.samples_per_pixel == 0) throw std::runtime_error("Sampler samples_per_pixel must be positive");
        if (sampler.kind == scene::SamplerKind::Stratified && (sampler.x_strata == 0 || sampler.y_strata == 0 || sampler.x_strata * sampler.y_strata != sampler.samples_per_pixel)) throw std::runtime_error("Stratified sampler requires samples_per_pixel == x_strata * y_strata");
        if (sampler.kind == scene::SamplerKind::Halton && sampler.randomization == scene::SamplerRandomization::FastOwen) throw std::runtime_error("Halton sampler does not support Fast Owen randomization");
        if (sampler.kind == scene::SamplerKind::ZSobol && !std::has_single_bit(sampler.samples_per_pixel)) throw std::runtime_error("ZSobol sampler requires a power-of-two sample count");
        if (sampler.kind == scene::SamplerKind::Pmj02bn && sampler.samples_per_pixel > 65'536) throw std::runtime_error("PMJ02BN sampler supports at most 65536 samples per pixel");

        std::uint32_t pixel_tile_size{1};
        std::vector<scene::Float2> pixel_samples(1);
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
            const std::uint32_t pmj_offset = this->sampler.table_data[12];
            for (std::uint32_t index = 0; index != 65'536; ++index) {
                const double x                 = static_cast<double>(this->sampler.table_data[pmj_offset + index * 2]) * 0x1p-32 * pixel_tile_size;
                const double y                 = static_cast<double>(this->sampler.table_data[pmj_offset + index * 2 + 1]) * 0x1p-32 * pixel_tile_size;
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

        GpuBuffer new_pixel_samples = upload_path_buffer(*this->runtime, std::span<const scene::Float2>{pixel_samples}, command_buffer);
        if (command_buffer) {
            const DescriptorHandle descriptor = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(descriptor, vk::DescriptorType::eStorageBuffer, new_pixel_samples);
            this->runtime->release_resource_descriptor(this->sampler.pixel_samples.descriptor);
            this->runtime->defer([buffer = std::move(this->sampler.pixel_samples.buffer)]() mutable {});
            this->sampler.pixel_samples.descriptor = descriptor;
        } else
            this->runtime->write_buffer(this->sampler.pixel_samples.descriptor, vk::DescriptorType::eStorageBuffer, new_pixel_samples);
        this->sampler.description          = sampler;
        this->sampler.pixel_samples.buffer = std::move(new_pixel_samples);
        this->sampler.pixel_tile_size      = pixel_tile_size;
    }

    void PathScene::compile(const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer) {
        const SpectrumTables spectrum_tables{this->spectrum_table_data};
        std::vector<CompiledSpectrum> spectra{};
        std::vector<scene::Float2> piecewise_spectra{};
        const std::size_t texture_count     = scene.resources.textures.size();
        const std::uint32_t invalid_texture = std::numeric_limits<std::uint32_t>::max();
        std::vector<scene::TextureId> normal_maps{};
        std::vector<scene::TextureId> bump_maps{};
        for (const scene::MaterialResource& material : scene.resources.materials)
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
        std::vector<PathTextureHeader> texture_headers{};
        std::vector<PathTextureMapping> texture_mappings{};
        std::vector<PathConstantTexture> constant_textures{};
        std::vector<PathImageTexture> image_textures{};
        std::vector<PathCheckerboardTexture> checkerboard_textures{};
        std::vector<PathScaleTexture> scale_textures{};
        std::vector<PathMixTexture> mix_textures{};
        std::vector<PathDirectionMixTexture> direction_mix_textures{};
        std::vector<PathBilerpTexture> bilerp_textures{};
        const auto compile_mapping = [&texture_mappings](const scene::TextureMapping& mapping) {
            PathTextureMapping result{};
            result.transform_row_0 = {1.0f, 0.0f, 0.0f, 0.0f};
            result.transform_row_1 = {0.0f, 1.0f, 0.0f, 0.0f};
            result.transform_row_2 = {0.0f, 0.0f, 1.0f, 0.0f};
            result.transform_row_3 = {0.0f, 0.0f, 0.0f, 1.0f};
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::UvTextureMapping>) {
                        result.metadata[0] = 0;
                        result.parameter_0 = {data.scale.x, data.scale.y, data.offset.x, data.offset.y};
                    } else {
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
            const std::uint32_t index = static_cast<std::uint32_t>(texture_mappings.size());
            texture_mappings.push_back(result);
            return index;
        };
        const auto compile_checkerboard_mapping = [&texture_mappings, &compile_mapping](const scene::CheckerboardMapping& mapping) {
            if (const scene::TextureMapping* two_dimensional = std::get_if<scene::TextureMapping>(&mapping.data)) return compile_mapping(*two_dimensional);
            const scene::TextureMapping3D& three_dimensional = std::get<scene::TextureMapping3D>(mapping.data);
            const std::array<float, 16>& transform           = three_dimensional.texture_from_render.matrix;
            PathTextureMapping result{{4, 0, 0, 0}, {transform[0], transform[1], transform[2], transform[3]}, {transform[4], transform[5], transform[6], transform[7]}, {transform[8], transform[9], transform[10], transform[11]}, {transform[12], transform[13], transform[14], transform[15]}};
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
                        constant_textures.push_back(PathConstantTexture{data.scalar, texture_spectrum(data.spectrum)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageTexture>) {
                        kind                                           = PathTextureKind::Image;
                        local_index                                    = static_cast<std::uint32_t>(image_textures.size());
                        const render::GpuTextureImage& runtime_texture = this->gpu_scene->texture_image(texture, texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat);
                        image_textures.push_back(PathImageTexture{runtime_texture.image_descriptor, runtime_texture.sampler_descriptor, {compile_mapping(data.mapping), static_cast<std::uint32_t>(data.channel), static_cast<std::uint32_t>(data.filter), data.invert ? 1u : 0u}, {data.scale, texture.color_space == scene::TextureColorSpace::Rec2020 ? 1.0f : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? 2.0f : 0.0f, data.maximum_anisotropy, static_cast<float>(std::to_underlying(data.wrap))}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CheckerboardTexture>) {
                        kind        = PathTextureKind::Checkerboard;
                        local_index = static_cast<std::uint32_t>(checkerboard_textures.size());
                        checkerboard_textures.push_back(PathCheckerboardTexture{{texture_handle(data.first), texture_handle(data.second), compile_checkerboard_mapping(data.mapping), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ScaleTexture>) {
                        kind        = PathTextureKind::Scale;
                        local_index = static_cast<std::uint32_t>(scale_textures.size());
                        scale_textures.push_back(PathScaleTexture{{texture_handle(data.first), texture_handle(data.second), 0, 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::MixTexture>) {
                        kind        = PathTextureKind::Mix;
                        local_index = static_cast<std::uint32_t>(mix_textures.size());
                        mix_textures.push_back(PathMixTexture{{texture_handle(data.first), texture_handle(data.second), texture_handle(data.amount), 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DirectionMixTexture>) {
                        kind        = PathTextureKind::DirectionMix;
                        local_index = static_cast<std::uint32_t>(direction_mix_textures.size());
                        direction_mix_textures.push_back(PathDirectionMixTexture{{texture_handle(data.first), texture_handle(data.second), 0, 0}, {data.direction.x, data.direction.y, data.direction.z, 0.0f}});
                    } else {
                        kind        = PathTextureKind::Bilerp;
                        local_index = static_cast<std::uint32_t>(bilerp_textures.size());
                        PathBilerpTexture compiled{};
                        compiled.scalars = data.scalars;
                        for (std::uint32_t corner = 0; corner != 4; ++corner) compiled.spectra[corner] = texture_spectrum(data.spectra[corner]);
                        compiled.data[0] = compile_mapping(data.mapping);
                        bilerp_textures.push_back(compiled);
                    }
                },
                texture.data);
            texture_headers.push_back(PathTextureHeader{static_cast<std::uint32_t>(kind), local_index, static_cast<std::uint32_t>(texture.value_kind), static_cast<std::uint32_t>(texture.spectrum_type), {}, {}});
        }
        const std::uint32_t texture_root_count = static_cast<std::uint32_t>(texture_headers.size());
        std::vector<PathTextureHeader> texture_program{};
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
            PathTextureHeader& root = texture_headers[handle];
            root.program[0]         = texture_root_count + static_cast<std::uint32_t>(texture_program.size());
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
        std::vector<PathMaterialHeader> material_headers{};
        std::vector<PathDiffuseMaterial> diffuse_materials{};
        std::vector<PathDiffuseTransmissionMaterial> diffuse_transmission_materials{};
        std::vector<PathConductorMaterial> conductor_materials{};
        std::vector<PathDielectricMaterial> dielectric_materials{};
        std::vector<PathThinDielectricMaterial> thin_dielectric_materials{};
        std::vector<PathCoatedDiffuseMaterial> coated_diffuse_materials{};
        std::vector<PathCoatedConductorMaterial> coated_conductor_materials{};
        std::vector<PathMixMaterial> mix_materials{};
        material_headers.reserve(scene.resources.materials.size());
        const auto material_index = [&scene](const scene::MaterialId id) {
            const std::vector<scene::MaterialResource>::const_iterator material = std::ranges::find(scene.resources.materials, id, &scene::MaterialResource::id);
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
        for (const scene::MaterialResource& material : scene.resources.materials) {
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
                        diffuse_materials.push_back(PathDiffuseMaterial{compile_path_spectrum(data.reflectance, "Diffuse reflectance", spectrum_tables, spectra, piecewise_spectra, texture_handle)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseTransmissionMaterialData>) {
                        kind        = PathMaterialKind::DiffuseTransmission;
                        local_index = static_cast<std::uint32_t>(diffuse_transmission_materials.size());
                        diffuse_transmission_materials.push_back(PathDiffuseTransmissionMaterial{compile_path_spectrum(data.reflectance, "Diffuse Transmission reflectance", spectrum_tables, spectra, piecewise_spectra, texture_handle), compile_path_spectrum(data.transmittance, "Diffuse Transmission transmittance", spectrum_tables, spectra, piecewise_spectra, texture_handle), {data.scale, data.scale, 0.0f, 0.0f}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ConductorMaterialData>) {
                        kind        = PathMaterialKind::Conductor;
                        local_index = static_cast<std::uint32_t>(conductor_materials.size());
                        PathConductorMaterial compiled{};
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.eta, "Conductor eta", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.k                  = compile_path_spectrum(optics.k, "Conductor k", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                } else {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.reflectance, "Conductor reflectance", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.metadata[0]        = 1;
                                }
                            },
                            data.optics);
                        const CompiledPathFloat u_roughness = compile_path_float(data.distribution.u_roughness.value_or(data.distribution.roughness), "Conductor u roughness", texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.distribution.v_roughness.value_or(data.distribution.roughness), "Conductor v roughness", texture_handle);
                        compiled.roughness                  = {u_roughness.value, v_roughness.value, 0.0f, 0.0f};
                        compiled.roughness_textures         = {u_roughness.texture, v_roughness.texture, 0, 0};
                        compiled.metadata[1]                = data.remap_roughness ? 1u : 0u;
                        conductor_materials.push_back(compiled);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DielectricMaterialData>) {
                        kind                                = PathMaterialKind::Dielectric;
                        local_index                         = static_cast<std::uint32_t>(dielectric_materials.size());
                        const CompiledPathFloat u_roughness = compile_path_float(data.distribution.u_roughness.value_or(data.distribution.roughness), "Dielectric u roughness", texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.distribution.v_roughness.value_or(data.distribution.roughness), "Dielectric v roughness", texture_handle);
                        dielectric_materials.push_back(PathDielectricMaterial{compile_path_spectrum(data.eta, "Dielectric eta", spectrum_tables, spectra, piecewise_spectra, texture_handle), {u_roughness.value, v_roughness.value, 0.0f, 0.0f}, {u_roughness.texture, v_roughness.texture, 0, 0}, {data.remap_roughness ? 1u : 0u, 0, 0, 0}});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ThinDielectricMaterialData>) {
                        kind        = PathMaterialKind::ThinDielectric;
                        local_index = static_cast<std::uint32_t>(thin_dielectric_materials.size());
                        thin_dielectric_materials.push_back(PathThinDielectricMaterial{compile_path_spectrum(data.eta, "Thin Dielectric eta", spectrum_tables, spectra, piecewise_spectra, texture_handle)});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedDiffuseMaterialData>) {
                        kind                                = PathMaterialKind::CoatedDiffuse;
                        local_index                         = static_cast<std::uint32_t>(coated_diffuse_materials.size());
                        const CompiledPathFloat u_roughness = compile_path_float(data.interface.u_roughness.value_or(data.interface.roughness), "Coated Diffuse interface u roughness", texture_handle);
                        const CompiledPathFloat v_roughness = compile_path_float(data.interface.v_roughness.value_or(data.interface.roughness), "Coated Diffuse interface v roughness", texture_handle);
                        const CompiledPathFloat thickness   = compile_path_float(data.coating.thickness, "Coated Diffuse thickness", texture_handle);
                        const CompiledPathFloat g           = compile_path_float(data.coating.g, "Coated Diffuse g", texture_handle);
                        coated_diffuse_materials.push_back(PathCoatedDiffuseMaterial{
                            compile_path_spectrum(data.reflectance, "Coated Diffuse reflectance", spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            compile_path_spectrum(data.eta, "Coated Diffuse eta", spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            compile_path_spectrum(data.coating.albedo, "Coated Diffuse layer albedo", spectrum_tables, spectra, piecewise_spectra, texture_handle),
                            {u_roughness.value, v_roughness.value, thickness.value, g.value},
                            {u_roughness.texture, v_roughness.texture, thickness.texture, g.texture},
                            {data.remap_roughness ? 1u : 0u, static_cast<std::uint32_t>(data.coating.max_depth), static_cast<std::uint32_t>(data.coating.sample_count), 0},
                        });
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CoatedConductorMaterialData>) {
                        kind        = PathMaterialKind::CoatedConductor;
                        local_index = static_cast<std::uint32_t>(coated_conductor_materials.size());
                        PathCoatedConductorMaterial compiled{};
                        compiled.interface_eta = compile_path_spectrum(data.interface_eta, "Coated Conductor interface eta", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                        std::visit(
                            [&](const auto& optics) {
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(optics)>, scene::ConductorEtaK>) {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.eta, "Coated Conductor eta", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.k                  = compile_path_spectrum(optics.k, "Coated Conductor k", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                } else {
                                    compiled.eta_or_reflectance = compile_path_spectrum(optics.reflectance, "Coated Conductor reflectance", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                                    compiled.metadata[0]        = 1;
                                }
                            },
                            data.optics);
                        compiled.albedo                               = compile_path_spectrum(data.coating.albedo, "Coated Conductor layer albedo", spectrum_tables, spectra, piecewise_spectra, texture_handle);
                        const CompiledPathFloat interface_u_roughness = compile_path_float(data.interface.u_roughness.value_or(data.interface.roughness), "Coated Conductor interface u roughness", texture_handle);
                        const CompiledPathFloat interface_v_roughness = compile_path_float(data.interface.v_roughness.value_or(data.interface.roughness), "Coated Conductor interface v roughness", texture_handle);
                        compiled.interface_roughness                  = {interface_u_roughness.value, interface_v_roughness.value, 0.0f, 0.0f};
                        compiled.interface_roughness_textures         = {interface_u_roughness.texture, interface_v_roughness.texture, 0, 0};
                        const CompiledPathFloat conductor_u_roughness = compile_path_float(data.conductor.u_roughness.value_or(data.conductor.roughness), "Coated Conductor u roughness", texture_handle);
                        const CompiledPathFloat conductor_v_roughness = compile_path_float(data.conductor.v_roughness.value_or(data.conductor.roughness), "Coated Conductor v roughness", texture_handle);
                        compiled.conductor_roughness                  = {conductor_u_roughness.value, conductor_v_roughness.value, 0.0f, 0.0f};
                        compiled.conductor_roughness_textures         = {conductor_u_roughness.texture, conductor_v_roughness.texture, 0, 0};
                        const CompiledPathFloat thickness             = compile_path_float(data.coating.thickness, "Coated Conductor thickness", texture_handle);
                        const CompiledPathFloat g                     = compile_path_float(data.coating.g, "Coated Conductor g", texture_handle);
                        compiled.coating                              = {thickness.value, g.value, 0.0f, 0.0f};
                        compiled.coating_textures                     = {thickness.texture, g.texture, 0, 0};
                        compiled.metadata[1]                          = data.remap_roughness ? 1u : 0u;
                        compiled.metadata[2]                          = static_cast<std::uint32_t>(data.coating.max_depth);
                        compiled.metadata[3]                          = static_cast<std::uint32_t>(data.coating.sample_count);
                        coated_conductor_materials.push_back(compiled);
                    } else {
                        kind                           = PathMaterialKind::Mix;
                        local_index                    = static_cast<std::uint32_t>(mix_materials.size());
                        const CompiledPathFloat amount = compile_path_float(data.amount, "Mix amount", texture_handle);
                        mix_materials.push_back(PathMixMaterial{{material_index(data.first), material_index(data.second), 0, 0}, amount.value, amount.texture, {}});
                    }
                },
                material.data);
            PathMaterialHeader material_header{static_cast<std::uint32_t>(kind), local_index, normal_map.value == 0 ? invalid_texture : texture_handle(normal_map), bump_map.value == 0 ? invalid_texture : texture_handle(bump_map)};
            material_header.stable_id = {
                static_cast<std::uint32_t>(material.id.value),
                static_cast<std::uint32_t>(material.id.value >> 32),
            };
            material_headers.push_back(material_header);
        }
        if (material_headers.empty()) throw std::runtime_error("PathScene requires at least one Material");
        std::vector<PathMaterialTextureRequest> material_texture_requests{};
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
                material_texture_requests.push_back(PathMaterialTextureRequest{{handle, static_cast<std::uint32_t>(role), owner, 0}});
            };
            std::function<void(std::uint32_t)> visit_material;
            visit_material = [&](const std::uint32_t index) {
                if (visited[index] != 0) return;
                visited[index]                          = 1;
                const scene::MaterialResource& material = scene.resources.materials[index];
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
        for (const PathMaterialHeader& header : material_headers) maximum_material_texture_requests = std::max(maximum_material_texture_requests, header.texture_requests[1]);
        if (material_texture_requests.empty()) material_texture_requests.emplace_back();
        if (diffuse_materials.empty()) diffuse_materials.emplace_back();
        if (diffuse_transmission_materials.empty()) diffuse_transmission_materials.emplace_back();
        if (conductor_materials.empty()) conductor_materials.emplace_back();
        if (dielectric_materials.empty()) dielectric_materials.emplace_back();
        if (thin_dielectric_materials.empty()) thin_dielectric_materials.emplace_back();
        if (coated_diffuse_materials.empty()) coated_diffuse_materials.emplace_back();
        if (coated_conductor_materials.empty()) coated_conductor_materials.emplace_back();
        if (mix_materials.empty()) mix_materials.emplace_back();

        std::vector<PathMedium> media{};
        std::vector<PathVolume> volumes{};
        std::vector<PathScene::VolumeGpuData> new_volume_gpu_data{};
        const auto compile_medium_spectrum = [&](const scene::SpectrumParameter& parameter) {
            const std::uint32_t index = static_cast<std::uint32_t>(spectra.size());
            spectra.push_back(compile_spectrum(parameter, spectrum_tables, piecewise_spectra));
            return index;
        };
        const auto volume_index = [&scene](const scene::VolumeId id) { return static_cast<std::uint32_t>(std::ranges::find(scene.resources.volumes, id, &scene::Volume::id) - scene.resources.volumes.begin()); };
        media.reserve(scene.resources.media.size());
        for (const scene::Medium& medium : scene.resources.media)
            std::visit(
                [&](const auto& data) {
                    PathMedium result{};
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

        volumes.reserve(scene.resources.volumes.size());
        new_volume_gpu_data.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) {
            const render::GpuVolume& shared = *std::ranges::find(this->gpu_scene->volumes, volume.id, &render::GpuVolume::id);
            PathScene::VolumeGpuData gpu_data{};
            gpu_data.id       = volume.id;
            gpu_data.revision = shared.revision;
            PathVolume result{};
            result.density                 = this->volume_zero.descriptor;
            result.temperature             = this->volume_zero.descriptor;
            result.emission_scale          = this->volume_zero.descriptor;
            result.sigma_a                 = this->volume_zero.descriptor;
            result.sigma_s                 = this->volume_zero.descriptor;
            result.emission                = this->volume_zero.descriptor;
            result.nanovdb_density         = this->volume_zero.descriptor;
            result.nanovdb_temperature     = this->volume_zero.descriptor;
            result.majorant                = this->volume_zero.descriptor;
            result.bounds_minimum          = {volume.bounds.minimum.x, volume.bounds.minimum.y, volume.bounds.minimum.z, 0.0f};
            result.bounds_maximum          = {volume.bounds.maximum.x, volume.bounds.maximum.y, volume.bounds.maximum.z, 0.0f};
            const scene::Transform inverse = volume.transform.inverse();
            result.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            result.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            result.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
            const auto field               = [this, &shared](const render::GpuVolumeField field) {
                const std::size_t index = std::to_underlying(field);
                return shared.present[index] ? shared.descriptors[index] : this->volume_zero.descriptor;
            };
            const auto upload_majorant = [&](const auto values) {
                GpuBuffer buffer                  = upload_path_buffer(*this->runtime, values, command_buffer);
                const DescriptorHandle descriptor = this->runtime->allocate_resource_descriptor();
                this->runtime->write_buffer(descriptor, vk::DescriptorType::eStorageBuffer, buffer);
                gpu_data.majorant        = GpuBufferBinding{descriptor};
                gpu_data.majorant.buffer = std::move(buffer);
                return descriptor;
            };
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                        gpu_data.resolution               = data.resolution;
                        result.metadata                   = {std::to_underlying(PathVolumeKind::DensityGrid), data.resolution.x, data.resolution.y, data.resolution.z};
                        result.majorant_metadata          = {16, 16, 16, (data.temperature.empty() ? 0u : 1u) | (data.emission_scale.empty() ? 0u : 2u)};
                        result.density                    = field(render::GpuVolumeField::Density);
                        result.temperature                = field(render::GpuVolumeField::Temperature);
                        result.emission_scale             = field(render::GpuVolumeField::EmissionScale);
                        const std::vector<float> majorant = build_density_majorant(data);
                        result.majorant                   = upload_majorant(std::span<const float>{majorant});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                        gpu_data.resolution               = data.resolution;
                        result.metadata                   = {std::to_underlying(PathVolumeKind::RgbGrid), data.resolution.x, data.resolution.y, data.resolution.z};
                        result.majorant_metadata          = {16, 16, 16, (data.sigma_a.empty() ? 0u : 1u) | (data.sigma_s.empty() ? 0u : 2u) | (data.emission.empty() ? 0u : 4u) | (std::to_underlying(data.color_space) << 8)};
                        result.sigma_a                    = field(render::GpuVolumeField::SigmaA);
                        result.sigma_s                    = field(render::GpuVolumeField::SigmaS);
                        result.emission                   = field(render::GpuVolumeField::Emission);
                        const std::vector<float> majorant = build_rgb_majorant(data, spectrum_tables);
                        result.majorant                   = upload_majorant(std::span<const float>{majorant});
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::NanoVdbVolume>) {
                        result.metadata[0]         = std::to_underlying(PathVolumeKind::NanoVdb);
                        result.majorant_metadata   = {data.majorant_resolution.x, data.majorant_resolution.y, data.majorant_resolution.z, data.temperature_data.empty() ? 0u : 1u};
                        result.nanovdb_density     = field(render::GpuVolumeField::NanoVdbDensity);
                        result.nanovdb_temperature = field(render::GpuVolumeField::NanoVdbTemperature);
                        result.majorant            = upload_majorant(std::span<const float>{data.majorant});
                    } else {
                        result.metadata[0]           = std::to_underlying(PathVolumeKind::ProceduralCloud);
                        result.procedural_parameters = {data.density, data.wispiness, data.frequency, 0.0f};
                        const std::array<float, 1> majorant{1.0f};
                        result.majorant = upload_majorant(std::span<const float>{majorant});
                    }
                },
                volume.data);
            volumes.push_back(result);
            new_volume_gpu_data.push_back(std::move(gpu_data));
        }
        if (media.empty()) media.emplace_back();
        if (volumes.empty()) volumes.emplace_back();

        const scene::Bounds3 bounds      = scene.bounds();
        const scene::Float3 scene_center = bounds.center();
        const float scene_radius         = bounds.radius();
        std::vector<PathLight> lights{};
        std::vector<PathLightDistribution> light_distributions{};
        std::vector<float> light_distribution_data{};
        std::vector<PathPortal> portals{};
        std::vector<PathLightBvhNode> light_bvh_nodes{};
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
            scene::Float3 luminance{0.212671f, 0.715160f, 0.072169f};
            if (texture.color_space == scene::TextureColorSpace::Rec2020)
                luminance = {0.262700f, 0.677998f, 0.059302f};
            else if (texture.color_space == scene::TextureColorSpace::Aces2065_1)
                luminance = {0.3439664498f, 0.7281660966f, -0.0721325464f};
            const scene::SpectrumColorSpace color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
            float luminance_sum{};
            float spectral_power_sum{};
            float channel_sum{};
            std::vector<scene::Float2> unused_piecewise_samples{};
            const std::size_t pixel_count = static_cast<std::size_t>(image.width) * image.height;
            for (std::size_t index = 0; index != pixel_count; ++index) {
                const scene::Float3 rgb{std::max(0.0f, image.texels[index].x), std::max(0.0f, image.texels[index].y), std::max(0.0f, image.texels[index].z)};
                luminance_sum += rgb.x * luminance.x + rgb.y * luminance.y + rgb.z * luminance.z;
                channel_sum += (rgb.x + rgb.y + rgb.z) / 3.0f;
                const CompiledSpectrum compiled = compile_spectrum(scene::SpectrumParameter{{rgb.x * image.scale, rgb.y * image.scale, rgb.z * image.scale}, {}, scene::SpectrumEncoding::RgbIlluminant, color_space}, spectrum_tables, unused_piecewise_samples);
                spectral_power_sum += spectrum_power_sample(compiled, unused_piecewise_samples, this->cie_samples);
            }
            return LightImageStatistics{luminance_sum / static_cast<float>(pixel_count) * image.scale, spectral_power_sum / static_cast<float>(pixel_count), channel_sum / static_cast<float>(pixel_count) * image.scale};
        };
        const auto initialize_light_transform = [](PathLight& target, const scene::Transform& transform) {
            const scene::Transform inverse = transform.inverse();
            target.transform_row_0         = {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]};
            target.transform_row_1         = {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]};
            target.transform_row_2         = {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]};
            target.inverse_transform_row_0 = {inverse.matrix[0], inverse.matrix[1], inverse.matrix[2], inverse.matrix[3]};
            target.inverse_transform_row_1 = {inverse.matrix[4], inverse.matrix[5], inverse.matrix[6], inverse.matrix[7]};
            target.inverse_transform_row_2 = {inverse.matrix[8], inverse.matrix[9], inverse.matrix[10], inverse.matrix[11]};
        };
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
                        PathLight compiled{};
                        compiled.references     = {invalid_path_index, invalid_path_index, invalid_path_index, invalid_path_index};
                        compiled.identifiers[3] = invalid_path_index;
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::PointLight>) {
                            compiled.identifiers         = {std::to_underlying(PathLightKind::Point), 1u, compile_light_spectrum(light.intensity), invalid_path_index};
                            const scene::Float3 position = light.transform.transform_point({});
                            compiled.position_and_scale  = {position.x, position.y, position.z, light.scale};
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = 4.0f * path_pi * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
                            compiled.selection[3] = 4.0f * path_pi * light.scale * spectrum_maximum(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
                            lights.push_back(compiled);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::SpotLight>) {
                            compiled.identifiers         = {std::to_underlying(PathLightKind::Spot), 1u, compile_light_spectrum(light.intensity), invalid_path_index};
                            const scene::Float3 position = light.transform.transform_point({});
                            compiled.position_and_scale  = {position.x, position.y, position.z, light.scale};
                            compiled.parameters[0]       = std::cos((light.cone_angle - light.cone_delta) * path_pi / 180.0f);
                            compiled.parameters[1]       = std::cos(light.cone_angle * path_pi / 180.0f);
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = 2.0f * path_pi * (1.0f - compiled.parameters[0] + (compiled.parameters[0] - compiled.parameters[1]) / 2.0f) * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
                            compiled.selection[3] = 4.0f * path_pi * light.scale * spectrum_maximum(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
                            lights.push_back(compiled);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(light)>, scene::DistantLight>) {
                            compiled.identifiers          = {std::to_underlying(PathLightKind::Distant), 2u, compile_light_spectrum(light.radiance), invalid_path_index};
                            const scene::Float3 direction = light.transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                            compiled.position_and_scale   = {direction.x, direction.y, direction.z, light.scale};
                            compiled.parameters           = {scene_center.x, scene_center.y, scene_center.z, scene_radius};
                            initialize_light_transform(compiled, light.transform);
                            compiled.selection[0] = path_pi * scene_radius * scene_radius * light.scale * spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
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
                            float average_power = spectrum_power_sample(spectra[compiled.identifiers[2]], piecewise_spectra, this->cie_samples);
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
                                const scene::Transform light_from_render           = environment.transform.inverse();
                                const scene::SpectrumColorSpace portal_color_space = texture.color_space == scene::TextureColorSpace::Rec2020 ? scene::SpectrumColorSpace::Rec2020 : texture.color_space == scene::TextureColorSpace::Aces2065_1 ? scene::SpectrumColorSpace::Aces2065_1 : scene::SpectrumColorSpace::Srgb;
                                const std::uint32_t portal_illuminant_curve        = portal_color_space == scene::SpectrumColorSpace::Aces2065_1 ? 4u : 3u;
                                for (const std::array<scene::Float3, 4>& points : light.portals) {
                                    const scene::Float3 frame_x      = (points[3] - points[0]).normalized();
                                    const scene::Float3 frame_y      = (points[1] - points[0]).normalized();
                                    const scene::Float3 frame_z      = frame_x.cross(frame_y).normalized();
                                    const std::uint32_t portal_index = static_cast<std::uint32_t>(portals.size());
                                    portals.push_back(PathPortal{{points[0].x, points[0].y, points[0].z, 0.0f}, {points[2].x, points[2].y, points[2].z, 0.0f}, {frame_x.x, frame_x.y, frame_x.z, 0.0f}, {frame_y.x, frame_y.y, frame_y.z, 0.0f}, {frame_z.x, frame_z.y, frame_z.z, 0.0f}});
                                    std::vector<float> weights(static_cast<std::size_t>(image.width) * image.height);
                                    std::vector<CompiledSpectrum> rectified_spectra(weights.size());
                                    float portal_power_sum{};
                                    std::vector<scene::Float2> unused_piecewise_samples{};
                                    for (std::uint32_t y = 0; y != image.height; ++y)
                                        for (std::uint32_t x = 0; x != image.width; ++x) {
                                            const scene::Float2 uv{(static_cast<float>(x) + 0.5f) / static_cast<float>(image.width), (static_cast<float>(y) + 0.5f) / static_cast<float>(image.height)};
                                            const float alpha                      = -path_pi / 2.0f + uv.x * path_pi;
                                            const float beta                       = -path_pi / 2.0f + uv.y * path_pi;
                                            const scene::Float3 local              = scene::Float3{std::tan(alpha), std::tan(beta), 1.0f}.normalized();
                                            const scene::Float3 render_direction   = frame_x * local.x + frame_y * local.y + frame_z * local.z;
                                            const scene::Float3 light_direction    = light_from_render.transform_vector(render_direction).normalized();
                                            const scene::Float4 value              = image_bilerp_octahedral(image, equal_area_sphere_to_square(light_direction));
                                            const float jacobian                   = path_pi * path_pi * (1.0f - local.x * local.x) * (1.0f - local.y * local.y) / local.z;
                                            weights[y * image.width + x]           = (value.x + value.y + value.z) / 3.0f * jacobian;
                                            const CompiledSpectrum rectified       = compile_spectrum(scene::SpectrumParameter{{std::max(0.0f, value.x), std::max(0.0f, value.y), std::max(0.0f, value.z)}, {}, scene::SpectrumEncoding::RgbIlluminant, portal_color_space}, spectrum_tables, unused_piecewise_samples);
                                            rectified_spectra[y * image.width + x] = rectified;
                                            portal_power_sum += spectrum_power_sample(rectified, unused_piecewise_samples, this->cie_samples) * image.scale / jacobian;
                                        }
                                    PathLight portal_light                     = compiled;
                                    portal_light.references[1]                 = compile_light_distribution(weights, image.width, image.height, light_distributions, light_distribution_data);
                                    PathLightDistribution& portal_distribution = light_distributions[portal_light.references[1]];
                                    portal_distribution.offsets[2]             = static_cast<std::uint32_t>(light_distribution_data.size());
                                    portal_distribution.offsets[3]             = portal_illuminant_curve;
                                    portal_distribution.parameters[2]          = image.scale;
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
        }

        std::vector<PathPrimitive> primitives{};
        std::vector<PathLightShape> light_shapes{};
        std::vector<std::uint32_t> face_materials{};
        const auto medium_index = [&scene](const scene::MediumId id) {
            if (id.value == 0) return invalid_path_index;
            return static_cast<std::uint32_t>(std::ranges::find(scene.resources.media, id, &scene::Medium::id) - scene.resources.media.begin());
        };
        this->camera_medium_index = medium_index(scene.camera.medium);
        const auto add_area_light = [&](PathLightShape shape, const std::uint32_t source_index, const float area, const bool delta_position) {
            const std::uint32_t shape_index = static_cast<std::uint32_t>(light_shapes.size());
            const std::uint32_t light_index = static_cast<std::uint32_t>(lights.size());
            shape.identifiers[0]            = light_index;
            light_shapes.push_back(shape);
            float scale = area_scales[source_index];
            if (area_powers[source_index] > 0.0f) scale *= area_powers[source_index] / ((area_two_sided[source_index] ? 2.0f : 1.0f) * area * path_pi * area_texture_luminance[source_index]);
            PathLight light{};
            light.identifiers = {std::to_underlying(PathLightKind::DiffuseArea), (area_two_sided[source_index] ? 8u : 0u) | (delta_position ? 1u : 0u), area_spectra[source_index], area_textures[source_index]};
            if (area_textures[source_index] != invalid_path_index) light.identifiers[1] |= 16u;
            light.references            = {shape_index, invalid_path_index, invalid_path_index, invalid_path_index};
            light.position_and_scale[3] = scale;
            const float spectral_power  = area_textures[source_index] == invalid_path_index ? spectrum_power_sample(spectra[area_spectra[source_index]], piecewise_spectra, this->cie_samples) : area_texture_power[source_index];
            light.selection[0]          = path_pi * (area_two_sided[source_index] ? 2.0f : 1.0f) * area * scale * spectral_power;
            light.selection[3]          = path_pi * area * scale * (area_textures[source_index] == invalid_path_index ? spectrum_maximum(spectra[area_spectra[source_index]], piecewise_spectra, this->cie_samples) : area_texture_channel_average[source_index]);
            lights.push_back(light);
        };
        primitives.reserve(this->gpu_scene->acceleration_draw_indices.size());
        for (const std::uint32_t draw_index : this->gpu_scene->acceleration_draw_indices) {
            const render::GpuDraw& draw              = this->gpu_scene->draws[draw_index];
            const scene::Instance& instance          = scene.resources.instances[draw.scene_instance_index];
            const scene::Prototype& prototype        = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive        = prototype.primitives[draw.prototype_primitive_index];
            const std::uint32_t face_material_offset = static_cast<std::uint32_t>(face_materials.size());
            for (const scene::MaterialId face_material : primitive.face_materials) {
                const std::vector<scene::MaterialResource>::const_iterator resource = std::ranges::find(scene.resources.materials, face_material, &scene::MaterialResource::id);
                face_materials.push_back(static_cast<std::uint32_t>(resource - scene.resources.materials.begin()));
            }
            const std::vector<scene::MaterialResource>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::MaterialResource::id);
            const std::uint32_t material_index                                  = static_cast<std::uint32_t>(material - scene.resources.materials.begin());
            std::uint32_t alpha                                                 = invalid_texture;
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
            const std::uint32_t first_light_shape   = static_cast<std::uint32_t>(lights.size());
            const scene::Geometry* geometry         = &*std::ranges::find(scene.resources.geometries, primitive.geometry, &scene::Geometry::id);
            const scene::TriangleMeshGeometry* mesh = std::get_if<scene::TriangleMeshGeometry>(&geometry->data);
            std::uint32_t geometry_kind{};
            std::array<float, 4> geometry_parameters{};
            if (geometry)
                std::visit(
                    [&geometry_kind, &geometry_parameters](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SphereGeometry>) {
                            geometry_kind       = 1;
                            geometry_parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiskGeometry>) {
                            geometry_kind       = 2;
                            geometry_parameters = {data.height, data.radius, data.inner_radius, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CylinderGeometry>) {
                            geometry_kind       = 3;
                            geometry_parameters = {data.radius, data.z_min, data.z_max, data.phi_max * std::numbers::pi_v<float> / 180.0f};
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::BoxGeometry>)
                            geometry_kind = 4;
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RectangleGeometry>)
                            geometry_kind = 5;
                    },
                    geometry->data);
            if (area_light != std::numeric_limits<std::uint32_t>::max()) {
                const scene::Transform transform = instance.transform * primitive.transform;
                if (mesh) {
                    const scene::Transform inverse = transform.inverse();
                    for (std::size_t index = 0; index != mesh->indices.size(); index += 3) {
                        std::array<scene::Float3, 3> positions{};
                        std::array<scene::Float3, 3> normals{};
                        for (std::uint32_t vertex = 0; vertex != 3; ++vertex) {
                            positions[vertex] = transform.transform_point(mesh->positions[mesh->indices[index + vertex]]);
                            if (!mesh->normals.empty()) {
                                const scene::Float3 source = mesh->normals[mesh->indices[index + vertex]];
                                normals[vertex]            = scene::Float3{inverse.matrix[0] * source.x + inverse.matrix[4] * source.y + inverse.matrix[8] * source.z, inverse.matrix[1] * source.x + inverse.matrix[5] * source.y + inverse.matrix[9] * source.z, inverse.matrix[2] * source.x + inverse.matrix[6] * source.y + inverse.matrix[10] * source.z}.normalized() * (primitive.reverse_orientation ? -1.0f : 1.0f);
                            }
                        }
                        const scene::Float3 edge_0 = positions[1] - positions[0];
                        const scene::Float3 edge_1 = positions[2] - positions[0];
                        const float area           = 0.5f * edge_0.cross(edge_1).length();
                        add_area_light(PathLightShape{{positions[0].x, positions[0].y, positions[0].z, area}, {positions[1].x, positions[1].y, positions[1].z, 0.0f}, {positions[2].x, positions[2].y, positions[2].z, 0.0f}, {0, primitive.reverse_orientation ? 1u : 0u, geometry_kind, zero_alpha_area_light ? invalid_texture : alpha}, {}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, mesh->texture_coordinates.empty() ? std::array<float, 4>{0.0f, 0.0f, 1.0f, 0.0f} : std::array<float, 4>{mesh->texture_coordinates[mesh->indices[index]].x, mesh->texture_coordinates[mesh->indices[index]].y, mesh->texture_coordinates[mesh->indices[index + 1]].x, mesh->texture_coordinates[mesh->indices[index + 1]].y}, mesh->texture_coordinates.empty() ? std::array<float, 4>{1.0f, 1.0f, 0.0f, 0.0f} : std::array<float, 4>{mesh->texture_coordinates[mesh->indices[index + 2]].x, mesh->texture_coordinates[mesh->indices[index + 2]].y, 0.0f, 0.0f},
                                           mesh->normals.empty() ? std::array<float, 4>{} : std::array<float, 4>{normals[0].x, normals[0].y, normals[0].z, 1.0f}, mesh->normals.empty() ? std::array<float, 4>{} : std::array<float, 4>{normals[1].x, normals[1].y, normals[1].z, 0.0f}, mesh->normals.empty() ? std::array<float, 4>{} : std::array<float, 4>{normals[2].x, normals[2].y, normals[2].z, 0.0f}},
                            area_light, area, zero_alpha_area_light);
                    }
                } else {
                    std::array<float, 4> position_0{0.0f, 0.0f, 0.0f, scene::surface_area(*geometry)};
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
                    add_area_light(PathLightShape{position_0, position_1, {}, {0, primitive.reverse_orientation ? 1u : 0u, geometry_kind, zero_alpha_area_light ? invalid_texture : alpha}, geometry_parameters, {transform.matrix[0], transform.matrix[1], transform.matrix[2], transform.matrix[3]}, {transform.matrix[4], transform.matrix[5], transform.matrix[6], transform.matrix[7]}, {transform.matrix[8], transform.matrix[9], transform.matrix[10], transform.matrix[11]}}, area_light, position_0[3], zero_alpha_area_light);
                }
            }
            primitives.push_back(PathPrimitive{material_index, area_light, first_light_shape, primitive.reverse_orientation ? 1u : 0u, this->gpu_scene->geometries[draw.resource_index].indices_descriptor, this->gpu_scene->geometries[draw.resource_index].normals_descriptor, this->gpu_scene->geometries[draw.resource_index].tangents_descriptor, this->gpu_scene->geometries[draw.resource_index].texture_coordinates_descriptor, {this->gpu_scene->geometries[draw.resource_index].attribute_flags, alpha, geometry_kind, 0}, geometry_parameters, {face_material_offset, static_cast<std::uint32_t>(primitive.face_materials.size()), 0, 0}, {medium_index(primitive.media.inside), medium_index(primitive.media.outside), 0, 0}, {static_cast<std::uint32_t>(instance.id.value), static_cast<std::uint32_t>(instance.id.value >> 32)}});
        }
        this->lights = static_cast<std::uint32_t>(lights.size());
        if (!lights.empty()) {
            if (scene.transport.light_sampler == scene::LightSamplerKind::Uniform) {
                const float probability = 1.0f / static_cast<float>(lights.size());
                float cumulative{};
                for (PathLight& light : lights) {
                    light.selection[1] = probability;
                    cumulative += probability;
                    light.selection[2] = cumulative;
                }
                lights.back().selection[2] = 1.0f;
            } else if (scene.transport.light_sampler == scene::LightSamplerKind::Power) {
                float total           = std::transform_reduce(lights.begin(), lights.end(), 0.0f, std::plus{}, [](const PathLight& light) { return light.selection[0]; });
                const bool zero_power = total == 0.0f;
                if (zero_power) total = static_cast<float>(lights.size());
                float cumulative{};
                for (PathLight& light : lights) {
                    light.selection[1] = zero_power ? 1.0f / total : light.selection[0] / total;
                    cumulative += light.selection[1];
                    light.selection[2] = cumulative;
                }
                lights.back().selection[2] = 1.0f;
            } else {
                struct LightBounds {
                    scene::Float3 minimum{};
                    scene::Float3 maximum{};
                    scene::Float3 direction{0.0f, 0.0f, 1.0f};
                    float phi{};
                    float cos_theta_o{-1.0f};
                    float cos_theta_e{};
                    bool two_sided{};
                };
                const auto include = [](scene::Float3& minimum, scene::Float3& maximum, const scene::Float3 point) {
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
                    const scene::Float3 axis = first.direction.cross(second.direction);
                    const float axis_length  = axis.length();
                    if (axis_length == 0.0f) {
                        result.direction   = {0.0f, 0.0f, 1.0f};
                        result.cos_theta_o = -1.0f;
                        return result;
                    }
                    const scene::Float3 normalized_axis = axis / axis_length;
                    const float rotation                = theta - theta_first;
                    const scene::Float3 rotated         = first.direction * std::cos(rotation) + normalized_axis.cross(first.direction) * std::sin(rotation);
                    result.direction                    = (rotated + normalized_axis * normalized_axis.dot(first.direction) * (1.0f - std::cos(rotation))).normalized();
                    result.cos_theta_o                  = std::cos(theta);
                    return result;
                };
                std::vector<std::pair<std::uint32_t, LightBounds>> bounded_lights{};
                for (std::uint32_t light_index = 0; light_index != lights.size(); ++light_index) {
                    const PathLight& light   = lights[light_index];
                    const PathLightKind kind = static_cast<PathLightKind>(light.identifiers[0]);
                    if (kind == PathLightKind::Distant || kind == PathLightKind::Infinite || kind == PathLightKind::PortalInfinite) {
                        ++light_bvh_infinite_count;
                        continue;
                    }
                    LightBounds light_bounds{};
                    light_bounds.minimum = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
                    light_bounds.maximum = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
                    light_bounds.phi     = std::max(light.selection[3], 0.0f);
                    if (kind == PathLightKind::Point || kind == PathLightKind::Spot) {
                        const scene::Float3 position{light.position_and_scale[0], light.position_and_scale[1], light.position_and_scale[2]};
                        light_bounds.minimum = position;
                        light_bounds.maximum = position;
                        if (kind == PathLightKind::Spot) {
                            light_bounds.direction   = scene::Float3{light.transform_row_0[2], light.transform_row_1[2], light.transform_row_2[2]}.normalized();
                            light_bounds.cos_theta_o = light.parameters[0];
                            light_bounds.cos_theta_e = std::cos(std::acos(std::clamp(light.parameters[1], -1.0f, 1.0f)) - std::acos(std::clamp(light.parameters[0], -1.0f, 1.0f)));
                        }
                    } else {
                        const PathLightShape& shape  = light_shapes[light.references[0]];
                        const std::uint32_t geometry = shape.identifiers[2];
                        if (geometry == 0) {
                            const scene::Float3 points[3]{{shape.position_0_and_area[0], shape.position_0_and_area[1], shape.position_0_and_area[2]}, {shape.position_1[0], shape.position_1[1], shape.position_1[2]}, {shape.position_2[0], shape.position_2[1], shape.position_2[2]}};
                            for (const scene::Float3 point : points) include(light_bounds.minimum, light_bounds.maximum, point);
                            light_bounds.direction = (points[1] - points[0]).cross(points[2] - points[0]).normalized();
                            if (shape.identifiers[1] != 0) light_bounds.direction = -light_bounds.direction;
                            light_bounds.cos_theta_o = 1.0f;
                        } else {
                            scene::Float3 local_minimum{};
                            scene::Float3 local_maximum{};
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
                                    for (const float z : {local_minimum.z, local_maximum.z}) include(light_bounds.minimum, light_bounds.maximum, {scene::Float3{shape.transform_row_0[0], shape.transform_row_0[1], shape.transform_row_0[2]}.dot({x, y, z}) + shape.transform_row_0[3], scene::Float3{shape.transform_row_1[0], shape.transform_row_1[1], shape.transform_row_1[2]}.dot({x, y, z}) + shape.transform_row_1[3], scene::Float3{shape.transform_row_2[0], shape.transform_row_2[1], shape.transform_row_2[2]}.dot({x, y, z}) + shape.transform_row_2[3]});
                            if (geometry == 2 || geometry == 5) {
                                const scene::Float3 x_axis{shape.transform_row_0[0], shape.transform_row_1[0], shape.transform_row_2[0]};
                                const scene::Float3 y_axis{shape.transform_row_0[1], shape.transform_row_1[1], shape.transform_row_2[1]};
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
                        const scene::Float3 diagonal = bounds.maximum - bounds.minimum;
                        return 2.0f * (diagonal.x * diagonal.y + diagonal.x * diagonal.z + diagonal.y * diagonal.z);
                    };
                    const auto cost = [&](const LightBounds& light, const LightBounds& bounds, const std::uint32_t dimension) {
                        if (light.phi == 0.0f) return 0.0f;
                        const float theta_o          = std::acos(std::clamp(light.cos_theta_o, -1.0f, 1.0f));
                        const float theta_e          = std::acos(std::clamp(light.cos_theta_e, -1.0f, 1.0f));
                        const float theta_w          = std::min(theta_o + theta_e, path_pi);
                        const float sin_theta_o      = std::sqrt(std::max(0.0f, 1.0f - light.cos_theta_o * light.cos_theta_o));
                        const float measure          = 2.0f * path_pi * (1.0f - light.cos_theta_o) + path_pi / 2.0f * (2.0f * theta_w * sin_theta_o - std::cos(theta_o - 2.0f * theta_w) - 2.0f * theta_o * sin_theta_o + light.cos_theta_o);
                        const scene::Float3 diagonal = bounds.maximum - bounds.minimum;
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
                            light_bvh_nodes.push_back(PathLightBvhNode{{light_bounds.minimum.x, light_bounds.minimum.y, light_bounds.minimum.z, light_bounds.phi}, {light_bounds.maximum.x, light_bounds.maximum.y, light_bounds.maximum.z, light_bounds.cos_theta_o}, {light_bounds.direction.x, light_bounds.direction.y, light_bounds.direction.z, light_bounds.cos_theta_e}, {light_index, 1u, light_bounds.two_sided ? 1u : 0u, 0u}});
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
                            bounds                       = union_bounds(bounds, bounded_lights[index].second);
                            const scene::Float3 centroid = (bounded_lights[index].second.minimum + bounded_lights[index].second.maximum) * 0.5f;
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
                                const scene::Float3 centroid    = (light_bounds.minimum + light_bounds.maximum) * 0.5f;
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
                                const scene::Float3 centroid = (entry.second.minimum + entry.second.maximum) * 0.5f;
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
                        light_bvh_nodes[node_index] = PathLightBvhNode{{combined.minimum.x, combined.minimum.y, combined.minimum.z, combined.phi}, {combined.maximum.x, combined.maximum.y, combined.maximum.z, combined.cos_theta_o}, {combined.direction.x, combined.direction.y, combined.direction.z, combined.cos_theta_e}, {second.first, 0u, combined.two_sided ? 1u : 0u, 0u}};
                        return {node_index, combined};
                    };
                    build(0, static_cast<std::uint32_t>(bounded_lights.size()), 0, 0);
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

        GpuBuffer new_primitives = upload_path_buffer(*this->runtime, std::span<const PathPrimitive>{primitives}, command_buffer);
        std::array<GpuBuffer, static_cast<std::size_t>(PathMaterialTable::Count)> new_materials{};
        new_materials[static_cast<std::size_t>(PathMaterialTable::Header)]              = upload_path_buffer(*this->runtime, std::span<const PathMaterialHeader>{material_headers}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::Diffuse)]             = upload_path_buffer(*this->runtime, std::span<const PathDiffuseMaterial>{diffuse_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::DiffuseTransmission)] = upload_path_buffer(*this->runtime, std::span<const PathDiffuseTransmissionMaterial>{diffuse_transmission_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::Conductor)]           = upload_path_buffer(*this->runtime, std::span<const PathConductorMaterial>{conductor_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::Dielectric)]          = upload_path_buffer(*this->runtime, std::span<const PathDielectricMaterial>{dielectric_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::ThinDielectric)]      = upload_path_buffer(*this->runtime, std::span<const PathThinDielectricMaterial>{thin_dielectric_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::CoatedDiffuse)]       = upload_path_buffer(*this->runtime, std::span<const PathCoatedDiffuseMaterial>{coated_diffuse_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::CoatedConductor)]     = upload_path_buffer(*this->runtime, std::span<const PathCoatedConductorMaterial>{coated_conductor_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::Mix)]                 = upload_path_buffer(*this->runtime, std::span<const PathMixMaterial>{mix_materials}, command_buffer);
        new_materials[static_cast<std::size_t>(PathMaterialTable::TextureRequest)]      = upload_path_buffer(*this->runtime, std::span<const PathMaterialTextureRequest>{material_texture_requests}, command_buffer);
        std::array<GpuBuffer, static_cast<std::size_t>(PathTextureTable::Count)> new_textures{};
        new_textures[static_cast<std::size_t>(PathTextureTable::Header)]       = upload_path_buffer(*this->runtime, std::span<const PathTextureHeader>{texture_headers}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Mapping)]      = upload_path_buffer(*this->runtime, std::span<const PathTextureMapping>{texture_mappings}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Constant)]     = upload_path_buffer(*this->runtime, std::span<const PathConstantTexture>{constant_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Image)]        = upload_path_buffer(*this->runtime, std::span<const PathImageTexture>{image_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Checkerboard)] = upload_path_buffer(*this->runtime, std::span<const PathCheckerboardTexture>{checkerboard_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Scale)]        = upload_path_buffer(*this->runtime, std::span<const PathScaleTexture>{scale_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Mix)]          = upload_path_buffer(*this->runtime, std::span<const PathMixTexture>{mix_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::DirectionMix)] = upload_path_buffer(*this->runtime, std::span<const PathDirectionMixTexture>{direction_mix_textures}, command_buffer);
        new_textures[static_cast<std::size_t>(PathTextureTable::Bilerp)]       = upload_path_buffer(*this->runtime, std::span<const PathBilerpTexture>{bilerp_textures}, command_buffer);
        GpuBuffer new_lights                                                   = upload_path_buffer(*this->runtime, std::span<const PathLight>{lights}, command_buffer);
        GpuBuffer new_light_shapes                                             = upload_path_buffer(*this->runtime, std::span<const PathLightShape>{light_shapes}, command_buffer);
        GpuBuffer new_light_distributions                                      = upload_path_buffer(*this->runtime, std::span<const PathLightDistribution>{light_distributions}, command_buffer);
        GpuBuffer new_light_distribution_data                                  = upload_path_buffer(*this->runtime, std::span<const float>{light_distribution_data}, command_buffer);
        GpuBuffer new_portals                                                  = upload_path_buffer(*this->runtime, std::span<const PathPortal>{portals}, command_buffer);
        GpuBuffer new_light_bvh_nodes                                          = upload_path_buffer(*this->runtime, std::span<const PathLightBvhNode>{light_bvh_nodes}, command_buffer);
        GpuBuffer new_light_bvh_bit_trails                                     = upload_path_buffer(*this->runtime, std::span<const std::uint32_t>{light_bvh_bit_trails}, command_buffer);
        GpuBuffer new_face_materials                                           = upload_path_buffer(*this->runtime, std::span<const std::uint32_t>{face_materials}, command_buffer);
        GpuBuffer new_media                                                    = upload_path_buffer(*this->runtime, std::span<const PathMedium>{media}, command_buffer);
        GpuBuffer new_volumes                                                  = upload_path_buffer(*this->runtime, std::span<const PathVolume>{volumes}, command_buffer);
        GpuBuffer new_spectra                                                  = upload_path_buffer(*this->runtime, std::span<const CompiledSpectrum>{spectra}, command_buffer);
        GpuBuffer new_piecewise_spectra                                        = upload_path_buffer(*this->runtime, std::span<const scene::Float2>{piecewise_spectra}, command_buffer);
        std::array<DescriptorHandle, static_cast<std::size_t>(PathMaterialTable::Count)> new_material_descriptors{};
        std::array<DescriptorHandle, static_cast<std::size_t>(PathTextureTable::Count)> new_texture_descriptors{};
        if (*this->primitives.buffer.buffer) {
            const DescriptorHandle new_primitive_descriptor = this->runtime->allocate_resource_descriptor();
            for (DescriptorHandle& descriptor : new_material_descriptors) descriptor = this->runtime->allocate_resource_descriptor();
            for (DescriptorHandle& descriptor : new_texture_descriptors) descriptor = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_descriptor                   = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_shape_descriptor             = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_distribution_descriptor      = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_distribution_data_descriptor = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_portal_descriptor                  = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_bvh_node_descriptor          = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_light_bvh_bit_trail_descriptor     = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_face_material_descriptor           = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_medium_descriptor                  = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_volume_descriptor                  = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_spectrum_descriptor                = this->runtime->allocate_resource_descriptor();
            const DescriptorHandle new_piecewise_spectrum_descriptor      = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(new_primitive_descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            for (std::size_t index = 0; index != new_materials.size(); ++index) this->runtime->write_buffer(new_material_descriptors[index], vk::DescriptorType::eStorageBuffer, new_materials[index]);
            for (std::size_t index = 0; index != new_textures.size(); ++index) this->runtime->write_buffer(new_texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_textures[index]);
            this->runtime->write_buffer(new_light_descriptor, vk::DescriptorType::eStorageBuffer, new_lights);
            this->runtime->write_buffer(new_light_shape_descriptor, vk::DescriptorType::eStorageBuffer, new_light_shapes);
            this->runtime->write_buffer(new_light_distribution_descriptor, vk::DescriptorType::eStorageBuffer, new_light_distributions);
            this->runtime->write_buffer(new_light_distribution_data_descriptor, vk::DescriptorType::eStorageBuffer, new_light_distribution_data);
            this->runtime->write_buffer(new_portal_descriptor, vk::DescriptorType::eStorageBuffer, new_portals);
            this->runtime->write_buffer(new_light_bvh_node_descriptor, vk::DescriptorType::eStorageBuffer, new_light_bvh_nodes);
            this->runtime->write_buffer(new_light_bvh_bit_trail_descriptor, vk::DescriptorType::eStorageBuffer, new_light_bvh_bit_trails);
            this->runtime->write_buffer(new_face_material_descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
            this->runtime->write_buffer(new_medium_descriptor, vk::DescriptorType::eStorageBuffer, new_media);
            this->runtime->write_buffer(new_volume_descriptor, vk::DescriptorType::eStorageBuffer, new_volumes);
            this->runtime->write_buffer(new_spectrum_descriptor, vk::DescriptorType::eStorageBuffer, new_spectra);
            this->runtime->write_buffer(new_piecewise_spectrum_descriptor, vk::DescriptorType::eStorageBuffer, new_piecewise_spectra);
            this->runtime->release_resource_descriptor(this->primitives.descriptor);
            for (const GpuBufferBinding& binding : this->materials) this->runtime->release_resource_descriptor(binding.descriptor);
            for (const GpuBufferBinding& binding : this->textures) this->runtime->release_resource_descriptor(binding.descriptor);
            this->runtime->release_resource_descriptor(this->light_table.descriptor);
            this->runtime->release_resource_descriptor(this->light_shapes.descriptor);
            this->runtime->release_resource_descriptor(this->light_distribution.descriptor);
            this->runtime->release_resource_descriptor(this->light_distribution_data.descriptor);
            this->runtime->release_resource_descriptor(this->portals.descriptor);
            this->runtime->release_resource_descriptor(this->light_bvh_nodes.descriptor);
            this->runtime->release_resource_descriptor(this->light_bvh_bit_trails.descriptor);
            this->runtime->release_resource_descriptor(this->face_materials.descriptor);
            this->runtime->release_resource_descriptor(this->media.descriptor);
            this->runtime->release_resource_descriptor(this->volumes.descriptor);
            for (const VolumeGpuData& volume : this->volume_gpu_data) this->runtime->release_resource_descriptor(volume.majorant.descriptor);
            this->runtime->release_resource_descriptor(this->spectra.descriptor);
            this->runtime->release_resource_descriptor(this->piecewise_spectra.descriptor);
            this->runtime->defer([primitives = std::move(this->primitives.buffer), materials = std::move(this->materials), textures = std::move(this->textures), lights = std::move(this->light_table.buffer), light_shapes = std::move(this->light_shapes.buffer), light_distributions = std::move(this->light_distribution.buffer), light_distribution_data = std::move(this->light_distribution_data.buffer), portals = std::move(this->portals.buffer), light_bvh_nodes = std::move(this->light_bvh_nodes.buffer), light_bvh_bit_trails = std::move(this->light_bvh_bit_trails.buffer), face_materials = std::move(this->face_materials.buffer), media = std::move(this->media.buffer), volumes = std::move(this->volumes.buffer), volume_gpu_data = std::move(this->volume_gpu_data), spectra = std::move(this->spectra.buffer), piecewise_spectra = std::move(this->piecewise_spectra.buffer)]() mutable {});
            this->primitives.descriptor = new_primitive_descriptor;
            for (std::size_t index = 0; index != this->materials.size(); ++index) this->materials[index].descriptor = new_material_descriptors[index];
            for (std::size_t index = 0; index != this->textures.size(); ++index) this->textures[index].descriptor = new_texture_descriptors[index];
            this->light_table.descriptor             = new_light_descriptor;
            this->light_shapes.descriptor            = new_light_shape_descriptor;
            this->light_distribution.descriptor      = new_light_distribution_descriptor;
            this->light_distribution_data.descriptor = new_light_distribution_data_descriptor;
            this->portals.descriptor                 = new_portal_descriptor;
            this->light_bvh_nodes.descriptor         = new_light_bvh_node_descriptor;
            this->light_bvh_bit_trails.descriptor    = new_light_bvh_bit_trail_descriptor;
            this->face_materials.descriptor          = new_face_material_descriptor;
            this->media.descriptor                   = new_medium_descriptor;
            this->volumes.descriptor                 = new_volume_descriptor;
            this->spectra.descriptor                 = new_spectrum_descriptor;
            this->piecewise_spectra.descriptor       = new_piecewise_spectrum_descriptor;
        } else {
            for (std::size_t index = 0; index != this->materials.size(); ++index) new_material_descriptors[index] = this->materials[index].descriptor;
            for (std::size_t index = 0; index != this->textures.size(); ++index) new_texture_descriptors[index] = this->textures[index].descriptor;
            this->runtime->write_buffer(this->primitives.descriptor, vk::DescriptorType::eStorageBuffer, new_primitives);
            for (std::size_t index = 0; index != new_materials.size(); ++index) this->runtime->write_buffer(new_material_descriptors[index], vk::DescriptorType::eStorageBuffer, new_materials[index]);
            for (std::size_t index = 0; index != new_textures.size(); ++index) this->runtime->write_buffer(new_texture_descriptors[index], vk::DescriptorType::eStorageBuffer, new_textures[index]);
            this->runtime->write_buffer(this->light_table.descriptor, vk::DescriptorType::eStorageBuffer, new_lights);
            this->runtime->write_buffer(this->light_shapes.descriptor, vk::DescriptorType::eStorageBuffer, new_light_shapes);
            this->runtime->write_buffer(this->light_distribution.descriptor, vk::DescriptorType::eStorageBuffer, new_light_distributions);
            this->runtime->write_buffer(this->light_distribution_data.descriptor, vk::DescriptorType::eStorageBuffer, new_light_distribution_data);
            this->runtime->write_buffer(this->portals.descriptor, vk::DescriptorType::eStorageBuffer, new_portals);
            this->runtime->write_buffer(this->light_bvh_nodes.descriptor, vk::DescriptorType::eStorageBuffer, new_light_bvh_nodes);
            this->runtime->write_buffer(this->light_bvh_bit_trails.descriptor, vk::DescriptorType::eStorageBuffer, new_light_bvh_bit_trails);
            this->runtime->write_buffer(this->face_materials.descriptor, vk::DescriptorType::eStorageBuffer, new_face_materials);
            this->runtime->write_buffer(this->media.descriptor, vk::DescriptorType::eStorageBuffer, new_media);
            this->runtime->write_buffer(this->volumes.descriptor, vk::DescriptorType::eStorageBuffer, new_volumes);
            this->runtime->write_buffer(this->spectra.descriptor, vk::DescriptorType::eStorageBuffer, new_spectra);
            this->runtime->write_buffer(this->piecewise_spectra.descriptor, vk::DescriptorType::eStorageBuffer, new_piecewise_spectra);
        }
        this->primitives.buffer = std::move(new_primitives);
        for (std::size_t index = 0; index != this->materials.size(); ++index) this->materials[index].buffer = std::move(new_materials[index]);
        for (std::size_t index = 0; index != this->textures.size(); ++index) this->textures[index].buffer = std::move(new_textures[index]);
        this->light_table.buffer             = std::move(new_lights);
        this->light_shapes.buffer            = std::move(new_light_shapes);
        this->light_distribution.buffer      = std::move(new_light_distributions);
        this->light_distribution_data.buffer = std::move(new_light_distribution_data);
        this->portals.buffer                 = std::move(new_portals);
        this->light_bvh_nodes.buffer         = std::move(new_light_bvh_nodes);
        this->light_bvh_bit_trails.buffer    = std::move(new_light_bvh_bit_trails);
        this->face_materials.buffer          = std::move(new_face_materials);
        this->media.buffer                   = std::move(new_media);
        this->volumes.buffer                 = std::move(new_volumes);
        this->volume_gpu_data                = std::move(new_volume_gpu_data);
        this->spectra.buffer                 = std::move(new_spectra);
        this->piecewise_spectra.buffer       = std::move(new_piecewise_spectra);
        GpuBuffer new_bindings               = this->runtime->create_buffer(sizeof(PathSceneBindings), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const PathSceneBindings bindings{
            this->primitives.descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::Header)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::Diffuse)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::DiffuseTransmission)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::Conductor)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::Dielectric)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::ThinDielectric)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::CoatedDiffuse)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::CoatedConductor)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::Mix)].descriptor,
            this->materials[static_cast<std::size_t>(PathMaterialTable::TextureRequest)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Header)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Mapping)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Constant)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Image)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Checkerboard)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Scale)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Mix)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::DirectionMix)].descriptor,
            this->textures[static_cast<std::size_t>(PathTextureTable::Bilerp)].descriptor,
            this->light_table.descriptor,
            this->light_shapes.descriptor,
            this->light_distribution.descriptor,
            this->light_distribution_data.descriptor,
            this->portals.descriptor,
            this->light_bvh_nodes.descriptor,
            this->light_bvh_bit_trails.descriptor,
            this->face_materials.descriptor,
            this->media.descriptor,
            this->volumes.descriptor,
            this->spectra.descriptor,
            this->piecewise_spectra.descriptor,
            this->cie_spectra.descriptor,
            this->rgb_to_spectrum.descriptor,
            {static_cast<std::uint32_t>(texture_count), maximum_texture_stack_size, 0, maximum_material_texture_requests},
            {static_cast<std::uint32_t>(scene.transport.light_sampler), light_bvh_node_count, light_bvh_infinite_count, 0},
        };
        std::memcpy(new_bindings.mapped, &bindings, sizeof(bindings));
        if (*this->bindings_table.buffer.buffer) {
            const DescriptorHandle descriptor = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(descriptor, vk::DescriptorType::eStorageBuffer, new_bindings);
            this->runtime->release_resource_descriptor(this->bindings_table.descriptor);
            this->runtime->defer([buffer = std::move(this->bindings_table.buffer)]() mutable {});
            this->bindings_table.descriptor = descriptor;
        } else
            this->runtime->write_buffer(this->bindings_table.descriptor, vk::DescriptorType::eStorageBuffer, new_bindings);
        this->bindings_table.buffer   = std::move(new_bindings);
        this->texture_stack_size      = maximum_texture_stack_size;
        this->material_texture_values = maximum_material_texture_requests;
        this->compiled_bounds         = {bounds.minimum, bounds.maximum};
        this->compiled_instance_transforms.clear();
        this->compiled_instance_transforms.reserve(scene.resources.instances.size());
        for (const scene::Instance& instance : scene.resources.instances) this->compiled_instance_transforms.push_back(instance.transform);
    }


    void PathScene::update_volumes(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.resources.volumes.size() != this->volume_gpu_data.size()) {
            this->compile(scene, &command_buffer);
            return;
        }
        for (std::size_t index = 0; index != scene.resources.volumes.size(); ++index) {
            const scene::Volume& volume     = scene.resources.volumes[index];
            VolumeGpuData& gpu_data         = this->volume_gpu_data[index];
            const render::GpuVolume& shared = this->gpu_scene->volumes[index];
            if (volume.id != gpu_data.id || shared.id != gpu_data.id) {
                this->compile(scene, &command_buffer);
                return;
            }
            if (shared.revision.content == gpu_data.revision.content) continue;
            if (shared.revision.topology != gpu_data.revision.topology || !shared.dirty_region) {
                this->compile(scene, &command_buffer);
                return;
            }
            const auto descriptor = [this, &shared](const render::GpuVolumeField field) {
                const std::size_t field_index = std::to_underlying(field);
                return shared.present[field_index] ? shared.descriptors[field_index] : this->volume_zero.descriptor;
            };
            DescriptorHandle density_descriptor = this->volume_zero.descriptor;
            DescriptorHandle sigma_a_descriptor = this->volume_zero.descriptor;
            DescriptorHandle sigma_s_descriptor = this->volume_zero.descriptor;
            std::uint32_t majorant_mode{};
            std::uint32_t majorant_flags{};
            const scene::UInt3 resolution = shared.resolution;
            if (std::holds_alternative<scene::DensityGridVolume>(volume.data)) {
                density_descriptor = descriptor(render::GpuVolumeField::Density);
            } else if (std::holds_alternative<scene::RgbGridVolume>(volume.data)) {
                majorant_mode = 1;
                if (shared.present[std::to_underlying(render::GpuVolumeField::SigmaA)]) {
                    sigma_a_descriptor = descriptor(render::GpuVolumeField::SigmaA);
                    majorant_flags |= 1u;
                }
                if (shared.present[std::to_underlying(render::GpuVolumeField::SigmaS)]) {
                    sigma_s_descriptor = descriptor(render::GpuVolumeField::SigmaS);
                    majorant_flags |= 2u;
                }
            } else {
                this->compile(scene, &command_buffer);
                return;
            }
            const scene::VolumeRegion dirty_region = shared.revision.content == gpu_data.revision.content + 1u ? *shared.dirty_region : scene::VolumeRegion{{}, resolution};
            const scene::VolumeRegion expanded{
                {
                    dirty_region.minimum.x > 0 ? dirty_region.minimum.x - 1 : 0,
                    dirty_region.minimum.y > 0 ? dirty_region.minimum.y - 1 : 0,
                    dirty_region.minimum.z > 0 ? dirty_region.minimum.z - 1 : 0,
                },
                {
                    std::min(resolution.x, dirty_region.maximum.x + 1),
                    std::min(resolution.y, dirty_region.maximum.y + 1),
                    std::min(resolution.z, dirty_region.maximum.z + 1),
                },
            };
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
            const MajorantPushData push_data{density_descriptor, sigma_a_descriptor, sigma_s_descriptor, gpu_data.majorant.descriptor, {resolution.x, resolution.y, resolution.z, 0}, brick_minimum, brick_maximum, {majorant_mode, majorant_flags, 0, 0}};
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->volume_majorant_shader);
            this->runtime->bind_descriptor_heaps(command_buffer);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            const std::uint32_t brick_count = (brick_maximum[0] - brick_minimum[0]) * (brick_maximum[1] - brick_minimum[1]) * (brick_maximum[2] - brick_minimum[2]);
            command_buffer.dispatch((brick_count + 63u) / 64u, 1, 1);
            const vk::MemoryBarrier2 majorant_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &majorant_dependency});
            gpu_data.revision = shared.revision;
        }
    }

    void PathScene::synchronize(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.value == this->compiled_revision.value) return;
        bool compiled{};
        if ((scene.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Transport)) != scene::SceneChange::None) {
            this->compile(scene, &command_buffer);
            compiled = true;
        } else if ((scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            const scene::Bounds3 bounds         = scene.bounds();
            bool transform_dependencies_changed = scene.resources.instances.size() != this->compiled_instance_transforms.size() || bounds.minimum.x != this->compiled_bounds.minimum.x || bounds.minimum.y != this->compiled_bounds.minimum.y || bounds.minimum.z != this->compiled_bounds.minimum.z || bounds.maximum.x != this->compiled_bounds.maximum.x || bounds.maximum.y != this->compiled_bounds.maximum.y || bounds.maximum.z != this->compiled_bounds.maximum.z;
            if (!transform_dependencies_changed)
                for (std::size_t index = 0; index != scene.resources.instances.size(); ++index) {
                    const scene::Instance& instance = scene.resources.instances[index];
                    if (instance.transform == this->compiled_instance_transforms[index]) continue;
                    const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                    if (std::ranges::any_of(prototype.primitives, [](const scene::Primitive& primitive) { return primitive.area_light.value != 0; })) {
                        transform_dependencies_changed = true;
                        break;
                    }
                }
            if (transform_dependencies_changed) {
                this->compile(scene, &command_buffer);
                compiled = true;
            } else {
                this->compiled_bounds = {bounds.minimum, bounds.maximum};
                this->compiled_instance_transforms.clear();
                this->compiled_instance_transforms.reserve(scene.resources.instances.size());
                for (const scene::Instance& instance : scene.resources.instances) this->compiled_instance_transforms.push_back(instance.transform);
            }
        }
        if (!compiled && (scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(scene, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Film) != scene::SceneChange::None) this->compile_filter(scene.film, &command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Sampler) != scene::SceneChange::None) this->compile_sampler(scene.sampler, &command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Transport) != scene::SceneChange::None) this->transport_settings = scene.transport;
        if ((scene.revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None) {
            this->scene_camera = scene.camera;
            if (scene.camera.medium.value == 0)
                this->camera_medium_index = invalid_path_index;
            else
                this->camera_medium_index = static_cast<std::uint32_t>(std::ranges::find(scene.resources.media, scene.camera.medium, &scene::Medium::id) - scene.resources.media.begin());
        }
        this->compiled_revision = scene.revision;
    }

    PathRenderSession::PathRenderSession(Spectra& runtime, const std::uint32_t frames_in_flight)
        : runtime(&runtime), frame_count(frames_in_flight), output_descriptor(runtime.allocate_resource_descriptor()), sampled_output_descriptor(runtime.allocate_resource_descriptor()), queue_counts(runtime.allocate_resource_descriptor()), ray_queue_0(runtime.allocate_resource_descriptor()), ray_queue_1(runtime.allocate_resource_descriptor()), ray_origins(runtime.allocate_resource_descriptor()), ray_directions(runtime.allocate_resource_descriptor()), ray_origin_dx(runtime.allocate_resource_descriptor()), ray_origin_dy(runtime.allocate_resource_descriptor()), ray_direction_dx(runtime.allocate_resource_descriptor()), ray_direction_dy(runtime.allocate_resource_descriptor()), throughputs(runtime.allocate_resource_descriptor()), radiances(runtime.allocate_resource_descriptor()), wavelengths(runtime.allocate_resource_descriptor()), wavelength_pdfs(runtime.allocate_resource_descriptor()), path_r_u(runtime.allocate_resource_descriptor()), path_r_l(runtime.allocate_resource_descriptor()), current_media(runtime.allocate_resource_descriptor()),
          light_context_normals(runtime.allocate_resource_descriptor()), path_flags(runtime.allocate_resource_descriptor()), eta_scales(runtime.allocate_resource_descriptor()), hit_normal_distances(runtime.allocate_resource_descriptor()), hit_geometric_normal_u(runtime.allocate_resource_descriptor()), hit_tangent_v(runtime.allocate_resource_descriptor()), hit_dpdu(runtime.allocate_resource_descriptor()), hit_dpdv(runtime.allocate_resource_descriptor()), hit_dndu(runtime.allocate_resource_descriptor()), hit_dndv(runtime.allocate_resource_descriptor()), hit_identifiers(runtime.allocate_resource_descriptor()), shadow_path_ids(runtime.allocate_resource_descriptor()), shadow_origins(runtime.allocate_resource_descriptor()), shadow_directions(runtime.allocate_resource_descriptor()), shadow_contributions(runtime.allocate_resource_descriptor()), shadow_r_p(runtime.allocate_resource_descriptor()), shadow_pdfs(runtime.allocate_resource_descriptor()), shadow_media(runtime.allocate_resource_descriptor()), texture_evaluation_stack(runtime.allocate_resource_descriptor()),
          evaluated_texture_values(runtime.allocate_resource_descriptor()), filter_weights(runtime.allocate_resource_descriptor()), film_rgb_sums(runtime.allocate_resource_descriptor()), film_weight_sums(runtime.allocate_resource_descriptor()), gbuffer_sample_albedo(runtime.allocate_resource_descriptor()), gbuffer_sample_shading_normal(runtime.allocate_resource_descriptor()), gbuffer_sample_geometric_normal(runtime.allocate_resource_descriptor()), gbuffer_sample_position_depth(runtime.allocate_resource_descriptor()), gbuffer_sample_uv(runtime.allocate_resource_descriptor()), gbuffer_sample_identity_0(runtime.allocate_resource_descriptor()), gbuffer_sample_identity_1(runtime.allocate_resource_descriptor()), gbuffer_albedo_sums(runtime.allocate_resource_descriptor()), gbuffer_shading_normal_sums(runtime.allocate_resource_descriptor()), gbuffer_geometric_normal_sums(runtime.allocate_resource_descriptor()), gbuffer_position_depth_sums(runtime.allocate_resource_descriptor()), gbuffer_uv_weight_sums(runtime.allocate_resource_descriptor()),
          gbuffer_identity_0(runtime.allocate_resource_descriptor()), gbuffer_identity_1(runtime.allocate_resource_descriptor()), bindings(runtime.allocate_resource_descriptor()) {
        this->parameters.reserve(frames_in_flight);
        for (std::uint32_t index = 0; index != frames_in_flight; ++index) {
            this->parameters.emplace_back(runtime.allocate_resource_descriptor());
            this->parameters.back().buffer = runtime.create_buffer(sizeof(WavefrontParameters), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            runtime.write_buffer(this->parameters.back().descriptor, vk::DescriptorType::eStorageBuffer, this->parameters.back().buffer);
        }
        this->bindings.buffer = runtime.create_buffer(sizeof(WavefrontBindings), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        runtime.write_buffer(this->bindings.descriptor, vk::DescriptorType::eStorageBuffer, this->bindings.buffer);
        const WavefrontBindings binding_data{
            this->output_descriptor,
            this->queue_counts.descriptor,
            this->ray_queue_0.descriptor,
            this->ray_queue_1.descriptor,
            this->ray_origins.descriptor,
            this->ray_directions.descriptor,
            this->ray_origin_dx.descriptor,
            this->ray_origin_dy.descriptor,
            this->ray_direction_dx.descriptor,
            this->ray_direction_dy.descriptor,
            this->throughputs.descriptor,
            this->radiances.descriptor,
            this->wavelengths.descriptor,
            this->wavelength_pdfs.descriptor,
            this->path_r_u.descriptor,
            this->path_r_l.descriptor,
            this->current_media.descriptor,
            this->light_context_normals.descriptor,
            this->path_flags.descriptor,
            this->eta_scales.descriptor,
            this->hit_normal_distances.descriptor,
            this->hit_geometric_normal_u.descriptor,
            this->hit_tangent_v.descriptor,
            this->hit_dpdu.descriptor,
            this->hit_dpdv.descriptor,
            this->hit_dndu.descriptor,
            this->hit_dndv.descriptor,
            this->hit_identifiers.descriptor,
            this->shadow_path_ids.descriptor,
            this->shadow_origins.descriptor,
            this->shadow_directions.descriptor,
            this->shadow_contributions.descriptor,
            this->shadow_r_p.descriptor,
            this->shadow_pdfs.descriptor,
            this->shadow_media.descriptor,
            this->texture_evaluation_stack.descriptor,
            this->evaluated_texture_values.descriptor,
            this->filter_weights.descriptor,
            this->film_rgb_sums.descriptor,
            this->film_weight_sums.descriptor,
            this->gbuffer_sample_albedo.descriptor,
            this->gbuffer_sample_shading_normal.descriptor,
            this->gbuffer_sample_geometric_normal.descriptor,
            this->gbuffer_sample_position_depth.descriptor,
            this->gbuffer_sample_uv.descriptor,
            this->gbuffer_sample_identity_0.descriptor,
            this->gbuffer_sample_identity_1.descriptor,
            this->gbuffer_albedo_sums.descriptor,
            this->gbuffer_shading_normal_sums.descriptor,
            this->gbuffer_geometric_normal_sums.descriptor,
            this->gbuffer_position_depth_sums.descriptor,
            this->gbuffer_uv_weight_sums.descriptor,
            this->gbuffer_identity_0.descriptor,
            this->gbuffer_identity_1.descriptor,
        };
        std::memcpy(this->bindings.buffer.mapped, &binding_data, sizeof(binding_data));
    }

    PathRenderSession::~PathRenderSession() {
        this->runtime->release_resource_descriptor(this->output_descriptor);
        this->runtime->release_resource_descriptor(this->sampled_output_descriptor);
        this->runtime->release_resource_descriptor(this->queue_counts.descriptor);
        this->runtime->release_resource_descriptor(this->ray_queue_0.descriptor);
        this->runtime->release_resource_descriptor(this->ray_queue_1.descriptor);
        this->runtime->release_resource_descriptor(this->ray_origins.descriptor);
        this->runtime->release_resource_descriptor(this->ray_directions.descriptor);
        this->runtime->release_resource_descriptor(this->ray_origin_dx.descriptor);
        this->runtime->release_resource_descriptor(this->ray_origin_dy.descriptor);
        this->runtime->release_resource_descriptor(this->ray_direction_dx.descriptor);
        this->runtime->release_resource_descriptor(this->ray_direction_dy.descriptor);
        this->runtime->release_resource_descriptor(this->throughputs.descriptor);
        this->runtime->release_resource_descriptor(this->radiances.descriptor);
        this->runtime->release_resource_descriptor(this->wavelengths.descriptor);
        this->runtime->release_resource_descriptor(this->wavelength_pdfs.descriptor);
        this->runtime->release_resource_descriptor(this->path_r_u.descriptor);
        this->runtime->release_resource_descriptor(this->path_r_l.descriptor);
        this->runtime->release_resource_descriptor(this->current_media.descriptor);
        this->runtime->release_resource_descriptor(this->light_context_normals.descriptor);
        this->runtime->release_resource_descriptor(this->path_flags.descriptor);
        this->runtime->release_resource_descriptor(this->eta_scales.descriptor);
        this->runtime->release_resource_descriptor(this->hit_normal_distances.descriptor);
        this->runtime->release_resource_descriptor(this->hit_geometric_normal_u.descriptor);
        this->runtime->release_resource_descriptor(this->hit_tangent_v.descriptor);
        this->runtime->release_resource_descriptor(this->hit_dpdu.descriptor);
        this->runtime->release_resource_descriptor(this->hit_dpdv.descriptor);
        this->runtime->release_resource_descriptor(this->hit_dndu.descriptor);
        this->runtime->release_resource_descriptor(this->hit_dndv.descriptor);
        this->runtime->release_resource_descriptor(this->hit_identifiers.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_path_ids.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_origins.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_directions.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_contributions.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_r_p.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_pdfs.descriptor);
        this->runtime->release_resource_descriptor(this->shadow_media.descriptor);
        this->runtime->release_resource_descriptor(this->texture_evaluation_stack.descriptor);
        this->runtime->release_resource_descriptor(this->evaluated_texture_values.descriptor);
        this->runtime->release_resource_descriptor(this->filter_weights.descriptor);
        this->runtime->release_resource_descriptor(this->film_rgb_sums.descriptor);
        this->runtime->release_resource_descriptor(this->film_weight_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_albedo.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_shading_normal.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_geometric_normal.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_position_depth.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_uv.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_identity_0.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_sample_identity_1.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_albedo_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_shading_normal_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_geometric_normal_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_position_depth_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_uv_weight_sums.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_identity_0.descriptor);
        this->runtime->release_resource_descriptor(this->gbuffer_identity_1.descriptor);
        this->runtime->release_resource_descriptor(this->bindings.descriptor);
        for (const GpuBufferBinding& binding : this->parameters) this->runtime->release_resource_descriptor(binding.descriptor);
    }

    void PathRenderSession::resize(const vk::Extent2D extent, const std::uint32_t texture_evaluation_stack_size, const std::uint32_t material_texture_value_count) {
        const std::uint32_t texture_stack_size  = std::max(texture_evaluation_stack_size, 1u);
        const std::uint32_t texture_value_count = std::max(material_texture_value_count, 1u);
        if (this->extent == extent && this->texture_stack_size == texture_stack_size && this->texture_value_count == texture_value_count) return;
        if (this->capacity != 0) this->runtime->wait_idle();
        this->extent              = extent;
        this->texture_stack_size  = texture_stack_size;
        this->texture_value_count = texture_value_count;
        this->capacity            = extent.width * extent.height;
        this->output_image        = this->runtime->create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);
        this->runtime->write_storage_image(this->output_descriptor, this->output_image, vk::ImageLayout::eGeneral);
        this->runtime->write_sampled_image(this->sampled_output_descriptor, this->output_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->queue_counts.buffer                    = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 3, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);
        this->ray_queue_0.buffer                     = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->ray_queue_1.buffer                     = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->ray_origins.buffer                     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->ray_directions.buffer                  = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->ray_origin_dx.buffer                   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->ray_origin_dy.buffer                   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->ray_direction_dx.buffer                = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->ray_direction_dy.buffer                = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->throughputs.buffer                     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->radiances.buffer                       = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->wavelengths.buffer                     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->wavelength_pdfs.buffer                 = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->path_r_u.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->path_r_l.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->current_media.buffer                   = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->light_context_normals.buffer           = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->path_flags.buffer                      = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->eta_scales.buffer                      = create_storage_buffer(*this->runtime, sizeof(float) * this->capacity);
        this->hit_normal_distances.buffer            = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_geometric_normal_u.buffer          = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_tangent_v.buffer                   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_dpdu.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_dpdv.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_dndu.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_dndv.buffer                        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->hit_identifiers.buffer                 = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 4 * this->capacity);
        this->shadow_path_ids.buffer                 = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->shadow_origins.buffer                  = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->shadow_directions.buffer               = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->shadow_contributions.buffer            = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->shadow_r_p.buffer                      = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->shadow_pdfs.buffer                     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->shadow_media.buffer                    = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * this->capacity);
        this->texture_evaluation_stack.buffer        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity * this->texture_stack_size);
        this->evaluated_texture_values.buffer        = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity * this->texture_value_count);
        this->filter_weights.buffer                  = create_storage_buffer(*this->runtime, sizeof(float) * this->capacity);
        this->film_rgb_sums.buffer                   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->film_weight_sums.buffer                = create_storage_buffer(*this->runtime, sizeof(float) * this->capacity);
        this->gbuffer_sample_albedo.buffer           = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_shading_normal.buffer   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_geometric_normal.buffer = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_position_depth.buffer   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_uv.buffer               = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_identity_0.buffer       = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 4 * this->capacity);
        this->gbuffer_sample_identity_1.buffer       = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 4 * this->capacity);
        this->gbuffer_albedo_sums.buffer             = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_shading_normal_sums.buffer     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_geometric_normal_sums.buffer   = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_position_depth_sums.buffer     = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_uv_weight_sums.buffer          = create_storage_buffer(*this->runtime, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_identity_0.buffer              = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_identity_1.buffer              = create_storage_buffer(*this->runtime, sizeof(std::uint32_t) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->indirect_commands                      = this->runtime->create_buffer(sizeof(vk::TraceRaysIndirectCommand2KHR) * 2, vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);

        this->runtime->write_buffer(this->queue_counts.descriptor, vk::DescriptorType::eStorageBuffer, this->queue_counts.buffer);
        this->runtime->write_buffer(this->ray_queue_0.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_queue_0.buffer);
        this->runtime->write_buffer(this->ray_queue_1.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_queue_1.buffer);
        this->runtime->write_buffer(this->ray_origins.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origins.buffer);
        this->runtime->write_buffer(this->ray_directions.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_directions.buffer);
        this->runtime->write_buffer(this->ray_origin_dx.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origin_dx.buffer);
        this->runtime->write_buffer(this->ray_origin_dy.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origin_dy.buffer);
        this->runtime->write_buffer(this->ray_direction_dx.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_direction_dx.buffer);
        this->runtime->write_buffer(this->ray_direction_dy.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_direction_dy.buffer);
        this->runtime->write_buffer(this->throughputs.descriptor, vk::DescriptorType::eStorageBuffer, this->throughputs.buffer);
        this->runtime->write_buffer(this->radiances.descriptor, vk::DescriptorType::eStorageBuffer, this->radiances.buffer);
        this->runtime->write_buffer(this->wavelengths.descriptor, vk::DescriptorType::eStorageBuffer, this->wavelengths.buffer);
        this->runtime->write_buffer(this->wavelength_pdfs.descriptor, vk::DescriptorType::eStorageBuffer, this->wavelength_pdfs.buffer);
        this->runtime->write_buffer(this->path_r_u.descriptor, vk::DescriptorType::eStorageBuffer, this->path_r_u.buffer);
        this->runtime->write_buffer(this->path_r_l.descriptor, vk::DescriptorType::eStorageBuffer, this->path_r_l.buffer);
        this->runtime->write_buffer(this->current_media.descriptor, vk::DescriptorType::eStorageBuffer, this->current_media.buffer);
        this->runtime->write_buffer(this->light_context_normals.descriptor, vk::DescriptorType::eStorageBuffer, this->light_context_normals.buffer);
        this->runtime->write_buffer(this->path_flags.descriptor, vk::DescriptorType::eStorageBuffer, this->path_flags.buffer);
        this->runtime->write_buffer(this->eta_scales.descriptor, vk::DescriptorType::eStorageBuffer, this->eta_scales.buffer);
        this->runtime->write_buffer(this->hit_normal_distances.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_normal_distances.buffer);
        this->runtime->write_buffer(this->hit_geometric_normal_u.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_geometric_normal_u.buffer);
        this->runtime->write_buffer(this->hit_tangent_v.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_tangent_v.buffer);
        this->runtime->write_buffer(this->hit_dpdu.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dpdu.buffer);
        this->runtime->write_buffer(this->hit_dpdv.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dpdv.buffer);
        this->runtime->write_buffer(this->hit_dndu.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dndu.buffer);
        this->runtime->write_buffer(this->hit_dndv.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dndv.buffer);
        this->runtime->write_buffer(this->hit_identifiers.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_identifiers.buffer);
        this->runtime->write_buffer(this->shadow_path_ids.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_path_ids.buffer);
        this->runtime->write_buffer(this->shadow_origins.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_origins.buffer);
        this->runtime->write_buffer(this->shadow_directions.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_directions.buffer);
        this->runtime->write_buffer(this->shadow_contributions.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_contributions.buffer);
        this->runtime->write_buffer(this->shadow_r_p.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_r_p.buffer);
        this->runtime->write_buffer(this->shadow_pdfs.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_pdfs.buffer);
        this->runtime->write_buffer(this->shadow_media.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_media.buffer);
        this->runtime->write_buffer(this->texture_evaluation_stack.descriptor, vk::DescriptorType::eStorageBuffer, this->texture_evaluation_stack.buffer);
        this->runtime->write_buffer(this->evaluated_texture_values.descriptor, vk::DescriptorType::eStorageBuffer, this->evaluated_texture_values.buffer);
        this->runtime->write_buffer(this->filter_weights.descriptor, vk::DescriptorType::eStorageBuffer, this->filter_weights.buffer);
        this->runtime->write_buffer(this->film_rgb_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->film_rgb_sums.buffer);
        this->runtime->write_buffer(this->film_weight_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->film_weight_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_albedo.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_albedo.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_shading_normal.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_shading_normal.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_geometric_normal.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_geometric_normal.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_position_depth.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_position_depth.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_uv.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_uv.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_identity_0.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_identity_0.buffer);
        this->runtime->write_buffer(this->gbuffer_sample_identity_1.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_identity_1.buffer);
        this->runtime->write_buffer(this->gbuffer_albedo_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_albedo_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_shading_normal_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_shading_normal_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_geometric_normal_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_geometric_normal_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_position_depth_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_position_depth_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_uv_weight_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_uv_weight_sums.buffer);
        this->runtime->write_buffer(this->gbuffer_identity_0.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_identity_0.buffer);
        this->runtime->write_buffer(this->gbuffer_identity_1.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_identity_1.buffer);
        this->output_layout                = vk::ImageLayout::eUndefined;
        this->sample_index                 = 0;
        this->indirect_commands_configured = false;
    }

    void PathRenderSession::reset() noexcept {
        this->sample_index = 0;
    }

    std::vector<float> PathRenderSession::read_radiance() {
        const vk::DeviceSize readback_size = static_cast<vk::DeviceSize>(this->extent.width) * this->extent.height * sizeof(float) * 4u;
        GpuBuffer readback                 = this->runtime->create_buffer(readback_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->runtime->immediate([this, &readback](const vk::raii::CommandBuffer& command_buffer) {
            const vk::ImageMemoryBarrier2 to_transfer{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
            const vk::BufferImageCopy copy{
                0,
                0,
                0,
                {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {this->extent.width, this->extent.height, 1},
            };
            command_buffer.copyImageToBuffer(*this->output_image.image, vk::ImageLayout::eTransferSrcOptimal, *readback.buffer, copy);
            const vk::ImageMemoryBarrier2 to_general{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_general});
        });
        std::vector<float> pixels(static_cast<std::size_t>(this->extent.width) * this->extent.height * 4u);
        std::memcpy(pixels.data(), readback.mapped, readback_size);
        return pixels;
    }

    render::RenderReadback PathRenderSession::readback() {
        render::RenderReadback result{
            .extent              = this->extent,
            .accumulated_samples = this->sample_index,
        };
        const std::size_t pixel_count = this->capacity;
        result.radiance.resize(pixel_count);
        result.albedo.resize(pixel_count);
        result.shading_normals.resize(pixel_count);
        result.geometric_normals.resize(pixel_count);
        result.positions.resize(pixel_count);
        result.depths.resize(pixel_count);
        result.texture_coordinates.resize(pixel_count);
        result.object_ids.resize(pixel_count);
        result.primitive_ids.resize(pixel_count);
        result.material_ids.resize(pixel_count);
        result.valid.resize(pixel_count);
        if (this->sample_index == 0) return result;

        const std::vector<float> radiance = this->read_radiance();
        static_assert(sizeof(scene::Float4) == sizeof(float) * 4);
        std::memcpy(result.radiance.data(), radiance.data(), radiance.size() * sizeof(float));

        constexpr std::size_t buffer_count = 7;
        const vk::DeviceSize buffer_size   = static_cast<vk::DeviceSize>(pixel_count) * sizeof(scene::Float4);
        GpuBuffer readback                 = this->runtime->create_buffer(buffer_size * buffer_count, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const std::array<const GpuBuffer*, buffer_count> sources{
            &this->gbuffer_albedo_sums.buffer,
            &this->gbuffer_shading_normal_sums.buffer,
            &this->gbuffer_geometric_normal_sums.buffer,
            &this->gbuffer_position_depth_sums.buffer,
            &this->gbuffer_uv_weight_sums.buffer,
            &this->gbuffer_identity_0.buffer,
            &this->gbuffer_identity_1.buffer,
        };
        this->runtime->immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            const vk::MemoryBarrier2 to_transfer{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &to_transfer});
            for (std::size_t index = 0; index != sources.size(); ++index)
                command_buffer.copyBuffer(*sources[index]->buffer, *readback.buffer,
                    vk::BufferCopy{
                        0,
                        buffer_size * index,
                        buffer_size,
                    });
        });
        const auto float_buffer = [&](const std::size_t index) {
            return std::span<const scene::Float4>{
                reinterpret_cast<const scene::Float4*>(static_cast<const std::byte*>(readback.mapped) + buffer_size * index),
                pixel_count,
            };
        };
        const auto integer_buffer = [&](const std::size_t index) {
            return std::span<const std::array<std::uint32_t, 4>>{
                reinterpret_cast<const std::array<std::uint32_t, 4>*>(static_cast<const std::byte*>(readback.mapped) + buffer_size * index),
                pixel_count,
            };
        };
        const std::span<const scene::Float4> albedo_sums               = float_buffer(0);
        const std::span<const scene::Float4> shading_normal_sums       = float_buffer(1);
        const std::span<const scene::Float4> geometric_normal_sums     = float_buffer(2);
        const std::span<const scene::Float4> position_depth_sums       = float_buffer(3);
        const std::span<const scene::Float4> uv_weight_sums            = float_buffer(4);
        const std::span<const std::array<std::uint32_t, 4>> identity_0 = integer_buffer(5);
        const std::span<const std::array<std::uint32_t, 4>> identity_1 = integer_buffer(6);
        const auto normalize                                           = [](const scene::Float4 value) {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            return length == 0.0f ? scene::Float3{} : scene::Float3{value.x / length, value.y / length, value.z / length};
        };
        for (std::size_t index = 0; index != pixel_count; ++index) {
            const float weight = uv_weight_sums[index].z;
            if (weight != 0.0f) {
                result.albedo[index] = {
                    albedo_sums[index].x / weight,
                    albedo_sums[index].y / weight,
                    albedo_sums[index].z / weight,
                };
                result.shading_normals[index]   = normalize(shading_normal_sums[index]);
                result.geometric_normals[index] = normalize(geometric_normal_sums[index]);
                result.positions[index]         = {
                    position_depth_sums[index].x / weight,
                    position_depth_sums[index].y / weight,
                    position_depth_sums[index].z / weight,
                };
                result.depths[index]              = position_depth_sums[index].w / weight;
                result.texture_coordinates[index] = {
                    uv_weight_sums[index].x / weight,
                    uv_weight_sums[index].y / weight,
                };
            }
            result.valid[index]         = identity_1[index][2] != 0 ? 1u : 0u;
            result.object_ids[index]    = static_cast<std::uint64_t>(identity_0[index][0]) | static_cast<std::uint64_t>(identity_0[index][1]) << 32;
            result.primitive_ids[index] = identity_0[index][2];
            result.material_ids[index]  = static_cast<std::uint64_t>(identity_0[index][3]) | static_cast<std::uint64_t>(identity_1[index][0]) << 32;
        }
        return result;
    }

    render::RenderOutput PathRenderSession::output() const noexcept {
        return {
            this->output_image,
            this->sampled_output_descriptor,
            vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
        };
    }

    namespace {
        static_assert(sizeof(vk::TraceRaysIndirectCommand2KHR) == 104);
        constexpr vk::DeviceSize indirect_width_offset = sizeof(vk::DeviceAddress) * 11;

        [[nodiscard]] constexpr vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        [[nodiscard]] vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::filesystem::path& path) {
            const std::vector<std::uint32_t> words = render::load_spirv(path);
            return vk::raii::ShaderModule{device, vk::ShaderModuleCreateInfo{{}, words.size() * sizeof(std::uint32_t), words.data()}};
        }

        [[nodiscard]] vk::raii::ShaderEXT create_compute_shader(const Spectra& runtime, const std::filesystem::path& path, const char* entry) {
            const std::vector<std::uint32_t> code = render::load_spirv(path);
            const vk::ShaderCreateInfoEXT create_info{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eCompute,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                code.size() * sizeof(std::uint32_t),
                code.data(),
                entry,
            };
            return vk::raii::ShaderEXT{runtime.device, create_info};
        }
    } // namespace

    WavefrontIntegrator::WavefrontIntegrator(Spectra& runtime, const std::filesystem::path& shader_directory)
        : runtime(&runtime), shader_directory(shader_directory), generate_camera_rays(create_compute_shader(runtime, shader_directory / "path_generate_camera_rays.spv", "generate_camera_rays")), evaluate_surface_textures(create_compute_shader(runtime, shader_directory / "path_evaluate_surface_textures.spv", "evaluate_surface_textures")), record_surface_gbuffer(create_compute_shader(runtime, shader_directory / "path_record_surface_gbuffer.spv", "record_surface_gbuffer")), sample_direct_lighting(create_compute_shader(runtime, shader_directory / "path_sample_direct_lighting.spv", "sample_direct_lighting")), shade_surfaces(create_compute_shader(runtime, shader_directory / "path_shade_surfaces.spv", "shade_surfaces")), resolve_visibility(create_compute_shader(runtime, shader_directory / "path_resolve_visibility.spv", "resolve_visibility")), accumulate_film(create_compute_shader(runtime, shader_directory / "path_accumulate_film.spv", "accumulate_film")) {
        this->create_ray_tracing_pipeline(shader_directory);
        this->create_shader_binding_table();
    }

    WavefrontIntegrator::~WavefrontIntegrator() = default;

    void WavefrontIntegrator::create_ray_tracing_pipeline(const std::filesystem::path& shader_directory) {
        const vk::raii::ShaderModule surface_ray_generation           = create_shader_module(this->runtime->device, shader_directory / "path_surface_ray_generation.spv");
        const vk::raii::ShaderModule shadow_ray_generation            = create_shader_module(this->runtime->device, shader_directory / "path_shadow_ray_generation.spv");
        const vk::raii::ShaderModule radiance_miss                    = create_shader_module(this->runtime->device, shader_directory / "path_radiance_miss.spv");
        const vk::raii::ShaderModule shadow_miss                      = create_shader_module(this->runtime->device, shader_directory / "path_shadow_miss.spv");
        const vk::raii::ShaderModule closest_hit                      = create_shader_module(this->runtime->device, shader_directory / "path_closest_hit.spv");
        const vk::raii::ShaderModule alpha_any_hit_surface            = create_shader_module(this->runtime->device, shader_directory / "path_alpha_any_hit_surface.spv");
        const vk::raii::ShaderModule alpha_any_hit_shadow             = create_shader_module(this->runtime->device, shader_directory / "path_alpha_any_hit_shadow.spv");
        const vk::raii::ShaderModule shadow_closest_hit               = create_shader_module(this->runtime->device, shader_directory / "path_shadow_closest_hit.spv");
        const vk::raii::ShaderModule procedural_intersection          = create_shader_module(this->runtime->device, shader_directory / "path_procedural_intersection.spv");
        const vk::raii::ShaderModule procedural_closest_hit           = create_shader_module(this->runtime->device, shader_directory / "path_procedural_closest_hit.spv");
        const vk::raii::ShaderModule procedural_alpha_any_hit_surface = create_shader_module(this->runtime->device, shader_directory / "path_procedural_alpha_any_hit_surface.spv");
        const vk::raii::ShaderModule procedural_alpha_any_hit_shadow  = create_shader_module(this->runtime->device, shader_directory / "path_procedural_alpha_any_hit_shadow.spv");
        const vk::raii::ShaderModule procedural_shadow_closest_hit    = create_shader_module(this->runtime->device, shader_directory / "path_procedural_shadow_closest_hit.spv");

        vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
        acceleration_structure_source.pushAddressOffset = 0;
        const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{
            0,
            0,
            1,
            vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure,
            vk::DescriptorMappingSourceEXT::ePushAddress,
            acceleration_structure_source,
        };
        const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
        vk::PipelineShaderStageCreateInfo surface_ray_generation_stage{
            {},
            vk::ShaderStageFlagBits::eRaygenKHR,
            *surface_ray_generation,
            "surface_ray_generation",
        };
        surface_ray_generation_stage.pNext = &mapping;
        vk::PipelineShaderStageCreateInfo shadow_ray_generation_stage{
            {},
            vk::ShaderStageFlagBits::eRaygenKHR,
            *shadow_ray_generation,
            "shadow_ray_generation",
        };
        shadow_ray_generation_stage.pNext = &mapping;
        const std::array general_stages{
            surface_ray_generation_stage,
            shadow_ray_generation_stage,
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eMissKHR, *radiance_miss, "radiance_miss"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eMissKHR, *shadow_miss, "shadow_miss"},
        };
        const std::array general_groups{
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 2, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 3, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
        };
        const vk::RayTracingPipelineInterfaceCreateInfoKHR pipeline_interface{sizeof(float) * 32u, sizeof(float) * 2u};
        const vk::PipelineCreateFlags2CreateInfo general_flags{
            vk::PipelineCreateFlagBits2::eLibraryKHR | vk::PipelineCreateFlagBits2::eDescriptorHeapEXT,
        };
        const vk::RayTracingPipelineCreateInfoKHR general_create_info{
            {},
            general_stages,
            general_groups,
            1,
            nullptr,
            &pipeline_interface,
            nullptr,
            {},
            {},
            0,
            &general_flags,
        };
        this->ray_generation_library = this->runtime->device.createRayTracingPipelineKHR(nullptr, nullptr, general_create_info);

        const std::array hit_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *closest_hit, "closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *alpha_any_hit_surface, "alpha_any_hit_surface"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *alpha_any_hit_shadow, "alpha_any_hit_shadow"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *shadow_closest_hit, "shadow_closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *procedural_closest_hit, "procedural_closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eIntersectionKHR, *procedural_intersection, "procedural_intersection"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *procedural_alpha_any_hit_surface, "procedural_alpha_any_hit_surface"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *procedural_alpha_any_hit_shadow, "procedural_alpha_any_hit_shadow"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *procedural_shadow_closest_hit, "procedural_shadow_closest_hit"},
        };
        const std::array hit_groups{
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 0, 1, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 4, 6, 5},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 3, 2, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 8, 7, 5},
        };
        const vk::PipelineCreateFlags2CreateInfo hit_flags{
            vk::PipelineCreateFlagBits2::eLibraryKHR | vk::PipelineCreateFlagBits2::eDescriptorHeapEXT,
        };
        const vk::RayTracingPipelineCreateInfoKHR hit_create_info{
            {},
            hit_stages,
            hit_groups,
            1,
            nullptr,
            &pipeline_interface,
            nullptr,
            {},
            {},
            0,
            &hit_flags,
        };
        this->hit_library = this->runtime->device.createRayTracingPipelineKHR(nullptr, nullptr, hit_create_info);

        const std::array<vk::Pipeline, 2> libraries{*this->ray_generation_library, *this->hit_library};
        const vk::PipelineLibraryCreateInfoKHR library_info{libraries};
        constexpr std::array dynamic_states{vk::DynamicState::eRayTracingPipelineStackSizeKHR};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, dynamic_states};
        const vk::PipelineCreateFlags2CreateInfo final_flags{vk::PipelineCreateFlagBits2::eDescriptorHeapEXT};
        const vk::RayTracingPipelineCreateInfoKHR final_create_info{
            {},
            {},
            {},
            1,
            &library_info,
            &pipeline_interface,
            &dynamic_state,
            {},
            {},
            0,
            &final_flags,
        };
        this->pipeline = this->runtime->device.createRayTracingPipelineKHR(nullptr, nullptr, final_create_info);

        const vk::DeviceSize surface_stack                         = this->pipeline.getRayTracingShaderGroupStackSizeKHR(0, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize shadow_stack                          = this->pipeline.getRayTracingShaderGroupStackSizeKHR(1, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize radiance_miss_stack                   = this->pipeline.getRayTracingShaderGroupStackSizeKHR(2, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize shadow_miss_stack                     = this->pipeline.getRayTracingShaderGroupStackSizeKHR(3, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize triangle_surface_closest_hit_stack    = this->pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize triangle_surface_any_hit_stack        = this->pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize procedural_surface_closest_hit_stack  = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize procedural_surface_intersection_stack = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eIntersection);
        const vk::DeviceSize procedural_surface_any_hit_stack      = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize triangle_shadow_closest_hit_stack     = this->pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize triangle_shadow_any_hit_stack         = this->pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize procedural_shadow_closest_hit_stack   = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize procedural_shadow_intersection_stack  = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eIntersection);
        const vk::DeviceSize procedural_shadow_any_hit_stack       = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eAnyHit);
        this->stack_size                                           = static_cast<std::uint32_t>(std::max(surface_stack, shadow_stack) + std::max({radiance_miss_stack, shadow_miss_stack, triangle_surface_closest_hit_stack, triangle_surface_any_hit_stack, procedural_surface_closest_hit_stack, procedural_surface_intersection_stack, procedural_surface_any_hit_stack, triangle_shadow_closest_hit_stack, triangle_shadow_any_hit_stack, procedural_shadow_closest_hit_stack, procedural_shadow_intersection_stack, procedural_shadow_any_hit_stack}));
    }

    void WavefrontIntegrator::create_shader_binding_table() {
        const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& properties = this->runtime->ray_tracing_properties;
        constexpr std::uint32_t group_count                                 = 8;
        const std::vector<std::byte> handles                                = this->pipeline.getRayTracingShaderGroupHandlesKHR<std::byte>(0, group_count, static_cast<std::size_t>(properties.shaderGroupHandleSize) * group_count);
        const vk::DeviceSize record_stride                                  = align_up(properties.shaderGroupHandleSize, properties.shaderGroupHandleAlignment);
        const vk::DeviceSize ray_generation_offset                          = 0;
        const vk::DeviceSize miss_offset                                    = align_up(record_stride * 2u, properties.shaderGroupBaseAlignment);
        const vk::DeviceSize hit_offset                                     = align_up(miss_offset + record_stride * 2u, properties.shaderGroupBaseAlignment);
        const vk::DeviceSize table_size                                     = hit_offset + record_stride * 4u;
        this->shader_binding_table                                          = this->runtime->create_buffer(table_size, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        std::byte* destination                                              = static_cast<std::byte*>(this->shader_binding_table.mapped);
        for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + ray_generation_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * group, properties.shaderGroupHandleSize);
        for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + miss_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * (group + 2), properties.shaderGroupHandleSize);
        for (std::uint32_t group = 0; group != 4; ++group) std::memcpy(destination + hit_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * (group + 4), properties.shaderGroupHandleSize);

        this->surface_ray_generation_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + ray_generation_offset,
            record_stride,
            record_stride,
        };
        this->shadow_ray_generation_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + ray_generation_offset + record_stride,
            record_stride,
            record_stride,
        };
        this->miss_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + miss_offset,
            record_stride,
            record_stride * 2u,
        };
        this->hit_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + hit_offset,
            record_stride,
            record_stride * 4u,
        };
    }

    void WavefrontIntegrator::configure_indirect_commands(PathRenderSession& session) const {
        const std::array commands{
            vk::TraceRaysIndirectCommand2KHR{this->surface_ray_generation_region.deviceAddress, this->surface_ray_generation_region.size, this->miss_region.deviceAddress, this->miss_region.size, this->miss_region.stride, this->hit_region.deviceAddress, this->hit_region.size, this->hit_region.stride, 0, 0, 0, 0, 1, 1},
            vk::TraceRaysIndirectCommand2KHR{this->shadow_ray_generation_region.deviceAddress, this->shadow_ray_generation_region.size, this->miss_region.deviceAddress, this->miss_region.size, this->miss_region.stride, this->hit_region.deviceAddress, this->hit_region.size, this->hit_region.stride, 0, 0, 0, 0, 1, 1},
        };
        std::memcpy(session.indirect_commands.mapped, commands.data(), sizeof(commands));
        session.indirect_commands_configured = true;
    }

    void WavefrontIntegrator::record(const PathScene& scene, PathRenderSession& session, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (!session.indirect_commands_configured) this->configure_indirect_commands(session);
        const scene::CameraResource& camera    = scene.scene_camera;
        const std::array<float, 16>& transform = camera.transform.matrix;
        WavefrontParameters parameters{
            {transform[0], transform[1], transform[2], transform[3]},
            {transform[4], transform[5], transform[6], transform[7]},
            {transform[8], transform[9], transform[10], transform[11]},
        };
        const scene::Transform camera_inverse = camera.transform.inverse();
        parameters.camera_inverse_row_0       = {camera_inverse.matrix[0], camera_inverse.matrix[1], camera_inverse.matrix[2], camera_inverse.matrix[3]};
        parameters.camera_inverse_row_1       = {camera_inverse.matrix[4], camera_inverse.matrix[5], camera_inverse.matrix[6], camera_inverse.matrix[7]};
        parameters.camera_inverse_row_2       = {camera_inverse.matrix[8], camera_inverse.matrix[9], camera_inverse.matrix[10], camera_inverse.matrix[11]};
        std::visit(
            [&parameters](const auto& data) {
                parameters.camera_screen = {data.screen.minimum.x, data.screen.minimum.y, data.screen.maximum.x, data.screen.maximum.y};
                parameters.camera_lens   = {data.lens_radius, data.focal_distance, data.near_plane, data.far_plane};
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                    parameters.camera_projection[0] = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    parameters.camera_metadata[0]   = 0;
                } else
                    parameters.camera_metadata[0] = 1;
            },
            camera.data);
        parameters.camera_metadata[3]          = scene.camera_medium_index;
        const scene::Film& film                = scene.filter.description;
        parameters.filter_parameters_0         = {film.filter.radius.x, film.filter.radius.y, film.filter.sigma, film.exposure};
        parameters.filter_parameters_1         = {film.filter.b, film.filter.c, film.filter.tau, scene.filter.absolute_integral};
        parameters.filter_metadata             = {std::to_underlying(film.filter.kind), scene.filter.resolution[0], scene.filter.resolution[1], 0};
        parameters.filter_distribution         = scene.filter.distribution.descriptor;
        const scene::Sampler& sampler          = scene.sampler.description;
        parameters.sampling_tables             = scene.sampler.tables.descriptor;
        parameters.pmj_pixel_samples           = scene.sampler.pixel_samples.descriptor;
        parameters.sampler_metadata            = {std::to_underlying(sampler.kind), std::to_underlying(sampler.randomization), sampler.samples_per_pixel, sampler.seed};
        parameters.sampler_parameters          = {sampler.x_strata, sampler.y_strata, sampler.jitter ? 1u : 0u, scene.sampler.pixel_tile_size};
        parameters.film_bounds                 = {0, 0, session.extent.width, session.extent.height};
        parameters.film_sensor_response        = scene.filter.sensor_response.descriptor;
        parameters.film_sensor_to_output_row_0 = {film.sensor_to_output_rgb[0], film.sensor_to_output_rgb[1], film.sensor_to_output_rgb[2], 0.0f};
        parameters.film_sensor_to_output_row_1 = {film.sensor_to_output_rgb[3], film.sensor_to_output_rgb[4], film.sensor_to_output_rgb[5], 0.0f};
        parameters.film_sensor_to_output_row_2 = {film.sensor_to_output_rgb[6], film.sensor_to_output_rgb[7], film.sensor_to_output_rgb[8], 0.0f};
        parameters.film_parameters             = {
            camera.exposure_time * film.iso / 100.0f,
            film.maximum_component_value.value_or(std::numeric_limits<float>::infinity()),
            0.0f,
            0.0f,
        };
        parameters.film_metadata = {
            film.gbuffer ? 1u : 0u,
            film.gbuffer_camera_space ? 1u : 0u,
            std::to_underlying(film.color_space),
            0,
        };
        parameters.extent       = {session.extent.width, session.extent.height};
        parameters.sample_index = session.sample_index;
        parameters.max_depth    = scene.transport_settings.maximum_depth;
        parameters.light_count  = scene.lights;
        parameters.reserved     = scene.transport_settings.regularize ? 1u : 0u;
        std::memcpy(session.parameters[frame_index].buffer.mapped, &parameters, sizeof(parameters));
        WavefrontPushData push_data{
            scene.gpu_scene->top_level_acceleration_structure.address,
            session.bindings.descriptor,
            session.parameters[frame_index].descriptor,
            scene.bindings_table.descriptor,
            0,
            0,
        };

        command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, 0, sizeof(std::uint32_t) * 3, 0);
        const vk::MemoryBarrier2 initial_memory{
            vk::PipelineStageFlagBits2::eHost | vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        const vk::ImageMemoryBarrier2 to_general{
            session.output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eComputeShader,
            session.output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            session.output_layout,
            vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *session.output_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &initial_memory, 0, nullptr, 1, &to_general});
        this->runtime->bind_descriptor_heaps(command_buffer);
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->generate_camera_rays);
        const std::uint32_t group_count = (session.capacity + 255u) / 256u;
        command_buffer.dispatch(group_count, 1, 1);

        std::uint32_t read_queue{};
        for (std::uint32_t bounce = 0; bounce <= scene.transport_settings.maximum_depth; ++bounce) {
            push_data.bounce                = bounce;
            push_data.read_queue            = read_queue;
            const std::uint32_t write_queue = 1u - read_queue;
            command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, static_cast<vk::DeviceSize>(write_queue) * sizeof(std::uint32_t), sizeof(std::uint32_t), 0);
            command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, sizeof(std::uint32_t) * 2, sizeof(std::uint32_t), 0);
            const vk::MemoryBarrier2 queue_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &queue_to_copy});
            const vk::BufferCopy surface_width_copy{
                static_cast<vk::DeviceSize>(read_queue) * sizeof(std::uint32_t),
                indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*session.queue_counts.buffer.buffer, *session.indirect_commands.buffer, surface_width_copy);
            const vk::MemoryBarrier2 surface_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->pipeline);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->stack_size);
            command_buffer.traceRaysIndirect2KHR(session.indirect_commands.address);

            const vk::MemoryBarrier2 surface_to_shade{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_to_shade});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->evaluate_surface_textures);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            const vk::MemoryBarrier2 texture_values_ready{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &texture_values_ready});
            if (film.gbuffer) {
                command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->record_surface_gbuffer);
                this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.dispatch(group_count, 1, 1);
            }
            if (scene.lights != 0) {
                command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->sample_direct_lighting);
                this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.dispatch(group_count, 1, 1);
                const vk::MemoryBarrier2 direct_lighting_ready{
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &direct_lighting_ready});
            }
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->shade_surfaces);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            const vk::MemoryBarrier2 shadow_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_to_copy});
            const vk::BufferCopy shadow_width_copy{
                sizeof(std::uint32_t) * 2,
                sizeof(vk::TraceRaysIndirectCommand2KHR) + indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*session.queue_counts.buffer.buffer, *session.indirect_commands.buffer, shadow_width_copy);
            const vk::MemoryBarrier2 shadow_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->pipeline);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->stack_size);
            command_buffer.traceRaysIndirect2KHR(session.indirect_commands.address + sizeof(vk::TraceRaysIndirectCommand2KHR));
            const vk::MemoryBarrier2 visibility_ready{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &visibility_ready});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resolve_visibility);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            read_queue = write_queue;
        }

        const vk::MemoryBarrier2 film_ready{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &film_ready});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->accumulate_film);
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(group_count, 1, 1);
        session.output_layout = vk::ImageLayout::eGeneral;
        ++session.sample_index;
    }

    PathTracer::PathTracer(Spectra& runtime, const render::GpuScene& assets, const scene::SceneView canonical_scene, const std::filesystem::path& shader_directory, const std::uint32_t frames_in_flight) : runtime(&runtime), shader_directory(shader_directory), path_scene(runtime, assets, canonical_scene, shader_directory), render_session(runtime, frames_in_flight) {}

    PathTracer::~PathTracer() = default;

    void PathTracer::prepare(const vk::Extent2D extent) {
        if (!this->integrator) this->integrator.emplace(*this->runtime, this->shader_directory);
        this->render_session.resize(extent, this->path_scene.texture_stack_size, this->path_scene.material_texture_values);
    }

    void PathTracer::synchronize(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        this->path_scene.synchronize(scene, command_buffer);
    }

    void PathTracer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        this->integrator->record(this->path_scene, this->render_session, command_buffer, frame_index);
    }

    void PathTracer::reset_accumulation() noexcept {
        this->render_session.reset();
    }

    void PathTracer::change_camera(const scene::CameraResource& camera) noexcept {
        this->path_scene.scene_camera = camera;
        this->render_session.reset();
    }

    render::RenderReadback PathTracer::readback() {
        return this->render_session.readback();
    }

    render::RenderOutput PathTracer::output() const noexcept {
        return this->render_session.output();
    }

    std::uint32_t PathTracer::accumulated_samples() const noexcept {
        return this->render_session.sample_index;
    }
} // namespace spectra::pathtracer
