#include <pathtracer/device_scene.cuh>
#include <pathtracer/core/textures.cuh>
#include <pathtracer/core/materials.cuh>
#include <pathtracer/core/media.cuh>
#include <pathtracer/core/shapes.cuh>
#include <pathtracer/core/lights.cuh>
#include <pathtracer/core/lightsamplers.cuh>
#include <pathtracer/core/cameras.cuh>
#include <pathtracer/core/film.cuh>
#include <pathtracer/core/filters.cuh>
#include <pathtracer/core/samplers.cuh>
#include <pathtracer/util/color.cuh>
#include <pathtracer/util/colorspace.cuh>
#include <pathtracer/util/spectrum.cuh>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spectra::pathtracer {
    struct DeviceSceneBuilder::State {
        std::unordered_map<const void*, Spectrum> spectra{};
        std::unordered_map<const DenselySampledSpectrum*, const DenselySampledSpectrum*> denseSpectra{};
        std::unordered_map<const RGBToSpectrumTable*, const RGBToSpectrumTable*> rgbToSpectrumTables{};
        std::unordered_map<const RGBColorSpace*, const RGBColorSpace*> colorSpaces{};
        std::unordered_map<const void*, TextureMapping2D> mappings2D{};
        std::unordered_map<const void*, TextureMapping3D> mappings3D{};
        std::unordered_map<const void*, FloatTexture> floatTextures{};
        std::unordered_map<const void*, SpectrumTexture> spectrumTextures{};
        std::unordered_map<const Image*, Image*> images{};
        std::unordered_map<const void*, Material> materials{};
        std::unordered_map<const MeasuredBxDFData*, const MeasuredBxDFData*> measuredBxDFs{};
        std::unordered_map<const void*, Medium> media{};
        std::unordered_map<const void*, Shape> shapes{};
        std::unordered_map<const Transform*, const Transform*> transforms{};
        std::unordered_map<const TriangleMesh*, const TriangleMesh*> triangleMeshes{};
        std::unordered_map<const BilinearPatchMesh*, const BilinearPatchMesh*> bilinearPatchMeshes{};
        std::unordered_map<const CurveCommon*, const CurveCommon*> curveCommon{};
        std::unordered_map<const void*, Light> lights{};
    };

    DeviceSceneBuilder::DeviceSceneBuilder(PathtracerDeviceArena& arena, cudaStream_t stream) : arena(arena), stream(stream), state(std::make_unique<State>()) {}

    DeviceSceneBuilder::~DeviceSceneBuilder() noexcept = default;

    Spectrum DeviceSceneBuilder::CompileSpectrum(Spectrum spectrum) {
        std::lock_guard lock(mutex);
        if (!spectrum) return nullptr;
        if (const auto found = state->spectra.find(spectrum.ptr()); found != state->spectra.end()) return found->second;

        Spectrum deviceSpectrum = spectrum.DispatchHost([&](const auto* host) -> Spectrum {
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DenselySampledSpectrum>) {
                return CompileSpectrum(host);
            } else {
                std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
                if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, PiecewiseLinearSpectrum>) {
                    device.lambdas = pstd::vector<Float>::DeviceView(arena.StoreArray(host->lambdas.data(), host->lambdas.size(), stream), host->lambdas.size());
                    device.values = pstd::vector<Float>::DeviceView(arena.StoreArray(host->values.data(), host->values.size(), stream), host->values.size());
                } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, RGBIlluminantSpectrum>)
                    device.illuminant = CompileSpectrum(host->illuminant);
                return arena.Store(device, stream);
            }
        });

        state->spectra.emplace(spectrum.ptr(), deviceSpectrum);
        return deviceSpectrum;
    }

    const DenselySampledSpectrum* DeviceSceneBuilder::CompileSpectrum(const DenselySampledSpectrum* spectrum) {
        std::lock_guard lock(mutex);
        if (spectrum == nullptr) return nullptr;
        if (const auto found = state->denseSpectra.find(spectrum); found != state->denseSpectra.end()) return found->second;

        DenselySampledSpectrum device = *spectrum;
        device.values = pstd::vector<Float>::DeviceView(arena.StoreArray(spectrum->values.data(), spectrum->values.size(), stream), spectrum->values.size());
        const DenselySampledSpectrum* result = arena.Store(device, stream);
        state->denseSpectra.emplace(spectrum, result);
        return result;
    }

    const RGBToSpectrumTable* DeviceSceneBuilder::CompileRGBToSpectrumTable(const RGBToSpectrumTable* table) {
        std::lock_guard lock(mutex);
        if (table == nullptr) return nullptr;
        if (const auto found = state->rgbToSpectrumTables.find(table); found != state->rgbToSpectrumTables.end()) return found->second;

        RGBToSpectrumTable device = *table;
        device.zNodes = arena.StoreArray(table->zNodes, RGBToSpectrumTable::res, stream);
        constexpr std::size_t coefficientCount = 3ull * RGBToSpectrumTable::res * RGBToSpectrumTable::res * RGBToSpectrumTable::res * 3;
        device.coeffs = reinterpret_cast<const RGBToSpectrumTable::CoefficientArray*>(arena.StoreArray(reinterpret_cast<const float*>(table->coeffs), coefficientCount, stream));
        const RGBToSpectrumTable* result = arena.Store(device, stream);
        state->rgbToSpectrumTables.emplace(table, result);
        return result;
    }

    const RGBColorSpace* DeviceSceneBuilder::CompileRGBColorSpace(const RGBColorSpace* colorSpace) {
        std::lock_guard lock(mutex);
        if (colorSpace == nullptr) return nullptr;
        if (const auto found = state->colorSpaces.find(colorSpace); found != state->colorSpaces.end()) return found->second;

        RGBColorSpace device = *colorSpace;
        device.illuminant.values = pstd::vector<Float>::DeviceView(arena.StoreArray(colorSpace->illuminant.values.data(), colorSpace->illuminant.values.size(), stream), colorSpace->illuminant.values.size());
        device.rgbToSpectrumTable = CompileRGBToSpectrumTable(colorSpace->rgbToSpectrumTable);
        const RGBColorSpace* result = arena.Store(device, stream);
        state->colorSpaces.emplace(colorSpace, result);
        return result;
    }

    PiecewiseConstant1D DeviceSceneBuilder::CompileDistribution(const PiecewiseConstant1D& distribution) {
        PiecewiseConstant1D device = distribution;
        device.func = pstd::vector<Float>::DeviceView(arena.StoreArray(distribution.func.data(), distribution.func.size(), stream), distribution.func.size());
        device.cdf = pstd::vector<Float>::DeviceView(arena.StoreArray(distribution.cdf.data(), distribution.cdf.size(), stream), distribution.cdf.size());
        return device;
    }

    PiecewiseConstant2D DeviceSceneBuilder::CompileDistribution(const PiecewiseConstant2D& distribution) {
        PiecewiseConstant2D device = distribution;
        std::vector<PiecewiseConstant1D> conditionals;
        conditionals.reserve(distribution.pConditionalV.size());
        for (const PiecewiseConstant1D& conditional : distribution.pConditionalV) conditionals.push_back(CompileDistribution(conditional));
        device.pConditionalV = pstd::vector<PiecewiseConstant1D>::DeviceView(arena.StoreArray(conditionals.data(), conditionals.size(), stream), conditionals.size());
        device.pMarginal = CompileDistribution(distribution.pMarginal);
        return device;
    }

    WindowedPiecewiseConstant2D DeviceSceneBuilder::CompileDistribution(const WindowedPiecewiseConstant2D& distribution) {
        WindowedPiecewiseConstant2D device = distribution;
        device.sat.sum = Array2D<double>::DeviceView(Bounds2i{{0, 0}, {distribution.sat.sum.XSize(), distribution.sat.sum.YSize()}}, arena.StoreArray(distribution.sat.sum.begin(), distribution.sat.sum.size(), stream));
        device.func = Array2D<Float>::DeviceView(Bounds2i{{0, 0}, {distribution.func.XSize(), distribution.func.YSize()}}, arena.StoreArray(distribution.func.begin(), distribution.func.size(), stream));
        return device;
    }

    AliasTable DeviceSceneBuilder::CompileAliasTable(const AliasTable& table) {
        AliasTable device = table;
        device.bins = pstd::vector<AliasTable::Bin>::DeviceView(arena.StoreArray(table.bins.data(), table.bins.size(), stream), table.bins.size());
        return device;
    }

    template <std::size_t Dimension>
    PiecewiseLinear2D<Dimension> DeviceSceneBuilder::CompileDistribution(const PiecewiseLinear2D<Dimension>& distribution) {
        PiecewiseLinear2D<Dimension> device = distribution;
        std::vector<pstd::vector<float>> parameterValues;
        parameterValues.reserve(distribution.m_param_values.size());
        for (const pstd::vector<float>& values : distribution.m_param_values)
            parameterValues.push_back(pstd::vector<float>::DeviceView(arena.StoreArray(values.data(), values.size(), stream), values.size()));
        device.m_param_values = pstd::vector<pstd::vector<float>>::DeviceView(arena.StoreArray(parameterValues.data(), parameterValues.size(), stream), parameterValues.size());
        device.m_data = pstd::vector<float>::DeviceView(arena.StoreArray(distribution.m_data.data(), distribution.m_data.size(), stream), distribution.m_data.size());
        device.m_marginal_cdf = pstd::vector<float>::DeviceView(arena.StoreArray(distribution.m_marginal_cdf.data(), distribution.m_marginal_cdf.size(), stream), distribution.m_marginal_cdf.size());
        device.m_conditional_cdf = pstd::vector<float>::DeviceView(arena.StoreArray(distribution.m_conditional_cdf.data(), distribution.m_conditional_cdf.size(), stream), distribution.m_conditional_cdf.size());
        return device;
    }

    const MeasuredBxDFData* DeviceSceneBuilder::CompileMeasuredBxDF(const MeasuredBxDFData* data) {
        if (data == nullptr) return nullptr;
        if (const auto found = state->measuredBxDFs.find(data); found != state->measuredBxDFs.end()) return found->second;
        MeasuredBxDFData device = *data;
        device.wavelengths = pstd::vector<float>::DeviceView(arena.StoreArray(data->wavelengths.data(), data->wavelengths.size(), stream), data->wavelengths.size());
        device.spectra = CompileDistribution(data->spectra);
        device.ndf = CompileDistribution(data->ndf);
        device.vndf = CompileDistribution(data->vndf);
        device.sigma = CompileDistribution(data->sigma);
        device.luminance = CompileDistribution(data->luminance);
        const MeasuredBxDFData* result = arena.Store(device, stream);
        state->measuredBxDFs.emplace(data, result);
        return result;
    }

    TextureMapping2D DeviceSceneBuilder::CompileTextureMapping(TextureMapping2D mapping) {
        std::lock_guard lock(mutex);
        if (!mapping) return nullptr;
        if (const auto found = state->mappings2D.find(mapping.ptr()); found != state->mappings2D.end()) return found->second;
        TextureMapping2D result = mapping.DispatchHost([&](const auto* host) -> TextureMapping2D { return arena.Store(*host, stream); });
        state->mappings2D.emplace(mapping.ptr(), result);
        return result;
    }

    TextureMapping3D DeviceSceneBuilder::CompileTextureMapping(TextureMapping3D mapping) {
        std::lock_guard lock(mutex);
        if (!mapping) return nullptr;
        if (const auto found = state->mappings3D.find(mapping.ptr()); found != state->mappings3D.end()) return found->second;
        TextureMapping3D result = mapping.DispatchHost([&](const auto* host) -> TextureMapping3D { return arena.Store(*host, stream); });
        state->mappings3D.emplace(mapping.ptr(), result);
        return result;
    }

    FloatTexture DeviceSceneBuilder::CompileFloatTexture(FloatTexture texture) {
        std::lock_guard lock(mutex);
        if (!texture) return nullptr;
        if (const auto found = state->floatTextures.find(texture.ptr()); found != state->floatTextures.end()) return found->second;

        FloatTexture result = texture.DispatchHost([&](const auto* host) -> FloatTexture {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatBilerpTexture>)
                device.mapping = CompileTextureMapping(host->mapping);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatCheckerboardTexture>) {
                device.map2D = CompileTextureMapping(host->map2D);
                device.map3D = CompileTextureMapping(host->map3D);
                device.tex[0] = CompileFloatTexture(host->tex[0]);
                device.tex[1] = CompileFloatTexture(host->tex[1]);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatDotsTexture>) {
                device.mapping = CompileTextureMapping(host->mapping);
                device.outsideDot = CompileFloatTexture(host->outsideDot);
                device.insideDot = CompileFloatTexture(host->insideDot);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FBmTexture> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, WindyTexture> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, WrinkledTexture>)
                device.mapping = CompileTextureMapping(host->mapping);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GPUFloatImageTexture>)
                device.mapping = CompileTextureMapping(host->mapping);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatMixTexture>) {
                device.tex1 = CompileFloatTexture(host->tex1);
                device.tex2 = CompileFloatTexture(host->tex2);
                device.amount = CompileFloatTexture(host->amount);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatDirectionMixTexture>) {
                device.tex1 = CompileFloatTexture(host->tex1);
                device.tex2 = CompileFloatTexture(host->tex2);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GPUFloatPtexTexture>)
                device.faceValues = pstd::vector<Float>::DeviceView(arena.StoreArray(host->faceValues.data(), host->faceValues.size(), stream), host->faceValues.size());
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, FloatScaledTexture>) {
                device.tex = CompileFloatTexture(host->tex);
                device.scale = CompileFloatTexture(host->scale);
            }
            return arena.Store(device, stream);
        });

        state->floatTextures.emplace(texture.ptr(), result);
        return result;
    }

    SpectrumTexture DeviceSceneBuilder::CompileSpectrumTexture(SpectrumTexture texture) {
        std::lock_guard lock(mutex);
        if (!texture) return nullptr;
        if (const auto found = state->spectrumTextures.find(texture.ptr()); found != state->spectrumTextures.end()) return found->second;

        SpectrumTexture result = texture.DispatchHost([&](const auto* host) -> SpectrumTexture {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumConstantTexture>)
                device.value = CompileSpectrum(host->value);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumBilerpTexture>) {
                device.mapping = CompileTextureMapping(host->mapping);
                device.v00 = CompileSpectrum(host->v00);
                device.v01 = CompileSpectrum(host->v01);
                device.v10 = CompileSpectrum(host->v10);
                device.v11 = CompileSpectrum(host->v11);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumCheckerboardTexture>) {
                device.map2D = CompileTextureMapping(host->map2D);
                device.map3D = CompileTextureMapping(host->map3D);
                device.tex[0] = CompileSpectrumTexture(host->tex[0]);
                device.tex[1] = CompileSpectrumTexture(host->tex[1]);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumDotsTexture>) {
                device.mapping = CompileTextureMapping(host->mapping);
                device.outsideDot = CompileSpectrumTexture(host->outsideDot);
                device.insideDot = CompileSpectrumTexture(host->insideDot);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GPUSpectrumImageTexture>) {
                device.mapping = CompileTextureMapping(host->mapping);
                device.colorSpace = CompileRGBColorSpace(host->colorSpace);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, MarbleTexture>)
                device.mapping = CompileTextureMapping(host->mapping);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumMixTexture>) {
                device.tex1 = CompileSpectrumTexture(host->tex1);
                device.tex2 = CompileSpectrumTexture(host->tex2);
                device.amount = CompileFloatTexture(host->amount);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumDirectionMixTexture>) {
                device.tex1 = CompileSpectrumTexture(host->tex1);
                device.tex2 = CompileSpectrumTexture(host->tex2);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GPUSpectrumPtexTexture>)
                device.faceValues = pstd::vector<RGB>::DeviceView(arena.StoreArray(host->faceValues.data(), host->faceValues.size(), stream), host->faceValues.size());
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectrumScaledTexture>) {
                device.tex = CompileSpectrumTexture(host->tex);
                device.scale = CompileFloatTexture(host->scale);
            }
            return arena.Store(device, stream);
        });

        state->spectrumTextures.emplace(texture.ptr(), result);
        return result;
    }

    Image DeviceSceneBuilder::CompileImageValue(const Image& image) {
        std::vector<Float> pixels(static_cast<std::size_t>(image.Resolution().x) * image.Resolution().y * image.NChannels());
        for (int y = 0; y < image.Resolution().y; ++y)
            for (int x = 0; x < image.Resolution().x; ++x)
                for (int channel = 0; channel < image.NChannels(); ++channel) pixels[channel + image.NChannels() * (x + y * image.Resolution().x)] = image.GetChannel({x, y}, channel);

        Image device;
        device.format = PixelFormat::Float;
        device.resolution = image.Resolution();
        device.channelNames = pstd::vector<std::string>::DeviceView(nullptr, image.NChannels());
        device.encoding = nullptr;
        device.p32 = pstd::vector<Float>::DeviceView(arena.StoreArray(pixels.data(), pixels.size(), stream), pixels.size());
        return device;
    }

    Image* DeviceSceneBuilder::CompileImage(const Image* image) {
        std::lock_guard lock(mutex);
        if (image == nullptr) return nullptr;
        if (const auto found = state->images.find(image); found != state->images.end()) return found->second;
        Image device = CompileImageValue(*image);
        Image* result = arena.Store(device, stream);
        state->images.emplace(image, result);
        return result;
    }

    Material DeviceSceneBuilder::CompileMaterial(Material material) {
        std::lock_guard lock(mutex);
        if (!material) return nullptr;
        if (const auto found = state->materials.find(material.ptr()); found != state->materials.end()) return found->second;

        Material result = material.DispatchHost([&](const auto* host) -> Material {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DielectricMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.uRoughness = CompileFloatTexture(host->uRoughness);
                device.vRoughness = CompileFloatTexture(host->vRoughness);
                device.eta = CompileSpectrum(host->eta);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, ThinDielectricMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.eta = CompileSpectrum(host->eta);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, MixMaterial>) {
                device.amount = CompileFloatTexture(host->amount);
                device.materials[0] = CompileMaterial(host->materials[0]);
                device.materials[1] = CompileMaterial(host->materials[1]);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, HairMaterial>) {
                device.sigma_a = CompileSpectrumTexture(host->sigma_a);
                device.color = CompileSpectrumTexture(host->color);
                device.eumelanin = CompileFloatTexture(host->eumelanin);
                device.pheomelanin = CompileFloatTexture(host->pheomelanin);
                device.eta = CompileFloatTexture(host->eta);
                device.beta_m = CompileFloatTexture(host->beta_m);
                device.beta_n = CompileFloatTexture(host->beta_n);
                device.alpha = CompileFloatTexture(host->alpha);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DiffuseMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, ConductorMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.eta = CompileSpectrumTexture(host->eta);
                device.k = CompileSpectrumTexture(host->k);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
                device.uRoughness = CompileFloatTexture(host->uRoughness);
                device.vRoughness = CompileFloatTexture(host->vRoughness);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, CoatedDiffuseMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
                device.albedo = CompileSpectrumTexture(host->albedo);
                device.uRoughness = CompileFloatTexture(host->uRoughness);
                device.vRoughness = CompileFloatTexture(host->vRoughness);
                device.thickness = CompileFloatTexture(host->thickness);
                device.g = CompileFloatTexture(host->g);
                device.eta = CompileSpectrum(host->eta);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, CoatedConductorMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.interfaceURoughness = CompileFloatTexture(host->interfaceURoughness);
                device.interfaceVRoughness = CompileFloatTexture(host->interfaceVRoughness);
                device.thickness = CompileFloatTexture(host->thickness);
                device.interfaceEta = CompileSpectrum(host->interfaceEta);
                device.g = CompileFloatTexture(host->g);
                device.albedo = CompileSpectrumTexture(host->albedo);
                device.conductorURoughness = CompileFloatTexture(host->conductorURoughness);
                device.conductorVRoughness = CompileFloatTexture(host->conductorVRoughness);
                device.conductorEta = CompileSpectrumTexture(host->conductorEta);
                device.k = CompileSpectrumTexture(host->k);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SubsurfaceMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.sigma_a = CompileSpectrumTexture(host->sigma_a);
                device.sigma_s = CompileSpectrumTexture(host->sigma_s);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
                device.mfp = CompileSpectrumTexture(host->mfp);
                device.uRoughness = CompileFloatTexture(host->uRoughness);
                device.vRoughness = CompileFloatTexture(host->vRoughness);
                device.table.rhoSamples = pstd::vector<Float>::DeviceView(arena.StoreArray(host->table.rhoSamples.data(), host->table.rhoSamples.size(), stream), host->table.rhoSamples.size());
                device.table.radiusSamples = pstd::vector<Float>::DeviceView(arena.StoreArray(host->table.radiusSamples.data(), host->table.radiusSamples.size(), stream), host->table.radiusSamples.size());
                device.table.profile = pstd::vector<Float>::DeviceView(arena.StoreArray(host->table.profile.data(), host->table.profile.size(), stream), host->table.profile.size());
                device.table.rhoEff = pstd::vector<Float>::DeviceView(arena.StoreArray(host->table.rhoEff.data(), host->table.rhoEff.size(), stream), host->table.rhoEff.size());
                device.table.profileCDF = pstd::vector<Float>::DeviceView(arena.StoreArray(host->table.profileCDF.data(), host->table.profileCDF.size(), stream), host->table.profileCDF.size());
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DiffuseTransmissionMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.reflectance = CompileSpectrumTexture(host->reflectance);
                device.transmittance = CompileSpectrumTexture(host->transmittance);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, MeasuredMaterial>) {
                device.normalMap = CompileImage(host->normalMap);
                device.displacement = CompileFloatTexture(host->displacement);
                device.brdf = CompileMeasuredBxDF(host->brdf);
            }
            return arena.Store(device, stream);
        });

        state->materials.emplace(material.ptr(), result);
        return result;
    }

    Medium DeviceSceneBuilder::CompileMedium(Medium medium) {
        std::lock_guard lock(mutex);
        if (!medium) return nullptr;
        if (const auto found = state->media.find(medium.ptr()); found != state->media.end()) return found->second;
        if (medium.Is<DeviceVolumeMedium>()) {
            state->media.emplace(medium.ptr(), medium);
            return medium;
        }

        auto packDenseSpectrum = [&](const DenselySampledSpectrum& host) {
            DenselySampledSpectrum device = host;
            device.values = pstd::vector<Float>::DeviceView(arena.StoreArray(host.values.data(), host.values.size(), stream), host.values.size());
            return device;
        };
        auto packGrid = [&]<typename T>(const SampledGrid<T>& host) {
            SampledGrid<T> device = host;
            std::vector<T> values(host.values.begin(), host.values.end());
            if constexpr (std::is_same_v<T, RGBIlluminantSpectrum>)
                for (RGBIlluminantSpectrum& value : values) value.illuminant = CompileSpectrum(value.illuminant);
            device.values = pstd::vector<T>::DeviceView(arena.StoreArray(values.data(), values.size(), stream), values.size());
            return device;
        };

        Medium result = medium.DispatchHost([&](const auto* host) -> Medium {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, HomogeneousMedium>) {
                device.sigma_a_spec = packDenseSpectrum(host->sigma_a_spec);
                device.sigma_s_spec = packDenseSpectrum(host->sigma_s_spec);
                device.Le_spec = packDenseSpectrum(host->Le_spec);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GridMedium>) {
                device.sigma_a_spec = packDenseSpectrum(host->sigma_a_spec);
                device.sigma_s_spec = packDenseSpectrum(host->sigma_s_spec);
                device.densityGrid = packGrid(host->densityGrid);
                if (host->temperatureGrid) device.temperatureGrid = packGrid(*host->temperatureGrid);
                device.Le_spec = packDenseSpectrum(host->Le_spec);
                device.LeScale = packGrid(host->LeScale);
                device.majorantGrid.voxels = pstd::vector<Float>::DeviceView(arena.StoreArray(host->majorantGrid.voxels.data(), host->majorantGrid.voxels.size(), stream), host->majorantGrid.voxels.size());
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, RGBGridMedium>) {
                if (host->LeGrid) device.LeGrid = packGrid(*host->LeGrid);
                if (host->sigma_aGrid) device.sigma_aGrid = packGrid(*host->sigma_aGrid);
                if (host->sigma_sGrid) device.sigma_sGrid = packGrid(*host->sigma_sGrid);
                device.majorantGrid.voxels = pstd::vector<Float>::DeviceView(arena.StoreArray(host->majorantGrid.voxels.data(), host->majorantGrid.voxels.size(), stream), host->majorantGrid.voxels.size());
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, CloudMedium>) {
                device.sigma_a_spec = packDenseSpectrum(host->sigma_a_spec);
                device.sigma_s_spec = packDenseSpectrum(host->sigma_s_spec);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, NanoVDBMedium>) {
                NanoVDBMedium packed;
                packed.bounds = host->bounds;
                packed.renderFromMedium = host->renderFromMedium;
                packed.sigma_a_spec = packDenseSpectrum(host->sigma_a_spec);
                packed.sigma_s_spec = packDenseSpectrum(host->sigma_s_spec);
                packed.phase = host->phase;
                packed.majorantGrid = host->majorantGrid;
                packed.majorantGrid.voxels = pstd::vector<Float>::DeviceView(arena.StoreArray(host->majorantGrid.voxels.data(), host->majorantGrid.voxels.size(), stream), host->majorantGrid.voxels.size());
                const uint8_t* densityBytes = arena.StoreArray(host->densityGrid.buffer().data(), host->densityGrid.buffer().size(), stream);
                packed.densityFloatGrid = reinterpret_cast<const nanovdb::FloatGrid*>(densityBytes + (reinterpret_cast<const uint8_t*>(host->densityFloatGrid) - host->densityGrid.buffer().data()));
                if (host->temperatureFloatGrid != nullptr) {
                    const uint8_t* temperatureBytes = arena.StoreArray(host->temperatureGrid.buffer().data(), host->temperatureGrid.buffer().size(), stream);
                    packed.temperatureFloatGrid = reinterpret_cast<const nanovdb::FloatGrid*>(temperatureBytes + (reinterpret_cast<const uint8_t*>(host->temperatureFloatGrid) - host->temperatureGrid.buffer().data()));
                }
                packed.LeScale = host->LeScale;
                packed.temperatureOffset = host->temperatureOffset;
                packed.temperatureScale = host->temperatureScale;
                return Medium(arena.Store(packed, stream));
            }
            return Medium(arena.Store(device, stream));
        });

        state->media.emplace(medium.ptr(), result);
        return result;
    }

    MediumInterface* DeviceSceneBuilder::CompileMediumInterface(const MediumInterface* mediumInterface) {
        if (mediumInterface == nullptr) return nullptr;
        MediumInterface device{CompileMedium(mediumInterface->inside), CompileMedium(mediumInterface->outside)};
        return arena.Store(device, stream);
    }

    const Transform* DeviceSceneBuilder::CompileTransform(const Transform* transform) {
        std::lock_guard lock(mutex);
        if (transform == nullptr) return nullptr;
        if (const auto found = state->transforms.find(transform); found != state->transforms.end()) return found->second;
        const Transform* result = arena.Store(*transform, stream);
        state->transforms.emplace(transform, result);
        return result;
    }

    const TriangleMesh* DeviceSceneBuilder::CompileTriangleMesh(const TriangleMesh* mesh) {
        std::lock_guard lock(mutex);
        if (mesh == nullptr) return nullptr;
        if (const auto found = state->triangleMeshes.find(mesh); found != state->triangleMeshes.end()) return found->second;
        TriangleMesh device = *mesh;
        device.vertexIndices = arena.StoreArray(mesh->vertexIndices, static_cast<std::size_t>(3) * mesh->nTriangles, stream);
        device.p = arena.StoreArray(mesh->p, mesh->nVertices, stream);
        device.n = mesh->n == nullptr ? nullptr : arena.StoreArray(mesh->n, mesh->nVertices, stream);
        device.s = mesh->s == nullptr ? nullptr : arena.StoreArray(mesh->s, mesh->nVertices, stream);
        device.uv = mesh->uv == nullptr ? nullptr : arena.StoreArray(mesh->uv, mesh->nVertices, stream);
        device.faceIndices = mesh->faceIndices == nullptr ? nullptr : arena.StoreArray(mesh->faceIndices, mesh->nTriangles, stream);
        const TriangleMesh* result = arena.Store(device, stream);
        state->triangleMeshes.emplace(mesh, result);
        return result;
    }

    const BilinearPatchMesh* DeviceSceneBuilder::CompileBilinearPatchMesh(const BilinearPatchMesh* mesh) {
        std::lock_guard lock(mutex);
        if (mesh == nullptr) return nullptr;
        if (const auto found = state->bilinearPatchMeshes.find(mesh); found != state->bilinearPatchMeshes.end()) return found->second;
        BilinearPatchMesh device = *mesh;
        device.vertexIndices = arena.StoreArray(mesh->vertexIndices, static_cast<std::size_t>(4) * mesh->nPatches, stream);
        device.p = arena.StoreArray(mesh->p, mesh->nVertices, stream);
        device.n = mesh->n == nullptr ? nullptr : arena.StoreArray(mesh->n, mesh->nVertices, stream);
        device.uv = mesh->uv == nullptr ? nullptr : arena.StoreArray(mesh->uv, mesh->nVertices, stream);
        device.faceIndices = mesh->faceIndices == nullptr ? nullptr : arena.StoreArray(mesh->faceIndices, mesh->nPatches, stream);
        if (mesh->imageDistribution != nullptr) device.imageDistribution = arena.Store(CompileDistribution(*mesh->imageDistribution), stream);
        const BilinearPatchMesh* result = arena.Store(device, stream);
        state->bilinearPatchMeshes.emplace(mesh, result);
        return result;
    }

    const CurveCommon* DeviceSceneBuilder::CompileCurveCommon(const CurveCommon* common) {
        std::lock_guard lock(mutex);
        if (common == nullptr) return nullptr;
        if (const auto found = state->curveCommon.find(common); found != state->curveCommon.end()) return found->second;
        CurveCommon device = *common;
        device.renderFromObject = CompileTransform(common->renderFromObject);
        device.objectFromRender = CompileTransform(common->objectFromRender);
        const CurveCommon* result = arena.Store(device, stream);
        state->curveCommon.emplace(common, result);
        return result;
    }

    Shape DeviceSceneBuilder::CompileShape(Shape shape) {
        std::lock_guard lock(mutex);
        if (!shape) return nullptr;
        if (const auto found = state->shapes.find(shape.ptr()); found != state->shapes.end()) return found->second;
        Shape result = shape.DispatchHost([&](const auto* host) -> Shape {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, Sphere> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, Cylinder> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, Disk>) {
                device.renderFromObject = CompileTransform(host->renderFromObject);
                device.objectFromRender = CompileTransform(host->objectFromRender);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, Triangle>)
                device.mesh = CompileTriangleMesh(host->mesh);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, BilinearPatch>)
                device.mesh = CompileBilinearPatchMesh(host->mesh);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, Curve>)
                device.common = CompileCurveCommon(host->common);
            return arena.Store(device, stream);
        });
        state->shapes.emplace(shape.ptr(), result);
        return result;
    }

    Light DeviceSceneBuilder::CompileLight(Light light) {
        std::lock_guard lock(mutex);
        if (!light) return nullptr;
        if (const auto found = state->lights.find(light.ptr()); found != state->lights.end()) return found->second;

        Light result = light.DispatchHost([&](const auto* host) -> Light {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            device.mediumInterface.inside = CompileMedium(host->mediumInterface.inside);
            device.mediumInterface.outside = CompileMedium(host->mediumInterface.outside);
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, PointLight>)
                device.I = CompileSpectrum(host->I);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DistantLight>)
                device.Lemit = CompileSpectrum(host->Lemit);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, ProjectionLight>) {
                device.image = CompileImageValue(host->image);
                device.imageColorSpace = CompileRGBColorSpace(host->imageColorSpace);
                device.distrib = CompileDistribution(host->distrib);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GoniometricLight>) {
                device.Iemit = CompileSpectrum(host->Iemit);
                device.image = CompileImageValue(host->image);
                device.distrib = CompileDistribution(host->distrib);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, DiffuseAreaLight>) {
                device.shape = CompileShape(host->shape);
                device.alpha = CompileFloatTexture(host->alpha);
                device.Lemit = CompileSpectrum(host->Lemit);
                device.image = CompileImageValue(host->image);
                device.imageColorSpace = CompileRGBColorSpace(host->imageColorSpace);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, UniformInfiniteLight>)
                device.Lemit = CompileSpectrum(host->Lemit);
            else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, ImageInfiniteLight>) {
                device.image = CompileImageValue(host->image);
                device.imageColorSpace = CompileRGBColorSpace(host->imageColorSpace);
                device.distribution = CompileDistribution(host->distribution);
                device.compensatedDistribution = CompileDistribution(host->compensatedDistribution);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, PortalImageInfiniteLight>) {
                device.image = CompileImageValue(host->image);
                device.distribution = CompileDistribution(host->distribution);
                device.imageColorSpace = CompileRGBColorSpace(host->imageColorSpace);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpotLight>)
                device.Iemit = CompileSpectrum(host->Iemit);
            return arena.Store(device, stream);
        });

        state->lights.emplace(light.ptr(), result);
        return result;
    }

    const Light* DeviceSceneBuilder::CompileLights(pstd::span<const Light> hostLights) {
        std::vector<Light> lights;
        lights.reserve(hostLights.size());
        for (Light light : hostLights) lights.push_back(CompileLight(light));
        return arena.StoreArray(lights.data(), lights.size(), stream);
    }

    LightSampler DeviceSceneBuilder::CompileLightSampler(LightSampler sampler) {
        if (!sampler) return nullptr;
        auto compileLights = [&](const pstd::vector<Light>& host) {
            std::vector<Light> lights;
            lights.reserve(host.size());
            for (Light light : host) lights.push_back(CompileLight(light));
            return pstd::vector<Light>::DeviceView(arena.StoreArray(lights.data(), lights.size(), stream), lights.size());
        };
        auto compileLightMap = [&](const auto& hostMap) {
            std::remove_cv_t<std::remove_reference_t<decltype(hostMap)>> rebuilt{Allocator{}};
            for (const auto& entry : hostMap.table)
                if (entry) rebuilt.Insert(CompileLight(entry->first), entry->second);
            std::remove_cv_t<std::remove_reference_t<decltype(hostMap)>> device = std::move(rebuilt);
            device.table = pstd::vector<typename std::remove_cv_t<std::remove_reference_t<decltype(hostMap)>>::TableEntry>::DeviceView(arena.StoreArray(device.table.data(), device.table.size(), stream), device.table.size());
            return device;
        };

        return sampler.DispatchHost([&](const auto* host) -> LightSampler {
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, UniformLightSampler>) {
                UniformLightSampler device;
                device.lights = compileLights(host->lights);
                return arena.Store(device, stream);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, PowerLightSampler>) {
                PowerLightSampler device{Allocator{}};
                device.lights = compileLights(host->lights);
                device.lightToIndex = compileLightMap(host->lightToIndex);
                device.aliasTable = CompileAliasTable(host->aliasTable);
                return arena.Store(device, stream);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, BVHLightSampler>) {
                BVHLightSampler device{Allocator{}};
                device.lights = compileLights(host->lights);
                device.infiniteLights = compileLights(host->infiniteLights);
                device.nodes = pstd::vector<LightBVHNode>::DeviceView(arena.StoreArray(host->nodes.data(), host->nodes.size(), stream), host->nodes.size());
                device.lightToBitTrail = compileLightMap(host->lightToBitTrail);
                device.allLightBounds = host->allLightBounds;
                return arena.Store(device, stream);
            } else if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, ExhaustiveLightSampler>) {
                ExhaustiveLightSampler device{Allocator{}};
                device.lights = compileLights(host->lights);
                device.boundedLights = compileLights(host->boundedLights);
                device.infiniteLights = compileLights(host->infiniteLights);
                device.lightBounds = pstd::vector<LightBounds>::DeviceView(arena.StoreArray(host->lightBounds.data(), host->lightBounds.size(), stream), host->lightBounds.size());
                device.lightToBoundedIndex = compileLightMap(host->lightToBoundedIndex);
                return arena.Store(device, stream);
            }
        });
    }

    Camera DeviceSceneBuilder::CompileCamera(Camera camera) {
        if (!camera) return nullptr;
        return camera.DispatchHost([&](const auto* host) -> Camera {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            device.medium = CompileMedium(host->medium);
            return arena.Store(device, stream);
        });
    }

    Filter DeviceSceneBuilder::CompileFilter(Filter filter) {
        if (!filter) return nullptr;
        return filter.DispatchHost([&](const auto* host) -> Filter { return arena.Store(*host, stream); });
    }

    Film DeviceSceneBuilder::CompileFilm(Film film) {
        if (!film) return nullptr;
        return film.DispatchHost([&](const auto* host) -> Film {
            std::remove_cv_t<std::remove_reference_t<decltype(*host)>> device = *host;
            device.filter = CompileFilter(host->filter);
            device.sensor = nullptr;
            if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, RGBFilm> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, GBufferFilm> || std::is_same_v<std::remove_cv_t<std::remove_reference_t<decltype(*host)>>, SpectralFilm>)
                device.colorSpace = CompileRGBColorSpace(host->colorSpace);
            return arena.Store(device, stream);
        });
    }

    Sampler DeviceSceneBuilder::CompileSampler(Sampler sampler) {
        if (!sampler) return nullptr;
        return sampler.DispatchHost([&](const auto* host) -> Sampler { return arena.Store(*host, stream); });
    }

    void DeviceSceneBuilder::InitializeRuntimeGlobals() {
        const DenselySampledSpectrum* x = CompileSpectrum(&Spectra::X());
        const DenselySampledSpectrum* y = CompileSpectrum(&Spectra::Y());
        const DenselySampledSpectrum* z = CompileSpectrum(&Spectra::Z());
        const RGBColorSpace* srgb = CompileRGBColorSpace(RGBColorSpace::SRGB());
        const RGBColorSpace* dciP3 = CompileRGBColorSpace(RGBColorSpace::DCI_P3());
        const RGBColorSpace* rec2020 = CompileRGBColorSpace(RGBColorSpace::Rec2020());
        const RGBColorSpace* aces2065_1 = CompileRGBColorSpace(RGBColorSpace::ACES2065_1());
        arena.FinishUploads(stream);
        Spectra::SetDeviceXYZ(x, y, z);
        RGBColorSpace::SetDeviceSpaces(srgb, dciP3, rec2020, aces2065_1);
    }
} // namespace spectra::pathtracer
