// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef SPECTRA_PATHTRACER_WAVEFRONT_INTERSECT_H
#define SPECTRA_PATHTRACER_WAVEFRONT_INTERSECT_H

#include <src/util/float.h>

#include <src/util/spectrum.h>
#include <spectra/pathtracer/wavefront/workitems.h>

namespace spectra
{
    // Wavefront Ray Intersection Enqueuing Functions
    inline PBRT_CPU_GPU void EnqueueWorkAfterMiss(RayWorkItem r,
                                                  MediumSampleQueue* mediumSampleQueue,
                                                  EscapedRayQueue* escapedRayQueue)
    {
        if (r.ray.medium)
        {
            mediumSampleQueue->Push(r, Infinity);
        }
        else if (escapedRayQueue)
        {
            escapedRayQueue->Push(r);
        }
    }

    inline PBRT_CPU_GPU void RecordShadowRayResult(const ShadowRayWorkItem w,
                                                   SOA<PixelSampleState>* pixelSampleState,
                                                   bool foundIntersection)
    {
        if (foundIntersection)
        {
            return;
        }
        SampledSpectrum Ld = w.Ld / (w.r_u + w.r_l).Average();

        SampledSpectrum Lpixel = pixelSampleState->L[w.pixelIndex];
        pixelSampleState->L[w.pixelIndex] = Lpixel + Ld;
    }

    inline PBRT_CPU_GPU void EnqueueWorkAfterIntersection(
        RayWorkItem r, Medium rayMedium, float tMax, SurfaceInteraction intr,
        MediumSampleQueue* mediumSampleQueue, RayQueue* nextRayQueue,
        HitAreaLightQueue* hitAreaLightQueue, MaterialEvalQueue* basicEvalMaterialQueue,
        MaterialEvalQueue* universalEvalMaterialQueue)
    {
        MediumInterface mediumInterface =
            intr.mediumInterface ? *intr.mediumInterface : MediumInterface(rayMedium);

        if (rayMedium)
        {
            assert(mediumSampleQueue);
            mediumSampleQueue->Push(MediumSampleWorkItem{
                r.ray,
                r.depth,
                tMax,
                r.lambda,
                r.beta,
                r.r_u,
                r.r_l,
                r.pixelIndex,
                r.prevIntrCtx,
                r.specularBounce,
                r.anyNonSpecularBounces,
                r.etaScale,
                intr.areaLight,
                intr.pi,
                intr.n,
                intr.dpdu,
                intr.dpdv,
                -r.ray.d,
                intr.uv,
                intr.material,
                intr.shading.n,
                intr.shading.dpdu,
                intr.shading.dpdv,
                intr.shading.dndu,
                intr.shading.dndv,
                intr.faceIndex,
                mediumInterface
            });
            return;
        }

        // FIXME: this is all basically duplicate code w/medium.cpp
        Material material = intr.material;

        const MixMaterial* mix = material.CastOrNullptr<MixMaterial>();
        while (mix)
        {
            MaterialEvalContext ctx(intr);
            material = mix->ChooseMaterial(BasicTextureEvaluator(), ctx);
            mix = material.CastOrNullptr<MixMaterial>();
        }

        if (!material)
        {
            Ray newRay = intr.SpawnRay(r.ray.d);
            nextRayQueue->PushIndirectRay(newRay, r.depth, r.prevIntrCtx, r.beta, r.r_u,
                                          r.r_l, r.lambda, r.etaScale, r.specularBounce,
                                          r.anyNonSpecularBounces, r.pixelIndex);
            return;
        }

        if (intr.areaLight)
        {
            // TODO: intr.wo == -ray.d?
            hitAreaLightQueue->Push(HitAreaLightWorkItem{
                intr.areaLight, intr.p(), intr.n, intr.uv, intr.wo, r.lambda, r.depth, r.beta,
                r.r_u, r.r_l, r.prevIntrCtx, (int)r.specularBounce, r.pixelIndex
            });
        }

        FloatTexture displacement = material.GetDisplacement();

        MaterialEvalQueue* q =
        (material.CanEvaluateTextures(BasicTextureEvaluator()) &&
            (!displacement || BasicTextureEvaluator().CanEvaluate({displacement}, {})))
            ? basicEvalMaterialQueue
            : universalEvalMaterialQueue;

        auto enqueue = [=](auto ptr)
        {
            using Material = typename std::remove_reference_t<decltype(*ptr)>;
            q->Push(MaterialEvalWorkItem<Material>{
                ptr,
                intr.pi,
                intr.n,
                intr.dpdu,
                intr.dpdv,
                intr.time,
                r.depth,
                intr.shading.n,
                intr.shading.dpdu,
                intr.shading.dpdv,
                intr.shading.dndu,
                intr.shading.dndv,
                intr.uv,
                intr.faceIndex,
                r.lambda,
                r.pixelIndex,
                r.anyNonSpecularBounces,
                intr.wo,
                r.beta,
                r.r_u,
                r.etaScale,
                mediumInterface
            });
        };
        material.Dispatch(enqueue);
    }

