module;
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

module scene;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct BoundingBoxShaderParameters {
        std::array<float, 16> model_view_projection{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<float, 4> color{};
    };

    std::uint64_t snapshot_id(const std::variant<xayah::VolumeSnapshot, xayah::MeshSnapshot, xayah::ParticlesSnapshot>& snapshot) {
        return std::visit([](const auto& value) -> std::uint64_t { return value.object_id; }, snapshot);
    }

    xayah::SceneObjectKind snapshot_kind(const std::variant<xayah::VolumeSnapshot, xayah::MeshSnapshot, xayah::ParticlesSnapshot>& snapshot) {
        return std::visit(
            [](const auto& value) -> xayah::SceneObjectKind {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::VolumeSnapshot>) return xayah::SceneObjectKind::volume;
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::MeshSnapshot>) return xayah::SceneObjectKind::mesh;
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::ParticlesSnapshot>) return xayah::SceneObjectKind::particles;
                throw std::runtime_error("Unsupported scene object snapshot kind");
            },
            snapshot);
    }

    const char* kind_label(const xayah::SceneObjectKind kind) {
        if (kind == xayah::SceneObjectKind::volume) return "Volume";
        if (kind == xayah::SceneObjectKind::mesh) return "Mesh";
        if (kind == xayah::SceneObjectKind::particles) return "Particles";
        throw std::runtime_error("Unsupported scene object kind label");
    }
} // namespace

namespace xayah {
    BoundingBoxRenderer::BoundingBoxRenderer() = default;

    BoundingBoxRenderer::~BoundingBoxRenderer() noexcept = default;

