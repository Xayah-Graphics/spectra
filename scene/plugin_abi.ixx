export module spectra.scene.plugin_abi;

import std;

export namespace spectra::scene {
    constexpr std::uint32_t plugin_abi_version = 2u;
    typedef void SpectraSceneInstance;

    typedef std::uint32_t SpectraSceneResult;
    constexpr std::uint32_t SPECTRA_SCENE_RESULT_OK = 0u;
    constexpr std::uint32_t SPECTRA_SCENE_RESULT_ERROR = 1u;
    constexpr std::uint32_t SPECTRA_SCENE_GPU_BUFFER_VOLUME_CHANNEL = 0u;
    constexpr std::uint32_t SPECTRA_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID = 1u;

    struct SpectraSceneOption {
        const char* key{};
        const char* value{};
    };

    struct SpectraSceneOptionSpan {
        const SpectraSceneOption* data{};
        std::uint64_t count{};
    };

    constexpr std::uint32_t SPECTRA_SCENE_OPTION_TEXT = 0u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_DIRECTORY_PATH = 1u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_FILE_PATH = 2u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_CHOICE = 3u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_BOOL = 4u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_FLOAT = 5u;
    constexpr std::uint32_t SPECTRA_SCENE_OPTION_UNSIGNED_INTEGER = 6u;

    struct SpectraSceneControlOptionChoice {
        const char* value{};
        const char* label{};
    };

    struct SpectraSceneControlOptionChoiceSpan {
        const SpectraSceneControlOptionChoice* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlSection {
        const char* id{};
        const char* label{};
    };

    struct SpectraSceneControlSectionSpan {
        const SpectraSceneControlSection* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlOptionSchema {
        const char* key{};
        const char* label{};
        const char* description{};
        std::uint32_t kind{};
        std::uint32_t required{};
        const char* default_value{};
        const char* section_id{};
        SpectraSceneControlOptionChoiceSpan choices{};
    };

    struct SpectraSceneControlOptionSchemaSpan {
        const SpectraSceneControlOptionSchema* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlAction {
        const char* id{};
        const char* label{};
        const char* description{};
        const char* section_id{};
        std::uint32_t style{};
        SpectraSceneControlOptionSchemaSpan options{};
    };

    struct SpectraSceneControlActionSpan {
        const SpectraSceneControlAction* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlSettingValue {
        const char* key{};
        const char* value{};
    };

    struct SpectraSceneControlSettingValueSpan {
        const SpectraSceneControlSettingValue* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlMetric {
        const char* key{};
        const char* label{};
        const char* value{};
        const char* section_id{};
        std::uint32_t placement_flags{};
        std::uint32_t has_color{};
        float color[4]{};
    };

    struct SpectraSceneControlMetricSpan {
        const SpectraSceneControlMetric* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlActionState {
        const char* action_id{};
        std::uint32_t enabled{};
        const char* disabled_reason{};
    };

    struct SpectraSceneControlActionStateSpan {
        const SpectraSceneControlActionState* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlStatusView {
        std::uint64_t struct_size{};
        const char* phase{};
        const char* headline{};
        const char* detail{};
        SpectraSceneControlMetricSpan metrics{};
        SpectraSceneControlActionStateSpan action_states{};
    };

    struct SpectraSceneControlLogEntry {
        std::uint64_t sequence{};
        const char* level{};
        const char* message{};
    };

