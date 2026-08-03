#pragma once

#include <cstdint>

inline constexpr std::uint32_t SPECTRA_PLUGIN_API_VERSION = 11;
inline constexpr char SPECTRA_PLUGIN_ENTRY_NAME[]         = "spectra_plugin_api_11";

struct SpectraPluginString {
    const char* data;
    std::uint64_t size;
};

struct SpectraPluginFloat2 {
    float x;
    float y;
};

struct SpectraPluginFloat3 {
    float x;
    float y;
    float z;
};

struct SpectraPluginTransform {
    float matrix[16];
};

enum class SpectraPluginPortDirection : std::uint32_t {
    Input,
    Output,
};

enum class SpectraPluginResourceKind : std::uint32_t {
    InstanceTransform,
    TriangleMesh,
    ParticleSet,
    Volume,
    DebugDraw,
};

enum class SpectraPluginMemoryDomain : std::uint32_t {
    Host,
    CudaExternal,
};

enum class SpectraPluginMeshUpdateMode : std::uint32_t {
    Deformable,
    TopologyChanging,
};

enum class SpectraPluginParameterKind : std::uint32_t {
    Boolean,
    Integer,
    Float,
    Float3,
    Enumeration,
};

enum class SpectraPluginParameterApplication : std::uint32_t {
    Live,
    ResetRequired,
};

enum class SpectraPluginDebugPrimitiveKind : std::uint32_t {
    Point,
    Line,
    Arrow,
    AxisAlignedBox,
    Contact,
    Constraint,
};

enum class SpectraPluginDebugDepthMode : std::uint32_t {
    Tested,
    XRay,
};

enum class SpectraPluginAttribute : std::uint32_t {
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

struct SpectraPluginPortDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
    SpectraPluginPortDirection direction;
    SpectraPluginResourceKind resource_kind;
    SpectraPluginMemoryDomain memory_domain;
    std::uint64_t capacity;
    std::uint64_t secondary_capacity;
    std::uint64_t attribute_mask;
    SpectraPluginMeshUpdateMode mesh_update_mode;
    std::uint32_t resolution[3];
};

struct SpectraPluginParameterValue {
    SpectraPluginParameterKind kind;
    std::int64_t integer;
    double floating[3];
};

struct SpectraPluginParameterDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
    SpectraPluginString unit;
    SpectraPluginParameterKind kind;
    SpectraPluginParameterApplication application_mode;
    SpectraPluginParameterValue default_value;
    SpectraPluginParameterValue minimum;
    SpectraPluginParameterValue maximum;
    const SpectraPluginString* enumerators;
    std::uint64_t enumerator_count;
};

struct SpectraPluginTelemetryDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
    SpectraPluginString unit;
};

struct SpectraPluginProviderDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
    SpectraPluginString interface_id;
    std::uint32_t interface_version;
    const SpectraPluginPortDescriptor* ports;
    std::uint64_t port_count;
    const SpectraPluginParameterDescriptor* parameters;
    std::uint64_t parameter_count;
    const SpectraPluginTelemetryDescriptor* telemetry_descriptors;
    std::uint64_t telemetry_count;
};

struct SpectraPluginBuffer {
    SpectraPluginAttribute attribute;
    void* external_memory_handle;
    void* host_address;
    std::uint64_t byte_size;
};

struct SpectraPluginPortSlot {
    std::uint32_t slot_index;
    const SpectraPluginBuffer* buffers;
    std::uint64_t buffer_count;
};

struct SpectraPluginPortConfiguration {
    std::uint64_t port_index;
    SpectraPluginPortDirection direction;
    SpectraPluginMemoryDomain memory_domain;
    const SpectraPluginPortSlot* slots;
    std::uint64_t slot_count;
    void* timeline_semaphore_handle;
    std::uint8_t vulkan_device_uuid[16];
    std::uint8_t vulkan_device_luid[8];
    std::uint32_t vulkan_device_node_mask;
};

struct SpectraPluginInputFrame {
    std::uint64_t port_index;
    std::uint64_t active_count;
    std::uint64_t secondary_count;
    std::uint32_t region_minimum[3];
    std::uint32_t region_maximum[3];
    std::uint32_t color_space;
};

struct SpectraPluginDebugPrimitive {
    SpectraPluginDebugPrimitiveKind kind;
    SpectraPluginDebugDepthMode depth_mode;
    SpectraPluginFloat3 first_position;
    SpectraPluginFloat3 second_position;
    SpectraPluginFloat3 color;
    float radius;
    std::uint64_t source_id;
};

struct SpectraPluginOutputCommit {
    std::uint32_t slot_index;
    std::uint64_t active_count;
    std::uint64_t secondary_count;
    std::uint64_t signal_value;
    std::uint32_t region_minimum[3];
    std::uint32_t region_maximum[3];
    std::uint32_t color_space;
};

struct SpectraPluginFrameSink {
    void* context;
    void (*write_debug_draw)(void* context, std::uint64_t port_index, const SpectraPluginDebugPrimitive* primitives, std::uint64_t primitive_count);
    void (*commit_output)(void* context, std::uint64_t port_index, const SpectraPluginOutputCommit* commit);
    void (*request_capacity)(void* context, std::uint64_t port_index, std::uint64_t capacity, std::uint64_t secondary_capacity);
};

struct SpectraPluginApi {
    std::uint32_t api_version;
    std::uint32_t struct_size;
    SpectraPluginProviderDescriptor (*describe_provider)();
    void* (*create_provider)();
    void (*destroy_provider)(void* provider);
    void (*configure_port)(void* provider, const SpectraPluginPortConfiguration* configuration);
    void (*set_input_frame)(void* provider, const SpectraPluginInputFrame* frame);
    void (*apply_parameters)(void* provider, const SpectraPluginParameterValue* values, std::uint64_t value_count);
    void (*reset)(void* provider, std::uint64_t seed);
    void (*step)(void* provider, double step_seconds, std::uint64_t step_count);
    double (*read_telemetry)(const void* provider, std::uint64_t telemetry_index);
    void (*publish_frame)(void* provider, std::uint64_t simulation_step, const SpectraPluginFrameSink* sink);
};

extern "C" {
__declspec(dllexport) const SpectraPluginApi* spectra_plugin_api_11();
}
