module;

#include <spectra/plugin_api.h>

module spectra.dynamics.frozen;

import std;
import vulkan;

namespace spectra::dynamics {
    namespace {
        struct Writer {
            std::vector<std::byte> data{};

            template <std::integral Value>
            void integer(const Value source) {
                const std::make_unsigned_t<Value> bits = static_cast<std::make_unsigned_t<Value>>(source);
                for (std::size_t index = 0; index != sizeof(Value); ++index) this->data.push_back(static_cast<std::byte>(bits >> index * 8u));
            }

            void floating(const float source) {
                this->integer(std::bit_cast<std::uint32_t>(source));
            }
            void floating(const double source) {
                this->integer(std::bit_cast<std::uint64_t>(source));
            }
            void boolean(const bool source) {
                this->integer<std::uint8_t>(source ? 1 : 0);
            }

            template <class Value>
            void enumeration(const Value source) {
                this->integer(std::to_underlying(source));
            }

            void string(const std::string_view source) {
                this->integer(static_cast<std::uint64_t>(source.size()));
                const std::span bytes = std::as_bytes(std::span{source});
                this->data.insert(this->data.end(), bytes.begin(), bytes.end());
            }

            void bytes(const std::span<const std::byte> source) {
                this->integer(static_cast<std::uint64_t>(source.size()));
                this->data.insert(this->data.end(), source.begin(), source.end());
            }

            void float4(const math::Float4 source) {
                this->floating(source.x);
                this->floating(source.y);
                this->floating(source.z);
                this->floating(source.w);
            }

            void uint3(const math::UInt3 source) {
                this->integer(source.x);
                this->integer(source.y);
                this->integer(source.z);
            }

            void transform(const math::Transform& source) {
                for (const float component : source.matrix) this->floating(component);
            }
        };

        struct Reader {
            std::span<const std::byte> data{};
            std::size_t offset{};

            void require(const std::size_t size) const {
                if (size > this->data.size() - this->offset) throw std::runtime_error("Truncated Spectra Frozen Dynamic Frame");
            }

            template <std::integral Value>
            [[nodiscard]] Value integer() {
                this->require(sizeof(Value));
                std::make_unsigned_t<Value> bits{};
                for (std::size_t index = 0; index != sizeof(Value); ++index) bits |= static_cast<std::make_unsigned_t<Value>>(std::to_integer<std::uint8_t>(this->data[this->offset++])) << index * 8u;
                return static_cast<Value>(bits);
            }

            [[nodiscard]] float floating32() {
                return std::bit_cast<float>(this->integer<std::uint32_t>());
            }
            [[nodiscard]] double floating64() {
                return std::bit_cast<double>(this->integer<std::uint64_t>());
            }

            [[nodiscard]] bool boolean() {
                const std::uint8_t value = this->integer<std::uint8_t>();
                if (value > 1) throw std::runtime_error("Invalid Boolean in Spectra Frozen Dynamic Frame");
                return value != 0;
            }

            template <class Value>
            [[nodiscard]] Value enumeration(const Value maximum) {
                const auto value = this->integer<std::underlying_type_t<Value>>();
                if (value > std::to_underlying(maximum)) throw std::runtime_error("Invalid enum in Spectra Frozen Dynamic Frame");
                return static_cast<Value>(value);
            }

            [[nodiscard]] std::size_t count() {
                const std::uint64_t size = this->integer<std::uint64_t>();
                if (size > std::numeric_limits<std::size_t>::max() || size > this->data.size() - this->offset) throw std::runtime_error("Invalid collection size in Spectra Frozen Dynamic Frame");
                return static_cast<std::size_t>(size);
            }

            [[nodiscard]] std::string string() {
                const std::size_t size = this->count();
                std::string result(size, '\0');
                std::memcpy(result.data(), this->data.data() + this->offset, size);
                this->offset += size;
                return result;
            }