    struct SpectraSceneControlLogEntrySpan {
        const SpectraSceneControlLogEntry* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlImage {
        const char* id{};
        const char* label{};
        const char* description{};
        const char* section_id{};
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct SpectraSceneControlImageSpan {
        const SpectraSceneControlImage* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlScalarSample {
        std::uint64_t step{};
        double time_seconds{};
        double value{};
    };

    struct SpectraSceneControlScalarSampleSpan {
        const SpectraSceneControlScalarSample* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlScalarSeries {
        const char* id{};
        const char* label{};
        const char* description{};
        const char* unit{};
        float color[4]{};
        const char* section_id{};
        std::uint64_t revision{};
        SpectraSceneControlScalarSampleSpan samples{};
    };

    struct SpectraSceneControlScalarSeriesSpan {
        const SpectraSceneControlScalarSeries* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneControlSnapshotView {
        std::uint64_t struct_size{};
        SpectraSceneControlSettingValueSpan settings{};
        SpectraSceneControlStatusView status{};
        SpectraSceneControlLogEntrySpan logs{};
        SpectraSceneControlImageSpan images{};
        SpectraSceneControlScalarSeriesSpan scalar_series{};
    };

    struct SpectraSceneUpdateInfo {
        std::uint64_t struct_size{};
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        std::uint32_t timeline_mode{};
        std::uint32_t timeline_playing{};
    };

    struct SpectraSceneGpuDeviceIdentity {
        std::uint32_t vendor_id{};
        std::uint32_t device_id{};
        std::uint8_t device_uuid[16]{};
        std::uint8_t device_luid[8]{};
        std::uint32_t device_node_mask{};
    };

    struct SpectraSceneGpuBufferRequest {
        std::uint64_t struct_size{};
        std::uint32_t kind{};
        std::uint64_t byte_size{};
        const char* debug_name{};
    };

    struct SpectraSceneGpuBufferAllocation {
        std::uint64_t struct_size{};
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        std::uint32_t kind{};
        std::uint32_t handle_kind{};
        std::uintptr_t handle{};
        SpectraSceneGpuDeviceIdentity device_identity{};
    };

    typedef SpectraSceneResult (*SpectraSceneRequestGpuBufferFn)(void* user_data, const SpectraSceneGpuBufferRequest* request, SpectraSceneGpuBufferAllocation* allocation);
    typedef SpectraSceneResult (*SpectraSceneReleaseGpuBufferFn)(void* user_data, std::uint64_t resource_id);
    typedef const char* (*SpectraSceneHostLastErrorFn)(void* user_data);

    struct SpectraSceneHostServices {
        std::uint64_t struct_size{};
        void* user_data{};
        SpectraSceneRequestGpuBufferFn request_gpu_buffer{};
        SpectraSceneReleaseGpuBufferFn release_gpu_buffer{};
        SpectraSceneHostLastErrorFn last_error{};
    };

    struct SpectraSceneOpenInfo {
        std::uint64_t struct_size{};
        const char* plugin_path{};
        SpectraSceneOptionSpan options{};
        const SpectraSceneHostServices* host_services{};
    };

    struct SpectraSceneTransform {
        float position[3]{};
        float rotation[4]{};
        float scale[3]{};
    };

    struct SpectraSceneMaterial {
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

    struct SpectraSceneMaterialSpan {
        const SpectraSceneMaterial* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneLight {
        const char* name{};
        const char* kind{};
        SpectraSceneTransform transform{};
        float color[3]{};
        float intensity{};
        float cone_angle_degrees{};
    };

    struct SpectraSceneLightSpan {
        const SpectraSceneLight* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneCamera {
        const char* name{};
        const char* local_coordinate_system{};
        SpectraSceneTransform transform{};
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

    struct SpectraSceneCameraSpan {
        const SpectraSceneCamera* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneMeshVertex {
        float position[3]{};
        float normal[3]{};
    };

    struct SpectraSceneMeshVertexSpan {
        const SpectraSceneMeshVertex* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneUInt32Span {
        const std::uint32_t* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneMesh {
        const char* name{};
        SpectraSceneMeshVertexSpan vertices{};
        SpectraSceneUInt32Span indices{};
        const char* material_name{};
        SpectraSceneTransform transform{};
    };

    struct SpectraSceneMeshSpan {
        const SpectraSceneMesh* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneSphere {
        const char* name{};
        float radius{};
        const char* material_name{};
        SpectraSceneTransform transform{};
    };

    struct SpectraSceneSphereSpan {
        const SpectraSceneSphere* data{};
        std::uint64_t count{};
    };

    struct SpectraScenePoint {
        float position[3]{};
        float normal[3]{};
        float color[4]{};
        float radius{};
    };

    struct SpectraScenePointSpan {
        const SpectraScenePoint* data{};
        std::uint64_t count{};
    };

    struct SpectraScenePointCloud {
        const char* name{};
        SpectraScenePointSpan points{};
        const char* material_name{};
        SpectraSceneTransform transform{};
    };

    struct SpectraScenePointCloudSpan {
        const SpectraScenePointCloud* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneFloatSpan {
        const float* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneVolumeChannel {
        const char* name{};
        std::uint32_t dimensions[3]{};
        SpectraSceneFloatSpan values{};
        std::uint32_t format{};
        std::uint32_t source_kind{};
        std::uint32_t index_encoding{};
        std::uint64_t buffer_id{};
        std::uintptr_t external_device_pointer{};
        std::uint64_t source_byte_size{};
        std::uint64_t revision{};
    };

    struct SpectraSceneVolumeChannelSpan {
        const SpectraSceneVolumeChannel* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneVolume {
        const char* name{};
        std::uint32_t dimensions[3]{};
        float origin[3]{};
        float voxel_size[3]{};
        SpectraSceneVolumeChannelSpan channels{};
        const char* material_name{};
    };

    struct SpectraSceneVolumeSpan {
        const SpectraSceneVolume* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneEntityRef {
        std::uint32_t kind{};
        const char* name{};
    };

    struct SpectraSceneViewportSegment {
        float start[3]{};
        float end[3]{};
    };

    struct SpectraSceneViewportSegmentSpan {
        const SpectraSceneViewportSegment* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneColor {
        float value[4]{};
    };

    struct SpectraSceneColorSpan {
        const SpectraSceneColor* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneViewportSegmentSet {
        const char* name{};
        SpectraSceneEntityRef owner{};
        SpectraSceneViewportSegmentSpan segments{};
        SpectraSceneColorSpan colors{};
        SpectraSceneFloatSpan widths{};
        float width{};
        std::uint32_t width_mode{};
        std::uint32_t depth_mode{};
        SpectraSceneTransform transform{};
    };

    struct SpectraSceneViewportSegmentSetSpan {
        const SpectraSceneViewportSegmentSet* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneViewportVoxelGrid {
        const char* name{};
        SpectraSceneEntityRef owner{};
        std::uint32_t dimensions[3]{};
        float origin[3]{};
        float voxel_size[3]{};
        SpectraSceneTransform transform{};
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

    struct SpectraSceneViewportVoxelGridSpan {
        const SpectraSceneViewportVoxelGrid* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneViewportCameraVisualImage {
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
        float tint[4]{};
    };

    struct SpectraSceneViewportCameraVisual {
        const char* name{};
        SpectraSceneEntityRef owner{};
        float color[4]{};
        float width{};
        std::uint32_t width_mode{};
        std::uint32_t depth_mode{};
        float visual_near{};
        float visual_far{};
        std::uint32_t has_image{};
        SpectraSceneViewportCameraVisualImage image{};
    };

    struct SpectraSceneViewportCameraVisualSpan {
        const SpectraSceneViewportCameraVisual* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneItems {
        SpectraSceneMaterialSpan materials{};
        SpectraSceneLightSpan lights{};
        SpectraSceneCameraSpan cameras{};
        SpectraSceneMeshSpan meshes{};
        SpectraSceneSphereSpan spheres{};
        SpectraScenePointCloudSpan point_clouds{};
        SpectraSceneVolumeSpan volumes{};
        SpectraSceneViewportSegmentSetSpan viewport_segment_sets{};
        SpectraSceneViewportVoxelGridSpan viewport_voxel_grids{};
        SpectraSceneViewportCameraVisualSpan viewport_camera_visuals{};
    };

    struct SpectraSceneDocumentView {
        std::uint64_t struct_size{};
        const char* default_coordinate_system{};
        const char* active_camera_name{};
        SpectraSceneItems items{};
    };

    struct SpectraSceneFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    struct SpectraSceneFrameView {
        std::uint64_t struct_size{};
        SpectraSceneItems items{};
    };

    typedef SpectraSceneResult (*SpectraSceneCreateFn)(const SpectraSceneOpenInfo* open_info, SpectraSceneInstance** instance);
    typedef void (*SpectraSceneDestroyFn)(SpectraSceneInstance* instance);
    typedef SpectraSceneResult (*SpectraSceneResetFn)(SpectraSceneInstance* instance);
    typedef SpectraSceneResult (*SpectraSceneUpdateFn)(SpectraSceneInstance* instance, const SpectraSceneUpdateInfo* update_info);
    typedef SpectraSceneResult (*SpectraSceneDocumentFn)(SpectraSceneInstance* instance, SpectraSceneDocumentView* document);
    typedef SpectraSceneResult (*SpectraSceneFrameFn)(SpectraSceneInstance* instance, SpectraSceneFrameInfo frame, SpectraSceneFrameView* snapshot);
    typedef SpectraSceneResult (*SpectraSceneRevisionFn)(SpectraSceneInstance* instance, std::uint64_t* revision);
    typedef SpectraSceneResult (*SpectraSceneControlActionFn)(SpectraSceneInstance* instance, const char* action_id, SpectraSceneOptionSpan options);
    typedef SpectraSceneResult (*SpectraSceneControlSettingUpdateFn)(SpectraSceneInstance* instance, const char* key, const char* value);
    typedef SpectraSceneResult (*SpectraSceneControlSnapshotFn)(SpectraSceneInstance* instance, SpectraSceneControlSnapshotView* snapshot);
    typedef const char* (*SpectraSceneLastErrorFn)(SpectraSceneInstance* instance);

    struct SpectraScenePlugin {
        std::uint32_t abi_version{};
        std::uint64_t struct_size{};
        const char* id{};
        const char* title{};
        const char* open_action_label{};
        const char* open_action_description{};
        const char* base_pbrt_path{};
        double frames_per_second{};
        SpectraSceneControlSectionSpan sections{};
        SpectraSceneControlOptionSchemaSpan open_options{};
        SpectraSceneControlActionSpan control_actions{};
        SpectraSceneControlOptionSchemaSpan control_settings{};
        SpectraSceneCreateFn create{};
        SpectraSceneDestroyFn destroy{};
        SpectraSceneResetFn reset{};
        SpectraSceneUpdateFn update{};
        SpectraSceneDocumentFn document{};
        SpectraSceneFrameFn frame{};
        SpectraSceneRevisionFn scene_revision{};
        SpectraSceneControlActionFn control_action{};
        SpectraSceneControlSettingUpdateFn control_setting_update{};
        SpectraSceneControlSnapshotFn control_snapshot{};
        SpectraSceneLastErrorFn last_error{};
    };

    typedef const SpectraScenePlugin* (*SpectraScenePluginEntryFn)(void);
} // namespace spectra::scene
