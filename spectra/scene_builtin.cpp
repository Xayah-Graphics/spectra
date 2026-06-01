#include <array>
#include <format>
#include <initializer_list>
#include <spectra/scene.h>
#include <spectra/scene_builtin.h>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef SPECTRA_PROJECT_SCENE_ROOT
#error "SPECTRA_PROJECT_SCENE_ROOT must point to the project-local scene directory."
#endif

namespace spectra::scene {
    namespace {
        static constexpr std::array<SceneInfo, 2> scenes{{
            {
                .name                    = "default",
                .title                   = "PBRT Book",
                .camera                  = "perspective",
                .sampler                 = "halton",
                .integrator              = "volpath",
                .accelerator             = "bvh",
                .shape_count             = 5,
                .material_count          = 3,
                .texture_count           = 5,
                .medium_count            = 0,
                .light_count             = 0,
                .area_light_count        = 2,
                .infinite_light_count    = 0,
                .object_definition_count = 0,
                .object_instance_count   = 0,
                .camera_fov_degrees      = 26.5f,
            },
            {
                .name                    = "explosion",
                .title                   = "Explosion",
                .camera                  = "perspective",
                .sampler                 = "zsobol",
                .integrator              = "volpath",
                .accelerator             = "bvh",
                .shape_count             = 2,
                .material_count          = 2,
                .texture_count           = 0,
                .medium_count            = 1,
                .light_count             = 1,
                .area_light_count        = 0,
                .infinite_light_count    = 1,
                .object_definition_count = 0,
                .object_instance_count   = 0,
                .camera_fov_degrees      = 37.0f,
            },
        }};

        [[nodiscard]] std::string SceneResource(std::string_view relative_path) {
            return std::format("{}/{}", SPECTRA_PROJECT_SCENE_ROOT, relative_path);
        }

        [[nodiscard]] FileLoc SceneLocation(std::string_view scene_name) {
            if (scene_name == "default") return FileLoc{"spectra://scene/default"};
            if (scene_name == "explosion") return FileLoc{"spectra://scene/explosion"};
            throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", scene_name));
        }

        [[nodiscard]] ParsedParameter* FloatParameter(std::string_view type, std::string_view name, std::initializer_list<Float> values, FileLoc location) {
            ParsedParameter* parameter = new ParsedParameter(location);
            parameter->type.assign(type);
            parameter->name.assign(name);
            for (Float value : values) parameter->AddFloat(value);
            return parameter;
        }

        [[nodiscard]] ParsedParameter* IntegerParameter(std::string_view name, std::initializer_list<int> values, FileLoc location) {
            ParsedParameter* parameter = new ParsedParameter(location);
            parameter->type            = "integer";
            parameter->name.assign(name);
            for (int value : values) parameter->AddInt(value);
            return parameter;
        }

        [[nodiscard]] ParsedParameter* StringParameter(std::string_view type, std::string_view name, std::initializer_list<std::string_view> values, FileLoc location) {
            ParsedParameter* parameter = new ParsedParameter(location);
            parameter->type.assign(type);
            parameter->name.assign(name);
            for (std::string_view value : values) parameter->AddString(value);
            return parameter;
        }

        [[nodiscard]] ParsedParameter* StringParameter(std::string_view name, std::initializer_list<std::string_view> values, FileLoc location) {
            return StringParameter("string", name, values, location);
        }

        [[nodiscard]] ParsedParameterVector ParameterList(std::initializer_list<ParsedParameter*> input) {
            ParsedParameterVector parameters{};
            for (ParsedParameter* parameter : input) parameters.push_back(parameter);
            return parameters;
        }

