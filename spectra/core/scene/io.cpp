module;

#if defined(_WIN32)
#include <Windows.h>
#endif

module spectra.scene.io;
import spectra.scene.assets;
import spectra.scene.usd;
import std;

namespace spectra::scene {
    namespace {
        [[nodiscard]] std::vector<std::string_view> scene_sources(const Scene& scene) {
            std::vector<std::string_view> sources{};
            const auto add = [&sources](const std::string& source) {
                if (!source.empty() && !std::ranges::contains(sources, source)) sources.push_back(source);
            };
            for (const Geometry& geometry : scene.resources.geometries)
                if (const TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) add(mesh->source);
            for (const SphereSet& spheres : scene.resources.sphere_sets) add(spheres.source);
            for (const Volume& volume : scene.resources.volumes)
                if (const OpenVdbVolume* grid = std::get_if<OpenVdbVolume>(&volume.data))
                    for (const OpenVdbField& field : grid->fields) add(field.source);
            for (const Texture& texture : scene.resources.textures)
                if (const ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) add(image->source);
            return sources;
        }
    } // namespace

    Scene load_scene(const std::filesystem::path& path) {
        if (path.extension() != ".usd" && path.extension() != ".usda" && path.extension() != ".usdc") throw std::runtime_error(std::format("Spectra accepts only USD scene files; '{}' is not .usd, .usda, or .usdc", path.string()));
        Scene scene = load_usd(path);
        load_scene_sources(scene, path.parent_path());
        return scene;
    }

    void save_scene(const Scene& scene, const std::filesystem::path& path, const std::filesystem::path& source_scene_path) {
        if (path.extension() != ".usd" && path.extension() != ".usda" && path.extension() != ".usdc") throw std::runtime_error(std::format("Spectra saves only USD scene files; '{}' is not .usd, .usda, or .usdc", path.string()));
        const std::filesystem::path target_root = path.parent_path();
        if (!target_root.empty()) std::filesystem::create_directories(target_root);
        const std::filesystem::path source_root = source_scene_path.empty() ? target_root : source_scene_path.parent_path();
        std::filesystem::path source_sidecar    = source_scene_path;
        source_sidecar.replace_extension(".physica.usda");
        if (scene.simulation && !source_scene_path.empty() && std::filesystem::exists(source_sidecar)) {
            std::filesystem::path target_sidecar = path;
            target_sidecar.replace_extension(".physica.usda");
            std::vector<std::filesystem::path> created_sources{};
            static std::atomic_uint64_t sidecar_temporary_sequence{};
            const std::filesystem::path temporary_sidecar = target_root / std::format(".{}.physica-save-{}-{}.usda", path.stem().string(), std::chrono::steady_clock::now().time_since_epoch().count(), sidecar_temporary_sequence.fetch_add(1, std::memory_order_relaxed));
            try {
                if (std::filesystem::absolute(source_scene_path).lexically_normal() != std::filesystem::absolute(path).lexically_normal()) {
                    std::filesystem::copy_file(source_scene_path, path);
                    created_sources.emplace_back(path);
                    if (std::filesystem::absolute(source_root).lexically_normal() != std::filesystem::absolute(target_root).lexically_normal())
                        for (const std::string_view source : scene_sources(scene)) {
                            const std::filesystem::path destination = target_root / source;
                            std::filesystem::create_directories(destination.parent_path());
                            std::filesystem::copy_file(source_root / source, destination);
                            created_sources.push_back(destination);
                        }
                }
                save_physica_usd(scene, temporary_sidecar, path);
#if defined(_WIN32)
                if (!MoveFileExW(temporary_sidecar.c_str(), target_sidecar.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(std::format("Failed to replace Physica USD sidecar '{}': Windows error {}", target_sidecar.string(), GetLastError()));
#else
                std::error_code replacement_error{};
                std::filesystem::rename(temporary_sidecar, target_sidecar, replacement_error);
                if (replacement_error) throw std::runtime_error(std::format("Failed to replace Physica USD sidecar '{}': {}", target_sidecar.string(), replacement_error.message()));
#endif
            } catch (...) {
                std::error_code error{};
                std::filesystem::remove(temporary_sidecar, error);
                for (const std::filesystem::path& created : created_sources | std::views::reverse) std::filesystem::remove(created, error);
                throw;
            }
            return;
        }
        std::vector<std::filesystem::path> created_sources{};
        static std::atomic_uint64_t temporary_sequence{};
        const std::filesystem::path temporary_path = target_root / std::format(".{}.usd-save-{}-{}{}", path.stem().string(), std::chrono::steady_clock::now().time_since_epoch().count(), temporary_sequence.fetch_add(1, std::memory_order_relaxed), path.extension().string());
        try {
            if (std::filesystem::absolute(source_root).lexically_normal() != std::filesystem::absolute(target_root).lexically_normal())
                for (const std::string_view source : scene_sources(scene)) {
                    const std::filesystem::path destination = target_root / source;
                    std::filesystem::create_directories(destination.parent_path());
                    std::filesystem::copy_file(source_root / source, destination);
                    created_sources.push_back(destination);
                }
            save_usd(scene, temporary_path);
#if defined(_WIN32)
            if (!MoveFileExW(temporary_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error(std::format("Failed to replace USD scene '{}': Windows error {}", path.string(), GetLastError()));
#else
            std::error_code replacement_error{};
            std::filesystem::rename(temporary_path, path, replacement_error);
            if (replacement_error) throw std::runtime_error(std::format("Failed to replace USD scene '{}': {}", path.string(), replacement_error.message()));
#endif
        } catch (...) {
            std::error_code error{};
            std::filesystem::remove(temporary_path, error);
            for (const std::filesystem::path& created : created_sources | std::views::reverse) std::filesystem::remove(created, error);
            throw;
        }
    }
} // namespace spectra::scene
