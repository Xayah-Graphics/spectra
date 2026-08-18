export module spectra.render.display.neural_field;

import spectra.render.display.types;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

export namespace spectra::render {
    struct NeuralFieldPass {
        NeuralFieldPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~NeuralFieldPass();

        NeuralFieldPass(const NeuralFieldPass&)            = delete;
        NeuralFieldPass(NeuralFieldPass&&)                 = delete;
        NeuralFieldPass& operator=(const NeuralFieldPass&) = delete;
        NeuralFieldPass& operator=(NeuralFieldPass&&)      = delete;

        [[nodiscard]] bool has_visible(scene::ResolvedSceneView scene) const noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, ColorTarget target, DepthBufferView depth, scene::ResolvedSceneView scene, const scene::Camera& camera);

        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        struct OptimizedMatrix {
            runtime::GpuBuffer buffer{};
            runtime::DescriptorLease descriptor{};
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
} // namespace spectra::render
