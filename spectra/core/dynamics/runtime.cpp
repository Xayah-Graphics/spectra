module;

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include <spectra/plugin_api.h>

#undef interface

module spectra.dynamics.runtime;

import spectra.plugin.abi;
import std;
import vulkan;

namespace spectra {
    namespace {
        static_assert(sizeof(SpectraPluginTransform) == sizeof(plugin_abi::SpectraPluginTransform));
        static_assert(alignof(SpectraPluginTransform) == alignof(plugin_abi::SpectraPluginTransform));
        static_assert(offsetof(SpectraPluginTransform, matrix) == offsetof(plugin_abi::SpectraPluginTransform, matrix));
        static_assert(sizeof(SpectraPluginPoint) == sizeof(plugin_abi::SpectraPluginPoint));
        static_assert(alignof(SpectraPluginPoint) == alignof(plugin_abi::SpectraPluginPoint));
        static_assert(offsetof(SpectraPluginPoint, position) == offsetof(plugin_abi::SpectraPluginPoint, position));
        static_assert(offsetof(SpectraPluginPoint, radius) == offsetof(plugin_abi::SpectraPluginPoint, radius));
        static_assert(offsetof(SpectraPluginPoint, color) == offsetof(plugin_abi::SpectraPluginPoint, color));
        static_assert(offsetof(SpectraPluginPoint, scalar) == offsetof(plugin_abi::SpectraPluginPoint, scalar));
        static_assert(sizeof(SpectraPluginSegment) == sizeof(plugin_abi::SpectraPluginSegment));
        static_assert(alignof(SpectraPluginSegment) == alignof(plugin_abi::SpectraPluginSegment));
        static_assert(offsetof(SpectraPluginSegment, first_position) == offsetof(plugin_abi::SpectraPluginSegment, first_position));
        static_assert(offsetof(SpectraPluginSegment, width) == offsetof(plugin_abi::SpectraPluginSegment, width));
        static_assert(offsetof(SpectraPluginSegment, second_position) == offsetof(plugin_abi::SpectraPluginSegment, second_position));
        static_assert(offsetof(SpectraPluginSegment, color) == offsetof(plugin_abi::SpectraPluginSegment, color));
        static_assert(sizeof(SpectraPluginCurve) == sizeof(plugin_abi::SpectraPluginCurve));
        static_assert(alignof(SpectraPluginCurve) == alignof(plugin_abi::SpectraPluginCurve));
        static_assert(offsetof(SpectraPluginCurve, control_0) == offsetof(plugin_abi::SpectraPluginCurve, control_0));
        static_assert(offsetof(SpectraPluginCurve, width) == offsetof(plugin_abi::SpectraPluginCurve, width));
        static_assert(offsetof(SpectraPluginCurve, control_1) == offsetof(plugin_abi::SpectraPluginCurve, control_1));
        static_assert(offsetof(SpectraPluginCurve, control_2) == offsetof(plugin_abi::SpectraPluginCurve, control_2));
        static_assert(offsetof(SpectraPluginCurve, control_3) == offsetof(plugin_abi::SpectraPluginCurve, control_3));
        static_assert(offsetof(SpectraPluginCurve, color) == offsetof(plugin_abi::SpectraPluginCurve, color));
        static_assert(sizeof(SpectraPluginVector) == sizeof(plugin_abi::SpectraPluginVector));
        static_assert(alignof(SpectraPluginVector) == alignof(plugin_abi::SpectraPluginVector));
        static_assert(offsetof(SpectraPluginVector, origin) == offsetof(plugin_abi::SpectraPluginVector, origin));
        static_assert(offsetof(SpectraPluginVector, width) == offsetof(plugin_abi::SpectraPluginVector, width));
        static_assert(offsetof(SpectraPluginVector, vector) == offsetof(plugin_abi::SpectraPluginVector, vector));
        static_assert(offsetof(SpectraPluginVector, color) == offsetof(plugin_abi::SpectraPluginVector, color));
        static_assert(sizeof(SpectraPluginCameraDistortion) == sizeof(plugin_abi::SpectraPluginCameraDistortion));
        static_assert(alignof(SpectraPluginCameraDistortion) == alignof(plugin_abi::SpectraPluginCameraDistortion));
        static_assert(offsetof(SpectraPluginCameraDistortion, radial_1) == offsetof(plugin_abi::SpectraPluginCameraDistortion, radial_1));
        static_assert(offsetof(SpectraPluginCameraDistortion, radial_2) == offsetof(plugin_abi::SpectraPluginCameraDistortion, radial_2));
        static_assert(offsetof(SpectraPluginCameraDistortion, tangential_1) == offsetof(plugin_abi::SpectraPluginCameraDistortion, tangential_1));
        static_assert(offsetof(SpectraPluginCameraDistortion, tangential_2) == offsetof(plugin_abi::SpectraPluginCameraDistortion, tangential_2));
        static_assert(sizeof(SpectraPluginCameraObservation) == sizeof(plugin_abi::SpectraPluginCameraObservation));
        static_assert(alignof(SpectraPluginCameraObservation) == alignof(plugin_abi::SpectraPluginCameraObservation));
        static_assert(offsetof(SpectraPluginCameraObservation, world_from_camera) == offsetof(plugin_abi::SpectraPluginCameraObservation, world_from_camera));
        static_assert(offsetof(SpectraPluginCameraObservation, intrinsics) == offsetof(plugin_abi::SpectraPluginCameraObservation, intrinsics));
        static_assert(offsetof(SpectraPluginCameraObservation, distortion) == offsetof(plugin_abi::SpectraPluginCameraObservation, distortion));
        static_assert(offsetof(SpectraPluginCameraObservation, image_layer) == offsetof(plugin_abi::SpectraPluginCameraObservation, image_layer));
        static_assert(std::to_underlying(scene::DynamicParameterKind::Boolean) == std::to_underlying(SpectraPluginParameterKind::Boolean));
        static_assert(std::to_underlying(scene::DynamicParameterKind::Integer) == std::to_underlying(SpectraPluginParameterKind::Integer));
        static_assert(std::to_underlying(scene::DynamicParameterKind::Float) == std::to_underlying(SpectraPluginParameterKind::Float));
        static_assert(std::to_underlying(scene::DynamicParameterKind::Float3) == std::to_underlying(SpectraPluginParameterKind::Float3));
        static_assert(std::to_underlying(scene::DynamicParameterKind::Enumeration) == std::to_underlying(SpectraPluginParameterKind::Enumeration));
        static_assert(std::to_underlying(scene::SpectrumColorSpace::Srgb) == std::to_underlying(SpectraPluginColorSpace::Srgb));
        static_assert(std::to_underlying(scene::SpectrumColorSpace::Rec2020) == std::to_underlying(SpectraPluginColorSpace::Rec2020));
        static_assert(std::to_underlying(scene::SpectrumColorSpace::Aces2065_1) == std::to_underlying(SpectraPluginColorSpace::Aces2065_1));

