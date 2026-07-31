module spectra.pathtracer;

import spectra.pathtracer.abi;
import std;

namespace spectra::pathtracer {
    namespace {
        static_assert(sizeof(vk::TraceRaysIndirectCommand2KHR) == 104);
        constexpr vk::DeviceSize indirect_width_offset = sizeof(vk::DeviceAddress) * 11;

        [[nodiscard]] constexpr vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        [[nodiscard]] vk::raii::ShaderModule create_shader_module(const vk::raii::Device& device, const std::filesystem::path& path) {
            const std::vector<std::uint32_t> words = load_spirv(path);
            return vk::raii::ShaderModule{device, vk::ShaderModuleCreateInfo{{}, words.size() * sizeof(std::uint32_t), words.data()}};
        }

        [[nodiscard]] vk::raii::ShaderEXT create_compute_shader(const GpuDevice& gpu, const std::filesystem::path& path, const char* entry) {
            const std::vector<std::uint32_t> code = load_spirv(path);
            const vk::ShaderCreateInfoEXT create_info{
                vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eCompute,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                code.size() * sizeof(std::uint32_t),
                code.data(),
                entry,
            };
            return vk::raii::ShaderEXT{gpu.device, create_info};
        }
    } // namespace

    WavefrontIntegrator::WavefrontIntegrator(GpuDevice& gpu, const std::filesystem::path& shader_directory) : gpu(&gpu), shader_directory(shader_directory), generate_camera_rays(create_compute_shader(gpu, shader_directory / "path_generate_camera_rays.spv", "generate_camera_rays")), evaluate_surface_textures(create_compute_shader(gpu, shader_directory / "path_evaluate_surface_textures.spv", "evaluate_surface_textures")), shade_surfaces(create_compute_shader(gpu, shader_directory / "path_shade_surfaces.spv", "shade_surfaces")), resolve_visibility(create_compute_shader(gpu, shader_directory / "path_resolve_visibility.spv", "resolve_visibility")), accumulate_film(create_compute_shader(gpu, shader_directory / "path_accumulate_film.spv", "accumulate_film")) {
        this->create_ray_tracing_pipeline(shader_directory);
        this->create_shader_binding_table();
    }

    WavefrontIntegrator::~WavefrontIntegrator() = default;

