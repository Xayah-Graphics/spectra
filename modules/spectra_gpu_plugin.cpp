module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <vulkan/vulkan_raii.hpp>

#include "spectra_gpu.h"

module spectra.gpu;
import std;
import spectra;

namespace {
    [[nodiscard]] std::string spectra_scene_title_text(const xayah::SpectraScene& scene) {
        const std::string title = scene.scene_path.filename().string();
        if (!title.empty()) return title;
        return scene.scene_path.string();
    }
} // namespace

namespace {
    struct SpectraCameraPose {
        spectra::Point3f eye{};
        spectra::Point3f center{};
        spectra::Vector3f up{};
        float basis_handedness{1.0f};
    };

    void validate_finite_point(const spectra::Point3f& point, const char* message) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::runtime_error(message);
    }

    void validate_finite_vector(const spectra::Vector3f& vector, const char* message) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) throw std::runtime_error(message);
    }

    void validate_transform_matrix(const spectra::Transform& transform, const char* message) {
        const spectra::SquareMatrix<4>& matrix = transform.GetMatrix();
        const spectra::SquareMatrix<4>& inverse = transform.GetInverseMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                if (!std::isfinite(static_cast<float>(matrix[row][column])) || !std::isfinite(static_cast<float>(inverse[row][column]))) throw std::runtime_error(message);
            }
        }
    }

    [[nodiscard]] float finite_length(const spectra::Vector3f& vector, const char* error_message) {
        validate_finite_vector(vector, error_message);
        const float length = spectra::Length(vector);
        if (!std::isfinite(length)) throw std::runtime_error(error_message);
        return length;
    }

    [[nodiscard]] spectra::Vector3f normalized_vector(const spectra::Vector3f& vector, const char* error_message) {
        const float length = finite_length(vector, error_message);
        if (!(length > 1.0e-20f)) throw std::runtime_error(error_message);
        return vector / length;
    }

    [[nodiscard]] spectra::Vector3f camera_effective_up(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up) {
        const spectra::Vector3f view_direction = center - eye;
        if (finite_length(spectra::Cross(view_direction, up), "Camera view/up cross product is invalid") > 1.0e-10f) return up;
        return std::abs(up.y) < 0.9f ? spectra::Vector3f{0.0f, 1.0f, 0.0f} : spectra::Vector3f{1.0f, 0.0f, 0.0f};
    }

    struct SpectraCameraFrame {
        spectra::Vector3f forward{};
        spectra::Vector3f right{};
        spectra::Vector3f up{};
    };

    [[nodiscard]] SpectraCameraFrame camera_frame_from_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up, const float basis_handedness) {
        if (basis_handedness != -1.0f && basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        SpectraCameraFrame frame{};
        frame.forward = normalized_vector(center - eye, "Camera eye and center must not overlap");
        const spectra::Vector3f effective_up = camera_effective_up(eye, center, up);
        const spectra::Vector3f positive_right = normalized_vector(spectra::Cross(effective_up, frame.forward), "Camera right vector is invalid");
        frame.right                         = positive_right * basis_handedness;
        frame.up                            = spectra::Cross(frame.forward, positive_right);
        return frame;
    }

    [[nodiscard]] spectra::Transform camera_from_world_transform_from_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up, const float basis_handedness) {
        const SpectraCameraFrame frame = camera_frame_from_pose(eye, center, up, basis_handedness);
        const spectra::Vector3f eye_vector{eye.x, eye.y, eye.z};
        spectra::Transform transform{spectra::SquareMatrix<4>{
            frame.right.x, frame.right.y, frame.right.z, -spectra::Dot(frame.right, eye_vector),
            frame.up.x, frame.up.y, frame.up.z, -spectra::Dot(frame.up, eye_vector),
            frame.forward.x, frame.forward.y, frame.forward.z, -spectra::Dot(frame.forward, eye_vector),
            0.0f, 0.0f, 0.0f, 1.0f,
        }};
        validate_transform_matrix(transform, "Camera transform contains a non-finite value");
        return transform;
    }

    void validate_bounds(const spectra::Bounds3f& bounds, const char* message) {
        validate_finite_point(bounds.pMin, message);
        validate_finite_point(bounds.pMax, message);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (bounds.pMin[axis] > bounds.pMax[axis]) throw std::runtime_error(message);
        }
    }

    [[nodiscard]] spectra::Point3f camera_focus_center_from_bounds(const spectra::Point3f& eye, const spectra::Vector3f& forward, const spectra::Bounds3f& focus_bounds) {
        validate_bounds(focus_bounds, "Camera focus bounds are invalid");

        const spectra::Point3f bounds_center{
            (focus_bounds.pMin.x + focus_bounds.pMax.x) * 0.5f,
            (focus_bounds.pMin.y + focus_bounds.pMax.y) * 0.5f,
            (focus_bounds.pMin.z + focus_bounds.pMax.z) * 0.5f,
        };
        float focus_distance = spectra::Dot(bounds_center - eye, forward);

        constexpr float parallel_epsilon = 1.0e-7f;
        constexpr float distance_epsilon = 1.0e-5f;
        float ray_min = 0.0f;
        float ray_max = std::numeric_limits<float>::infinity();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::abs(forward[axis]) <= parallel_epsilon) {
                if (eye[axis] < focus_bounds.pMin[axis] || eye[axis] > focus_bounds.pMax[axis]) throw std::runtime_error("Camera focus bounds do not intersect the initial view ray");
            } else {
                float t0 = (focus_bounds.pMin[axis] - eye[axis]) / forward[axis];
                float t1 = (focus_bounds.pMax[axis] - eye[axis]) / forward[axis];
                if (t0 > t1) std::swap(t0, t1);
                ray_min = std::max(ray_min, t0);
                ray_max = std::min(ray_max, t1);
                if (ray_min > ray_max) throw std::runtime_error("Camera focus bounds do not intersect the initial view ray");
            }
        }
        if (!(ray_max > distance_epsilon)) throw std::runtime_error("Camera focus bounds must be in front of the initial camera");
        const float lower_bound = std::max(ray_min, distance_epsilon);
        focus_distance = std::clamp(focus_distance, lower_bound, ray_max);
        if (!(focus_distance > distance_epsilon) || !std::isfinite(focus_distance)) throw std::runtime_error("Camera focus distance is invalid");
        return eye + forward * focus_distance;
    }

    [[nodiscard]] std::array<float, 2> camera_view_dimensions(const spectra::Point3f& eye, const spectra::Point3f& center, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (!std::isfinite(fov_degrees) || !(fov_degrees > 0.0f) || !(fov_degrees < 180.0f)) throw std::runtime_error("Camera fov must be finite and inside (0, 180)");
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        constexpr float radians_per_degree = 0.017453292519943295769f;
        const float distance               = finite_length(eye - center, "Camera view distance is invalid");
        const float half_height            = distance * std::tan(fov_degrees * radians_per_degree * 0.5f);
        const float height                 = half_height * 2.0f;
        const float width                  = height * std::max(viewport_size[0] / viewport_size[1], 0.001f);
        if (!std::isfinite(width) || !std::isfinite(height) || !(width > 0.0f) || !(height > 0.0f)) throw std::runtime_error("Camera view dimensions are invalid");
        return {width, height};
    }


    [[nodiscard]] float spectra_camera_fov_degrees(const xayah::SpectraScene& scene) {
        if (!scene.description.camera.present) throw std::runtime_error("Interactive Spectra GPU camera controls require an explicit perspective camera");
        if (scene.description.camera.name != "perspective") throw std::runtime_error(std::format("Interactive Spectra GPU camera controls require a perspective camera, not \"{}\"", scene.description.camera.name));
        constexpr float spectra_gpu_perspective_default_fov = 90.0f;
        for (const spectra::scene::SceneDescriptionParameter& parameter : scene.description.camera.parameters) {
            if (parameter.name != "fov") continue;
            if (parameter.floats.size() != 1) throw std::runtime_error("Spectra GPU perspective camera fov must have exactly one float value");
            return parameter.floats.front();
        }
        return spectra_gpu_perspective_default_fov;
    }

    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds) {
        const spectra::Transform world_from_camera = spectra::Inverse(camera_from_world);
        SpectraCameraPose pose{};
        pose.eye                          = world_from_camera(spectra::Point3f{0.0f, 0.0f, 0.0f});
        const spectra::Vector3f right        = normalized_vector(world_from_camera(spectra::Vector3f{1.0f, 0.0f, 0.0f}), "Base camera right vector is invalid");
        const spectra::Vector3f forward      = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 0.0f, 1.0f}), "Base camera forward vector is invalid");
        pose.up                           = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 1.0f, 0.0f}), "Base camera up vector is invalid");
        const spectra::Vector3f positive_right = normalized_vector(spectra::Cross(camera_effective_up(pose.eye, pose.eye + forward, pose.up), forward), "Base camera positive right vector is invalid");
        pose.basis_handedness             = spectra::Dot(right, positive_right) < 0.0f ? -1.0f : 1.0f;
        pose.center                        = camera_focus_center_from_bounds(pose.eye, forward, focus_bounds);
        return pose;
    }

    [[nodiscard]] spectra::Transform moving_from_camera_from_pose(const spectra::Transform& base_camera_from_world, const SpectraCameraPose& pose) {
        const spectra::Transform current_camera_from_world = camera_from_world_transform_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        return base_camera_from_world * spectra::Inverse(current_camera_from_world);
    }

    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        const SpectraCameraFrame frame       = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 2> view_size = camera_view_dimensions(pose.eye, pose.center, fov_degrees, viewport_size);
        const spectra::Vector3f offset          = frame.right * (-displacement[0] * view_size[0]) + frame.up * (displacement[1] * view_size[1]);
        pose.eye += offset;
        pose.center += offset;
        return true;
    }

    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        const float larger_displacement = std::abs(displacement[0]) > std::abs(displacement[1]) ? displacement[0] : -displacement[1];
        if (larger_displacement == 0.0f) return false;
        if (larger_displacement >= 0.99f) return false;
        const spectra::Vector3f direction = pose.center - pose.eye;
        if (!(finite_length(direction, "Camera dolly direction is invalid") > 1.0e-6f)) return false;
        pose.eye += direction * larger_displacement;
        return true;
    }

    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        if (pose.basis_handedness != -1.0f && pose.basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        constexpr float two_pi   = 6.2831853071795864769f;
        constexpr float pole_pad = 1.0e-3f;
        displacement[0] *= -pose.basis_handedness;
        displacement[0] *= two_pi;
        displacement[1] *= two_pi;

        const spectra::Point3f origin   = invert ? pose.eye : pose.center;
        const spectra::Point3f position = invert ? pose.center : pose.eye;
        spectra::Vector3f center_to_eye = position - origin;
        const float radius              = finite_length(center_to_eye, "Camera orbit radius is invalid");
        if (!(radius > 1.0e-6f)) return false;
        center_to_eye /= radius;

        const spectra::Vector3f normalized_up = normalized_vector(pose.up, "Camera up vector is invalid");
        const float cos_elevation             = spectra::Dot(center_to_eye, normalized_up);
        spectra::Vector3f horizontal          = center_to_eye - normalized_up * cos_elevation;
        const float sin_elevation             = finite_length(horizontal, "Camera orbit horizontal vector is invalid");
        const float elevation                 = std::atan2(sin_elevation, cos_elevation);
        if (sin_elevation < 1.0e-6f) {
            const spectra::Vector3f reference = std::abs(normalized_up.x) < 0.9f ? spectra::Vector3f{1.0f, 0.0f, 0.0f} : spectra::Vector3f{0.0f, 0.0f, 1.0f};
            horizontal                        = normalized_vector(reference - normalized_up * spectra::Dot(reference, normalized_up), "Camera orbit horizontal vector is invalid");
        } else {
            horizontal /= sin_elevation;
        }

        const float yaw_cos             = std::cos(-displacement[0]);
        const float yaw_sin             = std::sin(-displacement[0]);
        horizontal                      = horizontal * yaw_cos + spectra::Cross(normalized_up, horizontal) * yaw_sin;
        const float new_elevation       = std::clamp(elevation - displacement[1], pole_pad, 3.14159265358979323846f - pole_pad);
        const spectra::Vector3f new_offset = (normalized_up * std::cos(new_elevation) + horizontal * std::sin(new_elevation)) * radius;
        const spectra::Point3f new_position = origin + new_offset;
        if (invert) pose.center = new_position;
        else pose.eye = new_position;
        return true;
    }

    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        if (delta[0] == 0.0f && delta[1] == 0.0f) return false;
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        const SpectraCameraFrame frame = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const spectra::Vector3f movement = dolly
            ? frame.forward * (delta[0] * speed)
            : frame.right * (delta[0] * speed) + frame.up * (delta[1] * speed);
        pose.eye += movement;
        pose.center += movement;
        return true;
    }
} // namespace

