module;

#include <imgui.h>

module spectra.editor.ui;
import std;

namespace spectra {
    void EditorUi::draw_selection_diagnostics(const SceneEntityReference entity) {
        const scene::Scene& evaluated            = this->context.document.content.evaluated;
        EntityDiagnostics& diagnostics = this->context.settings.entity_diagnostics.try_emplace(entity).first->second;
        const SceneEntityKind entity_type = scene_entity_kind(entity);
        bool geometry{};
        bool spheres{};
        bool area_emitter{};
        bool medium_boundary{};
        if (entity_type == SceneEntityKind::Instance || entity_type == SceneEntityKind::AreaEmitter) {
            const scene::InstanceId instance_id = entity_type == SceneEntityKind::Instance ? std::get<scene::InstanceId>(entity.data) : std::get<SceneEntityReference::AreaEmitter>(entity.data).instance;
            const scene::Instance& instance   = *std::ranges::find(evaluated.resources.instances, instance_id, &scene::Instance::id);
            const scene::Prototype& prototype = *std::ranges::find(evaluated.resources.prototypes, instance.prototype, &scene::Prototype::id);
            for (const scene::Primitive& primitive : prototype.primitives) {
                geometry        = geometry || primitive.geometry.value != 0;
                spheres         = spheres || primitive.spheres.value != 0;
                area_emitter    = area_emitter || primitive.area_light.value != 0;
                medium_boundary = medium_boundary || primitive.media.inside.value != 0 || primitive.media.outside.value != 0;
            }
        }

        ImGui::TextDisabled("DIAGNOSTICS");
        ImGui::BeginDisabled(!this->context.settings.guides_visible);
        if (entity_type == SceneEntityKind::Instance || entity_type == SceneEntityKind::AreaEmitter || entity_type == SceneEntityKind::ParticleSet || entity_type == SceneEntityKind::Volume || entity_type == SceneEntityKind::NeuralField) ImGui::Checkbox("Bounds", &diagnostics.bounds);
        if (entity_type == SceneEntityKind::Instance) {
            if (geometry || spheres) ImGui::Checkbox(spheres && !geometry ? "Sphere wireframe" : "Wireframe", &diagnostics.wireframe);
            if (geometry || spheres) ImGui::Checkbox(spheres && !geometry ? "Centers" : spheres ? "Vertices / centers" : "Vertices", &diagnostics.vertices);
            if (geometry) {
                ImGui::Checkbox("Normals", &diagnostics.normals);
                ImGui::Checkbox("Tangents", &diagnostics.tangents);
            }
            if (area_emitter) ImGui::Checkbox("Area emitter", &diagnostics.area_emitter);
            if (medium_boundary) ImGui::Checkbox("Medium boundary", &diagnostics.medium_boundary);
        } else if (entity_type == SceneEntityKind::AreaEmitter)
            ImGui::Checkbox("Emitter surface", &diagnostics.area_emitter);
        else if (entity_type == SceneEntityKind::Camera) {
            ImGui::Checkbox("Frustum", &diagnostics.camera_frustum);
            ImGui::Checkbox("Focal plane", &diagnostics.camera_focal_plane);
            ImGui::Checkbox("Lens", &diagnostics.camera_lens);
        } else if (entity_type == SceneEntityKind::NeuralField) {
            const scene::NeuralField& field = *std::ranges::find(evaluated.resources.neural_fields, std::get<scene::NeuralFieldId>(entity.data), &scene::NeuralField::id);
            scene::NeuralFieldDiagnostics field_diagnostics = field.diagnostics;
            if (ImGui::Checkbox("Occupancy Grid", &field_diagnostics.occupancy_grid)) {
                this->context.document.update_neural_field_diagnostics(field.id, std::move(field_diagnostics));
            }
        } else if (entity_type == SceneEntityKind::Light)
            ImGui::Checkbox("Light guide", &diagnostics.light_guide);

        const bool instance_diagnostics    = entity_type == SceneEntityKind::Instance;
        const bool wireframe_diagnostics   = instance_diagnostics && (geometry || spheres) && diagnostics.wireframe;
        const bool point_diagnostics       = instance_diagnostics && (geometry || spheres) && diagnostics.vertices;
        const bool attribute_diagnostics   = instance_diagnostics && geometry && (diagnostics.normals || diagnostics.tangents);
        const bool emitter_diagnostics     = (instance_diagnostics && area_emitter || entity_type == SceneEntityKind::AreaEmitter) && diagnostics.area_emitter;
        const bool boundary_diagnostics    = instance_diagnostics && medium_boundary && diagnostics.medium_boundary;
        const bool line_diagnostics        = wireframe_diagnostics || attribute_diagnostics || emitter_diagnostics || boundary_diagnostics;
        const bool styled                  = line_diagnostics || point_diagnostics;
        if (styled) {
            ImGui::Spacing();
            constexpr const char* depth_modes[] = {"Tested", "X-Ray", "Overlay"};
            int depth_mode                      = static_cast<int>(std::to_underlying(diagnostics.depth_mode));
            if (ImGui::Combo("Depth", &depth_mode, depth_modes, 3)) diagnostics.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode);
            if (line_diagnostics) ImGui::DragFloat("Line width", &diagnostics.line_width, 0.1f, 0.25f, 8.0f, "%.2f px");
            if (point_diagnostics) ImGui::DragFloat("Point size", &diagnostics.point_size, 0.25f, 1.0f, 32.0f, "%.2f px");
            if (attribute_diagnostics) {
                ImGui::DragFloat("Vector scale", &diagnostics.vector_scale, 0.01f, 0.001f, 100.0f, "%.4g");
                constexpr std::uint32_t minimum_sampling = 1;
                constexpr std::uint32_t maximum_sampling = 1024;
                ImGui::DragScalar("Attribute sampling", ImGuiDataType_U32, &diagnostics.attribute_sampling, 1.0f, &minimum_sampling, &maximum_sampling, "%u");
            }
        }
        ImGui::EndDisabled();

