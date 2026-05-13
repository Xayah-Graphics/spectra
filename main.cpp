import spectra;
import pyro;
import std;

xayah::VolumeSnapshot make_pyro_volume_snapshot(const std::uint64_t object_id, const xayah::PyroFrame& frame) {
    xayah::CenteredScalarGrid density_grid;
    density_grid.name       = "density";
    density_grid.resolution = frame.resolution;
    density_grid.values     = frame.density;

    xayah::CenteredScalarGrid temperature_grid;
    temperature_grid.name       = "temperature";
    temperature_grid.resolution = frame.resolution;
    temperature_grid.values     = frame.temperature;

    xayah::StaggeredVectorGrid velocity_grid;
    velocity_grid.name       = "velocity";
    velocity_grid.resolution = frame.resolution;
    velocity_grid.x_values   = frame.velocity_x;
    velocity_grid.y_values   = frame.velocity_y;
    velocity_grid.z_values   = frame.velocity_z;

    xayah::VolumeSnapshot snapshot;
    snapshot.object_id = object_id;
    snapshot.centered_scalar_grids.emplace_back(std::move(density_grid));
    snapshot.centered_scalar_grids.emplace_back(std::move(temperature_grid));
    snapshot.staggered_vector_grids.emplace_back(std::move(velocity_grid));
    return snapshot;
}

xayah::Volume make_pyro_volume(const std::uint64_t object_id, const xayah::PyroBake& bake) {
    if (bake.frames.empty()) throw std::runtime_error("Cannot build a Pyro volume without baked frames");
    xayah::Volume volume;
    volume.id                       = object_id;
    volume.name                     = "pyro_smoke";
    volume.size                     = bake.size;
    volume.transform.translation[1] = bake.size[1] * 0.5f;

    xayah::VolumeSnapshot first_snapshot = make_pyro_volume_snapshot(object_id, bake.frames.front());
    volume.centered_scalar_grids         = std::move(first_snapshot.centered_scalar_grids);
    volume.staggered_vector_grids        = std::move(first_snapshot.staggered_vector_grids);
    volume.render_settings.grid_kind     = xayah::VolumeGridKind::centered_scalar;
    volume.render_settings.grid_name     = "density";
    volume.render_settings.display_mode  = xayah::VolumeDisplayMode::direct;
    volume.render_settings.opacity       = 0.78f;
    volume.render_settings.value_min     = 0.0f;
    volume.render_settings.value_max     = 1.0f;
    volume.render_settings.raymarch_step = 0.018f;
    return volume;
}

void populate_pyro_scene(xayah::Scene& scene, const xayah::PyroBake& bake) {
    constexpr std::uint64_t volume_id = 1;
    scene.add(make_pyro_volume(volume_id, bake));
    scene.bake.mode = xayah::ScenePlaybackMode::baked;

    for (const xayah::PyroFrame& pyro_frame : bake.frames) {
        xayah::BakedSceneFrame scene_frame;
        scene_frame.frame_index = pyro_frame.frame_index;
        scene_frame.objects.emplace_back(make_pyro_volume_snapshot(volume_id, pyro_frame));
        scene.bake.frames.emplace_back(std::move(scene_frame));
    }
}

int main(const int argc, char** argv) {
    constexpr int pyro_smoke_test_frame_count = 3;
    constexpr int pyro_visual_frame_count     = 500;

    bool pyro_smoke = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--pyro-smoke") {
            pyro_smoke = true;
        } else {
            throw std::runtime_error(std::string{"Unknown argument: "} + std::string{argument});
        }
    }

    xayah::PyroConfig config;
    config.resolution          = {48, 96, 48};
    config.cell_size           = 0.035f;
    config.pressure_iterations = 48;

    xayah::PyroSolver solver{config};
    xayah::PyroPlumeSource source;
    source.center      = {0.5f, 0.10f, 0.5f};
    source.radius      = {0.28f, 0.075f, 0.28f};
    source.density     = 26.0f;
    source.temperature = 42.0f;
    source.falloff     = 1.25f;
    solver.set_plume_source(source);
    const xayah::PyroBake bake = solver.bake(pyro_smoke ? pyro_smoke_test_frame_count : pyro_visual_frame_count);

    if (pyro_smoke) {
        for (const xayah::PyroFrame& frame : bake.frames) {
            const double total_density = std::accumulate(frame.density.begin(), frame.density.end(), 0.0);
            const float peak_density   = frame.density.empty() ? 0.0f : *std::max_element(frame.density.begin(), frame.density.end());
            std::cout << "pyro frame=" << frame.frame_index << " total_density=" << total_density << " peak_density=" << peak_density << '\n';
        }
        return 0;
    }

    xayah::Scene scene;
    populate_pyro_scene(scene, bake);
    xayah::Spectra spectra;
    spectra.render(scene);
    return 0;
}
