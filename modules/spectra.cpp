module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <cuda_runtime_api.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_mono.h>
#include <roboto/roboto_regular.h>

#include <pbrt/base/film.h>
#include <pbrt/base/sampler.h>
#include <pbrt/gpu/memory.h>
#include <pbrt/gpu/util.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/scene.h>
#include <pbrt/util/color.h>
#include <pbrt/util/transform.h>
#include <pbrt/wavefront/integrator.h>

#include <vulkan/vulkan_raii.hpp>
#include "spectra_raster_spirv.h"
module spectra;
import std;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::ImageAspectFlags aspect, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return VK_FALSE;
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type");
    }

    [[nodiscard]] vk::ExternalMemoryHandleTypeFlagBits pbrt_external_memory_handle_type() {
#if defined(_WIN32)
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] vk::ExternalSemaphoreHandleTypeFlagBits pbrt_external_semaphore_handle_type() {
#if defined(_WIN32)
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalMemoryHandleType pbrt_cuda_external_memory_handle_type() {
#if defined(_WIN32)
        return cudaExternalMemoryHandleTypeOpaqueWin32;
#else
        return cudaExternalMemoryHandleTypeOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalSemaphoreHandleType pbrt_cuda_external_semaphore_handle_type() {
#if defined(_WIN32)
        return cudaExternalSemaphoreHandleTypeOpaqueWin32;
#else
        return cudaExternalSemaphoreHandleTypeOpaqueFd;
#endif
    }

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

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string pbrt_parameter_count_text(const std::vector<xayah::SpectraPbrtParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const xayah::SpectraSceneRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    void draw_scene_render_setting_row(const char* label, const xayah::SpectraSceneRenderSetting& setting) {
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
        ImGui::TextUnformatted(pbrt_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] const char* scene_unsupported_kind_label(const xayah::SpectraSceneUnsupportedFeatureKind kind) {
        switch (kind) {
            case xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform:
                return "Animated Transform";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium:
                return "Participating Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::VdbMedium:
                return "VDB Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::ProceduralTexture:
                return "Procedural Texture";
            case xayah::SpectraSceneUnsupportedFeatureKind::AreaLightInObjectDefinition:
                return "Area Light Instance Policy";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParserAttribute:
                return "Parser Attribute";
        }
        throw std::runtime_error("Unknown Spectra scene unsupported feature kind");
    }

    [[nodiscard]] const char* raster_diagnostic_kind_label(const xayah::SpectraRasterDiagnosticKind kind) {
        switch (kind) {
            case xayah::SpectraRasterDiagnosticKind::UnsupportedShape: return "Unsupported Shape";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial: return "Unsupported Material";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedTexture: return "Unsupported Texture";
            case xayah::SpectraRasterDiagnosticKind::MissingMaterial: return "Missing Material";
            case xayah::SpectraRasterDiagnosticKind::InvalidMesh: return "Invalid Mesh";
            case xayah::SpectraRasterDiagnosticKind::MissingPlyFile: return "Missing PLY File";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform: return "Unsupported Animated Transform";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedObjectInstance: return "Unsupported Object Instance";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAreaLight: return "Unsupported Area Light";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMediumBinding: return "Unsupported Medium Binding";
        }
        throw std::runtime_error("Unknown Spectra raster diagnostic kind");
    }

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

    [[nodiscard]] std::string scene_unsupported_source_text(const xayah::SpectraSceneUnsupportedFeature& feature) {
        if (feature.source_name.empty()) return feature.source_type;
        return std::format("{} {}", feature.source_type, feature.source_name);
    }

    [[nodiscard]] std::string raster_diagnostic_source_text(const xayah::SpectraRasterDiagnostic& diagnostic) {
        if (diagnostic.source_name.empty()) return diagnostic.source_type;
        return std::format("{} {}", diagnostic.source_type, diagnostic.source_name);
    }

    [[nodiscard]] std::array<float, 16> identity_matrix_array() {
        return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    }

    void validate_matrix_array(const std::array<float, 16>& values) {
        for (const float value : values) {
            if (!std::isfinite(value)) throw std::runtime_error("Camera matrix contains a non-finite value");
        }
    }

    [[nodiscard]] std::array<float, 16> multiply_matrix_arrays(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) {
        validate_matrix_array(lhs);
        validate_matrix_array(rhs);
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                float value = 0.0f;
                for (std::size_t index = 0; index < 4; ++index) value += lhs[row * 4 + index] * rhs[index * 4 + column];
                result[row * 4 + column] = value;
            }
        }
        validate_matrix_array(result);
        return result;
    }

    [[nodiscard]] std::array<float, 16> translation_matrix_array(const float x, const float y, const float z) {
        std::array<float, 16> matrix = identity_matrix_array();
        matrix[3]  = x;
        matrix[7]  = y;
        matrix[11] = z;
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] std::array<float, 16> rotation_x_matrix_array(const float degrees) {
        constexpr float radians_per_degree = 0.017453292519943295769f;
        const float radians                = degrees * radians_per_degree;
        const float sin_theta              = std::sin(radians);
        const float cos_theta              = std::cos(radians);
        std::array<float, 16> matrix       = identity_matrix_array();
        matrix[5]                          = cos_theta;
        matrix[6]                          = -sin_theta;
        matrix[9]                          = sin_theta;
        matrix[10]                         = cos_theta;
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] std::array<float, 16> rotation_y_matrix_array(const float degrees) {
        constexpr float radians_per_degree = 0.017453292519943295769f;
        const float radians                = degrees * radians_per_degree;
        const float sin_theta              = std::sin(radians);
        const float cos_theta              = std::cos(radians);
        std::array<float, 16> matrix       = identity_matrix_array();
        matrix[0]                          = cos_theta;
        matrix[2]                          = sin_theta;
        matrix[8]                          = -sin_theta;
        matrix[10]                         = cos_theta;
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform) {
        std::array<float, 16> values{};
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = static_cast<float>(matrix[row][column]);
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                values[row * 4 + column] = value;
            }
        }
        return values;
    }

    [[nodiscard]] pbrt::Transform transform_from_matrix_array(const std::array<float, 16>& values) {
        pbrt::Float matrix[4][4]{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = values[row * 4 + column];
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                matrix[row][column] = static_cast<pbrt::Float>(value);
            }
        }
        return pbrt::Transform{matrix};
    }

    [[nodiscard]] std::array<float, 16> transpose_matrix_array(const std::array<float, 16>& matrix) {
        validate_matrix_array(matrix);
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) result[row * 4 + column] = matrix[column * 4 + row];
        }
        validate_matrix_array(result);
        return result;
    }

    [[nodiscard]] std::array<float, 16> inverse_matrix_array(const std::array<float, 16>& matrix) {
        validate_matrix_array(matrix);
        std::array<float, 16> inverse{};
        inverse[0]  = matrix[5] * matrix[10] * matrix[15] - matrix[5] * matrix[11] * matrix[14] - matrix[9] * matrix[6] * matrix[15] + matrix[9] * matrix[7] * matrix[14] + matrix[13] * matrix[6] * matrix[11] - matrix[13] * matrix[7] * matrix[10];
        inverse[4]  = -matrix[4] * matrix[10] * matrix[15] + matrix[4] * matrix[11] * matrix[14] + matrix[8] * matrix[6] * matrix[15] - matrix[8] * matrix[7] * matrix[14] - matrix[12] * matrix[6] * matrix[11] + matrix[12] * matrix[7] * matrix[10];
        inverse[8]  = matrix[4] * matrix[9] * matrix[15] - matrix[4] * matrix[11] * matrix[13] - matrix[8] * matrix[5] * matrix[15] + matrix[8] * matrix[7] * matrix[13] + matrix[12] * matrix[5] * matrix[11] - matrix[12] * matrix[7] * matrix[9];
        inverse[12] = -matrix[4] * matrix[9] * matrix[14] + matrix[4] * matrix[10] * matrix[13] + matrix[8] * matrix[5] * matrix[14] - matrix[8] * matrix[6] * matrix[13] - matrix[12] * matrix[5] * matrix[10] + matrix[12] * matrix[6] * matrix[9];
        inverse[1]  = -matrix[1] * matrix[10] * matrix[15] + matrix[1] * matrix[11] * matrix[14] + matrix[9] * matrix[2] * matrix[15] - matrix[9] * matrix[3] * matrix[14] - matrix[13] * matrix[2] * matrix[11] + matrix[13] * matrix[3] * matrix[10];
        inverse[5]  = matrix[0] * matrix[10] * matrix[15] - matrix[0] * matrix[11] * matrix[14] - matrix[8] * matrix[2] * matrix[15] + matrix[8] * matrix[3] * matrix[14] + matrix[12] * matrix[2] * matrix[11] - matrix[12] * matrix[3] * matrix[10];
        inverse[9]  = -matrix[0] * matrix[9] * matrix[15] + matrix[0] * matrix[11] * matrix[13] + matrix[8] * matrix[1] * matrix[15] - matrix[8] * matrix[3] * matrix[13] - matrix[12] * matrix[1] * matrix[11] + matrix[12] * matrix[3] * matrix[9];
        inverse[13] = matrix[0] * matrix[9] * matrix[14] - matrix[0] * matrix[10] * matrix[13] - matrix[8] * matrix[1] * matrix[14] + matrix[8] * matrix[2] * matrix[13] + matrix[12] * matrix[1] * matrix[10] - matrix[12] * matrix[2] * matrix[9];
        inverse[2]  = matrix[1] * matrix[6] * matrix[15] - matrix[1] * matrix[7] * matrix[14] - matrix[5] * matrix[2] * matrix[15] + matrix[5] * matrix[3] * matrix[14] + matrix[13] * matrix[2] * matrix[7] - matrix[13] * matrix[3] * matrix[6];
        inverse[6]  = -matrix[0] * matrix[6] * matrix[15] + matrix[0] * matrix[7] * matrix[14] + matrix[4] * matrix[2] * matrix[15] - matrix[4] * matrix[3] * matrix[14] - matrix[12] * matrix[2] * matrix[7] + matrix[12] * matrix[3] * matrix[6];
        inverse[10] = matrix[0] * matrix[5] * matrix[15] - matrix[0] * matrix[7] * matrix[13] - matrix[4] * matrix[1] * matrix[15] + matrix[4] * matrix[3] * matrix[13] + matrix[12] * matrix[1] * matrix[7] - matrix[12] * matrix[3] * matrix[5];
        inverse[14] = -matrix[0] * matrix[5] * matrix[14] + matrix[0] * matrix[6] * matrix[13] + matrix[4] * matrix[1] * matrix[14] - matrix[4] * matrix[2] * matrix[13] - matrix[12] * matrix[1] * matrix[6] + matrix[12] * matrix[2] * matrix[5];
        inverse[3]  = -matrix[1] * matrix[6] * matrix[11] + matrix[1] * matrix[7] * matrix[10] + matrix[5] * matrix[2] * matrix[11] - matrix[5] * matrix[3] * matrix[10] - matrix[9] * matrix[2] * matrix[7] + matrix[9] * matrix[3] * matrix[6];
        inverse[7]  = matrix[0] * matrix[6] * matrix[11] - matrix[0] * matrix[7] * matrix[10] - matrix[4] * matrix[2] * matrix[11] + matrix[4] * matrix[3] * matrix[10] + matrix[8] * matrix[2] * matrix[7] - matrix[8] * matrix[3] * matrix[6];
        inverse[11] = -matrix[0] * matrix[5] * matrix[11] + matrix[0] * matrix[7] * matrix[9] + matrix[4] * matrix[1] * matrix[11] - matrix[4] * matrix[3] * matrix[9] - matrix[8] * matrix[1] * matrix[7] + matrix[8] * matrix[3] * matrix[5];
        inverse[15] = matrix[0] * matrix[5] * matrix[10] - matrix[0] * matrix[6] * matrix[9] - matrix[4] * matrix[1] * matrix[10] + matrix[4] * matrix[2] * matrix[9] + matrix[8] * matrix[1] * matrix[6] - matrix[8] * matrix[2] * matrix[5];

        const float determinant = matrix[0] * inverse[0] + matrix[1] * inverse[4] + matrix[2] * inverse[8] + matrix[3] * inverse[12];
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-20f) throw std::runtime_error("Raster transform matrix is not invertible");
        for (float& value : inverse) value /= determinant;
        validate_matrix_array(inverse);
        return inverse;
    }

    [[nodiscard]] std::array<float, 16> normal_from_local_matrix_array(const std::array<float, 16>& object_from_local) {
        std::array<float, 16> normal_from_local = transpose_matrix_array(inverse_matrix_array(object_from_local));
        normal_from_local[3]                    = 0.0f;
        normal_from_local[7]                    = 0.0f;
        normal_from_local[11]                   = 0.0f;
        normal_from_local[12]                   = 0.0f;
        normal_from_local[13]                   = 0.0f;
        normal_from_local[14]                   = 0.0f;
        normal_from_local[15]                   = 1.0f;
        validate_matrix_array(normal_from_local);
        return normal_from_local;
    }

    [[nodiscard]] std::array<float, 3> transform_point_array(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
        validate_matrix_array(matrix);
        const float x = matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3];
        const float y = matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7];
        const float z = matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11];
        const float w = matrix[12] * point[0] + matrix[13] * point[1] + matrix[14] * point[2] + matrix[15];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) || std::abs(w) <= 1.0e-20f) throw std::runtime_error("Raster transformed point is invalid");
        return {x / w, y / w, z / w};
    }

    [[nodiscard]] std::array<float, 16> raster_perspective_matrix(const float fov_degrees, const float aspect) {
        if (!std::isfinite(fov_degrees) || !(fov_degrees > 0.0f) || !(fov_degrees < 180.0f)) throw std::runtime_error("Raster perspective fov must be finite and inside (0, 180)");
        if (!std::isfinite(aspect) || !(aspect > 0.0f)) throw std::runtime_error("Raster perspective aspect ratio must be finite and positive");
        constexpr float radians_per_degree = 0.017453292519943295769f;
        constexpr float near_plane         = 0.01f;
        constexpr float far_plane          = 1000000.0f;
        const float focal                  = 1.0f / std::tan(fov_degrees * radians_per_degree * 0.5f);
        std::array<float, 16> matrix{};
        matrix[0]  = focal / aspect;
        matrix[5]  = -focal;
        matrix[10] = far_plane / (far_plane - near_plane);
        matrix[11] = -(far_plane * near_plane) / (far_plane - near_plane);
        matrix[14] = 1.0f;
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] float raster_camera_fov_degrees(const xayah::SpectraScene& scene) {
        if (!scene.camera.present) throw std::runtime_error("Rasterizer requires an explicit PBRT perspective camera");
        if (scene.camera.name != "perspective") throw std::runtime_error(std::format("Rasterizer v1 requires a PBRT perspective camera, not \"{}\"", scene.camera.name));
        constexpr float pbrt_perspective_default_fov = 90.0f;
        for (const xayah::SpectraPbrtParameter& parameter : scene.camera.parameters) {
            if (parameter.name != "fov") continue;
            if (parameter.floats.size() != 1) throw std::runtime_error("PBRT perspective camera fov must have exactly one float value");
            return parameter.floats.front();
        }
        return pbrt_perspective_default_fov;
    }

    [[nodiscard]] std::array<float, 16> raster_view_projection_matrix(const xayah::SpectraScene& scene, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) {
        if (scene.film_resolution[0] <= 0 || scene.film_resolution[1] <= 0) throw std::runtime_error("Rasterizer requires positive PBRT film resolution metadata");
        const float aspect = static_cast<float>(scene.film_resolution[0]) / static_cast<float>(scene.film_resolution[1]);
        const std::array<float, 16> projection = raster_perspective_matrix(raster_camera_fov_degrees(scene), aspect);
        const std::array<float, 16> moved_camera_from_world = multiply_matrix_arrays(moving_from_camera, camera_from_world);
        return multiply_matrix_arrays(projection, moved_camera_from_world);
    }

    [[nodiscard]] ImVec4 imgui_srgb(const float red, const float green, const float blue, const float alpha) {
        return ImVec4{red, green, blue, alpha};
    }

    void load_imgui_fonts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts == nullptr) throw std::runtime_error("ImGui font atlas is unavailable");

        ImFontConfig font_config{};
        font_config.OversampleH   = 3;
        font_config.OversampleV   = 3;
        constexpr float font_size = 15.0f;
        ImFont* default_font      = io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_regular_compressed_data, g_roboto_regular_compressed_size, font_size, &font_config);
        if (default_font == nullptr) throw std::runtime_error("Failed to load Roboto regular font");

        ImFontConfig icon_config{};
        icon_config.MergeMode     = true;
        icon_config.PixelSnapH    = true;
        icon_config.OversampleH   = 3;
        icon_config.OversampleV   = 3;
        constexpr float icon_size = 1.28571429f * font_size;
        icon_config.GlyphOffset.x = icon_size * 0.01f;
        icon_config.GlyphOffset.y = icon_size * 0.2f;
        constexpr std::array<ImWchar, 3> icon_ranges{ICON_MIN_MS, ICON_MAX_MS, 0};
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_materialSymbolsRounded_compressed_data, g_materialSymbolsRounded_compressed_size, icon_size, &icon_config, icon_ranges.data()) == nullptr) throw std::runtime_error("Failed to load Material Symbols icon font");

        ImFontConfig mono_config{};
        mono_config.OversampleH = 3;
        mono_config.OversampleV = 3;
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_mono_compressed_data, g_roboto_mono_compressed_size, font_size, &mono_config) == nullptr) throw std::runtime_error("Failed to load Roboto mono font");
        io.FontDefault = default_font;
    }

    void apply_imgui_style(const bool viewports) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style                  = ImGui::GetStyle();
        style.WindowRounding               = 0.0f;
        style.WindowBorderSize             = 0.0f;
        style.ColorButtonPosition          = ImGuiDir_Right;
        style.FrameRounding                = 2.0f;
        style.FrameBorderSize              = 1.0f;
        style.GrabRounding                 = 4.0f;
        style.IndentSpacing                = 12.0f;
        style.Colors[ImGuiCol_WindowBg]    = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg]   = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_PopupBg]     = imgui_srgb(0.135f, 0.135f, 0.135f, 1.0f);
        style.Colors[ImGuiCol_Border]      = imgui_srgb(0.4f, 0.4f, 0.4f, 0.5f);
        style.Colors[ImGuiCol_FrameBg]     = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);

        const ImVec4 normal_color = imgui_srgb(0.465f, 0.465f, 0.525f, 1.0f);
        constexpr std::array normal_colors{
            ImGuiCol_Header,
            ImGuiCol_SliderGrab,
            ImGuiCol_Button,
            ImGuiCol_CheckMark,
            ImGuiCol_ResizeGrip,
            ImGuiCol_TextSelectedBg,
            ImGuiCol_Separator,
            ImGuiCol_FrameBgActive,
        };
        for (const ImGuiCol color_id : normal_colors) style.Colors[color_id] = normal_color;

        const ImVec4 active_color = imgui_srgb(0.365f, 0.365f, 0.425f, 1.0f);
        constexpr std::array active_colors{
            ImGuiCol_HeaderActive,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_ButtonActive,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SeparatorActive,
        };
        for (const ImGuiCol color_id : active_colors) style.Colors[color_id] = active_color;

        const ImVec4 hovered_color = imgui_srgb(0.565f, 0.565f, 0.625f, 1.0f);
        constexpr std::array hovered_colors{
            ImGuiCol_HeaderHovered,
            ImGuiCol_ButtonHovered,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_SeparatorHovered,
        };
        for (const ImGuiCol color_id : hovered_colors) style.Colors[color_id] = hovered_color;

        style.Colors[ImGuiCol_TitleBgActive]    = imgui_srgb(0.465f, 0.465f, 0.465f, 1.0f);
        style.Colors[ImGuiCol_TitleBg]          = imgui_srgb(0.125f, 0.125f, 0.125f, 1.0f);
        style.Colors[ImGuiCol_Tab]              = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);
        style.Colors[ImGuiCol_TabHovered]       = imgui_srgb(0.465f, 0.495f, 0.525f, 1.0f);
        style.Colors[ImGuiCol_TabActive]        = imgui_srgb(0.282f, 0.290f, 0.302f, 1.0f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = imgui_srgb(0.465f, 0.465f, 0.465f, 0.350f);
        style.Colors[ImGuiCol_ButtonActive]     = static_cast<ImVec4>(ImColor::HSV(0.3F, 0.5F, 0.5F));
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        if (viewports) {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }
} // namespace

