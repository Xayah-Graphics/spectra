#include "session.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <material_symbols/IconsMaterialSymbols.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string spectra_parameter_count_text(const std::vector<spectra::scene::SceneDescriptionParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const spectra::scene::SceneDescriptionRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    [[nodiscard]] std::string resolution_text(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) return "Pending";
        return std::format("{} x {}", resolution[0], resolution[1]);
    }

    [[nodiscard]] std::string positive_int_text(const int value) {
        if (value <= 0) return "Pending";
        return std::format("{}", value);
    }

    [[nodiscard]] const char* scene_texture_value_type_label(const spectra::scene::SceneDescriptionTextureValueType value_type) {
        switch (value_type) {
            case spectra::scene::SceneDescriptionTextureValueType::Unknown: return "Unknown";
            case spectra::scene::SceneDescriptionTextureValueType::Float: return "Float";
            case spectra::scene::SceneDescriptionTextureValueType::Spectrum: return "Spectrum";
        }
        throw std::runtime_error("Unknown Spectra scene texture value type");
    }

    void draw_scene_render_setting_row(const char* label, const spectra::scene::SceneDescriptionRenderSetting& setting) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const std::string setting_text = scene_render_setting_text(setting);
        if (setting.present) ImGui::TextWrapped("%s", setting_text.c_str());
        else ImGui::TextDisabled("%s", setting_text.c_str());
        ImGui::TableSetColumnIndex(2);
        if (setting.present) ImGui::TextWrapped("%s", scene_file_location_text(setting.location).c_str());
        else ImGui::TextDisabled("None");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(spectra_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

} // namespace

