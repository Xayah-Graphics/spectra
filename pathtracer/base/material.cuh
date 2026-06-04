#ifndef SPECTRA_PATHTRACER_BASE_MATERIAL_H
#define SPECTRA_PATHTRACER_BASE_MATERIAL_H

#include <map>
#include <pathtracer/base/bssrdf.cuh>
#include <pathtracer/base/texture.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <string>

namespace spectra {
    class BSDF;
    class Image;
    struct MeasuredBxDFData;
    class SampledWavelengths;
    class TextureParameterDictionary;
    struct FileLoc;

    struct MaterialEvalContext;

    // Material Declarations
    class CoatedDiffuseMaterial;
    class CoatedConductorMaterial;
    class ConductorMaterial;
    class DielectricMaterial;
    class DiffuseMaterial;
    class DiffuseTransmissionMaterial;
    class HairMaterial;
    class MeasuredMaterial;
    class SubsurfaceMaterial;
    class ThinDielectricMaterial;
    class MixMaterial;

    // Material Definition
    class Material : public TaggedPointer< // Material Types
                         CoatedDiffuseMaterial, CoatedConductorMaterial, ConductorMaterial, DielectricMaterial, DiffuseMaterial, DiffuseTransmissionMaterial, HairMaterial, MeasuredMaterial, SubsurfaceMaterial, ThinDielectricMaterial, MixMaterial

                         > {
    public:
        // Material Interface
        using TaggedPointer::TaggedPointer;

        static Material Create(const std::string& name, const TextureParameterDictionary& parameters, Image* normalMap,
            /*const */ std::map<std::string, Material>& namedMaterials, std::map<std::string, MeasuredBxDFData*>& measuredBxDFData, const FileLoc* loc, Allocator alloc);


        template <typename TextureEvaluator>
        inline BSDF GetBSDF(TextureEvaluator texEval, MaterialEvalContext ctx, SampledWavelengths& lambda, ScratchBuffer& buf) const;

        template <typename TextureEvaluator>
        inline BSSRDF GetBSSRDF(TextureEvaluator texEval, MaterialEvalContext ctx, SampledWavelengths& lambda, ScratchBuffer& buf) const;

        template <typename TextureEvaluator>
        __host__ __device__ inline bool CanEvaluateTextures(TextureEvaluator texEval) const;

        __host__ __device__ inline const Image* GetNormalMap() const;

        __host__ __device__ inline FloatTexture GetDisplacement() const;

        __host__ __device__ inline bool HasSubsurfaceScattering() const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_MATERIAL_H
