module;

#include <spectra/sdk/cuda_types.h>

export module spectra.sdk.cuda;

export import spectra.sdk;
import std;

namespace spectra::sdk::cuda {
    struct RawView {
        void* data{};
        std::uint64_t count{};
    };

    struct RawMeshView {
        RawView positions{};
        RawView normals{};
        RawView tangents{};
        RawView colors{};
        RawView scalars{};
        std::uint32_t* active_count{};
        std::uint32_t* triangle_count{};
    };

    struct RawMeshSetupView {
        RawView triangles{};
        RawView texture_coordinates{};
    };

    struct RawCamerasSetupView {
        RawView images{};
        UInt3 extent{};
    };

    struct RawImageView {
        RawView pixels{};
        UInt3 extent{};
    };

    struct RawVolumeView {
        void* state{};
        UInt3 resolution{};
    };

    struct RawParticlesView {
        void* state{};
        RawView positions{};
    };

    struct RawMacFieldView {
        RawView x{};
        RawView y{};
        RawView z{};
        UInt3 x_resolution{};
        UInt3 y_resolution{};
        UInt3 z_resolution{};
    };

    struct RawHashGridRadianceFieldView {
        RawView hash_grid{};
        RawView density_input{};
        RawView density_output{};
        RawView rgb_input{};
        RawView rgb_hidden{};
        RawView rgb_output{};
        RawView occupancy{};
    };

    struct MetricValue {
        double floating[3]{};
        std::int64_t integer{};
    };

    void register_output_internal(void* state, std::string_view id, OutputKind kind, MeshAttribute attributes, std::span<const std::string_view> field_ids, std::span<const FieldKind> field_kinds);
    void configure_metrics_internal(void* state, std::span<const std::string_view> ids);
    [[nodiscard]] RawMeshSetupView setup_mesh_internal(void* state, std::string_view id, std::uint32_t vertex_capacity, std::uint32_t triangle_capacity);
    [[nodiscard]] RawCamerasSetupView setup_cameras_internal(void* state, std::string_view id, std::span<const Camera> cameras, std::uint32_t width, std::uint32_t height);
    void setup_collection_internal(void* state, std::string_view id, OutputKind kind, std::uint32_t capacity);
    void setup_particles_internal(void* state, std::string_view id, std::uint32_t capacity, float radius);
    void setup_volume_internal(void* state, std::string_view id, UInt3 resolution);
    void setup_image_internal(void* state, std::string_view id, UInt3 extent);
    void setup_hash_grid_radiance_field_internal(void* state, std::string_view id);
    [[nodiscard]] RawMeshView frame_mesh_internal(void* state, std::string_view id);
    [[nodiscard]] RawView frame_collection_internal(void* state, std::string_view id, std::uint32_t active_count);
    [[nodiscard]] RawParticlesView frame_particles_internal(void* state, std::string_view id, std::uint32_t active_count);
    [[nodiscard]] RawVolumeView frame_volume_internal(void* state, std::string_view id);
    [[nodiscard]] RawImageView frame_image_internal(void* state, std::string_view id);
    [[nodiscard]] RawHashGridRadianceFieldView frame_hash_grid_radiance_field_internal(void* state, std::string_view id);
    [[nodiscard]] RawView field_internal(void* state, std::string_view id);
    [[nodiscard]] RawMacFieldView volume_mac_field_internal(void* state, std::string_view id);
    void upload_metric_internal(void* state, std::string_view id, const MetricValue& value);

    export struct MeshSetup {
        std::span<std::uint32_t> triangles{};
        std::span<Float2> texture_coordinates{};
    };

    export struct CamerasSetup {
        std::span<Rgba8> images{};
        UInt3 extent{};
    };

    export struct Mesh {
        std::span<Float3> positions{};
        std::span<Float3> normals{};
        std::span<Float3> tangents{};
        std::span<Float4> colors{};
        std::span<float> scalars{};
        std::uint32_t& vertex_count;
        std::uint32_t& triangle_count;
    };

    export struct Image {
        std::span<Float4> pixels{};
        UInt3 extent{};
    };

    export struct HashGridRadianceField {
        std::span<Half4> hash_grid{};
        std::span<Half> density_input{};
        std::span<Half> density_output{};
        std::span<Half> rgb_input{};
        std::span<Half> rgb_hidden{};
        std::span<Half> rgb_output{};
        std::span<std::uint32_t> occupancy{};
    };

