module;

#ifndef SPECTRA_PROJECT_SCENE_ROOT
#error "SPECTRA_PROJECT_SCENE_ROOT must point to the project-local scene directory."
#endif

module spectra.scene;

import spectra.util.math;
import std;

extern "C++" {
namespace spectra::scene {
    [[nodiscard]] SceneDirtyFlags operator|(const SceneDirtyFlags left, const SceneDirtyFlags right) {
        return static_cast<SceneDirtyFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    [[nodiscard]] SceneDirtyFlags operator&(const SceneDirtyFlags left, const SceneDirtyFlags right) {
        return static_cast<SceneDirtyFlags>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
    }

    SceneDirtyFlags& operator|=(SceneDirtyFlags& left, const SceneDirtyFlags right) {
        left = left | right;
        return left;
    }

    [[nodiscard]] bool HasDirtyFlag(const SceneDirtyFlags flags, const SceneDirtyFlags flag) {
        return (flags & flag) != SceneDirtyFlags::None;
    }

    namespace {
        [[nodiscard]] std::uint64_t SceneIdValue(const SceneCameraId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneMaterialId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneTextureId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneMediumId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneLightId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneShapeId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneObjectDefinitionId id) {
            return id.value;
        }

        [[nodiscard]] std::uint64_t SceneIdValue(const SceneObjectInstanceId id) {
            return id.value;
        }
    } // namespace

    void SceneEditBuilder::replaceSnapshot(SceneSnapshot snapshot, const SceneDirtyFlags dirty) {
        if (dirty == SceneDirtyFlags::None) throw std::runtime_error("Scene snapshot replacement must describe dirty state");
        this->replacement = std::move(snapshot);
        this->dirty       = dirty;
    }

    SceneWorkspace::SceneWorkspace(SceneSnapshot snapshot) {
        this->assignMissingIds(snapshot);
        if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(snapshot));
    }

    [[nodiscard]] bool SceneWorkspace::loaded() const {
        return this->currentSnapshot != nullptr;
    }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> SceneWorkspace::snapshot() const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded snapshot");
        return this->currentSnapshot;
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty scene edit");
        if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit a scene edit without dirty state");

        SceneSnapshot next                 = std::move(*edit.replacement);
        const SceneRevision beforeRevision = this->currentSnapshot->revision;
        next.revision                      = SceneRevision{beforeRevision.value + 1};
        this->assignMissingIds(next);
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(next));

        SceneEditBatch batch = this->fullEdit(beforeRevision, *this->currentSnapshot);
        batch.dirty          = edit.dirty;
        this->lastEdit       = batch;
        return batch;
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::changes_since(const SceneRevision revision) const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query scene changes from an unloaded workspace");
        if (revision == this->currentSnapshot->revision) {
            return SceneEditBatch{
                .beforeRevision = revision,
                .afterRevision  = revision,
                .dirty          = SceneDirtyFlags::None,
            };
        }
        if (revision.value == 0) return this->fullEdit(revision, *this->currentSnapshot);
        if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
        throw std::runtime_error("Scene edit history for the requested revision is unavailable");
    }

    void SceneWorkspace::assignMissingIds(SceneSnapshot& snapshot) {
        std::uint64_t maxId  = 0;
        const auto observeId = [&maxId](const auto id) { maxId = std::max(maxId, SceneIdValue(id)); };
        observeId(snapshot.renderSettings.camera.id);
        for (const SceneMaterial& material : snapshot.materials) observeId(material.id);
        for (const SceneTexture& texture : snapshot.textures) observeId(texture.id);
        for (const SceneMedium& medium : snapshot.media) observeId(medium.id);
        for (const SceneLight& light : snapshot.lights) observeId(light.id);
        for (const SceneShape& shape : snapshot.shapes) observeId(shape.id);
        for (const SceneObjectDefinition& definition : snapshot.objectDefinitions) {
            observeId(definition.id);
            for (const SceneShape& shape : definition.shapes) observeId(shape.id);
        }
        for (const SceneObjectInstance& instance : snapshot.objectInstances) observeId(instance.id);
        this->nextId = std::max(this->nextId, maxId + 1);

        if (snapshot.renderSettings.camera.id.value == 0) snapshot.renderSettings.camera.id = SceneCameraId{this->nextSceneId()};
        for (SceneMaterial& material : snapshot.materials)
            if (material.id.value == 0) material.id = SceneMaterialId{this->nextSceneId()};
        for (SceneTexture& texture : snapshot.textures)
            if (texture.id.value == 0) texture.id = SceneTextureId{this->nextSceneId()};
        for (SceneMedium& medium : snapshot.media)
            if (medium.id.value == 0) medium.id = SceneMediumId{this->nextSceneId()};
        for (SceneLight& light : snapshot.lights)
            if (light.id.value == 0) light.id = SceneLightId{this->nextSceneId()};
        for (SceneShape& shape : snapshot.shapes)
            if (shape.id.value == 0) shape.id = SceneShapeId{this->nextSceneId()};
        for (SceneObjectDefinition& definition : snapshot.objectDefinitions) {
            if (definition.id.value == 0) definition.id = SceneObjectDefinitionId{this->nextSceneId()};
            for (SceneShape& shape : definition.shapes)
                if (shape.id.value == 0) shape.id = SceneShapeId{this->nextSceneId()};
        }
        for (SceneObjectInstance& instance : snapshot.objectInstances)
            if (instance.id.value == 0) instance.id = SceneObjectInstanceId{this->nextSceneId()};
    }

    [[nodiscard]] std::uint64_t SceneWorkspace::nextSceneId() {
        const std::uint64_t id = this->nextId;
        ++this->nextId;
        return id;
    }

    [[nodiscard]] SceneEditBatch SceneWorkspace::fullEdit(const SceneRevision before, const SceneSnapshot& snapshot) const {
        SceneEditBatch batch{
            .beforeRevision = before,
            .afterRevision  = snapshot.revision,
            .dirty          = SceneDirtyFlags::Camera | SceneDirtyFlags::Film | SceneDirtyFlags::RenderSettings | SceneDirtyFlags::Transform | SceneDirtyFlags::Geometry | SceneDirtyFlags::Material | SceneDirtyFlags::Texture | SceneDirtyFlags::Light | SceneDirtyFlags::Medium | SceneDirtyFlags::Topology | SceneDirtyFlags::CompiledScene,
        };
        batch.cameras.push_back(snapshot.renderSettings.camera.id);
        for (const SceneMaterial& material : snapshot.materials) batch.materials.push_back(material.id);
        for (const SceneTexture& texture : snapshot.textures) batch.textures.push_back(texture.id);
        for (const SceneMedium& medium : snapshot.media) batch.media.push_back(medium.id);
        for (const SceneLight& light : snapshot.lights) batch.lights.push_back(light.id);
        for (const SceneShape& shape : snapshot.shapes) batch.shapes.push_back(shape.id);
        for (const SceneObjectDefinition& definition : snapshot.objectDefinitions) {
            batch.objectDefinitions.push_back(definition.id);
            for (const SceneShape& shape : definition.shapes) batch.shapes.push_back(shape.id);
        }
        for (const SceneObjectInstance& instance : snapshot.objectInstances) batch.objectInstances.push_back(instance.id);
        return batch;
    }
} // namespace spectra::scene

