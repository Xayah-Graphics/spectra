module spectra.pathtracer.spectrum;

import std;

namespace spectra::pathtracer {
    namespace {
        constexpr std::uint32_t table_resolution = 64;
        constexpr std::size_t coefficient_count =
            3ull *
            table_resolution *
            table_resolution *
            table_resolution *
            3ull;
        constexpr std::uint64_t expected_table_size =
            16ull +
            table_resolution * sizeof(float) +
            coefficient_count * sizeof(float);
        constexpr std::size_t table_word_count =
            expected_table_size / sizeof(std::uint32_t);

        [[nodiscard]] float lerp(
            const float value,
            const float first,
            const float second) noexcept {
            return std::fma(
                value,
                second - first,
                first);
        }

        [[nodiscard]] std::vector<std::uint32_t> load_tables(
            const std::filesystem::path& directory) {
            constexpr std::array names{
                "srgb.rgb2spec",
                "rec2020.rgb2spec",
                "aces2065_1.rgb2spec"};
            std::vector<std::uint32_t> data(
                table_word_count * names.size());
            for (std::size_t index = 0; index != names.size(); ++index) {
                const std::filesystem::path path = directory / names[index];
                std::ifstream stream{path, std::ios::binary};
                std::error_code error{};
                if (!stream ||
                    std::filesystem::file_size(path, error) != expected_table_size ||
                    error)
                    throw std::runtime_error(
                        std::format(
                            "Invalid RGB-to-spectrum table size: {}",
                            path.string()));
                stream.read(
                    reinterpret_cast<char*>(
                        data.data() + table_word_count * index),
                    expected_table_size);
                if (!stream)
                    throw std::runtime_error(
                        std::format(
                            "Failed to read RGB-to-spectrum table: {}",
                            path.string()));
            }
            return data;
        }
    } // namespace

    float RgbSigmoidPolynomial::evaluate(
        const float wavelength) const noexcept {
        const float value =
            std::fma(
                std::fma(
                    this->c0,
                    wavelength,
                    this->c1),
                wavelength,
                this->c2);
        if (std::isinf(value))
            return value > 0.0f
                ? 1.0f
                : 0.0f;
        return 0.5f +
            value /
                (
                    2.0f *
                    std::sqrt(
                        1.0f +
                        value * value)
                );
    }

    RgbToSpectrumTable::RgbToSpectrumTable(
        const std::span<const std::uint32_t> data) {
        const std::array<char, 8> magic{
            'S', 'P', 'R', 'G',
            'B', '0', '0', '1'};
        if (data.size() != table_word_count ||
            std::memcmp(data.data(), magic.data(), magic.size()) != 0 ||
            data[2] != 1 ||
            data[3] != table_resolution)
            throw std::runtime_error(
                "Invalid RGB-to-spectrum table header");
        for (std::size_t index = 0; index != this->scale.size(); ++index)
            this->scale[index] =
                std::bit_cast<float>(data[index + 4]);
        this->coefficients = data.subspan(
            4 + table_resolution,
            coefficient_count);
    }

    RgbSigmoidPolynomial
    RgbToSpectrumTable::polynomial(
        const scene::Float3 rgb) const noexcept {
        const std::array components{
            rgb.x,
            rgb.y,
            rgb.z};
        if (components[0] ==
                components[1] &&
            components[1] ==
                components[2]) {
            if (components[0] <= 0.0f)
                return {
                    0.0f,
                    0.0f,
                    -std::numeric_limits<
                        float>::infinity()};
            if (components[0] >= 1.0f)
                return {
                    0.0f,
                    0.0f,
                    std::numeric_limits<
                        float>::infinity()};
            return {
                0.0f,
                0.0f,
                (components[0] - 0.5f) /
                    std::sqrt(
                        components[0] *
                        (1.0f -
                         components[0]))};
        }
        const std::uint32_t maximum_component =
            components[0] > components[1]
                ? (
                      components[0] >
                              components[2]
                          ? 0u
                          : 2u
                  )
                : (
                      components[1] >
                              components[2]
                          ? 1u
                          : 2u
                  );
        const float z =
            components[maximum_component];
        const float x =
            components[
                (maximum_component + 1u) %
                3u] *
            static_cast<float>(
                table_resolution - 1u) /
            z;
        const float y =
            components[
                (maximum_component + 2u) %
                3u] *
            static_cast<float>(
                table_resolution - 1u) /
            z;
        const std::uint32_t x_index =
            std::min(
                static_cast<std::uint32_t>(x),
                table_resolution - 2u);
        const std::uint32_t y_index =
            std::min(
                static_cast<std::uint32_t>(y),
                table_resolution - 2u);
        const std::array<float, 64>::
            const_iterator upper =
                std::ranges::lower_bound(
                    this->scale,
                    z);
        const std::uint32_t z_index =
            std::clamp(
                static_cast<std::uint32_t>(
                    upper -
                    this->scale.begin()) -
                    1u,
                0u,
                table_resolution - 2u);
        const float x_offset =
            x -
            static_cast<float>(x_index);
        const float y_offset =
            y -
            static_cast<float>(y_index);
        const float z_offset =
            (z - this->scale[z_index]) /
            (
                this->scale[z_index + 1u] -
                this->scale[z_index]
            );
        std::array<float, 3> result{};
        for (std::uint32_t coefficient = 0;
             coefficient != 3;
             ++coefficient) {
            const auto value =
                [this,
                 maximum_component,
                 x_index,
                 y_index,
                 z_index,
                 coefficient](
                    const std::uint32_t x_delta,
                    const std::uint32_t y_delta,
                    const std::uint32_t z_delta) {
                    const std::size_t index =
                        (
                            (
                                (
                                    (
                                        maximum_component *
                                            table_resolution +
                                        z_index +
                                        z_delta
                                    ) *
                                        table_resolution +
                                    y_index +
                                    y_delta
                                ) *
                                    table_resolution +
                                x_index +
                                x_delta
                            ) *
                                3u +
                            coefficient
                        );
                    return std::bit_cast<float>(
                        this->coefficients[index]);
                };
            result[coefficient] =
                lerp(
                    z_offset,
                    lerp(
                        y_offset,
                        lerp(
                            x_offset,
                            value(0, 0, 0),
                            value(1, 0, 0)),
                        lerp(
                            x_offset,
                            value(0, 1, 0),
                            value(1, 1, 0))),
                    lerp(
                        y_offset,
                        lerp(
                            x_offset,
                            value(0, 0, 1),
                            value(1, 0, 1)),
                        lerp(
                            x_offset,
                            value(0, 1, 1),
                            value(1, 1, 1))));
        }
        return {
            result[0],
            result[1],
            result[2]};
    }

