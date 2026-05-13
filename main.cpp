import spectra;
import pyro;
import bouncingball;
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

    xayah::SceneFrameSnapshot make_bouncing_ball_scene_frame_snapshot(const int frame_index, const xayah::BouncingBallSolver& solver) {
        xayah::SceneFrameSnapshot snapshot;
        snapshot.frame_index = frame_index;
        snapshot.objects.emplace_back(solver.read_snapshot());
        return snapshot;
    }

    void run_pyro_demo() {
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
    }

    void run_bouncing_ball_demo() {
        xayah::BouncingBallConfig config;
        config.object_id      = 1;
        config.name           = "bouncing_ball";
        config.radius         = 0.35f;
        config.start_position = {-1.1f, 3.2f, 0.0f};
        config.initial_velocity  = {1.35f, 0.0f, 0.0f};
        config.floor_y           = 0.0f;
        config.restitution       = 0.84f;
        config.dt                = 1.0f / 60.0f;
        config.color             = {0.95f, 0.42f, 0.28f};
        config.show_bounding_box = true;

        xayah::BouncingBallSolver solver{config};

        xayah::Scene scene;
        scene.add(solver.make_mesh());

        xayah::Spectra spectra;
        spectra.render(scene, [&](const xayah::SceneFrameRequest& request) {
            if (request.reset_stream) solver.reset();
            solver.step();
            return make_bouncing_ball_scene_frame_snapshot(request.frame_index, solver);
        });
    }
} // namespace

int main(const int argc, char** argv) {
    if (argc > 2) throw std::runtime_error("Usage: test [--bouncingball|--pyro]");
    const std::string_view mode = argc == 2 ? std::string_view{argv[1]} : std::string_view{"--bouncingball"};
    if (mode == "--bouncingball") {
        run_bouncing_ball_demo();
        return 0;
    }
    if (mode == "--pyro") {
        run_pyro_demo();
        return 0;
    }
    throw std::runtime_error(std::string{"Unknown demo mode: "} + std::string{mode});
}