namespace {
    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string spectra_parameter_count_text(const std::vector<spectra::scene::SceneDescriptionParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const spectra::scene::SceneDescriptionRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    [[nodiscard]] std::string resolution_text(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) return "Pending";
        return std::format("{} x {}", resolution[0], resolution[1]);
    }

    [[nodiscard]] std::string positive_int_text(const int value) {
        if (value <= 0) return "Pending";
        return std::format("{}", value);
    }

    [[nodiscard]] const char* scene_texture_value_type_label(const spectra::scene::SceneDescriptionTextureValueType value_type) {
        switch (value_type) {
            case spectra::scene::SceneDescriptionTextureValueType::Unknown: return "Unknown";
            case spectra::scene::SceneDescriptionTextureValueType::Float: return "Float";
            case spectra::scene::SceneDescriptionTextureValueType::Spectrum: return "Spectrum";
        }
        throw std::runtime_error("Unknown Spectra scene texture value type");
    }

    void draw_scene_render_setting_row(const char* label, const spectra::scene::SceneDescriptionRenderSetting& setting) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const std::string setting_text = scene_render_setting_text(setting);
        if (setting.present) ImGui::TextWrapped("%s", setting_text.c_str());
        else ImGui::TextDisabled("%s", setting_text.c_str());
        ImGui::TableSetColumnIndex(2);
        if (setting.present) ImGui::TextWrapped("%s", scene_file_location_text(setting.location).c_str());
        else ImGui::TextDisabled("None");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(spectra_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

} // namespace


namespace xayah {
    class SpectraGpuInteractivePlugin final : public SpectraPlugin {
    public:
        explicit SpectraGpuInteractivePlugin(std::filesystem::path scene_path);
        ~SpectraGpuInteractivePlugin() noexcept override;

