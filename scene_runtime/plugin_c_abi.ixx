export module spectra.scene_runtime.plugin_c_abi;

import std;

export namespace spectra::scene_runtime {
        constexpr std::uint32_t plugin_abi_version = 28u;
        typedef void SpectraDynamicSceneInstance;

        typedef std::uint32_t SpectraDynamicSceneResult;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VOLUME_CHANNEL = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID = 1u;

        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_MATERIAL = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_LIGHT = 1u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA = 2u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_MESH = 3u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE = 4u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD = 5u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME = 6u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET = 100u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID = 101u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL = 102u;

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

        struct SpectraDynamicSceneControlSettingValue {
            const char* key{};
            const char* value{};
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

        struct SpectraDynamicSceneControlActionState {
            const char* action_id{};
            std::uint32_t enabled{};
            const char* disabled_reason{};
        };

        struct SpectraDynamicSceneControlActionStateSpan {
            const SpectraDynamicSceneControlActionState* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlStatusView {
            std::uint64_t struct_size{};
            const char* phase{};
            const char* headline{};
            const char* detail{};
            SpectraDynamicSceneControlMetricSpan metrics{};
            SpectraDynamicSceneControlActionStateSpan action_states{};
        };

        struct SpectraDynamicSceneControlLogEntry {
            std::uint64_t sequence{};
            const char* level{};
            const char* message{};
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

        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_SETTINGS = 0u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_STATUS = 1u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_LOG = 2u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_IMAGE = 3u;
        constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_SCALAR_SERIES = 4u;

        struct SpectraDynamicSceneControlTypedSpan {
            std::uint32_t kind{};
            std::uint32_t item_size{};
            const void* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneControlSnapshotView {
            std::uint64_t struct_size{};
            const SpectraDynamicSceneControlTypedSpan* items{};
            std::uint64_t item_count{};
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

        struct SpectraDynamicSceneGpuBufferRequest {
            std::uint64_t struct_size{};
            std::uint32_t kind{};
            std::uint64_t byte_size{};
            const char* debug_name{};
        };

        struct SpectraDynamicSceneGpuBufferAllocation {
            std::uint64_t struct_size{};
            std::uint64_t resource_id{};
            std::uint64_t byte_size{};
            std::uint32_t kind{};
            std::uint32_t handle_kind{};
            std::uintptr_t handle{};
            SpectraDynamicSceneGpuDeviceIdentity device_identity{};
        };

        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneRequestGpuBufferFn)(void* user_data, const SpectraDynamicSceneGpuBufferRequest* request, SpectraDynamicSceneGpuBufferAllocation* allocation);
        typedef SpectraDynamicSceneResult (*SpectraDynamicSceneReleaseGpuBufferFn)(void* user_data, std::uint64_t resource_id);
        typedef const char* (*SpectraDynamicSceneHostLastErrorFn)(void* user_data);

        struct SpectraDynamicSceneHostServices {
            std::uint64_t struct_size{};
            void* user_data{};
            SpectraDynamicSceneRequestGpuBufferFn request_gpu_buffer{};
            SpectraDynamicSceneReleaseGpuBufferFn release_gpu_buffer{};
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

        struct SpectraDynamicSceneLight {
            const char* name{};
            const char* kind{};
            SpectraDynamicSceneTransform transform{};
            float color[3]{};
            float intensity{};
            float cone_angle_degrees{};
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

        struct SpectraDynamicSceneSphere {
            const char* name{};
            float radius{};
            const char* material_name{};
            SpectraDynamicSceneTransform transform{};
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

        struct SpectraDynamicSceneTypedSpan {
            std::uint32_t kind{};
            std::uint32_t item_size{};
            const void* data{};
            std::uint64_t count{};
        };

        struct SpectraDynamicSceneDocumentView {
            std::uint64_t struct_size{};
            const char* default_coordinate_system{};
            const char* active_camera_name{};
            const SpectraDynamicSceneTypedSpan* items{};
            std::uint64_t item_count{};
        };

        struct SpectraDynamicSceneFrameInfo {
            double delta_seconds{};
            double time_seconds{};
            std::uint64_t frame_index{};
        };

        struct SpectraDynamicSceneFrameView {
            std::uint64_t struct_size{};
            const SpectraDynamicSceneTypedSpan* items{};
            std::uint64_t item_count{};
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

        struct SpectraDynamicScenePlugin {
            std::uint32_t abi_version{};
            std::uint64_t struct_size{};
            const char* id{};
            const char* title{};
            const char* controls_panel_title{};
            const char* open_action_label{};
            const char* open_action_description{};
            const char* base_pbrt_path{};
            double frames_per_second{};
            SpectraDynamicSceneOptionSchemaSpan open_options{};
            SpectraDynamicSceneControlActionSpan control_actions{};
            SpectraDynamicSceneOptionSchemaSpan control_settings{};
            SpectraDynamicSceneCreateFn create{};
            SpectraDynamicSceneDestroyFn destroy{};
            SpectraDynamicSceneResetFn reset{};
            SpectraDynamicSceneUpdateFn update{};
            SpectraDynamicSceneDocumentFn document{};
            SpectraDynamicSceneFrameFn frame{};
            SpectraDynamicSceneSceneRevisionFn scene_revision{};
            SpectraDynamicSceneControlActionFn control_action{};
            SpectraDynamicSceneControlSettingUpdateFn control_setting_update{};
            SpectraDynamicSceneControlSnapshotFn control_snapshot{};
            SpectraDynamicSceneLastErrorFn last_error{};
        };

        typedef const SpectraDynamicScenePlugin* (*SpectraDynamicScenePluginEntryFn)(void);
} // namespace spectra::scene_runtime
