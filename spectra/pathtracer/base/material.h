#ifndef SPECTRA_PATHTRACER_BASE_MATERIAL_H
#define SPECTRA_PATHTRACER_BASE_MATERIAL_H

#include <map>
#include <spectra/pathtracer/base/bssrdf.h>
#include <spectra/pathtracer/base/texture.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <string>

namespace spectra {
    class BSDF;
    class Image;
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
            /*const */ std::map<std::string, Material>& namedMaterials, const FileLoc* loc, Allocator alloc);


        template <typename TextureEvaluator>
        inline BSDF GetBSDF(TextureEvaluator texEval, MaterialEvalContext ctx, SampledWavelengths& lambda, ScratchBuffer& buf) const;

        template <typename TextureEvaluator>
        inline BSSRDF GetBSSRDF(TextureEvaluator texEval, MaterialEvalContext ctx, SampledWavelengths& lambda, ScratchBuffer& buf) const;

        template <typename TextureEvaluator>
        SPECTRA_CPU_GPU inline bool CanEvaluateTextures(TextureEvaluator texEval) const;

        SPECTRA_CPU_GPU inline const Image* GetNormalMap() const;

        SPECTRA_CPU_GPU inline FloatTexture GetDisplacement() const;

        SPECTRA_CPU_GPU inline bool HasSubsurfaceScattering() const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_MATERIAL_H