        [[nodiscard]] std::string plugin_string(const SpectraPluginString value) {
            return value.size == 0 ? std::string{} : std::string{value.data, value.size};
        }

        [[nodiscard]] scene::DynamicParameterValue scene_parameter_value(const SpectraPluginParameterValue value) noexcept {
            return {static_cast<scene::DynamicParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraPluginParameterValue plugin_parameter_value(const scene::DynamicParameterValue& value) noexcept {
            return {static_cast<SpectraPluginParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraPluginExternalHandle plugin_external_handle(const ExternalHandle& handle) noexcept {
            return {
                handle.type == ExternalHandleType::OpaqueWin32            ? SpectraPluginExternalHandleType::OpaqueWin32
                : handle.type == ExternalHandleType::OpaqueFileDescriptor ? SpectraPluginExternalHandleType::OpaqueFileDescriptor
                                                                          : SpectraPluginExternalHandleType::None,
                handle.value,
            };
        }

        [[nodiscard]] std::uint64_t dataset_element_size(const dynamics::DatasetDescriptor& dataset, const dynamics::DatasetBufferDescriptor& buffer) {
            if (buffer.kind == dynamics::DatasetBufferKind::MeshPosition || buffer.kind == dynamics::DatasetBufferKind::MeshNormal || buffer.kind == dynamics::DatasetBufferKind::MeshTangent) return sizeof(SpectraPluginFloat3);
            if (buffer.kind == dynamics::DatasetBufferKind::MeshTextureCoordinate) return sizeof(SpectraPluginFloat2);
            if (buffer.kind == dynamics::DatasetBufferKind::MeshIndex) return sizeof(std::uint32_t);
            if (buffer.kind == dynamics::DatasetBufferKind::Point) return sizeof(SpectraPluginPoint);
            if (buffer.kind == dynamics::DatasetBufferKind::Segment) return sizeof(SpectraPluginSegment);
            if (buffer.kind == dynamics::DatasetBufferKind::Curve) return sizeof(SpectraPluginCurve);
            if (buffer.kind == dynamics::DatasetBufferKind::Vector) return sizeof(SpectraPluginVector);
            if (buffer.kind == dynamics::DatasetBufferKind::CameraObservation) return sizeof(SpectraPluginCameraObservation);
            if (buffer.kind == dynamics::DatasetBufferKind::Transform) return sizeof(SpectraPluginTransform);
            if (buffer.kind == dynamics::DatasetBufferKind::TelemetryValue) return sizeof(SpectraPluginTelemetryGpuValue);
            if (buffer.kind == dynamics::DatasetBufferKind::FieldChannel) return dataset.field_channels[buffer.channel_index].kind == dynamics::FieldChannelKind::Float ? sizeof(float) : sizeof(SpectraPluginFloat3);
            if (dataset.image_format == dynamics::ImageFormat::Rgba8Unorm) return sizeof(std::uint32_t);
            if (dataset.image_format == dynamics::ImageFormat::Rgba16Float) return sizeof(std::uint16_t) * 4;
            return sizeof(float) * 4;
        }

        [[nodiscard]] std::uint64_t dataset_element_count(const dynamics::DatasetDescriptor& dataset, const dynamics::DatasetBufferDescriptor& buffer, const std::uint64_t capacity, const std::uint64_t secondary_capacity) noexcept {
            if (buffer.kind == dynamics::DatasetBufferKind::MeshIndex) return secondary_capacity;
            if (buffer.kind == dynamics::DatasetBufferKind::ImagePixel) {
                const std::uint64_t pixels = static_cast<std::uint64_t>(dataset.image_extent[0]) * dataset.image_extent[1];
                return dataset.kind == dynamics::DatasetKind::CameraObservationSet ? capacity * pixels : pixels;
            }
            return capacity;
        }

    } // namespace

    DynamicsRuntime::ProviderLibrary::ProviderLibrary(const std::filesystem::path& library_path, const std::string_view expected_provider_id) : library_path(std::filesystem::weakly_canonical(library_path)) {
#if defined(_WIN32)
        const HMODULE loaded = LoadLibraryW(this->library_path.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load Provider Library: {}", this->library_path.string()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*)()>(GetProcAddress(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
#else
        void* loaded = dlopen(this->library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!loaded) throw std::runtime_error(std::format("Linux failed to load Provider Library '{}': {}", this->library_path.string(), dlerror()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*)()>(dlsym(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
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
        if (!loaded_api || loaded_api->api_version != SPECTRA_PLUGIN_API_VERSION || loaded_api->struct_size != sizeof(SpectraPluginApi) || !loaded_api->describe_provider || !loaded_api->create_provider || !loaded_api->destroy_provider || !loaded_api->configure_dataset || !loaded_api->configure_telemetry || !loaded_api->apply_parameters || !loaded_api->reset || !loaded_api->step || !loaded_api->publish_frame || !loaded_api->tick_presentation) {
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

    DynamicsRuntime::ProviderLibrary::~ProviderLibrary() {
        if (!this->library_handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(this->library_handle));
#else
        dlclose(this->library_handle);
#endif
    }

    DynamicsRuntime::DynamicsRuntime(VulkanRuntime& runtime, SceneDocument& document) noexcept : context{runtime, document} {}

    DynamicsRuntime::~DynamicsRuntime() {
        this->destroy();
    }

    DynamicsRuntime::ProviderLibrary& DynamicsRuntime::provider_library(const std::string_view provider_id) const {
        const auto found = this->providers.by_id.find(std::string{provider_id});
        if (found == this->providers.by_id.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider_id));
        return *found->second;
    }

    const dynamics::ProviderDescriptor& DynamicsRuntime::provider_descriptor(const std::string_view provider_id) const {
        const auto found = std::ranges::find(this->providers.descriptors, provider_id, &dynamics::ProviderDescriptor::id);
        if (found == this->providers.descriptors.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider_id));
        return *found;
    }

    const dynamics::TelemetrySnapshot& DynamicsRuntime::telemetry(const std::size_t system_index) const {
        const auto found = std::ranges::find(this->systems.runtimes, system_index, &DynamicSystemRuntime::scene_system_index);
        if (found == this->systems.runtimes.end()) throw std::runtime_error("Scene Dynamics does not contain the requested System runtime");
        return found->telemetry;
    }

    bool DynamicsRuntime::initialized() const noexcept {
        return this->configuration.initialized;
    }

    std::span<const dynamics::MeshOutputBinding> DynamicsRuntime::mesh_bindings() const noexcept {
        return this->outputs.mesh_bindings;
    }

    std::span<const dynamics::SphereSetOutputBinding> DynamicsRuntime::sphere_set_bindings() const noexcept {
        return this->outputs.sphere_set_bindings;
    }

    std::span<const dynamics::GpuVisualizationDatasetView> DynamicsRuntime::visualizations() const noexcept {
        return this->publication.visualizations;
    }

    void DynamicsRuntime::collect_dataset(void* context, const std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit) {
        DynamicsRuntime& world          = *static_cast<DynamicsRuntime*>(context);
        DynamicSystemRuntime& system = *world.publication.publishing_system;
        world.commit_dataset(system, world.dataset_runtime(system, dataset_index), *commit, *world.publication.publishing_frame);
    }

    void DynamicsRuntime::collect_capacity(void* context, const std::uint64_t dataset_index, const std::uint64_t capacity, const std::uint64_t secondary_capacity) {
        DynamicsRuntime& world                   = *static_cast<DynamicsRuntime*>(context);
        DynamicDatasetRuntime& dataset        = world.dataset_runtime(*world.publication.publishing_system, dataset_index);
        dataset.requested_capacity            = std::max(dataset.requested_capacity, capacity);
        dataset.requested_secondary_capacity  = std::max(dataset.requested_secondary_capacity, secondary_capacity);
    }

    void DynamicsRuntime::collect_telemetry(void* context, const SpectraPluginTelemetryCommit* commit) {
        DynamicsRuntime& world                    = *static_cast<DynamicsRuntime*>(context);
        DynamicTelemetryRuntime& telemetry     = world.publication.publishing_system->telemetry_gpu;
        if (telemetry.output_pending) world.flush_telemetry(*world.publication.publishing_system);
        telemetry.current_slot_index           = commit->slot_index;
        telemetry.timeline_signal_value        = commit->signal_value;
        telemetry.simulation_step              = world.publication.publishing_frame->simulation_step;
        telemetry.sequence                     = telemetry.next_sequence++;
        telemetry.simulation_seconds           = world.publication.publishing_frame->simulation_seconds;
        telemetry.phase                        = plugin_string(commit->phase);
        telemetry.headline                     = plugin_string(commit->headline);
        telemetry.message                      = plugin_string(commit->message);
        telemetry.output_pending               = true;
    }

    void DynamicsRuntime::initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene) {
        this->destroy();
        this->configuration.source_scene = &source_scene;
        this->configuration.setup        = *source_scene.dynamic_setup;
        if (this->configuration.setup.clock.end_step && *this->configuration.setup.clock.end_step < this->configuration.setup.clock.start_step) throw std::runtime_error("Scene Dynamics clock end step precedes its start step");

        std::vector<std::string> required_providers{};
        for (const scene::DynamicSystem& system : this->configuration.setup.systems)
            if (!std::ranges::contains(required_providers, system.provider_id)) required_providers.emplace_back(system.provider_id);
        std::ranges::sort(required_providers);

        for (const std::string& required_provider : required_providers) {
            const std::filesystem::path path                      = scene_path.parent_path() / scene::provider_library_filename(required_provider);
            ProviderLibrary& library                              = this->providers.libraries.emplace_back(path, required_provider);
            const SpectraPluginProviderDescriptor source_provider = library.plugin_api->describe_provider();
            dynamics::ProviderDescriptor provider{.id = plugin_string(source_provider.id)};
            provider.datasets.reserve(source_provider.dataset_count);
            for (std::uint64_t dataset_index = 0; dataset_index < source_provider.dataset_count; ++dataset_index) {
                const SpectraPluginDatasetDescriptor& source = source_provider.datasets[dataset_index];
                dynamics::DatasetDescriptor dataset{
                    .id                 = plugin_string(source.id),
                    .kind               = static_cast<dynamics::DatasetKind>(source.kind),
                    .capacity           = source.capacity,
                    .secondary_capacity = source.secondary_capacity,
                    .mesh_update_mode   = static_cast<dynamics::MeshUpdateMode>(source.mesh_update_mode),
                    .resolution         = {source.resolution[0], source.resolution[1], source.resolution[2]},
                    .image_extent       = {source.image_extent[0], source.image_extent[1]},
                    .image_format       = static_cast<dynamics::ImageFormat>(source.image_format),
                    .color_space        = static_cast<scene::SpectrumColorSpace>(source.color_space),
                };
                dataset.buffers.reserve(source.buffer_count);
                for (std::uint64_t buffer_index = 0; buffer_index < source.buffer_count; ++buffer_index) dataset.buffers.emplace_back(static_cast<dynamics::DatasetBufferKind>(source.buffers[buffer_index].kind), source.buffers[buffer_index].channel_index);
                dataset.field_channels.reserve(source.field_channel_count);
                for (std::uint64_t channel_index = 0; channel_index < source.field_channel_count; ++channel_index) dataset.field_channels.emplace_back(plugin_string(source.field_channels[channel_index].id), static_cast<dynamics::FieldChannelKind>(source.field_channels[channel_index].kind));
                provider.datasets.emplace_back(std::move(dataset));
            }
            provider.parameters.reserve(source_provider.parameter_count);
            for (std::uint64_t parameter_index = 0; parameter_index < source_provider.parameter_count; ++parameter_index) {
                const SpectraPluginParameterDescriptor& parameter = source_provider.parameters[parameter_index];
                dynamics::ParameterDescriptor value{
                    .id               = plugin_string(parameter.id),
                    .name             = plugin_string(parameter.name),
                    .unit             = plugin_string(parameter.unit),
                    .section_id       = plugin_string(parameter.section_id),
                    .description      = plugin_string(parameter.description),
                    .application_mode = static_cast<dynamics::ParameterApplication>(parameter.application_mode),
                    .value            = scene_parameter_value(parameter.default_value),
                    .minimum          = scene_parameter_value(parameter.minimum),
                    .maximum          = scene_parameter_value(parameter.maximum),
                    .step             = scene_parameter_value(parameter.step),
                };
                for (std::uint64_t enumerator = 0; enumerator < parameter.enumerator_count; ++enumerator) value.enumerators.emplace_back(plugin_string(parameter.enumerators[enumerator]));
                provider.parameters.emplace_back(std::move(value));
            }
            for (std::uint64_t section_index = 0; section_index < source_provider.section_count; ++section_index) {
                const SpectraPluginSectionDescriptor& section = source_provider.sections[section_index];
                provider.sections.emplace_back(plugin_string(section.id), plugin_string(section.name));
            }
            for (std::uint64_t telemetry_index = 0; telemetry_index < source_provider.telemetry_count; ++telemetry_index) {
                const SpectraPluginTelemetryDescriptor& telemetry = source_provider.telemetry[telemetry_index];
                provider.telemetry.emplace_back(plugin_string(telemetry.id), plugin_string(telemetry.name), plugin_string(telemetry.unit), plugin_string(telemetry.section_id), static_cast<dynamics::TelemetryKind>(telemetry.kind), telemetry.plot);
            }
            if (!this->providers.by_id.emplace(provider.id, &library).second) throw std::runtime_error(std::format("Provider '{}' is loaded more than once", provider.id));
            this->providers.descriptors.emplace_back(std::move(provider));
        }

        std::unordered_map<std::string, std::size_t> scene_writers{};
        for (std::size_t system_index = 0; system_index < this->configuration.setup.systems.size(); ++system_index) {
            const scene::DynamicSystem& declared = this->configuration.setup.systems[system_index];
            if (!declared.enabled) continue;
            const dynamics::ProviderDescriptor& provider = this->provider_descriptor(declared.provider_id);
            ProviderLibrary& library                       = this->provider_library(provider.id);
            DynamicSystemRuntime& system = this->systems.runtimes.emplace_back(DynamicSystemRuntime{.scene_system_index = system_index, .provider_descriptor = &provider, .plugin_api = library.plugin_api});
            system.telemetry.values.resize(provider.telemetry.size());
            system.provider_instance = system.plugin_api->create_provider();
            if (!system.provider_instance) {
                this->systems.runtimes.pop_back();
                throw std::runtime_error(std::format("Provider '{}' refused to create its declared instance", provider.id));
            }
            for (const dynamics::ParameterDescriptor& parameter : provider.parameters) {
                const auto configured = std::ranges::find(declared.parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
                system.parameter_values.emplace_back(configured == declared.parameters.end() ? parameter.value : configured->value);
            }
            for (std::size_t dataset_index = 0; dataset_index < provider.datasets.size(); ++dataset_index) {
                DynamicDatasetRuntime dataset{.dataset_index = dataset_index, .descriptor = provider.datasets[dataset_index]};
                this->bind_dataset(dataset, declared);
                if (dataset.scene_binding) {
                    const std::string key = std::format("{}:{}", std::to_underlying(dataset.scene_binding->resource_kind), dataset.scene_binding->resource_id);
                    if (!scene_writers.emplace(key, system_index).second) throw std::runtime_error(std::format("Scene resource {} has more than one GPU Dataset writer", key));
                }
                system.datasets.emplace_back(std::move(dataset));
            }
        }

        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            for (std::size_t dataset_index = 0; dataset_index < system.datasets.size(); ++dataset_index) {
                this->declare_scene_output(system.datasets[dataset_index]);
                this->configure_dataset(system, dataset_index);
            }
            this->configure_telemetry(system);
            this->apply_parameters(system);
        }
        this->configuration.initialized = true;
        this->reset_simulation();
    }

    void DynamicsRuntime::bind_dataset(DynamicDatasetRuntime& dataset, const scene::DynamicSystem& system) const {
        const auto scene_binding = std::ranges::find(system.scene_bindings, dataset.descriptor.id, &scene::DynamicSceneBinding::dataset_id);
        if (scene_binding != system.scene_bindings.end()) dataset.scene_binding = *scene_binding;
        for (const scene::DynamicVisualizationView& view : system.visualizations)
            if (view.dataset_id == dataset.descriptor.id) dataset.visualizations.emplace_back(view);
        if (!dataset.scene_binding && dataset.visualizations.empty()) throw std::runtime_error(std::format("GPU Dataset '{}' is neither bound to the Render Scene nor used by a Visualization", dataset.descriptor.id));
    }

    void DynamicsRuntime::declare_scene_output(const DynamicDatasetRuntime& dataset) {
        if (!dataset.scene_binding) return;
        const scene::DynamicSceneBinding& binding = *dataset.scene_binding;
        if (binding.resource_kind == scene::DynamicSceneResourceKind::Geometry) this->outputs.mesh_bindings.emplace_back(scene::GeometryId{binding.resource_id}, dataset.descriptor.mesh_update_mode, static_cast<std::uint32_t>(dataset.descriptor.capacity), static_cast<std::uint32_t>(dataset.descriptor.secondary_capacity));
        if (binding.resource_kind == scene::DynamicSceneResourceKind::SphereSet) this->outputs.sphere_set_bindings.emplace_back(scene::SphereSetId{binding.resource_id}, static_cast<std::uint32_t>(dataset.descriptor.capacity));
    }

    void DynamicsRuntime::configure_dataset(DynamicSystemRuntime& system, const std::size_t dataset_index) {
        DynamicDatasetRuntime& dataset = system.datasets[dataset_index];
        if (dataset.output_pending) {
            this->context.runtime.resources.wait_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value);
            this->context.runtime.resources.signal_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value + 1);
        }
        std::vector<std::vector<DynamicDatasetBuffer>> previous_buffer_slots = std::move(dataset.buffer_slots);
        GpuExternalTimelineSemaphore previous_timeline_semaphore            = std::move(dataset.timeline_semaphore);
        dataset.capacity                    = std::max(dataset.descriptor.capacity, dataset.requested_capacity);
        dataset.secondary_capacity          = std::max(dataset.descriptor.secondary_capacity, dataset.requested_secondary_capacity);
        dataset.requested_capacity          = 0;
        dataset.requested_secondary_capacity = 0;
        dataset.active_count                = 0;
        dataset.secondary_count             = 0;
        dataset.timeline_signal_value       = 0;
        dataset.current_slot_index          = 0;
        dataset.output_pending              = false;
        dataset.buffer_slots.clear();
        dataset.buffer_slots.resize(VulkanFrames::frames_in_flight);
        dataset.timeline_semaphore = this->context.runtime.resources.create_external_simulation_timeline();

        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index)
            for (const dynamics::DatasetBufferDescriptor& descriptor : dataset.descriptor.buffers) {
                const std::uint64_t count = dataset_element_count(dataset.descriptor, descriptor, dataset.capacity, dataset.secondary_capacity);
                DynamicDatasetBuffer buffer{.kind = static_cast<SpectraPluginDatasetBufferKind>(descriptor.kind), .channel_index = descriptor.channel_index, .byte_size = count * dataset_element_size(dataset.descriptor, descriptor)};
                buffer.gpu_buffer = this->context.runtime.resources.create_external_buffer(buffer.byte_size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                buffer.descriptor = this->context.runtime.resources.allocate_resource_descriptor();
                this->context.runtime.resources.write_buffer_descriptor(buffer.descriptor, vk::DescriptorType::eStorageBuffer, buffer.gpu_buffer);
                dataset.buffer_slots[slot_index].emplace_back(std::move(buffer));
            }

        std::vector<std::vector<SpectraPluginGpuBuffer>> plugin_slot_buffers(VulkanFrames::frames_in_flight);
        std::vector<std::vector<ExternalHandle>> exported_handles(VulkanFrames::frames_in_flight);
        std::vector<SpectraPluginGpuSlot> plugin_slots{};
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            for (DynamicDatasetBuffer& buffer : dataset.buffer_slots[slot_index]) {
                exported_handles[slot_index].emplace_back(this->context.runtime.resources.export_buffer_memory_handle(buffer.gpu_buffer));
                plugin_slot_buffers[slot_index].emplace_back(buffer.kind, buffer.channel_index, plugin_external_handle(exported_handles[slot_index].back()), buffer.byte_size);
            }
            plugin_slots.emplace_back(slot_index, plugin_slot_buffers[slot_index].data(), plugin_slot_buffers[slot_index].size());
        }
        ExternalHandle timeline_handle = this->context.runtime.resources.export_timeline_semaphore_handle(dataset.timeline_semaphore);
        const GpuDeviceIdentity& gpu_identity = this->context.runtime.graphics.identity;
        SpectraPluginDatasetConfiguration configuration{dataset.dataset_index, plugin_slots.data(), plugin_slots.size(), plugin_external_handle(timeline_handle), {}, {}, gpu_identity.node_mask};
        std::ranges::copy(gpu_identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(gpu_identity.luid, configuration.vulkan_device_luid);
        system.plugin_api->configure_dataset(system.provider_instance, &configuration);
        if (!previous_buffer_slots.empty()) {
            for (std::vector<DynamicDatasetBuffer>& slot : previous_buffer_slots)
                for (DynamicDatasetBuffer& buffer : slot) this->context.runtime.frames.retire_resource_descriptor(buffer.descriptor);
            this->context.runtime.frames.defer_destruction([buffer_slots = std::move(previous_buffer_slots), timeline_semaphore = std::move(previous_timeline_semaphore)]() mutable {});
        }
    }

    void DynamicsRuntime::configure_telemetry(DynamicSystemRuntime& system) {
        if (system.provider_descriptor->telemetry.empty()) {
            system.plugin_api->configure_telemetry(system.provider_instance, nullptr);
            return;
        }
        DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
        telemetry.buffer_slots.resize(VulkanFrames::frames_in_flight);
        telemetry.timeline_semaphore = this->context.runtime.resources.create_external_simulation_timeline();
        const std::uint64_t byte_size = system.provider_descriptor->telemetry.size() * sizeof(SpectraPluginTelemetryGpuValue);
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            DynamicDatasetBuffer buffer{.kind = SpectraPluginDatasetBufferKind::TelemetryValue, .byte_size = byte_size};
            buffer.gpu_buffer = this->context.runtime.resources.create_external_buffer(byte_size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eStorageBuffer);
            buffer.descriptor = this->context.runtime.resources.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(buffer.descriptor, vk::DescriptorType::eStorageBuffer, buffer.gpu_buffer);
            telemetry.buffer_slots[slot_index].emplace_back(std::move(buffer));
            telemetry.readback_slots[slot_index].buffer = this->context.runtime.resources.create_buffer(byte_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        }
        telemetry.immediate_readback = this->context.runtime.resources.create_buffer(byte_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);

        std::vector<std::vector<SpectraPluginGpuBuffer>> plugin_slot_buffers(VulkanFrames::frames_in_flight);
        std::vector<std::vector<ExternalHandle>> exported_handles(VulkanFrames::frames_in_flight);
        std::vector<SpectraPluginGpuSlot> plugin_slots{};
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            DynamicDatasetBuffer& buffer = telemetry.buffer_slots[slot_index].front();
            exported_handles[slot_index].emplace_back(this->context.runtime.resources.export_buffer_memory_handle(buffer.gpu_buffer));
            plugin_slot_buffers[slot_index].emplace_back(buffer.kind, 0, plugin_external_handle(exported_handles[slot_index].back()), buffer.byte_size);
            plugin_slots.emplace_back(slot_index, plugin_slot_buffers[slot_index].data(), plugin_slot_buffers[slot_index].size());
        }
        ExternalHandle timeline_handle = this->context.runtime.resources.export_timeline_semaphore_handle(telemetry.timeline_semaphore);
        const GpuDeviceIdentity& gpu_identity = this->context.runtime.graphics.identity;
        SpectraPluginTelemetryConfiguration configuration{plugin_slots.data(), plugin_slots.size(), plugin_external_handle(timeline_handle), {}, {}, gpu_identity.node_mask};
        std::ranges::copy(gpu_identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(gpu_identity.luid, configuration.vulkan_device_luid);
        system.plugin_api->configure_telemetry(system.provider_instance, &configuration);
    }

    DynamicsRuntime::DynamicDatasetRuntime& DynamicsRuntime::dataset_runtime(DynamicSystemRuntime& system, const std::uint64_t dataset_index) {
        if (dataset_index >= system.datasets.size()) throw std::runtime_error("Provider published an unknown GPU Dataset");
        return system.datasets[dataset_index];
    }

    void DynamicsRuntime::consume_telemetry(DynamicSystemRuntime& system, const SpectraPluginTelemetryGpuValue* values, const std::uint64_t simulation_step, const double simulation_seconds, std::string phase, std::string headline, std::string message) {
        system.telemetry.phase    = std::move(phase);
        system.telemetry.headline = std::move(headline);
        system.telemetry.message  = std::move(message);
        dynamics::TelemetrySample sample{.simulation_step = simulation_step, .simulation_seconds = simulation_seconds};
        for (std::size_t index = 0; index < system.provider_descriptor->telemetry.size(); ++index) {
            dynamics::TelemetryValue value{system.provider_descriptor->telemetry[index].kind, values[index].integer, {values[index].floating[0], values[index].floating[1], values[index].floating[2]}};
            system.telemetry.values[index] = value;
            sample.values.emplace_back(value);
        }
        system.telemetry.history.emplace_back(std::move(sample));
        if (system.telemetry.history.size() > 4096) system.telemetry.history.pop_front();
    }

    void DynamicsRuntime::flush_telemetry(DynamicSystemRuntime& system) {
        DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
        if (!telemetry.output_pending) return;
        const DynamicDatasetBuffer& source = telemetry.buffer_slots[telemetry.current_slot_index].front();
        this->context.runtime.resources.submit_external_immediate(telemetry.timeline_semaphore, telemetry.timeline_signal_value, telemetry.timeline_signal_value + 1, [&](const vk::raii::CommandBuffer& command_buffer) { command_buffer.copyBuffer(*source.gpu_buffer.buffer, *telemetry.immediate_readback.buffer, vk::BufferCopy{0, 0, source.byte_size}); });
        std::vector<TelemetryReadbackSlot*> completed{};
        for (TelemetryReadbackSlot& readback : telemetry.readback_slots)
            if (readback.pending) completed.emplace_back(&readback);
        std::ranges::sort(completed, {}, &TelemetryReadbackSlot::sequence);
        for (TelemetryReadbackSlot* readback : completed) {
            this->consume_telemetry(system, static_cast<const SpectraPluginTelemetryGpuValue*>(readback->buffer.mapped), readback->simulation_step, readback->simulation_seconds, std::move(readback->phase), std::move(readback->headline), std::move(readback->message));
            readback->pending = false;
        }
        this->consume_telemetry(system, static_cast<const SpectraPluginTelemetryGpuValue*>(telemetry.immediate_readback.mapped), telemetry.simulation_step, telemetry.simulation_seconds, std::move(telemetry.phase), std::move(telemetry.headline), std::move(telemetry.message));
        telemetry.output_pending = false;
    }

    void DynamicsRuntime::apply_parameters(DynamicSystemRuntime& system) {
        std::vector<SpectraPluginParameterValue> encoded{};
        for (const scene::DynamicParameterValue& value : system.parameter_values) encoded.emplace_back(plugin_parameter_value(value));
        system.plugin_api->apply_parameters(system.provider_instance, encoded.data(), encoded.size());
    }

    void DynamicsRuntime::commit_dataset(DynamicSystemRuntime&, DynamicDatasetRuntime& dataset, const SpectraPluginDatasetCommit& commit, dynamics::DynamicFrame& frame) {
        if (commit.slot_index >= dataset.buffer_slots.size()) throw std::runtime_error("Provider committed an invalid GPU Dataset slot");
        if (commit.active_count > dataset.capacity || commit.secondary_count > dataset.secondary_capacity) throw std::runtime_error("Provider committed more GPU Dataset elements than configured");
        dataset.current_slot_index      = commit.slot_index;
        dataset.active_count            = commit.active_count;
        dataset.secondary_count         = commit.secondary_count;
        dataset.timeline_signal_value   = commit.signal_value;
        dataset.dirty_region.reset();
        if (dataset.descriptor.kind == dynamics::DatasetKind::Field) dataset.dirty_region = scene::VolumeRegion{{commit.region_minimum[0], commit.region_minimum[1], commit.region_minimum[2]}, {commit.region_maximum[0], commit.region_maximum[1], commit.region_maximum[2]}};
        dataset.output_pending = true;
        if (!dataset.scene_binding) return;

        std::variant<scene::GeometryId, scene::SphereSetId, scene::VolumeId> resource_id{};
        const scene::DynamicSceneBinding& binding = *dataset.scene_binding;
        if (binding.resource_kind == scene::DynamicSceneResourceKind::Geometry)
            resource_id = scene::GeometryId{binding.resource_id};
        else if (binding.resource_kind == scene::DynamicSceneResourceKind::SphereSet)
            resource_id = scene::SphereSetId{binding.resource_id};
        else
            resource_id = scene::VolumeId{binding.resource_id};
        std::vector<dynamics::GpuDatasetBufferView> buffers{};
        for (const DynamicDatasetBuffer& buffer : dataset.buffer_slots[dataset.current_slot_index]) buffers.emplace_back(static_cast<dynamics::DatasetBufferKind>(buffer.kind), buffer.channel_index, &buffer.gpu_buffer, buffer.descriptor);
        frame.scene_updates.emplace_back(dataset.descriptor.kind, resource_id, std::move(buffers), dataset.active_count, dataset.secondary_count, dataset.descriptor.resolution, dataset.descriptor.field_channels, dataset.dirty_region, dataset.descriptor.mesh_update_mode);
    }

    void DynamicsRuntime::publish_frame(const std::uint64_t simulation_step) {
        if (this->publication.frame_pending) {
            for (DynamicSystemRuntime& system : this->systems.runtimes)
                for (DynamicDatasetRuntime& dataset : system.datasets)
                    if (dataset.output_pending) {
                        this->context.runtime.resources.wait_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value);
                        this->context.runtime.resources.signal_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value + 1);
                        dataset.output_pending = false;
                    }
            this->publication.frame_pending = false;
        }
        for (DynamicSystemRuntime& system : this->systems.runtimes) this->flush_telemetry(system);
        dynamics::DynamicFrame next{.simulation_step = simulation_step, .simulation_seconds = static_cast<double>(simulation_step) * this->configuration.setup.clock.step_seconds};
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            const std::size_t scene_update_begin = next.scene_updates.size();
            for (std::size_t dataset_index = 0; dataset_index < system.datasets.size(); ++dataset_index) {
                DynamicDatasetRuntime& dataset = system.datasets[dataset_index];
                if (dataset.requested_capacity > dataset.capacity || dataset.requested_secondary_capacity > dataset.secondary_capacity) this->configure_dataset(system, dataset_index);
            }
            this->publication.publishing_system = &system;
            this->publication.publishing_frame  = &next;
            const SpectraPluginFrameSink sink{this, &DynamicsRuntime::collect_dataset, &DynamicsRuntime::collect_capacity, &DynamicsRuntime::collect_telemetry};
            system.plugin_api->publish_frame(system.provider_instance, simulation_step, &sink);
            bool reconfigure{};
            for (std::size_t dataset_index = 0; dataset_index < system.datasets.size(); ++dataset_index) {
                DynamicDatasetRuntime& dataset = system.datasets[dataset_index];
                if (dataset.requested_capacity > dataset.capacity || dataset.requested_secondary_capacity > dataset.secondary_capacity) {
                    this->configure_dataset(system, dataset_index);
                    reconfigure = true;
                }
            }
            if (reconfigure) {
                next.scene_updates.resize(scene_update_begin);
                this->flush_telemetry(system);
                system.plugin_api->publish_frame(system.provider_instance, simulation_step, &sink);
            }
        }
        this->publication.publishing_system = nullptr;
        this->publication.publishing_frame  = nullptr;
        this->publication.frame             = std::move(next);
        this->publication.frame_pending     = true;
    }

    void DynamicsRuntime::step_to(const std::uint64_t target_step) {
        if (target_step <= this->clock.simulation_step) return;
        const std::uint64_t step_count = target_step - this->clock.simulation_step;
        for (DynamicSystemRuntime& system : this->systems.runtimes) system.plugin_api->step(system.provider_instance, this->configuration.setup.clock.step_seconds, step_count);
        this->clock.simulation_step = target_step;
    }

    void DynamicsRuntime::reset_systems() {
        for (DynamicSystemRuntime& system : this->systems.runtimes) system.plugin_api->reset(system.provider_instance, this->configuration.setup.seed);
        this->clock.simulation_step = 0;
        this->step_to(this->configuration.setup.clock.start_step);
    }

    void DynamicsRuntime::evaluate_frame(const std::uint64_t target_step) {
        this->step_to(target_step);
        this->publish_frame(target_step);
    }

    void DynamicsRuntime::reset_simulation() {
        this->clock.accumulator = {};
        this->reset_systems();
        this->publish_frame(this->clock.simulation_step);
    }

    void DynamicsRuntime::advance(const std::chrono::duration<double> elapsed) {
        if (this->systems.runtimes.empty()) return;
        bool presentation_changed{};
        for (DynamicSystemRuntime& system : this->systems.runtimes) presentation_changed = system.plugin_api->tick_presentation(system.provider_instance, elapsed.count()) || presentation_changed;
        if (this->configuration.setup.clock.end_step && *this->configuration.setup.clock.end_step == this->configuration.setup.clock.start_step) {
            this->clock.playing     = false;
            this->clock.accumulator = {};
            if (presentation_changed) this->publish_frame(this->clock.simulation_step);
            return;
        }
        if (!this->clock.playing) {
            if (presentation_changed) this->publish_frame(this->clock.simulation_step);
            return;
        }
        this->clock.accumulator += elapsed;
        const std::chrono::duration<double> step_duration{this->configuration.setup.clock.step_seconds};
        std::uint64_t remaining_steps = static_cast<std::uint64_t>(this->clock.accumulator / step_duration);
        if (remaining_steps == 0) {
            if (presentation_changed) this->publish_frame(this->clock.simulation_step);
            return;
        }
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

    const dynamics::DynamicFrame* DynamicsRuntime::pending_frame() noexcept {
        if (!this->publication.frame_pending) return nullptr;
        this->publication.visualizations.clear();
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicDatasetRuntime& dataset : system.datasets) {
                if (dataset.output_pending) this->context.runtime.frames.enqueue_external_wait(dataset.timeline_semaphore, dataset.timeline_signal_value, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader);
                if (!this->configuration.setup.systems[system.scene_system_index].visible || dataset.active_count == 0) continue;
                std::vector<dynamics::GpuDatasetBufferView> buffers{};
                for (const DynamicDatasetBuffer& buffer : dataset.buffer_slots[dataset.current_slot_index]) buffers.emplace_back(static_cast<dynamics::DatasetBufferKind>(buffer.kind), buffer.channel_index, &buffer.gpu_buffer, buffer.descriptor);
                for (const scene::DynamicVisualizationView& view : dataset.visualizations) {
                    math::Transform transform{};
                    if (view.anchor.value != 0) transform = std::ranges::find(this->configuration.source_scene->resources.instances, view.anchor, &scene::Instance::id)->transform;
                    this->publication.visualizations.emplace_back(dataset.descriptor.kind, view, buffers, dataset.active_count, dataset.secondary_count, dataset.descriptor.resolution, dataset.descriptor.image_extent, dataset.descriptor.image_format, dataset.descriptor.color_space, dataset.descriptor.field_channels, transform);
                }
            }
        return &this->publication.frame;
    }

    void DynamicsRuntime::consume_frame() noexcept {
        if (!this->publication.frame_pending) return;
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicDatasetRuntime& dataset : system.datasets)
                if (dataset.output_pending) {
                    this->context.runtime.frames.enqueue_external_signal(dataset.timeline_semaphore, dataset.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
                    dataset.output_pending = false;
                }
        this->publication.frame_pending = false;
    }

    void DynamicsRuntime::resolve_telemetry(const std::uint32_t frame_slot_index) {
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            TelemetryReadbackSlot& readback = system.telemetry_gpu.readback_slots[frame_slot_index];
            if (!readback.pending) continue;
            const auto* values = static_cast<const SpectraPluginTelemetryGpuValue*>(readback.buffer.mapped);
            this->consume_telemetry(system, values, readback.simulation_step, readback.simulation_seconds, std::move(readback.phase), std::move(readback.headline), std::move(readback.message));
            readback.pending = false;
        }
    }

