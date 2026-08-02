module;

#include <spectra/plugin_api.h>

export module spectra.scene.dynamics;

import spectra;
import spectra.scene;
import std;

namespace spectra::scene::dynamics {
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

    export struct Parameter {
        std::string id{};
        std::string name{};
        std::string unit{};
        ParameterApplication application{ParameterApplication::Live};
        DynamicParameterValue value{};
        DynamicParameterValue minimum{};
        DynamicParameterValue maximum{};
        std::vector<std::string> enumerators{};
    };

    export struct Telemetry {
        std::string id{};
        std::string name{};
        std::string unit{};
        double value{};
    };

    export struct PortDescriptor {
        std::string id{};
        std::string name{};
        PortDirection direction{PortDirection::Output};
        ResourceKind kind{ResourceKind::DebugDraw};
        MemoryDomain memory_domain{MemoryDomain::Host};
        std::uint64_t capacity{};
        std::uint64_t secondary_capacity{};
        std::uint64_t attribute_mask{};
        MeshUpdateMode mesh_update_mode{MeshUpdateMode::Deformable};
        UInt3 resolution{};
    };

    export struct ProviderDescriptor {
        std::string id{};
        std::string name{};
        std::string interface_id{};
        std::uint32_t interface_version{1};
        std::vector<PortDescriptor> ports{};
        std::vector<Parameter> parameters{};
        std::vector<Telemetry> telemetry{};
    };

    export struct TimelineState {
        std::uint64_t step{};
        double seconds{};
        double step_seconds{1.0 / 120.0};
    };

    export struct DebugPrimitive {
        DynamicSystemId system{};
        DebugPrimitiveKind kind{DebugPrimitiveKind::Line};
        DebugDepthMode depth_mode{DebugDepthMode::Tested};
        Float3 first{};
        Float3 second{};
        Float3 color{1.0f, 1.0f, 1.0f};
        float radius{1.0f};
        std::uint64_t object{};
        std::uint64_t pick{};
    };

    export struct ExternalBufferView {
        Attribute attribute{};
        const GpuBuffer* buffer{};
        DescriptorHandle descriptor{};
    };

    export struct ExternalOutputView {
        ResourceKind kind{};
        std::variant<InstanceId, GeometryId, ParticleSetId, VolumeId, std::monostate> resource{};
        std::vector<ExternalBufferView> buffers{};
        std::uint64_t active_count{};
        std::uint64_t secondary_count{};
        std::optional<VolumeRegion> dirty_region{};
    };

    export struct MeshOutputBinding {
        GeometryId geometry{};
        MeshUpdateMode mode{MeshUpdateMode::Deformable};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
    };

    export struct InstanceTransformUpdate {
        InstanceId instance{};
        Transform transform{};
    };

    export struct TriangleMeshUpdate {
        GeometryId geometry{};
        std::uint64_t attributes{};
        std::uint64_t vertex_count{};
        std::uint64_t index_count{};
        std::vector<Float3> positions{};
        std::vector<Float3> normals{};
        std::vector<Float3> tangents{};
        std::vector<Float2> texture_coordinates{};
        std::vector<std::uint32_t> indices{};
    };

    export struct ParticleSetUpdate {
        ParticleSetId particles{};
        std::uint64_t attributes{};
        std::uint64_t particle_count{};
        std::vector<Float3> positions{};
        std::vector<float> radii{};
        std::vector<Float3> velocities{};
        std::vector<Float3> colors{};
        std::vector<float> temperatures{};
        std::vector<MaterialId> materials{};
    };

    export struct VolumeUpdate {
        VolumeId volume{};
        std::uint64_t attributes{};
        UInt3 resolution{};
        VolumeRegion region{};
        SpectrumColorSpace color_space{SpectrumColorSpace::Srgb};
        std::vector<float> density{};
        std::vector<float> temperature{};
        std::vector<float> emission_scale{};
        std::vector<Float3> sigma_a{};
        std::vector<Float3> sigma_s{};
        std::vector<Float3> emission{};
        std::vector<Float3> velocity{};
    };

    export struct PublishedFrame {
        std::uint64_t publication{};
        std::uint64_t simulation_step{};
        double simulation_seconds{};
        std::vector<InstanceTransformUpdate> transforms{};
        std::vector<TriangleMeshUpdate> meshes{};
        std::vector<ParticleSetUpdate> particles{};
        std::vector<VolumeUpdate> volumes{};
        std::vector<ExternalOutputView> external{};
        std::vector<DebugPrimitive> debug{};
    };

    export struct SystemState {
        DynamicSystemId id{};
        std::string name{};
        std::string provider_name{};
        bool enabled{};
        bool visible{};
        std::uint64_t completed_steps{};
        double simulation_seconds{};
        double last_batch_milliseconds{};
        double average_step_milliseconds{};
        std::vector<Parameter> parameters{};
        std::vector<Telemetry> telemetry{};
    };

