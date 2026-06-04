#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer;
import spectra.rasterizer;
import spectra.scene;
import spectra.scene.library;

namespace spectra::app {
    [[nodiscard]] pathtracer::ColorSpace ToPathtracerColorSpace(const scene::ColorSpace colorSpace) {
        switch (colorSpace) {
        case scene::ColorSpace::sRGB: return pathtracer::ColorSpace::sRGB;
        case scene::ColorSpace::DCI_P3: return pathtracer::ColorSpace::DCI_P3;
        case scene::ColorSpace::Rec2020: return pathtracer::ColorSpace::Rec2020;
        case scene::ColorSpace::ACES2065_1: return pathtracer::ColorSpace::ACES2065_1;
        }
        throw std::runtime_error("Unknown scene color space while adapting scene to pathtracer");
    }

    [[nodiscard]] pathtracer::SceneSourceLocation ToPathtracerSourceLocation(const scene::SceneSourceLocation& source) {
        return pathtracer::SceneSourceLocation{
            .filename = source.filename,
            .line     = source.line,
            .column   = source.column,
        };
    }

    [[nodiscard]] pathtracer::SceneRevision ToPathtracerRevision(const scene::SceneRevision revision) {
        return pathtracer::SceneRevision{.value = revision.value};
    }

    [[nodiscard]] pathtracer::SceneTransform ToPathtracerTransform(const math::Transform& transform) {
        return pathtracer::SceneTransform{
            .matrix  = transform.matrix,
            .inverse = transform.inverse,
        };
    }

    [[nodiscard]] pathtracer::SceneTransformSet ToPathtracerTransformSet(const scene::SceneTransformSet& transform) {
        return pathtracer::SceneTransformSet{
            .start     = ToPathtracerTransform(transform.start),
            .end       = ToPathtracerTransform(transform.end),
            .startTime = transform.startTime,
            .endTime   = transform.endTime,
            .animated  = transform.animated,
        };
    }

    [[nodiscard]] pathtracer::SceneParameter ToPathtracerParameter(const scene::SceneParameter& parameter) {
        return pathtracer::SceneParameter{
            .type        = parameter.type,
            .name        = parameter.name,
            .values      = parameter.values,
            .mayBeUnused = parameter.mayBeUnused,
            .colorSpace  = ToPathtracerColorSpace(parameter.colorSpace),
            .source      = ToPathtracerSourceLocation(parameter.source),
        };
    }

    [[nodiscard]] std::vector<pathtracer::SceneParameter> ToPathtracerParameters(const std::vector<scene::SceneParameter>& parameters) {
        std::vector<pathtracer::SceneParameter> result;
        result.reserve(parameters.size());
        for (const scene::SceneParameter& parameter : parameters) result.push_back(ToPathtracerParameter(parameter));
        return result;
    }

    [[nodiscard]] pathtracer::SceneEntity ToPathtracerEntity(const scene::SceneEntity& entity) {
        return pathtracer::SceneEntity{
            .type       = entity.type,
            .parameters = ToPathtracerParameters(entity.parameters),
            .colorSpace = ToPathtracerColorSpace(entity.colorSpace),
            .source     = ToPathtracerSourceLocation(entity.source),
        };
    }

    [[nodiscard]] pathtracer::SceneOption ToPathtracerOption(const scene::SceneOption& option) {
        return pathtracer::SceneOption{
            .name   = option.name,
            .value  = option.value,
            .source = ToPathtracerSourceLocation(option.source),
        };
    }

    [[nodiscard]] pathtracer::SceneMediumInterface ToPathtracerMediumInterface(const scene::SceneMediumInterface& mediumInterface) {
        return pathtracer::SceneMediumInterface{
            .inside  = mediumInterface.inside,
            .outside = mediumInterface.outside,
        };
    }

