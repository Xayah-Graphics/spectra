module;
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

module volume;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct VolumeShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 16> model{};
        std::array<float, 4> camera_local_step{};
        std::array<float, 4> local_min_opacity{};
        std::array<float, 4> spacing_value_min{};
        std::array<std::uint32_t, 4> resolution_kind{};
        std::array<std::uint32_t, 4> mode_options{};
        std::array<float, 4> slice_value_max{};
    };
} // namespace

namespace xayah {
    VolumeRenderer::VolumeRenderer() = default;

    VolumeRenderer::~VolumeRenderer() noexcept = default;

    void VolumeRenderer::create(const SceneRenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("Volume renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create volume renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a color format");
        if (context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a depth format");
        if (context.frame_count == 0) throw std::runtime_error("Cannot create volume renderer without frames in flight");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->descriptor_layout = vk::raii::DescriptorSetLayout{*context.device, descriptor_layout_create_info};

        const vk::DescriptorSetLayout descriptor_layout_handle = *this->descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout_handle};
        this->pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eBack,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
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

    void VolumeRenderer::destroy() noexcept {
        this->pipeline          = nullptr;
        this->pipeline_layout   = nullptr;
        this->descriptor_layout = nullptr;
    }

    bool VolumeRenderer::active() const {
        return static_cast<bool>(*this->pipeline);
    }

    Volume::Volume() = default;

    Volume::~Volume() noexcept = default;

    SceneObjectKind Volume::kind() const {
        return SceneObjectKind::volume;
    }

    void Volume::validate() const {
        if (this->id == 0) throw std::runtime_error(std::string{"Volume id must not be zero: "} + this->name);
        if (this->name.empty()) throw std::runtime_error("Volume name must not be empty");
        if (this->size[0] <= 0.0f || this->size[1] <= 0.0f || this->size[2] <= 0.0f) throw std::runtime_error(std::string{"Volume size must be positive: "} + this->name);
        if (this->transform.scale[0] <= 0.0f || this->transform.scale[1] <= 0.0f || this->transform.scale[2] <= 0.0f) throw std::runtime_error(std::string{"Volume transform scale must be positive: "} + this->name);
        if (this->centered_scalar_grids.empty() && this->staggered_vector_grids.empty()) throw std::runtime_error(std::string{"Volume has no grids: "} + this->name);
        if (this->render_settings.opacity < 0.0f || this->render_settings.opacity > 1.0f) throw std::runtime_error(std::string{"Volume opacity must be in [0, 1]: "} + this->name);
        if (this->render_settings.raymarch_step <= 0.0f) throw std::runtime_error(std::string{"Volume raymarch step must be positive: "} + this->name);
        if (this->render_settings.value_min >= this->render_settings.value_max) throw std::runtime_error(std::string{"Volume value range is invalid: "} + this->name);
        if (this->render_settings.slice_position < 0.0f || this->render_settings.slice_position > 1.0f) throw std::runtime_error(std::string{"Volume slice position must be in [0, 1]: "} + this->name);

        std::set<std::string> grid_names{};
        for (const CenteredScalarGrid& grid : this->centered_scalar_grids) {
            if (grid.name.empty()) throw std::runtime_error(std::string{"Centered scalar grid name must not be empty in volume: "} + this->name);
            if (!grid_names.insert(grid.name).second) throw std::runtime_error(std::string{"Duplicate grid name in volume "} + this->name + ": " + grid.name);
            if (grid.resolution[0] < 2 || grid.resolution[1] < 2 || grid.resolution[2] < 2) throw std::runtime_error(std::string{"Centered scalar grid resolution must be at least 2 on every axis: "} + grid.name);
            const std::size_t value_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2]);
            if (grid.values.size() != value_count) throw std::runtime_error(std::string{"Centered scalar grid value count does not match grid resolution: "} + grid.name);
        }

