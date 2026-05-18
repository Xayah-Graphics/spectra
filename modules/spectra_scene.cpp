module;
#include <cstring>
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

module spectra_scene;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct SpectraSceneSurfaceShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 4> light_direction{};
    };

    struct SpectraSceneOverlayShaderParameters {
        std::array<float, 16> model_view_projection{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<float, 4> color{};
    };

    [[nodiscard]] std::array<float, 16> identity_matrix() {
        return {
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> translation_matrix(const std::array<float, 3>& value) {
        std::array<float, 16> result = identity_matrix();
        result[12]                   = value[0];
        result[13]                   = value[1];
        result[14]                   = value[2];
        return result;
    }

    [[nodiscard]] std::array<float, 3> add_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
    }

    [[nodiscard]] std::array<float, 3> subtract_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    }

    [[nodiscard]] std::array<float, 3> multiply_vector(const std::array<float, 3>& value, const float scale) {
        return {value[0] * scale, value[1] * scale, value[2] * scale};
    }

    [[nodiscard]] float dot_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
    }

    [[nodiscard]] std::array<float, 3> cross_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0],
        };
    }

    [[nodiscard]] float length_vector(const std::array<float, 3>& value) {
        return std::sqrt(dot_vector(value, value));
    }

    [[nodiscard]] std::array<float, 3> normalize_vector(const std::array<float, 3>& value) {
        const float length = length_vector(value);
        if (length <= 0.000001f) throw std::runtime_error("Cannot normalize a zero-length vector");
        return multiply_vector(value, 1.0f / length);
    }

    [[nodiscard]] std::array<float, 3> transform_direction(const std::array<float, 16>& matrix, const std::array<float, 3>& value) {
        return {
            matrix[0] * value[0] + matrix[4] * value[1] + matrix[8] * value[2],
            matrix[1] * value[0] + matrix[5] * value[1] + matrix[9] * value[2],
            matrix[2] * value[0] + matrix[6] * value[1] + matrix[10] * value[2],
        };
    }

    void expand_bounds(xayah::BoundingBoxBounds& bounds, const xayah::BoundingBoxBounds& next) {
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            bounds.minimum[axis] = std::min(bounds.minimum[axis], next.minimum[axis]);
            bounds.maximum[axis] = std::max(bounds.maximum[axis], next.maximum[axis]);
        }
    }

    [[nodiscard]] xayah::BoundingBoxBounds transformed_bounds(const std::array<float, 16>& transform, const xayah::BoundingBoxBounds& local) {
        bool initialized{};
        xayah::BoundingBoxBounds result{};
        for (std::uint32_t corner = 0; corner < 8; ++corner) {
            const std::array<float, 3> point{
                (corner & 1u) != 0u ? local.maximum[0] : local.minimum[0],
                (corner & 2u) != 0u ? local.maximum[1] : local.minimum[1],
                (corner & 4u) != 0u ? local.maximum[2] : local.minimum[2],
            };
            const std::array<float, 3> transformed = xayah::transform_point(transform, point);
            if (!initialized) {
                result.minimum = transformed;
                result.maximum = transformed;
                initialized    = true;
            } else {
                for (std::uint32_t axis = 0; axis < 3; ++axis) {
                    result.minimum[axis] = std::min(result.minimum[axis], transformed[axis]);
                    result.maximum[axis] = std::max(result.maximum[axis], transformed[axis]);
                }
            }
        }
        return result;
    }

    [[nodiscard]] const char* entity_kind_label(const xayah::SpectraSceneEntityKind kind) {
        if (kind == xayah::SpectraSceneEntityKind::camera) return "Camera";
        if (kind == xayah::SpectraSceneEntityKind::geometry) return "Geometry";
        if (kind == xayah::SpectraSceneEntityKind::material) return "Material";
        if (kind == xayah::SpectraSceneEntityKind::texture) return "Texture";
        if (kind == xayah::SpectraSceneEntityKind::light) return "Light";
        if (kind == xayah::SpectraSceneEntityKind::render_setting) return "Render Setting";
        if (kind == xayah::SpectraSceneEntityKind::instance) return "Instance";
        throw std::runtime_error("Unknown SpectraScene entity kind");
    }

    [[nodiscard]] const char* geometry_kind_label(const xayah::SpectraGeometryKind kind) {
        if (kind == xayah::SpectraGeometryKind::triangle_mesh) return "Triangle Mesh";
        if (kind == xayah::SpectraGeometryKind::sphere) return "Sphere";
        if (kind == xayah::SpectraGeometryKind::disk) return "Disk";
        throw std::runtime_error("Unknown SpectraScene geometry kind");
    }

    [[nodiscard]] const char* material_kind_label(const xayah::SpectraMaterialKind kind) {
        if (kind == xayah::SpectraMaterialKind::diffuse) return "Diffuse";
        if (kind == xayah::SpectraMaterialKind::conductor) return "Conductor";
        if (kind == xayah::SpectraMaterialKind::dielectric) return "Dielectric";
        throw std::runtime_error("Unknown SpectraScene material kind");
    }

    [[nodiscard]] const char* light_kind_label(const xayah::SpectraLightKind kind) {
        if (kind == xayah::SpectraLightKind::area) return "Area";
        if (kind == xayah::SpectraLightKind::infinite) return "Infinite";
        throw std::runtime_error("Unknown SpectraScene light kind");
    }

    [[nodiscard]] std::string scene_entity_combo_label(const xayah::SpectraSceneEntity& entity) {
        if (entity.detail.empty()) return std::format("{} #{}", entity.name, entity.id);
        return std::format("{}  {}  #{}", entity.name, entity.detail, entity.id);
    }

    void append_preview_triangle(std::vector<xayah::SpectraSceneVertex>& vertices, const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& n0, const std::array<float, 3>& n1, const std::array<float, 3>& n2, const std::array<float, 3>& color) {
        vertices.push_back({p0, n0, color});
        vertices.push_back({p1, n1, color});
        vertices.push_back({p2, n2, color});
    }

    [[nodiscard]] std::array<float, 3> sphere_point(const float radius, const float theta, const float phi) {
        const float sin_theta = std::sin(theta);
        return {radius * sin_theta * std::cos(phi), radius * std::cos(theta), radius * sin_theta * std::sin(phi)};
    }

    void append_sphere_preview(std::vector<xayah::SpectraSceneVertex>& vertices, const std::array<float, 16>& transform, const float radius, const std::array<float, 3>& color) {
        if (radius <= 0.0f) throw std::runtime_error("SpectraScene sphere radius must be positive");
        constexpr float pi = 3.14159265358979323846f;
        constexpr std::uint32_t segment_count = 48;
        constexpr std::uint32_t stack_count   = 24;
        vertices.reserve(vertices.size() + static_cast<std::size_t>(segment_count) * stack_count * 6u);
        for (std::uint32_t stack = 0; stack < stack_count; ++stack) {
            const float theta0 = pi * static_cast<float>(stack) / static_cast<float>(stack_count);
            const float theta1 = pi * static_cast<float>(stack + 1u) / static_cast<float>(stack_count);
            for (std::uint32_t segment = 0; segment < segment_count; ++segment) {
                const float phi0 = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segment_count);
                const float phi1 = 2.0f * pi * static_cast<float>(segment + 1u) / static_cast<float>(segment_count);
                const std::array<float, 3> v00 = sphere_point(radius, theta0, phi0);
                const std::array<float, 3> v10 = sphere_point(radius, theta0, phi1);
                const std::array<float, 3> v01 = sphere_point(radius, theta1, phi0);
                const std::array<float, 3> v11 = sphere_point(radius, theta1, phi1);
                append_preview_triangle(
                    vertices,
                    xayah::transform_point(transform, v00),
                    xayah::transform_point(transform, v01),
                    xayah::transform_point(transform, v11),
                    normalize_vector(transform_direction(transform, normalize_vector(v00))),
                    normalize_vector(transform_direction(transform, normalize_vector(v01))),
                    normalize_vector(transform_direction(transform, normalize_vector(v11))),
                    color);
                append_preview_triangle(
                    vertices,
                    xayah::transform_point(transform, v00),
                    xayah::transform_point(transform, v11),
                    xayah::transform_point(transform, v10),
                    normalize_vector(transform_direction(transform, normalize_vector(v00))),
                    normalize_vector(transform_direction(transform, normalize_vector(v11))),
                    normalize_vector(transform_direction(transform, normalize_vector(v10))),
                    color);
            }
        }
    }

    void append_disk_preview(std::vector<xayah::SpectraSceneVertex>& vertices, const std::array<float, 16>& transform, const float radius, const float height, const std::array<float, 3>& color) {
        if (radius <= 0.0f) throw std::runtime_error("SpectraScene disk radius must be positive");
        constexpr float pi = 3.14159265358979323846f;
        constexpr std::uint32_t segment_count = 96;
        const std::array<float, 3> center{0.0f, height, 0.0f};
        const std::array<float, 3> normal = normalize_vector(transform_direction(transform, {0.0f, 1.0f, 0.0f}));
        for (std::uint32_t segment = 0; segment < segment_count; ++segment) {
            const float phi0 = 2.0f * pi * static_cast<float>(segment) / static_cast<float>(segment_count);
            const float phi1 = 2.0f * pi * static_cast<float>(segment + 1u) / static_cast<float>(segment_count);
            const std::array<float, 3> p0{radius * std::cos(phi0), height, radius * std::sin(phi0)};
            const std::array<float, 3> p1{radius * std::cos(phi1), height, radius * std::sin(phi1)};
            append_preview_triangle(vertices, xayah::transform_point(transform, center), xayah::transform_point(transform, p0), xayah::transform_point(transform, p1), normal, normal, normal, color);
        }
    }

    [[nodiscard]] std::array<float, 3> material_preview_color(const std::vector<xayah::SpectraSceneMaterial>& materials, const std::uint64_t material_id) {
        for (const xayah::SpectraSceneMaterial& material : materials) {
            if (material.entity_id == material_id) return material.base_color;
        }
        return {0.72f, 0.72f, 0.72f};
    }

    [[nodiscard]] std::array<float, 16> render_camera_transform(const xayah::CameraState& camera) {
        const std::array<float, 3> forward = normalize_vector(subtract_vector(camera.center, camera.eye));
        const std::array<float, 3> right   = normalize_vector(cross_vector(forward, camera.up));
        const std::array<float, 3> up      = normalize_vector(cross_vector(right, forward));
        return {
            right[0],
            right[1],
            right[2],
            0.0f,
            up[0],
            up[1],
            up[2],
            0.0f,
            forward[0],
            forward[1],
            forward[2],
            0.0f,
            camera.eye[0],
            camera.eye[1],
            camera.eye[2],
            1.0f,
        };
    }
} // namespace

