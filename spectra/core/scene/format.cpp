module;
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#undef interface
#endif

module spectra.scene.format;
import std;


namespace spectra::scene {
    namespace {
        constexpr std::uint32_t current_scene_format_version = 36;

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
        [[nodiscard]] std::string field_mapping_name(FieldMapping mapping);
        [[nodiscard]] std::string visualization_color_map_name(VisualizationColorMap map);

        [[nodiscard]] std::string particle_display_name(const ParticleDisplayMode display) {
            if (display == ParticleDisplayMode::Points) return "points";
            if (display == ParticleDisplayMode::Discs) return "discs";
            return "spheres";
        }

        void write_particle_sets(KdlWriter& writer, const std::vector<ParticleSet>& particle_sets) {
            if (particle_sets.empty()) return;
            writer.begin("particle-sets");
            for (const ParticleSet& particles : particle_sets) {
                std::string line = std::format("particle-set {} {}", particles.id.value, kdl_string(particles.name));
                if (!particles.visible) kdl_bool_property(line, "visible", false);
                writer.begin(line);
                write_bounds(writer, particles.domain);
                if (particles.transform != math::Transform{}) write_transform(writer, "transform", particles.transform);
                if (particles.radius != 0.01f) writer.line(std::format("radius {}", particles.radius));
                const ParticleVisualization& visualization = particles.visualization;
                std::string visualization_line{"visualization"};
                kdl_string_property(visualization_line, "display", particle_display_name(visualization.display));
                if (!visualization.field_id.empty()) kdl_string_property(visualization_line, "field", visualization.field_id);
                if (visualization.mapping != FieldMapping::Value) kdl_string_property(visualization_line, "mapping", field_mapping_name(visualization.mapping));
                if (visualization.depth_mode != VisualizationDepthMode::Tested) kdl_string_property(visualization_line, "depth", depth_buffer_mode_name(visualization.depth_mode));
                if (visualization.color_map != VisualizationColorMap::Viridis) kdl_string_property(visualization_line, "color-map", visualization_color_map_name(visualization.color_map));
                if (visualization.minimum != 0.0f) kdl_number_property(visualization_line, "minimum", visualization.minimum);
                if (visualization.maximum != 1.0f) kdl_number_property(visualization_line, "maximum", visualization.maximum);
                if (visualization.radius_scale != 1.0f) kdl_number_property(visualization_line, "radius-scale", visualization.radius_scale);
                if (visualization.point_size != 2.0f) kdl_number_property(visualization_line, "point-size", visualization.point_size);
                writer.begin(visualization_line);
                if (visualization.color != math::Float4{0.18f, 0.55f, 1.0f, 1.0f}) writer.line(std::format("color {} {} {} {}", visualization.color.x, visualization.color.y, visualization.color.z, visualization.color.w));
                writer.end();
                const ParticleDiagnostics& diagnostics = particles.diagnostics;
                if (!diagnostics.vector_field.empty()) {
                    std::string diagnostics_line{"diagnostics"};
                    kdl_string_property(diagnostics_line, "vector-field", diagnostics.vector_field);
                    if (diagnostics.color_map != VisualizationColorMap::Turbo) kdl_string_property(diagnostics_line, "color-map", visualization_color_map_name(diagnostics.color_map));
                    if (diagnostics.minimum != 0.0f) kdl_number_property(diagnostics_line, "minimum", diagnostics.minimum);
                    if (diagnostics.maximum != 1.0f) kdl_number_property(diagnostics_line, "maximum", diagnostics.maximum);
                    if (diagnostics.scale != 0.1f) kdl_number_property(diagnostics_line, "scale", diagnostics.scale);
                    if (diagnostics.width != 1.5f) kdl_number_property(diagnostics_line, "width", diagnostics.width);
                    if (diagnostics.sampling != 8u) kdl_number_property(diagnostics_line, "sampling", diagnostics.sampling);
                    writer.line(diagnostics_line);
                }
                writer.end();
            }
            writer.end();
        }

        [[nodiscard]] std::string visualization_composition_domain_name(VisualizationCompositionDomain domain);

