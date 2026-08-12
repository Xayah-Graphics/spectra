export module spectra.render.contract;

import spectra.runtime;
import spectra.render.gpu_scene;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export enum class RenderOutputLayer : std::uint8_t {
        RendererLinear,
        RendererDisplay,
        ComposedDisplay,
    };

    export [[nodiscard]] RenderOutputLayer parse_render_output_layer(std::string_view identifier);

    export struct RenderOutput {
        const GpuImage& image;
        DescriptorHandle sampled_descriptor{};
        vk::ImageLayout image_layout{};
        vk::PipelineStageFlags2 source_stage{};
        vk::AccessFlags2 source_access{};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
        float exposure{};
    };

    export struct RenderGBufferReadback {
        vk::Extent2D extent{};
        std::uint32_t accumulated_samples{};
        std::vector<math::Float4> radiance{};
        std::vector<math::Float3> albedo{};
        std::vector<math::Float3> shading_normals{};
        std::vector<math::Float3> geometric_normals{};
        std::vector<math::Float3> positions{};
        std::vector<float> depths{};
        std::vector<math::Float2> texture_coordinates{};
        std::vector<std::uint64_t> object_ids{};
        std::vector<std::uint32_t> primitive_ids{};
        std::vector<std::uint64_t> material_ids{};
    };

    export struct RendererDescriptor {
        std::string_view id{};
        std::string_view name{};

        auto operator<=>(const RendererDescriptor&) const = default;
    };

    export inline constexpr RendererDescriptor rasterizer_descriptor{"rasterizer", "Rasterizer"};
    export inline constexpr RendererDescriptor pathtracer_descriptor{"pathtracer", "Path Tracer"};

    export enum class RasterDisplayMode : std::uint8_t {
        Material,
        Wireframe,
    };

    export [[nodiscard]] RasterDisplayMode parse_raster_display_mode(std::string_view identifier);
    export void set_basic_graphics_state(const vk::raii::CommandBuffer& command_buffer);

    export struct RenderView {
        scene::Camera camera{};
        vk::Extent2D extent{};
        std::uint64_t camera_revision{};
    };

    export struct RenderProgress {
        std::uint32_t completed{};
        std::uint32_t target{};
        bool paused{};
    };

    export enum class PathTracerPreparationStage : std::uint8_t {
        LoadingShaders,
        CreatingRayTracingModules,
        CompilingRayTracingPipeline,
        CreatingComputeShaders,
        CreatingShaderBindingTable,
        CompilingSampler,
        CompilingFilter,
        CompilingTextures,
        CompilingMaterials,
        CompilingMedia,
        CompilingLights,
        CompilingGeometry,
        BuildingLightBvh,
        AssemblingScene,
        UploadingScene,
        AllocatingRenderSession,
        Ready,
    };

    export struct PathTracerPreparationProgress {
        PathTracerPreparationStage stage{PathTracerPreparationStage::LoadingShaders};
        std::uint32_t completed{};
        std::uint32_t total{};
        std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    };

    export struct DepthBufferView {
        const GpuImage& image;
        DescriptorHandle descriptor{};
        vk::ImageLayout& layout;
    };

    export struct ColorCompositionTarget {
        GpuImage& image;
        vk::ImageLayout& layout;
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
    };

} // namespace spectra
