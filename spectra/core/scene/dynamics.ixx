module;

#include <spectra/plugin_api.h>

export module spectra.scene.dynamics;

export import spectra.runtime;
export import spectra.scene;
export import spectra.scene.document;

import std;

namespace spectra::dynamics {
    export enum class ParameterApplication : std::uint8_t {
        Live,
        ResetRequired,
    };

    export enum class DebugPrimitiveKind : std::uint8_t {
        Point,
        Line,
        Arrow,
        AxisAlignedBox,
        Contact,
        Constraint,
    };

    export enum class DebugDepthMode : std::uint8_t {
        Tested,
        XRay,
    };

    export enum class PortDirection : std::uint8_t {
        Input,
        Output,
    };

    export enum class ResourceKind : std::uint8_t {
        InstanceTransform,
        TriangleMesh,
        ParticleSet,
        Volume,
        DebugDraw,
    };

    export enum class MemoryDomain : std::uint8_t {
        Host,
        CudaExternal,
    };

    export enum class Attribute : std::uint8_t {
        Position,
        Normal,
        Tangent,
        TextureCoordinate,
        Index,
        Radius,
        Color,
        Velocity,
        Temperature,
        Material,
        Density,
        EmissionScale,
        SigmaA,
        SigmaS,
        Emission,
        Transform,
        Bounds,
    };

    export enum class MeshUpdateMode : std::uint8_t {
        Deformable,
        TopologyChanging,
    };

    export struct ParameterDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        ParameterApplication application_mode{ParameterApplication::Live};
        scene::DynamicParameterValue value{};
        scene::DynamicParameterValue minimum{};
        scene::DynamicParameterValue maximum{};
        std::vector<std::string> enumerators{};
    };

    export struct PortDescriptor {
        std::string id{};
        PortDirection direction{PortDirection::Output};
        ResourceKind resource_kind{ResourceKind::DebugDraw};
        MemoryDomain memory_domain{MemoryDomain::Host};
        std::uint64_t capacity{};
        std::uint64_t secondary_capacity{};
        std::uint64_t attribute_mask{};
        MeshUpdateMode mesh_update_mode{MeshUpdateMode::Deformable};
        math::UInt3 resolution{};
    };

    export struct ProviderDescriptor {
        std::string id{};
        std::vector<PortDescriptor> ports{};
        std::vector<ParameterDescriptor> parameters{};
    };

    export struct SimulationTimeline {
        std::uint64_t simulation_step{};
        double simulation_seconds{};
    };

    export struct DebugPrimitive {
        DebugPrimitiveKind kind{DebugPrimitiveKind::Line};
        DebugDepthMode depth_mode{DebugDepthMode::Tested};
        math::Float3 first_position{};
        math::Float3 second_position{};
        math::Float3 color{1.0f, 1.0f, 1.0f};
        float radius{1.0f};
        std::uint64_t pick_id{};
    };

    export struct GpuOutputAttributeView {
        Attribute attribute{};
        const GpuBuffer* buffer{};
        DescriptorHandle descriptor{};
    };

    export struct GpuOutputResourceView {
        ResourceKind resource_kind{};
        std::variant<scene::InstanceId, scene::GeometryId, scene::ParticleSetId, scene::VolumeId, std::monostate> resource_id{};
        std::vector<GpuOutputAttributeView> attributes{};
        std::uint64_t active_count{};
        std::uint64_t secondary_count{};
        std::optional<scene::VolumeRegion> dirty_region{};
    };

    export struct MeshOutputBinding {
        scene::GeometryId geometry_id{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
    };

    export struct InstanceTransformUpdate {
        scene::InstanceId instance_id{};
        math::Transform transform{};
    };

    export struct TriangleMeshUpdate {
        scene::GeometryId geometry_id{};
        std::uint64_t attribute_mask{};
        std::uint64_t vertex_count{};
        std::uint64_t index_count{};
        std::vector<math::Float3> positions{};
        std::vector<math::Float3> normals{};
        std::vector<math::Float3> tangents{};
        std::vector<math::Float2> texture_coordinates{};
        std::vector<std::uint32_t> indices{};
    };

    export struct ParticleSetUpdate {
        scene::ParticleSetId particle_set_id{};
        std::uint64_t attribute_mask{};
        std::uint64_t particle_count{};
        std::vector<math::Float3> positions{};
        std::vector<float> radii{};
        std::vector<math::Float3> velocities{};
        std::vector<math::Float3> colors{};
        std::vector<float> temperatures{};
        std::vector<scene::MaterialId> materials{};
    };

    export struct VolumeUpdate {
        scene::VolumeId volume_id{};
        std::uint64_t attribute_mask{};
        math::UInt3 resolution{};
        scene::VolumeRegion region{};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
        std::vector<float> density{};
        std::vector<float> temperature{};
        std::vector<float> emission_scale{};
        std::vector<math::Float3> sigma_a{};
        std::vector<math::Float3> sigma_s{};
        std::vector<math::Float3> emission{};
        std::vector<math::Float3> velocity{};
    };

    export struct DynamicFrame {
        double simulation_seconds{};
        std::vector<InstanceTransformUpdate> instance_transform_updates{};
        std::vector<TriangleMeshUpdate> triangle_mesh_updates{};
        std::vector<ParticleSetUpdate> particle_set_updates{};
        std::vector<VolumeUpdate> volume_updates{};
        std::vector<GpuOutputResourceView> gpu_output_resources{};
        std::vector<DebugPrimitive> debug_primitives{};
    };

} // namespace spectra::dynamics

namespace spectra {
    export struct DynamicWorld {
        struct ProviderLibrary {
            ProviderLibrary(const std::filesystem::path& library_path, std::string_view expected_provider_id);
            ~ProviderLibrary();

