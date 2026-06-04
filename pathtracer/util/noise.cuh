#ifndef SPECTRA_PATHTRACER_UTIL_NOISE_H
#define SPECTRA_PATHTRACER_UTIL_NOISE_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    __host__ __device__ Float Noise(Float x, Float y = .5f, Float z = .5f);
    __host__ __device__ Float Noise(Point3f p);
    __host__ __device__ Vector3f DNoise(Point3f p);
    __host__ __device__ Float FBm(Point3f p, Vector3f dpdx, Vector3f dpdy, Float omega, int octaves);
    __host__ __device__ Float Turbulence(Point3f p, Vector3f dpdx, Vector3f dpdy, Float omega, int octaves);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_NOISE_H
