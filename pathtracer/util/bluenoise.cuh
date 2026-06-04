#ifndef SPECTRA_PATHTRACER_UTIL_BLUENOISE_H
#define SPECTRA_PATHTRACER_UTIL_BLUENOISE_H

#include <pathtracer/util/check.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    static constexpr int BlueNoiseResolution  = 128;
    static constexpr int NumBlueNoiseTextures = 48;

#if defined(__CUDA_ARCH__)
    extern __device__ const uint16_t BlueNoiseTextures[NumBlueNoiseTextures][BlueNoiseResolution][BlueNoiseResolution];
#else
    extern const uint16_t BlueNoiseTextures[NumBlueNoiseTextures][BlueNoiseResolution][BlueNoiseResolution];
#endif

    // Blue noise lookup functions
    __host__ __device__ inline float BlueNoise(int tableIndex, Point2i p);

    __host__ __device__ inline float BlueNoise(int textureIndex, Point2i p) {
        CHECK(textureIndex >= 0 && p.x >= 0 && p.y >= 0);
        textureIndex %= NumBlueNoiseTextures;
        int x = p.x % BlueNoiseResolution, y = p.y % BlueNoiseResolution;
        return BlueNoiseTextures[textureIndex][x][y] / 65535.f;
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_BLUENOISE_H