namespace xayah::pathtracer {
    void InteractiveSession::draw_viewport_window() {
        const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
        const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
        if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DisplayFramebufferScale.x) || !std::isfinite(io.DisplayFramebufferScale.y) || !(io.DisplayFramebufferScale.x > 0.0f) || !(io.DisplayFramebufferScale.y > 0.0f)) throw std::runtime_error("ImGui framebuffer scale must be finite and positive");
        const std::array<int, 2> viewport_framebuffer_size{
            static_cast<int>(std::round(viewport_size.x * io.DisplayFramebufferScale.x)),
            static_cast<int>(std::round(viewport_size.y * io.DisplayFramebufferScale.y)),
        };
        if (viewport_framebuffer_size[0] <= 0 || viewport_framebuffer_size[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        this->state->ui.viewport_known    = true;
        this->state->ui.viewport_position = {viewport_position.x, viewport_position.y};
        this->state->ui.viewport_size     = {viewport_size.x, viewport_size.y};
        this->state->ui.viewport_framebuffer_size = viewport_framebuffer_size;
        this->state->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
        this->state->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
        this->observe_viewport_render_resolution(viewport_framebuffer_size);
        if (this->pathtracer_ready()) {
            if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra pathtracer viewport descriptor requested without an active Spectra pathtracer session");
            const VkDescriptorSet descriptor = this->state->gpu_pathtracer->active_descriptor();
            if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Spectra pathtracer viewport descriptor is null");
            const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
            ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
            ImGui::SetCursorScreenPos(viewport_position);
        } else if (this->state->spectra_scene != nullptr) {
            const char* pending_label = this->state->render_resolution_sync.rebuilding ? "Rebuilding pathtracer" : "Waiting for viewport resolution";
            const ImVec2 text_size = ImGui::CalcTextSize(pending_label);
            ImGui::SetCursorScreenPos(ImVec2{viewport_position.x + std::max(0.0f, (viewport_size.x - text_size.x) * 0.5f), viewport_position.y + std::max(0.0f, (viewport_size.y - text_size.y) * 0.5f)});
            ImGui::TextDisabled("%s", pending_label);
            ImGui::SetCursorScreenPos(viewport_position);
        }
        ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    }

    void InteractiveSession::draw_camera_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const PathtracerStatus pathtracer_status = this->pathtracer_status();

            draw_statistics_row("Path Tracer", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Camera Speed");
            ImGui::TableSetColumnIndex(1);
            float speed = this->state->camera.speed;
            const float drag_speed = std::max(std::abs(speed) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraSpeed", &speed, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_speed(speed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement speed in world units per second. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->state->camera.initialized || !this->pathtracer_ready());
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }

    void InteractiveSession::draw_scene_browser_window() {
        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Asset Info");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (!ImGui::BeginTabBar("SpectraSceneBrowserTabs")) return;

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTabItem("Render Settings")) {
            if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
                ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                draw_scene_render_setting_row("Pixel Filter", this->state->spectra_scene->description.pixelFilter);
                draw_scene_render_setting_row("Film", this->state->spectra_scene->description.film);
                draw_scene_render_setting_row("Sampler", this->state->spectra_scene->description.sampler);
                draw_scene_render_setting_row("Accelerator", this->state->spectra_scene->description.accelerator);
                draw_scene_render_setting_row("Integrator", this->state->spectra_scene->description.integrator);
                draw_scene_render_setting_row("Camera", this->state->spectra_scene->description.camera);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shapes")) {
            if (this->state->spectra_scene->description.shapes.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer shapes recorded");
            } else if (ImGui::BeginTable("SpectraSceneShapes", 7, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Area Light", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionShape& shape : this->state->spectra_scene->description.shapes) {
                    const std::string material_text = !shape.materialName.empty() ? shape.materialName : shape.materialIndex >= 0 ? std::format("#{}", shape.materialIndex) : "None";
                    const std::string media_text    = shape.insideMedium.empty() && shape.outsideMedium.empty() ? "None" : std::format("{} / {}", optional_scene_text(shape.insideMedium), optional_scene_text(shape.outsideMedium));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", shape.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", material_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", media_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.objectDefinitionName).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.areaLightType).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextWrapped("%s", scene_file_location_text(shape.location).c_str());
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(spectra_parameter_count_text(shape.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials")) {
            if (this->state->spectra_scene->description.materials.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer materials recorded");
            } else if (ImGui::BeginTable("SpectraSceneMaterials", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMaterial& material : this->state->spectra_scene->description.materials) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(material.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(material.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(material.named ? "Named" : "Inline");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(material.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(material.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures")) {
            if (this->state->spectra_scene->description.textures.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer textures recorded");
            } else if (ImGui::BeginTable("SpectraSceneTextures", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Implementation", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionTexture& texture : this->state->spectra_scene->description.textures) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(scene_texture_value_type_label(texture.valueType));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.implementation).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(texture.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(texture.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Media")) {
            if (this->state->spectra_scene->description.mediums.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer media recorded");
            } else if (ImGui::BeginTable("SpectraSceneMedia", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Medium Interfaces");
            if (this->state->spectra_scene->description.mediumBindings.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer medium interfaces recorded");
            } else if (ImGui::BeginTable("SpectraSceneMediumInterfaces", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights")) {
            if (this->state->spectra_scene->description.lights.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer lights recorded");
            } else if (ImGui::BeginTable("SpectraSceneLights", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", light.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(light.area ? "Area" : "Light");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects")) {
            ImGui::SeparatorText("Definitions");
            if (this->state->spectra_scene->description.objectDefinitions.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer object definitions recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectDefinitions", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shapes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectDefinition& object_definition : this->state->spectra_scene->description.objectDefinitions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_definition.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu", object_definition.shapeIndices.size());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_definition.location).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Instances");
            if (this->state->spectra_scene->description.objectInstances.empty()) {
                ImGui::TextDisabled("No Spectra pathtracer object instances recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectInstances", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Animated", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectInstance& object_instance : this->state->spectra_scene->description.objectInstances) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_instance.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(object_instance.animatedTransform ? "Yes" : "No");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_instance.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    void InteractiveSession::draw_settings_window() {
        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra pathtracer session");
            return;
        }

        if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Spectra Pathtracer Sampler SPP");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(positive_int_text(this->state->spectra_scene->sampler_sample_count).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Current Sample");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d / %d", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Max Iterations");
            ImGui::TableSetColumnIndex(1);
            const int previous_target_sample_count = this->state->gpu_pathtracer->target_sample_count();
            int target_sample_count                = previous_target_sample_count;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->state->spectra_scene->sampler_sample_count)) {
                this->state->gpu_pathtracer->set_target_sample_count(target_sample_count);
                if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Accumulation");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset Accumulation")) this->request_pathtracer_accumulation_reset();

            ImGui::EndTable();
        }
    }

    void InteractiveSession::draw_inspector_window() {
        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            return;
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        const std::string viewport_resolution = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraInspectorPathTracerState", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraInspectorScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Viewport", viewport_resolution);
            draw_statistics_row("Swapchain", std::format("{} x {}", this->state->host.swapchain_extent.width, this->state->host.swapchain_extent.height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("SpectraInspectorResources", 2, table_flags)) {
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (this->state->gpu_pathtracer != nullptr) {
            ImGui::SeparatorText("Path Tracer");
            if (ImGui::BeginTable("SpectraInspectorPathTracer", 2, table_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Sample", std::format("{} / {}", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count()));
                draw_statistics_row("Completion", std::format("{:.1f}%", this->state->gpu_pathtracer->completion_ratio() * 100.0f));
                draw_statistics_row("Exposure", std::format("{:.3f}", this->state->gpu_pathtracer->current_exposure()));
                ImGui::EndTable();
            }
        }
    }

    void InteractiveSession::draw_environment_window() {
        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra pathtracer scene");
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->state->spectra_scene->description.lights.empty()) {
            ImGui::TextDisabled("No Spectra pathtracer lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->state->spectra_scene->description.mediums.empty()) {
            ImGui::TextDisabled("No Spectra pathtracer media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->state->spectra_scene->description.mediumBindings.empty()) {
            ImGui::TextDisabled("No Spectra pathtracer medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
            }
            ImGui::EndTable();
        }
    }

    void InteractiveSession::draw_tonemapper_window() {
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra pathtracer session");
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->state->gpu_pathtracer->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->state->gpu_pathtracer->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
    }

    void InteractiveSession::draw_statistics_window() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";
        const PathtracerStatus pathtracer_status = this->pathtracer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Path Tracer State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            draw_statistics_row("Scene", this->state->spectra_scene == nullptr ? "No Scene" : scene_title_text(*this->state->spectra_scene));
            draw_statistics_row("Frame ID", std::format("{}", this->state->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->state->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->state->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->state->host.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->state->host.swapchain_extent.width, this->state->host.swapchain_extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->state->statistics.last_frame_milliseconds));
            if (this->state->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->state->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->state->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra pathtracer scene");
            return;
        }

        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra pathtracer session");
            return;
        }

        const std::array<int, 2> film_resolution = this->state->spectra_scene->film_resolution;
        const int current_sample                 = this->state->gpu_pathtracer->current_sample();
        const int target_sample                  = this->state->gpu_pathtracer->target_sample_count();
        const float completion_ratio             = this->state->gpu_pathtracer->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", sampling_state);
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", resolution_text(film_resolution));
            if (this->state->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->state->statistics.throughput_mspp.average(), this->state->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->state->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->state->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->state->statistics.last_frame_rendered_sample ? "Rendered sample" : "No Spectra pathtracer sample");
            ImGui::EndTable();
        }
    }

} // namespace xayah::pathtracer
