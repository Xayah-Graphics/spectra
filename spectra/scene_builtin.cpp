#include <format>
#include <initializer_list>
#include <memory>
#include <optional>
#include <spectra/scene.h>
#include <spectra/scene_builtin.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef SPECTRA_PROJECT_SCENE_ROOT
#error "SPECTRA_PROJECT_SCENE_ROOT must point to the project-local scene directory."
#endif

namespace spectra::scene {
    namespace {
        struct BuiltinSceneDefinition {
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

            [[nodiscard]] SceneInfo Info() const {
                std::size_t definitionShapeCount     = 0;
                std::size_t definitionAreaLightCount = 0;
                for (const SceneObjectDefinition& definition : this->objectDefinitions) {
                    definitionShapeCount += definition.shapes.size();
                    for (const SceneShape& shape : definition.shapes)
                        if (shape.areaLight.has_value()) ++definitionAreaLightCount;
                }

                std::size_t areaLightCount = definitionAreaLightCount;
                for (const SceneShape& shape : this->shapes)
                    if (shape.areaLight.has_value()) ++areaLightCount;

                std::size_t infiniteLightCount = 0;
                for (const SceneLight& light : this->lights)
                    if (light.type == "infinite" || light.type == "portal") ++infiniteLightCount;

                return SceneInfo{
                    .name                    = this->name,
                    .title                   = this->title,
                    .camera                  = this->renderSettings.camera.type,
                    .sampler                 = this->renderSettings.sampler.type,
                    .integrator              = this->renderSettings.integrator.type,
                    .accelerator             = this->renderSettings.accelerator.type,
                    .shape_count             = this->shapes.size() + definitionShapeCount,
                    .material_count          = this->materials.size(),
                    .texture_count           = this->textures.size(),
                    .medium_count            = this->media.size(),
                    .light_count             = this->lights.size(),
                    .area_light_count        = areaLightCount,
                    .infinite_light_count    = infiniteLightCount,
                    .object_definition_count = this->objectDefinitions.size(),
                    .object_instance_count   = this->objectInstances.size(),
                    .camera_fov_degrees      = this->renderSettings.camera.fovDegrees,
                };
            }

            [[nodiscard]] std::unique_ptr<Scene> Build(std::optional<Point2i> filmResolutionOverride) const {
                std::unique_ptr<Scene> scene = std::make_unique<Scene>(this->name, this->title, this->source, filmResolutionOverride);
                scene->SetRenderSettings(this->renderSettings);
                for (const SceneMaterial& material : this->materials) scene->AddMaterial(material);
                for (const SceneTexture& texture : this->textures) scene->AddTexture(texture);
                for (const SceneMedium& medium : this->media) scene->AddMedium(medium);
                for (const SceneLight& light : this->lights) scene->AddLight(light);
                for (const SceneShape& shape : this->shapes) scene->AddShape(shape);
                for (const SceneObjectDefinition& definition : this->objectDefinitions) scene->AddObjectDefinition(definition);
                for (const SceneObjectInstance& instance : this->objectInstances) scene->AddObjectInstance(instance);
                return scene;
            }
        };

        [[nodiscard]] std::string SceneResource(std::string_view relativePath) {
            return std::format("{}/{}", SPECTRA_PROJECT_SCENE_ROOT, relativePath);
        }

        [[nodiscard]] SceneParameters Parameters(std::initializer_list<SceneParameter> parameters) {
            return SceneParameters{
                .values = std::vector<SceneParameter>(parameters),
            };
        }

        [[nodiscard]] SceneParameter FloatParameter(std::string_view type, std::string_view name, std::initializer_list<Float> values) {
            return SceneParameter{
                .type   = std::string{type},
                .name   = std::string{name},
                .floats = std::vector<Float>(values),
            };
        }

        [[nodiscard]] SceneParameter IntegerParameter(std::string_view name, std::initializer_list<int> values) {
            return SceneParameter{
                .type     = "integer",
                .name     = std::string{name},
                .integers = std::vector<int>(values),
            };
        }

        [[nodiscard]] SceneParameter StringParameter(std::string_view type, std::string_view name, std::initializer_list<std::string_view> values) {
            SceneParameter parameter{
                .type = std::string{type},
                .name = std::string{name},
            };
            parameter.strings.reserve(values.size());
            for (std::string_view value : values) parameter.strings.emplace_back(value);
            return parameter;
        }

        [[nodiscard]] SceneParameter StringParameter(std::string_view name, std::initializer_list<std::string_view> values) {
            return StringParameter("string", name, values);
        }

        [[nodiscard]] Transform Compose(std::initializer_list<Transform> transforms) {
            Transform result{};
            for (const Transform& transform : transforms) result = result * transform;
            return result;
        }

