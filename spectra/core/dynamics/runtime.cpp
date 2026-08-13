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
        static_assert(sizeof(SpectraPluginSphere) == sizeof(plugin_abi::SpectraPluginSphere));
        static_assert(sizeof(SpectraPluginInstanceTransform) == sizeof(plugin_abi::SpectraPluginInstanceTransform));
        static_assert(offsetof(SpectraPluginInstanceTransform, transform) == offsetof(plugin_abi::SpectraPluginInstanceTransform, transform));
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
        static_assert(offsetof(SpectraPluginSegment, scalar) == offsetof(plugin_abi::SpectraPluginSegment, scalar));
        static_assert(sizeof(SpectraPluginCurve) == sizeof(plugin_abi::SpectraPluginCurve));
        static_assert(alignof(SpectraPluginCurve) == alignof(plugin_abi::SpectraPluginCurve));
        static_assert(offsetof(SpectraPluginCurve, control_0) == offsetof(plugin_abi::SpectraPluginCurve, control_0));
        static_assert(offsetof(SpectraPluginCurve, width) == offsetof(plugin_abi::SpectraPluginCurve, width));
        static_assert(offsetof(SpectraPluginCurve, control_1) == offsetof(plugin_abi::SpectraPluginCurve, control_1));
        static_assert(offsetof(SpectraPluginCurve, control_2) == offsetof(plugin_abi::SpectraPluginCurve, control_2));
        static_assert(offsetof(SpectraPluginCurve, control_3) == offsetof(plugin_abi::SpectraPluginCurve, control_3));
        static_assert(offsetof(SpectraPluginCurve, color) == offsetof(plugin_abi::SpectraPluginCurve, color));
        static_assert(offsetof(SpectraPluginCurve, scalar) == offsetof(plugin_abi::SpectraPluginCurve, scalar));
        static_assert(sizeof(SpectraPluginVector) == sizeof(plugin_abi::SpectraPluginVector));
        static_assert(alignof(SpectraPluginVector) == alignof(plugin_abi::SpectraPluginVector));
        static_assert(offsetof(SpectraPluginVector, origin) == offsetof(plugin_abi::SpectraPluginVector, origin));
        static_assert(offsetof(SpectraPluginVector, width) == offsetof(plugin_abi::SpectraPluginVector, width));
        static_assert(offsetof(SpectraPluginVector, vector) == offsetof(plugin_abi::SpectraPluginVector, vector));
        static_assert(offsetof(SpectraPluginVector, color) == offsetof(plugin_abi::SpectraPluginVector, color));
        static_assert(offsetof(SpectraPluginVector, scalar) == offsetof(plugin_abi::SpectraPluginVector, scalar));
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
            if (value.size != 0 && value.data == nullptr) throw std::runtime_error("Provider returned a null string pointer with nonzero size");
            return value.size == 0 ? std::string{} : std::string{value.data, value.size};
        }

        void check_plugin_result(const SpectraPluginResult result, const std::string_view operation) {
            if (result.error.size != 0) throw std::runtime_error(std::format("Provider {} failed: {}", operation, plugin_string(result.error)));
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

        template <class Destination, class Source>
        [[nodiscard]] Destination checked_plugin_enum(const Source value, const std::initializer_list<Source> accepted, const std::string_view name) {
            if (!std::ranges::contains(accepted, value)) throw std::runtime_error(std::format("Provider declared unknown {} value {}", name, std::to_underlying(value)));
            return static_cast<Destination>(value);
        }

        [[nodiscard]] scene::DynamicParameterValue scene_parameter_value(const SpectraPluginParameterValue value) {
            const scene::DynamicParameterKind kind = checked_plugin_enum<scene::DynamicParameterKind>(value.kind, {SpectraPluginParameterKind::Boolean, SpectraPluginParameterKind::Integer, SpectraPluginParameterKind::Float, SpectraPluginParameterKind::Float3, SpectraPluginParameterKind::Enumeration}, "Parameter kind");
            return {kind, value.integer, {value.floating[0], value.floating[1], value.floating[2]}};
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

        struct DatasetBufferLayout {
            SpectraPluginBufferSemantic semantic{};
            std::uint32_t channel_index{};
            std::uint64_t element_count{};
            std::uint64_t element_size{};
        };

        [[nodiscard]] std::uint32_t checked_capacity_product(const std::initializer_list<std::uint32_t> factors) {
            std::uint32_t product = 1;
            for (const std::uint32_t factor : factors) {
                if (factor != 0 && product > std::numeric_limits<std::uint32_t>::max() / factor) throw std::runtime_error("Dynamic Dataset capacity exceeds the 32-bit GPU count contract");
                product *= factor;
            }
            return product;
        }

        [[nodiscard]] std::uint64_t checked_element_product(const std::initializer_list<std::uint64_t> factors) {
            std::uint64_t product = 1;
            for (const std::uint64_t factor : factors) {
                if (factor != 0 && product > std::numeric_limits<std::uint64_t>::max() / factor) throw std::runtime_error("Dynamic Dataset element count overflows");
                product *= factor;
            }
            return product;
        }

        [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> dataset_capacities(const dynamics::DatasetDescriptor& dataset) {
            if (const auto* value = std::get_if<dynamics::TriangleMeshDataset>(&dataset.details)) return {value->vertex_capacity, value->index_capacity};
            if (const auto* value = std::get_if<dynamics::SphereSetDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::InstanceTransformDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::PointDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::SegmentDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::CurveDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::VectorDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::CameraObservationDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::TransformDataset>(&dataset.details)) return {value->capacity, 0};
            if (const auto* value = std::get_if<dynamics::FieldDataset>(&dataset.details)) return {checked_capacity_product({value->resolution.x, value->resolution.y, value->resolution.z}), 0};
            if (const auto* value = std::get_if<dynamics::ImageDataset>(&dataset.details)) return {checked_capacity_product({value->extent[0], value->extent[1]}), 0};
            throw std::runtime_error("Unknown dynamic Dataset kind");
        }

        [[nodiscard]] std::vector<DatasetBufferLayout> dataset_buffer_layouts(const dynamics::DatasetDescriptor& dataset, const std::uint32_t capacity, const std::uint32_t secondary_capacity) {
            std::vector<DatasetBufferLayout> layouts{};
            if (const auto* value = std::get_if<dynamics::TriangleMeshDataset>(&dataset.details)) {
                layouts.emplace_back(SpectraPluginBufferSemantic::TrianglePosition, 0, capacity, sizeof(SpectraPluginFloat3));
                if ((value->attributes & std::to_underlying(SpectraPluginMeshAttribute::Normal)) != 0) layouts.emplace_back(SpectraPluginBufferSemantic::TriangleNormal, 0, capacity, sizeof(SpectraPluginFloat3));
                if ((value->attributes & std::to_underlying(SpectraPluginMeshAttribute::Tangent)) != 0) layouts.emplace_back(SpectraPluginBufferSemantic::TriangleTangent, 0, capacity, sizeof(SpectraPluginFloat3));
                if ((value->attributes & std::to_underlying(SpectraPluginMeshAttribute::TextureCoordinate)) != 0) layouts.emplace_back(SpectraPluginBufferSemantic::TriangleTextureCoordinate, 0, capacity, sizeof(SpectraPluginFloat2));
                if ((value->attributes & std::to_underlying(SpectraPluginMeshAttribute::Scalar)) != 0) layouts.emplace_back(SpectraPluginBufferSemantic::TriangleScalar, 0, capacity, sizeof(float));
                if (secondary_capacity != 0) layouts.emplace_back(SpectraPluginBufferSemantic::TriangleIndex, 0, secondary_capacity, sizeof(std::uint32_t));
            } else if (std::holds_alternative<dynamics::SphereSetDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Sphere, 0, capacity, sizeof(SpectraPluginSphere));
            else if (std::holds_alternative<dynamics::InstanceTransformDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::InstanceTransform, 0, capacity, sizeof(SpectraPluginInstanceTransform));
            else if (std::holds_alternative<dynamics::PointDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Point, 0, capacity, sizeof(SpectraPluginPoint));
            else if (std::holds_alternative<dynamics::SegmentDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Segment, 0, capacity, sizeof(SpectraPluginSegment));
            else if (std::holds_alternative<dynamics::CurveDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Curve, 0, capacity, sizeof(SpectraPluginCurve));
            else if (std::holds_alternative<dynamics::VectorDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Vector, 0, capacity, sizeof(SpectraPluginVector));
            else if (const auto* value = std::get_if<dynamics::FieldDataset>(&dataset.details)) {
                const std::uint64_t voxel_count = checked_element_product({value->resolution.x, value->resolution.y, value->resolution.z});
                for (std::uint32_t channel_index = 0; channel_index < value->channels.size(); ++channel_index) layouts.emplace_back(SpectraPluginBufferSemantic::FieldChannel, channel_index, voxel_count, value->channels[channel_index].kind == dynamics::FieldChannelKind::Float ? sizeof(float) : sizeof(SpectraPluginFloat3));
            } else if (const auto* value = std::get_if<dynamics::ImageDataset>(&dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::ImagePixel, 0, checked_element_product({value->extent[0], value->extent[1]}), dynamics::image_element_size(value->format));
            else if (const auto* value = std::get_if<dynamics::CameraObservationDataset>(&dataset.details)) {
                layouts.emplace_back(SpectraPluginBufferSemantic::CameraObservation, 0, capacity, sizeof(SpectraPluginCameraObservation));
                layouts.emplace_back(SpectraPluginBufferSemantic::ImagePixel, 0, checked_element_product({capacity, value->images.extent[0], value->images.extent[1]}), dynamics::image_element_size(value->images.format));
            } else if (std::holds_alternative<dynamics::TransformDataset>(dataset.details))
                layouts.emplace_back(SpectraPluginBufferSemantic::Transform, 0, capacity, sizeof(SpectraPluginTransform));
            return layouts;
        }

        [[nodiscard]] math::Transform plugin_transform(const SpectraPluginTransform& source) noexcept {
            math::Transform result{};
            std::ranges::copy(source.matrix, result.matrix.begin());
            return result;
        }

    } // namespace

    DynamicsRuntime::DynamicsRuntime(VulkanRuntime& runtime) noexcept : context{runtime} {}

    DynamicsRuntime::~DynamicsRuntime() {
        this->destroy();
    }

    void DynamicsRuntime::initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene) {
        this->destroy();
        try {
            this->configuration.source_scene = &source_scene;
            this->configuration.setup        = *source_scene.dynamic_setup;
            if (!(this->configuration.setup.clock.step_seconds > 0.0)) throw std::runtime_error("Scene Dynamics clock step duration must be positive");
            if (this->configuration.setup.clock.end_step && *this->configuration.setup.clock.end_step < this->configuration.setup.clock.start_step) throw std::runtime_error("Scene Dynamics clock end step precedes its start step");

            std::vector<std::string> required_providers{};
            for (const scene::DynamicSystem& system : this->configuration.setup.systems)
                if (!std::ranges::contains(required_providers, system.provider_id)) required_providers.emplace_back(system.provider_id);
            std::ranges::sort(required_providers);

            for (const std::string& required_provider : required_providers) {
                const std::filesystem::path path                       = scene_path.parent_path() / dynamics::provider_library_filename(required_provider);
                ProviderLibrary& library                               = this->providers.libraries.emplace_back(path, required_provider);
                const SpectraPluginProviderDescriptor& source_provider = library.descriptor;
                if ((source_provider.dataset_count != 0 && source_provider.datasets == nullptr) || (source_provider.parameter_count != 0 && source_provider.parameters == nullptr) || (source_provider.section_count != 0 && source_provider.sections == nullptr) || (source_provider.telemetry_count != 0 && source_provider.telemetry == nullptr)) throw std::runtime_error(std::format("Provider '{}' returned a null descriptor array", required_provider));
                if (source_provider.telemetry_count > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error(std::format("Provider '{}' declares more Telemetry values than the 32-bit GPU count contract permits", required_provider));
                dynamics::ProviderDescriptor provider{.id = plugin_string(source_provider.id)};
                provider.datasets.reserve(source_provider.dataset_count);
                for (std::uint64_t dataset_index = 0; dataset_index < source_provider.dataset_count; ++dataset_index) {
                    const SpectraPluginDatasetDescriptor& source = source_provider.datasets[dataset_index];
                    dynamics::DatasetDescriptor dataset{.id = plugin_string(source.id)};
                    switch (source.kind) {
                    case SpectraPluginDatasetKind::TriangleMesh:
                        {
                            const SpectraPluginTriangleMeshDatasetDescriptor& value = source.details.triangle_mesh;
                            constexpr std::uint32_t known_attributes                = std::to_underlying(SpectraPluginMeshAttribute::Normal) | std::to_underlying(SpectraPluginMeshAttribute::Tangent) | std::to_underlying(SpectraPluginMeshAttribute::TextureCoordinate) | std::to_underlying(SpectraPluginMeshAttribute::Scalar);
                            if ((value.attributes & ~known_attributes) != 0) throw std::runtime_error(std::format("Provider declared unknown Triangle Mesh attribute flags 0x{:x}", value.attributes));
                            const dynamics::MeshUpdateMode update_mode = checked_plugin_enum<dynamics::MeshUpdateMode>(value.update_mode, {SpectraPluginMeshUpdateMode::Deformable, SpectraPluginMeshUpdateMode::TopologyChanging}, "Mesh update mode");
                            dataset.resource_kind                      = scene::DynamicSceneResourceKind::Geometry;
                            dataset.details                            = dynamics::TriangleMeshDataset{value.vertex_capacity, value.index_capacity, update_mode, value.attributes};
                            break;
                        }
                    case SpectraPluginDatasetKind::SphereSet:
                        dataset.resource_kind = scene::DynamicSceneResourceKind::SphereSet;
                        dataset.details       = dynamics::SphereSetDataset{source.details.sphere_set.capacity};
                        break;
                    case SpectraPluginDatasetKind::InstanceTransformSet: dataset.details = dynamics::InstanceTransformDataset{source.details.instance_transforms.capacity}; break;
                    case SpectraPluginDatasetKind::PointSet: dataset.details = dynamics::PointDataset{source.details.points.capacity}; break;
                    case SpectraPluginDatasetKind::SegmentSet: dataset.details = dynamics::SegmentDataset{source.details.segments.capacity}; break;
                    case SpectraPluginDatasetKind::CurveSet: dataset.details = dynamics::CurveDataset{source.details.curves.capacity}; break;
                    case SpectraPluginDatasetKind::VectorSet: dataset.details = dynamics::VectorDataset{source.details.vectors.capacity}; break;
                    case SpectraPluginDatasetKind::Field:
                        {
                            const SpectraPluginFieldDatasetDescriptor& value = source.details.field;
                            if (value.channel_count > std::numeric_limits<std::uint32_t>::max() || (value.channel_count != 0 && value.channels == nullptr)) throw std::runtime_error("Provider Field Dataset has an invalid channel array");
                            dynamics::FieldDataset field{.resolution = {value.resolution[0], value.resolution[1], value.resolution[2]}, .local_from_grid = plugin_transform(value.local_from_grid)};
                            field.channels.reserve(value.channel_count);
                            for (std::uint64_t channel_index = 0; channel_index < value.channel_count; ++channel_index) {
                                const SpectraPluginFieldChannelDescriptor& channel = value.channels[channel_index];
                                const dynamics::FieldChannelKind kind              = checked_plugin_enum<dynamics::FieldChannelKind>(channel.kind, {SpectraPluginFieldChannelKind::Float, SpectraPluginFieldChannelKind::Float3}, "Field channel kind");
                                field.channels.emplace_back(plugin_string(channel.id), kind);
                            }
                            dataset.resource_kind = scene::DynamicSceneResourceKind::Volume;
                            dataset.details       = std::move(field);
                            break;
                        }
                    case SpectraPluginDatasetKind::Image:
                        {
                            const SpectraPluginImageDatasetDescriptor& value = source.details.image;
                            dataset.details                                  = dynamics::ImageDataset{{value.extent[0], value.extent[1]}, checked_plugin_enum<dynamics::ImageFormat>(value.format, {SpectraPluginImageFormat::Rgba8Unorm, SpectraPluginImageFormat::Rgba16Float, SpectraPluginImageFormat::Rgba32Float}, "Image format"), checked_plugin_enum<scene::SpectrumColorSpace>(value.color_space, {SpectraPluginColorSpace::Srgb, SpectraPluginColorSpace::Rec2020, SpectraPluginColorSpace::Aces2065_1}, "Image color space"), checked_plugin_enum<dynamics::TransferFunction>(value.transfer_function, {SpectraPluginTransferFunction::Linear, SpectraPluginTransferFunction::Srgb}, "Image transfer function")};
                            break;
                        }
                    case SpectraPluginDatasetKind::CameraObservationSet:
                        {
                            const SpectraPluginCameraObservationDatasetDescriptor& value = source.details.camera_observations;
                            dataset.details                                              = dynamics::CameraObservationDataset{value.capacity, {{value.image_extent[0], value.image_extent[1]}, checked_plugin_enum<dynamics::ImageFormat>(value.image_format, {SpectraPluginImageFormat::Rgba8Unorm, SpectraPluginImageFormat::Rgba16Float, SpectraPluginImageFormat::Rgba32Float}, "Camera image format"), checked_plugin_enum<scene::SpectrumColorSpace>(value.color_space, {SpectraPluginColorSpace::Srgb, SpectraPluginColorSpace::Rec2020, SpectraPluginColorSpace::Aces2065_1}, "Camera image color space"), checked_plugin_enum<dynamics::TransferFunction>(value.transfer_function, {SpectraPluginTransferFunction::Linear, SpectraPluginTransferFunction::Srgb}, "Camera image transfer function")}};
                            break;
                        }
                    case SpectraPluginDatasetKind::TransformSet: dataset.details = dynamics::TransformDataset{source.details.transforms.capacity}; break;
                    default: throw std::runtime_error(std::format("Provider declared unknown Dataset kind {}", std::to_underlying(source.kind)));
                    }
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
                        .application_mode = checked_plugin_enum<dynamics::ParameterApplication>(parameter.application_mode, {SpectraPluginParameterApplication::Live, SpectraPluginParameterApplication::ResetRequired}, "Parameter application mode"),
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
                    provider.telemetry.emplace_back(plugin_string(telemetry.id), plugin_string(telemetry.name), plugin_string(telemetry.unit), plugin_string(telemetry.section_id), checked_plugin_enum<dynamics::TelemetryKind>(telemetry.kind, {SpectraPluginTelemetryKind::Boolean, SpectraPluginTelemetryKind::Integer, SpectraPluginTelemetryKind::Float, SpectraPluginTelemetryKind::Float3}, "Telemetry kind"), telemetry.plot != 0);
                }
                library.provider = std::move(provider);
                if (!this->providers.by_id.emplace(library.provider.id, &library).second) throw std::runtime_error(std::format("Provider '{}' is loaded more than once", library.provider.id));
            }

            std::set<std::pair<scene::DynamicSceneResourceKind, std::uint64_t>> scene_writers{};
            for (std::size_t system_index = 0; system_index < this->configuration.setup.systems.size(); ++system_index) {
                const scene::DynamicSystem& declared = this->configuration.setup.systems[system_index];
                if (!declared.enabled) continue;
                const dynamics::ProviderDescriptor& provider = this->provider_descriptor(declared.provider_id);
                ProviderLibrary& library                     = this->provider_library(provider.id);
                DynamicSystemRuntime& system                 = this->systems.runtimes.emplace_back(DynamicSystemRuntime{.scene_system_index = system_index, .provider_descriptor = &provider, .plugin_api = library.plugin_api});
                system.telemetry.values.resize(provider.telemetry.size());
                const SpectraPluginProviderCreateResult created = system.plugin_api->create_provider();
                check_plugin_result(created.result, "creation");
                system.provider_instance = created.provider;
                if (!system.provider_instance) {
                    this->systems.runtimes.pop_back();
                    throw std::runtime_error(std::format("Provider '{}' refused to create its declared instance", provider.id));
                }
                for (const dynamics::ParameterDescriptor& parameter : provider.parameters) {
                    const auto configured = std::ranges::find(declared.parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
                    system.parameter_values.emplace_back(configured == declared.parameters.end() ? parameter.value : configured->value);
                }
                for (std::size_t dataset_index = 0; dataset_index < provider.datasets.size(); ++dataset_index) {
                    DynamicDatasetRuntime dataset{.descriptor = provider.datasets[dataset_index]};
                    this->bind_dataset(dataset, declared);
                    if (dataset.scene_binding) {
                        const std::pair key{*dataset.descriptor.resource_kind, dataset.scene_binding->resource_id};
                        if (!scene_writers.emplace(key).second) throw std::runtime_error(std::format("Scene resource {}:{} has more than one GPU Dataset writer", std::to_underlying(key.first), key.second));
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
                this->apply_parameters(system, system.parameter_values);
            }
            this->reset_simulation();
        } catch (...) {
            this->destroy();
            throw;
        }
    }

    void DynamicsRuntime::destroy() noexcept {
        if (!this->configuration.source_scene && this->providers.libraries.empty()) return;
        this->providers.by_id.clear();
        if (this->context.runtime.frames.frame.recording) {
            try {
                this->consume_snapshot();
            } catch (...) {
                std::terminate();
            }
            VulkanRuntime* runtime = &this->context.runtime;
            this->context.runtime.frames.defer_destruction([runtime, libraries = std::move(this->providers.libraries), systems = std::move(this->systems.runtimes)]() mutable {
                const auto release_output = [runtime](const GpuExternalTimelineSemaphore& timeline, const std::uint64_t provider_signal_value) {
                    if (provider_signal_value == 0) return;
                    std::uint64_t current_value{};
                    if (static_cast<vk::Result>(runtime->graphics.device.getDispatcher()->vkGetSemaphoreCounterValue(*runtime->graphics.device, *timeline.semaphore, &current_value)) != vk::Result::eSuccess) std::terminate();
                    if (current_value >= provider_signal_value + 1) return;
                    try {
                        runtime->resources.wait_external_timeline(timeline, provider_signal_value);
                        runtime->resources.signal_external_timeline(timeline, provider_signal_value + 1);
                    } catch (...) {
                        std::terminate();
                    }
                };
                for (DynamicSystemRuntime& system : systems) {
                    for (const DynamicDatasetRuntime& dataset : system.datasets) release_output(dataset.timeline_semaphore, dataset.timeline_signal_value);
                    release_output(system.telemetry_gpu.timeline_semaphore, system.telemetry_gpu.timeline_signal_value);
                }
                for (DynamicSystemRuntime& system : systems)
                    if (system.provider_instance && system.plugin_api->destroy_provider(system.provider_instance).error.size != 0) std::terminate();
                systems.clear();
                libraries.clear();
            });
        } else {
            if (this->publication.snapshot_acquired) std::terminate();
            if (static_cast<vk::Result>(this->context.runtime.graphics.device.getDispatcher()->vkDeviceWaitIdle(*this->context.runtime.graphics.device)) != vk::Result::eSuccess) std::terminate();
            try {
                this->discard_pending_snapshot();
            } catch (...) {
                std::terminate();
            }
            for (DynamicSystemRuntime& system : this->systems.runtimes)
                if (system.provider_instance && system.plugin_api->destroy_provider(system.provider_instance).error.size != 0) std::terminate();
            this->context.runtime.frames.defer_destruction([libraries = std::move(this->providers.libraries), systems = std::move(this->systems.runtimes)]() mutable {
                systems.clear();
                libraries.clear();
            });
        }
        this->publication   = {};
        this->outputs       = {};
        this->clock         = {};
        this->configuration = {};
    }

    bool DynamicsRuntime::initialized() const noexcept {
        return this->configuration.source_scene != nullptr;
    }

    const dynamics::ProviderDescriptor& DynamicsRuntime::provider_descriptor(const std::string_view provider_id) const {
        return this->provider_library(provider_id).provider;
    }

    const dynamics::TelemetrySnapshot& DynamicsRuntime::telemetry(const std::size_t system_index) const {
        const auto found = std::ranges::find(this->systems.runtimes, system_index, &DynamicSystemRuntime::scene_system_index);
        if (found == this->systems.runtimes.end()) throw std::runtime_error("Scene Dynamics does not contain the requested System runtime");
        return found->telemetry;
    }

    void DynamicsRuntime::write_telemetry(const std::filesystem::path& path) const {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::trunc};
        stream << "system,metric,unit,kind,value\n";
        for (const DynamicSystemRuntime& runtime : this->systems.runtimes) {
            const scene::DynamicSystem& system = this->configuration.setup.systems[runtime.scene_system_index];
            for (std::size_t index = 0; index != runtime.provider_descriptor->telemetry.size(); ++index) {
                const dynamics::TelemetryDescriptor& descriptor = runtime.provider_descriptor->telemetry[index];
                stream << csv_field(system.id.value) << ',' << csv_field(descriptor.id) << ',' << csv_field(descriptor.unit) << ',' << static_cast<std::uint32_t>(descriptor.kind) << ',';
                if (index < runtime.telemetry.values.size() && runtime.telemetry.values[index]) {
                    const dynamics::TelemetryValue& value = *runtime.telemetry.values[index];
                    if (value.kind == dynamics::TelemetryKind::Boolean || value.kind == dynamics::TelemetryKind::Integer)
                        stream << value.integer;
                    else if (value.kind == dynamics::TelemetryKind::Float)
                        stream << value.floating[0];
                    else
                        stream << csv_field(std::format("{} {} {}", value.floating[0], value.floating[1], value.floating[2]));
                }
                stream << '\n';
            }
        }
        if (!stream) throw std::runtime_error(std::format("Failed to write Telemetry output: {}", path.string()));
    }

    std::span<const dynamics::MeshOutputBinding> DynamicsRuntime::mesh_bindings() const noexcept {
        return this->outputs.mesh_bindings;
    }

    std::span<const dynamics::SphereSetOutputBinding> DynamicsRuntime::sphere_set_bindings() const noexcept {
        return this->outputs.sphere_set_bindings;
    }

    std::span<const dynamics::GpuVisualization> DynamicsRuntime::visualizations() const noexcept {
        return this->publication.snapshot.visualizations;
    }

    bool DynamicsRuntime::controls(const scene::InstanceId instance_id) const noexcept {
        for (const DynamicSystemRuntime& system : this->systems.runtimes)
            for (const DynamicDatasetRuntime& dataset : system.datasets) {
                if (!dataset.scene_binding) continue;
                const scene::DynamicSceneBinding& binding = *dataset.scene_binding;
                const auto instance                       = std::ranges::find(this->configuration.source_scene->resources.instances, instance_id, &scene::Instance::id);
                if (instance == this->configuration.source_scene->resources.instances.end()) continue;
                const scene::Prototype& prototype = *std::ranges::find(this->configuration.source_scene->resources.prototypes, instance->prototype, &scene::Prototype::id);
                if (std::ranges::any_of(prototype.primitives, [&binding, &dataset](const scene::Primitive& primitive) { return (dataset.descriptor.resource_kind == scene::DynamicSceneResourceKind::Geometry && binding.resource_id == primitive.geometry.value) || (dataset.descriptor.resource_kind == scene::DynamicSceneResourceKind::SphereSet && binding.resource_id == primitive.spheres.value); })) return true;
            }
        return false;
    }

    bool DynamicsRuntime::controls(const scene::VolumeId volume_id) const noexcept {
        for (const DynamicSystemRuntime& system : this->systems.runtimes)
            for (const DynamicDatasetRuntime& dataset : system.datasets)
                if (dataset.scene_binding && dataset.descriptor.resource_kind == scene::DynamicSceneResourceKind::Volume && dataset.scene_binding->resource_id == volume_id.value) return true;
        return false;
    }

    bool DynamicsRuntime::running() const noexcept {
        return this->clock.playing;
    }

    bool DynamicsRuntime::faulted() const noexcept {
        return this->configuration.faulted;
    }

    dynamics::SimulationTimeline DynamicsRuntime::timeline() const noexcept {
        return {this->clock.simulation_step, static_cast<double>(this->clock.simulation_step) * this->configuration.setup.clock.step_seconds};
    }

    void DynamicsRuntime::start() {
        if (this->configuration.faulted) throw std::runtime_error("Scene Dynamics stopped after a Provider failure and must be reinitialized");
        this->clock.playing = true;
    }

    void DynamicsRuntime::pause() {
        this->clock.playing = false;
    }

    void DynamicsRuntime::step() {
        if (this->configuration.faulted) throw std::runtime_error("Scene Dynamics stopped after a Provider failure and must be reinitialized");
        try {
            this->advance_one_step();
        } catch (...) {
            this->configuration.faulted = true;
            this->clock.playing         = false;
            throw;
        }
    }

    void DynamicsRuntime::advance() {
        if (this->configuration.faulted) return;
        if (this->systems.runtimes.empty() || !this->clock.playing || this->publication.snapshot_pending) return;
        if (this->configuration.setup.clock.end_step && *this->configuration.setup.clock.end_step == this->configuration.setup.clock.start_step) {
            this->clock.playing = false;
            return;
        }
        try {
            this->advance_one_step();
            if (this->configuration.setup.clock.end_step && this->clock.simulation_step == *this->configuration.setup.clock.end_step && !this->configuration.setup.clock.loop) this->clock.playing = false;
        } catch (...) {
            this->configuration.faulted = true;
            this->clock.playing         = false;
            throw;
        }
    }

    void DynamicsRuntime::evaluate(const std::uint64_t simulation_step) {
        if (this->configuration.faulted) throw std::runtime_error("Scene Dynamics stopped after a Provider failure and must be reinitialized");
        if (simulation_step < this->configuration.setup.clock.start_step) throw std::runtime_error("Requested Dynamics step precedes the configured start step");
        if (this->configuration.setup.clock.end_step && simulation_step > *this->configuration.setup.clock.end_step) throw std::runtime_error("Requested Dynamics step exceeds the configured end step");
        try {
            this->evaluate_frame(simulation_step);
        } catch (...) {
            this->configuration.faulted = true;
            this->clock.playing         = false;
            throw;
        }
    }

    void DynamicsRuntime::evaluate_time(const double simulation_seconds) {
        if (simulation_seconds < 0.0) throw std::runtime_error("Requested Dynamics time cannot be negative");
        const double simulation_step = std::floor(simulation_seconds / this->configuration.setup.clock.step_seconds);
        if (simulation_step >= std::ldexp(1.0, 64)) throw std::runtime_error("Requested Dynamics time exceeds the fixed-step timeline range");
        this->evaluate(static_cast<std::uint64_t>(simulation_step));
    }

    void DynamicsRuntime::reset() {
        if (this->configuration.faulted) throw std::runtime_error("Scene Dynamics stopped after a Provider failure and must be reinitialized");
        this->pause();
        try {
            this->reset_simulation();
        } catch (...) {
            this->configuration.faulted = true;
            throw;
        }
    }
    void DynamicsRuntime::apply_parameter_changes(const std::size_t system_index, const std::span<const scene::DynamicParameterSetting> parameters, const bool reset) {
        if (this->configuration.faulted) throw std::runtime_error("Scene Dynamics stopped after a Provider failure and must be reinitialized");
        scene::DynamicSetup next_setup = this->configuration.setup;
        next_setup.systems[system_index].parameters.assign(parameters.begin(), parameters.end());
        const auto found = std::ranges::find(this->systems.runtimes, system_index, &DynamicSystemRuntime::scene_system_index);
        if (found == this->systems.runtimes.end()) {
            this->configuration.setup = std::move(next_setup);
            return;
        }
        const bool playing                = this->clock.playing;
        DynamicSystemRuntime& destination = *found;
        std::vector<scene::DynamicParameterValue> next_values{};
        next_values.reserve(destination.provider_descriptor->parameters.size());
        for (const dynamics::ParameterDescriptor& descriptor : destination.provider_descriptor->parameters) {
            const auto configured = std::ranges::find(parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
            next_values.emplace_back(configured == parameters.end() ? descriptor.value : configured->value);
        }
        try {
            this->apply_parameters(destination, next_values);
            if (reset)
                this->reset_simulation();
            else if (!playing)
                this->publish_snapshot(this->clock.simulation_step);
        } catch (...) {
            this->configuration.faulted = true;
            this->clock.playing         = false;
            throw;
        }
        this->configuration.setup    = std::move(next_setup);
        destination.parameter_values = std::move(next_values);
    }

    const dynamics::DynamicSnapshot* DynamicsRuntime::acquire_snapshot() {
        if (!this->publication.snapshot_pending) return nullptr;
        if (this->publication.snapshot_acquired) throw std::runtime_error("The pending Dynamics snapshot has already been acquired");
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicDatasetRuntime& dataset : system.datasets) {
                if (dataset.output_pending) this->context.runtime.frames.enqueue_external_wait(dataset.timeline_semaphore, dataset.timeline_signal_value, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader);
            }
        this->publication.snapshot_acquired = true;
        return &this->publication.snapshot;
    }

    void DynamicsRuntime::consume_snapshot() {
        if (!this->publication.snapshot_pending) return;
        for (DynamicSystemRuntime& system : this->systems.runtimes)
            for (DynamicDatasetRuntime& dataset : system.datasets)
                if (dataset.output_pending) {
                    if (!this->publication.snapshot_acquired) this->context.runtime.frames.enqueue_external_wait(dataset.timeline_semaphore, dataset.timeline_signal_value, vk::PipelineStageFlagBits2::eAllCommands);
                    this->context.runtime.frames.enqueue_external_signal(dataset.timeline_semaphore, dataset.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
                    dataset.output_pending = false;
                }
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
            if (!telemetry.output_pending) continue;
            this->context.runtime.frames.enqueue_external_wait(telemetry.timeline_semaphore, telemetry.timeline_signal_value, vk::PipelineStageFlagBits2::eAllCommands);
            this->context.runtime.frames.enqueue_external_signal(telemetry.timeline_semaphore, telemetry.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
            telemetry.output_pending = false;
        }
        this->publication.snapshot_pending  = false;
        this->publication.snapshot_acquired = false;
    }

    void DynamicsRuntime::record_telemetry(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->publication.snapshot_pending) return;
        if (!this->publication.snapshot_acquired) throw std::runtime_error("Telemetry recording requires an acquired Dynamics snapshot");
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
            if (!telemetry.output_pending) continue;
            this->context.runtime.frames.enqueue_external_wait(telemetry.timeline_semaphore, telemetry.timeline_signal_value, vk::PipelineStageFlagBits2::eCopy);
            const DynamicDatasetBuffer& source = telemetry.buffer_slots[telemetry.current_slot_index].front();
            TelemetryReadbackSlot& destination = telemetry.readback_slots[frame_slot_index];
            command_buffer.copyBuffer(*source.gpu_buffer.buffer, *destination.buffer.buffer, vk::BufferCopy{0, 0, source.byte_size});
            destination.simulation_step    = telemetry.simulation_step;
            destination.simulation_seconds = telemetry.simulation_seconds;
            destination.phase              = telemetry.phase;
            destination.headline           = telemetry.headline;
            destination.message            = telemetry.message;
            destination.pending            = true;
            this->context.runtime.frames.enqueue_external_signal(telemetry.timeline_semaphore, telemetry.timeline_signal_value + 1, vk::PipelineStageFlagBits2::eAllCommands);
            telemetry.output_pending = false;
        }
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

    DynamicsRuntime::ProviderLibrary::ProviderLibrary(const std::filesystem::path& library_path, const std::string_view expected_provider_id) {
        const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(library_path);
#if defined(_WIN32)
        const HMODULE loaded = LoadLibraryW(canonical_path.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load Provider Library: {}", canonical_path.string()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*) () noexcept>(GetProcAddress(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
#else
        void* loaded = dlopen(canonical_path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!loaded) throw std::runtime_error(std::format("Linux failed to load Provider Library '{}': {}", canonical_path.string(), dlerror()));
        const auto entry = reinterpret_cast<const SpectraPluginApi* (*) () noexcept>(dlsym(loaded, SPECTRA_PLUGIN_ENTRY_NAME));
#endif
        try {
            if (!entry) throw std::runtime_error(std::format("Provider Library does not export {}: {}", SPECTRA_PLUGIN_ENTRY_NAME, canonical_path.string()));
            const SpectraPluginApi* loaded_api = entry();
            if (!loaded_api || loaded_api->api_version != SPECTRA_PLUGIN_API_VERSION || loaded_api->struct_size != sizeof(SpectraPluginApi) || !loaded_api->describe_provider || !loaded_api->create_provider || !loaded_api->destroy_provider || !loaded_api->configure_dataset || !loaded_api->configure_telemetry || !loaded_api->apply_parameters || !loaded_api->reset || !loaded_api->step || !loaded_api->publish_snapshot) throw std::runtime_error(std::format("Provider Library has an incomplete Plugin API {} entry: {}", SPECTRA_PLUGIN_API_VERSION, canonical_path.string()));
            const SpectraPluginProviderDescriptionResult description = loaded_api->describe_provider();
            check_plugin_result(description.result, "description");
            const std::string reported_provider_id = plugin_string(description.descriptor.id);
            if (reported_provider_id != expected_provider_id) throw std::runtime_error(std::format("Provider Library '{}' reports Provider '{}' instead of '{}'", canonical_path.string(), reported_provider_id, expected_provider_id));
            this->plugin_api = loaded_api;
            this->descriptor = description.descriptor;
        } catch (...) {
#if defined(_WIN32)
            FreeLibrary(loaded);
#else
            dlclose(loaded);
#endif
            throw;
        }
        this->library_handle = loaded;
    }

    DynamicsRuntime::ProviderLibrary::~ProviderLibrary() {
        if (!this->library_handle) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(this->library_handle));
#else
        dlclose(this->library_handle);
#endif
    }

    DynamicsRuntime::ProviderLibrary& DynamicsRuntime::provider_library(const std::string_view provider_id) const {
        const auto found = this->providers.by_id.find(std::string{provider_id});
        if (found == this->providers.by_id.end()) throw std::runtime_error(std::format("Scene Dynamics does not contain Provider '{}'", provider_id));
        return *found->second;
    }

    SpectraPluginResult DynamicsRuntime::collect_dataset(void* context, const std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit) noexcept {
        DynamicsRuntime& world = *static_cast<DynamicsRuntime*>(context);
        try {
            DynamicSystemRuntime& system   = *world.publication.publishing_system;
            DynamicDatasetRuntime& dataset = world.dataset_runtime(system, dataset_index);
            if (std::ranges::contains(world.publication.dataset_commits, &dataset, &PendingDatasetCommit::dataset)) throw std::runtime_error("Provider committed the same GPU Dataset more than once in one publication");
            world.publication.dataset_commits.emplace_back(&system, &dataset, *commit);
            return {};
        } catch (const std::exception& error) {
            world.publication.callback_error = error.what();
            return {{world.publication.callback_error.data(), world.publication.callback_error.size()}};
        }
    }

    SpectraPluginResult DynamicsRuntime::collect_telemetry(void* context, const SpectraPluginTelemetryCommit* commit) noexcept {
        DynamicsRuntime& world = *static_cast<DynamicsRuntime*>(context);
        try {
            if (std::ranges::contains(world.publication.telemetry_commits, world.publication.publishing_system, &PendingTelemetryCommit::system)) throw std::runtime_error("Provider committed Telemetry more than once in one publication");
            world.publication.telemetry_commits.emplace_back(world.publication.publishing_system, commit->slot_index, commit->signal_value, plugin_string(commit->phase), plugin_string(commit->headline), plugin_string(commit->message));
            return {};
        } catch (const std::exception& error) {
            world.publication.callback_error = error.what();
            return {{world.publication.callback_error.data(), world.publication.callback_error.size()}};
        }
    }

    void DynamicsRuntime::bind_dataset(DynamicDatasetRuntime& dataset, const scene::DynamicSystem& system) const {
        const auto scene_binding = std::ranges::find(system.scene_bindings, dataset.descriptor.id, &scene::DynamicSceneBinding::dataset_id);
        if (scene_binding != system.scene_bindings.end()) {
            if (!dataset.descriptor.resource_kind) throw std::runtime_error(std::format("Dataset '{}' kind cannot bind to a Scene resource", dataset.descriptor.id));
            if (*dataset.descriptor.resource_kind == scene::DynamicSceneResourceKind::Geometry) {
                const auto geometry = std::ranges::find(this->configuration.source_scene->resources.geometries, scene::GeometryId{scene_binding->resource_id}, &scene::Geometry::id);
                if (geometry == this->configuration.source_scene->resources.geometries.end() || !std::holds_alternative<scene::TriangleMeshGeometry>(geometry->data)) throw std::runtime_error(std::format("Triangle Mesh Dataset '{}' references a missing or non-mesh Scene Geometry", dataset.descriptor.id));
            } else if (*dataset.descriptor.resource_kind == scene::DynamicSceneResourceKind::SphereSet) {
                if (std::ranges::find(this->configuration.source_scene->resources.sphere_sets, scene::SphereSetId{scene_binding->resource_id}, &scene::SphereSet::id) == this->configuration.source_scene->resources.sphere_sets.end()) throw std::runtime_error(std::format("Sphere Set Dataset '{}' references a missing Scene Sphere Set", dataset.descriptor.id));
            } else if (std::ranges::find(this->configuration.source_scene->resources.volumes, scene::VolumeId{scene_binding->resource_id}, &scene::Volume::id) == this->configuration.source_scene->resources.volumes.end())
                throw std::runtime_error(std::format("Field Dataset '{}' references a missing Scene Volume", dataset.descriptor.id));
            dataset.scene_binding = *scene_binding;
        }
        for (const scene::DynamicVisualizationView& view : system.visualizations) {
            if (view.dataset_id != dataset.descriptor.id) continue;
            const bool compatible = (std::holds_alternative<dynamics::PointDataset>(dataset.descriptor.details) && std::holds_alternative<scene::PointVisualization>(view.data)) || (std::holds_alternative<dynamics::SegmentDataset>(dataset.descriptor.details) && std::holds_alternative<scene::SegmentVisualization>(view.data)) || (std::holds_alternative<dynamics::CurveDataset>(dataset.descriptor.details) && std::holds_alternative<scene::CurveVisualization>(view.data)) || (std::holds_alternative<dynamics::VectorDataset>(dataset.descriptor.details) && std::holds_alternative<scene::VectorVisualization>(view.data)) || (std::holds_alternative<dynamics::FieldDataset>(dataset.descriptor.details) && (std::holds_alternative<scene::FieldSliceVisualization>(view.data) || std::holds_alternative<scene::FieldVectorVisualization>(view.data))) || (std::holds_alternative<dynamics::ImageDataset>(dataset.descriptor.details) && std::holds_alternative<scene::ImageVisualization>(view.data))
                                 || (std::holds_alternative<dynamics::CameraObservationDataset>(dataset.descriptor.details) && std::holds_alternative<scene::CameraObservationVisualization>(view.data)) || (std::holds_alternative<dynamics::TransformDataset>(dataset.descriptor.details) && std::holds_alternative<scene::FrameVisualization>(view.data)) || (std::holds_alternative<dynamics::TriangleMeshDataset>(dataset.descriptor.details) && std::holds_alternative<scene::SurfaceVisualization>(view.data));
            if (!compatible) throw std::runtime_error(std::format("Visualization '{}' kind is incompatible with Dataset '{}'", view.name, dataset.descriptor.id));
            dataset.visualizations.emplace_back(view);
        }
        const bool implicit_scene_state = std::holds_alternative<dynamics::InstanceTransformDataset>(dataset.descriptor.details);
        if (!dataset.scene_binding && dataset.visualizations.empty() && !implicit_scene_state) throw std::runtime_error(std::format("GPU Dataset '{}' is neither bound to the Render Scene nor used by a Visualization", dataset.descriptor.id));
    }

    void DynamicsRuntime::declare_scene_output(const DynamicDatasetRuntime& dataset) {
        if (!dataset.scene_binding) return;
        const scene::DynamicSceneBinding& binding = *dataset.scene_binding;
        if (const auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&dataset.descriptor.details)) this->outputs.mesh_bindings.emplace_back(scene::GeometryId{binding.resource_id}, mesh->update_mode, mesh->vertex_capacity, mesh->index_capacity);
        if (const auto* spheres = std::get_if<dynamics::SphereSetDataset>(&dataset.descriptor.details)) this->outputs.sphere_set_bindings.emplace_back(scene::SphereSetId{binding.resource_id}, spheres->capacity);
    }

    void DynamicsRuntime::configure_dataset(DynamicSystemRuntime& system, const std::size_t dataset_index) {
        DynamicDatasetRuntime& dataset = system.datasets[dataset_index];
        if (dataset.output_pending) {
            this->context.runtime.resources.wait_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value);
            this->context.runtime.resources.signal_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value + 1);
            dataset.output_pending = false;
        }
        const auto [next_capacity, next_secondary_capacity] = dataset_capacities(dataset.descriptor);
        std::vector<std::vector<DynamicDatasetBuffer>> next_buffer_slots(VulkanFrames::frames_in_flight);
        GpuExternalTimelineSemaphore next_timeline_semaphore = this->context.runtime.resources.create_external_simulation_timeline();
        const std::vector<DatasetBufferLayout> layouts       = dataset_buffer_layouts(dataset.descriptor, next_capacity, next_secondary_capacity);
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index)
            for (const DatasetBufferLayout& layout : layouts) {
                if (layout.element_count > std::numeric_limits<std::uint64_t>::max() / layout.element_size) throw std::runtime_error("Dynamic Dataset buffer byte size overflows");
                DynamicDatasetBuffer buffer{.semantic = layout.semantic, .channel_index = layout.channel_index, .byte_size = std::max<std::uint64_t>(layout.element_count * layout.element_size, sizeof(std::uint32_t))};
                buffer.gpu_buffer = this->context.runtime.resources.create_external_buffer(buffer.byte_size, vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
                buffer.descriptor = this->context.runtime.frames.allocate_resource_descriptor();
                this->context.runtime.resources.write_buffer_descriptor(buffer.descriptor, vk::DescriptorType::eStorageBuffer, buffer.gpu_buffer);
                next_buffer_slots[slot_index].emplace_back(std::move(buffer));
            }

        std::vector<std::vector<SpectraPluginGpuBuffer>> plugin_slot_buffers(VulkanFrames::frames_in_flight);
        std::vector<std::vector<ExternalHandle>> exported_handles(VulkanFrames::frames_in_flight);
        std::vector<SpectraPluginGpuSlot> plugin_slots{};
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            for (DynamicDatasetBuffer& buffer : next_buffer_slots[slot_index]) {
                exported_handles[slot_index].emplace_back(this->context.runtime.resources.export_buffer_memory_handle(buffer.gpu_buffer));
                plugin_slot_buffers[slot_index].emplace_back(buffer.semantic, buffer.channel_index, plugin_external_handle(exported_handles[slot_index].back()), buffer.byte_size);
            }
            plugin_slots.emplace_back(slot_index, plugin_slot_buffers[slot_index].data(), plugin_slot_buffers[slot_index].size());
        }
        ExternalHandle timeline_handle        = this->context.runtime.resources.export_timeline_semaphore_handle(next_timeline_semaphore);
        const GpuDeviceIdentity& gpu_identity = this->context.runtime.graphics.identity;
        SpectraPluginDatasetConfiguration configuration{
            .dataset_index             = dataset_index,
            .slots                     = plugin_slots.data(),
            .slot_count                = plugin_slots.size(),
            .timeline_semaphore_handle = plugin_external_handle(timeline_handle),
            .vulkan_device_luid_valid  = static_cast<std::uint8_t>(gpu_identity.luid_valid),
            .vulkan_device_node_mask   = gpu_identity.node_mask,
        };
        std::ranges::copy(gpu_identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(gpu_identity.luid, configuration.vulkan_device_luid);
#if !defined(_WIN32)
        for (std::vector<ExternalHandle>& handles : exported_handles)
            for (ExternalHandle& handle : handles) static_cast<void>(handle.release());
        static_cast<void>(timeline_handle.release());
#endif
        check_plugin_result(system.plugin_api->configure_dataset(system.provider_instance, &configuration), "Dataset configuration");

        std::vector<std::vector<DynamicDatasetBuffer>> previous_buffer_slots = std::exchange(dataset.buffer_slots, std::move(next_buffer_slots));
        GpuExternalTimelineSemaphore previous_timeline_semaphore             = std::exchange(dataset.timeline_semaphore, std::move(next_timeline_semaphore));
        dataset.capacity                                                     = next_capacity;
        dataset.secondary_capacity                                           = next_secondary_capacity;
        dataset.timeline_signal_value                                        = 0;
        if (!previous_buffer_slots.empty()) {
            this->context.runtime.frames.defer_destruction([buffer_slots = std::move(previous_buffer_slots), timeline_semaphore = std::move(previous_timeline_semaphore)]() mutable {});
        }
    }

    void DynamicsRuntime::configure_telemetry(DynamicSystemRuntime& system) {
        if (system.provider_descriptor->telemetry.empty()) {
            check_plugin_result(system.plugin_api->configure_telemetry(system.provider_instance, nullptr), "Telemetry configuration");
            return;
        }
        DynamicTelemetryRuntime next{};
        next.buffer_slots.resize(VulkanFrames::frames_in_flight);
        next.timeline_semaphore       = this->context.runtime.resources.create_external_simulation_timeline();
        const std::uint64_t byte_size = system.provider_descriptor->telemetry.size() * sizeof(SpectraPluginTelemetryGpuValue);
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            DynamicDatasetBuffer buffer{.semantic = SpectraPluginBufferSemantic::TelemetryValue, .byte_size = byte_size};
            buffer.gpu_buffer = this->context.runtime.resources.create_external_buffer(byte_size, vk::BufferUsageFlagBits::eTransferSrc);
            next.buffer_slots[slot_index].emplace_back(std::move(buffer));
            next.readback_slots[slot_index].buffer = this->context.runtime.resources.create_buffer(byte_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        }
        std::vector<std::vector<SpectraPluginGpuBuffer>> plugin_slot_buffers(VulkanFrames::frames_in_flight);
        std::vector<std::vector<ExternalHandle>> exported_handles(VulkanFrames::frames_in_flight);
        std::vector<SpectraPluginGpuSlot> plugin_slots{};
        for (std::uint32_t slot_index = 0; slot_index < VulkanFrames::frames_in_flight; ++slot_index) {
            DynamicDatasetBuffer& buffer = next.buffer_slots[slot_index].front();
            exported_handles[slot_index].emplace_back(this->context.runtime.resources.export_buffer_memory_handle(buffer.gpu_buffer));
            plugin_slot_buffers[slot_index].emplace_back(buffer.semantic, 0, plugin_external_handle(exported_handles[slot_index].back()), buffer.byte_size);
            plugin_slots.emplace_back(slot_index, plugin_slot_buffers[slot_index].data(), plugin_slot_buffers[slot_index].size());
        }
        ExternalHandle timeline_handle        = this->context.runtime.resources.export_timeline_semaphore_handle(next.timeline_semaphore);
        const GpuDeviceIdentity& gpu_identity = this->context.runtime.graphics.identity;
        SpectraPluginTelemetryConfiguration configuration{
            .slots                     = plugin_slots.data(),
            .slot_count                = plugin_slots.size(),
            .timeline_semaphore_handle = plugin_external_handle(timeline_handle),
            .vulkan_device_luid_valid  = static_cast<std::uint8_t>(gpu_identity.luid_valid),
            .vulkan_device_node_mask   = gpu_identity.node_mask,
        };
        std::ranges::copy(gpu_identity.uuid, configuration.vulkan_device_uuid);
        std::ranges::copy(gpu_identity.luid, configuration.vulkan_device_luid);
#if !defined(_WIN32)
        for (std::vector<ExternalHandle>& handles : exported_handles)
            for (ExternalHandle& handle : handles) static_cast<void>(handle.release());
        static_cast<void>(timeline_handle.release());
#endif
        check_plugin_result(system.plugin_api->configure_telemetry(system.provider_instance, &configuration), "Telemetry configuration");
        DynamicTelemetryRuntime previous = std::exchange(system.telemetry_gpu, std::move(next));
        if (!previous.buffer_slots.empty()) {
            this->context.runtime.frames.defer_destruction([telemetry = std::move(previous)]() mutable {});
        }
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

    void DynamicsRuntime::apply_parameters(DynamicSystemRuntime& system, const std::span<const scene::DynamicParameterValue> values) {
        std::vector<SpectraPluginParameterValue> encoded{};
        encoded.reserve(values.size());
        for (const scene::DynamicParameterValue& value : values) encoded.emplace_back(plugin_parameter_value(value));
        check_plugin_result(system.plugin_api->apply_parameters(system.provider_instance, encoded.data(), encoded.size()), "parameter application");
    }

    void DynamicsRuntime::append_dataset(const PendingDatasetCommit& pending, dynamics::DynamicSnapshot& snapshot) const {
        const DynamicSystemRuntime& system               = *pending.system;
        const DynamicDatasetRuntime& dataset             = *pending.dataset;
        const SpectraPluginDatasetCommit& commit         = pending.commit;
        const std::vector<DynamicDatasetBuffer>& buffers = dataset.buffer_slots[commit.slot_index];
        const auto gpu_buffer                            = [&buffers](const SpectraPluginBufferSemantic semantic, const std::uint32_t channel_index = 0) {
            const auto found = std::ranges::find_if(buffers, [semantic, channel_index](const DynamicDatasetBuffer& buffer) { return buffer.semantic == semantic && buffer.channel_index == channel_index; });
            if (found == buffers.end()) throw std::runtime_error(std::format("Dynamic Dataset is missing required GPU buffer semantic {} channel {}", std::to_underlying(semantic), channel_index));
            return dynamics::GpuBufferView{&found->gpu_buffer, found->descriptor};
        };

        if (const auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&dataset.descriptor.details); mesh && dataset.scene_binding) {
            dynamics::GpuTriangleMeshUpdate update{
                .geometry_id  = scene::GeometryId{dataset.scene_binding->resource_id},
                .positions    = gpu_buffer(SpectraPluginBufferSemantic::TrianglePosition),
                .indices      = dataset.secondary_capacity != 0 ? std::optional{gpu_buffer(SpectraPluginBufferSemantic::TriangleIndex)} : std::nullopt,
                .vertex_count = commit.active_count,
                .index_count  = commit.secondary_count,
                .update_mode  = mesh->update_mode,
            };
            if ((mesh->attributes & std::to_underlying(SpectraPluginMeshAttribute::Normal)) != 0) update.normals = gpu_buffer(SpectraPluginBufferSemantic::TriangleNormal);
            if ((mesh->attributes & std::to_underlying(SpectraPluginMeshAttribute::Tangent)) != 0) update.tangents = gpu_buffer(SpectraPluginBufferSemantic::TriangleTangent);
            if ((mesh->attributes & std::to_underlying(SpectraPluginMeshAttribute::TextureCoordinate)) != 0) update.texture_coordinates = gpu_buffer(SpectraPluginBufferSemantic::TriangleTextureCoordinate);
            snapshot.scene_updates.emplace_back(dynamics::GpuSceneUpdate{std::move(update)});
        } else if (const auto* spheres = std::get_if<dynamics::SphereSetDataset>(&dataset.descriptor.details); spheres && dataset.scene_binding)
            snapshot.scene_updates.emplace_back(dynamics::GpuSceneUpdate{dynamics::GpuSphereSetUpdate{scene::SphereSetId{dataset.scene_binding->resource_id}, gpu_buffer(SpectraPluginBufferSemantic::Sphere), commit.active_count}});
        else if (std::holds_alternative<dynamics::InstanceTransformDataset>(dataset.descriptor.details))
            snapshot.scene_updates.emplace_back(dynamics::GpuSceneUpdate{dynamics::GpuInstanceTransformUpdate{gpu_buffer(SpectraPluginBufferSemantic::InstanceTransform), commit.active_count}});
        else if (const auto* field = std::get_if<dynamics::FieldDataset>(&dataset.descriptor.details); field && dataset.scene_binding) {
            dynamics::GpuFieldUpdate update{.volume_id = scene::VolumeId{dataset.scene_binding->resource_id}, .resolution = field->resolution, .local_from_grid = field->local_from_grid, .dirty_region = scene::VolumeRegion{{commit.region_minimum[0], commit.region_minimum[1], commit.region_minimum[2]}, {commit.region_maximum[0], commit.region_maximum[1], commit.region_maximum[2]}}};
            for (std::uint32_t channel_index = 0; channel_index < field->channels.size(); ++channel_index) update.channels.emplace_back(field->channels[channel_index], gpu_buffer(SpectraPluginBufferSemantic::FieldChannel, channel_index));
            snapshot.scene_updates.emplace_back(dynamics::GpuSceneUpdate{std::move(update)});
        }

        if (!this->configuration.setup.systems[system.scene_system_index].visible || commit.active_count == 0) return;
        for (const scene::DynamicVisualizationView& view : dataset.visualizations) {
            math::Transform transform{};
            if (view.anchor.value != 0) transform = std::ranges::find(this->configuration.source_scene->resources.instances, view.anchor, &scene::Instance::id)->transform;
            dynamics::VisualizationStyle style{view, transform};
            if (std::holds_alternative<dynamics::PointDataset>(dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuPointVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::Point), commit.active_count}});
            else if (std::holds_alternative<dynamics::SegmentDataset>(dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuSegmentVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::Segment), commit.active_count}});
            else if (std::holds_alternative<dynamics::CurveDataset>(dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuCurveVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::Curve), commit.active_count}});
            else if (std::holds_alternative<dynamics::VectorDataset>(dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuVectorVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::Vector), commit.active_count}});
            else if (const auto* field = std::get_if<dynamics::FieldDataset>(&dataset.descriptor.details)) {
                const std::string& channel_id = std::holds_alternative<scene::FieldSliceVisualization>(view.data) ? std::get<scene::FieldSliceVisualization>(view.data).channel_id : std::get<scene::FieldVectorVisualization>(view.data).channel_id;
                const auto found              = channel_id.empty() ? field->channels.begin() : std::ranges::find(field->channels, channel_id, &dynamics::FieldChannelDescriptor::id);
                if (found == field->channels.end()) throw std::runtime_error(std::format("Visualization '{}' references missing field channel '{}'", view.name, channel_id));
                const std::uint32_t channel_index = static_cast<std::uint32_t>(std::distance(field->channels.begin(), found));
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuFieldVisualization{style, field->resolution, field->local_from_grid, {*found, gpu_buffer(SpectraPluginBufferSemantic::FieldChannel, channel_index)}}});
            } else if (const auto* image = std::get_if<dynamics::ImageDataset>(&dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuImageVisualization{style, *image, gpu_buffer(SpectraPluginBufferSemantic::ImagePixel)}});
            else if (const auto* cameras = std::get_if<dynamics::CameraObservationDataset>(&dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuCameraObservationVisualization{style, *cameras, gpu_buffer(SpectraPluginBufferSemantic::CameraObservation), gpu_buffer(SpectraPluginBufferSemantic::ImagePixel), commit.active_count}});
            else if (std::holds_alternative<dynamics::TransformDataset>(dataset.descriptor.details))
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuTransformVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::Transform), commit.active_count}});
            else if (const auto* mesh = std::get_if<dynamics::TriangleMeshDataset>(&dataset.descriptor.details)) {
                if (std::get<scene::SurfaceVisualization>(view.data).color_source == scene::VisualizationColorSource::Scalar && (mesh->attributes & std::to_underlying(SpectraPluginMeshAttribute::Scalar)) == 0) throw std::runtime_error(std::format("Surface Visualization '{}' requires the Triangle Mesh scalar semantic", view.name));
                snapshot.visualizations.emplace_back(dynamics::GpuVisualization{dynamics::GpuSurfaceVisualization{style, gpu_buffer(SpectraPluginBufferSemantic::TrianglePosition), dataset.secondary_capacity != 0 ? std::optional{gpu_buffer(SpectraPluginBufferSemantic::TriangleIndex)} : std::nullopt, (mesh->attributes & std::to_underlying(SpectraPluginMeshAttribute::Scalar)) != 0 ? std::optional{gpu_buffer(SpectraPluginBufferSemantic::TriangleScalar)} : std::nullopt, commit.active_count, commit.secondary_count}});
            }
        }
    }

    void DynamicsRuntime::abort_publication() {
        for (const PendingDatasetCommit& pending : this->publication.dataset_commits) {
            this->context.runtime.resources.wait_external_timeline(pending.dataset->timeline_semaphore, pending.commit.signal_value);
            this->context.runtime.resources.signal_external_timeline(pending.dataset->timeline_semaphore, pending.commit.signal_value + 1);
        }
        for (const PendingTelemetryCommit& pending : this->publication.telemetry_commits) {
            DynamicTelemetryRuntime& telemetry = pending.system->telemetry_gpu;
            this->context.runtime.resources.wait_external_timeline(telemetry.timeline_semaphore, pending.signal_value);
            this->context.runtime.resources.signal_external_timeline(telemetry.timeline_semaphore, pending.signal_value + 1);
        }
        this->publication.dataset_commits.clear();
        this->publication.telemetry_commits.clear();
    }

    void DynamicsRuntime::commit_publication(dynamics::DynamicSnapshot& snapshot, const std::uint64_t simulation_step) {
        for (const PendingDatasetCommit& pending : this->publication.dataset_commits) {
            if (pending.commit.slot_index >= pending.dataset->buffer_slots.size()) throw std::runtime_error("Provider committed an invalid GPU Dataset slot");
            if (pending.commit.active_count > pending.dataset->capacity || pending.commit.secondary_count > pending.dataset->secondary_capacity) throw std::runtime_error("Provider committed more GPU Dataset elements than configured");
        }
        for (const PendingTelemetryCommit& pending : this->publication.telemetry_commits)
            if (pending.slot_index >= pending.system->telemetry_gpu.buffer_slots.size()) throw std::runtime_error("Provider committed an invalid Telemetry slot");
        const std::size_t visualization_count = std::ranges::fold_left(this->publication.dataset_commits, std::size_t{}, [](const std::size_t count, const PendingDatasetCommit& pending) { return count + pending.dataset->visualizations.size(); });
        snapshot.scene_updates.reserve(this->publication.dataset_commits.size());
        snapshot.visualizations.reserve(visualization_count);
        for (const PendingDatasetCommit& pending : this->publication.dataset_commits) this->append_dataset(pending, snapshot);
        for (const PendingDatasetCommit& pending : this->publication.dataset_commits) {
            DynamicDatasetRuntime& dataset = *pending.dataset;
            dataset.timeline_signal_value  = pending.commit.signal_value;
            dataset.output_pending         = true;
        }
        for (PendingTelemetryCommit& pending : this->publication.telemetry_commits) {
            DynamicTelemetryRuntime& telemetry = pending.system->telemetry_gpu;
            telemetry.current_slot_index       = pending.slot_index;
            telemetry.timeline_signal_value    = pending.signal_value;
            telemetry.simulation_step          = simulation_step;
            telemetry.simulation_seconds       = static_cast<double>(simulation_step) * this->configuration.setup.clock.step_seconds;
            telemetry.phase                    = std::move(pending.phase);
            telemetry.headline                 = std::move(pending.headline);
            telemetry.message                  = std::move(pending.message);
            telemetry.output_pending           = true;
        }
        this->publication.dataset_commits.clear();
        this->publication.telemetry_commits.clear();
    }

    void DynamicsRuntime::discard_pending_snapshot() {
        for (DynamicSystemRuntime& system : this->systems.runtimes) {
            for (DynamicDatasetRuntime& dataset : system.datasets)
                if (dataset.output_pending) {
                    this->context.runtime.resources.wait_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value);
                    this->context.runtime.resources.signal_external_timeline(dataset.timeline_semaphore, dataset.timeline_signal_value + 1);
                    dataset.output_pending = false;
                }
            DynamicTelemetryRuntime& telemetry = system.telemetry_gpu;
            if (telemetry.output_pending) {
                this->context.runtime.resources.wait_external_timeline(telemetry.timeline_semaphore, telemetry.timeline_signal_value);
                this->context.runtime.resources.signal_external_timeline(telemetry.timeline_semaphore, telemetry.timeline_signal_value + 1);
                telemetry.output_pending = false;
            }
        }
        this->publication.snapshot_pending  = false;
        this->publication.snapshot_acquired = false;
    }

    void DynamicsRuntime::publish_snapshot(const std::uint64_t simulation_step) {
        if (this->publication.snapshot_acquired) throw std::runtime_error("Cannot replace an acquired Dynamics snapshot before it is consumed");
        this->discard_pending_snapshot();
        dynamics::DynamicSnapshot snapshot{};
        const std::size_t dataset_count = std::ranges::fold_left(this->systems.runtimes, std::size_t{}, [](const std::size_t count, const DynamicSystemRuntime& system) { return count + system.datasets.size(); });
        this->publication.dataset_commits.reserve(dataset_count);
        this->publication.telemetry_commits.reserve(this->systems.runtimes.size());
        try {
            for (DynamicSystemRuntime& system : this->systems.runtimes) {
                this->publication.publishing_system = &system;
                this->publication.callback_error.clear();
                const SpectraPluginSnapshotSink sink{this, &DynamicsRuntime::collect_dataset, &DynamicsRuntime::collect_telemetry};
                check_plugin_result(system.plugin_api->publish_snapshot(system.provider_instance, simulation_step, &sink), "snapshot publication");
                if (!this->publication.callback_error.empty()) throw std::runtime_error(this->publication.callback_error);
            }
            this->commit_publication(snapshot, simulation_step);
        } catch (...) {
            this->abort_publication();
            this->publication.publishing_system = nullptr;
            throw;
        }
        this->publication.publishing_system = nullptr;
        this->publication.snapshot          = std::move(snapshot);
        this->publication.snapshot_pending  = true;
    }

    void DynamicsRuntime::step_to(const std::uint64_t target_step) {
        if (target_step <= this->clock.simulation_step) return;
        const std::uint64_t step_count = target_step - this->clock.simulation_step;
        for (DynamicSystemRuntime& system : this->systems.runtimes) check_plugin_result(system.plugin_api->step(system.provider_instance, this->configuration.setup.clock.step_seconds, step_count), "simulation step");
        this->clock.simulation_step = target_step;
    }

    void DynamicsRuntime::reset_systems() {
        for (DynamicSystemRuntime& system : this->systems.runtimes) check_plugin_result(system.plugin_api->reset(system.provider_instance, this->configuration.setup.seed), "reset");
        this->clock.simulation_step = 0;
        this->step_to(this->configuration.setup.clock.start_step);
    }

    void DynamicsRuntime::evaluate_frame(const std::uint64_t target_step) {
        if (target_step < this->configuration.setup.clock.start_step) throw std::runtime_error("Requested Dynamics step precedes the configured start step");
        if (target_step < this->clock.simulation_step) this->reset_systems();
        this->step_to(target_step);
        this->publish_snapshot(target_step);
    }

    void DynamicsRuntime::reset_simulation() {
        this->reset_systems();
        this->publish_snapshot(this->clock.simulation_step);
    }

    void DynamicsRuntime::advance_one_step() {
        if (this->configuration.setup.clock.end_step && this->clock.simulation_step >= *this->configuration.setup.clock.end_step) {
            if (!this->configuration.setup.clock.loop) return;
            this->reset_systems();
            this->publish_snapshot(this->clock.simulation_step);
            return;
        }
        if (this->clock.simulation_step == std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("Scene Dynamics fixed-step timeline is exhausted");
        const std::uint64_t requested = this->clock.simulation_step + 1;
        this->evaluate_frame(this->configuration.setup.clock.end_step ? std::min(requested, *this->configuration.setup.clock.end_step) : requested);
    }

} // namespace spectra
