#if defined(_WIN32)
#define SPECTRA_DYNAMIC_SCENE_EXPORT __declspec(dllexport)
#else
#define SPECTRA_DYNAMIC_SCENE_EXPORT __attribute__((visibility("default")))
#endif

import std;
import xayah.projects.pyro;

struct SpectraDynamicSceneInstance;

enum SpectraDynamicSceneResult {
    SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0,
    SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1,
};

struct SpectraDynamicSceneString {
    const char* data;
    std::uint64_t size;
};

struct SpectraDynamicSceneTransform {
    float position[3];
    float rotation[4];
    float scale[3];
};

struct SpectraDynamicSceneMaterial {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneString model;
    SpectraDynamicSceneString alpha_mode;
    float base_color[4];
    float emission_color[3];
    float emission_strength;
    float roughness;
    float metallic;
    float alpha_cutoff;
    float volume_density_scale;
    float volume_temperature_scale;
};

struct SpectraDynamicSceneMaterialSpan {
    const SpectraDynamicSceneMaterial* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneLight {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneString kind;
    SpectraDynamicSceneTransform transform;
    float color[3];
    float intensity;
    float cone_angle_degrees;
};

struct SpectraDynamicSceneLightSpan {
    const SpectraDynamicSceneLight* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneCamera {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneTransform transform;
    float target[3];
    float up[3];
    float vertical_fov_degrees;
    float near_plane;
    float far_plane;
};

struct SpectraDynamicSceneMeshVertex {
    float position[3];
    float normal[3];
};

struct SpectraDynamicSceneMeshVertexSpan {
    const SpectraDynamicSceneMeshVertex* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneUInt32Span {
    const std::uint32_t* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneMesh {
    SpectraDynamicSceneString name;
    SpectraDynamicSceneMeshVertexSpan vertices;
    SpectraDynamicSceneUInt32Span indices;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicSceneMeshSpan {
    const SpectraDynamicSceneMesh* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneSphere {
    SpectraDynamicSceneString name;
    float radius;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicSceneSphereSpan {
    const SpectraDynamicSceneSphere* data;
    std::uint64_t count;
};

struct SpectraDynamicScenePoint {
    float position[3];
    float normal[3];
    float color[4];
    float radius;
};

struct SpectraDynamicScenePointSpan {
    const SpectraDynamicScenePoint* data;
    std::uint64_t count;
};

struct SpectraDynamicScenePointCloud {
    SpectraDynamicSceneString name;
    SpectraDynamicScenePointSpan points;
    SpectraDynamicSceneString material_name;
    SpectraDynamicSceneTransform transform;
};

struct SpectraDynamicScenePointCloudSpan {
    const SpectraDynamicScenePointCloud* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneFloatSpan {
    const float* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneVolumeChannel {
    SpectraDynamicSceneString name;
    std::uint32_t dimensions[3];
    SpectraDynamicSceneFloatSpan values;
};

struct SpectraDynamicSceneVolumeChannelSpan {
    const SpectraDynamicSceneVolumeChannel* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneVolume {
    SpectraDynamicSceneString name;
    std::uint32_t dimensions[3];
    float origin[3];
    float voxel_size[3];
    SpectraDynamicSceneVolumeChannelSpan channels;
    SpectraDynamicSceneString material_name;
};

struct SpectraDynamicSceneVolumeSpan {
    const SpectraDynamicSceneVolume* data;
    std::uint64_t count;
};

struct SpectraDynamicSceneDocumentView {
    std::uint64_t struct_size;
    std::uint32_t has_camera;
    SpectraDynamicSceneCamera camera;
    SpectraDynamicSceneMaterialSpan materials;
    SpectraDynamicSceneLightSpan lights;
    SpectraDynamicSceneMeshSpan meshes;
    SpectraDynamicSceneSphereSpan spheres;
    SpectraDynamicScenePointCloudSpan point_clouds;
    SpectraDynamicSceneVolumeSpan volumes;
};

struct SpectraDynamicSceneFrameInfo {
    double delta_seconds;
    double time_seconds;
    std::uint64_t frame_index;
};

struct SpectraDynamicSceneFrameView {
    std::uint64_t struct_size;
    SpectraDynamicSceneMeshSpan meshes;
    SpectraDynamicSceneSphereSpan spheres;
    SpectraDynamicScenePointCloudSpan point_clouds;
    SpectraDynamicSceneVolumeSpan volumes;
};

typedef SpectraDynamicSceneResult (*SpectraDynamicSceneCreateFn)(SpectraDynamicSceneInstance** instance);
typedef void (*SpectraDynamicSceneDestroyFn)(SpectraDynamicSceneInstance* instance);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneResetFn)(SpectraDynamicSceneInstance* instance);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneStepFn)(SpectraDynamicSceneInstance* instance, float delta_seconds);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneDocumentFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* document);
typedef SpectraDynamicSceneResult (*SpectraDynamicSceneFrameFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneFrameInfo frame, SpectraDynamicSceneFrameView* snapshot);
typedef SpectraDynamicSceneString (*SpectraDynamicSceneLastErrorFn)(SpectraDynamicSceneInstance* instance);

struct SpectraDynamicScenePlugin {
    std::uint32_t abi_version;
    std::uint64_t struct_size;
    SpectraDynamicSceneString id;
    SpectraDynamicSceneString title;
    SpectraDynamicSceneString pbrt_template_path;
    double frames_per_second;
    SpectraDynamicSceneCreateFn create;
    SpectraDynamicSceneDestroyFn destroy;
    SpectraDynamicSceneResetFn reset;
    SpectraDynamicSceneStepFn step;
    SpectraDynamicSceneDocumentFn document;
    SpectraDynamicSceneFrameFn frame;
    SpectraDynamicSceneLastErrorFn last_error;
};

namespace xayah::projects::pyro {
    struct VisualTransform {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    };

    struct VisualMaterial {
        std::string_view name{};
        std::string_view model{"lit_surface"};
        std::string_view alpha_mode{"opaque"};
        std::array<float, 4> base_color{0.8f, 0.8f, 0.8f, 1.0f};
        std::array<float, 3> emission_color{0.0f, 0.0f, 0.0f};
        float emission_strength{0.0f};
        float roughness{0.5f};
        float metallic{0.0f};
        float alpha_cutoff{0.5f};
        float volume_density_scale{0.08f};
        float volume_temperature_scale{0.035f};
    };

    struct VisualLight {
        std::string_view name{};
        std::string_view kind{};
        VisualTransform transform{};
        std::array<float, 3> color{};
        float intensity{};
        float cone_angle_degrees{};
    };

    struct VisualCamera {
        std::string_view name{};
        VisualTransform transform{};
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float vertical_fov_degrees{45.0f};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    struct VisualVolumeChannel {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::span<const float> values{};
    };

    struct VisualVolume {
        std::string_view name{};
        std::array<std::uint32_t, 3> dimensions{0u, 0u, 0u};
        std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
        std::array<float, 3> voxel_size{1.0f, 1.0f, 1.0f};
        std::vector<VisualVolumeChannel> channels{};
        std::string_view material_name{};
        bool dynamic{true};
    };

    class Visualization final {
    public:
        Visualization() = default;

        [[nodiscard]] static std::string_view visualization_id() {
            return "project.pyro";
        }

        [[nodiscard]] static std::string_view visualization_title() {
            return "Pyro";
        }

        [[nodiscard]] double frames_per_second() const {
            return 60.0;
        }

        void reset() {
            this->solver.reset();
            this->frame_index = 0u;
            this->rebuild_volume(0);
        }

        void step(const float delta_seconds) {
            this->solver.step(delta_seconds);
            ++this->frame_index;
            this->rebuild_volume(this->to_solver_frame_index(this->frame_index));
        }

        [[nodiscard]] const std::vector<VisualMaterial>& materials() const {
            return this->visual_materials;
        }

        [[nodiscard]] const std::vector<VisualLight>& lights() const {
            return this->visual_lights;
        }

        [[nodiscard]] const VisualCamera& camera() const {
            return this->visual_camera;
        }

        [[nodiscard]] const std::vector<VisualVolume>& volumes() const {
            return this->visual_volumes;
        }

    private:
        Solver solver{};
        Frame current_frame{};
        std::uint64_t frame_index{};
        std::vector<VisualMaterial> visual_materials{
            VisualMaterial{
                .name              = "smoke",
                .model             = "volume",
                .alpha_mode        = "blend",
                .base_color        = std::array<float, 4>{0.58f, 0.62f, 0.68f, 0.72f},
                .emission_color    = std::array<float, 3>{0.95f, 0.48f, 0.18f},
                .emission_strength = 0.3f,
                .roughness         = 0.9f,
            },
        };
        std::vector<VisualLight> visual_lights{
            VisualLight{
                .name      = "environment",
                .kind      = "environment",
                .color     = std::array<float, 3>{0.24f, 0.26f, 0.30f},
                .intensity = 0.7f,
            },
        };
        VisualCamera visual_camera{
            .name                 = "camera.main",
            .transform            = VisualTransform{.position = std::array<float, 3>{2.35f, 1.65f, 2.8f}},
            .target               = std::array<float, 3>{0.6f, 0.86f, 0.6f},
            .vertical_fov_degrees = 39.0f,
            .near_plane           = 0.03f,
            .far_plane            = 80.0f,
        };
        std::vector<VisualVolume> visual_volumes{};

        [[nodiscard]] static int to_solver_frame_index(const std::uint64_t index) {
            if (index > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Pyro visualization frame index exceeds int range");
            return static_cast<int>(index);
        }

        void rebuild_volume(const int solver_frame_index) {
            this->current_frame = this->solver.read_frame(solver_frame_index);
            VisualVolume volume{
                .name          = "pyro.volume",
                .dimensions    = this->current_frame.resolution,
                .origin        = std::array<float, 3>{0.0f, 0.0f, 0.0f},
                .voxel_size    = std::array<float, 3>{this->current_frame.cell_size, this->current_frame.cell_size, this->current_frame.cell_size},
                .material_name = "smoke",
                .dynamic       = true,
            };
            volume.channels.push_back(VisualVolumeChannel{
                .name       = "density",
                .dimensions = this->current_frame.resolution,
                .values     = std::span<const float>{this->current_frame.density.data(), this->current_frame.density.size()},
            });
            volume.channels.push_back(VisualVolumeChannel{
                .name       = "temperature",
                .dimensions = this->current_frame.resolution,
                .values     = std::span<const float>{this->current_frame.temperature.data(), this->current_frame.temperature.size()},
            });
            this->visual_volumes.clear();
            this->visual_volumes.push_back(std::move(volume));
        }
    };
} // namespace xayah::projects::pyro

namespace {
    [[nodiscard]] SpectraDynamicSceneString make_string(const std::string_view value) noexcept {
        return SpectraDynamicSceneString{.data = value.data(), .size = static_cast<std::uint64_t>(value.size())};
    }

    template <typename Array>
    void copy3(float (&output)[3], const Array& input) noexcept {
        output[0] = static_cast<float>(input[0]);
        output[1] = static_cast<float>(input[1]);
        output[2] = static_cast<float>(input[2]);
    }

    template <typename Array>
    void copy4(float (&output)[4], const Array& input) noexcept {
        output[0] = static_cast<float>(input[0]);
        output[1] = static_cast<float>(input[1]);
        output[2] = static_cast<float>(input[2]);
        output[3] = static_cast<float>(input[3]);
    }

    [[nodiscard]] SpectraDynamicSceneTransform make_transform(const xayah::projects::pyro::VisualTransform& transform) noexcept {
        SpectraDynamicSceneTransform result{};
        copy3(result.position, transform.position);
        copy4(result.rotation, transform.rotation);
        copy3(result.scale, transform.scale);
        return result;
    }

    [[nodiscard]] SpectraDynamicSceneMaterial make_material(const xayah::projects::pyro::VisualMaterial& material) noexcept {
        SpectraDynamicSceneMaterial result{};
        result.name = make_string(material.name);
        result.model = make_string(material.model);
        result.alpha_mode = make_string(material.alpha_mode);
        copy4(result.base_color, material.base_color);
        copy3(result.emission_color, material.emission_color);
        result.emission_strength = material.emission_strength;
        result.roughness = material.roughness;
        result.metallic = material.metallic;
        result.alpha_cutoff = material.alpha_cutoff;
        result.volume_density_scale = material.volume_density_scale;
        result.volume_temperature_scale = material.volume_temperature_scale;
        return result;
    }

    [[nodiscard]] SpectraDynamicSceneLight make_light(const xayah::projects::pyro::VisualLight& light) noexcept {
        SpectraDynamicSceneLight result{};
        result.name = make_string(light.name);
        result.kind = make_string(light.kind);
        result.transform = make_transform(light.transform);
        copy3(result.color, light.color);
        result.intensity = light.intensity;
        result.cone_angle_degrees = light.cone_angle_degrees;
        return result;
    }

    [[nodiscard]] SpectraDynamicSceneCamera make_camera(const xayah::projects::pyro::VisualCamera& camera) noexcept {
        SpectraDynamicSceneCamera result{};
        result.name = make_string(camera.name);
        result.transform = make_transform(camera.transform);
        copy3(result.target, camera.target);
        copy3(result.up, camera.up);
        result.vertical_fov_degrees = camera.vertical_fov_degrees;
        result.near_plane = camera.near_plane;
        result.far_plane = camera.far_plane;
        return result;
    }

    struct VolumeChannelStorage {
        std::vector<float> values{};
        SpectraDynamicSceneVolumeChannel view{};
    };

    struct VolumeStorage {
        std::vector<VolumeChannelStorage> channels{};
        std::vector<SpectraDynamicSceneVolumeChannel> channel_views{};
        SpectraDynamicSceneVolume view{};
    };

    struct SceneViewStorage {
        std::vector<SpectraDynamicSceneMaterial> materials{};
        std::vector<SpectraDynamicSceneLight> lights{};
        SpectraDynamicSceneCamera camera{};
        std::vector<VolumeStorage> volumes{};
        std::vector<SpectraDynamicSceneVolume> volume_views{};
        SpectraDynamicSceneDocumentView document_view{};
        SpectraDynamicSceneFrameView frame_view{};

        void clear() {
            this->materials.clear();
            this->lights.clear();
            this->volumes.clear();
            this->volume_views.clear();
            this->camera = SpectraDynamicSceneCamera{};
            this->document_view = SpectraDynamicSceneDocumentView{};
            this->frame_view = SpectraDynamicSceneFrameView{};
        }

        void append_volumes(const std::vector<xayah::projects::pyro::VisualVolume>& volumes, const bool dynamic) {
            for (const xayah::projects::pyro::VisualVolume& volume : volumes) {
                if (volume.dynamic != dynamic) continue;
                VolumeStorage& storage = this->volumes.emplace_back();
                storage.view.name = make_string(volume.name);
                storage.view.dimensions[0] = volume.dimensions[0];
                storage.view.dimensions[1] = volume.dimensions[1];
                storage.view.dimensions[2] = volume.dimensions[2];
                copy3(storage.view.origin, volume.origin);
                copy3(storage.view.voxel_size, volume.voxel_size);
                storage.view.material_name = make_string(volume.material_name);
                for (const xayah::projects::pyro::VisualVolumeChannel& channel : volume.channels) {
                    VolumeChannelStorage& channel_storage = storage.channels.emplace_back();
                    channel_storage.view.name = make_string(channel.name);
                    channel_storage.view.dimensions[0] = channel.dimensions[0];
                    channel_storage.view.dimensions[1] = channel.dimensions[1];
                    channel_storage.view.dimensions[2] = channel.dimensions[2];
                    channel_storage.values.assign(channel.values.begin(), channel.values.end());
                    channel_storage.view.values = SpectraDynamicSceneFloatSpan{.data = channel_storage.values.data(), .count = static_cast<std::uint64_t>(channel_storage.values.size())};
                }
                storage.channel_views.reserve(storage.channels.size());
                for (const VolumeChannelStorage& channel : storage.channels) storage.channel_views.push_back(channel.view);
                storage.view.channels = SpectraDynamicSceneVolumeChannelSpan{.data = storage.channel_views.data(), .count = static_cast<std::uint64_t>(storage.channel_views.size())};
            }
            this->volume_views.reserve(this->volumes.size());
            for (const VolumeStorage& storage : this->volumes) this->volume_views.push_back(storage.view);
        }

        [[nodiscard]] SpectraDynamicSceneDocumentView build_document(const xayah::projects::pyro::Visualization& source) {
            this->clear();
            for (const xayah::projects::pyro::VisualMaterial& material : source.materials()) this->materials.push_back(make_material(material));
            for (const xayah::projects::pyro::VisualLight& light : source.lights()) this->lights.push_back(make_light(light));
            this->camera = make_camera(source.camera());
            this->append_volumes(source.volumes(), false);
            this->document_view = SpectraDynamicSceneDocumentView{
                .struct_size = sizeof(SpectraDynamicSceneDocumentView),
                .has_camera = 1u,
                .camera = this->camera,
                .materials = SpectraDynamicSceneMaterialSpan{.data = this->materials.data(), .count = static_cast<std::uint64_t>(this->materials.size())},
                .lights = SpectraDynamicSceneLightSpan{.data = this->lights.data(), .count = static_cast<std::uint64_t>(this->lights.size())},
                .volumes = SpectraDynamicSceneVolumeSpan{.data = this->volume_views.data(), .count = static_cast<std::uint64_t>(this->volume_views.size())},
            };
            return this->document_view;
        }

        [[nodiscard]] SpectraDynamicSceneFrameView build_frame(const xayah::projects::pyro::Visualization& source) {
            this->clear();
            this->append_volumes(source.volumes(), true);
            this->frame_view = SpectraDynamicSceneFrameView{
                .struct_size = sizeof(SpectraDynamicSceneFrameView),
                .volumes = SpectraDynamicSceneVolumeSpan{.data = this->volume_views.data(), .count = static_cast<std::uint64_t>(this->volume_views.size())},
            };
            return this->frame_view;
        }
    };

    struct PluginInstance {
        xayah::projects::pyro::Visualization source{};
        SceneViewStorage document_storage{};
        SceneViewStorage frame_storage{};
        std::string error{};
    };

    [[nodiscard]] PluginInstance* typed_instance(SpectraDynamicSceneInstance* instance) noexcept {
        return reinterpret_cast<PluginInstance*>(instance);
    }

    [[nodiscard]] std::string& thread_error() noexcept {
        static thread_local std::string error{};
        return error;
    }

    template <typename Function>
    [[nodiscard]] SpectraDynamicSceneResult guard(PluginInstance* instance, Function&& function) noexcept {
        try {
            if (instance == nullptr) throw std::runtime_error("Dynamic scene plugin instance is null");
            std::forward<Function>(function)();
            instance->error.clear();
            return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
        } catch (const std::exception& exception) {
            if (instance != nullptr) instance->error = exception.what();
            else thread_error() = exception.what();
            return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
        } catch (...) {
            if (instance != nullptr) instance->error = "Unknown dynamic scene plugin error";
            else thread_error() = "Unknown dynamic scene plugin error";
            return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
        }
    }

    [[nodiscard]] SpectraDynamicSceneResult create(SpectraDynamicSceneInstance** instance) noexcept {
        try {
            if (instance == nullptr) throw std::runtime_error("Dynamic scene create output pointer is null");
            *instance = reinterpret_cast<SpectraDynamicSceneInstance*>(new PluginInstance{});
            thread_error().clear();
            return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
        } catch (const std::exception& exception) {
            thread_error() = exception.what();
            return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
        } catch (...) {
            thread_error() = "Unknown dynamic scene plugin creation error";
            return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
        }
    }

    void destroy(SpectraDynamicSceneInstance* instance) noexcept {
        delete typed_instance(instance);
    }

    [[nodiscard]] SpectraDynamicSceneResult reset(SpectraDynamicSceneInstance* instance) noexcept {
        PluginInstance* plugin = typed_instance(instance);
        return guard(plugin, [plugin] { plugin->source.reset(); });
    }

    [[nodiscard]] SpectraDynamicSceneResult step(SpectraDynamicSceneInstance* instance, const float delta_seconds) noexcept {
        PluginInstance* plugin = typed_instance(instance);
        return guard(plugin, [plugin, delta_seconds] { plugin->source.step(delta_seconds); });
    }

    [[nodiscard]] SpectraDynamicSceneResult document(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* output) noexcept {
        PluginInstance* plugin = typed_instance(instance);
        return guard(plugin, [plugin, output] {
            if (output == nullptr) throw std::runtime_error("Dynamic scene document output pointer is null");
            *output = plugin->document_storage.build_document(plugin->source);
        });
    }

    [[nodiscard]] SpectraDynamicSceneResult frame(SpectraDynamicSceneInstance* instance, const SpectraDynamicSceneFrameInfo frame_info, SpectraDynamicSceneFrameView* output) noexcept {
        PluginInstance* plugin = typed_instance(instance);
        return guard(plugin, [plugin, frame_info, output] {
            static_cast<void>(frame_info);
            if (output == nullptr) throw std::runtime_error("Dynamic scene frame output pointer is null");
            *output = plugin->frame_storage.build_frame(plugin->source);
        });
    }

    [[nodiscard]] SpectraDynamicSceneString last_error(SpectraDynamicSceneInstance* instance) noexcept {
        PluginInstance* plugin = typed_instance(instance);
        if (plugin == nullptr) return make_string(thread_error());
        return make_string(plugin->error);
    }

    [[nodiscard]] double frames_per_second() noexcept {
        try {
            xayah::projects::pyro::Visualization source{};
            return source.frames_per_second();
        } catch (...) {
            return 0.0;
        }
    }
} // namespace

extern "C" SPECTRA_DYNAMIC_SCENE_EXPORT const SpectraDynamicScenePlugin* spectra_dynamic_scene_plugin(void) {
    static const std::string id{std::string{xayah::projects::pyro::Visualization::visualization_id()}};
    static const std::string title{std::string{xayah::projects::pyro::Visualization::visualization_title()}};
    static const SpectraDynamicScenePlugin plugin{
        .abi_version = 1u,
        .struct_size = sizeof(SpectraDynamicScenePlugin),
        .id = make_string(id),
        .title = make_string(title),
        .pbrt_template_path = SpectraDynamicSceneString{},
        .frames_per_second = frames_per_second(),
        .create = &create,
        .destroy = &destroy,
        .reset = &reset,
        .step = &step,
        .document = &document,
        .frame = &frame,
        .last_error = &last_error,
    };
    return &plugin;
}