    void BoundingBoxRenderer::create(const SceneRenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("Bounding box renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create bounding box renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined || context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create bounding box renderer without swapchain formats");

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

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(BoundingBoxShaderParameters)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
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

    void BoundingBoxRenderer::destroy() noexcept {
        this->pipeline        = nullptr;
        this->pipeline_layout = nullptr;
    }

    void BoundingBoxRenderer::render(const SceneRenderFrameContext& context, const Transform& transform, const BoundingBoxBounds& bounds, const std::array<float, 4>& color) {
        if (context.command_buffer == nullptr) throw std::runtime_error("Bounding box render context is incomplete");
        if (!*this->pipeline_layout || !*this->pipeline) throw std::runtime_error("Bounding box renderer is not initialized");
        BoundingBoxShaderParameters parameters{};
        parameters.model_view_projection = multiply_matrix(context.view_projection, transform_matrix(transform));
        parameters.bounds_min            = {bounds.minimum[0], bounds.minimum[1], bounds.minimum[2], 1.0f};
        parameters.bounds_max            = {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2], 1.0f};
        parameters.color                 = color;
        context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->pipeline);
        context.command_buffer->pushConstants(*this->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const BoundingBoxShaderParameters>{1, &parameters});
        context.command_buffer->draw(24, 1, 0, 0);
    }

    bool BoundingBoxRenderer::active() const {
        return static_cast<bool>(*this->pipeline);
    }

    Scene::Scene() = default;

    Scene::~Scene() noexcept = default;

    void Scene::add(Volume&& object) {
        this->objects.emplace_back(std::move(object));
    }

    void Scene::add(Mesh&& object) {
        this->objects.emplace_back(std::move(object));
    }

    void Scene::add(Particles&& object) {
        this->objects.emplace_back(std::move(object));
    }

    std::size_t Scene::object_count() const {
        return this->objects.size();
    }

    std::size_t Scene::volume_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(this->objects, [](const auto& object) { return std::holds_alternative<Volume>(object); }));
    }

    std::size_t Scene::mesh_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(this->objects, [](const auto& object) { return std::holds_alternative<Mesh>(object); }));
    }

    std::size_t Scene::particles_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(this->objects, [](const auto& object) { return std::holds_alternative<Particles>(object); }));
    }

    SceneObjectRef Scene::object_ref(const std::uint64_t object_id) const {
        if (object_id == 0) throw std::runtime_error("Scene object id must not be zero");
        for (std::size_t index = 0; index < this->objects.size(); ++index) {
            const std::variant<Volume, Mesh, Particles>& object = this->objects[index];
            const bool matches                                  = std::visit([object_id](const auto& value) { return value.id == object_id; }, object);
            if (matches) return SceneObjectRef{std::visit([](const auto& value) { return value.kind(); }, object), index};
        }
        throw std::runtime_error(std::string{"Scene object id does not exist: "} + std::to_string(object_id));
    }

    SceneObjectRef Scene::selected_object_ref() const {
        return this->object_ref(this->selection.object_id);
    }

    void Scene::select_object(const std::uint64_t object_id) {
        static_cast<void>(this->object_ref(object_id));
        this->selection.object_id = object_id;
    }

    bool Scene::has_selection() const {
        return this->selection.object_id != 0;
    }

    bool Scene::selected_object_visible() const {
        const SceneObjectRef reference = this->selected_object_ref();
        return std::visit([](const auto& object) { return object.visible; }, this->objects.at(reference.index));
    }

    Transform& Scene::object_transform(const std::uint64_t object_id) {
        const SceneObjectRef reference = this->object_ref(object_id);
        return std::visit([](auto& object) -> Transform& { return object.transform; }, this->objects.at(reference.index));
    }

    const Transform& Scene::object_transform(const std::uint64_t object_id) const {
        const SceneObjectRef reference = this->object_ref(object_id);
        return std::visit([](const auto& object) -> const Transform& { return object.transform; }, this->objects.at(reference.index));
    }

    Transform& Scene::selected_transform() {
        return this->object_transform(this->selection.object_id);
    }

    const Transform& Scene::selected_transform() const {
        return this->object_transform(this->selection.object_id);
    }

    const char* Scene::selected_kind_label() const {
        return kind_label(this->selected_object_ref().kind);
    }

    void Scene::validate() const {
        if (this->objects.empty()) throw std::runtime_error("Scene has no objects to render");

        std::set<std::uint64_t> object_ids{};
        std::set<std::string> volume_names{};
        std::set<std::string> mesh_names{};
        std::set<std::string> particles_names{};
        for (const std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](const auto& value) {
                    value.validate();
                    if (!object_ids.insert(value.id).second) throw std::runtime_error(std::string{"Duplicate scene object id: "} + std::to_string(value.id));
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>) {
                        if (!volume_names.insert(value.name).second) throw std::runtime_error(std::string{"Duplicate volume name: "} + value.name);
                    } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>) {
                        if (!mesh_names.insert(value.name).second) throw std::runtime_error(std::string{"Duplicate mesh name: "} + value.name);
                    } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Particles>) {
                        if (!particles_names.insert(value.name).second) throw std::runtime_error(std::string{"Duplicate particles name: "} + value.name);
                    }
                },
                object);
        }

        if (this->selection.object_id != 0) static_cast<void>(this->object_ref(this->selection.object_id));
    }

    void Scene::validate_bake() const {
        if (this->bake.mode == ScenePlaybackMode::live) return;
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");

        std::set<int> frame_indices{};
        int frame_min = this->bake.frames.front().frame_index;
        int frame_max = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (!frame_indices.insert(frame.frame_index).second) throw std::runtime_error(std::string{"Duplicate baked frame index: "} + std::to_string(frame.frame_index));
            if (frame.frame_index < frame_min) frame_min = frame.frame_index;
            if (frame.frame_index > frame_max) frame_max = frame.frame_index;
            if (frame.objects.size() != this->objects.size()) throw std::runtime_error(std::string{"Baked frame object count does not match scene object count: "} + std::to_string(frame.frame_index));

            std::set<std::uint64_t> baked_object_ids{};
            for (const std::variant<VolumeSnapshot, MeshSnapshot, ParticlesSnapshot>& snapshot : frame.objects) {
                const std::uint64_t object_id = snapshot_id(snapshot);
                if (object_id == 0) throw std::runtime_error("Baked object id must not be zero");
                if (!baked_object_ids.insert(object_id).second) throw std::runtime_error(std::string{"Duplicate baked object id in frame: "} + std::to_string(object_id));
                const SceneObjectRef reference = this->object_ref(object_id);
                if (reference.kind != snapshot_kind(snapshot)) throw std::runtime_error(std::string{"Baked object kind does not match scene object: "} + std::to_string(object_id));
            }
        }

        for (int frame_index = frame_min; frame_index <= frame_max; ++frame_index) {
            if (!frame_indices.contains(frame_index)) throw std::runtime_error(std::string{"Baked playback is missing frame: "} + std::to_string(frame_index));
        }
    }

    void Scene::initialize_selection() {
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [](auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>) value.initialize_render_settings();
                },
                object);
        }
        if (this->selection.object_id != 0) static_cast<void>(this->selected_object_ref());
    }

    int Scene::baked_frame_min() const {
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");
        int frame_min = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index < frame_min) frame_min = frame.frame_index;
        }
        return frame_min;
    }

    int Scene::baked_frame_max() const {
        if (this->bake.frames.empty()) throw std::runtime_error("Baked playback has no frames");
        int frame_max = this->bake.frames.front().frame_index;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index > frame_max) frame_max = frame.frame_index;
        }
        return frame_max;
    }

    BakedSceneFrame Scene::make_baked_frame(const int frame_index) const {
        BakedSceneFrame frame{};
        frame.frame_index = frame_index;
        frame.objects.reserve(this->objects.size());
        for (const std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit([&](const auto& value) { frame.objects.emplace_back(value.make_snapshot()); }, object);
        }
        return frame;
    }

    void Scene::apply_playback_frame(const int frame_index) {
        if (this->bake.mode == ScenePlaybackMode::live) return;

        const BakedSceneFrame* baked_frame = nullptr;
        for (const BakedSceneFrame& frame : this->bake.frames) {
            if (frame.frame_index == frame_index) {
                baked_frame = &frame;
                break;
            }
        }
        if (baked_frame == nullptr) throw std::runtime_error(std::string{"Baked frame does not exist: "} + std::to_string(frame_index));

        for (const std::variant<VolumeSnapshot, MeshSnapshot, ParticlesSnapshot>& snapshot : baked_frame->objects) {
            const SceneObjectRef reference = this->object_ref(snapshot_id(snapshot));
            std::visit(
                [&](auto& object, const auto& baked_object) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(object)>, Volume> && std::is_same_v<std::remove_cvref_t<decltype(baked_object)>, VolumeSnapshot>)
                        object.apply_snapshot(baked_object);
                    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(object)>, Mesh> && std::is_same_v<std::remove_cvref_t<decltype(baked_object)>, MeshSnapshot>)
                        object.apply_snapshot(baked_object);
                    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(object)>, Particles> && std::is_same_v<std::remove_cvref_t<decltype(baked_object)>, ParticlesSnapshot>)
                        object.apply_snapshot(baked_object);
                    else
                        throw std::runtime_error(std::string{"Baked object type does not match scene object id: "} + std::to_string(snapshot_id(snapshot)));
                },
                this->objects.at(reference.index), snapshot);
        }
    }

    void Scene::create_render_resources(const SceneRenderCreateContext& context) {
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create scene render resources without a Vulkan device");
        this->bounding_box_renderer.create(context);
        if (this->volume_count() != 0) this->volume_renderer.create(context);
        if (this->mesh_count() != 0) this->mesh_renderer.create(context);
        if (this->particles_count() != 0) this->particles_renderer.create(context);
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>)
                        value.create_render_resources(context, this->volume_renderer);
                    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>)
                        value.create_render_resources(context, this->mesh_renderer);
                    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Particles>)
                        value.create_render_resources(context, this->particles_renderer);
                },
                object);
        }
    }

    void Scene::destroy_render_resources() noexcept {
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit([](auto& value) { value.destroy_render_resources(); }, object);
        }
        this->particles_renderer.destroy();
        this->mesh_renderer.destroy();
        this->volume_renderer.destroy();
        this->bounding_box_renderer.destroy();
    }

    void Scene::recreate_render_resources(const SceneRenderCreateContext& context) {
        this->destroy_render_resources();
        this->create_render_resources(context);
    }

    void Scene::render(const SceneRenderFrameContext& context) {
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>) value.render(context, this->mesh_renderer);
                },
                object);
        }
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Particles>) value.render(context, this->particles_renderer);
                },
                object);
        }
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>) value.render(context, this->volume_renderer);
                },
                object);
        }

        constexpr std::array<float, 4> selected_bounding_box_color{1.0f, 0.76f, 0.30f, 0.96f};
        constexpr std::array<float, 4> volume_bounding_box_color{0.28f, 0.70f, 1.0f, 0.90f};
        constexpr std::array<float, 4> mesh_bounding_box_color{0.72f, 0.54f, 1.0f, 0.90f};
        constexpr std::array<float, 4> particles_bounding_box_color{0.36f, 0.92f, 0.68f, 0.90f};
        for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
            std::visit(
                [&](auto& value) {
                    if (!value.visible || !value.render_settings.show_bounding_box) return;
                    std::array<float, 4> color = selected_bounding_box_color;
                    if (this->selection.object_id != value.id) {
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>)
                            color = volume_bounding_box_color;
                        else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>)
                            color = mesh_bounding_box_color;
                        else
                            color = particles_bounding_box_color;
                    }
                    this->bounding_box_renderer.render(context, value.transform, value.bounds(), color);
                },
                object);
        }
    }

    void Scene::draw_hierarchy_ui() {
        constexpr ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        constexpr ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};
        constexpr ImVec4 muted_color{0.70f, 0.76f, 0.82f, 1.0f};

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.42f, 0.72f, 0.24f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.22f, 0.50f, 0.86f, 0.34f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.28f, 0.58f, 0.96f, 0.44f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0f, 3.0f});
        if (ImGui::BeginTable("SceneObjectList", 4, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("BBox", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            for (std::variant<Volume, Mesh, Particles>& object : this->objects) {
                std::visit(
                    [&](auto& value) {
                        const bool selected  = this->selection.object_id == value.id;
                        const std::string id = std::to_string(value.id);
                        std::string label    = std::string{kind_label(value.kind())} + "  " + value.name;
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Volume>)
                            label += "  " + std::to_string(value.centered_scalar_grids.size()) + " scalar, " + std::to_string(value.staggered_vector_grids.size()) + " vector";
                        else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>)
                            label += "  " + std::to_string(value.vertices.size()) + " vertices, " + std::to_string(value.indices.size() / 3) + " tris";
                        else
                            label += "  " + std::to_string(value.particles.size()) + " particles";
                        label += "##SceneObjectSelect:" + id;

                        const std::string visible_label = std::string{value.visible ? "V" : "H"} + "##SceneObjectVisible:" + id;
                        const std::string bbox_label    = std::string{"B##SceneObjectBounds:"} + id;

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : value.visible ? value_color : muted_color);
                        if (ImGui::Selectable(label.c_str(), selected)) this->select_object(value.id);
                        ImGui::PopStyleColor();

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_Border, value.visible ? ImVec4{0.24f, 0.82f, 0.55f, 0.78f} : ImVec4{0.86f, 0.30f, 0.32f, 0.78f});
                        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                        if (ImGui::Button(visible_label.c_str(), ImVec2{26.0f, 22.0f})) value.visible = !value.visible;
                        ImGui::PopStyleColor(5);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(value.visible ? "Hide object" : "Show object");

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                        ImGui::PushStyleColor(ImGuiCol_Border, value.render_settings.show_bounding_box ? ImVec4{0.28f, 0.70f, 1.0f, 0.82f} : ImVec4{0.86f, 0.30f, 0.32f, 0.78f});
                        ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                        if (ImGui::Button(bbox_label.c_str(), ImVec2{26.0f, 22.0f})) value.render_settings.show_bounding_box = !value.render_settings.show_bounding_box;
                        ImGui::PopStyleColor(5);
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip(value.render_settings.show_bounding_box ? "Hide bounding box" : "Show bounding box");

                        ImGui::TableNextColumn();
                        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, Mesh>) {
                            const char* mode_text        = value.render_settings.display_mode == MeshDisplayMode::surface ? "S" : "W";
                            const std::string mode_label = std::string{mode_text} + "##SceneMeshMode:" + id;
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                            ImGui::PushStyleColor(ImGuiCol_Border, value.render_settings.display_mode == MeshDisplayMode::surface ? ImVec4{0.66f, 0.48f, 0.96f, 0.78f} : ImVec4{0.28f, 0.80f, 0.88f, 0.78f});
                            ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                            if (ImGui::Button(mode_label.c_str(), ImVec2{28.0f, 0.0f})) value.render_settings.display_mode = value.render_settings.display_mode == MeshDisplayMode::surface ? MeshDisplayMode::wireframe : MeshDisplayMode::surface;
                            ImGui::PopStyleColor(5);
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip(value.render_settings.display_mode == MeshDisplayMode::surface ? "Surface mesh rendering" : "Wireframe mesh rendering");
                        }
                    },
                    object);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
    }

    void Scene::draw_selected_inspector_ui() {
        if (!this->has_selection()) return;
        const SceneObjectRef active_object = this->selected_object_ref();
        std::visit([](auto& object) { object.draw_inspector_ui(); }, this->objects.at(active_object.index));
    }
} // namespace xayah
