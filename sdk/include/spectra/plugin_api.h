#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr std::uint32_t SPECTRA_PLUGIN_API_VERSION = 17;
inline constexpr char SPECTRA_PLUGIN_ENTRY_NAME[]         = "spectra_plugin_api_17";

#if defined(_WIN32)
#define SPECTRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define SPECTRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

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

struct SpectraPluginFloat4 {
    float x;
    float y;
    float z;
    float w;
};

struct SpectraPluginTransform {
    float matrix[16];
};

enum class SpectraPluginDatasetKind : std::uint32_t {
    Mesh,
    PointSet,
    SegmentSet,
    CurveSet,
    VectorSet,
    Field,
    Image,
    CameraObservationSet,
    TransformSet,
};

enum class SpectraPluginDatasetBufferKind : std::uint32_t {
    MeshPosition,
    MeshNormal,
    MeshTangent,
    MeshTextureCoordinate,
    MeshIndex,
    Point,
    Segment,
    Curve,
    Vector,
    FieldChannel,
    ImagePixel,
    CameraObservation,
    Transform,
    TelemetryValue,
};

enum class SpectraPluginFieldChannelKind : std::uint32_t {
    Float,
    Float3,
};

enum class SpectraPluginImageFormat : std::uint32_t {
    Rgba8Unorm,
    Rgba16Float,
    Rgba32Float,
};