    void DynamicsRuntime::record_telemetry(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
            if (!telemetry.output_pending) continue;
            this->context.runtime.frames.enqueue_external_wait(telemetry.timeline_semaphore, telemetry.timeline_signal_value, vk::PipelineStageFlagBits2::eCopy);
            const DynamicDatasetBuffer& source = telemetry.buffer_slots[telemetry.current_slot_index].front();
            TelemetryReadbackSlot& destination = telemetry.readback_slots[frame_slot_index];
            command_buffer.copyBuffer(*source.gpu_buffer.buffer, *destination.buffer.buffer, vk::BufferCopy{0, 0, source.byte_size});
            destination.simulation_step    = telemetry.simulation_step;
            destination.sequence           = telemetry.sequence;
            destination.simulation_seconds = telemetry.simulation_seconds;
            destination.phase              = telemetry.phase;
            destination.headline           = telemetry.headline;
            destination.message            = telemetry.message;
            destination.pending            = true;
            this->context.runtime.frames.enqueue_external_signal(telemetry.timeline_semaphore, telemetry.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
            telemetry.output_pending = false;
        }
    }

    bool DynamicsRuntime::controls(const scene::InstanceId instance_id) const noexcept {
        for (const DynamicSystemRuntime& system : this->systems.runtimes)
            for (const DynamicDatasetRuntime& dataset : system.datasets) {
                if (!dataset.scene_binding) continue;
                const scene::DynamicSceneBinding& binding = *dataset.scene_binding;
                const auto instance = std::ranges::find(this->configuration.source_scene->resources.instances, instance_id, &scene::Instance::id);
                if (instance == this->configuration.source_scene->resources.instances.end()) continue;
                const scene::Prototype& prototype = *std::ranges::find(this->configuration.source_scene->resources.prototypes, instance->prototype, &scene::Prototype::id);
                if (std::ranges::any_of(prototype.primitives, [&binding](const scene::Primitive& primitive) {
                        return (binding.resource_kind == scene::DynamicSceneResourceKind::Geometry && binding.resource_id == primitive.geometry.value) || (binding.resource_kind == scene::DynamicSceneResourceKind::SphereSet && binding.resource_id == primitive.spheres.value) || (binding.resource_kind == scene::DynamicSceneResourceKind::Volume && binding.resource_id == primitive.volume.value);
                    }))
                    return true;
            }
        return false;
    }

