export module spectra.editor.viewport.picker;

import spectra.render.gpu_scene;
import spectra.render.contract;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct ViewportPicker {
        struct PickResult {
            bool ready{};
            std::optional<GpuAccelerationEntity> entity{};
            std::optional<scene::NeuralFieldId> neural_field{};
            std::optional<std::uint32_t> diagnostic_pick_index{};
            bool select{};
            bool additive{};
        };

        ViewportPicker(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) noexcept;
        ~ViewportPicker();

        ViewportPicker(const ViewportPicker&)            = delete;
        ViewportPicker(ViewportPicker&&)                 = delete;
        ViewportPicker& operator=(const ViewportPicker&) = delete;
        ViewportPicker& operator=(ViewportPicker&&)      = delete;

        void initialize(scene::SceneView scene);
        void destroy_scene() noexcept;
        void synchronize(scene::SceneView scene, GpuSceneUpdate gpu_update, const vk::raii::CommandBuffer& command_buffer);
        void submit_pick(float normalized_x, float normalized_y, bool select, bool additive) noexcept;
        void cancel_selection_requests() noexcept;
        [[nodiscard]] PickResult take_pick_result(std::uint32_t frame_slot_index) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, scene::SceneView scene, const scene::Camera& camera, DepthBufferView depth, const GpuImage* diagnostic_pick_image);

    private:
        struct PickRequest {
            float normalized_x{};
            float normalized_y{};
            bool select{};
            bool additive{};
        };

        struct PickFrameSlot {
            GpuBuffer result_buffer{};
            DescriptorLease result_descriptor{};
            std::optional<PickRequest> submitted_request{};
            std::vector<GpuAccelerationEntity> acceleration_entities{};
            std::optional<scene::NeuralFieldId> neural_field{};
        };

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            GpuBuffer primitives{};
            DescriptorLease primitives_descriptor{};
            bool initialized{};
        } scene;

        struct {
            vk::raii::ShaderEXT shader{nullptr};
            std::vector<PickFrameSlot> frame_slots{};
            std::optional<PickRequest> pending_request{};
        } picking;

        void upload(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
    };
} // namespace spectra
