module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

export module spectra.pathtracer.renderer;

export import spectra.pathtracer.host;
export import spectra.scene;

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
            Renderer(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::CameraWorkspace> camera_workspace);
            ~Renderer() noexcept;

            Renderer(const Renderer& other) = delete;
            Renderer(Renderer&& other) noexcept;
            Renderer& operator=(const Renderer& other) = delete;
            Renderer& operator=(Renderer&& other) noexcept;

            [[nodiscard]] static std::string_view name();
            void set_scene_workspace(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::CameraWorkspace> camera_workspace);
            void attach(HostView host);
            void detach() noexcept;
            void before_imgui_shutdown() noexcept;
            void after_imgui_created();
            [[nodiscard]] FrameResult begin_frame(HostView host, const FrameContext& frame);
            void record_frame(const vk::raii::CommandBuffer& command_buffer);

        private:
            class Impl;
            std::unique_ptr<Impl> impl;
        };
    };

} // namespace spectra::pathtracer
