module;

#include <spectra/plugin_api.h>

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
                scene::save_scene(std::move(scene), scene_path, source_scene_path, scene::SceneSaveMode::MaterializeAssets);
                std::filesystem::rename(temporary, destination);
                return destination / scene_path.filename();
            } catch (const std::exception& error) {
                std::error_code cleanup_error{};
                std::filesystem::remove_all(temporary, cleanup_error);
                return std::unexpected{std::string{error.what()}};
            }
        }

        FrozenSceneSnapshot record_gpu_snapshot(VulkanRuntime& runtime, const GpuSceneView gpu_scene, DynamicsRuntime& dynamics, const vk::raii::CommandBuffer& command_buffer, const scene::Scene& current_scene, const scene::Camera& camera, const vk::Extent2D extent, const float exposure) {
            FrozenSceneSnapshot snapshot{};
            snapshot.frozen_scene = current_scene;
            snapshot.frozen_scene.dynamic_setup.reset();
            snapshot.frozen_scene.frozen_dynamic_frame.reset();
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
            const auto add_dynamic_readback = [&snapshot, &readback_copies, &size](const FrozenSceneReadbackKind kind, const std::uint32_t resource_index, const std::uint32_t buffer_index, const dynamics::GpuBufferView source, const vk::DeviceSize bytes) {
                size = size + 15u & ~vk::DeviceSize{15u};
                snapshot.readback_regions.emplace_back(kind, resource_index, buffer_index, GpuVolumeField::Density, size, bytes);
                if (bytes != 0) readback_copies.emplace_back(source.buffer, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            for (const GpuGeometry& geometry : gpu_scene.geometries) {
                if (!geometry.cpu_data_stale) continue;
                const std::uint32_t resource   = static_cast<std::uint32_t>(std::ranges::find(current_scene.resources.geometries, geometry.geometry_id, &scene::Geometry::id) - current_scene.resources.geometries.begin());
                const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                    if (count == 0) return;
                    size                       = (size + 15u) & ~vk::DeviceSize{15u};
                    const vk::DeviceSize bytes = count * element_size;
                    snapshot.readback_regions.emplace_back(kind, resource, 0, GpuVolumeField::Density, size, count);
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
                    snapshot.readback_regions.emplace_back(kind, resource, 0, GpuVolumeField::Density, size, count);
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
                    snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::VolumeField, resource, 0, kind, size, count);
                    readback_copies.emplace_back(&volume.fields[field], vk::BufferCopy{0, size, bytes});
                    size += bytes;
                }
            }
            std::vector<scene::InstanceId> captured_instances{};
            captured_instances.reserve(gpu_scene.primitives.size());
            for (const GpuScenePrimitive& primitive : gpu_scene.primitives) {
                const scene::InstanceId instance_id = gpu_scene.primitive_instance_ids[primitive.scene_primitive_index];
                if (std::ranges::contains(captured_instances, instance_id)) continue;
                captured_instances.push_back(instance_id);
                const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(current_scene.resources.instances, instance_id, &scene::Instance::id) - current_scene.resources.instances.begin());
                size = (size + 15u) & ~vk::DeviceSize{15u};
                snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::InstanceTransform, resource, primitive.prototype_primitive_index, GpuVolumeField::Density, size, 1);
                readback_copies.emplace_back(gpu_scene.primitive_transform_buffer, vk::BufferCopy{static_cast<vk::DeviceSize>(primitive.scene_primitive_index) * sizeof(math::Transform), size, sizeof(math::Transform)});
                size += sizeof(math::Transform);
            }

            if (const dynamics::FrozenFrame* frozen = dynamics.frozen_frame()) {
                snapshot.frozen_frame = *frozen;
            } else if (current_scene.dynamic_setup) {
                const dynamics::DynamicFrame& published = dynamics.published_frame();
                snapshot.frozen_frame.emplace(dynamics::FrozenFrame{published.simulation, published.presentation});
                for (const dynamics::GpuSceneUpdate& update : published.scene_updates)
                    if (const auto* bounds = std::get_if<dynamics::GpuSceneBoundsUpdate>(&update.data)) {
                        const std::uint32_t bounds_index = static_cast<std::uint32_t>(snapshot.frozen_frame->bounds.size());
                        snapshot.frozen_frame->bounds.push_back({bounds->domain, std::vector<dynamics::SceneBound>(bounds->count)});
                        add_dynamic_readback(FrozenSceneReadbackKind::SceneBounds, bounds_index, 0, bounds->bounds, bounds->count * sizeof(dynamics::SceneBound));
                    }
                const auto image_element_size = [](const dynamics::ImageFormat format) -> std::uint64_t {
                    if (format == dynamics::ImageFormat::Rgba8Unorm) return sizeof(std::uint32_t);
                    if (format == dynamics::ImageFormat::Rgba16Float) return sizeof(std::uint16_t) * 4u;
                    return sizeof(float) * 4u;
                };
                for (const dynamics::GpuVisualization& source : dynamics.visualizations()) {
                    const std::uint32_t visualization_index = static_cast<std::uint32_t>(snapshot.frozen_frame->visualizations.size());
                    dynamics::FrozenVisualization destination{};
                    std::visit(
                        [&](const auto& visualization) {
                            destination.style   = visualization.style;
                            const auto buffer = [&](const dynamics::GpuBufferView view, const vk::DeviceSize bytes) {
                                const std::uint32_t buffer_index = static_cast<std::uint32_t>(destination.buffers.size());
                                destination.buffers.emplace_back();
                                add_dynamic_readback(FrozenSceneReadbackKind::VisualizationBuffer, visualization_index, buffer_index, view, view.buffer == nullptr ? 0 : bytes);
                            };
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuPointVisualization>) {
                                destination.kind          = dynamics::FrozenVisualizationKind::Points;
                                destination.primary_count = visualization.count;
                                buffer(visualization.points, visualization.count * sizeof(SpectraPluginPoint));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuSegmentVisualization>) {
                                destination.kind          = dynamics::FrozenVisualizationKind::Segments;
                                destination.primary_count = visualization.count;
                                buffer(visualization.segments, visualization.count * sizeof(SpectraPluginSegment));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuCurveVisualization>) {
                                destination.kind          = dynamics::FrozenVisualizationKind::Curves;
                                destination.primary_count = visualization.count;
                                buffer(visualization.curves, visualization.count * sizeof(SpectraPluginCurve));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuVectorVisualization>) {
                                destination.kind          = dynamics::FrozenVisualizationKind::Vectors;
                                destination.primary_count = visualization.count;
                                buffer(visualization.vectors, visualization.count * sizeof(SpectraPluginVector));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuFieldVisualization>) {
                                destination.kind            = dynamics::FrozenVisualizationKind::Field;
                                destination.resolution      = visualization.resolution;
                                destination.local_from_grid = visualization.local_from_grid;
                                destination.channel         = visualization.channel.channel;
                                const std::uint64_t count    = static_cast<std::uint64_t>(visualization.resolution.x) * visualization.resolution.y * visualization.resolution.z;
                                buffer(visualization.channel.values, count * (visualization.channel.channel.kind == dynamics::FieldChannelKind::Float ? sizeof(float) : sizeof(SpectraPluginFloat3)));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuImageVisualization>) {
                                destination.kind  = dynamics::FrozenVisualizationKind::Image;
                                destination.image = visualization.image;
                                buffer(visualization.pixels, static_cast<std::uint64_t>(visualization.image.extent[0]) * visualization.image.extent[1] * image_element_size(visualization.image.format));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuCameraObservationVisualization>) {
                                destination.kind                = dynamics::FrozenVisualizationKind::CameraObservations;
                                destination.camera_observations = visualization.dataset;
                                destination.primary_count       = visualization.count;
                                buffer(visualization.observations, visualization.count * sizeof(SpectraPluginCameraObservation));
                                buffer(visualization.images, visualization.count * visualization.dataset.images.extent[0] * visualization.dataset.images.extent[1] * image_element_size(visualization.dataset.images.format));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuTransformVisualization>) {
                                destination.kind          = dynamics::FrozenVisualizationKind::Transforms;
                                destination.primary_count = visualization.count;
                                buffer(visualization.transforms, visualization.count * sizeof(SpectraPluginTransform));
                            } else {
                                destination.kind            = dynamics::FrozenVisualizationKind::Surface;
                                destination.primary_count   = visualization.vertex_count;
                                destination.secondary_count = visualization.index_count;
                                buffer(visualization.positions, visualization.vertex_count * sizeof(SpectraPluginFloat3));
                                buffer(visualization.indices, visualization.index_count * sizeof(std::uint32_t));
                                buffer(visualization.scalars, visualization.vertex_count * sizeof(float));
                            }
                        },
                        source.data);
                    snapshot.frozen_frame->visualizations.push_back(std::move(destination));
                }
                for (std::size_t system_index = 0; system_index != current_scene.dynamic_setup->systems.size(); ++system_index) {
                    const scene::DynamicSystem& source_system = current_scene.dynamic_setup->systems[system_index];
                    if (!source_system.enabled) continue;
                    const dynamics::ProviderDescriptor& provider = dynamics.provider_descriptor(source_system.provider_id);
                    dynamics::FrozenTelemetrySystem system{source_system.id.value, source_system.name, source_system.provider_id, provider.telemetry, dynamics.telemetry(system_index)};
                    const auto update = std::ranges::find(published.telemetry, system_index, &dynamics::GpuTelemetryUpdate::system_index);
                    if (update != published.telemetry.end()) {
                        system.snapshot.phase    = update->phase;
                        system.snapshot.headline = update->headline;
                        system.snapshot.message  = update->message;
                        system.snapshot.values.resize(update->value_count);
                    }
                    const std::uint32_t frozen_system_index = static_cast<std::uint32_t>(snapshot.frozen_frame->telemetry.size());
                    snapshot.frozen_frame->telemetry.push_back(std::move(system));
                    if (update != published.telemetry.end()) add_dynamic_readback(FrozenSceneReadbackKind::TelemetryValues, frozen_system_index, 0, update->values, update->value_count * sizeof(SpectraPluginTelemetryGpuValue));
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
            case FrozenSceneReadbackKind::InstanceTransform:
                {
                    scene::Instance& instance = this->frozen_scene.resources.instances[source.resource_index];
                    const scene::Prototype& prototype = *std::ranges::find(this->frozen_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                    math::Transform world_from_primitive{};
                    std::memcpy(&world_from_primitive, static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset, sizeof(world_from_primitive));
                    instance.transform = world_from_primitive * prototype.primitives[source.primitive_index].transform.inverse();
                }
                break;
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
            case FrozenSceneReadbackKind::VisualizationBuffer:
                {
                    std::vector<std::byte>& destination = this->frozen_frame->visualizations[source.resource_index].buffers[source.primitive_index];
                    destination.resize(source.element_count);
                    std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset, source.element_count);
                }
                break;
            case FrozenSceneReadbackKind::TelemetryValues:
                {
                    dynamics::FrozenTelemetrySystem& system = this->frozen_frame->telemetry[source.resource_index];
                    const auto* values                       = reinterpret_cast<const SpectraPluginTelemetryGpuValue*>(static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset);
                    dynamics::TelemetrySample sample{this->frozen_frame->simulation.step, this->frozen_frame->simulation.seconds};
                    sample.values.reserve(system.descriptors.size());
                    for (std::size_t index = 0; index != system.descriptors.size(); ++index) {
                        const dynamics::TelemetryValue value{system.descriptors[index].kind, values[index].integer, {values[index].floating[0], values[index].floating[1], values[index].floating[2]}};
                        system.snapshot.values[index] = value;
                        sample.values.push_back(value);
                    }
                    if (!system.snapshot.history.empty() && system.snapshot.history.back().simulation_step == sample.simulation_step)
                        system.snapshot.history.back() = std::move(sample);
                    else
                        system.snapshot.history.push_back(std::move(sample));
                }
                break;
            case FrozenSceneReadbackKind::SceneBounds:
                {
                    std::vector<dynamics::SceneBound>& destination = this->frozen_frame->bounds[source.resource_index].values;
                    std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset, source.element_count);
                }
                break;
            }
        if (this->frozen_frame) this->frozen_scene.frozen_dynamic_frame = scene::FrozenDynamicFrame{.payload = dynamics::serialize_frozen_frame(*this->frozen_frame)};
        this->frozen_scene.mark_all_changed();
    }

    FrozenSceneExporter::FrozenSceneExporter(VulkanRuntime& runtime, GpuScene& gpu_scene, DynamicsRuntime& dynamics) noexcept : context{runtime, gpu_scene, dynamics} {
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
        for (std::uint32_t frame_slot_index = 0; frame_slot_index != this->export_state.slots.size(); ++frame_slot_index) {
            FrameSlot& slot = this->export_state.slots[frame_slot_index];
            if (slot.snapshot) {
                if (this->context.runtime.frames.frame.recording && frame_slot_index == this->context.runtime.frames.frame.current_slot_index) {
                    slot.snapshot.reset();
                    continue;
                }
                this->context.runtime.frames.wait_frame(frame_slot_index);
                slot.snapshot->materialize();
                if (this->export_state.task.valid()) static_cast<void>(this->export_state.task.get());
                this->export_state.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
                slot.snapshot.reset();
            }
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
        slot.snapshot          = record_gpu_snapshot(this->context.runtime, this->context.gpu_scene.view(), this->context.dynamics, command_buffer, current_scene, camera, extent, exposure);
    }
} // namespace spectra