        [[nodiscard]] std::string volume_diagnostic_mode_name(const VolumeDiagnosticMode mode) {
            switch (mode) {
            case VolumeDiagnosticMode::Off: return "off";
            case VolumeDiagnosticMode::Slice: return "slice";
            case VolumeDiagnosticMode::Cells: return "cells";
            case VolumeDiagnosticMode::RayMarch: return "ray-march";
            case VolumeDiagnosticMode::MaximumIntensityProjection: return "maximum-intensity-projection";
            case VolumeDiagnosticMode::Isosurface: return "isosurface";
            case VolumeDiagnosticMode::Glyphs: return "glyphs";
            case VolumeDiagnosticMode::Streamlines: return "streamlines";
            case VolumeDiagnosticMode::Lic: return "lic";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string field_mapping_name(const FieldMapping mapping) {
            switch (mapping) {
            case FieldMapping::Value: return "value";
            case FieldMapping::Magnitude: return "magnitude";
            case FieldMapping::X: return "x";
            case FieldMapping::Y: return "y";
            case FieldMapping::Z: return "z";
            case FieldMapping::Divergence: return "divergence";
            case FieldMapping::CurlMagnitude: return "curl-magnitude";
            case FieldMapping::QCriterion: return "q-criterion";
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
                                const FieldKind kind = field_kind(field);
                                const std::string_view field_kind = kind == FieldKind::Float ? "float" : kind == FieldKind::Float3 ? "float3" : kind == FieldKind::UInt32 ? "uint32" : "mac-float3";
                                std::string field_line = std::format("field {} {} kind={}", kdl_string(field.id), kdl_string(field.name), kdl_string(field_kind));
                                if (!field.unit.empty()) kdl_string_property(field_line, "unit", field.unit);
                                if (field_sampling(field) != VolumeFieldSampling::Cell) kdl_string_property(field_line, "sampling", "vertex");
                                const VolumeVectorSpace vector_space = field_vector_space(field);
                                if (vector_space != VolumeVectorSpace::Local) kdl_string_property(field_line, "space", vector_space == VolumeVectorSpace::Grid ? "grid" : "world");
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
                            if (diagnostics.mapping != FieldMapping::Value) kdl_string_property(diagnostics_line, "mapping", field_mapping_name(diagnostics.mapping));
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
                            if (diagnostics.category_mask != 0xfffffffeu) kdl_number_property(diagnostics_line, "category-mask", diagnostics.category_mask);
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

        void write_neural_fields(KdlWriter& writer, const std::vector<NeuralField>& fields) {
            if (fields.empty()) return;
            writer.begin("neural-fields");
            for (const NeuralField& field : fields) {
                std::string line = std::format("hash-grid-radiance-field {} {}", field.id.value, kdl_string(field.name));
                if (!field.visible) kdl_bool_property(line, "visible", false);
                if (field.transform == math::Transform{} && !field.diagnostics.occupancy_grid) writer.line(line);
                else {
                    writer.begin(line);
                    if (field.transform != math::Transform{}) write_transform(writer, "transform", field.transform);
                    if (field.diagnostics.occupancy_grid) writer.line("diagnostics occupancy-grid=#true");
                    writer.end();
                }
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

        [[nodiscard]] std::string visualization_view_kind_name(const DynamicVisualizationView& view) {
            return std::visit([]<typename Type>(const Type&) -> std::string {
                if constexpr (std::same_as<Type, SegmentVisualization>) return "segments";
                else if constexpr (std::same_as<Type, VectorVisualization>) return "vectors";
                else if constexpr (std::same_as<Type, ImageVisualization>) return "image";
                else return "surface";
            }, view.data);
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
                    std::string view_line = std::format("visualize {} {} kind={} depth={}", kdl_string(view.dataset_id), kdl_string(view.name), kdl_string(visualization_view_kind_name(view)), kdl_string(depth_buffer_mode_name(view.depth_mode)));
                    if (view.composition_domain != VisualizationCompositionDomain::DisplayReferred) kdl_string_property(view_line, "domain", visualization_composition_domain_name(view.composition_domain));
                    if (view.anchor.value != 0) kdl_number_property(view_line, "anchor", view.anchor.value);
                    std::visit(
                        [&view_line](const auto& data) {
                            constexpr bool has_width  = std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>;
                            constexpr bool has_scale  = std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization>;
                            constexpr bool has_scalar = std::same_as<std::remove_cvref_t<decltype(data)>, SegmentVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, VectorVisualization> || std::same_as<std::remove_cvref_t<decltype(data)>, SurfaceVisualization>;
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
            write_particle_sets(writer, scene.resources.particle_sets);
            write_volumes(writer, scene.resources.volumes);
            write_neural_fields(writer, scene.resources.neural_fields);
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