namespace xayah {
    struct SpectraPbrtInteractiveSession {
        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            cudaExternalMemory_t cuda_external_memory{};
            cudaExternalSemaphore_t cuda_external_semaphore{};
            float* cuda_pixels{nullptr};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        std::filesystem::path scene_path{};
        std::unique_ptr<pbrt::WavefrontPathIntegrator> integrator{};
        pbrt::Bounds2i pixel_bounds{};
        pbrt::Vector2i resolution{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        pbrt::Transform render_from_camera{};
        pbrt::Transform camera_from_render{};
        pbrt::Transform camera_from_world{};
        pbrt::Float exposure{1.0f};
        pbrt::Float initial_move_scale{1.0f};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};

        SpectraPbrtInteractiveSession(const SpectraScene& spectra_scene, pbrt::BasicScene& backend_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : scene_path{spectra_scene.scene_path} {
            try {
                if (this->scene_path.empty()) throw std::runtime_error("PBRT scene path is empty");
                if (!std::filesystem::exists(this->scene_path)) throw std::runtime_error(std::string{"PBRT scene does not exist: "} + this->scene_path.string());
                if (spectra_scene.pbrt_directives.empty()) throw std::runtime_error("Spectra scene has no PBRT parser directives");
                if (frame_count == 0) throw std::runtime_error("PBRT interactive requires at least one frame in flight");
                this->physical_device = &physical_device;
                this->device          = &device;
                this->frame_count     = frame_count;

                this->integrator = std::make_unique<pbrt::WavefrontPathIntegrator>(&pbrt::CUDATrackedMemoryResource::singleton, backend_scene);
#ifdef PBRT_BUILD_GPU_RENDERER
                if (pbrt::Options != nullptr && pbrt::Options->useGPU) this->integrator->PrefetchGPUAllocations();
#endif
                this->pixel_bounds = this->integrator->film.PixelBounds();
                this->resolution   = this->pixel_bounds.Diagonal();
                if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive");
                this->max_samples = this->integrator->sampler.SamplesPerPixel();
                if (this->max_samples <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
                this->target_samples = this->max_samples;
                this->render_one_sample(pbrt::Transform{});
                pbrt::GPUWait();

                this->render_from_camera = this->integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
                this->camera_from_render = pbrt::Inverse(this->render_from_camera);
                this->camera_from_world  = this->integrator->camera.GetCameraTransform().CameraFromWorld(this->integrator->camera.SampleTime(0.0f));
                const pbrt::Bounds3f scene_bounds = this->integrator->aggregate->Bounds();
                this->initial_move_scale          = pbrt::Length(scene_bounds.Diagonal()) / 1000.0f;
                if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT scene bounds must define a positive interactive move scale");

                this->validate_cuda_vulkan_device(physical_device);
                this->create_frame_resources(physical_device, device, frame_count);
                this->create_imgui_descriptors();
            } catch (...) {
                this->destroy_resources_noexcept();
                throw;
            }
        }

        ~SpectraPbrtInteractiveSession() noexcept {
            this->destroy_resources_noexcept();
        }

        void destroy_resources_noexcept() noexcept {
            try {
                if (this->device != nullptr) this->device->waitIdle();
                if (pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
            } catch (...) {
            }
            this->destroy_frame_resources_noexcept();
            this->integrator.reset();
        }

        SpectraPbrtInteractiveSession(const SpectraPbrtInteractiveSession& other)                = delete;
        SpectraPbrtInteractiveSession(SpectraPbrtInteractiveSession&& other) noexcept            = delete;
        SpectraPbrtInteractiveSession& operator=(const SpectraPbrtInteractiveSession& other)     = delete;
        SpectraPbrtInteractiveSession& operator=(SpectraPbrtInteractiveSession&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const {
            if (this->reset_requested) return 0;
            return this->sample_index;
        }

        [[nodiscard]] int sampler_sample_count() const {
            return this->max_samples;
        }

        [[nodiscard]] int target_sample_count() const {
            return this->target_samples;
        }

        [[nodiscard]] float current_exposure() const {
            return static_cast<float>(this->exposure);
        }

        [[nodiscard]] float camera_initial_move_scale() const {
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT interactive camera initial move scale must be positive");
            return static_cast<float>(this->initial_move_scale);
        }

        [[nodiscard]] std::array<int, 2> film_resolution() const {
            if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before metadata is queried");
            return {this->resolution.x, this->resolution.y};
        }

        [[nodiscard]] std::array<float, 16> camera_from_world_matrix() const {
            return matrix_array_from_transform(this->camera_from_world);
        }

        [[nodiscard]] std::uint64_t film_pixel_count() const {
            if (this->resolution.x <= 0 || this->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before statistics are queried");
            return static_cast<std::uint64_t>(this->resolution.x) * static_cast<std::uint64_t>(this->resolution.y);
        }

        [[nodiscard]] float completion_ratio() const {
            if (this->target_samples <= 0) throw std::runtime_error("PBRT target sample count must be positive before statistics are queried");
            const int visible_sample = this->current_sample();
            if (visible_sample < 0 || visible_sample > this->target_samples) throw std::runtime_error("PBRT visible sample count is outside the target sample range");
            return static_cast<float>(visible_sample) / static_cast<float>(this->target_samples);
        }

        [[nodiscard]] VkDescriptorSet active_descriptor() const {
            if (this->frames.empty()) return VK_NULL_HANDLE;
            return this->frames.at(this->active_frame_index).imgui_descriptor;
        }

        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT completion semaphore requested without frame resources");
            return *this->frames.at(this->active_frame_index).cuda_complete_semaphore;
        }

        void set_target_sample_count(const int target_sample_count) {
            if (target_sample_count < 1 || target_sample_count > this->max_samples) throw std::runtime_error("PBRT target sample count is outside the sampler SPP range");
            if (target_sample_count == this->target_samples) return;
            this->target_samples = target_sample_count;
            this->request_reset_accumulation();
        }

        void set_exposure(const float value) {
            if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("PBRT exposure must be in [0.001, 1000]");
            this->exposure = static_cast<pbrt::Float>(value);
        }

        void request_reset_accumulation() {
            this->reset_requested = true;
        }

        void release_imgui_descriptors() noexcept {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                    frame.imgui_descriptor = VK_NULL_HANDLE;
                }
            }
        }

        void create_imgui_descriptors() {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("PBRT interactive ImGui descriptor is already allocated");
                frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate ImGui descriptor for PBRT interactive image");
            }
        }

        void destroy_frame_resources_noexcept() noexcept {
            this->release_imgui_descriptors();
            for (FrameResource& frame : this->frames) {
                if (frame.cuda_pixels != nullptr) {
                    cudaFree(frame.cuda_pixels);
                    frame.cuda_pixels = nullptr;
                }
                if (frame.cuda_external_semaphore != nullptr) {
                    cudaDestroyExternalSemaphore(frame.cuda_external_semaphore);
                    frame.cuda_external_semaphore = nullptr;
                }
                if (frame.cuda_external_memory != nullptr) {
                    cudaDestroyExternalMemory(frame.cuda_external_memory);
                    frame.cuda_external_memory = nullptr;
                }
            }
            this->frames.clear();
            this->active_frame_index = 0;
        }

        void render_one_sample(const pbrt::Transform& camera_motion) {
            if (this->sample_index >= this->target_samples) throw std::runtime_error("PBRT sample index is already at the target sample count");
            this->integrator->RenderSample(this->pixel_bounds, camera_motion, this->sample_index);
            ++this->sample_index;
        }

        void rerender_after_reset(const std::uint32_t frame_index, const pbrt::Transform& camera_motion) {
            if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("PBRT interactive Vulkan handles are not available for reset");
            this->device->waitIdle();
            this->destroy_frame_resources_noexcept();
            this->integrator->ResetFilm(this->pixel_bounds);
            pbrt::GPUWait();
            this->sample_index     = 0;
            this->reset_requested  = false;
            this->render_one_sample(camera_motion);
            pbrt::GPUWait();
            this->create_frame_resources(*this->physical_device, *this->device, this->frame_count);
            this->create_imgui_descriptors();
            this->active_frame_index = frame_index;
        }

        [[nodiscard]] RenderFrameResult render_frame(const std::uint32_t frame_index, const std::array<float, 16>& moving_from_camera_matrix) {
            if (frame_index >= this->frames.size()) throw std::runtime_error("PBRT interactive frame index is out of range");
            this->active_frame_index = frame_index;
            RenderFrameResult result{};
            const pbrt::Transform moving_from_camera = transform_from_matrix_array(moving_from_camera_matrix);
            const pbrt::Transform camera_motion = this->render_from_camera * moving_from_camera * this->camera_from_render;
            if (this->reset_requested) {
                this->rerender_after_reset(frame_index, camera_motion);
                result.rendered_sample    = true;
                result.sample_pixels      = this->film_pixel_count() * static_cast<std::uint64_t>(this->sample_index);
                result.reset_accumulation = true;
            } else if (this->sample_index < this->target_samples) {
                this->render_one_sample(camera_motion);
                result.rendered_sample = true;
                result.sample_pixels   = this->film_pixel_count();
            }
            FrameResource& output_frame = this->frames.at(frame_index);
            this->integrator->UpdateFramebufferFromFilm(this->pixel_bounds, this->exposure, output_frame.cuda_pixels);

            cudaExternalSemaphoreSignalParams signal_params{};
            CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
            return result;
        }

        void record_copy(const vk::raii::CommandBuffer& command_buffer) {
            FrameResource& frame = this->frames.at(this->active_frame_index);
            const vk::PipelineStageFlags2 src_image_stage = frame.image_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader;
            const vk::AccessFlags2 src_image_access       = frame.image_layout == vk::ImageLayout::eUndefined ? vk::AccessFlagBits2::eNone : vk::AccessFlagBits2::eShaderSampledRead;
            transition_image_layout(command_buffer, *frame.image, frame.image_layout, vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor, src_image_stage, src_image_access, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            frame.image_layout = vk::ImageLayout::eTransferDstOptimal;

            const vk::BufferMemoryBarrier2 buffer_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eMemoryWrite,
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                *frame.interop_buffer,
                0,
                frame.interop_buffer_size,
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_barrier, 0, nullptr};
            command_buffer.pipelineBarrier2(dependency_info);

            const vk::BufferImageCopy copy_region{
                0,
                0,
                0,
                {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {static_cast<std::uint32_t>(this->resolution.x), static_cast<std::uint32_t>(this->resolution.y), 1},
            };
            command_buffer.copyBufferToImage(*frame.interop_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, copy_region);

            transition_image_layout(command_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame.image_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

    private:
        void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const {
            int cuda_device = 0;
            CUDA_CHECK(cudaGetDevice(&cuda_device));
            cudaDeviceProp cuda_properties{};
            CUDA_CHECK(cudaGetDeviceProperties(&cuda_properties, cuda_device));
            const auto vulkan_properties = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
            const vk::PhysicalDeviceIDProperties& vulkan_id = vulkan_properties.get<vk::PhysicalDeviceIDProperties>();
#if defined(_WIN32)
            if (!vulkan_id.deviceLUIDValid) throw std::runtime_error("Selected Vulkan device does not expose a valid LUID for CUDA interop");
            for (std::size_t index = 0; index < VK_LUID_SIZE; ++index) {
                if (static_cast<unsigned char>(cuda_properties.luid[index]) != vulkan_id.deviceLUID[index]) throw std::runtime_error("CUDA device LUID does not match selected Vulkan device LUID");
            }
#else
            for (std::size_t index = 0; index < VK_UUID_SIZE; ++index) {
                if (static_cast<unsigned char>(cuda_properties.uuid.bytes[index]) != vulkan_id.deviceUUID[index]) throw std::runtime_error("CUDA device UUID does not match selected Vulkan device UUID");
            }
#endif
        }

        void create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
            const vk::FormatProperties format_properties = physical_device.getFormatProperties(this->display_format);
            constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
            if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

            const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(this->resolution.x) * static_cast<vk::DeviceSize>(this->resolution.y);
            if (rgba_bytes == 0) throw std::runtime_error("PBRT interactive interop buffer cannot be zero bytes");
            this->frames.resize(frame_count);
            for (FrameResource& frame : this->frames) {
                this->create_interop_buffer(physical_device, device, frame, rgba_bytes);
                this->create_cuda_complete_semaphore(device, frame);
                this->create_display_image(physical_device, device, frame, this->display_format);
            }
        }

        void create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::DeviceSize rgba_bytes) {
            const vk::ExternalMemoryBufferCreateInfo external_buffer_info{pbrt_external_memory_handle_type()};
            const vk::BufferCreateInfo buffer_create_info{{}, rgba_bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 0, nullptr, &external_buffer_info};
            frame.interop_buffer = vk::raii::Buffer{device, buffer_create_info};

            const vk::MemoryRequirements memory_requirements = frame.interop_buffer.getMemoryRequirements();
            const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::ExportMemoryAllocateInfo export_allocate_info{pbrt_external_memory_handle_type()};
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type, &export_allocate_info};
            frame.interop_memory = vk::raii::DeviceMemory{device, allocate_info};
            frame.interop_buffer.bindMemory(*frame.interop_memory, 0);
            frame.interop_allocation_size = memory_requirements.size;
            frame.interop_buffer_size     = rgba_bytes;

            cudaExternalMemoryHandleDesc memory_handle_desc{};
            memory_handle_desc.type = pbrt_cuda_external_memory_handle_type();
            memory_handle_desc.size = static_cast<unsigned long long>(frame.interop_allocation_size);
#if defined(_WIN32)
            const vk::MemoryGetWin32HandleInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
            HANDLE memory_handle = device.getMemoryWin32HandleKHR(memory_handle_info);
            if (memory_handle == nullptr) throw std::runtime_error("Failed to export Vulkan memory Win32 handle for CUDA");
            memory_handle_desc.handle.win32.handle = memory_handle;
            CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
            CloseHandle(memory_handle);
#else
            const vk::MemoryGetFdInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
            int memory_fd = device.getMemoryFdKHR(memory_handle_info);
            if (memory_fd < 0) throw std::runtime_error("Failed to export Vulkan memory FD for CUDA");
            memory_handle_desc.handle.fd = memory_fd;
            CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
            close(memory_fd);
#endif

            cudaExternalMemoryBufferDesc buffer_desc{};
            buffer_desc.offset = 0;
            buffer_desc.size   = static_cast<unsigned long long>(frame.interop_buffer_size);
            CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void**>(&frame.cuda_pixels), frame.cuda_external_memory, &buffer_desc));
            if (frame.cuda_pixels == nullptr) throw std::runtime_error("CUDA external memory mapped to a null PBRT RGBA pointer");
        }

