module;

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <spectra_rasterizer_mesh_fragment_spv.h>
#include <spectra_rasterizer_mesh_pick_fragment_spv.h>
#include <spectra_rasterizer_mesh_pick_vertex_spv.h>
#include <spectra_rasterizer_mesh_selection_mask_fragment_spv.h>
#include <spectra_rasterizer_mesh_selection_mask_vertex_spv.h>
#include <spectra_rasterizer_mesh_vertex_spv.h>
#include <spectra_rasterizer_point_cloud_fragment_spv.h>
#include <spectra_rasterizer_point_cloud_pick_fragment_spv.h>
#include <spectra_rasterizer_point_cloud_pick_vertex_spv.h>
#include <spectra_rasterizer_point_cloud_selection_mask_fragment_spv.h>
#include <spectra_rasterizer_point_cloud_selection_mask_vertex_spv.h>
#include <spectra_rasterizer_point_cloud_vertex_spv.h>
#include <spectra_rasterizer_selection_outline_fragment_spv.h>
#include <spectra_rasterizer_selection_outline_vertex_spv.h>
#include <spectra_rasterizer_viewport_grid_fragment_spv.h>
#include <spectra_rasterizer_viewport_grid_vertex_spv.h>
#include <spectra_rasterizer_volume_fragment_spv.h>
#include <spectra_rasterizer_volume_pick_fragment_spv.h>
#include <spectra_rasterizer_volume_pick_vertex_spv.h>
#include <spectra_rasterizer_volume_selection_mask_fragment_spv.h>
#include <spectra_rasterizer_volume_selection_mask_vertex_spv.h>
#include <spectra_rasterizer_volume_vertex_spv.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer.renderer;

import spectra.rasterizer.host;
import spectra.rasterizer.math;
import spectra.scene;
import std;

namespace {
    struct RasterizerVertex {
        float px{};
        float py{};
        float pz{};
        float nx{};
        float ny{};
        float nz{};
    };

    struct PointCloudInstance {
        float px{};
        float py{};
        float pz{};
        float radius{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    struct DrawPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> base_color{};
        std::array<float, 4> emission{};
        std::array<float, 4> material{};
        std::array<std::uint32_t, 4> flags{};
    };

    struct VolumePushConstantsData {
        std::array<float, 4> originDensityScale{};
        std::array<float, 4> extentStepScale{};
        std::array<float, 4> base_color{};
        std::array<float, 4> emission{};
        std::array<float, 4> material{};
    };

    struct SelectionPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> color{};
        std::array<float, 4> base_color{};
        std::array<float, 4> material{};
        std::array<std::uint32_t, 4> flags{};
    };

    struct PointCloudSelectionPushConstantsData {
        std::array<float, 4> color{};
        std::uint32_t objectId{};
    };

    struct VolumeSelectionPushConstantsData {
        std::array<float, 4> originDensityScale{};
        std::array<float, 4> extentStepScale{};
        std::array<float, 4> color{};
        std::uint32_t objectId{};
    };

    struct OutlinePushConstantsData {
        std::array<float, 2> inverseExtent{};
        std::array<float, 2> padding{};
    };

    [[nodiscard]] spectra::rasterizer::math::Vector3 to_render_vector(const spectra::scene::Vector3& value) {
        return spectra::rasterizer::math::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] spectra::rasterizer::math::Quaternion to_render_quaternion(const spectra::scene::Quaternion& value) {
        return spectra::rasterizer::math::Quaternion{value.x, value.y, value.z, value.w};
    }

    [[nodiscard]] spectra::rasterizer::math::Transform to_render_transform(const spectra::scene::Transform& value) {
        return spectra::rasterizer::math::Transform{
            .position = to_render_vector(value.position),
            .rotation = to_render_quaternion(value.rotation),
            .scale    = to_render_vector(value.scale),
        };
    }

    [[nodiscard]] spectra::scene::Vector3 to_scene_vector(const spectra::rasterizer::math::Vector3& value) {
        return spectra::scene::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] bool finite_scene_vector(const spectra::scene::Vector3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] bool finite_scene_vector(const spectra::scene::Vector4 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    [[nodiscard]] const char* material_model_name(const spectra::scene::Scene::MaterialModel model) {
        switch (model) {
        case spectra::scene::Scene::MaterialModel::LitSurface: return "LitSurface";
        case spectra::scene::Scene::MaterialModel::UnlitSurface: return "UnlitSurface";
        case spectra::scene::Scene::MaterialModel::EmissiveSurface: return "EmissiveSurface";
        case spectra::scene::Scene::MaterialModel::Volume: return "Volume";
        case spectra::scene::Scene::MaterialModel::PointSprite: return "PointSprite";
        }
        throw std::runtime_error("Unknown Spectra rasterizer material model");
    }

    [[nodiscard]] std::uint32_t material_model_code(const spectra::scene::Scene::MaterialModel model) {
        switch (model) {
        case spectra::scene::Scene::MaterialModel::LitSurface:
        case spectra::scene::Scene::MaterialModel::UnlitSurface:
        case spectra::scene::Scene::MaterialModel::EmissiveSurface:
        case spectra::scene::Scene::MaterialModel::Volume:
        case spectra::scene::Scene::MaterialModel::PointSprite: return static_cast<std::uint32_t>(model);
        }
        throw std::runtime_error("Unknown Spectra rasterizer material model");
    }

    [[nodiscard]] std::uint32_t material_alpha_mode_code(const spectra::scene::Scene::MaterialAlphaMode alpha_mode) {
        switch (alpha_mode) {
        case spectra::scene::Scene::MaterialAlphaMode::Opaque:
        case spectra::scene::Scene::MaterialAlphaMode::Masked:
        case spectra::scene::Scene::MaterialAlphaMode::Blend: return static_cast<std::uint32_t>(alpha_mode);
        }
        throw std::runtime_error("Unknown Spectra rasterizer material alpha mode");
    }

    void validate_material_values(const spectra::scene::Scene::Material& material) {
        if (material.name.empty()) throw std::runtime_error("Rasterizer material names must not be empty");
        if (!finite_scene_vector(material.base_color)) throw std::runtime_error(std::format("Rasterizer material \"{}\" base color must be finite", material.name));
        if (!finite_scene_vector(material.emission_color)) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission color must be finite", material.name));
        if (material.base_color.x < 0.0f || material.base_color.y < 0.0f || material.base_color.z < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" base color RGB must be non-negative", material.name));
        if (material.base_color.w < 0.0f || material.base_color.w > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" alpha must be inside [0, 1]", material.name));
        if (material.emission_color.x < 0.0f || material.emission_color.y < 0.0f || material.emission_color.z < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission color must be non-negative", material.name));
        if (!std::isfinite(material.emission_strength) || material.emission_strength < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission strength must be finite and non-negative", material.name));
        if (!std::isfinite(material.roughness) || material.roughness <= 0.0f || material.roughness > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" roughness must be inside (0, 1]", material.name));
        if (!std::isfinite(material.metallic) || material.metallic < 0.0f || material.metallic > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" metallic must be inside [0, 1]", material.name));
        if (!std::isfinite(material.alpha_cutoff) || material.alpha_cutoff < 0.0f || material.alpha_cutoff > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" alpha cutoff must be inside [0, 1]", material.name));
        if (!std::isfinite(material.volume_density_scale) || material.volume_density_scale <= 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" volume density scale must be finite and positive", material.name));
        if (!std::isfinite(material.volume_temperature_scale) || material.volume_temperature_scale < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" volume temperature scale must be finite and non-negative", material.name));
        static_cast<void>(material_model_code(material.model));
        static_cast<void>(material_alpha_mode_code(material.alpha_mode));
        if ((material.model == spectra::scene::Scene::MaterialModel::PointSprite || material.model == spectra::scene::Scene::MaterialModel::Volume) && material.alpha_mode != spectra::scene::Scene::MaterialAlphaMode::Blend) throw std::runtime_error(std::format("Rasterizer {} material \"{}\" must use Blend alpha mode", material_model_name(material.model), material.name));
    }

    void validate_material_library(const spectra::scene::Scene::Document& scene) {
        std::set<std::string_view> names{};
        for (const spectra::scene::Scene::Material& material : scene.materials) {
            validate_material_values(material);
            const bool inserted = names.insert(std::string_view{material.name}).second;
            if (!inserted) throw std::runtime_error(std::format("Rasterizer material \"{}\" is duplicated", material.name));
        }
    }

    void require_surface_material(const spectra::scene::Scene::Material& material, const std::string_view mesh_name) {
        if (material.model == spectra::scene::Scene::MaterialModel::LitSurface || material.model == spectra::scene::Scene::MaterialModel::UnlitSurface || material.model == spectra::scene::Scene::MaterialModel::EmissiveSurface) return;
        throw std::runtime_error(std::format("Rasterizer mesh \"{}\" requires a surface material, got {} material \"{}\"", mesh_name, material_model_name(material.model), material.name));
    }

    void require_point_sprite_material(const spectra::scene::Scene::Material& material, const std::string_view point_cloud_name) {
        if (material.model == spectra::scene::Scene::MaterialModel::PointSprite) return;
        throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" requires a PointSprite material, got {} material \"{}\"", point_cloud_name, material_model_name(material.model), material.name));
    }

    void require_volume_material(const spectra::scene::Scene::Material& material, const std::string_view volume_name) {
        if (material.model == spectra::scene::Scene::MaterialModel::Volume) return;
        throw std::runtime_error(std::format("Rasterizer volume \"{}\" requires a Volume material, got {} material \"{}\"", volume_name, material_model_name(material.model), material.name));
    }

    [[nodiscard]] DrawPushConstantsData make_draw_push_constants(const spectra::scene::Transform& transform, const spectra::scene::Scene::Material& material) {
        const spectra::rasterizer::math::Matrix4 model_matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
        return DrawPushConstantsData{
            .model      = model_matrix.values,
            .base_color = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .emission   = {material.emission_color.x * material.emission_strength, material.emission_color.y * material.emission_strength, material.emission_color.z * material.emission_strength, 0.0f},
            .material   = {material.roughness, material.metallic, material.alpha_cutoff, 0.0f},
            .flags      = {material_model_code(material.model), material_alpha_mode_code(material.alpha_mode), 0u, 0u},
        };
    }

    [[nodiscard]] SelectionPushConstantsData make_selection_push_constants(const spectra::scene::Transform& transform, const spectra::scene::Scene::Material& material, const std::array<float, 4>& color, const std::uint32_t object_id) {
        const spectra::rasterizer::math::Matrix4 model_matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
        return SelectionPushConstantsData{
            .model      = model_matrix.values,
            .color      = color,
            .base_color = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .material   = {material.roughness, material.metallic, material.alpha_cutoff, 0.0f},
            .flags      = {object_id, material_alpha_mode_code(material.alpha_mode), 0u, 0u},
        };
    }

    struct LightUniformData {
        spectra::rasterizer::math::Vector3 direction{-0.35f, -0.8f, -0.45f};
        spectra::rasterizer::math::Vector3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
    };

    [[nodiscard]] LightUniformData scene_light_uniform(const spectra::scene::Scene::Document& scene) {
        LightUniformData data{};
        for (const spectra::scene::Scene::Light& light : scene.lights) {
            if (light.kind != spectra::scene::Scene::LightKind::Directional) continue;
            data.color      = to_render_vector(light.color);
            data.intensity  = light.intensity;
            break;
        }
        return data;
    }

    [[nodiscard]] float clamp_viewport_pitch(const float pitch) {
        constexpr float limit = std::numbers::pi_v<float> * 0.49f;
        return std::clamp(pitch, -limit, limit);
    }

    void draw_tooltip(const char* text) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
        ImGui::SetTooltip("%s", text);
    }

    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageAspectFlags aspect, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        if (old_layout == new_layout) return;
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

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type for Spectra rasterizer");
    }

    void draw_status_row(const char* label, const std::string_view value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    [[nodiscard]] const char* timeline_mode_text(const spectra::scene::Scene::TimelineMode mode) {
        switch (mode) {
        case spectra::scene::Scene::TimelineMode::Live: return "Live";
        case spectra::scene::Scene::TimelineMode::Record: return "Record";
        case spectra::scene::Scene::TimelineMode::Playback: return "Playback";
        }
        throw std::runtime_error("Unknown Spectra rasterizer timeline mode");
    }

    [[nodiscard]] float half_to_float(const std::uint16_t value) {
        const std::uint32_t sign     = (value >> 15u) & 0x1u;
        const std::uint32_t exponent = (value >> 10u) & 0x1fu;
        const std::uint32_t mantissa = value & 0x3ffu;
        float result{};
        if (exponent == 0u) {
            result = mantissa == 0u ? 0.0f : std::ldexp(static_cast<float>(mantissa), -24);
        } else if (exponent == 31u) {
            result = mantissa == 0u ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
        } else {
            result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
        }
        return sign == 0u ? result : -result;
    }

    [[nodiscard]] std::uint8_t float_to_srgb_byte(const float value) {
        if (!std::isfinite(value)) return 0u;
        const float linear = std::clamp(value, 0.0f, 1.0f);
        const float srgb   = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::clamp(std::lround(srgb * 255.0f), 0l, 255l));
    }

    [[nodiscard]] std::uint8_t float_to_alpha_byte(const float value) {
        if (!std::isfinite(value)) return 255u;
        return static_cast<std::uint8_t>(std::clamp(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f), 0l, 255l));
    }

    [[nodiscard]] std::vector<std::uint8_t> rgba16_float_to_rgba8(const void* source, const vk::Extent2D extent) {
        if (source == nullptr) throw std::runtime_error("Cannot encode Spectra rasterizer screenshot from null pixel data");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot encode an empty Spectra rasterizer screenshot");
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u);
        const std::byte* bytes = static_cast<const std::byte*>(source);
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                const std::size_t pixel_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) + static_cast<std::size_t>(x);
                const std::size_t source_base = pixel_index * 4u * sizeof(std::uint16_t);
                std::uint16_t red{};
                std::uint16_t green{};
                std::uint16_t blue{};
                std::uint16_t alpha{};
                std::memcpy(&red, bytes + source_base, sizeof(red));
                std::memcpy(&green, bytes + source_base + sizeof(std::uint16_t), sizeof(green));
                std::memcpy(&blue, bytes + source_base + sizeof(std::uint16_t) * 2u, sizeof(blue));
                std::memcpy(&alpha, bytes + source_base + sizeof(std::uint16_t) * 3u, sizeof(alpha));
                const std::size_t destination_base = pixel_index * 4u;
                pixels[destination_base]           = float_to_srgb_byte(half_to_float(red));
                pixels[destination_base + 1u]      = float_to_srgb_byte(half_to_float(green));
                pixels[destination_base + 2u]      = float_to_srgb_byte(half_to_float(blue));
                pixels[destination_base + 3u]      = float_to_alpha_byte(half_to_float(alpha));
            }
        }
        return pixels;
    }

    void append_u32_be(std::vector<std::uint8_t>& data, const std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    }

    [[nodiscard]] std::uint32_t png_crc32_update(std::uint32_t crc, const std::uint8_t byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        return crc;
    }

    [[nodiscard]] std::uint32_t png_crc32(const char* type, const std::vector<std::uint8_t>& data) {
        std::uint32_t crc = 0xffffffffu;
        for (std::size_t index = 0; index < 4u; ++index) crc = png_crc32_update(crc, static_cast<std::uint8_t>(type[index]));
        for (const std::uint8_t byte : data) crc = png_crc32_update(crc, byte);
        return ~crc;
    }

    void append_png_chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
        append_u32_be(png, static_cast<std::uint32_t>(data.size()));
        for (std::size_t index = 0; index < 4u; ++index) png.push_back(static_cast<std::uint8_t>(type[index]));
        png.insert(png.end(), data.begin(), data.end());
        append_u32_be(png, png_crc32(type, data));
    }

    [[nodiscard]] std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
        constexpr std::uint32_t modulus = 65521u;
        std::uint32_t a                 = 1u;
        std::uint32_t b                 = 0u;
        for (const std::uint8_t byte : data) {
            a = (a + byte) % modulus;
            b = (b + a) % modulus;
        }
        return (b << 16u) | a;
    }

    [[nodiscard]] std::vector<std::uint8_t> zlib_store(const std::vector<std::uint8_t>& data) {
        std::vector<std::uint8_t> compressed{};
        compressed.reserve(data.size() + data.size() / 65535u * 5u + 16u);
        compressed.push_back(0x78u);
        compressed.push_back(0x01u);
        std::size_t offset = 0;
        while (offset < data.size()) {
            const std::size_t block_size = std::min<std::size_t>(65535u, data.size() - offset);
            const bool final_block       = offset + block_size == data.size();
            compressed.push_back(final_block ? 0x01u : 0x00u);
            const std::uint16_t length = static_cast<std::uint16_t>(block_size);
            const std::uint16_t nlength = static_cast<std::uint16_t>(~length);
            compressed.push_back(static_cast<std::uint8_t>(length & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>((length >> 8u) & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>(nlength & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>((nlength >> 8u) & 0xffu));
            compressed.insert(compressed.end(), data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
            offset += block_size;
        }
        append_u32_be(compressed, adler32(data));
        return compressed;
    }

    void write_png_rgba8(const std::filesystem::path& path, const vk::Extent2D extent, const std::vector<std::uint8_t>& pixels) {
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot write an empty Spectra rasterizer screenshot");
        const std::size_t expected_size = static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u;
        if (pixels.size() != expected_size) throw std::runtime_error("Spectra rasterizer screenshot pixel buffer has an invalid size");

        std::vector<std::uint8_t> raw{};
        raw.reserve(static_cast<std::size_t>(extent.height) * (static_cast<std::size_t>(extent.width) * 4u + 1u));
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            raw.push_back(0u);
            const std::size_t row_begin = static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) * 4u;
            raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(row_begin), pixels.begin() + static_cast<std::ptrdiff_t>(row_begin + static_cast<std::size_t>(extent.width) * 4u));
        }

        std::vector<std::uint8_t> png{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
        std::vector<std::uint8_t> ihdr{};
        append_u32_be(ihdr, extent.width);
        append_u32_be(ihdr, extent.height);
        ihdr.push_back(8u);
        ihdr.push_back(6u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        append_png_chunk(png, "IHDR", ihdr);
        append_png_chunk(png, "IDAT", zlib_store(raw));
        const std::vector<std::uint8_t> empty_chunk{};
        append_png_chunk(png, "IEND", empty_chunk);

        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary};
        if (!output) throw std::runtime_error(std::format("Failed to open Spectra rasterizer screenshot file: {}", path.string()));
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!output) throw std::runtime_error(std::format("Failed to write Spectra rasterizer screenshot file: {}", path.string()));
    }

    [[nodiscard]] std::filesystem::path next_screenshot_path() {
        const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::filesystem::current_path() / "screenshots" / std::format("spectra-rasterizer-{}.png", milliseconds);
    }
} // namespace

