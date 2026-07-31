#pragma once

#include <cstdint>

inline constexpr std::uint32_t SPECTRA_PLUGIN_API_VERSION = 5;
inline constexpr char SPECTRA_PLUGIN_ENTRY_NAME[] = "spectra_plugin_api_5";

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

enum class SpectraPluginMeshUpdateMode : std::uint32_t {
    Static,
    Deformable,
    TopologyChanging,
};

enum class SpectraPluginPrimitiveKind : std::uint32_t {
    Geometry,
    ParticleSet,
};

enum class SpectraPluginEmissionSidedness : std::uint32_t {
    Front,
    Both,
};

enum class SpectraPluginFilterKind : std::uint32_t {
    Box,
    Gaussian,
    Mitchell,
    Sinc,
    Triangle,
};

enum class SpectraPluginSamplerKind : std::uint32_t {
    Independent,
    Stratified,
    Halton,
    Sobol,
    PaddedSobol,
    ZSobol,
    Pmj02bn,
};

struct SpectraPluginFilter {
    SpectraPluginFilterKind kind;
    SpectraPluginFloat2 radius;
    float sigma;
    float b;
    float c;
    float tau;
};

struct SpectraPluginSceneWriter {
    void* state;
    std::uint64_t (*create_diffuse_material)(
        void* state,
        SpectraPluginFloat3 reflectance);
    std::uint64_t (*create_diffuse_area_light)(
        void* state,
        SpectraPluginFloat3 radiance,
        SpectraPluginEmissionSidedness sidedness);
    std::uint64_t (*create_triangle_mesh)(
        void* state,
        const SpectraPluginFloat3* positions,
        std::uint64_t position_count,
        const SpectraPluginFloat3* normals,
        std::uint64_t normal_count,
        const SpectraPluginFloat3* tangents,
        std::uint64_t tangent_count,
        const SpectraPluginFloat2* texture_coordinates,
        std::uint64_t texture_coordinate_count,
        const std::uint32_t* indices,
        std::uint64_t index_count,
        SpectraPluginMeshUpdateMode update_mode);
    std::uint64_t (*create_prototype)(
        void* state,
        SpectraPluginPrimitiveKind kind,
        std::uint64_t resource,
        std::uint64_t material,
        std::uint64_t area_light,
        SpectraPluginTransform primitive_transform);
    std::uint64_t (*create_particle_set)(
        void* state,
        const SpectraPluginFloat3* positions,
        std::uint64_t position_count,
        const float* radii,
        const SpectraPluginFloat3* velocities,
        std::uint64_t velocity_count,
        const SpectraPluginFloat3* colors,
        std::uint64_t color_count,
        const float* temperatures,
        std::uint64_t temperature_count,
        std::uint64_t material,
        const std::uint64_t* particle_materials,
        std::uint64_t particle_material_count,
        SpectraPluginMeshUpdateMode update_mode);
    std::uint64_t (*create_instance)(
        void* state,
        std::uint64_t prototype,
        SpectraPluginTransform transform);
    std::uint64_t (*define_perspective_camera)(
        void* state,
        SpectraPluginTransform transform,
        float vertical_fov,
        float near_plane,
        float far_plane);
    std::uint64_t (*define_rgb_film)(
        void* state,
        std::uint32_t width,
        std::uint32_t height,
        SpectraPluginFilter filter);
    std::uint64_t (*define_sampler)(
        void* state,
        SpectraPluginSamplerKind kind,
        std::uint32_t samples_per_pixel,
        std::uint32_t seed);
    void (*update_triangle_mesh)(
        void* state,
        std::uint64_t mesh,
        const SpectraPluginFloat3* positions,
        std::uint64_t position_count,
        const SpectraPluginFloat3* normals,
        std::uint64_t normal_count,
        const SpectraPluginFloat3* tangents,
        std::uint64_t tangent_count,
        const SpectraPluginFloat2* texture_coordinates,
        std::uint64_t texture_coordinate_count,
        const std::uint32_t* indices,
        std::uint64_t index_count);
    void (*update_particle_set)(
        void* state,
        std::uint64_t particles,
        const SpectraPluginFloat3* positions,
        std::uint64_t position_count,
        const float* radii,
        const SpectraPluginFloat3* velocities,
        std::uint64_t velocity_count,
        const SpectraPluginFloat3* colors,
        std::uint64_t color_count,
        const float* temperatures,
        std::uint64_t temperature_count,
        const std::uint64_t* particle_materials,
        std::uint64_t particle_material_count);
    void (*update_transform)(
        void* state,
        std::uint64_t instance,
        SpectraPluginTransform transform);
    void (*update_diffuse_material)(
        void* state,
        std::uint64_t material,
        SpectraPluginFloat3 reflectance);
};

struct SpectraPluginControls {
    std::uint32_t running;
    std::uint32_t can_start;
    std::uint32_t can_stop;
    std::uint32_t can_advance;
};

struct SpectraPluginTimeline {
    double seconds;
    std::uint64_t frame;
};

struct SpectraPluginApi {
    std::uint32_t version;
    std::uint32_t size;
    const char* name;
    void* (*load)(const SpectraPluginSceneWriter* writer);
    void (*unload)(void* instance);
    void (*start)(void* instance);
    void (*stop)(void* instance);
    void (*advance)(void* instance, double seconds);
    SpectraPluginControls (*controls)(const void* instance);
    SpectraPluginTimeline (*timeline)(const void* instance);
};
