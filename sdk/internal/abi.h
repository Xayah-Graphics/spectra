#ifndef SPECTRA_SDK_INTERNAL_ABI_H
#define SPECTRA_SDK_INTERNAL_ABI_H

#include <cstdint>

inline constexpr std::uint32_t SPECTRA_SDK_ABI_VERSION = 3;
inline constexpr char SPECTRA_SDK_ENTRY_NAME[]         = "spectra_sdk_api_3";
inline constexpr std::uint32_t SPECTRA_SDK_HASH_GRID_ENTRY_COUNT   = 2920448;
inline constexpr std::uint32_t SPECTRA_SDK_DENSITY_INPUT_COUNT     = 64 * 32;
inline constexpr std::uint32_t SPECTRA_SDK_DENSITY_OUTPUT_COUNT    = 16 * 64;
inline constexpr std::uint32_t SPECTRA_SDK_RGB_INPUT_COUNT         = 64 * 32;
inline constexpr std::uint32_t SPECTRA_SDK_RGB_HIDDEN_COUNT        = 64 * 64;
inline constexpr std::uint32_t SPECTRA_SDK_RGB_OUTPUT_COUNT        = 16 * 64;
inline constexpr std::uint32_t SPECTRA_SDK_OCCUPANCY_WORD_COUNT    = 128 * 128 * 128 / 32;

#if defined(_WIN32)
#define SPECTRA_SDK_EXPORT __declspec(dllexport)
#else
#define SPECTRA_SDK_EXPORT __attribute__((visibility("default")))
#endif

struct SpectraSdkString {
    const char* data;
    std::uint64_t size;
};

struct SpectraSdkResult {
    SpectraSdkString error;
};

enum class SpectraSdkValueKind : std::uint32_t {
    Boolean,
    Integer,
    Float,
    Float3,
    Enumeration,
};

enum class SpectraSdkParameterApplication : std::uint32_t {
    Live,
    Reset,
    Recreate,
};

struct SpectraSdkValue {
    SpectraSdkValueKind kind;
    std::int64_t integer;
    double floating[3];
};

struct SpectraSdkParameterDescriptor {
    SpectraSdkString id;
    SpectraSdkString name;
    SpectraSdkString unit;
    SpectraSdkString description;
    SpectraSdkString section;
    SpectraSdkParameterApplication application;
    SpectraSdkValue default_value;
    SpectraSdkValue minimum;
    SpectraSdkValue maximum;
    SpectraSdkValue step;
    const SpectraSdkString* enumerators;
    std::uint64_t enumerator_count;
};

enum class SpectraSdkOutputKind : std::uint32_t {
    Mesh,
    Spheres,
    Volume,
    Instances,
    Points,
    Lines,
    Vectors,
    Image,
    HashGridRadianceField,
    Cameras,
    Metrics,
};

struct SpectraSdkCamera {
    float right[3];
    float down[3];
    float forward[3];
    float position[3];
    float focal[2];
    float principal[2];
};

enum class SpectraSdkVolumeFieldKind : std::uint32_t {
    Float,
    Float3,
    MacFloat3,
};

enum class SpectraSdkVolumeFieldSampling : std::uint32_t {
    Cell,
    Vertex,
};

enum class SpectraSdkVolumeVectorSpace : std::uint32_t {
    Grid,
    Local,
    World,
};

enum class SpectraSdkMeshAttribute : std::uint32_t {
    Normal            = 1u << 0u,
    Tangent           = 1u << 1u,
    TextureCoordinate = 1u << 2u,
    Color             = 1u << 3u,
    Scalar            = 1u << 4u,
};

struct SpectraSdkVolumeFieldDescriptor {
    SpectraSdkString id;
    SpectraSdkString name;
    SpectraSdkString unit;
    SpectraSdkVolumeFieldKind kind;
    SpectraSdkVolumeFieldSampling sampling;
    SpectraSdkVolumeVectorSpace vector_space;
};

struct SpectraSdkOutputDescriptor {
    SpectraSdkString id;
    SpectraSdkOutputKind kind;
    std::uint32_t mesh_attributes;
    const SpectraSdkVolumeFieldDescriptor* volume_fields;
    std::uint64_t volume_field_count;
};

