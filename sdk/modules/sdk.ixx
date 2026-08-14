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

    struct Float2 {
        float x{};
        float y{};
    };

    struct Float3 {
        float x{};
        float y{};
        float z{};
    };

    struct Float4 {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    struct UInt3 {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};
    };

    struct Transform {
        float matrix[16]{};
    };

    struct Sphere {
        Float3 position{};
        float radius{};
    };

    struct Instance {
        std::uint64_t instance_id{};
        Transform transform{};
    };

    struct Point {
        Float3 position{};
        float radius{};
        Float4 color{};
        float scalar{};
    };

    struct Line {
        Float3 first_position{};
        float width{};
        Float3 second_position{};
        Float4 color{};
        float scalar{};
    };

    struct Vector {
        Float3 origin{};
        float width{};
        Float3 vector{};
        Float4 color{};
        float scalar{};
    };

    static_assert(sizeof(Float2) == 8);
    static_assert(sizeof(Float3) == 12);
    static_assert(sizeof(Float4) == 16);
    static_assert(sizeof(UInt3) == 12);
    static_assert(sizeof(Transform) == 64);
    static_assert(sizeof(Sphere) == 16);
    static_assert(sizeof(Instance) == 72);
    static_assert(sizeof(Point) == 36);
    static_assert(sizeof(Line) == 48);
    static_assert(sizeof(Vector) == 48);

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
        static constexpr FixedString id               = Id;
        static constexpr auto member                  = Member;

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
        Spheres,
        Volume,
        Instances,
        Points,
        Lines,
        Vectors,
        Image,
    };

    enum class MeshAttribute : std::uint32_t {
        Normal            = 1u << 0u,
        Tangent           = 1u << 1u,
        TextureCoordinate = 1u << 2u,
        Color             = 1u << 3u,
        Scalar            = 1u << 4u,
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

    enum class VolumeFieldKind : std::uint32_t {
        Float,
        Float3,
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

    template <FixedString Id, typename Type>
    struct VolumeFieldDefinition {
        static constexpr FixedString id         = Id;
        static constexpr VolumeFieldKind kind   = std::same_as<Type, float> ? VolumeFieldKind::Float : std::same_as<Type, Float3> ? VolumeFieldKind::Float3 : VolumeFieldKind::MacFloat3;

        std::string_view name{};
        std::string_view unit{};
        VolumeFieldOptions options{};
    };

    template <FixedString Id, typename Type>
    [[nodiscard]] consteval auto field(const std::string_view name, const std::string_view unit = {}, const VolumeFieldOptions options = {}) {
        static_assert(std::same_as<Type, float> || std::same_as<Type, Float3> || std::same_as<Type, MacFloat3>);
        return VolumeFieldDefinition<Id, Type>{name, unit, options};
    }

    template <FixedString Id, OutputKind Kind, typename... Fields>
    struct OutputDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Output;
        static constexpr FixedString id               = Id;
        static constexpr OutputKind kind              = Kind;

        MeshOptions mesh_options{};
        std::tuple<Fields...> fields{};
    };

    template <FixedString Id>
    [[nodiscard]] consteval auto mesh(const MeshOptions options = {}) {
        return OutputDefinition<Id, OutputKind::Mesh>{options};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto spheres() {
        return OutputDefinition<Id, OutputKind::Spheres>{};
    }

    template <FixedString Id, typename... Fields>
    [[nodiscard]] consteval auto volume(const Fields... fields) {
        static_assert(sizeof...(Fields) != 0u);
        return OutputDefinition<Id, OutputKind::Volume, Fields...>{{}, {fields...}};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto instances() {
        return OutputDefinition<Id, OutputKind::Instances>{};
    }

    template <FixedString Id>
    [[nodiscard]] consteval auto points() {
        return OutputDefinition<Id, OutputKind::Points>{};
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

    template <FixedString Id, typename Type>
    struct MetricDefinition {
        static constexpr DefinitionCategory category = DefinitionCategory::Metric;
        static constexpr FixedString id               = Id;
        static constexpr bool is_boolean               = std::same_as<Type, bool>;
        static constexpr bool is_integral              = std::integral<Type>;
        static constexpr bool is_floating              = std::floating_point<Type>;

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
}
