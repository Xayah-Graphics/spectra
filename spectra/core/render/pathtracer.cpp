module spectra.render.pathtracer;

import spectra.render.pathtracer.abi;
import spectra.render.pathtracer.scene;
import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr std::uint32_t invalid_path_index = std::numeric_limits<std::uint32_t>::max();

        struct PathGBufferSnapshot {
            vk::Extent2D extent{};
            std::uint32_t accumulated_samples{};
            GpuBuffer buffer{};
        };

        [[nodiscard]] RenderGBufferReadback materialize_gbuffer_readback(const PathGBufferSnapshot& snapshot) {
            RenderGBufferReadback result{
                .extent              = snapshot.extent,
                .accumulated_samples = snapshot.accumulated_samples,
            };
            const std::size_t pixel_count = static_cast<std::size_t>(snapshot.extent.width) * snapshot.extent.height;
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
            if (snapshot.accumulated_samples == 0) return result;

            const vk::DeviceSize buffer_size = static_cast<vk::DeviceSize>(pixel_count) * sizeof(math::Float4);
            const auto float_buffer = [&](const std::size_t index) {
                return std::span<const math::Float4>{reinterpret_cast<const math::Float4*>(static_cast<const std::byte*>(snapshot.buffer.mapped) + buffer_size * index), pixel_count};
            };
            const auto integer_buffer = [&](const std::size_t index) {
                return std::span<const std::array<std::uint32_t, 4>>{reinterpret_cast<const std::array<std::uint32_t, 4>*>(static_cast<const std::byte*>(snapshot.buffer.mapped) + buffer_size * index), pixel_count};
            };
            std::ranges::copy(float_buffer(0), result.radiance.begin());
            const std::span<const math::Float4> albedo_sums                = float_buffer(1);
            const std::span<const math::Float4> shading_normal_sums        = float_buffer(2);
            const std::span<const math::Float4> geometric_normal_sums      = float_buffer(3);
            const std::span<const math::Float4> position_depth_sums        = float_buffer(4);
            const std::span<const math::Float4> uv_weight_sums             = float_buffer(5);
            const std::span<const std::array<std::uint32_t, 4>> identity_0 = integer_buffer(6);
            const std::span<const std::array<std::uint32_t, 4>> identity_1 = integer_buffer(7);
            const auto normalize                                           = [](const math::Float4 value) {
                const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
                return length == 0.0f ? math::Float3{} : math::Float3{value.x / length, value.y / length, value.z / length};
            };
            for (std::size_t index = 0; index != pixel_count; ++index) {
                const float weight = uv_weight_sums[index].z;
                if (weight != 0.0f) {
                    result.albedo[index]              = {albedo_sums[index].x / weight, albedo_sums[index].y / weight, albedo_sums[index].z / weight};
                    result.shading_normals[index]     = normalize(shading_normal_sums[index]);
                    result.geometric_normals[index]   = normalize(geometric_normal_sums[index]);
                    result.positions[index]           = {position_depth_sums[index].x / weight, position_depth_sums[index].y / weight, position_depth_sums[index].z / weight};
                    result.depths[index]               = position_depth_sums[index].w / weight;
                    result.texture_coordinates[index] = {uv_weight_sums[index].x / weight, uv_weight_sums[index].y / weight};
                }
                result.object_ids[index]    = static_cast<std::uint64_t>(identity_0[index][0]) | static_cast<std::uint64_t>(identity_0[index][1]) << 32;
                result.primitive_ids[index] = identity_0[index][2];
                result.material_ids[index]  = static_cast<std::uint64_t>(identity_0[index][3]) | static_cast<std::uint64_t>(identity_1[index][0]) << 32;
            }
            return result;
        }

        [[nodiscard]] GpuBuffer create_storage_buffer(VulkanRuntime& runtime, const vk::DeviceSize size, const vk::BufferUsageFlags additional_usage = {}) {
            return runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | additional_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        }

        template <class Element>
        [[nodiscard]] GpuBuffer upload_path_buffer(VulkanRuntime& runtime, const std::span<const Element> elements, const vk::raii::CommandBuffer& command_buffer) {
            GpuBuffer destination = create_storage_buffer(runtime, std::max(elements.size_bytes(), sizeof(Element)), vk::BufferUsageFlagBits::eTransferDst);
            if (elements.empty()) return destination;
            const GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(elements));
            command_buffer.copyBuffer(upload.buffer, *destination.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *destination.buffer,
                0,
                destination.size,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &dependency});
            return destination;
        }
        static_assert(sizeof(vk::TraceRaysIndirectCommand2KHR) == 104);
        constexpr vk::DeviceSize indirect_width_offset = sizeof(vk::DeviceAddress) * 11;

    } // namespace

    PathTracer::PathTracer(VulkanRuntime& runtime, GpuScene& gpu_scene, PathTracerResources& pathtracer, const scene::SceneView scene_view) : context{runtime, gpu_scene, pathtracer} {
        this->begin_scene_preparation(scene_view);
    }

    PathTracer::~PathTracer() {
        this->release_scene_preparation();
        this->destroy_session();
        this->destroy_scene();
    }

    bool PathTracer::complete_preparation(const scene::SceneView scene_view, const vk::raii::CommandBuffer& command_buffer) {
        if (this->session.initialized) return true;
        if (this->scene_preparation_task.wait_for(std::chrono::seconds{0}) != std::future_status::ready) return false;
        this->scene_preparation_task.get();
        if (this->scene_preparation->gpu.structure_revision != this->context.gpu_scene.view().structure_revision) {
            this->release_scene_preparation();
            this->destroy_scene();
            this->begin_scene_preparation(scene_view);
            return false;
        }

        this->scene_preparation->progress.report(PathTracerPreparationStage::UploadingScene);
        this->commit_sampler(std::move(this->scene_preparation->sampler), command_buffer);
        this->commit_filter(std::move(this->scene_preparation->filter), command_buffer);
        this->commit_scene(std::move(this->scene_preparation->prepared), command_buffer);
        this->update_volumes(scene_view, command_buffer);
        this->scene.camera             = scene_view.camera;
        this->scene.transport_settings = scene_view.transport;
        this->scene_preparation->progress.report(PathTracerPreparationStage::AllocatingRenderSession);
        this->initialize_session();
        this->scene_preparation->progress.report(PathTracerPreparationStage::Ready);
        this->release_scene_preparation();
        return true;
    }

    void PathTracer::wait_for_preparation() {
        if (this->scene_preparation_task.valid()) this->scene_preparation_task.get();
    }

    PathTracerPreparationProgress PathTracer::preparation_progress() const {
        return this->scene_preparation->progress.snapshot();
    }

    void PathTracer::invalidate(const scene::SceneChange changes, const GpuSceneUpdate gpu_update) noexcept {
        constexpr scene::SceneChange relevant        = scene::SceneChange::Geometry | scene::SceneChange::Transform | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Volume | scene::SceneChange::Camera | scene::SceneChange::Film | scene::SceneChange::Sampler | scene::SceneChange::Transport;
        this->control.pending_changes                = this->control.pending_changes | (changes & relevant);
        this->control.pending_gpu_changes            = this->control.pending_gpu_changes | gpu_update.gpu_changes;
        this->control.pending_gpu_structure_revision = std::max(this->control.pending_gpu_structure_revision, gpu_update.structure_revision);
    }

    void PathTracer::prepare(scene::SceneView scene_view, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        bool reset_required{};
        if (this->control.pending_changes != scene::SceneChange::None) {
            scene_view.revision.changes = this->control.pending_changes;
            this->synchronize_scene(scene_view, command_buffer);
            this->control.pending_changes = scene::SceneChange::None;
            reset_required                = true;
        }
        if (this->control.pending_gpu_changes != GpuSceneChange::None) {
            if ((this->control.pending_gpu_changes & GpuSceneChange::Structure) != GpuSceneChange::None && this->scene.compiled_gpu_structure_revision != this->control.pending_gpu_structure_revision)
                this->compile_scene(scene_view, command_buffer);
            else if ((this->control.pending_gpu_changes & GpuSceneChange::Volume) != GpuSceneChange::None)
                this->update_volumes(scene_view, command_buffer);
            if ((this->control.pending_gpu_changes & (GpuSceneChange::Geometry | GpuSceneChange::Transform)) != GpuSceneChange::None) this->update_dynamic_lights(command_buffer);
            this->control.pending_gpu_changes = GpuSceneChange::None;
            reset_required                    = true;
        }
        if (this->control.camera_revision != view.camera_revision) {
            this->scene.camera = view.camera;
            if (view.camera.medium.value == 0)
                this->scene.camera_medium_index = invalid_path_index;
            else
                this->scene.camera_medium_index = static_cast<std::uint32_t>(std::ranges::find(scene_view.resources.media, view.camera.medium, &scene::Medium::id) - scene_view.resources.media.begin());
            const math::Float3 camera_position = view.camera.frame().position;
            for (std::uint32_t volume_index = 0; volume_index != scene_view.resources.volumes.size(); ++volume_index) {
                const scene::Volume& volume = scene_view.resources.volumes[volume_index];
                if (!volume.visible) continue;
                const math::Float3 local = volume.transform.inverse().transform_point(camera_position);
                if (local.x >= volume.domain.minimum.x && local.x <= volume.domain.maximum.x && local.y >= volume.domain.minimum.y && local.y <= volume.domain.maximum.y && local.z >= volume.domain.minimum.z && local.z <= volume.domain.maximum.z) this->scene.camera_medium_index = static_cast<std::uint32_t>(scene_view.resources.media.size()) + volume_index;
            }
            this->control.camera_revision = view.camera_revision;
            reset_required                = true;
        }
        this->resize_session(view.extent, this->scene.texture_stack_size, this->scene.material_texture_value_count, command_buffer);
        if (reset_required) this->reset();
    }

    void PathTracer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (this->control.paused || this->session.sample_index >= this->scene.sampler.sampler.samples_per_pixel) return;
        this->record_integrator(command_buffer, frame_slot_index);
    }

    RenderProgress PathTracer::progress() const noexcept {
        return {this->session.sample_index, this->scene.sampler.sampler.samples_per_pixel, this->control.paused};
    }

    void PathTracer::set_paused(const bool paused) noexcept {
        this->control.paused = paused;
    }

    void PathTracer::reset() noexcept {
        this->session.sample_index = 0;
    }

    RenderGBufferReadback PathTracer::readback() {
        PathGBufferSnapshot snapshot{};
        snapshot.extent              = this->session.render_extent;
        snapshot.accumulated_samples = this->session.sample_index;
        if (snapshot.accumulated_samples == 0) return materialize_gbuffer_readback(snapshot);
        this->context.runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            constexpr std::size_t arena_buffer_count = 7;
            constexpr std::size_t buffer_count       = arena_buffer_count + 1;
            const vk::DeviceSize buffer_size         = static_cast<vk::DeviceSize>(this->session.pixel_capacity) * sizeof(math::Float4);
            const vk::DeviceSize required_size       = buffer_size * buffer_count;
            if (snapshot.buffer.size < required_size) snapshot.buffer = this->context.runtime.resources.create_buffer(required_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            const std::array<vk::DeviceSize, arena_buffer_count> source_offsets{
                this->session.gbuffer_albedo_sums.offset,
                this->session.gbuffer_shading_normal_sums.offset,
                this->session.gbuffer_geometric_normal_sums.offset,
                this->session.gbuffer_position_depth_sums.offset,
                this->session.gbuffer_uv_weight_sums.offset,
                this->session.gbuffer_identity_0.offset,
                this->session.gbuffer_identity_1.offset,
            };
            const vk::ImageMemoryBarrier2 image_to_transfer{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, this->session.output_layout, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->session.output_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            const vk::MemoryBarrier2 buffers_to_transfer{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &buffers_to_transfer, {}, {}, 1, &image_to_transfer});
            command_buffer.copyImageToBuffer(*this->session.output_image.image, vk::ImageLayout::eTransferSrcOptimal, *snapshot.buffer.buffer, vk::BufferImageCopy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {this->session.render_extent.width, this->session.render_extent.height, 1}});
            for (std::size_t index = 0; index != source_offsets.size(); ++index) command_buffer.copyBuffer(*this->session.arena.buffer, *snapshot.buffer.buffer, vk::BufferCopy{source_offsets[index], buffer_size * (index + 1u), buffer_size});
            const vk::ImageMemoryBarrier2 image_to_output{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eTransferSrcOptimal, this->session.output_layout, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->session.output_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, {}, {}, 1, &image_to_output});
        });
        return materialize_gbuffer_readback(snapshot);
    }

    RenderOutput PathTracer::output() const noexcept {
        const bool freshly_cleared = this->session.output_layout == vk::ImageLayout::eTransferDstOptimal;
        return {
            this->session.output_image,
            this->session.sampled_output_descriptor,
            this->session.output_layout,
            freshly_cleared ? vk::PipelineStageFlagBits2::eClear : vk::PipelineStageFlagBits2::eComputeShader,
            freshly_cleared ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eShaderStorageWrite,
            this->scene.filter.film.color_space,
            this->scene.filter.film.exposure,
        };
    }

    DepthBufferView PathTracer::depth_buffer() noexcept {
        return {this->session.depth_image, this->session.sampled_depth_descriptor, this->session.depth_layout};
    }

    void PathTracer::begin_scene_preparation(const scene::SceneView scene) {
        this->scene.primitives.descriptor              = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_table.descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_shapes.descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_distribution.descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_distribution_data.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.portals.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_bvh_nodes.descriptor         = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_bvh_bit_trails.descriptor    = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.light_bvh_counters_descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.face_materials.descriptor          = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.media.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.volumes.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.spectra.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.piecewise_spectra.descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.bindings.descriptor                = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.filter.distribution.descriptor     = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.filter.sensor_response.descriptor  = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.sampler.pixel_samples.descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.camera                             = scene.camera;
        this->scene.transport_settings                 = scene.transport;
        for (PathBufferSlice& slice : this->scene.materials) slice.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        for (PathBufferSlice& slice : this->scene.textures) slice.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->scene.initialized = true;

        std::shared_ptr<PathTracerScenePreparation> preparation = std::make_shared<PathTracerScenePreparation>();
        preparation->scene                                      = std::make_shared<const PathTracerScenePreparation::SceneSnapshot>(PathTracerScenePreparation::SceneSnapshot{snapshot_path_scene_resources(scene.resources), scene.camera, scene.film, scene.sampler, scene.transport, scene.revision, scene.bounds()});
        preparation->gpu                                        = snapshot_path_scene_gpu(this->context.gpu_scene, scene);
        preparation->progress.report(PathTracerPreparationStage::CompilingSampler);
        this->scene_preparation      = preparation;
        this->scene_preparation_task = std::async(std::launch::async, [this, preparation = std::move(preparation)] {
            preparation->sampler = prepare_path_sampler(preparation->scene->sampler, this->context.pathtracer);
            preparation->progress.report(PathTracerPreparationStage::CompilingFilter);
            preparation->filter = prepare_path_filter(preparation->scene->film, this->context.pathtracer);
            preparation->progress.report(PathTracerPreparationStage::CompilingTextures, 0, static_cast<std::uint32_t>(preparation->scene->resources.textures.size()));
            preparation->prepared = prepare_path_scene(preparation->scene->view(), preparation->scene->bounds, preparation->gpu, this->context.pathtracer, &preparation->progress);
        }).share();
    }

    void PathTracer::release_scene_preparation() noexcept {
        if (this->scene_preparation_task.valid()) this->scene_preparation_task.wait();
        this->scene_preparation_task = std::shared_future<void>{};
        this->scene_preparation.reset();
    }

    void PathTracer::destroy_scene() noexcept {
        if (!this->scene.initialized) return;
        this->context.runtime.frames.defer_destruction([arena = std::move(this->scene.arena), bindings = std::move(this->scene.bindings), volume_resources = std::move(this->scene.volume_resources), bvh_counters = std::move(this->scene.light_bvh_counters), filter_distribution = std::move(this->scene.filter.distribution), filter_sensor_response = std::move(this->scene.filter.sensor_response), sampler_samples = std::move(this->scene.sampler.pixel_samples)]() mutable {});
        this->scene.initialized = false;
    }

    void PathTracer::commit_filter(std::unique_ptr<PreparedPathFilter> prepared, const vk::raii::CommandBuffer& command_buffer) {
        GpuBuffer new_distribution    = upload_path_buffer(this->context.runtime, std::span<const float>{prepared->distribution}, command_buffer);
        GpuBuffer new_sensor_response = upload_path_buffer(this->context.runtime, std::span<const float>{prepared->sensor_response}, command_buffer);
        if (*this->scene.filter.distribution.buffer.buffer) {
            DescriptorLease distribution_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            DescriptorLease sensor_descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(distribution_descriptor, vk::DescriptorType::eStorageBuffer, new_distribution);
            this->context.runtime.resources.write_buffer_descriptor(sensor_descriptor, vk::DescriptorType::eStorageBuffer, new_sensor_response);
            this->context.runtime.frames.defer_destruction([distribution_buffer = std::move(this->scene.filter.distribution.buffer), sensor_buffer = std::move(this->scene.filter.sensor_response.buffer)]() mutable {});
            this->scene.filter.distribution.descriptor    = std::move(distribution_descriptor);
            this->scene.filter.sensor_response.descriptor = std::move(sensor_descriptor);
        } else {
            this->context.runtime.resources.write_buffer_descriptor(this->scene.filter.distribution.descriptor, vk::DescriptorType::eStorageBuffer, new_distribution);
            this->context.runtime.resources.write_buffer_descriptor(this->scene.filter.sensor_response.descriptor, vk::DescriptorType::eStorageBuffer, new_sensor_response);
        }
        this->scene.filter.film                   = std::move(prepared->film);
        this->scene.filter.distribution.buffer    = std::move(new_distribution);
        this->scene.filter.sensor_response.buffer = std::move(new_sensor_response);
        this->scene.filter.resolution             = prepared->resolution;
        this->scene.filter.absolute_integral      = prepared->absolute_integral;
    }

    void PathTracer::compile_filter(const scene::Film& film, const vk::raii::CommandBuffer& command_buffer) {
        this->commit_filter(prepare_path_filter(film, this->context.pathtracer), command_buffer);
    }

    void PathTracer::commit_sampler(std::unique_ptr<PreparedPathSampler> prepared, const vk::raii::CommandBuffer& command_buffer) {
        GpuBuffer new_pixel_samples = upload_path_buffer(this->context.runtime, std::span<const math::Float2>{prepared->pixel_samples}, command_buffer);
        if (*this->scene.sampler.pixel_samples.buffer.buffer) {
            DescriptorLease descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(descriptor, vk::DescriptorType::eStorageBuffer, new_pixel_samples);
            this->context.runtime.frames.defer_destruction([buffer = std::move(this->scene.sampler.pixel_samples.buffer)]() mutable {});
            this->scene.sampler.pixel_samples.descriptor = std::move(descriptor);
        } else
            this->context.runtime.resources.write_buffer_descriptor(this->scene.sampler.pixel_samples.descriptor, vk::DescriptorType::eStorageBuffer, new_pixel_samples);
        this->scene.sampler.sampler              = std::move(prepared->sampler);
        this->scene.sampler.pixel_samples.buffer = std::move(new_pixel_samples);
        this->scene.sampler.pixel_tile_size      = prepared->pixel_tile_size;
    }

    void PathTracer::compile_sampler(const scene::Sampler& sampler, const vk::raii::CommandBuffer& command_buffer) {
        this->commit_sampler(prepare_path_sampler(sampler, this->context.pathtracer), command_buffer);
    }

    void PathTracer::commit_scene(std::unique_ptr<PreparedPathScene> prepared, const vk::raii::CommandBuffer& command_buffer) {
        std::vector<PathVolumeResources> new_volume_gpu_data{};
        new_volume_gpu_data.reserve(prepared->volume_resources.size());
        for (std::size_t index = 0; index != prepared->volume_resources.size(); ++index) {
            PreparedPathVolume& source = prepared->volume_resources[index];
            GpuBuffer majorant         = upload_path_buffer(this->context.runtime, std::span<const float>{source.majorant}, command_buffer);
            DescriptorLease descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(descriptor, vk::DescriptorType::eStorageBuffer, majorant);
            prepared->compiled_volumes[index].majorant = descriptor;
            PathVolumeResources destination{};
            destination.majorant        = GpuBufferBinding{std::move(descriptor)};
            destination.majorant.buffer = std::move(majorant);
            destination.volume_id       = source.id;
            destination.revision        = source.revision;
            destination.resolution      = source.resolution;
            new_volume_gpu_data.push_back(std::move(destination));
        }
        if (!prepared->compiled_volumes.empty()) std::memcpy(prepared->arena.data() + prepared->volumes.offset, prepared->compiled_volumes.data(), prepared->compiled_volumes.size() * sizeof(pathtracer::PathVolume));

        GpuBuffer new_arena              = upload_path_buffer(this->context.runtime, std::span<const std::byte>{prepared->arena}, command_buffer);
        GpuBuffer new_light_bvh_counters = create_storage_buffer(this->context.runtime, std::max<vk::DeviceSize>(prepared->light_bvh_node_count * sizeof(std::uint32_t), sizeof(std::uint32_t)), vk::BufferUsageFlagBits::eTransferDst);
        DescriptorLease new_light_bvh_counters_descriptor{};
        if (*this->scene.light_bvh_counters.buffer) {
            new_light_bvh_counters_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.frames.defer_destruction([buffer = std::move(this->scene.light_bvh_counters)]() mutable {});
        } else
            new_light_bvh_counters_descriptor = std::move(this->scene.light_bvh_counters_descriptor);
        this->context.runtime.resources.write_buffer_descriptor(new_light_bvh_counters_descriptor, vk::DescriptorType::eStorageBuffer, new_light_bvh_counters);
        PathBufferSlice new_primitives                                                                = std::move(prepared->primitives);
        std::array<PathBufferSlice, static_cast<std::size_t>(PathMaterialTable::Count)> new_materials = std::move(prepared->materials);
        std::array<PathBufferSlice, static_cast<std::size_t>(PathTextureTable::Count)> new_textures   = std::move(prepared->textures);
        PathBufferSlice new_lights                                                                    = std::move(prepared->light_table);
        PathBufferSlice new_light_shapes                                                              = std::move(prepared->light_shapes);
        PathBufferSlice new_light_distributions                                                       = std::move(prepared->light_distribution);
        PathBufferSlice new_light_distribution_data                                                   = std::move(prepared->light_distribution_data);
        PathBufferSlice new_portals                                                                   = std::move(prepared->portals);
        PathBufferSlice new_light_bvh_nodes                                                           = std::move(prepared->light_bvh_nodes);
        PathBufferSlice new_light_bvh_bit_trails                                                      = std::move(prepared->light_bvh_bit_trails);
        PathBufferSlice new_face_materials                                                            = std::move(prepared->face_materials);
        PathBufferSlice new_media                                                                     = std::move(prepared->media);
        PathBufferSlice new_volumes                                                                   = std::move(prepared->volumes);
        PathBufferSlice new_spectra                                                                   = std::move(prepared->spectra);
        PathBufferSlice new_piecewise_spectra                                                         = std::move(prepared->piecewise_spectra);
        if (*this->scene.arena.buffer) {
            new_primitives.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            for (PathBufferSlice& slice : new_materials) slice.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            for (PathBufferSlice& slice : new_textures) slice.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            new_lights.descriptor                  = this->context.runtime.frames.allocate_resource_descriptor();
            new_light_shapes.descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
            new_light_distributions.descriptor     = this->context.runtime.frames.allocate_resource_descriptor();
            new_light_distribution_data.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            new_portals.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
            new_light_bvh_nodes.descriptor         = this->context.runtime.frames.allocate_resource_descriptor();
            new_light_bvh_bit_trails.descriptor    = this->context.runtime.frames.allocate_resource_descriptor();
            new_face_materials.descriptor          = this->context.runtime.frames.allocate_resource_descriptor();
            new_media.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
            new_volumes.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
            new_spectra.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
            new_piecewise_spectra.descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.frames.defer_destruction([arena = std::move(this->scene.arena), volumes = std::move(this->scene.volume_resources)]() mutable {});
        } else {
            new_primitives.descriptor = std::move(this->scene.primitives.descriptor);
            for (std::size_t index = 0; index != new_materials.size(); ++index) new_materials[index].descriptor = std::move(this->scene.materials[index].descriptor);
            for (std::size_t index = 0; index != new_textures.size(); ++index) new_textures[index].descriptor = std::move(this->scene.textures[index].descriptor);
            new_lights.descriptor                  = std::move(this->scene.light_table.descriptor);
            new_light_shapes.descriptor            = std::move(this->scene.light_shapes.descriptor);
            new_light_distributions.descriptor     = std::move(this->scene.light_distribution.descriptor);
            new_light_distribution_data.descriptor = std::move(this->scene.light_distribution_data.descriptor);
            new_portals.descriptor                 = std::move(this->scene.portals.descriptor);
            new_light_bvh_nodes.descriptor         = std::move(this->scene.light_bvh_nodes.descriptor);
            new_light_bvh_bit_trails.descriptor    = std::move(this->scene.light_bvh_bit_trails.descriptor);
            new_face_materials.descriptor          = std::move(this->scene.face_materials.descriptor);
            new_media.descriptor                   = std::move(this->scene.media.descriptor);
            new_volumes.descriptor                 = std::move(this->scene.volumes.descriptor);
            new_spectra.descriptor                 = std::move(this->scene.spectra.descriptor);
            new_piecewise_spectra.descriptor       = std::move(this->scene.piecewise_spectra.descriptor);
        }
        const auto write_slice = [this, &new_arena](const PathBufferSlice& slice) { this->context.runtime.resources.write_buffer_descriptor(slice.descriptor, vk::DescriptorType::eStorageBuffer, new_arena.address + slice.offset, slice.size); };
        write_slice(new_primitives);
        for (const PathBufferSlice& slice : new_materials) write_slice(slice);
        for (const PathBufferSlice& slice : new_textures) write_slice(slice);
        write_slice(new_lights);
        write_slice(new_light_shapes);
        write_slice(new_light_distributions);
        write_slice(new_light_distribution_data);
        write_slice(new_portals);
        write_slice(new_light_bvh_nodes);
        write_slice(new_light_bvh_bit_trails);
        write_slice(new_face_materials);
        write_slice(new_media);
        write_slice(new_volumes);
        write_slice(new_spectra);
        write_slice(new_piecewise_spectra);
        this->scene.primitives                    = std::move(new_primitives);
        this->scene.materials                     = std::move(new_materials);
        this->scene.textures                      = std::move(new_textures);
        this->scene.light_table                   = std::move(new_lights);
        this->scene.light_shapes                  = std::move(new_light_shapes);
        this->scene.light_distribution            = std::move(new_light_distributions);
        this->scene.light_distribution_data       = std::move(new_light_distribution_data);
        this->scene.portals                       = std::move(new_portals);
        this->scene.light_bvh_nodes               = std::move(new_light_bvh_nodes);
        this->scene.light_bvh_bit_trails          = std::move(new_light_bvh_bit_trails);
        this->scene.face_materials                = std::move(new_face_materials);
        this->scene.media                         = std::move(new_media);
        this->scene.volumes                       = std::move(new_volumes);
        this->scene.spectra                       = std::move(new_spectra);
        this->scene.piecewise_spectra             = std::move(new_piecewise_spectra);
        this->scene.arena                         = std::move(new_arena);
        this->scene.light_bvh_counters            = std::move(new_light_bvh_counters);
        this->scene.light_bvh_counters_descriptor = std::move(new_light_bvh_counters_descriptor);
        this->scene.volume_resources              = std::move(new_volume_gpu_data);
        GpuBuffer new_bindings                    = this->context.runtime.resources.create_buffer(sizeof(pathtracer::PathSceneBindings), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        const pathtracer::PathSceneBindings bindings{
            this->scene.primitives.descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::Header)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::Diffuse)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::DiffuseTransmission)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::Conductor)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::Dielectric)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::ThinDielectric)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::CoatedDiffuse)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::CoatedConductor)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::Mix)].descriptor,
            this->scene.materials[static_cast<std::size_t>(PathMaterialTable::TextureRequest)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Header)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Mapping)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Constant)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Image)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Checkerboard)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Scale)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Mix)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::DirectionMix)].descriptor,
            this->scene.textures[static_cast<std::size_t>(PathTextureTable::Bilerp)].descriptor,
            this->scene.light_table.descriptor,
            this->scene.light_shapes.descriptor,
            this->scene.light_distribution.descriptor,
            this->scene.light_distribution_data.descriptor,
            this->scene.portals.descriptor,
            this->scene.light_bvh_nodes.descriptor,
            this->scene.light_bvh_bit_trails.descriptor,
            this->scene.face_materials.descriptor,
            this->scene.media.descriptor,
            this->scene.volumes.descriptor,
            this->scene.spectra.descriptor,
            this->scene.piecewise_spectra.descriptor,
            this->context.pathtracer.cie_spectra_descriptor,
            this->context.pathtracer.rgb_to_spectrum_tables_descriptor,
            {prepared->texture_count, prepared->texture_stack_size, 0, prepared->material_texture_value_count},
            {static_cast<std::uint32_t>(prepared->light_sampler), prepared->light_bvh_node_count, prepared->light_bvh_infinite_count, 0},
        };
        std::memcpy(new_bindings.mapped, &bindings, sizeof(bindings));
        if (*this->scene.bindings.buffer.buffer) {
            DescriptorLease descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(descriptor, vk::DescriptorType::eStorageBuffer, new_bindings);
            this->context.runtime.frames.defer_destruction([buffer = std::move(this->scene.bindings.buffer)]() mutable {});
            this->scene.bindings.descriptor = std::move(descriptor);
        } else
            this->context.runtime.resources.write_buffer_descriptor(this->scene.bindings.descriptor, vk::DescriptorType::eStorageBuffer, new_bindings);
        this->scene.bindings.buffer                 = std::move(new_bindings);
        this->scene.texture_stack_size              = prepared->texture_stack_size;
        this->scene.material_texture_value_count    = prepared->material_texture_value_count;
        this->scene.light_count                     = prepared->light_count;
        this->scene.light_bvh_node_count            = prepared->light_bvh_node_count;
        this->scene.camera_medium_index             = prepared->camera_medium_index;
        this->scene.compiled_bounds                 = prepared->bounds;
        this->scene.compiled_instance_transforms    = std::move(prepared->instance_transforms);
        this->scene.compiled_revision               = prepared->revision;
        this->scene.compiled_gpu_structure_revision = prepared->gpu_structure_revision;
        this->scene.dynamic_area_lights             = std::move(prepared->dynamic_area_lights);
        this->update_dynamic_lights(command_buffer);
    }

    void PathTracer::compile_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        const PathSceneGpuSnapshot gpu = snapshot_path_scene_gpu(this->context.gpu_scene, scene);
        this->commit_scene(prepare_path_scene(scene, scene.bounds(), gpu, this->context.pathtracer, nullptr), command_buffer);
        this->update_volumes(scene, command_buffer);
    }

    void PathTracer::update_volumes(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.resources.volumes.size() != this->scene.volume_resources.size()) {
            this->compile_scene(scene, command_buffer);
            return;
        }
        for (std::size_t index = 0; index != scene.resources.volumes.size(); ++index) {
            const scene::Volume& volume   = scene.resources.volumes[index];
            PathVolumeResources& gpu_data = this->scene.volume_resources[index];
            const GpuVolume& shared       = this->context.gpu_scene.view().volumes[index];
            if (volume.id != gpu_data.volume_id || shared.volume_id != gpu_data.volume_id) {
                this->compile_scene(scene, command_buffer);
                return;
            }
            if (shared.revision.content == gpu_data.revision.content) continue;
            if (shared.revision.topology != gpu_data.revision.topology || !shared.dirty_region) {
                this->compile_scene(scene, command_buffer);
                return;
            }
            const auto descriptor = [this, &shared](const std::string_view id) -> DescriptorHandle {
                const std::vector<GpuVolumeField>::const_iterator found = std::ranges::find(shared.fields, id, &GpuVolumeField::id);
                return found == shared.fields.end() ? this->context.pathtracer.zero_volume_field_descriptor : found->descriptors.front();
            };
            DescriptorHandle density_descriptor = this->context.pathtracer.zero_volume_field_descriptor;
            DescriptorHandle sigma_a_descriptor = this->context.pathtracer.zero_volume_field_descriptor;
            DescriptorHandle sigma_s_descriptor = this->context.pathtracer.zero_volume_field_descriptor;
            std::uint32_t majorant_mode{};
            std::uint32_t majorant_flags{};
            const math::UInt3 resolution = shared.resolution;
            if (!std::holds_alternative<scene::GridVolume>(volume.data)) {
                this->compile_scene(scene, command_buffer);
                return;
            }
            const scene::VolumeRendering& rendering = volume.rendering;
            if (rendering.density_field.empty() && (!rendering.sigma_a_field.empty() || !rendering.sigma_s_field.empty() || !rendering.emission_field.empty())) {
                majorant_mode = 1;
                if (!rendering.sigma_a_field.empty()) sigma_a_descriptor = descriptor(rendering.sigma_a_field), majorant_flags |= 1u;
                if (!rendering.sigma_s_field.empty()) sigma_s_descriptor = descriptor(rendering.sigma_s_field), majorant_flags |= 2u;
            } else
                density_descriptor = descriptor(rendering.density_field);
            const scene::VolumeRegion dirty_region = shared.revision.content == gpu_data.revision.content + 1u ? *shared.dirty_region : scene::VolumeRegion{{}, resolution};
            const scene::VolumeRegion expanded{
                {
                    dirty_region.minimum.x > 0 ? dirty_region.minimum.x - 1 : 0,
                    dirty_region.minimum.y > 0 ? dirty_region.minimum.y - 1 : 0,
                    dirty_region.minimum.z > 0 ? dirty_region.minimum.z - 1 : 0,
                },
                {
                    std::min(resolution.x, dirty_region.maximum.x + 1),
                    std::min(resolution.y, dirty_region.maximum.y + 1),
                    std::min(resolution.z, dirty_region.maximum.z + 1),
                },
            };
            const std::array<std::uint32_t, 4> brick_minimum{expanded.minimum.x * 16u / resolution.x, expanded.minimum.y * 16u / resolution.y, expanded.minimum.z * 16u / resolution.z, 0};
            const std::array<std::uint32_t, 4> brick_maximum{std::min(16u, (expanded.maximum.x * 16u + resolution.x - 1u) / resolution.x), std::min(16u, (expanded.maximum.y * 16u + resolution.y - 1u) / resolution.y), std::min(16u, (expanded.maximum.z * 16u + resolution.z - 1u) / resolution.z), 0};
            struct alignas(16) MajorantPushData {
                DescriptorHandle density;
                DescriptorHandle sigma_a;
                DescriptorHandle sigma_s;
                DescriptorHandle majorant;
                std::array<std::uint32_t, 4> resolution;
                std::array<std::uint32_t, 4> brick_minimum;
                std::array<std::uint32_t, 4> brick_maximum;
                std::array<std::uint32_t, 4> metadata;
            };
            static_assert(sizeof(MajorantPushData) == 96);
            const MajorantPushData push_data{density_descriptor, sigma_a_descriptor, sigma_s_descriptor, gpu_data.majorant.descriptor, {resolution.x, resolution.y, resolution.z, 0}, brick_minimum, brick_maximum, {majorant_mode, majorant_flags, 0, 0}};
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::VolumeMajorant));
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            const std::uint32_t brick_count = (brick_maximum[0] - brick_minimum[0]) * (brick_maximum[1] - brick_minimum[1]) * (brick_maximum[2] - brick_minimum[2]);
            command_buffer.dispatch((brick_count + 63u) / 64u, 1, 1);
            const vk::MemoryBarrier2 majorant_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &majorant_dependency});
            gpu_data.revision = shared.revision;
        }
    }

    void PathTracer::update_dynamic_lights(const vk::raii::CommandBuffer& command_buffer) {
        if (this->scene.dynamic_area_lights.empty()) return;
        struct alignas(16) DynamicAreaLightPushData {
            DescriptorHandle positions{};
            DescriptorHandle indices{};
            DescriptorHandle radii{};
            DescriptorHandle normals{};
            DescriptorHandle texture_coordinates{};
            DescriptorHandle transforms{};
            DescriptorHandle lights{};
            DescriptorHandle shapes{};
            std::array<std::uint32_t, 4> range{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<float, 4> geometry_parameters{};
            std::array<float, 4> emission_parameters{};
            std::array<float, 4> selection_parameters{};
        };
        static_assert(sizeof(DynamicAreaLightPushData) == 144);
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        const auto push_for_range    = [&](const PathDynamicAreaLightRange& range) {
            DynamicAreaLightPushData push{};
            push.transforms           = gpu_scene.primitive_transforms;
            push.lights               = this->scene.light_table.descriptor;
            push.shapes               = this->scene.light_shapes.descriptor;
            push.geometry_parameters  = range.geometry_parameters;
            push.emission_parameters  = range.emission_parameters;
            push.selection_parameters = range.selection_parameters;
            std::uint32_t active_count{1};
            if (range.kind == 0) {
                const GpuGeometry& geometry = gpu_scene.geometries[range.resource_index];
                push.positions              = geometry.positions_descriptor;
                push.indices                = geometry.indices_descriptor;
                push.normals                = geometry.normals_descriptor;
                push.texture_coordinates    = geometry.texture_coordinates_descriptor;
                active_count                = geometry.index_count / 3u;
            } else if (range.kind == 1) {
                const GpuSphereSet& spheres = gpu_scene.sphere_sets[range.resource_index];
                push.positions              = spheres.positions_descriptor;
                push.radii                  = spheres.radii_descriptor;
                active_count                = spheres.sphere_count;
            }
            push.range    = {range.scene_primitive_index, range.first_light, active_count, range.capacity};
            push.metadata = {range.kind, range.reverse_orientation, range.kind == 0 ? range.attribute_mask : range.geometry_kind, range.alpha_texture};
            return push;
        };
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::DynamicAreaLightShapes));
        for (const PathDynamicAreaLightRange& range : this->scene.dynamic_area_lights) {
            const DynamicAreaLightPushData push = push_for_range(range);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch((range.capacity + 63u) / 64u, 1, 1);
        }
        const vk::MemoryBarrier2 shape_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shape_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::DynamicAreaLightFinalize));
        for (const PathDynamicAreaLightRange& range : this->scene.dynamic_area_lights) {
            const DynamicAreaLightPushData push = push_for_range(range);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch(1, 1, 1);
        }
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shape_dependency});
        struct alignas(16) DynamicLightSelectionPushData {
            DescriptorHandle lights{};
            std::uint32_t light_count{};
            std::uint32_t sampler_kind{};
        };
        static_assert(sizeof(DynamicLightSelectionPushData) == 16);
        const DynamicLightSelectionPushData selection_push{this->scene.light_table.descriptor, this->scene.light_count, static_cast<std::uint32_t>(this->scene.transport_settings.light_sampler)};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::DynamicLightSelection));
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&selection_push, 1}));
        command_buffer.dispatch(1, 1, 1);
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shape_dependency});
        if (this->scene.transport_settings.light_sampler == scene::LightSamplerKind::Bvh && this->scene.light_bvh_node_count != 0) {
            command_buffer.fillBuffer(*this->scene.light_bvh_counters.buffer, 0, this->scene.light_bvh_counters.size, 0);
            const vk::MemoryBarrier2 counter_dependency{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &counter_dependency});
            struct alignas(16) DynamicLightBvhPushData {
                DescriptorHandle lights{};
                DescriptorHandle shapes{};
                DescriptorHandle nodes{};
                DescriptorHandle counters{};
                std::uint32_t node_count{};
                std::array<std::uint32_t, 3> reserved{};
            };
            static_assert(sizeof(DynamicLightBvhPushData) == 48);
            const DynamicLightBvhPushData bvh_push{this->scene.light_table.descriptor, this->scene.light_shapes.descriptor, this->scene.light_bvh_nodes.descriptor, this->scene.light_bvh_counters_descriptor, this->scene.light_bvh_node_count};
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::DynamicLightBvh));
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&bvh_push, 1}));
            command_buffer.dispatch((this->scene.light_bvh_node_count + 63u) / 64u, 1, 1);
        }
        const vk::MemoryBarrier2 render_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &render_dependency});
    }

    void PathTracer::synchronize_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.number == this->scene.compiled_revision.number) return;
        bool compiled{};
        if ((scene.revision.changes & (scene::SceneChange::Geometry | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Transport)) != scene::SceneChange::None) {
            this->compile_scene(scene, command_buffer);
            compiled = true;
        } else if ((scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            const math::Bounds3 bounds          = scene.bounds();
            bool transform_dependencies_changed = scene.resources.instances.size() != this->scene.compiled_instance_transforms.size() || bounds.minimum.x != this->scene.compiled_bounds.minimum.x || bounds.minimum.y != this->scene.compiled_bounds.minimum.y || bounds.minimum.z != this->scene.compiled_bounds.minimum.z || bounds.maximum.x != this->scene.compiled_bounds.maximum.x || bounds.maximum.y != this->scene.compiled_bounds.maximum.y || bounds.maximum.z != this->scene.compiled_bounds.maximum.z;
            if (!transform_dependencies_changed)
                for (std::size_t index = 0; index != scene.resources.instances.size(); ++index) {
                    const scene::Instance& instance = scene.resources.instances[index];
                    if (instance.transform == this->scene.compiled_instance_transforms[index]) continue;
                    const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                    if (std::ranges::any_of(prototype.primitives, [](const scene::Primitive& primitive) { return primitive.area_light.value != 0; })) {
                        transform_dependencies_changed = true;
                        break;
                    }
                }
            if (transform_dependencies_changed) {
                this->compile_scene(scene, command_buffer);
                compiled = true;
            } else {
                this->scene.compiled_bounds = {bounds.minimum, bounds.maximum};
                this->scene.compiled_instance_transforms.clear();
                this->scene.compiled_instance_transforms.reserve(scene.resources.instances.size());
                for (const scene::Instance& instance : scene.resources.instances) this->scene.compiled_instance_transforms.push_back(instance.transform);
            }
        }
        if (!compiled && (scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->compile_scene(scene, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Film) != scene::SceneChange::None) this->compile_filter(scene.film, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Sampler) != scene::SceneChange::None) this->compile_sampler(scene.sampler, command_buffer);
        if ((scene.revision.changes & scene::SceneChange::Transport) != scene::SceneChange::None) this->scene.transport_settings = scene.transport;
        this->scene.compiled_revision = scene.revision;
    }

    void PathTracer::initialize_session() {
        this->session.output_descriptor                          = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.sampled_output_descriptor                  = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.storage_depth_descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.sampled_depth_descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.queue_counts.descriptor                    = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_queue_0.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_queue_1.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_origins.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_directions.descriptor                  = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_origin_dx.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_origin_dy.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_direction_dx.descriptor                = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.ray_direction_dy.descriptor                = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.throughputs.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.radiances.descriptor                       = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.wavelengths.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.wavelength_pdfs.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.r_u.descriptor                             = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.r_l.descriptor                             = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.current_media.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.light_context_normals.descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.flags.descriptor                           = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.eta_scales.descriptor                      = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_normal_distances.descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_geometric_normal_u.descriptor          = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_tangent_v.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_dpdu.descriptor                        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_dpdv.descriptor                        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_dndu.descriptor                        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_dndv.descriptor                        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.hit_identifiers.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_path_ids.descriptor                 = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_origins.descriptor                  = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_directions.descriptor               = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_contributions.descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_r_p.descriptor                      = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_pdfs.descriptor                     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.shadow_media.descriptor                    = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.texture_evaluation_stack.descriptor        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.evaluated_texture_values.descriptor        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.filter_weights.descriptor                  = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.film_rgb_sums.descriptor                   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.film_weight_sums.descriptor                = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_albedo.descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_shading_normal.descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_geometric_normal.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_position_depth.descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_uv.descriptor               = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_identity_0.descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_sample_identity_1.descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_albedo_sums.descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_shading_normal_sums.descriptor     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_geometric_normal_sums.descriptor   = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_position_depth_sums.descriptor     = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_uv_weight_sums.descriptor          = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_identity_0.descriptor              = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.gbuffer_identity_1.descriptor              = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.bindings.descriptor                        = this->context.runtime.frames.allocate_resource_descriptor();
        this->session.parameters.reserve(VulkanFrames::frames_in_flight);
        for (std::uint32_t index = 0; index != VulkanFrames::frames_in_flight; ++index) {
            this->session.parameters.emplace_back(this->context.runtime.frames.allocate_resource_descriptor());
            this->session.parameters.back().buffer = this->context.runtime.resources.create_buffer(sizeof(pathtracer::WavefrontParameters), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            this->context.runtime.resources.write_buffer_descriptor(this->session.parameters.back().descriptor, vk::DescriptorType::eStorageBuffer, this->session.parameters.back().buffer);
        }
        this->session.bindings.buffer = this->context.runtime.resources.create_buffer(sizeof(pathtracer::WavefrontBindings), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->context.runtime.resources.write_buffer_descriptor(this->session.bindings.descriptor, vk::DescriptorType::eStorageBuffer, this->session.bindings.buffer);
        const pathtracer::WavefrontBindings binding_data{
            this->session.output_descriptor,
            this->session.storage_depth_descriptor,
            this->session.queue_counts.descriptor,
            this->session.ray_queue_0.descriptor,
            this->session.ray_queue_1.descriptor,
            this->session.ray_origins.descriptor,
            this->session.ray_directions.descriptor,
            this->session.ray_origin_dx.descriptor,
            this->session.ray_origin_dy.descriptor,
            this->session.ray_direction_dx.descriptor,
            this->session.ray_direction_dy.descriptor,
            this->session.throughputs.descriptor,
            this->session.radiances.descriptor,
            this->session.wavelengths.descriptor,
            this->session.wavelength_pdfs.descriptor,
            this->session.r_u.descriptor,
            this->session.r_l.descriptor,
            this->session.current_media.descriptor,
            this->session.light_context_normals.descriptor,
            this->session.flags.descriptor,
            this->session.eta_scales.descriptor,
            this->session.hit_normal_distances.descriptor,
            this->session.hit_geometric_normal_u.descriptor,
            this->session.hit_tangent_v.descriptor,
            this->session.hit_dpdu.descriptor,
            this->session.hit_dpdv.descriptor,
            this->session.hit_dndu.descriptor,
            this->session.hit_dndv.descriptor,
            this->session.hit_identifiers.descriptor,
            this->session.shadow_path_ids.descriptor,
            this->session.shadow_origins.descriptor,
            this->session.shadow_directions.descriptor,
            this->session.shadow_contributions.descriptor,
            this->session.shadow_r_p.descriptor,
            this->session.shadow_pdfs.descriptor,
            this->session.shadow_media.descriptor,
            this->session.texture_evaluation_stack.descriptor,
            this->session.evaluated_texture_values.descriptor,
            this->session.filter_weights.descriptor,
            this->session.film_rgb_sums.descriptor,
            this->session.film_weight_sums.descriptor,
            this->session.gbuffer_sample_albedo.descriptor,
            this->session.gbuffer_sample_shading_normal.descriptor,
            this->session.gbuffer_sample_geometric_normal.descriptor,
            this->session.gbuffer_sample_position_depth.descriptor,
            this->session.gbuffer_sample_uv.descriptor,
            this->session.gbuffer_sample_identity_0.descriptor,
            this->session.gbuffer_sample_identity_1.descriptor,
            this->session.gbuffer_albedo_sums.descriptor,
            this->session.gbuffer_shading_normal_sums.descriptor,
            this->session.gbuffer_geometric_normal_sums.descriptor,
            this->session.gbuffer_position_depth_sums.descriptor,
            this->session.gbuffer_uv_weight_sums.descriptor,
            this->session.gbuffer_identity_0.descriptor,
            this->session.gbuffer_identity_1.descriptor,
        };
        std::memcpy(this->session.bindings.buffer.mapped, &binding_data, sizeof(binding_data));
        this->session.initialized = true;
    }

    void PathTracer::destroy_session() noexcept {
        if (!this->session.initialized) return;
        this->context.runtime.frames.defer_destruction([output = std::move(this->session.output_image), depth = std::move(this->session.depth_image), arena = std::move(this->session.arena), indirect_commands = std::move(this->session.indirect_commands), bindings = std::move(this->session.bindings), parameters = std::move(this->session.parameters)]() mutable {});
        this->session.render_extent                = vk::Extent2D{};
        this->session.output_layout                = vk::ImageLayout::eUndefined;
        this->session.depth_layout                 = vk::ImageLayout::eUndefined;
        this->session.pixel_capacity               = 0;
        this->session.texture_stack_size           = 0;
        this->session.texture_value_count          = 0;
        this->session.indirect_commands_configured = false;
        this->session.sample_index                 = 0;
        this->session.initialized                  = false;
    }

    void PathTracer::resize_session(const vk::Extent2D extent, const std::uint32_t texture_evaluation_stack_size, const std::uint32_t material_texture_value_count, const vk::raii::CommandBuffer& command_buffer) {
        const std::uint32_t texture_stack_size  = std::max(texture_evaluation_stack_size, 1u);
        const std::uint32_t texture_value_count = std::max(material_texture_value_count, 1u);
        if (this->session.render_extent == extent && this->session.texture_stack_size == texture_stack_size && this->session.texture_value_count == texture_value_count) return;
        if (this->session.pixel_capacity != 0) {
            this->destroy_session();
            this->initialize_session();
        }
        this->session.render_extent       = extent;
        this->session.texture_stack_size  = texture_stack_size;
        this->session.texture_value_count = texture_value_count;
        this->session.pixel_capacity      = static_cast<std::uint64_t>(extent.width) * extent.height;
        this->session.output_image        = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        this->session.depth_image         = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32Sfloat, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        this->context.runtime.resources.write_storage_image_descriptor(this->session.output_descriptor, this->session.output_image, vk::ImageLayout::eGeneral);
        this->context.runtime.resources.write_sampled_image_descriptor(this->session.sampled_output_descriptor, this->session.output_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->context.runtime.resources.write_storage_image_descriptor(this->session.storage_depth_descriptor, this->session.depth_image, vk::ImageLayout::eGeneral);
        this->context.runtime.resources.write_sampled_image_descriptor(this->session.sampled_depth_descriptor, this->session.depth_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        const std::array to_clear{
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->session.output_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->session.depth_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(to_clear.size()), to_clear.data()});
        constexpr vk::ImageSubresourceRange color_range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        command_buffer.clearColorImage(*this->session.output_image.image, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}, color_range);
        command_buffer.clearColorImage(*this->session.depth_image.image, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{std::array{1.0f, 0.0f, 0.0f, 0.0f}}, color_range);
        const vk::ImageMemoryBarrier2 depth_initialized{vk::PipelineStageFlagBits2::eClear, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->session.depth_image.image, color_range};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &depth_initialized});
        this->session.output_layout = vk::ImageLayout::eTransferDstOptimal;
        this->session.depth_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;
        const std::array allocations{
            std::pair{&this->session.queue_counts, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 3u)},
            std::pair{&this->session.ray_queue_0, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.ray_queue_1, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.ray_origins, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.ray_directions, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.ray_origin_dx, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.ray_origin_dy, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.ray_direction_dx, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.ray_direction_dy, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.throughputs, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.radiances, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.wavelengths, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.wavelength_pdfs, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.r_u, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.r_l, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.current_media, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.light_context_normals, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.flags, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.eta_scales, static_cast<vk::DeviceSize>(sizeof(float) * this->session.pixel_capacity)},
            std::pair{&this->session.hit_normal_distances, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_geometric_normal_u, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_tangent_v, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_dpdu, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_dpdv, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_dndu, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_dndv, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.hit_identifiers, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_path_ids, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_origins, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_directions, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_contributions, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_r_p, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_pdfs, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.shadow_media, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * this->session.pixel_capacity)},
            std::pair{&this->session.texture_evaluation_stack, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity * this->session.texture_stack_size)},
            std::pair{&this->session.evaluated_texture_values, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity * this->session.texture_value_count)},
            std::pair{&this->session.filter_weights, static_cast<vk::DeviceSize>(sizeof(float) * this->session.pixel_capacity)},
            std::pair{&this->session.film_rgb_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.film_weight_sums, static_cast<vk::DeviceSize>(sizeof(float) * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_albedo, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_shading_normal, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_geometric_normal, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_position_depth, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_uv, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_identity_0, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_sample_identity_1, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_albedo_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_shading_normal_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_geometric_normal_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_position_depth_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_uv_weight_sums, static_cast<vk::DeviceSize>(sizeof(float) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_identity_0, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 4u * this->session.pixel_capacity)},
            std::pair{&this->session.gbuffer_identity_1, static_cast<vk::DeviceSize>(sizeof(std::uint32_t) * 4u * this->session.pixel_capacity)},
        };
        vk::DeviceSize arena_size{};
        for (const auto& [slice, size] : allocations) {
            arena_size    = (arena_size + 15u) & ~vk::DeviceSize{15u};
            slice->offset = arena_size;
            slice->size   = size;
            arena_size += size;
        }
        this->session.arena = create_storage_buffer(this->context.runtime, arena_size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst);
        for (const auto& [slice, size] : allocations) this->context.runtime.resources.write_buffer_descriptor(slice->descriptor, vk::DescriptorType::eStorageBuffer, this->session.arena.address + slice->offset, size);
        this->session.indirect_commands            = this->context.runtime.resources.create_buffer(sizeof(vk::TraceRaysIndirectCommand2KHR) * 2u, vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->session.sample_index                 = 0;
        this->session.indirect_commands_configured = false;
    }

    void PathTracer::configure_indirect_commands() {
        const std::array commands{
            vk::TraceRaysIndirectCommand2KHR{this->context.pathtracer.surface_ray_generation_region.deviceAddress, this->context.pathtracer.surface_ray_generation_region.size, this->context.pathtracer.miss_region.deviceAddress, this->context.pathtracer.miss_region.size, this->context.pathtracer.miss_region.stride, this->context.pathtracer.hit_region.deviceAddress, this->context.pathtracer.hit_region.size, this->context.pathtracer.hit_region.stride, 0, 0, 0, 0, 1, 1},
            vk::TraceRaysIndirectCommand2KHR{this->context.pathtracer.shadow_ray_generation_region.deviceAddress, this->context.pathtracer.shadow_ray_generation_region.size, this->context.pathtracer.miss_region.deviceAddress, this->context.pathtracer.miss_region.size, this->context.pathtracer.miss_region.stride, this->context.pathtracer.hit_region.deviceAddress, this->context.pathtracer.hit_region.size, this->context.pathtracer.hit_region.stride, 0, 0, 0, 0, 1, 1},
        };
        std::memcpy(this->session.indirect_commands.mapped, commands.data(), sizeof(commands));
        this->session.indirect_commands_configured = true;
    }

    void PathTracer::record_integrator(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->session.indirect_commands_configured) this->configure_indirect_commands();
        const scene::Camera& camera            = this->scene.camera;
        const std::array<float, 16>& transform = camera.transform.matrix;
        pathtracer::WavefrontParameters parameters{
            {transform[0], transform[1], transform[2], transform[3]},
            {transform[4], transform[5], transform[6], transform[7]},
            {transform[8], transform[9], transform[10], transform[11]},
        };
        const math::Transform camera_inverse = camera.transform.inverse();
        parameters.camera_inverse_row_0      = {camera_inverse.matrix[0], camera_inverse.matrix[1], camera_inverse.matrix[2], camera_inverse.matrix[3]};
        parameters.camera_inverse_row_1      = {camera_inverse.matrix[4], camera_inverse.matrix[5], camera_inverse.matrix[6], camera_inverse.matrix[7]};
        parameters.camera_inverse_row_2      = {camera_inverse.matrix[8], camera_inverse.matrix[9], camera_inverse.matrix[10], camera_inverse.matrix[11]};
        std::visit(
            [&parameters](const auto& data) {
                parameters.camera_screen = {data.screen_window.minimum.x, data.screen_window.minimum.y, data.screen_window.maximum.x, data.screen_window.maximum.y};
                parameters.camera_lens   = {data.lens_radius, data.focal_distance, data.near_plane, data.far_plane};
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                    parameters.camera_projection[0] = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    parameters.camera_metadata[0]   = 0;
                } else
                    parameters.camera_metadata[0] = 1;
            },
            camera.data);
        parameters.camera_metadata[3]  = this->scene.camera_medium_index;
        const scene::Film& film        = this->scene.filter.film;
        parameters.filter_parameters_0 = {film.filter.radius.x, film.filter.radius.y, film.filter.sigma, 0.0f};
        parameters.filter_parameters_1 = {film.filter.b, film.filter.c, film.filter.tau, this->scene.filter.absolute_integral};
        parameters.filter_metadata     = {std::to_underlying(film.filter.kind), this->scene.filter.resolution[0], this->scene.filter.resolution[1], 0};
        parameters.filter_distribution = this->scene.filter.distribution.descriptor;
        const scene::Sampler& sampler  = this->scene.sampler.sampler;
        parameters.sampling_tables     = this->context.pathtracer.sampling_tables_descriptor;
        parameters.pmj_pixel_samples   = this->scene.sampler.pixel_samples.descriptor;
        parameters.sampler_metadata    = {std::to_underlying(sampler.kind), std::to_underlying(sampler.randomization), sampler.samples_per_pixel, sampler.seed};
        parameters.sampler_parameters  = {sampler.x_strata, sampler.y_strata, sampler.jitter ? 1u : 0u, this->scene.sampler.pixel_tile_size};
        parameters.film_bounds         = {
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(film.pixel_minimum[0]) * this->session.render_extent.width / film.resolution[0]),
            static_cast<std::uint32_t>(static_cast<std::uint64_t>(film.pixel_minimum[1]) * this->session.render_extent.height / film.resolution[1]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(film.pixel_maximum[0]) * this->session.render_extent.width + film.resolution[0] - 1u) / film.resolution[0]),
            static_cast<std::uint32_t>((static_cast<std::uint64_t>(film.pixel_maximum[1]) * this->session.render_extent.height + film.resolution[1] - 1u) / film.resolution[1]),
        };
        parameters.film_sensor_response        = this->scene.filter.sensor_response.descriptor;
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
            1u,
            film.gbuffer_camera_space ? 1u : 0u,
            std::to_underlying(film.color_space),
            0,
        };
        parameters.extent       = {this->session.render_extent.width, this->session.render_extent.height};
        parameters.sample_index = this->session.sample_index;
        parameters.max_depth    = this->scene.transport_settings.maximum_depth;
        parameters.light_count  = this->scene.light_count;
        parameters.reserved     = this->scene.transport_settings.regularize ? 1u : 0u;
        std::memcpy(this->session.parameters[frame_slot_index].buffer.mapped, &parameters, sizeof(parameters));
        pathtracer::WavefrontPushData push_data{
            this->context.gpu_scene.view().acceleration_structure,
            this->session.bindings.descriptor,
            this->session.parameters[frame_slot_index].descriptor,
            this->scene.bindings.descriptor,
            0,
            0,
        };

        command_buffer.fillBuffer(*this->session.arena.buffer, this->session.queue_counts.offset, sizeof(std::uint32_t) * 3u, 0);
        const vk::MemoryBarrier2 initial_memory{
            vk::PipelineStageFlagBits2::eHost | vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eHostWrite | vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        const vk::ImageMemoryBarrier2 to_general{
            this->session.output_layout == vk::ImageLayout::eUndefined          ? vk::PipelineStageFlagBits2::eNone
            : this->session.output_layout == vk::ImageLayout::eTransferDstOptimal ? vk::PipelineStageFlagBits2::eClear
                                                                                : vk::PipelineStageFlagBits2::eComputeShader,
            this->session.output_layout == vk::ImageLayout::eUndefined          ? vk::AccessFlags2{}
            : this->session.output_layout == vk::ImageLayout::eTransferDstOptimal ? vk::AccessFlagBits2::eTransferWrite
                                                                                : vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            this->session.output_layout,
            vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->session.output_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &initial_memory, 0, nullptr, 1, &to_general});
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::GenerateCameraRays));
        const std::uint32_t group_count = static_cast<std::uint32_t>((this->session.pixel_capacity + 255u) / 256u);
        command_buffer.dispatch(group_count, 1, 1);

        std::uint32_t read_queue{};
        for (std::uint32_t bounce = 0; bounce <= this->scene.transport_settings.maximum_depth; ++bounce) {
            push_data.bounce                = bounce;
            push_data.read_queue            = read_queue;
            const std::uint32_t write_queue = 1u - read_queue;
            command_buffer.fillBuffer(*this->session.arena.buffer, this->session.queue_counts.offset + static_cast<vk::DeviceSize>(write_queue) * sizeof(std::uint32_t), sizeof(std::uint32_t), 0);
            command_buffer.fillBuffer(*this->session.arena.buffer, this->session.queue_counts.offset + sizeof(std::uint32_t) * 2u, sizeof(std::uint32_t), 0);
            const vk::MemoryBarrier2 queue_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &queue_to_copy});
            const vk::BufferCopy surface_width_copy{
                this->session.queue_counts.offset + static_cast<vk::DeviceSize>(read_queue) * sizeof(std::uint32_t),
                indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*this->session.arena.buffer, *this->session.indirect_commands.buffer, surface_width_copy);
            const vk::MemoryBarrier2 surface_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->context.pathtracer.pipeline);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->context.pathtracer.stack_size);
            command_buffer.traceRaysIndirect2KHR(this->session.indirect_commands.address);

            const vk::MemoryBarrier2 surface_to_shade{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &surface_to_shade});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::EvaluateSurfaceTextures));
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            const vk::MemoryBarrier2 texture_values_ready{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &texture_values_ready});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::RecordSurfaceGBuffer));
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            if (this->scene.light_count != 0) {
                command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::SampleDirectLighting));
                this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
                command_buffer.dispatch(group_count, 1, 1);
                const vk::MemoryBarrier2 direct_lighting_ready{
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                    vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &direct_lighting_ready});
            }
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::ShadeSurfaces));
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.dispatch(group_count, 1, 1);
            const vk::MemoryBarrier2 shadow_to_copy{
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_to_copy});
            const vk::BufferCopy shadow_width_copy{
                this->session.queue_counts.offset + sizeof(std::uint32_t) * 2u,
                sizeof(vk::TraceRaysIndirectCommand2KHR) + indirect_width_offset,
                sizeof(std::uint32_t),
            };
            command_buffer.copyBuffer(*this->session.arena.buffer, *this->session.indirect_commands.buffer, shadow_width_copy);
            const vk::MemoryBarrier2 shadow_ready{
                vk::PipelineStageFlagBits2::eAllTransfer | vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &shadow_ready});
            command_buffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *this->context.pathtracer.pipeline);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.setRayTracingPipelineStackSizeKHR(this->context.pathtracer.stack_size);
            command_buffer.traceRaysIndirect2KHR(this->session.indirect_commands.address + sizeof(vk::TraceRaysIndirectCommand2KHR));
            const vk::MemoryBarrier2 visibility_ready{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eShaderStorageWrite,
                vk::PipelineStageFlagBits2::eComputeShader,
                vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &visibility_ready});
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::ResolveVisibility));
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
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
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::AccumulateFilm));
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(group_count, 1, 1);
        const vk::ImageMemoryBarrier2 depth_to_general{
            this->session.depth_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
            this->session.depth_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            this->session.depth_layout,
            vk::ImageLayout::eGeneral,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->session.depth_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &depth_to_general});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->context.pathtracer.shader(PathTracerComputeShader::ResolveGBufferDepth));
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.dispatch(group_count, 1, 1);
        const vk::ImageMemoryBarrier2 depth_to_sample{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderSampledRead,
            vk::ImageLayout::eGeneral,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->session.depth_image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &depth_to_sample});
        this->session.output_layout = vk::ImageLayout::eGeneral;
        this->session.depth_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;
        ++this->session.sample_index;
    }

} // namespace spectra
