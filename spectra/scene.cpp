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

    EditableScene::EditableScene(SceneSnapshot snapshot) {
        this->assignMissingIds(snapshot);
        if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(snapshot));
    }

    [[nodiscard]] bool EditableScene::loaded() const {
        return this->currentSnapshot != nullptr;
    }

    [[nodiscard]] std::shared_ptr<const SceneSnapshot> EditableScene::snapshot() const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Editable scene does not contain a loaded snapshot");
        return this->currentSnapshot;
    }

    [[nodiscard]] SceneEditBatch EditableScene::commit(SceneEditBuilder edit) {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded scene");
        if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty scene edit");
        if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit a scene edit without dirty state");

        SceneSnapshot next                 = std::move(*edit.replacement);
        const SceneRevision beforeRevision = this->currentSnapshot->revision;
        next.revision                      = SceneRevision{beforeRevision.value + 1};
        this->assignMissingIds(next);
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(next));

        SceneEditBatch batch{
            .beforeRevision = beforeRevision,
            .afterRevision  = this->currentSnapshot->revision,
            .dirty          = edit.dirty,
        };
        batch.cameras.push_back(this->currentSnapshot->renderSettings.camera.id);
        for (const SceneMaterial& material : this->currentSnapshot->materials) batch.materials.push_back(material.id);
        for (const SceneTexture& texture : this->currentSnapshot->textures) batch.textures.push_back(texture.id);
        for (const SceneMedium& medium : this->currentSnapshot->media) batch.media.push_back(medium.id);
        for (const SceneLight& light : this->currentSnapshot->lights) batch.lights.push_back(light.id);
        for (const SceneShape& shape : this->currentSnapshot->shapes) batch.shapes.push_back(shape.id);
        for (const SceneObjectDefinition& definition : this->currentSnapshot->objectDefinitions) {
            batch.objectDefinitions.push_back(definition.id);
            for (const SceneShape& shape : definition.shapes) batch.shapes.push_back(shape.id);
        }
        for (const SceneObjectInstance& instance : this->currentSnapshot->objectInstances) batch.objectInstances.push_back(instance.id);
        return batch;
    }

    void EditableScene::assignMissingIds(SceneSnapshot& snapshot) {
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

    [[nodiscard]] std::uint64_t EditableScene::nextSceneId() {
        const std::uint64_t id = this->nextId;
        ++this->nextId;
        return id;
    }
} // namespace spectra::scene

namespace spectra::scene::builtin {
    namespace {
        [[nodiscard]] std::string SceneResource(std::string_view relativePath) {
            return std::format("{}/{}", SPECTRA_PROJECT_SCENE_ROOT, relativePath);
        }

        [[nodiscard]] SceneParameters Parameters(std::initializer_list<SceneParameter> parameters) {
            return SceneParameters{
                .values = std::vector<SceneParameter>(parameters),
            };
        }

        [[nodiscard]] SceneParameter FloatParameter(std::string_view type, std::string_view name, std::initializer_list<float> values) {
            return SceneParameter{
                .type   = std::string{type},
                .name   = std::string{name},
                .values = std::vector<float>(values),
            };
        }

        [[nodiscard]] SceneParameter IntegerParameter(std::string_view name, std::initializer_list<int> values) {
            return SceneParameter{
                .type   = "integer",
                .name   = std::string{name},
                .values = std::vector<int>(values),
            };
        }

        [[nodiscard]] SceneParameter StringParameter(std::string_view type, std::string_view name, std::initializer_list<std::string_view> values) {
            SceneParameter parameter{
                .type   = std::string{type},
                .name   = std::string{name},
                .values = std::vector<std::string>{},
            };
            std::vector<std::string>& strings = std::get<std::vector<std::string>>(parameter.values);
            strings.reserve(values.size());
            for (std::string_view value : values) strings.emplace_back(value);
            return parameter;
        }