    export struct Runtime {
        Runtime(Spectra& runtime, const std::filesystem::path& scene_path, const Scene& scene);
        ~Runtime();

        Runtime(const Runtime&)            = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime& operator=(Runtime&&)      = delete;

        void set_running(bool running) noexcept;
        void advance();
        void seek(std::uint64_t frame);
        void reset();
        void update(std::chrono::duration<double> elapsed);
        void apply_parameters(std::size_t system, bool reset);
        void bind_scene(const Scene& scene) noexcept;
        [[nodiscard]] const PublishedFrame* prepare_frame() noexcept;
        void consume_frame() noexcept;
        [[nodiscard]] bool controls(InstanceId instance) const noexcept;
        [[nodiscard]] TimelineState timeline() const noexcept;
        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] const ProviderDescriptor& provider(std::string_view id) const;

        std::vector<ProviderDescriptor> providers{};
        std::vector<SystemState> systems{};
        std::vector<DebugPrimitive> debug_primitives{};
        std::vector<MeshOutputBinding> mesh_bindings{};
        std::vector<std::pair<ParticleSetId, std::uint32_t>> particle_capacities{};
        std::vector<InstanceId> hidden_instances{};

    private:
        struct Library {
            Library(const std::filesystem::path& source, std::string_view expected_provider);
            ~Library();

            Library(const Library&)            = delete;
            Library(Library&&)                 = delete;
            Library& operator=(const Library&) = delete;
            Library& operator=(Library&&)      = delete;

            std::filesystem::path path{};
            void* module{};
            const SpectraPluginApi* api{};
        };

        struct Buffer {
            SpectraPluginAttribute attribute{};
            std::vector<std::byte> host{};
            GpuBuffer storage{};
            DescriptorHandle descriptor{};
            std::uint64_t size{};
            bool descriptor_allocated{};
        };

        struct Binding {
            std::size_t port{};
            PortDescriptor descriptor{};
            std::variant<InstanceId, GeometryId, ParticleSetId, VolumeId, std::monostate> resource{};
            std::vector<std::vector<Buffer>> slots{};
            GpuExternalTimeline timeline{};
            std::uint64_t capacity{};
            std::uint64_t secondary_capacity{};
            std::uint64_t requested_capacity{};
            std::uint64_t requested_secondary_capacity{};
            std::uint64_t active_count{};
            std::uint64_t secondary_count{};
            std::uint64_t signal_value{};
            std::uint32_t current_slot{};
            std::uint32_t color_space{};
            std::optional<VolumeRegion> dirty_region{};
            bool pending{};
        };

        struct System {
            std::size_t scene_index{};
            const ProviderDescriptor* provider{};
            const SpectraPluginApi* api{};
            void* instance{};
            std::vector<DynamicParameterValue> parameter_values{};
            std::vector<Binding> bindings{};
            std::uint64_t completed_steps{};
            double total_step_milliseconds{};
        };

        struct FrameCollector {
            Runtime* runtime{};
            System* system{};
            PublishedFrame* frame{};
            SpectraPluginFrameSink sink{this, &FrameCollector::debug, &FrameCollector::commit, &FrameCollector::capacity};

            static void debug(void* opaque, std::uint64_t port, const SpectraPluginDebugPrimitive* primitives, std::uint64_t count);
            static void commit(void* opaque, std::uint64_t port, const SpectraPluginOutputCommit* commit);
            static void capacity(void* opaque, std::uint64_t port, std::uint64_t capacity, std::uint64_t secondary_capacity);
        };

        [[nodiscard]] Library& library(std::string_view provider) const;
        void bind_resource(Binding& binding, const DynamicPortBinding& source) const;
        void declare_output(const Binding& binding);
        void configure(System& system, std::size_t binding);
        [[nodiscard]] Binding& binding(System& system, std::uint64_t port);
        void set_inputs(System& system);
        void apply_parameters(System& system);
        void commit(System& system, Binding& binding, const SpectraPluginOutputCommit& commit, PublishedFrame& frame);
        void publish(std::uint64_t simulation_step);
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate(std::uint64_t target_step);

        Spectra* runtime{};
        const Scene* source_scene{};
        DynamicSetup setup{};
        std::deque<Library> libraries{};
        std::unordered_map<std::string, Library*> provider_libraries{};
        std::vector<System> system_storage{};
        std::map<std::pair<std::size_t, std::uint64_t>, std::uint64_t> debug_objects{};
        PublishedFrame published{};
        std::uint64_t publication{};
        std::uint64_t simulation_step{};
        std::uint64_t next_debug_object{1};
        std::chrono::duration<double> accumulated_time{};
        bool playback_running{};
        bool publication_pending{};
    };
} // namespace spectra::scene::dynamics
