import spectra;
import std;

int main() {
    xayah::Volume volume;
    volume.id     = 1;
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
    sphere_volume.id     = 2;
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

    xayah::Mesh cloth_mesh;
    cloth_mesh.id = 3;
    cloth_mesh.name = "cloth_patch";
    constexpr std::uint32_t cloth_resolution = 9;
    constexpr float cloth_extent             = 2.4f;
    constexpr float cloth_pi                 = 3.14159265358979323846f;

    for (std::uint32_t z = 0; z < cloth_resolution; ++z) {
        for (std::uint32_t x = 0; x < cloth_resolution; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(cloth_resolution - 1);
            const float v = static_cast<float>(z) / static_cast<float>(cloth_resolution - 1);
            xayah::MeshVertex vertex;
            vertex.position = {
                -cloth_extent * 0.5f + cloth_extent * u,
                0.45f + std::sin(u * cloth_pi * 2.0f) * std::cos(v * cloth_pi * 2.0f) * 0.08f,
                -cloth_extent * 0.5f + cloth_extent * v,
            };
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.color  = {0.20f + u * 0.40f, 0.45f + v * 0.25f, 0.92f};
            cloth_mesh.vertices.emplace_back(vertex);
        }
    }

    for (std::uint32_t z = 0; z < cloth_resolution - 1; ++z) {
        for (std::uint32_t x = 0; x < cloth_resolution - 1; ++x) {
            const std::uint32_t i0 = x + z * cloth_resolution;
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + cloth_resolution;
            const std::uint32_t i3 = i2 + 1;
            cloth_mesh.indices.emplace_back(i0);
            cloth_mesh.indices.emplace_back(i2);
            cloth_mesh.indices.emplace_back(i1);
            cloth_mesh.indices.emplace_back(i1);
            cloth_mesh.indices.emplace_back(i2);
            cloth_mesh.indices.emplace_back(i3);
        }
    }

    xayah::Scene scene;
    scene.volumes.emplace_back(std::move(volume));
    scene.volumes.emplace_back(std::move(sphere_volume));
    scene.meshes.emplace_back(std::move(cloth_mesh));
    scene.bake.mode = xayah::ScenePlaybackMode::baked;

    for (int frame_index = 0; frame_index < 3; ++frame_index) {
        xayah::BakedSceneFrame baked_frame;
        baked_frame.frame_index = frame_index;

        for (const xayah::Volume& live_volume : scene.volumes) {
            xayah::BakedVolumeFrame baked_volume;
            baked_volume.volume_id                = live_volume.id;
            baked_volume.centered_scalar_grids    = live_volume.centered_scalar_grids;
            baked_volume.staggered_vector_grids   = live_volume.staggered_vector_grids;
            const float frame_t                   = static_cast<float>(frame_index);
            const float plume_density_scale       = 0.65f + frame_t * 0.25f;
            const float sphere_density_scale      = 1.20f - frame_t * 0.25f;
            const float staggered_velocity_scale  = 0.85f + frame_t * 0.20f;

            for (xayah::CenteredScalarGrid& grid : baked_volume.centered_scalar_grids) {
                const float scale = live_volume.name == "offset_sphere" ? sphere_density_scale : plume_density_scale;
                for (float& value : grid.values) {
                    value = std::clamp(value * scale, 0.0f, 1.0f);
                }
            }

            for (xayah::StaggeredVectorGrid& grid : baked_volume.staggered_vector_grids) {
                for (float& value : grid.x_values) {
                    value *= staggered_velocity_scale;
                }
                for (float& value : grid.y_values) {
                    value *= staggered_velocity_scale;
                }
                for (float& value : grid.z_values) {
                    value *= staggered_velocity_scale;
                }
            }

            baked_frame.volumes.emplace_back(std::move(baked_volume));
        }

        for (const xayah::Mesh& live_mesh : scene.meshes) {
            xayah::BakedMeshFrame baked_mesh;
            baked_mesh.mesh_id   = live_mesh.id;
            baked_mesh.vertices  = live_mesh.vertices;
            const float frame_t  = static_cast<float>(frame_index);

            for (xayah::MeshVertex& vertex : baked_mesh.vertices) {
                const float wave = std::sin(vertex.position[0] * 3.0f + vertex.position[2] * 1.7f + frame_t * 1.1f);
                vertex.position[1] += wave * 0.16f;
            }

            baked_frame.meshes.emplace_back(std::move(baked_mesh));
        }

        scene.bake.frames.emplace_back(std::move(baked_frame));
    }

    xayah::Spectra spectra;
    spectra.render(scene);
    return 0;
}