    struct TransmittanceTraceResult
    {
        bool hit;
        Point3f pHit;
        Material material;
    };

    template <typename T, typename S>
    inline PBRT_CPU_GPU void TraceTransmittance(ShadowRayWorkItem sr,
                                                SOA<PixelSampleState>* pixelSampleState,
                                                T trace, S spawnTo)
    {
        SampledWavelengths lambda = sr.lambda;

        SampledSpectrum Ld = sr.Ld;

        Ray ray = sr.ray;
        Float tMax = sr.tMax;
        Point3f pLight = ray(tMax);
        RNG rng(Hash(ray.o), Hash(ray.d));

        SampledSpectrum T_ray(1.f);
        SampledSpectrum r_u(1.f), r_l(1.f);

        while (ray.d != Vector3f(0, 0, 0))
        {
            TransmittanceTraceResult result = trace(ray, tMax);

            if (result.hit && result.material)
            {
                // Hit opaque surface
                T_ray = SampledSpectrum(0.f);
                break;
            }

            if (ray.medium)
            {
                Float tEnd = !result.hit
                                 ? tMax
                                 : (Distance(ray.o, Point3f(result.pHit)) / Length(ray.d));
                SampledSpectrum T_maj = SampleT_maj(
                    ray, tEnd, rng.Uniform<Float>(), rng, lambda,
                    [&](Point3f p, MediumProperties mp, SampledSpectrum sigma_maj,
                        SampledSpectrum T_maj)
                    {
                        SampledSpectrum sigma_n =
                            ClampZero(sigma_maj - mp.sigma_a - mp.sigma_s);

                        // ratio-tracking: only evaluate null scattering
                        Float pr = T_maj[0] * sigma_maj[0];
                        T_ray *= T_maj * sigma_n / pr;
                        r_l *= T_maj * sigma_maj / pr;
                        r_u *= T_maj * sigma_n / pr;

                        // Possibly terminate transmittance computation using Russian roulette
                        SampledSpectrum Tr = T_ray / (r_l + r_u).Average();
                        if (Tr.MaxComponentValue() < 0.05f)
                        {
                            Float q = 0.75f;
                            if (rng.Uniform<Float>() < q)
                                T_ray = SampledSpectrum(0.);
                        else
                            T_ray /= 1 - q;
                    }

                        if (!T_ray)
                            return false;

                        return true;
                    });
                T_ray *= T_maj / T_maj[0];
                r_l *= T_maj / T_maj[0];
                r_u *= T_maj / T_maj[0];
            }

            if (!result.hit || !T_ray)
                // done
                break;

            ray = spawnTo(pLight);
        }

        if (T_ray)
        {
            // FIXME/reconcile: this takes r_l as input while
            // e.g. VolPathIntegrator::SampleLd() does not...
            Ld *= T_ray / (sr.r_u * r_u + sr.r_l * r_l).Average();

            SampledSpectrum Lpixel = pixelSampleState->L[sr.pixelIndex];
            pixelSampleState->L[sr.pixelIndex] = Lpixel + Ld;
        }
    }
} // namespace spectra

#endif  // PBRT_WAVEFRONT_INTERSECT_H
