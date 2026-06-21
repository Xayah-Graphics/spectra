export module spectra.dynamic_scene.plugin_abi;

import std;

export namespace spectra::dynamic_scene {
    constexpr std::uint32_t plugin_abi_version = 29u;
    typedef void SpectraInstance;

    typedef std::uint32_t SpectraResult;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_OK = 0u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_RESULT_ERROR = 1u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VOLUME_CHANNEL = 0u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID = 1u;

    struct SpectraOption {
        const char* key{};
        const char* value{};
    };

    struct SpectraOptionSpan {
        const SpectraOption* data{};
        std::uint64_t count{};
    };

    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_TEXT = 0u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_DIRECTORY_PATH = 1u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_FILE_PATH = 2u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_CHOICE = 3u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_BOOL = 4u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_FLOAT = 5u;
    constexpr std::uint32_t SPECTRA_DYNAMIC_SCENE_OPTION_UNSIGNED_INTEGER = 6u;

    struct SpectraOptionChoice {
        const char* value{};
        const char* label{};
    };

    struct SpectraOptionChoiceSpan {
        const SpectraOptionChoice* data{};
        std::uint64_t count{};
    };

    struct SpectraOptionSchema {
        const char* key{};
        const char* label{};
        const char* description{};
        std::uint32_t kind{};
        std::uint32_t required{};
        const char* default_value{};
        const char* group{};
        std::uint32_t advanced{};
        std::int32_t priority{};
        SpectraOptionChoiceSpan choices{};
    };

    struct SpectraOptionSchemaSpan {
        const SpectraOptionSchema* data{};
        std::uint64_t count{};
    };

    struct SpectraControlAction {
        const char* id{};
        const char* label{};
        const char* description{};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint32_t style{};
        SpectraOptionSchemaSpan options{};
    };

    struct SpectraControlActionSpan {
        const SpectraControlAction* data{};
        std::uint64_t count{};
    };

    struct SpectraControlSettingValue {
        const char* key{};
        const char* value{};
    };

    struct SpectraControlSettingValueSpan {
        const SpectraControlSettingValue* data{};
        std::uint64_t count{};
    };

    struct SpectraControlMetric {
        const char* key{};
        const char* label{};
        const char* value{};
        std::uint32_t placement_flags{};
        std::int32_t priority{};
        std::uint32_t has_color{};
        float color[4]{};
    };

    struct SpectraControlMetricSpan {
        const SpectraControlMetric* data{};
        std::uint64_t count{};
    };

    struct SpectraControlActionState {
        const char* action_id{};
        std::uint32_t enabled{};
        const char* disabled_reason{};
    };

    struct SpectraControlActionStateSpan {
        const SpectraControlActionState* data{};
        std::uint64_t count{};
    };

    struct SpectraControlStatusView {
        std::uint64_t struct_size{};
        const char* phase{};
        const char* headline{};
        const char* detail{};
        SpectraControlMetricSpan metrics{};
        SpectraControlActionStateSpan action_states{};
    };

    struct SpectraControlLogEntry {
        std::uint64_t sequence{};
        const char* level{};
        const char* message{};
    };

    struct SpectraControlLogEntrySpan {
        const SpectraControlLogEntry* data{};
        std::uint64_t count{};
    };

    struct SpectraControlImage {
        const char* id{};
        const char* label{};
        const char* description{};
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    struct SpectraControlImageSpan {
        const SpectraControlImage* data{};
        std::uint64_t count{};
    };

    struct SpectraControlScalarSample {
        std::uint64_t step{};
        double time_seconds{};
        double value{};
    };

    struct SpectraControlScalarSampleSpan {
        const SpectraControlScalarSample* data{};
        std::uint64_t count{};
    };

    struct SpectraControlScalarSeries {
        const char* id{};
        const char* label{};
        const char* description{};
        const char* unit{};
        float color[4]{};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint64_t revision{};
        SpectraControlScalarSampleSpan samples{};
    };

    struct SpectraControlScalarSeriesSpan {
        const SpectraControlScalarSeries* data{};
        std::uint64_t count{};
    };

    struct SpectraControlSnapshotView {
        std::uint64_t struct_size{};
        SpectraControlSettingValueSpan settings{};
        SpectraControlStatusView status{};
        SpectraControlLogEntrySpan logs{};
        SpectraControlImageSpan images{};
        SpectraControlScalarSeriesSpan scalar_series{};
    };

    struct SpectraUpdateInfo {
        std::uint64_t struct_size{};
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        std::uint32_t timeline_mode{};
        std::uint32_t timeline_playing{};
    };

    struct SpectraGpuDeviceIdentity {
        std::uint32_t vendor_id{};
        std::uint32_t device_id{};
        std::uint8_t device_uuid[16]{};
        std::uint8_t device_luid[8]{};
        std::uint32_t device_node_mask{};
    };