    SpectrumTables::SpectrumTables(const std::span<const std::uint32_t> data)
        : srgb(data.first(table_word_count)),
          rec2020(data.subspan(table_word_count, table_word_count)),
          aces2065_1(data.last(table_word_count)) {
        if (data.size() != table_word_count * 3) throw std::runtime_error("Invalid RGB-to-spectrum table collection");
    }

    const RgbToSpectrumTable&
    SpectrumTables::rgb(
        const scene::SpectrumColorSpace
            color_space) const noexcept {
        switch (color_space) {
        case scene::SpectrumColorSpace::Srgb:
            return this->srgb;
        case scene::SpectrumColorSpace::Rec2020:
            return this->rec2020;
        case scene::SpectrumColorSpace::Aces2065_1:
            return this->aces2065_1;
        }
        std::unreachable();
    }

    std::vector<std::uint32_t> load_spectrum_table_data(const std::filesystem::path& directory) {
        return load_tables(directory);
    }

    CompiledSpectrum compile_spectrum(
        const scene::SpectrumParameter& spectrum,
        const SpectrumTables& tables,
        std::vector<scene::Float2>&
            piecewise_samples) {
        if (spectrum.texture.value != 0)
            throw std::runtime_error(
                "A textured Spectrum must be compiled by the Texture Program");
        switch (spectrum.encoding) {
        case scene::SpectrumEncoding::RgbAlbedo:
        case scene::SpectrumEncoding::RgbUnbounded:
        case scene::SpectrumEncoding::RgbIlluminant: {
            scene::Float3 rgb = spectrum.value;
            float scale = 1.0f;
            if (spectrum.encoding !=
                scene::SpectrumEncoding::
                    RgbAlbedo) {
                const float maximum =
                    std::max(
                        rgb.x,
                        std::max(
                            rgb.y,
                            rgb.z));
                scale = 2.0f * maximum;
                if (scale != 0.0f) {
                    rgb.x /= scale;
                    rgb.y /= scale;
                    rgb.z /= scale;
                }
            }
            const RgbSigmoidPolynomial polynomial =
                tables.rgb(
                    spectrum.color_space)
                    .polynomial(rgb);
            CompiledIlluminant illuminant =
                CompiledIlluminant::None;
            if (spectrum.encoding ==
                scene::SpectrumEncoding::
                    RgbIlluminant)
                illuminant =
                    spectrum.color_space ==
                            scene::SpectrumColorSpace::
                                Aces2065_1
                        ? CompiledIlluminant::D60
                        : CompiledIlluminant::D65;
            return {
                {
                    polynomial.c0,
                    polynomial.c1,
                    polynomial.c2,
                    scale},
                {
                    std::to_underlying(
                        CompiledSpectrumKind::Rgb),
                    std::to_underlying(
                        illuminant),
                    0,
                    0}};
        }
        case scene::SpectrumEncoding::Constant:
            return {
                {spectrum.scalar, 0.0f, 0.0f, 0.0f},
                {
                    std::to_underlying(
                        CompiledSpectrumKind::
                            Constant),
                    0,
                    0,
                    0}};
        case scene::SpectrumEncoding::Blackbody: {
            const scene::BlackbodySpectrum blackbody{
                spectrum.temperature};
            return {
                {
                    spectrum.temperature,
                    blackbody.normalization,
                    0.0f,
                    0.0f},
                {
                    std::to_underlying(
                        CompiledSpectrumKind::
                            Blackbody),
                    0,
                    0,
                    0}};
        }
        case scene::SpectrumEncoding::PiecewiseLinear: {
            if (spectrum.wavelengths.size() !=
                    spectrum.samples.size() ||
                spectrum.wavelengths.size() < 2)
                throw std::runtime_error(
                    "Piecewise-linear Spectrum requires equal wavelength/sample arrays with at least two entries");
            for (std::size_t index = 1;
                 index !=
                 spectrum.wavelengths.size();
                 ++index)
                if (spectrum.wavelengths[index] <=
                    spectrum.wavelengths[
                        index - 1])
                    throw std::runtime_error(
                        "Piecewise-linear Spectrum wavelengths must be strictly increasing");
            const std::uint32_t offset =
                static_cast<std::uint32_t>(
                    piecewise_samples.size());
            for (std::size_t index = 0;
                 index !=
                 spectrum.wavelengths.size();
                 ++index)
                piecewise_samples.push_back(
                    {
                        spectrum.wavelengths[
                            index],
                        spectrum.samples[index]});
            return {
                {},
                {
                    std::to_underlying(
                        CompiledSpectrumKind::
                            PiecewiseLinear),
                    0,
                    offset,
                    static_cast<std::uint32_t>(
                        spectrum.wavelengths
                            .size())}};
        }
        }
        std::unreachable();
    }
} // namespace spectra::pathtracer
