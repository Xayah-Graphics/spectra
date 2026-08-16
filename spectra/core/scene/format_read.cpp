module;

#include <kdl/kdl.h>
#include <kdlpp.h>

module spectra.scene.format;
import spectra.scene.asset_import;
import std;

namespace spectra::scene {
    namespace {
        constexpr std::uint32_t current_scene_format_version = 36;
        [[nodiscard]] std::string kdl_text(const std::u8string_view value) {
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::string kdl_value_text(const kdl::Value& value) {
            return kdl_text(value.as<std::u8string_view>());
        }

        template <class Value>
        [[nodiscard]] Value kdl_number(const kdl::Value& value) {
            const kdl::Number number = value.as<kdl::Number>();
            if (number.representation() != kdl::NumberRepresentation::String) return number.as<Value>();
            const ::kdl_number encoded = static_cast<::kdl_number>(number);
            Value result{};
            std::from_chars(encoded.string.data, encoded.string.data + encoded.string.len, result);
            return result;
        }

        [[nodiscard]] const kdl::Value* kdl_property(const kdl::Node& node, const std::u8string_view name) {
            const auto found = node.properties().find(name);
            return found == node.properties().end() ? nullptr : &found->second;
        }

        template <class Value>
        [[nodiscard]] Value kdl_number_property(const kdl::Node& node, const std::u8string_view name, const Value default_value) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? default_value : kdl_number<Value>(*value);
        }

        [[nodiscard]] bool kdl_bool_property(const kdl::Node& node, const std::u8string_view name, const bool default_value) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? default_value : value->as<bool>();
        }