enum class SpectraPluginColorSpace : std::uint32_t {
    Srgb,
    Rec2020,
    Aces2065_1,
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

enum class SpectraPluginTelemetryKind : std::uint32_t {
    Boolean,
    Integer,
    Float,
    Float3,
};

enum class SpectraPluginExternalHandleType : std::uint32_t {
    None,
    OpaqueWin32,
    OpaqueFileDescriptor,
};

struct SpectraPluginExternalHandle {
    SpectraPluginExternalHandleType type;
    std::uint64_t value;
};

struct SpectraPluginPoint {
    SpectraPluginFloat3 position;
    float radius;
    SpectraPluginFloat4 color;
    float scalar;
};

struct SpectraPluginSegment {
    SpectraPluginFloat3 first_position;
    float width;
    SpectraPluginFloat3 second_position;
    SpectraPluginFloat4 color;
};

struct SpectraPluginCurve {
    SpectraPluginFloat3 control_0;
    float width;
    SpectraPluginFloat3 control_1;
    SpectraPluginFloat3 control_2;
    SpectraPluginFloat3 control_3;
    SpectraPluginFloat4 color;
};

struct SpectraPluginVector {
    SpectraPluginFloat3 origin;
    float width;
    SpectraPluginFloat3 vector;
    SpectraPluginFloat4 color;
};

struct SpectraPluginCameraDistortion {
    float radial_1;
    float radial_2;
    float tangential_1;
    float tangential_2;
};

struct SpectraPluginCameraObservation {
    SpectraPluginTransform world_from_camera;
    SpectraPluginFloat4 intrinsics;
    SpectraPluginCameraDistortion distortion;
    std::uint32_t image_layer;
};

struct SpectraPluginTelemetryGpuValue {
    double floating[3];
    std::int64_t integer;
};

static_assert(sizeof(SpectraPluginFloat2) == 8);
static_assert(alignof(SpectraPluginFloat2) == 4);
static_assert(offsetof(SpectraPluginFloat2, x) == 0);
static_assert(offsetof(SpectraPluginFloat2, y) == 4);
static_assert(sizeof(SpectraPluginFloat3) == 12);
static_assert(alignof(SpectraPluginFloat3) == 4);
static_assert(offsetof(SpectraPluginFloat3, x) == 0);
static_assert(offsetof(SpectraPluginFloat3, y) == 4);
static_assert(offsetof(SpectraPluginFloat3, z) == 8);
static_assert(sizeof(SpectraPluginFloat4) == 16);
static_assert(alignof(SpectraPluginFloat4) == 4);
static_assert(offsetof(SpectraPluginFloat4, x) == 0);
static_assert(offsetof(SpectraPluginFloat4, y) == 4);
static_assert(offsetof(SpectraPluginFloat4, z) == 8);
static_assert(offsetof(SpectraPluginFloat4, w) == 12);
static_assert(sizeof(SpectraPluginTransform) == 64);
static_assert(alignof(SpectraPluginTransform) == 4);
static_assert(offsetof(SpectraPluginTransform, matrix) == 0);
static_assert(sizeof(SpectraPluginPoint) == 36);
static_assert(alignof(SpectraPluginPoint) == 4);
static_assert(offsetof(SpectraPluginPoint, position) == 0);
static_assert(offsetof(SpectraPluginPoint, radius) == 12);
static_assert(offsetof(SpectraPluginPoint, color) == 16);
static_assert(offsetof(SpectraPluginPoint, scalar) == 32);
static_assert(sizeof(SpectraPluginSegment) == 44);
static_assert(alignof(SpectraPluginSegment) == 4);
static_assert(offsetof(SpectraPluginSegment, first_position) == 0);
static_assert(offsetof(SpectraPluginSegment, width) == 12);
static_assert(offsetof(SpectraPluginSegment, second_position) == 16);
static_assert(offsetof(SpectraPluginSegment, color) == 28);
static_assert(sizeof(SpectraPluginCurve) == 68);
static_assert(alignof(SpectraPluginCurve) == 4);
static_assert(offsetof(SpectraPluginCurve, control_0) == 0);
static_assert(offsetof(SpectraPluginCurve, width) == 12);
static_assert(offsetof(SpectraPluginCurve, control_1) == 16);
static_assert(offsetof(SpectraPluginCurve, control_2) == 28);
static_assert(offsetof(SpectraPluginCurve, control_3) == 40);
static_assert(offsetof(SpectraPluginCurve, color) == 52);
static_assert(sizeof(SpectraPluginVector) == 44);
static_assert(alignof(SpectraPluginVector) == 4);
static_assert(offsetof(SpectraPluginVector, origin) == 0);
static_assert(offsetof(SpectraPluginVector, width) == 12);
static_assert(offsetof(SpectraPluginVector, vector) == 16);
static_assert(offsetof(SpectraPluginVector, color) == 28);
static_assert(sizeof(SpectraPluginCameraDistortion) == 16);
static_assert(alignof(SpectraPluginCameraDistortion) == 4);
static_assert(offsetof(SpectraPluginCameraDistortion, radial_1) == 0);
static_assert(offsetof(SpectraPluginCameraDistortion, radial_2) == 4);
static_assert(offsetof(SpectraPluginCameraDistortion, tangential_1) == 8);
static_assert(offsetof(SpectraPluginCameraDistortion, tangential_2) == 12);
static_assert(sizeof(SpectraPluginCameraObservation) == 100);
static_assert(alignof(SpectraPluginCameraObservation) == 4);
static_assert(offsetof(SpectraPluginCameraObservation, world_from_camera) == 0);
static_assert(offsetof(SpectraPluginCameraObservation, intrinsics) == 64);
static_assert(offsetof(SpectraPluginCameraObservation, distortion) == 80);
static_assert(offsetof(SpectraPluginCameraObservation, image_layer) == 96);
static_assert(sizeof(SpectraPluginTelemetryGpuValue) == 32);
static_assert(alignof(SpectraPluginTelemetryGpuValue) == 8);
static_assert(offsetof(SpectraPluginTelemetryGpuValue, floating) == 0);
static_assert(offsetof(SpectraPluginTelemetryGpuValue, integer) == 24);

struct SpectraPluginFieldChannelDescriptor {
    SpectraPluginString id;
    SpectraPluginFieldChannelKind kind;
};

struct SpectraPluginDatasetBufferDescriptor {
    SpectraPluginDatasetBufferKind kind;
    std::uint32_t channel_index;
};

struct SpectraPluginDatasetDescriptor {
    SpectraPluginString id;
    SpectraPluginDatasetKind kind;
    std::uint64_t capacity;
    std::uint64_t secondary_capacity;
    SpectraPluginMeshUpdateMode mesh_update_mode;
    const SpectraPluginDatasetBufferDescriptor* buffers;
    std::uint64_t buffer_count;
    std::uint32_t resolution[3];
    const SpectraPluginFieldChannelDescriptor* field_channels;
    std::uint64_t field_channel_count;
    std::uint32_t image_extent[2];
    SpectraPluginImageFormat image_format;
    SpectraPluginColorSpace color_space;
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
    SpectraPluginString section_id;
    SpectraPluginString description;
    SpectraPluginParameterValue step;
};

struct SpectraPluginSectionDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
};

