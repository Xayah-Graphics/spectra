#ifndef SPECTRA_PATHTRACER_DEVICE_SCENE_H
#define SPECTRA_PATHTRACER_DEVICE_SCENE_H

#include <cuda_runtime_api.h>
#include <cstddef>
#include <memory>
#include <mutex>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/pstd.cuh>

namespace spectra {
    class Camera;
    class DenselySampledSpectrum;
    class Film;
    class Filter;
    class FloatTexture;
    class Image;
    class Light;
    class LightSampler;
    class Material;
    struct MeasuredBxDFData;
    class Medium;
    struct MediumInterface;
    class AliasTable;
    class PiecewiseConstant1D;
    class PiecewiseConstant2D;
    class RGBColorSpace;
    class RGBToSpectrumTable;
    class Shape;
    class Sampler;
    class Spectrum;
    class SpectrumTexture;
    class TextureMapping2D;
    class TextureMapping3D;
    class Transform;
    class TriangleMesh;
    class BilinearPatchMesh;
    struct CurveCommon;
    class WindowedPiecewiseConstant2D;
    template <std::size_t Dimension>
    class PiecewiseLinear2D;

    namespace pathtracer {
        class DeviceSceneBuilder final {
        public:
            DeviceSceneBuilder(PathtracerDeviceArena& arena, cudaStream_t stream);
            ~DeviceSceneBuilder() noexcept;

            DeviceSceneBuilder(const DeviceSceneBuilder&) = delete;
            DeviceSceneBuilder(DeviceSceneBuilder&&) noexcept = delete;
            DeviceSceneBuilder& operator=(const DeviceSceneBuilder&) = delete;
            DeviceSceneBuilder& operator=(DeviceSceneBuilder&&) noexcept = delete;

            void InitializeRuntimeGlobals();

            [[nodiscard]] Spectrum CompileSpectrum(Spectrum spectrum);
            [[nodiscard]] const DenselySampledSpectrum* CompileSpectrum(const DenselySampledSpectrum* spectrum);
            [[nodiscard]] const RGBToSpectrumTable* CompileRGBToSpectrumTable(const RGBToSpectrumTable* table);
            [[nodiscard]] const RGBColorSpace* CompileRGBColorSpace(const RGBColorSpace* colorSpace);
            [[nodiscard]] TextureMapping2D CompileTextureMapping(TextureMapping2D mapping);
            [[nodiscard]] TextureMapping3D CompileTextureMapping(TextureMapping3D mapping);
            [[nodiscard]] FloatTexture CompileFloatTexture(FloatTexture texture);
            [[nodiscard]] SpectrumTexture CompileSpectrumTexture(SpectrumTexture texture);
            [[nodiscard]] Image* CompileImage(const Image* image);
            [[nodiscard]] Material CompileMaterial(Material material);
            [[nodiscard]] Medium CompileMedium(Medium medium);
            [[nodiscard]] MediumInterface* CompileMediumInterface(const MediumInterface* mediumInterface);
            [[nodiscard]] const TriangleMesh* CompileTriangleMesh(const TriangleMesh* mesh);
            [[nodiscard]] const BilinearPatchMesh* CompileBilinearPatchMesh(const BilinearPatchMesh* mesh);
            [[nodiscard]] Shape CompileShape(Shape shape);
            [[nodiscard]] Light CompileLight(Light light);
            [[nodiscard]] const Light* CompileLights(pstd::span<const Light> lights);
            [[nodiscard]] LightSampler CompileLightSampler(LightSampler sampler);
            [[nodiscard]] Camera CompileCamera(Camera camera);
            [[nodiscard]] Film CompileFilm(Film film);
            [[nodiscard]] Filter CompileFilter(Filter filter);
            [[nodiscard]] Sampler CompileSampler(Sampler sampler);

        private:
            [[nodiscard]] PiecewiseConstant1D CompileDistribution(const PiecewiseConstant1D& distribution);
            [[nodiscard]] PiecewiseConstant2D CompileDistribution(const PiecewiseConstant2D& distribution);
            [[nodiscard]] WindowedPiecewiseConstant2D CompileDistribution(const WindowedPiecewiseConstant2D& distribution);
            [[nodiscard]] AliasTable CompileAliasTable(const AliasTable& table);
            template <std::size_t Dimension>
            [[nodiscard]] PiecewiseLinear2D<Dimension> CompileDistribution(const PiecewiseLinear2D<Dimension>& distribution);
            [[nodiscard]] const MeasuredBxDFData* CompileMeasuredBxDF(const MeasuredBxDFData* data);
            [[nodiscard]] const Transform* CompileTransform(const Transform* transform);
            [[nodiscard]] const CurveCommon* CompileCurveCommon(const CurveCommon* common);
            [[nodiscard]] Image CompileImageValue(const Image& image);

            struct State;

            PathtracerDeviceArena& arena;
            cudaStream_t stream{};
            std::recursive_mutex mutex{};
            std::unique_ptr<State> state{};
        };
    } // namespace pathtracer
} // namespace spectra

#endif // SPECTRA_PATHTRACER_DEVICE_SCENE_H