        if (entity_type == SceneEntityKind::ParticleSet) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            this->draw_particle_diagnostics(*std::ranges::find(evaluated.resources.particle_sets, std::get<scene::ParticleSetId>(entity.data), &scene::ParticleSet::id));
        } else if (entity_type == SceneEntityKind::Volume) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            this->draw_volume_diagnostics(*std::ranges::find(evaluated.resources.volumes, std::get<scene::VolumeId>(entity.data), &scene::Volume::id));
        }
    }

    void EditorUi::draw_particle_diagnostics(const scene::ParticleSet& particles) {
        ImGui::PushID(&particles);
        ImGui::TextDisabled("PARTICLE VISUALIZATION / %s", particles.name.c_str());
        scene::ParticleVisualization visualization = particles.visualization;
        scene::ParticleDiagnostics diagnostics      = particles.diagnostics;
        bool visualization_changed{};
        bool diagnostics_changed{};

        const auto selected_field = visualization.field_id.empty() ? particles.fields.end() : std::ranges::find(particles.fields, visualization.field_id, &scene::ParticleField::id);
        const char* field_preview = selected_field == particles.fields.end() ? "Uniform" : selected_field->name.c_str();
        if (ImGui::BeginCombo("Field", field_preview)) {
            if (ImGui::Selectable("Uniform", selected_field == particles.fields.end()) && !visualization.field_id.empty()) {
                visualization.field_id.clear();
                visualization.mapping = scene::FieldMapping::Value;
                visualization_changed = true;
            }
            for (std::size_t index = 0; index != particles.fields.size(); ++index) {
                const scene::ParticleField& field = particles.fields[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(field.name.c_str(), selected_field != particles.fields.end() && field.id == selected_field->id) && field.id != visualization.field_id) {
                    visualization.field_id = field.id;
                    visualization.mapping  = scene::field_kind(field) == scene::FieldKind::Float3 ? scene::FieldMapping::Magnitude : scene::FieldMapping::Value;
                    visualization_changed  = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        constexpr const char* displays[] = {"Points", "Discs", "Spheres"};
        int display                      = static_cast<int>(std::to_underlying(visualization.display));
        if (ImGui::Combo("Display", &display, displays, 3)) visualization.display = static_cast<scene::ParticleDisplayMode>(display), visualization_changed = true;
        const auto active_field = visualization.field_id.empty() ? particles.fields.end() : std::ranges::find(particles.fields, visualization.field_id, &scene::ParticleField::id);
        const bool continuous_field = active_field != particles.fields.end() && scene::field_kind(*active_field) != scene::FieldKind::UInt32;
        if (active_field != particles.fields.end() && scene::field_kind(*active_field) == scene::FieldKind::Float3) {
            constexpr const char* mappings[] = {"Magnitude", "X", "Y", "Z"};
            int mapping                     = static_cast<int>(std::to_underlying(visualization.mapping)) - 1;
            if (ImGui::Combo("Mapping", &mapping, mappings, 4)) visualization.mapping = static_cast<scene::FieldMapping>(mapping + 1), visualization_changed = true;
        }
        if (continuous_field) {
            constexpr const char* color_maps[] = {"Viridis", "Turbo", "Cool-Warm", "Grayscale"};
            int color_map = static_cast<int>(std::to_underlying(visualization.color_map));
            if (ImGui::Combo("Color map", &color_map, color_maps, 4)) visualization.color_map = static_cast<scene::VisualizationColorMap>(color_map), visualization_changed = true;
            visualization_changed = ImGui::DragFloatRange2("Range", &visualization.minimum, &visualization.maximum, 0.01f, 0.0f, 0.0f, "%.5g", "%.5g") || visualization_changed;
        }
        visualization_changed = ImGui::ColorEdit4("Tint", &visualization.color.x, ImGuiColorEditFlags_Float) || visualization_changed;
        if (visualization.display == scene::ParticleDisplayMode::Points)
            visualization_changed = ImGui::DragFloat("Point size", &visualization.point_size, 0.25f, 1.0f, 32.0f, "%.2f px") || visualization_changed;
        else
            visualization_changed = ImGui::DragFloat("Radius scale", &visualization.radius_scale, 0.01f, 0.01f, 10.0f, "%.3g") || visualization_changed;
        constexpr const char* depth_modes[] = {"Tested", "X-Ray", "Overlay"};
        int depth_mode                      = static_cast<int>(std::to_underlying(visualization.depth_mode));
        if (ImGui::Combo("Depth", &depth_mode, depth_modes, 3)) visualization.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode), visualization_changed = true;

        ImGui::Spacing();
        ImGui::TextDisabled("VECTOR DIAGNOSTICS");
        const auto selected_vector = diagnostics.vector_field.empty() ? particles.fields.end() : std::ranges::find(particles.fields, diagnostics.vector_field, &scene::ParticleField::id);
        const char* vector_preview = selected_vector == particles.fields.end() ? "Off" : selected_vector->name.c_str();
        if (ImGui::BeginCombo("Vector field", vector_preview)) {
            if (ImGui::Selectable("Off", selected_vector == particles.fields.end()) && !diagnostics.vector_field.empty()) diagnostics.vector_field.clear(), diagnostics_changed = true;
            for (std::size_t index = 0; index != particles.fields.size(); ++index) {
                const scene::ParticleField& field = particles.fields[index];
                if (scene::field_kind(field) != scene::FieldKind::Float3) continue;
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(field.name.c_str(), selected_vector != particles.fields.end() && field.id == selected_vector->id) && field.id != diagnostics.vector_field) diagnostics.vector_field = field.id, diagnostics_changed = true;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!diagnostics.vector_field.empty()) {
            constexpr const char* color_maps[] = {"Viridis", "Turbo", "Cool-Warm", "Grayscale"};
            constexpr std::uint32_t minimum_sampling = 1u;
            constexpr std::uint32_t maximum_sampling = 1024u;
            int color_map = static_cast<int>(std::to_underlying(diagnostics.color_map));
            if (ImGui::Combo("Vector color map", &color_map, color_maps, 4)) diagnostics.color_map = static_cast<scene::VisualizationColorMap>(color_map), diagnostics_changed = true;
            diagnostics_changed = ImGui::DragFloatRange2("Vector range", &diagnostics.minimum, &diagnostics.maximum, 0.01f, 0.0f, 0.0f, "%.5g", "%.5g") || diagnostics_changed;
            diagnostics_changed = ImGui::DragFloat("Vector scale", &diagnostics.scale, 0.001f) || diagnostics_changed;
            diagnostics_changed = ImGui::DragFloat("Vector width", &diagnostics.width, 0.1f, 0.25f, 8.0f, "%.2f px") || diagnostics_changed;
            diagnostics_changed = ImGui::DragScalar("Vector sampling", ImGuiDataType_U32, &diagnostics.sampling, 1.0f, &minimum_sampling, &maximum_sampling, "%u") || diagnostics_changed;
        }

        if (visualization_changed) {
            this->context.document.update_particle_set_visualization(particles.id, std::move(visualization));
        }
        if (diagnostics_changed) {
            this->context.document.update_particle_set_diagnostics(particles.id, std::move(diagnostics));
        }
        ImGui::PopID();
    }

    void EditorUi::draw_volume_diagnostics(const scene::Volume& volume) {
        ImGui::PushID(&volume);
        ImGui::TextDisabled("VOLUME VISUALIZATION / %s", volume.name.c_str());
        const auto* grid = std::get_if<scene::GridVolume>(&volume.data);
        if (!grid || grid->fields.empty()) {
            ImGui::TextDisabled("No diagnostic fields");
            ImGui::PopID();
            return;
        }
        scene::VolumeDiagnostics diagnostics = volume.diagnostics;
        bool changed{};
        std::vector<scene::VolumeField>::const_iterator selected_field = std::ranges::find(grid->fields, diagnostics.field_id, &scene::VolumeField::id);
        if (selected_field == grid->fields.end()) selected_field = grid->fields.begin();
        if (ImGui::BeginCombo("Field", selected_field->name.c_str())) {
            for (std::size_t index = 0; index != grid->fields.size(); ++index) {
                const scene::VolumeField& field = grid->fields[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(field.name.c_str(), field.id == selected_field->id) && field.id != diagnostics.field_id) {
                    diagnostics.field_id = field.id;
                    diagnostics.mode     = scene::VolumeDiagnosticMode::Off;
                    const scene::FieldKind field_type = scene::field_kind(field);
                    diagnostics.mapping  = field_type == scene::FieldKind::Float3 || field_type == scene::FieldKind::MacFloat3 ? scene::FieldMapping::Magnitude : scene::FieldMapping::Value;
                    selected_field       = grid->fields.begin() + static_cast<std::ptrdiff_t>(index);
                    changed              = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        const scene::FieldKind selected_field_kind = scene::field_kind(*selected_field);
        const bool vector_field      = selected_field_kind == scene::FieldKind::Float3 || selected_field_kind == scene::FieldKind::MacFloat3;
        const bool categorical_field = selected_field_kind == scene::FieldKind::UInt32;
        constexpr std::array scalar_modes{
            scene::VolumeDiagnosticMode::Off,
            scene::VolumeDiagnosticMode::Slice,
            scene::VolumeDiagnosticMode::RayMarch,
            scene::VolumeDiagnosticMode::MaximumIntensityProjection,
            scene::VolumeDiagnosticMode::Isosurface,
        };
        constexpr std::array vector_modes{
            scene::VolumeDiagnosticMode::Off,
            scene::VolumeDiagnosticMode::Slice,
            scene::VolumeDiagnosticMode::Glyphs,
            scene::VolumeDiagnosticMode::Streamlines,
            scene::VolumeDiagnosticMode::Lic,
        };
        constexpr std::array categorical_modes{
            scene::VolumeDiagnosticMode::Off,
            scene::VolumeDiagnosticMode::Slice,
            scene::VolumeDiagnosticMode::Cells,
        };
        constexpr std::array scalar_mode_names{"Off", "Slice", "Ray March", "Maximum Intensity", "Isosurface"};
        constexpr std::array vector_mode_names{"Off", "Slice", "Glyphs", "Streamlines", "LIC"};
        constexpr std::array categorical_mode_names{"Off", "Slice", "Cells"};
        if (categorical_field) {
            std::size_t mode_index = std::ranges::find(categorical_modes, diagnostics.mode) - categorical_modes.begin();
            if (mode_index == categorical_modes.size()) mode_index = 0;
            if (ImGui::BeginCombo("Diagnostics", categorical_mode_names[mode_index])) {
                for (std::size_t index = 0; index != categorical_modes.size(); ++index)
                    if (ImGui::Selectable(categorical_mode_names[index], index == mode_index) && index != mode_index) diagnostics.mode = categorical_modes[index], changed = true;
                ImGui::EndCombo();
            }
        } else if (vector_field) {
            std::size_t mode_index = std::ranges::find(vector_modes, diagnostics.mode) - vector_modes.begin();
            if (mode_index == vector_modes.size()) mode_index = 0;
            if (ImGui::BeginCombo("Diagnostics", vector_mode_names[mode_index])) {
                for (std::size_t index = 0; index != vector_modes.size(); ++index)
                    if (ImGui::Selectable(vector_mode_names[index], index == mode_index) && index != mode_index) diagnostics.mode = vector_modes[index], changed = true;
                ImGui::EndCombo();
            }
        } else {
            std::size_t mode_index = std::ranges::find(scalar_modes, diagnostics.mode) - scalar_modes.begin();
            if (mode_index == scalar_modes.size()) mode_index = 0;
            if (ImGui::BeginCombo("Diagnostics", scalar_mode_names[mode_index])) {
                for (std::size_t index = 0; index != scalar_modes.size(); ++index)
                    if (ImGui::Selectable(scalar_mode_names[index], index == mode_index) && index != mode_index) diagnostics.mode = scalar_modes[index], changed = true;
                ImGui::EndCombo();
            }
        }
        if (diagnostics.field_id.empty()) diagnostics.field_id = selected_field->id, changed = true;
        if (diagnostics.mode != scene::VolumeDiagnosticMode::Off) {
            constexpr const char* mappings[] = {"Value", "Magnitude", "X", "Y", "Z", "Divergence", "Curl Magnitude", "Q Criterion"};
            constexpr const char* color_maps[] = {"Viridis", "Turbo", "Cool-Warm", "Grayscale"};
            constexpr const char* depth_modes[] = {"Tested", "X-Ray", "Overlay"};
            constexpr const char* axes[] = {"X", "Y", "Z"};
            constexpr std::uint32_t minimum_steps = 1u;
            constexpr std::uint32_t maximum_steps = 512u;
            constexpr std::uint32_t minimum_sampling = 1u;
            constexpr std::uint32_t maximum_sampling = 256u;
            int mapping = static_cast<int>(std::to_underlying(diagnostics.mapping));
            if (vector_field && ImGui::Combo("Mapping", &mapping, mappings, 8)) diagnostics.mapping = static_cast<scene::FieldMapping>(mapping), changed = true;
            if (!categorical_field) {
                int color_map = static_cast<int>(std::to_underlying(diagnostics.color_map));
                if (ImGui::Combo("Color map", &color_map, color_maps, 4)) diagnostics.color_map = static_cast<scene::VisualizationColorMap>(color_map), changed = true;
                changed = ImGui::DragFloatRange2("Range", &diagnostics.minimum, &diagnostics.maximum, 0.01f, 0.0f, 0.0f, "%.5g", "%.5g") || changed;
            } else {
                ImGui::TextDisabled("Categories");
                for (std::uint32_t category = 0; category != 32; ++category) {
                    if (category % 8 != 0) ImGui::SameLine();
                    ImGui::PushID(static_cast<int>(category));
                    bool enabled = (diagnostics.category_mask & (1u << category)) != 0;
                    if (ImGui::Checkbox(std::format("{}", category).c_str(), &enabled)) diagnostics.category_mask ^= 1u << category, changed = true;
                    ImGui::PopID();
                }
            }
            changed = ImGui::ColorEdit4("Tint", &diagnostics.color.x, ImGuiColorEditFlags_Float) || changed;
            if (diagnostics.mode == scene::VolumeDiagnosticMode::Slice || diagnostics.mode == scene::VolumeDiagnosticMode::Lic) {
                int axis = static_cast<int>(diagnostics.axis);
                if (ImGui::Combo("Axis", &axis, axes, 3)) diagnostics.axis = static_cast<std::uint32_t>(axis), changed = true;
                changed = ImGui::SliderFloat("Slice", &diagnostics.slice_position, 0.0f, 1.0f) || changed;
            }
            if (diagnostics.mode == scene::VolumeDiagnosticMode::RayMarch || diagnostics.mode == scene::VolumeDiagnosticMode::MaximumIntensityProjection || diagnostics.mode == scene::VolumeDiagnosticMode::Isosurface || diagnostics.mode == scene::VolumeDiagnosticMode::Slice || diagnostics.mode == scene::VolumeDiagnosticMode::Lic) changed = ImGui::DragFloat("Opacity", &diagnostics.opacity, 0.01f, 0.0f, 10.0f) || changed;
            if (diagnostics.mode == scene::VolumeDiagnosticMode::Isosurface) changed = ImGui::DragFloat("Threshold", &diagnostics.threshold, 0.01f) || changed;
            if (diagnostics.mode == scene::VolumeDiagnosticMode::Cells) changed = ImGui::DragFloat("Width", &diagnostics.width, 0.1f, 0.25f, 8.0f, "%.2f px") || changed;
            if (diagnostics.mode == scene::VolumeDiagnosticMode::Glyphs || diagnostics.mode == scene::VolumeDiagnosticMode::Streamlines) {
                changed = ImGui::DragFloat("Scale", &diagnostics.scale, 0.001f) || changed;
                changed = ImGui::DragFloat("Width", &diagnostics.width, 0.1f, 0.25f, 8.0f, "%.2f px") || changed;
            }
            if (diagnostics.mode == scene::VolumeDiagnosticMode::RayMarch || diagnostics.mode == scene::VolumeDiagnosticMode::MaximumIntensityProjection || diagnostics.mode == scene::VolumeDiagnosticMode::Isosurface || diagnostics.mode == scene::VolumeDiagnosticMode::Streamlines || diagnostics.mode == scene::VolumeDiagnosticMode::Lic) changed = ImGui::DragScalar("Steps", ImGuiDataType_U32, &diagnostics.steps, 1.0f, &minimum_steps, &maximum_steps, "%u") || changed;
            if (diagnostics.mode == scene::VolumeDiagnosticMode::Glyphs || diagnostics.mode == scene::VolumeDiagnosticMode::Streamlines) changed = ImGui::DragScalar("Seeds", ImGuiDataType_U32, &diagnostics.sampling, 1.0f, &minimum_sampling, &maximum_sampling, "%u") || changed;
            int depth_mode = static_cast<int>(std::to_underlying(diagnostics.depth_mode));
            if (ImGui::Combo("Depth", &depth_mode, depth_modes, 3)) diagnostics.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode), changed = true;
        }
        if (changed) {
            this->context.document.update_volume_diagnostics(volume.id, std::move(diagnostics));
        }
        ImGui::PopID();
    }

} // namespace spectra