        [[nodiscard]] BuiltinSceneDefinition BookScene() {
            const std::string mesh00001     = SceneResource("pbrt-book/geometry/mesh_00001.ply");
            const std::string mesh00002     = SceneResource("pbrt-book/geometry/mesh_00002.ply");
            const std::string mesh00003     = SceneResource("pbrt-book/geometry/mesh_00003.ply");
            const std::string bookCover     = SceneResource("pbrt-book/texture/book_pbrt.png");
            const std::string bookPages     = SceneResource("pbrt-book/texture/book_pages.png");
            const std::string unevenBump    = SceneResource("pbrt-book/texture/uneven_bump.png");
            const Transform cameraFromWorld = Compose({
                Scale(-1.0f, 1.0f, 1.0f),
                LookAt(Point3f(0.0f, 2.1088f, 13.574f), Point3f(0.0f, 2.1088f, 12.574f), Vector3f(0.0f, 1.0f, 0.0f)),
            });

            BuiltinSceneDefinition scene{
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
                                .worldFromCamera = Inverse(cameraFromWorld),
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
                            .worldFromObject = Translate(Vector3f(34.92f, 55.92f, -15.351f)),
                            .material        = "default_diffuse",
                            .areaLight       = SceneAreaLight{.type = "diffuse", .parameters = Parameters({FloatParameter("rgb", "L", {41.5594f, 43.3127f, 45.066f})})},
                        },
                        SceneShape{
                            .type            = "sphere",
                            .parameters      = Parameters({FloatParameter("float", "radius", {7.5f})}),
                            .worldFromObject = Translate(Vector3f(-32.892f, 55.92f, 36.293f)),
                            .material        = "default_diffuse",
                            .areaLight       = SceneAreaLight{.type = "diffuse", .parameters = Parameters({FloatParameter("rgb", "L", {65.066f, 63.3127f, 61.5594f})})},
                        },
                        SceneShape{
                            .type            = "plymesh",
                            .parameters      = Parameters({StringParameter("filename", {std::string_view{mesh00001}})}),
                            .worldFromObject = Scale(0.213f, 0.213f, 0.213f),
                            .material        = "gray_diffuse",
                        },
                        SceneShape{
                            .type            = "plymesh",
                            .parameters      = Parameters({StringParameter("filename", {std::string_view{mesh00002}})}),
                            .worldFromObject = Compose({
                                Translate(Vector3f(0.0f, 2.2f, 0.0f)),
                                Rotate(77.3425f, Vector3f(0.403388f, -0.754838f, -0.517202f)),
                                Scale(0.5f, 0.5f, 0.5f),
                            }),
                            .material        = "book_pages",
                        },
                        SceneShape{
                            .type       = "plymesh",
                            .parameters = Parameters({StringParameter("filename", {std::string_view{mesh00003}})}),
                            .worldFromObject =
                                Compose(
                                    {
                                        Translate(Vector3f(0.0f, 2.2f, 0.0f)),
                                        Rotate(77.3425f, Vector3f(0.403388f, -0.754838f, -0.517202f)),
                                        Scale(0.5f, 0.5f, 0.5f),
                                    }),
                            .material = "book_cover",
                        },
                    },
            };
            return scene;
        }

        [[nodiscard]] BuiltinSceneDefinition ExplosionScene() {
            const std::string skyTexture    = SceneResource("explosion/textures/sky.exr");
            const std::string fireVolume    = SceneResource("explosion/fire.nvdb");
            const Transform cameraFromWorld = LookAt(Point3f(0.0f, 120.0f, 20.0f), Point3f(-0.5f, 0.0f, 30.0f), Vector3f(0.0f, 0.0f, 1.0f));

            BuiltinSceneDefinition scene{
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
                                .worldFromCamera = Inverse(cameraFromWorld),
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
                            .worldFromMedium = Compose({
                                Scale(1.0f, 1.0f, 1.6f),
                                Rotate(90.0f, Vector3f(1.0f, 0.0f, 0.0f)),
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
                            .worldFromLight = Rotate(10.0f, Vector3f(1.0f, 0.0f, 0.0f)),
                        },
                    },
                .shapes =
                    {
                        SceneShape{
                            .type            = "sphere",
                            .parameters      = Parameters({FloatParameter("float", "radius", {80.0f})}),
                            .worldFromObject = Translate(Vector3f(0.0f, 40.0f, 0.0f)),
                            .material        = "interface",
                            .insideMedium    = "kaboom",
                        },
                        SceneShape{
                            .type            = "disk",
                            .parameters      = Parameters({FloatParameter("float", "radius", {1000.0f})}),
                            .worldFromObject = Compose({
                                Translate(Vector3f(0.0f, -50.0f, 0.0f)),
                                Translate(Vector3f(0.0f, 0.0f, -4.0f)),
                            }),
                            .material        = "ground",
                        },
                    },
            };
            return scene;
        }

        [[nodiscard]] BuiltinSceneDefinition BuiltinScene(std::string_view name) {
            if (name == "default") return BookScene();
            if (name == "explosion") return ExplosionScene();
            throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", name));
        }
    } // namespace

    SceneInfo BuiltinSceneInfoFor(std::string_view name) {
        return BuiltinScene(name).Info();
    }

    std::unique_ptr<Scene> BuildBuiltinScene(std::string_view name, std::optional<Point2i> filmResolutionOverride) {
        return BuiltinScene(name).Build(filmResolutionOverride);
    }
} // namespace spectra::scene