        void create_cuda_complete_semaphore(const vk::raii::Device& device, FrameResource& frame) {
            const vk::ExportSemaphoreCreateInfo export_semaphore_info{pbrt_external_semaphore_handle_type()};
            const vk::SemaphoreCreateInfo semaphore_create_info{{}, &export_semaphore_info};
            frame.cuda_complete_semaphore = vk::raii::Semaphore{device, semaphore_create_info};

            cudaExternalSemaphoreHandleDesc semaphore_handle_desc{};
            semaphore_handle_desc.type = pbrt_cuda_external_semaphore_handle_type();
#if defined(_WIN32)
            const vk::SemaphoreGetWin32HandleInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
            HANDLE semaphore_handle = device.getSemaphoreWin32HandleKHR(semaphore_handle_info);
            if (semaphore_handle == nullptr) throw std::runtime_error("Failed to export Vulkan semaphore Win32 handle for CUDA");
            semaphore_handle_desc.handle.win32.handle = semaphore_handle;
            CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
            CloseHandle(semaphore_handle);
#else
            const vk::SemaphoreGetFdInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
            int semaphore_fd = device.getSemaphoreFdKHR(semaphore_handle_info);
            if (semaphore_fd < 0) throw std::runtime_error("Failed to export Vulkan semaphore FD for CUDA");
            semaphore_handle_desc.handle.fd = semaphore_fd;
            CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
            close(semaphore_fd);
#endif
            if (frame.cuda_external_semaphore == nullptr) throw std::runtime_error("CUDA external semaphore import returned null");
        }