        [[nodiscard]] SceneParameter StringParameter(std::string_view name, std::initializer_list<std::string_view> values) {
            return StringParameter("string", name, values);
        }

        [[nodiscard]] SceneSnapshot BookScene() {
            const std::string mesh00001           = SceneResource("pbrt-book/geometry/mesh_00001.ply");
            const std::string mesh00002           = SceneResource("pbrt-book/geometry/mesh_00002.ply");
            const std::string mesh00003           = SceneResource("pbrt-book/geometry/mesh_00003.ply");
            const std::string bookCover           = SceneResource("pbrt-book/texture/book_pbrt.png");
            const std::string bookPages           = SceneResource("pbrt-book/texture/book_pages.png");
            const std::string unevenBump          = SceneResource("pbrt-book/texture/uneven_bump.png");
            const math::Transform cameraFromWorld = math::Compose({
                math::Scale(-1.0f, 1.0f, 1.0f),
                math::LookAt(math::Point3(0.0f, 2.1088f, 13.574f), math::Point3(0.0f, 2.1088f, 12.574f), math::Vector3(0.0f, 1.0f, 0.0f)),
            });

            SceneSnapshot scene{
                .name   = "default",
                .title  = "PBRT Book",
                .source = "spectra://scene/default",
                .renderSettings =
                    SceneRenderSettings{
                        .film =
                            SceneComponent{
                                .type       = "rgb",
                                .parameters = Parameters({
                                    StringParameter("filename", {"book.exr"}),
                                    IntegerParameter("yresolution", {1080}),
                                    IntegerParameter("xresolution", {1920}),
                                    StringParameter("sensor", {"canon_eos_100d"}),
                                    FloatParameter("float", "iso", {150.0f}),
                                }),
                            },
                        .camera =
                            SceneCamera{
                                .type            = "perspective",
                                .parameters      = Parameters({FloatParameter("float", "fov", {26.5f})}),
                                .worldFromCamera = math::Inverse(cameraFromWorld),
                                .fovDegrees      = 26.5f,
                            },
                        .sampler =
                            SceneComponent{
                                .type       = "halton",
                                .parameters = Parameters({IntegerParameter("pixelsamples", {2048})}),
                            },
                    },
                .materials =
                    {
                        SceneMaterial{
                            .name = "default_diffuse",
                            .type = "diffuse",
                        },
                        SceneMaterial{
                            .name       = "gray_diffuse",
                            .type       = "diffuse",
                            .parameters = Parameters({FloatParameter("rgb", "reflectance", {0.5f, 0.5f, 0.5f})}),
                        },
                        SceneMaterial{
                            .name       = "book_pages",
                            .type       = "diffuse",
                            .parameters = Parameters({StringParameter("texture", "reflectance", {"book_pages"})}),
                        },
                        SceneMaterial{
                            .name       = "book_cover",
                            .type       = "coateddiffuse",
                            .parameters = Parameters({
                                StringParameter("texture", "displacement", {"uneven_bump"}),
                                StringParameter("texture", "reflectance", {"book_cover"}),
                                FloatParameter("float", "roughness", {0.0003f}),
                            }),
                        },
                    },
                .textures =
                    {
                        SceneTexture{
                            .kind       = TextureKind::Spectrum,
                            .name       = "book_cover",
                            .type       = "imagemap",
                            .parameters = Parameters({StringParameter("filename", {std::string_view{bookCover}})}),
                        },
                        SceneTexture{
                            .kind       = TextureKind::Spectrum,
                            .name       = "book_pages",
                            .type       = "imagemap",
                            .parameters = Parameters({StringParameter("filename", {std::string_view{bookPages}})}),
                        },
                        SceneTexture{
                            .kind       = TextureKind::Float,
                            .name       = "uneven_bump_raw",
                            .type       = "imagemap",
                            .parameters = Parameters({
                                StringParameter("filename", {std::string_view{unevenBump}}),
                                FloatParameter("float", "vscale", {1.5f}),
                                FloatParameter("float", "uscale", {1.5f}),
                            }),
                        },
                        SceneTexture{
                            .kind       = TextureKind::Float,
                            .name       = "uneven_bump_scale",
                            .type       = "constant",
                            .parameters = Parameters({FloatParameter("float", "value", {0.0002f})}),
                        },
                        SceneTexture{
                            .kind       = TextureKind::Float,
                            .name       = "uneven_bump",
                            .type       = "scale",
                            .parameters = Parameters({
                                StringParameter("texture", "scale", {"uneven_bump_scale"}),
                                StringParameter("texture", "tex", {"uneven_bump_raw"}),
                            }),
                        },
                    },
                .shapes =
                    {
                        SceneShape{
                            .type            = "sphere",
                            .parameters      = Parameters({FloatParameter("float", "radius", {7.5f})}),
                            .worldFromObject = math::Translate(math::Vector3(34.92f, 55.92f, -15.351f)),
                            .material        = "default_diffuse",
                            .areaLight       = SceneAreaLight{.type = "diffuse", .parameters = Parameters({FloatParameter("rgb", "L", {41.5594f, 43.3127f, 45.066f})})},
                        },
                        SceneShape{
                            .type            = "sphere",
                            .parameters      = Parameters({FloatParameter("float", "radius", {7.5f})}),
                            .worldFromObject = math::Translate(math::Vector3(-32.892f, 55.92f, 36.293f)),
                            .material        = "default_diffuse",
                            .areaLight       = SceneAreaLight{.type = "diffuse", .parameters = Parameters({FloatParameter("rgb", "L", {65.066f, 63.3127f, 61.5594f})})},
                        },
                        SceneShape{
                            .type            = "plymesh",
                            .parameters      = Parameters({StringParameter("filename", {std::string_view{mesh00001}})}),
                            .worldFromObject = math::Scale(0.213f, 0.213f, 0.213f),
                            .material        = "gray_diffuse",
                        },
                        SceneShape{
                            .type       = "plymesh",
                            .parameters = Parameters({StringParameter("filename", {std::string_view{mesh00002}})}),
                            .worldFromObject =
                                math::Compose(
                                    {
                                        math::Translate(math::Vector3(0.0f, 2.2f, 0.0f)),
                                        math::Rotate(77.3425f, math::Vector3(0.403388f, -0.754838f, -0.517202f)),
                                        math::Scale(0.5f, 0.5f, 0.5f),
                                    }),
                            .material = "book_pages",
                        },
                        SceneShape{
                            .type       = "plymesh",
                            .parameters = Parameters({StringParameter("filename", {std::string_view{mesh00003}})}),
                            .worldFromObject =
                                math::Compose(
                                    {
                                        math::Translate(math::Vector3(0.0f, 2.2f, 0.0f)),
                                        math::Rotate(77.3425f, math::Vector3(0.403388f, -0.754838f, -0.517202f)),
                                        math::Scale(0.5f, 0.5f, 0.5f),
                                    }),
                            .material = "book_cover",
                        },
                    },
            };
            return scene;
        }