    [[nodiscard]] pathtracer::SceneRenderSettings ToPathtracerRenderSettings(const scene::SceneRenderSettings& settings) {
        std::vector<pathtracer::SceneOption> options;
        options.reserve(settings.options.size());
        for (const scene::SceneOption& option : settings.options) options.push_back(ToPathtracerOption(option));

        return pathtracer::SceneRenderSettings{
            .filter          = ToPathtracerEntity(settings.filter),
            .film            = ToPathtracerEntity(settings.film),
            .camera          = ToPathtracerEntity(settings.camera),
            .sampler         = ToPathtracerEntity(settings.sampler),
            .integrator      = ToPathtracerEntity(settings.integrator),
            .accelerator     = ToPathtracerEntity(settings.accelerator),
            .cameraTransform = ToPathtracerTransformSet(settings.cameraTransform),
            .cameraMedium    = settings.cameraMedium,
            .options         = std::move(options),
        };
    }

    [[nodiscard]] pathtracer::SceneMaterial ToPathtracerMaterial(const scene::SceneMaterial& material) {
        return pathtracer::SceneMaterial{
            .name   = material.name,
            .entity = ToPathtracerEntity(material.entity),
        };
    }

    [[nodiscard]] pathtracer::SceneTexture ToPathtracerTexture(const scene::SceneTexture& texture) {
        return pathtracer::SceneTexture{
            .name      = texture.name,
            .kind      = texture.kind,
            .entity    = ToPathtracerEntity(texture.entity),
            .transform = ToPathtracerTransformSet(texture.transform),
        };
    }

    [[nodiscard]] pathtracer::SceneMedium ToPathtracerMedium(const scene::SceneMedium& medium) {
        return pathtracer::SceneMedium{
            .name      = medium.name,
            .entity    = ToPathtracerEntity(medium.entity),
            .transform = ToPathtracerTransformSet(medium.transform),
        };
    }

    [[nodiscard]] pathtracer::SceneLight ToPathtracerLight(const scene::SceneLight& light) {
        return pathtracer::SceneLight{
            .name      = light.name,
            .entity    = ToPathtracerEntity(light.entity),
            .transform = ToPathtracerTransformSet(light.transform),
            .medium    = light.medium,
        };
    }

    [[nodiscard]] std::optional<pathtracer::SceneAreaLight> ToPathtracerAreaLight(const std::optional<scene::SceneAreaLight>& areaLight) {
        if (!areaLight.has_value()) return std::nullopt;
        return pathtracer::SceneAreaLight{.entity = ToPathtracerEntity(areaLight->entity)};
    }

    [[nodiscard]] pathtracer::SceneShape ToPathtracerShape(const scene::SceneShape& shape) {
        return pathtracer::SceneShape{
            .name               = shape.name,
            .entity             = ToPathtracerEntity(shape.entity),
            .transform          = ToPathtracerTransformSet(shape.transform),
            .reverseOrientation = shape.reverseOrientation,
            .materialName       = shape.materialName,
            .areaLight          = ToPathtracerAreaLight(shape.areaLight),
            .mediumInterface    = ToPathtracerMediumInterface(shape.mediumInterface),
        };
    }

    [[nodiscard]] pathtracer::SceneObjectDefinition ToPathtracerObjectDefinition(const scene::SceneObjectDefinition& definition) {
        std::vector<pathtracer::SceneShape> shapes;
        shapes.reserve(definition.shapes.size());
        for (const scene::SceneShape& shape : definition.shapes) shapes.push_back(ToPathtracerShape(shape));

        return pathtracer::SceneObjectDefinition{
            .name   = definition.name,
            .shapes = std::move(shapes),
            .source = ToPathtracerSourceLocation(definition.source),
        };
    }

    [[nodiscard]] pathtracer::SceneObjectInstance ToPathtracerObjectInstance(const scene::SceneObjectInstance& instance) {
        return pathtracer::SceneObjectInstance{
            .name           = instance.name,
            .definitionName = instance.definitionName,
            .transform      = ToPathtracerTransformSet(instance.transform),
            .source         = ToPathtracerSourceLocation(instance.source),
        };
    }

