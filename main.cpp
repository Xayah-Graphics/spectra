import spectra;
import std;

int main() {
    xayah::Volume volume;
    volume.id     = 1;
    volume.name   = "plume";
    volume.size   = {3.0f, 3.0f, 3.0f};
    const std::array volume_local_min{
        -volume.size[0] * 0.5f,
        -volume.size[1] * 0.5f,
        -volume.size[2] * 0.5f,
    };

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
                const float px          = volume_local_min[0] + (static_cast<float>(x) + 0.5f) * scalar_spacing[0];
                const float py          = volume_local_min[1] + (static_cast<float>(y) + 0.5f) * scalar_spacing[1];
                const float pz          = volume_local_min[2] + (static_cast<float>(z) + 0.5f) * scalar_spacing[2];
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
                const float pz          = volume_local_min[2] + (static_cast<float>(z) + 0.5f) * vector_spacing[2];
                vector_grid.x_values[index] = -pz * 0.45f;
            }
        }
    }

    for (std::uint32_t z = 0; z < vector_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < vector_grid.resolution[1] + 1; ++y) {
            for (std::uint32_t x = 0; x < vector_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * vector_grid.resolution[0] + static_cast<std::size_t>(z) * vector_grid.resolution[0] * (vector_grid.resolution[1] + 1);
                const float px          = volume_local_min[0] + (static_cast<float>(x) + 0.5f) * vector_spacing[0];
                const float py          = volume_local_min[1] + static_cast<float>(y) * vector_spacing[1];
                const float pz          = volume_local_min[2] + (static_cast<float>(z) + 0.5f) * vector_spacing[2];
                const float plume       = std::exp(-2.2f * (px * px + pz * pz) - 0.9f * (py + 0.35f) * (py + 0.35f));
                vector_grid.y_values[index] = 0.35f + plume * 0.5f;
            }
        }
    }

    for (std::uint32_t z = 0; z < vector_grid.resolution[2] + 1; ++z) {
        for (std::uint32_t y = 0; y < vector_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < vector_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * vector_grid.resolution[0] + static_cast<std::size_t>(z) * vector_grid.resolution[0] * vector_grid.resolution[1];
                const float px          = volume_local_min[0] + (static_cast<float>(x) + 0.5f) * vector_spacing[0];
                vector_grid.z_values[index] = px * 0.45f;
            }
        }
    }

    volume.centered_scalar_grids.emplace_back(std::move(scalar_grid));
    volume.staggered_vector_grids.emplace_back(std::move(vector_grid));

    xayah::Volume sphere_volume;
    sphere_volume.id     = 2;
    sphere_volume.name   = "offset_sphere";
    sphere_volume.transform.translation = {1.6f, 0.0f, 0.2f};
    sphere_volume.size   = {1.8f, 1.8f, 1.8f};
    const std::array sphere_local_min{
        -sphere_volume.size[0] * 0.5f,
        -sphere_volume.size[1] * 0.5f,
        -sphere_volume.size[2] * 0.5f,
    };

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

    constexpr std::array sphere_center{0.0f, 0.0f, 0.0f};
    for (std::uint32_t z = 0; z < sphere_grid.resolution[2]; ++z) {
        for (std::uint32_t y = 0; y < sphere_grid.resolution[1]; ++y) {
            for (std::uint32_t x = 0; x < sphere_grid.resolution[0]; ++x) {
                const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(y) * sphere_grid.resolution[0] + static_cast<std::size_t>(z) * sphere_grid.resolution[0] * sphere_grid.resolution[1];
                const float px          = sphere_local_min[0] + (static_cast<float>(x) + 0.5f) * sphere_spacing[0];
                const float py          = sphere_local_min[1] + (static_cast<float>(y) + 0.5f) * sphere_spacing[1];
                const float pz          = sphere_local_min[2] + (static_cast<float>(z) + 0.5f) * sphere_spacing[2];
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

    xayah::Particles water_particles;
    water_particles.id = 4;
    water_particles.name = "water_particles";
    water_particles.render_settings.radius_scale = 1.0f;
    constexpr std::uint32_t particle_ring_count = 96;
    constexpr float particle_pi = 3.14159265358979323846f;

    for (std::uint32_t index = 0; index < particle_ring_count; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(particle_ring_count);
        const float angle = t * particle_pi * 2.0f;
        const float radial = 0.85f + 0.18f * std::sin(t * particle_pi * 10.0f);
        xayah::Particle particle;
        particle.position = {
            std::cos(angle) * radial,
            -0.35f + 0.28f * std::sin(t * particle_pi * 6.0f),
            std::sin(angle) * radial,
        };
        particle.radius = 0.035f + 0.012f * (0.5f + 0.5f * std::sin(t * particle_pi * 14.0f));
        particle.color = {0.28f, 0.62f + 0.24f * t, 0.98f};
        water_particles.particles.emplace_back(particle);
    }

    xayah::Scene scene;
    scene.add(std::move(volume));
    scene.add(std::move(sphere_volume));
    scene.add(std::move(cloth_mesh));
    scene.add(std::move(water_particles));
    scene.bake.mode = xayah::ScenePlaybackMode::baked;

    for (int frame_index = 0; frame_index < 3; ++frame_index) {
        xayah::BakedSceneFrame baked_frame = scene.make_baked_frame(frame_index);
        const float frame_t                  = static_cast<float>(frame_index);
        const float plume_density_scale      = 0.65f + frame_t * 0.25f;
        const float sphere_density_scale     = 1.20f - frame_t * 0.25f;
        const float staggered_velocity_scale = 0.85f + frame_t * 0.20f;

        for (std::variant<xayah::VolumeSnapshot, xayah::MeshSnapshot, xayah::ParticlesSnapshot>& object_snapshot : baked_frame.objects) {
            std::visit(
                [&](auto& snapshot) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(snapshot)>, xayah::VolumeSnapshot>) {
                        for (xayah::CenteredScalarGrid& grid : snapshot.centered_scalar_grids) {
                            const float scale = snapshot.object_id == 2 ? sphere_density_scale : plume_density_scale;
                            for (float& value : grid.values) {
                                value = std::clamp(value * scale, 0.0f, 1.0f);
                            }
                        }

                        for (xayah::StaggeredVectorGrid& grid : snapshot.staggered_vector_grids) {
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
                    } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(snapshot)>, xayah::MeshSnapshot>) {
                        for (xayah::MeshVertex& vertex : snapshot.vertices) {
                            const float wave = std::sin(vertex.position[0] * 3.0f + vertex.position[2] * 1.7f + frame_t * 1.1f);
                            vertex.position[1] += wave * 0.16f;
                        }
                    } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(snapshot)>, xayah::ParticlesSnapshot>) {
                        for (std::size_t index = 0; index < snapshot.particles.size(); ++index) {
                            xayah::Particle& particle = snapshot.particles[index];
                            const float t = static_cast<float>(index) / static_cast<float>(snapshot.particles.size());
                            particle.position[0] += std::sin(frame_t * 0.8f + t * particle_pi * 2.0f) * 0.12f;
                            particle.position[1] += frame_t * 0.10f + std::cos(frame_t * 0.6f + t * particle_pi * 8.0f) * 0.05f;
                            particle.position[2] += std::cos(frame_t * 0.8f + t * particle_pi * 2.0f) * 0.12f;
                        }
                    } else {
                        throw std::runtime_error("Unsupported baked scene object snapshot");
                    }
                },
                object_snapshot
            );
        }

        scene.bake.frames.emplace_back(std::move(baked_frame));
    }

    xayah::Spectra spectra;
    spectra.render(scene);
    return 0;
}
