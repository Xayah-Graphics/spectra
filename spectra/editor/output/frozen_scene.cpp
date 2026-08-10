module spectra.editor;

import :output.frozen_scene;
import spectra.scene.format;
import std;

namespace spectra {
    namespace {
        std::expected<std::filesystem::path, std::string> write_frozen_scene_package(scene::Scene scene, const std::filesystem::path& requested_path, const std::filesystem::path& source_scene_path) {
            const std::filesystem::path parent      = std::filesystem::absolute(requested_path.parent_path());
            const std::string name                  = requested_path.stem().string();
            const std::filesystem::path destination = parent / name;
            const std::filesystem::path temporary   = parent / std::format(".{}.spectra-export-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
            try {
                if (std::filesystem::exists(destination)) throw std::runtime_error(std::format("Frozen Scene destination '{}' already exists", destination.string()));
                std::filesystem::create_directory(temporary);
                const std::filesystem::path scene_path = temporary / std::format("{}.spectra", name);
                scene::save_scene(std::move(scene), scene_path, source_scene_path);
                std::filesystem::rename(temporary, destination);
                return destination / scene_path.filename();
            } catch (const std::exception& error) {
                std::error_code cleanup_error{};
                std::filesystem::remove_all(temporary, cleanup_error);
                return std::unexpected{std::string{error.what()}};
            }
        }

        FrozenSceneSnapshot record_gpu_snapshot(VulkanRuntime& runtime, const GpuSceneView gpu_scene, const vk::raii::CommandBuffer& command_buffer, const scene::Scene& current_scene, const scene::Camera& camera, const vk::Extent2D extent, const float exposure) {
            FrozenSceneSnapshot snapshot{};
            snapshot.frozen_scene = current_scene;
            snapshot.frozen_scene.dynamic_setup.reset();
            scene::Camera snapshot_camera = camera;
            snapshot_camera.id            = {std::ranges::fold_left(snapshot.frozen_scene.resources.cameras, std::uint64_t{}, [](const std::uint64_t maximum, const scene::Camera& value) { return std::max(maximum, value.id.value); }) + 1};
            snapshot_camera.name          = "Snapshot Camera";
            snapshot_camera.revision      = {};
            snapshot.frozen_scene.resources.cameras.push_back(snapshot_camera);
            snapshot.frozen_scene.active_camera = snapshot_camera.id;
            scene::Film snapshot_film           = snapshot.frozen_scene.film();
            snapshot_film.id                    = {std::ranges::fold_left(snapshot.frozen_scene.resources.films, std::uint64_t{}, [](const std::uint64_t maximum, const scene::Film& value) { return std::max(maximum, value.id.value); }) + 1};
            snapshot_film.name                  = "Snapshot Film";
            snapshot_film.revision              = {};
            snapshot_film.resolution            = {extent.width, extent.height};
            snapshot_film.pixel_minimum         = {};
            snapshot_film.pixel_maximum         = snapshot_film.resolution;
            snapshot_film.exposure += exposure;
            snapshot.frozen_scene.resources.films.push_back(snapshot_film);
            snapshot.frozen_scene.active_film = snapshot_film.id;

            vk::DeviceSize size{};
            struct ReadbackCopy {
                const GpuBuffer* source{};
                vk::BufferCopy region{};
            };
            std::vector<ReadbackCopy> readback_copies{};
            for (const GpuGeometry& geometry : gpu_scene.geometries) {
                if (!geometry.cpu_data_stale) continue;
                const std::uint32_t resource   = static_cast<std::uint32_t>(std::ranges::find(current_scene.resources.geometries, geometry.geometry_id, &scene::Geometry::id) - current_scene.resources.geometries.begin());
                const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                    if (count == 0) return;
                    size                       = (size + 15u) & ~vk::DeviceSize{15u};
                    const vk::DeviceSize bytes = count * element_size;
                    snapshot.readback_regions.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                    readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                    size += bytes;
                };
                add_readback_region(FrozenSceneReadbackKind::GeometryPosition, geometry.positions, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & 1u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryNormal, geometry.normals, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & 2u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTangent, geometry.tangents, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & 4u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTextureCoordinate, geometry.texture_coordinates, geometry.vertex_count, sizeof(math::Float2));
                add_readback_region(FrozenSceneReadbackKind::GeometryIndex, geometry.indices, geometry.index_count, sizeof(std::uint32_t));
            }
            for (const GpuSphereSet& spheres : gpu_scene.sphere_sets) {
                if (!spheres.cpu_data_stale) continue;
                const std::uint32_t resource   = static_cast<std::uint32_t>(std::ranges::find(current_scene.resources.sphere_sets, spheres.sphere_set_id, &scene::SphereSet::id) - current_scene.resources.sphere_sets.begin());
                const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                    if (count == 0) return;
                    size                       = (size + 15u) & ~vk::DeviceSize{15u};
                    const vk::DeviceSize bytes = count * element_size;
                    snapshot.readback_regions.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                    readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                    size += bytes;
                };
                add_readback_region(FrozenSceneReadbackKind::SpherePosition, spheres.positions, spheres.sphere_count, sizeof(math::Float3));
                add_readback_region(FrozenSceneReadbackKind::SphereRadius, spheres.radii, spheres.sphere_count, sizeof(float));
            }
            for (const GpuVolume& volume : gpu_scene.volumes) {
                if (!volume.cpu_data_stale) continue;
                const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(current_scene.resources.volumes, volume.volume_id, &scene::Volume::id) - current_scene.resources.volumes.begin());
                for (std::size_t field = 0; field != volume.fields.size(); ++field) {
                    if (!volume.field_present[field]) continue;
                    const GpuVolumeField kind         = static_cast<GpuVolumeField>(field);
                    const vk::DeviceSize element_size = kind == GpuVolumeField::SigmaA || kind == GpuVolumeField::SigmaS || kind == GpuVolumeField::Emission ? sizeof(math::Float3) : kind == GpuVolumeField::NanoVdbDensity || kind == GpuVolumeField::NanoVdbTemperature ? sizeof(std::uint32_t) : sizeof(float);
                    const std::uint64_t count         = volume.fields[field].size / element_size;
                    size                              = (size + 15u) & ~vk::DeviceSize{15u};
                    const vk::DeviceSize bytes        = count * element_size;
                    snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::VolumeField, resource, kind, size, count);
                    readback_copies.emplace_back(&volume.fields[field], vk::BufferCopy{0, size, bytes});
                    size += bytes;
                }
            }
            if (size != 0) {
                snapshot.readback_buffer = runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
                const vk::MemoryBarrier2 source_dependency{vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead};
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &source_dependency});
                for (const ReadbackCopy& copy : readback_copies) command_buffer.copyBuffer(*copy.source->buffer, *snapshot.readback_buffer.buffer, copy.region);
                const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
            }
            snapshot.frozen_scene.mark_all_changed();
            return snapshot;
        }
    } // namespace

    void FrozenSceneSnapshot::materialize() {
        const auto copy = [this]<class Element>(std::vector<Element>& destination, const FrozenSceneReadbackRegion& source) {
            destination.resize(source.element_count);
            std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset, source.element_count * sizeof(Element));
        };
        for (const FrozenSceneReadbackRegion& source : this->readback_regions) switch (source.kind) {
            case FrozenSceneReadbackKind::GeometryPosition: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).positions, source); break;
            case FrozenSceneReadbackKind::GeometryNormal: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).normals, source); break;
            case FrozenSceneReadbackKind::GeometryTangent: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).tangents, source); break;
            case FrozenSceneReadbackKind::GeometryTextureCoordinate: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).texture_coordinates, source); break;
            case FrozenSceneReadbackKind::GeometryIndex: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).indices, source); break;
            case FrozenSceneReadbackKind::SpherePosition: copy(this->frozen_scene.resources.sphere_sets[source.resource_index].positions, source); break;
            case FrozenSceneReadbackKind::SphereRadius: copy(this->frozen_scene.resources.sphere_sets[source.resource_index].radii, source); break;
            case FrozenSceneReadbackKind::VolumeField:
                {
                    scene::Volume& volume = this->frozen_scene.resources.volumes[source.resource_index];
                    std::visit(
                        [this, &copy, &source](auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                                if (source.volume_field == GpuVolumeField::Density)
                                    copy(data.density, source);
                                else if (source.volume_field == GpuVolumeField::Temperature)
                                    copy(data.temperature, source);
                                else if (source.volume_field == GpuVolumeField::EmissionScale)
                                    copy(data.emission_scale, source);
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                                if (source.volume_field == GpuVolumeField::SigmaA)
                                    copy(data.sigma_a, source);
                                else if (source.volume_field == GpuVolumeField::SigmaS)
                                    copy(data.sigma_s, source);
                                else if (source.volume_field == GpuVolumeField::Emission)
                                    copy(data.emission, source);
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::NanoVdbVolume>) {
                                if (source.volume_field == GpuVolumeField::NanoVdbDensity)
                                    copy(data.density_data, source);
                                else if (source.volume_field == GpuVolumeField::NanoVdbTemperature)
                                    copy(data.temperature_data, source);
                            }
                        },
                        volume.data);
                }
                break;
            }
        this->frozen_scene.mark_all_changed();
    }

    FrozenSceneExporter::FrozenSceneExporter(VulkanRuntime& runtime, GpuScene& gpu_scene) noexcept : context{runtime, gpu_scene} {
        this->export_state.slots.resize(VulkanFrames::frames_in_flight);
    }

    FrozenSceneExporter::~FrozenSceneExporter() {
        this->wait();
    }

    void FrozenSceneExporter::request(const std::filesystem::path& path) {
        if (this->in_progress()) throw std::runtime_error("A Frozen Scene export is already in progress");
        this->export_state.completed_result.reset();
        this->export_state.pending_request = path;
    }

    bool FrozenSceneExporter::in_progress() const noexcept {
        if (this->export_state.pending_request) return true;
        if (std::ranges::any_of(this->export_state.slots, [](const FrameSlot& slot) { return slot.snapshot.has_value(); })) return true;
        return this->export_state.task.valid();
    }

    std::optional<std::expected<std::filesystem::path, std::string>> FrozenSceneExporter::take_result() {
        if (this->export_state.task.valid() && this->export_state.task.wait_for(std::chrono::seconds{0}) == std::future_status::ready) this->export_state.completed_result = this->export_state.task.get();
        return std::exchange(this->export_state.completed_result, std::nullopt);
    }

    void FrozenSceneExporter::wait() {
        for (FrameSlot& slot : this->export_state.slots)
            if (slot.snapshot) {
                slot.snapshot->materialize();
                if (this->export_state.task.valid()) static_cast<void>(this->export_state.task.get());
                this->export_state.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
                slot.snapshot.reset();
            }
        if (this->export_state.task.valid()) this->export_state.completed_result = this->export_state.task.get();
    }

    std::optional<std::expected<std::filesystem::path, std::string>> FrozenSceneExporter::begin_frame(const std::uint32_t frame_slot_index) {
        FrameSlot& slot = this->export_state.slots[frame_slot_index];
        if (slot.snapshot) {
            slot.snapshot->materialize();
            this->export_state.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
            slot.snapshot.reset();
            slot.output_path.clear();
            slot.source_scene_path.clear();
        }
        return this->take_result();
    }

    void FrozenSceneExporter::record_snapshot(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const scene::Scene& current_scene, const scene::Camera& camera, const vk::Extent2D extent, const float exposure, const std::filesystem::path& source_scene_path) {
        if (!this->export_state.pending_request) return;
        FrameSlot& slot        = this->export_state.slots[frame_slot_index];
        slot.output_path       = *std::exchange(this->export_state.pending_request, std::nullopt);
        slot.source_scene_path = source_scene_path;
        slot.snapshot          = record_gpu_snapshot(this->context.runtime, this->context.gpu_scene.view(), command_buffer, current_scene, camera, extent, exposure);
    }
} // namespace spectra
