#ifndef SPECTRA_PATHTRACER_CORE_RENDER_CONFIG_H
#define SPECTRA_PATHTRACER_CORE_RENDER_CONFIG_H

#include <optional>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra::pathtracer {
    enum class RenderingSpace { Camera, CameraWorld, World };

    struct RuntimeConfig {
        int thread_count{30};
        std::optional<int> cuda_device{};
    };

    struct RenderConfig {
        int seed{};
        bool quiet{};
        bool disable_pixel_jitter{};
        bool disable_wavelength_jitter{};
        bool disable_texture_filtering{};
        RenderingSpace rendering_space{RenderingSpace::CameraWorld};
        std::optional<int> pixel_samples{};
        std::string output_file{};
        std::optional<Bounds2f> crop_window{};
        std::optional<Bounds2i> pixel_bounds{};
        Float displacement_edge_scale{1};
    };
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_CORE_RENDER_CONFIG_H