    [[nodiscard]] pathtracer::SceneSnapshot ToPathtracerScene(const scene::SceneSnapshot& source) {
        pathtracer::SceneSnapshot result{
            .revision       = ToPathtracerRevision(source.revision),
            .name           = source.name,
            .title          = source.title,
            .source         = source.source,
            .renderSettings = ToPathtracerRenderSettings(source.renderSettings),
        };

        result.materials.reserve(source.materials.size());
        for (const scene::SceneMaterial& material : source.materials) result.materials.push_back(ToPathtracerMaterial(material));

        result.textures.reserve(source.textures.size());
        for (const scene::SceneTexture& texture : source.textures) result.textures.push_back(ToPathtracerTexture(texture));

        result.media.reserve(source.media.size());
        for (const scene::SceneMedium& medium : source.media) result.media.push_back(ToPathtracerMedium(medium));

        result.lights.reserve(source.lights.size());
        for (const scene::SceneLight& light : source.lights) result.lights.push_back(ToPathtracerLight(light));

        result.shapes.reserve(source.shapes.size());
        for (const scene::SceneShape& shape : source.shapes) result.shapes.push_back(ToPathtracerShape(shape));

        result.objectDefinitions.reserve(source.objectDefinitions.size());
        for (const scene::SceneObjectDefinition& definition : source.objectDefinitions) result.objectDefinitions.push_back(ToPathtracerObjectDefinition(definition));

        result.objectInstances.reserve(source.objectInstances.size());
        for (const scene::SceneObjectInstance& instance : source.objectInstances) result.objectInstances.push_back(ToPathtracerObjectInstance(instance));

        return result;
    }

    [[nodiscard]] pathtracer::SceneProbeFeatureCategory ToPathtracerProbeFeatureCategory(const scene::SceneProbeFeatureCategory category) {
        switch (category) {
        case scene::SceneProbeFeatureCategory::PixelFilter: return pathtracer::SceneProbeFeatureCategory::PixelFilter;
        case scene::SceneProbeFeatureCategory::Film: return pathtracer::SceneProbeFeatureCategory::Film;
        case scene::SceneProbeFeatureCategory::Camera: return pathtracer::SceneProbeFeatureCategory::Camera;
        case scene::SceneProbeFeatureCategory::Sampler: return pathtracer::SceneProbeFeatureCategory::Sampler;
        case scene::SceneProbeFeatureCategory::Integrator: return pathtracer::SceneProbeFeatureCategory::Integrator;
        case scene::SceneProbeFeatureCategory::Accelerator: return pathtracer::SceneProbeFeatureCategory::Accelerator;
        case scene::SceneProbeFeatureCategory::Material: return pathtracer::SceneProbeFeatureCategory::Material;
        case scene::SceneProbeFeatureCategory::Texture: return pathtracer::SceneProbeFeatureCategory::Texture;
        case scene::SceneProbeFeatureCategory::Medium: return pathtracer::SceneProbeFeatureCategory::Medium;
        case scene::SceneProbeFeatureCategory::Light: return pathtracer::SceneProbeFeatureCategory::Light;
        case scene::SceneProbeFeatureCategory::AreaLight: return pathtracer::SceneProbeFeatureCategory::AreaLight;
        case scene::SceneProbeFeatureCategory::Shape: return pathtracer::SceneProbeFeatureCategory::Shape;
        case scene::SceneProbeFeatureCategory::LightSampler: return pathtracer::SceneProbeFeatureCategory::LightSampler;
        case scene::SceneProbeFeatureCategory::Option: return pathtracer::SceneProbeFeatureCategory::Option;
        case scene::SceneProbeFeatureCategory::AnimatedTransform: return pathtracer::SceneProbeFeatureCategory::AnimatedTransform;
        }
        throw std::runtime_error("Unknown scene probe feature category while adapting scene to pathtracer");
    }

