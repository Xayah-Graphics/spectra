module;

#include <spectra/plugin_api.h>

module spectra.editor.output.frozen_scene;
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
                scene::save_scene(scene, scene_path, source_scene_path, scene::SceneSaveMode::MaterializeAssets);
                std::filesystem::rename(temporary, destination);
                return destination / scene_path.filename();
            } catch (const std::exception& error) {
                std::error_code cleanup_error{};
                std::filesystem::remove_all(temporary, cleanup_error);
                return std::unexpected{cleanup_error ? std::format("{}; removing temporary export '{}' also failed: {}", error.what(), temporary.string(), cleanup_error.message()) : std::string{error.what()}};
            }
        }

        FrozenSceneSnapshot record_gpu_snapshot(VulkanRuntime& runtime, const GpuSceneView gpu_scene, DynamicsRuntime& dynamics, const vk::raii::CommandBuffer& command_buffer, const scene::Scene& current_scene, const scene::Camera& camera, const vk::Extent2D extent, const float exposure) {
            FrozenSceneSnapshot snapshot{};
            snapshot.frozen_scene = current_scene;
            snapshot.frozen_scene.dynamic_setup.reset();
            snapshot.frozen_scene.frozen_dynamic_snapshot.reset();
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
            const auto add_dynamic_readback = [&snapshot, &readback_copies, &size](const FrozenSceneReadbackKind kind, const std::uint32_t resource_index, const std::uint32_t buffer_index, const dynamics::GpuBufferView source, const std::uint64_t element_count, const vk::DeviceSize element_size) {
                if (element_count > std::numeric_limits<vk::DeviceSize>::max() / element_size) throw std::runtime_error("Frozen Scene readback size overflows");
                const vk::DeviceSize bytes = element_count * element_size;
                if (size > std::numeric_limits<vk::DeviceSize>::max() - 15u - bytes) throw std::runtime_error("Frozen Scene readback size overflows");
                size = size + 15u & ~vk::DeviceSize{15u};
                snapshot.readback_regions.emplace_back(kind, resource_index, buffer_index, GpuVolumeField::Density, size, element_count, element_size);
                if (bytes != 0) readback_copies.emplace_back(source.buffer, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            std::unordered_map<std::uint64_t, std::uint32_t> geometry_indices{};
            std::unordered_map<std::uint64_t, std::uint32_t> sphere_set_indices{};
            std::unordered_map<std::uint64_t, std::uint32_t> volume_indices{};
            std::unordered_map<std::uint64_t, std::uint32_t> instance_indices{};
            for (std::uint32_t index = 0; index != current_scene.resources.geometries.size(); ++index) geometry_indices.emplace(current_scene.resources.geometries[index].id.value, index);
            for (std::uint32_t index = 0; index != current_scene.resources.sphere_sets.size(); ++index) sphere_set_indices.emplace(current_scene.resources.sphere_sets[index].id.value, index);
            for (std::uint32_t index = 0; index != current_scene.resources.volumes.size(); ++index) volume_indices.emplace(current_scene.resources.volumes[index].id.value, index);
            for (std::uint32_t index = 0; index != current_scene.resources.instances.size(); ++index) instance_indices.emplace(current_scene.resources.instances[index].id.value, index);
            for (const GpuGeometry& geometry : gpu_scene.geometries) {
                if (!geometry.cpu_data_stale) continue;
                const std::uint32_t resource   = geometry_indices.at(geometry.geometry_id.value);
                const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                    if (count == 0) return;
                    if (count > std::numeric_limits<vk::DeviceSize>::max() / element_size) throw std::runtime_error("Frozen Scene readback size overflows");
                    const vk::DeviceSize bytes = count * element_size;
                    if (size > std::numeric_limits<vk::DeviceSize>::max() - 15u - bytes) throw std::runtime_error("Frozen Scene readback size overflows");
                    size = (size + 15u) & ~vk::DeviceSize{15u};
                    snapshot.readback_regions.emplace_back(kind, resource, 0, GpuVolumeField::Density, size, count, element_size);
                    readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                    size += bytes;
                };
                add_readback_region(FrozenSceneReadbackKind::GeometryPosition, geometry.positions, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & gpu_geometry_attribute_normal) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryNormal, geometry.normals, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & gpu_geometry_attribute_tangent) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTangent, geometry.tangents, geometry.vertex_count, sizeof(math::Float3));
                if ((geometry.attribute_mask & gpu_geometry_attribute_texture_coordinate) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTextureCoordinate, geometry.texture_coordinates, geometry.vertex_count, sizeof(math::Float2));
                add_readback_region(FrozenSceneReadbackKind::GeometryIndex, geometry.indices, geometry.index_count, sizeof(std::uint32_t));
            }
            for (const GpuSphereSet& spheres : gpu_scene.sphere_sets) {
                if (!spheres.cpu_data_stale) continue;
                const std::uint32_t resource   = sphere_set_indices.at(spheres.sphere_set_id.value);
                const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                    if (count == 0) return;
                    if (count > std::numeric_limits<vk::DeviceSize>::max() / element_size) throw std::runtime_error("Frozen Scene readback size overflows");
                    const vk::DeviceSize bytes = count * element_size;
                    if (size > std::numeric_limits<vk::DeviceSize>::max() - 15u - bytes) throw std::runtime_error("Frozen Scene readback size overflows");
                    size = (size + 15u) & ~vk::DeviceSize{15u};
                    snapshot.readback_regions.emplace_back(kind, resource, 0, GpuVolumeField::Density, size, count, element_size);
                    readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                    size += bytes;
                };
                add_readback_region(FrozenSceneReadbackKind::SpherePosition, spheres.positions, spheres.sphere_count, sizeof(math::Float3));
                add_readback_region(FrozenSceneReadbackKind::SphereRadius, spheres.radii, spheres.sphere_count, sizeof(float));
            }
            for (const GpuVolume& volume : gpu_scene.volumes) {
                if (!volume.cpu_data_stale) continue;
                const std::uint32_t resource = volume_indices.at(volume.volume_id.value);
                for (std::size_t field = 0; field != volume.fields.size(); ++field) {
                    if (!volume.field_present[field]) continue;
                    const GpuVolumeField kind         = static_cast<GpuVolumeField>(field);
                    const vk::DeviceSize element_size = kind == GpuVolumeField::SigmaA || kind == GpuVolumeField::SigmaS || kind == GpuVolumeField::Emission ? sizeof(math::Float3) : kind == GpuVolumeField::NanoVdbDensity || kind == GpuVolumeField::NanoVdbTemperature ? sizeof(std::uint32_t) : sizeof(float);
                    const std::uint64_t count         = volume.fields[field].size / element_size;
                    if (count > std::numeric_limits<vk::DeviceSize>::max() / element_size) throw std::runtime_error("Frozen Scene readback size overflows");
                    const vk::DeviceSize bytes = count * element_size;
                    if (size > std::numeric_limits<vk::DeviceSize>::max() - 15u - bytes) throw std::runtime_error("Frozen Scene readback size overflows");
                    size = (size + 15u) & ~vk::DeviceSize{15u};
                    snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::VolumeField, resource, 0, kind, size, count, element_size);
                    readback_copies.emplace_back(&volume.fields[field], vk::BufferCopy{0, size, bytes});
                    size += bytes;
                }
            }
            std::unordered_set<std::uint64_t> captured_instances{};
            captured_instances.reserve(gpu_scene.primitives.size());
            for (const GpuScenePrimitive& primitive : gpu_scene.primitives) {
                const scene::InstanceId instance_id = gpu_scene.primitive_instance_ids[primitive.scene_primitive_index];
                if (!captured_instances.insert(instance_id.value).second) continue;
                const std::uint32_t resource = instance_indices.at(instance_id.value);
                if (size > std::numeric_limits<vk::DeviceSize>::max() - 15u - sizeof(math::Transform)) throw std::runtime_error("Frozen Scene readback size overflows");
                size = (size + 15u) & ~vk::DeviceSize{15u};
                snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::InstanceTransform, resource, primitive.prototype_primitive_index, GpuVolumeField::Density, size, 1, sizeof(math::Transform));
                readback_copies.emplace_back(gpu_scene.primitive_transform_buffer, vk::BufferCopy{static_cast<vk::DeviceSize>(primitive.scene_primitive_index) * sizeof(math::Transform), size, sizeof(math::Transform)});
                size += sizeof(math::Transform);
            }

            if (const dynamics::FrozenSnapshot* frozen = dynamics.frozen_snapshot()) {
                snapshot.frozen_snapshot = *frozen;
            } else if (current_scene.dynamic_setup) {
                const dynamics::DynamicSnapshot& published = dynamics.published_snapshot();
                snapshot.frozen_snapshot.emplace(dynamics::FrozenSnapshot{published.simulation});
                const auto image_element_size = [](const dynamics::ImageFormat format) -> std::uint64_t {
                    switch (format) {
                    case dynamics::ImageFormat::Rgba8Unorm: return sizeof(std::uint32_t);
                    case dynamics::ImageFormat::Rgba16Float: return sizeof(std::uint16_t) * 4u;
                    case dynamics::ImageFormat::Rgba32Float: return sizeof(float) * 4u;
                    }
                    throw std::runtime_error("Unknown frozen Visualization image format");
                };
                for (const dynamics::GpuVisualization& source : dynamics.visualizations()) {
                    const std::uint32_t visualization_index = static_cast<std::uint32_t>(snapshot.frozen_snapshot->visualizations.size());
                    dynamics::FrozenVisualization destination{};
                    std::visit(
                        [&](const auto& visualization) {
                            destination.style = visualization.style;
                            const auto buffer = [&](const std::uint32_t buffer_index, const dynamics::GpuBufferView view, const std::uint64_t count, const vk::DeviceSize element_size) { add_dynamic_readback(FrozenSceneReadbackKind::VisualizationBuffer, visualization_index, buffer_index, view, count, element_size); };
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuPointVisualization>) {
                                destination.data = dynamics::FrozenElements{{}, visualization.count};
                                buffer(0, visualization.points, visualization.count, sizeof(SpectraPluginPoint));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuSegmentVisualization>) {
                                destination.data = dynamics::FrozenElements{{}, visualization.count};
                                buffer(0, visualization.segments, visualization.count, sizeof(SpectraPluginSegment));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuCurveVisualization>) {
                                destination.data = dynamics::FrozenElements{{}, visualization.count};
                                buffer(0, visualization.curves, visualization.count, sizeof(SpectraPluginCurve));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuVectorVisualization>) {
                                destination.data = dynamics::FrozenElements{{}, visualization.count};
                                buffer(0, visualization.vectors, visualization.count, sizeof(SpectraPluginVector));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuFieldVisualization>) {
                                destination.data          = dynamics::FrozenField{visualization.resolution, visualization.local_from_grid, visualization.channel.channel, {}};
                                const std::uint64_t count = static_cast<std::uint64_t>(visualization.resolution.x) * visualization.resolution.y * visualization.resolution.z;
                                buffer(0, visualization.channel.values, count, visualization.channel.channel.kind == dynamics::FieldChannelKind::Float ? sizeof(float) : sizeof(SpectraPluginFloat3));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuImageVisualization>) {
                                destination.data = dynamics::FrozenImage{visualization.image, {}};
                                buffer(0, visualization.pixels, static_cast<std::uint64_t>(visualization.image.extent[0]) * visualization.image.extent[1], image_element_size(visualization.image.format));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuCameraObservationVisualization>) {
                                destination.data = dynamics::FrozenCameraObservations{visualization.dataset, {}, {}, visualization.count};
                                buffer(0, visualization.observations, visualization.count, sizeof(SpectraPluginCameraObservation));
                                buffer(1, visualization.images, static_cast<std::uint64_t>(visualization.count) * visualization.dataset.images.extent[0] * visualization.dataset.images.extent[1], image_element_size(visualization.dataset.images.format));
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(visualization)>, dynamics::GpuTransformVisualization>) {
                                destination.data = dynamics::FrozenElements{{}, visualization.count};
                                buffer(0, visualization.transforms, visualization.count, sizeof(SpectraPluginTransform));
                            } else {
                                destination.data = dynamics::FrozenSurface{{}, visualization.indices ? std::optional{std::vector<std::byte>{}} : std::nullopt, visualization.scalars ? std::optional{std::vector<std::byte>{}} : std::nullopt, visualization.vertex_count, visualization.index_count};
                                buffer(0, visualization.positions, visualization.vertex_count, sizeof(SpectraPluginFloat3));
                                if (visualization.indices) buffer(1, *visualization.indices, visualization.index_count, sizeof(std::uint32_t));
                                if (visualization.scalars) buffer(2, *visualization.scalars, visualization.vertex_count, sizeof(float));
                            }
                        },
                        source.data);
                    snapshot.frozen_snapshot->visualizations.push_back(std::move(destination));
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
                    const std::uint32_t frozen_system_index = static_cast<std::uint32_t>(snapshot.frozen_snapshot->telemetry.size());
                    snapshot.frozen_snapshot->telemetry.push_back(std::move(system));
                    if (update != published.telemetry.end()) add_dynamic_readback(FrozenSceneReadbackKind::TelemetryValues, frozen_system_index, 0, update->values, update->value_count, sizeof(SpectraPluginTelemetryGpuValue));
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
            std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.byte_offset, source.element_count * sizeof(Element));
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
                    scene::Instance& instance         = this->frozen_scene.resources.instances[source.resource_index];
                    const scene::Prototype& prototype = *std::ranges::find(this->frozen_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                    math::Transform world_from_primitive{};
                    std::memcpy(&world_from_primitive, static_cast<const std::byte*>(this->readback_buffer.mapped) + source.byte_offset, sizeof(world_from_primitive));
                    instance.transform = world_from_primitive * prototype.primitives[source.subresource_index].transform.inverse();
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
                    dynamics::FrozenVisualization& visualization = this->frozen_snapshot->visualizations[source.resource_index];
                    std::vector<std::byte>* destination{};
                    std::visit(
                        [&destination, &source](auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, dynamics::FrozenElements>)
                                destination = &data.elements;
                            else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, dynamics::FrozenField>)
                                destination = &data.values;
                            else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, dynamics::FrozenImage>)
                                destination = &data.pixels;
                            else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, dynamics::FrozenCameraObservations>)
                                destination = source.subresource_index == 0 ? &data.observations : &data.images;
                            else if (source.subresource_index == 0)
                                destination = &data.positions;
                            else if (source.subresource_index == 1)
                                destination = &*data.indices;
                            else
                                destination = &*data.scalars;
                        },
                        visualization.data);
                    if (source.element_count > std::numeric_limits<std::size_t>::max() / source.element_size) throw std::runtime_error("Frozen Scene materialization size overflows");
                    const std::size_t byte_count = static_cast<std::size_t>(source.element_count * source.element_size);
                    destination->resize(byte_count);
                    std::memcpy(destination->data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.byte_offset, byte_count);
                }
                break;
            case FrozenSceneReadbackKind::TelemetryValues:
                {
                    dynamics::FrozenTelemetrySystem& system = this->frozen_snapshot->telemetry[source.resource_index];
                    const auto* values                      = reinterpret_cast<const SpectraPluginTelemetryGpuValue*>(static_cast<const std::byte*>(this->readback_buffer.mapped) + source.byte_offset);
                    dynamics::TelemetrySample sample{this->frozen_snapshot->simulation.step, this->frozen_snapshot->simulation.seconds};
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
            }
        if (this->frozen_snapshot) this->frozen_scene.frozen_dynamic_snapshot = scene::FrozenDynamicSnapshot{.payload = dynamics::serialize_frozen_snapshot(*this->frozen_snapshot)};
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