namespace spectra::scene::builtin {
    namespace {
        [[nodiscard]] std::string SceneResource(const std::string_view relativePath) {
            return std::format("{}/{}", SPECTRA_PROJECT_SCENE_ROOT, relativePath);
        }

        [[nodiscard]] SceneSpectrumInput Rgb(const float r, const float g, const float b) {
            return SceneSpectrumInput{SceneRgb{r, g, b}};
        }

        [[nodiscard]] SceneSpectrumInput SpectrumSamples(std::initializer_list<float> values) {
            return SceneSpectrumInput{std::vector<float>(values)};
        }

        [[nodiscard]] SceneSpectrumInput SpectrumTexture(const SceneTextureId texture) {
            return SceneSpectrumInput{SceneTextureReference{texture}};
        }

        [[nodiscard]] SceneFloatInput FloatValue(const float value) {
            return SceneFloatInput{value};
        }

        [[nodiscard]] SceneFloatInput FloatTexture(const SceneTextureId texture) {
            return SceneFloatInput{SceneTextureReference{texture}};
        }

        [[nodiscard]] SceneSnapshot BookScene() {
            const SceneMaterialId defaultDiffuse{1};
            const SceneMaterialId grayDiffuse{2};
            const SceneMaterialId bookPagesMaterial{3};
            const SceneMaterialId bookCoverMaterial{4};
            const SceneTextureId bookCoverTexture{5};
            const SceneTextureId bookPagesTexture{6};
            const SceneTextureId unevenBumpRaw{7};
            const SceneTextureId unevenBumpScale{8};
            const SceneTextureId unevenBump{9};
            const std::string mesh00001           = SceneResource("pbrt-book/geometry/mesh_00001.ply");
            const std::string mesh00002           = SceneResource("pbrt-book/geometry/mesh_00002.ply");
            const std::string mesh00003           = SceneResource("pbrt-book/geometry/mesh_00003.ply");
            const std::string bookCover           = SceneResource("pbrt-book/texture/book_pbrt.png");
            const std::string bookPages           = SceneResource("pbrt-book/texture/book_pages.png");
            const std::string unevenBumpFile      = SceneResource("pbrt-book/texture/uneven_bump.png");
            const math::Transform cameraFromWorld = math::Compose({
                math::Scale(-1.0f, 1.0f, 1.0f),
                math::LookAt(math::Point3(0.0f, 2.1088f, 13.574f), math::Point3(0.0f, 2.1088f, 12.574f), math::Vector3(0.0f, 1.0f, 0.0f)),
            });

            return SceneSnapshot{
                .name   = "default",
                .title  = "PBRT Book",
                .source = "spectra://scene/default",
                .renderSettings =
                    SceneRenderSettings{
                        .film =
                            SceneFilmSettings{
                                .filename    = "book.exr",
                                .sensor      = "canon_eos_100d",
                                .xResolution = 1920,
                                .yResolution = 1080,
                                .iso         = 150.0f,
                            },
                        .camera =
                            SceneCamera{
                                .kind            = CameraKind::Perspective,
                                .worldFromCamera = math::Inverse(cameraFromWorld),
                                .fovDegrees      = 26.5f,
                            },
                        .sampler =
                            SceneSamplerSettings{
                                .kind         = SamplerKind::Halton,
                                .pixelSamples = 2048,
                            },
                    },
                .materials =
                    {
                        SceneMaterial{
                            .id    = defaultDiffuse,
                            .name  = "default_diffuse",
                            .value = SceneDiffuseMaterial{},
                        },
                        SceneMaterial{
                            .id    = grayDiffuse,
                            .name  = "gray_diffuse",
                            .value = SceneDiffuseMaterial{.reflectance = Rgb(0.5f, 0.5f, 0.5f)},
                        },
                        SceneMaterial{
                            .id    = bookPagesMaterial,
                            .name  = "book_pages",
                            .value = SceneDiffuseMaterial{.reflectance = SpectrumTexture(bookPagesTexture)},
                        },
                        SceneMaterial{
                            .id    = bookCoverMaterial,
                            .name  = "book_cover",
                            .value = SceneCoatedDiffuseMaterial{.reflectance = SpectrumTexture(bookCoverTexture), .roughness = FloatValue(0.0003f), .displacement = FloatTexture(unevenBump)},
                        },
                    },
                .textures =
                    {
                        SceneTexture{
                            .id    = bookCoverTexture,
                            .kind  = TextureKind::Spectrum,
                            .name  = "book_cover",
                            .value = SceneImageTexture{.filename = bookCover},
                        },
                        SceneTexture{
                            .id    = bookPagesTexture,
                            .kind  = TextureKind::Spectrum,
                            .name  = "book_pages",
                            .value = SceneImageTexture{.filename = bookPages},
                        },
                        SceneTexture{
                            .id    = unevenBumpRaw,
                            .kind  = TextureKind::Float,
                            .name  = "uneven_bump_raw",
                            .value = SceneImageTexture{.filename = unevenBumpFile, .uScale = 1.5f, .vScale = 1.5f},
                        },
                        SceneTexture{
                            .id    = unevenBumpScale,
                            .kind  = TextureKind::Float,
                            .name  = "uneven_bump_scale",
                            .value = SceneConstantFloatTexture{.value = 0.0002f},
                        },
                        SceneTexture{
                            .id    = unevenBump,
                            .kind  = TextureKind::Float,
                            .name  = "uneven_bump",
                            .value = SceneScaleFloatTexture{.scale = unevenBumpScale, .texture = unevenBumpRaw},
                        },
                    },
                .shapes =
                    {
                        SceneShape{
                            .id              = SceneShapeId{10},
                            .name            = "warm_area_light",
                            .value           = SceneSphere{.radius = 7.5f},
                            .worldFromObject = math::Translate(math::Vector3(34.92f, 55.92f, -15.351f)),
                            .material        = defaultDiffuse,
                            .areaLight       = SceneAreaLight{.value = SceneDiffuseAreaLight{.emission = Rgb(41.5594f, 43.3127f, 45.066f)}},
                        },
                        SceneShape{
                            .id              = SceneShapeId{11},
                            .name            = "cool_area_light",
                            .value           = SceneSphere{.radius = 7.5f},
                            .worldFromObject = math::Translate(math::Vector3(-32.892f, 55.92f, 36.293f)),
                            .material        = defaultDiffuse,
                            .areaLight       = SceneAreaLight{.value = SceneDiffuseAreaLight{.emission = Rgb(65.066f, 63.3127f, 61.5594f)}},
                        },
                        SceneShape{
                            .id              = SceneShapeId{12},
                            .name            = "book_table",
                            .value           = ScenePlyMesh{.filename = mesh00001},
                            .worldFromObject = math::Scale(0.213f, 0.213f, 0.213f),
                            .material        = grayDiffuse,
                        },
                        SceneShape{
                            .id              = SceneShapeId{13},
                            .name            = "book_pages_mesh",
                            .value           = ScenePlyMesh{.filename = mesh00002},
                            .worldFromObject = math::Compose({
                                math::Translate(math::Vector3(0.0f, 2.2f, 0.0f)),
                                math::Rotate(77.3425f, math::Vector3(0.403388f, -0.754838f, -0.517202f)),
                                math::Scale(0.5f, 0.5f, 0.5f),
                            }),
                            .material        = bookPagesMaterial,
                        },
                        SceneShape{
                            .id              = SceneShapeId{14},
                            .name            = "book_cover_mesh",
                            .value           = ScenePlyMesh{.filename = mesh00003},
                            .worldFromObject = math::Compose({
                                math::Translate(math::Vector3(0.0f, 2.2f, 0.0f)),
                                math::Rotate(77.3425f, math::Vector3(0.403388f, -0.754838f, -0.517202f)),
                                math::Scale(0.5f, 0.5f, 0.5f),
                            }),
                            .material        = bookCoverMaterial,
                        },
                    },
            };
        }

