export module spectra.pathtracer.spectrum;

import spectra.scene;
import std;

export namespace spectra::pathtracer {
    struct RgbSigmoidPolynomial {
        float c0{};
        float c1{};
        float c2{};

        [[nodiscard]] float evaluate(
            float wavelength) const noexcept;

        friend auto operator<=>(
            const RgbSigmoidPolynomial&,
            const RgbSigmoidPolynomial&) = default;
    };

    struct RgbToSpectrumTable {
        explicit RgbToSpectrumTable(std::span<const std::uint32_t> data);

        [[nodiscard]] RgbSigmoidPolynomial
        polynomial(scene::Float3 rgb) const noexcept;

    private:
        std::array<float, 64> scale{};
        std::span<const std::uint32_t> coefficients{};
    };

    struct SpectrumTables {
        explicit SpectrumTables(std::span<const std::uint32_t> data);

        [[nodiscard]] const RgbToSpectrumTable& rgb(
            scene::SpectrumColorSpace color_space) const noexcept;

    private:
        RgbToSpectrumTable srgb;
        RgbToSpectrumTable rec2020;
        RgbToSpectrumTable aces2065_1;
    };

    [[nodiscard]] std::vector<std::uint32_t>
    load_spectrum_table_data(const std::filesystem::path& directory);

    enum class CompiledSpectrumKind :
        std::uint32_t {
        Rgb,
        Constant,
        Blackbody,
        PiecewiseLinear,
    };

    enum class CompiledIlluminant :
        std::uint32_t {
        None,
        D65,
        D60,
    };

    struct alignas(16) CompiledSpectrum {
        std::array<float, 4> parameters{};
        std::array<std::uint32_t, 4> metadata{};
    };

    [[nodiscard]] CompiledSpectrum
    compile_spectrum(
        const scene::SpectrumParameter& spectrum,
        const SpectrumTables& tables,
        std::vector<scene::Float2>&
            piecewise_samples);
} // namespace spectra::pathtracer