    [[nodiscard]] pathtracer::SceneProbeFeature ToPathtracerProbeFeature(const scene::SceneProbeFeature& feature) {
        return pathtracer::SceneProbeFeature{
            .category = ToPathtracerProbeFeatureCategory(feature.category),
            .type     = feature.type,
            .kind     = feature.kind,
            .source   = ToPathtracerSourceLocation(feature.source),
        };
    }

    [[nodiscard]] pathtracer::SceneProbeReport ToPathtracerProbeReport(const scene::SceneProbeReport& probe) {
        pathtracer::SceneProbeReport result{
            .revision = ToPathtracerRevision(probe.revision),
            .name     = probe.name,
            .title    = probe.title,
            .source   = probe.source,
        };
        result.features.reserve(probe.features.size());
        for (const scene::SceneProbeFeature& feature : probe.features) result.features.push_back(ToPathtracerProbeFeature(feature));
        result.diagnostics.reserve(probe.diagnostics.size());
        for (const scene::SceneDiagnostic& diagnostic : probe.diagnostics) {
            result.diagnostics.push_back(pathtracer::SceneDiagnostic{
                .source  = ToPathtracerSourceLocation(diagnostic.source),
                .message = diagnostic.message,
            });
        }
        return result;
    }

    [[nodiscard]] scene::SceneDiagnostic ToSpectraDiagnostic(const pathtracer::SceneDiagnostic& diagnostic) {
        return scene::SceneDiagnostic{
            .source =
                scene::SceneSourceLocation{
                    .filename = diagnostic.source.filename,
                    .line     = diagnostic.source.line,
                    .column   = diagnostic.source.column,
                },
            .message = diagnostic.message,
        };
    }

    [[nodiscard]] scene::SceneTranslationReport ToSpectraReport(const pathtracer::SceneTranslationReport& report) {
        scene::SceneTranslationReport result{
            .target    = report.target,
            .supported = report.supported,
        };
        result.diagnostics.reserve(report.diagnostics.size());
        for (const pathtracer::SceneDiagnostic& diagnostic : report.diagnostics) result.diagnostics.push_back(ToSpectraDiagnostic(diagnostic));
        return result;
    }

    [[nodiscard]] SpectraDockSlot ToSpectraDockSlot(const pathtracer::PathtracerDockSlot dockSlot) {
        switch (dockSlot) {
        case pathtracer::PathtracerDockSlot::Center: return SpectraDockSlot::Center;
        case pathtracer::PathtracerDockSlot::Left: return SpectraDockSlot::Left;
        case pathtracer::PathtracerDockSlot::LeftBottom: return SpectraDockSlot::LeftBottom;
        case pathtracer::PathtracerDockSlot::Right: return SpectraDockSlot::Right;
        case pathtracer::PathtracerDockSlot::RightBottom: return SpectraDockSlot::RightBottom;
        case pathtracer::PathtracerDockSlot::Bottom: return SpectraDockSlot::Bottom;
        case pathtracer::PathtracerDockSlot::Floating: return SpectraDockSlot::Floating;
        }
        throw std::runtime_error("Unknown pathtracer dock slot");
    }

    [[nodiscard]] SpectraDockSlot ToSpectraDockSlot(const rasterizer::RasterizerDockSlot dockSlot) {
        switch (dockSlot) {
        case rasterizer::RasterizerDockSlot::Center: return SpectraDockSlot::Center;
        case rasterizer::RasterizerDockSlot::Left: return SpectraDockSlot::Left;
        case rasterizer::RasterizerDockSlot::LeftBottom: return SpectraDockSlot::LeftBottom;
        case rasterizer::RasterizerDockSlot::Right: return SpectraDockSlot::Right;
        case rasterizer::RasterizerDockSlot::RightBottom: return SpectraDockSlot::RightBottom;
        case rasterizer::RasterizerDockSlot::Bottom: return SpectraDockSlot::Bottom;
        case rasterizer::RasterizerDockSlot::Floating: return SpectraDockSlot::Floating;
        }
        throw std::runtime_error("Unknown rasterizer dock slot");
    }

