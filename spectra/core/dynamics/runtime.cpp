module;

#if defined(_WIN32)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

#include "../../../sdk/internal/abi.h"

#undef interface

module spectra.dynamics.runtime;

import std;
import vulkan;

namespace spectra {
    namespace {
        [[nodiscard]] std::string sdk_string(const SpectraSdkString value) {
            return std::string{value.data, value.size};
        }

        void check_sdk_result(const SpectraSdkResult result, const std::string_view operation) {
            if (result.error.size != 0u) throw std::runtime_error(std::format("Provider {} failed: {}", operation, sdk_string(result.error)));
        }

        [[nodiscard]] std::string csv_field(const std::string_view source) {
            if (source.find_first_of(",\"\r\n") == std::string_view::npos) return std::string{source};
            std::string result{"\""};
            for (const char character : source) {
                if (character == '\"') result += '\"';
                result += character;
            }
            result += '\"';
            return result;
        }

        [[nodiscard]] scene::DynamicParameterValue scene_value(const SpectraSdkValue& value) noexcept {
            return {static_cast<scene::DynamicParameterKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraSdkValue sdk_value(const scene::DynamicParameterValue& value) noexcept {
            return {static_cast<SpectraSdkValueKind>(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraSdkExternalHandle sdk_handle(const ExternalHandle& value) noexcept {
            return {value.type == ExternalHandleType::OpaqueWin32 ? SpectraSdkExternalHandleType::OpaqueWin32 : SpectraSdkExternalHandleType::OpaqueFileDescriptor, value.value};
        }

        [[nodiscard]] std::uint64_t element_count(const math::UInt3 extent) noexcept {
            return static_cast<std::uint64_t>(extent.x) * extent.y * extent.z;
        }

        [[nodiscard]] std::uint32_t collection_element_size(const SpectraSdkOutputKind kind) noexcept {
            if (kind == SpectraSdkOutputKind::Spheres) return 16u;
            if (kind == SpectraSdkOutputKind::Instances) return 72u;
            if (kind == SpectraSdkOutputKind::Points) return 36u;
            if (kind == SpectraSdkOutputKind::Lines) return 48u;
            if (kind == SpectraSdkOutputKind::Vectors) return 48u;
            if (kind == SpectraSdkOutputKind::Image) return 16u;
            return 32u;
        }

        struct OutputExportLifetime {
            std::vector<SpectraSdkGpuBuffer> fixed{};
            std::vector<std::vector<SpectraSdkGpuBuffer>> buffers{};
            std::vector<SpectraSdkGpuSlot> slots{};
            std::vector<ExternalHandle> fixed_handles{};
            std::vector<std::vector<ExternalHandle>> handles{};
        };
    }

    DynamicsRuntime::DynamicsRuntime(VulkanRuntime& runtime) noexcept : context{runtime} {}

    DynamicsRuntime::~DynamicsRuntime() {
        this->destroy();
    }

    void DynamicsRuntime::initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene, scene::Scene& evaluated_scene) {
        this->destroy();
        try {
            this->configuration.source_scene    = &source_scene;
            this->configuration.evaluated_scene = &evaluated_scene;
            this->configuration.assets          = scene_path.parent_path();
            this->configuration.setup           = *source_scene.dynamic_setup;
            this->configuration.initialized     = true;

            std::vector<std::string> required_providers{};
            for (const scene::DynamicSystem& system : this->configuration.setup.systems)
                if (!std::ranges::contains(required_providers, system.provider_id)) required_providers.emplace_back(system.provider_id);
            std::ranges::sort(required_providers);

            std::vector<std::filesystem::path> provider_paths{};
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{this->configuration.assets}) {
#if defined(_WIN32)
                if (entry.is_regular_file() && entry.path().filename().string().ends_with(".spectra-provider.dll")) provider_paths.emplace_back(entry.path());
#else
                if (entry.is_regular_file() && entry.path().filename().string().ends_with(".spectra-provider.so")) provider_paths.emplace_back(entry.path());
#endif
            }
            std::ranges::sort(provider_paths);
            for (const std::filesystem::path& path : provider_paths) {
                ProviderLibrary& library = this->providers.libraries.emplace_back(path);
                const SpectraSdkProviderDescriptor& source = library.descriptor;
                dynamics::ProviderDescriptor provider{.id = sdk_string(source.id)};
                if (!std::ranges::contains(required_providers, provider.id)) {
                    this->providers.libraries.pop_back();
                    continue;
                }
                provider.parameters.reserve(source.parameter_count);
                for (std::uint64_t index = 0; index != source.parameter_count; ++index) {
                    const SpectraSdkParameterDescriptor& parameter = source.parameters[index];
                    dynamics::ParameterDescriptor value{
                        .id               = sdk_string(parameter.id),
                        .name             = sdk_string(parameter.name),
                        .unit             = sdk_string(parameter.unit),
                        .section_id       = sdk_string(parameter.section),
                        .description      = sdk_string(parameter.description),
                        .application_mode = static_cast<dynamics::ParameterApplication>(parameter.application),
                        .value            = scene_value(parameter.default_value),
                        .minimum          = scene_value(parameter.minimum),
                        .maximum          = scene_value(parameter.maximum),
                        .step             = scene_value(parameter.step),
                    };
                    for (std::uint64_t enumerator = 0; enumerator != parameter.enumerator_count; ++enumerator) value.enumerators.emplace_back(sdk_string(parameter.enumerators[enumerator]));
                    provider.parameters.emplace_back(std::move(value));
                }
                provider.datasets.reserve(source.output_count);
                for (std::uint64_t index = 0; index != source.output_count; ++index) {
                    const SpectraSdkOutputDescriptor& output = source.outputs[index];
                    dynamics::DatasetDescriptor dataset{.id = sdk_string(output.id)};
                    if (output.kind == SpectraSdkOutputKind::Mesh) dataset.resource_kind = scene::DynamicSceneResourceKind::Geometry, dataset.details = dynamics::TriangleMeshDataset{0u, 0u, dynamics::MeshUpdateMode::Deformable, output.mesh_attributes};
                    else if (output.kind == SpectraSdkOutputKind::Spheres) dataset.resource_kind = scene::DynamicSceneResourceKind::SphereSet, dataset.details = dynamics::SphereSetDataset{};
                    else if (output.kind == SpectraSdkOutputKind::Volume) {
                        dynamics::FieldDataset field{};
                        std::uint32_t buffer_offset{};
                        for (std::uint64_t field_index = 0; field_index != output.volume_field_count; ++field_index) {
                            const SpectraSdkVolumeFieldDescriptor& source_field = output.volume_fields[field_index];
                            const std::uint32_t buffer_count = source_field.kind == SpectraSdkVolumeFieldKind::MacFloat3 ? 3u : 1u;
                            field.fields.emplace_back(
                                sdk_string(source_field.id),
                                sdk_string(source_field.name),
                                sdk_string(source_field.unit),
                                static_cast<scene::VolumeFieldKind>(source_field.kind),
                                static_cast<scene::VolumeFieldSampling>(source_field.sampling),
                                static_cast<scene::VolumeVectorSpace>(source_field.vector_space),
                                buffer_offset,
                                buffer_count
                            );
                            buffer_offset += buffer_count;
                        }
                        dataset.resource_kind = scene::DynamicSceneResourceKind::Volume;
                        dataset.details       = std::move(field);
                    } else if (output.kind == SpectraSdkOutputKind::Instances) dataset.details = dynamics::InstanceTransformDataset{};
                    else if (output.kind == SpectraSdkOutputKind::Points) dataset.details = dynamics::PointDataset{};
                    else if (output.kind == SpectraSdkOutputKind::Lines) dataset.details = dynamics::SegmentDataset{};
                    else if (output.kind == SpectraSdkOutputKind::Vectors) dataset.details = dynamics::VectorDataset{};
                    else dataset.details = dynamics::ImageDataset{};
                    provider.datasets.emplace_back(std::move(dataset));
                }
                provider.telemetry.reserve(source.metric_count);
                for (std::uint64_t index = 0; index != source.metric_count; ++index) {
                    const SpectraSdkMetricDescriptor& metric = source.metrics[index];
                    provider.telemetry.emplace_back(sdk_string(metric.id), sdk_string(metric.name), sdk_string(metric.unit), sdk_string(metric.section), static_cast<dynamics::TelemetryKind>(metric.kind), metric.plot != 0u);
                }
                library.provider = std::move(provider);
                if (this->providers.by_id.contains(library.provider.id)) throw std::runtime_error(std::format("Multiple Provider DLLs declare '{}'", library.provider.id));
                this->providers.by_id.emplace(library.provider.id, &library);
            }
            for (const std::string& id : required_providers)
                if (!this->providers.by_id.contains(id)) throw std::runtime_error(std::format("Scene requires Provider '{}' but no matching .spectra-provider DLL was found in {}", id, this->configuration.assets.string()));

            for (std::size_t system_index = 0; system_index != this->configuration.setup.systems.size(); ++system_index) {
                const scene::DynamicSystem& declared = this->configuration.setup.systems[system_index];
                if (!declared.enabled) continue;
                ProviderLibrary& library = this->provider_library(declared.provider_id);
                SystemRuntime& system       = this->systems.values.emplace_back();
                system.scene_system_index   = system_index;
                system.provider_descriptor = &library.provider;
                system.api                 = library.api;
                for (const dynamics::ParameterDescriptor& parameter : library.provider.parameters) {
                    const auto found = std::ranges::find(declared.parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
                    system.parameter_values.emplace_back(found == declared.parameters.end() ? parameter.value : found->value);
                }
                this->create_system(system, declared);
                for (const OutputRuntime& output : system.outputs) this->declare_scene_output(output);
            }
            this->reset_simulation();
        } catch (...) {
            this->destroy();
            throw;
        }
    }

    void DynamicsRuntime::destroy() noexcept {
        if (!this->configuration.initialized && this->providers.libraries.empty()) return;
        try {
            if (static_cast<vk::Result>(this->context.runtime.graphics.device.getDispatcher()->vkDeviceWaitIdle(*this->context.runtime.graphics.device)) != vk::Result::eSuccess) std::terminate();
            this->discard_pending_snapshot();
            for (SystemRuntime& system : this->systems.values)
                if (system.provider_instance && system.api->destroy_provider(system.provider_instance).error.size != 0u) std::terminate();
        } catch (...) {
            std::terminate();
        }
        this->systems.values.clear();
        this->providers.by_id.clear();
        this->providers.libraries.clear();
        this->publication   = {};
        this->outputs       = {};
        this->clock         = {};
        this->configuration = {};
    }

    bool DynamicsRuntime::initialized() const noexcept { return this->configuration.initialized; }

    const dynamics::ProviderDescriptor& DynamicsRuntime::provider_descriptor(const std::string_view id) const { return this->provider_library(id).provider; }

    const dynamics::TelemetrySnapshot& DynamicsRuntime::telemetry(const std::size_t system_index) const { return std::ranges::find(this->systems.values, system_index, &SystemRuntime::scene_system_index)->telemetry; }

    void DynamicsRuntime::write_telemetry(const std::filesystem::path& path) const {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path};
        stream << "system,step,seconds,metric,value_x,value_y,value_z\n";
        for (const SystemRuntime& system : this->systems.values)
            for (const dynamics::TelemetrySample& sample : system.telemetry.history)
                for (std::size_t index = 0; index != sample.values.size(); ++index) {
                    const dynamics::TelemetryValue& value = sample.values[index];
                    stream << csv_field(system.provider_descriptor->id) << ',' << sample.simulation_step << ',' << sample.simulation_seconds << ',' << csv_field(system.provider_descriptor->telemetry[index].id) << ',' << (value.kind == dynamics::TelemetryKind::Boolean || value.kind == dynamics::TelemetryKind::Integer ? static_cast<double>(value.integer) : value.floating[0]) << ',' << value.floating[1] << ',' << value.floating[2] << '\n';
                }
    }

    std::span<const dynamics::MeshOutputBinding> DynamicsRuntime::mesh_bindings() const noexcept { return this->outputs.mesh_bindings; }
    std::span<const dynamics::SphereSetOutputBinding> DynamicsRuntime::sphere_set_bindings() const noexcept { return this->outputs.sphere_set_bindings; }
    std::span<const dynamics::GpuVisualization> DynamicsRuntime::visualizations() const noexcept { return this->publication.snapshot.visualizations; }

    bool DynamicsRuntime::controls(const scene::InstanceId instance_id) const noexcept {
        const auto instance = std::ranges::find(this->configuration.source_scene->resources.instances, instance_id, &scene::Instance::id);
        if (instance == this->configuration.source_scene->resources.instances.end()) return false;
        const scene::Prototype& prototype = *std::ranges::find(this->configuration.source_scene->resources.prototypes, instance->prototype, &scene::Prototype::id);
        return std::ranges::any_of(this->systems.values, [&prototype](const SystemRuntime& system) {
            return std::ranges::any_of(system.outputs, [&prototype](const OutputRuntime& output) {
                if (!output.scene_binding) return false;
                return std::ranges::any_of(prototype.primitives, [&output](const scene::Primitive& primitive) {
                    return (output.descriptor.resource_kind == scene::DynamicSceneResourceKind::Geometry && output.scene_binding->resource_id == primitive.geometry.value) || (output.descriptor.resource_kind == scene::DynamicSceneResourceKind::SphereSet && output.scene_binding->resource_id == primitive.spheres.value);
                });
            });
        });
    }

    bool DynamicsRuntime::controls(const scene::VolumeId volume_id) const noexcept {
        return std::ranges::any_of(this->systems.values, [volume_id](const SystemRuntime& system) { return std::ranges::any_of(system.outputs, [volume_id](const OutputRuntime& output) { return output.scene_binding && output.descriptor.resource_kind == scene::DynamicSceneResourceKind::Volume && output.scene_binding->resource_id == volume_id.value; }); });
    }

    bool DynamicsRuntime::running() const noexcept { return this->clock.playing; }
    bool DynamicsRuntime::faulted() const noexcept { return this->configuration.faulted; }
    dynamics::SimulationTimeline DynamicsRuntime::timeline() const noexcept { return {this->clock.simulation_step, this->clock.simulation_step * this->configuration.setup.clock.step_seconds}; }
    void DynamicsRuntime::start() { this->clock.playing = true; }
    void DynamicsRuntime::pause() { this->clock.playing = false; }
    void DynamicsRuntime::step() { this->pause(); this->advance_one_step(); }

    void DynamicsRuntime::advance() {
        if (this->configuration.faulted || this->systems.values.empty() || !this->clock.playing || this->publication.snapshot_pending) return;
        try { this->advance_one_step(); } catch (...) { this->configuration.faulted = true; this->clock.playing = false; throw; }
    }

    void DynamicsRuntime::evaluate(const std::uint64_t simulation_step) { this->pause(); this->evaluate_frame(simulation_step); }
    void DynamicsRuntime::evaluate_time(const double seconds) { this->evaluate(static_cast<std::uint64_t>(std::floor(seconds / this->configuration.setup.clock.step_seconds))); }
    void DynamicsRuntime::reset() { this->pause(); this->reset_simulation(); }

    bool DynamicsRuntime::apply_parameter_changes(const std::size_t system_index, const std::span<const scene::DynamicParameterSetting> parameters, const bool should_reset) {
        SystemRuntime& system = *std::ranges::find(this->systems.values, system_index, &SystemRuntime::scene_system_index);
        std::vector<scene::DynamicParameterValue> values{};
        for (const dynamics::ParameterDescriptor& descriptor : system.provider_descriptor->parameters) {
            const auto found = std::ranges::find(parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
            values.emplace_back(found == parameters.end() ? descriptor.value : found->value);
        }
        bool recreate{};
        for (std::size_t index = 0; index != values.size(); ++index)
            if (system.provider_descriptor->parameters[index].application_mode == dynamics::ParameterApplication::Recreate && values[index] != system.parameter_values[index]) recreate = true;
        if (recreate) {
            this->discard_pending_snapshot();
            if (static_cast<vk::Result>(this->context.runtime.graphics.device.getDispatcher()->vkDeviceWaitIdle(*this->context.runtime.graphics.device)) != vk::Result::eSuccess) throw std::runtime_error("Vulkan device wait failed during Provider recreation");
            SystemRuntime replacement{};
            replacement.scene_system_index = system.scene_system_index;
            replacement.provider_descriptor = system.provider_descriptor;
            replacement.api                 = system.api;
            replacement.parameter_values    = std::move(values);
            scene::DynamicSystem declared   = this->configuration.setup.systems[system_index];
            declared.parameters.assign(parameters.begin(), parameters.end());
            try {
                this->create_system(replacement, declared);
                check_sdk_result(replacement.api->reset(replacement.provider_instance, this->configuration.setup.seed), "reset");
                check_sdk_result(replacement.api->step(replacement.provider_instance, this->configuration.setup.clock.step_seconds, this->clock.simulation_step), "step");
                check_sdk_result(system.api->destroy_provider(system.provider_instance), "destruction");
            } catch (...) {
                if (replacement.provider_instance) static_cast<void>(replacement.api->destroy_provider(replacement.provider_instance));
                throw;
            }
            system = std::move(replacement);
            this->configuration.setup.systems[system_index].parameters.assign(parameters.begin(), parameters.end());
            this->outputs = {};
            for (const SystemRuntime& value : this->systems.values)
                for (const OutputRuntime& output : value.outputs) this->declare_scene_output(output);
            this->publish_frame();
            return true;
        }
        this->apply_parameters(system, values);
        system.parameter_values = std::move(values);
        if (should_reset) this->reset();
        return false;
    }

    const dynamics::DynamicSnapshot* DynamicsRuntime::acquire_snapshot() {
        if (!this->publication.snapshot_pending) return nullptr;
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) this->context.runtime.frames.enqueue_external_wait(system.timeline, system.signal_value, vk::PipelineStageFlagBits2::eAllCommands);
        this->publication.snapshot_acquired = true;
        return &this->publication.snapshot;
    }

    void DynamicsRuntime::consume_snapshot() {
        if (!this->publication.snapshot_pending) return;
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) {
                if (!this->publication.snapshot_acquired) this->context.runtime.frames.enqueue_external_wait(system.timeline, system.signal_value, vk::PipelineStageFlagBits2::eAllCommands);
                this->context.runtime.frames.enqueue_external_signal(system.timeline, system.signal_value + 1u, vk::PipelineStageFlagBits2::eAllCommands);
                system.output_pending = false;
            }
        this->publication.snapshot_pending  = false;
        this->publication.snapshot_acquired = false;
    }

    void DynamicsRuntime::record_telemetry(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->publication.snapshot_pending) return;
        for (SystemRuntime& system : this->systems.values) {
            if (system.provider_descriptor->telemetry.empty()) continue;
            const OutputRuntime& metrics = system.outputs.back();
            TelemetryReadbackSlot& readback = system.telemetry_readback[frame_slot_index];
            command_buffer.copyBuffer(*metrics.slots[system.current_slot][0].gpu_buffer.buffer, *readback.buffer.buffer, vk::BufferCopy{0u, 0u, metrics.slots[system.current_slot][0].byte_size});
            readback.simulation_step    = this->clock.simulation_step;
            readback.simulation_seconds = this->timeline().seconds;
            readback.pending            = true;
        }
    }