        [[nodiscard]] std::string_view name() const override;
        void attach(SpectraContext& context) override;
        void detach(SpectraContext& context) noexcept override;
        void before_imgui_shutdown(SpectraContext& context) noexcept override;
        void after_imgui_created(SpectraContext& context) override;
        void begin_frame(SpectraFrameContext& context) override;
        void record_frame(SpectraRecordContext& context) override;

    private:
        struct PathtracerFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        struct PathtracerStatus {
            std::array<int, 2> sample_range{0, 0};
            bool uses_external_completion{false};
            std::string state{};
        };

        struct RollingFloatAverage {
            static constexpr std::size_t sample_count{100};

            std::array<float, sample_count> values{};
            std::size_t count{0};
            std::size_t cursor{0};
            float sum{0.0f};

            void clear();
            void add(float value);
            [[nodiscard]] bool has_value() const;
            [[nodiscard]] float average() const;
        };

        struct PluginState {
            std::filesystem::path scene_path{};
            SpectraContext context{};
            bool attached{false};

            struct {
                bool viewport_known{false};
                bool viewport_hovered{false};
                bool viewport_focused{false};
                std::array<float, 2> viewport_position{0.0f, 0.0f};
                std::array<float, 2> viewport_size{1280.0f, 720.0f};
                std::array<int, 2> viewport_framebuffer_size{0, 0};
            } ui;

            std::unique_ptr<SpectraScene> spectra_scene{};
            std::unique_ptr<SpectraGpuPathtracer> gpu_pathtracer{};
            std::unique_ptr<SpectraGpuRuntime> gpu_runtime{};

            struct {
                bool candidate_known{false};
                bool pathtracer_created{false};
                bool rebuilding{false};
                float stable_seconds{0.0f};
                std::array<int, 2> candidate_resolution{0, 0};
                std::array<int, 2> active_resolution{0, 0};
            } render_resolution_sync;

            struct {
                bool initialized{false};
                bool input_enabled{false};
                float speed{1.0f};
                float fov_degrees{60.0f};
                float basis_handedness{1.0f};
                bool mouse_position_known{false};
                spectra::Point3f eye{0.0f, 0.0f, 0.0f};
                spectra::Point3f center{0.0f, 0.0f, 1.0f};
                spectra::Vector3f up{0.0f, 1.0f, 0.0f};
                std::array<float, 2> mouse_position{0.0f, 0.0f};
                spectra::Transform moving_from_camera{};
                spectra::Transform camera_from_world{};
            } camera;

            struct {
                RollingFloatAverage frame_milliseconds{};
                RollingFloatAverage throughput_mspp{};
                std::uint64_t current_frame_id{0};
                std::uint32_t active_frame_index{0};
                std::uint32_t active_swapchain_image_index{0};
                float last_frame_milliseconds{0.0f};
                float last_valid_throughput_mspp{0.0f};
                bool has_throughput{false};
                bool last_frame_rendered_sample{false};
            } statistics;
        };

        void register_panels(SpectraContext& context);
        [[nodiscard]] std::string window_detail() const;
        void unload_spectra_scene_noexcept() noexcept;
        void create_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void unload_pathtracer_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool pathtracer_ready() const;
        void clear_pathtracer_throughput_statistics();
        void update_frame_statistics(const SpectraFrameContext& frame, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        [[nodiscard]] PathtracerStatus pathtracer_status() const;
        [[nodiscard]] VkDescriptorSet pathtracer_viewport_descriptor() const;
        [[nodiscard]] std::array<int, 2> pathtracer_sample_range() const;
        [[nodiscard]] float pathtracer_initial_move_scale() const;
        [[nodiscard]] vk::Semaphore pathtracer_complete_semaphore() const;
        [[nodiscard]] PathtracerFrameResult render_pathtracer_frame(const SpectraFrameContext& frame);
        void record_pathtracer_output(const vk::raii::CommandBuffer& command_buffer);
        void request_pathtracer_accumulation_reset();
        void initialize_camera_state();
        void process_camera_input();
        void set_camera_speed(float speed);
        void reset_camera();
        void draw_viewport_window();
        void draw_camera_window();
        void draw_scene_browser_window();
        void draw_inspector_window();
        void draw_settings_window();
        void draw_environment_window();
        void draw_tonemapper_window();
        void draw_statistics_window();

        std::unique_ptr<PluginState> state{};
    };

    void SpectraGpuInteractivePlugin::RollingFloatAverage::clear() {
        this->values.fill(0.0f);
        this->count = 0;
        this->cursor = 0;
        this->sum = 0.0f;
    }

    void SpectraGpuInteractivePlugin::RollingFloatAverage::add(const float value) {
        if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("Rolling statistic value must be finite and non-negative");
        if (this->count < sample_count) {
            this->values[this->cursor] = value;
            this->sum += value;
            ++this->count;
        } else {
            this->sum -= this->values[this->cursor];
            this->values[this->cursor] = value;
            this->sum += value;
        }
        this->cursor = (this->cursor + 1) % sample_count;
    }

    bool SpectraGpuInteractivePlugin::RollingFloatAverage::has_value() const {
        return this->count > 0;
    }

    float SpectraGpuInteractivePlugin::RollingFloatAverage::average() const {
        if (this->count == 0) return 0.0f;
        return this->sum / static_cast<float>(this->count);
    }

    SpectraGpuInteractivePlugin::SpectraGpuInteractivePlugin(std::filesystem::path scene_path) : state{std::make_unique<PluginState>()} {
        if (scene_path.empty()) throw std::runtime_error("Spectra GPU interactive plugin requires a scene path");
        this->state->scene_path = std::move(scene_path);
    }

