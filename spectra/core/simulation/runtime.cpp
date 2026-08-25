module;

#include <abi.h>
#include <spectra/sdk/cuda_types.h>

#undef interface

module spectra.simulation.runtime;

import std;
import vulkan;

namespace spectra::simulation {
    namespace {
        [[nodiscard]] std::string sdk_string(const SpectraSdkString value) {
            if (value.size == 0u) return {};
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

        [[nodiscard]] scene::SimulationParameterKind scene_parameter_kind(const SpectraSdkValueKind kind) noexcept {
            switch (kind) {
            case SpectraSdkValueKind::Boolean: return scene::SimulationParameterKind::Boolean;
            case SpectraSdkValueKind::Integer: return scene::SimulationParameterKind::Integer;
            case SpectraSdkValueKind::Float: return scene::SimulationParameterKind::Float;
            case SpectraSdkValueKind::Float3: return scene::SimulationParameterKind::Float3;
            case SpectraSdkValueKind::Enumeration: return scene::SimulationParameterKind::Enumeration;
            }
            std::unreachable();
        }

        [[nodiscard]] SpectraSdkValueKind sdk_parameter_kind(const scene::SimulationParameterKind kind) noexcept {
            switch (kind) {
            case scene::SimulationParameterKind::Boolean: return SpectraSdkValueKind::Boolean;
            case scene::SimulationParameterKind::Integer: return SpectraSdkValueKind::Integer;
            case scene::SimulationParameterKind::Float: return SpectraSdkValueKind::Float;
            case scene::SimulationParameterKind::Float3: return SpectraSdkValueKind::Float3;
            case scene::SimulationParameterKind::Enumeration: return SpectraSdkValueKind::Enumeration;
            }
            std::unreachable();
        }

        [[nodiscard]] scene::SimulationParameterValue scene_value(const SpectraSdkValue& value) noexcept {
            return {scene_parameter_kind(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraSdkValue sdk_value(const scene::SimulationParameterValue& value) noexcept {
            return {sdk_parameter_kind(value.kind), value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
        }

        [[nodiscard]] SpectraSdkExternalHandle sdk_handle(const runtime::ExternalHandle& value) noexcept {
            return {value.value};
        }

        [[nodiscard]] ParameterApplication parameter_application(const SpectraSdkParameterApplication application) noexcept {
            switch (application) {
            case SpectraSdkParameterApplication::Live: return ParameterApplication::Live;
            case SpectraSdkParameterApplication::Reset: return ParameterApplication::Reset;
            case SpectraSdkParameterApplication::Recreate: return ParameterApplication::Recreate;
            }
            std::unreachable();
        }

        [[nodiscard]] scene::FieldKind field_kind(const SpectraSdkFieldKind kind) noexcept {
            switch (kind) {
            case SpectraSdkFieldKind::Float: return scene::FieldKind::Float;
            case SpectraSdkFieldKind::Float3: return scene::FieldKind::Float3;
            case SpectraSdkFieldKind::UInt32: return scene::FieldKind::UInt32;
            case SpectraSdkFieldKind::MacFloat3: return scene::FieldKind::MacFloat3;
            }
            std::unreachable();
        }

        [[nodiscard]] scene::VolumeFieldSampling field_sampling(const SpectraSdkVolumeFieldSampling sampling) noexcept {
            switch (sampling) {
            case SpectraSdkVolumeFieldSampling::Cell: return scene::VolumeFieldSampling::Cell;
            case SpectraSdkVolumeFieldSampling::Vertex: return scene::VolumeFieldSampling::Vertex;
            }
            std::unreachable();
        }

        [[nodiscard]] scene::VolumeVectorSpace vector_space(const SpectraSdkVolumeVectorSpace space) noexcept {
            switch (space) {
            case SpectraSdkVolumeVectorSpace::Grid: return scene::VolumeVectorSpace::Grid;
            case SpectraSdkVolumeVectorSpace::Local: return scene::VolumeVectorSpace::Local;
            case SpectraSdkVolumeVectorSpace::World: return scene::VolumeVectorSpace::World;
            }
            std::unreachable();
        }

        [[nodiscard]] TelemetryKind telemetry_kind(const SpectraSdkValueKind kind) noexcept {
            switch (kind) {
            case SpectraSdkValueKind::Boolean: return TelemetryKind::Boolean;
            case SpectraSdkValueKind::Integer:
            case SpectraSdkValueKind::Enumeration: return TelemetryKind::Integer;
            case SpectraSdkValueKind::Float: return TelemetryKind::Float;
            case SpectraSdkValueKind::Float3: return TelemetryKind::Float3;
            }
            std::unreachable();
        }

        [[nodiscard]] std::uint64_t element_count(const math::UInt3 extent) noexcept {
            return static_cast<std::uint64_t>(extent.x) * extent.y * extent.z;
        }

        struct OutputExportLifetime {
            std::vector<SpectraSdkGpuBuffer> fixed{};
            std::vector<std::vector<SpectraSdkGpuBuffer>> buffers{};
            std::vector<SpectraSdkGpuSlot> slots{};
            std::vector<runtime::ExternalHandle> fixed_handles{};
            std::vector<std::vector<runtime::ExternalHandle>> handles{};
        };
    } // namespace

    Runtime::Runtime(runtime::VulkanRuntime& runtime) noexcept : vulkan{runtime} {}

    Runtime::~Runtime() {
        this->destroy();
    }

    void Runtime::initialize(const std::filesystem::path& scene_path, const scene::Scene& authored_scene, scene::Scene& evaluated_scene) {
#if !defined(_WIN32)
        this->destroy();
        throw std::runtime_error("Spectra simulation providers are supported only on Windows");
#else
        this->destroy();
        try {
            this->configuration.authored_scene  = &authored_scene;
            this->configuration.evaluated_scene = &evaluated_scene;
            this->configuration.assets          = scene_path.parent_path();
            this->configuration.setup           = *authored_scene.simulation;
            this->configuration.initialized     = true;

            std::vector<std::string> required_providers{};
            for (const scene::SimulationSystem& system : this->configuration.setup.systems)
                if (!std::ranges::contains(required_providers, system.provider_id)) required_providers.emplace_back(system.provider_id);
            std::ranges::sort(required_providers);

            std::vector<std::filesystem::path> provider_paths{};
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{this->configuration.assets}) {
                if (entry.is_regular_file() && entry.path().filename().string().ends_with(".spectra-provider.dll")) provider_paths.emplace_back(entry.path());
            }
            std::ranges::sort(provider_paths);
            for (const std::filesystem::path& path : provider_paths) {
                ProviderLibrary& library                   = this->providers.libraries.emplace_back(path);
                const SpectraSdkProviderDescriptor& source = library.descriptor;
                ProviderDescriptor provider{.id = sdk_string(source.id), .build_time = library.build_time};
                if (!std::ranges::contains(required_providers, provider.id)) {
                    this->providers.libraries.pop_back();
                    continue;
                }
                provider.parameters.reserve(source.parameter_count);
                for (std::uint64_t index = 0; index != source.parameter_count; ++index) {
                    const SpectraSdkParameterDescriptor& parameter = source.parameters[index];
                    ParameterDescriptor value{
                        .id               = sdk_string(parameter.id),
                        .name             = sdk_string(parameter.name),
                        .unit             = sdk_string(parameter.unit),
                        .section_id       = sdk_string(parameter.section),
                        .description      = sdk_string(parameter.description),
                        .application_mode = parameter_application(parameter.application),
                        .value            = scene_value(parameter.default_value),
                        .minimum          = scene_value(parameter.minimum),
                        .maximum          = scene_value(parameter.maximum),
                        .step             = scene_value(parameter.step),
                    };
                    for (std::uint64_t enumerator = 0; enumerator != parameter.enumerator_count; ++enumerator) value.enumerators.emplace_back(sdk_string(parameter.enumerators[enumerator]));
                    provider.parameters.emplace_back(std::move(value));
                }
                provider.outputs.reserve(source.output_count);
                for (std::uint64_t index = 0; index != source.output_count; ++index) {
                    const SpectraSdkOutputDescriptor& output = source.outputs[index];
                    const std::string id                     = sdk_string(output.id);
                    if (output.kind == SpectraSdkOutputKind::Volume || output.kind == SpectraSdkOutputKind::Particles) {
                        std::vector<FieldDescriptor> fields{};
                        std::uint32_t buffer_offset{};
                        for (std::uint64_t field_index = 0; field_index != output.field_count; ++field_index) {
                            const SpectraSdkFieldDescriptor& source_field = output.fields[field_index];
                            const std::uint32_t buffer_count              = source_field.kind == SpectraSdkFieldKind::MacFloat3 ? 3u : 1u;
                            fields.emplace_back(sdk_string(source_field.id), sdk_string(source_field.name), sdk_string(source_field.unit), field_kind(source_field.kind), field_sampling(source_field.sampling), vector_space(source_field.vector_space), buffer_offset, buffer_count);
                            buffer_offset += buffer_count;
                        }
                        if (output.kind == SpectraSdkOutputKind::Volume) provider.outputs.emplace_back(id, FieldOutput{.fields = std::move(fields)});
                        else provider.outputs.emplace_back(id, ParticleSetOutput{.fields = std::move(fields)});
                        continue;
                    }
                    switch (output.kind) {
                    case SpectraSdkOutputKind::Mesh: provider.outputs.emplace_back(id, TriangleMeshOutput{0u, 0u, output.mesh_attributes}); break;
                    case SpectraSdkOutputKind::Spheres: provider.outputs.emplace_back(id, SphereSetOutput{}); break;
                    case SpectraSdkOutputKind::Instances: provider.outputs.emplace_back(id, InstanceTransformOutput{}); break;
                    case SpectraSdkOutputKind::Lines: provider.outputs.emplace_back(id, SegmentOutput{}); break;
                    case SpectraSdkOutputKind::Vectors: provider.outputs.emplace_back(id, VectorOutput{}); break;
                    case SpectraSdkOutputKind::Image: provider.outputs.emplace_back(id, ImageOutput{}); break;
                    case SpectraSdkOutputKind::HashGridRadianceField: provider.outputs.emplace_back(id, HashGridRadianceFieldOutput{}); break;
                    case SpectraSdkOutputKind::Cameras: provider.outputs.emplace_back(id, CameraOutput{}); break;
                    case SpectraSdkOutputKind::Volume:
                    case SpectraSdkOutputKind::Particles:
                    case SpectraSdkOutputKind::Metrics: std::unreachable();
                    }
                }
                provider.telemetry.reserve(source.metric_count);
                for (std::uint64_t index = 0; index != source.metric_count; ++index) {
                    const SpectraSdkMetricDescriptor& metric = source.metrics[index];
                    provider.telemetry.emplace_back(sdk_string(metric.id), sdk_string(metric.name), sdk_string(metric.unit), sdk_string(metric.section), telemetry_kind(metric.kind), metric.plot != 0u);
                }
                library.provider = std::move(provider);
                if (this->providers.by_id.contains(library.provider.id)) throw std::runtime_error(std::format("Multiple Provider DLLs declare '{}'", library.provider.id));
                this->providers.by_id.emplace(library.provider.id, &library);
            }
            for (const std::string& id : required_providers)
                if (!this->providers.by_id.contains(id)) throw std::runtime_error(std::format("Scene requires Provider '{}' but no matching .spectra-provider DLL was found in {}", id, this->configuration.assets.string()));

            for (std::size_t system_index = 0; system_index != this->configuration.setup.systems.size(); ++system_index) {
                const scene::SimulationSystem& declared = this->configuration.setup.systems[system_index];
                if (!declared.enabled) continue;
                ProviderLibrary& library   = this->provider_library(declared.provider_id);
                SystemRuntime& system      = this->systems.values.emplace_back();
                system.scene_system_index  = system_index;
                system.provider_descriptor = &library.provider;
                system.api                 = library.api;
                for (const ParameterDescriptor& parameter : library.provider.parameters) {
                    const auto found = std::ranges::find(declared.parameters, parameter.id, &scene::SimulationParameterSetting::parameter_id);
                    system.parameter_values.emplace_back(found == declared.parameters.end() ? parameter.value : found->value);
                }
                this->create_system(system, declared);
            }
            this->declare_outputs();
            this->reset_simulation();
        } catch (...) {
            this->destroy();
            throw;
        }
#endif
    }

    void Runtime::destroy() noexcept {
        if (!this->configuration.initialized && this->providers.libraries.empty()) return;
        try {
            if (static_cast<vk::Result>(this->vulkan.device.logical.getDispatcher()->vkDeviceWaitIdle(*this->vulkan.device.logical)) != vk::Result::eSuccess) std::terminate();
            this->discard_pending_frame();
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

    bool Runtime::initialized() const noexcept {
        return this->configuration.initialized;
    }

    const ProviderDescriptor& Runtime::provider_descriptor(const std::string_view id) const {
        return this->provider_library(id).provider;
    }

    const TelemetrySnapshot& Runtime::telemetry(const std::size_t system_index) const {
        return std::ranges::find(this->systems.values, system_index, &SystemRuntime::scene_system_index)->telemetry;
    }

    void Runtime::write_telemetry(const std::filesystem::path& path) const {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path};
        stream << "system,step,seconds,metric,value_x,value_y,value_z\n";
        for (const SystemRuntime& system : this->systems.values)
            for (const TelemetrySample& sample : system.telemetry.history)
                for (std::size_t index = 0; index != sample.values.size(); ++index) {
                    const TelemetryValue& value = sample.values[index];
                    stream << csv_field(system.provider_descriptor->id) << ',' << sample.simulation_step << ',' << sample.simulation_seconds << ',' << csv_field(system.provider_descriptor->telemetry[index].id) << ',' << (value.kind == TelemetryKind::Boolean || value.kind == TelemetryKind::Integer ? static_cast<double>(value.integer) : value.floating[0]) << ',' << value.floating[1] << ',' << value.floating[2] << '\n';
                }
    }

    std::span<const MeshOutputBinding> Runtime::mesh_bindings() const noexcept {
        return this->outputs.mesh_bindings;
    }
    std::span<const SphereSetOutputBinding> Runtime::sphere_set_bindings() const noexcept {
        return this->outputs.sphere_set_bindings;
    }
    std::span<const GpuVisualization> Runtime::visualizations() const noexcept {
        return this->publication.frame.visualizations;
    }

    bool Runtime::controls(const scene::InstanceId instance_id) const noexcept {
        const auto instance = std::ranges::find(this->configuration.authored_scene->resources.instances, instance_id, &scene::Instance::id);
        if (instance == this->configuration.authored_scene->resources.instances.end()) return false;
        const scene::Prototype& prototype = *std::ranges::find(this->configuration.authored_scene->resources.prototypes, instance->prototype, &scene::Prototype::id);
        return std::ranges::any_of(this->systems.values, [&prototype](const SystemRuntime& system) {
            return std::ranges::any_of(system.outputs, [&prototype](const OutputRuntime& output) {
                if (!output.scene_binding) return false;
                return std::ranges::any_of(prototype.primitives, [&output](const scene::Primitive& primitive) { return (std::holds_alternative<TriangleMeshOutput>(output.descriptor.details) && output.scene_binding->resource_id == primitive.geometry.value) || (std::holds_alternative<SphereSetOutput>(output.descriptor.details) && output.scene_binding->resource_id == primitive.spheres.value); });
            });
        });
    }

    bool Runtime::controls(const scene::VolumeId volume_id) const noexcept {
        return std::ranges::any_of(this->systems.values, [volume_id](const SystemRuntime& system) { return std::ranges::any_of(system.outputs, [volume_id](const OutputRuntime& output) { return output.scene_binding && std::holds_alternative<FieldOutput>(output.descriptor.details) && output.scene_binding->resource_id == volume_id.value; }); });
    }

    bool Runtime::controls(const scene::ParticleSetId particle_set_id) const noexcept {
        return std::ranges::any_of(this->systems.values, [particle_set_id](const SystemRuntime& system) { return std::ranges::any_of(system.outputs, [particle_set_id](const OutputRuntime& output) { return output.scene_binding && std::holds_alternative<ParticleSetOutput>(output.descriptor.details) && output.scene_binding->resource_id == particle_set_id.value; }); });
    }

    bool Runtime::controls(const scene::CameraId camera_id) const noexcept {
        return std::ranges::contains(this->outputs.camera_references, camera_id, &CameraReferenceImage::camera_id);
    }

    const CameraReferenceImage* Runtime::camera_reference(const scene::CameraId camera_id) const noexcept {
        const auto reference = std::ranges::find(this->outputs.camera_references, camera_id, &CameraReferenceImage::camera_id);
        return reference == this->outputs.camera_references.end() ? nullptr : &*reference;
    }

    bool Runtime::running() const noexcept {
        return this->clock.playing;
    }
    bool Runtime::faulted() const noexcept {
        return this->configuration.faulted;
    }
    SimulationTimeline Runtime::timeline() const noexcept {
        return {this->clock.simulation_step, this->clock.simulation_step * this->configuration.setup.clock.step_seconds};
    }
    void Runtime::start() {
        this->clock.playing = true;
    }
    void Runtime::pause() {
        this->clock.playing = false;
    }
    void Runtime::step() {
        this->pause();
        this->advance_one_step();
    }

    void Runtime::advance() {
        if (this->configuration.faulted || this->systems.values.empty() || !this->clock.playing || this->publication.frame_pending) return;
        try {
            this->advance_one_step();
        } catch (...) {
            this->configuration.faulted = true;
            this->clock.playing         = false;
            throw;
        }
    }

    void Runtime::evaluate(const std::uint64_t simulation_step) {
        this->pause();
        this->evaluate_frame(simulation_step);
    }
    void Runtime::evaluate_time(const double seconds) {
        this->evaluate(static_cast<std::uint64_t>(std::floor(seconds / this->configuration.setup.clock.step_seconds)));
    }
    void Runtime::reset() {
        this->pause();
        this->reset_simulation();
    }

    bool Runtime::apply_parameter_changes(const std::size_t system_index, const std::span<const scene::SimulationParameterSetting> parameters, const bool should_reset) {
        SystemRuntime& system = *std::ranges::find(this->systems.values, system_index, &SystemRuntime::scene_system_index);
        std::vector<scene::SimulationParameterValue> values{};
        for (const ParameterDescriptor& descriptor : system.provider_descriptor->parameters) {
            const auto found = std::ranges::find(parameters, descriptor.id, &scene::SimulationParameterSetting::parameter_id);
            values.emplace_back(found == parameters.end() ? descriptor.value : found->value);
        }
        bool recreate{};
        for (std::size_t index = 0; index != values.size(); ++index)
            if (system.provider_descriptor->parameters[index].application_mode == ParameterApplication::Recreate && values[index] != system.parameter_values[index]) recreate = true;
        if (recreate) {
            this->discard_pending_frame();
            if (static_cast<vk::Result>(this->vulkan.device.logical.getDispatcher()->vkDeviceWaitIdle(*this->vulkan.device.logical)) != vk::Result::eSuccess) throw std::runtime_error("Vulkan device wait failed during Provider recreation");
            SystemRuntime replacement{};
            replacement.scene_system_index   = system.scene_system_index;
            replacement.provider_descriptor  = system.provider_descriptor;
            replacement.api                  = system.api;
            replacement.parameter_values     = std::move(values);
            scene::SimulationSystem declared = this->configuration.setup.systems[system_index];
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
            this->declare_outputs();
            this->publish_frame();
            return true;
        }
        this->apply_parameters(system, values);
        system.parameter_values = std::move(values);
        if (should_reset) this->reset();
        return false;
    }

    const SimulationFrame* Runtime::acquire_frame() {
        if (!this->publication.frame_pending) return nullptr;
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) this->vulkan.frames.enqueue_external_wait(system.timeline, system.signal_value, vk::PipelineStageFlagBits2::eAllCommands);
        this->publication.frame_acquired = true;
        return &this->publication.frame;
    }

    void Runtime::consume_frame() {
        if (!this->publication.frame_pending) return;
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) {
                if (!this->publication.frame_acquired) this->vulkan.frames.enqueue_external_wait(system.timeline, system.signal_value, vk::PipelineStageFlagBits2::eAllCommands);
                this->vulkan.frames.enqueue_external_signal(system.timeline, system.signal_value + 1u, vk::PipelineStageFlagBits2::eAllCommands);
                system.output_pending = false;
            }
        this->publication.frame_pending  = false;
        this->publication.frame_acquired = false;
    }

    void Runtime::record_telemetry(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->publication.frame_pending) return;
        for (SystemRuntime& system : this->systems.values) {
            if (system.provider_descriptor->telemetry.empty()) continue;
            TelemetryReadbackSlot& readback = system.telemetry_readback[frame_slot_index];
            const OutputBuffer& metrics     = system.metrics.storage.slots[system.current_slot][0];
            command_buffer.copyBuffer(*metrics.gpu_buffer.buffer, *readback.buffer.buffer, vk::BufferCopy{0u, 0u, metrics.gpu_buffer.size});
            readback.simulation_step    = this->clock.simulation_step;
            readback.simulation_seconds = this->timeline().seconds;
            readback.pending            = true;
        }
    }

    void Runtime::resolve_telemetry(const std::uint32_t frame_slot_index) {
        for (SystemRuntime& system : this->systems.values) {
            TelemetryReadbackSlot& readback = system.telemetry_readback[frame_slot_index];
            if (!readback.pending) continue;
            struct MetricValue {
                double floating[3];
                std::int64_t integer;
            };
            const auto* values = static_cast<const MetricValue*>(readback.buffer.mapped);
            TelemetrySample sample{readback.simulation_step, readback.simulation_seconds};
            for (std::size_t index = 0; index != system.provider_descriptor->telemetry.size(); ++index) {
                TelemetryValue value{system.provider_descriptor->telemetry[index].kind, values[index].integer, {values[index].floating[0], values[index].floating[1], values[index].floating[2]}};
                system.telemetry.values[index] = value;
                sample.values.emplace_back(value);
            }
            system.telemetry.history.emplace_back(std::move(sample));
            if (system.telemetry.history.size() > 4096u) system.telemetry.history.pop_front();
            readback.pending = false;
        }
    }

    ProviderLibrary& Runtime::provider_library(const std::string_view id) const {
        return *this->providers.by_id.find(std::string{id})->second;
    }

    SpectraSdkResult Runtime::configure_output(void* source, const SpectraSdkOutputLayout* layout, SpectraSdkOutputRequest* request) noexcept {
        Runtime& runtime = *static_cast<Runtime*>(source);
        try {
            std::unique_ptr<OutputExportLifetime> lifetime = std::make_unique<OutputExportLifetime>();
            SystemRuntime& system                          = *runtime.publication.configuring_system;
            const std::size_t index                        = static_cast<std::size_t>(layout->output_index);
            OutputRuntime* output                          = layout->kind == SpectraSdkOutputKind::Metrics ? nullptr : &system.outputs[index];
            OutputStorage& storage                         = output ? output->storage : system.metrics.storage;
            const math::UInt3 resolution{layout->resolution[0], layout->resolution[1], layout->resolution[2]};
            if (output && std::get_if<TriangleMeshOutput>(&output->descriptor.details)) {
                TriangleMeshOutput& mesh = std::get<TriangleMeshOutput>(output->descriptor.details);
                mesh.vertex_capacity     = layout->primary_capacity;
                mesh.index_capacity      = layout->secondary_capacity * 3u;
                mesh.attributes          = layout->mesh_attributes;
            } else if (output && std::get_if<SphereSetOutput>(&output->descriptor.details)) std::get<SphereSetOutput>(output->descriptor.details).capacity = layout->primary_capacity;
            else if (output && std::get_if<InstanceTransformOutput>(&output->descriptor.details)) std::get<InstanceTransformOutput>(output->descriptor.details).capacity = layout->primary_capacity;
            else if (output && std::get_if<ParticleSetOutput>(&output->descriptor.details)) {
                ParticleSetOutput& particles = std::get<ParticleSetOutput>(output->descriptor.details);
                particles.capacity           = layout->primary_capacity;
                particles.radius             = layout->particle_radius;
            } else if (output && std::get_if<SegmentOutput>(&output->descriptor.details)) std::get<SegmentOutput>(output->descriptor.details).capacity = layout->primary_capacity;
            else if (output && std::get_if<VectorOutput>(&output->descriptor.details)) std::get<VectorOutput>(output->descriptor.details).capacity = layout->primary_capacity;
            else if (output && std::get_if<FieldOutput>(&output->descriptor.details)) std::get<FieldOutput>(output->descriptor.details).resolution = resolution;
            else if (output && std::get_if<ImageOutput>(&output->descriptor.details)) std::get<ImageOutput>(output->descriptor.details).extent = {resolution.x, resolution.y};
            else if (output && std::get_if<CameraOutput>(&output->descriptor.details)) {
                CameraOutput& cameras = std::get<CameraOutput>(output->descriptor.details);
                cameras.extent        = {resolution.x, resolution.y};
                cameras.cameras.clear();
                cameras.cameras.reserve(layout->camera_count);
                for (std::uint64_t camera_index = 0; camera_index != layout->camera_count; ++camera_index) {
                    const SpectraSdkCamera& camera = layout->cameras[camera_index];
                    cameras.cameras.emplace_back(math::Float3{camera.right[0], camera.right[1], camera.right[2]}, math::Float3{camera.down[0], camera.down[1], camera.down[2]}, math::Float3{camera.forward[0], camera.forward[1], camera.forward[2]}, math::Float3{camera.position[0], camera.position[1], camera.position[2]}, math::Float2{camera.focal[0], camera.focal[1]}, math::Float2{camera.principal[0], camera.principal[1]});
                }
            }

            std::vector<std::uint64_t> static_sizes{};
            std::vector<std::uint64_t> slot_buffer_sizes{};
            if (layout->kind == SpectraSdkOutputKind::Mesh) {
                if (layout->secondary_capacity != 0u) static_sizes.emplace_back(static_cast<std::uint64_t>(layout->secondary_capacity) * 3u * sizeof(std::uint32_t));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::TextureCoordinate)) != 0u) static_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float2));
                slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float3));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float3));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float3));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float4));
                if ((layout->mesh_attributes & std::to_underlying(SpectraSdkMeshAttribute::Scalar)) != 0u) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(float));
            } else if (layout->kind == SpectraSdkOutputKind::Volume) {
                const auto& fields = std::get<FieldOutput>(output->descriptor.details).fields;
                for (const FieldDescriptor& field : fields) {
                    if (field.kind == scene::FieldKind::MacFloat3) {
                        slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(resolution.x + 1u) * resolution.y * resolution.z * sizeof(float));
                        slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(resolution.x) * (resolution.y + 1u) * resolution.z * sizeof(float));
                        slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(resolution.x) * resolution.y * (resolution.z + 1u) * sizeof(float));
                    } else slot_buffer_sizes.emplace_back(element_count(resolution) * (field.kind == scene::FieldKind::Float ? sizeof(float) : field.kind == scene::FieldKind::Float3 ? sizeof(sdk::Float3) : sizeof(std::uint32_t)));
                }
            } else if (layout->kind == SpectraSdkOutputKind::Particles) {
                slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float3));
                for (const FieldDescriptor& field : std::get<ParticleSetOutput>(output->descriptor.details).fields) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * (field.kind == scene::FieldKind::Float ? sizeof(float) : field.kind == scene::FieldKind::Float3 ? sizeof(sdk::Float3) : sizeof(std::uint32_t)));
            } else if (layout->kind == SpectraSdkOutputKind::HashGridRadianceField) {
                slot_buffer_sizes = {
                    static_cast<std::uint64_t>(SPECTRA_SDK_HASH_GRID_ENTRY_COUNT) * sizeof(sdk::Half4),
                    static_cast<std::uint64_t>(SPECTRA_SDK_DENSITY_INPUT_COUNT) * sizeof(sdk::Half),
                    static_cast<std::uint64_t>(SPECTRA_SDK_DENSITY_OUTPUT_COUNT) * sizeof(sdk::Half),
                    static_cast<std::uint64_t>(SPECTRA_SDK_RGB_INPUT_COUNT) * sizeof(sdk::Half),
                    static_cast<std::uint64_t>(SPECTRA_SDK_RGB_HIDDEN_COUNT) * sizeof(sdk::Half),
                    static_cast<std::uint64_t>(SPECTRA_SDK_RGB_OUTPUT_COUNT) * sizeof(sdk::Half),
                    static_cast<std::uint64_t>(SPECTRA_SDK_OCCUPANCY_WORD_COUNT) * sizeof(std::uint32_t),
                };
            } else if (layout->kind == SpectraSdkOutputKind::Cameras) static_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * resolution.x * resolution.y * sizeof(sdk::Rgba8));
            else if (layout->kind == SpectraSdkOutputKind::Spheres) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Sphere));
            else if (layout->kind == SpectraSdkOutputKind::Instances) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Instance));
            else if (layout->kind == SpectraSdkOutputKind::Lines) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Line));
            else if (layout->kind == SpectraSdkOutputKind::Vectors) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Vector));
            else if (layout->kind == SpectraSdkOutputKind::Image) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(sdk::Float4));
            else if (layout->kind == SpectraSdkOutputKind::Metrics) slot_buffer_sizes.emplace_back(static_cast<std::uint64_t>(layout->primary_capacity) * sizeof(SpectraSdkMetricValue));
            else std::unreachable();

            storage.static_buffers.resize(static_sizes.size());
            lifetime->fixed.resize(static_sizes.size());
            lifetime->fixed_handles.resize(static_sizes.size());
            for (std::size_t buffer = 0; buffer != static_sizes.size(); ++buffer) {
                OutputBuffer& destination = storage.static_buffers[buffer];
                destination.gpu_buffer    = runtime.vulkan.resources.create_external_buffer(static_sizes[buffer], vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                destination.descriptor    = runtime.vulkan.frames.allocate_resource_descriptor();
                runtime.vulkan.resources.write_buffer_descriptor(destination.descriptor, vk::DescriptorType::eStorageBuffer, destination.gpu_buffer);
                lifetime->fixed_handles[buffer] = runtime.vulkan.resources.export_buffer_memory_handle(destination.gpu_buffer);
                lifetime->fixed[buffer]         = {sdk_handle(lifetime->fixed_handles[buffer]), destination.gpu_buffer.external_memory_size, destination.gpu_buffer.size};
            }
            storage.slots.resize(runtime::VulkanFrames::frames_in_flight);
            lifetime->buffers.resize(runtime::VulkanFrames::frames_in_flight);
            lifetime->handles.resize(runtime::VulkanFrames::frames_in_flight);
            lifetime->slots.resize(runtime::VulkanFrames::frames_in_flight);
            for (std::uint32_t slot = 0; slot != runtime::VulkanFrames::frames_in_flight; ++slot) {
                storage.slots[slot].resize(slot_buffer_sizes.size());
                lifetime->buffers[slot].resize(slot_buffer_sizes.size());
                lifetime->handles[slot].resize(slot_buffer_sizes.size());
                for (std::size_t buffer = 0; buffer != slot_buffer_sizes.size(); ++buffer) {
                    OutputBuffer& destination = storage.slots[slot][buffer];
                    destination.gpu_buffer    = runtime.vulkan.resources.create_external_buffer(slot_buffer_sizes[buffer], vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                    destination.descriptor    = runtime.vulkan.frames.allocate_resource_descriptor();
                    runtime.vulkan.resources.write_buffer_descriptor(destination.descriptor, vk::DescriptorType::eStorageBuffer, destination.gpu_buffer);
                    lifetime->handles[slot][buffer] = runtime.vulkan.resources.export_buffer_memory_handle(destination.gpu_buffer);
                    lifetime->buffers[slot][buffer] = {sdk_handle(lifetime->handles[slot][buffer]), destination.gpu_buffer.external_memory_size, destination.gpu_buffer.size};
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

    void Runtime::release_output(void* source) noexcept {
        delete static_cast<OutputExportLifetime*>(source);
    }

    void Runtime::bind_output(OutputRuntime& output, const scene::SimulationSystem& system) const {
        const auto binding = std::ranges::find(system.scene_bindings, output.descriptor.id, &scene::SimulationOutputBinding::output_id);
        if (binding != system.scene_bindings.end()) output.scene_binding = *binding;
        for (const scene::SimulationVisualization& view : system.visualizations)
            if (view.output_id == output.descriptor.id) output.visualizations.emplace_back(view);
    }

    void Runtime::declare_outputs() {
        for (const CameraReferenceImage& reference : this->outputs.camera_references) std::erase_if(this->configuration.evaluated_scene->resources.cameras, [&reference](const scene::Camera& camera) { return camera.id == reference.camera_id; });
        this->outputs                      = {};
        this->configuration.next_camera_id = 1u;
        for (const scene::Camera& camera : this->configuration.authored_scene->resources.cameras) this->configuration.next_camera_id = std::max(this->configuration.next_camera_id, camera.id.value + 1u);
        for (SystemRuntime& system : this->systems.values)
            for (OutputRuntime& output : system.outputs) this->declare_scene_output(output);
    }

    void Runtime::declare_scene_output(OutputRuntime& output) {
        if (const auto* cameras = std::get_if<CameraOutput>(&output.descriptor.details)) {
            const math::Bounds3 bounds = this->configuration.evaluated_scene->view().bounds();
            const math::Float3 center  = bounds.center();
            const float clip_margin    = std::max(bounds.diagonal().length() * 0.0001f, 0.00001f);
            const GpuBufferView pixels{&output.storage.static_buffers[0].gpu_buffer, output.storage.static_buffers[0].descriptor};
            for (std::size_t index = 0; index != cameras->cameras.size(); ++index) {
                const CameraDescriptor& source = cameras->cameras[index];
                float minimum_depth            = std::numeric_limits<float>::max();
                float maximum_depth            = std::numeric_limits<float>::lowest();
                for (std::uint32_t corner = 0; corner != 8u; ++corner) {
                    const math::Float3 position{
                        (corner & 1u) == 0u ? bounds.minimum.x : bounds.maximum.x,
                        (corner & 2u) == 0u ? bounds.minimum.y : bounds.maximum.y,
                        (corner & 4u) == 0u ? bounds.minimum.z : bounds.maximum.z,
                    };
                    const float depth = (position - source.position).dot(source.forward);
                    minimum_depth     = std::min(minimum_depth, depth);
                    maximum_depth     = std::max(maximum_depth, depth);
                }
                const float tangent       = static_cast<float>(cameras->extent[1]) / (2.0f * source.focal.y);
                const float screen_width  = static_cast<float>(cameras->extent[0]) / (source.focal.x * tangent);
                const float screen_height = 2.0f;
                const float near_plane    = std::max(clip_margin, minimum_depth - clip_margin);
                const float far_plane     = std::max(near_plane + clip_margin, maximum_depth + clip_margin);
                const scene::CameraId camera_id{this->configuration.next_camera_id++};
                this->configuration.evaluated_scene->resources.cameras.emplace_back(scene::Camera{
                    .id        = camera_id,
                    .name      = std::format("{} Camera {:03}", output.descriptor.id, index + 1u),
                    .transform = math::Transform{{
                        source.right.x,
                        -source.down.x,
                        -source.forward.x,
                        source.position.x,
                        source.right.y,
                        -source.down.y,
                        -source.forward.y,
                        source.position.y,
                        source.right.z,
                        -source.down.z,
                        -source.forward.z,
                        source.position.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    }},
                    .data =
                        scene::PerspectiveCameraData{
                            .vertical_fov = 2.0f * std::atan(tangent) * 180.0f / std::numbers::pi_v<float>,
                            .screen_window =
                                {
                                    {-source.principal.x * screen_width / static_cast<float>(cameras->extent[0]), -(static_cast<float>(cameras->extent[1]) - source.principal.y) * screen_height / static_cast<float>(cameras->extent[1])},
                                    {(static_cast<float>(cameras->extent[0]) - source.principal.x) * screen_width / static_cast<float>(cameras->extent[0]), source.principal.y * screen_height / static_cast<float>(cameras->extent[1])},
                                },
                            .focal_distance = std::max(0.001f, (center - source.position).dot(source.forward)),
                            .near_plane     = near_plane,
                            .far_plane      = far_plane,
                        },
                });
                this->outputs.camera_references.emplace_back(camera_id, output.descriptor.id, static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(cameras->cameras.size()), cameras->extent, source.focal, source.principal, pixels, static_cast<std::uint32_t>(index));
            }
            this->configuration.evaluated_scene->mark_changed(scene::SceneChange::Camera | scene::SceneChange::Structure);
            return;
        }
        if (!output.scene_binding) return;
        if (const auto* mesh = std::get_if<TriangleMeshOutput>(&output.descriptor.details)) {
            const auto view             = [](const OutputBuffer& buffer) { return GpuBufferView{&buffer.gpu_buffer, buffer.descriptor}; };
            const std::optional indices = mesh->index_capacity == 0u ? std::nullopt : std::optional{view(output.storage.static_buffers[0])};
            const bool has_uv           = (mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::TextureCoordinate)) != 0u;
            this->outputs.mesh_bindings.emplace_back(scene::GeometryId{output.scene_binding->resource_id}, mesh->vertex_capacity, mesh->index_capacity, indices, has_uv ? std::optional{view(output.storage.static_buffers.back())} : std::nullopt);
        } else if (const auto* spheres = std::get_if<SphereSetOutput>(&output.descriptor.details)) this->outputs.sphere_set_bindings.emplace_back(scene::SphereSetId{output.scene_binding->resource_id}, spheres->capacity);
        else if (const auto* particles = std::get_if<ParticleSetOutput>(&output.descriptor.details)) {
            scene::ParticleSet& particle_set = *std::ranges::find(this->configuration.evaluated_scene->resources.particle_sets, scene::ParticleSetId{output.scene_binding->resource_id}, &scene::ParticleSet::id);
            particle_set.radius              = particles->radius;
            particle_set.fields.clear();
            particle_set.fields.reserve(particles->fields.size());
            for (const FieldDescriptor& descriptor : particles->fields) {
                std::variant<scene::ScalarParticleField, scene::VectorParticleField, scene::CategoryParticleField> data{};
                if (descriptor.kind == scene::FieldKind::Float) data = scene::ScalarParticleField{};
                else if (descriptor.kind == scene::FieldKind::Float3) data = scene::VectorParticleField{descriptor.vector_space};
                else data = scene::CategoryParticleField{};
                particle_set.fields.emplace_back(descriptor.id, descriptor.name, descriptor.unit, std::move(data));
            }
            ++particle_set.revision.content;
            ++particle_set.revision.topology;
            this->configuration.evaluated_scene->mark_changed(scene::SceneChange::Particle | scene::SceneChange::Structure);
        } else if (const auto* field = std::get_if<FieldOutput>(&output.descriptor.details)) {
            scene::Volume& volume   = *std::ranges::find(this->configuration.evaluated_scene->resources.volumes, scene::VolumeId{output.scene_binding->resource_id}, &scene::Volume::id);
            scene::GridVolume& grid = std::get<scene::GridVolume>(volume.data);
            grid.resolution         = field->resolution;
            grid.fields.clear();
            grid.fields.reserve(field->fields.size());
            for (const FieldDescriptor& descriptor : field->fields) {
                std::variant<scene::ScalarVolumeField, scene::VectorVolumeField, scene::CategoryVolumeField, scene::MacVolumeField> data{};
                if (descriptor.kind == scene::FieldKind::Float) data = scene::ScalarVolumeField{descriptor.sampling};
                else if (descriptor.kind == scene::FieldKind::Float3) data = scene::VectorVolumeField{descriptor.sampling, descriptor.vector_space};
                else if (descriptor.kind == scene::FieldKind::UInt32) data = scene::CategoryVolumeField{descriptor.sampling};
                else data = scene::MacVolumeField{descriptor.vector_space};
                grid.fields.emplace_back(descriptor.id, descriptor.name, descriptor.unit, std::move(data));
            }
            ++volume.revision.content;
            ++volume.revision.topology;
            this->configuration.evaluated_scene->mark_changed(scene::SceneChange::Volume | scene::SceneChange::Structure);
        } else if (std::holds_alternative<HashGridRadianceFieldOutput>(output.descriptor.details)) this->configuration.evaluated_scene->mark_changed(scene::SceneChange::NeuralField);
    }

    void Runtime::create_system(SystemRuntime& system, const scene::SimulationSystem& declared) {
        const auto& identity = this->vulkan.device.identity;
        system.telemetry.values.resize(system.provider_descriptor->telemetry.size());
        std::vector<SpectraSdkValue> parameters{};
        for (const scene::SimulationParameterValue& value : system.parameter_values) parameters.emplace_back(sdk_value(value));
        const std::u8string encoded_assets = this->configuration.assets.generic_u8string();
        std::string assets{reinterpret_cast<const char*>(encoded_assets.data()), encoded_assets.size()};
        SpectraSdkCreateInfo create{{assets.data(), assets.size()}, parameters.data()};
        std::ranges::copy(identity.uuid, create.vulkan_device_uuid);
        std::ranges::copy(identity.luid, create.vulkan_device_luid);
        create.vulkan_device_luid_valid              = identity.luid_valid;
        create.vulkan_device_node_mask               = identity.node_mask;
        const SpectraSdkProviderCreateResult created = system.api->create_provider(&create);
        check_sdk_result(created.result, "creation");
        system.provider_instance = created.provider;
        for (const OutputDescriptor& descriptor : system.provider_descriptor->outputs) {
            OutputRuntime& output = system.outputs.emplace_back(descriptor);
            this->bind_output(output, declared);
        }
        system.timeline                         = this->vulkan.resources.create_external_simulation_timeline();
        runtime::ExternalHandle timeline_handle = this->vulkan.resources.export_timeline_semaphore_handle(system.timeline);
        SpectraSdkSetupSink sink{this, sdk_handle(timeline_handle)};
        sink.slot_count                      = runtime::VulkanFrames::frames_in_flight;
        sink.configure_output                = &Runtime::configure_output;
        sink.release_output                  = &Runtime::release_output;
        this->publication.configuring_system = &system;
        try {
            check_sdk_result(system.api->setup(system.provider_instance, &sink), "setup");
        } catch (...) {
            this->publication.configuring_system = nullptr;
            throw;
        }
        this->publication.configuring_system = nullptr;
        this->vulkan.resources.wait_external_timeline(system.timeline, 1u);
        this->vulkan.resources.signal_external_timeline(system.timeline, 2u);
        if (!system.provider_descriptor->telemetry.empty()) {
            const std::uint64_t bytes = system.provider_descriptor->telemetry.size() * sizeof(SpectraSdkMetricValue);
            for (TelemetryReadbackSlot& slot : system.telemetry_readback) slot.buffer = this->vulkan.resources.create_buffer(bytes, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        }
    }

    void Runtime::apply_parameters(SystemRuntime& system, const std::span<const scene::SimulationParameterValue> values) {
        std::vector<SpectraSdkValue> encoded{};
        for (const scene::SimulationParameterValue& value : values) encoded.emplace_back(sdk_value(value));
        check_sdk_result(system.api->apply_parameters(system.provider_instance, encoded.data()), "parameter application");
    }

    void Runtime::append_output(const SystemRuntime& system, const OutputRuntime& output, const SpectraSdkOutputCommit& commit, SimulationFrame& frame) const {
        const std::vector<OutputBuffer>& buffers = output.storage.slots[system.current_slot];
        const auto view                          = [](const OutputBuffer& buffer) { return GpuBufferView{&buffer.gpu_buffer, buffer.descriptor}; };
        if (const auto* mesh = std::get_if<TriangleMeshOutput>(&output.descriptor.details); mesh && output.scene_binding) {
            std::size_t index{};
            GpuTriangleMeshUpdate update{.geometry_id = scene::GeometryId{output.scene_binding->resource_id}, .positions = view(buffers[index++])};
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) update.normals = view(buffers[index++]);
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) update.tangents = view(buffers[index++]);
            if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) ++index;
            frame.scene_updates.emplace_back(std::move(update));
        } else if (std::holds_alternative<SphereSetOutput>(output.descriptor.details) && output.scene_binding) frame.scene_updates.emplace_back(GpuSphereSetUpdate{scene::SphereSetId{output.scene_binding->resource_id}, view(buffers[0]), commit.active_count});
        else if (const auto* particles = std::get_if<ParticleSetOutput>(&output.descriptor.details); particles && output.scene_binding) {
            GpuParticleSetUpdate update{.particle_set_id = scene::ParticleSetId{output.scene_binding->resource_id}, .positions = view(buffers[0]), .count = commit.active_count};
            for (const FieldDescriptor& field : particles->fields) update.fields.emplace_back(field, std::vector{view(buffers[field.buffer_offset + 1u])});
            frame.scene_updates.emplace_back(std::move(update));
        } else if (std::holds_alternative<InstanceTransformOutput>(output.descriptor.details)) frame.scene_updates.emplace_back(GpuInstanceTransformUpdate{view(buffers[0]), commit.active_count});
        else if (const auto* field_output = std::get_if<FieldOutput>(&output.descriptor.details); field_output && output.scene_binding) {
            GpuFieldUpdate update{.volume_id = scene::VolumeId{output.scene_binding->resource_id}};
            for (const FieldDescriptor& field : field_output->fields) {
                GpuFieldView field_view{.field = field};
                for (std::uint32_t component = 0; component != field.buffer_count; ++component) field_view.values.emplace_back(view(buffers[field.buffer_offset + component]));
                update.fields.emplace_back(std::move(field_view));
            }
            frame.scene_updates.emplace_back(std::move(update));
        } else if (std::holds_alternative<HashGridRadianceFieldOutput>(output.descriptor.details) && output.scene_binding) {
            frame.scene_updates.emplace_back(GpuHashGridRadianceFieldUpdate{
                scene::NeuralFieldId{output.scene_binding->resource_id},
                view(buffers[0]),
                view(buffers[1]),
                view(buffers[2]),
                view(buffers[3]),
                view(buffers[4]),
                view(buffers[5]),
                view(buffers[6]),
            });
        }

        if (!this->configuration.setup.systems[system.scene_system_index].visible || commit.active_count == 0u) return;
        for (const scene::SimulationVisualization& visualization : output.visualizations) {
            math::Transform transform{};
            if (visualization.anchor.value != 0u) transform = std::ranges::find(this->configuration.authored_scene->resources.instances, visualization.anchor, &scene::Instance::id)->transform;
            VisualizationStyle style{visualization, transform};
            if (std::holds_alternative<SegmentOutput>(output.descriptor.details)) frame.visualizations.emplace_back(GpuSegmentVisualization{style, view(buffers[0]), commit.active_count});
            else if (std::holds_alternative<VectorOutput>(output.descriptor.details)) frame.visualizations.emplace_back(GpuVectorVisualization{style, view(buffers[0]), commit.active_count});
            else if (const auto* image = std::get_if<ImageOutput>(&output.descriptor.details)) frame.visualizations.emplace_back(GpuImageVisualization{style, *image, view(buffers[0])});
            else if (const auto* mesh = std::get_if<TriangleMeshOutput>(&output.descriptor.details)) {
                std::size_t scalar_index{1u};
                if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Normal)) != 0u) ++scalar_index;
                if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Tangent)) != 0u) ++scalar_index;
                const std::optional<GpuBufferView> colors = (mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u ? std::optional{view(buffers[scalar_index])} : std::nullopt;
                if ((mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Color)) != 0u) ++scalar_index;
                const std::optional<GpuBufferView> scalars = (mesh->attributes & std::to_underlying(SpectraSdkMeshAttribute::Scalar)) != 0u ? std::optional{view(buffers[scalar_index])} : std::nullopt;
                frame.visualizations.emplace_back(GpuSurfaceVisualization{style, view(buffers[0]), mesh->index_capacity == 0u ? std::nullopt : std::optional{view(output.storage.static_buffers[0])}, colors, scalars, mesh->vertex_capacity, mesh->index_capacity});
            }
        }
    }

    void Runtime::discard_pending_frame() {
        for (SystemRuntime& system : this->systems.values)
            if (system.output_pending) {
                this->vulkan.resources.wait_external_timeline(system.timeline, system.signal_value);
                this->vulkan.resources.signal_external_timeline(system.timeline, system.signal_value + 1u);
                system.output_pending = false;
            }
        this->publication.frame_pending  = false;
        this->publication.frame_acquired = false;
    }

    void Runtime::publish_frame() {
        this->discard_pending_frame();
        SimulationFrame frame{};
        for (SystemRuntime& system : this->systems.values) {
            SpectraSdkFrameCommit commit{};
            check_sdk_result(system.api->publish(system.provider_instance, &commit), "publication");
            system.current_slot   = commit.slot_index;
            system.signal_value   = commit.signal_value;
            system.output_pending = true;
            for (std::size_t index = 0; index != system.outputs.size(); ++index)
                if (!std::holds_alternative<CameraOutput>(system.outputs[index].descriptor.details)) this->append_output(system, system.outputs[index], commit.outputs[index], frame);
        }
        this->publication.frame         = std::move(frame);
        this->publication.frame_pending = true;
    }

    void Runtime::step_to(const std::uint64_t target) {
        if (target <= this->clock.simulation_step) return;
        const std::uint64_t count = target - this->clock.simulation_step;
        for (SystemRuntime& system : this->systems.values) check_sdk_result(system.api->step(system.provider_instance, this->configuration.setup.clock.step_seconds, count), "step");
        this->clock.simulation_step = target;
    }

    void Runtime::reset_systems() {
        for (SystemRuntime& system : this->systems.values) check_sdk_result(system.api->reset(system.provider_instance, this->configuration.setup.seed), "reset");
        this->clock.simulation_step = 0u;
        this->step_to(this->configuration.setup.clock.start_step);
    }

    void Runtime::evaluate_frame(const std::uint64_t target) {
        if (target < this->clock.simulation_step) this->reset_systems();
        this->step_to(target);
        this->publish_frame();
    }

    void Runtime::reset_simulation() {
        this->reset_systems();
        this->publish_frame();
    }

    void Runtime::advance_one_step() {
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step >= *this->configuration.setup.clock.end_step) {
            if (!this->configuration.setup.clock.loop) return;
            this->reset_simulation();
            return;
        }
        this->evaluate_frame(this->clock.simulation_step + 1u);
    }
} // namespace spectra::simulation