    void DynamicsRuntime::resolve_telemetry(const std::uint32_t frame_slot_index) {
        for (SystemRuntime& system : this->systems.values) {
            TelemetryReadbackSlot& readback = system.telemetry_readback[frame_slot_index];
            if (!readback.pending) continue;
            struct MetricValue { double floating[3]; std::int64_t integer; };
            const auto* values = static_cast<const MetricValue*>(readback.buffer.mapped);
            dynamics::TelemetrySample sample{readback.simulation_step, readback.simulation_seconds};
            for (std::size_t index = 0; index != system.provider_descriptor->telemetry.size(); ++index) {
                dynamics::TelemetryValue value{system.provider_descriptor->telemetry[index].kind, values[index].integer, {values[index].floating[0], values[index].floating[1], values[index].floating[2]}};
                system.telemetry.values[index] = value;
                sample.values.emplace_back(value);
            }
            system.telemetry.history.emplace_back(std::move(sample));
            if (system.telemetry.history.size() > 4096u) system.telemetry.history.pop_front();
            readback.pending = false;
        }
    }

    DynamicsRuntime::ProviderLibrary::ProviderLibrary(const std::filesystem::path& library_path) {
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(library_path);
#if defined(_WIN32)
        const HMODULE loaded = LoadLibraryW(canonical.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load Provider: {}", canonical.string()));
#else
        void* loaded = dlopen(canonical.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!loaded) throw std::runtime_error(std::format("Linux failed to load Provider: {}", dlerror()));
#endif
        try {
#if defined(_WIN32)
            const auto entry = reinterpret_cast<const SpectraSdkApi* (*)() noexcept>(GetProcAddress(loaded, SPECTRA_SDK_ENTRY_NAME));
#else
            const auto entry = reinterpret_cast<const SpectraSdkApi* (*)() noexcept>(dlsym(loaded, SPECTRA_SDK_ENTRY_NAME));
#endif
            if (!entry) throw std::runtime_error("Provider does not export Spectra SDK ABI 1");
            api = entry();
            if (api->abi_version != SPECTRA_SDK_ABI_VERSION || api->struct_size != sizeof(SpectraSdkApi)) throw std::runtime_error("Provider has an incompatible Spectra SDK ABI");
            const SpectraSdkProviderDescriptionResult described = api->describe_provider();
            check_sdk_result(described.result, "description");
            descriptor = described.descriptor;
        } catch (...) {
#if defined(_WIN32)
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            throw;
        }
        library_handle = loaded;
    }

    DynamicsRuntime::ProviderLibrary::~ProviderLibrary() {
        if (!library_handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(library_handle));
#else
        dlclose(library_handle);
#endif
    }

    DynamicsRuntime::ProviderLibrary& DynamicsRuntime::provider_library(const std::string_view id) const { return *this->providers.by_id.find(std::string{id})->second; }

    SpectraSdkResult DynamicsRuntime::configure_output(void* source, const SpectraSdkOutputLayout* layout, SpectraSdkOutputRequest* request) noexcept {
        DynamicsRuntime& runtime = *static_cast<DynamicsRuntime*>(source);
        try {
            std::unique_ptr<OutputExportLifetime> lifetime = std::make_unique<OutputExportLifetime>();
            SystemRuntime& system = *runtime.publication.configuring_system;
            const std::size_t index = static_cast<std::size_t>(layout->output_index);
            if (index == system.outputs.size()) system.outputs.emplace_back(OutputRuntime{.descriptor = {.id = "__metrics"}});
            OutputRuntime& output       = system.outputs[index];
            output.kind                 = layout->kind;
            output.capacity             = layout->primary_capacity;
            output.secondary_capacity   = layout->secondary_capacity;
            output.resolution           = {layout->resolution[0], layout->resolution[1], layout->resolution[2]};
            if (auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&output.descriptor.details)) mesh->vertex_capacity = output.capacity, mesh->index_capacity = output.secondary_capacity * 3u, mesh->attributes = layout->mesh_attributes;
            else if (auto* spheres = std::get_if<dynamics::SphereSetDataset>(&output.descriptor.details)) spheres->capacity = output.capacity;
            else if (auto* instances = std::get_if<dynamics::InstanceTransformDataset>(&output.descriptor.details)) instances->capacity = output.capacity;
            else if (auto* points = std::get_if<dynamics::PointDataset>(&output.descriptor.details)) points->capacity = output.capacity;
            else if (auto* lines = std::get_if<dynamics::SegmentDataset>(&output.descriptor.details)) lines->capacity = output.capacity;
            else if (auto* vectors = std::get_if<dynamics::VectorDataset>(&output.descriptor.details)) vectors->capacity = output.capacity;
            else if (auto* volume = std::get_if<dynamics::FieldDataset>(&output.descriptor.details)) volume->resolution = output.resolution;
            else if (auto* image = std::get_if<dynamics::ImageDataset>(&output.descriptor.details)) image->extent = {output.resolution.x, output.resolution.y};

            std::vector<std::uint64_t> static_sizes{};
            std::vector<std::uint64_t> dynamic_sizes{};
            if (layout->kind == SpectraSdkOutputKind::Mesh) {
                if (layout->secondary_capacity != 0u) static_sizes.emplace_back(static_cast<std::uint64_t>(layout->secondary_capacity) * 3u * sizeof(std::uint32_t));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::TextureCoordinate)) != 0u) static_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 8u);
                dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 12u);
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 12u);
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 12u);
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 16u);
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Scalar)) != 0u) dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * 4u);
            } else if (layout->kind == SpectraSdkOutputKind::Volume) {
                const auto& fields = std::get<dynamics::FieldDataset>(output.descriptor.details).fields;
                for (const dynamics::VolumeFieldDescriptor& field : fields) {
                    if (field.kind == scene::VolumeFieldKind::MacFloat3) {
                        dynamic_sizes.emplace_back(static_cast<std::uint64_t>(output.resolution.x + 1u) * output.resolution.y * output.resolution.z * sizeof(float));
                        dynamic_sizes.emplace_back(static_cast<std::uint64_t>(output.resolution.x) * (output.resolution.y + 1u) * output.resolution.z * sizeof(float));
                        dynamic_sizes.emplace_back(static_cast<std::uint64_t>(output.resolution.x) * output.resolution.y * (output.resolution.z + 1u) * sizeof(float));
                    } else
                        dynamic_sizes.emplace_back(element_count(output.resolution) * (field.kind == scene::VolumeFieldKind::Float ? sizeof(float) : sizeof(math::Float3)));
                }
            } else dynamic_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * collection_element_size(layout->kind));

            output.static_buffers.resize(static_sizes.size());
            lifetime->fixed.resize(static_sizes.size());
            lifetime->fixed_handles.resize(static_sizes.size());
            for (std::size_t buffer = 0; buffer != static_sizes.size(); ++buffer) {
                OutputBuffer& destination = output.static_buffers[buffer];
                destination.byte_size     = static_sizes[buffer];
                destination.gpu_buffer    = runtime.context.runtime.resources.create_external_buffer(destination.byte_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer);
                destination.descriptor    = runtime.context.runtime.frames.allocate_resource_descriptor();
                runtime.context.runtime.resources.write_buffer_descriptor(destination.descriptor.handle(), vk::DescriptorType::eStorageBuffer, destination.gpu_buffer);
                lifetime->fixed_handles[buffer] = runtime.context.runtime.resources.export_buffer_memory_handle(destination.gpu_buffer);
                lifetime->fixed[buffer]         = {sdk_handle(lifetime->fixed_handles[buffer]), destination.byte_size};
            }
            output.slots.resize(VulkanFrames::frames_in_flight);
            lifetime->buffers.resize(VulkanFrames::frames_in_flight);
            lifetime->handles.resize(VulkanFrames::frames_in_flight);
            lifetime->slots.resize(VulkanFrames::frames_in_flight);
            for (std::uint32_t slot = 0; slot != VulkanFrames::frames_in_flight; ++slot) {
                output.slots[slot].resize(dynamic_sizes.size());
                lifetime->buffers[slot].resize(dynamic_sizes.size());
                lifetime->handles[slot].resize(dynamic_sizes.size());
                for (std::size_t buffer = 0; buffer != dynamic_sizes.size(); ++buffer) {
                    OutputBuffer& destination = output.slots[slot][buffer];
                    destination.byte_size     = dynamic_sizes[buffer];
                    destination.gpu_buffer    = runtime.context.runtime.resources.create_external_buffer(destination.byte_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);
                    destination.descriptor    = runtime.context.runtime.frames.allocate_resource_descriptor();
                    runtime.context.runtime.resources.write_buffer_descriptor(destination.descriptor.handle(), vk::DescriptorType::eStorageBuffer, destination.gpu_buffer);
                    lifetime->handles[slot][buffer] = runtime.context.runtime.resources.export_buffer_memory_handle(destination.gpu_buffer);
                    lifetime->buffers[slot][buffer] = {sdk_handle(lifetime->handles[slot][buffer]), destination.byte_size};
                }
                lifetime->slots[slot] = {lifetime->buffers[slot].data(), lifetime->buffers[slot].size()};
            }
            request->configuration = {lifetime->fixed.data(), lifetime->fixed.size(), lifetime->slots.data(), lifetime->slots.size()};
            request->lifetime      = lifetime.release();
            return {};
        } catch (const std::exception& error) {
            runtime.publication.callback_error = error.what();
            return {{runtime.publication.callback_error.data(), runtime.publication.callback_error.size()}};
        }
    }

    void DynamicsRuntime::release_output(void* source) noexcept {
        delete static_cast<OutputExportLifetime*>(source);
    }

    void DynamicsRuntime::bind_output(OutputRuntime& output, const scene::DynamicSystem& system) const {
        const auto binding = std::ranges::find(system.scene_bindings, output.descriptor.id, &scene::DynamicSceneBinding::dataset_id);
        if (binding != system.scene_bindings.end()) output.scene_binding = *binding;
        for (const scene::DynamicVisualizationView& view : system.visualizations)
            if (view.dataset_id == output.descriptor.id) output.visualizations.emplace_back(view);
    }

    void DynamicsRuntime::declare_scene_output(const OutputRuntime& output) {
        if (!output.scene_binding) return;
        if (const auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&output.descriptor.details)) this->outputs.mesh_bindings.emplace_back(scene::GeometryId{output.scene_binding->resource_id}, dynamics::MeshUpdateMode::Deformable, mesh->vertex_capacity, mesh->index_capacity);
        else if (const auto* spheres = std::get_if<dynamics::SphereSetDataset>(&output.descriptor.details)) this->outputs.sphere_set_bindings.emplace_back(scene::SphereSetId{output.scene_binding->resource_id}, spheres->capacity);
        else if (const auto* field = std::get_if<dynamics::FieldDataset>(&output.descriptor.details)) {
            scene::Volume& volume   = *std::ranges::find(this->configuration.evaluated_scene->resources.volumes, scene::VolumeId{output.scene_binding->resource_id}, &scene::Volume::id);
            scene::GridVolume& grid = std::get<scene::GridVolume>(volume.data);
            grid.resolution = output.resolution;
            grid.fields.clear();
            grid.fields.reserve(field->fields.size());
            for (const dynamics::VolumeFieldDescriptor& descriptor : field->fields) grid.fields.emplace_back(descriptor.id, descriptor.name, descriptor.unit, descriptor.kind, descriptor.sampling, descriptor.vector_space);
            ++volume.revision.content;
            ++volume.revision.topology;
            this->configuration.evaluated_scene->mark_changed(scene::SceneChange::Volume | scene::SceneChange::Structure);
        }
    }

    void DynamicsRuntime::create_system(SystemRuntime& system, const scene::DynamicSystem& declared) {
        const auto& identity = this->context.runtime.graphics.identity;
        system.telemetry.values.resize(system.provider_descriptor->telemetry.size());
        std::vector<SpectraSdkValue> parameters{};
        for (const scene::DynamicParameterValue& value : system.parameter_values) parameters.emplace_back(sdk_value(value));
        std::string assets = this->configuration.assets.string();
        SpectraSdkCreateInfo create{{assets.data(), assets.size()}, parameters.data()};
        std::ranges::copy(identity.uuid, create.vulkan_device_uuid);
        std::ranges::copy(identity.luid, create.vulkan_device_luid);
        create.vulkan_device_luid_valid = identity.luid_valid;
        create.vulkan_device_node_mask  = identity.node_mask;
        const SpectraSdkProviderCreateResult created = system.api->create_provider(&create);
        check_sdk_result(created.result, "creation");
        system.provider_instance = created.provider;
        for (const dynamics::DatasetDescriptor& descriptor : system.provider_descriptor->datasets) {
            OutputRuntime& output = system.outputs.emplace_back();
            output.descriptor     = descriptor;
            this->bind_output(output, declared);
        }
        system.timeline = this->context.runtime.resources.create_external_simulation_timeline();
        ExternalHandle timeline_handle = this->context.runtime.resources.export_timeline_semaphore_handle(system.timeline);
        SpectraSdkSetupSink sink{this, sdk_handle(timeline_handle)};
        sink.slot_count               = VulkanFrames::frames_in_flight;
        sink.configure_output         = &DynamicsRuntime::configure_output;
        sink.release_output           = &DynamicsRuntime::release_output;
        this->publication.configuring_system = &system;
        try {
            check_sdk_result(system.api->setup(system.provider_instance, &sink), "setup");
        } catch (...) {
            this->publication.configuring_system = nullptr;
            throw;
        }
        this->publication.configuring_system = nullptr;
        if (!system.provider_descriptor->telemetry.empty()) {
            const std::uint64_t bytes = system.provider_descriptor->telemetry.size() * 32u;
            for (TelemetryReadbackSlot& slot : system.telemetry_readback) slot.buffer = this->context.runtime.resources.create_buffer(bytes, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        }
    }

    void DynamicsRuntime::apply_parameters(SystemRuntime& system, const std::span<const scene::DynamicParameterValue> values) {
        std::vector<SpectraSdkValue> encoded{};
        for (const scene::DynamicParameterValue& value : values) encoded.emplace_back(sdk_value(value));
        check_sdk_result(system.api->apply_parameters(system.provider_instance, encoded.data()), "parameter application");
    }

    void DynamicsRuntime::append_output(const SystemRuntime& system, const OutputRuntime& output, const SpectraSdkOutputCommit& commit, dynamics::DynamicSnapshot& snapshot) const {
        const std::vector<OutputBuffer>& buffers = output.slots[system.current_slot];
        const auto view = [](const OutputBuffer& buffer) { return dynamics::GpuBufferView{&buffer.gpu_buffer, buffer.descriptor.handle()}; };
        if (output.kind == SpectraSdkOutputKind::Mesh && output.scene_binding) {
            const auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&output.descriptor.details);
            std::size_t index{};
            dynamics::GpuTriangleMeshUpdate update{.geometry_id = scene::GeometryId{output.scene_binding->resource_id}, .positions = view(buffers[index++]), .indices = output.secondary_capacity == 0u ? std::nullopt : std::optional{view(output.static_buffers[0])}, .vertex_count = commit.active_count, .index_count = commit.secondary_count * 3u};
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) update.normals = view(buffers[index++]);
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) update.tangents = view(buffers[index++]);
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::TextureCoordinate)) != 0u) update.texture_coordinates = view(output.static_buffers.back());
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) ++index;
            snapshot.scene_updates.emplace_back(std::move(update));
        } else if (output.kind == SpectraSdkOutputKind::Spheres && output.scene_binding) snapshot.scene_updates.emplace_back(dynamics::GpuSphereSetUpdate{scene::SphereSetId{output.scene_binding->resource_id}, view(buffers[0]), commit.active_count});
        else if (output.kind == SpectraSdkOutputKind::Instances) snapshot.scene_updates.emplace_back(dynamics::GpuInstanceTransformUpdate{view(buffers[0]), commit.active_count});
        else if (output.kind == SpectraSdkOutputKind::Volume && output.scene_binding) {
            const auto& dataset = std::get<dynamics::FieldDataset>(output.descriptor.details);
            dynamics::GpuFieldUpdate update{.volume_id = scene::VolumeId{output.scene_binding->resource_id}};
            for (const dynamics::VolumeFieldDescriptor& field : dataset.fields) {
                dynamics::GpuVolumeFieldView field_view{.field = field};
                for (std::uint32_t component = 0; component != field.buffer_count; ++component) field_view.values.emplace_back(view(buffers[field.buffer_offset + component]));
                update.fields.emplace_back(std::move(field_view));
            }
            snapshot.scene_updates.emplace_back(std::move(update));
        }

        if (!this->configuration.setup.systems[system.scene_system_index].visible || commit.active_count == 0u) return;
        for (const scene::DynamicVisualizationView& visualization : output.visualizations) {
            math::Transform transform{};
            if (visualization.anchor.value != 0u) transform = std::ranges::find(this->configuration.source_scene->resources.instances, visualization.anchor, &scene::Instance::id)->transform;
            dynamics::VisualizationStyle style{visualization, transform};
            if (output.kind == SpectraSdkOutputKind::Points) snapshot.visualizations.emplace_back(dynamics::GpuPointVisualization{style, view(buffers[0]), commit.active_count});
            else if (output.kind == SpectraSdkOutputKind::Lines) snapshot.visualizations.emplace_back(dynamics::GpuSegmentVisualization{style, view(buffers[0]), commit.active_count});
            else if (output.kind == SpectraSdkOutputKind::Vectors) snapshot.visualizations.emplace_back(dynamics::GpuVectorVisualization{style, view(buffers[0]), commit.active_count});
            else if (output.kind == SpectraSdkOutputKind::Image) snapshot.visualizations.emplace_back(dynamics::GpuImageVisualization{style, std::get<dynamics::ImageDataset>(output.descriptor.details), view(buffers[0])});
            else if (output.kind == SpectraSdkOutputKind::Mesh) {
                const auto& mesh = std::get<dynamics::TriangleMeshDataset>(output.descriptor.details);
                std::size_t scalar_index{1u};
                if ((mesh.attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) ++scalar_index;
                if ((mesh.attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) ++scalar_index;
                const std::optional<dynamics::GpuBufferView> colors = (mesh.attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u ? std::optional{view(buffers[scalar_index])} : std::nullopt;
                if ((mesh.attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) ++scalar_index;
                const std::optional<dynamics::GpuBufferView> scalars = (mesh.attributes & std::to_underlying(SpectraSdkMeshAttribute::Scalar)) != 0u ? std::optional{view(buffers[scalar_index])} : std::nullopt;
                snapshot.visualizations.emplace_back(dynamics::GpuSurfaceVisualization{style, view(buffers[0]), output.secondary_capacity == 0u ? std::nullopt : std::optional{view(output.static_buffers[0])}, colors, scalars, commit.active_count, commit.secondary_count * 3u});
            }
        }
    }

    void DynamicsRuntime::discard_pending_snapshot() {
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) {
                this->context.runtime.resources.wait_external_timeline(system.timeline, system.signal_value);
                this->context.runtime.resources.signal_external_timeline(system.timeline, system.signal_value + 1u);
                system.output_pending = false;
            }
        this->publication.snapshot_pending  = false;
        this->publication.snapshot_acquired = false;
    }

    void DynamicsRuntime::publish_frame() {
        this->discard_pending_snapshot();
        dynamics::DynamicSnapshot snapshot{};
        for (SystemRuntime& system : this->systems.values) {
            SpectraSdkFrameCommit commit{};
            check_sdk_result(system.api->publish(system.provider_instance, &commit), "publication");
            system.current_slot = commit.slot_index;
            system.signal_value = commit.signal_value;
            system.output_pending = true;
            for (std::size_t index = 0; index != system.outputs.size(); ++index)
                if (system.outputs[index].kind != SpectraSdkOutputKind::Metrics) this->append_output(system, system.outputs[index], commit.outputs[index], snapshot);
        }
        this->publication.snapshot          = std::move(snapshot);
        this->publication.snapshot_pending  = true;
    }

    void DynamicsRuntime::step_to(const std::uint64_t target) {
        if (target <= this->clock.simulation_step) return;
        const std::uint64_t count = target - this->clock.simulation_step;
        for (SystemRuntime& system : this->systems.values) check_sdk_result(system.api->step(system.provider_instance, this->configuration.setup.clock.step_seconds, count), "step");
        this->clock.simulation_step = target;
    }

    void DynamicsRuntime::reset_systems() {
        for (SystemRuntime& system : this->systems.values) check_sdk_result(system.api->reset(system.provider_instance, this->configuration.setup.seed), "reset");
        this->clock.simulation_step = 0u;
        this->step_to(this->configuration.setup.clock.start_step);
    }

    void DynamicsRuntime::evaluate_frame(const std::uint64_t target) {
        if (target < this->clock.simulation_step) this->reset_systems();
        this->step_to(target);
        this->publish_frame();
    }

    void DynamicsRuntime::reset_simulation() { this->reset_systems(); this->publish_frame(); }

    void DynamicsRuntime::advance_one_step() {
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step >= *this->configuration.setup.clock.end_step) {
            if (!this->configuration.setup.clock.loop) return;
            this->reset_simulation();
            return;
        }
        this->evaluate_frame(this->clock.simulation_step + 1u);
    }
}