namespace spectra::rasterizer {
    Renderer::Renderer(std::shared_ptr<scene::Scene> scene_workspace, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) {
        this->scene.workspace = std::move(scene_workspace);
        this->scene.camera_workspace = std::move(camera_workspace);
        if (this->scene.workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene workspace");
        if (this->scene.camera_workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene camera workspace");
        static_cast<void>(this->scene.workspace->document());
        this->ensure_viewport_camera_session();
        this->synchronize_viewport_camera();
    }

    Renderer::~Renderer() noexcept = default;

    std::string_view Renderer::name() {
        return "Spectra Rasterizer";
    }

    void Renderer::set_scene_workspace(std::shared_ptr<scene::Scene> scene_workspace, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) {
        if (scene_workspace == nullptr) throw std::runtime_error("Spectra rasterizer scene workspace must not be null");
        if (camera_workspace == nullptr) throw std::runtime_error("Spectra rasterizer camera workspace must not be null");
        static_cast<void>(scene_workspace->document());
        if (this->host.device != nullptr) this->host.device->waitIdle();
        this->clear_selection();
        this->selection.object_ids.clear();
        this->selection.objects_by_id.clear();
        this->selection.registry_valid = false;
        this->destroy_selection_resources();
        this->destroy_mesh_resources();
        this->destroy_point_cloud_resources();
        this->destroy_volume_resources();
        this->scene.workspace = std::move(scene_workspace);
        this->scene.camera_workspace = std::move(camera_workspace);
        this->scene.observed_camera_revision = scene::Scene::Revision{};
        this->viewport.camera_initialized = false;
        this->ensure_viewport_camera_session();
        this->synchronize_viewport_camera();
    }

    void Renderer::set_control_panel_extension(std::move_only_function<void()> draw) {
        this->ui.control_panel_extension = std::move(draw);
    }

    void Renderer::attach(HostView host) {
        if (this->lifecycle.attached) throw std::runtime_error("Spectra rasterizer plugin is already attached");
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->register_workspace_contributions(host);
        this->lifecycle.attached = true;
    }

    void Renderer::detach() noexcept {
        this->destroy_selection_resources();
        this->destroy_viewport_resources();
        this->destroy_screenshot_resources();
        this->destroy_mesh_resources();
        this->destroy_viewport_grid_resources();
        this->destroy_point_cloud_resources();
        this->destroy_volume_resources();
        this->destroy_camera_resources();
        this->host.physical_device  = nullptr;
        this->host.device           = nullptr;
        this->host.swapchain_extent = vk::Extent2D{};
        this->host.frame_count      = 0;
        this->lifecycle.attached    = false;
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::before_imgui_shutdown() noexcept {
        this->destroy_imgui_descriptor();
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::after_imgui_created() {
        this->lifecycle.imgui_ready = true;
        if (*this->viewport.image && this->viewport.imgui_descriptor == VK_NULL_HANDLE) this->create_imgui_descriptor();
    }

    void Renderer::update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count, const vk::Extent2D swapchain_extent) {
        if (frame_count == 0) throw std::runtime_error("Spectra rasterizer host frame count must be positive");
        if (swapchain_extent.width == 0 || swapchain_extent.height == 0) throw std::runtime_error("Spectra rasterizer host swapchain extent must be positive");
        this->host.physical_device  = &physical_device;
        this->host.device           = &device;
        this->host.swapchain_extent = swapchain_extent;
        this->host.frame_count      = frame_count;
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) this->ui.requested_extent = swapchain_extent;
    }

    void Renderer::wait_device_idle_for_cleanup() noexcept {
        try {
            if (this->host.device != nullptr) this->host.device->waitIdle();
        } catch (...) {
        }
    }

    void Renderer::register_workspace_contributions(HostView& host) {
        host.register_panel(Panel{
            .id                  = "rasterizer.viewport",
            .title               = "Rasterizer Viewport",
            .icon                = ICON_MS_GRID_VIEW,
            .shortcut_label      = "F7",
            .shortcut_key        = ImGuiKey_F7,
            .dock_slot           = DockSlot::Center,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
            .closable            = false,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_sidebar_tab(SidebarTab{
            .id             = "rasterizer.panel",
            .title          = "Rasterizer",
            .icon           = ICON_MS_TUNE,
            .shortcut_label = "F8",
            .shortcut_key   = ImGuiKey_F8,
            .draw           = [this] { this->draw_rasterizer_window(); },
        });
        host.register_toolbar_action(ToolbarAction{
            .id             = "rasterizer.timeline.play_pause",
            .title          = "Play / Pause",
            .icon           = ICON_MS_PLAY_PAUSE,
            .shortcut_label = "Space",
            .shortcut_key   = ImGuiKey_Space,
            .enabled        = [this] { return this->timeline_enabled(); },
            .active         = [this] { return this->timeline_playing(); },
            .trigger        = [this] { this->toggle_timeline_playback(); },
        });
        host.register_toolbar_action(ToolbarAction{
            .id             = "rasterizer.timeline.reset",
            .title          = "Reset",
            .icon           = ICON_MS_RESTART_ALT,
            .shortcut_label = "R",
            .shortcut_key   = ImGuiKey_R,
            .enabled        = [this] { return this->timeline_enabled(); },
            .active         = [] { return false; },
            .trigger        = [this] { this->request_timeline_reset(); },
        });
    }

    std::string Renderer::window_detail() const {
        const std::uint32_t width  = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->host.swapchain_extent.width;
        const std::uint32_t height = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->host.swapchain_extent.height;
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        const scene::Scene::Timeline timeline = this->scene.workspace->timeline();
        const char* scene_mode = scene->timeline_enabled ? timeline_mode_text(timeline.mode) : "Static";
        return std::format("{} | {} | {}x{}", scene->title.empty() ? scene->name : scene->title, scene_mode, width, height);
    }

    void Renderer::create_viewport_resources(const vk::Extent2D extent) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport without Vulkan handles");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer viewport with a zero extent");
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.image                             = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = this->viewport.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        this->viewport.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        this->viewport.image.bindMemory(*this->viewport.memory, 0);

        const vk::ImageViewCreateInfo image_view_create_info{{}, *this->viewport.image, vk::ImageViewType::e2D, this->viewport.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        this->viewport.view = vk::raii::ImageView{*this->host.device, image_view_create_info};
        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatOpaqueBlack,
            VK_FALSE,
        };
        this->viewport.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::ImageCreateInfo depth_image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.depth_format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.depth_image                             = vk::raii::Image{*this->host.device, depth_image_create_info};
        const vk::MemoryRequirements depth_memory_requirements = this->viewport.depth_image.getMemoryRequirements();
        const std::uint32_t depth_memory_type                  = find_memory_type_index(*this->host.physical_device, depth_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo depth_memory_allocate_info{depth_memory_requirements.size, depth_memory_type};
        this->viewport.depth_memory = vk::raii::DeviceMemory{*this->host.device, depth_memory_allocate_info};
        this->viewport.depth_image.bindMemory(*this->viewport.depth_memory, 0);
        const vk::ImageViewCreateInfo depth_view_create_info{{}, *this->viewport.depth_image, vk::ImageViewType::e2D, this->viewport.depth_format, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
        this->viewport.depth_view = vk::raii::ImageView{*this->host.device, depth_view_create_info};

        this->viewport.extent       = extent;
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
        if (this->lifecycle.imgui_ready) this->create_imgui_descriptor();
    }

    void Renderer::destroy_imgui_descriptor() noexcept {
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        this->wait_device_idle_for_cleanup();
        ImGui_ImplVulkan_RemoveTexture(this->viewport.imgui_descriptor);
        this->viewport.imgui_descriptor = VK_NULL_HANDLE;
    }

    void Renderer::destroy_viewport_resources() noexcept {
        this->destroy_imgui_descriptor();
        this->viewport.depth_view   = nullptr;
        this->viewport.depth_image  = nullptr;
        this->viewport.depth_memory = nullptr;
        this->viewport.sampler      = nullptr;
        this->viewport.view         = nullptr;
        this->viewport.image        = nullptr;
        this->viewport.memory       = nullptr;
        this->viewport.extent       = vk::Extent2D{};
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_screenshot_resources() noexcept {
        this->destroy_host_buffer(this->viewport.screenshot_buffer);
        this->viewport.screenshot_requested   = false;
        this->viewport.screenshot_pending     = false;
        this->viewport.screenshot_frame_index = 0;
        this->viewport.screenshot_extent      = vk::Extent2D{};
        this->viewport.screenshot_path.clear();
    }

    void Renderer::ensure_viewport_resources() {
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) return;
        if (*this->viewport.image && this->viewport.extent.width == this->ui.requested_extent.width && this->viewport.extent.height == this->ui.requested_extent.height) return;
        this->destroy_viewport_resources();
        this->create_viewport_resources(this->ui.requested_extent);
    }

    void Renderer::create_imgui_descriptor() {
        if (!*this->viewport.sampler || !*this->viewport.view) throw std::runtime_error("Cannot create Spectra rasterizer descriptor before viewport resources exist");
        if (this->viewport.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra rasterizer viewport descriptor is already allocated");
        this->viewport.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*this->viewport.sampler), static_cast<VkImageView>(*this->viewport.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra rasterizer viewport descriptor");
    }

    void Renderer::destroy_host_buffer(GpuBuffer& buffer) noexcept {
        if (buffer.mapped != nullptr && this->host.device != nullptr && *buffer.memory) vkUnmapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory));
        buffer.mapped   = nullptr;
        buffer.buffer   = nullptr;
        buffer.memory   = nullptr;
        buffer.capacity = 0;
    }

    void Renderer::ensure_host_buffer(GpuBuffer& buffer, const vk::DeviceSize required_size, const vk::BufferUsageFlags usage) {
        if (required_size == 0) throw std::runtime_error("Cannot allocate an empty Spectra rasterizer buffer");
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot allocate Spectra rasterizer buffers without Vulkan handles");
        if (*buffer.buffer && buffer.capacity >= required_size) return;
        this->destroy_host_buffer(buffer);
        const vk::BufferCreateInfo buffer_create_info{{}, required_size, usage, vk::SharingMode::eExclusive};
        buffer.buffer = vk::raii::Buffer{*this->host.device, buffer_create_info};
        const vk::MemoryRequirements memory_requirements = buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        buffer.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        buffer.buffer.bindMemory(*buffer.memory, 0);
        if (vkMapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory), 0, required_size, 0, &buffer.mapped) != VK_SUCCESS) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
        buffer.capacity = required_size;
        if (buffer.mapped == nullptr) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
    }

    void Renderer::create_image_2d(GpuImage2D& image, const vk::Extent2D extent, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer image without Vulkan handles");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer image with a zero extent");
        this->destroy_image_2d(image);
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            format,
            vk::Extent3D{extent.width, extent.height, 1},
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
        image.image                                      = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = image.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        image.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        image.image.bindMemory(*image.memory, 0);
        const vk::ImageViewCreateInfo image_view_create_info{{}, *image.image, vk::ImageViewType::e2D, format, {}, {aspect, 0, 1, 0, 1}};
        image.view   = vk::raii::ImageView{*this->host.device, image_view_create_info};
        image.extent = extent;
        image.format = format;
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_image_2d(GpuImage2D& image) noexcept {
        image.view   = nullptr;
        image.image  = nullptr;
        image.memory = nullptr;
        image.extent = vk::Extent2D{};
        image.format = vk::Format{};
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::create_volume_image(GpuImage3D& image, const vk::Extent3D extent) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume image without Vulkan handles");
        if (extent.width == 0 || extent.height == 0 || extent.depth == 0) throw std::runtime_error("Cannot create Spectra rasterizer volume image with zero dimensions");
        this->destroy_volume_image(image);
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e3D,
            vk::Format::eR32Sfloat,
            extent,
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        image.image                                      = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = image.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        image.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        image.image.bindMemory(*image.memory, 0);
        const vk::ImageViewCreateInfo image_view_create_info{{}, *image.image, vk::ImageViewType::e3D, vk::Format::eR32Sfloat, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        image.view   = vk::raii::ImageView{*this->host.device, image_view_create_info};
        image.extent = extent;
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_volume_image(GpuImage3D& image) noexcept {
        image.view   = nullptr;
        image.image  = nullptr;
        image.memory = nullptr;
        image.extent = vk::Extent3D{};
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_camera_resources() noexcept {
        if (this->camera.frame_count == 0 && !*this->camera.descriptor_set_layout && !*this->camera.descriptor_pool && this->camera.descriptor_sets.size() == 0 && this->camera.uniform_buffers.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (GpuBuffer& uniform_buffer : this->camera.uniform_buffers) this->destroy_host_buffer(uniform_buffer);
        this->camera.uniform_buffers.clear();
        this->camera.descriptor_sets       = nullptr;
        this->camera.descriptor_pool       = nullptr;
        this->camera.descriptor_set_layout = nullptr;
        this->camera.frame_count           = 0;
    }

    void Renderer::destroy_mesh_resources() noexcept {
        if (this->mesh_pass.frame_count == 0 && !*this->mesh_pass.pipeline_layout && !*this->mesh_pass.pipeline && !*this->mesh_pass.transparent_pipeline && this->mesh_pass.frame_scenes.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameSceneResources& frame_scene : this->mesh_pass.frame_scenes) {
            this->destroy_host_buffer(frame_scene.vertexBuffer);
            this->destroy_host_buffer(frame_scene.indexBuffer);
        }
        this->mesh_pass.frame_scenes.clear();
        this->mesh_pass.transparent_pipeline = nullptr;
        this->mesh_pass.pipeline             = nullptr;
        this->mesh_pass.pipeline_layout      = nullptr;
        this->mesh_pass.frame_count          = 0;
    }

    void Renderer::destroy_viewport_grid_resources() noexcept {
        if (this->viewport_grid_pass.frame_count == 0 && !*this->viewport_grid_pass.pipeline_layout && !*this->viewport_grid_pass.pipeline) return;
        this->wait_device_idle_for_cleanup();
        this->viewport_grid_pass.pipeline        = nullptr;
        this->viewport_grid_pass.pipeline_layout = nullptr;
        this->viewport_grid_pass.frame_count     = 0;
    }

    void Renderer::destroy_point_cloud_resources() noexcept {
        if (this->point_cloud_pass.frame_count == 0 && !*this->point_cloud_pass.pipeline_layout && !*this->point_cloud_pass.pipeline && this->point_cloud_pass.frame_point_clouds.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FramePointCloudResources& frame_point_cloud : this->point_cloud_pass.frame_point_clouds) this->destroy_host_buffer(frame_point_cloud.instanceBuffer);
        this->point_cloud_pass.frame_point_clouds.clear();
        this->point_cloud_pass.pipeline        = nullptr;
        this->point_cloud_pass.pipeline_layout = nullptr;
        this->point_cloud_pass.frame_count     = 0;
    }

    void Renderer::destroy_volume_resources() noexcept {
        if (this->volume_pass.frame_count == 0 && !*this->volume_pass.descriptor_set_layout && !*this->volume_pass.descriptor_pool && this->volume_pass.descriptor_sets.size() == 0 && !*this->volume_pass.sampler && !*this->volume_pass.pipeline_layout && !*this->volume_pass.pipeline && this->volume_pass.frame_volumes.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameVolumeResources& frame_volume : this->volume_pass.frame_volumes) {
            this->destroy_host_buffer(frame_volume.densityStagingBuffer);
            this->destroy_host_buffer(frame_volume.temperatureStagingBuffer);
            this->destroy_volume_image(frame_volume.densityImage);
            this->destroy_volume_image(frame_volume.temperatureImage);
        }
        this->volume_pass.frame_volumes.clear();
        this->volume_pass.pipeline              = nullptr;
        this->volume_pass.pipeline_layout       = nullptr;
        this->volume_pass.sampler               = nullptr;
        this->volume_pass.descriptor_sets       = nullptr;
        this->volume_pass.descriptor_pool       = nullptr;
        this->volume_pass.descriptor_set_layout = nullptr;
        this->volume_pass.frame_count           = 0;
    }

    void Renderer::destroy_selection_resources() noexcept {
        if (this->selection.frame_count != 0 || *this->selection.object_id_image.image || *this->selection.depth_image.image || *this->selection.mask_image.image || !this->selection.readback_buffers.empty()) this->wait_device_idle_for_cleanup();
        for (GpuBuffer& readback_buffer : this->selection.readback_buffers) this->destroy_host_buffer(readback_buffer);
        this->selection.readback_buffers.clear();
        this->destroy_image_2d(this->selection.object_id_image);
        this->destroy_image_2d(this->selection.depth_image);
        this->destroy_image_2d(this->selection.mask_image);
        this->selection.outline_pipeline              = nullptr;
        this->selection.outline_pipeline_layout       = nullptr;
        this->selection.volume_mask_pipeline          = nullptr;
        this->selection.volume_mask_pipeline_layout   = nullptr;
        this->selection.point_cloud_mask_pipeline         = nullptr;
        this->selection.point_cloud_mask_pipeline_layout  = nullptr;
        this->selection.mesh_mask_pipeline            = nullptr;
        this->selection.mesh_mask_pipeline_layout     = nullptr;
        this->selection.volume_picking_pipeline       = nullptr;
        this->selection.volume_picking_pipeline_layout = nullptr;
        this->selection.point_cloud_picking_pipeline      = nullptr;
        this->selection.point_cloud_picking_pipeline_layout = nullptr;
        this->selection.mesh_picking_pipeline         = nullptr;
        this->selection.mesh_picking_pipeline_layout  = nullptr;
        this->selection.outline_descriptor_sets       = nullptr;
        this->selection.outline_descriptor_pool       = nullptr;
        this->selection.outline_descriptor_set_layout = nullptr;
        this->selection.mask_sampler                  = nullptr;
        this->selection.frame_count                   = 0;
        this->selection.pick_requested                = false;
        this->selection.pick_pending                  = false;
        this->selection.requested_select              = false;
        this->selection.requested_additive            = false;
        this->selection.pending_select                = false;
        this->selection.pending_additive              = false;
        this->selection.requested_x                   = 0;
        this->selection.requested_y                   = 0;
        this->selection.pending_frame_index           = 0;
    }

    void Renderer::ensure_camera_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer camera resources without Vulkan handles");
        if (this->host.frame_count == 0) throw std::runtime_error("Spectra rasterizer frame count must be positive");
        if (*this->camera.descriptor_set_layout && this->camera.descriptor_sets.size() == this->host.frame_count && this->camera.uniform_buffers.size() == this->host.frame_count && this->camera.frame_count == this->host.frame_count) return;
        this->destroy_selection_resources();
        this->destroy_mesh_resources();
        this->destroy_viewport_grid_resources();
        this->destroy_point_cloud_resources();
        this->destroy_volume_resources();
        this->destroy_camera_resources();

        const vk::DescriptorSetLayoutBinding camera_binding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, 1u, &camera_binding};
        this->camera.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};

        const vk::DescriptorPoolSize descriptor_pool_size{vk::DescriptorType::eUniformBuffer, this->host.frame_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count, 1u, &descriptor_pool_size};
        this->camera.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->camera.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->camera.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->camera.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer camera descriptor sets");

        this->camera.uniform_buffers.resize(this->host.frame_count);
        for (GpuBuffer& uniform_buffer : this->camera.uniform_buffers) this->ensure_host_buffer(uniform_buffer, sizeof(CameraUniformData), vk::BufferUsageFlagBits::eUniformBuffer);

        std::vector<vk::DescriptorBufferInfo> buffer_infos{};
        std::vector<vk::WriteDescriptorSet> descriptor_writes{};
        buffer_infos.reserve(this->host.frame_count);
        descriptor_writes.reserve(this->host.frame_count);
        for (std::uint32_t frame_index = 0; frame_index < this->host.frame_count; ++frame_index) {
            buffer_infos.emplace_back(*this->camera.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData));
            descriptor_writes.emplace_back(*this->camera.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_infos.back(), nullptr);
        }
        this->host.device->updateDescriptorSets(descriptor_writes, {});
        this->camera.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_mesh_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer mesh resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer mesh pass requires camera descriptors");
        if (*this->mesh_pass.pipeline && *this->mesh_pass.transparent_pipeline && this->mesh_pass.frame_count == this->host.frame_count) return;
        this->destroy_mesh_resources();

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_mesh_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_mesh_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(RasterizerVertex), vk::VertexInputRate::eVertex};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, nx))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo opaque_depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo transparent_depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState opaque_color_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        constexpr vk::PipelineColorBlendAttachmentState transparent_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo opaque_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &opaque_color_blend_attachment};
        const vk::PipelineColorBlendStateCreateInfo transparent_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &transparent_color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(DrawPushConstantsData)};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
        this->mesh_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        const auto create_mesh_pipeline = [&](const vk::PipelineDepthStencilStateCreateInfo& depth_state, const vk::PipelineColorBlendStateCreateInfo& blend_state) {
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*this->mesh_pass.pipeline_layout);
            return vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };
        this->mesh_pass.pipeline = create_mesh_pipeline(opaque_depth_stencil_state, opaque_color_blend_state);
        this->mesh_pass.transparent_pipeline = create_mesh_pipeline(transparent_depth_stencil_state, transparent_color_blend_state);
        this->mesh_pass.frame_scenes.resize(this->host.frame_count);
        this->mesh_pass.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_viewport_grid_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport grid resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer viewport grid pass requires camera descriptors");
        if (*this->viewport_grid_pass.pipeline && this->viewport_grid_pass.frame_count == this->host.frame_count) return;
        this->destroy_viewport_grid_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 0u, nullptr};
        this->viewport_grid_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_viewport_grid_vertex_spv_sizeInBytes, spectra_rasterizer_viewport_grid_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_viewport_grid_fragment_spv_sizeInBytes, spectra_rasterizer_viewport_grid_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState grid_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo grid_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &grid_color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&grid_color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->viewport_grid_pass.pipeline_layout);
        this->viewport_grid_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->viewport_grid_pass.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_point_cloud_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer point cloud resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer point cloud pass requires camera descriptors");
        if (*this->point_cloud_pass.pipeline && this->point_cloud_pass.frame_count == this->host.frame_count) return;
        this->destroy_point_cloud_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 0u, nullptr};
        this->point_cloud_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_point_cloud_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_point_cloud_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(PointCloudInstance), vk::VertexInputRate::eInstance};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOne,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->point_cloud_pass.pipeline_layout);
        this->point_cloud_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->point_cloud_pass.frame_count = this->host.frame_count;
        this->point_cloud_pass.frame_point_clouds.resize(this->host.frame_count);
    }

    void Renderer::ensure_volume_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer volume pass requires camera descriptors");
        if (*this->volume_pass.pipeline && this->volume_pass.frame_count == this->host.frame_count) return;
        this->destroy_volume_resources();

        const std::array descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_bindings.size()), descriptor_bindings.data()};
        this->volume_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};
        const std::array descriptor_pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, this->host.frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, this->host.frame_count * 2u},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, this->host.frame_count},
        };
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
        this->volume_pass.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->volume_pass.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->volume_pass.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->volume_pass.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer volume descriptor sets");

        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->volume_pass.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::DescriptorSetLayout volume_descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(VolumePushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &volume_descriptor_set_layout, 1u, &push_constant_range};
        this->volume_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_volume_vertex_spv_sizeInBytes, spectra_rasterizer_volume_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_volume_fragment_spv_sizeInBytes, spectra_rasterizer_volume_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->volume_pass.pipeline_layout);
        this->volume_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->volume_pass.frame_count = this->host.frame_count;
        this->volume_pass.frame_volumes.resize(this->host.frame_count);
    }

    void Renderer::ensure_selection_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer selection resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer selection pass requires camera descriptors");
        if (!*this->volume_pass.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer selection pass requires volume descriptors");
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) return;
        if (*this->selection.object_id_image.image && *this->selection.depth_image.image && *this->selection.mask_image.image && *this->selection.mesh_picking_pipeline && *this->selection.outline_pipeline && this->selection.object_id_image.extent == this->viewport.extent && this->selection.frame_count == this->host.frame_count) return;
        this->destroy_selection_resources();

        this->create_image_2d(this->selection.object_id_image, this->viewport.extent, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, vk::ImageAspectFlagBits::eColor);
        this->create_image_2d(this->selection.depth_image, this->viewport.extent, this->viewport.depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
        this->create_image_2d(this->selection.mask_image, this->viewport.extent, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);

        const vk::SamplerCreateInfo mask_sampler_create_info{
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
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->selection.mask_sampler = vk::raii::Sampler{*this->host.device, mask_sampler_create_info};

        const std::array outline_descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo outline_descriptor_layout_create_info{{}, static_cast<std::uint32_t>(outline_descriptor_bindings.size()), outline_descriptor_bindings.data()};
        this->selection.outline_descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, outline_descriptor_layout_create_info};
        const std::array outline_descriptor_pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1u},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1u},
        };
        const vk::DescriptorPoolCreateInfo outline_descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1u, static_cast<std::uint32_t>(outline_descriptor_pool_sizes.size()), outline_descriptor_pool_sizes.data()};
        this->selection.outline_descriptor_pool = vk::raii::DescriptorPool{*this->host.device, outline_descriptor_pool_create_info};
        const vk::DescriptorSetLayout outline_descriptor_set_layout = *this->selection.outline_descriptor_set_layout;
        const vk::DescriptorSetAllocateInfo outline_descriptor_set_allocate_info{*this->selection.outline_descriptor_pool, 1u, &outline_descriptor_set_layout};
        this->selection.outline_descriptor_sets = vk::raii::DescriptorSets{*this->host.device, outline_descriptor_set_allocate_info};
        if (this->selection.outline_descriptor_sets.size() != 1u) throw std::runtime_error("Failed to allocate Spectra rasterizer selection outline descriptor set");
        const vk::DescriptorImageInfo mask_image_info{{}, *this->selection.mask_image.view, vk::ImageLayout::eShaderReadOnlyOptimal};
        const vk::DescriptorImageInfo mask_sampler_info{*this->selection.mask_sampler, {}, vk::ImageLayout::eUndefined};
        const std::array outline_descriptor_writes{
            vk::WriteDescriptorSet{*this->selection.outline_descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eSampledImage, &mask_image_info, nullptr, nullptr},
            vk::WriteDescriptorSet{*this->selection.outline_descriptor_sets.at(0), 1u, 0u, 1u, vk::DescriptorType::eSampler, &mask_sampler_info, nullptr, nullptr},
        };
        this->host.device->updateDescriptorSets(outline_descriptor_writes, {});

        const vk::DescriptorSetLayout camera_descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::DescriptorSetLayout volume_descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        constexpr vk::PipelineColorBlendAttachmentState write_color_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo write_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &write_color_blend_attachment};
        constexpr vk::PipelineColorBlendAttachmentState write_id_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR,
        };
        const vk::PipelineColorBlendStateCreateInfo write_id_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &write_id_blend_attachment};
        constexpr vk::PipelineColorBlendAttachmentState outline_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo outline_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &outline_color_blend_attachment};
        const vk::PipelineDepthStencilStateCreateInfo selection_depth_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo no_depth_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};

        const vk::VertexInputBindingDescription mesh_vertex_binding{0u, sizeof(RasterizerVertex), vk::VertexInputRate::eVertex};
        const std::array mesh_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, nx))},
        };
        const vk::PipelineVertexInputStateCreateInfo mesh_vertex_input_state{{}, 1u, &mesh_vertex_binding, static_cast<std::uint32_t>(mesh_vertex_attributes.size()), mesh_vertex_attributes.data()};
        const std::array mesh_selection_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
        };
        const vk::PipelineVertexInputStateCreateInfo mesh_selection_vertex_input_state{{}, 1u, &mesh_vertex_binding, static_cast<std::uint32_t>(mesh_selection_vertex_attributes.size()), mesh_selection_vertex_attributes.data()};
        const vk::VertexInputBindingDescription point_cloud_vertex_binding{0u, sizeof(PointCloudInstance), vk::VertexInputRate::eInstance};
        const std::array point_cloud_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo point_cloud_vertex_input_state{{}, 1u, &point_cloud_vertex_binding, static_cast<std::uint32_t>(point_cloud_vertex_attributes.size()), point_cloud_vertex_attributes.data()};
        const std::array point_cloud_selection_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
        };
        const vk::PipelineVertexInputStateCreateInfo point_cloud_selection_vertex_input_state{{}, 1u, &point_cloud_vertex_binding, static_cast<std::uint32_t>(point_cloud_selection_vertex_attributes.size()), point_cloud_selection_vertex_attributes.data()};
        const vk::PipelineVertexInputStateCreateInfo fullscreen_vertex_input_state{};

        const auto create_graphics_pipeline = [&](const std::size_t vertex_size, const std::uint32_t* vertex_data, const std::size_t fragment_size, const std::uint32_t* fragment_data, const vk::PipelineVertexInputStateCreateInfo& vertex_input_state, const vk::DescriptorSetLayout descriptor_set_layout, const vk::DeviceSize push_constant_size, const vk::Format color_format, const vk::Format depth_format, const vk::PipelineDepthStencilStateCreateInfo& depth_state, const vk::PipelineColorBlendStateCreateInfo& blend_state, vk::raii::PipelineLayout& pipeline_layout, vk::raii::Pipeline& pipeline) {
            const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, vertex_size, vertex_data};
            const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, fragment_size, fragment_data};
            const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
            const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };
            const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, static_cast<std::uint32_t>(push_constant_size)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
            pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
            const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, depth_format};
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*pipeline_layout);
            pipeline = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };

        create_graphics_pipeline(spectra_rasterizer_mesh_pick_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_pick_vertex_spv, spectra_rasterizer_mesh_pick_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_pick_fragment_spv, mesh_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(SelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.mesh_picking_pipeline_layout, this->selection.mesh_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_point_cloud_pick_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_pick_vertex_spv, spectra_rasterizer_point_cloud_pick_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_pick_fragment_spv, point_cloud_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(PointCloudSelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.point_cloud_picking_pipeline_layout, this->selection.point_cloud_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_volume_pick_vertex_spv_sizeInBytes, spectra_rasterizer_volume_pick_vertex_spv, spectra_rasterizer_volume_pick_fragment_spv_sizeInBytes, spectra_rasterizer_volume_pick_fragment_spv, fullscreen_vertex_input_state, volume_descriptor_set_layout, sizeof(VolumeSelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.volume_picking_pipeline_layout, this->selection.volume_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_mesh_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_selection_mask_vertex_spv, spectra_rasterizer_mesh_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_selection_mask_fragment_spv, mesh_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(SelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.mesh_mask_pipeline_layout, this->selection.mesh_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_point_cloud_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_selection_mask_vertex_spv, spectra_rasterizer_point_cloud_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_selection_mask_fragment_spv, point_cloud_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(PointCloudSelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.point_cloud_mask_pipeline_layout, this->selection.point_cloud_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_volume_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_volume_selection_mask_vertex_spv, spectra_rasterizer_volume_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_volume_selection_mask_fragment_spv, fullscreen_vertex_input_state, volume_descriptor_set_layout, sizeof(VolumeSelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.volume_mask_pipeline_layout, this->selection.volume_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_selection_outline_vertex_spv_sizeInBytes, spectra_rasterizer_selection_outline_vertex_spv, spectra_rasterizer_selection_outline_fragment_spv_sizeInBytes, spectra_rasterizer_selection_outline_fragment_spv, fullscreen_vertex_input_state, *this->selection.outline_descriptor_set_layout, sizeof(OutlinePushConstantsData), this->viewport.format, vk::Format::eUndefined, no_depth_state, outline_color_blend_state, this->selection.outline_pipeline_layout, this->selection.outline_pipeline);

        this->selection.readback_buffers.resize(this->host.frame_count);
        for (GpuBuffer& readback_buffer : this->selection.readback_buffers) this->ensure_host_buffer(readback_buffer, sizeof(std::uint32_t), vk::BufferUsageFlagBits::eTransferDst);
        this->selection.frame_count = this->host.frame_count;
    }

    scene::Scene::Material Renderer::resolve_material(const std::string_view material_name) const {
        if (material_name.empty()) throw std::runtime_error("Rasterizer material name must not be empty");
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        for (const scene::Scene::Material& material : scene->materials) {
            if (material.name == material_name) return material;
        }
        throw std::runtime_error(std::format("Rasterizer material \"{}\" does not exist", material_name));
    }

    const scene::Scene::VolumeChannel* Renderer::find_volume_channel(const scene::Scene::VolumeGrid& volume, const std::string_view channel_name) const {
        for (const scene::Scene::VolumeChannel& channel : volume.channels) {
            if (channel.name != channel_name) continue;
            const std::uint64_t expected_count = static_cast<std::uint64_t>(channel.dimensions[0]) * static_cast<std::uint64_t>(channel.dimensions[1]) * static_cast<std::uint64_t>(channel.dimensions[2]);
            if (expected_count == 0u) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" has zero dimensions", channel.name));
            if (expected_count != channel.values.size()) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" value count does not match dimensions", channel.name));
            for (const float value : channel.values) {
                if (!std::isfinite(value)) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" contains a non-finite value", channel.name));
            }
            return &channel;
        }
        return nullptr;
    }

    const scene::Scene::VolumeChannel& Renderer::require_volume_channel(const scene::Scene::VolumeGrid& volume, const std::string_view channel_name) const {
        const scene::Scene::VolumeChannel* channel = this->find_volume_channel(volume, channel_name);
        if (channel != nullptr) return *channel;
        throw std::runtime_error(std::format("Rasterizer volume \"{}\" does not contain required channel \"{}\"", volume.name, channel_name));
    }

    const scene::Scene::VolumeGrid* Renderer::select_render_volume_grid(const std::span<const scene::Scene::VolumeGrid> volumes) const {
        if (volumes.empty()) return nullptr;
        if (volumes.size() != 1u) throw std::runtime_error("Spectra rasterizer volume pass supports exactly one volume grid");
        return &volumes.front();
    }

    void Renderer::rebuild_selection_registry_if_needed() {
        const scene::Scene::Revision scene_revision = this->scene.workspace->revision();
        if (this->selection.registry_valid && this->selection.registry_revision == scene_revision) return;
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        this->selection.object_ids.clear();
        this->selection.objects_by_id.clear();
        std::set<ObjectKey> unique_keys{};
        std::uint32_t next_id = 1u;
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (mesh.positions.empty()) continue;
            this->register_selectable_object(SelectableObjectKind::Mesh, mesh.name, unique_keys, next_id);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (point_cloud.positions.empty()) continue;
            this->register_selectable_object(SelectableObjectKind::PointCloud, point_cloud.name, unique_keys, next_id);
        }
        const scene::Scene::VolumeGrid* volume = this->select_render_volume_grid(resolved_frame.volumes);
        if (volume != nullptr) this->register_selectable_object(SelectableObjectKind::VolumeGrid, volume->name, unique_keys, next_id);
        this->selection.registry_revision = scene_revision;
        this->selection.registry_valid = true;
        this->prune_selection_to_registry();
    }

    void Renderer::register_selectable_object(const SelectableObjectKind kind, const std::string_view name, std::set<ObjectKey>& unique_keys, std::uint32_t& next_id) {
        if (name.empty()) throw std::runtime_error("Spectra rasterizer selectable objects must have non-empty names");
        if (next_id == std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Spectra rasterizer selectable object id range is exhausted");
        ObjectKey key{kind, std::string{name}};
        const bool inserted = unique_keys.insert(key).second;
        if (!inserted) throw std::runtime_error(std::format("Spectra rasterizer selectable object name \"{}\" is duplicated within its object kind", name));
        const std::uint32_t object_id = next_id++;
        this->selection.object_ids.emplace(key, object_id);
        this->selection.objects_by_id.emplace(object_id, std::move(key));
    }

    std::uint32_t Renderer::object_id_for(const ObjectKey& key) const {
        const std::map<ObjectKey, std::uint32_t>::const_iterator iter = this->selection.object_ids.find(key);
        if (iter == this->selection.object_ids.end()) throw std::runtime_error(std::format("Spectra rasterizer object \"{}\" is not registered for selection", key.name));
        return iter->second;
    }

    const Renderer::ObjectKey* Renderer::object_for_id(const std::uint32_t object_id) const {
        if (object_id == 0u) return nullptr;
        const std::map<std::uint32_t, ObjectKey>::const_iterator iter = this->selection.objects_by_id.find(object_id);
        if (iter == this->selection.objects_by_id.end()) throw std::runtime_error(std::format("Spectra rasterizer picking returned unknown object id {}", object_id));
        return &iter->second;
    }

    void Renderer::prune_selection_to_registry() {
        for (std::set<ObjectKey>::iterator iter = this->selection.selected_objects.begin(); iter != this->selection.selected_objects.end();) {
            if (this->selection.object_ids.contains(*iter)) {
                ++iter;
                continue;
            }
            iter = this->selection.selected_objects.erase(iter);
        }
        if (this->selection.active_object.has_value() && !this->selection.object_ids.contains(*this->selection.active_object)) this->selection.active_object.reset();
        if (this->selection.hovered_object.has_value() && !this->selection.object_ids.contains(*this->selection.hovered_object)) this->selection.hovered_object.reset();
        if (!this->selection.active_object.has_value() && !this->selection.selected_objects.empty()) this->selection.active_object = *this->selection.selected_objects.begin();
    }

    bool Renderer::object_selected(const ObjectKey& key) const {
        return this->selection.selected_objects.contains(key);
    }

    bool Renderer::object_hovered(const ObjectKey& key) const {
        return this->selection.hovered_object.has_value() && *this->selection.hovered_object == key;
    }

    bool Renderer::object_active(const ObjectKey& key) const {
        return this->selection.active_object.has_value() && *this->selection.active_object == key;
    }

    std::array<float, 4> Renderer::selection_mask_color(const ObjectKey& key) const {
        if (this->object_active(key)) return {0.0f, 1.0f, 1.0f, 1.0f};
        if (this->object_selected(key)) return {0.0f, 1.0f, 0.0f, 1.0f};
        if (this->object_hovered(key)) return {1.0f, 0.0f, 0.0f, 1.0f};
        return {};
    }

    std::string Renderer::object_label(const ObjectKey& key) const {
        const char* kind_text = "Object";
        switch (key.kind) {
        case SelectableObjectKind::Mesh: kind_text = "Mesh"; break;
        case SelectableObjectKind::PointCloud: kind_text = "Point Cloud"; break;
        case SelectableObjectKind::VolumeGrid: kind_text = "Volume"; break;
        }
        return std::format("{} | {}", kind_text, key.name);
    }

    std::string Renderer::selection_summary() const {
        if (this->selection.active_object.has_value()) return std::format("{} selected | active {}", this->selection.selected_objects.size(), this->object_label(*this->selection.active_object));
        if (this->selection.hovered_object.has_value()) return std::format("hover {}", this->object_label(*this->selection.hovered_object));
        return "No selection";
    }

    void Renderer::clear_selection() {
        this->selection.selected_objects.clear();
        this->selection.active_object.reset();
        this->selection.hovered_object.reset();
        this->selection.pick_requested = false;
        this->selection.pick_pending = false;
    }

    void Renderer::apply_pick_result(const std::uint32_t object_id, const bool select, const bool additive) {
        const ObjectKey* key = this->object_for_id(object_id);
        if (key == nullptr) {
            this->selection.hovered_object.reset();
            if (select && !additive) {
                this->selection.selected_objects.clear();
                this->selection.active_object.reset();
            }
            return;
        }
        this->selection.hovered_object = *key;
        if (!select) return;
        if (additive) {
            if (this->selection.selected_objects.contains(*key)) {
                this->selection.selected_objects.erase(*key);
                if (this->selection.active_object.has_value() && *this->selection.active_object == *key) this->selection.active_object.reset();
            } else {
                this->selection.selected_objects.insert(*key);
                this->selection.active_object = *key;
            }
        } else {
            this->selection.selected_objects.clear();
            this->selection.selected_objects.insert(*key);
            this->selection.active_object = *key;
        }
        if (!this->selection.active_object.has_value() && !this->selection.selected_objects.empty()) this->selection.active_object = *this->selection.selected_objects.begin();
    }

    void Renderer::upload_scene_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer frame scene index is out of range");
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.workspace->revision();
        if (frame_scene.uploadedRevision == scene_revision) return;

        std::vector<RasterizerVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::vector<RenderDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (mesh.positions.empty()) continue;
            const scene::Scene::Material material = this->resolve_material(mesh.material_name);
            require_surface_material(material, mesh.name);
            if (mesh.normals.size() != mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide one normal per position", mesh.name));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide triangle indices", mesh.name));
            if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer vertex count exceeds uint32 range");
            const std::uint32_t vertex_offset = static_cast<std::uint32_t>(vertices.size());
            scene::Vector3 minimum{};
            scene::Vector3 maximum{};
            bool bounds_valid{false};
            vertices.reserve(vertices.size() + mesh.positions.size());
            for (std::size_t vertex_index = 0; vertex_index < mesh.positions.size(); ++vertex_index) {
                const scene::Vector3 position = mesh.positions.at(vertex_index);
                const scene::Vector3 normal   = mesh.normals.at(vertex_index);
                if (!finite_scene_vector(position)) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" contains a non-finite position", mesh.name));
                if (!finite_scene_vector(normal)) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" contains a non-finite normal", mesh.name));
                if (!bounds_valid) {
                    minimum      = position;
                    maximum      = position;
                    bounds_valid = true;
                } else {
                    minimum.x = std::min(minimum.x, position.x);
                    minimum.y = std::min(minimum.y, position.y);
                    minimum.z = std::min(minimum.z, position.z);
                    maximum.x = std::max(maximum.x, position.x);
                    maximum.y = std::max(maximum.y, position.y);
                    maximum.z = std::max(maximum.z, position.z);
                }
                vertices.push_back(RasterizerVertex{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .nx = normal.x,
                    .ny = normal.y,
                    .nz = normal.z,
                });
            }
            const std::uint32_t first_index = static_cast<std::uint32_t>(indices.size());
            indices.reserve(indices.size() + mesh.indices.size());
            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" has an out-of-range index", mesh.name));
                indices.push_back(vertex_offset + index);
            }
            const ObjectKey object_key{SelectableObjectKind::Mesh, mesh.name};
            draw_commands.push_back(RenderDrawCommand{
                .objectKey  = object_key,
                .objectId   = this->object_id_for(object_key),
                .firstIndex = first_index,
                .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
                .sortPoint  = scene::Vector3{(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f},
                .transform  = mesh.transform,
                .material   = material,
            });
        }

        if (!vertices.empty()) {
            const vk::DeviceSize vertex_bytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(RasterizerVertex));
            const vk::DeviceSize index_bytes  = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            this->ensure_host_buffer(frame_scene.vertexBuffer, vertex_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            this->ensure_host_buffer(frame_scene.indexBuffer, index_bytes, vk::BufferUsageFlagBits::eIndexBuffer);
            std::memcpy(frame_scene.vertexBuffer.mapped, vertices.data(), static_cast<std::size_t>(vertex_bytes));
            std::memcpy(frame_scene.indexBuffer.mapped, indices.data(), static_cast<std::size_t>(index_bytes));
        }
        frame_scene.drawCommands     = std::move(draw_commands);
        frame_scene.uploadedRevision = scene_revision;
    }

    void Renderer::upload_point_cloud_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->point_cloud_pass.frame_point_clouds.size()) throw std::runtime_error("Spectra rasterizer point cloud frame index is out of range");
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.workspace->revision();
        if (frame_point_cloud.uploadedRevision == scene_revision) return;

        std::vector<PointCloudInstance> instances{};
        std::vector<PointCloudDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (point_cloud.positions.empty()) continue;
            const scene::Scene::Material material = this->resolve_material(point_cloud.material_name);
            require_point_sprite_material(material, point_cloud.name);
            if (point_cloud.radii.size() != point_cloud.positions.size()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" must provide one radius per position", point_cloud.name));
            if (point_cloud.colors.size() != point_cloud.positions.size()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" must provide one color per position", point_cloud.name));
            if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer point cloud instance count exceeds uint32 range");
            const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
            const spectra::rasterizer::math::Matrix4 transform = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            const float emission_red = material.emission_color.x * material.emission_strength;
            const float emission_green = material.emission_color.y * material.emission_strength;
            const float emission_blue = material.emission_color.z * material.emission_strength;
            instances.reserve(instances.size() + point_cloud.positions.size());
            for (std::size_t point_index = 0; point_index < point_cloud.positions.size(); ++point_index) {
                if (!std::isfinite(point_cloud.radii.at(point_index)) || point_cloud.radii.at(point_index) <= 0.0f) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains an invalid radius", point_cloud.name));
                if (!finite_scene_vector(point_cloud.positions.at(point_index))) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains a non-finite position", point_cloud.name));
                const scene::Vector4 color = point_cloud.colors.at(point_index);
                if (!finite_scene_vector(color)) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains a non-finite color", point_cloud.name));
                if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || color.w < 0.0f || color.w > 1.0f) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains an invalid color", point_cloud.name));
                const spectra::rasterizer::math::Vector3 position = spectra::rasterizer::math::transform_point(transform, to_render_vector(point_cloud.positions.at(point_index)));
                instances.push_back(PointCloudInstance{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .radius = point_cloud.radii.at(point_index),
                    .r = color.x * material.base_color.x + emission_red,
                    .g = color.y * material.base_color.y + emission_green,
                    .b = color.z * material.base_color.z + emission_blue,
                    .a = color.w * material.base_color.w,
                });
            }
            const ObjectKey object_key{SelectableObjectKind::PointCloud, point_cloud.name};
            draw_commands.push_back(PointCloudDrawCommand{
                .objectKey     = object_key,
                .objectId      = this->object_id_for(object_key),
                .firstInstance = first_instance,
                .instanceCount = static_cast<std::uint32_t>(point_cloud.positions.size()),
            });
        }

        frame_point_cloud.drawCommands = std::move(draw_commands);
        if (!instances.empty()) {
            const vk::DeviceSize instance_bytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(PointCloudInstance));
            this->ensure_host_buffer(frame_point_cloud.instanceBuffer, instance_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            std::memcpy(frame_point_cloud.instanceBuffer.mapped, instances.data(), static_cast<std::size_t>(instance_bytes));
        }
        frame_point_cloud.uploadedRevision = scene_revision;
    }

    void Renderer::upload_volume_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.workspace->revision();
        if (frame_volume.uploadedRevision == scene_revision) return;

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        const scene::Scene::VolumeGrid* selected_volume = this->select_render_volume_grid(resolved_frame.volumes);
        if (selected_volume == nullptr) {
            frame_volume.uploadedRevision = scene_revision;
            frame_volume.uploadPending    = false;
            frame_volume.descriptorValid  = false;
            frame_volume.drawCommand      = VolumeDrawCommand{};
            return;
        }
        const scene::Scene::VolumeGrid& volume = *selected_volume;
        const scene::Scene::Material material = this->resolve_material(volume.material_name);
        require_volume_material(material, volume.name);
        if (volume.dimensions[0] == 0 || volume.dimensions[1] == 0 || volume.dimensions[2] == 0) throw std::runtime_error(std::format("Rasterizer volume \"{}\" has zero dimensions", volume.name));
        if (!finite_scene_vector(volume.origin)) throw std::runtime_error(std::format("Rasterizer volume \"{}\" origin must be finite", volume.name));
        if (!finite_scene_vector(volume.voxel_size) || volume.voxel_size.x <= 0.0f || volume.voxel_size.y <= 0.0f || volume.voxel_size.z <= 0.0f) throw std::runtime_error(std::format("Rasterizer volume \"{}\" voxel size must be finite and positive", volume.name));
        const scene::Scene::VolumeChannel& density_channel = this->require_volume_channel(volume, "density");
        const scene::Scene::VolumeChannel* temperature_channel = this->find_volume_channel(volume, "temperature");
        if (density_channel.dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel dimensions must match the volume dimensions", volume.name));
        if (temperature_channel != nullptr && temperature_channel->dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" temperature channel dimensions must match the volume dimensions", volume.name));

        const vk::Extent3D image_extent{volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]};
        if (!*frame_volume.densityImage.image || frame_volume.densityImage.extent != image_extent) {
            this->create_volume_image(frame_volume.densityImage, image_extent);
            this->create_volume_image(frame_volume.temperatureImage, image_extent);
            frame_volume.descriptorValid = false;
        }
        const vk::DeviceSize channel_bytes = static_cast<vk::DeviceSize>(density_channel.values.size() * sizeof(float));
        this->ensure_host_buffer(frame_volume.densityStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
        this->ensure_host_buffer(frame_volume.temperatureStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
        std::memcpy(frame_volume.densityStagingBuffer.mapped, density_channel.values.data(), static_cast<std::size_t>(channel_bytes));
        if (temperature_channel != nullptr) std::memcpy(frame_volume.temperatureStagingBuffer.mapped, temperature_channel->values.data(), static_cast<std::size_t>(channel_bytes));
        else std::memset(frame_volume.temperatureStagingBuffer.mapped, 0, static_cast<std::size_t>(channel_bytes));

        if (!frame_volume.descriptorValid) {
            const vk::DescriptorBufferInfo camera_buffer_info{*this->camera.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData)};
            const vk::DescriptorImageInfo density_image_info{{}, *frame_volume.densityImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo temperature_image_info{{}, *frame_volume.temperatureImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo sampler_info{*this->volume_pass.sampler, {}, vk::ImageLayout::eUndefined};
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &camera_buffer_info, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 1u, 0u, 1u, vk::DescriptorType::eSampledImage, &density_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 2u, 0u, 1u, vk::DescriptorType::eSampledImage, &temperature_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 3u, 0u, 1u, vk::DescriptorType::eSampler, &sampler_info, nullptr, nullptr},
            };
            this->host.device->updateDescriptorSets(descriptor_writes, {});
            frame_volume.descriptorValid = true;
        }
        const ObjectKey object_key{SelectableObjectKind::VolumeGrid, volume.name};
        frame_volume.drawCommand = VolumeDrawCommand{
            .objectKey = object_key,
            .objectId  = this->object_id_for(object_key),
            .volume   = volume,
            .material = material,
        };
        frame_volume.uploadPending    = true;
        frame_volume.uploadedRevision = scene_revision;
    }

    void Renderer::record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume) {
        if (!frame_volume.uploadPending) return;
        if (!*frame_volume.densityImage.image || !*frame_volume.temperatureImage.image) throw std::runtime_error("Spectra rasterizer volume upload is missing GPU images");
        if (!*frame_volume.densityStagingBuffer.buffer || !*frame_volume.temperatureStagingBuffer.buffer) throw std::runtime_error("Spectra rasterizer volume upload is missing staging buffers");
        transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame_volume.densityImage.layout = vk::ImageLayout::eTransferDstOptimal;
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eTransferDstOptimal;

        const vk::BufferImageCopy region{
            0,
            0,
            0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            frame_volume.densityImage.extent,
        };
        const std::array regions{region};
        command_buffer.copyBufferToImage(*frame_volume.densityStagingBuffer.buffer, *frame_volume.densityImage.image, vk::ImageLayout::eTransferDstOptimal, regions);
        command_buffer.copyBufferToImage(*frame_volume.temperatureStagingBuffer.buffer, *frame_volume.temperatureImage.image, vk::ImageLayout::eTransferDstOptimal, regions);

        transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame_volume.densityImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        frame_volume.uploadPending = false;
    }

    std::string Renderer::active_scene_id() const {
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        if (scene->name.empty()) throw std::runtime_error("Spectra rasterizer scene id must not be empty");
        return scene->name;
    }

    scene::Scene::CameraState Renderer::initial_camera_state_from_scene() const {
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        if (!scene->camera.has_value()) throw std::runtime_error("Spectra rasterizer viewport requires a scene camera");
        const scene::Scene::Camera& camera = *scene->camera;
        return scene::Scene::CameraState{
            .eye                = camera.transform.position,
            .target             = camera.target,
            .up                 = camera.up,
            .vertical_fov_degrees = camera.vertical_fov_degrees,
        };
    }

    scene::Scene::CameraState Renderer::current_viewport_camera_state() const {
        if (!this->viewport.camera_initialized) throw std::runtime_error("Spectra rasterizer viewport camera is not initialized");
        const spectra::rasterizer::math::CameraBasis orbit_basis = spectra::rasterizer::math::orbit_camera_basis(to_render_vector(this->viewport.camera_target), this->viewport.camera_yaw, this->viewport.camera_pitch, this->viewport.camera_distance);
        return scene::Scene::CameraState{
            .eye                = to_scene_vector(orbit_basis.eye),
            .target             = this->viewport.camera_target,
            .up                 = this->viewport.camera_up,
            .vertical_fov_degrees = this->viewport.camera_vertical_fov_degrees,
        };
    }

    void Renderer::ensure_viewport_camera_session() {
        this->scene.camera_workspace->ensure_camera(this->active_scene_id(), this->initial_camera_state_from_scene());
    }

    void Renderer::synchronize_viewport_camera() {
        const scene::Scene::CameraSnapshot snapshot = this->scene.camera_workspace->snapshot(this->active_scene_id());
        if (this->viewport.camera_initialized && snapshot.revision == this->scene.observed_camera_revision) return;
        this->apply_viewport_camera_state(snapshot);
    }

    void Renderer::apply_viewport_camera_state(const scene::Scene::CameraSnapshot& snapshot) {
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        if (!scene->camera.has_value()) throw std::runtime_error("Spectra rasterizer viewport requires a scene camera");
        const spectra::rasterizer::math::Vector3 eye = to_render_vector(snapshot.state.eye);
        const spectra::rasterizer::math::Vector3 target = to_render_vector(snapshot.state.target);
        const spectra::rasterizer::math::Vector3 offset = eye - target;
        const float distance = spectra::rasterizer::math::length(offset);
        if (!std::isfinite(distance) || distance <= 0.0f) throw std::runtime_error("Spectra rasterizer scene camera must not be located at its target");
        this->viewport.camera_target = snapshot.state.target;
        this->viewport.camera_up = snapshot.state.up;
        this->viewport.camera_distance = std::max(distance, 0.02f);
        this->viewport.camera_yaw = std::atan2(offset.x, offset.z);
        this->viewport.camera_pitch = clamp_viewport_pitch(std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f)));
        this->viewport.camera_vertical_fov_degrees = snapshot.state.vertical_fov_degrees;
        this->viewport.camera_near_plane = scene->camera->near_plane;
        this->viewport.camera_far_plane = scene->camera->far_plane;
        this->viewport.camera_initialized = true;
        this->scene.observed_camera_revision = snapshot.revision;
    }

    void Renderer::commit_viewport_camera() {
        const scene::Scene::CameraSnapshot snapshot = this->scene.camera_workspace->commit(this->active_scene_id(), this->current_viewport_camera_state());
        this->scene.observed_camera_revision = snapshot.revision;
    }

    void Renderer::reset_viewport_camera_from_scene() {
        const scene::Scene::CameraSnapshot snapshot = this->scene.camera_workspace->commit(this->active_scene_id(), this->initial_camera_state_from_scene());
        this->apply_viewport_camera_state(snapshot);
    }

    Renderer::SceneBounds Renderer::scene_bounds() const {
        SceneBounds bounds{};
        const auto include_point = [&bounds](const scene::Vector3& point) {
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        };
        const auto include_transformed_point = [&include_point](const scene::Vector3& point, const scene::Transform& transform) {
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
            include_point(to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point))));
        };

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            for (const scene::Vector3& position : mesh.positions) include_transformed_point(position, mesh.transform);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            for (std::size_t index = 0; index < point_cloud.positions.size(); ++index) {
                const scene::Vector3 center = to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point_cloud.positions.at(index))));
                const float radius = index < point_cloud.radii.size() ? std::max(0.0f, point_cloud.radii.at(index)) : 0.0f;
                include_point(scene::Vector3{center.x - radius, center.y - radius, center.z - radius});
                include_point(scene::Vector3{center.x + radius, center.y + radius, center.z + radius});
            }
        }
        for (const scene::Scene::VolumeGrid& volume : resolved_frame.volumes) {
            include_point(volume.origin);
            include_point(scene::Vector3{
                volume.origin.x + volume.voxel_size.x * static_cast<float>(volume.dimensions[0]),
                volume.origin.y + volume.voxel_size.y * static_cast<float>(volume.dimensions[1]),
                volume.origin.z + volume.voxel_size.z * static_cast<float>(volume.dimensions[2]),
            });
        }
        return bounds;
    }

    Renderer::SceneBounds Renderer::selected_scene_bounds() const {
        SceneBounds bounds{};
        const auto include_point = [&bounds](const scene::Vector3& point) {
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        };
        const auto include_transformed_point = [&include_point](const scene::Vector3& point, const scene::Transform& transform) {
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
            include_point(to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point))));
        };

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.workspace->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::Mesh, mesh.name})) continue;
            for (const scene::Vector3& position : mesh.positions) include_transformed_point(position, mesh.transform);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::PointCloud, point_cloud.name})) continue;
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            for (std::size_t index = 0; index < point_cloud.positions.size(); ++index) {
                const scene::Vector3 center = to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point_cloud.positions.at(index))));
                const float radius = index < point_cloud.radii.size() ? std::max(0.0f, point_cloud.radii.at(index)) : 0.0f;
                include_point(scene::Vector3{center.x - radius, center.y - radius, center.z - radius});
                include_point(scene::Vector3{center.x + radius, center.y + radius, center.z + radius});
            }
        }
        for (const scene::Scene::VolumeGrid& volume : resolved_frame.volumes) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::VolumeGrid, volume.name})) continue;
            include_point(volume.origin);
            include_point(scene::Vector3{
                volume.origin.x + volume.voxel_size.x * static_cast<float>(volume.dimensions[0]),
                volume.origin.y + volume.voxel_size.y * static_cast<float>(volume.dimensions[1]),
                volume.origin.z + volume.voxel_size.z * static_cast<float>(volume.dimensions[2]),
            });
        }
        return bounds;
    }

    void Renderer::frame_viewport_scene() {
        const SceneBounds bounds = this->scene_bounds();
        if (!bounds.valid) {
            this->reset_viewport_camera_from_scene();
            return;
        }
        const scene::Vector3 center{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f,
        };
        const spectra::rasterizer::math::Vector3 diagonal = to_render_vector(bounds.maximum) - to_render_vector(bounds.minimum);
        const float radius = std::max(0.1f, spectra::rasterizer::math::length(diagonal) * 0.5f);
        this->viewport.camera_target = center;
        this->viewport.camera_distance = std::clamp(radius * 2.6f, 0.02f, 1000000.0f);
        this->viewport.camera_far_plane = std::max(this->viewport.camera_far_plane, this->viewport.camera_distance + radius * 6.0f);
        this->viewport.camera_initialized = true;
        this->commit_viewport_camera();
    }

    void Renderer::frame_selected_objects() {
        const SceneBounds bounds = this->selected_scene_bounds();
        if (!bounds.valid) {
            this->frame_viewport_scene();
            return;
        }
        const scene::Vector3 center{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f,
        };
        const spectra::rasterizer::math::Vector3 diagonal = to_render_vector(bounds.maximum) - to_render_vector(bounds.minimum);
        const float radius = std::max(0.1f, spectra::rasterizer::math::length(diagonal) * 0.5f);
        this->viewport.camera_target = center;
        this->viewport.camera_distance = std::clamp(radius * 2.3f, 0.02f, 1000000.0f);
        this->viewport.camera_far_plane = std::max(this->viewport.camera_far_plane, this->viewport.camera_distance + radius * 6.0f);
        this->viewport.camera_initialized = true;
        this->commit_viewport_camera();
    }

    void Renderer::set_viewport_axis_view(const scene::Vector3 direction) {
        const spectra::rasterizer::math::Vector3 normalized = spectra::rasterizer::math::normalize(to_render_vector(direction));
        this->viewport.camera_yaw = std::atan2(normalized.x, normalized.z);
        this->viewport.camera_pitch = clamp_viewport_pitch(std::asin(std::clamp(normalized.y, -1.0f, 1.0f)));
        this->viewport.camera_up = std::abs(normalized.y) > 0.9f ? scene::Vector3{0.0f, 0.0f, -1.0f} : scene::Vector3{0.0f, 1.0f, 0.0f};
        this->viewport.camera_initialized = true;
        this->commit_viewport_camera();
    }

    void Renderer::orbit_viewport_camera(const ViewportDragDelta delta) {
        constexpr float orbit_radians_per_pixel = 0.006f;
        this->viewport.camera_yaw -= delta.x * orbit_radians_per_pixel;
        this->viewport.camera_pitch = clamp_viewport_pitch(this->viewport.camera_pitch + delta.y * orbit_radians_per_pixel);
        this->commit_viewport_camera();
    }

    void Renderer::pan_viewport_camera(const ViewportDragDelta delta, const float viewport_height) {
        if (!std::isfinite(viewport_height) || viewport_height <= 0.0f) throw std::runtime_error("Rasterizer viewport pan requires a positive viewport height");
        const spectra::rasterizer::math::CameraBasis orbit_basis = spectra::rasterizer::math::orbit_camera_basis(to_render_vector(this->viewport.camera_target), this->viewport.camera_yaw, this->viewport.camera_pitch, this->viewport.camera_distance);
        const spectra::rasterizer::math::CameraBasis basis = spectra::rasterizer::math::camera_basis(orbit_basis.eye, orbit_basis.target, to_render_vector(this->viewport.camera_up));
        const float pan_scale = spectra::rasterizer::math::perspective_pan_scale(this->viewport.camera_distance, this->viewport.camera_vertical_fov_degrees, viewport_height);
        spectra::rasterizer::math::Vector3 target = to_render_vector(this->viewport.camera_target);
        target += basis.side * (-delta.x * pan_scale);
        target += basis.up * (delta.y * pan_scale);
        this->viewport.camera_target = to_scene_vector(target);
        this->commit_viewport_camera();
    }

    void Renderer::zoom_viewport_camera(const float steps) {
        if (!std::isfinite(steps)) throw std::runtime_error("Rasterizer viewport zoom input must be finite");
        const float zoom_factor = std::pow(0.88f, steps);
        this->viewport.camera_distance = std::clamp(this->viewport.camera_distance * zoom_factor, 0.02f, 1000000.0f);
        this->commit_viewport_camera();
    }

    Renderer::CameraUniformData Renderer::make_viewport_camera_uniform() const {
        if (!this->viewport.camera_initialized) throw std::runtime_error("Spectra rasterizer viewport camera is not initialized");
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer camera uniform without a viewport extent");
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        const float aspect = static_cast<float>(this->viewport.extent.width) / static_cast<float>(this->viewport.extent.height);
        const spectra::rasterizer::math::CameraBasis orbit_basis = spectra::rasterizer::math::orbit_camera_basis(to_render_vector(this->viewport.camera_target), this->viewport.camera_yaw, this->viewport.camera_pitch, this->viewport.camera_distance);
        const spectra::rasterizer::math::CameraBasis basis = spectra::rasterizer::math::camera_basis(orbit_basis.eye, orbit_basis.target, to_render_vector(this->viewport.camera_up));
        const float far_plane = std::max(this->viewport.camera_far_plane, this->viewport.camera_distance * 4.0f);
        const spectra::rasterizer::math::Matrix4 projection = spectra::rasterizer::math::perspective_matrix(this->viewport.camera_vertical_fov_degrees, aspect, this->viewport.camera_near_plane, far_plane);
        const spectra::rasterizer::math::Matrix4 inverse_projection = spectra::rasterizer::math::inverse_perspective_matrix(this->viewport.camera_vertical_fov_degrees, aspect, this->viewport.camera_near_plane, far_plane);
        const spectra::rasterizer::math::Matrix4 view_projection = spectra::rasterizer::math::look_at_matrix(basis) * projection;
        const spectra::rasterizer::math::Matrix4 inverse_view_projection = inverse_projection * spectra::rasterizer::math::inverse_look_at_matrix(basis);
        const LightUniformData light = scene_light_uniform(*scene);
        const spectra::rasterizer::math::Vector3 normalized_light_direction = spectra::rasterizer::math::normalize(light.direction);
        return CameraUniformData{
            .viewProjection        = view_projection.values,
            .inverseViewProjection = inverse_view_projection.values,
            .cameraPosition        = {basis.eye.x, basis.eye.y, basis.eye.z, 0.0f},
            .lightDirection        = {normalized_light_direction.x, normalized_light_direction.y, normalized_light_direction.z, 0.0f},
            .lightColorIntensity   = {light.color.x, light.color.y, light.color.z, light.intensity},
            .cameraRight           = {basis.side.x, basis.side.y, basis.side.z, 0.0f},
            .cameraUp              = {basis.up.x, basis.up.y, basis.up.z, 0.0f},
            .viewport              = {static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, this->viewport.camera_distance},
        };
    }

    void Renderer::update_camera_uniform(const std::uint32_t frame_index) {
        if (frame_index >= this->camera.uniform_buffers.size()) throw std::runtime_error("Spectra rasterizer uniform frame index is out of range");
        if (!this->viewport.camera_initialized) this->synchronize_viewport_camera();
        const CameraUniformData camera_uniform = this->make_viewport_camera_uniform();
        std::memcpy(this->camera.uniform_buffers.at(frame_index).mapped, &camera_uniform, sizeof(camera_uniform));
    }

    FrameResult Renderer::begin_frame(HostView host, const FrameContext& frame) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        if (frame.frame_index >= this->host.frame_count) throw std::runtime_error("Spectra rasterizer frame index is out of range");
        this->consume_completed_screenshot(frame.frame_index);
        this->consume_completed_selection_pick(frame.frame_index);
        this->lifecycle.active_frame_index = frame.frame_index;
        FrameResult result{};
        this->ensure_viewport_resources();
        this->ensure_camera_resources();
        this->ensure_mesh_resources();
        this->ensure_viewport_grid_resources();
        this->ensure_point_cloud_resources();
        this->ensure_volume_resources();
        this->ensure_selection_resources();
        this->rebuild_selection_registry_if_needed();
        this->synchronize_viewport_camera();
        validate_material_library(*this->scene.workspace->document());
        this->upload_scene_resources(frame.frame_index);
        this->upload_point_cloud_resources(frame.frame_index);
        this->upload_volume_resources(frame.frame_index);
        this->update_camera_uniform(frame.frame_index);
        result.window_detail = this->window_detail();
        return result;
    }

    void Renderer::record_mesh_pass(const vk::raii::CommandBuffer& command_buffer) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            if (draw_command.material.alpha_mode == scene::Scene::MaterialAlphaMode::Blend) continue;
            const DrawPushConstantsData push_constants = make_draw_push_constants(draw_command.transform, draw_command.material);
            command_buffer.pushConstants(*this->mesh_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_transparent_mesh_pass(const vk::raii::CommandBuffer& command_buffer) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        struct TransparentMeshSortItem {
            const RenderDrawCommand* command{};
            float distanceSquared{};
        };
        std::vector<TransparentMeshSortItem> draw_commands{};
        draw_commands.reserve(frame_scene.drawCommands.size());
        const scene::Scene::CameraState camera_state = this->current_viewport_camera_state();
        const spectra::rasterizer::math::Vector3 camera_position = to_render_vector(camera_state.eye);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            if (draw_command.material.alpha_mode != scene::Scene::MaterialAlphaMode::Blend) continue;
            const spectra::rasterizer::math::Vector3 sort_point = spectra::rasterizer::math::transform_point(spectra::rasterizer::math::transform_matrix(to_render_transform(draw_command.transform)), to_render_vector(draw_command.sortPoint));
            const spectra::rasterizer::math::Vector3 delta = sort_point - camera_position;
            draw_commands.push_back(TransparentMeshSortItem{
                .command = &draw_command,
                .distanceSquared = spectra::rasterizer::math::dot(delta, delta),
            });
        }
        if (draw_commands.empty()) return;
        std::ranges::sort(draw_commands, [](const TransparentMeshSortItem& left, const TransparentMeshSortItem& right) {
            return left.distanceSquared > right.distanceSquared;
        });

        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.transparent_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const TransparentMeshSortItem& item : draw_commands) {
            const RenderDrawCommand& draw_command = *item.command;
            const DrawPushConstantsData push_constants = make_draw_push_constants(draw_command.transform, draw_command.material);
            command_buffer.pushConstants(*this->mesh_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_viewport_grid_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->viewport.grid_visible) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->viewport_grid_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_grid_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        command_buffer.draw(3u, 1u, 0u, 0u);
    }

    void Renderer::record_volume_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index);
        if (!frame_volume.descriptorValid || frame_volume.drawCommand.volume.name.empty()) return;
        const scene::Scene::VolumeGrid& volume = frame_volume.drawCommand.volume;
        const scene::Scene::Material& material = frame_volume.drawCommand.material;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline_layout, 0u, *this->volume_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const VolumePushConstantsData push_constants{
            .originDensityScale = {volume.origin.x, volume.origin.y, volume.origin.z, material.volume_density_scale},
            .extentStepScale    = {volume.voxel_size.x * static_cast<float>(volume.dimensions[0]), volume.voxel_size.y * static_cast<float>(volume.dimensions[1]), volume.voxel_size.z * static_cast<float>(volume.dimensions[2]), 1.0f},
            .base_color          = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .emission           = {material.emission_color.x * material.emission_strength, material.emission_color.y * material.emission_strength, material.emission_color.z * material.emission_strength, 0.0f},
            .material           = {material.volume_temperature_scale, 0.0f, 0.0f, 0.0f},
        };
        command_buffer.pushConstants(*this->volume_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(36u, 1u, 0u, 0u);
    }

    void Renderer::record_point_cloud_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->point_cloud_pass.frame_point_clouds.size()) throw std::runtime_error("Spectra rasterizer active point cloud frame index is out of range");
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(this->lifecycle.active_frame_index);
        if (frame_point_cloud.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->point_cloud_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->point_cloud_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_point_cloud.instanceBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        for (const PointCloudDrawCommand& draw_command : frame_point_cloud.drawCommands) command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
    }

    void Renderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->viewport.image) return;
        if (this->lifecycle.active_frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer active frame index is out of range");
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active frame volume index is out of range");
        this->record_pending_volume_upload(command_buffer, this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index));

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->viewport.depth_image, vk::ImageAspectFlagBits::eDepth, this->viewport.depth_layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->viewport.depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.035f, 0.038f, 0.043f, 1.0f};
        const vk::ClearValue clear_value{vk::ClearColorValue{clear_color}};
        const vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->viewport.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            clear_value,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->viewport.depth_view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear_value,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_pass(command_buffer);
        this->record_viewport_grid_pass(command_buffer);
        this->record_volume_pass(command_buffer);
        this->record_point_cloud_pass(command_buffer);
        this->record_transparent_mesh_pass(command_buffer);
        command_buffer.endRendering();

        this->record_selection_visuals(command_buffer);
        this->record_selection_pick_pass(command_buffer);

        if (this->viewport.screenshot_requested)
            this->record_viewport_screenshot_copy(command_buffer);
        else {
            transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            this->viewport.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
    }

    void Renderer::request_selection_pick(const std::uint32_t x, const std::uint32_t y, const bool select, const bool additive) {
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot pick a Spectra rasterizer object before viewport resources exist");
        if (x >= this->viewport.extent.width || y >= this->viewport.extent.height) throw std::runtime_error("Spectra rasterizer picking coordinate is outside the viewport");
        this->selection.requested_x        = x;
        this->selection.requested_y        = y;
        this->selection.requested_select   = select;
        this->selection.requested_additive = additive;
        this->selection.pick_requested     = true;
    }

    void Renderer::record_mesh_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.mesh_picking_pipeline : *this->selection.mesh_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.mesh_picking_pipeline_layout : *this->selection.mesh_mask_pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
            if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) continue;
            const SelectionPushConstantsData push_constants = make_selection_push_constants(draw_command.transform, draw_command.material, color, draw_command.objectId);
            command_buffer.pushConstants(picking ? *this->selection.mesh_picking_pipeline_layout : *this->selection.mesh_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_point_cloud_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(this->lifecycle.active_frame_index);
        if (frame_point_cloud.drawCommands.empty()) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.point_cloud_picking_pipeline : *this->selection.point_cloud_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.point_cloud_picking_pipeline_layout : *this->selection.point_cloud_mask_pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_point_cloud.instanceBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        for (const PointCloudDrawCommand& draw_command : frame_point_cloud.drawCommands) {
            const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
            if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) continue;
            const PointCloudSelectionPushConstantsData push_constants{
                .color    = color,
                .objectId = draw_command.objectId,
            };
            command_buffer.pushConstants(picking ? *this->selection.point_cloud_picking_pipeline_layout : *this->selection.point_cloud_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
        }
    }

    void Renderer::record_volume_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index);
        if (!frame_volume.descriptorValid || frame_volume.drawCommand.volume.name.empty()) return;
        const VolumeDrawCommand& draw_command = frame_volume.drawCommand;
        const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
        if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.volume_picking_pipeline : *this->selection.volume_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.volume_picking_pipeline_layout : *this->selection.volume_mask_pipeline_layout, 0u, *this->volume_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const scene::Scene::VolumeGrid& volume = draw_command.volume;
        const VolumeSelectionPushConstantsData push_constants{
            .originDensityScale = {volume.origin.x, volume.origin.y, volume.origin.z, draw_command.material.volume_density_scale},
            .extentStepScale    = {volume.voxel_size.x * static_cast<float>(volume.dimensions[0]), volume.voxel_size.y * static_cast<float>(volume.dimensions[1]), volume.voxel_size.z * static_cast<float>(volume.dimensions[2]), 1.0f},
            .color              = color,
            .objectId           = draw_command.objectId,
        };
        command_buffer.pushConstants(picking ? *this->selection.volume_picking_pipeline_layout : *this->selection.volume_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(36u, 1u, 0u, 0u);
    }

    void Renderer::record_selection_pick_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->selection.pick_requested || this->selection.pick_pending) return;
        if (!*this->selection.object_id_image.image || !*this->selection.depth_image.image) throw std::runtime_error("Spectra rasterizer selection picking resources are not initialized");
        if (this->lifecycle.active_frame_index >= this->selection.readback_buffers.size()) throw std::runtime_error("Spectra rasterizer selection readback frame index is out of range");
        transition_image_layout(command_buffer, *this->selection.object_id_image.image, vk::ImageAspectFlagBits::eColor, this->selection.object_id_image.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->selection.object_id_image.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->selection.depth_image.image, vk::ImageAspectFlagBits::eDepth, this->selection.depth_image.layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->selection.depth_image.layout = vk::ImageLayout::eDepthAttachmentOptimal;

        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        const vk::ClearValue id_clear{vk::ClearColorValue{std::array<std::uint32_t, 4>{0u, 0u, 0u, 0u}}};
        const vk::ClearValue depth_clear{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo id_attachment{
            *this->selection.object_id_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            id_clear,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->selection.depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &id_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_selection_pass(command_buffer, true);
        this->record_volume_selection_pass(command_buffer, true);
        this->record_point_cloud_selection_pass(command_buffer, true);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, *this->selection.object_id_image.image, vk::ImageAspectFlagBits::eColor, this->selection.object_id_image.layout, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
        this->selection.object_id_image.layout = vk::ImageLayout::eTransferSrcOptimal;
        const std::array<vk::BufferImageCopy, 1> copy_regions{vk::BufferImageCopy{
            0,
            0,
            0,
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{static_cast<std::int32_t>(this->selection.requested_x), static_cast<std::int32_t>(this->selection.requested_y), 0},
            vk::Extent3D{1, 1, 1},
        }};
        command_buffer.copyImageToBuffer(*this->selection.object_id_image.image, vk::ImageLayout::eTransferSrcOptimal, *this->selection.readback_buffers.at(this->lifecycle.active_frame_index).buffer, copy_regions);
        this->selection.pending_frame_index = this->lifecycle.active_frame_index;
        this->selection.pending_select      = this->selection.requested_select;
        this->selection.pending_additive    = this->selection.requested_additive;
        this->selection.pick_pending        = true;
        this->selection.pick_requested      = false;
    }

    void Renderer::record_selection_visuals(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->selection.visuals_visible) return;
        if (this->selection.selected_objects.empty() && !this->selection.hovered_object.has_value()) return;
        if (!*this->selection.mask_image.image || !*this->selection.depth_image.image) throw std::runtime_error("Spectra rasterizer selection visual resources are not initialized");
        transition_image_layout(command_buffer, *this->selection.mask_image.image, vk::ImageAspectFlagBits::eColor, this->selection.mask_image.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->selection.mask_image.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->selection.depth_image.image, vk::ImageAspectFlagBits::eDepth, this->selection.depth_image.layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->selection.depth_image.layout = vk::ImageLayout::eDepthAttachmentOptimal;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        constexpr std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 0.0f};
        const vk::ClearValue mask_clear{vk::ClearColorValue{clear_color}};
        const vk::ClearValue depth_clear{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo mask_attachment{
            *this->selection.mask_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            mask_clear,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->selection.depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &mask_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_selection_pass(command_buffer, false);
        this->record_volume_selection_pass(command_buffer, false);
        this->record_point_cloud_selection_pass(command_buffer, false);
        command_buffer.endRendering();
        transition_image_layout(command_buffer, *this->selection.mask_image.image, vk::ImageAspectFlagBits::eColor, this->selection.mask_image.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->selection.mask_image.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->record_selection_outline_pass(command_buffer);
    }

    void Renderer::record_selection_outline_pass(const vk::raii::CommandBuffer& command_buffer) {
        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;
        const vk::RenderingAttachmentInfo color_attachment{
            *this->viewport.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.beginRendering(rendering_info);
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->selection.outline_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->selection.outline_pipeline_layout, 0u, *this->selection.outline_descriptor_sets.at(0), {});
        const OutlinePushConstantsData push_constants{
            .inverseExtent = {1.0f / static_cast<float>(this->viewport.extent.width), 1.0f / static_cast<float>(this->viewport.extent.height)},
        };
        command_buffer.pushConstants(*this->selection.outline_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(3u, 1u, 0u, 0u);
        command_buffer.endRendering();
    }

    void Renderer::consume_completed_selection_pick(const std::uint32_t frame_index) {
        if (!this->selection.pick_pending || this->selection.pending_frame_index != frame_index) return;
        if (frame_index >= this->selection.readback_buffers.size()) throw std::runtime_error("Spectra rasterizer selection readback frame index is out of range");
        GpuBuffer& readback_buffer = this->selection.readback_buffers.at(frame_index);
        if (readback_buffer.mapped == nullptr) throw std::runtime_error("Spectra rasterizer selection readback buffer is not mapped");
        std::uint32_t object_id{};
        std::memcpy(&object_id, readback_buffer.mapped, sizeof(object_id));
        this->apply_pick_result(object_id, this->selection.pending_select, this->selection.pending_additive);
        this->selection.pick_pending = false;
    }

    void Renderer::request_viewport_screenshot() {
        if (!*this->viewport.image || this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot capture a Spectra rasterizer screenshot before viewport rendering exists");
        if (this->viewport.screenshot_requested || this->viewport.screenshot_pending) throw std::runtime_error("A Spectra rasterizer screenshot is already pending");
        this->viewport.screenshot_path      = next_screenshot_path();
        this->viewport.screenshot_requested = true;
    }

    void Renderer::record_viewport_screenshot_copy(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->viewport.screenshot_requested) return;
        if (this->viewport.screenshot_pending) throw std::runtime_error("Cannot record a Spectra rasterizer screenshot while another screenshot is pending");
        if (!*this->viewport.image || this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot record a Spectra rasterizer screenshot without a viewport image");
        constexpr vk::DeviceSize bytes_per_pixel = sizeof(std::uint16_t) * 4u;
        const vk::DeviceSize required_size       = static_cast<vk::DeviceSize>(this->viewport.extent.width) * static_cast<vk::DeviceSize>(this->viewport.extent.height) * bytes_per_pixel;
        this->ensure_host_buffer(this->viewport.screenshot_buffer, required_size, vk::BufferUsageFlagBits::eTransferDst);

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
        this->viewport.layout = vk::ImageLayout::eTransferSrcOptimal;
        const std::array<vk::BufferImageCopy, 1> copy_regions{vk::BufferImageCopy{
            0,
            0,
            0,
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{this->viewport.extent.width, this->viewport.extent.height, 1},
        }};
        command_buffer.copyImageToBuffer(*this->viewport.image, vk::ImageLayout::eTransferSrcOptimal, *this->viewport.screenshot_buffer.buffer, copy_regions);
        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->viewport.layout                 = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->viewport.screenshot_requested   = false;
        this->viewport.screenshot_pending     = true;
        this->viewport.screenshot_frame_index = this->lifecycle.active_frame_index;
        this->viewport.screenshot_extent      = this->viewport.extent;
    }

    void Renderer::consume_completed_screenshot(const std::uint32_t frame_index) {
        if (!this->viewport.screenshot_pending || this->viewport.screenshot_frame_index != frame_index) return;
        if (this->viewport.screenshot_buffer.mapped == nullptr) throw std::runtime_error("Spectra rasterizer screenshot readback buffer is not mapped");
        if (this->viewport.screenshot_path.empty()) throw std::runtime_error("Spectra rasterizer screenshot output path is empty");
        const std::vector<std::uint8_t> pixels = rgba16_float_to_rgba8(this->viewport.screenshot_buffer.mapped, this->viewport.screenshot_extent);
        write_png_rgba8(this->viewport.screenshot_path, this->viewport.screenshot_extent, pixels);
        this->viewport.screenshot_pending     = false;
        this->viewport.screenshot_frame_index = 0;
        this->viewport.screenshot_extent      = vk::Extent2D{};
        this->viewport.screenshot_path.clear();
    }

    void Renderer::handle_viewport_input(const ViewportImageRect image_rect) {
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};
        this->viewport.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(image_min, image_max);
        if (!this->viewport.hovered) return;
        if (!this->viewport.camera_initialized) this->reset_viewport_camera_from_scene();

        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) this->zoom_viewport_camera(io.MouseWheel);

        const ViewportDragDelta delta{
            .x = io.MouseDelta.x,
            .y = io.MouseDelta.y,
        };
        const ImVec2 mouse_position = io.MousePos;
        bool over_viewport_control = false;
        if (image_rect.width >= 230.0f && image_rect.height >= 54.0f) {
            constexpr float button_size = 30.0f;
            constexpr float gap = 4.0f;
            const ImVec2 toolbar_origin{image_min.x + 12.0f, image_min.y + 12.0f};
            const ImVec2 toolbar_padding{6.0f, 5.0f};
            const ImVec2 toolbar_max{toolbar_origin.x + toolbar_padding.x * 2.0f + button_size * 5.0f + gap * 4.0f, toolbar_origin.y + toolbar_padding.y * 2.0f + button_size};
            over_viewport_control = mouse_position.x >= toolbar_origin.x && mouse_position.x <= toolbar_max.x && mouse_position.y >= toolbar_origin.y && mouse_position.y <= toolbar_max.y;
        }
        if (!over_viewport_control && this->viewport.overlays_visible && image_rect.width >= 130.0f && image_rect.height >= 110.0f) {
            const ImVec2 gizmo_origin{image_min.x + image_size.x - 94.0f, image_min.y + 14.0f};
            const ImVec2 gizmo_max{gizmo_origin.x + 78.0f, gizmo_origin.y + 78.0f};
            over_viewport_control = mouse_position.x >= gizmo_origin.x && mouse_position.x <= gizmo_max.x && mouse_position.y >= gizmo_origin.y && mouse_position.y <= gizmo_max.y;
        }
        const float viewport_height = std::max(1.0f, image_size.y);
        if (io.KeyAlt) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) this->orbit_viewport_camera(delta);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) this->pan_viewport_camera(delta, viewport_height);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) this->zoom_viewport_camera(-delta.y * 0.035f);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            if (io.KeyShift) this->pan_viewport_camera(delta, viewport_height);
            else if (io.KeyCtrl) this->zoom_viewport_camera(-delta.y * 0.035f);
            else this->orbit_viewport_camera(delta);
        }

        if (!io.KeyAlt && !over_viewport_control && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const std::uint32_t pick_x = static_cast<std::uint32_t>(std::clamp(mouse_position.x - image_min.x, 0.0f, std::max(0.0f, image_size.x - 1.0f)));
            const std::uint32_t pick_y = static_cast<std::uint32_t>(std::clamp(mouse_position.y - image_min.y, 0.0f, std::max(0.0f, image_size.y - 1.0f)));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) this->request_selection_pick(pick_x, pick_y, true, io.KeyShift);
            else if (!this->selection.pick_requested && !this->selection.pick_pending) this->request_selection_pick(pick_x, pick_y, false, false);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) this->clear_selection();
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (this->selection.selected_objects.empty()) this->frame_viewport_scene();
            else this->frame_selected_objects();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) this->frame_viewport_scene();
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->set_viewport_axis_view(scene::Vector3{0.0f, 0.0f, 1.0f});
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->set_viewport_axis_view(scene::Vector3{1.0f, 0.0f, 0.0f});
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->set_viewport_axis_view(scene::Vector3{0.0f, 1.0f, 0.0f});
    }

    void Renderer::draw_viewport_toolbar(const ViewportImageRect image_rect) {
        if (image_rect.width < 230.0f || image_rect.height < 54.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_max{image_rect.x + image_rect.width, image_rect.y + image_rect.height};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        constexpr float button_size = 30.0f;
        constexpr float gap = 4.0f;
        const ImVec2 origin{image_min.x + 12.0f, image_min.y + 12.0f};
        const ImVec2 padding{6.0f, 5.0f};
        const ImVec2 background_max{origin.x + padding.x * 2.0f + button_size * 5.0f + gap * 4.0f, origin.y + padding.y * 2.0f + button_size};
        draw_list->AddRectFilled(origin, background_max, IM_COL32(14, 16, 19, 208), 7.0f);
        draw_list->AddRect(origin, background_max, IM_COL32(92, 102, 112, 96), 7.0f);

        ImGui::PushClipRect(image_min, image_max, true);
        ImGui::PushID("SpectraRasterizerViewportToolbar");
        const auto draw_button = [button_size](const char* label, const char* tooltip, const bool active) {
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.16f, 0.28f, 0.34f, 1.0f});
            const bool clicked = ImGui::Button(label, ImVec2{button_size, button_size});
            if (active) ImGui::PopStyleColor();
            draw_tooltip(tooltip);
            return clicked;
        };

        ImGui::SetCursorScreenPos(ImVec2{origin.x + padding.x, origin.y + padding.y});
        if (draw_button(ICON_MS_RESET_FOCUS "##reset_view", "Reset View", false)) this->reset_viewport_camera_from_scene();
        ImGui::SameLine(0.0f, gap);
        if (draw_button(ICON_MS_CENTER_FOCUS_STRONG "##frame_all", "Frame All", false)) this->frame_viewport_scene();
        ImGui::SameLine(0.0f, gap);
        if (draw_button(this->viewport.grid_visible ? ICON_MS_GRID_ON "##grid" : ICON_MS_GRID_OFF "##grid", "Grid", this->viewport.grid_visible)) this->viewport.grid_visible = !this->viewport.grid_visible;
        ImGui::SameLine(0.0f, gap);
        if (draw_button(this->viewport.overlays_visible ? ICON_MS_VISIBILITY "##overlays" : ICON_MS_VISIBILITY_OFF "##overlays", "Overlays", this->viewport.overlays_visible)) this->viewport.overlays_visible = !this->viewport.overlays_visible;
        ImGui::SameLine(0.0f, gap);
        const bool screenshot_busy = this->viewport.screenshot_requested || this->viewport.screenshot_pending;
        if (screenshot_busy) ImGui::BeginDisabled();
        if (draw_button(ICON_MS_SCREENSHOT "##screenshot", screenshot_busy ? "Screenshot Pending" : "Screenshot", false)) this->request_viewport_screenshot();
        if (screenshot_busy) ImGui::EndDisabled();
        if (screenshot_busy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", "Screenshot Pending");
        ImGui::PopID();
        ImGui::PopClipRect();
    }

    void Renderer::draw_orientation_gizmo(const ViewportImageRect image_rect) {
        if (!this->viewport.camera_initialized) return;
        if (image_rect.width < 130.0f || image_rect.height < 110.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        const ImVec2 origin{image_min.x + image_size.x - 94.0f, image_min.y + 14.0f};
        const ImVec2 size{78.0f, 78.0f};
        const ImVec2 center{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, IM_COL32(13, 15, 18, 186), 9.0f);
        draw_list->AddRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, IM_COL32(96, 106, 116, 90), 9.0f);

        struct GizmoAxis {
            const char* id{};
            const char* label{};
            spectra::rasterizer::math::Vector3 axis{};
            scene::Vector3 view_direction{};
            std::uint8_t line_red{};
            std::uint8_t line_green{};
            std::uint8_t line_blue{};
            std::uint8_t text_red{};
            std::uint8_t text_green{};
            std::uint8_t text_blue{};
            ImVec2 endpoint{};
            float depth{};
        };

        const float yaw = -this->viewport.camera_yaw;
        const float pitch = this->viewport.camera_pitch;
        const float yaw_cos = std::cos(yaw);
        const float yaw_sin = std::sin(yaw);
        const float pitch_cos = std::cos(pitch);
        const float pitch_sin = std::sin(pitch);
        constexpr float axis_radius = 25.0f;

        const auto project_axis = [&](const spectra::rasterizer::math::Vector3 axis) {
            const spectra::rasterizer::math::Vector3 pitched{
                axis.x,
                axis.y * pitch_cos - axis.z * pitch_sin,
                axis.y * pitch_sin + axis.z * pitch_cos,
            };
            const spectra::rasterizer::math::Vector3 projected{
                pitched.x * yaw_cos + pitched.z * yaw_sin,
                pitched.y,
                -pitched.x * yaw_sin + pitched.z * yaw_cos,
            };
            return projected;
        };

        std::array<GizmoAxis, 3> axes{{
            GizmoAxis{
                .id             = "x",
                .label          = "X",
                .axis           = spectra::rasterizer::math::Vector3{1.0f, 0.0f, 0.0f},
                .view_direction = scene::Vector3{1.0f, 0.0f, 0.0f},
                .line_red       = 232,
                .line_green     = 94,
                .line_blue      = 82,
                .text_red       = 255,
                .text_green     = 136,
                .text_blue      = 128,
            },
            GizmoAxis{
                .id             = "y",
                .label          = "Y",
                .axis           = spectra::rasterizer::math::Vector3{0.0f, 1.0f, 0.0f},
                .view_direction = scene::Vector3{0.0f, 1.0f, 0.0f},
                .line_red       = 112,
                .line_green     = 202,
                .line_blue      = 124,
                .text_red       = 154,
                .text_green     = 226,
                .text_blue      = 166,
            },
            GizmoAxis{
                .id             = "z",
                .label          = "Z",
                .axis           = spectra::rasterizer::math::Vector3{0.0f, 0.0f, 1.0f},
                .view_direction = scene::Vector3{0.0f, 0.0f, 1.0f},
                .line_red       = 96,
                .line_green     = 152,
                .line_blue      = 238,
                .text_red       = 136,
                .text_green     = 178,
                .text_blue      = 255,
            },
        }};

        for (GizmoAxis& axis : axes) {
            const spectra::rasterizer::math::Vector3 projected = project_axis(axis.axis);
            axis.endpoint = ImVec2{center.x + projected.x * axis_radius, center.y - projected.y * axis_radius};
            axis.depth = projected.z;
        }

        std::ranges::sort(axes, {}, &GizmoAxis::depth);
        for (const GizmoAxis& axis : axes) {
            const int line_alpha = axis.depth < 0.0f ? 150 : 232;
            const int text_alpha = axis.depth < 0.0f ? 190 : 255;
            draw_list->AddLine(center, axis.endpoint, IM_COL32(axis.line_red, axis.line_green, axis.line_blue, line_alpha), 2.0f);
            draw_list->AddCircleFilled(axis.endpoint, 3.0f, IM_COL32(axis.line_red, axis.line_green, axis.line_blue, text_alpha), 12);
            draw_list->AddText(ImVec2{axis.endpoint.x - 4.0f, axis.endpoint.y - 7.0f}, IM_COL32(axis.text_red, axis.text_green, axis.text_blue, text_alpha), axis.label);
        }

        ImGui::PushID("SpectraRasterizerOrientationGizmo");
        for (const GizmoAxis& axis : axes) {
            ImGui::SetCursorScreenPos(ImVec2{axis.endpoint.x - 10.0f, axis.endpoint.y - 10.0f});
            if (ImGui::InvisibleButton(axis.id, ImVec2{20.0f, 20.0f})) this->set_viewport_axis_view(axis.view_direction);
        }
        ImGui::PopID();
    }

    void Renderer::draw_viewport_overlays(const ViewportImageRect image_rect) {
        if (!this->viewport.overlays_visible) return;
        if (image_rect.width <= 1.0f || image_rect.height <= 1.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};

        ImGui::PushClipRect(image_min, image_max, true);
        const scene::Scene::Timeline timeline = this->scene.workspace->timeline();
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        constexpr const char* projection_text = "Perspective";
        const char* scene_mode = scene->timeline_enabled ? timeline_mode_text(timeline.mode) : "Static";
        std::string hud = scene->timeline_enabled ? std::format("{} | frame {} | {:.2f}s | {}x{} | {}", scene_mode, timeline.cursor.frame_index, timeline.cursor.time_seconds, this->viewport.extent.width, this->viewport.extent.height, projection_text) : std::format("{} | {}x{} | {}", scene_mode, this->viewport.extent.width, this->viewport.extent.height, projection_text);
        const ImVec2 hud_padding{10.0f, 7.0f};
        ImVec2 hud_text = ImGui::CalcTextSize(hud.c_str());
        if (hud_text.x + hud_padding.x * 2.0f > image_size.x - 24.0f) {
            hud = scene->timeline_enabled ? std::format("{} | f{} | {}", scene_mode, timeline.cursor.frame_index, projection_text) : std::format("{} | {}", scene_mode, projection_text);
            hud_text = ImGui::CalcTextSize(hud.c_str());
        }
        const ImVec2 hud_min{image_min.x + 12.0f, image_min.y + 58.0f};
        const ImVec2 hud_max{hud_min.x + hud_text.x + hud_padding.x * 2.0f, hud_min.y + hud_text.y + hud_padding.y * 2.0f};
        draw_list->AddRectFilled(hud_min, hud_max, IM_COL32(15, 18, 22, 184), 7.0f);
        draw_list->AddText(ImVec2{hud_min.x + hud_padding.x, hud_min.y + hud_padding.y}, IM_COL32(232, 236, 238, 255), hud.c_str());

        std::string selection_text = this->selection_summary();
        const ImVec2 selection_padding{10.0f, 7.0f};
        ImVec2 selection_size = ImGui::CalcTextSize(selection_text.c_str());
        if (selection_size.x + selection_padding.x * 2.0f > image_size.x - 24.0f) {
            selection_text = this->selection.active_object.has_value() ? std::format("{} selected", this->selection.selected_objects.size()) : "No selection";
            selection_size = ImGui::CalcTextSize(selection_text.c_str());
        }
        const ImVec2 selection_min{image_min.x + 12.0f, hud_max.y + 8.0f};
        const ImVec2 selection_max{selection_min.x + selection_size.x + selection_padding.x * 2.0f, selection_min.y + selection_size.y + selection_padding.y * 2.0f};
        const ImU32 selection_background = this->selection.active_object.has_value() ? IM_COL32(12, 38, 48, 190) : IM_COL32(15, 18, 22, 150);
        draw_list->AddRectFilled(selection_min, selection_max, selection_background, 7.0f);
        draw_list->AddRect(selection_min, selection_max, this->selection.active_object.has_value() ? IM_COL32(60, 198, 232, 112) : IM_COL32(92, 102, 112, 72), 7.0f);
        draw_list->AddText(ImVec2{selection_min.x + selection_padding.x, selection_min.y + selection_padding.y}, IM_COL32(218, 236, 242, 255), selection_text.c_str());

        const std::size_t primitive_count = scene->meshes.size() + scene->point_clouds.size() + scene->volumes.size() + scene->curve_sets.size() + scene->splat_sets.size() + scene->line_sets.size() + scene->vector_fields.size();
        std::string chip = std::format("rev {} | {} prim | dist {:.2f}", this->scene.workspace->revision().value, primitive_count, this->viewport.camera_distance);
        const ImVec2 chip_padding{10.0f, 7.0f};
        ImVec2 chip_text = ImGui::CalcTextSize(chip.c_str());
        if (chip_text.x + chip_padding.x * 2.0f > image_size.x - 24.0f) {
            chip = std::format("rev {} | dist {:.2f}", this->scene.workspace->revision().value, this->viewport.camera_distance);
            chip_text = ImGui::CalcTextSize(chip.c_str());
        }
        const ImVec2 chip_min{image_max.x - chip_text.x - chip_padding.x * 2.0f - 12.0f, image_max.y - chip_text.y - chip_padding.y * 2.0f - 12.0f};
        const ImVec2 chip_max{chip_min.x + chip_text.x + chip_padding.x * 2.0f, chip_min.y + chip_text.y + chip_padding.y * 2.0f};
        draw_list->AddRectFilled(chip_min, chip_max, IM_COL32(15, 18, 22, 164), 7.0f);
        draw_list->AddText(ImVec2{chip_min.x + chip_padding.x, chip_min.y + chip_padding.y}, IM_COL32(206, 214, 220, 255), chip.c_str());

        this->draw_orientation_gizmo(image_rect);
        ImGui::PopClipRect();
    }

    void Renderer::draw_viewport_window() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 1.0f && available.y > 1.0f) this->ui.requested_extent = vk::Extent2D{static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y)};
        const ImVec2 image_min = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(image_min, ImVec2{image_min.x + available.x, image_min.y + available.y}, IM_COL32(9, 11, 14, 255));
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) {
            ImGui::Dummy(available);
            return;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(this->viewport.imgui_descriptor), available, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_size = ImGui::GetItemRectSize();
        const ViewportImageRect image_rect{
            .x      = item_min.x,
            .y      = item_min.y,
            .width  = item_size.x,
            .height = item_size.y,
        };
        this->handle_viewport_input(image_rect);
        this->draw_viewport_overlays(image_rect);
        this->draw_viewport_toolbar(image_rect);
    }

    void Renderer::commit_timeline_from_ui(scene::Scene::Timeline timeline) {
        if (!this->timeline_enabled()) throw std::runtime_error("Static rasterizer scenes do not support timeline edits");
        scene::Scene::Edit edit{};
        edit.replace_timeline(std::move(timeline));
        const scene::Scene::DirtyFlags dirty = this->scene.workspace->commit(std::move(edit));
        if (!scene::Scene::has_dirty_flag(dirty, scene::Scene::DirtyFlags::Timeline)) throw std::runtime_error("Rasterizer timeline UI edit did not mark the timeline dirty");
    }

    bool Renderer::timeline_enabled() const {
        return this->scene.workspace->document()->timeline_enabled;
    }

    bool Renderer::timeline_playing() const {
        if (!this->timeline_enabled()) return false;
        return this->scene.workspace->timeline().playing;
    }

    void Renderer::toggle_timeline_playback() {
        if (!this->timeline_enabled()) throw std::runtime_error("Static rasterizer scenes do not support timeline playback");
        scene::Scene::Timeline timeline = this->scene.workspace->timeline();
        timeline.playing = !timeline.playing;
        this->commit_timeline_from_ui(std::move(timeline));
    }

    void Renderer::request_timeline_reset() {
        if (!this->timeline_enabled()) throw std::runtime_error("Static rasterizer scenes do not support timeline reset");
        scene::Scene::Timeline timeline = this->scene.workspace->timeline();
        ++timeline.reset_request_serial;
        this->commit_timeline_from_ui(std::move(timeline));
    }

    void Renderer::draw_rasterizer_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;
        if (this->ui.control_panel_extension) {
            this->ui.control_panel_extension();
            ImGui::Separator();
        }
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.workspace->document();
        scene::Scene::Timeline timeline = this->scene.workspace->timeline();
        bool timeline_changed = false;

        ImGui::TextUnformatted("Rasterizer");
        ImGui::SameLine();
        ImGui::TextDisabled(scene->timeline_enabled ? "Dynamic Scene" : "Static Scene");
        if (scene->timeline_enabled) {
            ImGui::SeparatorText("Timeline");
            if (ImGui::Button(timeline.playing ? ICON_MS_PAUSE : ICON_MS_PLAY_ARROW)) {
                timeline.playing = !timeline.playing;
                timeline_changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_RESTART_ALT)) {
                ++timeline.reset_request_serial;
                timeline_changed = true;
            }
            ImGui::SameLine();
            const bool live_selected = timeline.mode == scene::Scene::TimelineMode::Live;
            const bool record_selected = timeline.mode == scene::Scene::TimelineMode::Record;
            const bool playback_selected = timeline.mode == scene::Scene::TimelineMode::Playback;
            if (ImGui::RadioButton("Live", live_selected)) {
                timeline.mode = scene::Scene::TimelineMode::Live;
                timeline_changed = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Record", record_selected)) {
                timeline.mode = scene::Scene::TimelineMode::Record;
                timeline_changed = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Playback", playback_selected)) {
                timeline.mode = scene::Scene::TimelineMode::Playback;
                timeline_changed = true;
            }
            bool loop = timeline.loop;
            if (ImGui::Checkbox("Loop", &loop)) {
                timeline.loop = loop;
                timeline_changed = true;
            }
            if (timeline.recorded_frames.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Rasterizer recorded frame count exceeds ImGui slider range");
            int selected_frame = static_cast<int>(timeline.selected_frame_index);
            const int max_frame = timeline.recorded_frames.empty() ? 0 : static_cast<int>(timeline.recorded_frames.size() - 1u);
            if (selected_frame > max_frame) selected_frame = max_frame;
            ImGui::BeginDisabled(timeline.recorded_frames.empty());
            if (ImGui::SliderInt("Playback Frame", &selected_frame, 0, max_frame)) {
                timeline.selected_frame_index = static_cast<std::uint64_t>(selected_frame);
                timeline.mode = scene::Scene::TimelineMode::Playback;
                timeline_changed = true;
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(timeline.recorded_frames.empty());
            if (ImGui::Button("Clear Recording")) {
                ++timeline.clear_recording_request_serial;
                timeline_changed = true;
            }
            ImGui::EndDisabled();
            if (timeline_changed) this->commit_timeline_from_ui(std::move(timeline));
        }

        const scene::Scene::Timeline current_timeline = this->scene.workspace->timeline();
        ImGui::SeparatorText("Status");
        if (ImGui::BeginTable("SpectraRasterizerStatus", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Renderer", "Mesh / Volume / Point Cloud");
            draw_status_row("Scene", scene->title.empty() ? scene->name : scene->title);
            if (scene->timeline_enabled) {
                draw_status_row("Timeline", timeline_mode_text(current_timeline.mode));
                draw_status_row("Frame", std::format("{} / {:.3f}s", current_timeline.cursor.frame_index, current_timeline.cursor.time_seconds));
                draw_status_row("Recorded", std::format("{}", current_timeline.recorded_frames.size()));
            } else {
                draw_status_row("Scene Type", "Static");
            }
            draw_status_row("Viewport", std::format("{} x {}", this->viewport.extent.width, this->viewport.extent.height));
            draw_status_row("Swapchain", std::format("{} x {}", this->host.swapchain_extent.width, this->host.swapchain_extent.height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Selection");
        if (ImGui::BeginTable("SpectraRasterizerSelection", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Selected", std::format("{}", this->selection.selected_objects.size()));
            draw_status_row("Active", this->selection.active_object.has_value() ? this->object_label(*this->selection.active_object) : std::string{"None"});
            draw_status_row("Hover", this->selection.hovered_object.has_value() ? this->object_label(*this->selection.hovered_object) : std::string{"None"});
            ImGui::EndTable();
        }
        ImGui::BeginDisabled(this->selection.selected_objects.empty() && !this->selection.hovered_object.has_value());
        if (ImGui::Button("Clear Selection")) this->clear_selection();
        ImGui::EndDisabled();

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraRasterizerScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_status_row("Materials", std::format("{}", scene->materials.size()));
            draw_status_row("Lights", std::format("{}", scene->lights.size()));
            draw_status_row("Meshes", std::format("{}", scene->meshes.size()));
            draw_status_row("Point Clouds", std::format("{}", scene->point_clouds.size()));
            draw_status_row("Volumes", std::format("{}", scene->volumes.size()));
            draw_status_row("Curves", std::format("{}", scene->curve_sets.size()));
            draw_status_row("Splats", std::format("{}", scene->splat_sets.size()));
            draw_status_row("Line Sets", std::format("{}", scene->line_sets.size()));
            draw_status_row("Vector Fields", std::format("{}", scene->vector_fields.size()));
            ImGui::EndTable();
        }
    }
} // namespace spectra::rasterizer
