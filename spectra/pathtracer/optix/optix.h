// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef SPECTRA_OPTIX_OPTIX_H
#define SPECTRA_OPTIX_OPTIX_H

#include <src/util/float.h>

#include <src/base/light.h>
#include <src/base/material.h>
#include <src/base/medium.h>
#include <src/base/shape.h>
#include <src/base/texture.h>
#include <src/util/pstd.h>
#include <spectra/pathtracer/wavefront/workitems.h>
#include <spectra/pathtracer/wavefront/workqueue.h>

#include <optix.h>

namespace spectra
{
    class TriangleMesh;
    class BilinearPatchMesh;
} // namespace spectra

namespace spectra::optix
{
    inline constexpr OptixPayloadTypeID SpectraOptiXPayloadType = OPTIX_PAYLOAD_TYPE_ID_0;

    struct TriangleMeshRecord
    {
        const TriangleMesh* mesh;
        Material material;
        FloatTexture alphaTexture;
        pstd::span<Light> areaLights;
        MediumInterface* mediumInterface;
    };

    struct BilinearMeshRecord
    {
        const BilinearPatchMesh* mesh;
        Material material;
        FloatTexture alphaTexture;
        pstd::span<Light> areaLights;
        MediumInterface* mediumInterface;
    };

    struct QuadricRecord
    {
        Shape shape;
        Material material;
        FloatTexture alphaTexture;
        Light areaLight;
        MediumInterface* mediumInterface;
    };

    struct RayIntersectParameters
    {
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

#endif  // SPECTRA_OPTIX_OPTIX_H
