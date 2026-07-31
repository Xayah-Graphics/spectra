export module spectra.render.output;

import spectra;
import spectra.scene;
import std;
import vulkan;

namespace spectra::render {
    export struct RenderOutput {
        const GpuImage& image;
        DescriptorHandle sampled_descriptor{};
        vk::ImageLayout layout{};
        vk::PipelineStageFlags2 stage{};
        vk::AccessFlags2 access{};
    };

    export struct RenderReadback {
        vk::Extent2D extent{};
        std::uint32_t accumulated_samples{};
        std::vector<scene::Float4> radiance{};
        std::vector<scene::Float3> albedo{};
        std::vector<scene::Float3> shading_normals{};
        std::vector<scene::Float3> geometric_normals{};
        std::vector<scene::Float3> positions{};
        std::vector<float> depths{};
        std::vector<scene::Float2> texture_coordinates{};
        std::vector<std::uint64_t> object_ids{};
        std::vector<std::uint32_t> primitive_ids{};
        std::vector<std::uint64_t> material_ids{};
        std::vector<std::uint8_t> valid{};
    };
} // namespace spectra::render