struct SpectraSdkMetricDescriptor {
    SpectraSdkString id;
    SpectraSdkString name;
    SpectraSdkString unit;
    SpectraSdkString section;
    SpectraSdkValueKind kind;
    std::uint8_t plot;
};

struct SpectraSdkProviderDescriptor {
    SpectraSdkString id;
    const SpectraSdkParameterDescriptor* parameters;
    std::uint64_t parameter_count;
    const SpectraSdkOutputDescriptor* outputs;
    std::uint64_t output_count;
    const SpectraSdkMetricDescriptor* metrics;
    std::uint64_t metric_count;
};

enum class SpectraSdkExternalHandleType : std::uint32_t {
    OpaqueWin32,
    OpaqueFileDescriptor,
};

struct SpectraSdkExternalHandle {
    SpectraSdkExternalHandleType type;
    std::uint64_t value;
};

struct SpectraSdkGpuBuffer {
    SpectraSdkExternalHandle memory;
    std::uint64_t byte_size;
};

struct SpectraSdkGpuSlot {
    const SpectraSdkGpuBuffer* buffers;
    std::uint64_t buffer_count;
};

struct SpectraSdkOutputLayout {
    std::uint64_t output_index;
    SpectraSdkOutputKind kind;
    std::uint32_t primary_capacity;
    std::uint32_t secondary_capacity;
    std::uint32_t resolution[3];
    std::uint32_t mesh_attributes;
    const SpectraSdkCamera* cameras;
    std::uint64_t camera_count;
};

struct SpectraSdkOutputConfiguration {
    const SpectraSdkGpuBuffer* static_buffers;
    std::uint64_t static_buffer_count;
    const SpectraSdkGpuSlot* slots;
    std::uint64_t slot_count;
};

struct SpectraSdkOutputRequest {
    SpectraSdkOutputConfiguration configuration;
    void* lifetime;
};

struct SpectraSdkSetupSink {
    void* context;
    SpectraSdkExternalHandle timeline_semaphore;
    std::uint32_t slot_count;
    SpectraSdkResult (*configure_output)(void* context, const SpectraSdkOutputLayout* layout, SpectraSdkOutputRequest* request) noexcept;
    void (*release_output)(void* lifetime) noexcept;
};

struct SpectraSdkOutputCommit {
    std::uint32_t active_count;
    std::uint32_t secondary_count;
};

struct SpectraSdkFrameCommit {
    std::uint32_t slot_index;
    std::uint64_t signal_value;
    const SpectraSdkOutputCommit* outputs;
};

struct SpectraSdkCreateInfo {
    SpectraSdkString assets;
    const SpectraSdkValue* parameters;
    std::uint8_t vulkan_device_uuid[16];
    std::uint8_t vulkan_device_luid[8];
    std::uint8_t vulkan_device_luid_valid;
    std::uint8_t reserved[3];
    std::uint32_t vulkan_device_node_mask;
};

struct SpectraSdkProviderDescriptionResult {
    SpectraSdkResult result;
    SpectraSdkProviderDescriptor descriptor;
};

struct SpectraSdkProviderCreateResult {
    SpectraSdkResult result;
    void* provider;
};

struct SpectraSdkApi {
    std::uint32_t abi_version;
    std::uint32_t struct_size;
    SpectraSdkProviderDescriptionResult (*describe_provider)() noexcept;
    SpectraSdkProviderCreateResult (*create_provider)(const SpectraSdkCreateInfo* info) noexcept;
    SpectraSdkResult (*destroy_provider)(void* provider) noexcept;
    SpectraSdkResult (*setup)(void* provider, const SpectraSdkSetupSink* sink) noexcept;
    SpectraSdkResult (*apply_parameters)(void* provider, const SpectraSdkValue* values) noexcept;
    SpectraSdkResult (*reset)(void* provider, std::uint64_t seed) noexcept;
    SpectraSdkResult (*step)(void* provider, double step_seconds, std::uint64_t step_count) noexcept;
    SpectraSdkResult (*publish)(void* provider, SpectraSdkFrameCommit* commit) noexcept;
};

static_assert(sizeof(SpectraSdkValue) == 40);
static_assert(sizeof(SpectraSdkCamera) == 64);
static_assert(sizeof(SpectraSdkOutputCommit) == 8);
static_assert(sizeof(SpectraSdkApi) == 72);

#endif
