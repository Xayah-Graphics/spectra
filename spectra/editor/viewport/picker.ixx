export module spectra.editor.viewport.picker;

import spectra.render.gpu_scene;
import spectra.render.types;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra::editor {
    export struct Picker {
        struct PickResult {
            bool ready{};
            std::optional<render::GpuAccelerationEntity> entity{};
            std::optional<scene::NeuralFieldId> neural_field{};
            std::optional<std::uint32_t> diagnostic_pick_index{};
            bool select{};
            bool additive{};
        };

        Picker(runtime::VulkanRuntime& runtime, render::GpuScene& gpu_scene, std::filesystem::path shader_directory) noexcept;
        ~Picker();

        Picker(const Picker&)            = delete;
        Picker(Picker&&)                 = delete;
        Picker& operator=(const Picker&) = delete;
        Picker& operator=(Picker&&)      = delete;

        void initialize(scene::ResolvedSceneView scene);
        void destroy_scene() noexcept;
        void synchronize(scene::ResolvedSceneView scene, render::GpuSceneUpdate gpu_update, const vk::raii::CommandBuffer& command_buffer);
        void submit_pick(float normalized_x, float normalized_y, bool select, bool additive) noexcept;
        void cancel_selection_requests() noexcept;
        [[nodiscard]] PickResult take_pick_result(std::uint32_t frame_slot_index) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, scene::ResolvedSceneView scene, const scene::Camera& camera, render::DepthBufferView depth, const runtime::GpuImage* diagnostic_pick_image);

    private:
        struct PickRequest {
            float normalized_x{};
            float normalized_y{};
            bool select{};
            bool additive{};
        };

        struct PickFrameSlot {
            runtime::GpuBuffer result_buffer{};
            runtime::DescriptorLease result_descriptor{};
            std::optional<PickRequest> submitted_request{};
            std::vector<render::GpuAccelerationEntity> acceleration_entities{};
            std::optional<scene::NeuralFieldId> neural_field{};
        };

        struct {
            runtime::VulkanRuntime& runtime;
            render::GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            runtime::GpuBuffer primitives{};
            runtime::DescriptorLease primitives_descriptor{};
            bool initialized{};
        } scene;

        struct {
            vk::raii::ShaderEXT shader{nullptr};
            std::vector<PickFrameSlot> frame_slots{};
            std::optional<PickRequest> pending_request{};
        } picking;

        void upload(scene::ResolvedSceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
    };
} // namespace spectra::editor
