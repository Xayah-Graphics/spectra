module;

#include <imgui.h>

module spectra.editor.ui.simulation;

import std;

namespace spectra::editor {
    SimulationPanel::SimulationPanel(Document& document, simulation::Runtime& simulation) noexcept : document{document}, simulation{simulation} {}

    void SimulationPanel::reset() {
        this->selected_system = 0;
        this->parameter_drafts.clear();
        this->reset_pending    = false;
        this->recreate_pending = false;
    }
    bool SimulationPanel::synchronize() {
        bool changed{};
        if (this->observed_revision != this->document.authored.revision().number && !ImGui::IsAnyItemActive()) {
            this->observed_revision = this->document.authored.revision().number;
            this->parameter_drafts.clear();
            this->reset_pending    = false;
            this->recreate_pending = false;
            changed                = true;
        }
        const std::optional<scene::SimulationSetup>& setup = this->document.authored.simulation;
        if (!setup) {
            this->selected_system = 0;
            return changed;
        }
        if (this->selected_system < setup->systems.size() && setup->systems[this->selected_system].enabled) return changed;
        const auto first      = std::ranges::find(setup->systems, true, &scene::SimulationSystem::enabled);
        this->selected_system = first == setup->systems.end() ? 0 : static_cast<std::size_t>(first - setup->systems.begin());
        this->parameter_drafts.clear();
        this->reset_pending    = false;
        this->recreate_pending = false;
        return true;
    }
    void SimulationPanel::draw() {
        const scene::SimulationSetup* setup = this->document.authored.simulation ? &*this->document.authored.simulation : nullptr;
        std::vector<std::size_t> active_systems{};
        if (setup)
            for (std::size_t index = 0; index < setup->systems.size(); ++index) {
                if (!setup->systems[index].enabled) continue;
                active_systems.emplace_back(index);
            }
        if (active_systems.empty()) {
            ImGui::TextDisabled("No enabled simulation systems");
            return;
        }
        if (std::ranges::find(active_systems, this->selected_system) == active_systems.end()) this->selected_system = active_systems.front();

        const scene::SimulationSetup& simulation = *setup;
        ImGui::TextDisabled("SIMULATION SYSTEM");

        if (active_systems.size() > 1) {
            if (ImGui::BeginCombo("##SimulationSystem", simulation.systems[this->selected_system].name.c_str())) {
                for (const std::size_t index : active_systems)
                    if (ImGui::Selectable(simulation.systems[index].name.c_str(), index == this->selected_system)) {
                        this->selected_system = index;
                        this->parameter_drafts.clear();
                        this->reset_pending    = false;
                        this->recreate_pending = false;
                    }
                ImGui::EndCombo();
            }
        } else ImGui::TextUnformatted(simulation.systems[this->selected_system].name.c_str());

        const scene::SimulationSystem& scene_system    = simulation.systems[this->selected_system];
        const simulation::ProviderDescriptor& provider = this->simulation.provider_descriptor(scene_system.provider_id);
        if (provider.parameters.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("This system has no configurable parameters");
        }
        if (!provider.parameters.empty() && this->parameter_drafts.empty()) {
            this->parameter_drafts.reserve(provider.parameters.size());
            for (const simulation::ParameterDescriptor& descriptor : provider.parameters) {
                const auto configured = std::ranges::find(scene_system.parameters, descriptor.id, &scene::SimulationParameterSetting::parameter_id);
                this->parameter_drafts.emplace_back(descriptor.id, configured == scene_system.parameters.end() ? descriptor.value : configured->value);
            }
        }
        const auto parameter_values = [this, &scene_system, &provider](const bool include_reset_drafts) {
            std::vector<scene::SimulationParameterSetting> values{};
            values.reserve(provider.parameters.size());
            for (std::size_t index = 0; index < provider.parameters.size(); ++index) {
                const simulation::ParameterDescriptor& descriptor = provider.parameters[index];
                const auto stored                                 = std::ranges::find(scene_system.parameters, descriptor.id, &scene::SimulationParameterSetting::parameter_id);
                scene::SimulationParameterValue value             = stored == scene_system.parameters.end() ? descriptor.value : stored->value;
                if (include_reset_drafts || descriptor.application_mode == simulation::ParameterApplication::Live) value = this->parameter_drafts[index].value;
                values.emplace_back(descriptor.id, value);
            }
            return values;
        };

        std::string parameter_section{};
        for (std::size_t parameter_index = 0; parameter_index < provider.parameters.size(); ++parameter_index) {
            const simulation::ParameterDescriptor& parameter = provider.parameters[parameter_index];
            scene::SimulationParameterValue& value           = this->parameter_drafts[parameter_index].value;
            if (parameter.section_id != parameter_section) {
                parameter_section = parameter.section_id;
                ImGui::Spacing();
                ImGui::TextDisabled("PARAMETERS / %s", parameter_section.c_str());
            }
            ImGui::PushID(static_cast<int>(parameter_index));
            ImGui::Text("%s", parameter.name.c_str());
            if (!parameter.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", parameter.unit.c_str());
            }
            if (!parameter.description.empty()) ImGui::TextDisabled("%s", parameter.description.c_str());

            bool changed{};
            if (parameter.value.kind == scene::SimulationParameterKind::Boolean) {
                bool selected = value.integer != 0;
                changed       = ImGui::Checkbox("##Value", &selected);
                if (changed) value.integer = selected ? 1 : 0;
            } else if (parameter.value.kind == scene::SimulationParameterKind::Integer) changed = ImGui::DragScalar("##Value", ImGuiDataType_S64, &value.integer, static_cast<float>(parameter.step.integer), &parameter.minimum.integer, &parameter.maximum.integer, "%lld");
            else if (parameter.value.kind == scene::SimulationParameterKind::Float) changed = ImGui::DragScalar("##Value", ImGuiDataType_Double, value.floating.data(), static_cast<float>(parameter.step.floating[0]), parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.6g");
            else if (parameter.value.kind == scene::SimulationParameterKind::Float3) changed = ImGui::DragScalarN("##Value", ImGuiDataType_Double, value.floating.data(), 3, static_cast<float>(parameter.step.floating[0]), parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.5g");
            else {
                const char* preview = value.integer >= 0 && static_cast<std::size_t>(value.integer) < parameter.enumerators.size() ? parameter.enumerators[value.integer].c_str() : "";
                if (ImGui::BeginCombo("##Value", preview)) {
                    for (std::size_t option = 0; option < parameter.enumerators.size(); ++option)
                        if (ImGui::Selectable(parameter.enumerators[option].c_str(), value.integer == static_cast<std::int64_t>(option))) {
                            value.integer = static_cast<std::int64_t>(option);
                            changed       = true;
                        }
                    ImGui::EndCombo();
                }
            }

            if (parameter.application_mode != simulation::ParameterApplication::Live) {
                ImGui::SameLine();
                ImGui::TextDisabled(parameter.application_mode == simulation::ParameterApplication::Reset ? "reset" : "recreate");
                if (changed) {
                    this->reset_pending = true;
                    if (parameter.application_mode == simulation::ParameterApplication::Recreate) this->recreate_pending = true;
                }
            } else if ((changed && !ImGui::IsItemActive()) || ImGui::IsItemDeactivatedAfterEdit()) {
                std::vector<scene::SimulationParameterSetting> parameters = parameter_values(false);
                this->apply_parameters(std::move(parameters), false);
                ImGui::PopID();
                return;
            }
            ImGui::PopID();
        }

        if (this->reset_pending)
            if (ImGui::Button(this->recreate_pending ? "Apply & Recreate" : "Apply & Reset", ImVec2{144.0f, 27.0f})) {
                std::vector<scene::SimulationParameterSetting> parameters = parameter_values(true);
                this->apply_parameters(std::move(parameters), true);
            }

        if (scene_system.visualizations.empty()) return;
        ImGui::Spacing();
        ImGui::TextDisabled("VISUALIZATIONS");
        constexpr std::array<const char*, 3> depth_names{"Depth Tested", "X-Ray", "Overlay"};
        constexpr std::array<const char*, 2> domain_names{"Scene Linear", "Display Referred"};
        constexpr std::array<const char*, 3> color_source_names{"Element", "Uniform", "Scalar"};
        constexpr std::array<const char*, 4> color_map_names{"Viridis", "Turbo", "Cool-Warm", "Grayscale"};
        for (std::size_t visualization_index = 0u; visualization_index != scene_system.visualizations.size(); ++visualization_index) {
            scene::SimulationVisualization visualization = scene_system.visualizations[visualization_index];
            ImGui::PushID(static_cast<int>(visualization_index));
            ImGui::Separator();
            bool changed = ImGui::Checkbox(visualization.name.c_str(), &visualization.visible);
            ImGui::TextDisabled("%s", visualization.output_id.c_str());
            int depth = std::to_underlying(visualization.depth_mode);
            if (ImGui::Combo("Depth", &depth, depth_names.data(), static_cast<int>(depth_names.size()))) {
                visualization.depth_mode = static_cast<scene::VisualizationDepthMode>(depth);
                changed                  = true;
            }
            if (!std::holds_alternative<scene::NeuralFieldVisualization>(visualization.data)) {
                int domain = std::to_underlying(visualization.composition_domain);
                if (ImGui::Combo("Domain", &domain, domain_names.data(), static_cast<int>(domain_names.size()))) {
                    visualization.composition_domain = static_cast<scene::VisualizationCompositionDomain>(domain);
                    changed                          = true;
                }
            }
            changed                        = ImGui::ColorEdit4("Color", &visualization.color.x, ImGuiColorEditFlags_Float) || changed;
            const auto draw_field_controls = [&](auto& style) {
                changed    = ImGui::DragFloatRange2("Scalar Range", &style.scalar_minimum, &style.scalar_maximum, 0.01f, 0.0f, 0.0f, "%.4g", "%.4g") || changed;
                int source = std::to_underlying(style.color_source);
                if (ImGui::Combo("Color Source", &source, color_source_names.data(), static_cast<int>(color_source_names.size()))) style.color_source = static_cast<scene::VisualizationColorSource>(source), changed = true;
                int map = std::to_underlying(style.color_map);
                if (ImGui::Combo("Color Map", &map, color_map_names.data(), static_cast<int>(color_map_names.size()))) style.color_map = static_cast<scene::VisualizationColorMap>(map), changed = true;
            };
            std::visit(
                [&](auto& style) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::PointVisualization>) {
                        changed = ImGui::DragFloat("Size", &style.size, 0.1f, 0.1f, 64.0f, "%.2f") || changed;
                        draw_field_controls(style);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::SegmentVisualization>) {
                        changed = ImGui::DragFloat("Width", &style.width, 0.1f, 0.1f, 32.0f, "%.2f") || changed;
                        draw_field_controls(style);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::VectorVisualization>) {
                        changed = ImGui::DragFloat("Width", &style.width, 0.1f, 0.1f, 32.0f, "%.2f") || changed;
                        changed = ImGui::DragFloat("Scale", &style.scale, 0.01f, 0.0f, 100.0f, "%.4g") || changed;
                        draw_field_controls(style);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::ImageVisualization>) changed = ImGui::DragFloat4("Screen Rect", &style.screen_rect.x, 0.005f, 0.0f, 1.0f, "%.3f") || changed;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::SurfaceVisualization>) draw_field_controls(style);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(style)>, scene::DerivedMeshVisualization>) {
                        changed = ImGui::DragFloat("Width", &style.width, 0.1f, 0.1f, 32.0f, "%.2f") || changed;
                        if (style.mode == scene::DerivedMeshVisualizationMode::VertexNormals || style.mode == scene::DerivedMeshVisualizationMode::FaceNormals) changed = ImGui::DragFloat("Scale", &style.scale, 0.005f, 0.0f, 100.0f, "%.4g") || changed;
                    }
                },
                visualization.data);
            ImGui::PopID();
            if (changed) {
                this->apply_visualization(visualization_index, std::move(visualization));
                return;
            }
        }
    }

    void SimulationPanel::apply_parameters(std::vector<scene::SimulationParameterSetting> parameters, const bool reset_simulation) {
        try {
            const bool recreated = this->simulation.apply_parameter_changes(this->selected_system, parameters, reset_simulation);
            this->document.update_simulation_parameters(this->selected_system, std::move(parameters));
            this->rebuild_rendering  = this->rebuild_rendering || recreated;
            this->notification       = recreated ? "Parameters applied and provider recreated" : reset_simulation ? "Parameters applied and simulation reset" : "Parameter applied";
            this->notification_error = false;
            this->observed_revision  = this->document.authored.revision().number;
            if (reset_simulation) {
                this->parameter_drafts.clear();
                this->reset_pending    = false;
                this->recreate_pending = false;
            }
        } catch (const std::exception& error) {
            this->notification       = error.what();
            this->notification_error = true;
        }
    }

    void SimulationPanel::apply_visualization(const std::size_t visualization_index, scene::SimulationVisualization visualization) {
        try {
            this->simulation.update_visualization(this->selected_system, visualization_index, visualization);
            this->document.update_simulation_visualization(this->selected_system, visualization_index, std::move(visualization));
            this->notification       = "Visualization updated";
            this->notification_error = false;
            this->observed_revision  = this->document.authored.revision().number;
        } catch (const std::exception& error) {
            this->notification       = error.what();
            this->notification_error = true;
        }
    }

} // namespace spectra::editor
