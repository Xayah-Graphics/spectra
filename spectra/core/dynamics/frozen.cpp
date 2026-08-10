module spectra.dynamics.frozen;

import std;
import vulkan;

namespace spectra::dynamics {
    namespace {
        struct Writer {
            std::vector<std::byte> data{};

            template <class Value>
            void value(const Value& source) {
                const std::span bytes = std::as_bytes(std::span{&source, 1});
                this->data.insert(this->data.end(), bytes.begin(), bytes.end());
            }

            void string(const std::string_view source) {
                this->value(static_cast<std::uint64_t>(source.size()));
                const std::span bytes = std::as_bytes(std::span{source});
                this->data.insert(this->data.end(), bytes.begin(), bytes.end());
            }

            void bytes(const std::span<const std::byte> source) {
                this->value(static_cast<std::uint64_t>(source.size()));
                this->data.insert(this->data.end(), source.begin(), source.end());
            }
        };

        struct Reader {
            std::span<const std::byte> data{};
            std::size_t offset{};

            template <class Value>
            [[nodiscard]] Value value() {
                Value result{};
                std::memcpy(&result, this->data.data() + this->offset, sizeof(Value));
                this->offset += sizeof(Value);
                return result;
            }

            [[nodiscard]] std::string string() {
                const std::uint64_t size = this->value<std::uint64_t>();
                std::string result(size, '\0');
                std::memcpy(result.data(), this->data.data() + this->offset, size);
                this->offset += size;
                return result;
            }

            [[nodiscard]] std::vector<std::byte> bytes() {
                const std::uint64_t size = this->value<std::uint64_t>();
                std::vector<std::byte> result(size);
                std::memcpy(result.data(), this->data.data() + this->offset, size);
                this->offset += size;
                return result;
            }
        };

        void write_view(Writer& writer, const scene::DynamicVisualizationView& view) {
            writer.string(view.dataset_id);
            writer.string(view.name);
            writer.value(view.kind);
            writer.value(view.depth_mode);
            writer.value(view.composition_domain);
            writer.value(view.anchor);
            writer.string(view.channel_id);
            writer.value(view.color);
            writer.value(view.screen_rect);
            writer.value(view.width);
            writer.value(view.scale);
            writer.value(view.slice_position);
            writer.value(view.scalar_minimum);
            writer.value(view.scalar_maximum);
            writer.value(view.sampling);
            writer.value(view.slice_axis);
            writer.value(view.distortion_iterations);
            writer.value(view.distortion_tolerance);
            writer.value(view.point_glyph);
            writer.value(view.point_shading);
            writer.value(view.color_source);
            writer.value(view.color_map);
            writer.value(view.visible);
        }

        [[nodiscard]] scene::DynamicVisualizationView read_view(Reader& reader) {
            scene::DynamicVisualizationView view{};
            view.dataset_id             = reader.string();
            view.name                   = reader.string();
            view.kind                   = reader.value<scene::VisualizationViewKind>();
            view.depth_mode             = reader.value<scene::VisualizationDepthMode>();
            view.composition_domain     = reader.value<scene::VisualizationCompositionDomain>();
            view.anchor                 = reader.value<scene::InstanceId>();
            view.channel_id             = reader.string();
            view.color                  = reader.value<math::Float4>();
            view.screen_rect            = reader.value<math::Float4>();
            view.width                  = reader.value<float>();
            view.scale                  = reader.value<float>();
            view.slice_position         = reader.value<float>();
            view.scalar_minimum         = reader.value<float>();
            view.scalar_maximum         = reader.value<float>();
            view.sampling               = reader.value<std::uint32_t>();
            view.slice_axis             = reader.value<std::uint32_t>();
            view.distortion_iterations  = reader.value<std::uint32_t>();
            view.distortion_tolerance   = reader.value<float>();
            view.point_glyph            = reader.value<scene::PointGlyph>();
            view.point_shading          = reader.value<scene::PointShading>();
            view.color_source           = reader.value<scene::VisualizationColorSource>();
            view.color_map              = reader.value<scene::VisualizationColorMap>();
            view.visible                = reader.value<bool>();
            return view;
        }

        void write_telemetry_value(Writer& writer, const TelemetryValue& value) {
            writer.value(value.kind);
            writer.value(value.integer);
            writer.value(value.floating);
        }

        [[nodiscard]] TelemetryValue read_telemetry_value(Reader& reader) {
            return {reader.value<TelemetryKind>(), reader.value<std::int64_t>(), reader.value<std::array<double, 3>>()};
        }

