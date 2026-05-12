import spectra;
import std;

int main() {
    xayah::Volume volume;
    volume.name   = "plume";
    volume.origin = {-1.5f, -1.5f, -1.5f};
    volume.size   = {3.0f, 3.0f, 3.0f};

    xayah::CenteredScalarGrid scalar_grid;
    scalar_grid.name       = "smoke";
    scalar_grid.resolution = {48, 48, 48};

    const std::array scalar_spacing{
        volume.size[0] / static_cast<float>(scalar_grid.resolution[0]),
        volume.size[1] / static_cast<float>(scalar_grid.resolution[1]),
        volume.size[2] / static_cast<float>(scalar_grid.resolution[2]),
    };
    const std::size_t scalar_count = static_cast<std::size_t>(scalar_grid.resolution[0]) * static_cast<std::size_t>(scalar_grid.resolution[1]) * static_cast<std::size_t>(scalar_grid.resolution[2]);
    scalar_grid.values.resize(scalar_count);

    for (std::uint32_t z = 0; z < scalar_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < scalar_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < scalar_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * scalar_grid.resolution[0] + static_cast<std::size_t>(z) * scalar_grid.resolution[0] * scalar_grid.resolution[1];
                const float px          = volume.origin[0] + (static_cast<float>(x) + 0.5f) * scalar_spacing[0];
                const float py          = volume.origin[1] + (static_cast<float>(y) + 0.5f) * scalar_spacing[1];
                const float pz          = volume.origin[2] + (static_cast<float>(z) + 0.5f) * scalar_spacing[2];
                const float radius      = std::sqrt(px * px + py * py + pz * pz);
                const float plume       = std::exp(-2.2f * (px * px + pz * pz) - 0.9f * (py + 0.35f) * (py + 0.35f));
                const float cap         = std::exp(-5.0f * ((radius - 0.85f) * (radius - 0.85f)));
                scalar_grid.values[index] = std::clamp(plume * 0.85f + cap * 0.18f, 0.0f, 1.0f);
            }
        }
    }

    xayah::StaggeredVectorGrid vector_grid;
    vector_grid.name       = "flow";
    vector_grid.resolution = {24, 24, 24};

    const std::array vector_spacing{
        volume.size[0] / static_cast<float>(vector_grid.resolution[0]),
        volume.size[1] / static_cast<float>(vector_grid.resolution[1]),
        volume.size[2] / static_cast<float>(vector_grid.resolution[2]),
    };
    const std::size_t x_count = static_cast<std::size_t>(vector_grid.resolution[0] + 1) * static_cast<std::size_t>(vector_grid.resolution[1]) * static_cast<std::size_t>(vector_grid.resolution[2]);
    const std::size_t y_count = static_cast<std::size_t>(vector_grid.resolution[0]) * static_cast<std::size_t>(vector_grid.resolution[1] + 1) * static_cast<std::size_t>(vector_grid.resolution[2]);
    const std::size_t z_count = static_cast<std::size_t>(vector_grid.resolution[0]) * static_cast<std::size_t>(vector_grid.resolution[1]) * static_cast<std::size_t>(vector_grid.resolution[2] + 1);
    vector_grid.x_values.resize(x_count);
    vector_grid.y_values.resize(y_count);
    vector_grid.z_values.resize(z_count);

    for (std::uint32_t z = 0; z < vector_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < vector_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < vector_grid.resolution[0] + 1; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * (vector_grid.resolution[0] + 1) + static_cast<std::size_t>(z) * (vector_grid.resolution[0] + 1) * vector_grid.resolution[1];
                const float pz          = volume.origin[2] + (static_cast<float>(z) + 0.5f) * vector_spacing[2];
                vector_grid.x_values[index] = -pz * 0.45f;
            }
        }
    }

    for (std::uint32_t z = 0; z < vector_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < vector_grid.resolution[1] + 1; ++y) {
            for (std::uint32_t x = 0; x < vector_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * vector_grid.resolution[0] + static_cast<std::size_t>(z) * vector_grid.resolution[0] * (vector_grid.resolution[1] + 1);
                const float px          = volume.origin[0] + (static_cast<float>(x) + 0.5f) * vector_spacing[0];
                const float py          = volume.origin[1] + static_cast<float>(y) * vector_spacing[1];
                const float pz          = volume.origin[2] + (static_cast<float>(z) + 0.5f) * vector_spacing[2];
                const float plume       = std::exp(-2.2f * (px * px + pz * pz) - 0.9f * (py + 0.35f) * (py + 0.35f));
                vector_grid.y_values[index] = 0.35f + plume * 0.5f;
            }
        }
    }

    for (std::uint32_t z = 0; z < vector_grid.resolution[2] + 1; ++z) {
        for (std::uint32_t y = 0; y < vector_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < vector_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * vector_grid.resolution[0] + static_cast<std::size_t>(z) * vector_grid.resolution[0] * vector_grid.resolution[1];
                const float px          = volume.origin[0] + (static_cast<float>(x) + 0.5f) * vector_spacing[0];
                vector_grid.z_values[index] = px * 0.45f;
            }
        }
    }

    volume.centered_scalar_grids.emplace_back(std::move(scalar_grid));
    volume.staggered_vector_grids.emplace_back(std::move(vector_grid));

    xayah::Volume sphere_volume;
    sphere_volume.name   = "offset_sphere";
    sphere_volume.origin = {0.7f, -0.9f, -0.7f};
    sphere_volume.size   = {1.8f, 1.8f, 1.8f};

    xayah::CenteredScalarGrid sphere_grid;
    sphere_grid.name       = "density";
    sphere_grid.resolution = {36, 36, 36};

    const std::array sphere_spacing{
        sphere_volume.size[0] / static_cast<float>(sphere_grid.resolution[0]),
        sphere_volume.size[1] / static_cast<float>(sphere_grid.resolution[1]),
        sphere_volume.size[2] / static_cast<float>(sphere_grid.resolution[2]),
    };
    const std::size_t sphere_count = static_cast<std::size_t>(sphere_grid.resolution[0]) * static_cast<std::size_t>(sphere_grid.resolution[1]) * static_cast<std::size_t>(sphere_grid.resolution[2]);
    sphere_grid.values.resize(sphere_count);

    const std::array sphere_center{
        sphere_volume.origin[0] + sphere_volume.size[0] * 0.5f,
        sphere_volume.origin[1] + sphere_volume.size[1] * 0.5f,
        sphere_volume.origin[2] + sphere_volume.size[2] * 0.5f,
    };
    for (std::uint32_t z = 0; z < sphere_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < sphere_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < sphere_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * sphere_grid.resolution[0] + static_cast<std::size_t>(z) * sphere_grid.resolution[0] * sphere_grid.resolution[1];
                const float px          = sphere_volume.origin[0] + (static_cast<float>(x) + 0.5f) * sphere_spacing[0];
                const float py          = sphere_volume.origin[1] + (static_cast<float>(y) + 0.5f) * sphere_spacing[1];
                const float pz          = sphere_volume.origin[2] + (static_cast<float>(z) + 0.5f) * sphere_spacing[2];
                const float dx          = (px - sphere_center[0]) / 0.52f;
                const float dy          = (py - sphere_center[1]) / 0.52f;
                const float dz          = (pz - sphere_center[2]) / 0.52f;
                const float radius      = std::sqrt(dx * dx + dy * dy + dz * dz);
                const float shell       = std::exp(-3.5f * ((radius - 0.72f) * (radius - 0.72f)));
                const float core        = std::exp(-2.6f * (dx * dx + dy * dy + dz * dz));
                sphere_grid.values[index] = std::clamp(shell * 0.55f + core * 0.42f, 0.0f, 1.0f);
            }
        }
    }
    sphere_volume.centered_scalar_grids.emplace_back(std::move(sphere_grid));

    xayah::Scene scene;
    scene.volumes.emplace_back(std::move(volume));
    scene.volumes.emplace_back(std::move(sphere_volume));

    xayah::Spectra spectra;
    spectra.render(scene);
    return 0;
}
