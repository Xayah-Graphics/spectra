export module spectra.scene.pbrt;

export import spectra.scene;
import std;

export namespace spectra::scene {
    inline constexpr std::string_view CornellBoxSceneId = "cornell-box/cornell-box.pbrt";

    enum class PbrtSceneDirtyFlags : std::uint32_t {
        None     = 0,
        Snapshot = 1,
    };

    enum class PbrtColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

    struct PbrtSceneParameter {
        std::string type{};
        std::string name{};
        std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{std::vector<float>{}};
        bool mayBeUnused{false};
        PbrtColorSpace colorSpace{PbrtColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    struct PbrtSceneEntity {
        std::string type{};
        std::vector<PbrtSceneParameter> parameters{};
        PbrtColorSpace colorSpace{PbrtColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    struct PbrtSceneTransform {
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

    struct PbrtSceneTransformSet {
        PbrtSceneTransform start{};
        PbrtSceneTransform end{};
        float startTime{0.0f};
        float endTime{1.0f};
        bool animated{false};
    };

    struct PbrtSceneOption {
        std::string name{};
        std::string value{};
        SceneSourceLocation source{};
    };

    struct PbrtSceneMediumInterface {
        std::string inside{};
        std::string outside{};
    };

    struct PbrtSceneRenderSettings {
        PbrtSceneEntity filter{.type = "gaussian"};
        PbrtSceneEntity film{.type = "rgb"};
        PbrtSceneEntity camera{.type = "perspective"};
        PbrtSceneEntity sampler{.type = "zsobol"};
        PbrtSceneEntity integrator{.type = "volpath"};
        PbrtSceneEntity accelerator{.type = "bvh"};
        PbrtSceneTransformSet cameraTransform{};
        std::string cameraMedium{};
        std::vector<PbrtSceneOption> options{};
    };

    struct PbrtSceneMaterial {
        std::string name{};
        PbrtSceneEntity entity{};
    };

    struct PbrtSceneTexture {
        std::string name{};
        std::string kind{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
    };

    struct PbrtSceneMedium {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
    };

    struct PbrtSceneLight {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
        std::string medium{};
    };

    struct PbrtSceneAreaLight {
        PbrtSceneEntity entity{};
    };

    struct PbrtSceneShape {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<PbrtSceneAreaLight> areaLight{};
        PbrtSceneMediumInterface mediumInterface{};
    };

    struct PbrtSceneObjectDefinition {
        std::string name{};
        std::vector<PbrtSceneShape> shapes{};
        SceneSourceLocation source{};
    };

    struct PbrtSceneObjectInstance {
        std::string name{};
        std::string definitionName{};
        PbrtSceneTransformSet transform{};
        SceneSourceLocation source{};
    };

    struct PbrtSceneSnapshot {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        PbrtSceneRenderSettings renderSettings{};
        std::vector<PbrtSceneMaterial> materials{};
        std::vector<PbrtSceneTexture> textures{};
        std::vector<PbrtSceneMedium> media{};
        std::vector<PbrtSceneLight> lights{};
        std::vector<PbrtSceneShape> shapes{};
        std::vector<PbrtSceneObjectDefinition> objectDefinitions{};
        std::vector<PbrtSceneObjectInstance> objectInstances{};
    };

    struct PbrtSceneEditBatch {
        SceneRevision beforeRevision{};
        SceneRevision afterRevision{};
        PbrtSceneDirtyFlags dirty{PbrtSceneDirtyFlags::None};
    };

    class PbrtSceneEditBuilder {
    public:
        void replaceSnapshot(PbrtSceneSnapshot snapshot, PbrtSceneDirtyFlags dirty) {
            if (dirty != PbrtSceneDirtyFlags::Snapshot) throw std::runtime_error("PBRT scene snapshot replacement must use snapshot dirty state");
            this->replacement = std::move(snapshot);
            this->dirty       = dirty;
        }

    private:
        std::optional<PbrtSceneSnapshot> replacement{};
        PbrtSceneDirtyFlags dirty{PbrtSceneDirtyFlags::None};

        friend class PbrtSceneWorkspace;
    };

    class PbrtSceneWorkspace {
    public:
        PbrtSceneWorkspace() = default;

        explicit PbrtSceneWorkspace(PbrtSceneSnapshot snapshot) {
            if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
            this->currentSnapshot = std::make_shared<PbrtSceneSnapshot>(std::move(snapshot));
        }

        [[nodiscard]] bool loaded() const {
            return this->currentSnapshot != nullptr;
        }

        [[nodiscard]] std::shared_ptr<const PbrtSceneSnapshot> snapshot() const {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("PBRT scene workspace does not contain a loaded snapshot");
            return this->currentSnapshot;
        }

        [[nodiscard]] PbrtSceneEditBatch commit(PbrtSceneEditBuilder edit) {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded PBRT scene workspace");
            if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty PBRT scene edit");
            if (edit.dirty != PbrtSceneDirtyFlags::Snapshot) throw std::runtime_error("PBRT scene edit commit must use snapshot dirty state");

            PbrtSceneSnapshot next                 = std::move(*edit.replacement);
            const SceneRevision beforeRevision = this->currentSnapshot->revision;
            next.revision                      = SceneRevision{beforeRevision.value + 1};
            this->currentSnapshot              = std::make_shared<PbrtSceneSnapshot>(std::move(next));

            PbrtSceneEditBatch batch = this->fullEdit(beforeRevision);
            batch.dirty          = edit.dirty;
            this->lastEdit       = batch;
            return batch;
        }

        [[nodiscard]] PbrtSceneEditBatch changes_since(SceneRevision revision) const {
            if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query PBRT scene changes from an unloaded workspace");
            if (revision == this->currentSnapshot->revision) {
                return PbrtSceneEditBatch{
                    .beforeRevision = revision,
                    .afterRevision  = revision,
                    .dirty          = PbrtSceneDirtyFlags::None,
                };
            }
            if (revision.value == 0) return this->fullEdit(revision);
            if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
            throw std::runtime_error("PBRT scene edit history for the requested revision is unavailable");
        }

    private:
        [[nodiscard]] PbrtSceneEditBatch fullEdit(SceneRevision before) const {
            return PbrtSceneEditBatch{
                .beforeRevision = before,
                .afterRevision  = this->currentSnapshot->revision,
                .dirty          = PbrtSceneDirtyFlags::Snapshot,
            };
        }

        std::shared_ptr<const PbrtSceneSnapshot> currentSnapshot{};
        std::optional<PbrtSceneEditBatch> lastEdit{};
    };

    struct PbrtSceneInfo {
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

    struct PbrtSceneDiagnostic {
        SceneSourceLocation source{};
        std::string message{};
    };

    enum class PbrtSceneProbeFeatureCategory {
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

    struct PbrtSceneProbeFeature {
        PbrtSceneProbeFeatureCategory category{PbrtSceneProbeFeatureCategory::Option};
        std::string type{};
        std::string kind{};
        SceneSourceLocation source{};
    };

    struct PbrtSceneProbeReport {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        std::vector<PbrtSceneProbeFeature> features{};
        std::vector<PbrtSceneDiagnostic> diagnostics{};
    };

    struct PbrtSceneTranslationReport {
        std::string target{};
        bool supported{true};
        std::vector<PbrtSceneDiagnostic> diagnostics{};
    };

    [[nodiscard]] PbrtSceneInfo DescribeScene(const PbrtSceneSnapshot& scene) {
        const auto oneFloatParameter = [](const std::vector<PbrtSceneParameter>& parameters, const std::string& name, const float default_value) {
            for (const PbrtSceneParameter& parameter : parameters) {
                if (parameter.type != "float" && parameter.type != "integer") continue;
                if (parameter.name != name) continue;
                return std::visit(
                    [default_value](const auto& values) -> float {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<float>>) {
                            if (!values.empty()) return values.front();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<int>>) {
                            if (!values.empty()) return static_cast<float>(values.front());
                        }
                        return default_value;
                    },
                    parameter.values);
            }
            return default_value;
        };

        std::size_t definitionShapeCount     = 0;
        std::size_t definitionAreaLightCount = 0;
        for (const PbrtSceneObjectDefinition& definition : scene.objectDefinitions) {
            definitionShapeCount += definition.shapes.size();
            for (const PbrtSceneShape& shape : definition.shapes)
                if (shape.areaLight.has_value()) ++definitionAreaLightCount;
        }

        std::size_t areaLightCount = definitionAreaLightCount;
        for (const PbrtSceneShape& shape : scene.shapes)
            if (shape.areaLight.has_value()) ++areaLightCount;

        std::size_t infiniteLightCount = 0;
        for (const PbrtSceneLight& light : scene.lights)
            if (light.entity.type == "infinite") ++infiniteLightCount;

        float cameraFov = oneFloatParameter(scene.renderSettings.camera.parameters, "fov", scene.renderSettings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(cameraFov > 0.0f && cameraFov < 180.0f)) cameraFov = 45.0f;

        return PbrtSceneInfo{
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



    enum class PbrtSceneCatalogEntryState {
        Pending,
        Candidate,
        NonScene,
        Invalid,
    };

    struct PbrtSceneCatalogEntry {
        std::string id{};
        std::string displayName{};
        std::string group{};
        std::filesystem::path relativePath{};
        std::filesystem::path sourcePath{};
        PbrtSceneCatalogEntryState state{PbrtSceneCatalogEntryState::Pending};
        SceneRevision revision{};
        std::optional<PbrtSceneProbeReport> probe{};
        std::vector<PbrtSceneDiagnostic> issues{};
    };

    struct PbrtSceneCatalog {
        std::filesystem::path root{};
        std::vector<PbrtSceneCatalogEntry> entries{};
        std::size_t pending_count{};
        std::size_t candidate_count{};
        std::size_t non_scene_count{};
        std::size_t invalid_count{};
    };

    [[nodiscard]] PbrtSceneCatalog DiscoverPbrtSceneCatalog();
    [[nodiscard]] PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    [[nodiscard]] PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    [[nodiscard]] PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    [[nodiscard]] PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry);
    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    [[nodiscard]] PbrtSceneWorkspace BuildPbrtScene(std::string_view name);

} // namespace spectra::scene