        void write_image(Writer& writer, const ImageDataset& image) {
            writer.value(image.extent);
            writer.value(image.format);
            writer.value(image.color_space);
            writer.value(image.transfer_function);
        }

        [[nodiscard]] ImageDataset read_image(Reader& reader) {
            return {reader.value<std::array<std::uint32_t, 2>>(), reader.value<ImageFormat>(), reader.value<scene::SpectrumColorSpace>(), reader.value<TransferFunction>()};
        }
    } // namespace

    std::vector<std::byte> serialize_frozen_frame(const FrozenFrame& frame) {
        Writer writer{};
        writer.value(std::array<char, 8>{'S', 'P', 'D', 'Y', 'N', '0', '0', '1'});
        writer.value(std::uint32_t{1});
        writer.value(frame.simulation);
        writer.value(frame.presentation);
        writer.value(static_cast<std::uint64_t>(frame.bounds.size()));
        for (const FrozenBounds& bounds : frame.bounds) {
            writer.value(bounds.domain);
            writer.bytes(std::as_bytes(std::span{bounds.values}));
        }
        writer.value(static_cast<std::uint64_t>(frame.visualizations.size()));
        for (const FrozenVisualization& visualization : frame.visualizations) {
            writer.value(visualization.kind);
            write_view(writer, visualization.style.view);
            writer.value(visualization.style.transform);
            writer.value(visualization.resolution);
            writer.value(visualization.local_from_grid);
            writer.string(visualization.channel.id);
            writer.value(visualization.channel.kind);
            write_image(writer, visualization.image);
            writer.value(visualization.camera_observations.capacity);
            write_image(writer, visualization.camera_observations.images);
            writer.value(visualization.primary_count);
            writer.value(visualization.secondary_count);
            writer.value(static_cast<std::uint64_t>(visualization.buffers.size()));
            for (const std::vector<std::byte>& buffer : visualization.buffers) writer.bytes(buffer);
        }
        writer.value(static_cast<std::uint64_t>(frame.telemetry.size()));
        for (const FrozenTelemetrySystem& system : frame.telemetry) {
            writer.string(system.id);
            writer.string(system.name);
            writer.string(system.provider_id);
            writer.value(static_cast<std::uint64_t>(system.descriptors.size()));
            for (const TelemetryDescriptor& descriptor : system.descriptors) {
                writer.string(descriptor.id);
                writer.string(descriptor.name);
                writer.string(descriptor.unit);
                writer.string(descriptor.section_id);
                writer.value(descriptor.kind);
                writer.value(descriptor.plot);
            }
            writer.string(system.snapshot.phase);
            writer.string(system.snapshot.headline);
            writer.string(system.snapshot.message);
            writer.value(static_cast<std::uint64_t>(system.snapshot.values.size()));
            for (const std::optional<TelemetryValue>& value : system.snapshot.values) {
                writer.value(value.has_value());
                if (value) write_telemetry_value(writer, *value);
            }
            writer.value(static_cast<std::uint64_t>(system.snapshot.history.size()));
            for (const TelemetrySample& sample : system.snapshot.history) {
                writer.value(sample.simulation_step);
                writer.value(sample.simulation_seconds);
                writer.value(static_cast<std::uint64_t>(sample.values.size()));
                for (const TelemetryValue& value : sample.values) write_telemetry_value(writer, value);
            }
        }
        return std::move(writer.data);
    }