    [[nodiscard]] SpectraPanel ToSpectraPanel(pathtracer::PathtracerPanel panel) {
        return SpectraPanel{
            .id                  = std::move(panel.id),
            .title               = std::move(panel.title),
            .icon                = std::move(panel.icon),
            .owner_renderer      = std::string{pathtracer::PathtracerRenderer::target_name()},
            .shortcut_label      = std::move(panel.shortcut_label),
            .shortcut_key        = panel.shortcut_key,
            .dock_slot           = ToSpectraDockSlot(panel.dock_slot),
            .window_flags        = panel.window_flags,
            .visible             = panel.visible,
            .closable            = panel.closable,
            .show_in_menu        = panel.show_in_menu,
            .show_in_toolbar     = panel.show_in_toolbar,
            .zero_window_padding = panel.zero_window_padding,
            .draw                = std::move(panel.draw),
        };
    }

    [[nodiscard]] SpectraPanel ToSpectraPanel(rasterizer::RasterizerPanel panel) {
        return SpectraPanel{
            .id                  = std::move(panel.id),
            .title               = std::move(panel.title),
            .icon                = std::move(panel.icon),
            .owner_renderer      = std::string{rasterizer::RasterizerRenderer::target_name()},
            .shortcut_label      = std::move(panel.shortcut_label),
            .shortcut_key        = panel.shortcut_key,
            .dock_slot           = ToSpectraDockSlot(panel.dock_slot),
            .window_flags        = panel.window_flags,
            .visible             = panel.visible,
            .closable            = panel.closable,
            .show_in_menu        = panel.show_in_menu,
            .show_in_toolbar     = panel.show_in_toolbar,
            .zero_window_padding = panel.zero_window_padding,
            .draw                = std::move(panel.draw),
        };
    }

    class PathtracerSpectraHost final {
    public:
        explicit PathtracerSpectraHost(Spectra& host) : host(&host) {}

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const {
            return this->host->physical_device();
        }

        [[nodiscard]] const vk::raii::Device& device() const {
            return this->host->device();
        }

        [[nodiscard]] std::uint32_t frame_count() const {
            return this->host->frame_count();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() const {
            return this->host->swapchain_extent();
        }

        void register_panel(pathtracer::PathtracerPanel panel) const {
            this->host->register_panel(ToSpectraPanel(std::move(panel)));
        }

        void set_window_detail(std::string detail) const {
            this->host->set_window_detail(std::move(detail));
        }

    private:
        Spectra* host{};
    };

    class RasterizerSpectraHost final {
    public:
        explicit RasterizerSpectraHost(Spectra& host) : host(&host) {}

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const {
            return this->host->physical_device();
        }

        [[nodiscard]] const vk::raii::Device& device() const {
            return this->host->device();
        }

        [[nodiscard]] std::uint32_t frame_count() const {
            return this->host->frame_count();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() const {
            return this->host->swapchain_extent();
        }

        void register_panel(rasterizer::RasterizerPanel panel) const {
            this->host->register_panel(ToSpectraPanel(std::move(panel)));
        }

        void set_window_detail(std::string detail) const {
            this->host->set_window_detail(std::move(detail));
        }

    private:
        Spectra* host{};
    };

