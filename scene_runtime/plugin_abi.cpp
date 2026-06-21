module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.scene_runtime;

import std;
import spectra.scene;

namespace spectra::scene_runtime {
    namespace {
        constexpr std::uint32_t plugin_abi_version = 26u;
        constexpr std::string_view scene_api_name = "spectra.dynamic_scene.scene";
        constexpr std::string_view controls_api_name = "spectra.dynamic_scene.controls";
        constexpr std::uint32_t scene_api_version = 1u;
        constexpr std::uint32_t controls_api_version = 1u;

        typedef void SpectraDynamicSceneInstance;

        typedef std::uint32_t SpectraDynamicSceneResult;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1u;

        struct SpectraDynamicSceneOption {
            const char* key{};
            const char* value{};
        };

        struct SpectraDynamicSceneOptionSpan {
            const SpectraDynamicSceneOption* data{};
            std::uint64_t count{};
        };

        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_TEXT = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_DIRECTORY_PATH = 1u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_FILE_PATH = 2u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_CHOICE = 3u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_BOOL = 4u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_FLOAT = 5u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_UNSIGNED_INTEGER = 6u;

        struct SpectraDynamicSceneOptionChoice {
            const char* value{};
            const char* label{};
        };

        struct SpectraDynamicSceneOptionChoiceSpan {
            const SpectraDynamicSceneOptionChoice* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneOptionSchema {
            const char* key{};
            const char* label{};
            const char* description{};
            std::uint32_t kind{};
            std::uint32_t required{};
            const char* default_value{};
            const char* group{};
            std::uint32_t advanced{};
            std::int32_t priority{};
            SpectraDynamicSceneOptionChoiceSpan choices{};
        };

        struct SpectraDynamicSceneOptionSchemaSpan {
            const SpectraDynamicSceneOptionSchema* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlAction {
            const char* id{};
            const char* label{};
            const char* description{};
            std::uint32_t group{};
            std::int32_t priority{};
            std::uint32_t style{};
            SpectraDynamicSceneOptionSchemaSpan options{};
        };

        struct SpectraDynamicSceneControlActionSpan {
            const SpectraDynamicSceneControlAction* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlSetting {
            const char* key{};
            const char* label{};
            const char* description{};
            std::uint32_t kind{};
            const char* value{};
            const char* group{};
            std::uint32_t advanced{};
            std::int32_t priority{};
            SpectraDynamicSceneOptionChoiceSpan choices{};
        };

        struct SpectraDynamicSceneControlSettingSpan {
            const SpectraDynamicSceneControlSetting* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlSettingView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlSettingSpan settings{};
        };

        struct SpectraDynamicSceneControlMetric {
            const char* key{};
            const char* label{};
            const char* value{};
            std::uint32_t placement_flags{};
            std::int32_t priority{};
            std::uint32_t has_color{};
            float color[4]{};
        };

        struct SpectraDynamicSceneControlMetricSpan {
            const SpectraDynamicSceneControlMetric* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlDisabledAction {
            const char* action_id{};
            const char* reason{};
        };

        struct SpectraDynamicSceneControlDisabledActionSpan {
            const SpectraDynamicSceneControlDisabledAction* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlStatusView {
            std::uint64_t struct_size{};
            const char* phase{};
            const char* headline{};
            const char* detail{};
            SpectraDynamicSceneControlMetricSpan metrics{};
            const char* const* enabled_action_ids{};
            std::uint64_t enabled_action_id_count{};
            SpectraDynamicSceneControlDisabledActionSpan disabled_actions{};
        };

        struct SpectraDynamicSceneControlLogEntry {
            std::uint64_t sequence{};
            const char* level{};
            const char* message{};
        };

        struct SpectraDynamicSceneControlLogEntrySpan {
            const SpectraDynamicSceneControlLogEntry* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlLogView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlLogEntrySpan entries{};
        };

        struct SpectraDynamicSceneControlImage {
            const char* id{};
            const char* label{};
            const char* description{};
            const std::uint8_t* rgba8{};
            std::uint64_t rgba8_size{};
            std::uint64_t revision{};
            std::uint32_t width{};
            std::uint32_t height{};
        };

        struct SpectraDynamicSceneControlImageSpan {
            const SpectraDynamicSceneControlImage* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlImageView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlImageSpan images{};
        };

        struct SpectraDynamicSceneControlScalarSample {
            std::uint64_t step{};
            double time_seconds{};
            double value{};
        };

        struct SpectraDynamicSceneControlScalarSampleSpan {
            const SpectraDynamicSceneControlScalarSample* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlScalarSeries {
            const char* id{};
            const char* label{};
            const char* description{};
            const char* unit{};
            float color[4]{};
            std::uint32_t group{};
            std::int32_t priority{};
            std::uint64_t revision{};
            SpectraDynamicSceneControlScalarSampleSpan samples{};
        };

        struct SpectraDynamicSceneControlScalarSeriesSpan {
            const SpectraDynamicSceneControlScalarSeries* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlScalarSeriesView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlScalarSeriesSpan series{};
        };

        struct SpectraDynamicSceneControlSnapshotView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlSettingView settings{};
            SpectraDynamicSceneControlStatusView status{};
            SpectraDynamicSceneControlLogView logs{};
            SpectraDynamicSceneControlImageView images{};
            SpectraDynamicSceneControlScalarSeriesView scalar_series{};
        };

        struct SpectraDynamicSceneUpdateInfo {
            std::uint64_t struct_size{};
            double wall_delta_seconds{};
            double scene_delta_seconds{};
            double time_seconds{};
            std::uint64_t frame_index{};
            std::uint32_t timeline_mode{};
            std::uint32_t timeline_playing{};
        };

        struct SpectraDynamicSceneGpuDeviceIdentity {
            std::uint32_t vendor_id{};
            std::uint32_t device_id{};
            std::uint8_t device_uuid[16]{};
            std::uint8_t device_luid[8]{};
            std::uint32_t device_node_mask{};
        };

        struct SpectraDynamicSceneViewportVoxelBufferRequest {
            std::uint64_t struct_size{};
            std::uint64_t byte_size{};
            const char* debug_name{};
        };

        struct SpectraDynamicSceneViewportVoxelBufferAllocation {
            std::uint64_t struct_size{};
            std::uint64_t resource_id{};
            std::uint64_t byte_size{};
            std::uint32_t handle_kind{};
            std::uintptr_t handle{};
            SpectraDynamicSceneGpuDeviceIdentity device_identity{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneRequestViewportVoxelBufferFn)(void* user_data, const SpectraDynamicSceneViewportVoxelBufferRequest* request, SpectraDynamicSceneViewportVoxelBufferAllocation* allocation);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneReleaseViewportVoxelBufferFn)(void* user_data, std::uint64_t resource_id);

        struct SpectraDynamicSceneVolumeBufferRequest {
            std::uint64_t struct_size{};
            std::uint64_t byte_size{};
            const char* debug_name{};
        };

        struct SpectraDynamicSceneVolumeBufferAllocation {
            std::uint64_t struct_size{};
            std::uint64_t resource_id{};
            std::uint64_t byte_size{};
            std::uint32_t handle_kind{};
            std::uintptr_t handle{};
            SpectraDynamicSceneGpuDeviceIdentity device_identity{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneRequestVolumeBufferFn)(void* user_data, const SpectraDynamicSceneVolumeBufferRequest* request, SpectraDynamicSceneVolumeBufferAllocation* allocation);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneReleaseVolumeBufferFn)(void* user_data, std::uint64_t resource_id);
        typedef const char* (*SpectraDynamicSceneHostLastErrorFn)(void* user_data);

        struct SpectraDynamicSceneHostServices {
            std::uint64_t struct_size{};
            void* user_data{};
            SpectraDynamicSceneRequestViewportVoxelBufferFn request_viewport_voxel_buffer{};
            SpectraDynamicSceneReleaseViewportVoxelBufferFn release_viewport_voxel_buffer{};
            SpectraDynamicSceneRequestVolumeBufferFn request_volume_buffer{};
            SpectraDynamicSceneReleaseVolumeBufferFn release_volume_buffer{};
            SpectraDynamicSceneHostLastErrorFn last_error{};
        };

        struct SpectraDynamicSceneOpenInfo {
            std::uint64_t struct_size{};
            const char* plugin_path{};
            SpectraDynamicSceneOptionSpan options{};
            const SpectraDynamicSceneHostServices* host_services{};
        };

        struct SpectraDynamicSceneTransform {
            float position[3]{};
            float rotation[4]{};
            float scale[3]{};
        };

        struct SpectraDynamicSceneMaterial {
            const char* name{};
            const char* model{};
            const char* alpha_mode{};
            float base_color[4]{};
            float emission_color[3]{};
            float emission_strength{};
            float roughness{};
            float metallic{};
            float alpha_cutoff{};
            float volume_density_scale{};
            float volume_temperature_scale{};
        };

        struct SpectraDynamicSceneMaterialSpan {
            const SpectraDynamicSceneMaterial* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneLight {
            const char* name{};
            const char* kind{};
            SpectraDynamicSceneTransform transform{};
            float color[3]{};
            float intensity{};
            float cone_angle_degrees{};
        };

        struct SpectraDynamicSceneLightSpan {
            const SpectraDynamicSceneLight* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneCamera {
            const char* name{};
            const char* local_coordinate_system{};
            SpectraDynamicSceneTransform transform{};
            float target[3]{};
            float up[3]{};
            std::uint32_t projection{};
            float vertical_fov_degrees{};
            std::uint32_t image_width{};
            std::uint32_t image_height{};
            float fx{};
            float fy{};
            float cx{};
            float cy{};
            float near_plane{};
            float far_plane{};
        };

        struct SpectraDynamicSceneCameraSpan {
            const SpectraDynamicSceneCamera* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneMeshVertex {
            float position[3]{};
            float normal[3]{};
        };