    FrozenFrame deserialize_frozen_frame(const std::span<const std::byte> payload) {
        Reader reader{payload};
        if (reader.value<std::array<char, 8>>() != std::array<char, 8>{'S', 'P', 'D', 'Y', 'N', '0', '0', '1'} || reader.value<std::uint32_t>() != 1) throw std::runtime_error("Invalid Spectra Frozen Dynamic Frame header");
        FrozenFrame frame{reader.value<SimulationTimeline>(), reader.value<PresentationTimeline>()};
        frame.bounds.resize(reader.value<std::uint64_t>());
        for (FrozenBounds& bounds : frame.bounds) {
            bounds.domain = reader.value<BoundsDomain>();
            const std::vector<std::byte> values = reader.bytes();
            bounds.values.resize(values.size() / sizeof(SceneBound));
            std::memcpy(bounds.values.data(), values.data(), values.size());
        }
        frame.visualizations.resize(reader.value<std::uint64_t>());
        for (FrozenVisualization& visualization : frame.visualizations) {
            visualization.kind                      = reader.value<FrozenVisualizationKind>();
            visualization.style.view                = read_view(reader);
            visualization.style.transform           = reader.value<math::Transform>();
            visualization.resolution                = reader.value<math::UInt3>();
            visualization.local_from_grid           = reader.value<math::Transform>();
            visualization.channel.id                = reader.string();
            visualization.channel.kind              = reader.value<FieldChannelKind>();
            visualization.image                     = read_image(reader);
            visualization.camera_observations       = {reader.value<std::uint64_t>(), read_image(reader)};
            visualization.primary_count             = reader.value<std::uint64_t>();
            visualization.secondary_count           = reader.value<std::uint64_t>();
            visualization.buffers.resize(reader.value<std::uint64_t>());
            for (std::vector<std::byte>& buffer : visualization.buffers) buffer = reader.bytes();
        }
        frame.telemetry.resize(reader.value<std::uint64_t>());
        for (FrozenTelemetrySystem& system : frame.telemetry) {
            system.id          = reader.string();
            system.name        = reader.string();
            system.provider_id = reader.string();
            system.descriptors.resize(reader.value<std::uint64_t>());
            for (TelemetryDescriptor& descriptor : system.descriptors) {
                descriptor.id         = reader.string();
                descriptor.name       = reader.string();
                descriptor.unit       = reader.string();
                descriptor.section_id = reader.string();
                descriptor.kind       = reader.value<TelemetryKind>();
                descriptor.plot       = reader.value<bool>();
            }
            system.snapshot.phase    = reader.string();
            system.snapshot.headline = reader.string();
            system.snapshot.message  = reader.string();
            system.snapshot.values.resize(reader.value<std::uint64_t>());
            for (std::optional<TelemetryValue>& value : system.snapshot.values)
                if (reader.value<bool>()) value = read_telemetry_value(reader);
            const std::uint64_t history_size = reader.value<std::uint64_t>();
            for (std::uint64_t index = 0; index != history_size; ++index) {
                TelemetrySample sample{reader.value<std::uint64_t>(), reader.value<double>()};
                sample.values.resize(reader.value<std::uint64_t>());
                for (TelemetryValue& value : sample.values) value = read_telemetry_value(reader);
                system.snapshot.history.push_back(std::move(sample));
            }
        }
        return frame;
    }

