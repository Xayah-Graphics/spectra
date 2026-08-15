export module spectra.render.composition.neural_field;

import spectra.render.contract;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct NeuralFieldRenderer {
        NeuralFieldRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~NeuralFieldRenderer();

        NeuralFieldRenderer(const NeuralFieldRenderer&)            = delete;
        NeuralFieldRenderer(NeuralFieldRenderer&&)                 = delete;
        NeuralFieldRenderer& operator=(const NeuralFieldRenderer&) = delete;
        NeuralFieldRenderer& operator=(NeuralFieldRenderer&&)      = delete;

        [[nodiscard]] bool has_visible(scene::SceneView scene) const noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, ColorCompositionTarget target, DepthBufferView depth, scene::SceneView scene, const scene::Camera& camera);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct OptimizedMatrix {
            GpuBuffer buffer{};
            DescriptorLease descriptor{};
            std::size_t size{};
        };

        struct Model {
            OptimizedMatrix density_input{};
            OptimizedMatrix density_output{};
            OptimizedMatrix rgb_input{};
            OptimizedMatrix rgb_hidden{};
            OptimizedMatrix rgb_output{};
            scene::NeuralFieldId neural_field_id{};
            std::uint64_t revision{};
        };

        vk::raii::ShaderEXT render_shader{nullptr};
        std::optional<Model> model{};

    private:
        void synchronize_model(const vk::raii::CommandBuffer& command_buffer, const GpuNeuralField& source);
    };
} // namespace spectra