        struct SpectraDynamicSceneMeshVertexSpan {
            const SpectraDynamicSceneMeshVertex* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneUInt32Span {
            const std::uint32_t* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneMesh {
            const char* name{};
            SpectraDynamicSceneMeshVertexSpan vertices{};
            SpectraDynamicSceneUInt32Span indices{};
            const char* material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneMeshSpan {
            const SpectraDynamicSceneMesh* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneSphere {
            const char* name{};
            float radius{};
            const char* material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneSphereSpan {
            const SpectraDynamicSceneSphere* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicScenePoint {
            float position[3]{};
            float normal[3]{};
            float color[4]{};
            float radius{};
        };

        struct SpectraDynamicScenePointSpan {
            const SpectraDynamicScenePoint* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicScenePointCloud {
            const char* name{};
            SpectraDynamicScenePointSpan points{};
            const char* material_name{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicScenePointCloudSpan {
            const SpectraDynamicScenePointCloud* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneFloatSpan {
            const float* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneVolumeChannel {
            const char* name{};
            std::uint32_t dimensions[3]{};
            SpectraDynamicSceneFloatSpan values{};
            std::uint32_t format{};
            std::uint32_t source_kind{};
            std::uint32_t index_encoding{};
            std::uint64_t buffer_id{};
            std::uintptr_t external_device_pointer{};
            std::uint64_t source_byte_size{};
            std::uint64_t revision{};
        };

        struct SpectraDynamicSceneVolumeChannelSpan {
            const SpectraDynamicSceneVolumeChannel* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneVolume {
            const char* name{};
            std::uint32_t dimensions[3]{};
            float origin[3]{};
            float voxel_size[3]{};
            SpectraDynamicSceneVolumeChannelSpan channels{};
            const char* material_name{};
        };

        struct SpectraDynamicSceneVolumeSpan {
            const SpectraDynamicSceneVolume* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneEntityRef {
            std::uint32_t kind{};
            const char* name{};
        };

        struct SpectraDynamicSceneViewportSegment {
            float start[3]{};
            float end[3]{};
        };

        struct SpectraDynamicSceneViewportSegmentSpan {
            const SpectraDynamicSceneViewportSegment* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneColor {
            float value[4]{};
        };

        struct SpectraDynamicSceneColorSpan {
            const SpectraDynamicSceneColor* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportSegmentSet {
            const char* name{};
            SpectraDynamicSceneEntityRef owner{};
            SpectraDynamicSceneViewportSegmentSpan segments{};
            SpectraDynamicSceneColorSpan colors{};
            SpectraDynamicSceneFloatSpan widths{};
            float width{};
            std::uint32_t width_mode{};
            std::uint32_t depth_mode{};
            SpectraDynamicSceneTransform transform{};
        };

        struct SpectraDynamicSceneViewportSegmentSetSpan {
            const SpectraDynamicSceneViewportSegmentSet* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportVoxelGrid {
            const char* name{};
            SpectraDynamicSceneEntityRef owner{};
            std::uint32_t dimensions[3]{};
            float origin[3]{};
            float voxel_size[3]{};
            SpectraDynamicSceneTransform transform{};
            float color[4]{};
            float cell_scale{};
            std::uint32_t depth_mode{};
            std::uint32_t source_kind{};
            std::uint32_t index_encoding{};
            std::uint64_t buffer_id{};
            std::uint64_t source_byte_size{};
            std::uint64_t index_count{};
            std::uint64_t revision{};
        };

        struct SpectraDynamicSceneViewportVoxelGridSpan {
            const SpectraDynamicSceneViewportVoxelGrid* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneViewportCameraVisualImage {
            const std::uint8_t* rgba8{};
            std::uint64_t rgba8_size{};
            std::uint64_t revision{};
            std::uint32_t width{};
            std::uint32_t height{};
            float tint[4]{};
        };

        struct SpectraDynamicSceneViewportCameraVisual {
            const char* name{};
            SpectraDynamicSceneEntityRef owner{};
            float color[4]{};
            float width{};
            std::uint32_t width_mode{};
            std::uint32_t depth_mode{};
            float visual_near{};
            float visual_far{};
            std::uint32_t has_image{};
            SpectraDynamicSceneViewportCameraVisualImage image{};
        };

        struct SpectraDynamicSceneViewportCameraVisualSpan {
            const SpectraDynamicSceneViewportCameraVisual* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneDebugAttachmentSet {
            SpectraDynamicSceneViewportSegmentSetSpan viewport_segment_sets{};
            SpectraDynamicSceneViewportVoxelGridSpan viewport_voxel_grids{};
            SpectraDynamicSceneViewportCameraVisualSpan viewport_camera_visuals{};
        };

        struct SpectraDynamicSceneDocumentView {
            std::uint64_t struct_size{};
            const char* default_coordinate_system{};
            const char* active_camera_name{};
            SpectraDynamicSceneCameraSpan cameras{};
            SpectraDynamicSceneMaterialSpan materials{};
            SpectraDynamicSceneLightSpan lights{};
            SpectraDynamicSceneMeshSpan meshes{};
            SpectraDynamicSceneSphereSpan spheres{};
            SpectraDynamicScenePointCloudSpan point_clouds{};
            SpectraDynamicSceneVolumeSpan volumes{};
            SpectraDynamicSceneDebugAttachmentSet debug_attachments{};
        };

        struct SpectraDynamicSceneFrameInfo {
            double delta_seconds{};
            double time_seconds{};
            std::uint64_t frame_index{};
        };

        struct SpectraDynamicSceneFrameView {
            std::uint64_t struct_size{};
            SpectraDynamicSceneMeshSpan meshes{};
            SpectraDynamicSceneSphereSpan spheres{};
            SpectraDynamicScenePointCloudSpan point_clouds{};
            SpectraDynamicSceneVolumeSpan volumes{};
            SpectraDynamicSceneCameraSpan cameras{};
            SpectraDynamicSceneDebugAttachmentSet debug_attachments{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneCreateFn)(const SpectraDynamicSceneOpenInfo* open_info, SpectraDynamicSceneInstance** instance);
        typedef void (*SpectraDynamicSceneDestroyFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneResetFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneUpdateFn)(SpectraDynamicSceneInstance* instance, const SpectraDynamicSceneUpdateInfo* update_info);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneDocumentFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneDocumentView* document);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneFrameFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneFrameInfo frame, SpectraDynamicSceneFrameView* snapshot);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneSceneRevisionFn)(SpectraDynamicSceneInstance* instance, std::uint64_t* revision);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneControlActionFn)(SpectraDynamicSceneInstance* instance, const char* action_id, SpectraDynamicSceneOptionSpan options);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneControlSettingUpdateFn)(SpectraDynamicSceneInstance* instance, const char* key, const char* value);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneControlSnapshotFn)(SpectraDynamicSceneInstance* instance, SpectraDynamicSceneControlSnapshotView* snapshot);
        typedef const char* (*SpectraDynamicSceneLastErrorFn)(SpectraDynamicSceneInstance* instance);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneGetApiFn)(const char* api_name, std::uint32_t api_version, const void** api);

        struct SpectraDynamicSceneSceneApi {
            std::uint64_t struct_size{};
            const char* base_pbrt_path{};
            double frames_per_second{};
            SpectraDynamicSceneCreateFn create{};
            SpectraDynamicSceneDestroyFn destroy{};
            SpectraDynamicSceneResetFn reset{};
            SpectraDynamicSceneUpdateFn update{};
            SpectraDynamicSceneDocumentFn document{};
            SpectraDynamicSceneFrameFn frame{};
            SpectraDynamicSceneLastErrorFn last_error{};
        };

        struct SpectraDynamicSceneControlsApi {
            std::uint64_t struct_size{};
            SpectraDynamicSceneControlActionSpan control_actions{};
            SpectraDynamicSceneSceneRevisionFn scene_revision{};
            SpectraDynamicSceneControlActionFn control_action{};
            SpectraDynamicSceneControlSettingUpdateFn control_setting_update{};
            SpectraDynamicSceneControlSnapshotFn control_snapshot{};
        };

        struct SpectraDynamicScenePlugin {
            std::uint32_t abi_version{};
            std::uint64_t struct_size{};
            const char* id{};
            const char* title{};
            const char* controls_panel_title{};
            const char* open_action_label{};
            const char* open_action_description{};
            SpectraDynamicSceneOptionSchemaSpan open_options{};
            SpectraDynamicSceneGetApiFn get_api{};
        };

        typedef const SpectraDynamicScenePlugin* (*SpectraDynamicScenePluginEntryFn)(void);

        [[nodiscard]] std::string lowercase_ascii(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
            return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
        }

        [[nodiscard]] std::string abi_string(const char* value, const std::string_view context, const bool allow_empty) {
            const std::string_view view = value == nullptr ? std::string_view{} : std::string_view{value};
            if (!allow_empty && view.empty()) throw std::runtime_error(std::format("{} must not be empty", context));
            return std::string{view};
        }

        template <typename Value>
        [[nodiscard]] std::span<const Value> abi_span(const Value* data, const std::uint64_t count, const std::string_view context) {
            if (count == 0u) return {};
            if (data == nullptr) throw std::runtime_error(std::format("{} data pointer is null", context));
            if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::format("{} item count is too large", context));
            return std::span<const Value>{data, static_cast<std::size_t>(count)};
        }

        template <typename Span>
            requires requires(const Span& span) {
                span.data;
                span.count;
            }
        [[nodiscard]] auto abi_span(const Span& span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] float finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] double finite_double(const double value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] scene::Vector3 make_vector3(const float (&value)[3], const std::string_view context) {
            return scene::Vector3{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
            };
        }

        [[nodiscard]] scene::Vector4 make_vector4(const float (&value)[4], const std::string_view context) {
            return scene::Vector4{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
                finite_float(value[3], std::format("{} w", context)),
            };
        }

        [[nodiscard]] scene::Transform make_transform(const SpectraDynamicSceneTransform& transform, const std::string_view context) {
            return scene::Transform{
                .position = make_vector3(transform.position, std::format("{} position", context)),
                .rotation = scene::Quaternion{
                    finite_float(transform.rotation[0], std::format("{} rotation x", context)),
                    finite_float(transform.rotation[1], std::format("{} rotation y", context)),
                    finite_float(transform.rotation[2], std::format("{} rotation z", context)),
                    finite_float(transform.rotation[3], std::format("{} rotation w", context)),
                },
                .scale = make_vector3(transform.scale, std::format("{} scale", context)),
            };
        }

        [[nodiscard]] scene::Scene::ViewportSegmentWidthMode viewport_segment_width_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentWidthMode::Screen;
            case 1u: return scene::Scene::ViewportSegmentWidthMode::World;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment width mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportSegmentDepthMode viewport_segment_depth_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentDepthMode::DepthTested;
            case 1u: return scene::Scene::ViewportSegmentDepthMode::AlwaysVisible;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment depth mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridSourceKind viewport_voxel_grid_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridSourceKind::IndexList;
            case 1u: return scene::Scene::ViewportVoxelGridSourceKind::Bitfield;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid source kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridIndexEncoding viewport_voxel_grid_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridIndexEncoding::Linear;
            case 1u: return scene::Scene::ViewportVoxelGridIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid index encoding {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelSourceKind volume_channel_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelSourceKind::Values;
            case 1u: return scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel source kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelIndexEncoding volume_channel_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelIndexEncoding::Linear;
            case 1u: return scene::Scene::VolumeChannelIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel index encoding {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelFormat volume_channel_format_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelFormat::Float32;
            case 1u: return scene::Scene::VolumeChannelFormat::Float32x3;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel format {}", context, value));
        }

        [[nodiscard]] std::uint32_t volume_channel_component_count(const scene::Scene::VolumeChannelFormat format) {
            switch (format) {
            case scene::Scene::VolumeChannelFormat::Float32: return 1u;
            case scene::Scene::VolumeChannelFormat::Float32x3: return 3u;
            }
            throw std::runtime_error("Unknown dynamic scene volume channel format");
        }

        [[nodiscard]] scene::Scene::SceneEntityKind scene_entity_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::SceneEntityKind::Mesh;
            case 1u: return scene::Scene::SceneEntityKind::Sphere;
            case 2u: return scene::Scene::SceneEntityKind::PointCloud;
            case 3u: return scene::Scene::SceneEntityKind::VolumeGrid;
            case 4u: return scene::Scene::SceneEntityKind::Camera;
            case 5u: return scene::Scene::SceneEntityKind::Light;
            }
            throw std::runtime_error(std::format("{} has invalid scene entity kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::SceneEntityRef make_entity_ref(const SpectraDynamicSceneEntityRef& entity, const std::string_view context) {
            return scene::Scene::SceneEntityRef{
                .kind = scene_entity_kind_from_u32(entity.kind, context),
                .name = abi_string(entity.name, std::format("{} name", context), false),
            };
        }

        [[nodiscard]] scene::Scene::PreviewSurfaceKind preview_surface_kind_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "lit_surface") return scene::Scene::PreviewSurfaceKind::LitSurface;
            if (value == "unlit_surface") return scene::Scene::PreviewSurfaceKind::UnlitSurface;
            if (value == "emissive_surface") return scene::Scene::PreviewSurfaceKind::EmissiveSurface;
            if (value == "volume") return scene::Scene::PreviewSurfaceKind::Volume;
            if (value == "point_sprite") return scene::Scene::PreviewSurfaceKind::PointGlyph;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid preview surface kind \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewAlphaMode preview_alpha_mode_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "opaque") return scene::Scene::PreviewAlphaMode::Opaque;
            if (value == "masked") return scene::Scene::PreviewAlphaMode::Masked;
            if (value == "blend") return scene::Scene::PreviewAlphaMode::Blend;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid alpha mode \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewLightKind light_kind_from_string(const std::string_view value, const std::string_view light_name) {
            if (value == "directional") return scene::Scene::PreviewLightKind::Directional;
            if (value == "point") return scene::Scene::PreviewLightKind::Point;
            if (value == "spot") return scene::Scene::PreviewLightKind::Spot;
            if (value == "area") return scene::Scene::PreviewLightKind::Area;
            if (value == "environment") return scene::Scene::PreviewLightKind::Environment;
            throw std::runtime_error(std::format("Dynamic scene light \"{}\" has invalid kind \"{}\"", light_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewMaterial make_material(const SpectraDynamicSceneMaterial& material) {
            const std::string name = abi_string(material.name, "Dynamic scene material name", false);
            return scene::Scene::PreviewMaterial{
                .name = name,
                .surface_kind = preview_surface_kind_from_string(abi_string(material.model, std::format("Dynamic scene material \"{}\" model", name), true), name),
                .alpha_mode = preview_alpha_mode_from_string(abi_string(material.alpha_mode, std::format("Dynamic scene material \"{}\" alpha mode", name), true), name),
                .base_color = make_vector4(material.base_color, std::format("Dynamic scene material \"{}\" base color", name)),
                .emission_color = make_vector3(material.emission_color, std::format("Dynamic scene material \"{}\" emission color", name)),
                .emission_strength = finite_float(material.emission_strength, std::format("Dynamic scene material \"{}\" emission strength", name)),
                .roughness = finite_float(material.roughness, std::format("Dynamic scene material \"{}\" roughness", name)),
                .metallic = finite_float(material.metallic, std::format("Dynamic scene material \"{}\" metallic", name)),
                .alpha_cutoff = finite_float(material.alpha_cutoff, std::format("Dynamic scene material \"{}\" alpha cutoff", name)),
                .volume_density_scale = finite_float(material.volume_density_scale, std::format("Dynamic scene material \"{}\" volume density scale", name)),
                .volume_temperature_scale = finite_float(material.volume_temperature_scale, std::format("Dynamic scene material \"{}\" volume temperature scale", name)),
            };
        }

        [[nodiscard]] scene::Scene::PreviewLight make_light(const SpectraDynamicSceneLight& light) {
            const std::string name = abi_string(light.name, "Dynamic scene light name", false);
            return scene::Scene::PreviewLight{
                .name = name,
                .kind = light_kind_from_string(abi_string(light.kind, std::format("Dynamic scene light \"{}\" kind", name), true), name),
                .transform = make_transform(light.transform, std::format("Dynamic scene light \"{}\"", name)),
                .color = make_vector3(light.color, std::format("Dynamic scene light \"{}\" color", name)),
                .intensity = finite_float(light.intensity, std::format("Dynamic scene light \"{}\" intensity", name)),
                .cone_angle_degrees = finite_float(light.cone_angle_degrees, std::format("Dynamic scene light \"{}\" cone angle", name)),
            };
        }

        [[nodiscard]] scene::CameraProjection camera_projection(const SpectraDynamicSceneCamera& camera, const std::string& name) {
            scene::CameraProjection projection{
                .near_plane = finite_float(camera.near_plane, std::format("Dynamic scene camera \"{}\" near plane", name)),
                .far_plane = finite_float(camera.far_plane, std::format("Dynamic scene camera \"{}\" far plane", name)),
            };
            switch (camera.projection) {
            case 0u:
                projection.kind = scene::CameraProjectionKind::Perspective;
                projection.vertical_fov_degrees = finite_float(camera.vertical_fov_degrees, std::format("Dynamic scene camera \"{}\" vertical fov", name));
                return projection;
            case 1u:
                projection.kind = scene::CameraProjectionKind::Pinhole;
                projection.image_width = camera.image_width;
                projection.image_height = camera.image_height;
                projection.fx = finite_float(camera.fx, std::format("Dynamic scene camera \"{}\" fx", name));
                projection.fy = finite_float(camera.fy, std::format("Dynamic scene camera \"{}\" fy", name));
                projection.cx = finite_float(camera.cx, std::format("Dynamic scene camera \"{}\" cx", name));
                projection.cy = finite_float(camera.cy, std::format("Dynamic scene camera \"{}\" cy", name));
                return projection;
            }
            throw std::runtime_error(std::format("Dynamic scene camera \"{}\" has invalid projection {}", name, camera.projection));
        }

        [[nodiscard]] scene::Scene::Camera make_camera(const SpectraDynamicSceneCamera& camera) {
            const std::string name = abi_string(camera.name, "Dynamic scene camera name", false);
            const std::string local_coordinate_system_name = abi_string(camera.local_coordinate_system, std::format("Dynamic scene camera \"{}\" local coordinate system", name), false);
            const scene::Transform transform = make_transform(camera.transform, std::format("Dynamic scene camera \"{}\"", name));
            const scene::Vector3 target = make_vector3(camera.target, std::format("Dynamic scene camera \"{}\" target", name));
            const scene::Vector3 up = make_vector3(camera.up, std::format("Dynamic scene camera \"{}\" up", name));
            return scene::Scene::Camera{
                .name = name,
                .view = scene::CameraViewState{
                    .pose = scene::CameraPose{
                        .position = transform.position,
                        .orientation = scene::normalized_quaternion(transform.rotation, std::format("Dynamic scene camera \"{}\" orientation", name)),
                        .local_convention = scene::coordinate_system(local_coordinate_system_name).convention,
                    },
                    .focus = target,
                    .navigation_up = scene::normalize(up, std::format("Dynamic scene camera \"{}\" up", name)),
                    .projection = camera_projection(camera, name),
                },
            };
        }

        [[nodiscard]] scene::Scene::Mesh make_mesh(const SpectraDynamicSceneMesh& mesh, const bool dynamic) {
            const std::string name = abi_string(mesh.name, "Dynamic scene mesh name", false);
            scene::Scene::Mesh result{
                .name = name,
                .material_name = abi_string(mesh.material_name, std::format("Dynamic scene mesh \"{}\" material name", name), false),
                .transform = make_transform(mesh.transform, std::format("Dynamic scene mesh \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicSceneMeshVertex> vertices = abi_span(mesh.vertices.data, mesh.vertices.count, std::format("Dynamic scene mesh \"{}\" vertices", name));
            result.positions.reserve(vertices.size());
            result.normals.reserve(vertices.size());
            for (std::size_t index = 0u; index < vertices.size(); ++index) {
                result.positions.push_back(make_vector3(vertices[index].position, std::format("Dynamic scene mesh \"{}\" vertex #{} position", name, index)));
                result.normals.push_back(make_vector3(vertices[index].normal, std::format("Dynamic scene mesh \"{}\" vertex #{} normal", name, index)));
            }
            const std::span<const std::uint32_t> indices = abi_span(mesh.indices.data, mesh.indices.count, std::format("Dynamic scene mesh \"{}\" indices", name));
            result.indices.assign(indices.begin(), indices.end());
            if (result.positions.empty()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain vertices", name));
            if (result.indices.empty() || result.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain triangle indices", name));
            for (const std::uint32_t index : result.indices)
                if (index >= result.positions.size()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" contains an out-of-range vertex index", name));
            return result;
        }

        [[nodiscard]] scene::Scene::Sphere make_sphere(const SpectraDynamicSceneSphere& sphere, const bool dynamic) {
            const std::string name = abi_string(sphere.name, "Dynamic scene sphere name", false);
            const float radius = finite_float(sphere.radius, std::format("Dynamic scene sphere \"{}\" radius", name));
            if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene sphere \"{}\" radius must be positive", name));
            return scene::Scene::Sphere{
                .name = name,
                .radius = radius,
                .material_name = abi_string(sphere.material_name, std::format("Dynamic scene sphere \"{}\" material name", name), false),
                .transform = make_transform(sphere.transform, std::format("Dynamic scene sphere \"{}\"", name)),
                .dynamic = dynamic,
            };
        }

        [[nodiscard]] scene::Scene::PointCloud make_point_cloud(const SpectraDynamicScenePointCloud& point_cloud, const bool dynamic) {
            const std::string name = abi_string(point_cloud.name, "Dynamic scene point cloud name", false);
            scene::Scene::PointCloud result{
                .name = name,
                .material_name = abi_string(point_cloud.material_name, std::format("Dynamic scene point cloud \"{}\" material name", name), false),
                .transform = make_transform(point_cloud.transform, std::format("Dynamic scene point cloud \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicScenePoint> points = abi_span(point_cloud.points.data, point_cloud.points.count, std::format("Dynamic scene point cloud \"{}\" points", name));
            result.positions.reserve(points.size());
            result.normals.reserve(points.size());
            result.colors.reserve(points.size());
            result.radii.reserve(points.size());
            for (std::size_t index = 0u; index < points.size(); ++index) {
                result.positions.push_back(make_vector3(points[index].position, std::format("Dynamic scene point cloud \"{}\" point #{} position", name, index)));
                result.normals.push_back(make_vector3(points[index].normal, std::format("Dynamic scene point cloud \"{}\" point #{} normal", name, index)));
                result.colors.push_back(make_vector4(points[index].color, std::format("Dynamic scene point cloud \"{}\" point #{} color", name, index)));
                const float radius = finite_float(points[index].radius, std::format("Dynamic scene point cloud \"{}\" point #{} radius", name, index));
                if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene point cloud \"{}\" point #{} radius must be positive", name, index));
                result.radii.push_back(radius);
            }
            return result;
        }

        [[nodiscard]] scene::Scene::VolumeGrid make_volume(const SpectraDynamicSceneVolume& volume, const bool dynamic) {
            const std::string name = abi_string(volume.name, "Dynamic scene volume name", false);
            scene::Scene::VolumeGrid result{
                .name = name,
                .dimensions = {volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]},
                .origin = make_vector3(volume.origin, std::format("Dynamic scene volume \"{}\" origin", name)),
                .voxel_size = make_vector3(volume.voxel_size, std::format("Dynamic scene volume \"{}\" voxel size", name)),
                .material_name = abi_string(volume.material_name, std::format("Dynamic scene volume \"{}\" material name", name), false),
                .dynamic = dynamic,
            };
            if (result.dimensions[0] == 0u || result.dimensions[1] == 0u || result.dimensions[2] == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" dimensions must be positive", name));
            const std::span<const SpectraDynamicSceneVolumeChannel> channels = abi_span(volume.channels.data, volume.channels.count, std::format("Dynamic scene volume \"{}\" channels", name));
            for (const SpectraDynamicSceneVolumeChannel& channel : channels) {
                const std::string channel_name = abi_string(channel.name, std::format("Dynamic scene volume \"{}\" channel name", name), false);
                scene::Scene::VolumeChannel converted{
                    .name = channel_name,
                    .dimensions = {channel.dimensions[0], channel.dimensions[1], channel.dimensions[2]},
                    .format = volume_channel_format_from_u32(channel.format, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .source_kind = volume_channel_source_kind_from_u32(channel.source_kind, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .index_encoding = volume_channel_index_encoding_from_u32(channel.index_encoding, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .buffer_id = channel.buffer_id,
                    .external_device_pointer = channel.external_device_pointer,
                    .source_byte_size = channel.source_byte_size,
                    .revision = channel.revision,
                };
                if (converted.dimensions != result.dimensions) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" dimensions do not match", name, converted.name));
                const std::uint64_t cell_count = static_cast<std::uint64_t>(converted.dimensions[0]) * static_cast<std::uint64_t>(converted.dimensions[1]) * static_cast<std::uint64_t>(converted.dimensions[2]);
                const std::uint32_t component_count = volume_channel_component_count(converted.format);
                if (cell_count > std::numeric_limits<std::uint64_t>::max() / component_count) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value count exceeds uint64 range", name, converted.name));
                const std::uint64_t expected_count = cell_count * component_count;
                const std::span<const float> values = abi_span(channel.values.data, channel.values.count, std::format("Dynamic scene volume \"{}\" channel \"{}\" values", name, converted.name));
                if (converted.source_kind == scene::Scene::VolumeChannelSourceKind::Values) {
                    if (expected_count != values.size()) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value count does not match dimensions", name, converted.name));
                    converted.values.assign(values.begin(), values.end());
                    for (std::size_t index = 0u; index < converted.values.size(); ++index)
                        if (!std::isfinite(converted.values[index])) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value #{} must be finite", name, converted.name, index));
                    if (converted.name == "color")
                        for (std::size_t index = 0u; index < converted.values.size(); ++index)
                            if (converted.values[index] < 0.0f) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" color channel value #{} must be non-negative", name, index));
                } else {
                    if (!values.empty()) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source must not provide CPU values", name, converted.name));
                    if (expected_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" byte count exceeds uint64 range", name, converted.name));
                    if (converted.source_byte_size < expected_count * sizeof(float)) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source byte size is too small", name, converted.name));
                    if (converted.buffer_id == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source has no buffer id", name, converted.name));
                    if (converted.external_device_pointer == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source has no device pointer for pathtracer snapshot", name, converted.name));
                    if (converted.revision == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source revision must not be zero", name, converted.name));
                }
                result.channels.push_back(std::move(converted));
            }
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportCameraVisualImage make_viewport_camera_visual_image(const SpectraDynamicSceneViewportCameraVisualImage& image, const std::string& name) {
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image dimensions must be non-zero", name));
            const std::uint64_t expected_byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
            if (image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image byte count must be width * height * 4", name));
            scene::Scene::ViewportCameraVisualImage result{
                .width = image.width,
                .height = image.height,
                .rgba8 = image.rgba8,
                .rgba8_size = image.rgba8_size,
                .revision = image.revision,
                .tint = make_vector4(image.tint, std::format("Dynamic scene viewport camera visual \"{}\" image tint", name)),
            };
            static_cast<void>(abi_span(image.rgba8, image.rgba8_size, std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image", name)));
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportCameraVisual make_viewport_camera_visual(const SpectraDynamicSceneViewportCameraVisual& visual, const bool dynamic) {
            const std::string name = abi_string(visual.name, "Dynamic scene viewport camera visual name", false);
            scene::Scene::ViewportCameraVisual result{
                .name = name,
                .owner = make_entity_ref(visual.owner, std::format("Dynamic scene viewport camera visual \"{}\" owner", name)),
                .color = make_vector4(visual.color, std::format("Dynamic scene viewport camera visual \"{}\" color", name)),
                .width = finite_float(visual.width, std::format("Dynamic scene viewport camera visual \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(visual.width_mode, std::format("Dynamic scene viewport camera visual \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(visual.depth_mode, std::format("Dynamic scene viewport camera visual \"{}\"", name)),
                .visual_near = finite_float(visual.visual_near, std::format("Dynamic scene viewport camera visual \"{}\" near", name)),
                .visual_far = finite_float(visual.visual_far, std::format("Dynamic scene viewport camera visual \"{}\" far", name)),
                .dynamic = dynamic,
            };
            if (visual.has_image != 0u) result.image = make_viewport_camera_visual_image(visual.image, name);
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportSegmentSet make_viewport_segment_set(const SpectraDynamicSceneViewportSegmentSet& segment_set, const bool dynamic) {
            const std::string name = abi_string(segment_set.name, "Dynamic scene viewport segment set name", false);
            scene::Scene::ViewportSegmentSet result{
                .name = name,
                .owner = make_entity_ref(segment_set.owner, std::format("Dynamic scene viewport segment set \"{}\" owner", name)),
                .width = finite_float(segment_set.width, std::format("Dynamic scene viewport segment set \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(segment_set.width_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(segment_set.depth_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .transform = make_transform(segment_set.transform, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .dynamic = dynamic,
            };

            const std::span<const SpectraDynamicSceneViewportSegment> segments = abi_span(segment_set.segments, std::format("Dynamic scene viewport segment set \"{}\" segments", name));
            result.segments.reserve(segments.size());
            for (std::size_t index = 0u; index < segments.size(); ++index) {
                result.segments.push_back(scene::Scene::ViewportSegment{
                    .start = make_vector3(segments[index].start, std::format("Dynamic scene viewport segment set \"{}\" segment #{} start", name, index)),
                    .end = make_vector3(segments[index].end, std::format("Dynamic scene viewport segment set \"{}\" segment #{} end", name, index)),
                });
            }

            const std::span<const SpectraDynamicSceneColor> colors = abi_span(segment_set.colors, std::format("Dynamic scene viewport segment set \"{}\" colors", name));
            if (!colors.empty() && colors.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" color count does not match segment count", name));
            result.colors.reserve(colors.size());
            for (std::size_t index = 0u; index < colors.size(); ++index) result.colors.push_back(make_vector4(colors[index].value, std::format("Dynamic scene viewport segment set \"{}\" color #{}", name, index)));

            const std::span<const float> widths = abi_span(segment_set.widths.data, segment_set.widths.count, std::format("Dynamic scene viewport segment set \"{}\" widths", name));
            if (!widths.empty() && widths.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" width count does not match segment count", name));
            result.widths.reserve(widths.size());
            for (std::size_t index = 0u; index < widths.size(); ++index) result.widths.push_back(finite_float(widths[index], std::format("Dynamic scene viewport segment set \"{}\" width #{}", name, index)));
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGrid make_viewport_voxel_grid(const SpectraDynamicSceneViewportVoxelGrid& voxel_grid, const bool dynamic) {
            const std::string name = abi_string(voxel_grid.name, "Dynamic scene viewport voxel grid name", false);
            return scene::Scene::ViewportVoxelGrid{
                .name = name,
                .owner = make_entity_ref(voxel_grid.owner, std::format("Dynamic scene viewport voxel grid \"{}\" owner", name)),
                .dimensions = {voxel_grid.dimensions[0], voxel_grid.dimensions[1], voxel_grid.dimensions[2]},
                .origin = make_vector3(voxel_grid.origin, std::format("Dynamic scene viewport voxel grid \"{}\" origin", name)),
                .voxel_size = make_vector3(voxel_grid.voxel_size, std::format("Dynamic scene viewport voxel grid \"{}\" voxel size", name)),
                .transform = make_transform(voxel_grid.transform, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .color = make_vector4(voxel_grid.color, std::format("Dynamic scene viewport voxel grid \"{}\" color", name)),
                .cell_scale = finite_float(voxel_grid.cell_scale, std::format("Dynamic scene viewport voxel grid \"{}\" cell scale", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(voxel_grid.depth_mode, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .source_kind = viewport_voxel_grid_source_kind_from_u32(voxel_grid.source_kind, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .index_encoding = viewport_voxel_grid_index_encoding_from_u32(voxel_grid.index_encoding, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .buffer_id = voxel_grid.buffer_id,
                .source_byte_size = voxel_grid.source_byte_size,
                .index_count = voxel_grid.index_count,
                .revision = voxel_grid.revision,
                .dynamic = dynamic,
            };
        }

        template <typename Item>
        void require_unique_name(std::set<std::string>& names, const Item& item, const std::string_view kind) {
            if (item.name.empty()) throw std::runtime_error(std::format("Dynamic scene {} name must not be empty", kind));
            if (!names.insert(item.name).second) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" is duplicated", kind, item.name));
        }

        [[nodiscard]] std::set<std::string> collect_material_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewMaterial& material : document.materials) require_unique_name(names, material, "material");
            return names;
        }

        [[nodiscard]] std::set<std::string> collect_light_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewLight& light : document.lights) require_unique_name(names, light, "light");
            return names;
        }

        template <typename Primitive>
        void require_material_reference(const Primitive& primitive, const std::set<std::string>& material_names, const std::string_view kind) {
            if (primitive.material_name.empty()) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" material name must not be empty", kind, primitive.name));
            if (!material_names.contains(primitive.material_name)) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" references unknown material \"{}\"", kind, primitive.name, primitive.material_name));
        }

        [[nodiscard]] scene::Scene::DebugAttachmentSet make_debug_attachment_set(const SpectraDynamicSceneDebugAttachmentSet& attachments, const bool dynamic, const std::string_view context) {
            scene::Scene::DebugAttachmentSet result{};
            for (const SpectraDynamicSceneViewportSegmentSet& segment_set_view : abi_span(attachments.viewport_segment_sets, std::format("{} viewport segment sets", context)))
                result.viewport_segment_sets.push_back(make_viewport_segment_set(segment_set_view, dynamic));
            for (const SpectraDynamicSceneViewportVoxelGrid& voxel_grid_view : abi_span(attachments.viewport_voxel_grids, std::format("{} viewport voxel grids", context)))
                result.viewport_voxel_grids.push_back(make_viewport_voxel_grid(voxel_grid_view, dynamic));
            for (const SpectraDynamicSceneViewportCameraVisual& visual_view : abi_span(attachments.viewport_camera_visuals, std::format("{} viewport camera visuals", context)))
                result.viewport_camera_visuals.push_back(make_viewport_camera_visual(visual_view, dynamic));
            return result;
        }

        void append_document_view(scene::Scene::Document& document, const SpectraDynamicSceneDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneDocumentView)) throw std::runtime_error("Dynamic scene document view ABI size mismatch");
            const std::string coordinate_system_name = abi_string(view.default_coordinate_system, "Dynamic scene document default coordinate system", true);
            if (!coordinate_system_name.empty()) document.default_coordinate_system = scene::coordinate_system(coordinate_system_name);
            const std::string active_camera_name = abi_string(view.active_camera_name, "Dynamic scene document active camera name", true);
            if (!active_camera_name.empty()) document.active_camera_name = active_camera_name;
            for (const SpectraDynamicSceneCamera& camera_view : abi_span(view.cameras, "Dynamic scene document cameras"))
                document.cameras.push_back(make_camera(camera_view));
            for (const SpectraDynamicSceneMaterial& material_view : abi_span(view.materials, "Dynamic scene document materials")) {
                scene::Scene::PreviewMaterial material = make_material(material_view);
                require_unique_name(material_names, material, "material");
                document.materials.push_back(std::move(material));
            }
            for (const SpectraDynamicSceneLight& light_view : abi_span(view.lights, "Dynamic scene document lights")) {
                scene::Scene::PreviewLight light = make_light(light_view);
                require_unique_name(light_names, light, "light");
                document.lights.push_back(std::move(light));
            }
            for (const SpectraDynamicSceneMesh& mesh_view : abi_span(view.meshes, "Dynamic scene document meshes")) {
                scene::Scene::Mesh mesh = make_mesh(mesh_view, false);
                require_material_reference(mesh, material_names, "mesh");
                document.meshes.push_back(std::move(mesh));
            }
            for (const SpectraDynamicSceneSphere& sphere_view : abi_span(view.spheres, "Dynamic scene document spheres")) {
                scene::Scene::Sphere sphere = make_sphere(sphere_view, false);
                require_material_reference(sphere, material_names, "sphere");
                document.spheres.push_back(std::move(sphere));
            }
            for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_span(view.point_clouds, "Dynamic scene document point clouds")) {
                scene::Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, false);
                require_material_reference(point_cloud, material_names, "point cloud");
                document.point_clouds.push_back(std::move(point_cloud));
            }
            for (const SpectraDynamicSceneVolume& volume_view : abi_span(view.volumes, "Dynamic scene document volumes")) {
                scene::Scene::VolumeGrid volume = make_volume(volume_view, false);
                require_material_reference(volume, material_names, "volume");
                document.volumes.push_back(std::move(volume));
            }
            document.debug_attachments = make_debug_attachment_set(view.debug_attachments, false, "Dynamic scene document debug attachments");
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame_snapshot(const SpectraDynamicSceneFrameView& view, const scene::Scene::FrameInfo& frame, const std::set<std::string>& material_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneFrameView)) throw std::runtime_error("Dynamic scene frame view ABI size mismatch");
            scene::Scene::FrameSnapshot snapshot{.cursor = scene::Scene::make_frame_cursor(frame)};
            for (const SpectraDynamicSceneMesh& mesh_view : abi_span(view.meshes, "Dynamic scene frame meshes")) {
                scene::Scene::Mesh mesh = make_mesh(mesh_view, true);
                require_material_reference(mesh, material_names, "mesh");
                snapshot.meshes.push_back(std::move(mesh));
            }
            for (const SpectraDynamicSceneSphere& sphere_view : abi_span(view.spheres, "Dynamic scene frame spheres")) {
                scene::Scene::Sphere sphere = make_sphere(sphere_view, true);
                require_material_reference(sphere, material_names, "sphere");
                snapshot.spheres.push_back(std::move(sphere));
            }
            for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_span(view.point_clouds, "Dynamic scene frame point clouds")) {
                scene::Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, true);
                require_material_reference(point_cloud, material_names, "point cloud");
                snapshot.point_clouds.push_back(std::move(point_cloud));
            }
            for (const SpectraDynamicSceneVolume& volume_view : abi_span(view.volumes, "Dynamic scene frame volumes")) {
                scene::Scene::VolumeGrid volume = make_volume(volume_view, true);
                require_material_reference(volume, material_names, "volume");
                snapshot.volumes.push_back(std::move(volume));
            }
            for (const SpectraDynamicSceneCamera& camera_view : abi_span(view.cameras, "Dynamic scene frame cameras"))
                snapshot.cameras.push_back(make_camera(camera_view));
            snapshot.debug_attachments = make_debug_attachment_set(view.debug_attachments, true, "Dynamic scene frame debug attachments");
            return snapshot;
        }

        struct DynamicScenePluginOptionStorage {
            std::string key{};
            std::string value{};
        };

        struct DynamicScenePluginOpenRequestStorage {
            std::filesystem::path plugin_path{};
            std::vector<DynamicScenePluginOptionStorage> options{};
            std::vector<SpectraDynamicSceneOption> option_views{};
            std::shared_ptr<DynamicSceneHostServices> host_services{};
            SpectraDynamicSceneHostServices host_services_view{};
            std::string source_id{};
        };

        [[nodiscard]] std::uint32_t abi_gpu_resource_handle_kind(const DynamicSceneGpuResourceHandleKind kind) {
            switch (kind) {
            case DynamicSceneGpuResourceHandleKind::OpaqueWin32: return 1u;
            case DynamicSceneGpuResourceHandleKind::OpaqueFileDescriptor: return 2u;
            }
            throw std::runtime_error("Dynamic scene GPU resource handle kind is invalid");
        }

        [[nodiscard]] SpectraDynamicSceneGpuDeviceIdentity abi_gpu_device_identity(const DynamicSceneGpuDeviceIdentity& identity) {
            SpectraDynamicSceneGpuDeviceIdentity view{
                .vendor_id = identity.vendor_id,
                .device_id = identity.device_id,
                .device_node_mask = identity.device_node_mask,
            };
            for (std::size_t index = 0u; index < identity.device_uuid.size(); ++index) view.device_uuid[index] = identity.device_uuid[index];
            for (std::size_t index = 0u; index < identity.device_luid.size(); ++index) view.device_luid[index] = identity.device_luid[index];
            return view;
        }

        thread_local std::string dynamic_scene_host_service_callback_error{};

        [[nodiscard]] SpectraDynamicSceneResult request_viewport_voxel_buffer(void* user_data, const SpectraDynamicSceneViewportVoxelBufferRequest* request, SpectraDynamicSceneViewportVoxelBufferAllocation* allocation) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Dynamic scene viewport voxel buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Dynamic scene viewport voxel buffer allocation pointer is null");
                if (request->struct_size != sizeof(SpectraDynamicSceneViewportVoxelBufferRequest)) throw std::runtime_error("Dynamic scene viewport voxel buffer request ABI size mismatch");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                const DynamicSceneViewportVoxelBufferAllocation allocated = host_services.request_viewport_voxel_buffer(DynamicSceneViewportVoxelBufferRequest{
                    .byte_size = request->byte_size,
                    .debug_name = abi_string(request->debug_name, "Dynamic scene viewport voxel buffer debug name", true),
                });
                *allocation = SpectraDynamicSceneViewportVoxelBufferAllocation{
                    .struct_size = sizeof(SpectraDynamicSceneViewportVoxelBufferAllocation),
                    .resource_id = allocated.resource_id,
                    .byte_size = allocated.byte_size,
                    .handle_kind = abi_gpu_resource_handle_kind(allocated.handle_kind),
                    .handle = allocated.handle,
                    .device_identity = abi_gpu_device_identity(allocated.device_identity),
                };
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneResult release_viewport_voxel_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                host_services.release_viewport_voxel_buffer(resource_id);
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneResult request_volume_buffer(void* user_data, const SpectraDynamicSceneVolumeBufferRequest* request, SpectraDynamicSceneVolumeBufferAllocation* allocation) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Dynamic scene volume buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Dynamic scene volume buffer allocation pointer is null");
                if (request->struct_size != sizeof(SpectraDynamicSceneVolumeBufferRequest)) throw std::runtime_error("Dynamic scene volume buffer request ABI size mismatch");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                const DynamicSceneVolumeBufferAllocation allocated = host_services.request_volume_buffer(DynamicSceneVolumeBufferRequest{
                    .byte_size = request->byte_size,
                    .debug_name = abi_string(request->debug_name, "Dynamic scene volume buffer debug name", true),
                });
                *allocation = SpectraDynamicSceneVolumeBufferAllocation{
                    .struct_size = sizeof(SpectraDynamicSceneVolumeBufferAllocation),
                    .resource_id = allocated.resource_id,
                    .byte_size = allocated.byte_size,
                    .handle_kind = abi_gpu_resource_handle_kind(allocated.handle_kind),
                    .handle = allocated.handle,
                    .device_identity = abi_gpu_device_identity(allocated.device_identity),
                };
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraDynamicSceneResult release_volume_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                dynamic_scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Dynamic scene host services user data pointer is null");
                DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
                host_services.release_volume_buffer(resource_id);
                return SPECTRA_DYNAMIC_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                dynamic_scene_host_service_callback_error = error.what();
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            } catch (...) {
                dynamic_scene_host_service_callback_error = "unknown dynamic scene host service error";
                return SPECTRA_DYNAMIC_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] const char* dynamic_scene_host_services_last_error(void* user_data) noexcept {
            if (user_data == nullptr) return dynamic_scene_host_service_callback_error.c_str();
            DynamicSceneHostServices& host_services = *static_cast<DynamicSceneHostServices*>(user_data);
            const std::string_view service_error = host_services.last_error();
            thread_local std::string host_service_error_text{};
            if (!service_error.empty()) {
                host_service_error_text = service_error;
                return host_service_error_text.c_str();
            }
            return dynamic_scene_host_service_callback_error.c_str();
        }

        [[nodiscard]] SpectraDynamicSceneHostServices make_host_services_view(DynamicSceneHostServices& host_services) {
            return SpectraDynamicSceneHostServices{
                .struct_size = sizeof(SpectraDynamicSceneHostServices),
                .user_data = &host_services,
                .request_viewport_voxel_buffer = request_viewport_voxel_buffer,
                .release_viewport_voxel_buffer = release_viewport_voxel_buffer,
                .request_volume_buffer = request_volume_buffer,
                .release_volume_buffer = release_volume_buffer,
                .last_error = dynamic_scene_host_services_last_error,
            };
        }

        [[nodiscard]] bool parse_bool_default(const std::string_view value) {
            if (value == "true") return true;
            if (value == "false") return false;
            throw std::runtime_error("Dynamic scene open option bool default must be true or false");
        }

        [[nodiscard]] float parse_float_default(const std::string_view value, const std::string_view context) {
            float parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) throw std::runtime_error(std::format("{} must be a finite float", context));
            return parsed;
        }

        [[nodiscard]] std::uint64_t parse_unsigned_integer_default(const std::string_view value, const std::string_view context) {
            std::uint64_t parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error(std::format("{} must be an unsigned integer", context));
            return parsed;
        }

        [[nodiscard]] std::filesystem::path normalized_dynamic_scene_plugin_path(const std::filesystem::path& plugin_path) {
            if (plugin_path.empty()) throw std::runtime_error("Dynamic scene plugin path must not be empty");
            const std::string path_text = plugin_path.string();
            if (path_text.find('?') != std::string::npos) throw std::runtime_error("Dynamic scene plugin Scene URI query is not supported; open the plugin path and configure it in the Scene popover");
            const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a dynamic scene plugin library, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file does not exist", absolute_path.string()));
            if (!is_dynamic_scene_plugin_file(absolute_path)) throw std::runtime_error(std::format("{}: dynamic scene plugin file extension is not supported on this platform", absolute_path.string()));
            return absolute_path;
        }

        [[nodiscard]] std::uint64_t fnv1a64_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] std::string make_dynamic_scene_source_id(const std::filesystem::path& plugin_path, const std::vector<DynamicScenePluginOptionStorage>& options) {
            std::vector<DynamicScenePluginOptionStorage> sorted_options = options;
            std::ranges::sort(sorted_options, {}, &DynamicScenePluginOptionStorage::key);
            std::uint64_t hash = 14695981039346656037ull;
            hash = fnv1a64_append(hash, plugin_path.string());
            for (const DynamicScenePluginOptionStorage& option : sorted_options) {
                hash = fnv1a64_append(hash, "\n");
                hash = fnv1a64_append(hash, option.key);
                hash = fnv1a64_append(hash, "=");
                hash = fnv1a64_append(hash, option.value);
            }
            return std::format("{}#dynamic-open-{:016x}", plugin_path.string(), hash);
        }

        [[nodiscard]] DynamicScenePluginOpenRequestStorage make_plugin_open_request_storage(DynamicSceneOpenRequest request) {
            DynamicScenePluginOpenRequestStorage storage{
                .plugin_path = normalized_dynamic_scene_plugin_path(request.plugin_path),
            };
            if (request.host_services == nullptr) throw std::runtime_error("Dynamic scene open request requires host services");
            storage.host_services = std::move(request.host_services);
            storage.host_services_view = make_host_services_view(*storage.host_services);
            std::set<std::string> option_keys{};
            storage.options.reserve(request.options.size());
            for (DynamicSceneOption& option : request.options) {
                if (option.key.empty()) throw std::runtime_error("Dynamic scene open option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene open option '{}' is duplicated", option.key));
                storage.options.push_back(DynamicScenePluginOptionStorage{
                    .key = std::move(option.key),
                    .value = std::move(option.value),
                });
            }
            storage.source_id = make_dynamic_scene_source_id(storage.plugin_path, storage.options);
            storage.option_views.reserve(storage.options.size());
            for (const DynamicScenePluginOptionStorage& option : storage.options) {
                storage.option_views.push_back(SpectraDynamicSceneOption{
                    .key = option.key.c_str(),
                    .value = option.value.c_str(),
                });
            }
            return storage;
        }

        [[nodiscard]] DynamicSceneOptionKind make_open_option_kind(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
                case SPECTRA_DYNAMIC_SCENE_OPTION_TEXT: return DynamicSceneOptionKind::Text;
                case SPECTRA_DYNAMIC_SCENE_OPTION_DIRECTORY_PATH: return DynamicSceneOptionKind::DirectoryPath;
                case SPECTRA_DYNAMIC_SCENE_OPTION_FILE_PATH: return DynamicSceneOptionKind::FilePath;
                case SPECTRA_DYNAMIC_SCENE_OPTION_CHOICE: return DynamicSceneOptionKind::Choice;
                case SPECTRA_DYNAMIC_SCENE_OPTION_BOOL: return DynamicSceneOptionKind::Bool;
                case SPECTRA_DYNAMIC_SCENE_OPTION_FLOAT: return DynamicSceneOptionKind::Float;
                case SPECTRA_DYNAMIC_SCENE_OPTION_UNSIGNED_INTEGER: return DynamicSceneOptionKind::UnsignedInteger;
                default: throw std::runtime_error(std::format("{} has unknown kind {}", context, kind));
            }
        }

        [[nodiscard]] DynamicSceneOptionSchema make_open_option_schema(const SpectraDynamicSceneOptionSchema& schema, const std::string_view context) {
            DynamicSceneOptionSchema converted{
                .key = abi_string(schema.key, std::format("{} key", context), false),
                .label = abi_string(schema.label, std::format("{} label", context), false),
                .description = abi_string(schema.description, std::format("{} description", context), true),
                .kind = make_open_option_kind(schema.kind, context),
                .required = schema.required != 0u,
                .default_value = abi_string(schema.default_value, std::format("{} default value", context), true),
                .group = abi_string(schema.group, std::format("{} group", context), true),
                .advanced = schema.advanced != 0u,
                .priority = schema.priority,
            };
            if (schema.required != 0u && schema.required != 1u) throw std::runtime_error(std::format("{} required flag must be 0 or 1", context));
            if (schema.advanced != 0u && schema.advanced != 1u) throw std::runtime_error(std::format("{} advanced flag must be 0 or 1", context));
            const std::span<const SpectraDynamicSceneOptionChoice> choices = abi_span(schema.choices.data, schema.choices.count, std::format("{} choices", context));
            if (converted.kind == DynamicSceneOptionKind::Choice && choices.empty()) throw std::runtime_error(std::format("{} choice option must provide at least one choice", context));
            if (converted.kind != DynamicSceneOptionKind::Choice && !choices.empty()) throw std::runtime_error(std::format("{} non-choice option must not provide choices", context));
            std::set<std::string> choice_values{};
            converted.choices.reserve(choices.size());
            for (std::size_t choice_index = 0u; choice_index < choices.size(); ++choice_index) {
                const SpectraDynamicSceneOptionChoice& choice = choices[choice_index];
                DynamicSceneOptionChoice converted_choice{
                    .value = abi_string(choice.value, std::format("{} choice {} value", context, choice_index), false),
                    .label = abi_string(choice.label, std::format("{} choice {} label", context, choice_index), false),
                };
                if (!choice_values.insert(converted_choice.value).second) throw std::runtime_error(std::format("{} choice value '{}' is duplicated", context, converted_choice.value));
                converted.choices.push_back(std::move(converted_choice));
            }
            if (converted.kind == DynamicSceneOptionKind::Choice && !converted.default_value.empty() && !choice_values.contains(converted.default_value)) throw std::runtime_error(std::format("{} default value '{}' is not one of its choices", context, converted.default_value));
            if (converted.kind == DynamicSceneOptionKind::Bool && !converted.default_value.empty()) static_cast<void>(parse_bool_default(converted.default_value));
            if (converted.kind == DynamicSceneOptionKind::Float && !converted.default_value.empty()) static_cast<void>(parse_float_default(converted.default_value, std::format("{} default value", context)));
            if (converted.kind == DynamicSceneOptionKind::UnsignedInteger && !converted.default_value.empty()) static_cast<void>(parse_unsigned_integer_default(converted.default_value, std::format("{} default value", context)));
            return converted;
        }

        [[nodiscard]] std::vector<DynamicSceneOptionSchema> make_open_option_schemas(const SpectraDynamicSceneOptionSchemaSpan schemas, const std::string_view context) {
            const std::span<const SpectraDynamicSceneOptionSchema> schema_span = abi_span(schemas.data, schemas.count, context);
            std::set<std::string> schema_keys{};
            std::vector<DynamicSceneOptionSchema> converted{};
            converted.reserve(schema_span.size());
            for (std::size_t schema_index = 0u; schema_index < schema_span.size(); ++schema_index) {
                DynamicSceneOptionSchema schema = make_open_option_schema(schema_span[schema_index], std::format("{} {}", context, schema_index));
                if (!schema_keys.insert(schema.key).second) throw std::runtime_error(std::format("{} option '{}' is duplicated", context, schema.key));
                converted.push_back(std::move(schema));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlAction make_control_action(const SpectraDynamicSceneControlAction& action, const std::string_view context) {
            if (action.group > DynamicSceneControlActionGroupUtility) throw std::runtime_error(std::format("{} has unknown action group {}", context, action.group));
            if (action.style > DynamicSceneControlActionStyleDanger) throw std::runtime_error(std::format("{} has unknown action style {}", context, action.style));
            return DynamicSceneControlAction{
                .id = abi_string(action.id, std::format("{} id", context), false),
                .label = abi_string(action.label, std::format("{} label", context), false),
                .description = abi_string(action.description, std::format("{} description", context), true),
                .group = action.group,
                .priority = action.priority,
                .style = action.style,
                .options = make_open_option_schemas(action.options, std::format("{} option schema", context)),
            };
        }

        [[nodiscard]] std::vector<DynamicSceneControlAction> make_control_actions(const SpectraDynamicSceneControlActionSpan actions, const std::string_view context) {
            const std::span<const SpectraDynamicSceneControlAction> action_span = abi_span(actions.data, actions.count, context);
            std::set<std::string> action_ids{};
            std::vector<DynamicSceneControlAction> converted{};
            converted.reserve(action_span.size());
            for (std::size_t action_index = 0u; action_index < action_span.size(); ++action_index) {
                DynamicSceneControlAction action = make_control_action(action_span[action_index], std::format("{} {}", context, action_index));
                if (!action_ids.insert(action.id).second) throw std::runtime_error(std::format("{} action '{}' is duplicated", context, action.id));
                converted.push_back(std::move(action));
            }
            return converted;
        }

        [[nodiscard]] bool control_setting_kind_supported(const DynamicSceneOptionKind kind) {
            return kind == DynamicSceneOptionKind::Choice || kind == DynamicSceneOptionKind::Bool || kind == DynamicSceneOptionKind::Float || kind == DynamicSceneOptionKind::UnsignedInteger;
        }

        void validate_control_setting_value(const DynamicSceneControlSetting& setting, const std::string_view context) {
            switch (setting.kind) {
                case DynamicSceneOptionKind::Choice:
                    if (!std::ranges::any_of(setting.choices, [&setting](const DynamicSceneOptionChoice& choice) { return choice.value == setting.value; })) throw std::runtime_error(std::format("{} setting '{}' value '{}' is not one of its choices", context, setting.key, setting.value));
                    return;
                case DynamicSceneOptionKind::Bool:
                    static_cast<void>(parse_bool_default(setting.value));
                    return;
                case DynamicSceneOptionKind::Float:
                    static_cast<void>(parse_float_default(setting.value, std::format("{} setting '{}' value", context, setting.key)));
                    return;
                case DynamicSceneOptionKind::UnsignedInteger:
                    static_cast<void>(parse_unsigned_integer_default(setting.value, std::format("{} setting '{}' value", context, setting.key)));
                    return;
                default:
                    throw std::runtime_error(std::format("{} setting '{}' uses an unsupported kind", context, setting.key));
            }
        }

        [[nodiscard]] DynamicSceneControlSetting make_control_setting(const SpectraDynamicSceneControlSetting& setting, const std::string_view context) {
            DynamicSceneControlSetting converted{
                .key = abi_string(setting.key, std::format("{} key", context), false),
                .label = abi_string(setting.label, std::format("{} label", context), false),
                .description = abi_string(setting.description, std::format("{} description", context), true),
                .kind = make_open_option_kind(setting.kind, context),
                .value = abi_string(setting.value, std::format("{} value", context), false),
                .group = abi_string(setting.group, std::format("{} group", context), true),
                .advanced = setting.advanced != 0u,
                .priority = setting.priority,
            };
            if (setting.advanced != 0u && setting.advanced != 1u) throw std::runtime_error(std::format("{} advanced flag must be 0 or 1", context));
            if (!control_setting_kind_supported(converted.kind)) throw std::runtime_error(std::format("{} setting '{}' must use Bool, Choice, Float, or UnsignedInteger", context, converted.key));
            const std::span<const SpectraDynamicSceneOptionChoice> choices = abi_span(setting.choices.data, setting.choices.count, std::format("{} choices", context));
            if (converted.kind == DynamicSceneOptionKind::Choice && choices.empty()) throw std::runtime_error(std::format("{} setting '{}' choice kind requires choices", context, converted.key));
            if (converted.kind != DynamicSceneOptionKind::Choice && !choices.empty()) throw std::runtime_error(std::format("{} setting '{}' non-choice kind must not provide choices", context, converted.key));
            std::set<std::string> choice_values{};
            converted.choices.reserve(choices.size());
            for (std::size_t choice_index = 0u; choice_index < choices.size(); ++choice_index) {
                const SpectraDynamicSceneOptionChoice& choice = choices[choice_index];
                DynamicSceneOptionChoice converted_choice{
                    .value = abi_string(choice.value, std::format("{} choice {} value", context, choice_index), false),
                    .label = abi_string(choice.label, std::format("{} choice {} label", context, choice_index), false),
                };
                if (!choice_values.insert(converted_choice.value).second) throw std::runtime_error(std::format("{} setting '{}' choice value '{}' is duplicated", context, converted.key, converted_choice.value));
                converted.choices.push_back(std::move(converted_choice));
            }
            validate_control_setting_value(converted, context);
            return converted;
        }

        [[nodiscard]] std::vector<DynamicSceneControlSetting> make_control_settings(const SpectraDynamicSceneControlSettingView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlSettingView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            const std::span<const SpectraDynamicSceneControlSetting> setting_span = abi_span(view.settings.data, view.settings.count, std::format("{} settings", context));
            std::set<std::string> setting_keys{};
            std::vector<DynamicSceneControlSetting> converted{};
            converted.reserve(setting_span.size());
            for (std::size_t setting_index = 0u; setting_index < setting_span.size(); ++setting_index) {
                DynamicSceneControlSetting setting = make_control_setting(setting_span[setting_index], std::format("{} setting {}", context, setting_index));
                if (!setting_keys.insert(setting.key).second) throw std::runtime_error(std::format("{} setting '{}' is duplicated", context, setting.key));
                converted.push_back(std::move(setting));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlStatus make_control_status(const SpectraDynamicSceneControlStatusView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlStatusView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            DynamicSceneControlStatus status{
                .phase = abi_string(view.phase, std::format("{} phase", context), false),
                .headline = abi_string(view.headline, std::format("{} headline", context), false),
                .detail = abi_string(view.detail, std::format("{} detail", context), true),
            };
            const std::span<const SpectraDynamicSceneControlMetric> metrics = abi_span(view.metrics.data, view.metrics.count, std::format("{} metrics", context));
            std::set<std::string> metric_keys{};
            status.metrics.reserve(metrics.size());
            for (std::size_t metric_index = 0u; metric_index < metrics.size(); ++metric_index) {
                const SpectraDynamicSceneControlMetric& metric = metrics[metric_index];
                DynamicSceneControlMetric converted{
                    .key = abi_string(metric.key, std::format("{} metric {} key", context, metric_index), false),
                    .label = abi_string(metric.label, std::format("{} metric {} label", context, metric_index), false),
                    .value = abi_string(metric.value, std::format("{} metric {} value", context, metric_index), false),
                    .placement_flags = metric.placement_flags,
                    .priority = metric.priority,
                    .has_color = metric.has_color != 0u,
                    .color = {
                        finite_float(metric.color[0], std::format("{} metric {} color r", context, metric_index)),
                        finite_float(metric.color[1], std::format("{} metric {} color g", context, metric_index)),
                        finite_float(metric.color[2], std::format("{} metric {} color b", context, metric_index)),
                        finite_float(metric.color[3], std::format("{} metric {} color a", context, metric_index)),
                    },
                };
                if ((converted.placement_flags & ~(DynamicSceneControlPlacementViewportOverlay | DynamicSceneControlPlacementPanelSummary | DynamicSceneControlPlacementPanelDetail)) != 0u) throw std::runtime_error(std::format("{} metric '{}' has unknown placement flags {}", context, converted.key, converted.placement_flags));
                if (metric.has_color != 0u && metric.has_color != 1u) throw std::runtime_error(std::format("{} metric '{}' has_color flag must be 0 or 1", context, converted.key));
                if (!metric_keys.insert(converted.key).second) throw std::runtime_error(std::format("{} metric '{}' is duplicated", context, converted.key));
                status.metrics.push_back(std::move(converted));
            }
            const std::span<const char* const> enabled_action_ids = abi_span(view.enabled_action_ids, view.enabled_action_id_count, std::format("{} enabled action ids", context));
            std::set<std::string> enabled_ids{};
            status.enabled_action_ids.reserve(enabled_action_ids.size());
            for (std::size_t enabled_index = 0u; enabled_index < enabled_action_ids.size(); ++enabled_index) {
                std::string action_id = abi_string(enabled_action_ids[enabled_index], std::format("{} enabled action id {}", context, enabled_index), false);
                if (!enabled_ids.insert(action_id).second) throw std::runtime_error(std::format("{} enabled action id '{}' is duplicated", context, action_id));
                status.enabled_action_ids.push_back(std::move(action_id));
            }
            const std::span<const SpectraDynamicSceneControlDisabledAction> disabled_actions = abi_span(view.disabled_actions.data, view.disabled_actions.count, std::format("{} disabled actions", context));
            std::set<std::string> disabled_ids{};
            status.disabled_actions.reserve(disabled_actions.size());
            for (std::size_t disabled_index = 0u; disabled_index < disabled_actions.size(); ++disabled_index) {
                const SpectraDynamicSceneControlDisabledAction& disabled = disabled_actions[disabled_index];
                DynamicSceneControlDisabledAction converted{
                    .action_id = abi_string(disabled.action_id, std::format("{} disabled action {} id", context, disabled_index), false),
                    .reason = abi_string(disabled.reason, std::format("{} disabled action {} reason", context, disabled_index), false),
                };
                if (!disabled_ids.insert(converted.action_id).second) throw std::runtime_error(std::format("{} disabled action id '{}' is duplicated", context, converted.action_id));
                if (enabled_ids.contains(converted.action_id)) throw std::runtime_error(std::format("{} action '{}' cannot be both enabled and disabled", context, converted.action_id));
                status.disabled_actions.push_back(std::move(converted));
            }
            return status;
        }

        [[nodiscard]] std::vector<DynamicSceneControlLogEntry> make_control_logs(const SpectraDynamicSceneControlLogView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlLogView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            const std::span<const SpectraDynamicSceneControlLogEntry> entries = abi_span(view.entries.data, view.entries.count, std::format("{} entries", context));
            std::vector<DynamicSceneControlLogEntry> converted{};
            converted.reserve(entries.size());
            for (std::size_t entry_index = 0u; entry_index < entries.size(); ++entry_index) {
                const SpectraDynamicSceneControlLogEntry& entry = entries[entry_index];
                converted.push_back(DynamicSceneControlLogEntry{
                    .sequence = entry.sequence,
                    .level = abi_string(entry.level, std::format("{} entry {} level", context, entry_index), false),
                    .message = abi_string(entry.message, std::format("{} entry {} message", context, entry_index), false),
                });
            }
            return converted;
        }

        [[nodiscard]] std::uint64_t checked_control_image_rgba8_byte_count(const DynamicSceneControlImage& image, const std::string_view context) {
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("{} image '{}' dimensions must be non-zero", context, image.id));
            const std::uint64_t width = image.width;
            const std::uint64_t height = image.height;
            if (width > std::numeric_limits<std::uint64_t>::max() / height / 4u) throw std::runtime_error(std::format("{} image '{}' dimensions overflow RGBA8 byte count", context, image.id));
            return width * height * 4u;
        }

        [[nodiscard]] std::vector<DynamicSceneControlImage> make_control_images(const SpectraDynamicSceneControlImageView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlImageView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            const std::span<const SpectraDynamicSceneControlImage> images = abi_span(view.images.data, view.images.count, std::format("{} images", context));
            std::set<std::string> image_ids{};
            std::vector<DynamicSceneControlImage> converted{};
            converted.reserve(images.size());
            for (std::size_t image_index = 0u; image_index < images.size(); ++image_index) {
                const SpectraDynamicSceneControlImage& image = images[image_index];
                DynamicSceneControlImage converted_image{
                    .id = abi_string(image.id, std::format("{} image {} id", context, image_index), false),
                    .label = abi_string(image.label, std::format("{} image {} label", context, image_index), false),
                    .description = abi_string(image.description, std::format("{} image {} description", context, image_index), true),
                    .rgba8 = image.rgba8,
                    .rgba8_size = image.rgba8_size,
                    .revision = image.revision,
                    .width = image.width,
                    .height = image.height,
                };
                if (!image_ids.insert(converted_image.id).second) throw std::runtime_error(std::format("{} image '{}' is duplicated", context, converted_image.id));
                const std::uint64_t expected_byte_count = checked_control_image_rgba8_byte_count(converted_image, context);
                if (converted_image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("{} image '{}' RGBA8 byte count must be width * height * 4", context, converted_image.id));
                if (converted_image.rgba8 == nullptr) throw std::runtime_error(std::format("{} image '{}' RGBA8 pointer must not be null", context, converted_image.id));
                if (converted_image.revision == 0u) throw std::runtime_error(std::format("{} image '{}' revision must not be zero", context, converted_image.id));
                static_cast<void>(abi_span(converted_image.rgba8, converted_image.rgba8_size, std::format("{} image '{}' RGBA8 data", context, converted_image.id)));
                converted.push_back(std::move(converted_image));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlScalarSample make_control_scalar_sample(const SpectraDynamicSceneControlScalarSample& sample, const std::string_view context) {
            return DynamicSceneControlScalarSample{
                .step = sample.step,
                .time_seconds = finite_double(sample.time_seconds, std::format("{} time", context)),
                .value = finite_double(sample.value, std::format("{} value", context)),
            };
        }

        [[nodiscard]] std::vector<DynamicSceneControlScalarSeries> make_control_scalar_series(const SpectraDynamicSceneControlScalarSeriesView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlScalarSeriesView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            const std::span<const SpectraDynamicSceneControlScalarSeries> series_span = abi_span(view.series, std::format("{} series", context));
            std::set<std::string> series_ids{};
            std::vector<DynamicSceneControlScalarSeries> converted{};
            converted.reserve(series_span.size());
            for (std::size_t series_index = 0u; series_index < series_span.size(); ++series_index) {
                const SpectraDynamicSceneControlScalarSeries& series = series_span[series_index];
                DynamicSceneControlScalarSeries converted_series{
                    .id = abi_string(series.id, std::format("{} series {} id", context, series_index), false),
                    .label = abi_string(series.label, std::format("{} series {} label", context, series_index), false),
                    .description = abi_string(series.description, std::format("{} series {} description", context, series_index), true),
                    .unit = abi_string(series.unit, std::format("{} series {} unit", context, series_index), true),
                    .color = {
                        finite_float(series.color[0], std::format("{} series {} color r", context, series_index)),
                        finite_float(series.color[1], std::format("{} series {} color g", context, series_index)),
                        finite_float(series.color[2], std::format("{} series {} color b", context, series_index)),
                        finite_float(series.color[3], std::format("{} series {} color a", context, series_index)),
                    },
                    .group = series.group,
                    .priority = series.priority,
                    .revision = series.revision,
                };
                if (converted_series.group > DynamicSceneControlActionGroupUtility) throw std::runtime_error(std::format("{} series '{}' has unknown group {}", context, converted_series.id, converted_series.group));
                if (!series_ids.insert(converted_series.id).second) throw std::runtime_error(std::format("{} series '{}' is duplicated", context, converted_series.id));
                if (converted_series.revision == 0u) throw std::runtime_error(std::format("{} series '{}' revision must not be zero", context, converted_series.id));
                const std::span<const SpectraDynamicSceneControlScalarSample> samples = abi_span(series.samples, std::format("{} series '{}' samples", context, converted_series.id));
                converted_series.samples.reserve(samples.size());
                std::uint64_t previous_step{};
                for (std::size_t sample_index = 0u; sample_index < samples.size(); ++sample_index) {
                    DynamicSceneControlScalarSample sample = make_control_scalar_sample(samples[sample_index], std::format("{} series '{}' sample {}", context, converted_series.id, sample_index));
                    if (sample_index != 0u && sample.step < previous_step) throw std::runtime_error(std::format("{} series '{}' samples must be ordered by nondecreasing step", context, converted_series.id));
                    previous_step = sample.step;
                    converted_series.samples.push_back(sample);
                }
                converted.push_back(std::move(converted_series));
            }
            return converted;
        }

        class NativeLibrary final {
        public:
            explicit NativeLibrary(std::filesystem::path path) : path(std::move(path)) {
#if defined(_WIN32)
                this->handle = ::LoadLibraryW(this->path.wstring().c_str());
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin, Win32 error {}", this->path.string(), ::GetLastError()));
#else
                this->handle = ::dlopen(this->path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin: {}", this->path.string(), ::dlerror()));
#endif
            }

            NativeLibrary(const NativeLibrary& other) = delete;
            NativeLibrary(NativeLibrary&& other) = delete;
            NativeLibrary& operator=(const NativeLibrary& other) = delete;
            NativeLibrary& operator=(NativeLibrary&& other) = delete;

            ~NativeLibrary() noexcept {
#if defined(_WIN32)
                if (this->handle != nullptr) static_cast<void>(::FreeLibrary(this->handle));
#else
                if (this->handle != nullptr) static_cast<void>(::dlclose(this->handle));
#endif
            }

            [[nodiscard]] void* symbol(const char* name) const {
#if defined(_WIN32)
                void* symbol_address = reinterpret_cast<void*>(::GetProcAddress(this->handle, name));
                if (symbol_address == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\", Win32 error {}", this->path.string(), name, ::GetLastError()));
                return symbol_address;
#else
                ::dlerror();
                void* symbol_address = ::dlsym(this->handle, name);
                const char* error = ::dlerror();
                if (error != nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\": {}", this->path.string(), name, error));
                return symbol_address;
#endif
            }

        private:
            std::filesystem::path path{};
#if defined(_WIN32)
            HMODULE handle{};
#else
            void* handle{};
#endif
        };

        class DynamicScenePluginLibrary final {
        public:
            explicit DynamicScenePluginLibrary(DynamicScenePluginOpenRequestStorage open_request) : open_request(std::move(open_request)), plugin_directory(this->open_request.plugin_path.parent_path()), native(this->open_request.plugin_path) {
                void* entry_address = this->native.symbol("spectra_dynamic_scene_plugin");
                const SpectraDynamicScenePluginEntryFn entry = reinterpret_cast<SpectraDynamicScenePluginEntryFn>(entry_address);
                this->plugin = entry();
                if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin entry returned null", this->open_request.plugin_path.string()));
                this->validate_plugin_descriptor();
                this->scene_api = this->required_api<SpectraDynamicSceneSceneApi>(scene_api_name, scene_api_version);
                this->controls_api = this->optional_api<SpectraDynamicSceneControlsApi>(controls_api_name, controls_api_version);
                this->validate_scene_api();
                this->validate_controls_api();
            }

            DynamicScenePluginLibrary(const DynamicScenePluginLibrary& other) = delete;
            DynamicScenePluginLibrary(DynamicScenePluginLibrary&& other) = delete;
            DynamicScenePluginLibrary& operator=(const DynamicScenePluginLibrary& other) = delete;
            DynamicScenePluginLibrary& operator=(DynamicScenePluginLibrary&& other) = delete;
            ~DynamicScenePluginLibrary() noexcept = default;

            [[nodiscard]] std::string id() const {
                return abi_string(this->plugin->id, "Dynamic scene plugin id", false);
            }

            [[nodiscard]] std::string title() const {
                return abi_string(this->plugin->title, "Dynamic scene plugin title", false);
            }

            [[nodiscard]] std::string controls_panel_title() const {
                return abi_string(this->plugin->controls_panel_title, "Dynamic scene plugin controls panel title", false);
            }

            [[nodiscard]] std::string open_action_label() const {
                return abi_string(this->plugin->open_action_label, "Dynamic scene plugin open action label", false);
            }

            [[nodiscard]] std::string open_action_description() const {
                return abi_string(this->plugin->open_action_description, "Dynamic scene plugin open action description", true);
            }

            [[nodiscard]] std::string source_id() const {
                return this->open_request.source_id;
            }

            [[nodiscard]] const std::filesystem::path& path() const {
                return this->open_request.plugin_path;
            }

            [[nodiscard]] const std::filesystem::path& directory() const {
                return this->plugin_directory;
            }

            [[nodiscard]] std::vector<DynamicSceneOptionSchema> open_options() const {
                return make_open_option_schemas(this->plugin->open_options, "Dynamic scene plugin open option schema");
            }

            [[nodiscard]] std::vector<DynamicSceneControlAction> control_actions() const {
                if (this->controls_api == nullptr) return {};
                return make_control_actions(this->controls_api->control_actions, "Dynamic scene plugin controls action");
            }

            [[nodiscard]] double frames_per_second() const {
                return finite_double(this->scene_api->frames_per_second, "Dynamic scene plugin frame rate");
            }

            [[nodiscard]] scene::Scene::Document make_base_document() const {
                const std::string base_path_text = abi_string(this->scene_api->base_pbrt_path, "Dynamic scene plugin base PBRT path", true);
                if (base_path_text.empty()) {
                    return scene::Scene::Document{
                        .revision = scene::Scene::Revision{1},
                        .name = this->id(),
                        .title = this->title(),
                        .source = this->source_id(),
                        .frames_per_second = this->frames_per_second(),
                        .timeline_enabled = true,
                    };
                }
                const std::filesystem::path base_relative_path{base_path_text};
                if (base_relative_path.is_absolute()) throw std::runtime_error(std::format("{}: dynamic scene base PBRT path must be relative to the plugin directory", base_path_text));
                const std::filesystem::path base_path = (this->plugin_directory / base_relative_path).lexically_normal();
                if (!std::filesystem::is_regular_file(base_path)) throw std::runtime_error(std::format("{}: dynamic scene base PBRT file does not exist", base_path.string()));
                scene::Scene base_scene = scene::Scene::parse_pbrt_file(base_path);
                scene::Scene::Document document = *base_scene.document();
                document.revision = scene::Scene::Revision{1};
                document.name = this->id();
                document.title = this->title();
                document.source = this->source_id();
                document.frames_per_second = this->frames_per_second();
                document.timeline_enabled = true;
                return document;
            }

            void check_result(const SpectraDynamicSceneResult result, SpectraDynamicSceneInstance* instance, const std::string_view action) const {
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return;
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
                std::string error = abi_string(this->scene_api->last_error(instance), std::format("{} error message", action), true);
                if (error.empty()) error = "unknown plugin error";
                throw std::runtime_error(std::format("{} failed: {}", action, error));
            }

            [[nodiscard]] SpectraDynamicSceneInstance* create_instance() const {
                if (this->open_request.host_services == nullptr) throw std::runtime_error("Dynamic scene plugin instance creation requires host services");
                SpectraDynamicSceneInstance* instance{};
                const std::string plugin_path_text = this->open_request.plugin_path.string();
                const SpectraDynamicSceneOpenInfo open_info{
                    .struct_size = sizeof(SpectraDynamicSceneOpenInfo),
                    .plugin_path = plugin_path_text.c_str(),
                    .options = SpectraDynamicSceneOptionSpan{
                        .data = this->open_request.option_views.empty() ? nullptr : this->open_request.option_views.data(),
                        .count = static_cast<std::uint64_t>(this->open_request.option_views.size()),
                    },
                    .host_services = &this->open_request.host_services_view,
                };
                this->check_result(this->scene_api->create(&open_info, &instance), nullptr, "Dynamic scene plugin create");
                if (instance == nullptr) throw std::runtime_error("Dynamic scene plugin create returned a null instance");
                return instance;
            }

            void destroy_instance(SpectraDynamicSceneInstance* instance) const noexcept {
                if (instance != nullptr) this->scene_api->destroy(instance);
            }

            void reset(SpectraDynamicSceneInstance* instance) const {
                this->check_result(this->scene_api->reset(instance), instance, "Dynamic scene plugin reset");
            }

            void update(SpectraDynamicSceneInstance* instance, const DynamicSceneUpdateInfo& update) const {
                const SpectraDynamicSceneUpdateInfo update_info{
                    .struct_size = sizeof(SpectraDynamicSceneUpdateInfo),
                    .wall_delta_seconds = update.wall_delta_seconds,
                    .scene_delta_seconds = update.scene_delta_seconds,
                    .time_seconds = update.time_seconds,
                    .frame_index = update.frame_index,
                    .timeline_mode = static_cast<std::uint32_t>(update.timeline_mode),
                    .timeline_playing = update.timeline_playing ? 1u : 0u,
                };
                this->check_result(this->scene_api->update(instance, &update_info), instance, "Dynamic scene plugin update");
            }

            [[nodiscard]] std::uint64_t scene_revision(SpectraDynamicSceneInstance* instance) const {
                if (this->controls_api == nullptr) return 0u;
                std::uint64_t revision{};
                this->check_result(this->controls_api->scene_revision(instance, &revision), instance, "Dynamic scene plugin controls scene revision");
                if (revision == 0u) throw std::runtime_error("Dynamic scene plugin controls scene revision must not be zero");
                return revision;
            }

            void control_action(SpectraDynamicSceneInstance* instance, const std::string_view action_id, const std::span<const DynamicSceneOption> options) const {
                if (this->controls_api == nullptr) throw std::runtime_error("Dynamic scene plugin does not expose a controls API");
                if (action_id.empty()) throw std::runtime_error("Dynamic scene plugin controls action id must not be empty");
                const std::string action_id_text{action_id};
                std::vector<DynamicScenePluginOptionStorage> option_storage{};
                std::vector<SpectraDynamicSceneOption> option_views{};
                std::set<std::string> option_keys{};
                option_storage.reserve(options.size());
                option_views.reserve(options.size());
                for (const DynamicSceneOption& option : options) {
                    if (option.key.empty()) throw std::runtime_error("Dynamic scene controls action option key must not be empty");
                    if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Dynamic scene controls action option '{}' is duplicated", option.key));
                    option_storage.push_back(DynamicScenePluginOptionStorage{
                        .key = option.key,
                        .value = option.value,
                    });
                }
                for (const DynamicScenePluginOptionStorage& option : option_storage) {
                    option_views.push_back(SpectraDynamicSceneOption{
                        .key = option.key.c_str(),
                        .value = option.value.c_str(),
                    });
                }
                this->check_result(
                    this->controls_api->control_action(
                        instance,
                        action_id_text.c_str(),
                        SpectraDynamicSceneOptionSpan{
                            .data = option_views.empty() ? nullptr : option_views.data(),
                            .count = static_cast<std::uint64_t>(option_views.size()),
                        }),
                    instance,
                    std::format("Dynamic scene plugin controls action '{}'", action_id));
            }

            void control_setting_update(SpectraDynamicSceneInstance* instance, const std::string_view key, const std::string_view value) const {
                if (this->controls_api == nullptr) throw std::runtime_error("Dynamic scene plugin does not expose a controls API");
                if (key.empty()) throw std::runtime_error("Dynamic scene plugin controls setting key must not be empty");
                const std::string key_text{key};
                const std::string value_text{value};
                this->check_result(this->controls_api->control_setting_update(instance, key_text.c_str(), value_text.c_str()), instance, std::format("Dynamic scene plugin controls setting '{}'", key));
            }

            [[nodiscard]] DynamicSceneControlSnapshot control_snapshot(SpectraDynamicSceneInstance* instance) const {
                if (this->controls_api == nullptr) {
                    return DynamicSceneControlSnapshot{
                        .status = DynamicSceneControlStatus{
                            .phase = "Active",
                            .headline = "Dynamic scene active",
                        },
                    };
                }
                SpectraDynamicSceneControlSnapshotView view{};
                this->check_result(this->controls_api->control_snapshot(instance, &view), instance, "Dynamic scene plugin controls snapshot");
                if (view.struct_size != sizeof(SpectraDynamicSceneControlSnapshotView)) throw std::runtime_error("Dynamic scene plugin controls snapshot ABI size mismatch");
                DynamicSceneControlSnapshot snapshot{
                    .status = make_control_status(view.status, "Dynamic scene plugin controls snapshot status"),
                    .settings = make_control_settings(view.settings, "Dynamic scene plugin controls snapshot settings"),
                    .logs = make_control_logs(view.logs, "Dynamic scene plugin controls snapshot logs"),
                    .images = make_control_images(view.images, "Dynamic scene plugin controls snapshot images"),
                    .scalar_series = make_control_scalar_series(view.scalar_series, "Dynamic scene plugin controls snapshot scalar series"),
                };
                std::set<std::string> action_ids{};
                for (const DynamicSceneControlAction& action : this->control_actions()) action_ids.insert(action.id);
                for (const std::string& enabled_action_id : snapshot.status.enabled_action_ids)
                    if (!action_ids.contains(enabled_action_id)) throw std::runtime_error(std::format("Dynamic scene plugin controls status enabled unknown action '{}'", enabled_action_id));
                for (const DynamicSceneControlDisabledAction& disabled_action : snapshot.status.disabled_actions)
                    if (!action_ids.contains(disabled_action.action_id)) throw std::runtime_error(std::format("Dynamic scene plugin controls status disabled unknown action '{}'", disabled_action.action_id));
                return snapshot;
            }

            [[nodiscard]] SpectraDynamicSceneDocumentView document(SpectraDynamicSceneInstance* instance) const {
                SpectraDynamicSceneDocumentView view{};
                this->check_result(this->scene_api->document(instance, &view), instance, "Dynamic scene plugin document");
                return view;
            }

            [[nodiscard]] SpectraDynamicSceneFrameView frame(SpectraDynamicSceneInstance* instance, const scene::Scene::FrameInfo& frame_info) const {
                SpectraDynamicSceneFrameView view{};
                this->check_result(this->scene_api->frame(instance, SpectraDynamicSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Dynamic scene plugin frame");
                return view;
            }

        private:
            template <typename Api>
            [[nodiscard]] const Api* api(std::string_view name, const std::uint32_t version) const {
                const void* api_pointer{};
                const std::string api_name{name};
                const SpectraDynamicSceneResult result = this->plugin->get_api(api_name.c_str(), version, &api_pointer);
                if (result == SPECTRA_DYNAMIC_SCENE_RESULT_OK) return static_cast<const Api*>(api_pointer);
                if (result != SPECTRA_DYNAMIC_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{}: dynamic scene plugin get_api returned an unknown result code {}", this->open_request.plugin_path.string(), static_cast<int>(result)));
                throw std::runtime_error(std::format("{}: dynamic scene plugin get_api({}, {}) failed", this->open_request.plugin_path.string(), name, version));
            }

            template <typename Api>
            [[nodiscard]] const Api* required_api(std::string_view name, const std::uint32_t version) const {
                const Api* loaded_api = this->api<Api>(name, version);
                if (loaded_api == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin does not expose required API {} v{}", this->open_request.plugin_path.string(), name, version));
                return loaded_api;
            }

            template <typename Api>
            [[nodiscard]] const Api* optional_api(std::string_view name, const std::uint32_t version) const {
                return this->api<Api>(name, version);
            }

            void validate_plugin_descriptor() const {
                if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: dynamic scene plugin ABI version {} does not match host ABI version {}", this->open_request.plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
                if (this->plugin->struct_size != sizeof(SpectraDynamicScenePlugin)) throw std::runtime_error(std::format("{}: dynamic scene plugin descriptor size mismatch", this->open_request.plugin_path.string()));
                static_cast<void>(this->id());
                static_cast<void>(this->title());
                static_cast<void>(this->controls_panel_title());
                static_cast<void>(this->open_action_label());
                static_cast<void>(this->open_action_description());
                if (this->plugin->get_api == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin get_api function is null", this->open_request.plugin_path.string()));
                static_cast<void>(this->open_options());
            }

            void validate_scene_api() const {
                if (this->scene_api->struct_size != sizeof(SpectraDynamicSceneSceneApi)) throw std::runtime_error(std::format("{}: dynamic scene scene API descriptor size mismatch", this->open_request.plugin_path.string()));
                const double fps = this->frames_per_second();
                if (fps <= 0.0) throw std::runtime_error(std::format("{}: dynamic scene plugin frame rate must be positive", this->open_request.plugin_path.string()));
                if (this->scene_api->create == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API create function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->destroy == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API destroy function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->reset == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API reset function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->update == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API update function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->document == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API document function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->frame == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API frame function is null", this->open_request.plugin_path.string()));
                if (this->scene_api->last_error == nullptr) throw std::runtime_error(std::format("{}: dynamic scene scene API last_error function is null", this->open_request.plugin_path.string()));
            }

            void validate_controls_api() const {
                if (this->controls_api == nullptr) return;
                if (this->controls_api->struct_size != sizeof(SpectraDynamicSceneControlsApi)) throw std::runtime_error(std::format("{}: dynamic scene controls API descriptor size mismatch", this->open_request.plugin_path.string()));
                if (this->controls_api->scene_revision == nullptr) throw std::runtime_error(std::format("{}: dynamic scene controls API scene_revision function is null", this->open_request.plugin_path.string()));
                if (this->controls_api->control_action == nullptr) throw std::runtime_error(std::format("{}: dynamic scene controls API control_action function is null", this->open_request.plugin_path.string()));
                if (this->controls_api->control_setting_update == nullptr) throw std::runtime_error(std::format("{}: dynamic scene controls API control_setting_update function is null", this->open_request.plugin_path.string()));
                if (this->controls_api->control_snapshot == nullptr) throw std::runtime_error(std::format("{}: dynamic scene controls API control_snapshot function is null", this->open_request.plugin_path.string()));
                static_cast<void>(this->control_actions());
            }

            DynamicScenePluginOpenRequestStorage open_request{};
            std::filesystem::path plugin_directory{};
            NativeLibrary native;
            const SpectraDynamicScenePlugin* plugin{};
            const SpectraDynamicSceneSceneApi* scene_api{};
            const SpectraDynamicSceneControlsApi* controls_api{};
        };

        class DynamicScenePluginSourceInstance final : public DynamicSceneSourceInstance {
        public:
            explicit DynamicScenePluginSourceInstance(std::shared_ptr<DynamicScenePluginLibrary> plugin) : plugin(std::move(plugin)) {
                if (this->plugin == nullptr) throw std::runtime_error("Dynamic scene plugin source requires a plugin library");
                this->instance = this->plugin->create_instance();
            }

            DynamicScenePluginSourceInstance(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance(DynamicScenePluginSourceInstance&& other) = delete;
            DynamicScenePluginSourceInstance& operator=(const DynamicScenePluginSourceInstance& other) = delete;
            DynamicScenePluginSourceInstance& operator=(DynamicScenePluginSourceInstance&& other) = delete;

            ~DynamicScenePluginSourceInstance() noexcept override {
                this->plugin->destroy_instance(this->instance);
                this->instance = nullptr;
            }

            void reset() override {
                this->plugin->reset(this->instance);
            }

            void update(const DynamicSceneUpdateInfo& update) override {
                this->plugin->update(this->instance, update);
            }

            [[nodiscard]] std::uint64_t scene_revision() const override {
                return this->plugin->scene_revision(this->instance);
            }

            void execute_control_action(const std::string_view action_id, const std::span<const DynamicSceneOption> options) override {
                this->plugin->control_action(this->instance, action_id, options);
            }

            void update_control_setting(const std::string_view key, const std::string_view value) override {
                this->plugin->control_setting_update(this->instance, key, value);
            }

            [[nodiscard]] DynamicSceneControlSnapshot control_snapshot() const override {
                return this->plugin->control_snapshot(this->instance);
            }

            [[nodiscard]] scene::Scene::Document create_scene_document() const override {
                scene::Scene::Document document = this->plugin->make_base_document();
                std::set<std::string> material_names = collect_material_names(document);
                std::set<std::string> light_names = collect_light_names(document);
                append_document_view(document, this->plugin->document(this->instance), material_names, light_names);
                if (document.active_camera_name.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide an active camera name", this->plugin->id()));
                if (document.cameras.empty()) throw std::runtime_error(std::format("Dynamic scene plugin \"{}\" did not provide a camera or base PBRT camera", this->plugin->id()));
                document.timeline_enabled = true;
                document.frames_per_second = this->plugin->frames_per_second();
                this->material_names = std::move(material_names);
                this->document_validated = true;
                return document;
            }

            [[nodiscard]] scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const override {
                if (!this->document_validated) throw std::runtime_error("Dynamic scene plugin frame was requested before document material validation");
                return make_frame_snapshot(this->plugin->frame(this->instance, frame), frame, this->material_names);
            }

        private:
            std::shared_ptr<DynamicScenePluginLibrary> plugin{};
            SpectraDynamicSceneInstance* instance{};
            mutable std::set<std::string> material_names{};
            mutable bool document_validated{};
        };
    } // namespace

    bool is_dynamic_scene_plugin_file(const std::filesystem::path& path) {
#if defined(_WIN32)
        return path_extension_is(path, ".dll");
#elif defined(__APPLE__)
        return path_extension_is(path, ".dylib");
#else
        return path_extension_is(path, ".so");
#endif
    }

    DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path) {
        DynamicScenePluginOpenRequestStorage request{
            .plugin_path = normalized_dynamic_scene_plugin_path(plugin_path),
        };
        request.source_id = make_dynamic_scene_source_id(request.plugin_path, request.options);
        DynamicScenePluginLibrary plugin{std::move(request)};
        return DynamicScenePluginInfo{
            .id = plugin.id(),
            .title = plugin.title(),
            .controls_panel_title = plugin.controls_panel_title(),
            .open_action_label = plugin.open_action_label(),
            .open_action_description = plugin.open_action_description(),
            .path = plugin.path(),
            .open_options = plugin.open_options(),
            .control_actions = plugin.control_actions(),
        };
    }

    DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request) {
        DynamicScenePluginOpenRequestStorage open_request = make_plugin_open_request_storage(std::move(request));
        const std::filesystem::path absolute_path = open_request.plugin_path;
        const std::string source_id = open_request.source_id;
        std::shared_ptr<DynamicScenePluginLibrary> plugin = std::make_shared<DynamicScenePluginLibrary>(std::move(open_request));
        return DynamicScenePluginSource{
            .id = source_id,
            .title = plugin->title(),
            .path = absolute_path,
            .create_source = [plugin = std::move(plugin)] {
                return std::make_unique<DynamicScenePluginSourceInstance>(plugin);
            },
        };
    }
} // namespace spectra::scene_runtime
