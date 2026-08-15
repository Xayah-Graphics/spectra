module spectra.render.composition.neural_field;

import std;
import vulkan;

namespace spectra {
    namespace {
        struct alignas(16) NeuralFieldPushData {
            DescriptorHandle output{};
            DescriptorHandle depth{};
            DescriptorHandle hash_grid{};
            DescriptorHandle density_input{};
            DescriptorHandle density_output{};
            DescriptorHandle rgb_input{};
            DescriptorHandle rgb_hidden{};
            DescriptorHandle rgb_output{};
            DescriptorHandle occupancy{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<float, 4> camera_clip{};
            std::array<float, 4> inverse_transform_row_0{};
            std::array<float, 4> inverse_transform_row_1{};
            std::array<float, 4> inverse_transform_row_2{};
            std::array<float, 4> camera_position{};
            std::array<float, 4> camera_right{};
            std::array<float, 4> camera_up{};
            std::array<float, 4> camera_forward{};
        };

        static_assert(sizeof(NeuralFieldPushData) == 224);
    } // namespace

    NeuralFieldRenderer::NeuralFieldRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {
        if (!this->context.runtime.graphics.neural_field_supported) return;
        const std::vector<std::uint32_t> code = load_spirv(this->context.shader_directory / "neural_field_render.spv");
        this->render_shader = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), "render_neural_field"}};
    }

    NeuralFieldRenderer::~NeuralFieldRenderer() {
        this->context.runtime.frames.defer_destruction([render = std::move(this->render_shader), model = std::move(this->model)]() mutable {});
    }

    bool NeuralFieldRenderer::has_visible(const scene::SceneView scene) const noexcept {
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        return std::ranges::any_of(scene.resources.neural_fields, [&gpu_scene](const scene::NeuralField& field) {
            const auto gpu = std::ranges::find(gpu_scene.neural_fields, field.id, &GpuNeuralField::neural_field_id);
            return field.visible && gpu != gpu_scene.neural_fields.end() && gpu->revision != 0;
        });
    }

    void NeuralFieldRenderer::record(const vk::raii::CommandBuffer& command_buffer, const ColorCompositionTarget target, DepthBufferView depth, const scene::SceneView scene, const scene::Camera& camera) {
        if (!this->context.runtime.graphics.neural_field_supported) throw std::runtime_error("HashGridRadianceField requires VK_NV_cooperative_vector and shader float16 support");
        const scene::NeuralField& field = *std::ranges::find_if(scene.resources.neural_fields, [](const scene::NeuralField& candidate) { return candidate.visible; });
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        const GpuNeuralField& gpu_field = *std::ranges::find(gpu_scene.neural_fields, field.id, &GpuNeuralField::neural_field_id);
        this->synchronize_model(command_buffer, gpu_field);

        const vk::PipelineStageFlags2 target_stage = target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::PipelineStageFlagBits2::eCopy : target.layout == vk::ImageLayout::eColorAttachmentOptimal ? vk::PipelineStageFlagBits2::eColorAttachmentOutput : vk::PipelineStageFlagBits2::eComputeShader;
        const vk::AccessFlags2 target_access = target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::AccessFlagBits2::eTransferWrite : target.layout == vk::ImageLayout::eColorAttachmentOptimal ? vk::AccessFlagBits2::eColorAttachmentWrite : vk::AccessFlagBits2::eShaderStorageWrite;
        std::vector<vk::ImageMemoryBarrier2> barriers{
            vk::ImageMemoryBarrier2{target_stage, target_access, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite, target.layout, vk::ImageLayout::eGeneral, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        };
        if (depth.layout != vk::ImageLayout::eShaderReadOnlyOptimal) barriers.emplace_back(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead, depth.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *depth.image.image, vk::ImageSubresourceRange{depth.image.aspect, 0, 1, 0, 1});
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(barriers.size()), barriers.data()});
        target.layout = vk::ImageLayout::eGeneral;
        depth.layout  = vk::ImageLayout::eShaderReadOnlyOptimal;

        const math::Transform inverse_transform = field.transform.inverse();
        const std::array<float, 16>& camera_transform = camera.transform.matrix;
        const scene::CameraFrame camera_frame{
            {camera_transform[3], camera_transform[7], camera_transform[11]},
            {camera_transform[0], camera_transform[4], camera_transform[8]},
            {camera_transform[1], camera_transform[5], camera_transform[9]},
            {-camera_transform[2], -camera_transform[6], -camera_transform[10]},
        };
        const bool perspective = std::holds_alternative<scene::PerspectiveCameraData>(camera.data);
        const scene::ScreenWindow& screen_window = std::visit([](const auto& data) -> const scene::ScreenWindow& { return data.screen_window; }, camera.data);
        const float tangent = perspective ? std::tan(std::get<scene::PerspectiveCameraData>(camera.data).vertical_fov * std::numbers::pi_v<float> / 360.0f) : 1.0f;
        const float screen_width = screen_window.maximum.x - screen_window.minimum.x;
        const float screen_height = screen_window.maximum.y - screen_window.minimum.y;
        const float focal_x = static_cast<float>(target.image.extent.width) / (screen_width * tangent);
        const float focal_y = static_cast<float>(target.image.extent.height) / (screen_height * tangent);
        const float principal_x = -screen_window.minimum.x * static_cast<float>(target.image.extent.width) / screen_width;
        const float principal_y = screen_window.maximum.y * static_cast<float>(target.image.extent.height) / screen_height;
        float near_plane{};
        float far_plane{};
        std::visit([&near_plane, &far_plane](const auto& data) {
            near_plane = data.near_plane;
            far_plane  = data.far_plane;
        }, camera.data);
        const NeuralFieldPushData push{
            target.storage_descriptor,
            depth.descriptor,
            gpu_field.hash_grid.descriptor,
            this->model->density_input.descriptor,
            this->model->density_output.descriptor,
            this->model->rgb_input.descriptor,
            this->model->rgb_hidden.descriptor,
            this->model->rgb_output.descriptor,
            gpu_field.occupancy.descriptor,
            {target.image.extent.width, target.image.extent.height, std::to_underlying(target.color_space), perspective ? 1u : 0u},
            {near_plane, far_plane, 0.0f, 0.0f},
            {inverse_transform.matrix[0], inverse_transform.matrix[1], inverse_transform.matrix[2], inverse_transform.matrix[3]},
            {inverse_transform.matrix[4], inverse_transform.matrix[5], inverse_transform.matrix[6], inverse_transform.matrix[7]},
            {inverse_transform.matrix[8], inverse_transform.matrix[9], inverse_transform.matrix[10], inverse_transform.matrix[11]},
            {camera_frame.position.x, camera_frame.position.y, camera_frame.position.z, focal_x},
            {camera_frame.right.x, camera_frame.right.y, camera_frame.right.z, focal_y},
            {camera_frame.up.x, camera_frame.up.y, camera_frame.up.z, principal_x},
            {camera_frame.forward.x, camera_frame.forward.y, camera_frame.forward.z, principal_y},
        };
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->render_shader);
        command_buffer.dispatch((target.image.extent.width + 7u) / 8u, (target.image.extent.height + 7u) / 8u, 1);
    }

    void NeuralFieldRenderer::synchronize_model(const vk::raii::CommandBuffer& command_buffer, const GpuNeuralField& source) {
        if (this->model && this->model->neural_field_id == source.neural_field_id && this->model->revision == source.revision) return;
        Model replacement{};
        replacement.neural_field_id = source.neural_field_id;
        replacement.revision        = source.revision;
        const auto create_matrix = [this](const GpuBuffer& source_buffer, const std::uint32_t rows, const std::uint32_t columns) {
            std::size_t destination_size{};
            const vk::ConvertCooperativeVectorMatrixInfoNV query{
                static_cast<std::size_t>(source_buffer.size),
                {},
                &destination_size,
                {},
                vk::ComponentTypeKHR::eFloat16,
                vk::ComponentTypeKHR::eFloat16,
                rows,
                columns,
                vk::CooperativeVectorMatrixLayoutNV::eRowMajor,
                static_cast<std::size_t>(columns) * sizeof(std::uint16_t),
                vk::CooperativeVectorMatrixLayoutNV::eInferencingOptimal,
                0,
            };
            if (this->context.runtime.graphics.device.convertCooperativeVectorMatrixNV(query) != vk::Result::eSuccess) throw std::runtime_error("Vulkan failed to query cooperative vector matrix size");
            OptimizedMatrix result{};
            result.size       = destination_size;
            result.buffer     = this->context.runtime.resources.create_buffer(destination_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            result.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(result.descriptor, vk::DescriptorType::eStorageBuffer, result.buffer);
            return result;
        };
        replacement.density_input  = create_matrix(source.density_input.buffer, 64, 32);
        replacement.density_output = create_matrix(source.density_output.buffer, 16, 64);
        replacement.rgb_input      = create_matrix(source.rgb_input.buffer, 64, 32);
        replacement.rgb_hidden     = create_matrix(source.rgb_hidden.buffer, 64, 64);
        replacement.rgb_output     = create_matrix(source.rgb_output.buffer, 16, 64);
        std::array<std::size_t, 5> destination_sizes{replacement.density_input.size, replacement.density_output.size, replacement.rgb_input.size, replacement.rgb_hidden.size, replacement.rgb_output.size};
        const std::array<const GpuBuffer*, 5> sources{&source.density_input.buffer, &source.density_output.buffer, &source.rgb_input.buffer, &source.rgb_hidden.buffer, &source.rgb_output.buffer};
        const std::array<OptimizedMatrix*, 5> destinations{&replacement.density_input, &replacement.density_output, &replacement.rgb_input, &replacement.rgb_hidden, &replacement.rgb_output};
        constexpr std::array<std::array<std::uint32_t, 2>, 5> dimensions{{{64, 32}, {16, 64}, {64, 32}, {64, 64}, {16, 64}}};
        std::array<vk::ConvertCooperativeVectorMatrixInfoNV, 5> conversions{};
        for (std::size_t index = 0; index != conversions.size(); ++index)
            conversions[index] = {
                static_cast<std::size_t>(sources[index]->size),
                vk::DeviceOrHostAddressConstKHR{sources[index]->address},
                &destination_sizes[index],
                vk::DeviceOrHostAddressKHR{destinations[index]->buffer.address},
                vk::ComponentTypeKHR::eFloat16,
                vk::ComponentTypeKHR::eFloat16,
                dimensions[index][0],
                dimensions[index][1],
                vk::CooperativeVectorMatrixLayoutNV::eRowMajor,
                static_cast<std::size_t>(dimensions[index][1]) * sizeof(std::uint16_t),
                vk::CooperativeVectorMatrixLayoutNV::eInferencingOptimal,
                0,
            };
        command_buffer.convertCooperativeVectorMatrixNV(conversions);
        const vk::MemoryBarrier2 conversion_dependency{vk::PipelineStageFlagBits2::eConvertCooperativeVectorMatrixNV, vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &conversion_dependency});
        if (this->model) this->context.runtime.frames.defer_destruction([previous = std::move(*this->model)]() mutable {});
        this->model = std::move(replacement);
    }
} // namespace spectra
