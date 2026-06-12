export module spectra.rasterizer.visualization;

export import spectra.scene;
import std;

namespace spectra::rasterizer {
    export enum class VisualizationKind {
        Static,
        Dynamic,
    };

    export template <typename Value>
    concept VisualizationString = std::convertible_to<Value, std::string_view>;

    export template <typename Value>
    concept VisualizationVector3 = std::same_as<std::remove_cvref_t<Value>, std::array<float, 3>>;

    export template <typename Value>
    concept VisualizationVector4 = std::same_as<std::remove_cvref_t<Value>, std::array<float, 4>>;

    export template <typename Value>
    concept VisualizationDimensions3 = std::same_as<std::remove_cvref_t<Value>, std::array<std::uint32_t, 3>>;

    export template <typename Value>
    concept VisualizationTransform = requires(const Value& value) {
        { value.position } -> VisualizationVector3;
        { value.rotation } -> VisualizationVector4;
        { value.scale } -> VisualizationVector3;
    };

    export template <typename Value>
    concept VisualizationMaterial = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.model } -> VisualizationString;
        { value.alpha_mode } -> VisualizationString;
        { value.base_color } -> VisualizationVector4;
        { value.emission_color } -> VisualizationVector3;
        { value.emission_strength } -> std::convertible_to<float>;
        { value.roughness } -> std::convertible_to<float>;
        { value.metallic } -> std::convertible_to<float>;
        { value.alpha_cutoff } -> std::convertible_to<float>;
        { value.volume_density_scale } -> std::convertible_to<float>;
        { value.volume_temperature_scale } -> std::convertible_to<float>;
    };

    export template <typename Value>
    concept VisualizationLight = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.kind } -> VisualizationString;
        { value.transform } -> VisualizationTransform;
        { value.color } -> VisualizationVector3;
        { value.intensity } -> std::convertible_to<float>;
        { value.cone_angle_degrees } -> std::convertible_to<float>;
    };

    export template <typename Value>
    concept VisualizationCamera = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.transform } -> VisualizationTransform;
        { value.target } -> VisualizationVector3;
        { value.up } -> VisualizationVector3;
        { value.vertical_fov_degrees } -> std::convertible_to<float>;
        { value.near_plane } -> std::convertible_to<float>;
        { value.far_plane } -> std::convertible_to<float>;
    };

    export template <typename Value>
    concept VisualizationMeshVertex = requires(const Value& value) {
        { value.position } -> VisualizationVector3;
        { value.normal } -> VisualizationVector3;
    };

    export template <typename Range>
    concept VisualizationMaterialRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationMaterial<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Range>
    concept VisualizationLightRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationLight<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Range>
    concept VisualizationIndexRange = std::ranges::input_range<std::remove_cvref_t<Range>> && std::convertible_to<std::ranges::range_reference_t<std::remove_cvref_t<Range>>, std::uint32_t>;

    export template <typename Range>
    concept VisualizationMeshVertexRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationMeshVertex<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Value>
    concept VisualizationMesh = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.material_name } -> VisualizationString;
        { value.transform } -> VisualizationTransform;
        { value.dynamic } -> std::convertible_to<bool>;
        { value.vertices } -> VisualizationMeshVertexRange;
        { value.indices } -> VisualizationIndexRange;
    };

    export template <typename Value>
    concept VisualizationPoint = requires(const Value& value) {
        { value.position } -> VisualizationVector3;
        { value.normal } -> VisualizationVector3;
        { value.color } -> VisualizationVector4;
        { value.radius } -> std::convertible_to<float>;
    };

    export template <typename Range>
    concept VisualizationPointRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationPoint<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Value>
    concept VisualizationPointCloud = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.material_name } -> VisualizationString;
        { value.transform } -> VisualizationTransform;
        { value.dynamic } -> std::convertible_to<bool>;
        { value.points } -> VisualizationPointRange;
    };

    export template <typename Value>
    concept VisualizationVolumeChannel = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.dimensions } -> VisualizationDimensions3;
        { value.values } -> std::ranges::input_range;
        requires std::convertible_to<std::ranges::range_reference_t<std::remove_cvref_t<decltype(value.values)>>, float>;
    };

    export template <typename Range>
    concept VisualizationVolumeChannelRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationVolumeChannel<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Value>
    concept VisualizationVolume = requires(const Value& value) {
        { value.name } -> VisualizationString;
        { value.dimensions } -> VisualizationDimensions3;
        { value.origin } -> VisualizationVector3;
        { value.voxel_size } -> VisualizationVector3;
        { value.material_name } -> VisualizationString;
        { value.dynamic } -> std::convertible_to<bool>;
        { value.channels } -> VisualizationVolumeChannelRange;
    };

    export template <typename Range>
    concept VisualizationMeshRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationMesh<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Range>
    concept VisualizationPointCloudRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationPointCloud<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Range>
    concept VisualizationVolumeRange = std::ranges::input_range<std::remove_cvref_t<Range>> && VisualizationVolume<std::ranges::range_reference_t<std::remove_cvref_t<Range>>>;

    export template <typename Source>
    concept VisualizationMeshProvider = requires(const Source& source) {
        { source.meshes() } -> VisualizationMeshRange;
    };

    export template <typename Source>
    concept VisualizationPointCloudProvider = requires(const Source& source) {
        { source.point_clouds() } -> VisualizationPointCloudRange;
    };

    export template <typename Source>
    concept VisualizationVolumeProvider = requires(const Source& source) {
        { source.volumes() } -> VisualizationVolumeRange;
    };

    export template <typename Source>
    concept VisualizationPrimitiveProvider = VisualizationMeshProvider<Source> || VisualizationPointCloudProvider<Source> || VisualizationVolumeProvider<Source>;

    export template <typename Source>
    concept VisualizationSource = std::default_initializable<Source> && VisualizationPrimitiveProvider<Source> && requires(Source& source, const Source& const_source, const float delta_seconds) {
        { Source::visualization_id() } -> VisualizationString;
        { Source::visualization_title() } -> VisualizationString;
        { const_source.frames_per_second() } -> std::convertible_to<double>;
        { source.reset() } -> std::same_as<void>;
        { source.step(delta_seconds) } -> std::same_as<void>;
        { const_source.materials() } -> VisualizationMaterialRange;
        { const_source.lights() } -> VisualizationLightRange;
        { const_source.camera() } -> VisualizationCamera;
    };

    export class VisualizationSourceInstance {
    public:
        VisualizationSourceInstance() = default;

        VisualizationSourceInstance(const VisualizationSourceInstance& other) = delete;
        VisualizationSourceInstance(VisualizationSourceInstance&& other) = delete;
        VisualizationSourceInstance& operator=(const VisualizationSourceInstance& other) = delete;
        VisualizationSourceInstance& operator=(VisualizationSourceInstance&& other) = delete;
        virtual ~VisualizationSourceInstance() noexcept = default;

        virtual void reset() = 0;
        virtual void step(float delta_seconds) = 0;
        [[nodiscard]] virtual scene::Scene::Document create_scene_document() const = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const = 0;
    };

    export template <VisualizationSource Source>
    class VisualizationSourceModel final : public VisualizationSourceInstance {
    public:
        VisualizationSourceModel() = default;

        void reset() override {
            this->source.reset();
        }

        void step(const float delta_seconds) override {
            this->source.step(delta_seconds);
        }

        [[nodiscard]] scene::Scene::Document create_scene_document() const override {
            return make_document(this->source);
        }

        [[nodiscard]] scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const override {
            scene::Scene::FrameSnapshot snapshot{
                .cursor = scene::Scene::make_frame_cursor(frame),
            };
            if constexpr (VisualizationMeshProvider<Source>) append_meshes(snapshot.meshes, this->source.meshes(), true);
            if constexpr (VisualizationPointCloudProvider<Source>) append_point_clouds(snapshot.point_clouds, this->source.point_clouds(), true);
            if constexpr (VisualizationVolumeProvider<Source>) append_volumes(snapshot.volumes, this->source.volumes(), true);
            return snapshot;
        }

    private:
        Source source{};

        [[nodiscard]] static scene::Vector3 make_vector3(const std::array<float, 3>& value, const std::string_view context) {
            if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2])) throw std::runtime_error(std::format("{} contains a non-finite vector", context));
            return scene::Vector3{value[0], value[1], value[2]};
        }

        [[nodiscard]] static scene::Vector4 make_vector4(const std::array<float, 4>& value, const std::string_view context) {
            if (!std::isfinite(value[0]) || !std::isfinite(value[1]) || !std::isfinite(value[2]) || !std::isfinite(value[3])) throw std::runtime_error(std::format("{} contains a non-finite vector", context));
            return scene::Vector4{value[0], value[1], value[2], value[3]};
        }

        [[nodiscard]] static float finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        template <VisualizationTransform Transform>
        [[nodiscard]] static scene::Transform make_transform(const Transform& transform, const std::string_view context) {
            return scene::Transform{
                .position = make_vector3(transform.position, std::format("{} position", context)),
                .rotation = scene::Quaternion{
                    finite_float(transform.rotation[0], std::format("{} rotation x", context)),
                    finite_float(transform.rotation[1], std::format("{} rotation y", context)),
                    finite_float(transform.rotation[2], std::format("{} rotation z", context)),
                    finite_float(transform.rotation[3], std::format("{} rotation w", context)),
                },
                .scale = make_vector3(transform.scale, std::format("{} scale", context)),
            };
        }

        [[nodiscard]] static scene::Scene::MaterialModel material_model_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "lit_surface") return scene::Scene::MaterialModel::LitSurface;
            if (value == "unlit_surface") return scene::Scene::MaterialModel::UnlitSurface;
            if (value == "emissive_surface") return scene::Scene::MaterialModel::EmissiveSurface;
            if (value == "volume") return scene::Scene::MaterialModel::Volume;
            if (value == "point_sprite") return scene::Scene::MaterialModel::PointSprite;
            throw std::runtime_error(std::format("Visualization material \"{}\" has invalid model \"{}\"", material_name, value));
        }

        [[nodiscard]] static scene::Scene::MaterialAlphaMode material_alpha_mode_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "opaque") return scene::Scene::MaterialAlphaMode::Opaque;
            if (value == "masked") return scene::Scene::MaterialAlphaMode::Masked;
            if (value == "blend") return scene::Scene::MaterialAlphaMode::Blend;
            throw std::runtime_error(std::format("Visualization material \"{}\" has invalid alpha mode \"{}\"", material_name, value));
        }

        [[nodiscard]] static scene::Scene::LightKind light_kind_from_string(const std::string_view value, const std::string_view light_name) {
            if (value == "directional") return scene::Scene::LightKind::Directional;
            if (value == "point") return scene::Scene::LightKind::Point;
            if (value == "spot") return scene::Scene::LightKind::Spot;
            if (value == "area") return scene::Scene::LightKind::Area;
            if (value == "environment") return scene::Scene::LightKind::Environment;
            throw std::runtime_error(std::format("Visualization light \"{}\" has invalid kind \"{}\"", light_name, value));
        }

        template <VisualizationMaterial Material>
        [[nodiscard]] static scene::Scene::Material make_material(const Material& material) {
            const std::string_view name{material.name};
            if (name.empty()) throw std::runtime_error("Visualization material name must not be empty");
            return scene::Scene::Material{
                .name                     = std::string{name},
                .model                    = material_model_from_string(std::string_view{material.model}, name),
                .alpha_mode               = material_alpha_mode_from_string(std::string_view{material.alpha_mode}, name),
                .base_color               = make_vector4(material.base_color, std::format("Visualization material \"{}\" base color", name)),
                .emission_color           = make_vector3(material.emission_color, std::format("Visualization material \"{}\" emission color", name)),
                .emission_strength        = finite_float(static_cast<float>(material.emission_strength), std::format("Visualization material \"{}\" emission strength", name)),
                .roughness                = finite_float(static_cast<float>(material.roughness), std::format("Visualization material \"{}\" roughness", name)),
                .metallic                 = finite_float(static_cast<float>(material.metallic), std::format("Visualization material \"{}\" metallic", name)),
                .alpha_cutoff             = finite_float(static_cast<float>(material.alpha_cutoff), std::format("Visualization material \"{}\" alpha cutoff", name)),
                .volume_density_scale     = finite_float(static_cast<float>(material.volume_density_scale), std::format("Visualization material \"{}\" volume density scale", name)),
                .volume_temperature_scale = finite_float(static_cast<float>(material.volume_temperature_scale), std::format("Visualization material \"{}\" volume temperature scale", name)),
            };
        }

        template <VisualizationLight Light>
        [[nodiscard]] static scene::Scene::Light make_light(const Light& light) {
            const std::string_view name{light.name};
            if (name.empty()) throw std::runtime_error("Visualization light name must not be empty");
            return scene::Scene::Light{
                .name               = std::string{name},
                .kind               = light_kind_from_string(std::string_view{light.kind}, name),
                .transform          = make_transform(light.transform, std::format("Visualization light \"{}\"", name)),
                .color              = make_vector3(light.color, std::format("Visualization light \"{}\" color", name)),
                .intensity          = finite_float(static_cast<float>(light.intensity), std::format("Visualization light \"{}\" intensity", name)),
                .cone_angle_degrees = finite_float(static_cast<float>(light.cone_angle_degrees), std::format("Visualization light \"{}\" cone angle", name)),
            };
        }

        template <VisualizationCamera Camera>
        [[nodiscard]] static scene::Scene::Camera make_camera(const Camera& camera) {
            const std::string_view name{camera.name};
            if (name.empty()) throw std::runtime_error("Visualization camera name must not be empty");
            return scene::Scene::Camera{
                .name                 = std::string{name},
                .transform            = make_transform(camera.transform, std::format("Visualization camera \"{}\"", name)),
                .target               = make_vector3(camera.target, std::format("Visualization camera \"{}\" target", name)),
                .up                   = make_vector3(camera.up, std::format("Visualization camera \"{}\" up", name)),
                .vertical_fov_degrees = finite_float(static_cast<float>(camera.vertical_fov_degrees), std::format("Visualization camera \"{}\" vertical fov", name)),
                .near_plane           = finite_float(static_cast<float>(camera.near_plane), std::format("Visualization camera \"{}\" near plane", name)),
                .far_plane            = finite_float(static_cast<float>(camera.far_plane), std::format("Visualization camera \"{}\" far plane", name)),
            };
        }

        template <VisualizationMesh Mesh>
        [[nodiscard]] static scene::Scene::Mesh make_mesh(const Mesh& mesh) {
            const std::string_view name{mesh.name};
            if (name.empty()) throw std::runtime_error("Visualization mesh name must not be empty");
            if (std::string_view{mesh.material_name}.empty()) throw std::runtime_error(std::format("Visualization mesh \"{}\" material name must not be empty", name));
            scene::Scene::Mesh result{
                .name          = std::string{name},
                .material_name = std::string{std::string_view{mesh.material_name}},
                .transform     = make_transform(mesh.transform, std::format("Visualization mesh \"{}\"", name)),
                .dynamic       = static_cast<bool>(mesh.dynamic),
            };
            if constexpr (std::ranges::sized_range<std::remove_cvref_t<decltype(mesh.vertices)>>) {
                const std::size_t vertex_count = std::ranges::size(mesh.vertices);
                result.positions.reserve(vertex_count);
                result.normals.reserve(vertex_count);
            }
            if constexpr (std::ranges::sized_range<std::remove_cvref_t<decltype(mesh.indices)>>) result.indices.reserve(std::ranges::size(mesh.indices));
            const std::string vertex_position_context = std::format("Visualization mesh \"{}\" vertex position", name);
            const std::string vertex_normal_context = std::format("Visualization mesh \"{}\" vertex normal", name);
            for (const auto& vertex : mesh.vertices) {
                result.positions.push_back(make_vector3(vertex.position, std::string_view{vertex_position_context}));
                result.normals.push_back(make_vector3(vertex.normal, std::string_view{vertex_normal_context}));
            }
            for (const auto& index : mesh.indices) result.indices.push_back(static_cast<std::uint32_t>(index));
            if (result.positions.empty()) throw std::runtime_error(std::format("Visualization mesh \"{}\" must contain vertices", name));
            if (result.indices.empty() || result.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Visualization mesh \"{}\" must contain triangle indices", name));
            for (const std::uint32_t index : result.indices) {
                if (index >= result.positions.size()) throw std::runtime_error(std::format("Visualization mesh \"{}\" contains an out-of-range vertex index", name));
            }
            return result;
        }

        template <VisualizationPointCloud PointCloud>
        [[nodiscard]] static scene::Scene::PointCloud make_point_cloud(const PointCloud& point_cloud) {
            const std::string_view name{point_cloud.name};
            if (name.empty()) throw std::runtime_error("Visualization point cloud name must not be empty");
            if (std::string_view{point_cloud.material_name}.empty()) throw std::runtime_error(std::format("Visualization point cloud \"{}\" material name must not be empty", name));
            scene::Scene::PointCloud result{
                .name          = std::string{name},
                .material_name = std::string{std::string_view{point_cloud.material_name}},
                .transform     = make_transform(point_cloud.transform, std::format("Visualization point cloud \"{}\"", name)),
                .dynamic       = static_cast<bool>(point_cloud.dynamic),
            };
            if constexpr (std::ranges::sized_range<std::remove_cvref_t<decltype(point_cloud.points)>>) {
                const std::size_t point_count = std::ranges::size(point_cloud.points);
                result.positions.reserve(point_count);
                result.normals.reserve(point_count);
                result.colors.reserve(point_count);
                result.radii.reserve(point_count);
            }
            const std::string point_position_context = std::format("Visualization point cloud \"{}\" point position", name);
            const std::string point_normal_context = std::format("Visualization point cloud \"{}\" point normal", name);
            const std::string point_color_context = std::format("Visualization point cloud \"{}\" point color", name);
            const std::string point_radius_context = std::format("Visualization point cloud \"{}\" point radius", name);
            for (const auto& point : point_cloud.points) {
                result.positions.push_back(make_vector3(point.position, std::string_view{point_position_context}));
                result.normals.push_back(make_vector3(point.normal, std::string_view{point_normal_context}));
                result.colors.push_back(make_vector4(point.color, std::string_view{point_color_context}));
                const float radius = finite_float(static_cast<float>(point.radius), std::string_view{point_radius_context});
                if (radius <= 0.0f) throw std::runtime_error(std::format("Visualization point cloud \"{}\" point radius must be positive", name));
                result.radii.push_back(radius);
            }
            return result;
        }

        template <VisualizationVolumeChannel Channel>
        [[nodiscard]] static scene::Scene::VolumeChannel make_volume_channel(const Channel& channel) {
            const std::string_view name{channel.name};
            if (name.empty()) throw std::runtime_error("Visualization volume channel name must not be empty");
            scene::Scene::VolumeChannel result{
                .name       = std::string{name},
                .dimensions = channel.dimensions,
            };
            if constexpr (std::ranges::common_range<std::remove_cvref_t<decltype(channel.values)>>) {
                result.values.assign(std::ranges::begin(channel.values), std::ranges::end(channel.values));
            } else {
                if constexpr (std::ranges::sized_range<std::remove_cvref_t<decltype(channel.values)>>) result.values.reserve(std::ranges::size(channel.values));
                for (const auto& value : channel.values) result.values.push_back(static_cast<float>(value));
            }
            for (std::size_t value_index = 0u; value_index < result.values.size(); ++value_index) {
                if (!std::isfinite(result.values[value_index])) throw std::runtime_error(std::format("Visualization volume channel \"{}\" value #{} must be finite", name, value_index));
            }
            const std::uint64_t expected_count = static_cast<std::uint64_t>(result.dimensions[0]) * static_cast<std::uint64_t>(result.dimensions[1]) * static_cast<std::uint64_t>(result.dimensions[2]);
            if (expected_count != result.values.size()) throw std::runtime_error(std::format("Visualization volume channel \"{}\" value count does not match dimensions", name));
            return result;
        }

        template <VisualizationVolume Volume>
        [[nodiscard]] static scene::Scene::VolumeGrid make_volume(const Volume& volume) {
            const std::string_view name{volume.name};
            if (name.empty()) throw std::runtime_error("Visualization volume name must not be empty");
            if (std::string_view{volume.material_name}.empty()) throw std::runtime_error(std::format("Visualization volume \"{}\" material name must not be empty", name));
            scene::Scene::VolumeGrid result{
                .name          = std::string{name},
                .dimensions    = volume.dimensions,
                .origin        = make_vector3(volume.origin, std::format("Visualization volume \"{}\" origin", name)),
                .voxel_size    = make_vector3(volume.voxel_size, std::format("Visualization volume \"{}\" voxel size", name)),
                .material_name = std::string{std::string_view{volume.material_name}},
                .dynamic       = static_cast<bool>(volume.dynamic),
            };
            if constexpr (std::ranges::sized_range<std::remove_cvref_t<decltype(volume.channels)>>) result.channels.reserve(std::ranges::size(volume.channels));
            for (const auto& channel : volume.channels) {
                scene::Scene::VolumeChannel converted_channel = make_volume_channel(channel);
                if (converted_channel.dimensions != result.dimensions) throw std::runtime_error(std::format("Visualization volume \"{}\" channel \"{}\" dimensions do not match the volume", name, converted_channel.name));
                result.channels.push_back(std::move(converted_channel));
            }
            return result;
        }

        template <VisualizationMeshRange MeshRange>
        static void append_meshes(std::vector<scene::Scene::Mesh>& output, const MeshRange& meshes, const bool dynamic) {
            for (const auto& mesh : meshes) {
                if (static_cast<bool>(mesh.dynamic) != dynamic) continue;
                output.push_back(make_mesh(mesh));
            }
        }

        template <VisualizationPointCloudRange PointCloudRange>
        static void append_point_clouds(std::vector<scene::Scene::PointCloud>& output, const PointCloudRange& point_clouds, const bool dynamic) {
            for (const auto& point_cloud : point_clouds) {
                if (static_cast<bool>(point_cloud.dynamic) != dynamic) continue;
                output.push_back(make_point_cloud(point_cloud));
            }
        }

        template <VisualizationVolumeRange VolumeRange>
        static void append_volumes(std::vector<scene::Scene::VolumeGrid>& output, const VolumeRange& volumes, const bool dynamic) {
            for (const auto& volume : volumes) {
                if (static_cast<bool>(volume.dynamic) != dynamic) continue;
                output.push_back(make_volume(volume));
            }
        }

        [[nodiscard]] static scene::Scene::Document make_document(const Source& source) {
            const double frames_per_second = static_cast<double>(source.frames_per_second());
            if (!std::isfinite(frames_per_second) || frames_per_second <= 0.0) throw std::runtime_error("Visualization frame rate must be finite and positive");
            const std::string_view id{Source::visualization_id()};
            const std::string_view title{Source::visualization_title()};
            if (id.empty()) throw std::runtime_error("Visualization id must not be empty");
            if (title.empty()) throw std::runtime_error("Visualization title must not be empty");
            scene::Scene::Document document{
                .revision          = scene::Scene::Revision{1},
                .name              = std::string{id},
                .title             = std::string{title},
                .source            = std::format("visualization://{}", id),
                .frames_per_second = frames_per_second,
                .timeline_enabled  = true,
                .camera            = make_camera(source.camera()),
            };
            for (const auto& material : source.materials()) document.materials.push_back(make_material(material));
            for (const auto& light : source.lights()) document.lights.push_back(make_light(light));
            if constexpr (VisualizationMeshProvider<Source>) append_meshes(document.meshes, source.meshes(), false);
            if constexpr (VisualizationPointCloudProvider<Source>) append_point_clouds(document.point_clouds, source.point_clouds(), false);
            if constexpr (VisualizationVolumeProvider<Source>) append_volumes(document.volumes, source.volumes(), false);
            return document;
        }
    };

    export struct VisualizationEntry {
        VisualizationEntry(const VisualizationEntry& other) = delete;
        VisualizationEntry(VisualizationEntry&& other) noexcept = default;
        VisualizationEntry& operator=(const VisualizationEntry& other) = delete;
        VisualizationEntry& operator=(VisualizationEntry&& other) noexcept = default;
        ~VisualizationEntry() noexcept = default;

        std::string id{};
        std::string title{};
        VisualizationKind kind{VisualizationKind::Static};

    private:
        VisualizationEntry(std::string id, std::string title, VisualizationKind kind, std::move_only_function<scene::Scene::Document()> create_static_document, std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()> create_dynamic_source);

        std::move_only_function<scene::Scene::Document()> create_static_document{};
        std::move_only_function<std::unique_ptr<VisualizationSourceInstance>()> create_dynamic_source{};

        friend class VisualizationRegistry;
        friend class VisualizationController;
    };

    export class VisualizationRegistry final {
    public:
        VisualizationRegistry() = default;

        VisualizationRegistry(const VisualizationRegistry& other) = delete;
        VisualizationRegistry(VisualizationRegistry&& other) noexcept = default;
        VisualizationRegistry& operator=(const VisualizationRegistry& other) = delete;
        VisualizationRegistry& operator=(VisualizationRegistry&& other) noexcept = default;
        ~VisualizationRegistry() noexcept = default;

        void register_static_visualization(std::string id, std::string title, std::move_only_function<scene::Scene::Document()> create_document);

        template <VisualizationSource Source>
        void register_source() {
            const std::string id{Source::visualization_id()};
            this->ensure_unique_visualization_id(id);
            this->entries.push_back(VisualizationEntry{id, std::string{Source::visualization_title()}, VisualizationKind::Dynamic, std::move_only_function<scene::Scene::Document()>{}, [] { return std::make_unique<VisualizationSourceModel<Source>>(); }});
        }

        [[nodiscard]] std::unique_ptr<VisualizationSourceInstance> create_dynamic_source(std::size_t index);
        [[nodiscard]] scene::Scene::Document create_static_document(std::size_t index);
        [[nodiscard]] const VisualizationEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;

    private:
        void ensure_unique_visualization_id(const std::string& id) const;

        std::vector<VisualizationEntry> entries{};
    };

    export class VisualizationController final {
    public:
        explicit VisualizationController(VisualizationRegistry registry);

        VisualizationController(const VisualizationController& other) = delete;
        VisualizationController(VisualizationController&& other) = delete;
        VisualizationController& operator=(const VisualizationController& other) = delete;
        VisualizationController& operator=(VisualizationController&& other) = delete;
        ~VisualizationController() noexcept = default;

        [[nodiscard]] std::shared_ptr<scene::Scene> active_workspace();
        [[nodiscard]] const VisualizationEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] bool pending_switch() const;
        void request_activate(std::size_t index);
        [[nodiscard]] bool apply_pending_visualization();
        void update_active_visualization(double delta_seconds);

    private:
        struct VisualizationSlot {
            std::unique_ptr<VisualizationSourceInstance> source{};
            std::shared_ptr<scene::Scene> workspace{};
            double frame_accumulator_seconds{};
            double stream_time_seconds{};
            std::uint64_t stream_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        [[nodiscard]] VisualizationSlot& ensure_slot(std::size_t index);
        [[nodiscard]] scene::Scene::Document create_dynamic_slot(std::size_t index, VisualizationSlot* slot);
        void reset_dynamic_visualization(VisualizationSlot& slot, scene::Scene::Timeline timeline);

        VisualizationRegistry registry{};
        std::vector<VisualizationSlot> slots{};
        std::size_t current_active_index{};
        std::optional<std::size_t> pending_active_index{};
    };
} // namespace spectra::rasterizer
