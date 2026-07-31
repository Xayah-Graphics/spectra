module spectra.pathtracer;

import spectra.pathtracer.abi;
import std;

namespace spectra::pathtracer {
    namespace {
        [[nodiscard]] GpuBuffer create_storage_buffer(GpuDevice& gpu, const vk::DeviceSize size, const vk::BufferUsageFlags additional_usage = {}) {
            return gpu.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | additional_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        }

    } // namespace

    PathRenderSession::PathRenderSession(GpuDevice& gpu, const std::uint32_t frames_in_flight)
        : gpu(&gpu), frame_count(frames_in_flight), output_descriptor(gpu.allocate_resource_descriptor()), sampled_output_descriptor(gpu.allocate_resource_descriptor()), queue_counts(gpu.allocate_resource_descriptor()), ray_queue_0(gpu.allocate_resource_descriptor()), ray_queue_1(gpu.allocate_resource_descriptor()), ray_origins(gpu.allocate_resource_descriptor()), ray_directions(gpu.allocate_resource_descriptor()), ray_origin_dx(gpu.allocate_resource_descriptor()), ray_origin_dy(gpu.allocate_resource_descriptor()), ray_direction_dx(gpu.allocate_resource_descriptor()), ray_direction_dy(gpu.allocate_resource_descriptor()), throughputs(gpu.allocate_resource_descriptor()), radiances(gpu.allocate_resource_descriptor()), wavelengths(gpu.allocate_resource_descriptor()), wavelength_pdfs(gpu.allocate_resource_descriptor()),
          path_r_u(gpu.allocate_resource_descriptor()), path_r_l(gpu.allocate_resource_descriptor()), current_media(gpu.allocate_resource_descriptor()), light_context_normals(gpu.allocate_resource_descriptor()), path_flags(gpu.allocate_resource_descriptor()), eta_scales(gpu.allocate_resource_descriptor()), hit_normal_distances(gpu.allocate_resource_descriptor()), hit_geometric_normal_u(gpu.allocate_resource_descriptor()), hit_tangent_v(gpu.allocate_resource_descriptor()), hit_dpdu(gpu.allocate_resource_descriptor()), hit_dpdv(gpu.allocate_resource_descriptor()), hit_dndu(gpu.allocate_resource_descriptor()), hit_dndv(gpu.allocate_resource_descriptor()), hit_identifiers(gpu.allocate_resource_descriptor()), shadow_path_ids(gpu.allocate_resource_descriptor()), shadow_origins(gpu.allocate_resource_descriptor()),
          shadow_directions(gpu.allocate_resource_descriptor()), shadow_contributions(gpu.allocate_resource_descriptor()), shadow_r_p(gpu.allocate_resource_descriptor()), shadow_pdfs(gpu.allocate_resource_descriptor()), shadow_media(gpu.allocate_resource_descriptor()), texture_evaluation_stack(gpu.allocate_resource_descriptor()), evaluated_texture_values(gpu.allocate_resource_descriptor()), filter_weights(gpu.allocate_resource_descriptor()), film_rgb_sums(gpu.allocate_resource_descriptor()), film_weight_sums(gpu.allocate_resource_descriptor()), gbuffer_sample_albedo(gpu.allocate_resource_descriptor()), gbuffer_sample_shading_normal(gpu.allocate_resource_descriptor()), gbuffer_sample_geometric_normal(gpu.allocate_resource_descriptor()),
          gbuffer_sample_position_depth(gpu.allocate_resource_descriptor()), gbuffer_sample_uv(gpu.allocate_resource_descriptor()), gbuffer_sample_identity_0(gpu.allocate_resource_descriptor()), gbuffer_sample_identity_1(gpu.allocate_resource_descriptor()), gbuffer_albedo_sums(gpu.allocate_resource_descriptor()), gbuffer_shading_normal_sums(gpu.allocate_resource_descriptor()), gbuffer_geometric_normal_sums(gpu.allocate_resource_descriptor()), gbuffer_position_depth_sums(gpu.allocate_resource_descriptor()), gbuffer_uv_weight_sums(gpu.allocate_resource_descriptor()), gbuffer_identity_0(gpu.allocate_resource_descriptor()), gbuffer_identity_1(gpu.allocate_resource_descriptor()), bindings(gpu.allocate_resource_descriptor()) {
        this->parameters.reserve(frames_in_flight);
        for (std::uint32_t index = 0; index != frames_in_flight; ++index) {
            this->parameters.emplace_back(gpu.allocate_resource_descriptor());
            this->parameters.back().buffer = gpu.create_buffer(sizeof(WavefrontParameters), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            gpu.write_buffer(this->parameters.back().descriptor, vk::DescriptorType::eStorageBuffer, this->parameters.back().buffer);
        }
        this->bindings.buffer = gpu.create_buffer(sizeof(WavefrontBindings), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        gpu.write_buffer(this->bindings.descriptor, vk::DescriptorType::eStorageBuffer, this->bindings.buffer);
        const WavefrontBindings binding_data{
            this->output_descriptor,
            this->queue_counts.descriptor,
            this->ray_queue_0.descriptor,
            this->ray_queue_1.descriptor,
            this->ray_origins.descriptor,
            this->ray_directions.descriptor,
            this->ray_origin_dx.descriptor,
            this->ray_origin_dy.descriptor,
            this->ray_direction_dx.descriptor,
            this->ray_direction_dy.descriptor,
            this->throughputs.descriptor,
            this->radiances.descriptor,
            this->wavelengths.descriptor,
            this->wavelength_pdfs.descriptor,
            this->path_r_u.descriptor,
            this->path_r_l.descriptor,
            this->current_media.descriptor,
            this->light_context_normals.descriptor,
            this->path_flags.descriptor,
            this->eta_scales.descriptor,
            this->hit_normal_distances.descriptor,
            this->hit_geometric_normal_u.descriptor,
            this->hit_tangent_v.descriptor,
            this->hit_dpdu.descriptor,
            this->hit_dpdv.descriptor,
            this->hit_dndu.descriptor,
            this->hit_dndv.descriptor,
            this->hit_identifiers.descriptor,
            this->shadow_path_ids.descriptor,
            this->shadow_origins.descriptor,
            this->shadow_directions.descriptor,
            this->shadow_contributions.descriptor,
            this->shadow_r_p.descriptor,
            this->shadow_pdfs.descriptor,
            this->shadow_media.descriptor,
            this->texture_evaluation_stack.descriptor,
            this->evaluated_texture_values.descriptor,
            this->filter_weights.descriptor,
            this->film_rgb_sums.descriptor,
            this->film_weight_sums.descriptor,
            this->gbuffer_sample_albedo.descriptor,
            this->gbuffer_sample_shading_normal.descriptor,
            this->gbuffer_sample_geometric_normal.descriptor,
            this->gbuffer_sample_position_depth.descriptor,
            this->gbuffer_sample_uv.descriptor,
            this->gbuffer_sample_identity_0.descriptor,
            this->gbuffer_sample_identity_1.descriptor,
            this->gbuffer_albedo_sums.descriptor,
            this->gbuffer_shading_normal_sums.descriptor,
            this->gbuffer_geometric_normal_sums.descriptor,
            this->gbuffer_position_depth_sums.descriptor,
            this->gbuffer_uv_weight_sums.descriptor,
            this->gbuffer_identity_0.descriptor,
            this->gbuffer_identity_1.descriptor,
        };
        std::memcpy(this->bindings.buffer.mapped, &binding_data, sizeof(binding_data));
    }

    PathRenderSession::~PathRenderSession() {
        this->gpu->release_resource_descriptor(this->output_descriptor);
        this->gpu->release_resource_descriptor(this->sampled_output_descriptor);
        this->gpu->release_resource_descriptor(this->queue_counts.descriptor);
        this->gpu->release_resource_descriptor(this->ray_queue_0.descriptor);
        this->gpu->release_resource_descriptor(this->ray_queue_1.descriptor);
        this->gpu->release_resource_descriptor(this->ray_origins.descriptor);
        this->gpu->release_resource_descriptor(this->ray_directions.descriptor);
        this->gpu->release_resource_descriptor(this->ray_origin_dx.descriptor);
        this->gpu->release_resource_descriptor(this->ray_origin_dy.descriptor);
        this->gpu->release_resource_descriptor(this->ray_direction_dx.descriptor);
        this->gpu->release_resource_descriptor(this->ray_direction_dy.descriptor);
        this->gpu->release_resource_descriptor(this->throughputs.descriptor);
        this->gpu->release_resource_descriptor(this->radiances.descriptor);
        this->gpu->release_resource_descriptor(this->wavelengths.descriptor);
        this->gpu->release_resource_descriptor(this->wavelength_pdfs.descriptor);
        this->gpu->release_resource_descriptor(this->path_r_u.descriptor);
        this->gpu->release_resource_descriptor(this->path_r_l.descriptor);
        this->gpu->release_resource_descriptor(this->current_media.descriptor);
        this->gpu->release_resource_descriptor(this->light_context_normals.descriptor);
        this->gpu->release_resource_descriptor(this->path_flags.descriptor);
        this->gpu->release_resource_descriptor(this->eta_scales.descriptor);
        this->gpu->release_resource_descriptor(this->hit_normal_distances.descriptor);
        this->gpu->release_resource_descriptor(this->hit_geometric_normal_u.descriptor);
        this->gpu->release_resource_descriptor(this->hit_tangent_v.descriptor);
        this->gpu->release_resource_descriptor(this->hit_dpdu.descriptor);
        this->gpu->release_resource_descriptor(this->hit_dpdv.descriptor);
        this->gpu->release_resource_descriptor(this->hit_dndu.descriptor);
        this->gpu->release_resource_descriptor(this->hit_dndv.descriptor);
        this->gpu->release_resource_descriptor(this->hit_identifiers.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_path_ids.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_origins.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_directions.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_contributions.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_r_p.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_pdfs.descriptor);
        this->gpu->release_resource_descriptor(this->shadow_media.descriptor);
        this->gpu->release_resource_descriptor(this->texture_evaluation_stack.descriptor);
        this->gpu->release_resource_descriptor(this->evaluated_texture_values.descriptor);
        this->gpu->release_resource_descriptor(this->filter_weights.descriptor);
        this->gpu->release_resource_descriptor(this->film_rgb_sums.descriptor);
        this->gpu->release_resource_descriptor(this->film_weight_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_albedo.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_shading_normal.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_geometric_normal.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_position_depth.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_uv.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_identity_0.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_sample_identity_1.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_albedo_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_shading_normal_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_geometric_normal_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_position_depth_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_uv_weight_sums.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_identity_0.descriptor);
        this->gpu->release_resource_descriptor(this->gbuffer_identity_1.descriptor);
        this->gpu->release_resource_descriptor(this->bindings.descriptor);
        for (const GpuBufferBinding& binding : this->parameters) this->gpu->release_resource_descriptor(binding.descriptor);
    }

    void PathRenderSession::resize(const vk::Extent2D extent, const std::uint32_t texture_evaluation_stack_size, const std::uint32_t material_texture_value_count) {
        const std::uint32_t texture_stack_size  = std::max(texture_evaluation_stack_size, 1u);
        const std::uint32_t texture_value_count = std::max(material_texture_value_count, 1u);
        if (this->extent == extent && this->texture_stack_size == texture_stack_size && this->texture_value_count == texture_value_count) return;
        if (this->capacity != 0) this->gpu->wait_idle();
        this->extent              = extent;
        this->texture_stack_size  = texture_stack_size;
        this->texture_value_count = texture_value_count;
        this->capacity            = extent.width * extent.height;
        this->output_image        = this->gpu->create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);
        this->gpu->write_storage_image(this->output_descriptor, this->output_image, vk::ImageLayout::eGeneral);
        this->gpu->write_sampled_image(this->sampled_output_descriptor, this->output_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->queue_counts.buffer                    = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 3, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);
        this->ray_queue_0.buffer                     = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->ray_queue_1.buffer                     = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->ray_origins.buffer                     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->ray_directions.buffer                  = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->ray_origin_dx.buffer                   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->ray_origin_dy.buffer                   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->ray_direction_dx.buffer                = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->ray_direction_dy.buffer                = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->throughputs.buffer                     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->radiances.buffer                       = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->wavelengths.buffer                     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->wavelength_pdfs.buffer                 = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->path_r_u.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->path_r_l.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->current_media.buffer                   = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->light_context_normals.buffer           = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->path_flags.buffer                      = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->eta_scales.buffer                      = create_storage_buffer(*this->gpu, sizeof(float) * this->capacity);
        this->hit_normal_distances.buffer            = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_geometric_normal_u.buffer          = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_tangent_v.buffer                   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_dpdu.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_dpdv.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_dndu.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_dndv.buffer                        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->hit_identifiers.buffer                 = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 4 * this->capacity);
        this->shadow_path_ids.buffer                 = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->shadow_origins.buffer                  = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->shadow_directions.buffer               = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->shadow_contributions.buffer            = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->shadow_r_p.buffer                      = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->shadow_pdfs.buffer                     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->shadow_media.buffer                    = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * this->capacity);
        this->texture_evaluation_stack.buffer        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity * this->texture_stack_size);
        this->evaluated_texture_values.buffer        = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity * this->texture_value_count);
        this->filter_weights.buffer                  = create_storage_buffer(*this->gpu, sizeof(float) * this->capacity);
        this->film_rgb_sums.buffer                   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->film_weight_sums.buffer                = create_storage_buffer(*this->gpu, sizeof(float) * this->capacity);
        this->gbuffer_sample_albedo.buffer           = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_shading_normal.buffer   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_geometric_normal.buffer = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_position_depth.buffer   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_uv.buffer               = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity);
        this->gbuffer_sample_identity_0.buffer       = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 4 * this->capacity);
        this->gbuffer_sample_identity_1.buffer       = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 4 * this->capacity);
        this->gbuffer_albedo_sums.buffer             = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_shading_normal_sums.buffer     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_geometric_normal_sums.buffer   = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_position_depth_sums.buffer     = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_uv_weight_sums.buffer          = create_storage_buffer(*this->gpu, sizeof(float) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_identity_0.buffer              = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->gbuffer_identity_1.buffer              = create_storage_buffer(*this->gpu, sizeof(std::uint32_t) * 4 * this->capacity, vk::BufferUsageFlagBits::eTransferSrc);
        this->indirect_commands               = this->gpu->create_buffer(sizeof(vk::TraceRaysIndirectCommand2KHR) * 2, vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);

        this->gpu->write_buffer(this->queue_counts.descriptor, vk::DescriptorType::eStorageBuffer, this->queue_counts.buffer);
        this->gpu->write_buffer(this->ray_queue_0.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_queue_0.buffer);
        this->gpu->write_buffer(this->ray_queue_1.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_queue_1.buffer);
        this->gpu->write_buffer(this->ray_origins.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origins.buffer);
        this->gpu->write_buffer(this->ray_directions.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_directions.buffer);
        this->gpu->write_buffer(this->ray_origin_dx.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origin_dx.buffer);
        this->gpu->write_buffer(this->ray_origin_dy.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_origin_dy.buffer);
        this->gpu->write_buffer(this->ray_direction_dx.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_direction_dx.buffer);
        this->gpu->write_buffer(this->ray_direction_dy.descriptor, vk::DescriptorType::eStorageBuffer, this->ray_direction_dy.buffer);
        this->gpu->write_buffer(this->throughputs.descriptor, vk::DescriptorType::eStorageBuffer, this->throughputs.buffer);
        this->gpu->write_buffer(this->radiances.descriptor, vk::DescriptorType::eStorageBuffer, this->radiances.buffer);
        this->gpu->write_buffer(this->wavelengths.descriptor, vk::DescriptorType::eStorageBuffer, this->wavelengths.buffer);
        this->gpu->write_buffer(this->wavelength_pdfs.descriptor, vk::DescriptorType::eStorageBuffer, this->wavelength_pdfs.buffer);
        this->gpu->write_buffer(this->path_r_u.descriptor, vk::DescriptorType::eStorageBuffer, this->path_r_u.buffer);
        this->gpu->write_buffer(this->path_r_l.descriptor, vk::DescriptorType::eStorageBuffer, this->path_r_l.buffer);
        this->gpu->write_buffer(this->current_media.descriptor, vk::DescriptorType::eStorageBuffer, this->current_media.buffer);
        this->gpu->write_buffer(this->light_context_normals.descriptor, vk::DescriptorType::eStorageBuffer, this->light_context_normals.buffer);
        this->gpu->write_buffer(this->path_flags.descriptor, vk::DescriptorType::eStorageBuffer, this->path_flags.buffer);
        this->gpu->write_buffer(this->eta_scales.descriptor, vk::DescriptorType::eStorageBuffer, this->eta_scales.buffer);
        this->gpu->write_buffer(this->hit_normal_distances.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_normal_distances.buffer);
        this->gpu->write_buffer(this->hit_geometric_normal_u.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_geometric_normal_u.buffer);
        this->gpu->write_buffer(this->hit_tangent_v.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_tangent_v.buffer);
        this->gpu->write_buffer(this->hit_dpdu.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dpdu.buffer);
        this->gpu->write_buffer(this->hit_dpdv.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dpdv.buffer);
        this->gpu->write_buffer(this->hit_dndu.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dndu.buffer);
        this->gpu->write_buffer(this->hit_dndv.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_dndv.buffer);
        this->gpu->write_buffer(this->hit_identifiers.descriptor, vk::DescriptorType::eStorageBuffer, this->hit_identifiers.buffer);
        this->gpu->write_buffer(this->shadow_path_ids.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_path_ids.buffer);
        this->gpu->write_buffer(this->shadow_origins.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_origins.buffer);
        this->gpu->write_buffer(this->shadow_directions.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_directions.buffer);
        this->gpu->write_buffer(this->shadow_contributions.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_contributions.buffer);
        this->gpu->write_buffer(this->shadow_r_p.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_r_p.buffer);
        this->gpu->write_buffer(this->shadow_pdfs.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_pdfs.buffer);
        this->gpu->write_buffer(this->shadow_media.descriptor, vk::DescriptorType::eStorageBuffer, this->shadow_media.buffer);
        this->gpu->write_buffer(this->texture_evaluation_stack.descriptor, vk::DescriptorType::eStorageBuffer, this->texture_evaluation_stack.buffer);
        this->gpu->write_buffer(this->evaluated_texture_values.descriptor, vk::DescriptorType::eStorageBuffer, this->evaluated_texture_values.buffer);
        this->gpu->write_buffer(this->filter_weights.descriptor, vk::DescriptorType::eStorageBuffer, this->filter_weights.buffer);
        this->gpu->write_buffer(this->film_rgb_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->film_rgb_sums.buffer);
        this->gpu->write_buffer(this->film_weight_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->film_weight_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_albedo.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_albedo.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_shading_normal.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_shading_normal.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_geometric_normal.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_geometric_normal.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_position_depth.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_position_depth.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_uv.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_uv.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_identity_0.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_identity_0.buffer);
        this->gpu->write_buffer(this->gbuffer_sample_identity_1.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_sample_identity_1.buffer);
        this->gpu->write_buffer(this->gbuffer_albedo_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_albedo_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_shading_normal_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_shading_normal_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_geometric_normal_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_geometric_normal_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_position_depth_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_position_depth_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_uv_weight_sums.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_uv_weight_sums.buffer);
        this->gpu->write_buffer(this->gbuffer_identity_0.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_identity_0.buffer);
        this->gpu->write_buffer(this->gbuffer_identity_1.descriptor, vk::DescriptorType::eStorageBuffer, this->gbuffer_identity_1.buffer);
        this->output_layout                = vk::ImageLayout::eUndefined;
        this->sample_index                 = 0;
        this->indirect_commands_configured = false;
    }

    void PathRenderSession::reset() noexcept {
        this->sample_index = 0;
    }

    std::vector<float> PathRenderSession::read_radiance() {
        const vk::DeviceSize readback_size = static_cast<vk::DeviceSize>(this->extent.width) * this->extent.height * sizeof(float) * 4u;
        GpuBuffer readback                 = this->gpu->create_buffer(readback_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->gpu->immediate([this, &readback](const vk::raii::CommandBuffer& command_buffer) {
            const vk::ImageMemoryBarrier2 to_transfer{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eGeneral,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
            const vk::BufferImageCopy copy{
                0,
                0,
                0,
                {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {this->extent.width, this->extent.height, 1},
            };
            command_buffer.copyImageToBuffer(*this->output_image.image, vk::ImageLayout::eTransferSrcOptimal, *readback.buffer, copy);
            const vk::ImageMemoryBarrier2 to_general{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->output_image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_general});
        });
        std::vector<float> pixels(static_cast<std::size_t>(this->extent.width) * this->extent.height * 4u);
        std::memcpy(pixels.data(), readback.mapped, readback_size);
        return pixels;
    }

    render::RenderReadback PathRenderSession::readback() {
        render::RenderReadback result{
            .extent              = this->extent,
            .accumulated_samples = this->sample_index,
        };
        const std::size_t pixel_count = this->capacity;
        result.radiance.resize(pixel_count);
        result.albedo.resize(pixel_count);
        result.shading_normals.resize(pixel_count);
        result.geometric_normals.resize(pixel_count);
        result.positions.resize(pixel_count);
        result.depths.resize(pixel_count);
        result.texture_coordinates.resize(pixel_count);
        result.object_ids.resize(pixel_count);
        result.primitive_ids.resize(pixel_count);
        result.material_ids.resize(pixel_count);
        result.valid.resize(pixel_count);
        if (this->sample_index == 0) return result;

        const std::vector<float> radiance = this->read_radiance();
        static_assert(sizeof(scene::Float4) == sizeof(float) * 4);
        std::memcpy(result.radiance.data(), radiance.data(), radiance.size() * sizeof(float));

        constexpr std::size_t buffer_count = 7;
        const vk::DeviceSize buffer_size   = static_cast<vk::DeviceSize>(pixel_count) * sizeof(scene::Float4);
        GpuBuffer readback                 = this->gpu->create_buffer(buffer_size * buffer_count, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const std::array<const GpuBuffer*, buffer_count> sources{
            &this->gbuffer_albedo_sums.buffer,
            &this->gbuffer_shading_normal_sums.buffer,
            &this->gbuffer_geometric_normal_sums.buffer,
            &this->gbuffer_position_depth_sums.buffer,
            &this->gbuffer_uv_weight_sums.buffer,
            &this->gbuffer_identity_0.buffer,
            &this->gbuffer_identity_1.buffer,
        };
        this->gpu->immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            const vk::MemoryBarrier2 to_transfer{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &to_transfer});
            for (std::size_t index = 0; index != sources.size(); ++index)
                command_buffer.copyBuffer(*sources[index]->buffer, *readback.buffer,
                    vk::BufferCopy{
                        0,
                        buffer_size * index,
                        buffer_size,
                    });
        });
        const auto float_buffer = [&](const std::size_t index) {
            return std::span<const scene::Float4>{
                reinterpret_cast<const scene::Float4*>(static_cast<const std::byte*>(readback.mapped) + buffer_size * index),
                pixel_count,
            };
        };
        const auto integer_buffer = [&](const std::size_t index) {
            return std::span<const std::array<std::uint32_t, 4>>{
                reinterpret_cast<const std::array<std::uint32_t, 4>*>(static_cast<const std::byte*>(readback.mapped) + buffer_size * index),
                pixel_count,
            };
        };
        const std::span<const scene::Float4> albedo_sums               = float_buffer(0);
        const std::span<const scene::Float4> shading_normal_sums       = float_buffer(1);
        const std::span<const scene::Float4> geometric_normal_sums     = float_buffer(2);
        const std::span<const scene::Float4> position_depth_sums       = float_buffer(3);
        const std::span<const scene::Float4> uv_weight_sums            = float_buffer(4);
        const std::span<const std::array<std::uint32_t, 4>> identity_0 = integer_buffer(5);
        const std::span<const std::array<std::uint32_t, 4>> identity_1 = integer_buffer(6);
        const auto normalize                                           = [](const scene::Float4 value) {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            return length == 0.0f ? scene::Float3{} : scene::Float3{value.x / length, value.y / length, value.z / length};
        };
        for (std::size_t index = 0; index != pixel_count; ++index) {
            const float weight = uv_weight_sums[index].z;
            if (weight != 0.0f) {
                result.albedo[index] = {
                    albedo_sums[index].x / weight,
                    albedo_sums[index].y / weight,
                    albedo_sums[index].z / weight,
                };
                result.shading_normals[index]   = normalize(shading_normal_sums[index]);
                result.geometric_normals[index] = normalize(geometric_normal_sums[index]);
                result.positions[index]         = {
                    position_depth_sums[index].x / weight,
                    position_depth_sums[index].y / weight,
                    position_depth_sums[index].z / weight,
                };
                result.depths[index]              = position_depth_sums[index].w / weight;
                result.texture_coordinates[index] = {
                    uv_weight_sums[index].x / weight,
                    uv_weight_sums[index].y / weight,
                };
            }
            result.valid[index]         = identity_1[index][2] != 0 ? 1u : 0u;
            result.object_ids[index]    = static_cast<std::uint64_t>(identity_0[index][0]) | static_cast<std::uint64_t>(identity_0[index][1]) << 32;
            result.primitive_ids[index] = identity_0[index][2];
            result.material_ids[index]  = static_cast<std::uint64_t>(identity_0[index][3]) | static_cast<std::uint64_t>(identity_1[index][0]) << 32;
        }
        return result;
    }

    render::RenderOutput PathRenderSession::output() const noexcept {
        return {
            this->output_image,
            this->sampled_output_descriptor,
            vk::ImageLayout::eGeneral,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
        };
    }
} // namespace spectra::pathtracer
