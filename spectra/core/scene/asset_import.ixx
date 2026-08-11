export module spectra.scene.asset_import;

import spectra.scene;
import std;

namespace spectra::scene {
    export void load_triangle_mesh_source(TriangleMeshGeometry& mesh, const std::filesystem::path& path);
    export void load_image_source(ImageTexture& image, TextureColorSpace color_space, const std::filesystem::path& path);
} // namespace spectra::scene
