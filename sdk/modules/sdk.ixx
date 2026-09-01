module;

#include <spectra/sdk/cuda_types.h>

export module spectra.sdk;

import std;

export namespace spectra::sdk {
    template <std::size_t Size>
    struct FixedString {
        char value[Size]{};

        consteval FixedString(const char (&source)[Size]) {
            std::ranges::copy(source, value);
        }

        [[nodiscard]] constexpr std::string_view view() const noexcept {
            return {value, Size - 1u};
        }

        friend constexpr bool operator==(const FixedString&, const FixedString&) = default;
    };

    struct PresentationSequence {
        std::uint64_t frame_count{};
        double start_seconds{};
        double frame_seconds{};
    };

    struct PresentationFrame {
        std::uint64_t index{};
        double seconds{};
    };

    struct IndexSelectionInput {
        std::string_view id{};
        std::span<const std::uint32_t> indices{};
    };

    struct MeshInput {
        std::string_view id{};
        std::string_view prim_path{};
        std::span<const Float3> positions{};
        std::span<const std::uint32_t> indices{};
        std::span<const Float2> texture_coordinates{};
        Transform transform{};
        std::span<const IndexSelectionInput> selections{};
    };

    struct SceneInputs {
        std::span<const MeshInput> meshes{};
        double step_seconds{};
    };

    enum class ParameterApplication : std::uint32_t {
        Live,
        Reset,
        Recreate,
    };

    struct ParameterOptions {
        double minimum{};
        double maximum{};
        double step{};
        ParameterApplication application{ParameterApplication::Live};
        std::string_view description{};
        std::string_view section{};
        std::span<const std::string_view> enumerators{};
    };

    enum class DefinitionCategory : std::uint8_t {
        Parameter,
        Output,
        Metric,
    };

    template <FixedString Id, auto Member>
    struct ParameterDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Parameter;
        static constexpr FixedString id              = Id;
        static constexpr auto member                 = Member;