        void BuildBookScene(SceneBuilder& builder) {
            const FileLoc location        = SceneLocation("default");
            const std::string mesh_00001  = SceneResource("pbrt-book/geometry/mesh_00001.ply");
            const std::string mesh_00002  = SceneResource("pbrt-book/geometry/mesh_00002.ply");
            const std::string mesh_00003  = SceneResource("pbrt-book/geometry/mesh_00003.ply");
            const std::string book_cover  = SceneResource("pbrt-book/texture/book_pbrt.png");
            const std::string book_pages  = SceneResource("pbrt-book/texture/book_pages.png");
            const std::string uneven_bump = SceneResource("pbrt-book/texture/uneven_bump.png");

            builder.Sampler("halton", ParameterList({IntegerParameter("pixelsamples", {2048}, location)}), location);
            builder.Film("rgb",
                ParameterList({
                    StringParameter("filename", {"book.exr"}, location),
                    IntegerParameter("yresolution", {1080}, location),
                    IntegerParameter("xresolution", {1920}, location),
                    StringParameter("sensor", {"canon_eos_100d"}, location),
                    FloatParameter("float", "iso", {150.0f}, location),
                }),
                location);
            builder.Scale(-1.0f, 1.0f, 1.0f, location);
            builder.LookAt(0.0f, 2.1088f, 13.574f, 0.0f, 2.1088f, 12.574f, 0.0f, 1.0f, 0.0f, location);
            builder.Camera("perspective", ParameterList({FloatParameter("float", "fov", {26.5f}, location)}), location);
            builder.WorldBegin(location);

            builder.AttributeBegin(location);
            builder.AreaLightSource("diffuse", ParameterList({FloatParameter("rgb", "L", {41.5594f, 43.3127f, 45.066f}, location)}), location);
            builder.Translate(34.92f, 55.92f, -15.351f, location);
            builder.Shape("sphere", ParameterList({FloatParameter("float", "radius", {7.5f}, location)}), location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.AreaLightSource("diffuse", ParameterList({FloatParameter("rgb", "L", {65.066f, 63.3127f, 61.5594f}, location)}), location);
            builder.Translate(-32.892f, 55.92f, 36.293f, location);
            builder.Shape("sphere", ParameterList({FloatParameter("float", "radius", {7.5f}, location)}), location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.Material("diffuse", ParameterList({FloatParameter("rgb", "reflectance", {0.5f, 0.5f, 0.5f}, location)}), location);
            builder.Scale(0.213f, 0.213f, 0.213f, location);
            builder.AttributeBegin(location);
            builder.Shape("plymesh", ParameterList({StringParameter("filename", {std::string_view{mesh_00001}}, location)}), location);
            builder.AttributeEnd(location);
            builder.AttributeEnd(location);

            builder.Texture("book_cover", "spectrum", "imagemap", ParameterList({StringParameter("filename", {std::string_view{book_cover}}, location)}), location);
            builder.Texture("book_pages", "spectrum", "imagemap", ParameterList({StringParameter("filename", {std::string_view{book_pages}}, location)}), location);
            builder.Texture("uneven_bump_raw", "float", "imagemap",
                ParameterList({
                    StringParameter("filename", {std::string_view{uneven_bump}}, location),
                    FloatParameter("float", "vscale", {1.5f}, location),
                    FloatParameter("float", "uscale", {1.5f}, location),
                }),
                location);
            builder.Texture("uneven_bump_scale", "float", "constant", ParameterList({FloatParameter("float", "value", {0.0002f}, location)}), location);
            builder.Texture("uneven_bump", "float", "scale",
                ParameterList({
                    StringParameter("texture", "scale", {"uneven_bump_scale"}, location),
                    StringParameter("texture", "tex", {"uneven_bump_raw"}, location),
                }),
                location);

            builder.AttributeBegin(location);
            builder.Material("diffuse", ParameterList({StringParameter("texture", "reflectance", {"book_pages"}, location)}), location);
            builder.Translate(0.0f, 2.2f, 0.0f, location);
            builder.Rotate(77.3425f, 0.403388f, -0.754838f, -0.517202f, location);
            builder.Scale(0.5f, 0.5f, 0.5f, location);
            builder.AttributeBegin(location);
            builder.Shape("plymesh", ParameterList({StringParameter("filename", {std::string_view{mesh_00002}}, location)}), location);
            builder.AttributeEnd(location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.Material("coateddiffuse",
                ParameterList({
                    StringParameter("texture", "displacement", {"uneven_bump"}, location),
                    StringParameter("texture", "reflectance", {"book_cover"}, location),
                    FloatParameter("float", "roughness", {0.0003f}, location),
                }),
                location);
            builder.Translate(0.0f, 2.2f, 0.0f, location);
            builder.Rotate(77.3425f, 0.403388f, -0.754838f, -0.517202f, location);
            builder.Scale(0.5f, 0.5f, 0.5f, location);
            builder.AttributeBegin(location);
            builder.Shape("plymesh", ParameterList({StringParameter("filename", {std::string_view{mesh_00003}}, location)}), location);
            builder.AttributeEnd(location);
            builder.AttributeEnd(location);

            builder.Finish();
        }

        void BuildExplosionScene(SceneBuilder& builder) {
            const FileLoc location        = SceneLocation("explosion");
            const std::string sky_texture = SceneResource("explosion/textures/sky.exr");
            const std::string fire_volume = SceneResource("explosion/fire.nvdb");

            builder.LookAt(0.0f, 120.0f, 20.0f, -0.5f, 0.0f, 30.0f, 0.0f, 0.0f, 1.0f, location);
            builder.Camera("perspective", ParameterList({FloatParameter("float", "fov", {37.0f}, location)}), location);
            builder.Film("rgb",
                ParameterList({
                    IntegerParameter("xresolution", {1300}, location),
                    IntegerParameter("yresolution", {1800}, location),
                    StringParameter("sensor", {"nikon_d850"}, location),
                    FloatParameter("float", "whitebalance", {6000.0f}, location),
                    FloatParameter("float", "iso", {100.0f}, location),
                    StringParameter("filename", {"explosion.exr"}, location),
                }),
                location);
            builder.Integrator("volpath", ParameterList({IntegerParameter("maxdepth", {5}, location)}), location);
            builder.WorldBegin(location);

            builder.AttributeBegin(location);
            builder.Rotate(10.0f, 1.0f, 0.0f, 0.0f, location);
            builder.LightSource("infinite",
                ParameterList({
                    StringParameter("filename", {std::string_view{sky_texture}}, location),
                    FloatParameter("float", "scale", {2.0f}, location),
                }),
                location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.Scale(1.0f, 1.0f, 1.6f, location);
            builder.Rotate(90.0f, 1.0f, 0.0f, 0.0f, location);
            builder.MakeNamedMedium("kaboom",
                ParameterList({
                    StringParameter("type", {"nanovdb"}, location),
                    StringParameter("filename", {std::string_view{fire_volume}}, location),
                    FloatParameter("spectrum", "sigma_s", {200.0f, 10.0f, 900.0f, 10.0f}, location),
                    FloatParameter("spectrum", "sigma_a", {200.0f, 10.0f, 900.0f, 10.0f}, location),
                    FloatParameter("float", "Lescale", {5.0f}, location),
                    FloatParameter("float", "temperaturecutoff", {1.0f}, location),
                    FloatParameter("float", "temperaturescale", {100.0f}, location),
                }),
                location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.MediumInterface("kaboom", "", location);
            builder.Material("interface", ParsedParameterVector{}, location);
            builder.Translate(0.0f, 40.0f, 0.0f, location);
            builder.Shape("sphere", ParameterList({FloatParameter("float", "radius", {80.0f}, location)}), location);
            builder.AttributeEnd(location);

            builder.AttributeBegin(location);
            builder.Translate(0.0f, -50.0f, 0.0f, location);
            builder.Material("coateddiffuse",
                ParameterList({
                    FloatParameter("rgb", "reflectance", {0.4f, 0.4f, 0.4f}, location),
                    FloatParameter("float", "roughness", {0.001f}, location),
                }),
                location);
            builder.Translate(0.0f, 0.0f, -4.0f, location);
            builder.Shape("disk", ParameterList({FloatParameter("float", "radius", {1000.0f}, location)}), location);
            builder.AttributeEnd(location);

            builder.Finish();
        }
    } // namespace

    const SceneInfo& BuiltinSceneInfoFor(std::string_view name) {
        for (const SceneInfo& scene : scenes)
            if (scene.name == name) return scene;
        throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", name));
    }

    void BuildBuiltinScene(std::string_view name, SceneBuilder& builder) {
        if (name == "default") {
            BuildBookScene(builder);
            return;
        }
        if (name == "explosion") {
            BuildExplosionScene(builder);
            return;
        }
        throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", name));
    }
} // namespace spectra::scene