    void write_telemetry(const std::filesystem::path& path, const FrozenFrame& frame) {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::trunc};
        stream << "system,metric,unit,kind,value\n";
        for (const FrozenTelemetrySystem& system : frame.telemetry)
            for (std::size_t index = 0; index != system.descriptors.size(); ++index) {
                const TelemetryDescriptor& descriptor = system.descriptors[index];
                stream << system.id << ',' << descriptor.id << ',' << descriptor.unit << ',' << static_cast<std::uint32_t>(descriptor.kind) << ',';
                if (index < system.snapshot.values.size() && system.snapshot.values[index]) {
                    const TelemetryValue& value = *system.snapshot.values[index];
                    if (value.kind == TelemetryKind::Boolean || value.kind == TelemetryKind::Integer)
                        stream << value.integer;
                    else if (value.kind == TelemetryKind::Float)
                        stream << value.floating[0];
                    else
                        stream << '"' << value.floating[0] << ' ' << value.floating[1] << ' ' << value.floating[2] << '"';
                }
                stream << '\n';
            }
        if (!stream) throw std::runtime_error(std::format("Failed to write Telemetry output: {}", path.string()));
    }

    FrozenFrameRuntime::FrozenFrameRuntime(VulkanRuntime& runtime) noexcept : runtime{runtime} {}

    FrozenFrameRuntime::~FrozenFrameRuntime() {
        this->destroy();
    }

    void FrozenFrameRuntime::initialize(const std::span<const std::byte> payload) {
        FrozenFrame next_data = deserialize_frozen_frame(payload);
        std::deque<Buffer> next_buffers{};
        DynamicFrame next_gpu_frame{};
        std::vector<GpuVisualization> next_gpu_visualizations{};
        std::uint64_t total_size{};
        for (const FrozenBounds& bounds : next_data.bounds) total_size = (total_size + 15u & ~std::uint64_t{15u}) + std::max<std::size_t>(bounds.values.size() * sizeof(SceneBound), 4u);
        for (const FrozenVisualization& visualization : next_data.visualizations)
            for (const std::vector<std::byte>& buffer : visualization.buffers) total_size = (total_size + 15u & ~std::uint64_t{15u}) + std::max<std::size_t>(buffer.size(), 4u);
        GpuBuffer staging = this->runtime.resources.create_buffer(std::max<std::uint64_t>(total_size, 4u), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        struct Copy {
            vk::Buffer destination{};
            vk::BufferCopy region{};
        };
        std::vector<Copy> copies{};
        std::uint64_t offset{};
        const auto upload = [&](const std::span<const std::byte> source) -> GpuBufferView {
            offset                    = offset + 15u & ~std::uint64_t{15u};
            const std::size_t size    = std::max<std::size_t>(source.size(), 4u);
            Buffer& destination       = next_buffers.emplace_back();
            destination.gpu          = this->runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            destination.descriptor   = this->runtime.resources.allocate_resource_descriptor();
            std::memset(static_cast<std::byte*>(staging.mapped) + offset, 0, size);
            std::memcpy(static_cast<std::byte*>(staging.mapped) + offset, source.data(), source.size());
            copies.push_back({*destination.gpu.buffer, {offset, 0, size}});
            offset += size;
            this->runtime.resources.write_buffer_descriptor(destination.descriptor, vk::DescriptorType::eStorageBuffer, destination.gpu);
            return {&destination.gpu, destination.descriptor};
        };
        next_gpu_frame.simulation   = next_data.simulation;
        next_gpu_frame.presentation = next_data.presentation;
        for (const FrozenBounds& bounds : next_data.bounds) next_gpu_frame.scene_updates.push_back({GpuSceneBoundsUpdate{upload(std::as_bytes(std::span{bounds.values})), bounds.values.size(), bounds.domain}});
        for (const FrozenVisualization& visualization : next_data.visualizations) {
            std::vector<GpuBufferView> views{};
            views.reserve(visualization.buffers.size());
            for (const std::vector<std::byte>& buffer : visualization.buffers) views.push_back(upload(buffer));
            switch (visualization.kind) {
            case FrozenVisualizationKind::Points: next_gpu_visualizations.push_back({GpuPointVisualization{visualization.style, views[0], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Segments: next_gpu_visualizations.push_back({GpuSegmentVisualization{visualization.style, views[0], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Curves: next_gpu_visualizations.push_back({GpuCurveVisualization{visualization.style, views[0], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Vectors: next_gpu_visualizations.push_back({GpuVectorVisualization{visualization.style, views[0], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Field: next_gpu_visualizations.push_back({GpuFieldVisualization{visualization.style, visualization.resolution, visualization.local_from_grid, {visualization.channel, views[0]}}}); break;
            case FrozenVisualizationKind::Image: next_gpu_visualizations.push_back({GpuImageVisualization{visualization.style, visualization.image, views[0]}}); break;
            case FrozenVisualizationKind::CameraObservations: next_gpu_visualizations.push_back({GpuCameraObservationVisualization{visualization.style, visualization.camera_observations, views[0], views[1], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Transforms: next_gpu_visualizations.push_back({GpuTransformVisualization{visualization.style, views[0], visualization.primary_count}}); break;
            case FrozenVisualizationKind::Surface: next_gpu_visualizations.push_back({GpuSurfaceVisualization{visualization.style, views[0], views[1], views[2], visualization.primary_count, visualization.secondary_count}}); break;
            }
        }
        if (!copies.empty()) this->runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
            for (const Copy& copy : copies) command_buffer.copyBuffer(*staging.buffer, copy.destination, copy.region);
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        });
        this->destroy();
        this->data = std::move(next_data);
        this->buffers.swap(next_buffers);
        this->gpu_frame          = std::move(next_gpu_frame);
        this->gpu_visualizations = std::move(next_gpu_visualizations);
        this->ready              = true;
    }

    void FrozenFrameRuntime::destroy() noexcept {
        if (!this->ready) return;
        this->runtime.frames.defer_destruction([buffers = std::move(this->buffers)]() mutable {});
        this->gpu_visualizations.clear();
        this->gpu_frame = {};
        this->data  = {};
        this->ready = false;
    }

    bool FrozenFrameRuntime::initialized() const noexcept {
        return this->ready;
    }

    std::span<const GpuVisualization> FrozenFrameRuntime::visualizations() const noexcept {
        return this->gpu_visualizations;
    }

    const FrozenFrame& FrozenFrameRuntime::frame() const noexcept {
        return this->data;
    }
} // namespace spectra::dynamics