    class PathtracerSpectraRenderer final {
    public:
        explicit PathtracerSpectraRenderer(std::shared_ptr<scene::SceneWorkspace> sourceWorkspace) : source_workspace(std::move(sourceWorkspace)) {
            if (this->source_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a Spectra scene workspace");
            const std::shared_ptr<const scene::SceneSnapshot> sourceSnapshot = this->source_workspace->snapshot();
            if (sourceSnapshot == nullptr) throw std::runtime_error("Pathtracer adapter received an empty Spectra scene workspace");
            this->source_revision = sourceSnapshot->revision;
            this->target_workspace = std::make_shared<pathtracer::SceneWorkspace>(ToPathtracerScene(*sourceSnapshot));
            this->renderer         = std::make_unique<pathtracer::PathtracerRenderer>(this->target_workspace);
        }

        PathtracerSpectraRenderer(const PathtracerSpectraRenderer& other)                = delete;
        PathtracerSpectraRenderer(PathtracerSpectraRenderer&& other) noexcept            = default;
        PathtracerSpectraRenderer& operator=(const PathtracerSpectraRenderer& other)     = delete;
        PathtracerSpectraRenderer& operator=(PathtracerSpectraRenderer&& other) noexcept = default;
        ~PathtracerSpectraRenderer() noexcept                                            = default;

        [[nodiscard]] std::string_view name() const {
            return pathtracer::PathtracerRenderer::target_name();
        }

        void attach(Spectra& host) {
            this->synchronize_scene_workspace();
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->attach(pathtracerHost);
        }

        void detach(Spectra& host) noexcept {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->detach(pathtracerHost);
        }

        void before_imgui_shutdown(Spectra& host) noexcept {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->before_imgui_shutdown(pathtracerHost);
        }

        void after_imgui_created(Spectra& host) {
            PathtracerSpectraHost pathtracerHost{host};
            this->renderer->after_imgui_created(pathtracerHost);
        }

        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& host, const SpectraFrameInfo& frame) {
            this->synchronize_scene_workspace();
            PathtracerSpectraHost pathtracerHost{host};
            pathtracer::PathtracerFrameResult result = this->renderer->begin_frame(pathtracerHost, pathtracer::PathtracerFrameInfo{
                                                                                                        .frame_index = frame.frame_index,
                                                                                                        .image_index = frame.image_index,
                                                                                                    });
            return SpectraFrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer->record_frame(commandBuffer);
        }

    private:
        void synchronize_scene_workspace() {
            const std::shared_ptr<const scene::SceneSnapshot> sourceSnapshot = this->source_workspace->snapshot();
            if (sourceSnapshot == nullptr) throw std::runtime_error("Pathtracer adapter received an empty Spectra scene workspace");
            if (sourceSnapshot->revision == this->source_revision) return;
            pathtracer::SceneEditBuilder edit;
            edit.replaceSnapshot(ToPathtracerScene(*sourceSnapshot), pathtracer::SceneDirtyFlags::Snapshot);
            [[maybe_unused]] const pathtracer::SceneEditBatch batch = this->target_workspace->commit(std::move(edit));
            this->source_revision = sourceSnapshot->revision;
        }

        std::shared_ptr<scene::SceneWorkspace> source_workspace{};
        scene::SceneRevision source_revision{};
        std::shared_ptr<pathtracer::SceneWorkspace> target_workspace{};
        std::unique_ptr<pathtracer::PathtracerRenderer> renderer{};
    };

    class RasterizerSpectraRenderer final {
    public:
        RasterizerSpectraRenderer() = default;

        [[nodiscard]] std::string_view name() const {
            return rasterizer::RasterizerRenderer::target_name();
        }

        void attach(Spectra& host) {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer.attach(rasterizerHost);
        }

        void detach(Spectra& host) noexcept {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer.detach(rasterizerHost);
        }

        void before_imgui_shutdown(Spectra& host) noexcept {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer.before_imgui_shutdown(rasterizerHost);
        }

        void after_imgui_created(Spectra& host) {
            RasterizerSpectraHost rasterizerHost{host};
            this->renderer.after_imgui_created(rasterizerHost);
        }

        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& host, const SpectraFrameInfo& frame) {
            RasterizerSpectraHost rasterizerHost{host};
            rasterizer::RasterizerFrameResult result = this->renderer.begin_frame(rasterizerHost, rasterizer::RasterizerFrameInfo{
                                                                                                      .frame_index = frame.frame_index,
                                                                                                      .image_index = frame.image_index,
                                                                                                  });
            return SpectraFrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& commandBuffer) {
            this->renderer.record_frame(commandBuffer);
        }

