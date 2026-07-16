#ifndef SPECTRA_PATHTRACER_GPU_VOLUME_H
#define SPECTRA_PATHTRACER_GPU_VOLUME_H

#include <cstddef>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <memory>
#include <pathtracer/base/medium.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/transform.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    class RGBColorSpace;
}

namespace spectra::pathtracer {
    struct DeviceVolumeChannelBuildInput {
        const float* host_values{};
        const float* device_values{};
        std::size_t source_value_count{};
        std::uint32_t component_count{};
        std::uint32_t first_component{};
        float scale{1.f};
        float bias{};
        bool morton_encoded{};
        cudaEvent_t ready_event{};
    };

    struct DeviceVolumeMediumBuildInput {
        Point3i dimensions{};
        Bounds3f bounds{};
        Transform render_from_medium{};
        const RGBColorSpace* color_space{};
        DeviceVolumeChannelBuildInput density{};
        DeviceVolumeChannelBuildInput color{};
        DeviceVolumeChannelBuildInput emission{};
        bool has_color{};
        bool has_emission{};
    };

    class DeviceVolumeMediumStorage final {
    public:
        DeviceVolumeMediumStorage(const DeviceVolumeMediumBuildInput& input, cudaStream_t stream);
        ~DeviceVolumeMediumStorage() noexcept = default;

        DeviceVolumeMediumStorage(const DeviceVolumeMediumStorage& other)                = delete;
        DeviceVolumeMediumStorage(DeviceVolumeMediumStorage&& other) noexcept            = delete;
        DeviceVolumeMediumStorage& operator=(const DeviceVolumeMediumStorage& other)     = delete;
        DeviceVolumeMediumStorage& operator=(DeviceVolumeMediumStorage&& other) noexcept = delete;

        [[nodiscard]] Medium medium() const noexcept;

    private:
        PathtracerDeviceBuffer density{};
        PathtracerDeviceBuffer color{};
        PathtracerDeviceBuffer emission{};
        PathtracerDeviceBuffer majorants{};
        PathtracerDeviceArena descriptorArena{};
        Medium handle{};
    };
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_GPU_VOLUME_H
