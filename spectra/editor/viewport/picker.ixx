export module spectra.editor:viewport.picker;

import spectra.render;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct ViewportPicker {
        struct PickRequest {
            float normalized_x{};
            float normalized_y{};
            bool select{};
            bool additive{};
            std::optional<std::uint64_t> debug_object_id{};
            bool debug_xray{};
        };

        struct PickResult {
            bool ready{};
            std::optional<std::uint32_t> acceleration_instance_index{};
            std::optional<std::uint64_t> debug_object_id{};
            bool debug_xray{};
            bool select{};
            bool additive{};
        };

        struct PickFrameSlot {
            GpuBuffer result_buffer{};
            DescriptorHandle result_descriptor{};
            std::optional<PickRequest> submitted_request{};
        };

        ViewportPicker(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) noexcept;
        ~ViewportPicker();

        ViewportPicker(const ViewportPicker&)            = delete;
        ViewportPicker(ViewportPicker&&)                 = delete;
        ViewportPicker& operator=(const ViewportPicker&) = delete;
        ViewportPicker& operator=(ViewportPicker&&)      = delete;

        void initialize(scene::SceneView scene);
        void destroy_scene() noexcept;
        void synchronize(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void submit_pick(float normalized_x, float normalized_y, bool select, bool additive, std::optional<std::uint64_t> debug_object_id, bool debug_xray) noexcept;
        [[nodiscard]] PickResult take_pick_result(std::uint32_t frame_slot_index) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, const scene::Camera& camera);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitives{};
            DescriptorHandle primitives_descriptor{};
            bool initialized{};
        } scene;

        struct {
            vk::raii::ShaderEXT shader{nullptr};
            std::vector<PickFrameSlot> frame_slots{};
            std::optional<PickRequest> pending_request{};
        } picking;

    private:
        void upload(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
    };
} // namespace spectra
