module;
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef interface
#endif
#include <kdl/kdl.h>
#include <kdlpp.h>

module spectra.scene.format;
import spectra.scene.asset_import;
import std;


namespace spectra::scene {
    namespace {
        constexpr std::uint32_t current_scene_format_version = 35;

        struct KdlWriter {
            std::string content{};
            std::uint32_t indentation{};

            void line(const std::string_view value) {
                this->content.append(this->indentation * 4, ' ');
                this->content.append(value);
                this->content.push_back('\n');
            }

            void begin(const std::string_view value) {
                this->line(std::format("{} {{", value));
                ++this->indentation;
            }

            void end() {
                --this->indentation;
                this->line("}");
            }
        };

        [[nodiscard]] std::string kdl_string(const std::string_view value) {
            std::string result{"\""};
            for (const unsigned char character : value) {
                switch (character) {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\b': result += "\\b"; break;
                case '\f': result += "\\f"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (character < 0x20)
                        result += std::format("\\u{{{:x}}}", character);
                    else
                        result.push_back(static_cast<char>(character));
                }
            }
            result.push_back('"');
            return result;
        }

        template <class Value>
        void kdl_number_property(std::string& line, const std::string_view name, const Value value) {
            line += std::format(" {}={}", name, value);
        }

        void kdl_string_property(std::string& line, const std::string_view name, const std::string_view value) {
            line += std::format(" {}={}", name, kdl_string(value));
        }

        void write_source(std::string& line, const std::string_view source) {
            kdl_string_property(line, "source", source);
        }

        void kdl_bool_property(std::string& line, const std::string_view name, const bool value) {
            line += std::format(" {}=#{}", name, value ? "true" : "false");
        }

        void write_transform(KdlWriter& writer, const std::string_view name, const math::Transform& transform) {
            writer.begin(name);
            for (std::uint32_t row = 0; row != 4; ++row) writer.line(std::format("row {} {} {} {}", transform.matrix[row * 4], transform.matrix[row * 4 + 1], transform.matrix[row * 4 + 2], transform.matrix[row * 4 + 3]));
            writer.end();
        }

        void write_bounds(KdlWriter& writer, const math::Bounds3 bounds) {
            writer.line(std::format("bounds {} {} {} {} {} {}", bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z));
        }

        [[nodiscard]] std::string spectrum_encoding_name(const SpectrumEncoding encoding) {
            switch (encoding) {
            case SpectrumEncoding::RgbAlbedo: return "rgb-albedo";
            case SpectrumEncoding::RgbUnbounded: return "rgb-unbounded";
            case SpectrumEncoding::RgbIlluminant: return "rgb-illuminant";
            case SpectrumEncoding::Constant: return "constant";
            case SpectrumEncoding::Blackbody: return "blackbody";
            case SpectrumEncoding::PiecewiseLinear: return "piecewise-linear";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string spectrum_color_space_name(const SpectrumColorSpace color_space) {
            switch (color_space) {
            case SpectrumColorSpace::Srgb: return "srgb";
            case SpectrumColorSpace::Rec2020: return "rec2020";
            case SpectrumColorSpace::Aces2065_1: return "aces2065-1";
            }
            std::unreachable();
        }

        void write_spectrum(KdlWriter& writer, const std::string_view name, const SpectrumParameter& spectrum) {
            std::string line = std::format("{} {}", name, spectrum_encoding_name(spectrum.encoding));
            if (spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) line += std::format(" {} {} {}", spectrum.value.x, spectrum.value.y, spectrum.value.z);
            if (spectrum.encoding == SpectrumEncoding::Constant) line += std::format(" {}", spectrum.scalar);
            if (spectrum.encoding == SpectrumEncoding::Blackbody) line += std::format(" {}", spectrum.temperature);
            if (spectrum.texture.value != 0) kdl_number_property(line, "texture", spectrum.texture.value);
            if ((spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) && spectrum.color_space != SpectrumColorSpace::Srgb) kdl_string_property(line, "color-space", spectrum_color_space_name(spectrum.color_space));
            if (spectrum.encoding != SpectrumEncoding::PiecewiseLinear) {
                writer.line(line);
                return;
            }
            writer.begin(line);
            for (std::size_t index = 0; index != spectrum.wavelengths.size(); ++index) writer.line(std::format("sample {} {}", spectrum.wavelengths[index], spectrum.samples[index]));
            writer.end();
        }

        void write_float_parameter(KdlWriter& writer, const std::string_view name, const FloatParameter parameter) {
            std::string line = std::format("{} {}", name, parameter.value);
            if (parameter.texture.value != 0) kdl_number_property(line, "texture", parameter.texture.value);
            writer.line(line);
        }

        void write_roughness(KdlWriter& writer, const MaterialRoughness& roughness) {
            if (roughness.roughness.value != 0.0f || roughness.roughness.texture.value != 0) write_float_parameter(writer, "roughness", roughness.roughness);
            if (roughness.u_roughness) write_float_parameter(writer, "u-roughness", *roughness.u_roughness);
            if (roughness.v_roughness) write_float_parameter(writer, "v-roughness", *roughness.v_roughness);
        }

        void write_texture_mapping(KdlWriter& writer, const TextureMapping& mapping) {
            std::visit(
                [&writer](const auto& value) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, UvTextureMapping>) {
                        if (value.scale == math::Float2{1.0f, 1.0f} && value.offset == math::Float2{}) return;
                        writer.begin("mapping uv");
                        if (value.scale != math::Float2{1.0f, 1.0f}) writer.line(std::format("scale {} {}", value.scale.x, value.scale.y));
                        if (value.offset != math::Float2{}) writer.line(std::format("offset {} {}", value.offset.x, value.offset.y));
                        writer.end();
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, PlanarTextureMapping>) {
                        writer.begin("mapping planar");
                        if (value.first_axis != math::Float3{1.0f, 0.0f, 0.0f}) writer.line(std::format("first-axis {} {} {}", value.first_axis.x, value.first_axis.y, value.first_axis.z));
                        if (value.second_axis != math::Float3{0.0f, 1.0f, 0.0f}) writer.line(std::format("second-axis {} {} {}", value.second_axis.x, value.second_axis.y, value.second_axis.z));
                        if (value.offset != math::Float2{}) writer.line(std::format("offset {} {}", value.offset.x, value.offset.y));
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, SphericalTextureMapping>) {
                        writer.begin("mapping spherical");
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    } else {
                        writer.begin("mapping cylindrical");
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    }
                },
                mapping.data);
        }

        void write_checkerboard_mapping(KdlWriter& writer, const CheckerboardMapping& mapping) {
            if (const TextureMapping* two_dimensional = std::get_if<TextureMapping>(&mapping.data)) {
                write_texture_mapping(writer, *two_dimensional);
                return;
            }
            writer.begin("mapping \"3d\"");
            const TextureMapping3D& three_dimensional = std::get<TextureMapping3D>(mapping.data);
            if (three_dimensional.texture_from_render != math::Transform{}) write_transform(writer, "transform", three_dimensional.texture_from_render);
            writer.end();
        }

