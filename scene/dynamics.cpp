module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <spectra/plugin_api.h>

module spectra.scene.dynamics;

import std;
import vulkan;

namespace spectra::scene::dynamics {
    namespace {
        [[nodiscard]] std::string text(const SpectraPluginString value) {
            return {value.data, value.size};
        }

        [[nodiscard]] DynamicParameterValue parameter_value(const SpectraPluginParameterValue value) noexcept {
            return {static_cast<DynamicParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraPluginParameterValue parameter_value(const DynamicParameterValue& value) noexcept {
            return {static_cast<SpectraPluginParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] std::uint64_t attribute_size(const SpectraPluginAttribute attribute) noexcept {
            if (attribute == SpectraPluginAttribute::Position || attribute == SpectraPluginAttribute::Normal || attribute == SpectraPluginAttribute::Tangent || attribute == SpectraPluginAttribute::Color || attribute == SpectraPluginAttribute::Velocity || attribute == SpectraPluginAttribute::SigmaA || attribute == SpectraPluginAttribute::SigmaS || attribute == SpectraPluginAttribute::Emission) return sizeof(SpectraPluginFloat3);
            if (attribute == SpectraPluginAttribute::TextureCoordinate) return sizeof(SpectraPluginFloat2);
            if (attribute == SpectraPluginAttribute::Index) return sizeof(std::uint32_t);
            if (attribute == SpectraPluginAttribute::Material) return sizeof(std::uint64_t);
            if (attribute == SpectraPluginAttribute::Transform) return sizeof(SpectraPluginTransform);
            if (attribute == SpectraPluginAttribute::Bounds) return sizeof(SpectraPluginFloat3) * 2;
            return sizeof(float);
        }

        [[nodiscard]] std::vector<Float3> float3_values(const SpectraPluginFloat3* source, const std::size_t count) {
            std::vector<Float3> values(count);
            for (std::size_t index = 0; index < count; ++index) values[index] = {source[index].x, source[index].y, source[index].z};
            return values;
        }

        [[nodiscard]] std::vector<Float2> float2_values(const SpectraPluginFloat2* source, const std::size_t count) {
            std::vector<Float2> values(count);
            for (std::size_t index = 0; index < count; ++index) values[index] = {source[index].x, source[index].y};
            return values;
        }

        struct Win32Handle {
            ~Win32Handle() {
                if (this->value) CloseHandle(this->value);
            }

            Win32Handle() = default;
            explicit Win32Handle(const HANDLE value) : value(value) {}
            Win32Handle(const Win32Handle&) = delete;
            Win32Handle(Win32Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
            Win32Handle& operator=(const Win32Handle&) = delete;
            Win32Handle& operator=(Win32Handle&& other) noexcept {
                if (this == &other) return *this;
                if (this->value) CloseHandle(this->value);
                this->value = std::exchange(other.value, nullptr);
                return *this;
            }

            HANDLE value{};
        };
    } // namespace

    Runtime::Library::Library(const std::filesystem::path& source, const std::string_view expected_provider) : path(std::filesystem::weakly_canonical(source)) {
        const HMODULE loaded = LoadLibraryW(this->path.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load Provider Library: {}", this->path.string()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*) ()>(GetProcAddress(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
        if (!entry) {
            FreeLibrary(loaded);
            throw std::runtime_error(std::format("Provider Library does not export {}: {}", SPECTRA_PLUGIN_ENTRY_NAME, this->path.string()));
        }
        const SpectraPluginApi* loaded_api = entry();
        if (!loaded_api || loaded_api->version != SPECTRA_PLUGIN_API_VERSION || loaded_api->size != sizeof(SpectraPluginApi) || !loaded_api->describe_provider || !loaded_api->create_provider || !loaded_api->destroy_provider || !loaded_api->configure_port || !loaded_api->set_input_frame || !loaded_api->apply_parameters || !loaded_api->reset || !loaded_api->step || !loaded_api->publish_frame) {
            FreeLibrary(loaded);
            throw std::runtime_error(std::format("Provider Library has an incomplete Plugin API 11 entry: {}", this->path.string()));
        }
        const SpectraPluginProviderDescriptor described = loaded_api->describe_provider();
        if (text(described.id) != expected_provider) {
            FreeLibrary(loaded);
            throw std::runtime_error(std::format("Provider Library '{}' reports Provider '{}' instead of '{}'", this->path.string(), text(described.id), expected_provider));
        }
        this->module = loaded;
        this->api    = loaded_api;
    }

    Runtime::Library::~Library() {
        if (this->module) FreeLibrary(static_cast<HMODULE>(this->module));
    }

    Runtime::Library& Runtime::library(const std::string_view provider) const {
        const auto found = this->provider_libraries.find(std::string{provider});
        if (found == this->provider_libraries.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider));
        return *found->second;
    }

    const ProviderDescriptor& Runtime::provider(const std::string_view id) const {
        const auto found = std::ranges::find(this->providers, id, &ProviderDescriptor::id);
        if (found == this->providers.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", id));
        return *found;
    }

    void Runtime::FrameCollector::debug(void* opaque, const std::uint64_t port, const SpectraPluginDebugPrimitive* primitives, const std::uint64_t count) {
        FrameCollector& collector = *static_cast<FrameCollector*>(opaque);
        Binding& output           = collector.runtime->binding(*collector.system, port);
        if (output.descriptor.kind != ResourceKind::DebugDraw || output.descriptor.direction != PortDirection::Output) throw std::runtime_error("Provider published Debug Draw through a non-Debug output Port");
        if (!collector.runtime->setup.systems[collector.system->scene_index].visible) return;
        const InstanceId anchor_id = std::get<InstanceId>(output.resource);
        const Instance& anchor     = *std::ranges::find(collector.runtime->source_scene->resources.instances, anchor_id, &Instance::id);
        const float radius_scale          = std::max({anchor.transform.transform_vector({1.0f, 0.0f, 0.0f}).length(), anchor.transform.transform_vector({0.0f, 1.0f, 0.0f}).length(), anchor.transform.transform_vector({0.0f, 0.0f, 1.0f}).length()});
        for (std::uint64_t index = 0; index < count; ++index) {
            const SpectraPluginDebugPrimitive& source = primitives[index];
            Float3 first                       = anchor.transform.transform_point({source.first.x, source.first.y, source.first.z});
            Float3 second                      = anchor.transform.transform_point({source.second.x, source.second.y, source.second.z});
            if (source.kind == SpectraPluginDebugPrimitiveKind::AxisAlignedBox) {
                Bounds3 bounds = Bounds3::empty();
                for (const float x : {source.first.x, source.second.x})
                    for (const float y : {source.first.y, source.second.y})
                        for (const float z : {source.first.z, source.second.z}) bounds.include(anchor.transform.transform_point({x, y, z}));
                first  = bounds.minimum;
                second = bounds.maximum;
            }
            std::uint64_t pick{};
            if (source.object != 0) {
                const auto [entry, inserted] = collector.runtime->debug_objects.try_emplace({collector.system->scene_index, source.object}, collector.runtime->next_debug_object);
                if (inserted) ++collector.runtime->next_debug_object;
                pick = entry->second;
            }
            collector.frame->debug.emplace_back(collector.runtime->setup.systems[collector.system->scene_index].id, static_cast<DebugPrimitiveKind>(source.kind), static_cast<DebugDepthMode>(source.depth_mode), first, second, Float3{source.color.x, source.color.y, source.color.z}, source.radius * radius_scale, source.object, pick);
        }
    }

    void Runtime::FrameCollector::commit(void* opaque, const std::uint64_t port, const SpectraPluginOutputCommit* commit) {
        FrameCollector& collector = *static_cast<FrameCollector*>(opaque);
        collector.runtime->commit(*collector.system, collector.runtime->binding(*collector.system, port), *commit, *collector.frame);
    }

    void Runtime::FrameCollector::capacity(void* opaque, const std::uint64_t port, const std::uint64_t capacity, const std::uint64_t secondary_capacity) {
        FrameCollector& collector           = *static_cast<FrameCollector*>(opaque);
        Binding& output                     = collector.runtime->binding(*collector.system, port);
        output.requested_capacity           = std::max(output.requested_capacity, capacity);
        output.requested_secondary_capacity = std::max(output.requested_secondary_capacity, secondary_capacity);
    }

    Runtime::Runtime(Spectra& runtime, const std::filesystem::path& scene_path, const Scene& scene) : runtime(&runtime), source_scene(&scene), setup(*scene.dynamic_setup) {
        std::vector<std::string> required_providers{};
        required_providers.reserve(this->setup.systems.size());
        for (const DynamicSystem& system : this->setup.systems)
            if (!std::ranges::contains(required_providers, system.provider)) required_providers.emplace_back(system.provider);
        std::ranges::sort(required_providers);
        for (const std::string& required_provider : required_providers) {
            const std::filesystem::path path                      = scene_path.parent_path() / std::filesystem::path{required_provider + ".spectra-plugin.dll"};
            Library& library                                      = this->libraries.emplace_back(path, required_provider);
            const SpectraPluginProviderDescriptor source_provider = library.api->describe_provider();
            ProviderDescriptor provider{
                .id                = text(source_provider.id),
                .name              = text(source_provider.name),
                .interface_id      = text(source_provider.interface_id),
                .interface_version = source_provider.interface_version,
            };
            provider.ports.reserve(source_provider.port_count);
            for (std::uint64_t port_index = 0; port_index < source_provider.port_count; ++port_index) {
                const SpectraPluginPortDescriptor& port = source_provider.ports[port_index];
                provider.ports.emplace_back(text(port.id), text(port.name), static_cast<PortDirection>(port.direction), static_cast<ResourceKind>(port.kind), static_cast<MemoryDomain>(port.memory_domain), port.capacity, port.secondary_capacity, port.attribute_mask, static_cast<MeshUpdateMode>(port.mesh_update_mode), UInt3{port.resolution[0], port.resolution[1], port.resolution[2]});
            }
            provider.parameters.reserve(source_provider.parameter_count);
            for (std::uint64_t parameter_index = 0; parameter_index < source_provider.parameter_count; ++parameter_index) {
                const SpectraPluginParameterDescriptor& parameter = source_provider.parameters[parameter_index];
                Parameter value{
                    .id          = text(parameter.id),
                    .name        = text(parameter.name),
                    .unit        = text(parameter.unit),
                    .application = static_cast<ParameterApplication>(parameter.application),
                    .value       = parameter_value(parameter.value),
                    .minimum     = parameter_value(parameter.minimum),
                    .maximum     = parameter_value(parameter.maximum),
                };
                value.enumerators.reserve(parameter.enumerator_count);
                for (std::uint64_t enumerator = 0; enumerator < parameter.enumerator_count; ++enumerator) value.enumerators.emplace_back(text(parameter.enumerators[enumerator]));
                provider.parameters.emplace_back(std::move(value));
            }
            provider.telemetry.reserve(source_provider.telemetry_count);
            for (std::uint64_t telemetry_index = 0; telemetry_index < source_provider.telemetry_count; ++telemetry_index) {
                const SpectraPluginTelemetryDescriptor& telemetry = source_provider.telemetry[telemetry_index];
                provider.telemetry.emplace_back(text(telemetry.id), text(telemetry.name), text(telemetry.unit));
            }
            if (source_provider.telemetry_count != 0 && !library.api->telemetry) throw std::runtime_error(std::format("Provider '{}' declares telemetry without a telemetry callback", provider.id));
            if (!this->provider_libraries.emplace(provider.id, &library).second) throw std::runtime_error(std::format("Provider '{}' is loaded more than once", provider.id));
            this->providers.emplace_back(std::move(provider));
        }

        std::unordered_map<std::string, std::size_t> writers{};
        for (std::size_t system_index = 0; system_index < this->setup.systems.size(); ++system_index) {
            const DynamicSystem& declared = this->setup.systems[system_index];
            if (!declared.enabled) continue;
            const ProviderDescriptor& provider = this->provider(declared.provider);
            for (const PortDescriptor& port : provider.ports) {
                if (port.direction != PortDirection::Output || port.kind == ResourceKind::DebugDraw) continue;
                const auto found = std::ranges::find(declared.bindings, port.id, &DynamicPortBinding::port);
                if (found == declared.bindings.end()) throw std::runtime_error(std::format("Provider Port '{}' is unbound", port.id));
                if (std::ranges::count(declared.bindings, port.id, &DynamicPortBinding::port) != 1) throw std::runtime_error(std::format("Provider Port '{}' must bind exactly one Scene resource", port.id));
                const DynamicPortBinding& source = *found;
                const std::string key                   = std::format("{}:{}", std::to_underlying(source.resource_kind), source.resource);
                const auto [writer, inserted]           = writers.try_emplace(key, system_index);
                if (!inserted && writer->second != system_index) throw std::runtime_error(std::format("Dynamic Systems cannot share output resource {}", source.resource));
            }
        }

        this->systems.reserve(this->setup.systems.size());
        for (std::size_t system_index = 0; system_index < this->setup.systems.size(); ++system_index) {
            const DynamicSystem& declared = this->setup.systems[system_index];
            const ProviderDescriptor& provider   = this->provider(declared.provider);
            SystemState state{.id = declared.id, .name = declared.name, .provider_name = provider.name, .enabled = declared.enabled, .visible = declared.visible};
            state.parameters.reserve(provider.parameters.size());
            for (const Parameter& descriptor : provider.parameters) {
                const auto configured                    = std::ranges::find(declared.parameters, descriptor.id, &DynamicParameterSetting::id);
                const DynamicParameterValue value = configured == declared.parameters.end() ? descriptor.value : configured->value;
                state.parameters.emplace_back(descriptor.id, descriptor.name, descriptor.unit, descriptor.application, value, descriptor.minimum, descriptor.maximum, descriptor.enumerators);
            }
            for (const Telemetry& descriptor : provider.telemetry) state.telemetry.emplace_back(descriptor.id, descriptor.name, descriptor.unit, 0.0);
            this->systems.emplace_back(std::move(state));

            if (!declared.visible)
                for (const DynamicPortBinding& binding : declared.bindings) {
                    const auto port = std::ranges::find(provider.ports, binding.port, &PortDescriptor::id);
                    if (port == provider.ports.end() || port->direction != PortDirection::Output || port->kind == ResourceKind::DebugDraw) continue;
                    for (const Instance& instance : scene.resources.instances) {
                        const auto prototype = std::ranges::find(scene.resources.prototypes, instance.prototype, &Prototype::id);
                        if (prototype == scene.resources.prototypes.end()) continue;
                        const bool owns = (binding.resource_kind == DynamicResourceKind::Instance && binding.resource == instance.id.value) || std::ranges::any_of(prototype->primitives, [&binding](const Primitive& primitive) { return (binding.resource_kind == DynamicResourceKind::Geometry && binding.resource == primitive.geometry.value) || (binding.resource_kind == DynamicResourceKind::ParticleSet && binding.resource == primitive.particles.value) || (binding.resource_kind == DynamicResourceKind::Volume && binding.resource == primitive.volume.value); });
                        if (owns && !std::ranges::contains(this->hidden_instances, instance.id)) this->hidden_instances.emplace_back(instance.id);
                    }
                }
            if (!declared.enabled) continue;

            Library& library = this->library(provider.id);
            System system{.scene_index = system_index, .provider = &provider, .api = library.api};
            system.instance = system.api->create_provider();
            if (!system.instance) throw std::runtime_error(std::format("Provider '{}' refused to create its declared instance", provider.id));
            for (const Parameter& parameter : this->systems[system_index].parameters) system.parameter_values.emplace_back(parameter.value);
            this->system_storage.emplace_back(std::move(system));
        }

        for (System& system : this->system_storage) {
            const DynamicSystem& declared = this->setup.systems[system.scene_index];
            for (std::size_t port_index = 0; port_index < system.provider->ports.size(); ++port_index) {
                const PortDescriptor& port = system.provider->ports[port_index];
                Binding binding{.port = port_index, .descriptor = port, .capacity = port.capacity, .secondary_capacity = port.secondary_capacity};
                const auto source = std::ranges::find(declared.bindings, port.id, &DynamicPortBinding::port);
                if (source == declared.bindings.end()) throw std::runtime_error(std::format("Provider Port '{}' is unbound", port.id));
                if (std::ranges::count(declared.bindings, port.id, &DynamicPortBinding::port) != 1) throw std::runtime_error(std::format("Provider Port '{}' must bind exactly one Scene resource", port.id));
                this->bind_resource(binding, *source);
                if (port.direction == PortDirection::Input) {
                    if (port.memory_domain != MemoryDomain::Host) throw std::runtime_error("In-process research Providers only accept Host Scene inputs");
                    const std::string key = std::format("{}:{}", std::to_underlying(source->resource_kind), source->resource);
                    const auto writer     = writers.find(key);
                    if (writer != writers.end() && writer->second != system.scene_index) throw std::runtime_error("A Dynamic System input cannot consume another Dynamic System output");
                }
                system.bindings.emplace_back(std::move(binding));
            }
            for (std::size_t binding = 0; binding < system.bindings.size(); ++binding) {
                this->configure(system, binding);
                if (system.bindings[binding].descriptor.direction == PortDirection::Output) this->declare_output(system.bindings[binding]);
            }
            this->apply_parameters(system);
            this->set_inputs(system);
            system.api->reset(system.instance, this->setup.seed);
        }
        this->seek(this->setup.clock.start_step);
    }

    Runtime::~Runtime() {
        for (System& system : this->system_storage) system.api->destroy_provider(system.instance);
        for (System& system : this->system_storage)
            for (Binding& binding : system.bindings)
                for (std::vector<Buffer>& slot : binding.slots)
                    for (Buffer& buffer : slot)
                        if (buffer.descriptor_allocated) this->runtime->release_resource_descriptor(buffer.descriptor);
    }

    void Runtime::bind_resource(Binding& binding, const DynamicPortBinding& source) const {
        if (source.resource_kind == DynamicResourceKind::Instance) {
            const InstanceId id{source.resource};
            if (!std::ranges::contains(this->source_scene->resources.instances, id, &Instance::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Instance", binding.descriptor.id));
            binding.resource = id;
        } else if (source.resource_kind == DynamicResourceKind::Geometry) {
            const GeometryId id{source.resource};
            if (!std::ranges::contains(this->source_scene->resources.geometries, id, &Geometry::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Geometry", binding.descriptor.id));
            binding.resource = id;
        } else if (source.resource_kind == DynamicResourceKind::ParticleSet) {
            const ParticleSetId id{source.resource};
            if (!std::ranges::contains(this->source_scene->resources.particle_sets, id, &ParticleSet::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene ParticleSet", binding.descriptor.id));
            binding.resource = id;
        } else {
            const VolumeId id{source.resource};
            if (!std::ranges::contains(this->source_scene->resources.volumes, id, &Volume::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Volume", binding.descriptor.id));
            binding.resource = id;
        }
        const bool matches = (binding.descriptor.kind == ResourceKind::InstanceTransform && std::holds_alternative<InstanceId>(binding.resource)) || (binding.descriptor.kind == ResourceKind::TriangleMesh && std::holds_alternative<GeometryId>(binding.resource)) || (binding.descriptor.kind == ResourceKind::ParticleSet && std::holds_alternative<ParticleSetId>(binding.resource)) || (binding.descriptor.kind == ResourceKind::Volume && std::holds_alternative<VolumeId>(binding.resource)) || (binding.descriptor.kind == ResourceKind::DebugDraw && std::holds_alternative<InstanceId>(binding.resource));
        if (!matches) throw std::runtime_error(std::format("Dynamic Port '{}' binds the wrong Scene resource kind", binding.descriptor.id));
    }

    void Runtime::declare_output(const Binding& binding) {
        if (const GeometryId* geometry = std::get_if<GeometryId>(&binding.resource)) this->mesh_bindings.emplace_back(*geometry, binding.descriptor.mesh_update_mode, static_cast<std::uint32_t>(binding.capacity), static_cast<std::uint32_t>(binding.secondary_capacity));
        if (const ParticleSetId* particles = std::get_if<ParticleSetId>(&binding.resource)) this->particle_capacities.emplace_back(*particles, static_cast<std::uint32_t>(binding.capacity));
    }

    void Runtime::configure(System& system, const std::size_t binding_index) {
        Binding& binding = system.bindings[binding_index];
        if (binding.descriptor.kind == ResourceKind::DebugDraw) return;
        if (binding.signal_value != 0) this->runtime->wait_external_timeline(binding.timeline, binding.signal_value + 1);
        for (std::vector<Buffer>& slot : binding.slots)
            for (Buffer& buffer : slot)
                if (buffer.descriptor_allocated) this->runtime->release_resource_descriptor(buffer.descriptor);
        binding.capacity                     = std::max(binding.capacity, binding.requested_capacity);
        binding.secondary_capacity           = std::max(binding.secondary_capacity, binding.requested_secondary_capacity);
        binding.requested_capacity           = 0;
        binding.requested_secondary_capacity = 0;
        binding.signal_value                 = 0;
        binding.pending                      = false;
        binding.slots.clear();
        const std::uint32_t slot_count = binding.descriptor.memory_domain == MemoryDomain::CudaExternal ? Spectra::frames_in_flight : 1;
        binding.slots.resize(slot_count);
        if (binding.descriptor.memory_domain == MemoryDomain::CudaExternal) binding.timeline = this->runtime->create_external_timeline();
        for (std::uint32_t slot = 0; slot < slot_count; ++slot)
            for (std::uint32_t attribute_index = 0; attribute_index <= static_cast<std::uint32_t>(SpectraPluginAttribute::Bounds); ++attribute_index) {
                if ((binding.descriptor.attribute_mask & (1ull << attribute_index)) == 0) continue;
                const SpectraPluginAttribute attribute = static_cast<SpectraPluginAttribute>(attribute_index);
                const std::uint64_t count              = attribute == SpectraPluginAttribute::Index ? binding.secondary_capacity : binding.capacity;
                Buffer buffer{.attribute = attribute, .size = count * attribute_size(attribute)};
                if (binding.descriptor.memory_domain == MemoryDomain::Host)
                    buffer.host.resize(buffer.size);
                else {
                    buffer.storage                  = this->runtime->create_external_buffer(buffer.size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                    buffer.descriptor           = this->runtime->allocate_resource_descriptor();
                    buffer.descriptor_allocated = true;
                    this->runtime->write_buffer_descriptor(buffer.descriptor, vk::DescriptorType::eStorageBuffer, buffer.storage);
                }
                binding.slots[slot].emplace_back(std::move(buffer));
            }

        std::vector<std::vector<SpectraPluginBuffer>> buffers(slot_count);
        std::vector<SpectraPluginPortSlot> slots(slot_count);
        std::vector<Win32Handle> handles{};
        for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
            buffers[slot].reserve(binding.slots[slot].size());
            for (Buffer& buffer : binding.slots[slot]) {
                void* handle{};
                if (binding.descriptor.memory_domain == MemoryDomain::CudaExternal) {
                    handles.emplace_back(static_cast<HANDLE>(this->runtime->export_memory_handle(buffer.storage)));
                    handle = handles.back().value;
                }
                buffers[slot].emplace_back(buffer.attribute, handle, buffer.host.data(), buffer.size);
            }
            slots[slot] = {slot, buffers[slot].data(), buffers[slot].size()};
        }
        Win32Handle timeline{};
        if (binding.descriptor.memory_domain == MemoryDomain::CudaExternal) timeline = Win32Handle{static_cast<HANDLE>(this->runtime->export_semaphore_handle(binding.timeline))};
        const GpuIdentity identity = this->runtime->gpu_identity();
        SpectraPluginPortConfiguration configuration{binding.port, static_cast<SpectraPluginPortDirection>(binding.descriptor.direction), static_cast<SpectraPluginMemoryDomain>(binding.descriptor.memory_domain), slots.data(), slots.size(), timeline.value, {}, {}, identity.node_mask};
        std::ranges::copy(identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(identity.luid, configuration.vulkan_device_luid);
        system.api->configure_port(system.instance, &configuration);
    }

    Runtime::Binding& Runtime::binding(System& system, const std::uint64_t port) {
        if (port >= system.bindings.size()) throw std::runtime_error("Provider published an unknown Port");
        return system.bindings[port];
    }

    void Runtime::set_inputs(System& system) {
        for (Binding& binding : system.bindings) {
            if (binding.descriptor.direction != PortDirection::Input) continue;
            binding.active_count    = binding.capacity;
            binding.secondary_count = binding.secondary_capacity;
            binding.dirty_region.reset();
            binding.color_space = 0;
            for (Buffer& destination : binding.slots.front()) {
                if (const InstanceId* instance_id = std::get_if<InstanceId>(&binding.resource)) {
                    const Instance& instance = *std::ranges::find(this->source_scene->resources.instances, *instance_id, &Instance::id);
                    if (destination.attribute == SpectraPluginAttribute::Transform)
                        std::ranges::copy(instance.transform.matrix, reinterpret_cast<SpectraPluginTransform*>(destination.host.data())->matrix);
                    else if (destination.attribute == SpectraPluginAttribute::Bounds) {
                        const Bounds3 bounds = *this->source_scene->view().local_bounds(*instance_id);
                        SpectraPluginFloat3* values = reinterpret_cast<SpectraPluginFloat3*>(destination.host.data());
                        values[0]                   = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
                        values[1]                   = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
                    }
                    binding.active_count    = 1;
                    binding.secondary_count = 0;
                } else if (const GeometryId* geometry_id = std::get_if<GeometryId>(&binding.resource)) {
                    const Geometry& geometry         = *std::ranges::find(this->source_scene->resources.geometries, *geometry_id, &Geometry::id);
                    const TriangleMeshGeometry& mesh = std::get<TriangleMeshGeometry>(geometry.data);
                    const void* source{};
                    std::size_t size{};
                    if (destination.attribute == SpectraPluginAttribute::Position) {
                        source = mesh.positions.data();
                        size   = mesh.positions.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Normal) {
                        source = mesh.normals.data();
                        size   = mesh.normals.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Tangent) {
                        source = mesh.tangents.data();
                        size   = mesh.tangents.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::TextureCoordinate) {
                        source = mesh.texture_coordinates.data();
                        size   = mesh.texture_coordinates.size() * sizeof(Float2);
                    } else if (destination.attribute == SpectraPluginAttribute::Index) {
                        source = mesh.indices.data();
                        size   = mesh.indices.size() * sizeof(std::uint32_t);
                    }
                    if (size != 0) std::memcpy(destination.host.data(), source, size);
                    binding.active_count    = mesh.positions.size();
                    binding.secondary_count = mesh.indices.size();
                } else if (const ParticleSetId* particles_id = std::get_if<ParticleSetId>(&binding.resource)) {
                    const ParticleSet& particles = *std::ranges::find(this->source_scene->resources.particle_sets, *particles_id, &ParticleSet::id);
                    const void* source{};
                    std::size_t size{};
                    if (destination.attribute == SpectraPluginAttribute::Position) {
                        source = particles.positions.data();
                        size   = particles.positions.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Radius) {
                        source = particles.radii.data();
                        size   = particles.radii.size() * sizeof(float);
                    } else if (destination.attribute == SpectraPluginAttribute::Velocity) {
                        source = particles.velocities.data();
                        size   = particles.velocities.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Color) {
                        source = particles.colors.data();
                        size   = particles.colors.size() * sizeof(Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Temperature) {
                        source = particles.temperatures.data();
                        size   = particles.temperatures.size() * sizeof(float);
                    } else if (destination.attribute == SpectraPluginAttribute::Material) {
                        source = particles.particle_materials.data();
                        size   = particles.particle_materials.size() * sizeof(MaterialId);
                    }
                    if (size != 0) std::memcpy(destination.host.data(), source, size);
                    binding.active_count    = particles.positions.size();
                    binding.secondary_count = 0;
                } else if (const VolumeId* volume_id = std::get_if<VolumeId>(&binding.resource)) {
                    const Volume& volume = *std::ranges::find(this->source_scene->resources.volumes, *volume_id, &Volume::id);
                    const void* source{};
                    std::size_t size{};
                    if (const DensityGridVolume* grid = std::get_if<DensityGridVolume>(&volume.data)) {
                        if (destination.attribute == SpectraPluginAttribute::Density) {
                            source = grid->density.data();
                            size   = grid->density.size() * sizeof(float);
                        } else if (destination.attribute == SpectraPluginAttribute::Temperature) {
                            source = grid->temperature.data();
                            size   = grid->temperature.size() * sizeof(float);
                        } else if (destination.attribute == SpectraPluginAttribute::EmissionScale) {
                            source = grid->emission_scale.data();
                            size   = grid->emission_scale.size() * sizeof(float);
                        }
                        binding.active_count = static_cast<std::uint64_t>(grid->resolution.x) * grid->resolution.y * grid->resolution.z;
                        binding.color_space  = static_cast<std::uint32_t>(SpectrumColorSpace::Srgb);
                    } else {
                        const RgbGridVolume& rgb_grid = std::get<RgbGridVolume>(volume.data);
                        if (destination.attribute == SpectraPluginAttribute::SigmaA) {
                            source = rgb_grid.sigma_a.data();
                            size   = rgb_grid.sigma_a.size() * sizeof(Float3);
                        } else if (destination.attribute == SpectraPluginAttribute::SigmaS) {
                            source = rgb_grid.sigma_s.data();
                            size   = rgb_grid.sigma_s.size() * sizeof(Float3);
                        } else if (destination.attribute == SpectraPluginAttribute::Emission) {
                            source = rgb_grid.emission.data();
                            size   = rgb_grid.emission.size() * sizeof(Float3);
                        }
                        binding.active_count = static_cast<std::uint64_t>(rgb_grid.resolution.x) * rgb_grid.resolution.y * rgb_grid.resolution.z;
                        binding.color_space  = static_cast<std::uint32_t>(rgb_grid.color_space);
                    }
                    if (size != 0) std::memcpy(destination.host.data(), source, size);
                    binding.secondary_count = 0;
                    binding.dirty_region    = VolumeRegion{{}, binding.descriptor.resolution};
                }
            }
            const VolumeRegion region = binding.dirty_region.value_or(VolumeRegion{});
            const SpectraPluginInputFrame frame{binding.port, binding.active_count, binding.secondary_count, {region.minimum.x, region.minimum.y, region.minimum.z}, {region.maximum.x, region.maximum.y, region.maximum.z}, binding.color_space};
            system.api->set_input_frame(system.instance, &frame);
        }
    }

    void Runtime::apply_parameters(System& system) {
        std::vector<SpectraPluginParameterValue> encoded{};
        encoded.reserve(system.parameter_values.size());
        for (const DynamicParameterValue& value : system.parameter_values) encoded.emplace_back(parameter_value(value));
        system.api->apply_parameters(system.instance, encoded.data(), encoded.size());
    }

    void Runtime::commit(System& system, Binding& binding, const SpectraPluginOutputCommit& commit, PublishedFrame& frame) {
        if (binding.descriptor.kind == ResourceKind::DebugDraw || binding.descriptor.direction != PortDirection::Output) throw std::runtime_error("Provider committed data through a non-resource output Port");
        if (commit.slot >= binding.slots.size()) throw std::runtime_error("Provider committed an invalid output slot");
        if (commit.active_count > binding.capacity || commit.secondary_count > binding.secondary_capacity) throw std::runtime_error("Provider committed more elements than its configured output capacity");
        binding.current_slot    = commit.slot;
        binding.active_count    = commit.active_count;
        binding.secondary_count = commit.secondary_count;
        binding.signal_value    = commit.signal_value;
        binding.color_space     = commit.color_space;
        binding.dirty_region.reset();
        if (binding.descriptor.kind == ResourceKind::Volume) binding.dirty_region = VolumeRegion{{commit.minimum[0], commit.minimum[1], commit.minimum[2]}, {commit.maximum[0], commit.maximum[1], commit.maximum[2]}};
        if (binding.descriptor.memory_domain == MemoryDomain::CudaExternal) {
            binding.pending = true;
            return;
        }

        const std::vector<Buffer>& buffers = binding.slots.front();
        const auto buffer                  = [&buffers](const SpectraPluginAttribute attribute) -> const Buffer* {
            const auto found = std::ranges::find(buffers, attribute, &Buffer::attribute);
            return found == buffers.end() ? nullptr : std::to_address(found);
        };
        if (binding.descriptor.kind == ResourceKind::InstanceTransform) {
            const SpectraPluginTransform& value = *reinterpret_cast<const SpectraPluginTransform*>(buffer(SpectraPluginAttribute::Transform)->host.data());
            Transform transform{};
            std::ranges::copy(value.matrix, transform.matrix.begin());
            frame.transforms.emplace_back(std::get<InstanceId>(binding.resource), transform);
        } else if (binding.descriptor.kind == ResourceKind::TriangleMesh) {
            const Buffer* positions           = buffer(SpectraPluginAttribute::Position);
            const Buffer* normals             = buffer(SpectraPluginAttribute::Normal);
            const Buffer* tangents            = buffer(SpectraPluginAttribute::Tangent);
            const Buffer* texture_coordinates = buffer(SpectraPluginAttribute::TextureCoordinate);
            const Buffer* indices             = buffer(SpectraPluginAttribute::Index);
            frame.meshes.emplace_back(std::get<GeometryId>(binding.resource), binding.descriptor.attribute_mask, binding.active_count, binding.secondary_count, positions ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(positions->host.data()), binding.active_count) : std::vector<Float3>{}, normals ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(normals->host.data()), binding.active_count) : std::vector<Float3>{}, tangents ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(tangents->host.data()), binding.active_count) : std::vector<Float3>{}, texture_coordinates ? float2_values(reinterpret_cast<const SpectraPluginFloat2*>(texture_coordinates->host.data()), binding.active_count) : std::vector<Float2>{}, indices ? std::vector<std::uint32_t>{reinterpret_cast<const std::uint32_t*>(indices->host.data()), reinterpret_cast<const std::uint32_t*>(indices->host.data()) + binding.secondary_count} : std::vector<std::uint32_t>{});
        } else if (binding.descriptor.kind == ResourceKind::ParticleSet) {
            const Buffer* positions    = buffer(SpectraPluginAttribute::Position);
            const Buffer* radii        = buffer(SpectraPluginAttribute::Radius);
            const Buffer* velocities   = buffer(SpectraPluginAttribute::Velocity);
            const Buffer* colors       = buffer(SpectraPluginAttribute::Color);
            const Buffer* temperatures = buffer(SpectraPluginAttribute::Temperature);
            const Buffer* materials    = buffer(SpectraPluginAttribute::Material);
            std::vector<MaterialId> material_values{};
            if (materials) {
                material_values.resize(binding.active_count);
                for (std::size_t index = 0; index < material_values.size(); ++index) material_values[index] = MaterialId{reinterpret_cast<const std::uint64_t*>(materials->host.data())[index]};
            }
            frame.particles.emplace_back(std::get<ParticleSetId>(binding.resource), binding.descriptor.attribute_mask, binding.active_count, positions ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(positions->host.data()), binding.active_count) : std::vector<Float3>{}, radii ? std::vector<float>{reinterpret_cast<const float*>(radii->host.data()), reinterpret_cast<const float*>(radii->host.data()) + binding.active_count} : std::vector<float>{}, velocities ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(velocities->host.data()), binding.active_count) : std::vector<Float3>{}, colors ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(colors->host.data()), binding.active_count) : std::vector<Float3>{}, temperatures ? std::vector<float>{reinterpret_cast<const float*>(temperatures->host.data()), reinterpret_cast<const float*>(temperatures->host.data()) + binding.active_count} : std::vector<float>{}, std::move(material_values));
        } else if (binding.descriptor.kind == ResourceKind::Volume) {
            const Buffer* density            = buffer(SpectraPluginAttribute::Density);
            const Buffer* temperature        = buffer(SpectraPluginAttribute::Temperature);
            const Buffer* emission_scale     = buffer(SpectraPluginAttribute::EmissionScale);
            const Buffer* sigma_a            = buffer(SpectraPluginAttribute::SigmaA);
            const Buffer* sigma_s            = buffer(SpectraPluginAttribute::SigmaS);
            const Buffer* emission           = buffer(SpectraPluginAttribute::Emission);
            const Buffer* velocity           = buffer(SpectraPluginAttribute::Velocity);
            const VolumeRegion region = *binding.dirty_region;
            frame.volumes.emplace_back(std::get<VolumeId>(binding.resource), binding.descriptor.attribute_mask, binding.descriptor.resolution, region, static_cast<SpectrumColorSpace>(binding.color_space), density ? std::vector<float>{reinterpret_cast<const float*>(density->host.data()), reinterpret_cast<const float*>(density->host.data()) + binding.active_count} : std::vector<float>{}, temperature ? std::vector<float>{reinterpret_cast<const float*>(temperature->host.data()), reinterpret_cast<const float*>(temperature->host.data()) + binding.active_count} : std::vector<float>{}, emission_scale ? std::vector<float>{reinterpret_cast<const float*>(emission_scale->host.data()), reinterpret_cast<const float*>(emission_scale->host.data()) + binding.active_count} : std::vector<float>{}, sigma_a ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(sigma_a->host.data()), binding.active_count) : std::vector<Float3>{},
                sigma_s ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(sigma_s->host.data()), binding.active_count) : std::vector<Float3>{}, emission ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(emission->host.data()), binding.active_count) : std::vector<Float3>{}, velocity && this->setup.systems[system.scene_index].visible ? float3_values(reinterpret_cast<const SpectraPluginFloat3*>(velocity->host.data()), binding.active_count) : std::vector<Float3>{});
        }
    }

    void Runtime::publish(const std::uint64_t simulation_step) {
        if (this->publication_pending) {
            for (System& system : this->system_storage)
                for (Binding& binding : system.bindings)
                    if (binding.pending) {
                        this->runtime->wait_external_timeline(binding.timeline, binding.signal_value);
                        this->runtime->signal_external_timeline(binding.timeline, binding.signal_value + 1);
                        binding.pending = false;
                    }
            this->publication_pending = false;
        }
        PublishedFrame next{.publication = ++this->publication, .simulation_step = simulation_step};
        for (System& system : this->system_storage) {
            for (Binding& binding : system.bindings)
                if (binding.requested_capacity > binding.capacity || binding.requested_secondary_capacity > binding.secondary_capacity) this->configure(system, binding.port);
            FrameCollector collector{this, &system, &next};
            system.api->publish_frame(system.instance, simulation_step, &collector.sink);
            bool reconfigure{};
            for (Binding& binding : system.bindings)
                if (binding.requested_capacity > binding.capacity || binding.requested_secondary_capacity > binding.secondary_capacity) {
                    this->configure(system, binding.port);
                    reconfigure = true;
                }
            if (reconfigure) system.api->publish_frame(system.instance, simulation_step, &collector.sink);
            for (std::size_t telemetry = 0; telemetry < this->systems[system.scene_index].telemetry.size(); ++telemetry) this->systems[system.scene_index].telemetry[telemetry].value = system.api->telemetry(system.instance, telemetry);
        }
        next.simulation_seconds   = static_cast<double>(simulation_step) * this->setup.clock.step_seconds;
        this->published           = std::move(next);
        this->debug_primitives    = this->published.debug;
        this->publication_pending = true;
    }

    void Runtime::step_to(const std::uint64_t target_step) {
        if (target_step <= this->simulation_step) return;
        const std::uint64_t count = target_step - this->simulation_step;
        for (System& system : this->system_storage) {
            this->set_inputs(system);
            const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            system.api->step(system.instance, this->setup.clock.step_seconds, count);
            const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
            system.completed_steps += count;
            system.total_step_milliseconds += elapsed;
            SystemState& state       = this->systems[system.scene_index];
            state.completed_steps           = system.completed_steps;
            state.simulation_seconds        = static_cast<double>(system.completed_steps) * this->setup.clock.step_seconds;
            state.last_batch_milliseconds   = elapsed;
            state.average_step_milliseconds = system.total_step_milliseconds / static_cast<double>(system.completed_steps);
        }
        this->simulation_step = target_step;
    }

    void Runtime::reset_systems() {
        for (System& system : this->system_storage) {
            this->set_inputs(system);
            system.api->reset(system.instance, this->setup.seed);
            system.completed_steps          = 0;
            system.total_step_milliseconds  = 0.0;
            SystemState& state       = this->systems[system.scene_index];
            state.completed_steps           = 0;
            state.simulation_seconds        = 0.0;
            state.last_batch_milliseconds   = 0.0;
            state.average_step_milliseconds = 0.0;
        }
        this->simulation_step = 0;
        this->step_to(this->setup.clock.start_step);
    }

    void Runtime::evaluate(const std::uint64_t target_step) {
        this->step_to(target_step);
        this->publish(target_step);
    }

    void Runtime::reset() {
        this->accumulated_time = {};
        this->reset_systems();
        this->publish(this->simulation_step);
    }

    void Runtime::seek(const std::uint64_t step) {
        this->accumulated_time = {};
        if (step < this->simulation_step) {
            this->reset_systems();
        }
        if (step != this->simulation_step)
            this->evaluate(step);
        else if (!this->publication_pending)
            this->publish(step);
    }

    void Runtime::update(const std::chrono::duration<double> elapsed) {
        if (!this->playback_running || this->system_storage.empty()) return;
        this->accumulated_time += elapsed;
        const std::chrono::duration<double> step_duration{this->setup.clock.step_seconds};
        std::uint64_t remaining_steps = static_cast<std::uint64_t>(this->accumulated_time / step_duration);
        if (remaining_steps == 0) return;
        this->accumulated_time -= step_duration * remaining_steps;

        while (remaining_steps != 0) {
            if (!this->setup.clock.end_step) {
                this->step_to(this->simulation_step + remaining_steps);
                remaining_steps = 0;
                continue;
            }
            if (this->simulation_step == *this->setup.clock.end_step) {
                if (!this->setup.clock.loop) {
                    this->playback_running = false;
                    this->accumulated_time = {};
                    remaining_steps       = 0;
                    continue;
                }
                this->reset_systems();
            }
            const std::uint64_t segment = std::min(remaining_steps, *this->setup.clock.end_step - this->simulation_step);
            this->step_to(this->simulation_step + segment);
            remaining_steps -= segment;
        }
        this->publish(this->simulation_step);
        if (this->setup.clock.end_step && this->simulation_step == *this->setup.clock.end_step && !this->setup.clock.loop) {
            this->playback_running = false;
            this->accumulated_time = {};
        }
    }

    const PublishedFrame* Runtime::prepare_frame() noexcept {
        if (!this->publication_pending) return nullptr;
        this->published.external.clear();
        for (System& system : this->system_storage)
            for (Binding& binding : system.bindings) {
                if (!binding.pending) continue;
                this->runtime->enqueue_external_wait(binding.timeline, binding.signal_value, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader);
                std::vector<ExternalBufferView> views{};
                for (const Buffer& buffer : binding.slots[binding.current_slot]) {
                    if (binding.descriptor.kind == ResourceKind::Volume && buffer.attribute == SpectraPluginAttribute::Velocity && !this->setup.systems[system.scene_index].visible) continue;
                    views.emplace_back(static_cast<Attribute>(buffer.attribute), &buffer.storage, buffer.descriptor);
                }
                this->published.external.emplace_back(binding.descriptor.kind, binding.resource, std::move(views), binding.active_count, binding.secondary_count, binding.dirty_region);
            }
        return &this->published;
    }

    void Runtime::consume_frame() noexcept {
        if (!this->publication_pending) return;
        for (System& system : this->system_storage)
            for (Binding& binding : system.bindings)
                if (binding.pending) {
                    this->runtime->enqueue_external_signal(binding.timeline, binding.signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
                    binding.pending = false;
                }
        this->publication_pending = false;
    }

    bool Runtime::controls(const InstanceId instance_id) const noexcept {
        for (const System& system : this->system_storage)
            for (const Binding& binding : system.bindings) {
                if (binding.descriptor.direction != PortDirection::Output || binding.descriptor.kind == ResourceKind::DebugDraw) continue;
                if (const InstanceId* direct = std::get_if<InstanceId>(&binding.resource); direct && *direct == instance_id) return true;
                const auto instance = std::ranges::find(this->source_scene->resources.instances, instance_id, &Instance::id);
                if (instance == this->source_scene->resources.instances.end()) continue;
                const auto prototype = std::ranges::find(this->source_scene->resources.prototypes, instance->prototype, &Prototype::id);
                if (prototype == this->source_scene->resources.prototypes.end()) continue;
                if (const GeometryId* geometry = std::get_if<GeometryId>(&binding.resource); geometry && std::ranges::any_of(prototype->primitives, [geometry](const Primitive& primitive) { return primitive.geometry == *geometry; })) return true;
                if (const ParticleSetId* particles = std::get_if<ParticleSetId>(&binding.resource); particles && std::ranges::any_of(prototype->primitives, [particles](const Primitive& primitive) { return primitive.particles == *particles; })) return true;
                if (const VolumeId* volume = std::get_if<VolumeId>(&binding.resource); volume && std::ranges::any_of(prototype->primitives, [volume](const Primitive& primitive) { return primitive.volume == *volume; })) return true;
            }
        return false;
    }

    void Runtime::set_running(const bool running) noexcept {
        this->playback_running = running;
    }
    void Runtime::advance() {
        this->accumulated_time = {};
        if (this->setup.clock.end_step && this->simulation_step >= *this->setup.clock.end_step) {
            if (!this->setup.clock.loop) return;
            this->reset_systems();
        }
        const std::uint64_t requested = this->simulation_step + 1;
        this->evaluate(this->setup.clock.end_step ? std::min(requested, *this->setup.clock.end_step) : requested);
    }

    void Runtime::apply_parameters(const std::size_t system, const bool reset) {
        const auto found = std::ranges::find(this->system_storage, system, &System::scene_index);
        if (found == this->system_storage.end()) return;
        System& destination = *found;
        destination.parameter_values.clear();
        for (const Parameter& parameter : this->systems[system].parameters) destination.parameter_values.emplace_back(parameter.value);
        this->apply_parameters(destination);
        if (reset) this->reset();
    }

    void Runtime::bind_scene(const Scene& scene) noexcept {
        this->source_scene = &scene;
    }

    TimelineState Runtime::timeline() const noexcept {
        return {
            this->simulation_step,
            this->published.simulation_seconds,
            this->setup.clock.step_seconds,
        };
    }

    bool Runtime::running() const noexcept {
        return this->playback_running;
    }
} // namespace spectra::scene::dynamics
