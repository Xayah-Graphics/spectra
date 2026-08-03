export module spectra.editor:interaction;

import spectra.runtime;
import spectra.scene;
import spectra.scene.dynamics;
import spectra.render;
import std;

namespace spectra {
    export enum class CameraSource : std::uint8_t {
        Scene,
        Viewport,
    };

    export enum class AxesPlane : std::uint8_t {
        Xz,
        Xy,
        Yz,
    };

    export struct SelectionState {
        std::vector<scene::InstanceId> selected_instances{};
        std::optional<scene::InstanceId> active_instance{};
        std::optional<scene::InstanceId> hovered_instance{};
        std::optional<std::uint64_t> selected_debug_object{};
        std::optional<std::uint64_t> hovered_debug_object{};
    };

    export struct EditorInteraction {
        struct TransformEdit {
            std::uint64_t before_serial{};
            std::uint64_t after_serial{};
            scene::InstanceId instance_id{};
            math::Transform before_transform{};
            math::Transform after_transform{};
        };

        struct DynamicSetupEdit {
            std::uint64_t before_serial{};
            std::uint64_t after_serial{};
            std::optional<scene::DynamicSetup> before_setup{};
            std::optional<scene::DynamicSetup> after_setup{};
            bool preserve_simulation{};
        };

        EditorInteraction(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, GpuScene& gpu_scene, Renderers& renderers) noexcept;

        void initialize_from_scene();
        void camera_changed() noexcept;
        void orbit_viewport_camera(float x_pixels, float y_pixels) noexcept;
        void pan_viewport_camera(float x_pixels, float y_pixels, float viewport_height) noexcept;
        void zoom_viewport_camera(float steps) noexcept;
        void frame_viewport_camera(math::Bounds3 bounds, float aspect) noexcept;
        void frame_scene(float aspect) noexcept;
        void frame_selection(float aspect) noexcept;
        void view_axis(math::Float3 direction, float aspect) noexcept;
        void select_instance(scene::InstanceId instance_id, bool additive);
        void clear_selection() noexcept;
        void clear_hover() noexcept;
        [[nodiscard]] std::pair<std::optional<std::uint64_t>, bool> debug_object_at(float normalized_x, float normalized_y) const noexcept;
        void begin_transform_edit(scene::InstanceId instance_id);
        void update_transform_edit(math::Transform transform);
        void finish_transform_edit();
        [[nodiscard]] bool can_undo() const noexcept;
        [[nodiscard]] bool can_redo() const noexcept;
        void undo();
        void redo();
        [[nodiscard]] bool scene_modified() const noexcept;
        void prune_selection() noexcept;

        void set_dynamic_system_parameters(std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters, bool reset);
        void set_dynamic_clock(scene::DynamicClock clock);
        void set_dynamic_seed(std::uint64_t seed);
        void set_dynamic_system_enabled(std::size_t system_index, bool enabled);
        void set_dynamic_system_visible(std::size_t system_index, bool visible);
        void set_dynamic_system_provider(std::size_t system_index, const dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings);
        void add_dynamic_system(const dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings);
        void remove_dynamic_system(std::size_t system_index);
        void set_dynamic_port_binding(std::size_t system_index, std::string port_id, scene::DynamicPortBinding binding);
        void commit_dynamic_setup(std::optional<scene::DynamicSetup> setup, bool preserve_simulation = false);

        void destroy_scene_rendering() noexcept;
        void rebuild_scene_rendering(scene::Scene& source_scene);

        struct {
            VulkanRuntime& runtime;
            SceneDocument& document;
            DynamicWorld& dynamics;
            GpuScene& gpu_scene;
            Renderers& renderers;
        } context;

        struct {
            SelectionState selection{};
            bool overlays_visible{true};
            scene::Camera camera{};
            math::Float3 focus{};
            math::Float3 navigation_up{0.0f, 1.0f, 0.0f};
            std::uint64_t camera_revision{1};
            CameraSource source{CameraSource::Viewport};
            AxesPlane axes_plane{AxesPlane::Xz};
            scene::Camera render_camera{};
            std::uint64_t render_camera_revision{};
            float aspect{1.0f};
            CameraSource synchronized_source{CameraSource::Viewport};
            std::uint64_t synchronized_camera_revision{};
            scene::ResourceRevision synchronized_scene_camera_revision{};
        } view;

        struct {
            std::vector<std::variant<TransformEdit, DynamicSetupEdit>> undo_history{};
            std::vector<std::variant<TransformEdit, DynamicSetupEdit>> redo_history{};
            std::optional<TransformEdit> transform_edit{};
            std::uint64_t current_edit_serial{};
            std::uint64_t saved_edit_serial{};
            std::uint64_t next_edit_serial{1};
        } editing;

    private:
        void apply_edit(const std::variant<TransformEdit, DynamicSetupEdit>& edit, bool apply_before);
        void push_edit(std::variant<TransformEdit, DynamicSetupEdit> edit);
        [[nodiscard]] std::optional<std::uint32_t> instance_index(scene::InstanceId instance_id) const noexcept;
    };
} // namespace spectra
