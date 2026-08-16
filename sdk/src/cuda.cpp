module;

#include <cuda_runtime_api.h>
#include <spectra/sdk/cuda_types.h>

#include "../internal/abi.h"

module spectra.sdk.cuda;

import std;

namespace spectra::sdk::cuda {
    namespace {
        void check_cuda(const cudaError_t result, const char* operation) {
            if (result == cudaSuccess) return;
            throw std::runtime_error(std::format("{}: {}", operation, cudaGetErrorString(result)));
        }

        struct ImportedBuffer {
            cudaExternalMemory_t memory{};
            void* data{};
            std::uint64_t byte_size{};

            ImportedBuffer() = default;

            explicit ImportedBuffer(const SpectraSdkGpuBuffer& source) : byte_size(source.byte_size) {
                cudaExternalMemoryHandleDesc description{};
                description.type                = cudaExternalMemoryHandleTypeOpaqueWin32;
                description.handle.win32.handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(source.memory.value));
                description.size                = source.byte_size;
                const cudaError_t import_result = cudaImportExternalMemory(&memory, &description);
                check_cuda(import_result, "cudaImportExternalMemory");
                cudaExternalMemoryBufferDesc buffer{};
                buffer.size = source.byte_size;
                const cudaError_t mapping_result = cudaExternalMemoryGetMappedBuffer(&data, memory, &buffer);
                if (mapping_result == cudaSuccess) return;
                cudaDestroyExternalMemory(memory);
                memory = nullptr;
                check_cuda(mapping_result, "cudaExternalMemoryGetMappedBuffer");
            }

            ~ImportedBuffer() {
                if (data) cudaFree(data);
                if (memory) cudaDestroyExternalMemory(memory);
            }

            ImportedBuffer(ImportedBuffer&& other) noexcept : memory(std::exchange(other.memory, nullptr)), data(std::exchange(other.data, nullptr)), byte_size(std::exchange(other.byte_size, 0u)) {}

            ImportedBuffer& operator=(ImportedBuffer&& other) noexcept {
                if (this == &other) return *this;
                ImportedBuffer replacement{std::move(other)};
                std::swap(memory, replacement.memory);
                std::swap(data, replacement.data);
                std::swap(byte_size, replacement.byte_size);
                return *this;
            }

            ImportedBuffer(const ImportedBuffer&)            = delete;
            ImportedBuffer& operator=(const ImportedBuffer&) = delete;
        };

        struct OutputState {
            std::string id{};
            OutputKind kind{};
            MeshAttribute mesh_attributes{};
            std::vector<std::string> field_ids{};
            std::vector<FieldKind> field_kinds{};
            std::vector<std::size_t> field_buffer_offsets{};
            UInt3 resolution{};
            std::uint32_t primary_capacity{};
            std::uint32_t secondary_capacity{};
            std::vector<ImportedBuffer> fixed_buffers{};
            std::vector<std::vector<ImportedBuffer>> slots{};
            std::uint32_t* current_slot{};
        };

        struct State {
            SpectraSdkSetupSink sink{};
            cudaExternalSemaphore_t timeline{};
            std::vector<OutputState> outputs{};
            std::vector<std::string> metric_ids{};
            std::vector<MetricValue> metric_values{};
            std::vector<void*> metric_staging{};
            std::uint64_t ready_value{};
            std::uint32_t next_slot{};
            cudaStream_t stream{};
            SpectraSdkFrameCommit* commit{};
            std::vector<SpectraSdkOutputCommit> commits{};

            ~State() {
                for (void* staging : metric_staging)
                    if (staging) cudaFreeHost(staging);
                if (timeline) cudaDestroyExternalSemaphore(timeline);
            }
        };

        [[nodiscard]] State& setup_state(void* source) {
            return *static_cast<State*>(source);
        }

        [[nodiscard]] OutputState& output_state(State& state, const std::string_view id) {
            return *std::ranges::find(state.outputs, id, &OutputState::id);
        }

        [[nodiscard]] std::size_t output_index(const State& state, const OutputState& output) noexcept {
            return static_cast<std::size_t>(&output - state.outputs.data());
        }