    struct SpectraGpuBufferRequest {
        std::uint64_t struct_size{};
        std::uint32_t kind{};
        std::uint64_t byte_size{};
        const char* debug_name{};
    };

    struct SpectraGpuBufferAllocation {
        std::uint64_t struct_size{};
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        std::uint32_t kind{};
        std::uint32_t handle_kind{};
        std::uintptr_t handle{};
        SpectraGpuDeviceIdentity device_identity{};
    };

    typedef SpectraResult (*SpectraRequestGpuBufferFn)(void* user_data, const SpectraGpuBufferRequest* request, SpectraGpuBufferAllocation* allocation);
    typedef SpectraResult (*SpectraReleaseGpuBufferFn)(void* user_data, std::uint64_t resource_id);
    typedef const char* (*SpectraHostLastErrorFn)(void* user_data);

    struct SpectraHostServices {
        std::uint64_t struct_size{};
        void* user_data{};
        SpectraRequestGpuBufferFn request_gpu_buffer{};
        SpectraReleaseGpuBufferFn release_gpu_buffer{};
        SpectraHostLastErrorFn last_error{};
    };

    struct SpectraOpenInfo {
        std::uint64_t struct_size{};
        const char* plugin_path{};
        SpectraOptionSpan options{};
        const SpectraHostServices* host_services{};
    };

    struct SpectraTransform {
        float position[3]{};
        float rotation[4]{};
        float scale[3]{};
    };

    struct SpectraMaterial {
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

    struct SpectraMaterialSpan {
        const SpectraMaterial* data{};
        std::uint64_t count{};
    };

    struct SpectraLight {
        const char* name{};
        const char* kind{};
        SpectraTransform transform{};
        float color[3]{};
        float intensity{};
        float cone_angle_degrees{};
    };

    struct SpectraLightSpan {
        const SpectraLight* data{};
        std::uint64_t count{};
    };

    struct SpectraCamera {
        const char* name{};
        const char* local_coordinate_system{};
        SpectraTransform transform{};
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

    struct SpectraCameraSpan {
        const SpectraCamera* data{};
        std::uint64_t count{};
    };

    struct SpectraMeshVertex {
        float position[3]{};
        float normal[3]{};
    };

    struct SpectraMeshVertexSpan {
        const SpectraMeshVertex* data{};
        std::uint64_t count{};
    };

    struct SpectraUInt32Span {
        const std::uint32_t* data{};
        std::uint64_t count{};
    };

    struct SpectraMesh {
        const char* name{};
        SpectraMeshVertexSpan vertices{};
        SpectraUInt32Span indices{};
        const char* material_name{};
        SpectraTransform transform{};
    };

    struct SpectraMeshSpan {
        const SpectraMesh* data{};
        std::uint64_t count{};
    };

    struct SpectraSphere {
        const char* name{};
        float radius{};
        const char* material_name{};
        SpectraTransform transform{};
    };

    struct SpectraSphereSpan {
        const SpectraSphere* data{};
        std::uint64_t count{};
    };

    struct SpectraPoint {
        float position[3]{};
        float normal[3]{};
        float color[4]{};
        float radius{};
    };

    struct SpectraPointSpan {
        const SpectraPoint* data{};
        std::uint64_t count{};
    };

    struct SpectraPointCloud {
        const char* name{};
        SpectraPointSpan points{};
        const char* material_name{};
        SpectraTransform transform{};
    };

    struct SpectraPointCloudSpan {
        const SpectraPointCloud* data{};
        std::uint64_t count{};
    };

    struct SpectraFloatSpan {
        const float* data{};
        std::uint64_t count{};
    };

    struct SpectraVolumeChannel {
        const char* name{};
        std::uint32_t dimensions[3]{};
        SpectraFloatSpan values{};
        std::uint32_t format{};
        std::uint32_t source_kind{};
        std::uint32_t index_encoding{};
        std::uint64_t buffer_id{};
        std::uintptr_t external_device_pointer{};
        std::uint64_t source_byte_size{};
        std::uint64_t revision{};
    };

    struct SpectraVolumeChannelSpan {
        const SpectraVolumeChannel* data{};
        std::uint64_t count{};
    };

    struct SpectraVolume {
        const char* name{};
        std::uint32_t dimensions[3]{};
        float origin[3]{};
        float voxel_size[3]{};
        SpectraVolumeChannelSpan channels{};
        const char* material_name{};
    };

    struct SpectraVolumeSpan {
        const SpectraVolume* data{};
        std::uint64_t count{};
    };

    struct SpectraEntityRef {
        std::uint32_t kind{};
        const char* name{};
    };

    struct SpectraViewportSegment {
        float start[3]{};
        float end[3]{};
    };

    struct SpectraViewportSegmentSpan {
        const SpectraViewportSegment* data{};
        std::uint64_t count{};
    };

    struct SpectraColor {
        float value[4]{};
    };