        [[nodiscard]] SceneSnapshot ExplosionScene() {
            const std::string skyTexture          = SceneResource("explosion/textures/sky.exr");
            const std::string fireVolume          = SceneResource("explosion/fire.nvdb");
            const math::Transform cameraFromWorld = math::LookAt(math::Point3(0.0f, 120.0f, 20.0f), math::Point3(-0.5f, 0.0f, 30.0f), math::Vector3(0.0f, 0.0f, 1.0f));

            SceneSnapshot scene{
                .name   = "explosion",
                .title  = "Explosion",
                .source = "spectra://scene/explosion",
                .renderSettings =
                    SceneRenderSettings{
                        .film =
                            SceneComponent{
                                .type       = "rgb",
                                .parameters = Parameters({
                                    IntegerParameter("xresolution", {1300}),
                                    IntegerParameter("yresolution", {1800}),
                                    StringParameter("sensor", {"nikon_d850"}),
                                    FloatParameter("float", "whitebalance", {6000.0f}),
                                    FloatParameter("float", "iso", {100.0f}),
                                    StringParameter("filename", {"explosion.exr"}),
                                }),
                            },
                        .camera =
                            SceneCamera{
                                .type            = "perspective",
                                .parameters      = Parameters({FloatParameter("float", "fov", {37.0f})}),
                                .worldFromCamera = math::Inverse(cameraFromWorld),
                                .fovDegrees      = 37.0f,
                            },
                        .integrator =
                            SceneComponent{
                                .type       = "volpath",
                                .parameters = Parameters({IntegerParameter("maxdepth", {5})}),
                            },
                    },
                .materials =
                    {
                        SceneMaterial{
                            .name = "interface",
                            .type = "interface",
                        },
                        SceneMaterial{
                            .name       = "ground",
                            .type       = "coateddiffuse",
                            .parameters = Parameters({
                                FloatParameter("rgb", "reflectance", {0.4f, 0.4f, 0.4f}),
                                FloatParameter("float", "roughness", {0.001f}),
                            }),
                        },
                    },
                .media =
                    {
                        SceneMedium{
                            .name            = "kaboom",
                            .type            = "nanovdb",
                            .parameters      = Parameters({
                                StringParameter("filename", {std::string_view{fireVolume}}),
                                FloatParameter("spectrum", "sigma_s", {200.0f, 10.0f, 900.0f, 10.0f}),
                                FloatParameter("spectrum", "sigma_a", {200.0f, 10.0f, 900.0f, 10.0f}),
                                FloatParameter("float", "Lescale", {5.0f}),
                                FloatParameter("float", "temperaturecutoff", {1.0f}),
                                FloatParameter("float", "temperaturescale", {100.0f}),
                            }),
                            .worldFromMedium = math::Compose({
                                math::Scale(1.0f, 1.0f, 1.6f),
                                math::Rotate(90.0f, math::Vector3(1.0f, 0.0f, 0.0f)),
                            }),
                        },
                    },
                .lights =
                    {
                        SceneLight{
                            .type           = "infinite",
                            .parameters     = Parameters({
                                StringParameter("filename", {std::string_view{skyTexture}}),
                                FloatParameter("float", "scale", {2.0f}),
                            }),
                            .worldFromLight = math::Rotate(10.0f, math::Vector3(1.0f, 0.0f, 0.0f)),
                        },
                    },
                .shapes =
                    {
                        SceneShape{
                            .type            = "sphere",
                            .parameters      = Parameters({FloatParameter("float", "radius", {80.0f})}),
                            .worldFromObject = math::Translate(math::Vector3(0.0f, 40.0f, 0.0f)),
                            .material        = "interface",
                            .insideMedium    = "kaboom",
                        },
                        SceneShape{
                            .type       = "disk",
                            .parameters = Parameters({FloatParameter("float", "radius", {1000.0f})}),
                            .worldFromObject =
                                math::Compose(
                                    {
                                        math::Translate(math::Vector3(0.0f, -50.0f, 0.0f)),
                                        math::Translate(math::Vector3(0.0f, 0.0f, -4.0f)),
                                    }),
                            .material = "ground",
                        },
                    },
            };
            return scene;
        }
    } // namespace

    EditableScene BuildScene(std::string_view name) {
        if (name == "default") return EditableScene{BookScene()};
        if (name == "explosion") return EditableScene{ExplosionScene()};
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
            if (light.type == "infinite" || light.type == "portal") ++infiniteLightCount;

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
            .camera_fov_degrees      = scene.renderSettings.camera.fovDegrees,
        };
    }

    EditableScene BuildScene(std::string_view name) {
        return builtin::BuildScene(name);
    }
} // namespace spectra::scene
}