        std::string_view name{};
        std::string_view unit{};
        ParameterOptions options{};
    };

    template <FixedString Id, auto Member>
    [[nodiscard]] consteval auto parameter(const std::string_view name, const std::string_view unit = {}, const ParameterOptions options = {}) {
        return ParameterDefinition<Id, Member>{name, unit, options};
    }

    enum class OutputKind : std::uint32_t {
        Mesh,
        MeshField,
        IndexedPoints,
        IndexedSegments,
        Spheres,
        Volume,
        Instances,
        Particles,
        Lines,
        Vectors,
        Image,
        HashGridRadianceField,
        Cameras,
    };

    enum class MeshAttribute : std::uint32_t {
        Normal            = 1u << 0u,
        Tangent           = 1u << 1u,
        TextureCoordinate = 1u << 2u,
    };

    [[nodiscard]] constexpr MeshAttribute operator|(const MeshAttribute left, const MeshAttribute right) noexcept {
        return static_cast<MeshAttribute>(std::to_underlying(left) | std::to_underlying(right));
    }

    [[nodiscard]] constexpr bool contains(const MeshAttribute attributes, const MeshAttribute attribute) noexcept {
        return (std::to_underlying(attributes) & std::to_underlying(attribute)) != 0u;
    }

    struct MeshOptions {
        MeshAttribute attributes{};
    };

    enum class MeshElementDomain : std::uint32_t {
        Vertex,
        Face,
        Edge,
    };

    enum class VisualizationKind : std::uint32_t {
        None,
        Points,
        Segments,
        Vectors,
        Surface,
        Image,
    };

    struct VisualizationOptions {
        std::string_view name{};
        std::string_view anchor{};
        VisualizationKind kind{VisualizationKind::None};
        MeshElementDomain domain{MeshElementDomain::Vertex};
        bool visible{};
    };

    enum class FieldKind : std::uint32_t {
        Float,
        Float2,
        Float3,
        Float4,
        UInt32,
        MacFloat3,
    };

    enum class VolumeFieldSampling : std::uint32_t {
        Cell,
        Vertex,
    };

    enum class VolumeVectorSpace : std::uint32_t {
        Grid,
        Local,
        World,
    };

    struct MacFloat3 {};

    struct VolumeFieldOptions {
        VolumeFieldSampling sampling{VolumeFieldSampling::Cell};
        VolumeVectorSpace vector_space{VolumeVectorSpace::Local};
    };

    struct DefaultFieldOptions {};

    template <FixedString Id, typename Type, typename Options>
    struct FieldDefinition {
        static constexpr FixedString id = Id;
        static constexpr FieldKind kind = std::same_as<Type, float> ? FieldKind::Float : std::same_as<Type, Float2> ? FieldKind::Float2 : std::same_as<Type, Float3> ? FieldKind::Float3 : std::same_as<Type, Float4> ? FieldKind::Float4 : std::same_as<Type, std::uint32_t> ? FieldKind::UInt32 : FieldKind::MacFloat3;

        std::string_view name{};
        std::string_view unit{};
        Options options{};
    };

    template <FixedString Id, typename Type>
    [[nodiscard]] consteval auto field(const std::string_view name, const std::string_view unit = {}) {
        static_assert(std::same_as<Type, float> || std::same_as<Type, Float2> || std::same_as<Type, Float3> || std::same_as<Type, Float4> || std::same_as<Type, std::uint32_t> || std::same_as<Type, MacFloat3>);
        return FieldDefinition<Id, Type, DefaultFieldOptions>{name, unit};
    }

    template <FixedString Id, typename Type>
    [[nodiscard]] consteval auto volume_field(const std::string_view name, const std::string_view unit, const VolumeFieldOptions options) {
        static_assert(std::same_as<Type, float> || std::same_as<Type, Float3> || std::same_as<Type, std::uint32_t> || std::same_as<Type, MacFloat3>);
        return FieldDefinition<Id, Type, VolumeFieldOptions>{name, unit, options};
    }

    template <FixedString Id, OutputKind Kind>
    struct OutputDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id              = Id;
        static constexpr OutputKind kind             = Kind;
    };

    template <FixedString Id>
    struct MeshDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id              = Id;
        static constexpr OutputKind kind             = OutputKind::Mesh;

        MeshOptions options{};
    };

    template <FixedString Id, typename Type>
    struct MeshFieldDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id              = Id;
        static constexpr OutputKind kind             = OutputKind::MeshField;
        static constexpr FieldKind field_kind        = std::same_as<Type, float> ? FieldKind::Float : std::same_as<Type, Float2> ? FieldKind::Float2 : std::same_as<Type, Float3> ? FieldKind::Float3 : std::same_as<Type, Float4> ? FieldKind::Float4 : FieldKind::UInt32;

        std::string_view name{};
        std::string_view unit{};
        VisualizationOptions visualization{};
    };

    template <FixedString Id, OutputKind Kind>
    struct IndexedDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id              = Id;
        static constexpr OutputKind kind             = Kind;

        VisualizationOptions visualization{};
    };

    template <FixedString Id, OutputKind Kind, typename... Fields>
    struct FieldOutputDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id              = Id;
        static constexpr OutputKind kind             = Kind;

        std::tuple<Fields...> fields{};
    };

    template <FixedString Id>
    [[nodiscard]] consteval auto mesh(const MeshOptions options = {}) {
        return MeshDefinition<Id>{options};
    }

    template <FixedString Id, typename Type>
    [[nodiscard]] consteval auto mesh_field(const std::string_view name, const std::string_view unit, const VisualizationOptions visualization) {
        static_assert(std::same_as<Type, float> || std::same_as<Type, Float2> || std::same_as<Type, Float3> || std::same_as<Type, Float4> || std::same_as<Type, std::uint32_t>);
        return MeshFieldDefinition<Id, Type>{name, unit, visualization};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto indexed_points(const VisualizationOptions visualization) {
        return IndexedDefinition<Id, OutputKind::IndexedPoints>{visualization};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto indexed_segments(const VisualizationOptions visualization) {
        return IndexedDefinition<Id, OutputKind::IndexedSegments>{visualization};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto spheres() {
        return OutputDefinition<Id, OutputKind::Spheres>{};
    }

    template <FixedString Id, typename... Fields>
    [[nodiscard]] consteval auto volume(const Fields... fields) {
        static_assert(sizeof...(Fields) != 0u);
        return FieldOutputDefinition<Id, OutputKind::Volume, Fields...>{{fields...}};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto instances() {
        return OutputDefinition<Id, OutputKind::Instances>{};
    }

    template <FixedString Id, typename... Fields>
    [[nodiscard]] consteval auto particles(const Fields... fields) {
        static_assert(((std::remove_cvref_t<Fields>::kind == FieldKind::Float || std::remove_cvref_t<Fields>::kind == FieldKind::Float3 || std::remove_cvref_t<Fields>::kind == FieldKind::UInt32) && ...));
        static_assert((std::same_as<std::remove_cvref_t<decltype(std::declval<Fields>().options)>, DefaultFieldOptions> && ...));
        return FieldOutputDefinition<Id, OutputKind::Particles, Fields...>{{fields...}};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto lines() {
        return OutputDefinition<Id, OutputKind::Lines>{};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto vectors() {
        return OutputDefinition<Id, OutputKind::Vectors>{};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto image() {
        return OutputDefinition<Id, OutputKind::Image>{};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto hash_grid_radiance_field() {
        return OutputDefinition<Id, OutputKind::HashGridRadianceField>{};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto cameras() {
        return OutputDefinition<Id, OutputKind::Cameras>{};
    }

    template <FixedString Id, typename Type>
    struct MetricDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Metric;
        static constexpr FixedString id              = Id;
        static constexpr bool is_boolean             = std::same_as<Type, bool>;
        static constexpr bool is_integral            = std::integral<Type>;
        static constexpr bool is_floating            = std::floating_point<Type>;

        std::string_view name{};
        std::string_view unit{};
        std::string_view section{};
        bool plot{};
    };

    template <FixedString Id, typename Type>
    [[nodiscard]] consteval auto metric(const std::string_view name, const std::string_view unit = {}, const std::string_view section = {}, const bool plot = false) {
        static_assert(std::same_as<Type, bool> || std::integral<Type> || std::floating_point<Type> || std::same_as<Type, Float3>);
        return MetricDefinition<Id, Type>{name, unit, section, plot};
    }

    template <typename... Definitions>
    struct Description {
        std::string_view id{};
        std::tuple<Definitions...> definitions{};
    };

    template <typename... Definitions>
    [[nodiscard]] consteval auto describe(const std::string_view id, const Definitions... definitions) {
        return Description<Definitions...>{id, {definitions...}};
    }
} // namespace spectra::sdk
