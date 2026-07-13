module;

export module spectra.pathtracer.renderer;

export import spectra.renderer.host;
export import spectra.scene;
export import vulkan;

import std;

namespace spectra::pathtracer {
    struct SceneSupportReport {
        bool supported{true};
        std::vector<scene::Scene::Diagnostic> diagnostics{};
    };

    [[nodiscard]] SceneSupportReport AnalyzeSceneSupport(const scene::Scene::ResolvedScene& scene);

    export {
        struct OfflineRenderRequest {
            std::string output_file{};
            std::optional<int> pixel_samples{};
            int seed{0};
            std::optional<int> cuda_device{};
            bool quiet{false};
        };

        void RenderScene(const scene::Scene::ResolvedScene& scene, OfflineRenderRequest request);

        class Renderer final {
        public:
            Renderer(std::shared_ptr<scene::Scene> source_scene, std::shared_ptr<scene::CameraWorkspace> camera_workspace);
            ~Renderer() noexcept;

            Renderer(const Renderer& other) = delete;
            Renderer(Renderer&& other) noexcept;
            Renderer& operator=(const Renderer& other) = delete;
            Renderer& operator=(Renderer&& other) noexcept;

            [[nodiscard]] static std::string_view name();
            void attach(HostView host);
            template <Host HostType>
            void attach(HostType& host) {
                this->attach(HostView{host});
            }
            void detach() noexcept;
            void before_imgui_shutdown() noexcept;
            void after_imgui_created();
            [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
            template <Host HostType>
            [[nodiscard]] FrameResult begin_frame(HostType& host, const FrameContext& frame) {
                return this->begin_frame(HostView{host}, frame);
            }
            void record_frame(const vk::raii::CommandBuffer& command_buffer);

        private:
            class Impl;
            std::unique_ptr<Impl> impl;
        };
    };

} // namespace spectra::pathtracer
