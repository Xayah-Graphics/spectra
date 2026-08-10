export module spectra.render.pathtracer.resources;

import spectra.runtime;
import spectra.render.contract;

import std;
import vulkan;

namespace spectra {
    export struct PathTracerPreparationState {
        void report(PathTracerPreparationStage stage, std::uint32_t completed = 0, std::uint32_t total = 0);
        [[nodiscard]] PathTracerPreparationProgress snapshot() const;

    private:
        mutable std::mutex mutex{};
        PathTracerPreparationProgress progress{};
    };

    export enum class PathTracerComputeShader : std::size_t {
        VolumeMajorant,
        GenerateCameraRays,
        EvaluateSurfaceTextures,
        RecordSurfaceGBuffer,
        SampleDirectLighting,
        ShadeSurfaces,
        ResolveVisibility,
        AccumulateFilm,
    };

    export struct PathTracerResources {
        PathTracerResources(VulkanRuntime& runtime, const std::filesystem::path& resource_directory);
        ~PathTracerResources();

        PathTracerResources(const PathTracerResources&)            = delete;
        PathTracerResources(PathTracerResources&&)                 = delete;
        PathTracerResources& operator=(const PathTracerResources&) = delete;
        PathTracerResources& operator=(PathTracerResources&&)      = delete;

        [[nodiscard]] const vk::raii::ShaderEXT& shader(PathTracerComputeShader shader) const noexcept;
        [[nodiscard]] bool complete_preparation();
        void wait_for_preparation();
        [[nodiscard]] PathTracerPreparationProgress preparation_progress() const;

        VulkanRuntime& runtime;
        std::vector<std::uint32_t> rgb_to_spectrum_table_data{};
        std::vector<std::uint32_t> sampling_table_data{};
        std::vector<float> cie_samples{};
        GpuBuffer static_data{};
        DescriptorHandle zero_volume_field_descriptor{};
        DescriptorHandle cie_spectra_descriptor{};
        DescriptorHandle rgb_to_spectrum_tables_descriptor{};
        DescriptorHandle sampling_tables_descriptor{};
        std::vector<vk::raii::ShaderEXT> compute_shaders{};
        vk::raii::Pipeline pipeline{nullptr};
        std::vector<std::byte> shader_group_handles{};
        GpuBuffer shader_binding_table{};
        vk::StridedDeviceAddressRegionKHR surface_ray_generation_region{};
        vk::StridedDeviceAddressRegionKHR shadow_ray_generation_region{};
        vk::StridedDeviceAddressRegionKHR miss_region{};
        vk::StridedDeviceAddressRegionKHR hit_region{};
        std::uint32_t stack_size{};
        std::future<void> shader_preparation{};
        PathTracerPreparationState preparation{};
    };
} // namespace spectra