    SpectraGpuInteractivePlugin::~SpectraGpuInteractivePlugin() noexcept = default;

    std::string_view SpectraGpuInteractivePlugin::name() const {
        return "Spectra GPU Interactive";
    }

    void SpectraGpuInteractivePlugin::attach(SpectraContext& context) {
        if (this->state->attached) throw std::runtime_error("Spectra GPU interactive plugin is already attached");
        this->state->context = context;
        this->state->attached = true;
        try {
            this->state->gpu_runtime = std::make_unique<SpectraGpuRuntime>();
            std::unique_ptr<SpectraScene> loaded_scene = std::make_unique<SpectraScene>();
            try {
                loaded_scene->load(this->state->scene_path);
                this->state->spectra_scene = std::move(loaded_scene);
            } catch (...) {
                loaded_scene->unload_noexcept();
                throw;
            }
            this->register_panels(context);
            context.set_window_detail(this->window_detail());
        } catch (...) {
            this->detach(context);
            throw;
        }
    }

    void SpectraGpuInteractivePlugin::detach(SpectraContext&) noexcept {
        try {
            if (this->state->attached) this->state->context.device().waitIdle();
        } catch (...) {
        }
        this->unload_pathtracer_noexcept();
        this->unload_spectra_scene_noexcept();
        this->state->gpu_runtime.reset();
        this->state->context = SpectraContext{};
        this->state->attached = false;
    }

    void SpectraGpuInteractivePlugin::before_imgui_shutdown(SpectraContext&) noexcept {
        if (this->state->gpu_pathtracer != nullptr) this->state->gpu_pathtracer->release_viewport_descriptors_noexcept();
    }

    void SpectraGpuInteractivePlugin::after_imgui_created(SpectraContext&) {
        if (this->state->gpu_pathtracer != nullptr) this->state->gpu_pathtracer->create_viewport_descriptors();
    }

    void SpectraGpuInteractivePlugin::begin_frame(SpectraFrameContext& context) {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot update Spectra GPU pathtracer frame without an active Spectra scene");
        this->synchronize_render_resolution();
        if (this->pathtracer_ready()) {
            this->process_camera_input();
            const PathtracerFrameResult render_result = this->render_pathtracer_frame(context);
            context.request_external_completion(this->pathtracer_complete_semaphore());
            this->update_frame_statistics(context, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        } else {
            this->update_frame_statistics(context, false, false, 0);
        }
        context.set_window_detail(this->window_detail());
    }

    void SpectraGpuInteractivePlugin::record_frame(SpectraRecordContext& context) {
        if (this->pathtracer_ready()) this->record_pathtracer_output(context.command_buffer());
    }

    void SpectraGpuInteractivePlugin::register_panels(SpectraContext& context) {
        SpectraPanel viewport{};
        viewport.id = "spectra_gpu.viewport";
        viewport.title = "Viewport";
        viewport.dock_slot = SpectraDockSlot::Center;
        viewport.window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        viewport.closable = false;
        viewport.show_in_menu = false;
        viewport.show_in_toolbar = false;
        viewport.zero_window_padding = true;
        viewport.draw = [this](SpectraPanelContext&) { this->draw_viewport_window(); };
        context.register_panel(std::move(viewport));

        SpectraPanel camera{};
        camera.id = "spectra_gpu.camera";
        camera.title = "Camera";
        camera.icon = ICON_MS_PHOTO_CAMERA;
        camera.shortcut_label = "F1";
        camera.shortcut_key = ImGuiKey_F1;
        camera.dock_slot = SpectraDockSlot::Left;
        camera.draw = [this](SpectraPanelContext&) { this->draw_camera_window(); };
        context.register_panel(std::move(camera));

        SpectraPanel scene_browser{};
        scene_browser.id = "spectra_gpu.scene_browser";
        scene_browser.title = "Scene Browser";
        scene_browser.icon = ICON_MS_ACCOUNT_TREE;
        scene_browser.shortcut_label = "F2";
        scene_browser.shortcut_key = ImGuiKey_F2;
        scene_browser.dock_slot = SpectraDockSlot::Right;
        scene_browser.draw = [this](SpectraPanelContext&) { this->draw_scene_browser_window(); };
        context.register_panel(std::move(scene_browser));

        SpectraPanel settings{};
        settings.id = "spectra_gpu.settings";
        settings.title = "Settings";
        settings.icon = ICON_MS_SETTINGS;
        settings.shortcut_label = "F3";
        settings.shortcut_key = ImGuiKey_F3;
        settings.dock_slot = SpectraDockSlot::Left;
        settings.draw = [this](SpectraPanelContext&) { this->draw_settings_window(); };
        context.register_panel(std::move(settings));

        SpectraPanel inspector{};
        inspector.id = "spectra_gpu.inspector";
        inspector.title = "Inspector";
        inspector.icon = ICON_MS_LIST_ALT;
        inspector.shortcut_label = "F4";
        inspector.shortcut_key = ImGuiKey_F4;
        inspector.dock_slot = SpectraDockSlot::RightBottom;
        inspector.draw = [this](SpectraPanelContext&) { this->draw_inspector_window(); };
        context.register_panel(std::move(inspector));

        SpectraPanel environment{};
        environment.id = "spectra_gpu.environment";
        environment.title = "Environment";
        environment.icon = ICON_MS_PUBLIC;
        environment.shortcut_label = "F5";
        environment.shortcut_key = ImGuiKey_F5;
        environment.dock_slot = SpectraDockSlot::LeftBottom;
        environment.draw = [this](SpectraPanelContext&) { this->draw_environment_window(); };
        context.register_panel(std::move(environment));

        SpectraPanel tonemapper{};
        tonemapper.id = "spectra_gpu.tonemapper";
        tonemapper.title = "Tonemapper";
        tonemapper.icon = ICON_MS_TONALITY;
        tonemapper.shortcut_label = "F6";
        tonemapper.shortcut_key = ImGuiKey_F6;
        tonemapper.dock_slot = SpectraDockSlot::LeftBottom;
        tonemapper.draw = [this](SpectraPanelContext&) { this->draw_tonemapper_window(); };
        context.register_panel(std::move(tonemapper));

        SpectraPanel statistics{};
        statistics.id = "spectra_gpu.statistics";
        statistics.title = "Statistics";
        statistics.icon = ICON_MS_ANALYTICS;
        statistics.dock_slot = SpectraDockSlot::Bottom;
        statistics.show_in_toolbar = false;
        statistics.draw = [this](SpectraPanelContext&) { this->draw_statistics_window(); };
        context.register_panel(std::move(statistics));
    }

    std::string SpectraGpuInteractivePlugin::window_detail() const {
        std::uint32_t width = this->state->context.swapchain_extent().width;
        std::uint32_t height = this->state->context.swapchain_extent().height;
        if (this->state->render_resolution_sync.pathtracer_created) {
            width = static_cast<std::uint32_t>(this->state->render_resolution_sync.active_resolution[0]);
            height = static_cast<std::uint32_t>(this->state->render_resolution_sync.active_resolution[1]);
        } else if (this->state->ui.viewport_known && this->state->ui.viewport_framebuffer_size[0] > 0 && this->state->ui.viewport_framebuffer_size[1] > 0) {
            width = static_cast<std::uint32_t>(this->state->ui.viewport_framebuffer_size[0]);
            height = static_cast<std::uint32_t>(this->state->ui.viewport_framebuffer_size[1]);
        }
        const std::string scene_title = this->state->spectra_scene == nullptr ? "No Scene" : spectra_scene_title_text(*this->state->spectra_scene);
        const std::array<int, 2> sample_range = this->state->spectra_scene == nullptr ? std::array<int, 2>{0, 0} : this->pathtracer_sample_range();
        return std::format("{} | Spectra GPU Pathtracer | {}x{} | sample {}/{}", scene_title, width, height, sample_range[0], sample_range[1]);
    }

    void SpectraGpuInteractivePlugin::unload_spectra_scene_noexcept() noexcept {
        if (this->state->spectra_scene != nullptr) {
            this->state->spectra_scene->unload_noexcept();
            this->state->spectra_scene.reset();
        }
    }

    void SpectraGpuInteractivePlugin::create_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot create Spectra GPU pathtracer without a loaded Spectra scene");
        if (this->state->gpu_pathtracer != nullptr) throw std::runtime_error("Spectra GPU pathtracer is already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra GPU pathtracer with a non-positive resolution");
        try {
            if (this->state->gpu_runtime == nullptr) throw std::runtime_error("Spectra GPU runtime is not initialized");
            this->state->gpu_runtime->reset_options_for_scene();
            this->state->gpu_pathtracer = std::make_unique<SpectraGpuPathtracer>(*this->state->spectra_scene, resolution, this->state->context.physical_device(), this->state->context.device(), this->state->context.frame_count());
            this->state->spectra_scene->set_runtime_metadata(this->state->gpu_pathtracer->film_resolution(), this->state->gpu_pathtracer->sampler_sample_count(), this->state->gpu_pathtracer->camera_from_world_transform());
            this->state->render_resolution_sync.active_resolution = resolution;
            this->state->render_resolution_sync.pathtracer_created  = true;
        } catch (...) {
            if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            throw;
        }
    }