        void create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::Format display_format) {
            const vk::ImageCreateInfo image_create_info{
                {},
                vk::ImageType::e2D,
                display_format,
                vk::Extent3D{static_cast<std::uint32_t>(this->resolution.x), static_cast<std::uint32_t>(this->resolution.y), 1},
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined,
            };
            frame.image = vk::raii::Image{device, image_create_info};

            const vk::MemoryRequirements memory_requirements = frame.image.getMemoryRequirements();
            const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            frame.image_memory = vk::raii::DeviceMemory{device, allocate_info};
            frame.image.bindMemory(*frame.image_memory, 0);

            const vk::ImageViewCreateInfo image_view_create_info{
                {},
                *frame.image,
                vk::ImageViewType::e2D,
                display_format,
                {},
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            frame.image_view = vk::raii::ImageView{device, image_view_create_info};

            const vk::SamplerCreateInfo sampler_create_info{
                {},
                vk::Filter::eNearest,
                vk::Filter::eNearest,
                vk::SamplerMipmapMode::eNearest,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                vk::SamplerAddressMode::eClampToEdge,
                0.0f,
                VK_FALSE,
                1.0f,
                VK_FALSE,
                vk::CompareOp::eNever,
                0.0f,
                0.0f,
                vk::BorderColor::eFloatOpaqueBlack,
                VK_FALSE,
            };
            frame.sampler = vk::raii::Sampler{device, sampler_create_info};
        }
    };

    struct SpectraVulkanRasterizer {
        struct FrameResource {
            vk::raii::DeviceMemory color_memory{nullptr};
            vk::raii::Image color_image{nullptr};
            vk::raii::ImageView color_image_view{nullptr};
            vk::raii::Sampler color_sampler{nullptr};
            vk::ImageLayout color_layout{vk::ImageLayout::eUndefined};
            vk::raii::DeviceMemory depth_memory{nullptr};
            vk::raii::Image depth_image{nullptr};
            vk::raii::ImageView depth_image_view{nullptr};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
        };

        struct BufferResource {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize size{0};
        };

        struct SpectraRasterDrawGpu {
            std::array<float, 16> object_from_local{};
            std::array<float, 16> normal_from_local{};
            std::uint32_t material_index{0};
            std::array<std::uint32_t, 7> padding{};
        };

        struct SpectraRasterMaterialGpu {
            std::array<float, 4> base_color_roughness{};
        };

        struct SpectraRasterPushConstants {
            std::array<float, 16> view_projection{};
            std::uint32_t draw_index{0};
            std::array<std::uint32_t, 7> padding{};
        };
        static_assert(sizeof(SpectraRasterDrawGpu) == 160);
        static_assert(sizeof(SpectraRasterPushConstants) == 96);

        const SpectraScene* scene{nullptr};
        const SpectraRasterScene* raster_scene{nullptr};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        const vk::raii::Queue* graphics_queue{nullptr};
        const vk::raii::CommandPool* command_pool{nullptr};
        vk::Extent2D extent{};
        vk::Format color_format{vk::Format::eR8G8B8A8Unorm};
        vk::Format depth_format{vk::Format::eD32Sfloat};
        std::uint32_t active_frame_index{0};
        std::size_t draw_count{0};
        std::size_t triangle_count{0};
        float initial_move_scale{1.0f};
        BufferResource vertex_buffer{};
        BufferResource index_buffer{};
        BufferResource draw_buffer{};
        BufferResource material_buffer{};
        vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::DescriptorSets descriptor_sets{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::ShaderModule vertex_shader{nullptr};
        vk::raii::ShaderModule fragment_shader{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
        std::vector<FrameResource> frames{};

        SpectraVulkanRasterizer(const SpectraScene& scene, const SpectraRasterScene& raster_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::Queue& graphics_queue, const vk::raii::CommandPool& command_pool, const std::uint32_t frame_count) : scene{&scene}, raster_scene{&raster_scene}, physical_device{&physical_device}, device{&device}, graphics_queue{&graphics_queue}, command_pool{&command_pool} {
            try {
                if (frame_count == 0) throw std::runtime_error("Vulkan rasterizer requires at least one frame in flight");
                if (scene.film_resolution[0] <= 0 || scene.film_resolution[1] <= 0) throw std::runtime_error("Vulkan rasterizer requires positive PBRT film resolution metadata");
                this->extent = vk::Extent2D{static_cast<std::uint32_t>(scene.film_resolution[0]), static_cast<std::uint32_t>(scene.film_resolution[1])};
                this->validate_formats();
                this->create_scene_buffers();
                this->create_frame_resources(frame_count);
                this->create_descriptors();
                this->create_pipeline();
                this->create_imgui_descriptors();
            } catch (...) {
                this->destroy_resources_noexcept();
                throw;
            }
        }

        ~SpectraVulkanRasterizer() noexcept {
            this->destroy_resources_noexcept();
        }

        SpectraVulkanRasterizer(const SpectraVulkanRasterizer& other)                = delete;
        SpectraVulkanRasterizer(SpectraVulkanRasterizer&& other) noexcept            = delete;
        SpectraVulkanRasterizer& operator=(const SpectraVulkanRasterizer& other)     = delete;
        SpectraVulkanRasterizer& operator=(SpectraVulkanRasterizer&& other) noexcept = delete;

        [[nodiscard]] VkDescriptorSet active_descriptor() const {
            if (this->frames.empty()) return VK_NULL_HANDLE;
            return this->frames.at(this->active_frame_index).imgui_descriptor;
        }

        [[nodiscard]] float camera_initial_move_scale() const {
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("Vulkan rasterizer camera initial move scale must be positive");
            return this->initial_move_scale;
        }

        void render_frame(const std::uint32_t frame_index) {
            if (frame_index >= this->frames.size()) throw std::runtime_error("Vulkan rasterizer frame index is out of range");
            this->active_frame_index = frame_index;
        }

        void record_draw(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) {
            if (this->scene == nullptr || this->raster_scene == nullptr) throw std::runtime_error("Vulkan rasterizer cannot record without scene data");
            FrameResource& frame = this->frames.at(this->active_frame_index);
            const vk::PipelineStageFlags2 color_src_stage = frame.color_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader;
            const vk::AccessFlags2 color_src_access       = frame.color_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead;
            transition_image_layout(command_buffer, *frame.color_image, frame.color_layout, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor, color_src_stage, color_src_access, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
            frame.color_layout = vk::ImageLayout::eColorAttachmentOptimal;
            if (frame.depth_layout == vk::ImageLayout::eUndefined) {
                transition_image_layout(command_buffer, *frame.depth_image, frame.depth_layout, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth, vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
                frame.depth_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            }

            constexpr std::array<float, 4> clear_color{0.025f, 0.025f, 0.028f, 1.0f};
            const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
            const vk::RenderingAttachmentInfo color_attachment{
                *frame.color_image_view,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                {},
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                color_clear_value,
            };
            const vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0}};
            const vk::RenderingAttachmentInfo depth_attachment{
                *frame.depth_image_view,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                {},
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                depth_clear_value,
            };
            const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->extent}, 1, 0, 1, &color_attachment, &depth_attachment, nullptr};
            command_buffer.beginRendering(rendering_info);
            if (this->draw_count > 0) this->record_geometry(command_buffer, camera_from_world, moving_from_camera);
            command_buffer.endRendering();

            transition_image_layout(command_buffer, *frame.color_image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame.color_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }

        void release_imgui_descriptors() noexcept {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                    frame.imgui_descriptor = VK_NULL_HANDLE;
                }
            }
        }

        void create_imgui_descriptors() {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Vulkan rasterizer ImGui descriptor is already allocated");
                frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.color_sampler), static_cast<VkImageView>(*frame.color_image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate ImGui descriptor for Vulkan rasterizer image");
            }
        }

        void destroy_resources_noexcept() noexcept {
            try {
                if (this->device != nullptr) this->device->waitIdle();
            } catch (...) {
            }
            this->release_imgui_descriptors();
            this->frames.clear();
            this->pipeline = nullptr;
            this->fragment_shader = nullptr;
            this->vertex_shader = nullptr;
            this->pipeline_layout = nullptr;
            this->descriptor_sets.clear();
            this->descriptor_pool = nullptr;
            this->descriptor_set_layout = nullptr;
            this->material_buffer = {};
            this->draw_buffer = {};
            this->index_buffer = {};
            this->vertex_buffer = {};
            this->active_frame_index = 0;
        }

        [[nodiscard]] std::size_t vertex_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->vertices.size();
        }

        [[nodiscard]] std::size_t index_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->indices.size();
        }

        [[nodiscard]] std::size_t material_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->materials.size();
        }

        [[nodiscard]] std::size_t diagnostic_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->diagnostics.size();
        }

    private:
        void validate_formats() const {
            const vk::FormatProperties color_properties = this->physical_device->getFormatProperties(this->color_format);
            constexpr vk::FormatFeatureFlags color_required = vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
            if ((color_properties.optimalTilingFeatures & color_required) != color_required) throw std::runtime_error("Vulkan device does not support sampled color attachment R8G8B8A8_UNORM images");
            const vk::FormatProperties depth_properties = this->physical_device->getFormatProperties(this->depth_format);
            if ((depth_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlagBits::eDepthStencilAttachment) throw std::runtime_error("Vulkan device does not support D32_SFLOAT depth attachment images");
        }

        [[nodiscard]] BufferResource create_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags memory_properties) const {
            if (size == 0) throw std::runtime_error("Vulkan rasterizer cannot create a zero-sized buffer");
            BufferResource resource{};
            const vk::BufferCreateInfo buffer_create_info{{}, size, usage, vk::SharingMode::eExclusive};
            resource.buffer = vk::raii::Buffer{*this->device, buffer_create_info};
            const vk::MemoryRequirements memory_requirements = resource.buffer.getMemoryRequirements();
            const std::uint32_t memory_type = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, memory_properties);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            resource.memory = vk::raii::DeviceMemory{*this->device, allocate_info};
            resource.buffer.bindMemory(*resource.memory, 0);
            resource.size = size;
            return resource;
        }

        void submit_upload(const vk::raii::Buffer& staging_buffer, const vk::raii::Buffer& destination_buffer, const vk::DeviceSize size) const {
            const vk::CommandBufferAllocateInfo allocate_info{**this->command_pool, vk::CommandBufferLevel::ePrimary, 1};
            vk::raii::CommandBuffers command_buffers{*this->device, allocate_info};
            const vk::raii::CommandBuffer& command_buffer = command_buffers.front();
            constexpr vk::CommandBufferBeginInfo begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
            command_buffer.begin(begin_info);
            const vk::BufferCopy copy_region{0, 0, size};
            command_buffer.copyBuffer(*staging_buffer, *destination_buffer, copy_region);
            const vk::BufferMemoryBarrier2 buffer_barrier{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eShaderStorageRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                *destination_buffer,
                0,
                size,
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_barrier, 0, nullptr};
            command_buffer.pipelineBarrier2(dependency_info);
            command_buffer.end();

            const vk::CommandBufferSubmitInfo command_buffer_submit_info{*command_buffer};
            const vk::SubmitInfo2 submit_info{{}, 0, nullptr, 1, &command_buffer_submit_info, 0, nullptr};
            this->graphics_queue->submit2(submit_info, nullptr);
            this->graphics_queue->waitIdle();
        }

        template <typename T>
        [[nodiscard]] BufferResource upload_vector_buffer(const std::vector<T>& values, const vk::BufferUsageFlags usage) const {
            if (values.empty()) throw std::runtime_error("Vulkan rasterizer cannot upload an empty typed buffer");
            const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(sizeof(T)) * static_cast<vk::DeviceSize>(values.size());
            BufferResource staging = this->create_buffer(byte_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            void* mapped = staging.memory.mapMemory(0, byte_size);
            std::memcpy(mapped, values.data(), static_cast<std::size_t>(byte_size));
            staging.memory.unmapMemory();
            BufferResource destination = this->create_buffer(byte_size, usage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
            this->submit_upload(staging.buffer, destination.buffer, byte_size);
            return destination;
        }

        [[nodiscard]] std::vector<SpectraRasterMaterialGpu> build_gpu_materials() const {
            std::vector<SpectraRasterMaterialGpu> materials{};
            materials.reserve(std::max<std::size_t>(this->raster_scene->materials.size(), 1));
            for (const SpectraRasterMaterial& material : this->raster_scene->materials) {
                SpectraRasterMaterialGpu gpu_material{};
                gpu_material.base_color_roughness = {material.base_color[0], material.base_color[1], material.base_color[2], material.roughness};
                materials.push_back(gpu_material);
            }
            if (materials.empty()) materials.push_back(SpectraRasterMaterialGpu{{1.0f, 1.0f, 1.0f, 1.0f}});
            return materials;
        }

        [[nodiscard]] std::vector<SpectraRasterDrawGpu> build_gpu_draws() {
            std::vector<SpectraRasterDrawGpu> draws{};
            draws.reserve(std::max<std::size_t>(this->raster_scene->draws.size(), 1));
            bool has_bounds = false;
            std::array<float, 3> bounds_min{};
            std::array<float, 3> bounds_max{};
            this->draw_count = this->raster_scene->draws.size();
            this->triangle_count = 0;
            for (const SpectraRasterDraw& draw : this->raster_scene->draws) {
                if (draw.geometry_index >= this->raster_scene->geometries.size()) throw std::runtime_error("Raster draw references a geometry index outside SpectraRasterScene");
                if (draw.material_index >= this->raster_scene->materials.size()) throw std::runtime_error("Raster draw references a material index outside SpectraRasterScene");
                const SpectraRasterGeometry& geometry = this->raster_scene->geometries[draw.geometry_index];
                if (geometry.first_index + geometry.index_count > this->raster_scene->indices.size()) throw std::runtime_error("Raster geometry index range is outside SpectraRasterScene");
                if (geometry.first_vertex + geometry.vertex_count > this->raster_scene->vertices.size()) throw std::runtime_error("Raster geometry vertex range is outside SpectraRasterScene");

                SpectraRasterDrawGpu gpu_draw{};
                gpu_draw.object_from_local = draw.transform;
                gpu_draw.normal_from_local = normal_from_local_matrix_array(draw.transform);
                if (draw.reverse_orientation) {
                    for (const std::size_t offset : std::array<std::size_t, 9>{0, 1, 2, 4, 5, 6, 8, 9, 10}) gpu_draw.normal_from_local[offset] = -gpu_draw.normal_from_local[offset];
                }
                gpu_draw.material_index = static_cast<std::uint32_t>(draw.material_index);
                draws.push_back(gpu_draw);
                this->triangle_count += geometry.index_count / 3u;

                for (std::size_t vertex_index = geometry.first_vertex; vertex_index < geometry.first_vertex + geometry.vertex_count; ++vertex_index) {
                    const std::array<float, 3> point = transform_point_array(draw.transform, this->raster_scene->vertices[vertex_index].position);
                    if (!has_bounds) {
                        bounds_min = point;
                        bounds_max = point;
                        has_bounds = true;
                    } else {
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            bounds_min[axis] = std::min(bounds_min[axis], point[axis]);
                            bounds_max[axis] = std::max(bounds_max[axis], point[axis]);
                        }
                    }
                }
            }
            if (draws.empty()) draws.push_back(SpectraRasterDrawGpu{identity_matrix_array(), identity_matrix_array(), 0, {}});
            if (has_bounds) {
                const float dx = bounds_max[0] - bounds_min[0];
                const float dy = bounds_max[1] - bounds_min[1];
                const float dz = bounds_max[2] - bounds_min[2];
                this->initial_move_scale = std::sqrt(dx * dx + dy * dy + dz * dz) / 1000.0f;
                if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("Raster scene bounds must define a positive interactive move scale");
            }
            return draws;
        }

        void create_scene_buffers() {
            if (this->raster_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer buffers without SpectraRasterScene");
            const std::vector<SpectraRasterMaterialGpu> gpu_materials = this->build_gpu_materials();
            const std::vector<SpectraRasterDrawGpu> gpu_draws = this->build_gpu_draws();
            this->material_buffer = this->upload_vector_buffer(gpu_materials, vk::BufferUsageFlagBits::eStorageBuffer);
            this->draw_buffer = this->upload_vector_buffer(gpu_draws, vk::BufferUsageFlagBits::eStorageBuffer);
            if (!this->raster_scene->vertices.empty()) this->vertex_buffer = this->upload_vector_buffer(this->raster_scene->vertices, vk::BufferUsageFlagBits::eVertexBuffer);
            if (!this->raster_scene->indices.empty()) this->index_buffer = this->upload_vector_buffer(this->raster_scene->indices, vk::BufferUsageFlagBits::eIndexBuffer);
        }

        void create_image_resource(FrameResource& frame, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect, vk::raii::DeviceMemory& memory, vk::raii::Image& image, vk::raii::ImageView& image_view) const {
            const vk::ImageCreateInfo image_create_info{
                {},
                vk::ImageType::e2D,
                format,
                vk::Extent3D{this->extent.width, this->extent.height, 1},
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                usage,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined,
            };
            image = vk::raii::Image{*this->device, image_create_info};
            const vk::MemoryRequirements memory_requirements = image.getMemoryRequirements();
            const std::uint32_t memory_type = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            memory = vk::raii::DeviceMemory{*this->device, allocate_info};
            image.bindMemory(*memory, 0);

            const vk::ImageViewCreateInfo image_view_create_info{{}, *image, vk::ImageViewType::e2D, format, {}, {aspect, 0, 1, 0, 1}};
            image_view = vk::raii::ImageView{*this->device, image_view_create_info};
        }

        void create_frame_resources(const std::uint32_t frame_count) {
            this->frames.resize(frame_count);
            for (FrameResource& frame : this->frames) {
                this->create_image_resource(frame, this->color_format, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor, frame.color_memory, frame.color_image, frame.color_image_view);
                this->create_image_resource(frame, this->depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth, frame.depth_memory, frame.depth_image, frame.depth_image_view);
                const vk::SamplerCreateInfo sampler_create_info{
                    {},
                    vk::Filter::eLinear,
                    vk::Filter::eLinear,
                    vk::SamplerMipmapMode::eNearest,
                    vk::SamplerAddressMode::eClampToEdge,
                    vk::SamplerAddressMode::eClampToEdge,
                    vk::SamplerAddressMode::eClampToEdge,
                    0.0f,
                    VK_FALSE,
                    1.0f,
                    VK_FALSE,
                    vk::CompareOp::eNever,
                    0.0f,
                    0.0f,
                    vk::BorderColor::eFloatOpaqueBlack,
                    VK_FALSE,
                };
                frame.color_sampler = vk::raii::Sampler{*this->device, sampler_create_info};
            }
        }

        void create_descriptors() {
            const std::array descriptor_bindings{
                vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
                vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment},
            };
            const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_bindings.size()), descriptor_bindings.data()};
            this->descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->device, descriptor_set_layout_create_info};

            const std::array descriptor_pool_sizes{
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 2},
            };
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->descriptor_pool = vk::raii::DescriptorPool{*this->device, descriptor_pool_create_info};
            const vk::DescriptorSetLayout descriptor_set_layout_handle = *this->descriptor_set_layout;
            const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->descriptor_pool, 1, &descriptor_set_layout_handle};
            this->descriptor_sets = vk::raii::DescriptorSets{*this->device, descriptor_set_allocate_info};
            if (this->descriptor_sets.size() != 1) throw std::runtime_error("Vulkan rasterizer failed to allocate descriptor set");

            const vk::DescriptorBufferInfo draw_buffer_info{*this->draw_buffer.buffer, 0, this->draw_buffer.size};
            const vk::DescriptorBufferInfo material_buffer_info{*this->material_buffer.buffer, 0, this->material_buffer.size};
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*this->descriptor_sets.front(), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &draw_buffer_info},
                vk::WriteDescriptorSet{*this->descriptor_sets.front(), 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &material_buffer_info},
            };
            this->device->updateDescriptorSets(descriptor_writes, {});
        }

        void create_pipeline() {
            const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, xayah::generated::spectra_raster_vertex_spirv_size, xayah::generated::spectra_raster_vertex_spirv.data()};
            const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, xayah::generated::spectra_raster_fragment_spirv_size, xayah::generated::spectra_raster_fragment_spirv.data()};
            this->vertex_shader = vk::raii::ShaderModule{*this->device, vertex_shader_create_info};
            this->fragment_shader = vk::raii::ShaderModule{*this->device, fragment_shader_create_info};

            const vk::DescriptorSetLayout descriptor_set_layout_handle = *this->descriptor_set_layout;
            const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, static_cast<std::uint32_t>(sizeof(SpectraRasterPushConstants))};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_set_layout_handle, 1, &push_constant_range};
            this->pipeline_layout = vk::raii::PipelineLayout{*this->device, pipeline_layout_create_info};

            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *this->vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *this->fragment_shader, "main"},
            };
            const vk::VertexInputBindingDescription vertex_binding{0, static_cast<std::uint32_t>(sizeof(SpectraRasterVertex)), vk::VertexInputRate::eVertex};
            const std::array vertex_attributes{
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, 12},
            };
            const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
            const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
            const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
            const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
            const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
            const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
            const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{0, 1, &this->color_format, this->depth_format, vk::Format::eUndefined};

            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            pipeline_create_info.setStages(shader_stages);
            pipeline_create_info.setPVertexInputState(&vertex_input_state);
            pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            pipeline_create_info.setPViewportState(&viewport_state);
            pipeline_create_info.setPRasterizationState(&rasterization_state);
            pipeline_create_info.setPMultisampleState(&multisample_state);
            pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
            pipeline_create_info.setPColorBlendState(&color_blend_state);
            pipeline_create_info.setPDynamicState(&dynamic_state);
            pipeline_create_info.setLayout(*this->pipeline_layout);
            this->pipeline = vk::raii::Pipeline{*this->device, nullptr, pipeline_create_info};
        }

        void record_geometry(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) const {
            if (!*this->pipeline || !*this->pipeline_layout || this->descriptor_sets.empty()) throw std::runtime_error("Vulkan rasterizer pipeline is not ready");
            if (!*this->vertex_buffer.buffer || !*this->index_buffer.buffer) throw std::runtime_error("Vulkan rasterizer draw list is non-empty but vertex/index buffers are missing");
            const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->extent.width), static_cast<float>(this->extent.height), 0.0f, 1.0f};
            const vk::Rect2D scissor{{0, 0}, this->extent};
            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->pipeline);
            command_buffer.setViewport(0, viewport);
            command_buffer.setScissor(0, scissor);
            const vk::DescriptorSet descriptor_set = *this->descriptor_sets.front();
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->pipeline_layout, 0, descriptor_set, {});
            const std::array vertex_buffers{*this->vertex_buffer.buffer};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            command_buffer.bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            command_buffer.bindIndexBuffer(*this->index_buffer.buffer, 0, vk::IndexType::eUint32);

            SpectraRasterPushConstants push_constants{};
            push_constants.view_projection = raster_view_projection_matrix(*this->scene, camera_from_world, moving_from_camera);
            for (std::size_t draw_index = 0; draw_index < this->raster_scene->draws.size(); ++draw_index) {
                const SpectraRasterDraw& draw = this->raster_scene->draws[draw_index];
                if (draw.geometry_index >= this->raster_scene->geometries.size()) throw std::runtime_error("Raster draw references a geometry index outside SpectraRasterScene while recording");
                const SpectraRasterGeometry& geometry = this->raster_scene->geometries[draw.geometry_index];
                if (draw_index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster draw index exceeds uint32 push-constant range");
                if (geometry.first_index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) || geometry.index_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster geometry index range exceeds Vulkan uint32 draw range");
                push_constants.draw_index = static_cast<std::uint32_t>(draw_index);
                command_buffer.pushConstants(*this->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, static_cast<std::uint32_t>(sizeof(push_constants)), &push_constants);
                command_buffer.drawIndexed(static_cast<std::uint32_t>(geometry.index_count), 1, static_cast<std::uint32_t>(geometry.first_index), 0, 0);
            }
        }
    };

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};
        this->window_title.base = app_name_string;

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> enabled_device_extensions{vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName};
#if defined(_WIN32)
        enabled_device_extensions.push_back(vk::KHRExternalMemoryWin32ExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreWin32ExtensionName);