            [[nodiscard]] std::vector<std::byte> bytes() {
                const std::size_t size = this->count();
                std::vector<std::byte> result(size);
                std::memcpy(result.data(), this->data.data() + this->offset, size);
                this->offset += size;
                return result;
            }

            [[nodiscard]] math::Float4 float4() {
                return {this->floating32(), this->floating32(), this->floating32(), this->floating32()};
            }
            [[nodiscard]] math::UInt3 uint3() {
                return {this->integer<std::uint32_t>(), this->integer<std::uint32_t>(), this->integer<std::uint32_t>()};
            }

            [[nodiscard]] math::Transform transform() {
                math::Transform result{};
                for (float& component : result.matrix) component = this->floating32();
                return result;
            }

            [[nodiscard]] bool finished() const noexcept {
                return this->offset == this->data.size();
            }
        };

        void write_view(Writer& writer, const scene::DynamicVisualizationView& view) {
            writer.string(view.dataset_id);
            writer.string(view.name);
            writer.enumeration(scene::visualization_view_kind(view));
            writer.enumeration(view.depth_mode);
            writer.enumeration(view.composition_domain);
            writer.integer(view.anchor.value);
            writer.float4(view.color);
            writer.boolean(view.visible);
            std::visit(
                [&writer](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointVisualization>) {
                        writer.floating(data.width);
                        writer.floating(data.scale);
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.enumeration(data.glyph);
                        writer.enumeration(data.shading);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SegmentVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::CurveVisualization>) {
                        writer.floating(data.width);
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::VectorVisualization>) {
                        writer.floating(data.width);
                        writer.floating(data.scale);
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::FieldSliceVisualization>) {
                        writer.string(data.channel_id);
                        writer.floating(data.slice_position);
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.integer(data.slice_axis);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::FieldVectorVisualization>) {
                        writer.string(data.channel_id);
                        writer.floating(data.width);
                        writer.floating(data.scale);
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.integer(data.sampling);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageVisualization>)
                        writer.float4(data.screen_rect);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::CameraObservationVisualization>) {
                        writer.float4(data.screen_rect);
                        writer.floating(data.width);
                        writer.floating(data.scale);
                        writer.integer(data.distortion_iterations);
                        writer.floating(data.distortion_tolerance);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::FrameVisualization>) {
                        writer.floating(data.width);
                        writer.floating(data.scale);
                    } else {
                        writer.floating(data.scalar_minimum);
                        writer.floating(data.scalar_maximum);
                        writer.enumeration(data.color_source);
                        writer.enumeration(data.color_map);
                    }
                },
                view.data);
        }

        [[nodiscard]] scene::DynamicVisualizationView read_view(Reader& reader) {
            scene::DynamicVisualizationView view{
                .dataset_id         = reader.string(),
                .name               = reader.string(),
                .depth_mode         = scene::VisualizationDepthMode::Tested,
                .composition_domain = scene::VisualizationCompositionDomain::DisplayReferred,
            };
            const scene::VisualizationViewKind kind = reader.enumeration(scene::VisualizationViewKind::Surface);
            view.depth_mode                         = reader.enumeration(scene::VisualizationDepthMode::Overlay);
            view.composition_domain                 = reader.enumeration(scene::VisualizationCompositionDomain::DisplayReferred);
            view.anchor                             = {reader.integer<std::uint64_t>()};
            view.color                              = reader.float4();
            view.visible                            = reader.boolean();
            switch (kind) {
            case scene::VisualizationViewKind::Points: view.data = scene::PointVisualization{reader.floating32(), reader.floating32(), reader.floating32(), reader.floating32(), reader.enumeration(scene::PointGlyph::Cross), reader.enumeration(scene::PointShading::Lit), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::Segments: view.data = scene::SegmentVisualization{reader.floating32(), reader.floating32(), reader.floating32(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::Curves: view.data = scene::CurveVisualization{reader.floating32(), reader.floating32(), reader.floating32(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::Vectors: view.data = scene::VectorVisualization{reader.floating32(), reader.floating32(), reader.floating32(), reader.floating32(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::FieldSlice: view.data = scene::FieldSliceVisualization{reader.string(), reader.floating32(), reader.floating32(), reader.floating32(), reader.integer<std::uint32_t>(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::FieldVectors: view.data = scene::FieldVectorVisualization{reader.string(), reader.floating32(), reader.floating32(), reader.floating32(), reader.floating32(), reader.integer<std::uint32_t>(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            case scene::VisualizationViewKind::Image: view.data = scene::ImageVisualization{reader.float4()}; break;
            case scene::VisualizationViewKind::CameraObservations: view.data = scene::CameraObservationVisualization{reader.float4(), reader.floating32(), reader.floating32(), reader.integer<std::uint32_t>(), reader.floating32()}; break;
            case scene::VisualizationViewKind::Frames: view.data = scene::FrameVisualization{reader.floating32(), reader.floating32()}; break;
            case scene::VisualizationViewKind::Surface: view.data = scene::SurfaceVisualization{reader.floating32(), reader.floating32(), reader.enumeration(scene::VisualizationColorSource::Scalar), reader.enumeration(scene::VisualizationColorMap::Grayscale)}; break;
            }
            return view;
        }

        void write_telemetry_value(Writer& writer, const TelemetryValue& value) {
            writer.enumeration(value.kind);
            writer.integer(value.integer);
            for (const double component : value.floating) writer.floating(component);
        }

        [[nodiscard]] TelemetryValue read_telemetry_value(Reader& reader) {
            return {reader.enumeration(TelemetryKind::Float3), reader.integer<std::int64_t>(), {reader.floating64(), reader.floating64(), reader.floating64()}};
        }

        void write_image(Writer& writer, const ImageDataset& image) {
            writer.integer(image.extent[0]);
            writer.integer(image.extent[1]);
            writer.enumeration(image.format);
            writer.enumeration(image.color_space);
            writer.enumeration(image.transfer_function);
        }

        [[nodiscard]] ImageDataset read_image(Reader& reader) {
            return {{reader.integer<std::uint32_t>(), reader.integer<std::uint32_t>()}, reader.enumeration(ImageFormat::Rgba32Float), reader.enumeration(scene::SpectrumColorSpace::Aces2065_1), reader.enumeration(TransferFunction::Srgb)};
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

        [[nodiscard]] std::size_t checked_byte_size(const std::uint64_t count, const std::size_t element_size) {
            if (count > std::numeric_limits<std::size_t>::max() / element_size) throw std::runtime_error("Frozen Visualization payload size overflows");
            return static_cast<std::size_t>(count) * element_size;
        }

        [[nodiscard]] std::size_t image_byte_size(const ImageDataset& image, const std::uint32_t layers = 1) {
            const std::size_t element_size = image.format == ImageFormat::Rgba8Unorm ? 4u : image.format == ImageFormat::Rgba16Float ? 8u : 16u;
            const std::uint64_t pixels     = static_cast<std::uint64_t>(image.extent[0]) * image.extent[1] * layers;
            return checked_byte_size(pixels, element_size);
        }

        void validate_frozen_frame(const FrozenFrame& frame) {
            for (const FrozenVisualization& visualization : frame.visualizations) {
                const scene::VisualizationViewKind kind = scene::visualization_view_kind(visualization.style.view);
                std::visit(
                    [kind](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenElements>) {
                            const std::size_t element_size = kind == scene::VisualizationViewKind::Points ? sizeof(SpectraPluginPoint) : kind == scene::VisualizationViewKind::Segments ? sizeof(SpectraPluginSegment) : kind == scene::VisualizationViewKind::Curves ? sizeof(SpectraPluginCurve) : kind == scene::VisualizationViewKind::Vectors ? sizeof(SpectraPluginVector) : kind == scene::VisualizationViewKind::Frames ? sizeof(SpectraPluginTransform) : 0u;
                            if (element_size == 0 || data.elements.size() != checked_byte_size(data.count, element_size)) throw std::runtime_error("Frozen element payload is incompatible with its Visualization view");
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenField>) {
                            if (kind != scene::VisualizationViewKind::FieldSlice && kind != scene::VisualizationViewKind::FieldVectors) throw std::runtime_error("Frozen Field payload is incompatible with its Visualization view");
                            const std::uint64_t count      = static_cast<std::uint64_t>(data.resolution.x) * data.resolution.y * data.resolution.z;
                            const std::size_t element_size = data.channel.kind == FieldChannelKind::Float ? sizeof(float) : sizeof(SpectraPluginFloat3);
                            if (data.values.size() != checked_byte_size(count, element_size)) throw std::runtime_error("Frozen Field payload size is inconsistent with its resolution");
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenImage>) {
                            if (kind != scene::VisualizationViewKind::Image || data.pixels.size() != image_byte_size(data.image)) throw std::runtime_error("Frozen Image payload is incompatible with its Visualization view");
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenCameraObservations>) {
                            if (kind != scene::VisualizationViewKind::CameraObservations || data.count > data.dataset.capacity || data.observations.size() != checked_byte_size(data.count, sizeof(SpectraPluginCameraObservation)) || data.images.size() != image_byte_size(data.dataset.images, data.count)) throw std::runtime_error("Frozen Camera Observation payload is inconsistent with its Dataset");
                        } else {
                            if (kind != scene::VisualizationViewKind::Surface || data.positions.size() != checked_byte_size(data.vertex_count, sizeof(SpectraPluginFloat3)) || (data.indices ? data.indices->size() != checked_byte_size(data.index_count, sizeof(std::uint32_t)) : data.index_count != 0) || (data.scalars && data.scalars->size() != checked_byte_size(data.vertex_count, sizeof(float)))) throw std::runtime_error("Frozen Surface payload is inconsistent with its element counts");
                        }
                    },
                    visualization.data);
            }
            for (const FrozenTelemetrySystem& system : frame.telemetry) {
                if (system.snapshot.values.size() > system.descriptors.size()) throw std::runtime_error("Frozen Telemetry value count exceeds its descriptor count");
                for (const TelemetrySample& sample : system.snapshot.history)
                    if (sample.values.size() != system.descriptors.size()) throw std::runtime_error("Frozen Telemetry history is inconsistent with its descriptors");
            }
        }
    } // namespace

    std::vector<std::byte> serialize_frozen_frame(const FrozenFrame& frame) {
        validate_frozen_frame(frame);
        Writer writer{};
        for (const char character : std::string_view{"SPDYN003"}) writer.integer(static_cast<std::uint8_t>(character));
        writer.integer(std::uint32_t{3});
        writer.integer(frame.simulation.step);
        writer.floating(frame.simulation.seconds);
        writer.integer(frame.presentation.frame);
        writer.floating(frame.presentation.seconds);
        writer.integer(static_cast<std::uint64_t>(frame.visualizations.size()));
        for (const FrozenVisualization& visualization : frame.visualizations) {
            write_view(writer, visualization.style.view);
            writer.transform(visualization.style.transform);
            std::visit(
                [&writer](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenElements>) {
                        writer.bytes(data.elements);
                        writer.integer(data.count);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenField>) {
                        writer.uint3(data.resolution);
                        writer.transform(data.local_from_grid);
                        writer.string(data.channel.id);
                        writer.enumeration(data.channel.kind);
                        writer.bytes(data.values);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenImage>) {
                        write_image(writer, data.image);
                        writer.bytes(data.pixels);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenCameraObservations>) {
                        writer.integer(data.dataset.capacity);
                        write_image(writer, data.dataset.images);
                        writer.bytes(data.observations);
                        writer.bytes(data.images);
                        writer.integer(data.count);
                    } else {
                        writer.bytes(data.positions);
                        writer.boolean(data.indices.has_value());
                        if (data.indices) writer.bytes(*data.indices);
                        writer.boolean(data.scalars.has_value());
                        if (data.scalars) writer.bytes(*data.scalars);
                        writer.integer(data.vertex_count);
                        writer.integer(data.index_count);
                    }
                },
                visualization.data);
        }
        writer.integer(static_cast<std::uint64_t>(frame.telemetry.size()));
        for (const FrozenTelemetrySystem& system : frame.telemetry) {
            writer.string(system.id);
            writer.string(system.name);
            writer.string(system.provider_id);
            writer.integer(static_cast<std::uint64_t>(system.descriptors.size()));
            for (const TelemetryDescriptor& descriptor : system.descriptors) {
                writer.string(descriptor.id);
                writer.string(descriptor.name);
                writer.string(descriptor.unit);
                writer.string(descriptor.section_id);
                writer.enumeration(descriptor.kind);
                writer.boolean(descriptor.plot);
            }
            writer.string(system.snapshot.phase);
            writer.string(system.snapshot.headline);
            writer.string(system.snapshot.message);
            writer.integer(static_cast<std::uint64_t>(system.snapshot.values.size()));
            for (const std::optional<TelemetryValue>& value : system.snapshot.values) {
                writer.boolean(value.has_value());
                if (value) write_telemetry_value(writer, *value);
            }
            writer.integer(static_cast<std::uint64_t>(system.snapshot.history.size()));
            for (const TelemetrySample& sample : system.snapshot.history) {
                writer.integer(sample.simulation_step);
                writer.floating(sample.simulation_seconds);
                writer.integer(static_cast<std::uint64_t>(sample.values.size()));
                for (const TelemetryValue& value : sample.values) write_telemetry_value(writer, value);
            }
        }
        return std::move(writer.data);
    }

    FrozenFrame deserialize_frozen_frame(const std::span<const std::byte> payload) {
        Reader reader{payload};
        for (const char expected : std::string_view{"SPDYN003"})
            if (reader.integer<std::uint8_t>() != static_cast<std::uint8_t>(expected)) throw std::runtime_error("Invalid Spectra Frozen Dynamic Frame header");
        if (reader.integer<std::uint32_t>() != 3) throw std::runtime_error("Unsupported Spectra Frozen Dynamic Frame version");
        FrozenFrame frame{{reader.integer<std::uint64_t>(), reader.floating64()}, {reader.integer<std::uint64_t>(), reader.floating64()}};
        frame.visualizations.resize(reader.count());
        for (FrozenVisualization& visualization : frame.visualizations) {
            visualization.style.view      = read_view(reader);
            visualization.style.transform = reader.transform();
            switch (scene::visualization_view_kind(visualization.style.view)) {
            case scene::VisualizationViewKind::Points:
            case scene::VisualizationViewKind::Segments:
            case scene::VisualizationViewKind::Curves:
            case scene::VisualizationViewKind::Vectors:
            case scene::VisualizationViewKind::Frames: visualization.data = FrozenElements{reader.bytes(), reader.integer<std::uint32_t>()}; break;
            case scene::VisualizationViewKind::FieldSlice:
            case scene::VisualizationViewKind::FieldVectors: visualization.data = FrozenField{reader.uint3(), reader.transform(), {reader.string(), reader.enumeration(FieldChannelKind::Float3)}, reader.bytes()}; break;
            case scene::VisualizationViewKind::Image: visualization.data = FrozenImage{read_image(reader), reader.bytes()}; break;
            case scene::VisualizationViewKind::CameraObservations: visualization.data = FrozenCameraObservations{{reader.integer<std::uint32_t>(), read_image(reader)}, reader.bytes(), reader.bytes(), reader.integer<std::uint32_t>()}; break;
            case scene::VisualizationViewKind::Surface:
                {
                    FrozenSurface surface{.positions = reader.bytes()};
                    if (reader.boolean()) surface.indices = reader.bytes();
                    if (reader.boolean()) surface.scalars = reader.bytes();
                    surface.vertex_count = reader.integer<std::uint32_t>();
                    surface.index_count  = reader.integer<std::uint32_t>();
                    visualization.data   = std::move(surface);
                    break;
                }
            }
        }
        frame.telemetry.resize(reader.count());
        for (FrozenTelemetrySystem& system : frame.telemetry) {
            system.id          = reader.string();
            system.name        = reader.string();
            system.provider_id = reader.string();
            system.descriptors.resize(reader.count());
            for (TelemetryDescriptor& descriptor : system.descriptors) {
                descriptor.id         = reader.string();
                descriptor.name       = reader.string();
                descriptor.unit       = reader.string();
                descriptor.section_id = reader.string();
                descriptor.kind       = reader.enumeration(TelemetryKind::Float3);
                descriptor.plot       = reader.boolean();
            }
            system.snapshot.phase    = reader.string();
            system.snapshot.headline = reader.string();
            system.snapshot.message  = reader.string();
            system.snapshot.values.resize(reader.count());
            for (std::optional<TelemetryValue>& value : system.snapshot.values)
                if (reader.boolean()) value = read_telemetry_value(reader);
            const std::size_t history_size = reader.count();
            for (std::size_t index = 0; index != history_size; ++index) {
                TelemetrySample sample{reader.integer<std::uint64_t>(), reader.floating64()};
                sample.values.resize(reader.count());
                for (TelemetryValue& value : sample.values) value = read_telemetry_value(reader);
                system.snapshot.history.push_back(std::move(sample));
            }
        }
        if (!reader.finished()) throw std::runtime_error("Spectra Frozen Dynamic Frame contains trailing data");
        validate_frozen_frame(frame);
        return frame;
    }

    void write_telemetry(const std::filesystem::path& path, const FrozenFrame& frame) {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::trunc};
        stream << "system,metric,unit,kind,value\n";
        for (const FrozenTelemetrySystem& system : frame.telemetry)
            for (std::size_t index = 0; index != system.descriptors.size(); ++index) {
                const TelemetryDescriptor& descriptor = system.descriptors[index];
                stream << csv_field(system.id) << ',' << csv_field(descriptor.id) << ',' << csv_field(descriptor.unit) << ',' << static_cast<std::uint32_t>(descriptor.kind) << ',';
                if (index < system.snapshot.values.size() && system.snapshot.values[index]) {
                    const TelemetryValue& value = *system.snapshot.values[index];
                    if (value.kind == TelemetryKind::Boolean || value.kind == TelemetryKind::Integer)
                        stream << value.integer;
                    else if (value.kind == TelemetryKind::Float)
                        stream << value.floating[0];
                    else
                        stream << csv_field(std::format("{} {} {}", value.floating[0], value.floating[1], value.floating[2]));
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
        std::vector<std::span<const std::byte>> sources{};
        for (const FrozenVisualization& visualization : next_data.visualizations)
            std::visit(
                [&sources](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenElements>)
                        sources.emplace_back(data.elements);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenField>)
                        sources.emplace_back(data.values);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenImage>)
                        sources.emplace_back(data.pixels);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenCameraObservations>) {
                        sources.emplace_back(data.observations);
                        sources.emplace_back(data.images);
                    } else {
                        sources.emplace_back(data.positions);
                        if (data.indices) sources.emplace_back(*data.indices);
                        if (data.scalars) sources.emplace_back(*data.scalars);
                    }
                },
                visualization.data);
        std::uint64_t total_size{};
        for (const std::span<const std::byte> source : sources) {
            if (total_size > std::numeric_limits<std::uint64_t>::max() - 15u) throw std::runtime_error("Frozen GPU upload size overflows");
            total_size               = total_size + 15u & ~std::uint64_t{15u};
            const std::uint64_t size = std::max<std::uint64_t>(source.size(), sizeof(std::uint32_t));
            if (total_size > std::numeric_limits<std::uint64_t>::max() - size) throw std::runtime_error("Frozen GPU upload size overflows");
            total_size += size;
        }
        std::deque<Buffer> next_buffers{};
        DynamicFrame next_gpu_frame{.simulation = next_data.simulation, .presentation = next_data.presentation};
        std::vector<GpuVisualization> next_gpu_visualizations{};
        GpuBuffer staging = this->runtime.resources.create_buffer(std::max<std::uint64_t>(total_size, sizeof(std::uint32_t)), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        struct Copy {
            vk::Buffer destination{};
            vk::BufferCopy region{};
        };
        std::vector<Copy> copies{};
        std::uint64_t offset{};
        const auto upload = [&](const std::span<const std::byte> source) -> GpuBufferView {
            offset                   = offset + 15u & ~std::uint64_t{15u};
            const std::uint64_t size = std::max<std::uint64_t>(source.size(), sizeof(std::uint32_t));
            Buffer& destination      = next_buffers.emplace_back();
            destination.gpu          = this->runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            destination.descriptor   = this->runtime.frames.allocate_resource_descriptor();
            std::memset(static_cast<std::byte*>(staging.mapped) + offset, 0, size);
            std::memcpy(static_cast<std::byte*>(staging.mapped) + offset, source.data(), source.size());
            copies.push_back({*destination.gpu.buffer, {offset, 0, size}});
            offset += size;
            this->runtime.resources.write_buffer_descriptor(destination.descriptor, vk::DescriptorType::eStorageBuffer, destination.gpu);
            return {&destination.gpu, destination.descriptor};
        };
        for (const FrozenVisualization& visualization : next_data.visualizations)
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenElements>) {
                        const GpuBufferView elements = upload(data.elements);
                        switch (scene::visualization_view_kind(visualization.style.view)) {
                        case scene::VisualizationViewKind::Points: next_gpu_visualizations.push_back({GpuPointVisualization{visualization.style, elements, data.count}}); break;
                        case scene::VisualizationViewKind::Segments: next_gpu_visualizations.push_back({GpuSegmentVisualization{visualization.style, elements, data.count}}); break;
                        case scene::VisualizationViewKind::Curves: next_gpu_visualizations.push_back({GpuCurveVisualization{visualization.style, elements, data.count}}); break;
                        case scene::VisualizationViewKind::Vectors: next_gpu_visualizations.push_back({GpuVectorVisualization{visualization.style, elements, data.count}}); break;
                        case scene::VisualizationViewKind::Frames: next_gpu_visualizations.push_back({GpuTransformVisualization{visualization.style, elements, data.count}}); break;
                        default: throw std::runtime_error("Frozen element payload has an incompatible Visualization kind");
                        }
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenField>)
                        next_gpu_visualizations.push_back({GpuFieldVisualization{visualization.style, data.resolution, data.local_from_grid, {data.channel, upload(data.values)}}});
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenImage>)
                        next_gpu_visualizations.push_back({GpuImageVisualization{visualization.style, data.image, upload(data.pixels)}});
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, FrozenCameraObservations>)
                        next_gpu_visualizations.push_back({GpuCameraObservationVisualization{visualization.style, data.dataset, upload(data.observations), upload(data.images), data.count}});
                    else
                        next_gpu_visualizations.push_back({GpuSurfaceVisualization{visualization.style, upload(data.positions), data.indices ? std::optional{upload(*data.indices)} : std::nullopt, data.scalars ? std::optional{upload(*data.scalars)} : std::nullopt, data.vertex_count, data.index_count}});
                },
                visualization.data);
        if (!copies.empty())
            this->runtime.resources.submit_immediate([&](const vk::raii::CommandBuffer& command_buffer) {
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
        this->data      = {};
        this->ready     = false;
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
    const DynamicFrame& FrozenFrameRuntime::pending_frame() const noexcept {
        return this->gpu_frame;
    }
} // namespace spectra::dynamics
