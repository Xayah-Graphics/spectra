import spectra;
import pyro;
import bouncingball;
import cloth;
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

    void validate_demo_color(const std::array<float, 3>& color, const char* label) {
        for (const float value : color) {
            if (value < 0.0f || value > 1.0f) throw std::runtime_error(std::string{label} + " color values must be in [0, 1]");
        }
    }

    xayah::MeshVertex make_mesh_vertex(const xayah::BouncingBallVertex& vertex, const std::array<float, 3>& color) {
        return xayah::MeshVertex{vertex.position, vertex.normal, color};
    }

    xayah::MeshVertex make_mesh_vertex(const xayah::ClothVertex& vertex, const std::array<float, 3>& color) {
        return xayah::MeshVertex{vertex.position, vertex.normal, color};
    }

    std::vector<xayah::MeshVertex> make_mesh_vertices(const std::vector<xayah::BouncingBallVertex>& vertices, const std::array<float, 3>& color) {
        std::vector<xayah::MeshVertex> mesh_vertices;
        mesh_vertices.reserve(vertices.size());
        for (const xayah::BouncingBallVertex& vertex : vertices) mesh_vertices.emplace_back(make_mesh_vertex(vertex, color));
        return mesh_vertices;
    }

    std::vector<xayah::MeshVertex> make_mesh_vertices(const std::vector<xayah::ClothVertex>& vertices, const std::array<float, 3>& color) {
        std::vector<xayah::MeshVertex> mesh_vertices;
        mesh_vertices.reserve(vertices.size());
        for (const xayah::ClothVertex& vertex : vertices) mesh_vertices.emplace_back(make_mesh_vertex(vertex, color));
        return mesh_vertices;
    }

    xayah::MeshVertex sphere_vertex(const float radius, const float theta, const float phi, const std::array<float, 3>& color) {
        const float sin_phi = std::sin(phi);
        const std::array normal{std::cos(theta) * sin_phi, std::cos(phi), std::sin(theta) * sin_phi};
        return xayah::MeshVertex{{normal[0] * radius, normal[1] * radius, normal[2] * radius}, normal, color};
    }

    xayah::Mesh make_sphere_mesh(const std::uint64_t object_id, const std::string& name, const std::array<float, 3>& center, const float radius, const std::array<float, 3>& color, const std::uint32_t latitude_segments, const std::uint32_t longitude_segments) {
        if (object_id == 0) throw std::runtime_error("Sphere mesh object_id must not be zero");
        if (name.empty()) throw std::runtime_error("Sphere mesh name must not be empty");
        if (radius <= 0.0f) throw std::runtime_error("Sphere mesh radius must be positive");
        if (latitude_segments < 3u) throw std::runtime_error("Sphere mesh latitude_segments must be at least 3");
        if (longitude_segments < 3u) throw std::runtime_error("Sphere mesh longitude_segments must be at least 3");
        validate_demo_color(color, "Sphere mesh");

        const std::uint32_t latitude_count  = latitude_segments + 1u;
        const std::uint32_t longitude_count = longitude_segments + 1u;
        if (static_cast<std::uint64_t>(latitude_count) * static_cast<std::uint64_t>(longitude_count) > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Sphere mesh is too large");

        xayah::Mesh mesh;
        mesh.id                    = object_id;
        mesh.name                  = name;
        mesh.transform.translation = center;
        mesh.vertices.reserve(static_cast<std::size_t>(latitude_count) * static_cast<std::size_t>(longitude_count));
        for (std::uint32_t latitude = 0; latitude <= latitude_segments; ++latitude) {
            const float v   = static_cast<float>(latitude) / static_cast<float>(latitude_segments);
            const float phi = v * std::numbers::pi_v<float>;
            for (std::uint32_t longitude = 0; longitude <= longitude_segments; ++longitude) {
                const float u     = static_cast<float>(longitude) / static_cast<float>(longitude_segments);
                const float theta = u * std::numbers::pi_v<float> * 2.0f;
                mesh.vertices.emplace_back(sphere_vertex(radius, theta, phi, color));
            }
        }

        mesh.indices.reserve(static_cast<std::size_t>(latitude_segments) * static_cast<std::size_t>(longitude_segments) * 6u);
        for (std::uint32_t latitude = 0; latitude < latitude_segments; ++latitude) {
            for (std::uint32_t longitude = 0; longitude < longitude_segments; ++longitude) {
                const std::uint32_t current = latitude * longitude_count + longitude;
                const std::uint32_t next    = current + longitude_count;
                if (latitude != 0u) {
                    mesh.indices.emplace_back(current);
                    mesh.indices.emplace_back(next);
                    mesh.indices.emplace_back(current + 1u);
                }
                if (latitude + 1u != latitude_segments) {
                    mesh.indices.emplace_back(current + 1u);
                    mesh.indices.emplace_back(next);
                    mesh.indices.emplace_back(next + 1u);
                }
            }
        }
        mesh.render_settings.display_mode      = xayah::MeshDisplayMode::surface;
        mesh.render_settings.show_bounding_box = false;
        return mesh;
    }

    xayah::Mesh make_bouncing_ball_mesh(const std::uint64_t object_id, const std::string& name, const xayah::BouncingBallSolver& solver, const std::array<float, 3>& color, const bool show_bounding_box, const xayah::MeshDisplayMode display_mode) {
        validate_demo_color(color, "BouncingBall mesh");
        xayah::Mesh mesh;
        mesh.id                                = object_id;
        mesh.name                              = name;
        mesh.transform.translation             = solver.current_position();
        mesh.vertices                          = make_mesh_vertices(solver.mesh_vertices(), color);
        mesh.indices                           = solver.mesh_indices();
        mesh.render_settings.display_mode      = display_mode;
        mesh.render_settings.show_bounding_box = show_bounding_box;
        return mesh;
    }

    xayah::MeshSnapshot make_bouncing_ball_snapshot(const std::uint64_t object_id, const xayah::BouncingBallSolver& solver, const std::array<float, 3>& color) {
        xayah::Transform transform;
        transform.translation = solver.current_position();
        return xayah::MeshSnapshot{object_id, transform, make_mesh_vertices(solver.mesh_vertices(), color)};
    }

    xayah::Mesh make_cloth_mesh(const std::uint64_t object_id, const std::string& name, const xayah::ClothSolver& solver, const std::array<float, 3>& color, const bool show_bounding_box, const xayah::MeshDisplayMode display_mode) {
        validate_demo_color(color, "Cloth mesh");
        xayah::Mesh mesh;
        mesh.id                                = object_id;
        mesh.name                              = name;
        mesh.vertices                          = make_mesh_vertices(solver.mesh_vertices(), color);
        mesh.indices                           = solver.mesh_indices();
        mesh.render_settings.display_mode      = display_mode;
        mesh.render_settings.show_bounding_box = show_bounding_box;
        return mesh;
    }

    xayah::MeshSnapshot make_cloth_snapshot(const std::uint64_t object_id, const xayah::ClothSolver& solver, const std::array<float, 3>& color) {
        xayah::MeshSnapshot snapshot;
        snapshot.object_id = object_id;
        snapshot.vertices  = make_mesh_vertices(solver.mesh_vertices(), color);
        return snapshot;
    }

    xayah::SceneFrameSnapshot make_bouncing_ball_scene_frame_snapshot(const int frame_index, const std::uint64_t object_id, const xayah::BouncingBallSolver& solver, const std::array<float, 3>& color) {
        xayah::SceneFrameSnapshot snapshot;
        snapshot.frame_index = frame_index;
        snapshot.objects.emplace_back(make_bouncing_ball_snapshot(object_id, solver, color));
        return snapshot;
    }

    xayah::SceneFrameSnapshot make_cloth_scene_frame_snapshot(const int frame_index, const std::uint64_t object_id, const xayah::ClothSolver& solver, const std::array<float, 3>& color) {
        xayah::SceneFrameSnapshot snapshot;
        snapshot.frame_index = frame_index;
        snapshot.objects.emplace_back(make_cloth_snapshot(object_id, solver, color));
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
            solver.step(request.delta_seconds);
            return make_pyro_scene_frame_snapshot(volume_id, solver.read_frame(request.frame_index));
        });
    }

    void run_bouncing_ball_demo() {
        constexpr std::uint64_t object_id = 1;
        const std::array<float, 3> color{0.95f, 0.42f, 0.28f};

        xayah::BouncingBallConfig config;
        config.radius           = 0.35f;
        config.start_position   = {0.0f, 3.2f, 0.0f};
        config.initial_velocity = {0.0f, 0.0f, 0.0f};
        config.floor_y          = 0.0f;
        config.restitution      = 0.84f;

        xayah::BouncingBallSolver solver{config};

        xayah::Scene scene;
        scene.add(make_bouncing_ball_mesh(object_id, "bouncing_ball", solver, color, true, xayah::MeshDisplayMode::surface));

        xayah::Spectra spectra;
        spectra.render(scene, [&](const xayah::SceneFrameRequest& request) {
            if (request.reset_stream) solver.reset();
            solver.step(request.delta_seconds);
            return make_bouncing_ball_scene_frame_snapshot(request.frame_index, object_id, solver, color);
        });
    }

    void run_cloth_demo() {
        constexpr std::uint64_t cloth_id  = 1;
        constexpr std::uint64_t sphere_id = 2;

        xayah::ClothSphereCollider collider;
        collider.center = {0.0f, 0.72f, 0.05f};
        collider.radius = 0.72f;

        xayah::ClothConfig config;
        config.columns             = 45;
        config.rows                = 37;
        config.width               = 3.4f;
        config.depth               = 2.7f;
        config.origin              = {-1.7f, 2.35f, -1.35f};
        config.solver_iterations   = 10;
        config.max_substep_seconds = 1.0f / 120.0f;
        config.stretch_compliance  = 0.000001f;
        config.shear_compliance    = 0.00001f;
        config.bend_compliance     = 0.00045f;
        config.collision_margin    = 0.018f;
        const std::array<float, 3> cloth_color{0.32f, 0.56f, 0.96f};

        xayah::ClothSolver solver{config, collider};

        xayah::Scene scene;
        scene.add(make_sphere_mesh(sphere_id, "collision_sphere", collider.center, collider.radius, {0.95f, 0.58f, 0.30f}, 24u, 48u));
        scene.add(make_cloth_mesh(cloth_id, "xpbd_cloth", solver, cloth_color, false, xayah::MeshDisplayMode::surface));

        xayah::Spectra spectra;
        spectra.render(scene, [&](const xayah::SceneFrameRequest& request) {
            if (request.reset_stream) solver.reset();
            solver.step(request.delta_seconds);
            return make_cloth_scene_frame_snapshot(request.frame_index, cloth_id, solver, cloth_color);
        });
    }
} // namespace

int main(const int argc, char** argv) {
    if (argc > 2) throw std::runtime_error("Usage: test [--cloth|--bouncingball|--pyro]");
    const std::string_view mode = argc == 2 ? std::string_view{argv[1]} : std::string_view{"--cloth"};
    if (mode == "--cloth") {
        run_cloth_demo();
        return 0;
    }
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