    void WavefrontIntegrator::create_ray_tracing_pipeline(const std::filesystem::path& shader_directory) {
        const vk::raii::ShaderModule surface_ray_generation           = create_shader_module(this->gpu->device, shader_directory / "path_surface_ray_generation.spv");
        const vk::raii::ShaderModule shadow_ray_generation            = create_shader_module(this->gpu->device, shader_directory / "path_shadow_ray_generation.spv");
        const vk::raii::ShaderModule radiance_miss                    = create_shader_module(this->gpu->device, shader_directory / "path_radiance_miss.spv");
        const vk::raii::ShaderModule shadow_miss                      = create_shader_module(this->gpu->device, shader_directory / "path_shadow_miss.spv");
        const vk::raii::ShaderModule closest_hit                      = create_shader_module(this->gpu->device, shader_directory / "path_closest_hit.spv");
        const vk::raii::ShaderModule alpha_any_hit_surface            = create_shader_module(this->gpu->device, shader_directory / "path_alpha_any_hit_surface.spv");
        const vk::raii::ShaderModule alpha_any_hit_shadow             = create_shader_module(this->gpu->device, shader_directory / "path_alpha_any_hit_shadow.spv");
        const vk::raii::ShaderModule shadow_closest_hit               = create_shader_module(this->gpu->device, shader_directory / "path_shadow_closest_hit.spv");
        const vk::raii::ShaderModule procedural_intersection          = create_shader_module(this->gpu->device, shader_directory / "path_procedural_intersection.spv");
        const vk::raii::ShaderModule procedural_closest_hit           = create_shader_module(this->gpu->device, shader_directory / "path_procedural_closest_hit.spv");
        const vk::raii::ShaderModule procedural_alpha_any_hit_surface = create_shader_module(this->gpu->device, shader_directory / "path_procedural_alpha_any_hit_surface.spv");
        const vk::raii::ShaderModule procedural_alpha_any_hit_shadow  = create_shader_module(this->gpu->device, shader_directory / "path_procedural_alpha_any_hit_shadow.spv");
        const vk::raii::ShaderModule procedural_shadow_closest_hit    = create_shader_module(this->gpu->device, shader_directory / "path_procedural_shadow_closest_hit.spv");

        vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
        acceleration_structure_source.pushAddressOffset = 0;
        const vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{
            0,
            0,
            1,
            vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure,
            vk::DescriptorMappingSourceEXT::ePushAddress,
            acceleration_structure_source,
        };
        const vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{acceleration_structure_mapping};
        vk::PipelineShaderStageCreateInfo surface_ray_generation_stage{
            {},
            vk::ShaderStageFlagBits::eRaygenKHR,
            *surface_ray_generation,
            "surface_ray_generation",
        };
        surface_ray_generation_stage.pNext = &mapping;
        vk::PipelineShaderStageCreateInfo shadow_ray_generation_stage{
            {},
            vk::ShaderStageFlagBits::eRaygenKHR,
            *shadow_ray_generation,
            "shadow_ray_generation",
        };
        shadow_ray_generation_stage.pNext = &mapping;
        const std::array general_stages{
            surface_ray_generation_stage,
            shadow_ray_generation_stage,
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eMissKHR, *radiance_miss, "radiance_miss"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eMissKHR, *shadow_miss, "shadow_miss"},
        };
        const std::array general_groups{
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 2, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 3, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
        };
        const vk::RayTracingPipelineInterfaceCreateInfoKHR pipeline_interface{sizeof(float) * 32u, sizeof(float) * 2u};
        const vk::PipelineCreateFlags2CreateInfo general_flags{
            vk::PipelineCreateFlagBits2::eLibraryKHR | vk::PipelineCreateFlagBits2::eDescriptorHeapEXT,
        };
        const vk::RayTracingPipelineCreateInfoKHR general_create_info{
            {},
            general_stages,
            general_groups,
            1,
            nullptr,
            &pipeline_interface,
            nullptr,
            {},
            {},
            0,
            &general_flags,
        };
        this->ray_generation_library = this->gpu->device.createRayTracingPipelineKHR(nullptr, nullptr, general_create_info);