    private:
        rasterizer::RasterizerRenderer renderer{};
    };

    static_assert(SpectraRendererForHost<PathtracerSpectraRenderer, Spectra>);
    static_assert(SpectraRendererForHost<RasterizerSpectraRenderer, Spectra>);

    [[nodiscard]] scene::SceneTranslationTarget PathtracerTranslationTarget() {
        return scene::SceneTranslationTarget{
            .rendererName = std::string{pathtracer::PathtracerRenderer::target_name()},
            .probe =
                [](const scene::SceneProbeReport& probe) {
                    scene::SceneTranslationReport report = ToSpectraReport(pathtracer::AnalyzePathtracerSceneProbe(ToPathtracerProbeReport(probe)));
                    if (report.target.empty()) report.target = std::string{pathtracer::PathtracerRenderer::target_name()};
                    return report;
                },
            .analyze =
                [](const scene::SceneSnapshot& document) {
                    scene::SceneTranslationReport report = ToSpectraReport(pathtracer::AnalyzePathtracerSceneSupport(ToPathtracerScene(document)));
                    if (report.target.empty()) report.target = std::string{pathtracer::PathtracerRenderer::target_name()};
                    return report;
                },
        };
    }

    [[nodiscard]] scene::SceneTranslationReport UnsupportedRasterizerReport(std::string source) {
        scene::SceneTranslationReport report{.target = std::string{rasterizer::RasterizerRenderer::target_name()}, .supported = false};
        report.diagnostics.push_back(scene::SceneDiagnostic{
            .source  = scene::SceneSourceLocation{.filename = std::move(source), .line = 1, .column = 1},
            .message = "Rasterizer backend does not currently provide PBRT scene rasterization translation",
        });
        return report;
    }

    [[nodiscard]] scene::SceneTranslationTarget RasterizerTranslationTarget() {
        return scene::SceneTranslationTarget{
            .rendererName = std::string{rasterizer::RasterizerRenderer::target_name()},
            .probe        = [](const scene::SceneProbeReport& probe) { return UnsupportedRasterizerReport(probe.source); },
            .analyze      = [](const scene::SceneSnapshot& document) { return UnsupportedRasterizerReport(document.source); },
        };
    }

    [[nodiscard]] std::string_view DefaultRendererName() {
        return pathtracer::PathtracerRenderer::target_name();
    }

    void RegisterRendererSceneTargets(scene::SceneLibrary& sceneLibrary) {
        sceneLibrary.register_translation_target(PathtracerTranslationTarget());
        sceneLibrary.register_translation_target(RasterizerTranslationTarget());
    }

    void RegisterRenderers(Spectra& app, std::shared_ptr<scene::SceneWorkspace> sceneWorkspace) {
        app.register_renderer(PathtracerSpectraRenderer{std::move(sceneWorkspace)});
        app.register_renderer(RasterizerSpectraRenderer{});
    }
} // namespace spectra::app

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra_gui");

        std::shared_ptr<spectra::scene::SceneLibrary> scene_library = std::make_shared<spectra::scene::SceneLibrary>();
        spectra::app::RegisterRendererSceneTargets(*scene_library);
        scene_library->load_first_supported_scene(spectra::app::DefaultRendererName());

        std::shared_ptr<spectra::scene::SceneWorkspace> document_workspace = scene_library->document_workspace();
        spectra::Spectra app{"Spectra"};
        app.set_renderer_availability_callback([scene_library](const std::string_view renderer_name) { return scene_library->renderer_availability(renderer_name); });
        app.set_renderer_activation_callback([scene_library](const std::string_view renderer_name) { scene_library->set_active_renderer(renderer_name); });
        scene_library->attach(app);
        spectra::app::RegisterRenderers(app, std::move(document_workspace));
        app.run();
        scene_library->detach();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
