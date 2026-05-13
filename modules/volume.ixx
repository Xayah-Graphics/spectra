module;
#include <vulkan/vulkan_raii.hpp>

export module volume;
export import scene_object;
import std;

namespace xayah {
    export struct CenteredScalarGrid {
        std::string name{};
        std::array<std::uint32_t, 3> resolution{0, 0, 0};
        std::vector<float> values{};
    };

    export struct StaggeredVectorGrid {
        std::string name{};
        std::array<std::uint32_t, 3> resolution{0, 0, 0};
        std::vector<float> x_values{};
        std::vector<float> y_values{};
        std::vector<float> z_values{};
    };

    export enum class VolumeGridKind : std::uint32_t {
        centered_scalar  = 0,
        staggered_vector = 1,
    };

    export enum class VolumeDisplayMode : std::uint32_t {
        direct = 0,
        slice  = 1,
    };

    export enum class VolumeSliceAxis : std::uint32_t {
        x = 0,
        y = 1,
        z = 2,
    };

    export enum class VolumeColorMap : std::uint32_t {
        grayscale = 0,
        viridis   = 1,
        turbo     = 2,
        heat      = 3,
    };

    export struct VolumeRenderSettings {
        std::string grid_name{};
        VolumeGridKind grid_kind{VolumeGridKind::centered_scalar};
        VolumeDisplayMode display_mode{VolumeDisplayMode::direct};
        bool show_bounding_box{true};
        VolumeSliceAxis slice_axis{VolumeSliceAxis::y};
        VolumeColorMap color_map{VolumeColorMap::viridis};
        float slice_position{0.5f};
        float value_min{0.0f};
        float value_max{1.0f};
        float opacity{0.65f};
        float raymarch_step{0.025f};
    };

    export struct VolumeSnapshot {
        std::uint64_t object_id{0};
        std::vector<CenteredScalarGrid> centered_scalar_grids{};
        std::vector<StaggeredVectorGrid> staggered_vector_grids{};
    };

    export class VolumeRenderer {
    public:
        VolumeRenderer();
        ~VolumeRenderer() noexcept;

        VolumeRenderer(const VolumeRenderer& other)                = delete;
        VolumeRenderer(VolumeRenderer&& other) noexcept            = delete;
        VolumeRenderer& operator=(const VolumeRenderer& other)     = delete;
        VolumeRenderer& operator=(VolumeRenderer&& other) noexcept = delete;

        void create(const SceneRenderCreateContext& context);
        void destroy() noexcept;
        [[nodiscard]] bool active() const;

        vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
    };

    export class Volume {
    public:
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::array<float, 3> size{2.0f, 2.0f, 2.0f};
        std::vector<CenteredScalarGrid> centered_scalar_grids{};
        std::vector<StaggeredVectorGrid> staggered_vector_grids{};
        VolumeRenderSettings render_settings{};

        Volume();
        ~Volume() noexcept;

        Volume(const Volume& other)                = delete;
        Volume(Volume&& other) noexcept            = default;
        Volume& operator=(const Volume& other)     = delete;
        Volume& operator=(Volume&& other) noexcept = default;

        [[nodiscard]] SceneObjectKind kind() const;
        void validate() const;
        void initialize_render_settings();
        void select_first_grid();
        [[nodiscard]] const CenteredScalarGrid& render_centered_scalar_grid() const;
        [[nodiscard]] const StaggeredVectorGrid& render_staggered_vector_grid() const;
        void create_render_resources(const SceneRenderCreateContext& context, const VolumeRenderer& renderer);
        void destroy_render_resources() noexcept;
        void render(const SceneRenderFrameContext& context, const VolumeRenderer& renderer);
        void draw_inspector_ui();
        [[nodiscard]] BoundingBoxBounds bounds() const;
        [[nodiscard]] VolumeSnapshot make_snapshot() const;
        void apply_snapshot(const VolumeSnapshot& snapshot);

    private:
        struct VolumeDrawResources {
            vk::raii::Buffer x_data_buffer{nullptr};
            vk::raii::DeviceMemory x_data_memory{nullptr};
            vk::DeviceSize x_data_size{0};
            vk::raii::Buffer y_data_buffer{nullptr};
            vk::raii::DeviceMemory y_data_memory{nullptr};
            vk::DeviceSize y_data_size{0};
            vk::raii::Buffer z_data_buffer{nullptr};
            vk::raii::DeviceMemory z_data_memory{nullptr};
            vk::DeviceSize z_data_size{0};
            vk::raii::Buffer parameters_buffer{nullptr};
            vk::raii::DeviceMemory parameters_memory{nullptr};
            vk::DeviceSize parameters_size{0};
        };

        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::DescriptorSets descriptor_sets{nullptr};
        std::vector<VolumeDrawResources> frame_resources{};
    };

    static_assert(SceneObject<Volume>);
} // namespace xayah
