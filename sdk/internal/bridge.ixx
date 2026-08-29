module;

#include "abi.h"
#include <cuda_runtime_api.h>
#include <spectra/sdk/cuda_types.h>

export module spectra.sdk.internal.bridge;

import spectra.sdk;
import spectra.sdk.cuda;
import std;

export namespace spectra::sdk::internal {
    template <typename Class, typename Type>
    Class member_owner(Type Class::*);

    template <typename Class, typename Type>
    Type member_value(Type Class::*);

    [[nodiscard]] constexpr SpectraSdkString abi_string(const std::string_view value) noexcept {
        return {value.data(), value.size()};
    }

    [[nodiscard]] constexpr SpectraSdkParameterApplication abi_parameter_application(const ParameterApplication application) noexcept {
        switch (application) {
            case ParameterApplication::Live: return SpectraSdkParameterApplication::Live;
            case ParameterApplication::Reset: return SpectraSdkParameterApplication::Reset;
            case ParameterApplication::Recreate: return SpectraSdkParameterApplication::Recreate;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr SpectraSdkOutputKind abi_output_kind(const OutputKind kind) noexcept {
        switch (kind) {
            case OutputKind::Mesh: return SpectraSdkOutputKind::Mesh;
            case OutputKind::Spheres: return SpectraSdkOutputKind::Spheres;
            case OutputKind::Volume: return SpectraSdkOutputKind::Volume;
            case OutputKind::Instances: return SpectraSdkOutputKind::Instances;
            case OutputKind::Particles: return SpectraSdkOutputKind::Particles;
            case OutputKind::Lines: return SpectraSdkOutputKind::Lines;
            case OutputKind::Vectors: return SpectraSdkOutputKind::Vectors;
            case OutputKind::Image: return SpectraSdkOutputKind::Image;
            case OutputKind::HashGridRadianceField: return SpectraSdkOutputKind::HashGridRadianceField;
            case OutputKind::Cameras: return SpectraSdkOutputKind::Cameras;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr SpectraSdkFieldKind abi_field_kind(const FieldKind kind) noexcept {
        switch (kind) {
            case FieldKind::Float: return SpectraSdkFieldKind::Float;
            case FieldKind::Float3: return SpectraSdkFieldKind::Float3;
            case FieldKind::UInt32: return SpectraSdkFieldKind::UInt32;
            case FieldKind::MacFloat3: return SpectraSdkFieldKind::MacFloat3;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr SpectraSdkVolumeFieldSampling abi_field_sampling(const VolumeFieldSampling sampling) noexcept {
        switch (sampling) {
            case VolumeFieldSampling::Cell: return SpectraSdkVolumeFieldSampling::Cell;
            case VolumeFieldSampling::Vertex: return SpectraSdkVolumeFieldSampling::Vertex;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr SpectraSdkVolumeVectorSpace abi_vector_space(const VolumeVectorSpace space) noexcept {
        switch (space) {
            case VolumeVectorSpace::Grid: return SpectraSdkVolumeVectorSpace::Grid;
            case VolumeVectorSpace::Local: return SpectraSdkVolumeVectorSpace::Local;
            case VolumeVectorSpace::World: return SpectraSdkVolumeVectorSpace::World;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr std::uint32_t abi_mesh_attributes(const MeshAttribute attributes) noexcept {
        std::uint32_t result{};
        if (contains(attributes, MeshAttribute::Normal)) result |= std::to_underlying(SpectraSdkMeshAttribute::Normal);
        if (contains(attributes, MeshAttribute::Tangent)) result |= std::to_underlying(SpectraSdkMeshAttribute::Tangent);
        if (contains(attributes, MeshAttribute::TextureCoordinate)) result |= std::to_underlying(SpectraSdkMeshAttribute::TextureCoordinate);
        if (contains(attributes, MeshAttribute::Color)) result |= std::to_underlying(SpectraSdkMeshAttribute::Color);
        if (contains(attributes, MeshAttribute::Scalar)) result |= std::to_underlying(SpectraSdkMeshAttribute::Scalar);
        return result;
    }

    template <typename Type>
    [[nodiscard]] constexpr SpectraSdkValueKind value_kind() noexcept {
        if constexpr (std::same_as<Type, bool>) return SpectraSdkValueKind::Boolean;
        if constexpr (std::is_enum_v<Type>) return SpectraSdkValueKind::Enumeration;
        if constexpr (std::integral<Type>) return SpectraSdkValueKind::Integer;
        if constexpr (std::floating_point<Type>) return SpectraSdkValueKind::Float;
        return SpectraSdkValueKind::Float3;
    }

    template <typename Type>
    [[nodiscard]] constexpr SpectraSdkValue abi_value(const Type value) noexcept {
        SpectraSdkValue result{.kind = value_kind<Type>()};
        if constexpr (std::same_as<Type, bool>) result.integer = value ? 1 : 0;
        else if constexpr (std::is_enum_v<Type>) result.integer = std::to_underlying(value);
        else if constexpr (std::integral<Type>) result.integer = value;
        else if constexpr (std::floating_point<Type>) result.floating[0] = value;
        else result.floating[0] = value.x, result.floating[1] = value.y, result.floating[2] = value.z;
        return result;
    }

    template <typename Type>
    [[nodiscard]] constexpr Type sdk_value(const SpectraSdkValue& value) noexcept {
        if constexpr (std::same_as<Type, bool>) return value.integer != 0;
        else if constexpr (std::is_enum_v<Type>) return static_cast<Type>(value.integer);
        else if constexpr (std::integral<Type>) return static_cast<Type>(value.integer);
        else if constexpr (std::floating_point<Type>) return static_cast<Type>(value.floating[0]);
        else return {static_cast<float>(value.floating[0]), static_cast<float>(value.floating[1]), static_cast<float>(value.floating[2])};
    }

    template <typename Provider>
    struct ProviderBridge {
        template <typename Definition>
        static constexpr std::size_t parameter_count = Definition::category == DefinitionCategory::Parameter ? 1u : 0u;

        template <typename Definition>
        static constexpr std::size_t output_count = Definition::category == DefinitionCategory::Output ? 1u : 0u;

        template <typename Definition>
        static constexpr std::size_t metric_count = Definition::category == DefinitionCategory::Metric ? 1u : 0u;

        template <std::size_t... Index>
        [[nodiscard]] static consteval std::size_t count_parameters(std::index_sequence<Index...>) noexcept {
            return (parameter_count<std::tuple_element_t<Index, decltype(Provider::description.definitions)>> + ... + 0u);
        }

        template <std::size_t... Index>
        [[nodiscard]] static consteval std::size_t count_outputs(std::index_sequence<Index...>) noexcept {
            return (output_count<std::tuple_element_t<Index, decltype(Provider::description.definitions)>> + ... + 0u);
        }

        template <std::size_t... Index>
        [[nodiscard]] static consteval std::size_t count_metrics(std::index_sequence<Index...>) noexcept {
            return (metric_count<std::tuple_element_t<Index, decltype(Provider::description.definitions)>> + ... + 0u);
        }

        static constexpr std::size_t definition_count = std::tuple_size_v<decltype(Provider::description.definitions)>;
        static constexpr std::size_t parameters_size  = count_parameters(std::make_index_sequence<definition_count>{});
        static constexpr std::size_t outputs_size     = count_outputs(std::make_index_sequence<definition_count>{});
        static constexpr std::size_t metrics_size     = count_metrics(std::make_index_sequence<definition_count>{});

        struct Storage {
            std::array<SpectraSdkParameterDescriptor, parameters_size> parameters{};
            std::array<SpectraSdkOutputDescriptor, outputs_size> outputs{};
            std::array<SpectraSdkMetricDescriptor, metrics_size> metrics{};
            std::vector<std::vector<SpectraSdkString>> enumerators{};
            std::vector<std::vector<SpectraSdkFieldDescriptor>> fields{};
            SpectraSdkProviderDescriptor provider{};

            Storage() {
                enumerators.resize(parameters_size);
                fields.resize(outputs_size);
                std::size_t parameter_index{};
                std::size_t output_index{};
                std::size_t metric_index{};
                std::apply(
                    [this, &parameter_index, &output_index, &metric_index](const auto&... definition) {
                        ([this, &parameter_index, &output_index, &metric_index](const auto& value) {
                            if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Parameter) {
                                decltype(member_owner(std::remove_cvref_t<decltype(value)>::member)) defaults{};
                                std::vector<SpectraSdkString>& names = enumerators[parameter_index];
                                for (const std::string_view name : value.options.enumerators) names.emplace_back(abi_string(name));
                                parameters[parameter_index++] = {
                                    abi_string(std::remove_cvref_t<decltype(value)>::id.view()),
                                    abi_string(value.name),
                                    abi_string(value.unit),
                                    abi_string(value.options.description),
                                    abi_string(value.options.section),
                                    abi_parameter_application(value.options.application),
                                    abi_value(defaults.*std::remove_cvref_t<decltype(value)>::member),
                                    abi_value(static_cast<decltype(member_value(std::remove_cvref_t<decltype(value)>::member))>(value.options.minimum)),
                                    abi_value(static_cast<decltype(member_value(std::remove_cvref_t<decltype(value)>::member))>(value.options.maximum)),
                                    abi_value(static_cast<decltype(member_value(std::remove_cvref_t<decltype(value)>::member))>(value.options.step)),
                                    names.data(),
                                    names.size(),
                                };
                            } else if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Output) {
                                std::vector<SpectraSdkFieldDescriptor>& values = fields[output_index];
                                if constexpr (requires { value.fields; }) {
                                    std::apply(
                                        [&values](const auto&... field) {
                                            (values.emplace_back(
                                                abi_string(std::remove_cvref_t<decltype(field)>::id.view()),
                                                abi_string(field.name),
                                                abi_string(field.unit),
                                                abi_field_kind(std::remove_cvref_t<decltype(field)>::kind),
                                                [&field] {
                                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(field.options)>, VolumeFieldOptions>) return abi_field_sampling(field.options.sampling);
                                                    return SpectraSdkVolumeFieldSampling::Cell;
                                                }(),
                                                [&field] {
                                                    if constexpr (std::same_as<std::remove_cvref_t<decltype(field.options)>, VolumeFieldOptions>) return abi_vector_space(field.options.vector_space);
                                                    return SpectraSdkVolumeVectorSpace::Local;
                                                }()
                                            ), ...);
                                        },
                                        value.fields
                                    );
                                }
                                const MeshAttribute attributes = []<typename Definition>(const Definition& definition) {
                                    if constexpr (requires { definition.options.attributes; }) return definition.options.attributes;
                                    return MeshAttribute{};
                                }(value);
                                outputs[output_index++] = {abi_string(std::remove_cvref_t<decltype(value)>::id.view()), abi_output_kind(std::remove_cvref_t<decltype(value)>::kind), abi_mesh_attributes(attributes), values.data(), values.size()};
                            } else {
                                constexpr SpectraSdkValueKind kind = std::remove_cvref_t<decltype(value)>::is_boolean ? SpectraSdkValueKind::Boolean : std::remove_cvref_t<decltype(value)>::is_integral ? SpectraSdkValueKind::Integer : std::remove_cvref_t<decltype(value)>::is_floating ? SpectraSdkValueKind::Float : SpectraSdkValueKind::Float3;
                                metrics[metric_index++] = {abi_string(std::remove_cvref_t<decltype(value)>::id.view()), abi_string(value.name), abi_string(value.unit), abi_string(value.section), kind, static_cast<std::uint8_t>(value.plot)};
                            }
                        }(definition), ...);
                    },
                    Provider::description.definitions
                );
                provider = {abi_string(Provider::description.id), parameters.data(), parameters.size(), outputs.data(), outputs.size(), metrics.data(), metrics.size()};
            }
        };

        struct Instance {
            Provider provider;
            cuda::Output output;

            Instance(std::remove_cvref_t<decltype(std::declval<Provider>().settings)> settings, const std::filesystem::path& assets) : provider(std::move(settings), assets) {}
        };

        static thread_local std::string error;

        [[nodiscard]] static SpectraSdkResult success() noexcept {
            error.clear();
            return {};
        }

        [[nodiscard]] static SpectraSdkResult failure(const std::exception& exception) noexcept {
            error = exception.what();
            return {{error.data(), error.size()}};
        }

        [[nodiscard]] static SpectraSdkResult failure() noexcept {
            error = "Unknown Provider exception";
            return {{error.data(), error.size()}};
        }

        [[nodiscard]] static const Storage& storage() {
            static const Storage value{};
            return value;
        }

        [[nodiscard]] static SpectraSdkProviderDescriptionResult describe() noexcept {
            try {
                return {success(), storage().provider};
            } catch (const std::exception& exception) {
                return {failure(exception), {}};
            } catch (...) {
                return {failure(), {}};
            }
        }

        template <typename Settings>
        static void apply_settings(Settings& settings, const SpectraSdkValue* values) noexcept {
            std::size_t parameter_index{};
            std::apply(
                [&settings, values, &parameter_index](const auto&... definition) {
                    ([&settings, values, &parameter_index](const auto& value) {
                        if constexpr (std::remove_cvref_t<decltype(value)>::category == DefinitionCategory::Parameter) {
                            settings.*std::remove_cvref_t<decltype(value)>::member = sdk_value<decltype(member_value(std::remove_cvref_t<decltype(value)>::member))>(values[parameter_index++]);
                        }
                    }(definition), ...);
                },
                Provider::description.definitions
            );
        }

        [[nodiscard]] static SpectraSdkProviderCreateResult create(const SpectraSdkCreateInfo* info) noexcept {
            try {
                select_device(info);
                std::remove_cvref_t<decltype(std::declval<Provider>().settings)> settings{};
                apply_settings(settings, info->parameters);
                const std::filesystem::path assets{std::u8string{reinterpret_cast<const char8_t*>(info->assets.data), info->assets.size}};
                return {success(), new Instance{std::move(settings), assets}};
            } catch (const std::exception& exception) {
                return {failure(exception), nullptr};
            } catch (...) {
                return {failure(), nullptr};
            }
        }

        [[nodiscard]] static SpectraSdkResult destroy(void* source) noexcept {
            try {
                Instance* instance = static_cast<Instance*>(source);
                instance->output.synchronize();
                delete instance;
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static SpectraSdkResult setup(void* source, const SpectraSdkSetupSink* sink) noexcept {
            try {
                Instance& instance = *static_cast<Instance*>(source);
                cuda::Setup setup{sink};
                setup.declare(Provider::description);
                instance.provider.setup(setup);
                setup.complete();
                instance.output = cuda::Output{setup};
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static SpectraSdkResult apply_parameters(void* source, const SpectraSdkValue* values) noexcept {
            try {
                Instance& instance = *static_cast<Instance*>(source);
                apply_settings(instance.provider.settings, values);
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static SpectraSdkResult reset(void* source, const std::uint64_t seed) noexcept {
            try {
                static_cast<Instance*>(source)->provider.reset(seed);
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static SpectraSdkResult step(void* source, const double seconds, const std::uint64_t count) noexcept {
            try {
                Provider& provider = static_cast<Instance*>(source)->provider;
                for (std::uint64_t index = 0; index != count; ++index) provider.step(seconds);
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static SpectraSdkResult publish(void* source, const SpectraSdkPresentationFrame* presentation, SpectraSdkFrameCommit* commit) noexcept {
            try {
                Instance& instance = *static_cast<Instance*>(source);
                instance.output.prepare(commit);
                instance.provider.publish(instance.output, PresentationFrame{presentation->index, presentation->seconds});
                return success();
            } catch (const std::exception& exception) {
                return failure(exception);
            } catch (...) {
                return failure();
            }
        }

        [[nodiscard]] static const SpectraSdkApi* api() noexcept {
            static const SpectraSdkApi value{SPECTRA_SDK_ABI_VERSION, sizeof(SpectraSdkApi), &describe, &create, &destroy, &setup, &apply_parameters, &reset, &step, &publish};
            return &value;
        }

        static void select_device(const SpectraSdkCreateInfo* info) {
            int device_count{};
            if (cudaGetDeviceCount(&device_count) != cudaSuccess) throw std::runtime_error("cudaGetDeviceCount failed");
            for (int device = 0; device != device_count; ++device) {
                cudaDeviceProp properties{};
                if (cudaGetDeviceProperties(&properties, device) != cudaSuccess) throw std::runtime_error("cudaGetDeviceProperties failed");
                if (!std::ranges::equal(std::span{reinterpret_cast<const std::uint8_t*>(properties.uuid.bytes), 16u}, std::span{info->vulkan_device_uuid, 16u})) continue;
#if defined(_WIN32)
                if (info->vulkan_device_luid_valid && (!std::ranges::equal(std::span{reinterpret_cast<const std::uint8_t*>(properties.luid), 8u}, std::span{info->vulkan_device_luid, 8u}) || properties.luidDeviceNodeMask != info->vulkan_device_node_mask)) throw std::runtime_error("CUDA and Vulkan device identity disagree");
#endif
                if (cudaSetDevice(device) != cudaSuccess) throw std::runtime_error("cudaSetDevice failed");
                return;
            }
            throw std::runtime_error("CUDA cannot find the Vulkan physical device");
        }
    };

    template <typename Provider>
    thread_local std::string ProviderBridge<Provider>::error{};
}
