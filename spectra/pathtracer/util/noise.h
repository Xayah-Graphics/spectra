#ifndef SPECTRA_PATHTRACER_UTIL_NOISE_H
#define SPECTRA_PATHTRACER_UTIL_NOISE_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/vecmath.h>

namespace spectra
{
    SPECTRA_CPU_GPU
    Float Noise(Float x, Float y = .5f, Float z = .5f);
    SPECTRA_CPU_GPU
    Float Noise(Point3f p);
    SPECTRA_CPU_GPU
    Vector3f DNoise(Point3f p);
    SPECTRA_CPU_GPU
    Float FBm(Point3f p, Vector3f dpdx, Vector3f dpdy, Float omega, int octaves);
    SPECTRA_CPU_GPU
    Float Turbulence(Point3f p, Vector3f dpdx, Vector3f dpdy, Float omega, int octaves);
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_NOISE_H