        for (const StaggeredVectorGrid& grid : this->staggered_vector_grids) {
            if (grid.name.empty()) throw std::runtime_error(std::string{"Staggered vector grid name must not be empty in volume: "} + this->name);
            if (!grid_names.insert(grid.name).second) throw std::runtime_error(std::string{"Duplicate grid name in volume "} + this->name + ": " + grid.name);
            if (grid.resolution[0] < 2 || grid.resolution[1] < 2 || grid.resolution[2] < 2) throw std::runtime_error(std::string{"Staggered vector grid resolution must be at least 2 on every axis: "} + grid.name);
            const std::size_t x_count = static_cast<std::size_t>(grid.resolution[0] + 1) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2]);
            const std::size_t y_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1] + 1) * static_cast<std::size_t>(grid.resolution[2]);
            const std::size_t z_count = static_cast<std::size_t>(grid.resolution[0]) * static_cast<std::size_t>(grid.resolution[1]) * static_cast<std::size_t>(grid.resolution[2] + 1);
            if (grid.x_values.size() != x_count) throw std::runtime_error(std::string{"Staggered vector grid x-face value count does not match grid resolution: "} + grid.name);
            if (grid.y_values.size() != y_count) throw std::runtime_error(std::string{"Staggered vector grid y-face value count does not match grid resolution: "} + grid.name);
            if (grid.z_values.size() != z_count) throw std::runtime_error(std::string{"Staggered vector grid z-face value count does not match grid resolution: "} + grid.name);
        }

        if (this->render_settings.grid_kind == VolumeGridKind::centered_scalar) static_cast<void>(this->render_centered_scalar_grid());
        if (this->render_settings.grid_kind == VolumeGridKind::staggered_vector) static_cast<void>(this->render_staggered_vector_grid());
    }

    void Volume::initialize_render_settings() {
        if (this->render_settings.grid_name.empty())
            this->select_first_grid();
        else if (this->render_settings.grid_kind == VolumeGridKind::centered_scalar)
            static_cast<void>(this->render_centered_scalar_grid());
        else
            static_cast<void>(this->render_staggered_vector_grid());
    }

    void Volume::select_first_grid() {
        if (!this->centered_scalar_grids.empty()) {
            this->render_settings.grid_kind = VolumeGridKind::centered_scalar;
            this->render_settings.grid_name = this->centered_scalar_grids.front().name;
            return;
        }
        if (!this->staggered_vector_grids.empty()) {
            this->render_settings.grid_kind = VolumeGridKind::staggered_vector;
            this->render_settings.grid_name = this->staggered_vector_grids.front().name;
            return;
        }
        throw std::runtime_error(std::string{"Volume has no selectable grids: "} + this->name);
    }

    const CenteredScalarGrid& Volume::render_centered_scalar_grid() const {
        for (const CenteredScalarGrid& grid : this->centered_scalar_grids) {
            if (grid.name == this->render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Volume render centered scalar grid does not exist: "} + this->render_settings.grid_name);
    }

    const StaggeredVectorGrid& Volume::render_staggered_vector_grid() const {
        for (const StaggeredVectorGrid& grid : this->staggered_vector_grids) {
            if (grid.name == this->render_settings.grid_name) return grid;
        }
        throw std::runtime_error(std::string{"Volume render staggered vector grid does not exist: "} + this->render_settings.grid_name);
    }

    void Volume::create_render_resources(const SceneRenderCreateContext& context, const VolumeRenderer& renderer) {
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create volume object resources without a Vulkan device");
        if (!*renderer.descriptor_layout) throw std::runtime_error("Cannot create volume object resources without a shared descriptor layout");
        if (*this->descriptor_pool || this->descriptor_sets.size() != 0 || !this->frame_resources.empty()) throw std::runtime_error(std::string{"Volume render resources are already initialized: "} + this->name);
        if (context.frame_count == 0) throw std::runtime_error("Cannot create volume object resources without frames in flight");
        if (context.frame_count > std::numeric_limits<std::uint32_t>::max() / 4) throw std::runtime_error("Volume descriptor pool size is too large");

        this->frame_resources.resize(context.frame_count);
        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, context.frame_count * 4};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, context.frame_count, 1, &pool_size};
        this->descriptor_pool = vk::raii::DescriptorPool{*context.device, descriptor_pool_create_info};

        std::vector layouts(context.frame_count, *renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->descriptor_pool, context.frame_count, layouts.data()};
        this->descriptor_sets = vk::raii::DescriptorSets{*context.device, allocate_info};
        if (this->descriptor_sets.size() != context.frame_count) throw std::runtime_error(std::string{"Failed to allocate volume descriptor sets: "} + this->name);
    }

    void Volume::destroy_render_resources() noexcept {
        this->descriptor_sets = nullptr;
        this->descriptor_pool = nullptr;
        this->frame_resources.clear();
    }

    void Volume::render(const SceneRenderFrameContext& context, const VolumeRenderer& renderer) {
        if (!this->visible) return;
        if (context.physical_device == nullptr || context.device == nullptr || context.command_buffer == nullptr) throw std::runtime_error("Volume render context is incomplete");
        if (context.frame_index >= context.frame_count) throw std::runtime_error("Frame index is outside volume object resource range");
        if (!*renderer.pipeline_layout || !*renderer.pipeline || this->descriptor_sets.size() == 0) throw std::runtime_error(std::string{"Volume renderer is not initialized: "} + this->name);
        if (this->frame_resources.size() != context.frame_count || this->descriptor_sets.size() != context.frame_count) throw std::runtime_error(std::string{"Volume render resources do not match frame count: "} + this->name);

        const VolumeRenderSettings& settings      = this->render_settings;
        const std::array<float, 16> model         = transform_matrix(this->transform);
        const std::array<float, 16> inverse_model = inverse_affine_matrix(model);
        const std::array<float, 3> camera_local   = transform_point(inverse_model, context.camera_position);
        const std::array<float, 3> local_min{
            -this->size[0] * 0.5f,
            -this->size[1] * 0.5f,
            -this->size[2] * 0.5f,
        };

        constexpr vk::BufferUsageFlags storage_buffer_usage{vk::BufferUsageFlagBits::eStorageBuffer};
        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        VolumeDrawResources& resources                             = this->frame_resources.at(context.frame_index);

        if (settings.grid_kind == VolumeGridKind::centered_scalar) {
            const CenteredScalarGrid& grid = this->render_centered_scalar_grid();
            const std::array spacing{
                this->size[0] / static_cast<float>(grid.resolution[0]),
                this->size[1] / static_cast<float>(grid.resolution[1]),
                this->size[2] / static_cast<float>(grid.resolution[2]),
            };

            VolumeShaderParameters parameters{};
            parameters.view_projection   = context.view_projection;
            parameters.model             = model;
            parameters.camera_local_step = {camera_local[0], camera_local[1], camera_local[2], settings.raymarch_step};
            parameters.local_min_opacity = {local_min[0], local_min[1], local_min[2], settings.opacity};
            parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], settings.value_min};
            parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::centered_scalar)};
            parameters.mode_options      = {static_cast<std::uint32_t>(settings.display_mode), static_cast<std::uint32_t>(settings.slice_axis), static_cast<std::uint32_t>(settings.color_map), 0};
            parameters.slice_value_max   = {settings.slice_position, settings.value_max, 0.0f, 0.0f};

            ensure_buffer(*context.physical_device, *context.device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
            ensure_buffer(*context.physical_device, *context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
            write_buffer(resources.x_data_memory, resources.x_data_size, grid.values.data(), grid.values.size() * sizeof(float));
            write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
        } else {
            const StaggeredVectorGrid& grid = this->render_staggered_vector_grid();
            const std::array spacing{
                this->size[0] / static_cast<float>(grid.resolution[0]),
                this->size[1] / static_cast<float>(grid.resolution[1]),
                this->size[2] / static_cast<float>(grid.resolution[2]),
            };

            VolumeShaderParameters parameters{};
            parameters.view_projection   = context.view_projection;
            parameters.model             = model;
            parameters.camera_local_step = {camera_local[0], camera_local[1], camera_local[2], settings.raymarch_step};
            parameters.local_min_opacity = {local_min[0], local_min[1], local_min[2], settings.opacity};
            parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], settings.value_min};
            parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::staggered_vector)};
            parameters.mode_options      = {static_cast<std::uint32_t>(settings.display_mode), static_cast<std::uint32_t>(settings.slice_axis), static_cast<std::uint32_t>(settings.color_map), 0};
            parameters.slice_value_max   = {settings.slice_position, settings.value_max, 0.0f, 0.0f};

            ensure_buffer(*context.physical_device, *context.device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.x_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
            ensure_buffer(*context.physical_device, *context.device, resources.y_data_buffer, resources.y_data_memory, resources.y_data_size, grid.y_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
            ensure_buffer(*context.physical_device, *context.device, resources.z_data_buffer, resources.z_data_memory, resources.z_data_size, grid.z_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
            ensure_buffer(*context.physical_device, *context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
            write_buffer(resources.x_data_memory, resources.x_data_size, grid.x_values.data(), grid.x_values.size() * sizeof(float));
            write_buffer(resources.y_data_memory, resources.y_data_size, grid.y_values.data(), grid.y_values.size() * sizeof(float));
            write_buffer(resources.z_data_memory, resources.z_data_size, grid.z_values.data(), grid.z_values.size() * sizeof(float));
            write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
        }

        const vk::raii::Buffer& y_data_buffer = settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.y_data_buffer;
        const vk::raii::Buffer& z_data_buffer = settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.z_data_buffer;
        const vk::DeviceSize y_data_size      = settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.y_data_size;
        const vk::DeviceSize z_data_size      = settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.z_data_size;
        const std::array buffer_infos{
            vk::DescriptorBufferInfo{*resources.x_data_buffer, 0, resources.x_data_size},
            vk::DescriptorBufferInfo{*y_data_buffer, 0, y_data_size},
            vk::DescriptorBufferInfo{*z_data_buffer, 0, z_data_size},
            vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
        };
        const vk::DescriptorSet descriptor_set = *this->descriptor_sets[context.frame_index];
        const std::array writes{
            vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
            vk::WriteDescriptorSet{descriptor_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1]},
            vk::WriteDescriptorSet{descriptor_set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[2]},
            vk::WriteDescriptorSet{descriptor_set, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[3]},
        };
        context.device->updateDescriptorSets(writes, {});

        const std::uint32_t vertex_count = settings.display_mode == VolumeDisplayMode::direct ? 36u : 6u;
        context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *renderer.pipeline);
        context.command_buffer->bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
        context.command_buffer->draw(vertex_count, 1, 0, 0);
    }

    void Volume::draw_inspector_ui() {
        constexpr ImVec4 label_color{0.58f, 0.66f, 0.75f, 1.0f};
        constexpr ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        constexpr ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};
        constexpr ImVec4 muted_color{0.70f, 0.76f, 0.82f, 1.0f};

        if (ImGui::BeginTable("VolumeInspectorIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Name");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%s", this->name.c_str());

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Local size");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", this->size[0], this->size[1], this->size[2]);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Grids");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%zu scalar / %zu vector", this->centered_scalar_grids.size(), this->staggered_vector_grids.size());
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextColored(accent_color, "Grids");
        if (ImGui::BeginTable("VolumeInspectorGridList", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Resolution");
            ImGui::TableHeadersRow();
            for (const CenteredScalarGrid& grid : this->centered_scalar_grids) {
                const bool selected     = this->render_settings.grid_kind == VolumeGridKind::centered_scalar && grid.name == this->render_settings.grid_name;
                const std::string label = std::string{"Scalar##VolumeGridScalar:"} + grid.name;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : label_color);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    this->render_settings.grid_kind = VolumeGridKind::centered_scalar;
                    this->render_settings.grid_name = grid.name;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextColored(selected ? accent_color : value_color, "%s", grid.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(muted_color, "%u x %u x %u", grid.resolution[0], grid.resolution[1], grid.resolution[2]);
            }
            for (const StaggeredVectorGrid& grid : this->staggered_vector_grids) {
                const bool selected     = this->render_settings.grid_kind == VolumeGridKind::staggered_vector && grid.name == this->render_settings.grid_name;
                const std::string label = std::string{"Vector##VolumeGridVector:"} + grid.name;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : label_color);
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    this->render_settings.grid_kind = VolumeGridKind::staggered_vector;
                    this->render_settings.grid_name = grid.name;
                }
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::TextColored(selected ? accent_color : value_color, "%s", grid.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(muted_color, "%u x %u x %u", grid.resolution[0], grid.resolution[1], grid.resolution[2]);
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextColored(accent_color, "Render");
        ImGui::Checkbox("Bounding Box", &this->render_settings.show_bounding_box);
        const char* mode_label = this->render_settings.display_mode == VolumeDisplayMode::direct ? "Direct" : "Slice";
        if (ImGui::BeginCombo("Mode", mode_label)) {
            const bool direct_selected = this->render_settings.display_mode == VolumeDisplayMode::direct;
            if (ImGui::Selectable("Direct", direct_selected)) this->render_settings.display_mode = VolumeDisplayMode::direct;
            if (direct_selected) ImGui::SetItemDefaultFocus();
            const bool slice_selected = this->render_settings.display_mode == VolumeDisplayMode::slice;
            if (ImGui::Selectable("Slice", slice_selected)) this->render_settings.display_mode = VolumeDisplayMode::slice;
            if (slice_selected) ImGui::SetItemDefaultFocus();
            ImGui::EndCombo();
        }

        const char* color_map_label = "Viridis";
        if (this->render_settings.color_map == VolumeColorMap::grayscale) color_map_label = "Grayscale";
        if (this->render_settings.color_map == VolumeColorMap::turbo) color_map_label = "Turbo";
        if (this->render_settings.color_map == VolumeColorMap::heat) color_map_label = "Heat";
        if (ImGui::BeginCombo("Color Map", color_map_label)) {
            const bool grayscale_selected = this->render_settings.color_map == VolumeColorMap::grayscale;
            if (ImGui::Selectable("Grayscale", grayscale_selected)) this->render_settings.color_map = VolumeColorMap::grayscale;
            if (grayscale_selected) ImGui::SetItemDefaultFocus();
            const bool viridis_selected = this->render_settings.color_map == VolumeColorMap::viridis;
            if (ImGui::Selectable("Viridis", viridis_selected)) this->render_settings.color_map = VolumeColorMap::viridis;
            if (viridis_selected) ImGui::SetItemDefaultFocus();
            const bool turbo_selected = this->render_settings.color_map == VolumeColorMap::turbo;
            if (ImGui::Selectable("Turbo", turbo_selected)) this->render_settings.color_map = VolumeColorMap::turbo;
            if (turbo_selected) ImGui::SetItemDefaultFocus();
            const bool heat_selected = this->render_settings.color_map == VolumeColorMap::heat;
            if (ImGui::Selectable("Heat", heat_selected)) this->render_settings.color_map = VolumeColorMap::heat;
            if (heat_selected) ImGui::SetItemDefaultFocus();
            ImGui::EndCombo();
        }

        if (this->render_settings.display_mode == VolumeDisplayMode::slice) {
            const char* axis_label = "Y";
            if (this->render_settings.slice_axis == VolumeSliceAxis::x) axis_label = "X";
            if (this->render_settings.slice_axis == VolumeSliceAxis::z) axis_label = "Z";
            if (ImGui::BeginCombo("Axis", axis_label)) {
                const bool x_selected = this->render_settings.slice_axis == VolumeSliceAxis::x;
                if (ImGui::Selectable("X", x_selected)) this->render_settings.slice_axis = VolumeSliceAxis::x;
                if (x_selected) ImGui::SetItemDefaultFocus();
                const bool y_selected = this->render_settings.slice_axis == VolumeSliceAxis::y;
                if (ImGui::Selectable("Y", y_selected)) this->render_settings.slice_axis = VolumeSliceAxis::y;
                if (y_selected) ImGui::SetItemDefaultFocus();
                const bool z_selected = this->render_settings.slice_axis == VolumeSliceAxis::z;
                if (ImGui::Selectable("Z", z_selected)) this->render_settings.slice_axis = VolumeSliceAxis::z;
                if (z_selected) ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }
            ImGui::SliderFloat("Slice", &this->render_settings.slice_position, 0.0f, 1.0f, "%.3f");
        }

        std::array value_range{this->render_settings.value_min, this->render_settings.value_max};
        if (ImGui::InputFloat2("Value Range", value_range.data())) {
            this->render_settings.value_min = value_range[0];
            this->render_settings.value_max = value_range[1];
        }
        ImGui::SliderFloat("Opacity", &this->render_settings.opacity, 0.0f, 1.0f, "%.3f");
        ImGui::InputFloat("Raymarch Step", &this->render_settings.raymarch_step, 0.001f, 0.01f, "%.4f");
    }

    BoundingBoxBounds Volume::bounds() const {
        return BoundingBoxBounds{
            {-this->size[0] * 0.5f, -this->size[1] * 0.5f, -this->size[2] * 0.5f},
            {this->size[0] * 0.5f, this->size[1] * 0.5f, this->size[2] * 0.5f},
        };
    }

    VolumeSnapshot Volume::make_snapshot() const {
        return VolumeSnapshot{this->id, this->centered_scalar_grids, this->staggered_vector_grids};
    }

    void Volume::apply_snapshot(const VolumeSnapshot& snapshot) {
        if (snapshot.object_id != this->id) throw std::runtime_error(std::string{"Volume snapshot id does not match object: "} + this->name);
        if (snapshot.centered_scalar_grids.size() != this->centered_scalar_grids.size()) throw std::runtime_error(std::string{"Snapshot centered scalar grid count does not match volume: "} + this->name);
        if (snapshot.staggered_vector_grids.size() != this->staggered_vector_grids.size()) throw std::runtime_error(std::string{"Snapshot staggered vector grid count does not match volume: "} + this->name);

        for (CenteredScalarGrid& grid : this->centered_scalar_grids) {
            const CenteredScalarGrid* snapshot_grid = nullptr;
            for (const CenteredScalarGrid& candidate : snapshot.centered_scalar_grids) {
                if (candidate.name == grid.name) {
                    snapshot_grid = &candidate;
                    break;
                }
            }
            if (snapshot_grid == nullptr) throw std::runtime_error(std::string{"Snapshot is missing centered scalar grid: "} + grid.name);
            if (snapshot_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Snapshot centered scalar grid resolution does not match live grid: "} + grid.name);
            if (snapshot_grid->values.size() != grid.values.size()) throw std::runtime_error(std::string{"Snapshot centered scalar grid value count does not match live grid: "} + grid.name);
            grid.values = snapshot_grid->values;
        }

        for (StaggeredVectorGrid& grid : this->staggered_vector_grids) {
            const StaggeredVectorGrid* snapshot_grid = nullptr;
            for (const StaggeredVectorGrid& candidate : snapshot.staggered_vector_grids) {
                if (candidate.name == grid.name) {
                    snapshot_grid = &candidate;
                    break;
                }
            }
            if (snapshot_grid == nullptr) throw std::runtime_error(std::string{"Snapshot is missing staggered vector grid: "} + grid.name);
            if (snapshot_grid->resolution != grid.resolution) throw std::runtime_error(std::string{"Snapshot staggered vector grid resolution does not match live grid: "} + grid.name);
            if (snapshot_grid->x_values.size() != grid.x_values.size()) throw std::runtime_error(std::string{"Snapshot staggered vector grid x-face value count does not match live grid: "} + grid.name);
            if (snapshot_grid->y_values.size() != grid.y_values.size()) throw std::runtime_error(std::string{"Snapshot staggered vector grid y-face value count does not match live grid: "} + grid.name);
            if (snapshot_grid->z_values.size() != grid.z_values.size()) throw std::runtime_error(std::string{"Snapshot staggered vector grid z-face value count does not match live grid: "} + grid.name);
            grid.x_values = snapshot_grid->x_values;
            grid.y_values = snapshot_grid->y_values;
            grid.z_values = snapshot_grid->z_values;
        }
    }
} // namespace xayah