        [[nodiscard]] SceneSnapshot ExplosionScene() {
            const SceneMaterialId interfaceMaterial{1};
            const SceneMaterialId groundMaterial{2};
            const SceneMediumId fireMedium{3};
            const SceneLightId skyLight{4};
            const std::string skyTexture          = SceneResource("explosion/textures/sky.exr");
            const std::string fireVolume          = SceneResource("explosion/fire.nvdb");
            const math::Transform cameraFromWorld = math::LookAt(math::Point3(0.0f, 120.0f, 20.0f), math::Point3(-0.5f, 0.0f, 30.0f), math::Vector3(0.0f, 0.0f, 1.0f));

            return SceneSnapshot{
                .name   = "explosion",
                .title  = "Explosion",
                .source = "spectra://scene/explosion",
                .renderSettings =
                    SceneRenderSettings{
                        .film =
                            SceneFilmSettings{
                                .filename     = "explosion.exr",
                                .sensor       = "nikon_d850",
                                .xResolution  = 1300,
                                .yResolution  = 1800,
                                .iso          = 100.0f,
                                .whiteBalance = 6000.0f,
                            },
                        .camera =
                            SceneCamera{
                                .worldFromCamera = math::Inverse(cameraFromWorld),
                                .fovDegrees      = 37.0f,
                            },
                        .integrator =
                            SceneIntegratorSettings{
                                .maxDepth = 5,
                            },
                    },
                .materials =
                    {
                        SceneMaterial{
                            .id    = interfaceMaterial,
                            .name  = "interface",
                            .value = SceneInterfaceMaterial{},
                        },
                        SceneMaterial{
                            .id    = groundMaterial,
                            .name  = "ground",
                            .value = SceneCoatedDiffuseMaterial{.reflectance = Rgb(0.4f, 0.4f, 0.4f), .roughness = FloatValue(0.001f)},
                        },
                    },
                .media =
                    {
                        SceneMedium{
                            .id   = fireMedium,
                            .name = "kaboom",
                            .value =
                                SceneNanoVdbMedium{
                                    .filename          = fireVolume,
                                    .sigmaS            = std::vector<float>{200.0f, 10.0f, 900.0f, 10.0f},
                                    .sigmaA            = std::vector<float>{200.0f, 10.0f, 900.0f, 10.0f},
                                    .leScale           = 5.0f,
                                    .temperatureCutoff = 1.0f,
                                    .temperatureScale  = 100.0f,
                                },
                            .worldFromMedium = math::Compose({
                                math::Scale(1.0f, 1.0f, 1.6f),
                                math::Rotate(90.0f, math::Vector3(1.0f, 0.0f, 0.0f)),
                            }),
                        },
                    },
                .lights =
                    {
                        SceneLight{
                            .id             = skyLight,
                            .name           = "sky",
                            .value          = SceneInfiniteLight{.filename = skyTexture, .scale = 2.0f},
                            .worldFromLight = math::Rotate(10.0f, math::Vector3(1.0f, 0.0f, 0.0f)),
                        },
                    },
                .shapes =
                    {
                        SceneShape{
                            .id              = SceneShapeId{5},
                            .name            = "volume_boundary",
                            .value           = SceneSphere{.radius = 80.0f},
                            .worldFromObject = math::Translate(math::Vector3(0.0f, 40.0f, 0.0f)),
                            .material        = interfaceMaterial,
                            .insideMedium    = fireMedium,
                        },
                        SceneShape{
                            .id              = SceneShapeId{6},
                            .name            = "ground_disk",
                            .value           = SceneDisk{.radius = 1000.0f},
                            .worldFromObject = math::Compose({
                                math::Translate(math::Vector3(0.0f, -50.0f, 0.0f)),
                                math::Translate(math::Vector3(0.0f, 0.0f, -4.0f)),
                            }),
                            .material        = groundMaterial,
                        },
                    },
            };
        }
    } // namespace

