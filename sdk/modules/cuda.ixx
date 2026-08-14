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

    struct RawImageView {
        RawView pixels{};
        UInt3 extent{};
    };

    struct RawVolumeView {
        void* state{};
        UInt3 resolution{};
        std::uint32_t* region_minimum{};
        std::uint32_t* region_maximum{};
    };

    struct MetricValue {
        double floating[3]{};
        std::int64_t integer{};
    };

    [[nodiscard]] RawMeshSetupView setup_mesh_internal(void* state, std::string_view id, std::uint32_t vertex_capacity, std::uint32_t triangle_capacity);
    void setup_collection_internal(void* state, std::string_view id, OutputKind kind, std::uint32_t capacity);
    void setup_volume_internal(void* state, std::string_view id, UInt3 resolution);
    void setup_image_internal(void* state, std::string_view id, UInt3 extent);
    void register_output_internal(void* state, std::string_view id, OutputKind kind, MeshAttribute attributes, std::span<const std::string_view> channel_ids, std::span<const VolumeChannelKind> channel_kinds);
    void configure_metrics_internal(void* state, std::span<const std::string_view> ids);
    [[nodiscard]] RawMeshView frame_mesh_internal(void* state, std::string_view id);
    [[nodiscard]] RawView frame_collection_internal(void* state, std::string_view id, std::uint32_t active_count);
    [[nodiscard]] RawVolumeView frame_volume_internal(void* state, std::string_view id);
    [[nodiscard]] RawImageView frame_image_internal(void* state, std::string_view id);
    [[nodiscard]] RawView volume_channel_internal(void* state, std::string_view id);
    void upload_metric_internal(void* state, std::string_view id, const MetricValue& value);

    export struct MeshSetup {
        std::span<std::uint32_t> triangles{};
        std::span<Float2> texture_coordinates{};
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

    export struct Volume {
        void* state{};
        UInt3 resolution{};
        std::span<std::uint32_t, 3> region_minimum;
        std::span<std::uint32_t, 3> region_maximum;

        template <FixedString Id, typename Type>
        [[nodiscard]] std::span<Type> channel() const {
            const RawView view = volume_channel_internal(state, Id.view());
            return {static_cast<Type*>(view.data), view.count};
        }
    };

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
        void spheres(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Spheres, capacity);
        }

        template <FixedString Id>
        void instances(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Instances, capacity);
        }

        template <FixedString Id>
        void points(const std::uint32_t capacity) {
            setup_collection_internal(state, Id.view(), OutputKind::Points, capacity);
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

        void complete();

        template <typename... Definitions>
        void declare(const Description<Definitions...>& description) {
            std::vector<std::string_view> metric_ids{};
            std::apply(
                [this, &metric_ids](const auto&... definition) {
                    ([this, &metric_ids](const auto& value) {
                        if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Output) {
                            std::vector<std::string_view> channel_ids{};
                            std::vector<VolumeChannelKind> channel_kinds{};
                            std::apply(
                                [&channel_ids, &channel_kinds](const auto&... channel) {
                                    (channel_ids.emplace_back(std::remove_cvref_t<decltype(channel)>::id.view()), ...);
                                    (channel_kinds.emplace_back(std::remove_cvref_t<decltype(channel)>::kind), ...);
                                },
                                value.channels
                            );
                            register_output_internal(state, std::remove_cvref_t<decltype(value)>::id.view(), std::remove_cvref_t<decltype(value)>::kind, value.mesh_options.attributes, channel_ids, channel_kinds);
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
        [[nodiscard]] std::span<Point> points(const std::uint32_t count) const {
            const RawView view = frame_collection_internal(state, Id.view(), count);
            return {static_cast<Point*>(view.data), count};
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
            return {view.state, view.resolution, std::span<std::uint32_t, 3>{view.region_minimum, 3u}, std::span<std::uint32_t, 3>{view.region_maximum, 3u}};
        }

        template <FixedString Id>
        [[nodiscard]] Image image() const {
            const RawImageView view = frame_image_internal(state, Id.view());
            return {{static_cast<Float4*>(view.pixels.data), view.pixels.count}, view.extent};
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
