module;

#include <imgui.h>

export module spectra.editor.ui.imgui;

import spectra.editor.platform.window;
import spectra.render.types;
import spectra.runtime;
import std;
import vulkan;

namespace spectra::editor {
    export struct ImGuiBackend {
        ImGuiBackend(WindowPlatform& platform, runtime::VulkanRuntime& runtime, std::filesystem::path shader_directory);
        ~ImGuiBackend();

        ImGuiBackend(const ImGuiBackend&)            = delete;
        ImGuiBackend(ImGuiBackend&&)                 = delete;
        ImGuiBackend& operator=(const ImGuiBackend&) = delete;
        ImGuiBackend& operator=(ImGuiBackend&&)      = delete;

        void begin_frame();
        void end_frame();
        void bind_viewport(render::RenderOutput output) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::ImageLayout target_layout, vk::ImageLayout final_layout);
        std::uint64_t viewport_texture_id{};
        vk::Extent2D viewport_extent{};

    private:
        struct FrameResources {
            runtime::GpuBuffer vertex_buffer{};
            runtime::GpuBuffer index_buffer{};
            runtime::DescriptorLease vertex_descriptor{};
            runtime::DescriptorLease index_descriptor{};
            std::size_t vertex_capacity{};
            std::size_t index_capacity{};
        };

        struct {
            WindowPlatform& platform;
            runtime::VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXTs shaders{nullptr};
            std::vector<FrameResources> frames{};
            runtime::DescriptorLease sampler_descriptor{};
        } renderer;

        void initialize_renderer();
        void update_texture(ImTextureData& texture, const vk::raii::CommandBuffer& command_buffer);
        void destroy_texture(ImTextureData& texture);
        void setup_render_state(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
    };
} // namespace spectra::editor