    export struct MacField;

    export struct Volume {
        void* state{};
        UInt3 resolution{};

        template <FixedString Id, typename Type>
        [[nodiscard]] std::span<Type> field() const {
            const RawView view = field_internal(state, Id.view());
            return {static_cast<Type*>(view.data), view.count};
        }

        template <FixedString Id>
        [[nodiscard]] MacField field() const;
    };

    export struct Particles {
        void* state{};
        std::span<Float3> positions{};

        template <FixedString Id, typename Type>
        [[nodiscard]] std::span<Type> field() const {
            const RawView view = field_internal(state, Id.view());
            return {static_cast<Type*>(view.data), view.count};
        }
    };

    export struct MacField {
        std::span<float> x{};
        std::span<float> y{};
        std::span<float> z{};
        UInt3 x_resolution{};
        UInt3 y_resolution{};
        UInt3 z_resolution{};
    };

    template <FixedString Id>
    [[nodiscard]] MacField Volume::field() const {
        const RawMacFieldView view = volume_mac_field_internal(state, Id.view());
        return {
            {static_cast<float*>(view.x.data), view.x.count},
            {static_cast<float*>(view.y.data), view.y.count},
            {static_cast<float*>(view.z.data), view.z.count},
            view.x_resolution,
            view.y_resolution,
            view.z_resolution,
        };
    }

    export struct Metric {
        void* state{};
        std::string_view id{};

        template <typename Type>
        void upload(const Type source) const noexcept {
            MetricValue value{};
            if constexpr (std::same_as<Type, bool> || std::integral<Type>) value.integer = static_cast<std::int64_t>(source);
            else if constexpr (std::floating_point<Type>) value.floating[0] = static_cast<double>(source);
            else {
                value.floating[0] = source.x;
                value.floating[1] = source.y;
                value.floating[2] = source.z;
            }
            upload_metric_internal(state, id, value);
        }
    };

    export struct Setup {
        void* state{};

        explicit Setup(const void* sink);
        ~Setup();
        Setup(Setup&& other) noexcept;
        Setup& operator=(Setup&& other) noexcept;
        Setup(const Setup&)            = delete;
        Setup& operator=(const Setup&) = delete;

        template <FixedString Id>
        [[nodiscard]] MeshSetup mesh(const std::uint32_t vertex_capacity, const std::uint32_t triangle_capacity) {
            const RawMeshSetupView view = setup_mesh_internal(state, Id.view(), vertex_capacity, triangle_capacity);
            return {
                {static_cast<std::uint32_t*>(view.triangles.data), view.triangles.count},
                {static_cast<Float2*>(view.texture_coordinates.data), view.texture_coordinates.count},
            };
        }

        template <FixedString Id>
        [[nodiscard]] CamerasSetup cameras(const std::span<const Camera> values, const std::uint32_t width, const std::uint32_t height) {
            const RawCamerasSetupView view = setup_cameras_internal(state, Id.view(), values, width, height);
            return {{static_cast<Rgba8*>(view.images.data), view.images.count}, view.extent};
        }

        template <FixedString Id>
        void spheres(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Spheres, capacity);
        }