namespace xayah {
    SpectraScenePreviewRenderer::SpectraScenePreviewRenderer() = default;

    SpectraScenePreviewRenderer::~SpectraScenePreviewRenderer() noexcept = default;

    void SpectraScenePreviewRenderer::create(const RenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("SpectraScene preview renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create SpectraScene preview renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined || context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create SpectraScene preview renderer without swapchain formats");
        if (context.frame_count == 0) throw std::runtime_error("Cannot create SpectraScene preview renderer without frames in flight");

        const vk::Format stencil_format = static_cast<bool>(context.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? context.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &context.color_format;
        rendering_create_info.depthAttachmentFormat   = context.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        {
            const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "spectra_preview.vert.spv");
            const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "spectra_preview.frag.spv");
            const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
            const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
            const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
            const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };

            constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(SpectraSceneSurfaceShaderParameters)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
            this->surface_pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

            constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(SpectraSceneVertex), vk::VertexInputRate::eVertex};
            constexpr std::array vertex_attributes{
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 3>))},
                vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 3>) * 2u)},
            };
            const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
            constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
            constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.blendEnable    = VK_FALSE;
            color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.pNext               = &rendering_create_info;
            pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
            pipeline_create_info.pStages             = shader_stages.data();
            pipeline_create_info.pVertexInputState   = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState      = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState   = &multisample_state;
            pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
            pipeline_create_info.pColorBlendState    = &color_blend_state;
            pipeline_create_info.pDynamicState       = &dynamic_state;
            pipeline_create_info.layout              = *this->surface_pipeline_layout;
            this->surface_pipeline                   = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
        }

        {
            const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.vert.spv");
            const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.frag.spv");
            const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
            const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
            const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
            const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };

            constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(SpectraSceneOverlayShaderParameters)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
            this->overlay_pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

            constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
            constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
            constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.blendEnable         = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
            color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
            color_blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.pNext               = &rendering_create_info;
            pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
            pipeline_create_info.pStages             = shader_stages.data();
            pipeline_create_info.pVertexInputState   = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState      = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState   = &multisample_state;
            pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
            pipeline_create_info.pColorBlendState    = &color_blend_state;
            pipeline_create_info.pDynamicState       = &dynamic_state;
            pipeline_create_info.layout              = *this->overlay_pipeline_layout;
            this->overlay_pipeline                   = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
        }

        this->frame_resources.resize(context.frame_count);
    }

    void SpectraScenePreviewRenderer::destroy() noexcept {
        this->frame_resources.clear();
        this->overlay_pipeline        = nullptr;
        this->overlay_pipeline_layout = nullptr;
        this->surface_pipeline        = nullptr;
        this->surface_pipeline_layout = nullptr;
    }

    void SpectraScenePreviewRenderer::render(const RenderFrameContext& context, const std::span<const SpectraSceneVertex> vertices, const std::span<const SpectraScenePreviewOverlay> overlays) {
        if (context.physical_device == nullptr || context.device == nullptr || context.command_buffer == nullptr) throw std::runtime_error("SpectraScene preview render context is incomplete");
        if (context.frame_index >= context.frame_count || context.frame_count != this->frame_resources.size()) throw std::runtime_error("SpectraScene preview frame index is outside resource range");
        if (!*this->surface_pipeline_layout || !*this->surface_pipeline || !*this->overlay_pipeline_layout || !*this->overlay_pipeline) throw std::runtime_error("SpectraScene preview renderer is not initialized");
        if (!vertices.empty()) {
            if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("SpectraScene preview vertex count exceeds Vulkan draw limit");
            constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            FrameResources& resources                                  = this->frame_resources.at(context.frame_index);
            ensure_buffer(*context.physical_device, *context.device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, vertices.size() * sizeof(SpectraSceneVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
            write_buffer(resources.vertex_memory, resources.vertex_size, vertices.data(), vertices.size() * sizeof(SpectraSceneVertex));

            SpectraSceneSurfaceShaderParameters parameters{};
            parameters.view_projection = context.view_projection;
            parameters.light_direction = {-0.45f, -0.85f, -0.25f, 0.0f};
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->surface_pipeline);
            context.command_buffer->pushConstants(*this->surface_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const SpectraSceneSurfaceShaderParameters>{1, &parameters});
            const std::array vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            context.command_buffer->bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            context.command_buffer->draw(static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
        }

        if (!overlays.empty()) {
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->overlay_pipeline);
            for (const SpectraScenePreviewOverlay& overlay : overlays) {
                SpectraSceneOverlayShaderParameters parameters{};
                parameters.model_view_projection = multiply_matrix(context.view_projection, overlay.transform);
                parameters.bounds_min            = {overlay.bounds.minimum[0], overlay.bounds.minimum[1], overlay.bounds.minimum[2], 1.0f};
                parameters.bounds_max            = {overlay.bounds.maximum[0], overlay.bounds.maximum[1], overlay.bounds.maximum[2], 1.0f};
                parameters.color                 = overlay.color;
                context.command_buffer->pushConstants(*this->overlay_pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const SpectraSceneOverlayShaderParameters>{1, &parameters});
                context.command_buffer->draw(24, 1, 0, 0);
            }
        }
    }

    bool SpectraScenePreviewRenderer::active() const {
        return static_cast<bool>(*this->surface_pipeline) && static_cast<bool>(*this->overlay_pipeline);
    }

    SpectraScene::SpectraScene() = default;

    SpectraScene::~SpectraScene() noexcept = default;

    void SpectraScene::create_default() {
        this->source_path        = std::filesystem::path{};
        this->entities           = {};
        this->cameras            = {};
        this->geometries         = {};
        this->materials          = {};
        this->lights             = {};
        this->selection          = {};
        this->next_entity_id     = 1;
        this->camera_revision    = 1;
        this->film_revision      = 1;
        this->geometry_revision  = 1;
        this->material_revision  = 1;
        this->light_revision     = 1;
        this->film               = {};
        this->sampler            = {};

        const std::uint64_t camera_id = this->add_entity(SpectraSceneEntityKind::camera, "Main Camera", "perspective", {{-0.05f, -0.05f, -0.05f}, {0.05f, 0.05f, 0.05f}});
        SpectraSceneCamera camera{};
        camera.entity_id         = camera_id;
        camera.state.eye         = {4.0f, 3.0f, 5.0f};
        camera.state.center      = {0.0f, 0.0f, 0.0f};
        camera.state.up          = {0.0f, 1.0f, 0.0f};
        camera.state.fov_degrees = 45.0f;
        this->cameras.push_back(camera);
        this->entity_by_id(camera_id).transform = render_camera_transform(camera.state);

        const std::uint64_t material_id = this->add_entity(SpectraSceneEntityKind::material, "Matte Gray", "diffuse", {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}});
        SpectraSceneMaterial material{};
        material.entity_id  = material_id;
        material.kind       = SpectraMaterialKind::diffuse;
        material.base_color = {0.72f, 0.72f, 0.72f};
        this->materials.push_back(material);

        const std::uint64_t geometry_id = this->add_entity(SpectraSceneEntityKind::geometry, "Default Sphere", "sphere", {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
        SpectraSceneGeometry geometry{};
        geometry.entity_id   = geometry_id;
        geometry.kind        = SpectraGeometryKind::sphere;
        geometry.material_id = material_id;
        geometry.radius      = 1.0f;
        this->geometries.push_back(geometry);

        const std::uint64_t light_id = this->add_entity(SpectraSceneEntityKind::light, "Infinite Light", "infinite", {{-0.05f, -0.05f, -0.05f}, {0.05f, 0.05f, 0.05f}});
        SpectraSceneLight light{};
        light.entity_id = light_id;
        light.kind      = SpectraLightKind::infinite;
        light.color     = {3.0f, 3.0f, 3.0f};
        this->lights.push_back(light);
        this->validate();
    }

    void SpectraScene::validate() const {
        if (this->entities.empty()) throw std::runtime_error("SpectraScene has no entities");
        if (this->cameras.empty()) throw std::runtime_error("SpectraScene has no camera");
        if (this->materials.empty()) throw std::runtime_error("SpectraScene has no material");
        if (this->geometries.empty()) throw std::runtime_error("SpectraScene has no geometry");
        if (this->lights.empty()) throw std::runtime_error("SpectraScene has no light");
        std::set<std::uint64_t> ids{};
        for (const SpectraSceneEntity& entity : this->entities) {
            if (entity.id == 0) throw std::runtime_error("SpectraScene entity id must not be zero");
            if (!ids.insert(entity.id).second) throw std::runtime_error(std::format("Duplicate SpectraScene entity id: {}", entity.id));
        }
        for (const SpectraSceneCamera& camera : this->cameras) static_cast<void>(this->entity_by_id(camera.entity_id));
        for (const SpectraSceneGeometry& geometry : this->geometries) {
            static_cast<void>(this->entity_by_id(geometry.entity_id));
            static_cast<void>(this->material_by_entity_id(geometry.material_id));
            if (geometry.kind == SpectraGeometryKind::sphere && geometry.radius <= 0.0f) throw std::runtime_error("SpectraScene sphere radius must be positive");
            if (geometry.kind == SpectraGeometryKind::disk && geometry.radius <= 0.0f) throw std::runtime_error("SpectraScene disk radius must be positive");
        }
        for (const SpectraSceneMaterial& material : this->materials) static_cast<void>(this->entity_by_id(material.entity_id));
        for (const SpectraSceneLight& light : this->lights) static_cast<void>(this->entity_by_id(light.entity_id));
        if (this->selection.entity_id != 0 && this->find_entity(this->selection.entity_id) == nullptr) throw std::runtime_error("SpectraScene selection points to a missing entity");
        if (this->film.resolution[0] <= 0 || this->film.resolution[1] <= 0) throw std::runtime_error("SpectraScene film resolution must be positive");
        if (this->sampler.samples_per_pixel <= 0) throw std::runtime_error("SpectraScene sampler SPP must be positive");
    }

    void SpectraScene::create_render_resources(const RenderCreateContext& context) {
        this->preview_renderer.create(context);
    }

    void SpectraScene::destroy_render_resources() noexcept {
        this->preview_renderer.destroy();
    }

    void SpectraScene::recreate_render_resources(const RenderCreateContext& context) {
        this->destroy_render_resources();
        this->create_render_resources(context);
    }

    void SpectraScene::render(const RenderFrameContext& context) {
        std::vector<SpectraSceneVertex> vertices{};
        std::vector<SpectraScenePreviewOverlay> overlays{};
        for (const SpectraSceneGeometry& geometry : this->geometries) {
            const SpectraSceneEntity& entity = this->entity_by_id(geometry.entity_id);
            if (!entity.visible) continue;
            const std::array<float, 3> color = material_preview_color(this->materials, geometry.material_id);
            if (geometry.kind == SpectraGeometryKind::sphere) append_sphere_preview(vertices, entity.transform, geometry.radius, color);
            if (geometry.kind == SpectraGeometryKind::disk) append_disk_preview(vertices, entity.transform, geometry.radius, geometry.height, color);
            if (geometry.kind == SpectraGeometryKind::triangle_mesh) {
                if (geometry.indices.empty()) {
                    for (const SpectraSceneVertex& vertex : geometry.vertices) {
                        SpectraSceneVertex transformed = vertex;
                        transformed.position = transform_point(entity.transform, vertex.position);
                        transformed.normal   = normalize_vector(transform_direction(entity.transform, vertex.normal));
                        vertices.push_back(transformed);
                    }
                } else {
                    for (const std::uint32_t index : geometry.indices) {
                        if (index >= geometry.vertices.size()) throw std::runtime_error("SpectraScene triangle mesh index is out of range");
                        SpectraSceneVertex transformed = geometry.vertices[index];
                        transformed.position = transform_point(entity.transform, transformed.position);
                        transformed.normal   = normalize_vector(transform_direction(entity.transform, transformed.normal));
                        vertices.push_back(transformed);
                    }
                }
            }
            if (this->selection.entity_id == entity.id) overlays.push_back({entity.transform, entity.local_bounds, {0.95f, 0.72f, 0.22f, 1.0f}});
        }
        for (const SpectraSceneLight& light : this->lights) {
            const SpectraSceneEntity& entity = this->entity_by_id(light.entity_id);
            if (!entity.visible) continue;
            if (this->selection.entity_id == entity.id) overlays.push_back({entity.transform, entity.local_bounds, {0.95f, 0.72f, 0.22f, 1.0f}});
        }
        this->preview_renderer.render(context, vertices, overlays);
    }

    RenderSceneSnapshot SpectraScene::create_render_snapshot() const {
        this->validate();
        RenderSceneSnapshot snapshot{};
        snapshot.camera            = this->cameras.front().state;
        snapshot.film              = this->film;
        snapshot.sampler           = this->sampler;
        snapshot.camera_revision   = this->camera_revision;
        snapshot.film_revision     = this->film_revision;
        snapshot.geometry_revision = this->geometry_revision;
        snapshot.material_revision = this->material_revision;
        snapshot.light_revision    = this->light_revision;
        snapshot.materials.reserve(this->materials.size());
        for (const SpectraSceneMaterial& material : this->materials) {
            snapshot.materials.push_back({material.kind, material.base_color, material.roughness, material.eta});
        }

        for (const SpectraSceneGeometry& geometry : this->geometries) {
            const SpectraSceneEntity& entity = this->entity_by_id(geometry.entity_id);
            if (!entity.visible) continue;
            std::uint32_t material_index = 0;
            for (std::uint32_t index = 0; index < this->materials.size(); ++index) {
                if (this->materials[index].entity_id == geometry.material_id) {
                    material_index = index;
                    break;
                }
            }
            if (geometry.kind == SpectraGeometryKind::sphere) snapshot.spheres.push_back({entity.transform, geometry.radius, material_index});
            if (geometry.kind == SpectraGeometryKind::disk) snapshot.disks.push_back({entity.transform, geometry.radius, geometry.height, material_index});
            if (geometry.kind == SpectraGeometryKind::triangle_mesh) {
                if (geometry.indices.size() % 3u != 0u) throw std::runtime_error("SpectraScene triangle mesh index count must be divisible by 3");
                for (std::size_t index = 0; index < geometry.indices.size(); index += 3u) {
                    const SpectraSceneVertex& v0 = geometry.vertices.at(geometry.indices[index + 0u]);
                    const SpectraSceneVertex& v1 = geometry.vertices.at(geometry.indices[index + 1u]);
                    const SpectraSceneVertex& v2 = geometry.vertices.at(geometry.indices[index + 2u]);
                    snapshot.triangles.push_back({
                        transform_point(entity.transform, v0.position),
                        transform_point(entity.transform, v1.position),
                        transform_point(entity.transform, v2.position),
                        normalize_vector(transform_direction(entity.transform, v0.normal)),
                        normalize_vector(transform_direction(entity.transform, v1.normal)),
                        normalize_vector(transform_direction(entity.transform, v2.normal)),
                        material_index,
                    });
                }
            }
        }

        snapshot.lights.reserve(this->lights.size());
        for (const SpectraSceneLight& light : this->lights) {
            const SpectraSceneEntity& entity = this->entity_by_id(light.entity_id);
            if (!entity.visible) continue;
            snapshot.lights.push_back({light.kind, entity.transform, light.color, light.intensity});
        }
        return snapshot;
    }

    const std::filesystem::path& SpectraScene::path() const {
        return this->source_path;
    }

    std::size_t SpectraScene::entity_count() const {
        return this->entities.size();
    }

    std::size_t SpectraScene::object_count() const {
        return this->geometries.size();
    }

    SpectraSceneStats SpectraScene::stats() const {
        SpectraSceneStats result{};
        for (const SpectraSceneEntity& entity : this->entities) {
            if (entity.kind == SpectraSceneEntityKind::camera) ++result.cameras;
            if (entity.kind == SpectraSceneEntityKind::geometry) ++result.geometries;
            if (entity.kind == SpectraSceneEntityKind::material) ++result.materials;
            if (entity.kind == SpectraSceneEntityKind::texture) ++result.textures;
            if (entity.kind == SpectraSceneEntityKind::light) ++result.lights;
            if (entity.kind == SpectraSceneEntityKind::render_setting) ++result.render_settings;
            if (entity.kind == SpectraSceneEntityKind::instance) ++result.instances;
        }
        for (const SpectraSceneGeometry& geometry : this->geometries) {
            if (geometry.kind == SpectraGeometryKind::sphere) result.preview_triangles += 48u * 24u * 2u;
            if (geometry.kind == SpectraGeometryKind::disk) result.preview_triangles += 96u;
            if (geometry.kind == SpectraGeometryKind::triangle_mesh) result.preview_triangles += geometry.indices.empty() ? geometry.vertices.size() / 3u : geometry.indices.size() / 3u;
        }
        result.camera_revision   = this->camera_revision;
        result.geometry_revision = this->geometry_revision;
        result.material_revision = this->material_revision;
        result.light_revision    = this->light_revision;
        return result;
    }

    bool SpectraScene::has_selection() const {
        return this->selection.entity_id != 0;
    }

    void SpectraScene::clear_selection() {
        this->selection.entity_id = 0;
    }

    void SpectraScene::select_entity(const std::uint64_t entity_id) {
        if (this->find_entity(entity_id) == nullptr) throw std::runtime_error("Cannot select a missing SpectraScene entity");
        this->selection.entity_id = entity_id;
    }

    SpectraSceneEntity& SpectraScene::selected_entity() {
        SpectraSceneEntity* entity = this->find_entity(this->selection.entity_id);
        if (entity == nullptr) throw std::runtime_error("SpectraScene selection is empty or invalid");
        return *entity;
    }

    const SpectraSceneEntity& SpectraScene::selected_entity() const {
        const SpectraSceneEntity* entity = this->find_entity(this->selection.entity_id);
        if (entity == nullptr) throw std::runtime_error("SpectraScene selection is empty or invalid");
        return *entity;
    }

    std::vector<std::uint64_t> SpectraScene::entity_ids(const SpectraSceneEntityKind kind) const {
        std::vector<std::uint64_t> ids{};
        ids.reserve(this->entities.size());
        for (const SpectraSceneEntity& entity : this->entities) {
            if (entity.kind == kind) ids.push_back(entity.id);
        }
        return ids;
    }

    SpectraSceneEntity& SpectraScene::entity_by_id(const std::uint64_t entity_id) {
        SpectraSceneEntity* entity = this->find_entity(entity_id);
        if (entity == nullptr) throw std::runtime_error("SpectraScene entity id does not exist");
        return *entity;
    }

    const SpectraSceneEntity& SpectraScene::entity_by_id(const std::uint64_t entity_id) const {
        const SpectraSceneEntity* entity = this->find_entity(entity_id);
        if (entity == nullptr) throw std::runtime_error("SpectraScene entity id does not exist");
        return *entity;
    }

    SpectraSceneCamera& SpectraScene::camera_by_entity_id(const std::uint64_t entity_id) {
        for (SpectraSceneCamera& camera : this->cameras) {
            if (camera.entity_id == entity_id) return camera;
        }
        throw std::runtime_error("SpectraScene camera entity id does not exist");
    }

    const SpectraSceneCamera& SpectraScene::camera_by_entity_id(const std::uint64_t entity_id) const {
        for (const SpectraSceneCamera& camera : this->cameras) {
            if (camera.entity_id == entity_id) return camera;
        }
        throw std::runtime_error("SpectraScene camera entity id does not exist");
    }

    SpectraSceneMaterial& SpectraScene::material_by_entity_id(const std::uint64_t entity_id) {
        for (SpectraSceneMaterial& material : this->materials) {
            if (material.entity_id == entity_id) return material;
        }
        throw std::runtime_error("SpectraScene material entity id does not exist");
    }

    const SpectraSceneMaterial& SpectraScene::material_by_entity_id(const std::uint64_t entity_id) const {
        for (const SpectraSceneMaterial& material : this->materials) {
            if (material.entity_id == entity_id) return material;
        }
        throw std::runtime_error("SpectraScene material entity id does not exist");
    }

    SpectraSceneLight& SpectraScene::light_by_entity_id(const std::uint64_t entity_id) {
        for (SpectraSceneLight& light : this->lights) {
            if (light.entity_id == entity_id) return light;
        }
        throw std::runtime_error("SpectraScene light entity id does not exist");
    }

    const SpectraSceneLight& SpectraScene::light_by_entity_id(const std::uint64_t entity_id) const {
        for (const SpectraSceneLight& light : this->lights) {
            if (light.entity_id == entity_id) return light;
        }
        throw std::runtime_error("SpectraScene light entity id does not exist");
    }

    BoundingBoxBounds SpectraScene::world_bounds() const {
        bool initialized{};
        BoundingBoxBounds result{};
        for (const SpectraSceneEntity& entity : this->entities) {
            if (!entity.visible || entity.kind != SpectraSceneEntityKind::geometry) continue;
            const BoundingBoxBounds bounds = this->entity_world_bounds(entity);
            if (!initialized) {
                result      = bounds;
                initialized = true;
            } else {
                expand_bounds(result, bounds);
            }
        }
        if (!initialized) throw std::runtime_error("SpectraScene has no previewable bounds");
        return result;
    }

    BoundingBoxBounds SpectraScene::selected_world_bounds() const {
        return this->entity_world_bounds(this->selected_entity());
    }

    void SpectraScene::mark_entity_transform_edited(const std::uint64_t entity_id) {
        SpectraSceneEntity& entity = this->entity_by_id(entity_id);
        if (entity.kind == SpectraSceneEntityKind::camera) ++this->camera_revision;
        if (entity.kind == SpectraSceneEntityKind::geometry || entity.kind == SpectraSceneEntityKind::instance) ++this->geometry_revision;
        if (entity.kind == SpectraSceneEntityKind::light) ++this->light_revision;
    }

    void SpectraScene::mark_entity_visibility_edited(const std::uint64_t entity_id) {
        const SpectraSceneEntity& entity = this->entity_by_id(entity_id);
        if (entity.kind == SpectraSceneEntityKind::geometry || entity.kind == SpectraSceneEntityKind::instance) ++this->geometry_revision;
        if (entity.kind == SpectraSceneEntityKind::light) ++this->light_revision;
    }

    void SpectraScene::mark_camera_edited(const std::uint64_t entity_id) {
        static_cast<void>(this->camera_by_entity_id(entity_id));
        ++this->camera_revision;
    }

    void SpectraScene::mark_material_edited(const std::uint64_t entity_id) {
        static_cast<void>(this->material_by_entity_id(entity_id));
        ++this->material_revision;
    }

    void SpectraScene::mark_light_edited(const std::uint64_t entity_id) {
        static_cast<void>(this->light_by_entity_id(entity_id));
        ++this->light_revision;
    }

    void SpectraScene::draw_scene_browser_ui() {
        const SpectraSceneStats scene_stats = this->stats();
        if (ImGui::BeginTable("SpectraSceneSummary", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            const auto row = [](const char* name, const std::size_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", value);
            };
            row("Cameras", scene_stats.cameras);
            row("Geometry", scene_stats.geometries);
            row("Materials", scene_stats.materials);
            row("Lights", scene_stats.lights);
            row("Preview Triangles", scene_stats.preview_triangles);
            ImGui::EndTable();
        }

        if (ImGui::BeginTabBar("SpectraSceneBrowserTabs")) {
            if (ImGui::BeginTabItem("Entities")) {
                for (const SpectraSceneEntityKind kind : {SpectraSceneEntityKind::camera, SpectraSceneEntityKind::geometry, SpectraSceneEntityKind::material, SpectraSceneEntityKind::texture, SpectraSceneEntityKind::light, SpectraSceneEntityKind::instance, SpectraSceneEntityKind::render_setting}) {
                    if (ImGui::TreeNodeEx(entity_kind_label(kind), ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (SpectraSceneEntity& entity : this->entities) {
                            if (entity.kind != kind) continue;
                            const bool selected = this->selection.entity_id == entity.id;
                            const std::string label = std::format("{}  {}  #{}", entity_kind_label(entity.kind), entity.name, entity.id);
                            if (ImGui::Selectable(label.c_str(), selected)) this->selection.entity_id = entity.id;
                            if (selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void SpectraScene::draw_selected_inspector_ui(const bool editing_enabled) {
        if (!this->has_selection()) {
            ImGui::TextDisabled("No selection");
            return;
        }
        SpectraSceneEntity& entity = this->selected_entity();
        ImGui::Text("%s", entity.name.c_str());
        ImGui::TextDisabled("%s  %s  #%llu", entity_kind_label(entity.kind), entity.detail.c_str(), static_cast<unsigned long long>(entity.id));
        ImGui::BeginDisabled(!editing_enabled);
        if (ImGui::Checkbox("Visible", &entity.visible)) this->mark_entity_visibility_edited(entity.id);
        ImGui::EndDisabled();
        if (!editing_enabled) ImGui::TextDisabled("Editing is locked while a render job is running");
        if (entity.kind == SpectraSceneEntityKind::geometry || entity.kind == SpectraSceneEntityKind::light || entity.kind == SpectraSceneEntityKind::camera) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) this->draw_entity_transform_ui(entity.id, editing_enabled);
        }
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) this->draw_entity_parameters_ui(entity.id, editing_enabled);
    }

    void SpectraScene::draw_entity_transform_ui(const std::uint64_t entity_id, const bool editing_enabled) {
        SpectraSceneEntity& entity = this->entity_by_id(entity_id);
        if (entity.kind == SpectraSceneEntityKind::camera) {
            ImGui::TextDisabled("Camera transform is derived from camera parameters");
            ImGui::BeginDisabled(true);
            ImGui::DragFloat4("X", &entity.transform[0], 0.01f, -100000.0f, 100000.0f, "%.4f");
            ImGui::DragFloat4("Y", &entity.transform[4], 0.01f, -100000.0f, 100000.0f, "%.4f");
            ImGui::DragFloat4("Z", &entity.transform[8], 0.01f, -100000.0f, 100000.0f, "%.4f");
            ImGui::DragFloat4("T", &entity.transform[12], 0.01f, -100000.0f, 100000.0f, "%.4f");
            ImGui::EndDisabled();
            return;
        }
        ImGui::BeginDisabled(!editing_enabled);
        bool changed = false;
        changed |= ImGui::DragFloat4("X", &entity.transform[0], 0.01f, -100000.0f, 100000.0f, "%.4f");
        changed |= ImGui::DragFloat4("Y", &entity.transform[4], 0.01f, -100000.0f, 100000.0f, "%.4f");
        changed |= ImGui::DragFloat4("Z", &entity.transform[8], 0.01f, -100000.0f, 100000.0f, "%.4f");
        changed |= ImGui::DragFloat4("T", &entity.transform[12], 0.01f, -100000.0f, 100000.0f, "%.4f");
        ImGui::EndDisabled();
        if (changed) this->mark_entity_transform_edited(entity_id);
    }

    void SpectraScene::draw_entity_parameters_ui(const std::uint64_t entity_id, const bool editing_enabled) {
        SpectraSceneEntity& entity = this->entity_by_id(entity_id);
        ImGui::BeginDisabled(!editing_enabled);
        if (entity.kind == SpectraSceneEntityKind::camera) {
            SpectraSceneCamera& camera = this->camera_by_entity_id(entity_id);
            bool changed = false;
            changed |= ImGui::InputFloat3("Eye", camera.state.eye.data(), "%.3f");
            changed |= ImGui::InputFloat3("Center", camera.state.center.data(), "%.3f");
            changed |= ImGui::InputFloat3("Up", camera.state.up.data(), "%.3f");
            changed |= ImGui::SliderFloat("FOV", &camera.state.fov_degrees, 1.0f, 179.0f, "%.1f deg");
            if (changed) {
                entity.transform = render_camera_transform(camera.state);
                this->mark_camera_edited(entity_id);
            }
        } else if (entity.kind == SpectraSceneEntityKind::geometry) {
            for (SpectraSceneGeometry& geometry : this->geometries) {
                if (geometry.entity_id != entity_id) continue;
                ImGui::TextDisabled("%s", geometry_kind_label(geometry.kind));
                bool changed = false;
                if (geometry.kind == SpectraGeometryKind::sphere || geometry.kind == SpectraGeometryKind::disk) changed |= ImGui::DragFloat("Radius", &geometry.radius, 0.01f, 0.001f, 100000.0f, "%.4f");
                if (geometry.kind == SpectraGeometryKind::disk) changed |= ImGui::DragFloat("Height", &geometry.height, 0.01f, -100000.0f, 100000.0f, "%.4f");
                if (changed) {
                    entity.local_bounds = {{-geometry.radius, -geometry.radius, -geometry.radius}, {geometry.radius, geometry.radius, geometry.radius}};
                    ++this->geometry_revision;
                }
                break;
            }
        } else if (entity.kind == SpectraSceneEntityKind::material) {
            SpectraSceneMaterial& material = this->material_by_entity_id(entity_id);
            bool changed = false;
            ImGui::TextDisabled("%s", material_kind_label(material.kind));
            changed |= ImGui::ColorEdit3("Base Color", material.base_color.data());
            changed |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::SliderFloat("Eta", &material.eta, 1.0f, 3.0f, "%.3f");
            if (changed) this->mark_material_edited(entity_id);
        } else if (entity.kind == SpectraSceneEntityKind::light) {
            SpectraSceneLight& light = this->light_by_entity_id(entity_id);
            bool changed = false;
            ImGui::TextDisabled("%s", light_kind_label(light.kind));
            changed |= ImGui::ColorEdit3("Color", light.color.data(), ImGuiColorEditFlags_Float);
            changed |= ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            if (changed) this->mark_light_edited(entity_id);
        } else {
            ImGui::TextDisabled("No editable SpectraScene parameters");
        }
        ImGui::EndDisabled();
    }

    std::uint64_t SpectraScene::add_entity(const SpectraSceneEntityKind kind, std::string name, std::string detail, const BoundingBoxBounds bounds) {
        SpectraSceneEntity entity{};
        entity.id           = this->next_entity_id++;
        entity.kind         = kind;
        entity.name         = std::move(name);
        entity.detail       = std::move(detail);
        entity.component_index = 0;
        entity.transform    = identity_matrix();
        entity.local_bounds = bounds;
        this->entities.push_back(std::move(entity));
        return this->entities.back().id;
    }

    SpectraSceneEntity* SpectraScene::find_entity(const std::uint64_t entity_id) {
        const auto iterator = std::ranges::find(this->entities, entity_id, &SpectraSceneEntity::id);
        return iterator == this->entities.end() ? nullptr : &*iterator;
    }

    const SpectraSceneEntity* SpectraScene::find_entity(const std::uint64_t entity_id) const {
        const auto iterator = std::ranges::find(this->entities, entity_id, &SpectraSceneEntity::id);
        return iterator == this->entities.end() ? nullptr : &*iterator;
    }

    BoundingBoxBounds SpectraScene::entity_world_bounds(const SpectraSceneEntity& entity) const {
        if (entity.kind == SpectraSceneEntityKind::geometry || entity.kind == SpectraSceneEntityKind::light || entity.kind == SpectraSceneEntityKind::camera) return transformed_bounds(entity.transform, entity.local_bounds);
        throw std::runtime_error("Selected SpectraScene entity has no previewable bounds");
    }
} // namespace xayah
