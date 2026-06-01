#ifndef SPECTRA_PATHTRACER_BASE_TEXTURE_H
#define SPECTRA_PATHTRACER_BASE_TEXTURE_H

#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <string>

namespace spectra {
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
    class FloatImageTexture;
    class FloatMixTexture;
    class FloatDirectionMixTexture;
    class FloatPtexTexture;
    class GPUFloatPtexTexture;
    class FloatScaledTexture;
    class WindyTexture;
    class WrinkledTexture;

    // FloatTexture Definition
    class FloatTexture : public TaggedPointer< // FloatTextures
                             FloatImageTexture, GPUFloatImageTexture, FloatMixTexture, FloatDirectionMixTexture, FloatScaledTexture, FloatConstantTexture, FloatBilerpTexture, FloatCheckerboardTexture, FloatDotsTexture, FBmTexture, FloatPtexTexture, GPUFloatPtexTexture, WindyTexture, WrinkledTexture

                             > {
    public:
        // FloatTexture Interface
        using TaggedPointer::TaggedPointer;

        static FloatTexture Create(const std::string& name, const Transform& renderFromTexture, const TextureParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);


        __host__ __device__ inline Float Evaluate(TextureEvalContext ctx) const;
    };

    class RGBConstantTexture;
    class RGBReflectanceConstantTexture;
    class SpectrumConstantTexture;
    class SpectrumBilerpTexture;
    class SpectrumCheckerboardTexture;
    class SpectrumImageTexture;
    class GPUSpectrumImageTexture;
    class MarbleTexture;
    class SpectrumMixTexture;
    class SpectrumDirectionMixTexture;
    class SpectrumDotsTexture;
    class SpectrumPtexTexture;
    class GPUSpectrumPtexTexture;
    class SpectrumScaledTexture;

    // SpectrumTexture Definition
    class SpectrumTexture : public TaggedPointer< // SpectrumTextures
                                SpectrumImageTexture, GPUSpectrumImageTexture, SpectrumMixTexture, SpectrumDirectionMixTexture, SpectrumScaledTexture, SpectrumConstantTexture, SpectrumBilerpTexture, SpectrumCheckerboardTexture, MarbleTexture, SpectrumDotsTexture, SpectrumPtexTexture, GPUSpectrumPtexTexture

                                > {
    public:
        // SpectrumTexture Interface
        using TaggedPointer::TaggedPointer;

        static SpectrumTexture Create(const std::string& name, const Transform& renderFromTexture, const TextureParameterDictionary& parameters, SpectrumType spectrumType, const FileLoc* loc, Allocator alloc);


        __host__ __device__ inline SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_TEXTURE_H