            ProviderLibrary(const ProviderLibrary&)            = delete;
            ProviderLibrary(ProviderLibrary&&)                 = delete;
            ProviderLibrary& operator=(const ProviderLibrary&) = delete;
            ProviderLibrary& operator=(ProviderLibrary&&)      = delete;

            std::filesystem::path library_path{};
            void* library_handle{};
            const SpectraPluginApi* plugin_api{};
        };

        struct DynamicPortBuffer {
            SpectraPluginAttribute attribute{};
            std::vector<std::byte> host_storage{};
            GpuBuffer gpu_buffer{};
            DescriptorHandle buffer_descriptor{};
            std::uint64_t byte_size{};
            bool owns_descriptor{};
        };

        struct DynamicPortRuntime {
            std::size_t port_index{};
            dynamics::PortDescriptor descriptor{};
            std::variant<scene::InstanceId, scene::GeometryId, scene::ParticleSetId, scene::VolumeId, std::monostate> resource_id{};
            std::vector<std::vector<DynamicPortBuffer>> buffer_slots{};
            GpuExternalTimelineSemaphore timeline_semaphore{};
            std::uint64_t capacity{};
            std::uint64_t secondary_capacity{};
            std::uint64_t requested_capacity{};
            std::uint64_t requested_secondary_capacity{};
            std::uint64_t active_count{};
            std::uint64_t secondary_count{};
            std::uint64_t timeline_signal_value{};
            std::uint32_t current_output_slot_index{};
            std::uint32_t color_space{};
            std::optional<scene::VolumeRegion> dirty_region{};
            bool output_pending{};
        };

        struct DynamicSystemRuntime {
            std::size_t scene_system_index{};
            const dynamics::ProviderDescriptor* provider_descriptor{};
            const SpectraPluginApi* plugin_api{};
            void* provider_instance{};
            std::vector<scene::DynamicParameterValue> parameter_values{};
            std::vector<DynamicPortRuntime> ports{};
        };

        DynamicWorld(VulkanRuntime& runtime, SceneDocument& document) noexcept;
        ~DynamicWorld();

        DynamicWorld(const DynamicWorld&)            = delete;
        DynamicWorld(DynamicWorld&&)                 = delete;
        DynamicWorld& operator=(const DynamicWorld&) = delete;
        DynamicWorld& operator=(DynamicWorld&&)      = delete;

        void initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene);
        void destroy() noexcept;
        void advance(std::chrono::duration<double> elapsed);
        [[nodiscard]] const dynamics::DynamicFrame* pending_frame() noexcept;
        void consume_frame() noexcept;
        [[nodiscard]] bool controls(scene::InstanceId instance_id) const noexcept;
        [[nodiscard]] const dynamics::ProviderDescriptor& provider_descriptor(std::string_view provider_id) const;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] dynamics::SimulationTimeline timeline() const noexcept;
        void start();
        void pause();
        void step();
        void reset();
        void seek(std::uint64_t simulation_step);
        void apply_parameter_changes(std::size_t system_index, std::span<const scene::DynamicParameterSetting> parameters, bool reset);

        struct {
            VulkanRuntime& runtime;
            SceneDocument& document;
        } context;

        struct {
            bool initialized{};
            const scene::Scene* source_scene{};
            scene::DynamicSetup setup{};
        } configuration;

        struct {
            std::deque<ProviderLibrary> libraries{};
            std::unordered_map<std::string, ProviderLibrary*> by_id{};
            std::vector<dynamics::ProviderDescriptor> descriptors{};
        } providers;

        struct {
            std::vector<DynamicSystemRuntime> runtimes{};
        } systems;

        struct {
            std::uint64_t simulation_step{};
            std::chrono::duration<double> accumulator{};
            bool playing{};
        } clock;

        struct {
            std::map<std::pair<std::size_t, std::uint64_t>, std::uint64_t> debug_object_ids{};
            dynamics::DynamicFrame frame{};
            std::uint64_t next_debug_object_id{1};
            bool frame_pending{};
            DynamicSystemRuntime* publishing_system{};
            dynamics::DynamicFrame* publishing_frame{};
            std::vector<dynamics::DebugPrimitive> debug_primitives{};
        } publication;

        struct {
            std::vector<dynamics::MeshOutputBinding> mesh_bindings{};
            std::vector<std::pair<scene::ParticleSetId, std::uint32_t>> particle_capacities{};
            std::vector<scene::InstanceId> hidden_instances{};
        } outputs;

    private:
        [[nodiscard]] ProviderLibrary& provider_library(std::string_view provider_id) const;
        void bind_resource(DynamicPortRuntime& port, const scene::DynamicPortBinding& binding) const;
        void declare_output(const DynamicPortRuntime& port);
        void configure_port(DynamicSystemRuntime& system, std::size_t port_index);
        [[nodiscard]] DynamicPortRuntime& port_runtime(DynamicSystemRuntime& system, std::uint64_t port_index);
        void set_inputs(DynamicSystemRuntime& system);
        void apply_parameters(DynamicSystemRuntime& system);
        void commit_output(DynamicSystemRuntime& system, DynamicPortRuntime& port, const SpectraPluginOutputCommit& commit, dynamics::DynamicFrame& dynamic_frame);
        void publish_frame(std::uint64_t simulation_step);
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate_frame(std::uint64_t target_step);
        void reset_simulation();
        void advance_one_step();
        static void collect_debug(void* context, std::uint64_t port_index, const SpectraPluginDebugPrimitive* primitives, std::uint64_t primitive_count);
        static void collect_output(void* context, std::uint64_t port_index, const SpectraPluginOutputCommit* commit);
        static void collect_capacity(void* context, std::uint64_t port_index, std::uint64_t capacity, std::uint64_t secondary_capacity);
    };
} // namespace spectra
