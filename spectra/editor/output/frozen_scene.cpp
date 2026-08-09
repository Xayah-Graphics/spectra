module spectra.editor;

import :output.frozen_scene;
import spectra.scene.format;
import std;

namespace spectra {
    namespace {
        std::expected<std::filesystem::path, std::string> write_frozen_scene_package(scene::Scene scene, const std::filesystem::path& requested_path, const std::filesystem::path& source_scene_path) {
            const std::filesystem::path parent      = std::filesystem::absolute(requested_path.parent_path());
            const std::string name                  = requested_path.stem().string();
            const std::filesystem::path destination = parent / name;
            const std::filesystem::path temporary   = parent / std::format(".{}.spectra-export-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
            try {
                if (std::filesystem::exists(destination)) throw std::runtime_error(std::format("Frozen Scene destination '{}' already exists", destination.string()));
                std::filesystem::create_directory(temporary);
                const std::filesystem::path scene_path = temporary / std::format("{}.spectra", name);
                scene::save_scene(std::move(scene), scene_path, source_scene_path);
                std::filesystem::rename(temporary, destination);
                return destination / scene_path.filename();
            } catch (const std::exception& error) {
                std::error_code cleanup_error{};
                std::filesystem::remove_all(temporary, cleanup_error);
                return std::unexpected{std::string{error.what()}};
            }
        }
    } // namespace

    FrozenSceneExporter::FrozenSceneExporter(GpuScene& gpu_scene) noexcept : gpu_scene{gpu_scene} {
        this->export_state.slots.resize(VulkanFrames::frames_in_flight);
    }

    FrozenSceneExporter::~FrozenSceneExporter() {
        this->wait();
    }

    void FrozenSceneExporter::request(const std::filesystem::path& path) {
        if (this->in_progress()) throw std::runtime_error("A Frozen Scene export is already in progress");
        this->export_state.completed_result.reset();
        this->export_state.pending_request = path;
    }

    bool FrozenSceneExporter::in_progress() const noexcept {
        if (this->export_state.pending_request) return true;
        if (std::ranges::any_of(this->export_state.slots, [](const FrameSlot& slot) { return slot.snapshot.has_value(); })) return true;
        return this->export_state.task.valid();
    }

    std::optional<std::expected<std::filesystem::path, std::string>> FrozenSceneExporter::take_result() {
        if (this->export_state.task.valid() && this->export_state.task.wait_for(std::chrono::seconds{0}) == std::future_status::ready) this->export_state.completed_result = this->export_state.task.get();
        return std::exchange(this->export_state.completed_result, std::nullopt);
    }

    void FrozenSceneExporter::wait() {
        for (FrameSlot& slot : this->export_state.slots)
            if (slot.snapshot) {
                slot.snapshot->materialize();
                if (this->export_state.task.valid()) static_cast<void>(this->export_state.task.get());
                this->export_state.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
                slot.snapshot.reset();
            }
        if (this->export_state.task.valid()) this->export_state.completed_result = this->export_state.task.get();
    }

    std::optional<std::expected<std::filesystem::path, std::string>> FrozenSceneExporter::begin_frame(const std::uint32_t frame_slot_index) {
        FrameSlot& slot = this->export_state.slots[frame_slot_index];
        if (slot.snapshot) {
            slot.snapshot->materialize();
            this->export_state.task = std::async(std::launch::async, write_frozen_scene_package, std::move(slot.snapshot->frozen_scene), slot.output_path, slot.source_scene_path);
            slot.snapshot.reset();
            slot.output_path.clear();
            slot.source_scene_path.clear();
        }
        return this->take_result();
    }

    void FrozenSceneExporter::record_snapshot(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, const scene::Camera& camera, const vk::Extent2D extent, const float exposure, const std::filesystem::path& source_scene_path) {
        if (!this->export_state.pending_request) return;
        FrameSlot& slot        = this->export_state.slots[frame_slot_index];
        slot.output_path       = *std::exchange(this->export_state.pending_request, std::nullopt);
        slot.source_scene_path = source_scene_path;
        slot.snapshot          = this->gpu_scene.record_frozen_scene_snapshot(command_buffer, camera, extent, exposure);
    }
} // namespace spectra