        [[nodiscard]] std::string kdl_string_property(const kdl::Node& node, const std::u8string_view name, const std::string_view default_value = {}) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? std::string{default_value} : kdl_value_text(*value);
        }

        [[nodiscard]] const kdl::Node* kdl_child(const kdl::Node& node, const std::u8string_view name) {
            const auto found = std::ranges::find(node.children(), name, &kdl::Node::name);
            return found == node.children().end() ? nullptr : &*found;
        }

        [[nodiscard]] math::Float2 read_float2(const kdl::Node& node, const std::size_t offset = 0) {
            return {kdl_number<float>(node.args()[offset]), kdl_number<float>(node.args()[offset + 1])};
        }

        [[nodiscard]] math::Float3 read_float3(const kdl::Node& node, const std::size_t offset = 0) {
            return {kdl_number<float>(node.args()[offset]), kdl_number<float>(node.args()[offset + 1]), kdl_number<float>(node.args()[offset + 2])};
        }

        [[nodiscard]] math::Transform read_transform(const kdl::Node& parent, const std::u8string_view name = u8"transform") {
            math::Transform transform{};
            const kdl::Node* node = kdl_child(parent, name);
            if (node == nullptr) return transform;
            for (std::uint32_t row = 0; row != 4; ++row)
                for (std::uint32_t column = 0; column != 4; ++column) transform.matrix[row * 4 + column] = kdl_number<float>(node->children()[row].args()[column]);
            return transform;
        }

        [[nodiscard]] math::Bounds3 read_bounds(const kdl::Node& parent, const math::Bounds3 default_value = {}) {
            const kdl::Node* node = kdl_child(parent, u8"bounds");
            if (node == nullptr) return default_value;
            return {read_float3(*node), read_float3(*node, 3)};
        }

        [[nodiscard]] SpectrumEncoding read_spectrum_encoding(const std::string_view value) {
            if (value == "rgb-albedo") return SpectrumEncoding::RgbAlbedo;
            if (value == "rgb-unbounded") return SpectrumEncoding::RgbUnbounded;
            if (value == "rgb-illuminant") return SpectrumEncoding::RgbIlluminant;
            if (value == "constant") return SpectrumEncoding::Constant;
            if (value == "blackbody") return SpectrumEncoding::Blackbody;
            if (value == "piecewise-linear") return SpectrumEncoding::PiecewiseLinear;
            throw std::runtime_error(std::format("Unknown Spectrum encoding {}", value));
        }

        [[nodiscard]] SpectrumColorSpace read_spectrum_color_space(const std::string_view value) {
            if (value == "srgb") return SpectrumColorSpace::Srgb;
            if (value == "rec2020") return SpectrumColorSpace::Rec2020;
            if (value == "aces2065-1") return SpectrumColorSpace::Aces2065_1;
            throw std::runtime_error(std::format("Unknown Spectrum color space {}", value));
        }

        [[nodiscard]] SpectrumParameter read_spectrum(const kdl::Node& node) {
            SpectrumParameter spectrum{};
            spectrum.encoding = read_spectrum_encoding(kdl_value_text(node.args()[0]));
            if (spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) spectrum.value = read_float3(node, 1);
            if (spectrum.encoding == SpectrumEncoding::Constant) spectrum.scalar = kdl_number<float>(node.args()[1]);
            if (spectrum.encoding == SpectrumEncoding::Blackbody) spectrum.temperature = kdl_number<float>(node.args()[1]);
            spectrum.texture.value = kdl_number_property<std::uint64_t>(node, u8"texture", 0);
            spectrum.color_space   = read_spectrum_color_space(kdl_string_property(node, u8"color-space", "srgb"));
            if (spectrum.encoding == SpectrumEncoding::PiecewiseLinear)
                for (const kdl::Node& sample : node.children()) {
                    spectrum.wavelengths.push_back(kdl_number<float>(sample.args()[0]));
                    spectrum.samples.push_back(kdl_number<float>(sample.args()[1]));
                }
            return spectrum;
        }

        [[nodiscard]] FloatParameter read_float_parameter(const kdl::Node& node) {
            return {
                .value   = kdl_number<float>(node.args()[0]),
                .texture = {kdl_number_property<std::uint64_t>(node, u8"texture", 0)},
            };
        }

        [[nodiscard]] MaterialRoughness read_roughness(const kdl::Node& node) {
            MaterialRoughness roughness{};
            if (const kdl::Node* value = kdl_child(node, u8"roughness")) roughness.roughness = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(node, u8"u-roughness")) roughness.u_roughness = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(node, u8"v-roughness")) roughness.v_roughness = read_float_parameter(*value);
            return roughness;
        }

        [[nodiscard]] TextureMapping read_texture_mapping(const kdl::Node& parent) {
            const kdl::Node* node = kdl_child(parent, u8"mapping");
            if (node == nullptr) return {};
            const std::string kind = kdl_value_text(node->args()[0]);
            if (kind == "uv") {
                UvTextureMapping value{};
                if (const kdl::Node* scale = kdl_child(*node, u8"scale")) value.scale = read_float2(*scale);
                if (const kdl::Node* offset = kdl_child(*node, u8"offset")) value.offset = read_float2(*offset);
                return {value};
            }
            if (kind == "planar") {
                PlanarTextureMapping value{};
                if (const kdl::Node* axis = kdl_child(*node, u8"first-axis")) value.first_axis = read_float3(*axis);
                if (const kdl::Node* axis = kdl_child(*node, u8"second-axis")) value.second_axis = read_float3(*axis);
                if (const kdl::Node* offset = kdl_child(*node, u8"offset")) value.offset = read_float2(*offset);
                value.texture_from_render = read_transform(*node);
                return {value};
            }
            if (kind == "spherical") return {SphericalTextureMapping{read_transform(*node)}};
            if (kind == "cylindrical") return {CylindricalTextureMapping{read_transform(*node)}};
            throw std::runtime_error(std::format("Unknown Texture mapping {}", kind));
        }

        [[nodiscard]] CheckerboardMapping read_checkerboard_mapping(const kdl::Node& parent) {
            const kdl::Node* node = kdl_child(parent, u8"mapping");
            if (node == nullptr) return {};
            if (kdl_value_text(node->args()[0]) == "3d") return {TextureMapping3D{read_transform(*node)}};
            return {read_texture_mapping(parent)};
        }

        [[nodiscard]] TextureValueKind read_texture_value_kind(const std::string_view value) {
            if (value == "float") return TextureValueKind::Float;
            if (value == "spectrum") return TextureValueKind::Spectrum;
            throw std::runtime_error(std::format("Unknown Texture value kind {}", value));
        }

        [[nodiscard]] TextureSpectrumType read_texture_spectrum_type(const std::string_view value) {
            if (value == "albedo") return TextureSpectrumType::Albedo;
            if (value == "unbounded") return TextureSpectrumType::Unbounded;
            if (value == "illuminant") return TextureSpectrumType::Illuminant;
            throw std::runtime_error(std::format("Unknown Texture Spectrum type {}", value));
        }

        [[nodiscard]] TextureColorSpace read_texture_color_space(const std::string_view value) {
            if (value == "linear") return TextureColorSpace::Linear;
            if (value == "srgb") return TextureColorSpace::Srgb;
            if (value == "aces2065-1") return TextureColorSpace::Aces2065_1;
            if (value == "rec2020") return TextureColorSpace::Rec2020;
            throw std::runtime_error(std::format("Unknown Texture color space {}", value));
        }

        [[nodiscard]] TextureWrapMode read_texture_wrap(const std::string_view value) {
            if (value == "repeat") return TextureWrapMode::Repeat;
            if (value == "clamp") return TextureWrapMode::Clamp;
            if (value == "black") return TextureWrapMode::Black;
            throw std::runtime_error(std::format("Unknown Texture wrap {}", value));
        }

        [[nodiscard]] TextureChannel read_texture_channel(const std::string_view value) {
            if (value == "red") return TextureChannel::Red;
            if (value == "green") return TextureChannel::Green;
            if (value == "blue") return TextureChannel::Blue;
            if (value == "alpha") return TextureChannel::Alpha;
            if (value == "average") return TextureChannel::Average;
            if (value == "luminance") return TextureChannel::Luminance;
            throw std::runtime_error(std::format("Unknown Texture channel {}", value));
        }

        [[nodiscard]] TextureFilter read_texture_filter(const std::string_view value) {
            if (value == "point") return TextureFilter::Point;
            if (value == "bilinear") return TextureFilter::Bilinear;
            if (value == "trilinear") return TextureFilter::Trilinear;
            if (value == "ewa") return TextureFilter::Ewa;
            throw std::runtime_error(std::format("Unknown Texture filter {}", value));
        }

        void read_texture_common(Texture& texture, const kdl::Node& node) {
            texture.id.value      = kdl_number<std::uint64_t>(node.args()[0]);
            texture.name          = kdl_value_text(node.args()[1]);
            texture.value_kind    = read_texture_value_kind(kdl_string_property(node, u8"value", "spectrum"));
            texture.spectrum_type = read_texture_spectrum_type(kdl_string_property(node, u8"spectrum-type", "albedo"));
            texture.color_space   = read_texture_color_space(kdl_string_property(node, u8"color-space", "srgb"));
        }

        void read_geometries(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Geometry geometry{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"triangle-mesh") {
                    geometry.data = TriangleMeshGeometry{.source = kdl_string_property(node, u8"source")};
                } else if (node.name() == u8"sphere") {
                    const float radius = kdl_number_property<float>(node, u8"radius", 1.0f);
                    geometry.data      = SphereGeometry{
                             .radius  = radius,
                             .z_min   = kdl_number_property<float>(node, u8"z-min", -radius),
                             .z_max   = kdl_number_property<float>(node, u8"z-max", radius),
                             .phi_max = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                } else if (node.name() == u8"box")
                    geometry.data = BoxGeometry{read_bounds(node, BoxGeometry{}.bounds)};
                else if (node.name() == u8"rectangle") {
                    RectangleGeometry rectangle{};
                    if (const kdl::Node* minimum = kdl_child(node, u8"minimum")) rectangle.minimum = read_float2(*minimum);
                    if (const kdl::Node* maximum = kdl_child(node, u8"maximum")) rectangle.maximum = read_float2(*maximum);
                    geometry.data = rectangle;
                } else if (node.name() == u8"disk")
                    geometry.data = DiskGeometry{
                        .height       = kdl_number_property<float>(node, u8"height", 0.0f),
                        .radius       = kdl_number_property<float>(node, u8"radius", 1.0f),
                        .inner_radius = kdl_number_property<float>(node, u8"inner-radius", 0.0f),
                        .phi_max      = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                else if (node.name() == u8"cylinder")
                    geometry.data = CylinderGeometry{
                        .radius  = kdl_number_property<float>(node, u8"radius", 1.0f),
                        .z_min   = kdl_number_property<float>(node, u8"z-min", -1.0f),
                        .z_max   = kdl_number_property<float>(node, u8"z-max", 1.0f),
                        .phi_max = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                else
                    throw std::runtime_error(std::format("Unknown Geometry {}", kdl_text(node.name())));
                resources.geometries.push_back(std::move(geometry));
            }
        }

        void read_sphere_sets(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.sphere_sets.push_back({
                    .id     = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name   = kdl_value_text(node.args()[1]),
                    .source = kdl_string_property(node, u8"source"),
                });
        }

        [[nodiscard]] VisualizationDepthMode read_depth_buffer_mode(std::string_view value);
        [[nodiscard]] VisualizationCompositionDomain read_visualization_composition_domain(std::string_view value);
        [[nodiscard]] VisualizationColorMap read_visualization_color_map(std::string_view value);

        [[nodiscard]] FieldMapping read_field_mapping(const std::string_view value) {
            if (value == "value") return FieldMapping::Value;
            if (value == "magnitude") return FieldMapping::Magnitude;
            if (value == "x") return FieldMapping::X;
            if (value == "y") return FieldMapping::Y;
            if (value == "z") return FieldMapping::Z;
            if (value == "divergence") return FieldMapping::Divergence;
            if (value == "curl-magnitude") return FieldMapping::CurlMagnitude;
            if (value == "q-criterion") return FieldMapping::QCriterion;
            throw std::runtime_error(std::format("Unknown Field mapping {}", value));
        }

        [[nodiscard]] ParticleDisplayMode read_particle_display(const std::string_view value) {
            if (value == "points") return ParticleDisplayMode::Points;
            if (value == "discs") return ParticleDisplayMode::Discs;
            if (value == "spheres") return ParticleDisplayMode::Spheres;
            throw std::runtime_error(std::format("Unknown Particle display {}", value));
        }

        void read_particle_sets(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                const kdl::Node& domain = *kdl_child(node, u8"bounds");
                ParticleSet particles{
                    .id        = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name      = kdl_value_text(node.args()[1]),
                    .domain    = {{kdl_number<float>(domain.args()[0]), kdl_number<float>(domain.args()[1]), kdl_number<float>(domain.args()[2])}, {kdl_number<float>(domain.args()[3]), kdl_number<float>(domain.args()[4]), kdl_number<float>(domain.args()[5])}},
                    .transform = read_transform(node),
                    .radius    = kdl_child(node, u8"radius") ? kdl_number<float>(kdl_child(node, u8"radius")->args()[0]) : 0.01f,
                    .visible   = kdl_bool_property(node, u8"visible", true),
                };
                if (const kdl::Node* visualization = kdl_child(node, u8"visualization")) {
                    particles.visualization = {
                        .field_id     = kdl_string_property(*visualization, u8"field"),
                        .display      = read_particle_display(kdl_string_property(*visualization, u8"display", "discs")),
                        .mapping      = read_field_mapping(kdl_string_property(*visualization, u8"mapping", "value")),
                        .depth_mode   = read_depth_buffer_mode(kdl_string_property(*visualization, u8"depth", "tested")),
                        .color_map    = read_visualization_color_map(kdl_string_property(*visualization, u8"color-map", "viridis")),
                        .minimum      = kdl_number_property<float>(*visualization, u8"minimum", 0.0f),
                        .maximum      = kdl_number_property<float>(*visualization, u8"maximum", 1.0f),
                        .radius_scale = kdl_number_property<float>(*visualization, u8"radius-scale", 1.0f),
                        .point_size   = kdl_number_property<float>(*visualization, u8"point-size", 2.0f),
                    };
                    if (const kdl::Node* color = kdl_child(*visualization, u8"color")) particles.visualization.color = {kdl_number<float>(color->args()[0]), kdl_number<float>(color->args()[1]), kdl_number<float>(color->args()[2]), kdl_number<float>(color->args()[3])};
                }
                if (const kdl::Node* diagnostics = kdl_child(node, u8"diagnostics")) {
                    particles.diagnostics = {
                        .vector_field = kdl_string_property(*diagnostics, u8"vector-field"),
                        .color_map    = read_visualization_color_map(kdl_string_property(*diagnostics, u8"color-map", "turbo")),
                        .minimum      = kdl_number_property<float>(*diagnostics, u8"minimum", 0.0f),
                        .maximum      = kdl_number_property<float>(*diagnostics, u8"maximum", 1.0f),
                        .scale        = kdl_number_property<float>(*diagnostics, u8"scale", 0.1f),
                        .width        = kdl_number_property<float>(*diagnostics, u8"width", 1.5f),
                        .sampling     = kdl_number_property<std::uint32_t>(*diagnostics, u8"sampling", 8u),
                    };
                }
                resources.particle_sets.emplace_back(std::move(particles));
            }
        }

        [[nodiscard]] VolumeDiagnosticMode read_volume_diagnostic_mode(const std::string_view value) {
            if (value == "off") return VolumeDiagnosticMode::Off;
            if (value == "slice") return VolumeDiagnosticMode::Slice;
            if (value == "cells") return VolumeDiagnosticMode::Cells;
            if (value == "ray-march") return VolumeDiagnosticMode::RayMarch;
            if (value == "maximum-intensity-projection") return VolumeDiagnosticMode::MaximumIntensityProjection;
            if (value == "isosurface") return VolumeDiagnosticMode::Isosurface;
            if (value == "glyphs") return VolumeDiagnosticMode::Glyphs;
            if (value == "streamlines") return VolumeDiagnosticMode::Streamlines;
            if (value == "lic") return VolumeDiagnosticMode::Lic;
            throw std::runtime_error(std::format("Unknown Volume diagnostic mode {}", value));
        }

        void read_volumes(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                const kdl::Node& domain = *kdl_child(node, u8"domain");
                Volume volume{
                    .id              = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name            = kdl_value_text(node.args()[1]),
                    .domain          = {{kdl_number<float>(domain.args()[0]), kdl_number<float>(domain.args()[1]), kdl_number<float>(domain.args()[2])}, {kdl_number<float>(domain.args()[3]), kdl_number<float>(domain.args()[4]), kdl_number<float>(domain.args()[5])}},
                    .transform       = read_transform(node),
                    .exterior_medium = {kdl_number_property<std::uint64_t>(node, u8"exterior-medium", 0)},
                    .visible         = kdl_bool_property(node, u8"visible", true),
                };
                if (node.name() == u8"grid") {
                    GridVolume data{.source = kdl_string_property(node, u8"source")};
                    if (const kdl::Node* resolution = kdl_child(node, u8"resolution")) data.resolution = {kdl_number<std::uint32_t>(resolution->args()[0]), kdl_number<std::uint32_t>(resolution->args()[1]), kdl_number<std::uint32_t>(resolution->args()[2])};
                    for (const kdl::Node& field_node : node.children()) {
                        if (field_node.name() != u8"field") continue;
                        const std::string kind = kdl_string_property(field_node, u8"kind");
                        const std::string sampling = kdl_string_property(field_node, u8"sampling", "cell");
                        const std::string space = kdl_string_property(field_node, u8"space", "local");
                        const VolumeFieldSampling field_sampling = sampling == "cell" ? VolumeFieldSampling::Cell : sampling == "vertex" ? VolumeFieldSampling::Vertex : throw std::runtime_error(std::format("Unknown Volume Field sampling {}", sampling));
                        const VolumeVectorSpace vector_space = space == "grid" ? VolumeVectorSpace::Grid : space == "local" ? VolumeVectorSpace::Local : space == "world" ? VolumeVectorSpace::World : throw std::runtime_error(std::format("Unknown Volume Field space {}", space));
                        std::variant<ScalarVolumeField, VectorVolumeField, CategoryVolumeField, MacVolumeField> field_data{};
                        if (kind == "float") field_data = ScalarVolumeField{field_sampling};
                        else if (kind == "float3") field_data = VectorVolumeField{field_sampling, vector_space};
                        else if (kind == "uint32") field_data = CategoryVolumeField{field_sampling};
                        else if (kind == "mac-float3") field_data = MacVolumeField{vector_space};
                        else throw std::runtime_error(std::format("Unknown Volume Field kind {}", kind));
                        VolumeField field{kdl_value_text(field_node.args()[0]), kdl_value_text(field_node.args()[1]), kdl_string_property(field_node, u8"unit"), std::move(field_data)};
                        data.fields.emplace_back(std::move(field));
                    }
                    volume.data = std::move(data);
                } else if (node.name() == u8"procedural-cloud") {
                    ProceduralCloudVolume data{};
                    if (const kdl::Node* value = kdl_child(node, u8"density")) data.density = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"wispiness")) data.wispiness = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"frequency")) data.frequency = kdl_number<float>(value->args()[0]);
                    volume.data = data;
                } else
                    throw std::runtime_error(std::format("Unknown Volume {}", kdl_text(node.name())));
                const kdl::Node& rendering_node = *kdl_child(node, u8"rendering");
                volume.rendering = {
                    .density_field               = kdl_string_property(rendering_node, u8"density-field"),
                    .temperature_field           = kdl_string_property(rendering_node, u8"temperature-field"),
                    .emission_scale_field        = kdl_string_property(rendering_node, u8"emission-scale-field"),
                    .sigma_a_field               = kdl_string_property(rendering_node, u8"sigma-a-field"),
                    .sigma_s_field               = kdl_string_property(rendering_node, u8"sigma-s-field"),
                    .emission_field              = kdl_string_property(rendering_node, u8"emission-field"),
                    .field_color_space           = read_spectrum_color_space(kdl_string_property(rendering_node, u8"field-color-space", "srgb")),
                    .sigma_a                     = read_spectrum(*kdl_child(rendering_node, u8"sigma-a")),
                    .sigma_s                     = read_spectrum(*kdl_child(rendering_node, u8"sigma-s")),
                    .emission                    = read_spectrum(*kdl_child(rendering_node, u8"emission")),
                    .density_scale               = kdl_child(rendering_node, u8"density-scale") ? kdl_number<float>(kdl_child(rendering_node, u8"density-scale")->args()[0]) : 1.0f,
                    .emission_scale              = kdl_child(rendering_node, u8"emission-scale") ? kdl_number<float>(kdl_child(rendering_node, u8"emission-scale")->args()[0]) : 1.0f,
                    .anisotropy                  = kdl_child(rendering_node, u8"anisotropy") ? kdl_number<float>(kdl_child(rendering_node, u8"anisotropy")->args()[0]) : 0.0f,
                    .temperature_scale           = kdl_child(rendering_node, u8"temperature-scale") ? kdl_number<float>(kdl_child(rendering_node, u8"temperature-scale")->args()[0]) : 1.0f,
                    .temperature_offset          = kdl_child(rendering_node, u8"temperature-offset") ? kdl_number<float>(kdl_child(rendering_node, u8"temperature-offset")->args()[0]) : 0.0f,
                    .minimum_emission_temperature = kdl_child(rendering_node, u8"minimum-emission-temperature") ? kdl_number<float>(kdl_child(rendering_node, u8"minimum-emission-temperature")->args()[0]) : 100.0f,
                    .blackbody_emission          = kdl_child(rendering_node, u8"blackbody-emission") && kdl_child(rendering_node, u8"blackbody-emission")->args()[0].as<bool>(),
                };
                if (const kdl::Node* diagnostics_node = kdl_child(node, u8"diagnostics")) {
                    volume.diagnostics = {
                        .field_id       = kdl_string_property(*diagnostics_node, u8"field"),
                        .mode           = read_volume_diagnostic_mode(kdl_string_property(*diagnostics_node, u8"mode", "off")),
                        .mapping        = read_field_mapping(kdl_string_property(*diagnostics_node, u8"mapping", "value")),
                        .depth_mode     = read_depth_buffer_mode(kdl_string_property(*diagnostics_node, u8"depth", "tested")),
                        .color_map      = read_visualization_color_map(kdl_string_property(*diagnostics_node, u8"color-map", "viridis")),
                        .minimum        = kdl_number_property<float>(*diagnostics_node, u8"minimum", 0.0f),
                        .maximum        = kdl_number_property<float>(*diagnostics_node, u8"maximum", 1.0f),
                        .slice_position = kdl_number_property<float>(*diagnostics_node, u8"slice", 0.5f),
                        .opacity        = kdl_number_property<float>(*diagnostics_node, u8"opacity", 1.0f),
                        .threshold      = kdl_number_property<float>(*diagnostics_node, u8"threshold", 0.5f),
                        .scale          = kdl_number_property<float>(*diagnostics_node, u8"scale", 0.1f),
                        .width          = kdl_number_property<float>(*diagnostics_node, u8"width", 1.5f),
                        .axis           = kdl_number_property<std::uint32_t>(*diagnostics_node, u8"axis", 2u),
                        .sampling       = kdl_number_property<std::uint32_t>(*diagnostics_node, u8"sampling", 8u),
                        .steps          = kdl_number_property<std::uint32_t>(*diagnostics_node, u8"steps", 32u),
                        .category_mask  = kdl_number_property<std::uint32_t>(*diagnostics_node, u8"category-mask", 0xfffffffeu),
                    };
                    if (const kdl::Node* color = kdl_child(*diagnostics_node, u8"color")) volume.diagnostics.color = {kdl_number<float>(color->args()[0]), kdl_number<float>(color->args()[1]), kdl_number<float>(color->args()[2]), kdl_number<float>(color->args()[3])};
                }
                resources.volumes.push_back(std::move(volume));
            }
        }

        void read_neural_fields(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                if (node.name() != u8"hash-grid-radiance-field") throw std::runtime_error(std::format("Unknown Neural Field {}", kdl_text(node.name())));
                const kdl::Node* diagnostics = kdl_child(node, u8"diagnostics");
                NeuralField field{
                    .id        = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name      = kdl_value_text(node.args()[1]),
                    .transform = read_transform(node),
                    .diagnostics = {
                        .occupancy_grid = diagnostics && kdl_bool_property(*diagnostics, u8"occupancy-grid", false),
                    },
                    .visible   = kdl_bool_property(node, u8"visible", true),
                };
                resources.neural_fields.emplace_back(std::move(field));
            }
        }

        void read_textures(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Texture texture{};
                read_texture_common(texture, node);
                if (node.name() == u8"constant") {
                    ConstantTexture data{};
                    if (texture.value_kind == TextureValueKind::Float)
                        data.scalar = kdl_number<float>(kdl_child(node, u8"scalar")->args()[0]);
                    else
                        data.spectrum = read_spectrum(*kdl_child(node, u8"spectrum"));
                    texture.data = std::move(data);
                } else if (node.name() == u8"image") {
                    ImageTexture image{
                        .source             = kdl_string_property(node, u8"source"),
                        .mapping            = read_texture_mapping(node),
                        .wrap               = read_texture_wrap(kdl_string_property(node, u8"wrap", "repeat")),
                        .channel            = read_texture_channel(kdl_string_property(node, u8"channel", "luminance")),
                        .filter             = read_texture_filter(kdl_string_property(node, u8"filter", "bilinear")),
                        .maximum_anisotropy = kdl_number_property<float>(node, u8"maximum-anisotropy", 8.0f),
                        .scale              = kdl_number_property<float>(node, u8"scale", 1.0f),
                        .invert             = kdl_bool_property(node, u8"invert", false),
                    };
                    texture.data = std::move(image);
                } else if (node.name() == u8"checkerboard") {
                    texture.data = CheckerboardTexture{
                        .first   = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second  = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                        .mapping = read_checkerboard_mapping(node),
                    };
                } else if (node.name() == u8"scale") {
                    texture.data = ScaleTexture{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                } else if (node.name() == u8"mix") {
                    texture.data = MixTexture{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                        .amount = {kdl_number_property<std::uint64_t>(node, u8"amount", 0)},
                    };
                } else if (node.name() == u8"direction-mix") {
                    DirectionMixTexture data{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                    if (const kdl::Node* direction = kdl_child(node, u8"direction")) data.direction = read_float3(*direction);
                    texture.data = data;
                } else if (node.name() == u8"bilerp") {
                    BilerpTexture data{.mapping = read_texture_mapping(node)};
                    if (texture.value_kind == TextureValueKind::Float)
                        for (const kdl::Node& corner : node.children())
                            if (corner.name() == u8"corner") data.scalars[kdl_number<std::uint32_t>(corner.args()[0])] = kdl_number<float>(corner.args()[1]);
                    if (texture.value_kind == TextureValueKind::Spectrum) {
                        constexpr std::array<std::u8string_view, 4> names{u8"corner-0", u8"corner-1", u8"corner-2", u8"corner-3"};
                        for (std::uint32_t corner = 0; corner != 4; ++corner) data.spectra[corner] = read_spectrum(*kdl_child(node, names[corner]));
                    }
                    texture.data = std::move(data);
                } else
                    throw std::runtime_error(std::format("Unknown Texture {}", kdl_text(node.name())));
                resources.textures.push_back(std::move(texture));
            }
        }

        [[nodiscard]] std::variant<ConductorEtaK, ConductorReflectance> read_conductor_optics(const kdl::Node& node) {
            if (const kdl::Node* eta_k = kdl_child(node, u8"eta-k"))
                return ConductorEtaK{
                    .eta = read_spectrum(*kdl_child(*eta_k, u8"eta")),
                    .k   = read_spectrum(*kdl_child(*eta_k, u8"k")),
                };
            const kdl::Node& reflectance = *kdl_child(node, u8"reflectance-optics");
            return ConductorReflectance{read_spectrum(*kdl_child(reflectance, u8"reflectance"))};
        }

        [[nodiscard]] CoatingLayer read_coating(const kdl::Node& node) {
            CoatingLayer coating{};
            const kdl::Node& source = *kdl_child(node, u8"coating");
            if (const kdl::Node* value = kdl_child(source, u8"thickness")) coating.thickness = read_float_parameter(*value);
            coating.albedo = read_spectrum(*kdl_child(source, u8"albedo"));
            if (const kdl::Node* value = kdl_child(source, u8"g")) coating.g = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(source, u8"maximum-depth")) coating.max_depth = kdl_number<std::int32_t>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(source, u8"samples")) coating.sample_count = kdl_number<std::int32_t>(value->args()[0]);
            return coating;
        }

        void read_normal_and_bump(const kdl::Node& node, TextureId& normal_map, TextureId& bump_map) {
            if (const kdl::Node* value = kdl_child(node, u8"normal-map")) normal_map.value = kdl_number<std::uint64_t>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(node, u8"bump-map")) bump_map.value = kdl_number<std::uint64_t>(value->args()[0]);
        }

        void read_materials(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Material material{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"interface")
                    material.data = InterfaceMaterialData{};
                else if (node.name() == u8"diffuse") {
                    DiffuseMaterialData data{};
                    data.reflectance = read_spectrum(*kdl_child(node, u8"reflectance"));
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"diffuse-transmission") {
                    DiffuseTransmissionMaterialData data{};
                    data.reflectance   = read_spectrum(*kdl_child(node, u8"reflectance"));
                    data.transmittance = read_spectrum(*kdl_child(node, u8"transmittance"));
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"conductor") {
                    ConductorMaterialData data{};
                    data.optics       = read_conductor_optics(node);
                    data.distribution = read_roughness(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"dielectric") {
                    DielectricMaterialData data{};
                    data.eta          = read_spectrum(*kdl_child(node, u8"eta"));
                    data.distribution = read_roughness(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"thin-dielectric") {
                    ThinDielectricMaterialData data{};
                    data.eta = read_spectrum(*kdl_child(node, u8"eta"));
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"coated-diffuse") {
                    CoatedDiffuseMaterialData data{};
                    data.reflectance = read_spectrum(*kdl_child(node, u8"reflectance"));
                    data.eta         = read_spectrum(*kdl_child(node, u8"eta"));
                    data.interface   = read_roughness(*kdl_child(node, u8"interface"));
                    data.coating     = read_coating(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"coated-conductor") {
                    CoatedConductorMaterialData data{};
                    const kdl::Node& interface = *kdl_child(node, u8"interface");
                    const kdl::Node& conductor = *kdl_child(node, u8"conductor");
                    data.interface_eta         = read_spectrum(*kdl_child(interface, u8"eta"));
                    data.interface             = read_roughness(interface);
                    data.optics                = read_conductor_optics(conductor);
                    data.conductor             = read_roughness(conductor);
                    data.coating               = read_coating(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"mix") {
                    MixMaterialData data{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                    if (const kdl::Node* value = kdl_child(node, u8"amount")) data.amount = read_float_parameter(*value);
                    material.data = data;
                } else
                    throw std::runtime_error(std::format("Unknown Material {}", kdl_text(node.name())));
                resources.materials.push_back(std::move(material));
            }
        }

        void read_media(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Medium medium{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() != u8"homogeneous") throw std::runtime_error(std::format("Unknown Medium {}", kdl_text(node.name())));
                medium.sigma_a  = read_spectrum(*kdl_child(node, u8"sigma-a"));
                medium.sigma_s  = read_spectrum(*kdl_child(node, u8"sigma-s"));
                medium.emission = read_spectrum(*kdl_child(node, u8"emission"));
                if (const kdl::Node* value = kdl_child(node, u8"density-scale")) medium.density_scale = kdl_number<float>(value->args()[0]);
                if (const kdl::Node* value = kdl_child(node, u8"emission-scale")) medium.emission_scale = kdl_number<float>(value->args()[0]);
                if (const kdl::Node* value = kdl_child(node, u8"anisotropy")) medium.anisotropy = kdl_number<float>(value->args()[0]);
                resources.media.push_back(std::move(medium));
            }
        }

        [[nodiscard]] InfiniteLight read_infinite_light(const kdl::Node& node) {
            InfiniteLight light{};
            light.radiance  = read_spectrum(*kdl_child(node, u8"radiance"));
            light.transform = read_transform(node);
            if (const kdl::Node* value = kdl_child(node, u8"scale")) light.scale = kdl_number<float>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(node, u8"emission-texture")) light.emission_texture.value = kdl_number<std::uint64_t>(value->args()[0]);
            return light;
        }

        void read_lights(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Light light{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"point") {
                    PointLight data{};
                    data.intensity = read_spectrum(*kdl_child(node, u8"intensity"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"spot") {
                    SpotLight data{};
                    data.intensity = read_spectrum(*kdl_child(node, u8"intensity"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"cone-angle")) data.cone_angle = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"cone-delta")) data.cone_delta = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"distant") {
                    DistantLight data{};
                    data.radiance  = read_spectrum(*kdl_child(node, u8"radiance"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"diffuse-area") {
                    DiffuseAreaLight data{};
                    data.radiance = read_spectrum(*kdl_child(node, u8"radiance"));
                    if (const kdl::Node* value = kdl_child(node, u8"sidedness")) data.sidedness = kdl_value_text(value->args()[0]) == "both" ? EmissionSidedness::Both : EmissionSidedness::Front;
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"power")) data.power = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"emission-texture")) data.emission_texture.value = kdl_number<std::uint64_t>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"infinite")
                    light.data = read_infinite_light(node);
                else if (node.name() == u8"portal-infinite") {
                    PortalInfiniteLight data{.environment = read_infinite_light(*kdl_child(node, u8"environment"))};
                    for (const kdl::Node& portal : node.children())
                        if (portal.name() == u8"portal") {
                            std::array<math::Float3, 4> corners{};
                            for (std::uint32_t corner = 0; corner != 4; ++corner) corners[corner] = read_float3(portal.children()[corner]);
                            data.portals.push_back(corners);
                        }
                    light.data = std::move(data);
                } else
                    throw std::runtime_error(std::format("Unknown Light {}", kdl_text(node.name())));
                resources.lights.push_back(std::move(light));
            }
        }

        void read_cameras(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Camera camera{
                    .id            = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name          = kdl_value_text(node.args()[1]),
                    .transform     = read_transform(node),
                    .exposure_time = kdl_number_property<float>(node, u8"exposure-time", 1.0f),
                    .medium        = {kdl_number_property<std::uint64_t>(node, u8"medium", 0)},
                };
                ScreenWindow screen{};
                if (const kdl::Node* value = kdl_child(node, u8"screen")) {
                    screen.minimum = read_float2(*value);
                    screen.maximum = read_float2(*value, 2);
                }
                if (node.name() == u8"perspective") {
                    PerspectiveCameraData data{.screen_window = screen};
                    if (const kdl::Node* value = kdl_child(node, u8"vertical-fov")) data.vertical_fov = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"lens-radius")) data.lens_radius = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"focal-distance")) data.focal_distance = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"near-plane")) data.near_plane = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"far-plane")) data.far_plane = kdl_number<float>(value->args()[0]);
                    camera.data = data;
                } else if (node.name() == u8"orthographic") {
                    OrthographicCameraData data{.screen_window = screen};
                    if (const kdl::Node* value = kdl_child(node, u8"lens-radius")) data.lens_radius = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"focal-distance")) data.focal_distance = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"near-plane")) data.near_plane = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"far-plane")) data.far_plane = kdl_number<float>(value->args()[0]);
                    camera.data = data;
                } else
                    throw std::runtime_error(std::format("Unknown Camera {}", kdl_text(node.name())));
                resources.cameras.push_back(std::move(camera));
            }
        }

        [[nodiscard]] FilterKind read_filter_kind(const std::string_view value) {
            if (value == "box") return FilterKind::Box;
            if (value == "gaussian") return FilterKind::Gaussian;
            if (value == "mitchell") return FilterKind::Mitchell;
            if (value == "sinc") return FilterKind::Sinc;
            if (value == "triangle") return FilterKind::Triangle;
            throw std::runtime_error(std::format("Unknown Film filter {}", value));
        }

        void read_films(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Film film{
                    .id                   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name                 = kdl_value_text(node.args()[1]),
                    .exposure             = kdl_number_property<float>(node, u8"exposure", 0.0f),
                    .iso                  = kdl_number_property<float>(node, u8"iso", 100.0f),
                    .color_space          = read_spectrum_color_space(kdl_string_property(node, u8"color-space", "srgb")),
                    .gbuffer              = kdl_bool_property(node, u8"gbuffer", false),
                    .gbuffer_camera_space = kdl_bool_property(node, u8"gbuffer-camera-space", true),
                };
                if (const kdl::Value* value = kdl_property(node, u8"maximum-component")) film.maximum_component_value = kdl_number<float>(*value);
                const kdl::Node& resolution = *kdl_child(node, u8"resolution");
                film.resolution             = {kdl_number<std::uint32_t>(resolution.args()[0]), kdl_number<std::uint32_t>(resolution.args()[1])};
                film.pixel_maximum          = film.resolution;
                if (const kdl::Node* range = kdl_child(node, u8"pixel-range")) {
                    film.pixel_minimum = {kdl_number<std::uint32_t>(range->args()[0]), kdl_number<std::uint32_t>(range->args()[1])};
                    film.pixel_maximum = {kdl_number<std::uint32_t>(range->args()[2]), kdl_number<std::uint32_t>(range->args()[3])};
                }
                if (const kdl::Node* response = kdl_child(node, u8"sensor-response"))
                    for (const kdl::Node& values : response->children())
                        for (const kdl::Value& value : values.args()) film.sensor_response.push_back(kdl_number<float>(value));
                if (const kdl::Node* matrix = kdl_child(node, u8"sensor-to-output-rgb"))
                    for (std::uint32_t row = 0; row != 3; ++row)
                        for (std::uint32_t column = 0; column != 3; ++column) film.sensor_to_output_rgb[row * 3 + column] = kdl_number<float>(matrix->children()[row].args()[column]);
                if (const kdl::Node* filter = kdl_child(node, u8"filter")) {
                    film.filter.kind     = read_filter_kind(kdl_value_text(filter->args()[0]));
                    film.filter.radius.x = kdl_number_property<float>(*filter, u8"radius-x", 0.5f);
                    film.filter.radius.y = kdl_number_property<float>(*filter, u8"radius-y", 0.5f);
                    film.filter.sigma    = kdl_number_property<float>(*filter, u8"sigma", 0.5f);
                    film.filter.b        = kdl_number_property<float>(*filter, u8"b", 1.0f / 3.0f);
                    film.filter.c        = kdl_number_property<float>(*filter, u8"c", 1.0f / 3.0f);
                    film.filter.tau      = kdl_number_property<float>(*filter, u8"tau", 3.0f);
                }
                resources.films.push_back(std::move(film));
            }
        }

        [[nodiscard]] SamplerKind read_sampler_kind(const std::string_view value) {
            if (value == "independent") return SamplerKind::Independent;
            if (value == "stratified") return SamplerKind::Stratified;
            if (value == "halton") return SamplerKind::Halton;
            if (value == "sobol") return SamplerKind::Sobol;
            if (value == "padded-sobol") return SamplerKind::PaddedSobol;
            if (value == "zsobol") return SamplerKind::ZSobol;
            if (value == "pmj02bn") return SamplerKind::Pmj02bn;
            throw std::runtime_error(std::format("Unknown Sampler kind {}", value));
        }

        [[nodiscard]] SamplerRandomization read_sampler_randomization(const std::string_view value) {
            if (value == "none") return SamplerRandomization::None;
            if (value == "permute-digits") return SamplerRandomization::PermuteDigits;
            if (value == "fast-owen") return SamplerRandomization::FastOwen;
            if (value == "owen") return SamplerRandomization::Owen;
            throw std::runtime_error(std::format("Unknown Sampler randomization {}", value));
        }

        void read_samplers(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.samplers.push_back({
                    .id                = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name              = kdl_value_text(node.args()[1]),
                    .kind              = read_sampler_kind(kdl_string_property(node, u8"kind", "independent")),
                    .samples_per_pixel = kdl_number_property<std::uint32_t>(node, u8"samples", 1),
                    .seed              = kdl_number_property<std::uint32_t>(node, u8"seed", 0),
                    .jitter            = kdl_bool_property(node, u8"jitter", true),
                    .x_strata          = kdl_number_property<std::uint32_t>(node, u8"x-strata", 1),
                    .y_strata          = kdl_number_property<std::uint32_t>(node, u8"y-strata", 1),
                    .randomization     = read_sampler_randomization(kdl_string_property(node, u8"randomization", "owen")),
                });
        }

        void read_prototypes(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Prototype prototype{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                for (const kdl::Node& primitive_node : node.children()) {
                    Primitive primitive{
                        .geometry            = {kdl_number_property<std::uint64_t>(primitive_node, u8"geometry", 0)},
                        .spheres             = {kdl_number_property<std::uint64_t>(primitive_node, u8"spheres", 0)},
                        .material            = {kdl_number_property<std::uint64_t>(primitive_node, u8"material", 0)},
                        .area_light          = {kdl_number_property<std::uint64_t>(primitive_node, u8"area-light", 0)},
                        .alpha               = {kdl_number_property<std::uint64_t>(primitive_node, u8"alpha", 0)},
                        .reverse_orientation = kdl_bool_property(primitive_node, u8"reverse-orientation", false),
                        .transform           = read_transform(primitive_node),
                    };
                    if (const kdl::Node* media = kdl_child(primitive_node, u8"media")) {
                        primitive.media.inside.value  = kdl_number_property<std::uint64_t>(*media, u8"inside", 0);
                        primitive.media.outside.value = kdl_number_property<std::uint64_t>(*media, u8"outside", 0);
                    }
                    if (const kdl::Node* materials = kdl_child(primitive_node, u8"face-materials"))
                        for (const kdl::Value& value : materials->args()) primitive.face_materials.push_back({kdl_number<std::uint64_t>(value)});
                    prototype.primitives.push_back(std::move(primitive));
                }
                resources.prototypes.push_back(std::move(prototype));
            }
        }

        void read_instances(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.instances.push_back({
                    .id        = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name      = kdl_value_text(node.args()[1]),
                    .prototype = {kdl_number_property<std::uint64_t>(node, u8"prototype", 0)},
                    .transform = read_transform(node),
                    .visible   = kdl_bool_property(node, u8"visible", true),
                });
        }

        [[nodiscard]] DynamicParameterKind read_dynamic_parameter_kind(const std::string_view value) {
            if (value == "boolean") return DynamicParameterKind::Boolean;
            if (value == "integer") return DynamicParameterKind::Integer;
            if (value == "float") return DynamicParameterKind::Float;
            if (value == "float3") return DynamicParameterKind::Float3;
            if (value == "enumeration") return DynamicParameterKind::Enumeration;
            throw std::runtime_error(std::format("Unknown Dynamic parameter kind {}", value));
        }

        [[nodiscard]] VisualizationDepthMode read_depth_buffer_mode(const std::string_view value) {
            if (value == "tested") return VisualizationDepthMode::Tested;
            if (value == "xray") return VisualizationDepthMode::XRay;
            if (value == "overlay") return VisualizationDepthMode::Overlay;
            throw std::runtime_error(std::format("Unknown Visualization depth mode {}", value));
        }

        [[nodiscard]] VisualizationCompositionDomain read_visualization_composition_domain(const std::string_view value) {
            if (value == "scene-linear") return VisualizationCompositionDomain::SceneLinear;
            if (value == "display-referred") return VisualizationCompositionDomain::DisplayReferred;
            throw std::runtime_error(std::format("Unknown Visualization composition domain: {}", value));
        }

        [[nodiscard]] VisualizationColorSource read_visualization_color_source(const std::string_view value) {
            if (value == "element") return VisualizationColorSource::Element;
            if (value == "uniform") return VisualizationColorSource::Uniform;
            if (value == "scalar") return VisualizationColorSource::Scalar;
            throw std::runtime_error(std::format("Unknown Visualization color source {}", value));
        }

        [[nodiscard]] VisualizationColorMap read_visualization_color_map(const std::string_view value) {
            if (value == "viridis") return VisualizationColorMap::Viridis;
            if (value == "turbo") return VisualizationColorMap::Turbo;
            if (value == "cool-warm") return VisualizationColorMap::CoolWarm;
            if (value == "grayscale") return VisualizationColorMap::Grayscale;
            throw std::runtime_error(std::format("Unknown Visualization color map {}", value));
        }

        [[nodiscard]] DynamicSetup read_dynamics(const kdl::Node& node) {
            DynamicSetup setup{.seed = kdl_number_property<std::uint64_t>(node, u8"seed", 0)};
            if (const kdl::Node* clock = kdl_child(node, u8"clock")) {
                setup.clock.step_seconds = kdl_number_property<double>(*clock, u8"step-seconds", 1.0 / 120.0);
                setup.clock.start_step   = kdl_number_property<std::uint64_t>(*clock, u8"start-step", 0);
                if (const kdl::Value* value = kdl_property(*clock, u8"end-step")) setup.clock.end_step = kdl_number<std::uint64_t>(*value);
                setup.clock.loop = kdl_bool_property(*clock, u8"loop", false);
            }
            for (const kdl::Node& system_node : node.children()) {
                if (system_node.name() != u8"system") continue;
                DynamicSystem system{
                    .id          = {kdl_value_text(system_node.args()[0])},
                    .name        = kdl_value_text(system_node.args()[1]),
                    .provider_id = kdl_string_property(system_node, u8"provider"),
                    .enabled     = kdl_bool_property(system_node, u8"enabled", true),
                    .visible     = kdl_bool_property(system_node, u8"visible", true),
                };
                for (const kdl::Node& child : system_node.children()) {
                    if (child.name() == u8"parameter") {
                        DynamicParameterSetting parameter{
                            .parameter_id = kdl_value_text(child.args()[0]),
                        };
                        parameter.value.kind = read_dynamic_parameter_kind(kdl_value_text(child.args()[1]));
                        if (parameter.value.kind == DynamicParameterKind::Boolean)
                            parameter.value.integer = child.args()[2].as<bool>() ? 1 : 0;
                        else if (parameter.value.kind == DynamicParameterKind::Integer || parameter.value.kind == DynamicParameterKind::Enumeration)
                            parameter.value.integer = kdl_number<std::int64_t>(child.args()[2]);
                        else if (parameter.value.kind == DynamicParameterKind::Float)
                            parameter.value.floating[0] = kdl_number<double>(child.args()[2]);
                        else
                            for (std::uint32_t component = 0; component != 3; ++component) parameter.value.floating[component] = kdl_number<double>(child.args()[component + 2]);
                        system.parameters.push_back(std::move(parameter));
                    } else if (child.name() == u8"scene-bind")
                        system.scene_bindings.push_back({
                            .dataset_id  = kdl_value_text(child.args()[0]),
                            .resource_id = kdl_number<std::uint64_t>(child.args()[1]),
                        });
                    else if (child.name() == u8"visualize") {
                        DynamicVisualizationView view{
                            .dataset_id         = kdl_value_text(child.args()[0]),
                            .name               = kdl_value_text(child.args()[1]),
                            .depth_mode         = read_depth_buffer_mode(kdl_string_property(child, u8"depth", "tested")),
                            .composition_domain = read_visualization_composition_domain(kdl_string_property(child, u8"domain", "display-referred")),
                            .anchor             = {kdl_number_property<std::uint64_t>(child, u8"anchor", 0)},
                            .visible            = kdl_bool_property(child, u8"visible", true),
                        };
                        const float width                           = kdl_number_property<float>(child, u8"width", 1.0f);
                        const float scale                           = kdl_number_property<float>(child, u8"scale", 1.0f);
                        const float scalar_minimum                  = kdl_number_property<float>(child, u8"scalar-min", 0.0f);
                        const float scalar_maximum                  = kdl_number_property<float>(child, u8"scalar-max", 1.0f);
                        const VisualizationColorSource color_source = read_visualization_color_source(kdl_string_property(child, u8"color-source", "element"));
                        const VisualizationColorMap color_map       = read_visualization_color_map(kdl_string_property(child, u8"color-map", "viridis"));
                        math::Float4 screen_rect{0.02f, 0.02f, 0.32f, 0.32f};
                        if (const kdl::Node* source = kdl_child(child, u8"screen-rect")) screen_rect = {kdl_number<float>(source->args()[0]), kdl_number<float>(source->args()[1]), kdl_number<float>(source->args()[2]), kdl_number<float>(source->args()[3])};
                        const std::string view_kind = kdl_string_property(child, u8"kind");
                        if (view_kind == "segments") view.data = SegmentVisualization{width, scalar_minimum, scalar_maximum, color_source, color_map};
                        else if (view_kind == "vectors") view.data = VectorVisualization{width, scale, scalar_minimum, scalar_maximum, color_source, color_map};
                        else if (view_kind == "image") view.data = ImageVisualization{screen_rect};
                        else if (view_kind == "surface") view.data = SurfaceVisualization{scalar_minimum, scalar_maximum, color_source, color_map};
                        else throw std::runtime_error(std::format("Unknown Visualization view kind {}", view_kind));
                        if (const kdl::Node* color = kdl_child(child, u8"color")) view.color = {kdl_number<float>(color->args()[0]), kdl_number<float>(color->args()[1]), kdl_number<float>(color->args()[2]), kdl_number<float>(color->args()[3])};
                        system.visualizations.push_back(std::move(view));
                    }
                }
                setup.systems.push_back(std::move(system));
            }
            return setup;
        }

        [[nodiscard]] LightSamplerKind read_light_sampler(const std::string_view value) {
            if (value == "uniform") return LightSamplerKind::Uniform;
            if (value == "power") return LightSamplerKind::Power;
            if (value == "bvh") return LightSamplerKind::Bvh;
            throw std::runtime_error(std::format("Unknown Light sampler {}", value));
        }

        [[nodiscard]] Scene parse_scene(const std::filesystem::path& path) {
            std::ifstream stream{path, std::ios::binary};
            if (!stream) throw std::runtime_error(std::format("Failed to open Spectra scene: {}", path.string()));
            const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
            const kdl::Document document = kdl::parse({reinterpret_cast<const char8_t*>(text.data()), text.size()}, kdl::KdlVersion::Kdl_2);
            if (document.nodes().empty()) throw std::runtime_error(std::format("Spectra scene has no root node: {}", path.string()));
            const kdl::Node& root              = document.nodes()[0];
            const std::uint32_t format_version = kdl_number<std::uint32_t>(root.args()[0]);
            if (format_version != current_scene_format_version) throw std::runtime_error(std::format("Unsupported Spectra scene format {}", format_version));
            Scene scene{kdl_value_text(root.args()[1])};
            for (const kdl::Node& node : root.children()) {
                if (node.name() == u8"active") {
                    scene.active_camera.value  = kdl_number_property<std::uint64_t>(node, u8"camera", 0);
                    scene.active_film.value    = kdl_number_property<std::uint64_t>(node, u8"film", 0);
                    scene.active_sampler.value = kdl_number_property<std::uint64_t>(node, u8"sampler", 0);
                } else if (node.name() == u8"transport") {
                    scene.transport.maximum_depth = kdl_number_property<std::uint32_t>(node, u8"maximum-depth", 5);
                    scene.transport.light_sampler = read_light_sampler(kdl_string_property(node, u8"light-sampler", "bvh"));
                    scene.transport.regularize    = kdl_bool_property(node, u8"regularize", false);
                } else if (node.name() == u8"geometries")
                    read_geometries(scene.resources, node);
                else if (node.name() == u8"sphere-sets")
                    read_sphere_sets(scene.resources, node);
                else if (node.name() == u8"particle-sets")
                    read_particle_sets(scene.resources, node);
                else if (node.name() == u8"volumes")
                    read_volumes(scene.resources, node);
                else if (node.name() == u8"neural-fields")
                    read_neural_fields(scene.resources, node);
                else if (node.name() == u8"textures")
                    read_textures(scene.resources, node);
                else if (node.name() == u8"materials")
                    read_materials(scene.resources, node);
                else if (node.name() == u8"media")
                    read_media(scene.resources, node);
                else if (node.name() == u8"lights")
                    read_lights(scene.resources, node);
                else if (node.name() == u8"cameras")
                    read_cameras(scene.resources, node);
                else if (node.name() == u8"films")
                    read_films(scene.resources, node);
                else if (node.name() == u8"samplers")
                    read_samplers(scene.resources, node);
                else if (node.name() == u8"prototypes")
                    read_prototypes(scene.resources, node);
                else if (node.name() == u8"instances")
                    read_instances(scene.resources, node);
                else if (node.name() == u8"dynamics")
                    scene.dynamic_setup = read_dynamics(node);
                else
                    throw std::runtime_error(std::format("Unknown Spectra scene section {}", kdl_text(node.name())));
            }
            if (std::ranges::count_if(scene.resources.neural_fields, [](const NeuralField& field) { return field.visible; }) > 1) throw std::runtime_error("Spectra supports one visible Neural Field");
            return scene;
        }

    } // namespace

    Scene load_scene(const std::filesystem::path& path) {
        Scene scene = parse_scene(path);
        load_scene_sources(scene, path.parent_path());
        return scene;
    }

} // namespace spectra::scene