    SceneWorkspace BuildScene(const std::string_view name) {
        if (name == "default") return SceneWorkspace{BookScene()};
        if (name == "explosion") return SceneWorkspace{ExplosionScene()};
        throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", name));
    }
} // namespace spectra::scene::builtin

namespace spectra::scene {
    SceneInfo DescribeScene(const SceneSnapshot& scene) {
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
            if (std::holds_alternative<SceneInfiniteLight>(light.value)) ++infiniteLightCount;

        std::string camera = "perspective";
        if (scene.renderSettings.camera.kind != CameraKind::Perspective) throw std::runtime_error("Unknown Spectra camera kind");

        std::string sampler{};
        switch (scene.renderSettings.sampler.kind) {
        case SamplerKind::Halton: sampler = "halton"; break;
        case SamplerKind::ZSobol: sampler = "zsobol"; break;
        }

        std::string integrator{};
        switch (scene.renderSettings.integrator.kind) {
        case IntegratorKind::VolPath: integrator = "volpath"; break;
        }

        std::string accelerator{};
        switch (scene.renderSettings.accelerator.kind) {
        case AcceleratorKind::Bvh: accelerator = "bvh"; break;
        }

        return SceneInfo{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = camera,
            .sampler                 = sampler,
            .integrator              = integrator,
            .accelerator             = accelerator,
            .shape_count             = scene.shapes.size() + definitionShapeCount,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = areaLightCount,
            .infinite_light_count    = infiniteLightCount,
            .object_definition_count = scene.objectDefinitions.size(),
            .object_instance_count   = scene.objectInstances.size(),
            .camera_fov_degrees      = scene.renderSettings.camera.fovDegrees,
        };
    }

    SceneWorkspace BuildScene(const std::string_view name) {
        return builtin::BuildScene(name);
    }
} // namespace spectra::scene
}
