#ifndef SPECTRA_PATHTRACER_BASE_TEXTURE_H
#define SPECTRA_PATHTRACER_BASE_TEXTURE_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <string>

namespace spectra {
    namespace pathtracer {
        class PathtracerTextureCache;
    }

    class TextureParameterDictionary;
    class SampledSpectrum;
    class SampledWavelengths;
    class Transform;
    enum class SpectrumType;
    struct FileLoc;

    struct TextureEvalContext;

    class FloatConstantTexture;
    class FloatBilerpTexture;
    class FloatCheckerboardTexture;
    class FloatDotsTexture;
    class FBmTexture;
    class GPUFloatImageTexture;
    class FloatMixTexture;
    class FloatDirectionMixTexture;
    class GPUFloatPtexTexture;
    class FloatScaledTexture;
    class WindyTexture;
    class WrinkledTexture;

    // FloatTexture Definition
    class FloatTexture : public TaggedPointer< // FloatTextures
                             GPUFloatImageTexture, FloatMixTexture, FloatDirectionMixTexture, FloatScaledTexture, FloatConstantTexture, FloatBilerpTexture, FloatCheckerboardTexture, FloatDotsTexture, FBmTexture, GPUFloatPtexTexture, WindyTexture, WrinkledTexture

                             > {
    public:
        // FloatTexture Interface
        using TaggedPointer::TaggedPointer;

        static FloatTexture Create(const std::string& name, const Transform& renderFromTexture, const TextureParameterDictionary& parameters, const FileLoc* loc, pathtracer::PathtracerTextureCache& textureCache, Allocator alloc);


        __host__ __device__ inline Float Evaluate(TextureEvalContext ctx) const;
    };

    class RGBConstantTexture;
    class RGBReflectanceConstantTexture;
    class SpectrumConstantTexture;
    class SpectrumBilerpTexture;
    class SpectrumCheckerboardTexture;
    class GPUSpectrumImageTexture;
    class MarbleTexture;
    class SpectrumMixTexture;
    class SpectrumDirectionMixTexture;
    class SpectrumDotsTexture;
    class GPUSpectrumPtexTexture;
    class SpectrumScaledTexture;

    // SpectrumTexture Definition
    class SpectrumTexture : public TaggedPointer< // SpectrumTextures
                                GPUSpectrumImageTexture, SpectrumMixTexture, SpectrumDirectionMixTexture, SpectrumScaledTexture, SpectrumConstantTexture, SpectrumBilerpTexture, SpectrumCheckerboardTexture, MarbleTexture, SpectrumDotsTexture, GPUSpectrumPtexTexture

                                > {
    public:
        // SpectrumTexture Interface
        using TaggedPointer::TaggedPointer;

        static SpectrumTexture Create(const std::string& name, const Transform& renderFromTexture, const TextureParameterDictionary& parameters, SpectrumType spectrumType, const FileLoc* loc, pathtracer::PathtracerTextureCache& textureCache, Allocator alloc);


        __host__ __device__ inline SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_TEXTURE_H