    void DynamicsRuntime::advance_one_step() {
        this->clock.accumulator = {};
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step >= *this->configuration.setup.clock.end_step) {
            if (!this->configuration.setup.clock.loop) return;
            this->reset_systems();
        }
        const std::uint64_t requested = this->clock.simulation_step + 1;
        this->evaluate_frame(this->configuration.setup.clock.end_step ? std::min(requested, *this->configuration.setup.clock.end_step) : requested);
    }

    void DynamicsRuntime::apply_parameter_changes(const std::size_t system_index, const std::span<const scene::DynamicParameterSetting> parameters, const bool reset) {
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
        if (reset)
            this->reset_simulation();
        else
            this->publish_frame(this->clock.simulation_step);
    }

    void DynamicsRuntime::destroy() noexcept {
        if (!this->configuration.initialized && this->providers.libraries.empty()) return;
        for (DynamicSystemRuntime& system : this->systems.runtimes) system.plugin_api->destroy_provider(system.provider_instance);
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            for (DynamicDatasetRuntime& dataset : system.datasets)
                for (std::vector<DynamicDatasetBuffer>& slot : dataset.buffer_slots)
                    for (DynamicDatasetBuffer& buffer : slot) this->context.runtime.frames.retire_resource_descriptor(buffer.descriptor);
            for (std::vector<DynamicDatasetBuffer>& slot : system.telemetry_gpu.buffer_slots)
                for (DynamicDatasetBuffer& buffer : slot) this->context.runtime.frames.retire_resource_descriptor(buffer.descriptor);
        }
        this->systems.runtimes.clear();
        this->providers.by_id.clear();
        this->providers.libraries.clear();
        this->providers.descriptors.clear();
        this->publication = {};
        this->outputs      = {};
        this->clock        = {};
        this->configuration = {};
    }

    bool DynamicsRuntime::running() const noexcept {
        return this->clock.playing;
    }

    dynamics::SimulationTimeline DynamicsRuntime::timeline() const noexcept {
        return {this->clock.simulation_step, this->publication.frame.simulation_seconds};
    }

    void DynamicsRuntime::start() {
        this->clock.playing = true;
    }

    void DynamicsRuntime::pause() {
        this->clock.playing = false;
    }

    void DynamicsRuntime::step() {
        this->advance_one_step();
    }

    void DynamicsRuntime::reset() {
        this->clock.playing = false;
        this->reset_simulation();
    }
} // namespace spectra