        void import_timeline(State& state) {
            cudaExternalSemaphoreHandleDesc description{};
            description.type                = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
            description.handle.win32.handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(state.sink.timeline_semaphore.value));
            const cudaError_t result = cudaImportExternalSemaphore(&state.timeline, &description);
            check_cuda(result, "cudaImportExternalSemaphore");
        }

        void import_configuration(OutputState& output, const SpectraSdkOutputConfiguration& configuration) {
            output.fixed_buffers.reserve(configuration.static_buffer_count);
            for (std::uint64_t index = 0; index != configuration.static_buffer_count; ++index) output.fixed_buffers.emplace_back(configuration.static_buffers[index]);
            output.slots.resize(configuration.slot_count);
            for (std::uint64_t slot_index = 0; slot_index != configuration.slot_count; ++slot_index) {
                const SpectraSdkGpuSlot& source = configuration.slots[slot_index];
                output.slots[slot_index].reserve(source.buffer_count);
                for (std::uint64_t buffer_index = 0; buffer_index != source.buffer_count; ++buffer_index) output.slots[slot_index].emplace_back(source.buffers[buffer_index]);
            }
        }

        void request_output(State& state, OutputState& output, const SpectraSdkOutputLayout& layout) {
            SpectraSdkOutputRequest request{};
            const SpectraSdkResult result = state.sink.configure_output(state.sink.context, &layout, &request);
            if (result.error.size != 0u) throw std::runtime_error(std::string{result.error.data, result.error.size});
            try {
                import_configuration(output, request.configuration);
            } catch (...) {
                state.sink.release_output(request.lifetime);
                throw;
            }
            state.sink.release_output(request.lifetime);
        }

        [[nodiscard]] SpectraSdkOutputKind abi_kind(const OutputKind kind) noexcept {
            return static_cast<SpectraSdkOutputKind>(kind);
        }

        [[nodiscard]] RawView raw_view(const ImportedBuffer& buffer, const std::uint64_t element_size) noexcept {
            return {buffer.data, buffer.byte_size / element_size};
        }
    }

    Setup::Setup(const void* sink) {
        std::unique_ptr<State> value{new State{.sink = *static_cast<const SpectraSdkSetupSink*>(sink)}};
        import_timeline(*value);
        state = value.release();
    }

    Setup::~Setup() {
        delete static_cast<State*>(state);
    }

    Setup::Setup(Setup&& other) noexcept : state(std::exchange(other.state, nullptr)) {}

    Setup& Setup::operator=(Setup&& other) noexcept {
        if (this == &other) return *this;
        delete static_cast<State*>(state);
        state = std::exchange(other.state, nullptr);
        return *this;
    }

    void register_output_internal(void* source, const std::string_view id, const OutputKind kind, const MeshAttribute attributes, const std::span<const std::string_view> field_ids, const std::span<const FieldKind> field_kinds) {
        State& state              = setup_state(source);
        OutputState& output       = state.outputs.emplace_back();
        output.id                 = id;
        output.kind               = kind;
        output.mesh_attributes    = attributes;
        output.field_ids.assign(field_ids.begin(), field_ids.end());
        output.field_kinds.assign(field_kinds.begin(), field_kinds.end());
        output.field_buffer_offsets.reserve(field_kinds.size());
        std::size_t buffer_offset{};
        for (const FieldKind field_kind : field_kinds) {
            output.field_buffer_offsets.push_back(buffer_offset);
            buffer_offset += field_kind == FieldKind::MacFloat3 ? 3u : 1u;
        }
        output.current_slot = &state.next_slot;
    }

    void configure_metrics_internal(void* source, const std::span<const std::string_view> ids) {
        State& state = setup_state(source);
        state.metric_ids.reserve(ids.size());
        for (const std::string_view id : ids) state.metric_ids.emplace_back(id);
        state.metric_values.resize(ids.size());
    }

    RawMeshSetupView setup_mesh_internal(void* source, const std::string_view id, const std::uint32_t vertex_capacity, const std::uint32_t triangle_capacity) {
        State& state              = setup_state(source);
        OutputState& output       = output_state(state, id);
        output.primary_capacity   = vertex_capacity;
        output.secondary_capacity = triangle_capacity;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(output.kind), vertex_capacity, triangle_capacity, {}, std::to_underlying(output.mesh_attributes)};
        request_output(state, output, layout);
        const RawView triangles = triangle_capacity == 0u ? RawView{} : raw_view(output.fixed_buffers[0], sizeof(std::uint32_t));
        const bool has_uv        = contains(output.mesh_attributes, MeshAttribute::TextureCoordinate);
        return {triangles, has_uv ? raw_view(output.fixed_buffers[triangle_capacity == 0u ? 0u : 1u], sizeof(Float2)) : RawView{}};
    }

    RawCamerasSetupView setup_cameras_internal(void* source, const std::string_view id, const std::span<const Camera> cameras, const std::uint32_t width, const std::uint32_t height) {
        State& state            = setup_state(source);
        OutputState& output     = output_state(state, id);
        output.resolution       = {width, height, static_cast<std::uint32_t>(cameras.size())};
        output.primary_capacity = static_cast<std::uint32_t>(cameras.size());
        std::vector<SpectraSdkCamera> encoded{};
        encoded.reserve(cameras.size());
        for (const Camera& camera : cameras) encoded.emplace_back(std::bit_cast<SpectraSdkCamera>(camera));
        const SpectraSdkOutputLayout layout{
            output_index(state, output),
            abi_kind(output.kind),
            output.primary_capacity,
            0u,
            {width, height, output.primary_capacity},
            0u,
            0.0f,
            encoded.data(),
            encoded.size(),
        };
        request_output(state, output, layout);
        return {raw_view(output.fixed_buffers[0], sizeof(Rgba8)), output.resolution};
    }

    void setup_collection_internal(void* source, const std::string_view id, const OutputKind kind, const std::uint32_t capacity) {
        State& state            = setup_state(source);
        OutputState& output     = output_state(state, id);
        output.primary_capacity = capacity;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(kind), capacity};
        request_output(state, output, layout);
    }

    void setup_particles_internal(void* source, const std::string_view id, const std::uint32_t capacity, const float radius) {
        State& state            = setup_state(source);
        OutputState& output     = output_state(state, id);
        output.primary_capacity = capacity;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(output.kind), capacity, 0u, {}, 0u, radius};
        request_output(state, output, layout);
    }

    void setup_volume_internal(void* source, const std::string_view id, const UInt3 resolution) {
        State& state            = setup_state(source);
        OutputState& output     = output_state(state, id);
        output.resolution       = resolution;
        output.primary_capacity = resolution.x * resolution.y * resolution.z;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(output.kind), output.primary_capacity, 0u, {resolution.x, resolution.y, resolution.z}};
        request_output(state, output, layout);
    }

    void setup_image_internal(void* source, const std::string_view id, const UInt3 extent) {
        State& state            = setup_state(source);
        OutputState& output     = output_state(state, id);
        output.resolution       = extent;
        output.primary_capacity = extent.x * extent.y;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(output.kind), output.primary_capacity, 0u, {extent.x, extent.y, 1u}};
        request_output(state, output, layout);
    }

    void setup_hash_grid_radiance_field_internal(void* source, const std::string_view id) {
        State& state          = setup_state(source);
        OutputState& output   = output_state(state, id);
        output.primary_capacity = SPECTRA_SDK_HASH_GRID_ENTRY_COUNT;
        const SpectraSdkOutputLayout layout{output_index(state, output), abi_kind(output.kind), output.primary_capacity};
        request_output(state, output, layout);
    }

    void Setup::complete() {
        State& state = setup_state(this->state);
        if (!state.metric_ids.empty()) {
            OutputState& output     = state.outputs.emplace_back();
            output.id               = "__metrics";
            output.kind             = static_cast<OutputKind>(SpectraSdkOutputKind::Metrics);
            output.primary_capacity = static_cast<std::uint32_t>(state.metric_ids.size());
            const SpectraSdkOutputLayout layout{output_index(state, output), SpectraSdkOutputKind::Metrics, output.primary_capacity};
            request_output(state, output, layout);
            state.metric_staging.resize(state.sink.slot_count);
            const std::uint64_t bytes = state.metric_ids.size() * sizeof(MetricValue);
            for (void*& staging : state.metric_staging) check_cuda(cudaHostAlloc(&staging, bytes, cudaHostAllocWriteCombined), "cudaHostAlloc");
        }
        state.commits.resize(state.outputs.size());
        check_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize setup");
    }

    Output::Output(Setup& setup) noexcept : state(setup.state) {
        setup.state = nullptr;
    }

    Output::~Output() {
        delete static_cast<State*>(state);
    }

    Output::Output(Output&& other) noexcept : state(std::exchange(other.state, nullptr)) {}

    Output& Output::operator=(Output&& other) noexcept {
        if (this == &other) return *this;
        delete static_cast<State*>(state);
        state = std::exchange(other.state, nullptr);
        return *this;
    }

    void Output::prepare(void* commit) const noexcept {
        setup_state(state).commit = static_cast<SpectraSdkFrameCommit*>(commit);
    }

    Frame Output::begin(void* stream) const {
        State& state = setup_state(this->state);
        state.stream = static_cast<cudaStream_t>(stream);
        if (state.ready_value != 0u) {
            cudaExternalSemaphoreWaitParams parameters{};
            parameters.params.fence.value = state.ready_value + 1u;
            check_cuda(cudaWaitExternalSemaphoresAsync(&state.timeline, &parameters, 1u, state.stream), "cudaWaitExternalSemaphoresAsync");
        }
        state.commits.assign(state.outputs.size(), {});
        return {this->state};
    }

    RawMeshView frame_mesh_internal(void* source, const std::string_view id) {
        State& state          = setup_state(source);
        OutputState& output   = output_state(state, id);
        const auto& buffers   = output.slots[state.next_slot];
        std::size_t index{};
        const RawView positions = raw_view(buffers[index++], sizeof(Float3));
        const RawView normals   = contains(output.mesh_attributes, MeshAttribute::Normal) ? raw_view(buffers[index++], sizeof(Float3)) : RawView{};
        const RawView tangents  = contains(output.mesh_attributes, MeshAttribute::Tangent) ? raw_view(buffers[index++], sizeof(Float3)) : RawView{};
        const RawView colors    = contains(output.mesh_attributes, MeshAttribute::Color) ? raw_view(buffers[index++], sizeof(Float4)) : RawView{};
        const RawView scalars   = contains(output.mesh_attributes, MeshAttribute::Scalar) ? raw_view(buffers[index++], sizeof(float)) : RawView{};
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count             = output.primary_capacity;
        commit.secondary_count          = output.secondary_capacity;
        return {positions, normals, tangents, colors, scalars, &commit.active_count, &commit.secondary_count};
    }

    RawView frame_collection_internal(void* source, const std::string_view id, const std::uint32_t active_count) {
        State& state          = setup_state(source);
        OutputState& output   = output_state(state, id);
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count             = active_count;
        return raw_view(output.slots[state.next_slot][0], output.kind == OutputKind::Spheres ? sizeof(Sphere) : output.kind == OutputKind::Instances ? sizeof(Instance) : output.kind == OutputKind::Lines ? sizeof(Line) : sizeof(Vector));
    }

    RawParticlesView frame_particles_internal(void* source, const std::string_view id, const std::uint32_t active_count) {
        State& state          = setup_state(source);
        OutputState& output   = output_state(state, id);
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count             = active_count;
        return {&output, raw_view(output.slots[state.next_slot][0], sizeof(Float3))};
    }

    RawVolumeView frame_volume_internal(void* source, const std::string_view id) {
        State& state        = setup_state(source);
        OutputState& output = output_state(state, id);
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count             = output.primary_capacity;
        return {&output, output.resolution};
    }

    RawImageView frame_image_internal(void* source, const std::string_view id) {
        State& state        = setup_state(source);
        OutputState& output = output_state(state, id);
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count             = output.primary_capacity;
        return {raw_view(output.slots[state.next_slot][0], sizeof(Float4)), output.resolution};
    }

    RawHashGridRadianceFieldView frame_hash_grid_radiance_field_internal(void* source, const std::string_view id) {
        State& state        = setup_state(source);
        OutputState& output = output_state(state, id);
        const auto& buffers = output.slots[state.next_slot];
        SpectraSdkOutputCommit& commit = state.commits[output_index(state, output)];
        commit.active_count = output.primary_capacity;
        return {
            raw_view(buffers[0], sizeof(Half4)),
            raw_view(buffers[1], sizeof(Half)),
            raw_view(buffers[2], sizeof(Half)),
            raw_view(buffers[3], sizeof(Half)),
            raw_view(buffers[4], sizeof(Half)),
            raw_view(buffers[5], sizeof(Half)),
            raw_view(buffers[6], sizeof(std::uint32_t)),
        };
    }

    RawView field_internal(void* source, const std::string_view id) {
        OutputState& output = *static_cast<OutputState*>(source);
        const auto found = std::ranges::find(output.field_ids, id);
        const std::size_t index = static_cast<std::size_t>(std::distance(output.field_ids.begin(), found));
        const std::size_t offset = output.field_buffer_offsets[index] + (output.kind == OutputKind::Particles ? 1u : 0u);
        const std::uint64_t element_size = output.field_kinds[index] == FieldKind::Float ? sizeof(float) : output.field_kinds[index] == FieldKind::Float3 ? sizeof(Float3) : sizeof(std::uint32_t);
        return raw_view(output.slots[*output.current_slot][offset], element_size);
    }

    RawMacFieldView volume_mac_field_internal(void* source, const std::string_view id) {
        OutputState& output = *static_cast<OutputState*>(source);
        const auto found = std::ranges::find(output.field_ids, id);
        const std::size_t index = static_cast<std::size_t>(std::distance(output.field_ids.begin(), found));
        const std::size_t offset = output.field_buffer_offsets[index];
        const auto& buffers = output.slots[*output.current_slot];
        return {
            raw_view(buffers[offset], sizeof(float)),
            raw_view(buffers[offset + 1u], sizeof(float)),
            raw_view(buffers[offset + 2u], sizeof(float)),
            {output.resolution.x + 1u, output.resolution.y, output.resolution.z},
            {output.resolution.x, output.resolution.y + 1u, output.resolution.z},
            {output.resolution.x, output.resolution.y, output.resolution.z + 1u},
        };
    }

    void upload_metric_internal(void* source, const std::string_view id, const MetricValue& value) {
        State& state       = setup_state(source);
        const auto found   = std::ranges::find(state.metric_ids, id);
        state.metric_values[static_cast<std::size_t>(std::distance(state.metric_ids.begin(), found))] = value;
    }

    void Frame::commit() {
        State& state = setup_state(this->state);
        if (!state.metric_ids.empty()) {
            std::memcpy(state.metric_staging[state.next_slot], state.metric_values.data(), state.metric_values.size() * sizeof(MetricValue));
            OutputState& output = state.outputs.back();
            check_cuda(cudaMemcpyAsync(output.slots[state.next_slot][0].data, state.metric_staging[state.next_slot], state.metric_values.size() * sizeof(MetricValue), cudaMemcpyHostToDevice, state.stream), "cudaMemcpyAsync metrics");
            state.commits.back().active_count = static_cast<std::uint32_t>(state.metric_values.size());
        }
        state.ready_value += state.ready_value == 0u ? 1u : 2u;
        cudaExternalSemaphoreSignalParams parameters{};
        parameters.params.fence.value = state.ready_value;
        check_cuda(cudaSignalExternalSemaphoresAsync(&state.timeline, &parameters, 1u, state.stream), "cudaSignalExternalSemaphoresAsync");
        *state.commit = {state.next_slot, state.ready_value, state.commits.data()};
        state.next_slot = (state.next_slot + 1u) % state.sink.slot_count;
    }

    void Output::synchronize() const {
        if (!state) return;
        check_cuda(cudaStreamSynchronize(setup_state(state).stream), "cudaStreamSynchronize");
    }

}