struct SpectraPluginTelemetryDescriptor {
    SpectraPluginString id;
    SpectraPluginString name;
    SpectraPluginString unit;
    SpectraPluginString section_id;
    SpectraPluginTelemetryKind kind;
    bool plot;
};

struct SpectraPluginProviderDescriptor {
    SpectraPluginString id;
    const SpectraPluginDatasetDescriptor* datasets;
    std::uint64_t dataset_count;
    const SpectraPluginParameterDescriptor* parameters;
    std::uint64_t parameter_count;
    const SpectraPluginSectionDescriptor* sections;
    std::uint64_t section_count;
    const SpectraPluginTelemetryDescriptor* telemetry;
    std::uint64_t telemetry_count;
};

struct SpectraPluginGpuBuffer {
    SpectraPluginDatasetBufferKind kind;
    std::uint32_t channel_index;
    SpectraPluginExternalHandle external_memory_handle;
    std::uint64_t byte_size;
};

struct SpectraPluginGpuSlot {
    std::uint32_t slot_index;
    const SpectraPluginGpuBuffer* buffers;
    std::uint64_t buffer_count;
};

struct SpectraPluginDatasetConfiguration {
    std::uint64_t dataset_index;
    const SpectraPluginGpuSlot* slots;
    std::uint64_t slot_count;
    SpectraPluginExternalHandle timeline_semaphore_handle;
    std::uint8_t vulkan_device_uuid[16];
    std::uint8_t vulkan_device_luid[8];
    std::uint32_t vulkan_device_node_mask;
};

struct SpectraPluginTelemetryConfiguration {
    const SpectraPluginGpuSlot* slots;
    std::uint64_t slot_count;
    SpectraPluginExternalHandle timeline_semaphore_handle;
    std::uint8_t vulkan_device_uuid[16];
    std::uint8_t vulkan_device_luid[8];
    std::uint32_t vulkan_device_node_mask;
};

struct SpectraPluginDatasetCommit {
    std::uint32_t slot_index;
    std::uint64_t active_count;
    std::uint64_t secondary_count;
    std::uint64_t signal_value;
    std::uint32_t region_minimum[3];
    std::uint32_t region_maximum[3];
};

struct SpectraPluginTelemetryCommit {
    std::uint32_t slot_index;
    std::uint64_t signal_value;
    SpectraPluginString phase;
    SpectraPluginString headline;
    SpectraPluginString message;
};

struct SpectraPluginFrameSink {
    void* context;
    void (*commit_dataset)(void* context, std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit);
    void (*request_dataset_capacity)(void* context, std::uint64_t dataset_index, std::uint64_t capacity, std::uint64_t secondary_capacity);
    void (*commit_telemetry)(void* context, const SpectraPluginTelemetryCommit* commit);
};

struct SpectraPluginApi {
    std::uint32_t api_version;
    std::uint32_t struct_size;
    SpectraPluginProviderDescriptor (*describe_provider)();
    void* (*create_provider)();
    void (*destroy_provider)(void* provider);
    void (*configure_dataset)(void* provider, const SpectraPluginDatasetConfiguration* configuration);
    void (*configure_telemetry)(void* provider, const SpectraPluginTelemetryConfiguration* configuration);
    void (*apply_parameters)(void* provider, const SpectraPluginParameterValue* values, std::uint64_t value_count);
    void (*reset)(void* provider, std::uint64_t seed);
    void (*step)(void* provider, double step_seconds, std::uint64_t step_count);
    void (*publish_frame)(void* provider, std::uint64_t simulation_step, const SpectraPluginFrameSink* sink);
    bool (*tick_presentation)(void* provider, double elapsed_seconds);
};

extern "C" {
SPECTRA_PLUGIN_EXPORT const SpectraPluginApi* spectra_plugin_api_17();
}