        const std::array hit_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *closest_hit, "closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *alpha_any_hit_surface, "alpha_any_hit_surface"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *alpha_any_hit_shadow, "alpha_any_hit_shadow"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *shadow_closest_hit, "shadow_closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *procedural_closest_hit, "procedural_closest_hit"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eIntersectionKHR, *procedural_intersection, "procedural_intersection"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *procedural_alpha_any_hit_surface, "procedural_alpha_any_hit_surface"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eAnyHitKHR, *procedural_alpha_any_hit_shadow, "procedural_alpha_any_hit_shadow"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eClosestHitKHR, *procedural_shadow_closest_hit, "procedural_shadow_closest_hit"},
        };
        const std::array hit_groups{
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 0, 1, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 4, 6, 5},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 3, 2, vk::ShaderUnusedKHR},
            vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 8, 7, 5},
        };
        const vk::PipelineCreateFlags2CreateInfo hit_flags{
            vk::PipelineCreateFlagBits2::eLibraryKHR | vk::PipelineCreateFlagBits2::eDescriptorHeapEXT,
        };
        const vk::RayTracingPipelineCreateInfoKHR hit_create_info{
            {},
            hit_stages,
            hit_groups,
            1,
            nullptr,
            &pipeline_interface,
            nullptr,
            {},
            {},
            0,
            &hit_flags,
        };
        this->hit_library = this->gpu->device.createRayTracingPipelineKHR(nullptr, nullptr, hit_create_info);

        const std::array<vk::Pipeline, 2> libraries{*this->ray_generation_library, *this->hit_library};
        const vk::PipelineLibraryCreateInfoKHR library_info{libraries};
        constexpr std::array dynamic_states{vk::DynamicState::eRayTracingPipelineStackSizeKHR};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, dynamic_states};
        const vk::PipelineCreateFlags2CreateInfo final_flags{vk::PipelineCreateFlagBits2::eDescriptorHeapEXT};
        const vk::RayTracingPipelineCreateInfoKHR final_create_info{
            {},
            {},
            {},
            1,
            &library_info,
            &pipeline_interface,
            &dynamic_state,
            {},
            {},
            0,
            &final_flags,
        };
        this->pipeline = this->gpu->device.createRayTracingPipelineKHR(nullptr, nullptr, final_create_info);

        const vk::DeviceSize surface_stack                         = this->pipeline.getRayTracingShaderGroupStackSizeKHR(0, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize shadow_stack                          = this->pipeline.getRayTracingShaderGroupStackSizeKHR(1, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize radiance_miss_stack                   = this->pipeline.getRayTracingShaderGroupStackSizeKHR(2, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize shadow_miss_stack                     = this->pipeline.getRayTracingShaderGroupStackSizeKHR(3, vk::ShaderGroupShaderKHR::eGeneral);
        const vk::DeviceSize triangle_surface_closest_hit_stack    = this->pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize triangle_surface_any_hit_stack        = this->pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize procedural_surface_closest_hit_stack  = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize procedural_surface_intersection_stack = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eIntersection);
        const vk::DeviceSize procedural_surface_any_hit_stack      = this->pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize triangle_shadow_closest_hit_stack     = this->pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize triangle_shadow_any_hit_stack         = this->pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eAnyHit);
        const vk::DeviceSize procedural_shadow_closest_hit_stack   = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eClosestHit);
        const vk::DeviceSize procedural_shadow_intersection_stack  = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eIntersection);
        const vk::DeviceSize procedural_shadow_any_hit_stack       = this->pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eAnyHit);
        this->stack_size                                           = static_cast<std::uint32_t>(std::max(surface_stack, shadow_stack) + std::max({radiance_miss_stack, shadow_miss_stack, triangle_surface_closest_hit_stack, triangle_surface_any_hit_stack, procedural_surface_closest_hit_stack, procedural_surface_intersection_stack, procedural_surface_any_hit_stack, triangle_shadow_closest_hit_stack, triangle_shadow_any_hit_stack, procedural_shadow_closest_hit_stack, procedural_shadow_intersection_stack, procedural_shadow_any_hit_stack}));
    }

    void WavefrontIntegrator::create_shader_binding_table() {
        const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& properties = this->gpu->ray_tracing_properties;
        constexpr std::uint32_t group_count                                 = 8;
        const std::vector<std::byte> handles                                = this->pipeline.getRayTracingShaderGroupHandlesKHR<std::byte>(0, group_count, static_cast<std::size_t>(properties.shaderGroupHandleSize) * group_count);
        const vk::DeviceSize record_stride                                  = align_up(properties.shaderGroupHandleSize, properties.shaderGroupHandleAlignment);
        const vk::DeviceSize ray_generation_offset                          = 0;
        const vk::DeviceSize miss_offset                                    = align_up(record_stride * 2u, properties.shaderGroupBaseAlignment);
        const vk::DeviceSize hit_offset                                     = align_up(miss_offset + record_stride * 2u, properties.shaderGroupBaseAlignment);
        const vk::DeviceSize table_size                                     = hit_offset + record_stride * 4u;
        this->shader_binding_table                                          = this->gpu->create_buffer(table_size, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        std::byte* destination                                              = static_cast<std::byte*>(this->shader_binding_table.mapped);
        for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + ray_generation_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * group, properties.shaderGroupHandleSize);
        for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + miss_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * (group + 2), properties.shaderGroupHandleSize);
        for (std::uint32_t group = 0; group != 4; ++group) std::memcpy(destination + hit_offset + record_stride * group, handles.data() + properties.shaderGroupHandleSize * (group + 4), properties.shaderGroupHandleSize);

        this->surface_ray_generation_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + ray_generation_offset,
            record_stride,
            record_stride,
        };
        this->shadow_ray_generation_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + ray_generation_offset + record_stride,
            record_stride,
            record_stride,
        };
        this->miss_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + miss_offset,
            record_stride,
            record_stride * 2u,
        };
        this->hit_region = vk::StridedDeviceAddressRegionKHR{
            this->shader_binding_table.address + hit_offset,
            record_stride,
            record_stride * 4u,
        };
    }

    void WavefrontIntegrator::configure_indirect_commands(PathRenderSession& session) const {
        const std::array commands{
            vk::TraceRaysIndirectCommand2KHR{this->surface_ray_generation_region.deviceAddress, this->surface_ray_generation_region.size, this->miss_region.deviceAddress, this->miss_region.size, this->miss_region.stride, this->hit_region.deviceAddress, this->hit_region.size, this->hit_region.stride, 0, 0, 0, 0, 1, 1},
            vk::TraceRaysIndirectCommand2KHR{this->shadow_ray_generation_region.deviceAddress, this->shadow_ray_generation_region.size, this->miss_region.deviceAddress, this->miss_region.size, this->miss_region.stride, this->hit_region.deviceAddress, this->hit_region.size, this->hit_region.stride, 0, 0, 0, 0, 1, 1},
        };
        std::memcpy(session.indirect_commands.mapped, commands.data(), sizeof(commands));
        session.indirect_commands_configured = true;
    }

    void WavefrontIntegrator::record(const PathScene& scene, PathRenderSession& session, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (!session.indirect_commands_configured) this->configure_indirect_commands(session);
        const scene::CameraResource& camera    = scene.scene_camera;
        const std::array<float, 16>& transform = camera.transform.matrix;
        WavefrontParameters parameters{
            {transform[0], transform[1], transform[2], transform[3]},
            {transform[4], transform[5], transform[6], transform[7]},
            {transform[8], transform[9], transform[10], transform[11]},
        };
        const scene::Transform camera_inverse = camera.transform.inverse();
        parameters.camera_inverse_row_0       = {camera_inverse.matrix[0], camera_inverse.matrix[1], camera_inverse.matrix[2], camera_inverse.matrix[3]};
        parameters.camera_inverse_row_1       = {camera_inverse.matrix[4], camera_inverse.matrix[5], camera_inverse.matrix[6], camera_inverse.matrix[7]};
        parameters.camera_inverse_row_2       = {camera_inverse.matrix[8], camera_inverse.matrix[9], camera_inverse.matrix[10], camera_inverse.matrix[11]};
        std::visit(
            [&parameters](const auto& data) {
                parameters.camera_screen = {data.screen.minimum.x, data.screen.minimum.y, data.screen.maximum.x, data.screen.maximum.y};
                parameters.camera_lens   = {data.lens_radius, data.focal_distance, data.near_plane, data.far_plane};
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                    parameters.camera_projection[0] = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    parameters.camera_metadata[0]   = 0;
                } else
                    parameters.camera_metadata[0] = 1;
            },
            camera.data);
        parameters.camera_metadata[3]          = scene.camera_medium_index;
        const scene::Film& film                = scene.filter.description;
        parameters.filter_parameters_0         = {film.filter.radius.x, film.filter.radius.y, film.filter.sigma, film.exposure};
        parameters.filter_parameters_1         = {film.filter.b, film.filter.c, film.filter.tau, scene.filter.absolute_integral};
        parameters.filter_metadata             = {std::to_underlying(film.filter.kind), scene.filter.resolution[0], scene.filter.resolution[1], 0};
        parameters.filter_distribution         = scene.filter.distribution.descriptor;
        const scene::Sampler& sampler          = scene.sampler.description;
        parameters.sampling_tables             = scene.sampler.tables.descriptor;
        parameters.pmj_pixel_samples           = scene.sampler.pixel_samples.descriptor;
        parameters.sampler_metadata            = {std::to_underlying(sampler.kind), std::to_underlying(sampler.randomization), sampler.samples_per_pixel, sampler.seed};
        parameters.sampler_parameters          = {sampler.x_strata, sampler.y_strata, sampler.jitter ? 1u : 0u, scene.sampler.pixel_tile_size};
        parameters.film_bounds                 = {0, 0, session.extent.width, session.extent.height};
        parameters.film_sensor_response        = scene.filter.sensor_response.descriptor;
        parameters.film_sensor_to_output_row_0 = {film.sensor_to_output_rgb[0], film.sensor_to_output_rgb[1], film.sensor_to_output_rgb[2], 0.0f};
        parameters.film_sensor_to_output_row_1 = {film.sensor_to_output_rgb[3], film.sensor_to_output_rgb[4], film.sensor_to_output_rgb[5], 0.0f};
        parameters.film_sensor_to_output_row_2 = {film.sensor_to_output_rgb[6], film.sensor_to_output_rgb[7], film.sensor_to_output_rgb[8], 0.0f};
        parameters.film_parameters             = {
            camera.exposure_time * film.iso / 100.0f,
            film.maximum_component_value.value_or(std::numeric_limits<float>::infinity()),
            0.0f,
            0.0f,
        };
        parameters.film_metadata = {
            film.gbuffer ? 1u : 0u,
            film.gbuffer_camera_space ? 1u : 0u,
            std::to_underlying(film.color_space),
            0,
        };
        parameters.extent       = {session.extent.width, session.extent.height};
        parameters.sample_index = session.sample_index;
        parameters.max_depth    = scene.transport_settings.maximum_depth;
        parameters.light_count  = scene.lights;
        parameters.reserved     = scene.transport_settings.regularize ? 1u : 0u;
        std::memcpy(session.parameters[frame_index].buffer.mapped, &parameters, sizeof(parameters));
        WavefrontPushData push_data{
            scene.shared_assets->top_level_acceleration_structure.address,
            session.bindings.descriptor,
            session.parameters[frame_index].descriptor,
            scene.bindings_table.descriptor,
            0,
            0,
        };

        command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, 0, sizeof(std::uint32_t) * 3, 0);
        const vk::MemoryBarrier2 initial_memory{
            vk::PipelineStageFlagBits2::eHost | vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        const vk::ImageMemoryBarrier2 to_general{
            session.output_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eComputeShader,
            session.output_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            session.output_layout,
            vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *session.output_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &initial_memory, 0, nullptr, 1, &to_general});
        this->gpu->bind_descriptor_heaps(command_buffer);
        this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->generate_camera_rays);
        const std::uint32_t group_count = (session.capacity + 255u) / 256u;
        command_buffer.dispatch(group_count, 1, 1);

        std::uint32_t read_queue{};
        for (std::uint32_t bounce = 0; bounce <= scene.transport_settings.maximum_depth; ++bounce) {
            push_data.bounce                = bounce;
            push_data.read_queue            = read_queue;
            const std::uint32_t write_queue = 1u - read_queue;
            command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, static_cast<vk::DeviceSize>(write_queue) * sizeof(std::uint32_t), sizeof(std::uint32_t), 0);
            command_buffer.fillBuffer(*session.queue_counts.buffer.buffer, sizeof(std::uint32_t) * 2, sizeof(std::uint32_t), 0);
            const vk::MemoryBarrier2 queue_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &queue_to_copy});
            const vk::BufferCopy surface_width_copy{
                static_cast<vk::DeviceSize>(read_queue) * sizeof(std::uint32_t),
                indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*session.queue_counts.buffer.buffer, *session.indirect_commands.buffer, surface_width_copy);
            const vk::MemoryBarrier2 surface_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->pipeline);
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->stack_size);
            command_buffer.traceRaysIndirect2KHR(session.indirect_commands.address);

            const vk::MemoryBarrier2 surface_to_shade{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_to_shade});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->evaluate_surface_textures);
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            const vk::MemoryBarrier2 texture_values_ready{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &texture_values_ready});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->shade_surfaces);
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);

            const vk::MemoryBarrier2 shadow_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_to_copy});
            const vk::BufferCopy shadow_width_copy{
                sizeof(std::uint32_t) * 2,
                sizeof(vk::TraceRaysIndirectCommand2KHR) + indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*session.queue_counts.buffer.buffer, *session.indirect_commands.buffer, shadow_width_copy);
            const vk::MemoryBarrier2 shadow_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->pipeline);
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->stack_size);
            command_buffer.traceRaysIndirect2KHR(session.indirect_commands.address + sizeof(vk::TraceRaysIndirectCommand2KHR));
            const vk::MemoryBarrier2 visibility_ready{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &visibility_ready});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resolve_visibility);
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            read_queue = write_queue;
        }

        const vk::MemoryBarrier2 film_ready{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &film_ready});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->accumulate_film);
        this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(group_count, 1, 1);
        session.output_layout = vk::ImageLayout::eGeneral;
        ++session.sample_index;
    }
} // namespace spectra::pathtracer
