module spectra.pathtracer.runtime;

import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr std::uint32_t table_resolution         = 64;
        constexpr std::size_t coefficient_count          = 3ull * table_resolution * table_resolution * table_resolution * 3ull;
        constexpr std::uint64_t rgb_table_size           = 16ull + table_resolution * sizeof(float) + coefficient_count * sizeof(float);
        constexpr std::uint64_t cie_table_size           = 16ull + 5ull * 471ull * sizeof(float);
        constexpr std::uint64_t sampling_table_size      = 6'005'504;

        struct ShaderEntry {
            const char* file{};
            const char* entry{};
        };

        constexpr std::array compute_shader_entries{
            ShaderEntry{"volume_majorant.spv", "build_majorant"},
            ShaderEntry{"path_generate_camera_rays.spv", "generate_camera_rays"},
            ShaderEntry{"path_evaluate_surface_textures.spv", "evaluate_surface_textures"},
            ShaderEntry{"path_record_surface_gbuffer.spv", "record_surface_gbuffer"},
            ShaderEntry{"path_sample_direct_lighting.spv", "sample_direct_lighting"},
            ShaderEntry{"path_shade_surfaces.spv", "shade_surfaces"},
            ShaderEntry{"path_resolve_visibility.spv", "resolve_visibility"},
            ShaderEntry{"path_accumulate_film.spv", "accumulate_film"},
        };

        constexpr std::array ray_shader_entries{
            ShaderEntry{"path_surface_ray_generation.spv", "surface_ray_generation"},
            ShaderEntry{"path_shadow_ray_generation.spv", "shadow_ray_generation"},
            ShaderEntry{"path_radiance_miss.spv", "radiance_miss"},
            ShaderEntry{"path_shadow_miss.spv", "shadow_miss"},
            ShaderEntry{"path_closest_hit.spv", "closest_hit"},
            ShaderEntry{"path_alpha_any_hit_surface.spv", "alpha_any_hit_surface"},
            ShaderEntry{"path_alpha_any_hit_shadow.spv", "alpha_any_hit_shadow"},
            ShaderEntry{"path_shadow_closest_hit.spv", "shadow_closest_hit"},
            ShaderEntry{"path_procedural_closest_hit.spv", "procedural_closest_hit"},
            ShaderEntry{"path_procedural_intersection.spv", "procedural_intersection"},
            ShaderEntry{"path_procedural_alpha_any_hit_surface.spv", "procedural_alpha_any_hit_surface"},
            ShaderEntry{"path_procedural_alpha_any_hit_shadow.spv", "procedural_alpha_any_hit_shadow"},
            ShaderEntry{"path_procedural_shadow_closest_hit.spv", "procedural_shadow_closest_hit"},
        };
        struct RayTracingPipelineDescription {
            explicit RayTracingPipelineDescription(const std::span<const vk::ShaderModule, ray_shader_entries.size()> modules) {
                this->acceleration_structure_source.pushAddressOffset = 0;
                this->acceleration_structure_mapping = vk::DescriptorSetAndBindingMappingEXT{0, 0, 1, vk::SpirvResourceTypeFlagBitsEXT::eAccelerationStructure, vk::DescriptorMappingSourceEXT::ePushAddress, this->acceleration_structure_source};
                this->mapping                        = vk::ShaderDescriptorSetAndBindingMappingInfoEXT{this->acceleration_structure_mapping};
                constexpr std::array stages{
                    vk::ShaderStageFlagBits::eRaygenKHR,
                    vk::ShaderStageFlagBits::eRaygenKHR,
                    vk::ShaderStageFlagBits::eMissKHR,
                    vk::ShaderStageFlagBits::eMissKHR,
                    vk::ShaderStageFlagBits::eClosestHitKHR,
                    vk::ShaderStageFlagBits::eAnyHitKHR,
                    vk::ShaderStageFlagBits::eAnyHitKHR,
                    vk::ShaderStageFlagBits::eClosestHitKHR,
                    vk::ShaderStageFlagBits::eClosestHitKHR,
                    vk::ShaderStageFlagBits::eIntersectionKHR,
                    vk::ShaderStageFlagBits::eAnyHitKHR,
                    vk::ShaderStageFlagBits::eAnyHitKHR,
                    vk::ShaderStageFlagBits::eClosestHitKHR,
                };
                for (std::size_t index = 0; index != this->stages.size(); ++index) this->stages[index] = vk::PipelineShaderStageCreateInfo{{}, stages[index], modules[index], ray_shader_entries[index].entry};
                this->stages[0].pNext = &this->mapping;
                this->stages[1].pNext = &this->mapping;
                this->groups = {
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 2, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eGeneral, 3, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 4, 5, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 8, 10, 9},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, vk::ShaderUnusedKHR, 7, 6, vk::ShaderUnusedKHR},
                    vk::RayTracingShaderGroupCreateInfoKHR{vk::RayTracingShaderGroupTypeKHR::eProceduralHitGroup, vk::ShaderUnusedKHR, 12, 11, 9},
                };
                this->flags.flags = vk::PipelineCreateFlagBits2::eDescriptorHeapEXT;
                this->create_info = vk::RayTracingPipelineCreateInfoKHR{{}, this->stages, this->groups, 1, nullptr, nullptr, &this->dynamic_state, {}, {}, 0, &this->flags};
            }

            RayTracingPipelineDescription(const RayTracingPipelineDescription&)            = delete;
            RayTracingPipelineDescription(RayTracingPipelineDescription&&)                 = delete;
            RayTracingPipelineDescription& operator=(const RayTracingPipelineDescription&) = delete;
            RayTracingPipelineDescription& operator=(RayTracingPipelineDescription&&)      = delete;

            vk::DescriptorMappingSourceDataEXT acceleration_structure_source{};
            vk::DescriptorSetAndBindingMappingEXT acceleration_structure_mapping{};
            vk::ShaderDescriptorSetAndBindingMappingInfoEXT mapping{};
            std::array<vk::PipelineShaderStageCreateInfo, ray_shader_entries.size()> stages{};
            std::array<vk::RayTracingShaderGroupCreateInfoKHR, 8> groups{};
            std::array<vk::DynamicState, 1> dynamic_states{vk::DynamicState::eRayTracingPipelineStackSizeKHR};
            vk::PipelineDynamicStateCreateInfo dynamic_state{{}, dynamic_states};
            vk::PipelineCreateFlags2CreateInfo flags{};
            vk::RayTracingPipelineCreateInfoKHR create_info{};
        };

        [[nodiscard]] std::vector<std::uint32_t> load_rgb_tables(const std::filesystem::path& directory) {
            constexpr std::array names{"srgb.rgb2spec", "rec2020.rgb2spec", "aces2065_1.rgb2spec"};
            std::vector<std::uint32_t> data(rgb_table_size / sizeof(std::uint32_t) * names.size());
            for (std::size_t index = 0; index != names.size(); ++index) {
                const std::filesystem::path path = directory / names[index];
                std::ifstream stream{path, std::ios::binary};
                std::error_code error{};
                if (!stream || std::filesystem::file_size(path, error) != rgb_table_size || error) throw std::runtime_error(std::format("Invalid RGB-to-spectrum table: {}", path.string()));
                stream.read(reinterpret_cast<char*>(data.data() + rgb_table_size / sizeof(std::uint32_t) * index), rgb_table_size);
                if (!stream) throw std::runtime_error(std::format("Cannot read RGB-to-spectrum table: {}", path.string()));
            }
            return data;
        }

        [[nodiscard]] std::vector<std::byte> load_binary_asset(const std::filesystem::path& path, const std::uint64_t size) {
            std::ifstream stream{path, std::ios::binary};
            std::error_code error{};
            if (!stream || std::filesystem::file_size(path, error) != size || error) throw std::runtime_error(std::format("Invalid Path Tracer asset: {}", path.string()));
            std::vector<std::byte> data(size);
            stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!stream) throw std::runtime_error(std::format("Cannot read Path Tracer asset: {}", path.string()));
            return data;
        }

        [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
            std::ifstream stream{path, std::ios::binary | std::ios::ate};
            if (!stream) throw std::runtime_error(std::format("Cannot open shader: {}", path.string()));
            const std::streamsize size = stream.tellg();
            std::vector<std::uint32_t> code(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(code.data()), size);
            if (!stream) throw std::runtime_error(std::format("Cannot read shader: {}", path.string()));
            return code;
        }

        [[nodiscard]] vk::raii::ShaderEXT create_compute_shader(const vk::raii::Device& device, const std::span<const std::uint32_t> code, const char* entry) {
            return vk::raii::ShaderEXT{device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size_bytes(), code.data(), entry}};
        }

        [[nodiscard]] vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        [[nodiscard]] std::uint32_t query_stack_size(const vk::raii::Pipeline& pipeline) {
            const std::array general{
                pipeline.getRayTracingShaderGroupStackSizeKHR(0, vk::ShaderGroupShaderKHR::eGeneral),
                pipeline.getRayTracingShaderGroupStackSizeKHR(1, vk::ShaderGroupShaderKHR::eGeneral),
            };
            const std::array shaders{
                pipeline.getRayTracingShaderGroupStackSizeKHR(2, vk::ShaderGroupShaderKHR::eGeneral),
                pipeline.getRayTracingShaderGroupStackSizeKHR(3, vk::ShaderGroupShaderKHR::eGeneral),
                pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eClosestHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(4, vk::ShaderGroupShaderKHR::eAnyHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eClosestHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eIntersection),
                pipeline.getRayTracingShaderGroupStackSizeKHR(5, vk::ShaderGroupShaderKHR::eAnyHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eClosestHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(6, vk::ShaderGroupShaderKHR::eAnyHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eClosestHit),
                pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eIntersection),
                pipeline.getRayTracingShaderGroupStackSizeKHR(7, vk::ShaderGroupShaderKHR::eAnyHit),
            };
            return static_cast<std::uint32_t>(*std::ranges::max_element(general) + *std::ranges::max_element(shaders));
        }

        void join_deferred_operation(const vk::raii::DeferredOperationKHR& operation) {
            while (true) {
                const vk::Result result = operation.join();
                if (result == vk::Result::eSuccess || result == vk::Result::eThreadDoneKHR) return;
                if (result == vk::Result::eThreadIdleKHR) {
                    std::this_thread::yield();
                    continue;
                }
                return;
            }
        }

        void initialize_ray_tracing_pipeline(PathTracerRuntime& runtime, std::array<std::vector<std::uint32_t>, ray_shader_entries.size()> shader_code) {
            std::vector<vk::raii::ShaderModule> shader_modules{};
            shader_modules.reserve(ray_shader_entries.size());
            std::array<vk::ShaderModule, ray_shader_entries.size()> raw_modules{};
            for (std::size_t index = 0; index != ray_shader_entries.size(); ++index) {
                shader_modules.emplace_back(runtime.runtime.graphics.device, vk::ShaderModuleCreateInfo{{}, shader_code[index].size() * sizeof(std::uint32_t), shader_code[index].data()});
                raw_modules[index] = *shader_modules.back();
                runtime.preparation.report(PathTracerPreparationStage::CreatingRayTracingModules, static_cast<std::uint32_t>(index + 1u), static_cast<std::uint32_t>(ray_shader_entries.size()));
            }

            const RayTracingPipelineDescription description{raw_modules};
            const vk::raii::DeferredOperationKHR deferred_operation{runtime.runtime.graphics.device};
            runtime.preparation.report(PathTracerPreparationStage::CompilingRayTracingPipeline);
            const auto [creation_result, pipeline] = (*runtime.runtime.graphics.device).createRayTracingPipelineKHR(
                *deferred_operation,
                {},
                description.create_info,
                nullptr,
                *runtime.runtime.graphics.device.getDispatcher());
            if (creation_result == vk::Result::eOperationDeferredKHR) {
                const std::uint32_t concurrency = std::min(deferred_operation.getMaxConcurrency(), std::max(1u, std::thread::hardware_concurrency()));
                std::vector<std::jthread> workers{};
                workers.reserve(concurrency - 1u);
                for (std::uint32_t index = 1; index != concurrency; ++index) workers.emplace_back([&deferred_operation] { join_deferred_operation(deferred_operation); });
                join_deferred_operation(deferred_operation);
                workers.clear();
                if (deferred_operation.getResult() != vk::Result::eSuccess) throw std::runtime_error("Path Tracer ray tracing pipeline preparation did not complete");
            } else if (creation_result != vk::Result::eSuccess && creation_result != vk::Result::eOperationNotDeferredKHR)
                throw std::runtime_error(std::format("Path Tracer ray tracing pipeline creation failed: {}", vk::to_string(creation_result)));

            runtime.pipeline = vk::raii::Pipeline{runtime.runtime.graphics.device, pipeline};
            const std::uint32_t group_count = static_cast<std::uint32_t>(description.groups.size());
            runtime.shader_group_handles = runtime.pipeline.getRayTracingShaderGroupHandlesKHR<std::byte>(0, group_count, static_cast<std::size_t>(runtime.runtime.graphics.ray_tracing_properties.shaderGroupHandleSize) * group_count);
            runtime.stack_size           = query_stack_size(runtime.pipeline);
        }

        void initialize_shaders(PathTracerRuntime& runtime, const std::filesystem::path& shader_directory) {
            std::array<std::vector<std::uint32_t>, compute_shader_entries.size()> compute_shader_code{};
            std::array<std::vector<std::uint32_t>, ray_shader_entries.size()> ray_shader_code{};
            constexpr std::uint32_t shader_count = static_cast<std::uint32_t>(compute_shader_entries.size() + ray_shader_entries.size());
            std::uint32_t loaded_shader_count{};
            runtime.preparation.report(PathTracerPreparationStage::LoadingShaders, 0, shader_count);
            for (std::size_t index = 0; index != compute_shader_entries.size(); ++index) {
                compute_shader_code[index] = load_spirv(shader_directory / compute_shader_entries[index].file);
                runtime.preparation.report(PathTracerPreparationStage::LoadingShaders, ++loaded_shader_count, shader_count);
            }
            for (std::size_t index = 0; index != ray_shader_entries.size(); ++index) {
                ray_shader_code[index] = load_spirv(shader_directory / ray_shader_entries[index].file);
                runtime.preparation.report(PathTracerPreparationStage::LoadingShaders, ++loaded_shader_count, shader_count);
            }

            std::atomic_uint32_t completed_compute_shaders{};
            std::future<void> compute_preparation = std::async(std::launch::async, [&runtime, shader_code = std::move(compute_shader_code), &completed_compute_shaders] {
                runtime.compute_shaders.reserve(compute_shader_entries.size());
                for (std::size_t index = 0; index != compute_shader_entries.size(); ++index) {
                    runtime.compute_shaders.push_back(create_compute_shader(runtime.runtime.graphics.device, shader_code[index], compute_shader_entries[index].entry));
                    completed_compute_shaders.fetch_add(1u, std::memory_order_relaxed);
                }
            });
            initialize_ray_tracing_pipeline(runtime, std::move(ray_shader_code));
            while (compute_preparation.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
                runtime.preparation.report(PathTracerPreparationStage::CreatingComputeShaders, completed_compute_shaders.load(std::memory_order_relaxed), static_cast<std::uint32_t>(compute_shader_entries.size()));
                std::this_thread::yield();
            }
            compute_preparation.get();
            runtime.preparation.report(PathTracerPreparationStage::CreatingShaderBindingTable);
        }

        void initialize_shader_binding_table(PathTracerRuntime& runtime) {
            const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR& properties = runtime.runtime.graphics.ray_tracing_properties;
            const vk::DeviceSize record_stride = align_up(properties.shaderGroupHandleSize, properties.shaderGroupHandleAlignment);
            const vk::DeviceSize miss_offset   = align_up(record_stride * 2u, properties.shaderGroupBaseAlignment);
            const vk::DeviceSize hit_offset    = align_up(miss_offset + record_stride * 2u, properties.shaderGroupBaseAlignment);
            const vk::DeviceSize table_size    = hit_offset + record_stride * 4u;
            runtime.shader_binding_table = runtime.runtime.resources.create_buffer(table_size, vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::byte* destination = static_cast<std::byte*>(runtime.shader_binding_table.mapped);
            for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + record_stride * group, runtime.shader_group_handles.data() + properties.shaderGroupHandleSize * group, properties.shaderGroupHandleSize);
            for (std::uint32_t group = 0; group != 2; ++group) std::memcpy(destination + miss_offset + record_stride * group, runtime.shader_group_handles.data() + properties.shaderGroupHandleSize * (group + 2), properties.shaderGroupHandleSize);
            for (std::uint32_t group = 0; group != 4; ++group) std::memcpy(destination + hit_offset + record_stride * group, runtime.shader_group_handles.data() + properties.shaderGroupHandleSize * (group + 4), properties.shaderGroupHandleSize);
            runtime.surface_ray_generation_region = vk::StridedDeviceAddressRegionKHR{runtime.shader_binding_table.address, record_stride, record_stride};
            runtime.shadow_ray_generation_region  = vk::StridedDeviceAddressRegionKHR{runtime.shader_binding_table.address + record_stride, record_stride, record_stride};
            runtime.miss_region                   = vk::StridedDeviceAddressRegionKHR{runtime.shader_binding_table.address + miss_offset, record_stride, record_stride * 2u};
            runtime.hit_region                    = vk::StridedDeviceAddressRegionKHR{runtime.shader_binding_table.address + hit_offset, record_stride, record_stride * 4u};
            runtime.shader_group_handles = std::vector<std::byte>{};
        }
    } // namespace

    void PathTracerPreparationState::report(const PathTracerPreparationStage stage, const std::uint32_t completed, const std::uint32_t total) {
        const std::scoped_lock lock{this->mutex};
        if (this->progress.stage != stage) this->progress.started = std::chrono::steady_clock::now();
        this->progress.stage     = stage;
        this->progress.completed = completed;
        this->progress.total     = total;
    }

    PathTracerPreparationProgress PathTracerPreparationState::snapshot() const {
        const std::scoped_lock lock{this->mutex};
        return this->progress;
    }

    const vk::raii::ShaderEXT& PathTracerRuntime::shader(const PathTracerComputeShader shader) const noexcept {
        return this->compute_shaders[std::to_underlying(shader)];
    }

    PathTracerRuntime::PathTracerRuntime(VulkanRuntime& runtime, const std::filesystem::path& resource_directory) : runtime(runtime) {
        const std::filesystem::path shader_directory = resource_directory / "shaders";
        this->shader_preparation = std::async(std::launch::async, [this, shader_directory] { initialize_shaders(*this, shader_directory); });

        const std::filesystem::path spectral_directory = resource_directory / "spectral";
        this->rgb_to_spectrum_table_data = load_rgb_tables(spectral_directory);
        const std::vector<std::byte> cie_table = load_binary_asset(spectral_directory / "cie1931.spectrum", cie_table_size);
        const std::vector<std::byte> sampling_table = load_binary_asset(spectral_directory / "sampling.tables", sampling_table_size);
        this->sampling_table_data.resize(sampling_table.size() / sizeof(std::uint32_t));
        std::memcpy(this->sampling_table_data.data(), sampling_table.data(), sampling_table.size());
        this->cie_samples.resize(5u * 471u);
        std::memcpy(this->cie_samples.data(), cie_table.data() + 16, this->cie_samples.size() * sizeof(float));
        const vk::DeviceSize rgb_offset      = 0;
        const vk::DeviceSize cie_offset      = align_up(this->rgb_to_spectrum_table_data.size() * sizeof(std::uint32_t), 16);
        const vk::DeviceSize sampling_offset = align_up(cie_offset + cie_table.size(), 16);
        const vk::DeviceSize zero_offset     = align_up(sampling_offset + this->sampling_table_data.size() * sizeof(std::uint32_t), 16);
        const vk::DeviceSize total_size      = zero_offset + sizeof(std::uint32_t) * 4u;
        GpuBuffer staging = runtime.resources.create_buffer(total_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        std::memcpy(static_cast<std::byte*>(staging.mapped) + rgb_offset, this->rgb_to_spectrum_table_data.data(), this->rgb_to_spectrum_table_data.size() * sizeof(std::uint32_t));
        std::memcpy(static_cast<std::byte*>(staging.mapped) + cie_offset, cie_table.data(), cie_table.size());
        std::memcpy(static_cast<std::byte*>(staging.mapped) + sampling_offset, this->sampling_table_data.data(), this->sampling_table_data.size() * sizeof(std::uint32_t));
        std::memset(static_cast<std::byte*>(staging.mapped) + zero_offset, 0, sizeof(std::uint32_t) * 4u);
        this->static_data = runtime.resources.create_buffer(total_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            command_buffer.copyBuffer(*staging.buffer, *this->static_data.buffer, vk::BufferCopy{0, 0, total_size});
            const vk::BufferMemoryBarrier2 barrier{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead, {}, {}, *this->static_data.buffer, 0, total_size};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, barrier, {}});
        });
        this->zero_volume_field_descriptor      = runtime.resources.allocate_resource_descriptor();
        this->cie_spectra_descriptor            = runtime.resources.allocate_resource_descriptor();
        this->rgb_to_spectrum_tables_descriptor = runtime.resources.allocate_resource_descriptor();
        this->sampling_tables_descriptor        = runtime.resources.allocate_resource_descriptor();
        runtime.resources.write_buffer_descriptor(this->zero_volume_field_descriptor, vk::DescriptorType::eStorageBuffer, this->static_data.address + zero_offset, sizeof(std::uint32_t) * 4u);
        runtime.resources.write_buffer_descriptor(this->cie_spectra_descriptor, vk::DescriptorType::eStorageBuffer, this->static_data.address + cie_offset, cie_table.size());
        runtime.resources.write_buffer_descriptor(this->rgb_to_spectrum_tables_descriptor, vk::DescriptorType::eStorageBuffer, this->static_data.address + rgb_offset, this->rgb_to_spectrum_table_data.size() * sizeof(std::uint32_t));
        runtime.resources.write_buffer_descriptor(this->sampling_tables_descriptor, vk::DescriptorType::eStorageBuffer, this->static_data.address + sampling_offset, this->sampling_table_data.size() * sizeof(std::uint32_t));
    }

    PathTracerRuntime::~PathTracerRuntime() {
        if (this->shader_preparation.valid()) this->shader_preparation.wait();
        this->runtime.frames.retire_resource_descriptor(this->zero_volume_field_descriptor);
        this->runtime.frames.retire_resource_descriptor(this->cie_spectra_descriptor);
        this->runtime.frames.retire_resource_descriptor(this->rgb_to_spectrum_tables_descriptor);
        this->runtime.frames.retire_resource_descriptor(this->sampling_tables_descriptor);
    }

    bool PathTracerRuntime::complete_preparation() {
        if (!this->shader_preparation.valid()) return true;
        if (this->shader_preparation.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return false;
        this->shader_preparation.get();
        this->preparation.report(PathTracerPreparationStage::CreatingShaderBindingTable);
        initialize_shader_binding_table(*this);
        this->preparation.report(PathTracerPreparationStage::Ready);
        return true;
    }

    void PathTracerRuntime::wait_for_preparation() {
        this->shader_preparation.wait();
        static_cast<void>(this->complete_preparation());
    }

    PathTracerPreparationProgress PathTracerRuntime::preparation_progress() const {
        return this->preparation.snapshot();
    }
} // namespace spectra
