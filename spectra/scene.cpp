#include <format>
#include <initializer_list>
#include <spectra/scene.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef SPECTRA_PROJECT_SCENE_ROOT
#error "SPECTRA_PROJECT_SCENE_ROOT must point to the project-local scene directory."
#endif

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

        [[nodiscard]] SceneParameter FloatParameter(std::string_view type, std::string_view name, std::initializer_list<Float> values) {
            return SceneParameter{
                .type   = std::string{type},
                .name   = std::string{name},
                .values = std::vector<Float>(values),
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

        [[nodiscard]] Transform Compose(std::initializer_list<Transform> transforms) {
            Transform result{};
            for (const Transform& transform : transforms) result = result * transform;
            return result;
        }

        [[nodiscard]] Scene BookScene() {
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

            Scene scene{
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

        [[nodiscard]] Scene ExplosionScene() {
            const std::string skyTexture    = SceneResource("explosion/textures/sky.exr");
            const std::string fireVolume    = SceneResource("explosion/fire.nvdb");
            const Transform cameraFromWorld = LookAt(Point3f(0.0f, 120.0f, 20.0f), Point3f(-0.5f, 0.0f, 30.0f), Vector3f(0.0f, 0.0f, 1.0f));

            Scene scene{
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
    } // namespace

    Scene BuildScene(std::string_view name) {
        if (name == "default") return BookScene();
        if (name == "explosion") return ExplosionScene();
        throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", name));
    }
} // namespace spectra::scene::builtin

namespace spectra::scene {
    SceneInfo DescribeScene(const Scene& scene) {
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

    SceneInfo SceneInfoFor(std::string_view name) {
        return DescribeScene(builtin::BuildScene(name));
    }

    Scene BuildScene(std::string_view name) {
        return builtin::BuildScene(name);
    }
} // namespace spectra::scene
