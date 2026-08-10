export module spectra.render.sampling;

import spectra.runtime;
import std;

namespace spectra {
    export struct SamplingResources {
        SamplingResources(VulkanRuntime& runtime, const std::filesystem::path& resource_directory);
        ~SamplingResources();

        SamplingResources(const SamplingResources&)            = delete;
        SamplingResources(SamplingResources&&)                 = delete;
        SamplingResources& operator=(const SamplingResources&) = delete;
        SamplingResources& operator=(SamplingResources&&)      = delete;

        VulkanRuntime& runtime;
        std::vector<std::uint32_t> table_data{};
        GpuBuffer tables{};
        DescriptorLease tables_descriptor{};
    };
} // namespace spectra
