module;
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

module mesh;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct MeshShaderVertex {
        [[maybe_unused]] std::array<float, 4> position{};
        [[maybe_unused]] std::array<float, 4> normal{};
        [[maybe_unused]] std::array<float, 4> color{};
    };

    MeshShaderVertex mesh_shader_vertex(const xayah::MeshVertex& vertex) {
        return MeshShaderVertex{
            {vertex.position[0], vertex.position[1], vertex.position[2], 1.0f},
            {vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0f},
            {vertex.color[0], vertex.color[1], vertex.color[2], 1.0f},
        };
    }

    struct MeshShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 16> model{};
        std::array<float, 16> normal_matrix{};
        std::array<float, 4> light_direction{};
    };
} // namespace

namespace xayah {
    MeshRenderer::MeshRenderer() = default;

    MeshRenderer::~MeshRenderer() noexcept = default;

    void MeshRenderer::create(const SceneRenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("Mesh renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create mesh renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a color format");
        if (context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a depth format");
        if (context.frame_count == 0) throw std::runtime_error("Cannot create mesh renderer without frames in flight");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->descriptor_layout = vk::raii::DescriptorSetLayout{*context.device, descriptor_layout_create_info};

        const vk::DescriptorSetLayout descriptor_layout_handle = *this->descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout_handle};
        this->pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(MeshShaderVertex), vk::VertexInputRate::eVertex};
        constexpr std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>))},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>) * 2)},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
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
        this->surface_pipeline                   = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
        input_assembly_state.topology            = vk::PrimitiveTopology::eLineList;
        this->wireframe_pipeline                 = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
    }

    void MeshRenderer::destroy() noexcept {
        this->wireframe_pipeline = nullptr;
        this->surface_pipeline   = nullptr;
        this->pipeline_layout    = nullptr;
        this->descriptor_layout  = nullptr;
    }

    bool MeshRenderer::active() const {
        return static_cast<bool>(*this->surface_pipeline);
    }

    Mesh::Mesh() = default;

    Mesh::~Mesh() noexcept = default;

    SceneObjectKind Mesh::kind() const {
        return SceneObjectKind::mesh;
    }

    void Mesh::validate() const {
        if (this->id == 0) throw std::runtime_error(std::string{"Mesh id must not be zero: "} + this->name);
        if (this->name.empty()) throw std::runtime_error("Mesh name must not be empty");
        if (this->vertices.empty()) throw std::runtime_error(std::string{"Mesh has no vertices: "} + this->name);
        if (this->indices.empty()) throw std::runtime_error(std::string{"Mesh has no indices: "} + this->name);
        if (this->indices.size() % 3 != 0) throw std::runtime_error(std::string{"Mesh index count must be divisible by 3: "} + this->name);
        if (this->indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::string{"Mesh has too many indices for Vulkan draw: "} + this->name);
        if (this->transform.scale[0] <= 0.0f || this->transform.scale[1] <= 0.0f || this->transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Mesh transform scale must be positive: "} + this->name);

        for (const MeshVertex& vertex : this->vertices) {
            const float normal_length_squared = vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
            if (normal_length_squared <= 0.000001f) throw std::runtime_error(std::string{"Mesh vertex normal must not be zero: "} + this->name);
        }

        for (const std::uint32_t index : this->indices) {
            if (index >= this->vertices.size()) throw std::runtime_error(std::string{"Mesh index is outside vertex range: "} + this->name);
        }
    }

    void Mesh::create_render_resources(const SceneRenderCreateContext& context, const MeshRenderer& renderer) {
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create mesh object resources without a Vulkan device");
        if (!*renderer.descriptor_layout) throw std::runtime_error("Cannot create mesh object resources without a shared descriptor layout");
        if (*this->descriptor_pool || this->descriptor_sets.size() != 0 || !this->frame_resources.empty()) throw std::runtime_error(std::string{"Mesh render resources are already initialized: "} + this->name);
        if (context.frame_count == 0) throw std::runtime_error("Cannot create mesh object resources without frames in flight");

        this->frame_resources.resize(context.frame_count);
        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, context.frame_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, context.frame_count, 1, &pool_size};
        this->descriptor_pool = vk::raii::DescriptorPool{*context.device, descriptor_pool_create_info};

        std::vector layouts(context.frame_count, *renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->descriptor_pool, context.frame_count, layouts.data()};
        this->descriptor_sets = vk::raii::DescriptorSets{*context.device, allocate_info};
        if (this->descriptor_sets.size() != context.frame_count) throw std::runtime_error(std::string{"Failed to allocate mesh descriptor sets: "} + this->name);
    }

    void Mesh::destroy_render_resources() noexcept {
        this->descriptor_sets = nullptr;
        this->descriptor_pool = nullptr;
        this->frame_resources.clear();
    }

    void Mesh::render(const SceneRenderFrameContext& context, const MeshRenderer& renderer) {
        if (!this->visible) return;
        if (context.physical_device == nullptr || context.device == nullptr || context.command_buffer == nullptr) throw std::runtime_error("Mesh render context is incomplete");
        if (context.frame_index >= context.frame_count) throw std::runtime_error("Frame index is outside mesh object resource range");
        if (!*renderer.pipeline_layout || !*renderer.surface_pipeline || !*renderer.wireframe_pipeline || this->descriptor_sets.size() == 0) throw std::runtime_error(std::string{"Mesh renderer is not initialized: "} + this->name);
        if (this->frame_resources.size() != context.frame_count || this->descriptor_sets.size() != context.frame_count) throw std::runtime_error(std::string{"Mesh render resources do not match frame count: "} + this->name);

        std::vector<MeshShaderVertex> shader_vertices{};
        if (this->render_settings.display_mode == MeshDisplayMode::wireframe) {
            if (this->indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 2)) throw std::runtime_error(std::string{"Mesh has too many indices for wireframe draw: "} + this->name);
            shader_vertices.reserve(this->indices.size() * 2);
            for (std::size_t index = 0; index < this->indices.size(); index += 3) {
                const std::uint32_t i0 = this->indices[index + 0];
                const std::uint32_t i1 = this->indices[index + 1];
                const std::uint32_t i2 = this->indices[index + 2];
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i0]));
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i1]));
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i1]));
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i2]));
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i2]));
                shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[i0]));
            }
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *renderer.wireframe_pipeline);
        } else {
            shader_vertices.reserve(this->indices.size());
            for (const std::uint32_t index : this->indices) shader_vertices.emplace_back(mesh_shader_vertex(this->vertices[index]));
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *renderer.surface_pipeline);
        }
        if (shader_vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::string{"Mesh has too many expanded vertices for draw: "} + this->name);

        MeshShaderParameters parameters{};
        parameters.view_projection = context.view_projection;
        parameters.model           = transform_matrix(this->transform);
        parameters.normal_matrix   = normal_matrix(parameters.model);
        parameters.light_direction = {-0.45f, -0.85f, -0.25f, 0.0f};

        constexpr vk::BufferUsageFlags storage_buffer_usage{vk::BufferUsageFlagBits::eStorageBuffer};
        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        MeshDrawResources& resources                               = this->frame_resources.at(context.frame_index);
        ensure_buffer(*context.physical_device, *context.device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, shader_vertices.size() * sizeof(MeshShaderVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
        ensure_buffer(*context.physical_device, *context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(MeshShaderParameters), storage_buffer_usage, upload_memory_properties);
        write_buffer(resources.vertex_memory, resources.vertex_size, shader_vertices.data(), shader_vertices.size() * sizeof(MeshShaderVertex));
        write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(MeshShaderParameters));

        const std::array buffer_infos{
            vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
        };
        const vk::DescriptorSet descriptor_set = *this->descriptor_sets[context.frame_index];
        const std::array writes{
            vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
        };
        context.device->updateDescriptorSets(writes, {});

        context.command_buffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
        const std::array vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        context.command_buffer->bindVertexBuffers(0, vertex_buffers, vertex_offsets);
        context.command_buffer->draw(static_cast<std::uint32_t>(shader_vertices.size()), 1, 0, 0);
    }

    void Mesh::draw_inspector_ui() {
        constexpr ImVec4 label_color{0.58f, 0.66f, 0.75f, 1.0f};
        constexpr ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        constexpr ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};

        const BoundingBoxBounds local_bounds = this->bounds();
        if (ImGui::BeginTable("MeshInspectorIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Name");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%s", this->name.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Vertices");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%zu", this->vertices.size());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Triangles");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%zu", this->indices.size() / 3);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Local bounds min");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", local_bounds.minimum[0], local_bounds.minimum[1], local_bounds.minimum[2]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Local bounds max");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", local_bounds.maximum[0], local_bounds.maximum[1], local_bounds.maximum[2]);
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextColored(accent_color, "Render");
        ImGui::Checkbox("Bounding Box", &this->render_settings.show_bounding_box);
        const char* mode_label = this->render_settings.display_mode == MeshDisplayMode::surface ? "Surface" : "Wireframe";
        if (ImGui::BeginCombo("Mode", mode_label)) {
            const bool surface_selected = this->render_settings.display_mode == MeshDisplayMode::surface;
            if (ImGui::Selectable("Surface", surface_selected)) this->render_settings.display_mode = MeshDisplayMode::surface;
            if (surface_selected) ImGui::SetItemDefaultFocus();
            const bool wireframe_selected = this->render_settings.display_mode == MeshDisplayMode::wireframe;
            if (ImGui::Selectable("Wireframe", wireframe_selected)) this->render_settings.display_mode = MeshDisplayMode::wireframe;
            if (wireframe_selected) ImGui::SetItemDefaultFocus();
            ImGui::EndCombo();
        }
    }

    BoundingBoxBounds Mesh::bounds() const {
        if (this->vertices.empty()) throw std::runtime_error(std::string{"Cannot compute bounding box for empty mesh: "} + this->name);
        BoundingBoxBounds bounds{this->vertices.front().position, this->vertices.front().position};
        for (const MeshVertex& vertex : this->vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (vertex.position[axis] < bounds.minimum[axis]) bounds.minimum[axis] = vertex.position[axis];
                if (vertex.position[axis] > bounds.maximum[axis]) bounds.maximum[axis] = vertex.position[axis];
            }
        }
        return bounds;
    }

    MeshSnapshot Mesh::make_snapshot() const {
        return MeshSnapshot{this->id, this->vertices};
    }

    void Mesh::apply_snapshot(const MeshSnapshot& snapshot) {
        if (snapshot.object_id != this->id) throw std::runtime_error(std::string{"Mesh snapshot id does not match object: "} + this->name);
        if (snapshot.vertices.size() != this->vertices.size()) throw std::runtime_error(std::string{"Snapshot mesh vertex count does not match live mesh: "} + this->name);
        for (const MeshVertex& vertex : snapshot.vertices) {
            const float normal_length_squared = vertex.normal[0] * vertex.normal[0] + vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2];
            if (normal_length_squared <= 0.000001f) throw std::runtime_error(std::string{"Snapshot mesh vertex normal must not be zero: "} + this->name);
        }
        this->vertices = snapshot.vertices;
    }
} // namespace xayah