        void write_geometries(KdlWriter& writer, const std::vector<Geometry>& geometries) {
            if (geometries.empty()) return;
            writer.begin("geometries");
            for (const Geometry& geometry : geometries) {
                std::visit(
                    [&writer, &geometry](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                            std::string line = std::format("triangle-mesh {} {}", geometry.id.value, kdl_string(geometry.name));
                            write_source(line, data.source);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) {
                            std::string line = std::format("sphere {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.z_min != -data.radius) kdl_number_property(line, "z-min", data.z_min);
                            if (data.z_max != data.radius) kdl_number_property(line, "z-max", data.z_max);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                            const std::string line = std::format("box {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.bounds == BoxGeometry{}.bounds)
                                writer.line(line);
                            else {
                                writer.begin(line);
                                write_bounds(writer, data.bounds);
                                writer.end();
                            }
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) {
                            const std::string line = std::format("rectangle {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.minimum == RectangleGeometry{}.minimum && data.maximum == RectangleGeometry{}.maximum)
                                writer.line(line);
                            else {
                                writer.begin(line);
                                if (data.minimum != RectangleGeometry{}.minimum) writer.line(std::format("minimum {} {}", data.minimum.x, data.minimum.y));
                                if (data.maximum != RectangleGeometry{}.maximum) writer.line(std::format("maximum {} {}", data.maximum.x, data.maximum.y));
                                writer.end();
                            }
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) {
                            std::string line = std::format("disk {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.height != 0.0f) kdl_number_property(line, "height", data.height);
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.inner_radius != 0.0f) kdl_number_property(line, "inner-radius", data.inner_radius);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        } else {
                            std::string line = std::format("cylinder {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.z_min != -1.0f) kdl_number_property(line, "z-min", data.z_min);
                            if (data.z_max != 1.0f) kdl_number_property(line, "z-max", data.z_max);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        }
                    },
                    geometry.data);
            }
            writer.end();
        }

        void write_sphere_sets(KdlWriter& writer, const std::vector<SphereSet>& sphere_sets) {
            if (sphere_sets.empty()) return;
            writer.begin("sphere-sets");
            for (const SphereSet& spheres : sphere_sets) {
                std::string line = std::format("sphere-set {} {}", spheres.id.value, kdl_string(spheres.name));
                if (!spheres.source.empty()) write_source(line, spheres.source);
                writer.line(line);
            }
            writer.end();
        }

        [[nodiscard]] std::string depth_buffer_mode_name(VisualizationDepthMode mode);
        [[nodiscard]] std::string visualization_composition_domain_name(VisualizationCompositionDomain domain);
        [[nodiscard]] std::string visualization_color_map_name(VisualizationColorMap map);

        [[nodiscard]] std::string volume_diagnostic_mode_name(const VolumeDiagnosticMode mode) {
            switch (mode) {
            case VolumeDiagnosticMode::Off: return "off";
            case VolumeDiagnosticMode::Slice: return "slice";
            case VolumeDiagnosticMode::RayMarch: return "ray-march";
            case VolumeDiagnosticMode::MaximumIntensityProjection: return "maximum-intensity-projection";
            case VolumeDiagnosticMode::Isosurface: return "isosurface";
            case VolumeDiagnosticMode::Glyphs: return "glyphs";
            case VolumeDiagnosticMode::Streamlines: return "streamlines";
            case VolumeDiagnosticMode::Lic: return "lic";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string volume_field_mapping_name(const VolumeFieldMapping mapping) {
            switch (mapping) {
            case VolumeFieldMapping::Value: return "value";
            case VolumeFieldMapping::Magnitude: return "magnitude";
            case VolumeFieldMapping::X: return "x";
            case VolumeFieldMapping::Y: return "y";
            case VolumeFieldMapping::Z: return "z";
            case VolumeFieldMapping::Divergence: return "divergence";
            case VolumeFieldMapping::CurlMagnitude: return "curl-magnitude";
            case VolumeFieldMapping::QCriterion: return "q-criterion";
            }
            std::unreachable();
        }

        void write_volumes(KdlWriter& writer, const std::vector<Volume>& volumes) {
            if (volumes.empty()) return;
            writer.begin("volumes");
            for (const Volume& volume : volumes) {
                std::visit(
                    [&writer, &volume](const auto& data) {
                        const std::string_view kind = std::same_as<std::remove_cvref_t<decltype(data)>, GridVolume> ? "grid" : "procedural-cloud";
                        std::string line = std::format("{} {} {}", kind, volume.id.value, kdl_string(volume.name));
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, GridVolume>) write_source(line, data.source);
                        if (!volume.visible) line.append(" visible=#false");
                        if (volume.exterior_medium.value != 0) line.append(std::format(" exterior-medium={}", volume.exterior_medium.value));
                        writer.begin(line);
                        writer.line(std::format("domain {} {} {} {} {} {}", volume.domain.minimum.x, volume.domain.minimum.y, volume.domain.minimum.z, volume.domain.maximum.x, volume.domain.maximum.y, volume.domain.maximum.z));
                        if (volume.transform != math::Transform{}) write_transform(writer, "transform", volume.transform);
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, GridVolume>) {
                            if (data.resolution != math::UInt3{}) writer.line(std::format("resolution {} {} {}", data.resolution.x, data.resolution.y, data.resolution.z));
                            for (const VolumeField& field : data.fields) {
                                const std::string_view field_kind = field.kind == VolumeFieldKind::Float ? "float" : field.kind == VolumeFieldKind::Float3 ? "float3" : "mac-float3";
                                std::string field_line = std::format("field {} {} kind={}", kdl_string(field.id), kdl_string(field.name), kdl_string(field_kind));
                                if (!field.unit.empty()) kdl_string_property(field_line, "unit", field.unit);
                                if (field.sampling != VolumeFieldSampling::Cell) kdl_string_property(field_line, "sampling", "vertex");
                                if (field.vector_space != VolumeVectorSpace::Local) kdl_string_property(field_line, "space", field.vector_space == VolumeVectorSpace::Grid ? "grid" : "world");
                                writer.line(field_line);
                            }
                        } else {
                            if (data.density != 1.0f) writer.line(std::format("density {}", data.density));
                            if (data.wispiness != 1.0f) writer.line(std::format("wispiness {}", data.wispiness));
                            if (data.frequency != 5.0f) writer.line(std::format("frequency {}", data.frequency));
                        }
                        const VolumeRendering& rendering = volume.rendering;
                        std::string rendering_line{"rendering"};
                        if (!rendering.density_field.empty()) kdl_string_property(rendering_line, "density-field", rendering.density_field);
                        if (!rendering.temperature_field.empty()) kdl_string_property(rendering_line, "temperature-field", rendering.temperature_field);
                        if (!rendering.emission_scale_field.empty()) kdl_string_property(rendering_line, "emission-scale-field", rendering.emission_scale_field);
                        if (!rendering.sigma_a_field.empty()) kdl_string_property(rendering_line, "sigma-a-field", rendering.sigma_a_field);
                        if (!rendering.sigma_s_field.empty()) kdl_string_property(rendering_line, "sigma-s-field", rendering.sigma_s_field);
                        if (!rendering.emission_field.empty()) kdl_string_property(rendering_line, "emission-field", rendering.emission_field);
                        if (rendering.field_color_space != SpectrumColorSpace::Srgb) kdl_string_property(rendering_line, "field-color-space", spectrum_color_space_name(rendering.field_color_space));
                        writer.begin(rendering_line);
                        write_spectrum(writer, "sigma-a", rendering.sigma_a);
                        write_spectrum(writer, "sigma-s", rendering.sigma_s);
                        write_spectrum(writer, "emission", rendering.emission);
                        if (rendering.density_scale != 1.0f) writer.line(std::format("density-scale {}", rendering.density_scale));
                        if (rendering.emission_scale != 1.0f) writer.line(std::format("emission-scale {}", rendering.emission_scale));
                        if (rendering.anisotropy != 0.0f) writer.line(std::format("anisotropy {}", rendering.anisotropy));
                        if (rendering.temperature_scale != 1.0f) writer.line(std::format("temperature-scale {}", rendering.temperature_scale));
                        if (rendering.temperature_offset != 0.0f) writer.line(std::format("temperature-offset {}", rendering.temperature_offset));
                        if (rendering.minimum_emission_temperature != 100.0f) writer.line(std::format("minimum-emission-temperature {}", rendering.minimum_emission_temperature));
                        if (rendering.blackbody_emission) writer.line("blackbody-emission #true");
                        writer.end();
                        const VolumeDiagnostics& diagnostics = volume.diagnostics;
                        if (diagnostics.mode != VolumeDiagnosticMode::Off) {
                            std::string diagnostics_line = std::format("diagnostics field={} mode={}", kdl_string(diagnostics.field_id), kdl_string(volume_diagnostic_mode_name(diagnostics.mode)));
                            if (diagnostics.mapping != VolumeFieldMapping::Value) kdl_string_property(diagnostics_line, "mapping", volume_field_mapping_name(diagnostics.mapping));
                            if (diagnostics.depth_mode != VisualizationDepthMode::Tested) kdl_string_property(diagnostics_line, "depth", depth_buffer_mode_name(diagnostics.depth_mode));
                            if (diagnostics.color_map != VisualizationColorMap::Viridis) kdl_string_property(diagnostics_line, "color-map", visualization_color_map_name(diagnostics.color_map));
                            if (diagnostics.minimum != 0.0f) kdl_number_property(diagnostics_line, "minimum", diagnostics.minimum);
                            if (diagnostics.maximum != 1.0f) kdl_number_property(diagnostics_line, "maximum", diagnostics.maximum);
                            if (diagnostics.slice_position != 0.5f) kdl_number_property(diagnostics_line, "slice", diagnostics.slice_position);
                            if (diagnostics.opacity != 1.0f) kdl_number_property(diagnostics_line, "opacity", diagnostics.opacity);
                            if (diagnostics.threshold != 0.5f) kdl_number_property(diagnostics_line, "threshold", diagnostics.threshold);
                            if (diagnostics.scale != 0.1f) kdl_number_property(diagnostics_line, "scale", diagnostics.scale);
                            if (diagnostics.width != 1.5f) kdl_number_property(diagnostics_line, "width", diagnostics.width);
                            if (diagnostics.axis != 2u) kdl_number_property(diagnostics_line, "axis", diagnostics.axis);
                            if (diagnostics.sampling != 8u) kdl_number_property(diagnostics_line, "sampling", diagnostics.sampling);
                            if (diagnostics.steps != 32u) kdl_number_property(diagnostics_line, "steps", diagnostics.steps);
                            writer.begin(diagnostics_line);
                            if (diagnostics.color != math::Float4{1.0f, 1.0f, 1.0f, 1.0f}) writer.line(std::format("color {} {} {} {}", diagnostics.color.x, diagnostics.color.y, diagnostics.color.z, diagnostics.color.w));
                            writer.end();
                        }
                        writer.end();
                    },
                    volume.data);
            }
            writer.end();
        }

        [[nodiscard]] std::string texture_value_kind_name(const TextureValueKind kind) {
            return kind == TextureValueKind::Float ? "float" : "spectrum";
        }

        [[nodiscard]] std::string texture_spectrum_type_name(const TextureSpectrumType type) {
            switch (type) {
            case TextureSpectrumType::Albedo: return "albedo";
            case TextureSpectrumType::Unbounded: return "unbounded";
            case TextureSpectrumType::Illuminant: return "illuminant";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_color_space_name(const TextureColorSpace color_space) {
            switch (color_space) {
            case TextureColorSpace::Linear: return "linear";
            case TextureColorSpace::Srgb: return "srgb";
            case TextureColorSpace::Aces2065_1: return "aces2065-1";
            case TextureColorSpace::Rec2020: return "rec2020";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_wrap_name(const TextureWrapMode wrap) {
            switch (wrap) {
            case TextureWrapMode::Repeat: return "repeat";
            case TextureWrapMode::Clamp: return "clamp";
            case TextureWrapMode::Black: return "black";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_channel_name(const TextureChannel channel) {
            switch (channel) {
            case TextureChannel::Red: return "red";
            case TextureChannel::Green: return "green";
            case TextureChannel::Blue: return "blue";
            case TextureChannel::Alpha: return "alpha";
            case TextureChannel::Average: return "average";
            case TextureChannel::Luminance: return "luminance";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_filter_name(const TextureFilter filter) {
            switch (filter) {
            case TextureFilter::Point: return "point";
            case TextureFilter::Bilinear: return "bilinear";
            case TextureFilter::Trilinear: return "trilinear";
            case TextureFilter::Ewa: return "ewa";
            }
            std::unreachable();
        }

        void add_texture_properties(std::string& line, const Texture& texture) {
            if (texture.value_kind != TextureValueKind::Spectrum) kdl_string_property(line, "value", texture_value_kind_name(texture.value_kind));
            if (texture.spectrum_type != TextureSpectrumType::Albedo) kdl_string_property(line, "spectrum-type", texture_spectrum_type_name(texture.spectrum_type));
            if (texture.color_space != TextureColorSpace::Srgb) kdl_string_property(line, "color-space", texture_color_space_name(texture.color_space));
        }

        void write_textures(KdlWriter& writer, const std::vector<Texture>& textures) {
            if (textures.empty()) return;
            writer.begin("textures");
            for (const Texture& texture : textures) {
                std::visit(
                    [&writer, &texture](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConstantTexture>) {
                            std::string line = std::format("constant {} {}", texture.id.value, kdl_string(texture.name));
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            if (texture.value_kind == TextureValueKind::Float)
                                writer.line(std::format("scalar {}", data.scalar));
                            else
                                write_spectrum(writer, "spectrum", data.spectrum);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageTexture>) {
                            std::string line = std::format("image {} {}", texture.id.value, kdl_string(texture.name));
                            write_source(line, data.source);
                            add_texture_properties(line, texture);
                            if (data.wrap != TextureWrapMode::Repeat) kdl_string_property(line, "wrap", texture_wrap_name(data.wrap));
                            if (data.channel != TextureChannel::Luminance) kdl_string_property(line, "channel", texture_channel_name(data.channel));
                            if (data.filter != TextureFilter::Bilinear) kdl_string_property(line, "filter", texture_filter_name(data.filter));
                            if (data.maximum_anisotropy != 8.0f) kdl_number_property(line, "maximum-anisotropy", data.maximum_anisotropy);
                            if (data.scale != 1.0f) kdl_number_property(line, "scale", data.scale);
                            if (data.invert) kdl_bool_property(line, "invert", true);
                            writer.begin(line);
                            write_texture_mapping(writer, data.mapping);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CheckerboardTexture>) {
                            std::string line = std::format("checkerboard {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            write_checkerboard_mapping(writer, data.mapping);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ScaleTexture>) {
                            std::string line = std::format("scale {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, MixTexture>) {
                            std::string line = std::format("mix {} {} first={} second={} amount={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value, data.amount.value);
                            add_texture_properties(line, texture);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DirectionMixTexture>) {
                            std::string line = std::format("direction-mix {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            if (data.direction != math::Float3{0.0f, 1.0f, 0.0f}) writer.line(std::format("direction {} {} {}", data.direction.x, data.direction.y, data.direction.z));
                            writer.end();
                        } else {
                            std::string line = std::format("bilerp {} {}", texture.id.value, kdl_string(texture.name));
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            for (std::uint32_t corner = 0; corner != 4; ++corner) {
                                if (texture.value_kind == TextureValueKind::Float)
                                    writer.line(std::format("corner {} {}", corner, data.scalars[corner]));
                                else
                                    write_spectrum(writer, std::format("corner-{}", corner), data.spectra[corner]);
                            }
                            write_texture_mapping(writer, data.mapping);
                            writer.end();
                        }
                    },
                    texture.data);
            }
            writer.end();
        }

        void write_normal_and_bump(KdlWriter& writer, const TextureId normal_map, const TextureId bump_map) {
            if (normal_map.value != 0) writer.line(std::format("normal-map {}", normal_map.value));
            if (bump_map.value != 0) writer.line(std::format("bump-map {}", bump_map.value));
        }

        void write_conductor_optics(KdlWriter& writer, const std::variant<ConductorEtaK, ConductorReflectance>& optics) {
            if (const ConductorEtaK* eta_k = std::get_if<ConductorEtaK>(&optics)) {
                writer.begin("eta-k");
                write_spectrum(writer, "eta", eta_k->eta);
                write_spectrum(writer, "k", eta_k->k);
                writer.end();
                return;
            }
            writer.begin("reflectance-optics");
            write_spectrum(writer, "reflectance", std::get<ConductorReflectance>(optics).reflectance);
            writer.end();
        }

        void write_coating(KdlWriter& writer, const CoatingLayer& coating) {
            writer.begin("coating");
            if (coating.thickness.value != 0.01f || coating.thickness.texture.value != 0) write_float_parameter(writer, "thickness", coating.thickness);
            write_spectrum(writer, "albedo", coating.albedo);
            if (coating.g.value != 0.0f || coating.g.texture.value != 0) write_float_parameter(writer, "g", coating.g);
            if (coating.max_depth != 10) writer.line(std::format("maximum-depth {}", coating.max_depth));
            if (coating.sample_count != 1) writer.line(std::format("samples {}", coating.sample_count));
            writer.end();
        }

        void write_materials(KdlWriter& writer, const std::vector<Material>& materials) {
            if (materials.empty()) return;
            writer.begin("materials");
            for (const Material& material : materials) {
                std::visit(
                    [&writer, &material](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InterfaceMaterialData>) {
                            writer.line(std::format("interface {} {}", material.id.value, kdl_string(material.name)));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseMaterialData>) {
                            writer.begin(std::format("diffuse {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseTransmissionMaterialData>) {
                            writer.begin(std::format("diffuse-transmission {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_spectrum(writer, "transmittance", data.transmittance);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConductorMaterialData>) {
                            writer.begin(std::format("conductor {} {}", material.id.value, kdl_string(material.name)));
                            write_conductor_optics(writer, data.optics);
                            write_roughness(writer, data.distribution);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DielectricMaterialData>) {
                            writer.begin(std::format("dielectric {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "eta", data.eta);
                            write_roughness(writer, data.distribution);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ThinDielectricMaterialData>) {
                            writer.begin(std::format("thin-dielectric {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "eta", data.eta);
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedDiffuseMaterialData>) {
                            writer.begin(std::format("coated-diffuse {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_spectrum(writer, "eta", data.eta);
                            writer.begin("interface");
                            write_roughness(writer, data.interface);
                            writer.end();
                            write_coating(writer, data.coating);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedConductorMaterialData>) {
                            writer.begin(std::format("coated-conductor {} {}", material.id.value, kdl_string(material.name)));
                            writer.begin("interface");
                            write_spectrum(writer, "eta", data.interface_eta);
                            write_roughness(writer, data.interface);
                            writer.end();
                            writer.begin("conductor");
                            write_conductor_optics(writer, data.optics);
                            write_roughness(writer, data.conductor);
                            writer.end();
                            write_coating(writer, data.coating);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else {
                            writer.begin(std::format("mix {} {} first={} second={}", material.id.value, kdl_string(material.name), data.first.value, data.second.value));
                            if (data.amount.value != 0.5f || data.amount.texture.value != 0) write_float_parameter(writer, "amount", data.amount);
                            writer.end();
                        }
                    },
                    material.data);
            }
            writer.end();
        }

        void write_media(KdlWriter& writer, const std::vector<Medium>& media) {
            if (media.empty()) return;
            writer.begin("media");
            for (const Medium& medium : media) {
                writer.begin(std::format("homogeneous {} {}", medium.id.value, kdl_string(medium.name)));
                write_spectrum(writer, "sigma-a", medium.sigma_a);
                write_spectrum(writer, "sigma-s", medium.sigma_s);
                write_spectrum(writer, "emission", medium.emission);
                if (medium.density_scale != 1.0f) writer.line(std::format("density-scale {}", medium.density_scale));
                if (medium.emission_scale != 1.0f) writer.line(std::format("emission-scale {}", medium.emission_scale));
                if (medium.anisotropy != 0.0f) writer.line(std::format("anisotropy {}", medium.anisotropy));
                writer.end();
            }
            writer.end();
        }

        void write_infinite_light(KdlWriter& writer, const InfiniteLight& light) {
            write_spectrum(writer, "radiance", light.radiance);
            if (light.transform != math::Transform{}) write_transform(writer, "transform", light.transform);
            if (light.scale != 1.0f) writer.line(std::format("scale {}", light.scale));
            if (light.emission_texture.value != 0) writer.line(std::format("emission-texture {}", light.emission_texture.value));
        }

        void write_lights(KdlWriter& writer, const std::vector<Light>& lights) {
            if (lights.empty()) return;
            writer.begin("lights");
            for (const Light& light : lights) {
                std::visit(
                    [&writer, &light](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointLight>) {
                            writer.begin(std::format("point {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "intensity", data.intensity);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SpotLight>) {
                            writer.begin(std::format("spot {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "intensity", data.intensity);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            if (data.cone_angle != 30.0f) writer.line(std::format("cone-angle {}", data.cone_angle));
                            if (data.cone_delta != 5.0f) writer.line(std::format("cone-delta {}", data.cone_delta));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DistantLight>) {
                            writer.begin(std::format("distant {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "radiance", data.radiance);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseAreaLight>) {
                            writer.begin(std::format("diffuse-area {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "radiance", data.radiance);
                            if (data.sidedness == EmissionSidedness::Both) writer.line("sidedness both");
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            if (data.power) writer.line(std::format("power {}", *data.power));
                            if (data.emission_texture.value != 0) writer.line(std::format("emission-texture {}", data.emission_texture.value));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InfiniteLight>) {
                            writer.begin(std::format("infinite {} {}", light.id.value, kdl_string(light.name)));
                            write_infinite_light(writer, data);
                            writer.end();
                        } else {
                            writer.begin(std::format("portal-infinite {} {}", light.id.value, kdl_string(light.name)));
                            writer.begin("environment");
                            write_infinite_light(writer, data.environment);
                            writer.end();
                            for (const std::array<math::Float3, 4>& portal : data.portals) {
                                writer.begin("portal");
                                for (const math::Float3 corner : portal) writer.line(std::format("corner {} {} {}", corner.x, corner.y, corner.z));
                                writer.end();
                            }
                            writer.end();
                        }
                    },
                    light.data);
            }
            writer.end();
        }

        void write_cameras(KdlWriter& writer, const std::vector<Camera>& cameras) {
            if (cameras.empty()) return;
            writer.begin("cameras");
            for (const Camera& camera : cameras) {
                std::visit(
                    [&writer, &camera](const auto& data) {
                        const std::string kind = std::same_as<std::remove_cvref_t<decltype(data)>, PerspectiveCameraData> ? "perspective" : "orthographic";
                        std::string line       = std::format("{} {} {}", kind, camera.id.value, kdl_string(camera.name));
                        if (camera.exposure_time != 1.0f) kdl_number_property(line, "exposure-time", camera.exposure_time);
                        if (camera.medium.value != 0) kdl_number_property(line, "medium", camera.medium.value);
                        writer.begin(line);
                        if (camera.transform != math::Transform{}) write_transform(writer, "transform", camera.transform);
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PerspectiveCameraData>)
                            if (data.vertical_fov != 45.0f) writer.line(std::format("vertical-fov {}", data.vertical_fov));
                        if (data.screen_window.minimum != math::Float2{-1.0f, -1.0f} || data.screen_window.maximum != math::Float2{1.0f, 1.0f}) writer.line(std::format("screen {} {} {} {}", data.screen_window.minimum.x, data.screen_window.minimum.y, data.screen_window.maximum.x, data.screen_window.maximum.y));
                        if (data.lens_radius != 0.0f) writer.line(std::format("lens-radius {}", data.lens_radius));
                        if (data.focal_distance != 1.0f) writer.line(std::format("focal-distance {}", data.focal_distance));
                        if (data.near_plane != 0.01f) writer.line(std::format("near-plane {}", data.near_plane));
                        if (data.far_plane != 1000.0f) writer.line(std::format("far-plane {}", data.far_plane));
                        writer.end();
                    },
                    camera.data);
            }
            writer.end();
        }

        [[nodiscard]] std::string filter_kind_name(const FilterKind kind) {
            switch (kind) {
            case FilterKind::Box: return "box";
            case FilterKind::Gaussian: return "gaussian";
            case FilterKind::Mitchell: return "mitchell";
            case FilterKind::Sinc: return "sinc";
            case FilterKind::Triangle: return "triangle";
            }
            std::unreachable();
        }

        void write_float_chunks(KdlWriter& writer, const std::string_view name, const std::span<const float> values) {
            if (values.empty()) return;
            writer.begin(name);
            for (std::size_t offset = 0; offset < values.size(); offset += 16) {
                std::string line{"values"};
                for (const float value : values.subspan(offset, std::min<std::size_t>(16, values.size() - offset))) line += std::format(" {}", value);
                writer.line(line);
            }
            writer.end();
        }

        void write_films(KdlWriter& writer, const std::vector<Film>& films) {
            if (films.empty()) return;
            writer.begin("films");
            for (const Film& film : films) {
                std::string line = std::format("film {} {}", film.id.value, kdl_string(film.name));
                if (film.exposure != 0.0f) kdl_number_property(line, "exposure", film.exposure);
                if (film.iso != 100.0f) kdl_number_property(line, "iso", film.iso);
                if (film.color_space != SpectrumColorSpace::Srgb) kdl_string_property(line, "color-space", spectrum_color_space_name(film.color_space));
                if (film.maximum_component_value) kdl_number_property(line, "maximum-component", *film.maximum_component_value);
                if (film.gbuffer) kdl_bool_property(line, "gbuffer", true);
                if (film.gbuffer && !film.gbuffer_camera_space) kdl_bool_property(line, "gbuffer-camera-space", false);
                writer.begin(line);
                writer.line(std::format("resolution {} {}", film.resolution[0], film.resolution[1]));
                if (film.pixel_minimum != std::array<std::uint32_t, 2>{} || film.pixel_maximum != film.resolution) writer.line(std::format("pixel-range {} {} {} {}", film.pixel_minimum[0], film.pixel_minimum[1], film.pixel_maximum[0], film.pixel_maximum[1]));
                write_float_chunks(writer, "sensor-response", film.sensor_response);
                if (film.sensor_to_output_rgb != Film{}.sensor_to_output_rgb) {
                    writer.begin("sensor-to-output-rgb");
                    for (std::uint32_t row = 0; row != 3; ++row) writer.line(std::format("row {} {} {}", film.sensor_to_output_rgb[row * 3], film.sensor_to_output_rgb[row * 3 + 1], film.sensor_to_output_rgb[row * 3 + 2]));
                    writer.end();
                }
                if (film.filter != Filter{}) {
                    std::string filter = std::format("filter {}", filter_kind_name(film.filter.kind));
                    if (film.filter.radius != math::Float2{0.5f, 0.5f}) filter += std::format(" radius-x={} radius-y={}", film.filter.radius.x, film.filter.radius.y);
                    if (film.filter.sigma != 0.5f) kdl_number_property(filter, "sigma", film.filter.sigma);
                    if (film.filter.b != 1.0f / 3.0f) kdl_number_property(filter, "b", film.filter.b);
                    if (film.filter.c != 1.0f / 3.0f) kdl_number_property(filter, "c", film.filter.c);
                    if (film.filter.tau != 3.0f) kdl_number_property(filter, "tau", film.filter.tau);
                    writer.line(filter);
                }
                writer.end();
            }
            writer.end();
        }

        [[nodiscard]] std::string sampler_kind_name(const SamplerKind kind) {
            switch (kind) {
            case SamplerKind::Independent: return "independent";
            case SamplerKind::Stratified: return "stratified";
            case SamplerKind::Halton: return "halton";
            case SamplerKind::Sobol: return "sobol";
            case SamplerKind::PaddedSobol: return "padded-sobol";
            case SamplerKind::ZSobol: return "zsobol";
            case SamplerKind::Pmj02bn: return "pmj02bn";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string sampler_randomization_name(const SamplerRandomization randomization) {
            switch (randomization) {
            case SamplerRandomization::None: return "none";
            case SamplerRandomization::PermuteDigits: return "permute-digits";
            case SamplerRandomization::FastOwen: return "fast-owen";
            case SamplerRandomization::Owen: return "owen";
            }
            std::unreachable();
        }

        void write_samplers(KdlWriter& writer, const std::vector<Sampler>& samplers) {
            if (samplers.empty()) return;
            writer.begin("samplers");
            for (const Sampler& sampler : samplers) {
                std::string line = std::format("sampler {} {}", sampler.id.value, kdl_string(sampler.name));
                if (sampler.kind != SamplerKind::Independent) kdl_string_property(line, "kind", sampler_kind_name(sampler.kind));
                if (sampler.samples_per_pixel != 1) kdl_number_property(line, "samples", sampler.samples_per_pixel);
                if (sampler.seed != 0) kdl_number_property(line, "seed", sampler.seed);
                if (!sampler.jitter) kdl_bool_property(line, "jitter", false);
                if (sampler.x_strata != 1) kdl_number_property(line, "x-strata", sampler.x_strata);
                if (sampler.y_strata != 1) kdl_number_property(line, "y-strata", sampler.y_strata);
                if (sampler.randomization != SamplerRandomization::Owen) kdl_string_property(line, "randomization", sampler_randomization_name(sampler.randomization));
                writer.line(line);
            }
            writer.end();
        }

        void write_prototypes(KdlWriter& writer, const std::vector<Prototype>& prototypes) {
            if (prototypes.empty()) return;
            writer.begin("prototypes");
            for (const Prototype& prototype : prototypes) {
                writer.begin(std::format("prototype {} {}", prototype.id.value, kdl_string(prototype.name)));
                for (const Primitive& primitive : prototype.primitives) {
                    std::string line{"primitive"};
                    if (primitive.geometry.value != 0) kdl_number_property(line, "geometry", primitive.geometry.value);
                    if (primitive.spheres.value != 0) kdl_number_property(line, "spheres", primitive.spheres.value);
                    if (primitive.material.value != 0) kdl_number_property(line, "material", primitive.material.value);
                    if (primitive.area_light.value != 0) kdl_number_property(line, "area-light", primitive.area_light.value);
                    if (primitive.alpha.value != 0) kdl_number_property(line, "alpha", primitive.alpha.value);
                    if (primitive.reverse_orientation) kdl_bool_property(line, "reverse-orientation", true);
                    const bool has_children = primitive.media.inside.value != 0 || primitive.media.outside.value != 0 || !primitive.face_materials.empty() || primitive.transform != math::Transform{};
                    if (!has_children) {
                        writer.line(line);
                        continue;
                    }
                    writer.begin(line);
                    if (primitive.media.inside.value != 0 || primitive.media.outside.value != 0) {
                        std::string media{"media"};
                        if (primitive.media.inside.value != 0) kdl_number_property(media, "inside", primitive.media.inside.value);
                        if (primitive.media.outside.value != 0) kdl_number_property(media, "outside", primitive.media.outside.value);
                        writer.line(media);
                    }
                    if (!primitive.face_materials.empty()) {
                        std::string face_materials{"face-materials"};
                        for (const MaterialId material : primitive.face_materials) face_materials += std::format(" {}", material.value);
                        writer.line(face_materials);
                    }
                    if (primitive.transform != math::Transform{}) write_transform(writer, "transform", primitive.transform);
                    writer.end();
                }
                writer.end();
            }
            writer.end();
        }

        void write_instances(KdlWriter& writer, const std::vector<Instance>& instances) {
            if (instances.empty()) return;
            writer.begin("instances");
            for (const Instance& instance : instances) {
                std::string line = std::format("instance {} {} prototype={}", instance.id.value, kdl_string(instance.name), instance.prototype.value);
                if (!instance.visible) kdl_bool_property(line, "visible", false);
                if (instance.transform == math::Transform{})
                    writer.line(line);
                else {
                    writer.begin(line);
                    write_transform(writer, "transform", instance.transform);
                    writer.end();
                }
            }
            writer.end();
        }

        [[nodiscard]] std::string light_sampler_name(const LightSamplerKind kind) {
            switch (kind) {
            case LightSamplerKind::Uniform: return "uniform";
            case LightSamplerKind::Power: return "power";
            case LightSamplerKind::Bvh: return "bvh";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string visualization_view_kind_name(const VisualizationViewKind kind) {
            switch (kind) {
            case VisualizationViewKind::Points: return "points";
            case VisualizationViewKind::Segments: return "segments";
            case VisualizationViewKind::Vectors: return "vectors";
            case VisualizationViewKind::Image: return "image";
            case VisualizationViewKind::Surface: return "surface";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string depth_buffer_mode_name(const VisualizationDepthMode mode) {
            switch (mode) {
            case VisualizationDepthMode::Tested: return "tested";
            case VisualizationDepthMode::XRay: return "xray";
            case VisualizationDepthMode::Overlay: return "overlay";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string visualization_composition_domain_name(const VisualizationCompositionDomain domain) {
            if (domain == VisualizationCompositionDomain::SceneLinear) return "scene-linear";
            return "display-referred";
        }

        [[nodiscard]] std::string point_glyph_name(const PointGlyph glyph) {
            switch (glyph) {
            case PointGlyph::ScreenDisc: return "screen-disc";
            case PointGlyph::WorldDisc: return "world-disc";
            case PointGlyph::Sphere: return "sphere";
            case PointGlyph::Cross: return "cross";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string point_shading_name(const PointShading shading) {
            return shading == PointShading::Lit ? "lit" : "unlit";
        }

        [[nodiscard]] std::string visualization_color_source_name(const VisualizationColorSource source) {
            switch (source) {
            case VisualizationColorSource::Element: return "element";
            case VisualizationColorSource::Uniform: return "uniform";
            case VisualizationColorSource::Scalar: return "scalar";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string visualization_color_map_name(const VisualizationColorMap map) {
            switch (map) {
            case VisualizationColorMap::Viridis: return "viridis";
            case VisualizationColorMap::Turbo: return "turbo";
            case VisualizationColorMap::CoolWarm: return "cool-warm";
            case VisualizationColorMap::Grayscale: return "grayscale";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string dynamic_parameter_kind_name(const DynamicParameterKind kind) {
            switch (kind) {
            case DynamicParameterKind::Boolean: return "boolean";
            case DynamicParameterKind::Integer: return "integer";
            case DynamicParameterKind::Float: return "float";
            case DynamicParameterKind::Float3: return "float3";
            case DynamicParameterKind::Enumeration: return "enumeration";
            }
            std::unreachable();
        }

        void write_dynamics(KdlWriter& writer, const DynamicSetup& setup) {
            std::string line{"dynamics"};
            if (setup.seed != 0) kdl_number_property(line, "seed", setup.seed);
            writer.begin(line);
            std::string clock{"clock"};
            if (setup.clock.step_seconds != 1.0 / 120.0) kdl_number_property(clock, "step-seconds", setup.clock.step_seconds);
            if (setup.clock.start_step != 0) kdl_number_property(clock, "start-step", setup.clock.start_step);
            if (setup.clock.end_step) kdl_number_property(clock, "end-step", *setup.clock.end_step);
            if (setup.clock.loop) kdl_bool_property(clock, "loop", true);
            if (clock != "clock") writer.line(clock);
            for (const DynamicSystem& system : setup.systems) {
                std::string system_line = std::format("system {} {} provider={}", kdl_string(system.id.value), kdl_string(system.name), kdl_string(system.provider_id));
                if (!system.enabled) kdl_bool_property(system_line, "enabled", false);
                if (!system.visible) kdl_bool_property(system_line, "visible", false);
                writer.begin(system_line);
                for (const DynamicParameterSetting& parameter : system.parameters) {
                    std::string parameter_line = std::format("parameter {} {}", kdl_string(parameter.parameter_id), dynamic_parameter_kind_name(parameter.value.kind));
                    if (parameter.value.kind == DynamicParameterKind::Boolean)
                        parameter_line += std::format(" #{}", parameter.value.integer != 0 ? "true" : "false");
                    else if (parameter.value.kind == DynamicParameterKind::Integer || parameter.value.kind == DynamicParameterKind::Enumeration)
                        parameter_line += std::format(" {}", parameter.value.integer);
                    else if (parameter.value.kind == DynamicParameterKind::Float)
                        parameter_line += std::format(" {}", parameter.value.floating[0]);
                    else
                        parameter_line += std::format(" {} {} {}", parameter.value.floating[0], parameter.value.floating[1], parameter.value.floating[2]);
                    writer.line(parameter_line);
                }
                for (const DynamicSceneBinding& binding : system.scene_bindings) writer.line(std::format("scene-bind {} {}", kdl_string(binding.dataset_id), binding.resource_id));
                for (const DynamicVisualizationView& view : system.visualizations) {
                    std::string view_line = std::format("visualize {} {} kind={} depth={}", kdl_string(view.dataset_id), kdl_string(view.name), kdl_string(visualization_view_kind_name(visualization_view_kind(view))), kdl_string(depth_buffer_mode_name(view.depth_mode)));
                    if (view.composition_domain != VisualizationCompositionDomain::DisplayReferred) kdl_string_property(view_line, "domain", visualization_composition_domain_name(view.composition_domain));
                    if (view.anchor.value != 0) kdl_number_property(view_line, "anchor", view.anchor.value);
                    std::visit(
                        [&view_line](const auto& data) {
                            constexpr bool has_width  = std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>;
                            constexpr bool has_scale  = std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>;
                            constexpr bool has_scalar = std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, SurfaceVisualization>;
                            if constexpr (has_width)
                                if (data.width != 1.0f) kdl_number_property(view_line, "width", data.width);
                            if constexpr (has_scale)
                                if (data.scale != 1.0f) kdl_number_property(view_line, "scale", data.scale);
                            if constexpr (has_scalar) {
                                if (data.scalar_minimum != 0.0f) kdl_number_property(view_line, "scalar-min", data.scalar_minimum);
                                if (data.scalar_maximum != 1.0f) kdl_number_property(view_line, "scalar-max", data.scalar_maximum);
                                if (data.color_source != VisualizationColorSource::Element) kdl_string_property(view_line, "color-source", visualization_color_source_name(data.color_source));
                                if (data.color_map != VisualizationColorMap::Viridis) kdl_string_property(view_line, "color-map", visualization_color_map_name(data.color_map));
                            }
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointVisualization>) {
                                if (data.glyph != PointGlyph::ScreenDisc) kdl_string_property(view_line, "glyph", point_glyph_name(data.glyph));
                                if (data.shading != PointShading::Unlit) kdl_string_property(view_line, "shading", point_shading_name(data.shading));
                            }
                        },
                        view.data);
                    if (!view.visible) kdl_bool_property(view_line, "visible", false);
                    writer.begin(view_line);
                    if (view.color != math::Float4{1.0f, 1.0f, 1.0f, 1.0f}) writer.line(std::format("color {} {} {} {}", view.color.x, view.color.y, view.color.z, view.color.w));
                    std::visit(
                        [&writer](const auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageVisualization>)
                                if (data.screen_rect != math::Float4{0.02f, 0.02f, 0.32f, 0.32f}) writer.line(std::format("screen-rect {} {} {} {}", data.screen_rect.x, data.screen_rect.y, data.screen_rect.z, data.screen_rect.w));
                        },
                        view.data);
                    writer.end();
                }
                writer.end();
            }
            writer.end();
        }

        [[nodiscard]] std::string serialize_scene_kdl(const Scene& scene) {
            KdlWriter writer{};
            writer.begin(std::format("spectra {} {}", current_scene_format_version, kdl_string(scene.name)));
            writer.line(std::format("active camera={} film={} sampler={}", scene.active_camera.value, scene.active_film.value, scene.active_sampler.value));
            std::string transport{"transport"};
            if (scene.transport.maximum_depth != 5) kdl_number_property(transport, "maximum-depth", scene.transport.maximum_depth);
            if (scene.transport.light_sampler != LightSamplerKind::Bvh) kdl_string_property(transport, "light-sampler", light_sampler_name(scene.transport.light_sampler));
            if (scene.transport.regularize) kdl_bool_property(transport, "regularize", true);
            if (transport != "transport") writer.line(transport);
            write_geometries(writer, scene.resources.geometries);
            write_sphere_sets(writer, scene.resources.sphere_sets);
            write_volumes(writer, scene.resources.volumes);
            write_textures(writer, scene.resources.textures);
            write_materials(writer, scene.resources.materials);
            write_media(writer, scene.resources.media);
            write_lights(writer, scene.resources.lights);
            write_cameras(writer, scene.resources.cameras);
            write_films(writer, scene.resources.films);
            write_samplers(writer, scene.resources.samplers);
            write_prototypes(writer, scene.resources.prototypes);
            write_instances(writer, scene.resources.instances);
            if (scene.dynamic_setup) write_dynamics(writer, *scene.dynamic_setup);
            writer.end();
            return writer.content;
        }

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

        [[nodiscard]] VolumeDiagnosticMode read_volume_diagnostic_mode(const std::string_view value) {
            if (value == "off") return VolumeDiagnosticMode::Off;
            if (value == "slice") return VolumeDiagnosticMode::Slice;
            if (value == "ray-march") return VolumeDiagnosticMode::RayMarch;
            if (value == "maximum-intensity-projection") return VolumeDiagnosticMode::MaximumIntensityProjection;
            if (value == "isosurface") return VolumeDiagnosticMode::Isosurface;
            if (value == "glyphs") return VolumeDiagnosticMode::Glyphs;
            if (value == "streamlines") return VolumeDiagnosticMode::Streamlines;
            if (value == "lic") return VolumeDiagnosticMode::Lic;
            throw std::runtime_error(std::format("Unknown Volume diagnostic mode {}", value));
        }

        [[nodiscard]] VolumeFieldMapping read_volume_field_mapping(const std::string_view value) {
            if (value == "value") return VolumeFieldMapping::Value;
            if (value == "magnitude") return VolumeFieldMapping::Magnitude;
            if (value == "x") return VolumeFieldMapping::X;
            if (value == "y") return VolumeFieldMapping::Y;
            if (value == "z") return VolumeFieldMapping::Z;
            if (value == "divergence") return VolumeFieldMapping::Divergence;
            if (value == "curl-magnitude") return VolumeFieldMapping::CurlMagnitude;
            if (value == "q-criterion") return VolumeFieldMapping::QCriterion;
            throw std::runtime_error(std::format("Unknown Volume Field mapping {}", value));
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
                        VolumeField field{
                            .id           = kdl_value_text(field_node.args()[0]),
                            .name         = kdl_value_text(field_node.args()[1]),
                            .unit         = kdl_string_property(field_node, u8"unit"),
                            .kind         = kind == "float" ? VolumeFieldKind::Float : kind == "float3" ? VolumeFieldKind::Float3 : kind == "mac-float3" ? VolumeFieldKind::MacFloat3 : throw std::runtime_error(std::format("Unknown Volume Field kind {}", kind)),
                            .sampling     = sampling == "cell" ? VolumeFieldSampling::Cell : sampling == "vertex" ? VolumeFieldSampling::Vertex : throw std::runtime_error(std::format("Unknown Volume Field sampling {}", sampling)),
                            .vector_space = space == "grid" ? VolumeVectorSpace::Grid : space == "local" ? VolumeVectorSpace::Local : space == "world" ? VolumeVectorSpace::World : throw std::runtime_error(std::format("Unknown Volume Field space {}", space)),
                        };
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
                        .mapping        = read_volume_field_mapping(kdl_string_property(*diagnostics_node, u8"mapping", "value")),
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
                    };
                    if (const kdl::Node* color = kdl_child(*diagnostics_node, u8"color")) volume.diagnostics.color = {kdl_number<float>(color->args()[0]), kdl_number<float>(color->args()[1]), kdl_number<float>(color->args()[2]), kdl_number<float>(color->args()[3])};
                }
                resources.volumes.push_back(std::move(volume));
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

        [[nodiscard]] VisualizationViewKind read_visualization_view_kind(const std::string_view value) {
            if (value == "points") return VisualizationViewKind::Points;
            if (value == "segments") return VisualizationViewKind::Segments;
            if (value == "vectors") return VisualizationViewKind::Vectors;
            if (value == "image") return VisualizationViewKind::Image;
            if (value == "surface") return VisualizationViewKind::Surface;
            throw std::runtime_error(std::format("Unknown Visualization view kind {}", value));
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

        [[nodiscard]] PointGlyph read_point_glyph(const std::string_view value) {
            if (value == "screen-disc") return PointGlyph::ScreenDisc;
            if (value == "world-disc") return PointGlyph::WorldDisc;
            if (value == "sphere") return PointGlyph::Sphere;
            if (value == "cross") return PointGlyph::Cross;
            throw std::runtime_error(std::format("Unknown Point glyph {}", value));
        }

        [[nodiscard]] PointShading read_point_shading(const std::string_view value) {
            if (value == "unlit") return PointShading::Unlit;
            if (value == "lit") return PointShading::Lit;
            throw std::runtime_error(std::format("Unknown Point shading {}", value));
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
                        switch (read_visualization_view_kind(kdl_string_property(child, u8"kind"))) {
                        case VisualizationViewKind::Points: view.data = PointVisualization{width, scale, scalar_minimum, scalar_maximum, read_point_glyph(kdl_string_property(child, u8"glyph", "screen-disc")), read_point_shading(kdl_string_property(child, u8"shading", "unlit")), color_source, color_map}; break;
                        case VisualizationViewKind::Segments: view.data = SegmentVisualization{width, scalar_minimum, scalar_maximum, color_source, color_map}; break;
                        case VisualizationViewKind::Vectors: view.data = VectorVisualization{width, scale, scalar_minimum, scalar_maximum, color_source, color_map}; break;
                        case VisualizationViewKind::Image: view.data = ImageVisualization{screen_rect}; break;
                        case VisualizationViewKind::Surface: view.data = SurfaceVisualization{scalar_minimum, scalar_maximum, color_source, color_map}; break;
                        }
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
            Scene scene{.name = kdl_value_text(root.args()[1])};
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
                else if (node.name() == u8"volumes")
                    read_volumes(scene.resources, node);
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
            return scene;
        }

        [[nodiscard]] std::vector<std::string_view> scene_sources(const Scene& scene) {
            std::vector<std::string_view> sources{};
            const auto add = [&sources](const std::string& source) {
                if (!source.empty() && !std::ranges::contains(sources, source)) sources.push_back(source);
            };
            for (const Geometry& geometry : scene.resources.geometries)
                if (const TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) add(mesh->source);
            for (const SphereSet& spheres : scene.resources.sphere_sets) add(spheres.source);
            for (const Volume& volume : scene.resources.volumes) {
                if (const GridVolume* grid = std::get_if<GridVolume>(&volume.data)) add(grid->source);
            }
            for (const Texture& texture : scene.resources.textures)
                if (const ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) add(image->source);
            return sources;
        }
    } // namespace

    Scene load_scene(const std::filesystem::path& path) {
        Scene scene = parse_scene(path);
        load_scene_sources(scene, path.parent_path());
        return scene;
    }

    void save_scene(const Scene& scene, const std::filesystem::path& path, const std::filesystem::path& source_scene_path) {
        const std::filesystem::path target_root = path.parent_path();
        if (!target_root.empty()) std::filesystem::create_directories(target_root);
        const std::filesystem::path source_root = source_scene_path.empty() ? target_root : source_scene_path.parent_path();
        std::vector<std::filesystem::path> created_sources{};
        static std::atomic_uint64_t temporary_sequence{};
        std::filesystem::path temporary_path = path;
        temporary_path += std::format(".spectra-save-{}-{}.tmp", std::chrono::steady_clock::now().time_since_epoch().count(), temporary_sequence.fetch_add(1, std::memory_order_relaxed));
        try {
            if (std::filesystem::absolute(source_root).lexically_normal() != std::filesystem::absolute(target_root).lexically_normal())
                for (const std::string_view source : scene_sources(scene)) {
                    const std::filesystem::path destination = target_root / source;
                    std::filesystem::create_directories(destination.parent_path());
                    std::filesystem::copy_file(source_root / source, destination);
                    created_sources.push_back(destination);
                }
            const std::string document = serialize_scene_kdl(scene);
            std::ofstream stream{temporary_path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra scene: {}", temporary_path.string()));
            stream.write(document.data(), static_cast<std::streamsize>(document.size()));
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra scene: {}", temporary_path.string()));
            stream.close();
            if (!stream) throw std::runtime_error(std::format("Failed to close Spectra scene: {}", temporary_path.string()));
#if defined(_WIN32)
            if (!MoveFileExW(temporary_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(std::format("Failed to replace Spectra scene '{}': Windows error {}", path.string(), GetLastError()));
#else
            std::error_code replacement_error{};
            std::filesystem::rename(temporary_path, path, replacement_error);
            if (replacement_error) throw std::runtime_error(std::format("Failed to replace Spectra scene '{}': {}", path.string(), replacement_error.message()));
#endif
        } catch (...) {
            std::error_code error{};
            std::filesystem::remove(temporary_path, error);
            for (const std::filesystem::path& created : created_sources | std::views::reverse) std::filesystem::remove(created, error);
            throw;
        }
    }
} // namespace spectra::scene