        template <FixedString Id>
        void instances(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Instances, capacity);
        }

        template <FixedString Id>
        void particles(const std::uint32_t capacity, const float radius) {
            setup_particles_internal(state, Id.view(), capacity, radius);
        }

        template <FixedString Id>
        void lines(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Lines, capacity);
        }

        template <FixedString Id>
        void vectors(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Vectors, capacity);
        }

        template <FixedString Id>
        void volume(const UInt3 resolution) {
            setup_volume_internal(state, Id.view(), resolution);
        }

        template <FixedString Id>
        void image(const std::uint32_t width, const std::uint32_t height) {
            setup_image_internal(state, Id.view(), {width, height, 1u});
        }

        template <FixedString Id>
        void hash_grid_radiance_field() {
            setup_hash_grid_radiance_field_internal(state, Id.view());
        }

        void complete();

        template <typename... Definitions>
        void declare(const Description<Definitions...>& description) {
            std::vector<std::string_view> metric_ids{};
            std::apply(
                [this, &metric_ids](const auto&... definition) {
                    ([this, &metric_ids](const auto& value) {
                        if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Output) {
                            std::vector<std::string_view> field_ids{};
                            std::vector<FieldKind> field_kinds{};
                            std::apply(
                                [&field_ids, &field_kinds](const auto&... field) {
                                    (field_ids.emplace_back(std::remove_cvref_t<decltype(field)>::id.view()), ...);
                                    (field_kinds.emplace_back(std::remove_cvref_t<decltype(field)>::kind), ...);
                                },
                                value.fields
                            );
                            register_output_internal(state, std::remove_cvref_t<decltype(value)>::id.view(), std::remove_cvref_t<decltype(value)>::kind, value.mesh_options.attributes, field_ids, field_kinds);
                        } else if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Metric)
                            metric_ids.emplace_back(std::remove_cvref_t<decltype(value)>::id.view());
                    }(definition), ...);
                },
                description.definitions
            );
            configure_metrics_internal(state, metric_ids);
        }
    };

    export struct Frame {
        void* state{};

        template <FixedString Id>
        [[nodiscard]] Mesh mesh() const {
            const RawMeshView view = frame_mesh_internal(state, Id.view());
            return {
                {static_cast<Float3*>(view.positions.data), view.positions.count},
                {static_cast<Float3*>(view.normals.data), view.normals.count},
                {static_cast<Float3*>(view.tangents.data), view.tangents.count},
                {static_cast<Float4*>(view.colors.data), view.colors.count},
                {static_cast<float*>(view.scalars.data), view.scalars.count},
                *view.active_count,
                *view.triangle_count,
            };
        }

        template <FixedString Id>
        [[nodiscard]] std::span<Sphere> spheres(const std::uint32_t count) const {
            const RawView view = frame_collection_internal(state, Id.view(), count);
            return {static_cast<Sphere*>(view.data), count};
        }

        template <FixedString Id>
        [[nodiscard]] std::span<Instance> instances(const std::uint32_t count) const {
            const RawView view = frame_collection_internal(state, Id.view(), count);
            return {static_cast<Instance*>(view.data), count};
        }

        template <FixedString Id>
        [[nodiscard]] Particles particles(const std::uint32_t count) const {
            const RawParticlesView view = frame_particles_internal(state, Id.view(), count);
            return {view.state, {static_cast<Float3*>(view.positions.data), count}};
        }

        template <FixedString Id>
        [[nodiscard]] std::span<Line> lines(const std::uint32_t count) const {
            const RawView view = frame_collection_internal(state, Id.view(), count);
            return {static_cast<Line*>(view.data), count};
        }

        template <FixedString Id>
        [[nodiscard]] std::span<Vector> vectors(const std::uint32_t count) const {
            const RawView view = frame_collection_internal(state, Id.view(), count);
            return {static_cast<Vector*>(view.data), count};
        }

        template <FixedString Id>
        [[nodiscard]] Volume volume() const {
            const RawVolumeView view = frame_volume_internal(state, Id.view());
            return {view.state, view.resolution};
        }

        template <FixedString Id>
        [[nodiscard]] Image image() const {
            const RawImageView view = frame_image_internal(state, Id.view());
            return {{static_cast<Float4*>(view.pixels.data), view.pixels.count}, view.extent};
        }

        template <FixedString Id>
        [[nodiscard]] HashGridRadianceField hash_grid_radiance_field() const {
            const RawHashGridRadianceFieldView view = frame_hash_grid_radiance_field_internal(state, Id.view());
            return {
                {static_cast<Half4*>(view.hash_grid.data), view.hash_grid.count},
                {static_cast<Half*>(view.density_input.data), view.density_input.count},
                {static_cast<Half*>(view.density_output.data), view.density_output.count},
                {static_cast<Half*>(view.rgb_input.data), view.rgb_input.count},
                {static_cast<Half*>(view.rgb_hidden.data), view.rgb_hidden.count},
                {static_cast<Half*>(view.rgb_output.data), view.rgb_output.count},
                {static_cast<std::uint32_t*>(view.occupancy.data), view.occupancy.count},
            };
        }

        template <FixedString Id>
        [[nodiscard]] Metric metric() const {
            return {state, Id.view()};
        }

        void commit();
    };

    export struct Output {
        void* state{};

        Output() = default;
        explicit Output(Setup& setup) noexcept;
        ~Output();
        Output(Output&& other) noexcept;
        Output& operator=(Output&& other) noexcept;
        Output(const Output&)            = delete;
        Output& operator=(const Output&) = delete;

        void prepare(void* commit) const noexcept;
        [[nodiscard]] Frame begin(void* stream) const;
        void synchronize() const;
    };
}
