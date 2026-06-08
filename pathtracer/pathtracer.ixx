module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module spectra.pathtracer;

import std;

extern "C++" {
    namespace pstd::pmr {
        class memory_resource;
    }

    namespace spectra::pathtracer {
        class CompiledPathtracerScene;
        struct RenderConfig;
    } // namespace spectra::pathtracer
}

export namespace spectra::pathtracer {
    struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    enum class SceneDirtyFlags : std::uint32_t {
        None     = 0,
        Snapshot = 1,
    };

    enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

    struct SceneSourceLocation {
        std::string filename{};
        int line{1};
        int column{1};
    };

    struct SceneParameter {
        std::string type{};
        std::string name{};
        std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{std::vector<float>{}};
        bool mayBeUnused{false};
        ColorSpace colorSpace{ColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    struct SceneEntity {
        std::string type{};
        std::vector<SceneParameter> parameters{};
        ColorSpace colorSpace{ColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    struct SceneTransform {
        std::array<float, 16> matrix{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        std::array<float, 16> inverse{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    };

    struct SceneTransformSet {
        SceneTransform start{};
        SceneTransform end{};
        float startTime{0.0f};
        float endTime{1.0f};
        bool animated{false};
    };

    struct SceneOption {
        std::string name{};
        std::string value{};
        SceneSourceLocation source{};
    };

    struct SceneMediumInterface {
        std::string inside{};
        std::string outside{};
    };

    struct SceneRenderSettings {
        SceneEntity filter{.type = "gaussian"};
        SceneEntity film{.type = "rgb"};
        SceneEntity camera{.type = "perspective"};
        SceneEntity sampler{.type = "zsobol"};
        SceneEntity integrator{.type = "volpath"};
        SceneEntity accelerator{.type = "bvh"};
        SceneTransformSet cameraTransform{};
        std::string cameraMedium{};
        std::vector<SceneOption> options{};
    };

    struct SceneMaterial {
        std::string name{};
        SceneEntity entity{};
    };

    struct SceneTexture {
        std::string name{};
        std::string kind{};
        SceneEntity entity{};
        SceneTransformSet transform{};
    };

    struct SceneMedium {
        std::string name{};
        SceneEntity entity{};
        SceneTransformSet transform{};
    };

    struct SceneLight {
        std::string name{};
        SceneEntity entity{};
        SceneTransformSet transform{};
        std::string medium{};
    };

    struct SceneAreaLight {
        SceneEntity entity{};
    };

    struct SceneShape {
        std::string name{};
        SceneEntity entity{};
        SceneTransformSet transform{};
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<SceneAreaLight> areaLight{};
        SceneMediumInterface mediumInterface{};
    };

    struct SceneObjectDefinition {
        std::string name{};
        std::vector<SceneShape> shapes{};
        SceneSourceLocation source{};
    };

    struct SceneObjectInstance {
        std::string name{};
        std::string definitionName{};
        SceneTransformSet transform{};
        SceneSourceLocation source{};
    };

    struct SceneSnapshot {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        SceneRenderSettings renderSettings{};
        std::vector<SceneMaterial> materials{};
        std::vector<SceneTexture> textures{};
        std::vector<SceneMedium> media{};
        std::vector<SceneLight> lights{};
        std::vector<SceneShape> shapes{};
        std::vector<SceneObjectDefinition> objectDefinitions{};
        std::vector<SceneObjectInstance> objectInstances{};
    };

    struct SceneEditBatch {
        SceneRevision beforeRevision{};
        SceneRevision afterRevision{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};
    };

    class SceneEditBuilder {
    public:
        void replaceSnapshot(SceneSnapshot snapshot, SceneDirtyFlags dirty) {
            if (dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Pathtracer scene snapshot replacement must use snapshot dirty state");
            this->replacement = std::move(snapshot);
            this->dirty       = dirty;
        }

    private:
        std::optional<SceneSnapshot> replacement{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};

        friend class SceneWorkspace;
    };

    class SceneWorkspace {
    public:
        SceneWorkspace() = default;

        explicit SceneWorkspace(SceneSnapshot snapshot) {
            if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
            this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(snapshot));
        }

        [[nodiscard]] bool loaded() const {
            return this->currentSnapshot != nullptr;
        }

        [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshot() const {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("Pathtracer scene workspace does not contain a loaded snapshot");
            return this->currentSnapshot;
        }

        [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit) {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded pathtracer scene workspace");
            if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty pathtracer scene edit");
            if (edit.dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Pathtracer scene edit commit must use snapshot dirty state");

            SceneSnapshot next                 = std::move(*edit.replacement);
            const SceneRevision beforeRevision = this->currentSnapshot->revision;
            next.revision                      = SceneRevision{beforeRevision.value + 1};
            this->currentSnapshot              = std::make_shared<SceneSnapshot>(std::move(next));

            SceneEditBatch batch = this->fullEdit(beforeRevision);
            batch.dirty          = edit.dirty;
            this->lastEdit       = batch;
            return batch;
        }

        [[nodiscard]] SceneEditBatch changes_since(SceneRevision revision) const {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query pathtracer scene changes from an unloaded workspace");
            if (revision == this->currentSnapshot->revision) {
                return SceneEditBatch{
                    .beforeRevision = revision,
                    .afterRevision  = revision,
                    .dirty          = SceneDirtyFlags::None,
                };
            }
            if (revision.value == 0) return this->fullEdit(revision);
            if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
            throw std::runtime_error("Pathtracer scene edit history for the requested revision is unavailable");
        }

    private:
        [[nodiscard]] SceneEditBatch fullEdit(SceneRevision before) const {
            return SceneEditBatch{
                .beforeRevision = before,
                .afterRevision  = this->currentSnapshot->revision,
                .dirty          = SceneDirtyFlags::Snapshot,
            };
        }

        std::shared_ptr<const SceneSnapshot> currentSnapshot{};
        std::optional<SceneEditBatch> lastEdit{};
    };

    struct SceneInfo {
        std::string name{};
        std::string title{};
        std::string camera{};
        std::string sampler{};
        std::string integrator{};
        std::string accelerator{};
        std::size_t shape_count{};
        std::size_t material_count{};
        std::size_t texture_count{};
        std::size_t medium_count{};
        std::size_t light_count{};
        std::size_t area_light_count{};
        std::size_t infinite_light_count{};
        std::size_t object_definition_count{};
        std::size_t object_instance_count{};
        float camera_fov_degrees{};
    };

    struct SceneDiagnostic {
        SceneSourceLocation source{};
        std::string message{};
    };

    enum class SceneProbeFeatureCategory {
        PixelFilter,
        Film,
        Camera,
        Sampler,
        Integrator,
        Accelerator,
        Material,
        Texture,
        Medium,
        Light,
        AreaLight,
        Shape,
        LightSampler,
        Option,
        AnimatedTransform,
    };

    struct SceneProbeFeature {
        SceneProbeFeatureCategory category{SceneProbeFeatureCategory::Option};
        std::string type{};
        std::string kind{};
        SceneSourceLocation source{};
    };

    struct SceneProbeReport {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        std::vector<SceneProbeFeature> features{};
        std::vector<SceneDiagnostic> diagnostics{};
    };

    struct SceneTranslationReport {
        std::string target{};
        bool supported{true};
        std::vector<SceneDiagnostic> diagnostics{};
    };

    [[nodiscard]] SceneInfo DescribeScene(const SceneSnapshot& scene) {
        const auto oneFloatParameter = [](const std::vector<SceneParameter>& parameters, const std::string& name, const float fallback) {
            for (const SceneParameter& parameter : parameters) {
                if (parameter.type != "float" && parameter.type != "integer") continue;
                if (parameter.name != name) continue;
                return std::visit(
                    [fallback](const auto& values) -> float {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<float>>) {
                            if (!values.empty()) return values.front();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<int>>) {
                            if (!values.empty()) return static_cast<float>(values.front());
                        }
                        return fallback;
                    },
                    parameter.values);
            }
            return fallback;
        };

        std::size_t definitionShapeCount     = 0;
        std::size_t definitionAreaLightCount = 0;
        for (const SceneObjectDefinition& definition : scene.objectDefinitions) {
            definitionShapeCount += definition.shapes.size();
            for (const SceneShape& shape : definition.shapes)
                if (shape.areaLight.has_value()) ++definitionAreaLightCount;
        }

        std::size_t areaLightCount = definitionAreaLightCount;
        for (const SceneShape& shape : scene.shapes)
            if (shape.areaLight.has_value()) ++areaLightCount;

        std::size_t infiniteLightCount = 0;
        for (const SceneLight& light : scene.lights)
            if (light.entity.type == "infinite") ++infiniteLightCount;

        float cameraFov = oneFloatParameter(scene.renderSettings.camera.parameters, "fov", scene.renderSettings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(cameraFov > 0.0f && cameraFov < 180.0f)) cameraFov = 45.0f;

        return SceneInfo{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = scene.renderSettings.camera.type,
            .sampler                 = scene.renderSettings.sampler.type,
            .integrator              = scene.renderSettings.integrator.type,
            .accelerator             = scene.renderSettings.accelerator.type,
            .shape_count             = scene.shapes.size() + definitionShapeCount,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = areaLightCount,
            .infinite_light_count    = infiniteLightCount,
            .object_definition_count = scene.objectDefinitions.size(),
            .object_instance_count   = scene.objectInstances.size(),
            .camera_fov_degrees      = cameraFov,
        };
    }

    enum class PathtracerDockSlot {
        Center = 0,
        Floating = 1,
    };

    struct PathtracerPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        PathtracerDockSlot dock_slot{PathtracerDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct PathtracerSidebarTab {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    struct PathtracerToolbarAction {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<bool()> active{};
        std::move_only_function<void()> trigger{};
    };

    struct PathtracerFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct PathtracerFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Frame>
    concept PathtracerFrameInfoLike = requires(const Frame& frame) {
        { frame.frame_slot_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
    };

    template <typename Host>
    concept PathtracerHost = requires(Host& host, PathtracerPanel panel, PathtracerSidebarTab tab, PathtracerToolbarAction action) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.register_sidebar_tab(std::move(tab)) } -> std::same_as<void>;
        { host.register_toolbar_action(std::move(action)) } -> std::same_as<void>;
    };

    class PathtracerHostView {
    public:
        template <PathtracerHost Host>
        explicit PathtracerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](PathtracerPanel panel) { host.register_panel(std::move(panel)); }), registerSidebarTabCallback([&host](PathtracerSidebarTab tab) { host.register_sidebar_tab(std::move(tab)); }), registerToolbarActionCallback([&host](PathtracerToolbarAction action) { host.register_toolbar_action(std::move(action)); }) {}

        PathtracerHostView(const PathtracerHostView& other)                = delete;
        PathtracerHostView(PathtracerHostView&& other) noexcept            = default;
        PathtracerHostView& operator=(const PathtracerHostView& other)     = delete;
        PathtracerHostView& operator=(PathtracerHostView&& other) noexcept = default;
        ~PathtracerHostView() noexcept                                     = default;

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() {
            return this->physicalDeviceCallback();
        }

        [[nodiscard]] const vk::raii::Device& device() {
            return this->deviceCallback();
        }

        [[nodiscard]] std::uint32_t frame_count() {
            return this->frameCountCallback();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() {
            return this->swapchainExtentCallback();
        }

        void register_panel(PathtracerPanel panel) {
            this->registerPanelCallback(std::move(panel));
        }

        void register_sidebar_tab(PathtracerSidebarTab tab) {
            this->registerSidebarTabCallback(std::move(tab));
        }

        void register_toolbar_action(PathtracerToolbarAction action) {
            this->registerToolbarActionCallback(std::move(action));
        }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physicalDeviceCallback{};
        std::move_only_function<const vk::raii::Device&()> deviceCallback{};
        std::move_only_function<std::uint32_t()> frameCountCallback{};
        std::move_only_function<vk::Extent2D()> swapchainExtentCallback{};
        std::move_only_function<void(PathtracerPanel)> registerPanelCallback{};
        std::move_only_function<void(PathtracerSidebarTab)> registerSidebarTabCallback{};
        std::move_only_function<void(PathtracerToolbarAction)> registerToolbarActionCallback{};
    };

    [[nodiscard]] SceneTranslationReport AnalyzePathtracerSceneProbe(const SceneProbeReport& probe);
    [[nodiscard]] SceneTranslationReport AnalyzePathtracerSceneSupport(const SceneSnapshot& scene);
    [[nodiscard]] std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const SceneSnapshot& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource);

    class PathtracerRenderer final {
    public:
        explicit PathtracerRenderer(std::shared_ptr<SceneWorkspace> source_workspace);
        ~PathtracerRenderer() noexcept;

        PathtracerRenderer(const PathtracerRenderer& other) = delete;
        PathtracerRenderer(PathtracerRenderer&& other) noexcept;
        PathtracerRenderer& operator=(const PathtracerRenderer& other) = delete;
        PathtracerRenderer& operator=(PathtracerRenderer&& other) noexcept;

        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] std::string_view name() const;

        template <PathtracerHost Host>
        void attach(Host& host) {
            this->attach(PathtracerHostView{host});
        }

        template <PathtracerHost Host>
        void detach(Host&) noexcept {
            this->detach();
        }

        template <PathtracerHost Host>
        void before_imgui_shutdown(Host&) noexcept {
            this->before_imgui_shutdown();
        }

        template <PathtracerHost Host>
        void after_imgui_created(Host&) {
            this->after_imgui_created();
        }

        template <PathtracerHost Host, typename Frame>
            requires PathtracerFrameInfoLike<Frame>
        [[nodiscard]] PathtracerFrameResult begin_frame(Host& host, const Frame& frame) {
            return this->begin_frame(PathtracerHostView{host}, PathtracerFrameInfo{
                                                                       .frame_index = static_cast<std::uint32_t>(frame.frame_slot_index),
                                                                       .image_index = static_cast<std::uint32_t>(frame.image_index),
                                                                   });
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        void attach(PathtracerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] PathtracerFrameResult begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame);
    };

} // namespace spectra::pathtracer
