#ifndef SPECTRA_OPTIX_OPTIX_H
#define SPECTRA_OPTIX_OPTIX_H

#include <optix.h>
#include <spectra/pathtracer/base/light.cuh>
#include <spectra/pathtracer/base/material.cuh>
#include <spectra/pathtracer/base/medium.cuh>
#include <spectra/pathtracer/base/shape.cuh>
#include <spectra/pathtracer/base/texture.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/wavefront/workitems.cuh>
#include <spectra/pathtracer/wavefront/workqueue.cuh>

namespace spectra {
    class TriangleMesh;
    class BilinearPatchMesh;
} // namespace spectra

namespace spectra::optix {
    inline constexpr OptixPayloadTypeID SpectraOptiXPayloadType = OPTIX_PAYLOAD_TYPE_ID_0;

    struct TriangleMeshRecord {
        const TriangleMesh* mesh;
        Material material;
        FloatTexture alphaTexture;
        pstd::span<Light> areaLights;
        MediumInterface* mediumInterface;
    };

    struct BilinearMeshRecord {
        const BilinearPatchMesh* mesh;
        Material material;
        FloatTexture alphaTexture;
        pstd::span<Light> areaLights;
        MediumInterface* mediumInterface;
    };

    struct QuadricRecord {
        Shape shape;
        Material material;
        FloatTexture alphaTexture;
        Light areaLight;
        MediumInterface* mediumInterface;
    };

    struct RayIntersectParameters {
        OptixTraversableHandle traversable;

        const RayQueue* rayQueue;

        // closest hit
        RayQueue* nextRayQueue;
        EscapedRayQueue* escapedRayQueue;
        HitAreaLightQueue* hitAreaLightQueue;
        MaterialEvalQueue *basicEvalMaterialQueue, *universalEvalMaterialQueue;
        MediumSampleQueue* mediumSampleQueue;

        // shadow rays
        ShadowRayQueue* shadowRayQueue;
        SOA<PixelSampleState> pixelSampleState;

        // Subsurface scattering...
        SubsurfaceScatterQueue* subsurfaceScatterQueue;
    };
} // namespace spectra::optix

#endif // SPECTRA_OPTIX_OPTIX_H
