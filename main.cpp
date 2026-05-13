import spectra;
import pyro;
import std;

namespace {
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

    xayah::PyroFrame make_empty_pyro_frame(const xayah::PyroConfig& config) {
        const std::uint64_t nx = config.resolution[0];
        const std::uint64_t ny = config.resolution[1];
        const std::uint64_t nz = config.resolution[2];

        xayah::PyroFrame frame;
        frame.frame_index = 0;
        frame.resolution  = config.resolution;
        frame.cell_size   = config.cell_size;
        frame.density.resize(static_cast<std::size_t>(nx * ny * nz), 0.0f);
        frame.temperature.resize(static_cast<std::size_t>(nx * ny * nz), 0.0f);
        frame.velocity_x.resize(static_cast<std::size_t>((nx + 1u) * ny * nz), 0.0f);
        frame.velocity_y.resize(static_cast<std::size_t>(nx * (ny + 1u) * nz), 0.0f);
        frame.velocity_z.resize(static_cast<std::size_t>(nx * ny * (nz + 1u)), 0.0f);
        return frame;
    }

    xayah::Volume make_pyro_volume(const std::uint64_t object_id, const xayah::PyroConfig& config) {
        xayah::Volume volume;
        volume.id   = object_id;
        volume.name = "pyro_smoke";
        volume.size = {
            static_cast<float>(config.resolution[0]) * config.cell_size,
            static_cast<float>(config.resolution[1]) * config.cell_size,
            static_cast<float>(config.resolution[2]) * config.cell_size,
        };
        volume.transform.translation[1] = volume.size[1] * 0.5f;

        xayah::VolumeSnapshot first_snapshot = make_pyro_volume_snapshot(object_id, make_empty_pyro_frame(config));
        volume.centered_scalar_grids         = std::move(first_snapshot.centered_scalar_grids);
        volume.staggered_vector_grids        = std::move(first_snapshot.staggered_vector_grids);
        volume.render_settings.grid_kind     = xayah::VolumeGridKind::centered_scalar;
        volume.render_settings.grid_name     = "density";
        volume.render_settings.display_mode  = xayah::VolumeDisplayMode::direct;
        volume.render_settings.opacity       = 0.92f;
        volume.render_settings.value_min     = 0.0f;
        volume.render_settings.value_max     = 8.0f;
        volume.render_settings.raymarch_step = 0.012f;
        return volume;
    }

    xayah::SceneFrameSnapshot make_pyro_scene_frame_snapshot(const std::uint64_t object_id, const xayah::PyroFrame& frame) {
        xayah::SceneFrameSnapshot snapshot;
        snapshot.frame_index = frame.frame_index;
        snapshot.objects.emplace_back(make_pyro_volume_snapshot(object_id, frame));
        return snapshot;
    }
} // namespace

int main() {
    constexpr std::uint64_t volume_id = 1;
    xayah::PyroConfig simulation;
    simulation.resolution          = {48, 96, 48};
    simulation.cell_size           = 0.035f;
    simulation.pressure_iterations = 48;

    xayah::PyroPlumeSource source;
    source.center      = {0.5f, 0.10f, 0.5f};
    source.radius      = {0.28f, 0.075f, 0.28f};
    source.density     = 26.0f;
    source.temperature = 42.0f;
    source.falloff     = 1.25f;

    xayah::PyroSolver solver{simulation};
    solver.set_plume_source(source);

    xayah::Scene scene;
    scene.add(make_pyro_volume(volume_id, simulation));

    xayah::Spectra spectra;
    spectra.render(scene, [&](const xayah::SceneFrameRequest& request) {
        if (request.reset_stream) {
            solver.reset();
            solver.set_plume_source(source);
        }
        solver.step();
        return make_pyro_scene_frame_snapshot(volume_id, solver.read_frame(request.frame_index));
    });
    return 0;
}