    void SpectraGpuInteractivePlugin::rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state->render_resolution_sync.rebuilding) throw std::runtime_error("Spectra GPU pathtracer resolution rebuild is already active");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot rebuild Spectra GPU pathtracer with a non-positive resolution");
        if (this->state->render_resolution_sync.pathtracer_created && this->state->render_resolution_sync.active_resolution == resolution) return;

        const bool preserve_camera = this->state->camera.initialized;
        const SpectraCameraPose preserved_pose{this->state->camera.eye, this->state->camera.center, this->state->camera.up, this->state->camera.basis_handedness};
        const float preserved_speed     = this->state->camera.speed;
        const int preserved_samples     = this->state->gpu_pathtracer == nullptr ? 0 : this->state->gpu_pathtracer->target_sample_count();
        const float preserved_exposure  = this->state->gpu_pathtracer == nullptr ? 1.0f : this->state->gpu_pathtracer->current_exposure();
        this->state->render_resolution_sync.rebuilding = true;
        try {
            this->state->context.device().waitIdle();
            if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            this->create_pathtracer_for_resolution(resolution);
            if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU pathtracer was not created");
            if (preserved_samples > 0) this->state->gpu_pathtracer->set_target_sample_count(preserved_samples);
            this->state->gpu_pathtracer->set_exposure(preserved_exposure);
            if (preserve_camera) {
                this->state->camera.camera_from_world          = this->state->spectra_scene->camera_from_world;
                this->state->camera.eye                        = preserved_pose.eye;
                this->state->camera.center                     = preserved_pose.center;
                this->state->camera.up                         = preserved_pose.up;
                this->state->camera.basis_handedness           = preserved_pose.basis_handedness;
                this->state->camera.speed                      = preserved_speed;
                this->state->camera.fov_degrees                = spectra_camera_fov_degrees(*this->state->spectra_scene);
                this->state->camera.mouse_position_known       = false;
                this->state->camera.input_enabled              = false;
                this->state->camera.moving_from_camera         = moving_from_camera_from_pose(this->state->camera.camera_from_world, preserved_pose);
            } else
                this->initialize_camera_state();
            this->clear_pathtracer_throughput_statistics();
            this->state->statistics.last_frame_rendered_sample = false;
            this->state->render_resolution_sync.rebuilding     = false;
        } catch (...) {
            this->state->render_resolution_sync.rebuilding = false;
            throw;
        }
    }

    void SpectraGpuInteractivePlugin::unload_pathtracer_noexcept() noexcept {
        if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
        this->state->gpu_pathtracer.reset();
        this->state->render_resolution_sync.pathtracer_created  = false;
        this->state->render_resolution_sync.active_resolution = {0, 0};
    }

    void SpectraGpuInteractivePlugin::observe_viewport_render_resolution(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid while tracking viewport resolution");
        if (!this->state->render_resolution_sync.candidate_known || this->state->render_resolution_sync.candidate_resolution != resolution) {
            this->state->render_resolution_sync.candidate_known     = true;
            this->state->render_resolution_sync.candidate_resolution = resolution;
            this->state->render_resolution_sync.stable_seconds      = 0.0f;
            return;
        }
        this->state->render_resolution_sync.stable_seconds += io.DeltaTime;
    }

    void SpectraGpuInteractivePlugin::synchronize_render_resolution() {
        constexpr float resolution_stability_seconds = 0.3f;
        if (this->state->spectra_scene == nullptr) return;
        if (!this->state->render_resolution_sync.candidate_known) return;
        if (this->state->render_resolution_sync.stable_seconds < resolution_stability_seconds) return;
        if (this->state->render_resolution_sync.pathtracer_created && this->state->render_resolution_sync.active_resolution == this->state->render_resolution_sync.candidate_resolution) return;
        this->rebuild_pathtracer_for_resolution(this->state->render_resolution_sync.candidate_resolution);
    }

    [[nodiscard]] bool SpectraGpuInteractivePlugin::pathtracer_ready() const {
        return this->state->render_resolution_sync.pathtracer_created && this->state->gpu_pathtracer != nullptr;
    }

