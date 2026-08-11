export module spectra.scene.package;

import spectra.scene;
import std;

namespace spectra::scene {
    export struct PackageResourceReference {
        AssetReference asset{};
        SourceReference source{};
    };

    export struct PackageReferences {
        std::vector<std::optional<PackageResourceReference>> geometries{};
        std::vector<AssetReference> sphere_sets{};
        std::vector<std::optional<AssetReference>> volumes{};
        std::vector<std::optional<PackageResourceReference>> textures{};
        std::optional<AssetReference> frozen_dynamic_frame{};
    };

    export struct AssetTransaction {
        std::vector<std::filesystem::path> created_paths{};

        void record(const std::filesystem::path& path);
        void commit() noexcept;
        void rollback();
    };

    export void load_package_resources(Scene& scene, const std::filesystem::path& package_root);
    export [[nodiscard]] PackageReferences prepare_package_resources(const Scene& scene, const std::filesystem::path& package_root, const std::filesystem::path& source_root, bool preserve_sources, AssetTransaction& transaction);
} // namespace spectra::scene