    struct SpectraColorSpan {
        const SpectraColor* data{};
        std::uint64_t count{};
    };

    struct SpectraViewportSegmentSet {
        const char* name{};
        SpectraEntityRef owner{};
        SpectraViewportSegmentSpan segments{};
        SpectraColorSpan colors{};
        SpectraFloatSpan widths{};
        float width{};
        std::uint32_t width_mode{};
        std::uint32_t depth_mode{};
        SpectraTransform transform{};
    };

    struct SpectraViewportSegmentSetSpan {
        const SpectraViewportSegmentSet* data{};
        std::uint64_t count{};
    };

    struct SpectraViewportVoxelGrid {
        const char* name{};
        SpectraEntityRef owner{};
        std::uint32_t dimensions[3]{};
        float origin[3]{};
        float voxel_size[3]{};
        SpectraTransform transform{};
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

    struct SpectraViewportVoxelGridSpan {
        const SpectraViewportVoxelGrid* data{};
        std::uint64_t count{};
    };

    struct SpectraViewportCameraVisualImage {
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
        float tint[4]{};
    };

    struct SpectraViewportCameraVisual {
        const char* name{};
        SpectraEntityRef owner{};
        float color[4]{};
        float width{};
        std::uint32_t width_mode{};
        std::uint32_t depth_mode{};
        float visual_near{};
        float visual_far{};
        std::uint32_t has_image{};
        SpectraViewportCameraVisualImage image{};
    };

    struct SpectraViewportCameraVisualSpan {
        const SpectraViewportCameraVisual* data{};
        std::uint64_t count{};
    };

    struct SpectraSceneItems {
        SpectraMaterialSpan materials{};
        SpectraLightSpan lights{};
        SpectraCameraSpan cameras{};
        SpectraMeshSpan meshes{};
        SpectraSphereSpan spheres{};
        SpectraPointCloudSpan point_clouds{};
        SpectraVolumeSpan volumes{};
        SpectraViewportSegmentSetSpan viewport_segment_sets{};
        SpectraViewportVoxelGridSpan viewport_voxel_grids{};
        SpectraViewportCameraVisualSpan viewport_camera_visuals{};
    };

    struct SpectraDocumentView {
        std::uint64_t struct_size{};
        const char* default_coordinate_system{};
        const char* active_camera_name{};
        SpectraSceneItems items{};
    };

    struct SpectraFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    struct SpectraFrameView {
        std::uint64_t struct_size{};
        SpectraSceneItems items{};
    };

    typedef SpectraResult (*SpectraCreateFn)(const SpectraOpenInfo* open_info, SpectraInstance** instance);
    typedef void (*SpectraDestroyFn)(SpectraInstance* instance);
    typedef SpectraResult (*SpectraResetFn)(SpectraInstance* instance);
    typedef SpectraResult (*SpectraUpdateFn)(SpectraInstance* instance, const SpectraUpdateInfo* update_info);
    typedef SpectraResult (*SpectraDocumentFn)(SpectraInstance* instance, SpectraDocumentView* document);
    typedef SpectraResult (*SpectraFrameFn)(SpectraInstance* instance, SpectraFrameInfo frame, SpectraFrameView* snapshot);
    typedef SpectraResult (*SpectraSceneRevisionFn)(SpectraInstance* instance, std::uint64_t* revision);
    typedef SpectraResult (*SpectraControlActionFn)(SpectraInstance* instance, const char* action_id, SpectraOptionSpan options);
    typedef SpectraResult (*SpectraControlSettingUpdateFn)(SpectraInstance* instance, const char* key, const char* value);
    typedef SpectraResult (*SpectraControlSnapshotFn)(SpectraInstance* instance, SpectraControlSnapshotView* snapshot);
    typedef const char* (*SpectraLastErrorFn)(SpectraInstance* instance);

    struct SpectraPlugin {
        std::uint32_t abi_version{};
        std::uint64_t struct_size{};
        const char* id{};
        const char* title{};
        const char* controls_panel_title{};
        const char* open_action_label{};
        const char* open_action_description{};
        const char* base_pbrt_path{};
        double frames_per_second{};
        SpectraOptionSchemaSpan open_options{};
        SpectraControlActionSpan control_actions{};
        SpectraOptionSchemaSpan control_settings{};
        SpectraCreateFn create{};
        SpectraDestroyFn destroy{};
        SpectraResetFn reset{};
        SpectraUpdateFn update{};
        SpectraDocumentFn document{};
        SpectraFrameFn frame{};
        SpectraSceneRevisionFn scene_revision{};
        SpectraControlActionFn control_action{};
        SpectraControlSettingUpdateFn control_setting_update{};
        SpectraControlSnapshotFn control_snapshot{};
        SpectraLastErrorFn last_error{};
    };

    typedef const SpectraPlugin* (*SpectraPluginEntryFn)(void);
} // namespace spectra::dynamic_scene
