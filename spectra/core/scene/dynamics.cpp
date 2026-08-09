module;

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <spectra/plugin_api.h>

#undef interface

module spectra.scene.dynamics;

import std;
import vulkan;

namespace spectra {
    namespace {
        [[nodiscard]] std::string plugin_string(const SpectraPluginString value) {
            return {value.data, value.size};
        }

        [[nodiscard]] scene::DynamicParameterValue scene_parameter_value(const SpectraPluginParameterValue value) noexcept {
            return {static_cast<scene::DynamicParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraPluginParameterValue plugin_parameter_value(const scene::DynamicParameterValue& value) noexcept {
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

        [[nodiscard]] std::vector<math::Float3> copy_plugin_float3_values(const SpectraPluginFloat3* source, const std::size_t count) {
            std::vector<math::Float3> values(count);
            for (std::size_t index = 0; index < count; ++index) values[index] = {source[index].x, source[index].y, source[index].z};
            return values;
        }

        [[nodiscard]] std::vector<math::Float2> copy_plugin_float2_values(const SpectraPluginFloat2* source, const std::size_t count) {
            std::vector<math::Float2> values(count);
            for (std::size_t index = 0; index < count; ++index) values[index] = {source[index].x, source[index].y};
            return values;
        }

        [[nodiscard]] SpectraPluginExternalHandle plugin_external_handle(const ExternalHandle& handle) noexcept {
            return {
                handle.type == ExternalHandleType::OpaqueWin32 ? SpectraPluginExternalHandleType::OpaqueWin32 : handle.type == ExternalHandleType::OpaqueFileDescriptor ? SpectraPluginExternalHandleType::OpaqueFileDescriptor : SpectraPluginExternalHandleType::None,
                handle.value,
            };
        }
    } // namespace

    DynamicWorld::ProviderLibrary::ProviderLibrary(const std::filesystem::path& library_path, const std::string_view expected_provider_id) : library_path(std::filesystem::weakly_canonical(library_path)) {
#if defined(_WIN32)
        const HMODULE loaded = LoadLibraryW(this->library_path.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load Provider Library: {}", this->library_path.string()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*) ()>(GetProcAddress(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
#else
        void* loaded = dlopen(this->library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!loaded) throw std::runtime_error(std::format("Linux failed to load Provider Library '{}': {}", this->library_path.string(), dlerror()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*) ()>(dlsym(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
#endif
        if (!entry) {
#if defined(_WIN32)
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            throw std::runtime_error(std::format("Provider Library does not export {}: {}", SPECTRA_PLUGIN_ENTRY_NAME, this->library_path.string()));
        }
        const SpectraPluginApi* loaded_api = entry();
        if (!loaded_api || loaded_api->api_version != SPECTRA_PLUGIN_API_VERSION || loaded_api->struct_size != sizeof(SpectraPluginApi) || !loaded_api->describe_provider || !loaded_api->create_provider || !loaded_api->destroy_provider || !loaded_api->configure_port || !loaded_api->set_input_frame || !loaded_api->apply_parameters || !loaded_api->reset || !loaded_api->step || !loaded_api->publish_frame) {
#if defined(_WIN32)
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            throw std::runtime_error(std::format("Provider Library has an incomplete Plugin API {} entry: {}", SPECTRA_PLUGIN_API_VERSION, this->library_path.string()));
        }
        const SpectraPluginProviderDescriptor described = loaded_api->describe_provider();
        if (plugin_string(described.id) != expected_provider_id) {
#if defined(_WIN32)
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            throw std::runtime_error(std::format("Provider Library '{}' reports Provider '{}' instead of '{}'", this->library_path.string(), plugin_string(described.id), expected_provider_id));
        }
        this->library_handle = loaded;
        this->plugin_api     = loaded_api;
    }

    DynamicWorld::ProviderLibrary::~ProviderLibrary() {
        if (!this->library_handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(this->library_handle));
#else
        dlclose(this->library_handle);
#endif
    }

    DynamicWorld::DynamicWorld(VulkanRuntime& runtime, SceneDocument& document) noexcept : context{runtime, document} {}

    DynamicWorld::~DynamicWorld() {
        this->destroy();
    }

    DynamicWorld::ProviderLibrary& DynamicWorld::provider_library(const std::string_view provider_id) const {
        const auto found = this->providers.by_id.find(std::string{provider_id});
        if (found == this->providers.by_id.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider_id));
        return *found->second;
    }

    const dynamics::ProviderDescriptor& DynamicWorld::provider_descriptor(const std::string_view provider_id) const {
        const auto found = std::ranges::find(this->providers.descriptors, provider_id, &dynamics::ProviderDescriptor::id);
        if (found == this->providers.descriptors.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider_id));
        return *found;
    }

    void DynamicWorld::collect_debug(void* context, const std::uint64_t port_index, const SpectraPluginDebugPrimitive* primitives, const std::uint64_t primitive_count) {
        DynamicWorld& world          = *static_cast<DynamicWorld*>(context);
        DynamicSystemRuntime& system = *world.publication.publishing_system;
        DynamicPortRuntime& output   = world.port_runtime(system, port_index);
        if (output.descriptor.resource_kind != dynamics::ResourceKind::DebugDraw || output.descriptor.direction != dynamics::PortDirection::Output) throw std::runtime_error("Provider published Debug Draw through a non-Debug output Port");
        if (!world.configuration.setup.systems[system.scene_system_index].visible) return;
        const scene::InstanceId anchor_id = std::get<scene::InstanceId>(output.resource_id);
        const scene::Instance& anchor     = *std::ranges::find(world.configuration.source_scene->resources.instances, anchor_id, &scene::Instance::id);
        const float radius_scale          = std::max({anchor.transform.transform_vector({1.0f, 0.0f, 0.0f}).length(), anchor.transform.transform_vector({0.0f, 1.0f, 0.0f}).length(), anchor.transform.transform_vector({0.0f, 0.0f, 1.0f}).length()});
        for (std::uint64_t index = 0; index < primitive_count; ++index) {
            const SpectraPluginDebugPrimitive& source = primitives[index];
            math::Float3 first_position              = anchor.transform.transform_point({source.first_position.x, source.first_position.y, source.first_position.z});
            math::Float3 second_position             = anchor.transform.transform_point({source.second_position.x, source.second_position.y, source.second_position.z});
            if (source.kind == SpectraPluginDebugPrimitiveKind::AxisAlignedBox) {
                math::Bounds3 bounds = math::Bounds3::empty();
                for (const float x : {source.first_position.x, source.second_position.x})
                    for (const float y : {source.first_position.y, source.second_position.y})
                        for (const float z : {source.first_position.z, source.second_position.z}) bounds.include(anchor.transform.transform_point({x, y, z}));
                first_position  = bounds.minimum;
                second_position = bounds.maximum;
            }
            std::uint64_t pick{};
            if (source.source_id != 0) {
                const auto [entry, inserted] = world.publication.debug_object_ids.try_emplace({system.scene_system_index, source.source_id}, world.publication.next_debug_object_id);
                if (inserted) ++world.publication.next_debug_object_id;
                pick = entry->second;
            }
            world.publication.publishing_frame->debug_primitives.emplace_back(static_cast<dynamics::DebugPrimitiveKind>(source.kind), static_cast<dynamics::DebugDepthMode>(source.depth_mode), first_position, second_position, math::Float3{source.color.x, source.color.y, source.color.z}, source.radius * radius_scale, pick);
        }
    }

    void DynamicWorld::collect_output(void* context, const std::uint64_t port_index, const SpectraPluginOutputCommit* commit) {
        DynamicWorld& world          = *static_cast<DynamicWorld*>(context);
        DynamicSystemRuntime& system = *world.publication.publishing_system;
        world.commit_output(system, world.port_runtime(system, port_index), *commit, *world.publication.publishing_frame);
    }

    void DynamicWorld::collect_capacity(void* context, const std::uint64_t port_index, const std::uint64_t capacity, const std::uint64_t secondary_capacity) {
        DynamicWorld& world                 = *static_cast<DynamicWorld*>(context);
        DynamicSystemRuntime& system        = *world.publication.publishing_system;
        DynamicPortRuntime& output          = world.port_runtime(system, port_index);
        output.requested_capacity           = std::max(output.requested_capacity, capacity);
        output.requested_secondary_capacity = std::max(output.requested_secondary_capacity, secondary_capacity);
    }

    void DynamicWorld::initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene) {
        this->destroy();
        this->configuration.source_scene = &source_scene;
        this->configuration.setup        = *source_scene.dynamic_setup;
        std::vector<std::string> required_providers{};
        required_providers.reserve(this->configuration.setup.systems.size());
        for (const scene::DynamicSystem& system : this->configuration.setup.systems)
            if (!std::ranges::contains(required_providers, system.provider_id)) required_providers.emplace_back(system.provider_id);
        std::ranges::sort(required_providers);
        for (const std::string& required_provider : required_providers) {
            const std::filesystem::path path                      = scene_path.parent_path() / scene::provider_library_filename(required_provider);
            ProviderLibrary& library                              = this->providers.libraries.emplace_back(path, required_provider);
            const SpectraPluginProviderDescriptor source_provider = library.plugin_api->describe_provider();
            dynamics::ProviderDescriptor provider{.id = plugin_string(source_provider.id)};
            provider.ports.reserve(source_provider.port_count);
            for (std::uint64_t port_index = 0; port_index < source_provider.port_count; ++port_index) {
                const SpectraPluginPortDescriptor& port = source_provider.ports[port_index];
                provider.ports.emplace_back(plugin_string(port.id), static_cast<dynamics::PortDirection>(port.direction), static_cast<dynamics::ResourceKind>(port.resource_kind), static_cast<dynamics::MemoryDomain>(port.memory_domain), port.capacity, port.secondary_capacity, port.attribute_mask, static_cast<dynamics::MeshUpdateMode>(port.mesh_update_mode), math::UInt3{port.resolution[0], port.resolution[1], port.resolution[2]});
            }
            provider.parameters.reserve(source_provider.parameter_count);
            for (std::uint64_t parameter_index = 0; parameter_index < source_provider.parameter_count; ++parameter_index) {
                const SpectraPluginParameterDescriptor& parameter = source_provider.parameters[parameter_index];
                dynamics::ParameterDescriptor value{
                    .id               = plugin_string(parameter.id),
                    .name             = plugin_string(parameter.name),
                    .unit             = plugin_string(parameter.unit),
                    .application_mode = static_cast<dynamics::ParameterApplication>(parameter.application_mode),
                    .value            = scene_parameter_value(parameter.default_value),
                    .minimum          = scene_parameter_value(parameter.minimum),
                    .maximum          = scene_parameter_value(parameter.maximum),
                };
                value.enumerators.reserve(parameter.enumerator_count);
                for (std::uint64_t enumerator = 0; enumerator < parameter.enumerator_count; ++enumerator) value.enumerators.emplace_back(plugin_string(parameter.enumerators[enumerator]));
                provider.parameters.emplace_back(std::move(value));
            }
            if (!this->providers.by_id.emplace(provider.id, &library).second) throw std::runtime_error(std::format("Provider '{}' is loaded more than once", provider.id));
            this->providers.descriptors.emplace_back(std::move(provider));
        }

        std::unordered_map<std::string, std::size_t> writers{};
        for (std::size_t system_index = 0; system_index < this->configuration.setup.systems.size(); ++system_index) {
            const scene::DynamicSystem& declared = this->configuration.setup.systems[system_index];
            if (!declared.enabled) continue;
            const dynamics::ProviderDescriptor& provider = this->provider_descriptor(declared.provider_id);
            for (const dynamics::PortDescriptor& port : provider.ports) {
                if (port.direction != dynamics::PortDirection::Output || port.resource_kind == dynamics::ResourceKind::DebugDraw) continue;
                const auto found = std::ranges::find(declared.bindings, port.id, &scene::DynamicPortBinding::port_id);
                if (found == declared.bindings.end()) throw std::runtime_error(std::format("Provider Port '{}' is unbound", port.id));
                if (std::ranges::count(declared.bindings, port.id, &scene::DynamicPortBinding::port_id) != 1) throw std::runtime_error(std::format("Provider Port '{}' must bind exactly one Scene resource", port.id));
                const scene::DynamicPortBinding& source = *found;
                const std::string key                   = std::format("{}:{}", std::to_underlying(source.resource_kind), source.resource_id);
                const auto [writer, inserted]           = writers.try_emplace(key, system_index);
                if (!inserted && writer->second != system_index) throw std::runtime_error(std::format("Dynamic Systems cannot share output resource {}", source.resource_id));
            }
        }

        for (std::size_t system_index = 0; system_index < this->configuration.setup.systems.size(); ++system_index) {
            const scene::DynamicSystem& declared         = this->configuration.setup.systems[system_index];
            const dynamics::ProviderDescriptor& provider = this->provider_descriptor(declared.provider_id);
            if (!declared.visible)
                for (const scene::DynamicPortBinding& binding : declared.bindings) {
                    const auto port = std::ranges::find(provider.ports, binding.port_id, &dynamics::PortDescriptor::id);
                    if (port == provider.ports.end() || port->direction != dynamics::PortDirection::Output || port->resource_kind == dynamics::ResourceKind::DebugDraw) continue;
                    for (const scene::Instance& instance : source_scene.resources.instances) {
                        const auto prototype = std::ranges::find(source_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                        if (prototype == source_scene.resources.prototypes.end()) continue;
                        const bool owns = (binding.resource_kind == scene::DynamicResourceKind::Instance && binding.resource_id == instance.id.value) || std::ranges::any_of(prototype->primitives, [&binding](const scene::Primitive& primitive) { return (binding.resource_kind == scene::DynamicResourceKind::Geometry && binding.resource_id == primitive.geometry.value) || (binding.resource_kind == scene::DynamicResourceKind::ParticleSet && binding.resource_id == primitive.particles.value) || (binding.resource_kind == scene::DynamicResourceKind::Volume && binding.resource_id == primitive.volume.value); });
                        if (owns && !std::ranges::contains(this->outputs.hidden_instances, instance.id)) this->outputs.hidden_instances.emplace_back(instance.id);
                    }
                }
            if (!declared.enabled) continue;

            ProviderLibrary& library = this->provider_library(provider.id);
            DynamicSystemRuntime system{.scene_system_index = system_index, .provider_descriptor = &provider, .plugin_api = library.plugin_api};
            system.provider_instance = system.plugin_api->create_provider();
            if (!system.provider_instance) throw std::runtime_error(std::format("Provider '{}' refused to create its declared instance", provider.id));
            for (const dynamics::ParameterDescriptor& parameter : provider.parameters) {
                const auto configured = std::ranges::find(declared.parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
                system.parameter_values.emplace_back(configured == declared.parameters.end() ? parameter.value : configured->value);
            }
            this->systems.runtimes.emplace_back(std::move(system));
        }

        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            const scene::DynamicSystem& declared = this->configuration.setup.systems[system.scene_system_index];
            for (std::size_t port_index = 0; port_index < system.provider_descriptor->ports.size(); ++port_index) {
                const dynamics::PortDescriptor& port = system.provider_descriptor->ports[port_index];
                DynamicPortRuntime binding{.port_index = port_index, .descriptor = port, .capacity = port.capacity, .secondary_capacity = port.secondary_capacity};
                const auto source = std::ranges::find(declared.bindings, port.id, &scene::DynamicPortBinding::port_id);
                if (source == declared.bindings.end()) throw std::runtime_error(std::format("Provider Port '{}' is unbound", port.id));
                if (std::ranges::count(declared.bindings, port.id, &scene::DynamicPortBinding::port_id) != 1) throw std::runtime_error(std::format("Provider Port '{}' must bind exactly one Scene resource", port.id));
                this->bind_resource(binding, *source);
                if (port.direction == dynamics::PortDirection::Input) {
                    if (port.memory_domain != dynamics::MemoryDomain::Host) throw std::runtime_error("In-process research Providers only accept Host Scene inputs");
                    const std::string key = std::format("{}:{}", std::to_underlying(source->resource_kind), source->resource_id);
                    const auto writer     = writers.find(key);
                    if (writer != writers.end() && writer->second != system.scene_system_index) throw std::runtime_error("A Dynamic System input cannot consume another Dynamic System output");
                }
                system.ports.emplace_back(std::move(binding));
            }
            for (std::size_t port_index = 0; port_index < system.ports.size(); ++port_index) {
                this->configure_port(system, port_index);
                if (system.ports[port_index].descriptor.direction == dynamics::PortDirection::Output) this->declare_output(system.ports[port_index]);
            }
            this->apply_parameters(system);
            this->set_inputs(system);
            system.plugin_api->reset(system.provider_instance, this->configuration.setup.seed);
        }
        this->configuration.initialized = true;
        this->seek(this->configuration.setup.clock.start_step);
    }

    void DynamicWorld::destroy() noexcept {
        if (!this->configuration.initialized && this->providers.libraries.empty()) return;
        for (DynamicSystemRuntime& system : this->systems.runtimes) system.plugin_api->destroy_provider(system.provider_instance);
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicPortRuntime& binding : system.ports)
                for (std::vector<DynamicPortBuffer>& slot_buffers : binding.buffer_slots)
                    for (DynamicPortBuffer& port_buffer : slot_buffers)
                        if (port_buffer.owns_descriptor) this->context.runtime.frames.retire_resource_descriptor(port_buffer.buffer_descriptor);
        this->systems.runtimes.clear();
        this->providers.by_id.clear();
        this->providers.libraries.clear();
        this->publication.debug_object_ids.clear();
        this->publication.frame                = {};
        this->clock.simulation_step            = 0;
        this->publication.next_debug_object_id = 1;
        this->clock.accumulator                = {};
        this->clock.playing                    = false;
        this->publication.frame_pending        = false;
        this->publication.publishing_system    = nullptr;
        this->publication.publishing_frame     = nullptr;
        this->providers.descriptors.clear();
        this->publication.debug_primitives.clear();
        this->outputs.mesh_bindings.clear();
        this->outputs.particle_capacities.clear();
        this->outputs.hidden_instances.clear();
        this->configuration.source_scene = nullptr;
        this->configuration.setup        = {};
        this->configuration.initialized  = false;
    }

    void DynamicWorld::bind_resource(DynamicPortRuntime& port, const scene::DynamicPortBinding& binding) const {
        if (binding.resource_kind == scene::DynamicResourceKind::Instance) {
            const scene::InstanceId instance_id{binding.resource_id};
            if (!std::ranges::contains(this->configuration.source_scene->resources.instances, instance_id, &scene::Instance::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Instance", port.descriptor.id));
            port.resource_id = instance_id;
        } else if (binding.resource_kind == scene::DynamicResourceKind::Geometry) {
            const scene::GeometryId geometry_id{binding.resource_id};
            if (!std::ranges::contains(this->configuration.source_scene->resources.geometries, geometry_id, &scene::Geometry::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Geometry", port.descriptor.id));
            port.resource_id = geometry_id;
        } else if (binding.resource_kind == scene::DynamicResourceKind::ParticleSet) {
            const scene::ParticleSetId particle_set_id{binding.resource_id};
            if (!std::ranges::contains(this->configuration.source_scene->resources.particle_sets, particle_set_id, &scene::ParticleSet::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene ParticleSet", port.descriptor.id));
            port.resource_id = particle_set_id;
        } else {
            const scene::VolumeId volume_id{binding.resource_id};
            if (!std::ranges::contains(this->configuration.source_scene->resources.volumes, volume_id, &scene::Volume::id)) throw std::runtime_error(std::format("Dynamic Port '{}' binds a missing Scene Volume", port.descriptor.id));
            port.resource_id = volume_id;
        }
        const bool resource_kind_matches = (port.descriptor.resource_kind == dynamics::ResourceKind::InstanceTransform && std::holds_alternative<scene::InstanceId>(port.resource_id)) || (port.descriptor.resource_kind == dynamics::ResourceKind::TriangleMesh && std::holds_alternative<scene::GeometryId>(port.resource_id)) || (port.descriptor.resource_kind == dynamics::ResourceKind::ParticleSet && std::holds_alternative<scene::ParticleSetId>(port.resource_id)) || (port.descriptor.resource_kind == dynamics::ResourceKind::Volume && std::holds_alternative<scene::VolumeId>(port.resource_id)) || (port.descriptor.resource_kind == dynamics::ResourceKind::DebugDraw && std::holds_alternative<scene::InstanceId>(port.resource_id));
        if (!resource_kind_matches) throw std::runtime_error(std::format("Dynamic Port '{}' binds the wrong Scene resource kind", port.descriptor.id));
    }

    void DynamicWorld::declare_output(const DynamicPortRuntime& port) {
        if (const scene::GeometryId* geometry_id = std::get_if<scene::GeometryId>(&port.resource_id)) this->outputs.mesh_bindings.emplace_back(*geometry_id, port.descriptor.mesh_update_mode, static_cast<std::uint32_t>(port.capacity), static_cast<std::uint32_t>(port.secondary_capacity));
        if (const scene::ParticleSetId* particle_set_id = std::get_if<scene::ParticleSetId>(&port.resource_id)) this->outputs.particle_capacities.emplace_back(*particle_set_id, static_cast<std::uint32_t>(port.capacity));
    }

    void DynamicWorld::configure_port(DynamicSystemRuntime& system, const std::size_t port_index) {
        DynamicPortRuntime& binding = system.ports[port_index];
        if (binding.descriptor.resource_kind == dynamics::ResourceKind::DebugDraw) return;
        if (binding.timeline_signal_value != 0) this->context.runtime.resources.wait_external_timeline(binding.timeline_semaphore, binding.timeline_signal_value + 1);
        for (std::vector<DynamicPortBuffer>& slot_buffers : binding.buffer_slots)
            for (DynamicPortBuffer& port_buffer : slot_buffers)
                if (port_buffer.owns_descriptor) this->context.runtime.frames.retire_resource_descriptor(port_buffer.buffer_descriptor);
        binding.capacity                     = std::max(binding.capacity, binding.requested_capacity);
        binding.secondary_capacity           = std::max(binding.secondary_capacity, binding.requested_secondary_capacity);
        binding.requested_capacity           = 0;
        binding.requested_secondary_capacity = 0;
        binding.timeline_signal_value        = 0;
        binding.output_pending               = false;
        binding.buffer_slots.clear();
        const std::uint32_t output_slot_count = binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal ? VulkanFrames::frames_in_flight : 1;
        binding.buffer_slots.resize(output_slot_count);
        if (binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal) binding.timeline_semaphore = this->context.runtime.resources.create_external_simulation_timeline();
        for (std::uint32_t slot_index = 0; slot_index < output_slot_count; ++slot_index)
            for (std::uint32_t attribute_index = 0; attribute_index <= static_cast<std::uint32_t>(SpectraPluginAttribute::Bounds); ++attribute_index) {
                if ((binding.descriptor.attribute_mask & (1ull << attribute_index)) == 0) continue;
                const SpectraPluginAttribute attribute = static_cast<SpectraPluginAttribute>(attribute_index);
                const std::uint64_t count              = attribute == SpectraPluginAttribute::Index ? binding.secondary_capacity : binding.capacity;
                DynamicPortBuffer port_buffer{.attribute = attribute, .byte_size = count * attribute_size(attribute)};
                if (binding.descriptor.memory_domain == dynamics::MemoryDomain::Host)
                    port_buffer.host_storage.resize(port_buffer.byte_size);
                else {
                    port_buffer.gpu_buffer        = this->context.runtime.resources.create_external_buffer(port_buffer.byte_size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                    port_buffer.buffer_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
                    port_buffer.owns_descriptor   = true;
                    this->context.runtime.resources.write_buffer_descriptor(port_buffer.buffer_descriptor, vk::DescriptorType::eStorageBuffer, port_buffer.gpu_buffer);
                }
                binding.buffer_slots[slot_index].emplace_back(std::move(port_buffer));
            }

        std::vector<std::vector<SpectraPluginBuffer>> plugin_slot_buffers(output_slot_count);
        std::vector<SpectraPluginPortSlot> plugin_slots(output_slot_count);
        std::vector<ExternalHandle> exported_buffer_handles{};
        for (std::uint32_t slot_index = 0; slot_index < output_slot_count; ++slot_index) {
            plugin_slot_buffers[slot_index].reserve(binding.buffer_slots[slot_index].size());
            for (DynamicPortBuffer& port_buffer : binding.buffer_slots[slot_index]) {
                SpectraPluginExternalHandle exported_memory_handle{};
                if (binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal) {
                    exported_buffer_handles.emplace_back(this->context.runtime.resources.export_buffer_memory_handle(port_buffer.gpu_buffer));
                    exported_memory_handle = plugin_external_handle(exported_buffer_handles.back());
                }
                plugin_slot_buffers[slot_index].emplace_back(port_buffer.attribute, exported_memory_handle, port_buffer.host_storage.data(), port_buffer.byte_size);
            }
            plugin_slots[slot_index] = {slot_index, plugin_slot_buffers[slot_index].data(), plugin_slot_buffers[slot_index].size()};
        }
        ExternalHandle exported_timeline_handle{};
        if (binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal) exported_timeline_handle = this->context.runtime.resources.export_timeline_semaphore_handle(binding.timeline_semaphore);
        const GpuDeviceIdentity gpu_identity = this->context.runtime.graphics.identity;
        const SpectraPluginExternalHandle timeline_handle = binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal ? plugin_external_handle(exported_timeline_handle) : SpectraPluginExternalHandle{};
        SpectraPluginPortConfiguration configuration{binding.port_index, static_cast<SpectraPluginPortDirection>(binding.descriptor.direction), static_cast<SpectraPluginMemoryDomain>(binding.descriptor.memory_domain), plugin_slots.data(), plugin_slots.size(), timeline_handle, {}, {}, gpu_identity.node_mask};
        std::ranges::copy(gpu_identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(gpu_identity.luid, configuration.vulkan_device_luid);
        system.plugin_api->configure_port(system.provider_instance, &configuration);
    }

    DynamicWorld::DynamicPortRuntime& DynamicWorld::port_runtime(DynamicSystemRuntime& system, const std::uint64_t port_index) {
        if (port_index >= system.ports.size()) throw std::runtime_error("Provider published an unknown Port");
        return system.ports[port_index];
    }

    void DynamicWorld::set_inputs(DynamicSystemRuntime& system) {
        for (DynamicPortRuntime& binding : system.ports) {
            if (binding.descriptor.direction != dynamics::PortDirection::Input) continue;
            binding.active_count    = binding.capacity;
            binding.secondary_count = binding.secondary_capacity;
            binding.dirty_region.reset();
            binding.color_space = 0;
            for (DynamicPortBuffer& destination : binding.buffer_slots.front()) {
                if (const scene::InstanceId* instance_id = std::get_if<scene::InstanceId>(&binding.resource_id)) {
                    const scene::Instance& instance = *std::ranges::find(this->configuration.source_scene->resources.instances, *instance_id, &scene::Instance::id);
                    if (destination.attribute == SpectraPluginAttribute::Transform)
                        std::ranges::copy(instance.transform.matrix, reinterpret_cast<SpectraPluginTransform*>(destination.host_storage.data())->matrix);
                    else if (destination.attribute == SpectraPluginAttribute::Bounds) {
                        const math::Bounds3 bounds = *this->configuration.source_scene->view().local_bounds(*instance_id);
                        SpectraPluginFloat3* values = reinterpret_cast<SpectraPluginFloat3*>(destination.host_storage.data());
                        values[0]                   = {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
                        values[1]                   = {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
                    }
                    binding.active_count    = 1;
                    binding.secondary_count = 0;
                } else if (const scene::GeometryId* geometry_id = std::get_if<scene::GeometryId>(&binding.resource_id)) {
                    const scene::Geometry& geometry         = *std::ranges::find(this->configuration.source_scene->resources.geometries, *geometry_id, &scene::Geometry::id);
                    const scene::TriangleMeshGeometry& mesh = std::get<scene::TriangleMeshGeometry>(geometry.data);
                    const void* source{};
                    std::size_t size{};
                    if (destination.attribute == SpectraPluginAttribute::Position) {
                        source = mesh.positions.data();
                        size   = mesh.positions.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Normal) {
                        source = mesh.normals.data();
                        size   = mesh.normals.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Tangent) {
                        source = mesh.tangents.data();
                        size   = mesh.tangents.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::TextureCoordinate) {
                        source = mesh.texture_coordinates.data();
                        size   = mesh.texture_coordinates.size() * sizeof(math::Float2);
                    } else if (destination.attribute == SpectraPluginAttribute::Index) {
                        source = mesh.indices.data();
                        size   = mesh.indices.size() * sizeof(std::uint32_t);
                    }
                    if (size != 0) std::memcpy(destination.host_storage.data(), source, size);
                    binding.active_count    = mesh.positions.size();
                    binding.secondary_count = mesh.indices.size();
                } else if (const scene::ParticleSetId* particles_id = std::get_if<scene::ParticleSetId>(&binding.resource_id)) {
                    const scene::ParticleSet& particles = *std::ranges::find(this->configuration.source_scene->resources.particle_sets, *particles_id, &scene::ParticleSet::id);
                    const void* source{};
                    std::size_t size{};
                    if (destination.attribute == SpectraPluginAttribute::Position) {
                        source = particles.positions.data();
                        size   = particles.positions.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Radius) {
                        source = particles.radii.data();
                        size   = particles.radii.size() * sizeof(float);
                    } else if (destination.attribute == SpectraPluginAttribute::Velocity) {
                        source = particles.velocities.data();
                        size   = particles.velocities.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Color) {
                        source = particles.colors.data();
                        size   = particles.colors.size() * sizeof(math::Float3);
                    } else if (destination.attribute == SpectraPluginAttribute::Temperature) {
                        source = particles.temperatures.data();
                        size   = particles.temperatures.size() * sizeof(float);
                    } else if (destination.attribute == SpectraPluginAttribute::Material) {
                        source = particles.particle_materials.data();
                        size   = particles.particle_materials.size() * sizeof(scene::MaterialId);
                    }
                    if (size != 0) std::memcpy(destination.host_storage.data(), source, size);
                    binding.active_count    = particles.positions.size();
                    binding.secondary_count = 0;
                } else if (const scene::VolumeId* volume_id = std::get_if<scene::VolumeId>(&binding.resource_id)) {
                    const scene::Volume& volume = *std::ranges::find(this->configuration.source_scene->resources.volumes, *volume_id, &scene::Volume::id);
                    const void* source{};
                    std::size_t size{};
                    if (const scene::DensityGridVolume* grid = std::get_if<scene::DensityGridVolume>(&volume.data)) {
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
                        binding.color_space  = static_cast<std::uint32_t>(scene::SpectrumColorSpace::Srgb);
                    } else {
                        const scene::RgbGridVolume& rgb_grid = std::get<scene::RgbGridVolume>(volume.data);
                        if (destination.attribute == SpectraPluginAttribute::SigmaA) {
                            source = rgb_grid.sigma_a.data();
                            size   = rgb_grid.sigma_a.size() * sizeof(math::Float3);
                        } else if (destination.attribute == SpectraPluginAttribute::SigmaS) {
                            source = rgb_grid.sigma_s.data();
                            size   = rgb_grid.sigma_s.size() * sizeof(math::Float3);
                        } else if (destination.attribute == SpectraPluginAttribute::Emission) {
                            source = rgb_grid.emission.data();
                            size   = rgb_grid.emission.size() * sizeof(math::Float3);
                        }
                        binding.active_count = static_cast<std::uint64_t>(rgb_grid.resolution.x) * rgb_grid.resolution.y * rgb_grid.resolution.z;
                        binding.color_space  = static_cast<std::uint32_t>(rgb_grid.color_space);
                    }
                    if (size != 0) std::memcpy(destination.host_storage.data(), source, size);
                    binding.secondary_count = 0;
                    binding.dirty_region    = scene::VolumeRegion{{}, binding.descriptor.resolution};
                }
            }
            const scene::VolumeRegion region = binding.dirty_region.value_or(scene::VolumeRegion{});
            const SpectraPluginInputFrame input_frame{binding.port_index, binding.active_count, binding.secondary_count, {region.minimum.x, region.minimum.y, region.minimum.z}, {region.maximum.x, region.maximum.y, region.maximum.z}, binding.color_space};
            system.plugin_api->set_input_frame(system.provider_instance, &input_frame);
        }
    }

    void DynamicWorld::apply_parameters(DynamicSystemRuntime& system) {
        std::vector<SpectraPluginParameterValue> encoded{};
        encoded.reserve(system.parameter_values.size());
        for (const scene::DynamicParameterValue& value : system.parameter_values) encoded.emplace_back(plugin_parameter_value(value));
        system.plugin_api->apply_parameters(system.provider_instance, encoded.data(), encoded.size());
    }

    void DynamicWorld::commit_output(DynamicSystemRuntime& system, DynamicPortRuntime& binding, const SpectraPluginOutputCommit& commit, dynamics::DynamicFrame& dynamic_frame) {
        if (binding.descriptor.resource_kind == dynamics::ResourceKind::DebugDraw || binding.descriptor.direction != dynamics::PortDirection::Output) throw std::runtime_error("Provider committed data through a non-resource output Port");
        if (commit.slot_index >= binding.buffer_slots.size()) throw std::runtime_error("Provider committed an invalid output slot");
        if (commit.active_count > binding.capacity || commit.secondary_count > binding.secondary_capacity) throw std::runtime_error("Provider committed more elements than its configured output capacity");
        binding.current_output_slot_index = commit.slot_index;
        binding.active_count              = commit.active_count;
        binding.secondary_count           = commit.secondary_count;
        binding.timeline_signal_value     = commit.signal_value;
        binding.color_space               = commit.color_space;
        binding.dirty_region.reset();
        if (binding.descriptor.resource_kind == dynamics::ResourceKind::Volume) binding.dirty_region = scene::VolumeRegion{{commit.region_minimum[0], commit.region_minimum[1], commit.region_minimum[2]}, {commit.region_maximum[0], commit.region_maximum[1], commit.region_maximum[2]}};
        if (binding.descriptor.memory_domain == dynamics::MemoryDomain::CudaExternal) {
            binding.output_pending = true;
            return;
        }

        const std::vector<DynamicPortBuffer>& port_buffers = binding.buffer_slots.front();
        const auto find_buffer                             = [&port_buffers](const SpectraPluginAttribute attribute) -> const DynamicPortBuffer* {
            const auto found = std::ranges::find(port_buffers, attribute, &DynamicPortBuffer::attribute);
            return found == port_buffers.end() ? nullptr : std::to_address(found);
        };
        if (binding.descriptor.resource_kind == dynamics::ResourceKind::InstanceTransform) {
            const SpectraPluginTransform& value = *reinterpret_cast<const SpectraPluginTransform*>(find_buffer(SpectraPluginAttribute::Transform)->host_storage.data());
            math::Transform transform{};
            std::ranges::copy(value.matrix, transform.matrix.begin());
            dynamic_frame.instance_transform_updates.emplace_back(std::get<scene::InstanceId>(binding.resource_id), transform);
        } else if (binding.descriptor.resource_kind == dynamics::ResourceKind::TriangleMesh) {
            const DynamicPortBuffer* positions           = find_buffer(SpectraPluginAttribute::Position);
            const DynamicPortBuffer* normals             = find_buffer(SpectraPluginAttribute::Normal);
            const DynamicPortBuffer* tangents            = find_buffer(SpectraPluginAttribute::Tangent);
            const DynamicPortBuffer* texture_coordinates = find_buffer(SpectraPluginAttribute::TextureCoordinate);
            const DynamicPortBuffer* indices             = find_buffer(SpectraPluginAttribute::Index);
            dynamic_frame.triangle_mesh_updates.emplace_back(std::get<scene::GeometryId>(binding.resource_id), binding.descriptor.attribute_mask, binding.active_count, binding.secondary_count, positions ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(positions->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, normals ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(normals->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, tangents ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(tangents->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, texture_coordinates ? copy_plugin_float2_values(reinterpret_cast<const SpectraPluginFloat2*>(texture_coordinates->host_storage.data()), binding.active_count) : std::vector<math::Float2>{},
                indices ? std::vector<std::uint32_t>{reinterpret_cast<const std::uint32_t*>(indices->host_storage.data()), reinterpret_cast<const std::uint32_t*>(indices->host_storage.data()) + binding.secondary_count} : std::vector<std::uint32_t>{});
        } else if (binding.descriptor.resource_kind == dynamics::ResourceKind::ParticleSet) {
            const DynamicPortBuffer* positions    = find_buffer(SpectraPluginAttribute::Position);
            const DynamicPortBuffer* radii        = find_buffer(SpectraPluginAttribute::Radius);
            const DynamicPortBuffer* velocities   = find_buffer(SpectraPluginAttribute::Velocity);
            const DynamicPortBuffer* colors       = find_buffer(SpectraPluginAttribute::Color);
            const DynamicPortBuffer* temperatures = find_buffer(SpectraPluginAttribute::Temperature);
            const DynamicPortBuffer* materials    = find_buffer(SpectraPluginAttribute::Material);
            std::vector<scene::MaterialId> material_values{};
            if (materials) {
                material_values.resize(binding.active_count);
                for (std::size_t index = 0; index < material_values.size(); ++index) material_values[index] = scene::MaterialId{reinterpret_cast<const std::uint64_t*>(materials->host_storage.data())[index]};
            }
            dynamic_frame.particle_set_updates.emplace_back(std::get<scene::ParticleSetId>(binding.resource_id), binding.descriptor.attribute_mask, binding.active_count, positions ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(positions->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, radii ? std::vector<float>{reinterpret_cast<const float*>(radii->host_storage.data()), reinterpret_cast<const float*>(radii->host_storage.data()) + binding.active_count} : std::vector<float>{}, velocities ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(velocities->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, colors ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(colors->host_storage.data()), binding.active_count) : std::vector<math::Float3>{},
                temperatures ? std::vector<float>{reinterpret_cast<const float*>(temperatures->host_storage.data()), reinterpret_cast<const float*>(temperatures->host_storage.data()) + binding.active_count} : std::vector<float>{}, std::move(material_values));
        } else if (binding.descriptor.resource_kind == dynamics::ResourceKind::Volume) {
            const DynamicPortBuffer* density        = find_buffer(SpectraPluginAttribute::Density);
            const DynamicPortBuffer* temperature    = find_buffer(SpectraPluginAttribute::Temperature);
            const DynamicPortBuffer* emission_scale = find_buffer(SpectraPluginAttribute::EmissionScale);
            const DynamicPortBuffer* sigma_a        = find_buffer(SpectraPluginAttribute::SigmaA);
            const DynamicPortBuffer* sigma_s        = find_buffer(SpectraPluginAttribute::SigmaS);
            const DynamicPortBuffer* emission       = find_buffer(SpectraPluginAttribute::Emission);
            const DynamicPortBuffer* velocity       = find_buffer(SpectraPluginAttribute::Velocity);
            const scene::VolumeRegion region        = *binding.dirty_region;
            dynamic_frame.volume_updates.emplace_back(std::get<scene::VolumeId>(binding.resource_id), binding.descriptor.attribute_mask, binding.descriptor.resolution, region, static_cast<scene::SpectrumColorSpace>(binding.color_space), density ? std::vector<float>{reinterpret_cast<const float*>(density->host_storage.data()), reinterpret_cast<const float*>(density->host_storage.data()) + binding.active_count} : std::vector<float>{}, temperature ? std::vector<float>{reinterpret_cast<const float*>(temperature->host_storage.data()), reinterpret_cast<const float*>(temperature->host_storage.data()) + binding.active_count} : std::vector<float>{}, emission_scale ? std::vector<float>{reinterpret_cast<const float*>(emission_scale->host_storage.data()), reinterpret_cast<const float*>(emission_scale->host_storage.data()) + binding.active_count} : std::vector<float>{},
                sigma_a ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(sigma_a->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, sigma_s ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(sigma_s->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, emission ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(emission->host_storage.data()), binding.active_count) : std::vector<math::Float3>{}, velocity && this->configuration.setup.systems[system.scene_system_index].visible ? copy_plugin_float3_values(reinterpret_cast<const SpectraPluginFloat3*>(velocity->host_storage.data()), binding.active_count) : std::vector<math::Float3>{});
        }
    }

    void DynamicWorld::publish_frame(const std::uint64_t simulation_step) {
        if (this->publication.frame_pending) {
            for (DynamicSystemRuntime& system : this->systems.runtimes)
                for (DynamicPortRuntime& binding : system.ports)
                    if (binding.output_pending) {
                        this->context.runtime.resources.wait_external_timeline(binding.timeline_semaphore, binding.timeline_signal_value);
                        this->context.runtime.resources.signal_external_timeline(binding.timeline_semaphore, binding.timeline_signal_value + 1);
                        binding.output_pending = false;
                    }
            this->publication.frame_pending = false;
        }
        dynamics::DynamicFrame next{};
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            for (DynamicPortRuntime& binding : system.ports)
                if (binding.requested_capacity > binding.capacity || binding.requested_secondary_capacity > binding.secondary_capacity) this->configure_port(system, binding.port_index);
            this->publication.publishing_system = &system;
            this->publication.publishing_frame  = &next;
            const SpectraPluginFrameSink sink{this, &DynamicWorld::collect_debug, &DynamicWorld::collect_output, &DynamicWorld::collect_capacity};
            system.plugin_api->publish_frame(system.provider_instance, simulation_step, &sink);
            bool reconfigure{};
            for (DynamicPortRuntime& binding : system.ports)
                if (binding.requested_capacity > binding.capacity || binding.requested_secondary_capacity > binding.secondary_capacity) {
                    this->configure_port(system, binding.port_index);
                    reconfigure = true;
                }
            if (reconfigure) system.plugin_api->publish_frame(system.provider_instance, simulation_step, &sink);
        }
        this->publication.publishing_system = nullptr;
        this->publication.publishing_frame  = nullptr;
        next.simulation_seconds             = static_cast<double>(simulation_step) * this->configuration.setup.clock.step_seconds;
        this->publication.frame             = std::move(next);
        this->publication.debug_primitives  = this->publication.frame.debug_primitives;
        this->publication.frame_pending     = true;
    }

    void DynamicWorld::step_to(const std::uint64_t target_step) {
        if (target_step <= this->clock.simulation_step) return;
        const std::uint64_t step_count = target_step - this->clock.simulation_step;
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            this->set_inputs(system);
            system.plugin_api->step(system.provider_instance, this->configuration.setup.clock.step_seconds, step_count);
        }
        this->clock.simulation_step = target_step;
    }

    void DynamicWorld::reset_systems() {
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            this->set_inputs(system);
            system.plugin_api->reset(system.provider_instance, this->configuration.setup.seed);
        }
        this->clock.simulation_step = 0;
        this->step_to(this->configuration.setup.clock.start_step);
    }

    void DynamicWorld::evaluate_frame(const std::uint64_t target_step) {
        this->step_to(target_step);
        this->publish_frame(target_step);
    }

    void DynamicWorld::reset_simulation() {
        this->clock.accumulator = {};
        this->reset_systems();
        this->publish_frame(this->clock.simulation_step);
    }

    void DynamicWorld::seek(const std::uint64_t step) {
        this->clock.playing     = false;
        this->clock.accumulator = {};
        if (step < this->clock.simulation_step) {
            this->reset_systems();
        }
        if (step != this->clock.simulation_step)
            this->evaluate_frame(step);
        else if (!this->publication.frame_pending)
            this->publish_frame(step);
    }

    void DynamicWorld::advance(const std::chrono::duration<double> elapsed) {
        if (!this->clock.playing || this->systems.runtimes.empty()) return;
        this->clock.accumulator += elapsed;
        const std::chrono::duration<double> step_duration{this->configuration.setup.clock.step_seconds};
        std::uint64_t remaining_steps = static_cast<std::uint64_t>(this->clock.accumulator / step_duration);
        if (remaining_steps == 0) return;
        this->clock.accumulator -= step_duration * remaining_steps;

        while (remaining_steps != 0) {
            if (!this->configuration.setup.clock.end_step) {
                this->step_to(this->clock.simulation_step + remaining_steps);
                remaining_steps = 0;
                continue;
            }
            if (this->clock.simulation_step == *this->configuration.setup.clock.end_step) {
                if (!this->configuration.setup.clock.loop) {
                    this->clock.playing     = false;
                    this->clock.accumulator = {};
                    remaining_steps         = 0;
                    continue;
                }
                this->reset_systems();
            }
            const std::uint64_t segment = std::min(remaining_steps, *this->configuration.setup.clock.end_step - this->clock.simulation_step);
            this->step_to(this->clock.simulation_step + segment);
            remaining_steps -= segment;
        }
        this->publish_frame(this->clock.simulation_step);
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step == *this->configuration.setup.clock.end_step && !this->configuration.setup.clock.loop) {
            this->clock.playing     = false;
            this->clock.accumulator = {};
        }
    }

    const dynamics::DynamicFrame* DynamicWorld::pending_frame() noexcept {
        if (!this->publication.frame_pending) return nullptr;
        this->publication.frame.gpu_output_resources.clear();
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicPortRuntime& binding : system.ports) {
                if (!binding.output_pending) continue;
                this->context.runtime.frames.enqueue_external_wait(binding.timeline_semaphore, binding.timeline_signal_value, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader);
                std::vector<dynamics::GpuOutputAttributeView> attributes{};
                for (const DynamicPortBuffer& port_buffer : binding.buffer_slots[binding.current_output_slot_index]) {
                    if (binding.descriptor.resource_kind == dynamics::ResourceKind::Volume && port_buffer.attribute == SpectraPluginAttribute::Velocity && !this->configuration.setup.systems[system.scene_system_index].visible) continue;
                    attributes.emplace_back(static_cast<dynamics::Attribute>(port_buffer.attribute), &port_buffer.gpu_buffer, port_buffer.buffer_descriptor);
                }
                this->publication.frame.gpu_output_resources.emplace_back(binding.descriptor.resource_kind, binding.resource_id, std::move(attributes), binding.active_count, binding.secondary_count, binding.dirty_region);
            }
        return &this->publication.frame;
    }

    void DynamicWorld::consume_frame() noexcept {
        if (!this->publication.frame_pending) return;
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicPortRuntime& binding : system.ports)
                if (binding.output_pending) {
                    this->context.runtime.frames.enqueue_external_signal(binding.timeline_semaphore, binding.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
                    binding.output_pending = false;
                }
        this->publication.frame_pending = false;
    }

    bool DynamicWorld::controls(const scene::InstanceId instance_id) const noexcept {
        for (const DynamicSystemRuntime& system : this->systems.runtimes)
            for (const DynamicPortRuntime& binding : system.ports) {
                if (binding.descriptor.direction != dynamics::PortDirection::Output || binding.descriptor.resource_kind == dynamics::ResourceKind::DebugDraw) continue;
                if (const scene::InstanceId* direct = std::get_if<scene::InstanceId>(&binding.resource_id); direct && *direct == instance_id) return true;
                const auto instance = std::ranges::find(this->configuration.source_scene->resources.instances, instance_id, &scene::Instance::id);
                if (instance == this->configuration.source_scene->resources.instances.end()) continue;
                const auto prototype = std::ranges::find(this->configuration.source_scene->resources.prototypes, instance->prototype, &scene::Prototype::id);
                if (prototype == this->configuration.source_scene->resources.prototypes.end()) continue;
                if (const scene::GeometryId* geometry = std::get_if<scene::GeometryId>(&binding.resource_id); geometry && std::ranges::any_of(prototype->primitives, [geometry](const scene::Primitive& primitive) { return primitive.geometry == *geometry; })) return true;
                if (const scene::ParticleSetId* particles = std::get_if<scene::ParticleSetId>(&binding.resource_id); particles && std::ranges::any_of(prototype->primitives, [particles](const scene::Primitive& primitive) { return primitive.particles == *particles; })) return true;
                if (const scene::VolumeId* volume = std::get_if<scene::VolumeId>(&binding.resource_id); volume && std::ranges::any_of(prototype->primitives, [volume](const scene::Primitive& primitive) { return primitive.volume == *volume; })) return true;
            }
        return false;
    }

    void DynamicWorld::advance_one_step() {
        this->clock.accumulator = {};
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step >= *this->configuration.setup.clock.end_step) {
            if (!this->configuration.setup.clock.loop) return;
            this->reset_systems();
        }
        const std::uint64_t requested = this->clock.simulation_step + 1;
        this->evaluate_frame(this->configuration.setup.clock.end_step ? std::min(requested, *this->configuration.setup.clock.end_step) : requested);
    }

    void DynamicWorld::apply_parameter_changes(const std::size_t system_index, const std::span<const scene::DynamicParameterSetting> parameters, const bool reset) {
        this->configuration.setup.systems[system_index].parameters.assign(parameters.begin(), parameters.end());
        const auto found = std::ranges::find(this->systems.runtimes, system_index, &DynamicSystemRuntime::scene_system_index);
        if (found == this->systems.runtimes.end()) return;
        DynamicSystemRuntime& destination = *found;
        destination.parameter_values.clear();
        for (const dynamics::ParameterDescriptor& descriptor : destination.provider_descriptor->parameters) {
            const auto configured = std::ranges::find(parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
            destination.parameter_values.emplace_back(configured == parameters.end() ? descriptor.value : configured->value);
        }
        this->apply_parameters(destination);
        if (reset) this->reset_simulation();
    }

} // namespace spectra

namespace spectra {
    bool DynamicWorld::running() const noexcept {
        return this->clock.playing;
    }

    dynamics::SimulationTimeline DynamicWorld::timeline() const noexcept {
        return {this->clock.simulation_step, this->publication.frame.simulation_seconds};
    }

    void DynamicWorld::start() {
        this->clock.playing = true;
    }

    void DynamicWorld::pause() {
        this->clock.playing = false;
    }

    void DynamicWorld::step() {
        this->advance_one_step();
    }

    void DynamicWorld::reset() {
        this->clock.playing = false;
        this->reset_simulation();
    }

} // namespace spectra