    void SpectraGpuInteractivePlugin::clear_pathtracer_throughput_statistics() {
        this->state->statistics.throughput_mspp.clear();
        this->state->statistics.last_valid_throughput_mspp = 0.0f;
        this->state->statistics.has_throughput             = false;
    }

    void SpectraGpuInteractivePlugin::update_frame_statistics(const SpectraFrameContext& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Spectra GPU pathtracer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Spectra GPU pathtracer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        ++this->state->statistics.current_frame_id;
        this->state->statistics.active_frame_index           = frame.frame_index();
        this->state->statistics.active_swapchain_image_index = frame.image_index();
        this->state->statistics.last_frame_milliseconds      = frame_milliseconds;
        this->state->statistics.last_frame_rendered_sample   = rendered_sample;
        this->state->statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->state->statistics.throughput_mspp.add(throughput);
            this->state->statistics.last_valid_throughput_mspp = throughput;
            this->state->statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] SpectraGpuInteractivePlugin::PathtracerStatus SpectraGpuInteractivePlugin::pathtracer_status() const {
        PathtracerStatus status{};
        status.sample_range = this->pathtracer_sample_range();
        if (this->state->render_resolution_sync.rebuilding) {
            status.state = "Rebuilding";
            return status;
        }
        if (!this->state->render_resolution_sync.pathtracer_created) {
            status.state = this->state->render_resolution_sync.candidate_known ? "Pending Resolution" : "Waiting for Viewport";
            return status;
        }
        status.uses_external_completion = this->state->gpu_pathtracer != nullptr;
        if (this->state->gpu_pathtracer == nullptr) {
            status.state = "Unavailable";
            return status;
        }
        status.state = status.sample_range[0] >= status.sample_range[1] ? "Completed" : "Sampling";
        return status;
    }

