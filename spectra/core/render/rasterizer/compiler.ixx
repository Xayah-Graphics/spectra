export module spectra.render.rasterizer.compiler;

import spectra.render.gpu_scene;
import spectra.render.rasterizer.abi;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

export namespace spectra::render {
    inline constexpr std::uint32_t invalid_raster_index = std::numeric_limits<std::uint32_t>::max();

    struct RasterAreaEmitterRange {
        std::uint32_t scene_primitive_index{};
        std::uint32_t resource_index{};
        std::uint32_t kind{};
        std::uint32_t geometry_kind{};
        std::array<float, 4> geometry_parameters{};
        std::array<float, 4> emission_parameters{};
        std::array<float, 4> radiance{};
    };

    struct RasterTextureCompilation {
        std::vector<RasterTextureHeader> headers{};
        std::vector<RasterTextureMapping> mappings{};
        std::vector<RasterConstantTexture> constants{};
        std::vector<RasterImageTexture> images{};
        std::vector<RasterCompositeTexture> checkerboards{};
        std::vector<RasterCompositeTexture> scales{};
        std::vector<RasterCompositeTexture> mixes{};
        std::vector<RasterDirectionMixTexture> direction_mixes{};
        std::vector<RasterBilerpTexture> bilerps{};
        std::vector<std::uint32_t> handles{};
    };

    struct RasterMaterialCompilation {
        std::vector<RasterMaterial> materials{};
        std::vector<RasterMaterialRange> ranges{};
        std::vector<RasterMaterialTerm> terms{};
        std::vector<RasterMaterialFactor> factors{};
    };

    [[nodiscard]] math::Float3 raster_output_rgb(math::Float3 value, scene::SpectrumColorSpace color_space) noexcept;
    [[nodiscard]] math::Float3 raster_spectrum_rgb(const scene::SpectrumParameter& parameter);
    [[nodiscard]] std::uint32_t raster_texture_source_index(scene::ResolvedSceneView scene, scene::TextureId id);
    [[nodiscard]] RasterTextureCompilation compile_raster_textures(GpuScene& gpu_scene, scene::ResolvedSceneView scene);
    [[nodiscard]] RasterMaterialCompilation compile_raster_materials(scene::ResolvedSceneView scene, const RasterTextureCompilation& textures);
    [[nodiscard]] math::Float3 raster_emission_texture_average(scene::ResolvedSceneView scene, scene::TextureId id);
    void compile_raster_emitters(GpuSceneView gpu_scene, scene::ResolvedSceneView scene, const RasterTextureCompilation& textures, std::vector<RasterAreaLight>& surface_lights, std::vector<RasterAreaEmitterRange>& area_emitters);

    template <typename Element>
    [[nodiscard]] runtime::GpuBuffer upload_raster_buffer(runtime::VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage) {
        runtime::GpuBuffer destination       = runtime.resources.create_buffer(elements.size_bytes(), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        const runtime::GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(elements));
        command_buffer.copyBuffer(upload.buffer, *destination.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
        const vk::BufferMemoryBarrier2 dependency{
            vk::PipelineStageFlagBits2::eCopy,
            vk::AccessFlagBits2::eTransferWrite,
            static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlagBits2::eAllCommands,
            static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlagBits2::eShaderStorageRead,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *destination.buffer,
            0,
            destination.size,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, 1, &dependency});
        return destination;
    }
} // namespace spectra::render
