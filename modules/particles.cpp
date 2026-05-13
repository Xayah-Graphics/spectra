module;
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

module particles;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct ParticleShaderParameters {
        std::array<float, 16> view_projection{};
    };

    struct ParticleShaderVertex {
        [[maybe_unused]] std::array<float, 4> position{};
        [[maybe_unused]] std::array<float, 4> local_position{};
        [[maybe_unused]] std::array<float, 4> color{};
    };
} // namespace

namespace xayah {
    ParticlesRenderer::ParticlesRenderer() = default;

    ParticlesRenderer::~ParticlesRenderer() noexcept = default;

    void ParticlesRenderer::create(const SceneRenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("Particles renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create particles renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a color format");
        if (context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a depth format");
        if (context.frame_count == 0) throw std::runtime_error("Cannot create particles renderer without frames in flight");

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(ParticleShaderParameters)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(ParticleShaderVertex), vk::VertexInputRate::eVertex};
        constexpr std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>))},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>) * 2)},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format stencil_format = static_cast<bool>(context.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? context.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &context.color_format;
        rendering_create_info.depthAttachmentFormat   = context.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

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
        pipeline_create_info.layout              = *this->pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->pipeline                           = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
    }

    void ParticlesRenderer::destroy() noexcept {
        this->pipeline        = nullptr;
        this->pipeline_layout = nullptr;
    }

    bool ParticlesRenderer::active() const {
        return static_cast<bool>(*this->pipeline);
    }

    Particles::Particles() = default;

    Particles::~Particles() noexcept = default;

    SceneObjectKind Particles::kind() const {
        return SceneObjectKind::particles;
    }

    void Particles::validate() const {
        if (this->id == 0) throw std::runtime_error(std::string{"Particles id must not be zero: "} + this->name);
        if (this->name.empty()) throw std::runtime_error("Particles name must not be empty");
        if (this->particles.empty()) throw std::runtime_error(std::string{"Particles object has no particles: "} + this->name);
        if (this->render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + this->name);
        if (this->transform.scale[0] <= 0.0f || this->transform.scale[1] <= 0.0f || this->transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Particles transform scale must be positive: "} + this->name);

        for (const Particle& particle : this->particles) {
            if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + this->name);
        }
    }

    void Particles::create_render_resources(const SceneRenderCreateContext& context, const ParticlesRenderer&) {
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create particles object resources without a Vulkan device");
        if (!this->frame_resources.empty()) throw std::runtime_error(std::string{"Particles render resources are already initialized: "} + this->name);
        if (context.frame_count == 0) throw std::runtime_error("Cannot create particles object resources without frames in flight");
        this->frame_resources.resize(context.frame_count);
    }

    void Particles::destroy_render_resources() noexcept {
        this->frame_resources.clear();
    }

    void Particles::render(const SceneRenderFrameContext& context, const ParticlesRenderer& renderer) {
        if (!this->visible || this->particles.empty()) return;
        if (context.physical_device == nullptr || context.device == nullptr || context.command_buffer == nullptr) throw std::runtime_error("Particles render context is incomplete");
        if (context.frame_index >= context.frame_count) throw std::runtime_error("Frame index is outside particles object resource range");
        if (!*renderer.pipeline_layout || !*renderer.pipeline) throw std::runtime_error(std::string{"Particles renderer is not initialized: "} + this->name);
        if (this->frame_resources.size() != context.frame_count) throw std::runtime_error(std::string{"Particles render resources do not match frame count: "} + this->name);
        if (this->render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + this->name);
        if (this->particles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 6)) throw std::runtime_error(std::string{"Particles object has too many particles for draw: "} + this->name);

        std::vector<ParticleShaderVertex> shader_vertices{};
        shader_vertices.reserve(this->particles.size() * 6);
        const std::array<float, 16> model = transform_matrix(this->transform);
        const float object_radius_scale   = maximum_scale(this->transform);
        constexpr std::array particle_corners{
            std::array{-1.0f, -1.0f},
            std::array{1.0f, -1.0f},
            std::array{1.0f, 1.0f},
            std::array{-1.0f, -1.0f},
            std::array{1.0f, 1.0f},
            std::array{-1.0f, 1.0f},
        };
        for (const Particle& particle : this->particles) {
            if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + this->name);
            const std::array<float, 3> center = transform_point(model, particle.position);
            const float radius                = particle.radius * this->render_settings.radius_scale * object_radius_scale;
            for (const std::array<float, 2>& corner : particle_corners) {
                shader_vertices.emplace_back(ParticleShaderVertex{
                    {
                        center[0] + context.camera_right[0] * corner[0] * radius + context.camera_up[0] * corner[1] * radius,
                        center[1] + context.camera_right[1] * corner[0] * radius + context.camera_up[1] * corner[1] * radius,
                        center[2] + context.camera_right[2] * corner[0] * radius + context.camera_up[2] * corner[1] * radius,
                        1.0f,
                    },
                    {corner[0], corner[1], 0.0f, 0.0f},
                    {particle.color[0], particle.color[1], particle.color[2], 1.0f},
                });
            }
        }

        ParticleShaderParameters parameters{};
        parameters.view_projection = context.view_projection;

        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        ParticleDrawResources& resources                           = this->frame_resources.at(context.frame_index);
        ensure_buffer(*context.physical_device, *context.device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, shader_vertices.size() * sizeof(ParticleShaderVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
        write_buffer(resources.vertex_memory, resources.vertex_size, shader_vertices.data(), shader_vertices.size() * sizeof(ParticleShaderVertex));

        context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *renderer.pipeline);
        context.command_buffer->pushConstants(*renderer.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const ParticleShaderParameters>{1, &parameters});
        const std::array vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        context.command_buffer->bindVertexBuffers(0, vertex_buffers, vertex_offsets);
        context.command_buffer->draw(static_cast<std::uint32_t>(shader_vertices.size()), 1, 0, 0);
    }

    void Particles::draw_inspector_ui() {
        constexpr ImVec4 label_color{0.58f, 0.66f, 0.75f, 1.0f};
        constexpr ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        constexpr ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};
        constexpr ImVec4 muted_color{0.70f, 0.76f, 0.82f, 1.0f};

        BoundingBoxBounds local_bounds{};
        float radius_min = 0.0f;
        float radius_max = 0.0f;
        if (!this->particles.empty()) {
            local_bounds = this->bounds();
            radius_min   = this->particles.front().radius;
            radius_max   = this->particles.front().radius;
            for (const Particle& particle : this->particles) {
                if (particle.radius < radius_min) radius_min = particle.radius;
                if (particle.radius > radius_max) radius_max = particle.radius;
            }
        }

        if (ImGui::BeginTable("ParticlesInspectorIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Name");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%s", this->name.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Particles");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%zu", this->particles.size());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Local bounds min");
            ImGui::TableNextColumn();
            if (this->particles.empty())
                ImGui::TextColored(muted_color, "n/a");
            else
                ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", local_bounds.minimum[0], local_bounds.minimum[1], local_bounds.minimum[2]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Local bounds max");
            ImGui::TableNextColumn();
            if (this->particles.empty())
                ImGui::TextColored(muted_color, "n/a");
            else
                ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", local_bounds.maximum[0], local_bounds.maximum[1], local_bounds.maximum[2]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Radius");
            ImGui::TableNextColumn();
            if (this->particles.empty())
                ImGui::TextColored(muted_color, "n/a");
            else
                ImGui::TextColored(value_color, "%.3f - %.3f", radius_min, radius_max);
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextColored(accent_color, "Render");
        ImGui::Checkbox("Bounding Box", &this->render_settings.show_bounding_box);
        ImGui::TextColored(value_color, "Billboard");
        ImGui::InputFloat("Radius Scale", &this->render_settings.radius_scale, 0.05f, 0.2f, "%.3f");
    }

    BoundingBoxBounds Particles::bounds() const {
        if (this->particles.empty()) throw std::runtime_error(std::string{"Cannot compute bounding box for empty particles object: "} + this->name);
        if (this->render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + this->name);

        const Particle& first_particle = this->particles.front();
        if (first_particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + this->name);
        const float first_radius = first_particle.radius * this->render_settings.radius_scale;
        BoundingBoxBounds bounds{
            {
                first_particle.position[0] - first_radius,
                first_particle.position[1] - first_radius,
                first_particle.position[2] - first_radius,
            },
            {
                first_particle.position[0] + first_radius,
                first_particle.position[1] + first_radius,
                first_particle.position[2] + first_radius,
            },
        };

        for (const Particle& particle : this->particles) {
            if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + this->name);
            const float radius = particle.radius * this->render_settings.radius_scale;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float minimum = particle.position[axis] - radius;
                const float maximum = particle.position[axis] + radius;
                if (minimum < bounds.minimum[axis]) bounds.minimum[axis] = minimum;
                if (maximum > bounds.maximum[axis]) bounds.maximum[axis] = maximum;
            }
        }
        return bounds;
    }

    ParticlesSnapshot Particles::make_snapshot() const {
        return ParticlesSnapshot{this->id, this->particles};
    }

    void Particles::apply_snapshot(const ParticlesSnapshot& snapshot) {
        if (snapshot.object_id != this->id) throw std::runtime_error(std::string{"Particles snapshot id does not match object: "} + this->name);
        for (const Particle& particle : snapshot.particles) {
            if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Baked particle radius must be positive: "} + this->name);
        }
        this->particles = snapshot.particles;
    }
} // namespace xayah
