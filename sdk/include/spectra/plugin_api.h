#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr std::uint32_t SPECTRA_PLUGIN_API_VERSION = 21;
inline constexpr char SPECTRA_PLUGIN_ENTRY_NAME[]         = "spectra_plugin_api_21";

#if defined(_WIN32)
#define SPECTRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#define SPECTRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

struct SpectraPluginString {
    const char* data;
    std::uint64_t size;
};

// Every string and pointer returned by describe_provider remains valid until the
// Provider Library is unloaded. Strings returned by all other callbacks remain
// valid until the next call on that Provider. A zero-sized error is success.
// No exception may cross any function pointer below.
struct SpectraPluginResult {
    SpectraPluginString error;
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
    TriangleMesh,
    SphereSet,
    InstanceTransformSet,
    PointSet,
    SegmentSet,
    CurveSet,
    VectorSet,
    Field,
    Image,
    CameraObservationSet,
    TransformSet,
};

enum class SpectraPluginBufferSemantic : std::uint32_t {
    TrianglePosition,
    TriangleNormal,
    TriangleTangent,
    TriangleTextureCoordinate,
    TriangleScalar,
    TriangleIndex,
    Sphere,
    InstanceTransform,
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

enum class SpectraPluginTransferFunction : std::uint32_t {
    Linear,
    Srgb,
};

enum class SpectraPluginMeshUpdateMode : std::uint32_t {
    Deformable,
    TopologyChanging,
};

enum class SpectraPluginMeshAttribute : std::uint32_t {
    Normal            = 1u << 0u,
    Tangent           = 1u << 1u,
    TextureCoordinate = 1u << 2u,
    Scalar            = 1u << 3u,
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

struct SpectraPluginSphere {
    SpectraPluginFloat3 position;
    float radius;
};

struct SpectraPluginInstanceTransform {
    std::uint64_t instance_id;
    SpectraPluginTransform transform;
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
    float scalar;
};

struct SpectraPluginCurve {
    SpectraPluginFloat3 control_0;
    float width;
    SpectraPluginFloat3 control_1;
    SpectraPluginFloat3 control_2;
    SpectraPluginFloat3 control_3;
    SpectraPluginFloat4 color;
    float scalar;
};

struct SpectraPluginVector {
    SpectraPluginFloat3 origin;
    float width;
    SpectraPluginFloat3 vector;
    SpectraPluginFloat4 color;
    float scalar;
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
static_assert(sizeof(SpectraPluginFloat3) == 12);
static_assert(sizeof(SpectraPluginFloat4) == 16);
static_assert(sizeof(SpectraPluginTransform) == 64);
static_assert(sizeof(SpectraPluginSphere) == 16);
static_assert(sizeof(SpectraPluginInstanceTransform) == 72);
static_assert(offsetof(SpectraPluginInstanceTransform, transform) == 8);
static_assert(sizeof(SpectraPluginPoint) == 36);
static_assert(offsetof(SpectraPluginPoint, scalar) == 32);
static_assert(sizeof(SpectraPluginSegment) == 48);
static_assert(offsetof(SpectraPluginSegment, scalar) == 44);
static_assert(sizeof(SpectraPluginCurve) == 72);
static_assert(offsetof(SpectraPluginCurve, scalar) == 68);
static_assert(sizeof(SpectraPluginVector) == 48);
static_assert(offsetof(SpectraPluginVector, scalar) == 44);
static_assert(sizeof(SpectraPluginCameraDistortion) == 16);
static_assert(sizeof(SpectraPluginCameraObservation) == 100);
static_assert(offsetof(SpectraPluginCameraObservation, image_layer) == 96);
static_assert(sizeof(SpectraPluginTelemetryGpuValue) == 32);
static_assert(alignof(SpectraPluginTelemetryGpuValue) == 8);

struct SpectraPluginFieldChannelDescriptor {
    SpectraPluginString id;
    SpectraPluginFieldChannelKind kind;
};

struct SpectraPluginTriangleMeshDatasetDescriptor {
    std::uint32_t vertex_capacity;
    std::uint32_t index_capacity;
    SpectraPluginMeshUpdateMode update_mode;
    std::uint32_t attributes;
};

struct SpectraPluginSphereSetDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginInstanceTransformDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginPointDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginSegmentDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginCurveDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginVectorDatasetDescriptor {
    std::uint32_t capacity;
};

struct SpectraPluginFieldDatasetDescriptor {
    std::uint32_t resolution[3];
    const SpectraPluginFieldChannelDescriptor* channels;
    std::uint64_t channel_count;
    SpectraPluginTransform local_from_grid;
};

struct SpectraPluginImageDatasetDescriptor {
    std::uint32_t extent[2];
    SpectraPluginImageFormat format;
    SpectraPluginColorSpace color_space;
    SpectraPluginTransferFunction transfer_function;
};

struct SpectraPluginCameraObservationDatasetDescriptor {
    std::uint32_t capacity;
    std::uint32_t image_extent[2];
    SpectraPluginImageFormat image_format;
    SpectraPluginColorSpace color_space;
    SpectraPluginTransferFunction transfer_function;
};

struct SpectraPluginTransformDatasetDescriptor {
    std::uint32_t capacity;
};

union SpectraPluginDatasetDetails {
    SpectraPluginTriangleMeshDatasetDescriptor triangle_mesh;
    SpectraPluginSphereSetDatasetDescriptor sphere_set;
    SpectraPluginInstanceTransformDatasetDescriptor instance_transforms;
    SpectraPluginPointDatasetDescriptor points;
    SpectraPluginSegmentDatasetDescriptor segments;
    SpectraPluginCurveDatasetDescriptor curves;
    SpectraPluginVectorDatasetDescriptor vectors;
    SpectraPluginFieldDatasetDescriptor field;
    SpectraPluginImageDatasetDescriptor image;
    SpectraPluginCameraObservationDatasetDescriptor camera_observations;
    SpectraPluginTransformDatasetDescriptor transforms;
};

struct SpectraPluginDatasetDescriptor {
    SpectraPluginString id;
    SpectraPluginDatasetKind kind;
    SpectraPluginDatasetDetails details;
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
    std::uint8_t plot;
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

struct SpectraPluginProviderDescriptionResult {
    SpectraPluginResult result;
    SpectraPluginProviderDescriptor descriptor;
};

struct SpectraPluginProviderCreateResult {
    SpectraPluginResult result;
    void* provider;
};

struct SpectraPluginPresentationTickResult {
    SpectraPluginResult result;
    std::uint8_t dirty;
};

struct SpectraPluginGpuBuffer {
    SpectraPluginBufferSemantic semantic;
    std::uint32_t channel_index;
    SpectraPluginExternalHandle external_memory_handle;
    std::uint64_t byte_size;
};

struct SpectraPluginGpuSlot {
    std::uint32_t slot_index;
    const SpectraPluginGpuBuffer* buffers;
    std::uint64_t buffer_count;
};

// Configuration objects, slot arrays, buffer arrays, and their handles are valid
// only for the configure_dataset/configure_telemetry call. The Provider must import
// every resource before returning and retain only the imported GPU objects.
// OpaqueWin32 handles are borrowed and must not be closed by the Provider; Host
// closes them after the callback. OpaqueFileDescriptor ownership transfers to the
// Provider when the callback begins. Provider must pass each descriptor directly
// to one external-resource import or close it before returning; an imported
// descriptor is consumed by the import and must not be duplicated or closed.
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
    std::uint32_t active_count;
    std::uint32_t secondary_count;
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
    // The sink and every commit pointer are valid only during publish_frame. Host
    // copies commit values and strings before each sink callback returns.
    void* context;
    SpectraPluginResult (*commit_dataset)(void* context, std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit) noexcept;
    SpectraPluginResult (*request_dataset_capacity)(void* context, std::uint64_t dataset_index, std::uint32_t capacity, std::uint32_t secondary_capacity) noexcept;
    SpectraPluginResult (*commit_telemetry)(void* context, const SpectraPluginTelemetryCommit* commit) noexcept;
};

struct SpectraPluginApi {
    std::uint32_t api_version;
    std::uint32_t struct_size;
    SpectraPluginProviderDescriptionResult (*describe_provider)() noexcept;
    // A failed create_provider returns a null provider. A successful Provider is
    // owned by Host until the matching destroy_provider call.
    SpectraPluginProviderCreateResult (*create_provider)() noexcept;
    // destroy_provider must complete all GPU work that can access Host resources
    // and destroy every imported external GPU object before returning.
    SpectraPluginResult (*destroy_provider)(void* provider) noexcept;
    SpectraPluginResult (*configure_dataset)(void* provider, const SpectraPluginDatasetConfiguration* configuration) noexcept;
    SpectraPluginResult (*configure_telemetry)(void* provider, const SpectraPluginTelemetryConfiguration* configuration) noexcept;
    SpectraPluginResult (*apply_parameters)(void* provider, const SpectraPluginParameterValue* values, std::uint64_t value_count) noexcept;
    SpectraPluginResult (*reset)(void* provider, std::uint64_t seed) noexcept;
    SpectraPluginResult (*step)(void* provider, double step_seconds, std::uint64_t step_count) noexcept;
    SpectraPluginResult (*publish_frame)(void* provider, std::uint64_t simulation_step, const SpectraPluginFrameSink* sink) noexcept;
    SpectraPluginPresentationTickResult (*tick_presentation)(void* provider, double elapsed_seconds) noexcept;
};

static_assert(sizeof(SpectraPluginTriangleMeshDatasetDescriptor) == 16);
static_assert(sizeof(SpectraPluginSphereSetDatasetDescriptor) == 4);
static_assert(sizeof(SpectraPluginTelemetryDescriptor) == 72);
static_assert(offsetof(SpectraPluginTelemetryDescriptor, plot) == 68);
static_assert(sizeof(SpectraPluginPresentationTickResult) == 24);
static_assert(offsetof(SpectraPluginPresentationTickResult, dirty) == 16);
static_assert(sizeof(SpectraPluginDatasetCommit) == 48);
static_assert(offsetof(SpectraPluginDatasetCommit, signal_value) == 16);
static_assert(sizeof(SpectraPluginFrameSink) == 32);
static_assert(sizeof(SpectraPluginApi) == 88);

extern "C" {
SPECTRA_PLUGIN_EXPORT const SpectraPluginApi* spectra_plugin_api_21() noexcept;
}
