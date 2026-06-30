module;

export module spectra.pathtracer.renderer;

export import spectra.pathtracer.host;
export import spectra.scene;
export import vulkan;

import std;

extern "C++" {
    namespace pstd::pmr {
        class memory_resource;
    }

    namespace spectra::pathtracer {
        class CompiledScene;
        struct RenderConfig;
    } // namespace spectra::pathtracer
}

namespace spectra::pathtracer {
    export {
        struct SceneSupportReport {
            std::string target{};
            bool supported{true};
            std::vector<scene::Scene::Diagnostic> diagnostics{};
        };

        [[nodiscard]] SceneSupportReport AnalyzeSceneSupport(const scene::Scene::ResolvedScene& scene);
        [[nodiscard]] std::unique_ptr<CompiledScene> CompileScene(const scene::Scene::ResolvedScene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource);

        class Renderer final {
        public:
            Renderer(std::shared_ptr<scene::Scene> source_scene, std::shared_ptr<scene::CameraWorkspace> camera_workspace);
            ~Renderer() noexcept;

            Renderer(const Renderer& other) = delete;
            Renderer(Renderer&& other) noexcept;
            Renderer& operator=(const Renderer& other) = delete;
            Renderer& operator=(Renderer&& other) noexcept;

            [[nodiscard]] static std::string_view name();
            void set_scene(std::shared_ptr<scene::Scene> source_scene, std::shared_ptr<scene::CameraWorkspace> camera_workspace);
            void attach(HostView host);
            template <Host HostType>
            void attach(HostType& host) {
                this->attach(HostView{host});
            }
            void detach() noexcept;
            void before_imgui_shutdown() noexcept;
            void after_imgui_created();
            [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
            template <Host HostType, typename HostFrameContext>
                requires requires(const HostFrameContext& frame) {
                    { frame.frame_slot_index } -> std::convertible_to<std::uint32_t>;
                    { frame.image_index } -> std::convertible_to<std::uint32_t>;
                    { frame.frame_number } -> std::convertible_to<std::uint64_t>;
                    { frame.delta_seconds } -> std::convertible_to<double>;
                }
            [[nodiscard]] FrameResult begin_frame(HostType& host, const HostFrameContext& frame) {
                return this->begin_frame(HostView{host}, FrameContext{
                    .frame_index   = static_cast<std::uint32_t>(frame.frame_slot_index),
                    .image_index   = static_cast<std::uint32_t>(frame.image_index),
                    .frame_number  = static_cast<std::uint64_t>(frame.frame_number),
                    .delta_seconds = static_cast<double>(frame.delta_seconds),
                });
            }
            void record_frame(const vk::raii::CommandBuffer& command_buffer);

        private:
            class Impl;
            std::unique_ptr<Impl> impl;
        };
    };

} // namespace spectra::pathtracer