    [[nodiscard]] VkDescriptorSet SpectraGpuInteractivePlugin::pathtracer_viewport_descriptor() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU pathtracer viewport descriptor requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->active_descriptor();
    }

    [[nodiscard]] std::array<int, 2> SpectraGpuInteractivePlugin::pathtracer_sample_range() const {
        if (this->state->gpu_pathtracer == nullptr) return {0, 0};
        return {this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count()};
    }

    [[nodiscard]] float SpectraGpuInteractivePlugin::pathtracer_initial_move_scale() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU camera move scale requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->camera_initial_move_scale();
    }

    [[nodiscard]] vk::Semaphore SpectraGpuInteractivePlugin::pathtracer_complete_semaphore() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU completion semaphore requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->active_cuda_complete_semaphore();
    }

    [[nodiscard]] SpectraGpuInteractivePlugin::PathtracerFrameResult SpectraGpuInteractivePlugin::render_pathtracer_frame(const SpectraFrameContext& frame) {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot render Spectra GPU pathtracer without an active Spectra GPU session");
        const SpectraGpuPathtracer::RenderFrameResult render_result = this->state->gpu_pathtracer->render_frame(frame.frame_index(), this->state->camera.moving_from_camera);
        return {render_result.sample_pixels, render_result.rendered_sample, render_result.reset_accumulation};
    }

    void SpectraGpuInteractivePlugin::record_pathtracer_output(const vk::raii::CommandBuffer& command_buffer) {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot record Spectra GPU pathtracer output without an active Spectra GPU session");
        this->state->gpu_pathtracer->record_copy(command_buffer);
    }

    void SpectraGpuInteractivePlugin::request_pathtracer_accumulation_reset() {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot reset Spectra GPU accumulation without an active Spectra GPU session");
        this->state->gpu_pathtracer->request_reset_accumulation();
        this->clear_pathtracer_throughput_statistics();
    }

    void SpectraGpuInteractivePlugin::initialize_camera_state() {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        const float initial_move_scale = this->pathtracer_initial_move_scale();
        if (!std::isfinite(initial_move_scale) || !(initial_move_scale > 0.0f)) throw std::runtime_error("Initial camera move scale must be finite and positive");
        this->state->camera.camera_from_world = this->state->spectra_scene->camera_from_world;
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU camera focus bounds requested without an active Spectra GPU session");
        const SpectraCameraPose pose   = camera_pose_from_base_transform(this->state->camera.camera_from_world, this->state->gpu_pathtracer->camera_initial_focus_bounds());
        this->state->camera.initialized       = true;
        this->state->camera.input_enabled     = false;
        this->state->camera.speed             = initial_move_scale * 60.0f;
        this->state->camera.fov_degrees       = spectra_camera_fov_degrees(*this->state->spectra_scene);
        this->state->camera.basis_handedness  = pose.basis_handedness;
        this->state->camera.eye               = pose.eye;
        this->state->camera.center            = pose.center;
        this->state->camera.up                = pose.up;
        this->state->camera.mouse_position    = {0.0f, 0.0f};
        this->state->camera.mouse_position_known = false;
        this->state->camera.moving_from_camera   = spectra::Transform{};
    }

    void SpectraGpuInteractivePlugin::set_camera_speed(const float speed) {
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        this->state->camera.speed = speed;
    }

    void SpectraGpuInteractivePlugin::reset_camera() {
        if (!this->state->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        if (!this->pathtracer_ready()) throw std::runtime_error("Cannot reset camera without an active Spectra GPU pathtracer");
        const SpectraCameraPose pose  = camera_pose_from_base_transform(this->state->camera.camera_from_world, this->state->gpu_pathtracer->camera_initial_focus_bounds());
        this->state->camera.eye              = pose.eye;
        this->state->camera.center           = pose.center;
        this->state->camera.up               = pose.up;
        this->state->camera.basis_handedness = pose.basis_handedness;
        this->state->camera.mouse_position_known = false;
        this->state->camera.moving_from_camera   = spectra::Transform{};
        this->request_pathtracer_accumulation_reset();
    }

    void SpectraGpuInteractivePlugin::process_camera_input() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) this->state->context.request_close();

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->state->ui.viewport_known && mouse_position.x >= this->state->ui.viewport_position[0] && mouse_position.x < this->state->ui.viewport_position[0] + this->state->ui.viewport_size[0] && mouse_position.y >= this->state->ui.viewport_position[1] && mouse_position.y < this->state->ui.viewport_position[1] + this->state->ui.viewport_size[1];
        this->state->camera.input_enabled  = in_viewport_rect && (this->state->ui.viewport_hovered || this->state->ui.viewport_focused) && !io.WantTextInput;
        if (!this->state->camera.input_enabled) {
            this->state->camera.mouse_position_known = false;
            return;
        }
        if (!this->state->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            this->reset_camera();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->set_camera_speed(this->state->camera.speed * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->set_camera_speed(this->state->camera.speed * 0.5f);

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl;
        const bool alt   = io.KeyAlt;
        SpectraCameraPose pose{this->state->camera.eye, this->state->camera.center, this->state->camera.up, this->state->camera.basis_handedness};
        bool camera_changed = false;
        if (!alt) {
            if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid");
            float key_motion_factor = io.DeltaTime;
            if (shift) key_motion_factor *= 5.0f;
            if (ctrl) key_motion_factor *= 0.1f;
            if (key_motion_factor > 0.0f) {
                if (ImGui::IsKeyDown(ImGuiKey_W)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) camera_changed = camera_key_motion(pose, {0.0f, key_motion_factor}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) camera_changed = camera_key_motion(pose, {0.0f, -key_motion_factor}, this->state->camera.speed, false) || camera_changed;
            }
        }

        const std::array<float, 2> viewport_size = this->state->ui.viewport_size;
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        const std::array<float, 2> current_mouse_position{mouse_position.x, mouse_position.y};
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            this->state->camera.mouse_position       = current_mouse_position;
            this->state->camera.mouse_position_known = true;
        }

        const bool left_dragging   = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
        const bool middle_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f);
        const bool right_dragging  = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
        if (left_dragging || middle_dragging || right_dragging) {
            if (!this->state->camera.mouse_position_known) {
                this->state->camera.mouse_position       = current_mouse_position;
                this->state->camera.mouse_position_known = true;
            }
            const std::array<float, 2> mouse_displacement{
                (current_mouse_position[0] - this->state->camera.mouse_position[0]) / viewport_size[0],
                (current_mouse_position[1] - this->state->camera.mouse_position[1]) / viewport_size[1],
            };
            if (left_dragging) {
                if ((ctrl && shift) || alt) camera_changed = camera_orbit(pose, {mouse_displacement[0], -mouse_displacement[1]}, true) || camera_changed;
                else if (shift) camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
                else if (ctrl) camera_changed = camera_pan(pose, mouse_displacement, this->state->camera.fov_degrees, viewport_size) || camera_changed;
                else camera_changed = camera_orbit(pose, mouse_displacement, false) || camera_changed;
            } else if (middle_dragging) {
                camera_changed = camera_pan(pose, mouse_displacement, this->state->camera.fov_degrees, viewport_size) || camera_changed;
            } else if (right_dragging) {
                camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
            }
            this->state->camera.mouse_position = current_mouse_position;
        }

        if (io.MouseWheel != 0.0f && !shift) {
            constexpr float wheel_speed = 10.0f;
            const float wheel_value     = io.MouseWheel * wheel_speed;
            const float dolly_delta     = wheel_value * std::abs(wheel_value) / viewport_size[0];
            camera_changed              = camera_dolly(pose, {dolly_delta, 0.0f}) || camera_changed;
        }

        if (camera_changed) {
            this->state->camera.eye                  = pose.eye;
            this->state->camera.center               = pose.center;
            this->state->camera.up                   = pose.up;
            this->state->camera.basis_handedness     = pose.basis_handedness;
            this->state->camera.moving_from_camera   = moving_from_camera_from_pose(this->state->camera.camera_from_world, pose);
            this->request_pathtracer_accumulation_reset();
        }
    }


    void SpectraGpuInteractivePlugin::draw_viewport_window() {
        const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
        const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
        if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DisplayFramebufferScale.x) || !std::isfinite(io.DisplayFramebufferScale.y) || !(io.DisplayFramebufferScale.x > 0.0f) || !(io.DisplayFramebufferScale.y > 0.0f)) throw std::runtime_error("ImGui framebuffer scale must be finite and positive");
        const std::array<int, 2> viewport_framebuffer_size{
            static_cast<int>(std::round(viewport_size.x * io.DisplayFramebufferScale.x)),
            static_cast<int>(std::round(viewport_size.y * io.DisplayFramebufferScale.y)),
        };
        if (viewport_framebuffer_size[0] <= 0 || viewport_framebuffer_size[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        this->state->ui.viewport_known    = true;
        this->state->ui.viewport_position = {viewport_position.x, viewport_position.y};
        this->state->ui.viewport_size     = {viewport_size.x, viewport_size.y};
        this->state->ui.viewport_framebuffer_size = viewport_framebuffer_size;
        this->state->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
        this->state->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
        this->observe_viewport_render_resolution(viewport_framebuffer_size);
        if (this->pathtracer_ready()) {
            const VkDescriptorSet descriptor = this->pathtracer_viewport_descriptor();
            if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Spectra GPU pathtracer viewport descriptor is null");
            const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
            ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
            ImGui::SetCursorScreenPos(viewport_position);
        } else if (this->state->spectra_scene != nullptr) {
            const char* pending_label = this->state->render_resolution_sync.rebuilding ? "Rebuilding pathtracer" : "Waiting for viewport resolution";
            const ImVec2 text_size = ImGui::CalcTextSize(pending_label);
            ImGui::SetCursorScreenPos(ImVec2{viewport_position.x + std::max(0.0f, (viewport_size.x - text_size.x) * 0.5f), viewport_position.y + std::max(0.0f, (viewport_size.y - text_size.y) * 0.5f)});
            ImGui::TextDisabled("%s", pending_label);
            ImGui::SetCursorScreenPos(viewport_position);
        }
        ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    }

    void SpectraGpuInteractivePlugin::draw_camera_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const PathtracerStatus pathtracer_status = this->pathtracer_status();

            draw_statistics_row("Path Tracer", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Camera Speed");
            ImGui::TableSetColumnIndex(1);
            float speed = this->state->camera.speed;
            const float drag_speed = std::max(std::abs(speed) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraSpeed", &speed, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_speed(speed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement speed in world units per second. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->state->camera.initialized || !this->pathtracer_ready());
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }

    void SpectraGpuInteractivePlugin::draw_scene_browser_window() {

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Asset Info");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", spectra_scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (!ImGui::BeginTabBar("SpectraSceneBrowserTabs")) {
            return;
        }

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTabItem("Render Settings")) {
            if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
                ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                draw_scene_render_setting_row("Pixel Filter", this->state->spectra_scene->description.pixelFilter);
                draw_scene_render_setting_row("Film", this->state->spectra_scene->description.film);
                draw_scene_render_setting_row("Sampler", this->state->spectra_scene->description.sampler);
                draw_scene_render_setting_row("Accelerator", this->state->spectra_scene->description.accelerator);
                draw_scene_render_setting_row("Integrator", this->state->spectra_scene->description.integrator);
                draw_scene_render_setting_row("Camera", this->state->spectra_scene->description.camera);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shapes")) {
            if (this->state->spectra_scene->description.shapes.empty()) {
                ImGui::TextDisabled("No Spectra GPU shapes recorded");
            } else if (ImGui::BeginTable("SpectraSceneShapes", 7, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Area Light", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionShape& shape : this->state->spectra_scene->description.shapes) {
                    const std::string material_text = !shape.materialName.empty() ? shape.materialName : shape.materialIndex >= 0 ? std::format("#{}", shape.materialIndex) : "None";
                    const std::string media_text    = shape.insideMedium.empty() && shape.outsideMedium.empty() ? "None" : std::format("{} / {}", optional_scene_text(shape.insideMedium), optional_scene_text(shape.outsideMedium));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", shape.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", material_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", media_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.objectDefinitionName).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.areaLightType).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextWrapped("%s", scene_file_location_text(shape.location).c_str());
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(spectra_parameter_count_text(shape.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials")) {
            if (this->state->spectra_scene->description.materials.empty()) {
                ImGui::TextDisabled("No Spectra GPU materials recorded");
            } else if (ImGui::BeginTable("SpectraSceneMaterials", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMaterial& material : this->state->spectra_scene->description.materials) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(material.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(material.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(material.named ? "Named" : "Inline");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(material.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(material.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures")) {
            if (this->state->spectra_scene->description.textures.empty()) {
                ImGui::TextDisabled("No Spectra GPU textures recorded");
            } else if (ImGui::BeginTable("SpectraSceneTextures", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Implementation", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionTexture& texture : this->state->spectra_scene->description.textures) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(scene_texture_value_type_label(texture.valueType));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.implementation).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(texture.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(texture.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Media")) {
            if (this->state->spectra_scene->description.mediums.empty()) {
                ImGui::TextDisabled("No Spectra GPU media recorded");
            } else if (ImGui::BeginTable("SpectraSceneMedia", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Medium Interfaces");
            if (this->state->spectra_scene->description.mediumBindings.empty()) {
                ImGui::TextDisabled("No Spectra GPU medium interfaces recorded");
            } else if (ImGui::BeginTable("SpectraSceneMediumInterfaces", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights")) {
            if (this->state->spectra_scene->description.lights.empty()) {
                ImGui::TextDisabled("No Spectra GPU lights recorded");
            } else if (ImGui::BeginTable("SpectraSceneLights", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", light.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(light.area ? "Area" : "Light");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects")) {
            ImGui::SeparatorText("Definitions");
            if (this->state->spectra_scene->description.objectDefinitions.empty()) {
                ImGui::TextDisabled("No Spectra GPU object definitions recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectDefinitions", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shapes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectDefinition& object_definition : this->state->spectra_scene->description.objectDefinitions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_definition.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu", object_definition.shapeIndices.size());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_definition.location).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Instances");
            if (this->state->spectra_scene->description.objectInstances.empty()) {
                ImGui::TextDisabled("No Spectra GPU object instances recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectInstances", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Animated", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectInstance& object_instance : this->state->spectra_scene->description.objectInstances) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_instance.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(object_instance.animatedTransform ? "Yes" : "No");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_instance.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }


        ImGui::EndTabBar();
    }

    void SpectraGpuInteractivePlugin::draw_inspector_window() {

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            return;
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        const std::string viewport_resolution       = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraInspectorPathTracerState", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraInspectorScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", spectra_scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Viewport", viewport_resolution);
            draw_statistics_row("Swapchain", std::format("{} x {}", this->state->context.swapchain_extent().width, this->state->context.swapchain_extent().height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("SpectraInspectorResources", 2, table_flags)) {
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (this->state->gpu_pathtracer != nullptr) {
            ImGui::SeparatorText("Path Tracer");
            if (ImGui::BeginTable("SpectraInspectorPathTracer", 2, table_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Sample", std::format("{} / {}", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count()));
                draw_statistics_row("Completion", std::format("{:.1f}%", this->state->gpu_pathtracer->completion_ratio() * 100.0f));
                draw_statistics_row("Exposure", std::format("{:.3f}", this->state->gpu_pathtracer->current_exposure()));
                ImGui::EndTable();
            }
        }

    }

    void SpectraGpuInteractivePlugin::draw_settings_window() {

        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            return;
        }

        if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Spectra GPU Sampler SPP");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(positive_int_text(this->state->spectra_scene->sampler_sample_count).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Current Sample");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d / %d", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Max Iterations");
            ImGui::TableSetColumnIndex(1);
            const int previous_target_sample_count = this->state->gpu_pathtracer->target_sample_count();
            int target_sample_count                = previous_target_sample_count;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->state->spectra_scene->sampler_sample_count)) {
                this->state->gpu_pathtracer->set_target_sample_count(target_sample_count);
                if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Accumulation");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset Accumulation")) this->request_pathtracer_accumulation_reset();

            ImGui::EndTable();
        }
    }

    void SpectraGpuInteractivePlugin::draw_environment_window() {

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU scene");
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->state->spectra_scene->description.lights.empty()) {
            ImGui::TextDisabled("No Spectra GPU lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->state->spectra_scene->description.mediums.empty()) {
            ImGui::TextDisabled("No Spectra GPU media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->state->spectra_scene->description.mediumBindings.empty()) {
            ImGui::TextDisabled("No Spectra GPU medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
            }
            ImGui::EndTable();
        }

    }

    void SpectraGpuInteractivePlugin::draw_tonemapper_window() {
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->state->gpu_pathtracer->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->state->gpu_pathtracer->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
    }

    void SpectraGpuInteractivePlugin::draw_statistics_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";
        const PathtracerStatus pathtracer_status = this->pathtracer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Path Tracer State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            draw_statistics_row("Scene", this->state->spectra_scene == nullptr ? "No Scene" : spectra_scene_title_text(*this->state->spectra_scene));
            draw_statistics_row("Frame ID", std::format("{}", this->state->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->state->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->state->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->state->context.frame_count()));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->state->context.swapchain_extent().width, this->state->context.swapchain_extent().height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->state->statistics.last_frame_milliseconds));
            if (this->state->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->state->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->state->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU scene");
            return;
        }

        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            return;
        }

        const std::array<int, 2> film_resolution = this->state->spectra_scene->film_resolution;
        const int current_sample                 = this->state->gpu_pathtracer->current_sample();
        const int target_sample                  = this->state->gpu_pathtracer->target_sample_count();
        const float completion_ratio             = this->state->gpu_pathtracer->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", sampling_state);
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", resolution_text(film_resolution));
            if (this->state->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->state->statistics.throughput_mspp.average(), this->state->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->state->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->state->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->state->statistics.last_frame_rendered_sample ? "Rendered sample" : "No Spectra GPU sample");
            ImGui::EndTable();
        }

    }


    std::unique_ptr<SpectraPlugin> create_spectra_gpu_interactive_plugin(std::filesystem::path scene_path) {
        return std::make_unique<SpectraGpuInteractivePlugin>(std::move(scene_path));
    }
} // namespace xayah
