module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module spectra.rasterizer;

import std;

export namespace spectra::rasterizer {
    struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    enum class SceneDirtyFlags : std::uint32_t {
        None            = 0,
        Document        = 1u << 0u,
        Timeline        = 1u << 1u,
        Frame           = 1u << 2u,
        RenderResources = 1u << 3u,
    };

    [[nodiscard]] constexpr SceneDirtyFlags operator|(const SceneDirtyFlags lhs, const SceneDirtyFlags rhs) {
        return static_cast<SceneDirtyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    [[nodiscard]] constexpr bool HasSceneDirtyFlag(const SceneDirtyFlags flags, const SceneDirtyFlags flag) {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0u;
    }

    struct SceneSourceLocation {
        std::string filename{};
        int line{1};
        int column{1};
    };

    struct SceneVector2 {
        float x{};
        float y{};
    };

    struct SceneVector3 {
        float x{};
        float y{};
        float z{};
    };

    struct SceneVector4 {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    struct SceneQuaternion {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    struct SceneTransform {
        SceneVector3 position{};
        SceneQuaternion rotation{};
        SceneVector3 scale{1.0f, 1.0f, 1.0f};
    };

    struct SceneMaterial {
        std::string name{};
        SceneVector4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
        SceneVector3 emissionColor{};
        float emissionStrength{};
        float roughness{0.5f};
        float metallic{};
    };

    enum class SceneLightKind {
        Directional,
        Point,
        Spot,
        Area,
        Environment,
    };

    struct SceneLight {
        std::string name{};
        SceneLightKind kind{SceneLightKind::Directional};
        SceneTransform transform{};
        SceneVector3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float coneAngleDegrees{45.0f};
        SceneSourceLocation source{};
    };

    struct SceneCamera {
        std::string name{};
        SceneTransform transform{};
        SceneVector3 target{};
        float verticalFovDegrees{45.0f};
        float nearPlane{0.01f};
        float farPlane{200.0f};
        SceneSourceLocation source{};
    };

    struct SceneMesh {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> normals{};
        std::vector<SceneVector2> texcoords{};
        std::vector<std::uint32_t> indices{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{false};
        SceneSourceLocation source{};
    };

    struct SceneParticleSet {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> velocities{};
        std::vector<float> radii{};
        std::vector<SceneVector4> colors{};
        std::string materialName{};
        SceneTransform transform{};
        float mass{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    enum class SceneVolumeKind {
        LiquidLevelSet,
        GasDensity,
        GasTemperature,
        GasVelocity,
    };

    enum class SceneVolumeChannelLayout {
        CellCentered,
        FaceX,
        FaceY,
        FaceZ,
    };

    struct SceneVolumeChannel {
        std::string name{};
        SceneVolumeChannelLayout layout{SceneVolumeChannelLayout::CellCentered};
        std::array<std::uint32_t, 3> dimensions{};
        std::vector<float> values{};
    };

    struct SceneVolumeGrid {
        std::string name{};
        SceneVolumeKind kind{SceneVolumeKind::GasDensity};
        std::array<std::uint32_t, 3> dimensions{};
        SceneVector3 origin{};
        SceneVector3 voxelSize{1.0f, 1.0f, 1.0f};
        std::vector<std::string> channelNames{};
        std::vector<SceneVolumeChannel> channels{};
        std::string materialName{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    struct SceneCloth {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        SceneTransform transform{};
        float massPerArea{1.0f};
        float stretchStiffness{1.0f};
        float bendStiffness{0.2f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    struct SceneRigidBody {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        SceneTransform transform{};
        SceneVector3 linearVelocity{};
        SceneVector3 angularVelocity{};
        float mass{1.0f};
        bool staticBody{false};
        SceneSourceLocation source{};
    };

    struct SceneCollider {
        std::string name{};
        std::string meshName{};
        SceneTransform transform{};
        float friction{0.5f};
        float restitution{0.1f};
        SceneSourceLocation source{};
    };

    struct SceneDocument {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        SceneVector3 gravity{0.0f, -9.8f, 0.0f};
        double framesPerSecond{24.0};
        std::optional<SceneCamera> camera{};
        std::vector<SceneMaterial> materials{};
        std::vector<SceneLight> lights{};
        std::vector<SceneMesh> meshes{};
        std::vector<SceneParticleSet> particleSets{};
        std::vector<SceneVolumeGrid> volumes{};
        std::vector<SceneCloth> cloths{};
        std::vector<SceneRigidBody> rigidBodies{};
        std::vector<SceneCollider> colliders{};
    };

    enum class SimulationTimelineMode {
        Live,
        Record,
        Playback,
    };

    struct FrameCursor {
        std::uint64_t frameIndex{};
        double timeSeconds{};
    };

    struct SceneFrameSnapshot {
        SceneRevision revision{};
        FrameCursor cursor{};
        std::vector<SceneMesh> meshes{};
        std::vector<SceneParticleSet> particleSets{};
        std::vector<SceneVolumeGrid> volumes{};
        std::vector<SceneCloth> cloths{};
        std::vector<SceneRigidBody> rigidBodies{};
    };

    struct SimulationTimeline {
        SimulationTimelineMode mode{SimulationTimelineMode::Playback};
        double framesPerSecond{24.0};
        bool playing{true};
        bool loop{true};
        FrameCursor cursor{};
        std::uint64_t selectedFrameIndex{};
        std::uint64_t resetRequestSerial{};
        std::uint64_t clearRecordingRequestSerial{};
        std::optional<SceneFrameSnapshot> currentFrame{};
        std::vector<SceneFrameSnapshot> recordedFrames{};
    };

    struct SceneEditBatch {
        SceneRevision beforeRevision{};
        SceneRevision afterRevision{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};
    };

    class SceneEditBuilder {
    public:
        void replaceDocument(SceneDocument document) {
            this->documentReplacement = std::move(document);
            this->dirty               = this->dirty | SceneDirtyFlags::Document | SceneDirtyFlags::RenderResources;
        }

        void replaceTimeline(SimulationTimeline timeline) {
            this->timelineReplacement = std::move(timeline);
            this->dirty               = this->dirty | SceneDirtyFlags::Timeline;
        }

        void replaceFrame(SceneFrameSnapshot frame) {
            this->frameReplacement = std::move(frame);
            this->dirty            = this->dirty | SceneDirtyFlags::Frame | SceneDirtyFlags::RenderResources;
        }

    private:
        std::optional<SceneDocument> documentReplacement{};
        std::optional<SimulationTimeline> timelineReplacement{};
        std::optional<SceneFrameSnapshot> frameReplacement{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};

        friend class SceneWorkspace;
    };

    class SceneWorkspace {
    public:
        SceneWorkspace() = default;

        explicit SceneWorkspace(SceneDocument document) {
            if (document.revision.value == 0) document.revision = SceneRevision{1};
            this->currentRevision = document.revision;
            this->currentDocument = std::make_shared<SceneDocument>(std::move(document));
            this->currentTimeline.framesPerSecond = this->currentDocument->framesPerSecond;
        }

        [[nodiscard]] bool loaded() const {
            return this->currentDocument != nullptr;
        }

        [[nodiscard]] SceneRevision revision() const {
            if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
            return this->currentRevision;
        }

        [[nodiscard]] std::shared_ptr<const SceneDocument> document() const {
            if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
            return this->currentDocument;
        }

        [[nodiscard]] SimulationTimeline timeline() const {
            if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
            return this->currentTimeline;
        }

        [[nodiscard]] std::optional<SceneFrameSnapshot> frame() const {
            if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
            return this->currentTimeline.currentFrame;
        }

        [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit) {
            if (this->currentDocument == nullptr) throw std::runtime_error("Cannot edit an unloaded rasterizer scene workspace");
            if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit an empty rasterizer scene edit");

            const SceneRevision beforeRevision = this->currentRevision;
            this->currentRevision              = SceneRevision{beforeRevision.value + 1};
            if (edit.documentReplacement.has_value()) {
                SceneDocument next = std::move(*edit.documentReplacement);
                next.revision      = this->currentRevision;
                this->currentDocument = std::make_shared<SceneDocument>(std::move(next));
            }
            if (edit.timelineReplacement.has_value()) this->currentTimeline = std::move(*edit.timelineReplacement);
            if (edit.frameReplacement.has_value()) {
                edit.frameReplacement->revision = this->currentRevision;
                this->currentTimeline.cursor = edit.frameReplacement->cursor;
                this->currentTimeline.currentFrame = std::move(*edit.frameReplacement);
            }

            SceneEditBatch batch{
                .beforeRevision = beforeRevision,
                .afterRevision  = this->currentRevision,
                .dirty          = edit.dirty,
            };
            this->lastEdit = batch;
            return batch;
        }

        [[nodiscard]] SceneEditBatch changes_since(const SceneRevision revision) const {
            if (this->currentDocument == nullptr) throw std::runtime_error("Cannot query rasterizer scene changes from an unloaded workspace");
            if (revision == this->currentRevision) {
                return SceneEditBatch{
                    .beforeRevision = revision,
                    .afterRevision  = revision,
                    .dirty          = SceneDirtyFlags::None,
                };
            }
            if (revision.value == 0) return this->fullEdit(revision);
            if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
            throw std::runtime_error("Rasterizer scene edit history for the requested revision is unavailable");
        }

    private:
        [[nodiscard]] SceneEditBatch fullEdit(SceneRevision before) const {
            return SceneEditBatch{
                .beforeRevision = before,
                .afterRevision  = this->currentRevision,
                .dirty          = SceneDirtyFlags::Document | SceneDirtyFlags::Timeline | SceneDirtyFlags::Frame | SceneDirtyFlags::RenderResources,
            };
        }

        SceneRevision currentRevision{};
        std::shared_ptr<const SceneDocument> currentDocument{};
        SimulationTimeline currentTimeline{};
        std::optional<SceneEditBatch> lastEdit{};
    };

    enum class RasterizerDockSlot {
        Center,
        Floating,
    };

    struct RasterizerPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        RasterizerDockSlot dock_slot{RasterizerDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct RasterizerSidebarTab {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    struct RasterizerFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
        std::uint64_t frame_number{};
        double delta_seconds{};
    };

    struct RasterizerFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Frame>
    concept RasterizerFrameInfoLike = requires(const Frame& frame) {
        { frame.frame_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
        { frame.frame_number } -> std::convertible_to<std::uint64_t>;
        { frame.delta_seconds } -> std::convertible_to<double>;
    };

    template <typename Host>
    concept RasterizerHost = requires(Host& host, RasterizerPanel panel, RasterizerSidebarTab tab, std::string detail) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.register_sidebar_tab(std::move(tab)) } -> std::same_as<void>;
        { host.set_window_detail(std::move(detail)) } -> std::same_as<void>;
    };

    class RasterizerHostView {
    public:
        template <RasterizerHost Host>
        explicit RasterizerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](RasterizerPanel panel) { host.register_panel(std::move(panel)); }), registerSidebarTabCallback([&host](RasterizerSidebarTab tab) { host.register_sidebar_tab(std::move(tab)); }), setWindowDetailCallback([&host](std::string detail) { host.set_window_detail(std::move(detail)); }) {}

        RasterizerHostView(const RasterizerHostView& other)                = delete;
        RasterizerHostView(RasterizerHostView&& other) noexcept            = default;
        RasterizerHostView& operator=(const RasterizerHostView& other)     = delete;
        RasterizerHostView& operator=(RasterizerHostView&& other) noexcept = default;
        ~RasterizerHostView() noexcept                                     = default;

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

        void register_panel(RasterizerPanel panel) {
            this->registerPanelCallback(std::move(panel));
        }

        void register_sidebar_tab(RasterizerSidebarTab tab) {
            this->registerSidebarTabCallback(std::move(tab));
        }

        void set_window_detail(std::string detail) {
            this->setWindowDetailCallback(std::move(detail));
        }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physicalDeviceCallback{};
        std::move_only_function<const vk::raii::Device&()> deviceCallback{};
        std::move_only_function<std::uint32_t()> frameCountCallback{};
        std::move_only_function<vk::Extent2D()> swapchainExtentCallback{};
        std::move_only_function<void(RasterizerPanel)> registerPanelCallback{};
        std::move_only_function<void(RasterizerSidebarTab)> registerSidebarTabCallback{};
        std::move_only_function<void(std::string)> setWindowDetailCallback{};
    };

    class RasterizerRenderer final {
    public:
        explicit RasterizerRenderer(std::shared_ptr<SceneWorkspace> scene_workspace);
        ~RasterizerRenderer() noexcept;

        RasterizerRenderer(const RasterizerRenderer& other) = delete;
        RasterizerRenderer(RasterizerRenderer&& other) noexcept;
        RasterizerRenderer& operator=(const RasterizerRenderer& other) = delete;
        RasterizerRenderer& operator=(RasterizerRenderer&& other) noexcept;

        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] std::string_view name() const;
        void set_scene_workspace(std::shared_ptr<SceneWorkspace> scene_workspace);
        void set_control_panel_extension(std::move_only_function<void()> draw);

        template <RasterizerHost Host>
        void attach(Host& host) {
            this->attach(RasterizerHostView{host});
        }

        template <RasterizerHost Host>
        void detach(Host&) noexcept {
            this->detach();
        }

        template <RasterizerHost Host>
        void before_imgui_shutdown(Host&) noexcept {
            this->before_imgui_shutdown();
        }

        template <RasterizerHost Host>
        void after_imgui_created(Host&) {
            this->after_imgui_created();
        }

        template <RasterizerHost Host, typename Frame>
            requires RasterizerFrameInfoLike<Frame>
        [[nodiscard]] RasterizerFrameResult begin_frame(Host& host, const Frame& frame) {
            return this->begin_frame(RasterizerHostView{host}, RasterizerFrameInfo{
                                                                   .frame_index = static_cast<std::uint32_t>(frame.frame_index),
                                                                   .image_index = static_cast<std::uint32_t>(frame.image_index),
                                                                   .frame_number = static_cast<std::uint64_t>(frame.frame_number),
                                                                   .delta_seconds = static_cast<double>(frame.delta_seconds),
                                                               });
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        void attach(RasterizerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        void set_scene_workspace_impl(std::shared_ptr<SceneWorkspace> scene_workspace);
        void set_control_panel_extension_impl(std::move_only_function<void()> draw);
        [[nodiscard]] RasterizerFrameResult begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame);
    };
} // namespace spectra::rasterizer
