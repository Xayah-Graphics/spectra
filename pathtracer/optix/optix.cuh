#ifndef SPECTRA_OPTIX_OPTIX_H
#define SPECTRA_OPTIX_OPTIX_H

#include <optix.h>
#include <pathtracer/base/light.cuh>
#include <pathtracer/base/material.cuh>
#include <pathtracer/base/medium.cuh>
#include <pathtracer/base/shape.cuh>
#include <pathtracer/base/texture.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/wavefront/workitems.cuh>
#include <pathtracer/wavefront/workqueue.cuh>

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
