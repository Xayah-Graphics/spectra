module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <driver_types.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>
#include <vulkan/vulkan_raii.hpp>
#include "spectra_pbrt_fwd.h"
module spectra;
import std;
#include "spectra_internal.h"

namespace {
    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string pbrt_parameter_count_text(const std::vector<xayah::SpectraPbrtParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const xayah::SpectraSceneRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    void draw_scene_render_setting_row(const char* label, const xayah::SpectraSceneRenderSetting& setting) {
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
        ImGui::TextUnformatted(pbrt_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] const char* scene_unsupported_kind_label(const xayah::SpectraSceneUnsupportedFeatureKind kind) {
        switch (kind) {
            case xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform:
                return "Animated Transform";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium:
                return "Participating Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::VdbMedium:
                return "VDB Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::ProceduralTexture:
                return "Procedural Texture";
            case xayah::SpectraSceneUnsupportedFeatureKind::AreaLightInObjectDefinition:
                return "Area Light Instance Policy";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParserAttribute:
                return "Parser Attribute";
        }
        throw std::runtime_error("Unknown Spectra scene unsupported feature kind");
    }

    [[nodiscard]] const char* raster_diagnostic_kind_label(const xayah::SpectraRasterDiagnosticKind kind) {
        switch (kind) {
            case xayah::SpectraRasterDiagnosticKind::UnsupportedShape: return "Unsupported Shape";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial: return "Unsupported Material";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedTexture: return "Unsupported Texture";
            case xayah::SpectraRasterDiagnosticKind::MissingMaterial: return "Missing Material";
            case xayah::SpectraRasterDiagnosticKind::InvalidMesh: return "Invalid Mesh";
            case xayah::SpectraRasterDiagnosticKind::MissingPlyFile: return "Missing PLY File";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform: return "Unsupported Animated Transform";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedObjectInstance: return "Unsupported Object Instance";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAreaLight: return "Unsupported Area Light";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMediumBinding: return "Unsupported Medium Binding";
        }
        throw std::runtime_error("Unknown Spectra raster diagnostic kind");
    }

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

    [[nodiscard]] std::string scene_unsupported_source_text(const xayah::SpectraSceneUnsupportedFeature& feature) {
        if (feature.source_name.empty()) return feature.source_type;
        return std::format("{} {}", feature.source_type, feature.source_name);
    }

    [[nodiscard]] std::string raster_diagnostic_source_text(const xayah::SpectraRasterDiagnostic& diagnostic) {
        if (diagnostic.source_name.empty()) return diagnostic.source_type;
        return std::format("{} {}", diagnostic.source_type, diagnostic.source_name);
    }

} // namespace

namespace xayah {
    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) this->ui.camera_visible = !this->ui.camera_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) this->ui.scene_diagnostics_visible = !this->ui.scene_diagnostics_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) this->ui.settings_visible = !this->ui.settings_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) this->ui.statistics_visible = !this->ui.statistics_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) this->ui.environment_visible = !this->ui.environment_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) this->ui.tonemapper_visible = !this->ui.tonemapper_visible;
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(ICON_MS_PHOTO_CAMERA " Camera", "F1", &this->ui.camera_visible);
            ImGui::MenuItem(ICON_MS_DIAGNOSIS " Scene Diagnostics", "F2", &this->ui.scene_diagnostics_visible);
            ImGui::MenuItem(ICON_MS_SETTINGS " Settings", "F3", &this->ui.settings_visible);
            ImGui::MenuItem(ICON_MS_ANALYTICS " Statistics", "F4", &this->ui.statistics_visible);
            ImGui::MenuItem(ICON_MS_PUBLIC " Environment", "F5", &this->ui.environment_visible);
            ImGui::MenuItem(ICON_MS_TONALITY " Tonemapper", "F6", &this->ui.tonemapper_visible);
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }


    void Spectra::draw_menu_toolbar() {
        struct ToggleButton {
            const char* icon;
            const char* shortcut;
            bool* visible;
            const char* tooltip;
        };

        const std::array<ToggleButton, 6> toggles{{
            {ICON_MS_PHOTO_CAMERA, "F1", &this->ui.camera_visible, "Camera"},
            {ICON_MS_DIAGNOSIS, "F2", &this->ui.scene_diagnostics_visible, "Scene Diagnostics"},
            {ICON_MS_SETTINGS, "F3", &this->ui.settings_visible, "Settings"},
            {ICON_MS_ANALYTICS, "F4", &this->ui.statistics_visible, "Statistics"},
            {ICON_MS_PUBLIC, "F5", &this->ui.environment_visible, "Environment"},
            {ICON_MS_TONALITY, "F6", &this->ui.tonemapper_visible, "Tonemapper"},
        }};

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toggles.size()) * button_size + static_cast<float>(toggles.size() + 1) * 2.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 180.0f) return;

        ImGui::SameLine(window_width * 0.5f - total_width * 0.5f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (const ToggleButton& toggle : toggles) {
            ImGui::PushStyleColor(ImGuiCol_Button, *toggle.visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
            if (ImGui::Button(toggle.icon, ImVec2{button_size, button_size})) *toggle.visible = !*toggle.visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && toggle.shortcut != nullptr) ImGui::SetTooltip("Toggle %s Window (%s)", toggle.tooltip, toggle.shortcut);
            if (ImGui::IsItemHovered() && toggle.shortcut == nullptr) ImGui::SetTooltip("Toggle %s Window", toggle.tooltip);
            ImGui::SameLine(0.0f, 2.0f);
        }
    }


    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (main_viewport->WorkSize.x <= 640.0f || main_viewport->WorkSize.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        const ImVec4 dockspace_window_background     = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{dockspace_window_background.x, dockspace_window_background.y, dockspace_window_background.z, 0.0f});
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, dockspace_flags);
        ImGui::PopStyleColor();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->ui.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.23f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.22f, nullptr, &center_id);
        if (right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGui::DockBuilderDockWindow("Viewport", center_id);
        ImGui::DockBuilderDockWindow("Camera", right_id);
        ImGui::DockBuilderDockWindow("Scene Diagnostics", right_id);
        ImGui::DockBuilderDockWindow("Settings", right_id);
        ImGui::DockBuilderDockWindow("Environment", right_id);
        ImGui::DockBuilderDockWindow("Tonemapper", right_id);
        ImGui::DockBuilderDockWindow("Statistics", bottom_id);
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->ui.dock_layout_initialized = true;
    }


    void Spectra::draw_viewport_window() {
        constexpr ImGuiWindowFlags viewport_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        if (ImGui::Begin("Viewport", nullptr, viewport_window_flags)) {
            const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
            const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
            if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
            this->ui.viewport_known    = true;
            this->ui.viewport_position = {viewport_position.x, viewport_position.y};
            this->ui.viewport_size     = {viewport_size.x, viewport_size.y};
            this->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
            this->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
            if (this->spectra_scene != nullptr) {
                const VkDescriptorSet descriptor = this->active_viewport_descriptor();
                if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Active renderer viewport descriptor is null");
                const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
                ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
                ImGui::SetCursorScreenPos(viewport_position);
            }
            ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        } else {
            this->ui.viewport_known   = false;
            this->ui.viewport_hovered = false;
            this->ui.viewport_focused = false;
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }


    void Spectra::draw_camera_window() {
        if (!this->ui.camera_visible) return;
        if (!ImGui::Begin("Camera", &this->ui.camera_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const ActiveRendererStatus renderer_status = this->active_renderer_status();

            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("PBRT Dirty", renderer_status.pathtracer_accumulation_dirty ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Move Scale");
            ImGui::TableSetColumnIndex(1);
            float move_scale = this->camera.move_scale;
            const float drag_speed = std::max(std::abs(move_scale) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraMoveScale", &move_scale, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_move_scale(move_scale);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement distance per key step. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->camera.initialized || this->spectra_scene == nullptr);
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_scene_diagnostics_window() {
        if (!this->ui.scene_diagnostics_visible) return;
        if (!ImGui::Begin("Scene Diagnostics", &this->ui.scene_diagnostics_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", this->spectra_scene->scene_label);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", this->spectra_scene->scene_path_text.c_str());

            draw_statistics_row("Active Renderer", this->active_renderer_label());
            draw_statistics_row("Film Resolution", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
            draw_statistics_row("Sampler SPP", std::format("{}", this->spectra_scene->sampler_sample_count));
            draw_statistics_row("Directives", std::format("{}", this->spectra_scene->pbrt_directives.size()));
            draw_statistics_row("Shapes", std::format("{}", this->spectra_scene->shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->spectra_scene->materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->spectra_scene->textures.size()));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->spectra_scene->object_definitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->spectra_scene->object_instances.size()));
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Render Settings");
        if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            draw_scene_render_setting_row("Pixel Filter", this->spectra_scene->pixel_filter);
            draw_scene_render_setting_row("Film", this->spectra_scene->film);
            draw_scene_render_setting_row("Sampler", this->spectra_scene->sampler);
            draw_scene_render_setting_row("Accelerator", this->spectra_scene->accelerator);
            draw_scene_render_setting_row("Integrator", this->spectra_scene->integrator);
            draw_scene_render_setting_row("Camera", this->spectra_scene->camera);
            ImGui::EndTable();
        }

        if (this->raster_scene != nullptr) {
            ImGui::SeparatorText("Raster Scene");
            if (ImGui::BeginTable("SpectraRasterSceneSummary", 2, summary_table_flags)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Vertices", std::format("{}", this->raster_scene->vertices.size()));
                draw_statistics_row("Indices", std::format("{}", this->raster_scene->indices.size()));
                draw_statistics_row("Triangles", std::format("{}", this->raster_scene->indices.size() / 3u));
                draw_statistics_row("Geometries", std::format("{}", this->raster_scene->geometries.size()));
                draw_statistics_row("Draws", std::format("{}", this->raster_scene->draws.size()));
                draw_statistics_row("Materials", std::format("{}", this->raster_scene->materials.size()));
                draw_statistics_row("Diagnostics", std::format("{}", this->raster_scene->diagnostics.size()));
                ImGui::EndTable();
            }
        }

        constexpr ImGuiTableFlags diagnostics_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("PBRT Diagnostics");
        if (this->spectra_scene->unsupported_features.empty()) {
            ImGui::TextDisabled("No unsupported PBRT features recorded");
        } else if (ImGui::BeginTable("SpectraSceneDiagnostics", 4, diagnostics_table_flags)) {
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraSceneUnsupportedFeature& feature : this->spectra_scene->unsupported_features) {
                const std::string source_text   = scene_unsupported_source_text(feature);
                const std::string location_text = scene_file_location_text(feature.location);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(scene_unsupported_kind_label(feature.kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", source_text.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", location_text.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", feature.message.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Raster Diagnostics");
        if (this->raster_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra raster scene");
        } else if (this->raster_scene->diagnostics.empty()) {
            ImGui::TextDisabled("No raster diagnostics recorded");
        } else if (ImGui::BeginTable("SpectraRasterSceneDiagnostics", 4, diagnostics_table_flags)) {
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraRasterDiagnostic& diagnostic : this->raster_scene->diagnostics) {
                const std::string source_text   = raster_diagnostic_source_text(diagnostic);
                const std::string location_text = scene_file_location_text(diagnostic.location);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(raster_diagnostic_kind_label(diagnostic.kind));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", source_text.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", location_text.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }


    void Spectra::draw_settings_window() {
        if (!this->ui.settings_visible) return;
        if (!ImGui::Begin("Settings", &this->ui.settings_visible)) {
            ImGui::End();
            return;
        }
        ActiveRendererStatus renderer_status = this->active_renderer_status();
        if (ImGui::BeginTable("SpectraRendererSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Active Renderer");
            ImGui::TableSetColumnIndex(1);
            const char* renderer_items[]{"PBRT Pathtracer", "Vulkan Rasterizer"};
            int active_renderer_item = static_cast<int>(this->ui.active_render_mode);
            if (active_renderer_item < 0 || active_renderer_item > 1) throw std::runtime_error("Unknown Spectra render mode item");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ActiveRenderer", &active_renderer_item, renderer_items, static_cast<int>(std::size(renderer_items)))) {
                if (active_renderer_item == 0) this->set_active_render_mode(SpectraRenderMode::PbrtPathtracer);
                else if (active_renderer_item == 1) this->set_active_render_mode(SpectraRenderMode::VulkanRasterizer);
                else throw std::runtime_error("Unknown Spectra render mode item selected");
                renderer_status = this->active_renderer_status();
            }
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("External Completion", renderer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Path Tracer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->pbrt_interactive == nullptr) {
                ImGui::TextDisabled("No active PBRT interactive session");
            } else if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer ? renderer_status.state : "Inactive");
                draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("PBRT Sampler SPP");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", this->spectra_scene->sampler_sample_count);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Current Sample");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Max Iterations");
                ImGui::TableSetColumnIndex(1);
                const int previous_target_sample_count = this->pbrt_interactive->target_sample_count();
                int target_sample_count                = previous_target_sample_count;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->spectra_scene->sampler_sample_count)) {
                    this->pbrt_interactive->set_target_sample_count(target_sample_count);
                    if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Accumulation");
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Reset Accumulation")) {
                    this->pbrt_interactive->request_reset_accumulation();
                    this->clear_pathtracer_throughput_statistics();
                }

                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Rasterizer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->vulkan_rasterizer == nullptr || this->raster_scene == nullptr) {
                ImGui::TextDisabled("No active Vulkan rasterizer session");
            } else if (ImGui::BeginTable("SpectraRasterizerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer ? renderer_status.state : "Inactive");
                draw_statistics_row("Output", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
                draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }


    void Spectra::draw_environment_window() {
        if (!this->ui.environment_visible) return;
        if (!ImGui::Begin("Environment", &this->ui.environment_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const SpectraSceneLight& light : this->spectra_scene->lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->spectra_scene->lights.empty()) {
            ImGui::TextDisabled("No PBRT lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneLight& light : this->spectra_scene->lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outside_medium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(pbrt_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->spectra_scene->mediums.empty()) {
            ImGui::TextDisabled("No PBRT media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMedium& medium : this->spectra_scene->mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(pbrt_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->spectra_scene->medium_bindings.empty()) {
            ImGui::TextDisabled("No PBRT medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMediumBinding& binding : this->spectra_scene->medium_bindings) {
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

        ImGui::End();
    }


    void Spectra::draw_tonemapper_window() {
        if (!this->ui.tonemapper_visible) return;
        if (!ImGui::Begin("Tonemapper", &this->ui.tonemapper_visible)) {
            ImGui::End();
            return;
        }
        if (this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer) {
            ImGui::TextDisabled("Tonemapper applies to PBRT Pathtracer only");
            ImGui::End();
            return;
        }
        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
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
            float exposure = this->pbrt_interactive->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->pbrt_interactive->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_statistics_window() {
        if (!this->ui.statistics_visible) return;
        if (!ImGui::Begin("Statistics", &this->ui.statistics_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->ui.viewport_known ? std::format("{:.0f} x {:.0f}", this->ui.viewport_size[0], this->ui.viewport_size[1]) : "Unknown";
        const ActiveRendererStatus renderer_status = this->active_renderer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("Accumulation", renderer_status.has_accumulation ? "Yes" : "No");
            draw_statistics_row("Scene", this->spectra_scene == nullptr ? "No Scene" : this->spectra_scene->scene_label);
            draw_statistics_row("Frame ID", std::format("{}", this->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->sync.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->swapchain.extent.width, this->swapchain.extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->statistics.last_frame_milliseconds));
            if (this->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraSceneStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            draw_statistics_row("Raster Diagnostics", this->raster_scene == nullptr ? "No raster scene" : std::format("{}", this->raster_scene->diagnostics.size()));
            ImGui::EndTable();
        }

        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                break;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot show Vulkan rasterizer statistics without an active rasterizer session");
                ImGui::SeparatorText("Rasterizer");
                if (ImGui::BeginTable("SpectraRasterizerStatistics", 2, table_flags)) {
                    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    draw_statistics_row("State", renderer_status.state);
                    draw_statistics_row("Output Resolution", std::format("{} x {}", this->spectra_scene->film_resolution[0], this->spectra_scene->film_resolution[1]));
                    draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                    draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                    draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                    draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                    draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                    draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                    ImGui::EndTable();
                }
                ImGui::End();
                return;
        }

        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }

        const std::array<int, 2> film_resolution = this->spectra_scene->film_resolution;
        const int current_sample                 = this->pbrt_interactive->current_sample();
        const int target_sample                  = this->pbrt_interactive->target_sample_count();
        const float completion_ratio             = this->pbrt_interactive->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", this->camera.pathtracer_accumulation_dirty ? "Camera Dirty" : sampling_state);
            draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", std::format("{} x {}", film_resolution[0], film_resolution[1]));
            if (this->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->statistics.throughput_mspp.average(), this->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->statistics.last_frame_rendered_sample ? "Rendered sample" : "No PBRT sample");
            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace xayah