#else
        enabled_device_extensions.push_back(vk::KHRExternalMemoryFdExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreFdExtensionName);
#endif
        std::vector<const char*> enabled_instance_extensions{};

        {
            std::uint32_t glfw_extension_count = 0;
            const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");
            enabled_instance_extensions = {glfw_extensions, glfw_extensions + glfw_extension_count};
            enabled_instance_extensions.push_back(vk::EXTDebugUtilsExtensionName);

            const std::vector<vk::LayerProperties> available_layers = this->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : enabled_instance_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : enabled_instance_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

            const vk::ApplicationInfo application_info{app_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), engine_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }
        {
            constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
                {},
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                &debug_callback,
            };
            this->context.debug_messenger = this->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
        }
        {
            if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            this->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
            if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
            glfwSetWindowUserPointer(this->surface.window.get(), this);
            glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
        }
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
            this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
        }
        {
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
                if (physical_device.getProperties().apiVersion < VK_API_VERSION_1_4) continue;

                const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
                bool required_extensions_available                              = true;
                for (const char* required_extension : enabled_device_extensions) {
                    if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) required_extensions_available = false;
                }
                if (!required_extensions_available) continue;

                const std::vector<vk::QueueFamilyProperties> queue_families = physical_device.getQueueFamilyProperties();
                for (std::uint32_t queue_family_index = 0; queue_family_index < queue_families.size(); ++queue_family_index) {
                    if (!static_cast<bool>(queue_families[queue_family_index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                    if (!physical_device.getSurfaceSupportKHR(queue_family_index, this->surface.surface)) continue;
                    this->context.physical_device      = physical_device;
                    this->context.graphics_queue_index = queue_family_index;
                    selected                           = true;
                    break;
                }
                if (selected) break;
            }
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
        }
        {
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
            this->context.device         = vk::raii::Device{this->context.physical_device, device_create_info};
            this->context.graphics_queue = vk::raii::Queue{this->context.device, this->context.graphics_queue_index, 0};
        }
        {
            const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->context.graphics_queue_index};
            this->context.command_pool = vk::raii::CommandPool{this->context.device, command_pool_create_info};
        }
        this->create_swapchain();
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
            const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->context.command_pool, vk::CommandBufferLevel::ePrimary, this->sync.frame_count};
            this->sync.command_buffers = vk::raii::CommandBuffers{this->context.device, command_buffer_allocate_info};
            if (this->sync.command_buffers.size() != this->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

            this->sync.image_available_semaphores.reserve(this->sync.frame_count);
            this->sync.in_flight_fences.reserve(this->sync.frame_count);
            for (std::uint32_t frame_index = 0; frame_index < this->sync.frame_count; ++frame_index) {
                this->sync.image_available_semaphores.emplace_back(this->context.device, semaphore_create_info);
                this->sync.in_flight_fences.emplace_back(this->context.device, fence_create_info);
            }
        }
        this->create_imgui();

        const auto properties_chain = this->context.physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        const vk::PhysicalDeviceProperties& properties = properties_chain.get<vk::PhysicalDeviceProperties2>().properties;
        const vk::PhysicalDeviceDriverProperties& driver = properties_chain.get<vk::PhysicalDeviceDriverProperties>();
        std::println("Spectra Vulkan device: {} | Vulkan {}.{}.{} | Driver {} ({})", properties.deviceName.data(), vk::apiVersionMajor(properties.apiVersion), vk::apiVersionMinor(properties.apiVersion), vk::apiVersionPatch(properties.apiVersion), driver.driverName.data(), vk::to_string(driver.driverID));
        std::println("Spectra swapchain: {} {}x{} images {} present {}", vk::to_string(this->swapchain.format), this->swapchain.extent.width, this->swapchain.extent.height, this->swapchain.images.size(), vk::to_string(this->swapchain.present_mode));
    } catch (...) {
        this->destroy_imgui();
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }

        this->unload_vulkan_rasterizer_noexcept();
        this->pbrt_interactive.reset();
        this->unload_raster_scene_noexcept();
        this->unload_pbrt_backend_scene_noexcept();
        this->unload_spectra_scene_noexcept();
        this->destroy_imgui();
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
        this->context.command_pool = nullptr;
        this->swapchain.image_views.clear();
        this->swapchain.handle = nullptr;
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->context.graphics_queue  = nullptr;
        this->context.device          = nullptr;
        this->surface.surface         = nullptr;
        this->surface.window          = nullptr;
        this->context.physical_device = nullptr;
        this->context.debug_messenger = nullptr;
        this->context.instance        = nullptr;
        if (this->surface.glfw_initialized) glfwTerminate();
        this->surface.glfw_initialized = false;
    }

    void Spectra::create_imgui() {
        if (this->imgui.initialized) throw std::runtime_error("ImGui is already initialized");
        if (this->surface.window.get() == nullptr) throw std::runtime_error("Cannot initialize ImGui without a GLFW window");
        if (this->swapchain.images.empty()) throw std::runtime_error("Cannot initialize ImGui without swapchain images");

        bool context_created            = false;
        bool glfw_backend_initialized   = false;
        bool vulkan_backend_initialized = false;
        try {
            constexpr std::array descriptor_pool_sizes{
                vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000},
            };
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()), static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->imgui.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};
            this->imgui.color_format    = this->swapchain.format;
            this->imgui.min_image_count = std::max(2u, this->sync.frame_count);
            this->imgui.image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (this->imgui.image_count < this->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created = true;

            ImGuiIO& io = ImGui::GetIO();
            if (this->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            if (this->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            load_imgui_fonts();
            apply_imgui_style(this->imgui.viewports);

            if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
            glfw_backend_initialized = true;

            auto color_attachment_format = static_cast<VkFormat>(this->imgui.color_format);
            VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
            pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = VK_API_VERSION_1_4;
            init_info.Instance                                     = static_cast<VkInstance>(*this->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->context.device);
            init_info.QueueFamily                                  = this->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->context.graphics_queue);
            init_info.DescriptorPool                               = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
            init_info.MinImageCount                                = this->imgui.min_image_count;
            init_info.ImageCount                                   = this->imgui.image_count;
            init_info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering                          = true;
            init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
            if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
            vulkan_backend_initialized = true;
            this->imgui.initialized    = true;
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->imgui.descriptor_pool = nullptr;
            this->imgui.color_format    = vk::Format::eUndefined;
            this->imgui.min_image_count = 2;
            this->imgui.image_count     = 2;
            this->imgui.initialized     = false;
            throw;
        }
    }

    void Spectra::destroy_imgui() noexcept {
        if (this->vulkan_rasterizer != nullptr) this->vulkan_rasterizer->release_imgui_descriptors();
        if (this->pbrt_interactive != nullptr) this->pbrt_interactive->release_imgui_descriptors();
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool = nullptr;
        this->imgui.color_format    = vk::Format::eUndefined;
        this->imgui.min_image_count = 2;
        this->imgui.image_count     = 2;
        this->imgui.initialized     = false;
        this->ui.dock_layout_initialized = false;
    }

    void Spectra::load_spectra_scene(const std::filesystem::path& scene_path) {
        if (this->spectra_scene != nullptr) throw std::runtime_error("Spectra scene is already loaded");
        std::unique_ptr<SpectraScene> loaded_scene = std::make_unique<SpectraScene>();
        try {
            loaded_scene->load(scene_path);
            this->spectra_scene = std::move(loaded_scene);
        } catch (...) {
            loaded_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_spectra_scene_noexcept() noexcept {
        if (this->spectra_scene != nullptr) {
            this->spectra_scene->unload_noexcept();
            this->spectra_scene.reset();
        }
    }

    void Spectra::load_pbrt_backend_scene() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot load PBRT backend scene without a loaded Spectra scene");
        if (this->pbrt_backend_scene != nullptr) throw std::runtime_error("PBRT backend scene is already loaded");
        std::unique_ptr<SpectraPbrtBackendScene> loaded_backend_scene = std::make_unique<SpectraPbrtBackendScene>();
        try {
            loaded_backend_scene->load(*this->spectra_scene);
            this->pbrt_backend_scene = std::move(loaded_backend_scene);
        } catch (...) {
            loaded_backend_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_pbrt_backend_scene_noexcept() noexcept {
        if (this->pbrt_backend_scene != nullptr) {
            this->pbrt_backend_scene->unload_noexcept();
            this->pbrt_backend_scene.reset();
        }
    }

    void Spectra::load_raster_scene() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot build raster scene without a loaded Spectra scene");
        if (this->raster_scene != nullptr) throw std::runtime_error("Spectra raster scene is already loaded");
        std::unique_ptr<SpectraRasterScene> loaded_raster_scene = std::make_unique<SpectraRasterScene>();
        try {
            loaded_raster_scene->build(*this->spectra_scene);
            this->raster_scene = std::move(loaded_raster_scene);
        } catch (...) {
            loaded_raster_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_raster_scene_noexcept() noexcept {
        if (this->raster_scene != nullptr) {
            this->raster_scene->unload_noexcept();
            this->raster_scene.reset();
        }
    }

    void Spectra::load_vulkan_rasterizer() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer without a loaded Spectra scene");
        if (this->raster_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer without a built Spectra raster scene");
        if (this->vulkan_rasterizer != nullptr) throw std::runtime_error("Vulkan rasterizer is already loaded");
        this->vulkan_rasterizer = std::make_unique<SpectraVulkanRasterizer>(*this->spectra_scene, *this->raster_scene, this->context.physical_device, this->context.device, this->context.graphics_queue, this->context.command_pool, this->sync.frame_count);
    }

    void Spectra::unload_vulkan_rasterizer_noexcept() noexcept {
        this->vulkan_rasterizer.reset();
    }

    void Spectra::run_interactive_scene(const std::filesystem::path& scene_path) {
        if (this->spectra_scene != nullptr) throw std::runtime_error("Spectra scene is already active");
        if (this->raster_scene != nullptr) throw std::runtime_error("Spectra raster scene is already active");
        if (this->pbrt_backend_scene != nullptr) throw std::runtime_error("PBRT backend scene is already active");
        if (this->pbrt_interactive != nullptr) throw std::runtime_error("PBRT interactive session is already active");
        if (this->vulkan_rasterizer != nullptr) throw std::runtime_error("Vulkan rasterizer is already active");
        try {
            this->load_spectra_scene(scene_path);
            this->load_pbrt_backend_scene();
            if (this->spectra_scene == nullptr || this->pbrt_backend_scene == nullptr) throw std::runtime_error("Spectra scene load did not produce both shared and PBRT backend scene resources");
            this->pbrt_interactive = std::make_unique<SpectraPbrtInteractiveSession>(*this->spectra_scene, this->pbrt_backend_scene->basic_scene(), this->context.physical_device, this->context.device, this->sync.frame_count);
            this->spectra_scene->set_runtime_metadata(this->pbrt_interactive->film_resolution(), this->pbrt_interactive->sampler_sample_count(), this->pbrt_interactive->camera_from_world_matrix());
            this->load_raster_scene();
            this->load_vulkan_rasterizer();
            this->initialize_camera_state();
            this->render_loop();
            this->context.device.waitIdle();
            this->unload_vulkan_rasterizer_noexcept();
            this->unload_raster_scene_noexcept();
            this->pbrt_interactive.reset();
            this->unload_pbrt_backend_scene_noexcept();
            this->unload_spectra_scene_noexcept();
        } catch (...) {
            try {
                if (*this->context.device) this->context.device.waitIdle();
            } catch (...) {
            }
            this->unload_vulkan_rasterizer_noexcept();
            this->unload_raster_scene_noexcept();
            this->pbrt_interactive.reset();
            this->unload_pbrt_backend_scene_noexcept();
            this->unload_spectra_scene_noexcept();
            throw;
        }
    }

    void Spectra::render_loop() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot enter Spectra render loop without an active Spectra scene");
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
        this->context.device.waitIdle();
    }

    void Spectra::update_window_title(const float delta_seconds) {
        if (this->surface.window == nullptr) throw std::runtime_error("Cannot update window title without a GLFW window");

        ++this->window_title.frame_count;
        this->window_title.refresh_timer += delta_seconds;
        if (this->window_title.refresh_timer <= 1.0f) return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.Framerate <= 0.0f) return;

        std::uint32_t width  = this->swapchain.extent.width;
        std::uint32_t height = this->swapchain.extent.height;
        if (this->ui.viewport_known && this->ui.viewport_size[0] > 0.0f && this->ui.viewport_size[1] > 0.0f) {
            width  = static_cast<std::uint32_t>(std::max(1.0f, std::round(this->ui.viewport_size[0])));
            height = static_cast<std::uint32_t>(std::max(1.0f, std::round(this->ui.viewport_size[1])));
        }

        const std::string scene_label = this->spectra_scene == nullptr ? "No Scene" : this->spectra_scene->scene_label;
        const std::array<int, 2> sample_range = this->spectra_scene == nullptr ? std::array<int, 2>{0, 0} : this->active_renderer_sample_range();
        const std::string title       = std::format("{} - {} | {} | {}x{} | sample {}/{} | {:.0f} FPS / {:.3f}ms | frame {}", this->window_title.base, scene_label, this->active_renderer_label(), width, height, sample_range[0], sample_range[1], io.Framerate, 1000.0f / io.Framerate, this->window_title.frame_count);
        glfwSetWindowTitle(this->surface.window.get(), title.c_str());
        this->window_title.refresh_timer = 0.0f;
    }

    void Spectra::clear_pathtracer_throughput_statistics() {
        this->statistics.throughput_mspp.clear();
        this->statistics.last_valid_throughput_mspp = 0.0f;
        this->statistics.has_throughput             = false;
    }

    void Spectra::update_frame_statistics(const FrameState& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Renderer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Renderer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        this->statistics.current_frame_id             = this->window_title.frame_count + 1;
        this->statistics.active_frame_index           = frame.frame_index;
        this->statistics.active_swapchain_image_index = frame.image_index;
        this->statistics.last_frame_milliseconds      = frame_milliseconds;
        this->statistics.last_frame_rendered_sample   = rendered_sample;
        this->statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->statistics.throughput_mspp.add(throughput);
            this->statistics.last_valid_throughput_mspp = throughput;
            this->statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] const char* Spectra::active_renderer_label() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                return "PBRT Pathtracer";
            case SpectraRenderMode::VulkanRasterizer:
                return "Vulkan Rasterizer";
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] Spectra::ActiveRendererStatus Spectra::active_renderer_status() const {
        ActiveRendererStatus status{};
        status.label                              = this->active_renderer_label();
        status.sample_range                       = this->active_renderer_sample_range();
        status.pathtracer_accumulation_dirty      = this->camera.pathtracer_accumulation_dirty;
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                status.has_accumulation = true;
                status.uses_external_completion = this->pbrt_interactive != nullptr;
                if (this->pbrt_interactive == nullptr) {
                    status.state = "Unavailable";
                    return status;
                }
                if (status.pathtracer_accumulation_dirty) {
                    status.state = "Camera Dirty";
                    return status;
                }
                status.state = status.sample_range[0] >= status.sample_range[1] ? "Completed" : "Sampling";
                return status;
            case SpectraRenderMode::VulkanRasterizer:
                status.has_accumulation = false;
                status.uses_external_completion = false;
                if (this->vulkan_rasterizer == nullptr) {
                    status.state = "Unavailable";
                    return status;
                }
                status.state = this->vulkan_rasterizer->draw_count == 0 ? "Clear Only" : "Rasterizing";
                return status;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] VkDescriptorSet Spectra::active_viewport_descriptor() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT pathtracer viewport descriptor requested without an active PBRT session");
                return this->pbrt_interactive->active_descriptor();
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Vulkan rasterizer viewport descriptor requested without an active rasterizer session");
                return this->vulkan_rasterizer->active_descriptor();
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] std::array<int, 2> Spectra::active_renderer_sample_range() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) return {0, 0};
                return {this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count()};
            case SpectraRenderMode::VulkanRasterizer:
                return {1, 1};
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] float Spectra::active_renderer_initial_move_scale() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT camera move scale requested without an active PBRT session");
                return this->pbrt_interactive->camera_initial_move_scale();
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Vulkan rasterizer camera move scale requested without an active rasterizer session");
                return this->vulkan_rasterizer->camera_initial_move_scale();
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] bool Spectra::active_renderer_uses_external_completion_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return true;
            case SpectraRenderMode::VulkanRasterizer:
                return false;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] vk::Semaphore Spectra::active_renderer_complete_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return this->pbrt_interactive->active_cuda_complete_semaphore();
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer does not use an external completion semaphore");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] Spectra::ActiveRendererFrameResult Spectra::render_active_renderer_frame(const FrameState& frame) {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer: {
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot render PBRT pathtracer without an active PBRT session");
                const SpectraPbrtInteractiveSession::RenderFrameResult render_result = this->pbrt_interactive->render_frame(frame.frame_index, this->camera.moving_from_camera);
                return {render_result.sample_pixels, render_result.rendered_sample, render_result.reset_accumulation};
            }
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot render Vulkan rasterizer without an active rasterizer session");
                this->vulkan_rasterizer->render_frame(frame.frame_index);
                return {};
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::record_renderer_output(const SpectraRenderMode render_mode, const vk::raii::CommandBuffer& command_buffer) {
        switch (render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot record PBRT pathtracer output without an active PBRT session");
                this->pbrt_interactive->record_copy(command_buffer);
                return;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot record Vulkan rasterizer output without an active rasterizer session");
                this->vulkan_rasterizer->record_draw(command_buffer, this->camera.camera_from_world, this->camera.moving_from_camera);
                return;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::reset_active_renderer_accumulation() {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                this->request_pathtracer_accumulation_reset();
                return;
            case SpectraRenderMode::VulkanRasterizer:
                return;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::request_pathtracer_accumulation_reset() {
        if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot reset PBRT accumulation without an active PBRT session");
        this->pbrt_interactive->request_reset_accumulation();
        this->camera.pathtracer_accumulation_dirty = false;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::mark_pathtracer_accumulation_dirty() {
        if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot mark PBRT accumulation dirty without an active PBRT session");
        if (this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer) {
            this->request_pathtracer_accumulation_reset();
            return;
        }
        this->camera.pathtracer_accumulation_dirty = true;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::set_active_render_mode(const SpectraRenderMode render_mode) {
        if (render_mode == this->ui.active_render_mode) return;
        switch (render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot switch to PBRT Pathtracer without an active PBRT session");
                if (this->camera.pathtracer_accumulation_dirty) this->request_pathtracer_accumulation_reset();
                break;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot switch to Vulkan Rasterizer without an active rasterizer session");
                break;
        }
        this->context.device.waitIdle();
        this->ui.active_render_mode = render_mode;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::initialize_camera_state() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        this->camera.initialized        = true;
        this->camera.input_enabled      = false;
        this->camera.move_scale         = this->active_renderer_initial_move_scale();
        this->camera.moving_from_camera = identity_matrix_array();
        this->camera.camera_from_world  = this->spectra_scene->camera_from_world;
        this->camera.pathtracer_accumulation_dirty = false;
    }

    void Spectra::set_camera_move_scale(const float move_scale) {
        if (!std::isfinite(move_scale) || !(move_scale > 0.0f)) throw std::runtime_error("Camera move scale must be finite and positive");
        this->camera.move_scale = move_scale;
    }

    void Spectra::reset_camera() {
        if (!this->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        this->camera.moving_from_camera = identity_matrix_array();
        this->mark_pathtracer_accumulation_dirty();
    }

    void Spectra::process_camera_input(GLFWwindow* window) {
        if (window == nullptr) throw std::runtime_error("Cannot process camera input without a GLFW window");
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, GLFW_TRUE);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->ui.viewport_known && mouse_position.x >= this->ui.viewport_position[0] && mouse_position.x < this->ui.viewport_position[0] + this->ui.viewport_size[0] && mouse_position.y >= this->ui.viewport_position[1] && mouse_position.y < this->ui.viewport_position[1] + this->ui.viewport_size[1];
        this->camera.input_enabled  = in_viewport_rect && (this->ui.viewport_hovered || this->ui.viewport_focused) && !io.WantTextInput;
        if (!this->camera.input_enabled) return;
        if (!this->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        std::array<float, 16> moving_from_camera = this->camera.moving_from_camera;
        bool needs_reset                         = false;
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(-this->camera.move_scale, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(this->camera.move_scale, 0.0f, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(0.0f, 0.0f, -this->camera.move_scale));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(0.0f, 0.0f, this->camera.move_scale));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(0.0f, -this->camera.move_scale, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, translation_matrix_array(0.0f, this->camera.move_scale, 0.0f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_y_matrix_array(-0.5f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_y_matrix_array(0.5f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_x_matrix_array(-0.5f));
            needs_reset        = true;
        }
        if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_x_matrix_array(0.5f));
            needs_reset        = true;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
            if (io.MouseDelta.x < 0.0f) moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_y_matrix_array(-1.0f));
            if (io.MouseDelta.x > 0.0f) moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_y_matrix_array(1.0f));
            if (io.MouseDelta.y > 0.0f) moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_x_matrix_array(-1.0f));
            if (io.MouseDelta.y < 0.0f) moving_from_camera = multiply_matrix_arrays(moving_from_camera, rotation_x_matrix_array(1.0f));
            needs_reset = needs_reset || io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            moving_from_camera = identity_matrix_array();
            needs_reset        = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false)) this->set_camera_move_scale(this->camera.move_scale * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false)) this->set_camera_move_scale(this->camera.move_scale * 0.5f);

        if (needs_reset) {
            validate_matrix_array(moving_from_camera);
            this->camera.moving_from_camera = moving_from_camera;
            this->mark_pathtracer_accumulation_dirty();
        }
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present = false;
        frame.frame_index            = this->sync.frame_index;
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot update renderer frame without an active Spectra scene");
        frame.render_mode = this->ui.active_render_mode;
        this->process_camera_input(this->surface.window.get());
        const ActiveRendererFrameResult render_result = this->render_active_renderer_frame(frame);
        frame.wait_for_external_completion = this->active_renderer_uses_external_completion_semaphore();
        if (frame.wait_for_external_completion) frame.external_completion_semaphore = this->active_renderer_complete_semaphore();
        this->update_frame_statistics(frame, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        return true;
    }

    void Spectra::record_frame(const FrameState& frame) {
        this->draw_main_menu();
        this->draw_dockspace();
        this->draw_viewport_window();
        this->draw_camera_window();
        this->draw_scene_diagnostics_window();
        this->draw_settings_window();
        this->draw_environment_window();
        this->draw_tonemapper_window();
        this->draw_statistics_window();

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);
        if (this->spectra_scene != nullptr) this->record_renderer_output(frame.render_mode, command_buffer);

        {
            const vk::ImageMemoryBarrier2 color_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->swapchain.image_layouts[frame.image_index],
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                this->swapchain.images[frame.image_index],
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.02f, 0.02f, 0.025f, 1.0f};
        const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            color_clear_value,
        };
        const vk::RenderingInfo clear_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(clear_rendering_info);
        command_buffer.endRendering();

        ImGui::Render();
        const vk::RenderingAttachmentInfo imgui_color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo imgui_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &imgui_color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(imgui_rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, this->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame) {
        if (this->imgui.viewports) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        std::array<vk::SemaphoreSubmitInfo, 2> wait_semaphore_infos{
            vk::SemaphoreSubmitInfo{*this->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands},
            vk::SemaphoreSubmitInfo{},
        };
        std::uint32_t wait_semaphore_count = 1;
        if (frame.wait_for_external_completion) {
            wait_semaphore_infos[1] = vk::SemaphoreSubmitInfo{frame.external_completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer};
            wait_semaphore_count    = 2;
        }
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, wait_semaphore_count, wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        bool frame_presented = true;
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
                frame.recreate_after_present = true;
            else if (present_result == vk::Result::eErrorSurfaceLostKHR) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (present_result != vk::Result::eSuccess)
                throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
            frame_presented              = false;
        } catch (const vk::SystemError& error) {
            if (error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (error.code().value() == static_cast<int>(vk::Result::eSuboptimalKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else
                throw;
        }
        if (frame.recreate_after_present) this->recreate_swapchain();
        if (frame_presented) this->update_window_title(ImGui::GetIO().DeltaTime);

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }

    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) this->ui.camera_visible = !this->ui.camera_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) this->ui.scene_diagnostics_visible = !this->ui.scene_diagnostics_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) this->ui.settings_visible = !this->ui.settings_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) this->ui.statistics_visible = !this->ui.statistics_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) this->ui.environment_visible = !this->ui.environment_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) this->ui.tonemapper_visible = !this->ui.tonemapper_visible;
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(ICON_MS_PHOTO_CAMERA " Camera", "F1", &this->ui.camera_visible);
            ImGui::MenuItem(ICON_MS_DIAGNOSIS " Scene Diagnostics", "F2", &this->ui.scene_diagnostics_visible);
            ImGui::MenuItem(ICON_MS_SETTINGS " Settings", "F3", &this->ui.settings_visible);
            ImGui::MenuItem(ICON_MS_ANALYTICS " Statistics", "F4", &this->ui.statistics_visible);
            ImGui::MenuItem(ICON_MS_PUBLIC " Environment", "F5", &this->ui.environment_visible);
            ImGui::MenuItem(ICON_MS_TONALITY " Tonemapper", "F6", &this->ui.tonemapper_visible);
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }

    void Spectra::draw_menu_toolbar() {
        struct ToggleButton {
            const char* icon;
            const char* shortcut;
            bool* visible;
            const char* tooltip;
        };

        const std::array<ToggleButton, 6> toggles{{
            {ICON_MS_PHOTO_CAMERA, "F1", &this->ui.camera_visible, "Camera"},
            {ICON_MS_DIAGNOSIS, "F2", &this->ui.scene_diagnostics_visible, "Scene Diagnostics"},
            {ICON_MS_SETTINGS, "F3", &this->ui.settings_visible, "Settings"},
            {ICON_MS_ANALYTICS, "F4", &this->ui.statistics_visible, "Statistics"},
            {ICON_MS_PUBLIC, "F5", &this->ui.environment_visible, "Environment"},
            {ICON_MS_TONALITY, "F6", &this->ui.tonemapper_visible, "Tonemapper"},
        }};

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toggles.size()) * button_size + static_cast<float>(toggles.size() + 1) * 2.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 180.0f) return;

        ImGui::SameLine(window_width * 0.5f - total_width * 0.5f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (const ToggleButton& toggle : toggles) {
            ImGui::PushStyleColor(ImGuiCol_Button, *toggle.visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
            if (ImGui::Button(toggle.icon, ImVec2{button_size, button_size})) *toggle.visible = !*toggle.visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && toggle.shortcut != nullptr) ImGui::SetTooltip("Toggle %s Window (%s)", toggle.tooltip, toggle.shortcut);
            if (ImGui::IsItemHovered() && toggle.shortcut == nullptr) ImGui::SetTooltip("Toggle %s Window", toggle.tooltip);
            ImGui::SameLine(0.0f, 2.0f);
        }
    }

    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (main_viewport->WorkSize.x <= 640.0f || main_viewport->WorkSize.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        const ImVec4 dockspace_window_background     = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{dockspace_window_background.x, dockspace_window_background.y, dockspace_window_background.z, 0.0f});
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, dockspace_flags);
        ImGui::PopStyleColor();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->ui.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.23f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.22f, nullptr, &center_id);
        if (right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGui::DockBuilderDockWindow("Viewport", center_id);
        ImGui::DockBuilderDockWindow("Camera", right_id);
        ImGui::DockBuilderDockWindow("Scene Diagnostics", right_id);
        ImGui::DockBuilderDockWindow("Settings", right_id);
        ImGui::DockBuilderDockWindow("Environment", right_id);
        ImGui::DockBuilderDockWindow("Tonemapper", right_id);
        ImGui::DockBuilderDockWindow("Statistics", bottom_id);
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->ui.dock_layout_initialized = true;
    }

    void Spectra::draw_viewport_window() {
        constexpr ImGuiWindowFlags viewport_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        if (ImGui::Begin("Viewport", nullptr, viewport_window_flags)) {
            const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
            const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
            if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
            this->ui.viewport_known    = true;
            this->ui.viewport_position = {viewport_position.x, viewport_position.y};
            this->ui.viewport_size     = {viewport_size.x, viewport_size.y};
            this->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
            this->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
            if (this->spectra_scene != nullptr) {
                const VkDescriptorSet descriptor = this->active_viewport_descriptor();
                if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Active renderer viewport descriptor is null");
                const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
                ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
                ImGui::SetCursorScreenPos(viewport_position);
            }
            ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        } else {
            this->ui.viewport_known   = false;
            this->ui.viewport_hovered = false;
            this->ui.viewport_focused = false;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void Spectra::draw_camera_window() {
        if (!this->ui.camera_visible) return;
        if (!ImGui::Begin("Camera", &this->ui.camera_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const ActiveRendererStatus renderer_status = this->active_renderer_status();

            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("PBRT Dirty", renderer_status.pathtracer_accumulation_dirty ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Move Scale");
            ImGui::TableSetColumnIndex(1);
            float move_scale = this->camera.move_scale;
            const float drag_speed = std::max(std::abs(move_scale) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraMoveScale", &move_scale, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_move_scale(move_scale);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement distance per key step. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->camera.initialized || this->spectra_scene == nullptr);
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::draw_scene_diagnostics_window() {
        if (!this->ui.scene_diagnostics_visible) return;
        if (!ImGui::Begin("Scene Diagnostics", &this->ui.scene_diagnostics_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", this->spectra_scene->scene_label);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", this->spectra_scene->scene_path_text.c_str());

            draw_statistics_row("Active Renderer", this->active_renderer_label());
            draw_statistics_row("Film Resolution", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
            draw_statistics_row("Sampler SPP", std::format("{}", this->spectra_scene->sampler_sample_count));
            draw_statistics_row("Directives", std::format("{}", this->spectra_scene->pbrt_directives.size()));
            draw_statistics_row("Shapes", std::format("{}", this->spectra_scene->shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->spectra_scene->materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->spectra_scene->textures.size()));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->spectra_scene->object_definitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->spectra_scene->object_instances.size()));
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Render Settings");
        if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            draw_scene_render_setting_row("Pixel Filter", this->spectra_scene->pixel_filter);
            draw_scene_render_setting_row("Film", this->spectra_scene->film);
            draw_scene_render_setting_row("Sampler", this->spectra_scene->sampler);
            draw_scene_render_setting_row("Accelerator", this->spectra_scene->accelerator);
            draw_scene_render_setting_row("Integrator", this->spectra_scene->integrator);
            draw_scene_render_setting_row("Camera", this->spectra_scene->camera);
            ImGui::EndTable();
        }

        if (this->raster_scene != nullptr) {
            ImGui::SeparatorText("Raster Scene");
            if (ImGui::BeginTable("SpectraRasterSceneSummary", 2, summary_table_flags)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Vertices", std::format("{}", this->raster_scene->vertices.size()));
                draw_statistics_row("Indices", std::format("{}", this->raster_scene->indices.size()));
                draw_statistics_row("Triangles", std::format("{}", this->raster_scene->indices.size() / 3u));
                draw_statistics_row("Geometries", std::format("{}", this->raster_scene->geometries.size()));
                draw_statistics_row("Draws", std::format("{}", this->raster_scene->draws.size()));
                draw_statistics_row("Materials", std::format("{}", this->raster_scene->materials.size()));
                draw_statistics_row("Diagnostics", std::format("{}", this->raster_scene->diagnostics.size()));
                ImGui::EndTable();
            }
        }

        constexpr ImGuiTableFlags diagnostics_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("PBRT Diagnostics");
        if (this->spectra_scene->unsupported_features.empty()) {
            ImGui::TextDisabled("No unsupported PBRT features recorded");
        } else if (ImGui::BeginTable("SpectraSceneDiagnostics", 4, diagnostics_table_flags)) {
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraSceneUnsupportedFeature& feature : this->spectra_scene->unsupported_features) {
                const std::string source_text   = scene_unsupported_source_text(feature);
                const std::string location_text = scene_file_location_text(feature.location);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(scene_unsupported_kind_label(feature.kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", source_text.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", location_text.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", feature.message.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Raster Diagnostics");
        if (this->raster_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra raster scene");
        } else if (this->raster_scene->diagnostics.empty()) {
            ImGui::TextDisabled("No raster diagnostics recorded");
        } else if (ImGui::BeginTable("SpectraRasterSceneDiagnostics", 4, diagnostics_table_flags)) {
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraRasterDiagnostic& diagnostic : this->raster_scene->diagnostics) {
                const std::string source_text   = raster_diagnostic_source_text(diagnostic);
                const std::string location_text = scene_file_location_text(diagnostic.location);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(raster_diagnostic_kind_label(diagnostic.kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", source_text.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", location_text.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }

    void Spectra::draw_settings_window() {
        if (!this->ui.settings_visible) return;
        if (!ImGui::Begin("Settings", &this->ui.settings_visible)) {
            ImGui::End();
            return;
        }
        ActiveRendererStatus renderer_status = this->active_renderer_status();
        if (ImGui::BeginTable("SpectraRendererSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Active Renderer");
            ImGui::TableSetColumnIndex(1);
            const char* renderer_items[]{"PBRT Pathtracer", "Vulkan Rasterizer"};
            int active_renderer_item = static_cast<int>(this->ui.active_render_mode);
            if (active_renderer_item < 0 || active_renderer_item > 1) throw std::runtime_error("Unknown Spectra render mode item");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ActiveRenderer", &active_renderer_item, renderer_items, static_cast<int>(std::size(renderer_items)))) {
                if (active_renderer_item == 0) this->set_active_render_mode(SpectraRenderMode::PbrtPathtracer);
                else if (active_renderer_item == 1) this->set_active_render_mode(SpectraRenderMode::VulkanRasterizer);
                else throw std::runtime_error("Unknown Spectra render mode item selected");
                renderer_status = this->active_renderer_status();
            }
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("External Completion", renderer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Path Tracer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->pbrt_interactive == nullptr) {
                ImGui::TextDisabled("No active PBRT interactive session");
            } else if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer ? renderer_status.state : "Inactive");
                draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("PBRT Sampler SPP");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", this->spectra_scene->sampler_sample_count);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Current Sample");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Max Iterations");
                ImGui::TableSetColumnIndex(1);
                const int previous_target_sample_count = this->pbrt_interactive->target_sample_count();
                int target_sample_count                = previous_target_sample_count;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->spectra_scene->sampler_sample_count)) {
                    this->pbrt_interactive->set_target_sample_count(target_sample_count);
                    if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Accumulation");
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Reset Accumulation")) {
                    this->pbrt_interactive->request_reset_accumulation();
                    this->clear_pathtracer_throughput_statistics();
                }

                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Rasterizer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->vulkan_rasterizer == nullptr || this->raster_scene == nullptr) {
                ImGui::TextDisabled("No active Vulkan rasterizer session");
            } else if (ImGui::BeginTable("SpectraRasterizerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer ? renderer_status.state : "Inactive");
                draw_statistics_row("Output", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
                draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void Spectra::draw_environment_window() {
        if (!this->ui.environment_visible) return;
        if (!ImGui::Begin("Environment", &this->ui.environment_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const SpectraSceneLight& light : this->spectra_scene->lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->spectra_scene->lights.empty()) {
            ImGui::TextDisabled("No PBRT lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneLight& light : this->spectra_scene->lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outside_medium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(pbrt_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->spectra_scene->mediums.empty()) {
            ImGui::TextDisabled("No PBRT media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMedium& medium : this->spectra_scene->mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(pbrt_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->spectra_scene->medium_bindings.empty()) {
            ImGui::TextDisabled("No PBRT medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMediumBinding& binding : this->spectra_scene->medium_bindings) {
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

        ImGui::End();
    }

    void Spectra::draw_tonemapper_window() {
        if (!this->ui.tonemapper_visible) return;
        if (!ImGui::Begin("Tonemapper", &this->ui.tonemapper_visible)) {
            ImGui::End();
            return;
        }
        if (this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer) {
            ImGui::TextDisabled("Tonemapper applies to PBRT Pathtracer only");
            ImGui::End();
            return;
        }
        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
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
            float exposure = this->pbrt_interactive->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->pbrt_interactive->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
        ImGui::End();
    }

    void Spectra::draw_statistics_window() {
        if (!this->ui.statistics_visible) return;
        if (!ImGui::Begin("Statistics", &this->ui.statistics_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->ui.viewport_known ? std::format("{:.0f} x {:.0f}", this->ui.viewport_size[0], this->ui.viewport_size[1]) : "Unknown";
        const ActiveRendererStatus renderer_status = this->active_renderer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("Accumulation", renderer_status.has_accumulation ? "Yes" : "No");
            draw_statistics_row("Scene", this->spectra_scene == nullptr ? "No Scene" : this->spectra_scene->scene_label);
            draw_statistics_row("Frame ID", std::format("{}", this->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->sync.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->swapchain.extent.width, this->swapchain.extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->statistics.last_frame_milliseconds));
            if (this->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraSceneStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            draw_statistics_row("Raster Diagnostics", this->raster_scene == nullptr ? "No raster scene" : std::format("{}", this->raster_scene->diagnostics.size()));
            ImGui::EndTable();
        }

        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                break;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot show Vulkan rasterizer statistics without an active rasterizer session");
                ImGui::SeparatorText("Rasterizer");
                if (ImGui::BeginTable("SpectraRasterizerStatistics", 2, table_flags)) {
                    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    draw_statistics_row("State", renderer_status.state);
                    draw_statistics_row("Output Resolution", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
                    draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                    draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                    draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                    draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                    draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                    draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                    ImGui::EndTable();
                }
                ImGui::End();
                return;
        }

        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }

        const std::array<int, 2> film_resolution = this->spectra_scene->film_resolution;
        const int current_sample                 = this->pbrt_interactive->current_sample();
        const int target_sample                  = this->pbrt_interactive->target_sample_count();
        const float completion_ratio             = this->pbrt_interactive->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", this->camera.pathtracer_accumulation_dirty ? "Camera Dirty" : sampling_state);
            draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", std::format("{} x {}", film_resolution[0], film_resolution[1]));
            if (this->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->statistics.throughput_mspp.average(), this->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->statistics.last_frame_rendered_sample ? "Rendered sample" : "No PBRT sample");
            ImGui::EndTable();
        }

        ImGui::End();
    }

    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities   = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes     = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);
            if (surface_formats.empty()) throw std::runtime_error("Surface has no formats");
            if (present_modes.empty()) throw std::runtime_error("Surface has no present modes");

            if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
                this->swapchain.format      = vk::Format::eB8G8R8A8Unorm;
                this->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
            } else {
                this->swapchain.format      = surface_formats.front().format;
                this->swapchain.color_space = surface_formats.front().colorSpace;

                bool selected = false;
                constexpr std::array preferred_surface_formats{
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                };
                for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_surface_formats) {
                    for (const vk::SurfaceFormatKHR& surface_format : surface_formats) {
                        if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) {
                            this->swapchain.format      = surface_format.format;
                            this->swapchain.color_space = surface_format.colorSpace;
                            selected                    = true;
                            break;
                        }
                    }
                    if (selected) break;
                }
            }

            this->swapchain.present_mode = vk::PresentModeKHR::eFifo;
            for (const vk::PresentModeKHR present_mode : present_modes) {
                if (present_mode == vk::PresentModeKHR::eMailbox) {
                    this->swapchain.present_mode = present_mode;
                    break;
                }
                if (present_mode == vk::PresentModeKHR::eImmediate) this->swapchain.present_mode = present_mode;
            }

            this->swapchain.extent = surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? vk::Extent2D{std::clamp(this->surface.extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width), std::clamp(this->surface.extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)} : surface_capabilities.currentExtent;
            if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

            this->swapchain.image_count = surface_capabilities.minImageCount + 1;
            if (this->swapchain.image_count < 2) this->swapchain.image_count = 2;
            if (surface_capabilities.maxImageCount != 0 && this->swapchain.image_count > surface_capabilities.maxImageCount) this->swapchain.image_count = surface_capabilities.maxImageCount;

            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) throw std::runtime_error("Swapchain must support color attachment usage");
            this->swapchain.usage = vk::ImageUsageFlagBits::eColorAttachment;
        }
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            auto composite_alpha                                  = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            if (!(surface_capabilities.supportedCompositeAlpha & composite_alpha)) {
                if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
                else
                    throw std::runtime_error("Surface has no supported composite alpha mode");
            }

            const vk::SurfaceTransformFlagBitsKHR pre_transform = surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surface_capabilities.currentTransform;
            const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->surface.surface, this->swapchain.image_count, this->swapchain.format, this->swapchain.color_space, this->swapchain.extent, 1, this->swapchain.usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->swapchain.present_mode, VK_TRUE, *old_swapchain};
            this->swapchain.handle = vk::raii::SwapchainKHR{this->context.device, swapchain_create_info};
        }
        {
            this->swapchain.images = this->swapchain.handle.getImages();
            if (this->swapchain.images.empty()) throw std::runtime_error("Swapchain has no images");
            this->swapchain.image_layouts.assign(this->swapchain.images.size(), vk::ImageLayout::eUndefined);
        }
        {
            this->swapchain.image_views.clear();
            this->swapchain.image_views.reserve(this->swapchain.images.size());
            for (const vk::Image image : this->swapchain.images) {
                const vk::ImageViewCreateInfo image_view_create_info{{}, image, vk::ImageViewType::e2D, this->swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                this->swapchain.image_views.emplace_back(this->context.device, image_view_create_info);
            }
            if (this->swapchain.image_views.size() != this->swapchain.images.size()) throw std::runtime_error("Failed to create all swapchain image views");
        }
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            this->sync.render_finished_semaphores.clear();
            this->sync.render_finished_semaphores.reserve(this->swapchain.images.size());
            for (std::uint32_t image_index = 0; image_index < this->swapchain.images.size(); ++image_index) this->sync.render_finished_semaphores.emplace_back(this->context.device, semaphore_create_info);
            this->sync.image_in_flight_frame.assign(this->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    void Spectra::recreate_swapchain() {
        {
            int width  = 0;
            int height = 0;
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
                if (width == 0 || height == 0) glfwWaitEvents();
            }
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }

        this->context.device.waitIdle();
        vk::raii::SwapchainKHR old_swapchain = std::move(this->swapchain.handle);
        this->swapchain.image_views.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_in_flight_frame.clear();
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->create_swapchain(std::move(old_swapchain));

        const std::uint32_t image_count = static_cast<std::uint32_t>(this->swapchain.images.size());
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized during swapchain recreation");
        if (this->imgui.color_format != this->swapchain.format || this->imgui.image_count != image_count) {
            const bool docking   = this->imgui.docking;
            const bool viewports = this->imgui.viewports;
            this->destroy_imgui();
            this->imgui.docking   = docking;
            this->imgui.viewports = viewports;
            this->create_imgui();
            if (this->pbrt_interactive != nullptr) this->pbrt_interactive->create_imgui_descriptors();
            if (this->vulkan_rasterizer != nullptr) this->vulkan_rasterizer->create_imgui_descriptors();
        }
        this->surface.resize_requested = false;
    }
} // namespace xayah
