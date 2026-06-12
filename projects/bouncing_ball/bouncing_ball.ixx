export module xayah.projects.bouncing_ball;
import std;

namespace xayah::projects::bouncing_ball {
    export struct Vertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct Config {
        float radius{0.35f};
        std::array<float, 3> start_position{0.0f, 3.0f, 0.0f};
        std::array<float, 3> initial_velocity{0.0f, 0.0f, 0.0f};
        std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
        float floor_y{0.0f};
        float restitution{0.82f};
        std::uint32_t latitude_segments{18};
        std::uint32_t longitude_segments{36};
    };

    export class Solver {
    public:
        explicit Solver(const Config& config = {});

        void reset();
        void step(float delta_seconds);
        [[nodiscard]] const std::array<float, 3>& current_position() const;
        [[nodiscard]] const std::vector<Vertex>& mesh_vertices() const;
        [[nodiscard]] const std::vector<std::uint32_t>& mesh_indices() const;

    private:
        Config config{};
        std::array<float, 3> position{};
        std::array<float, 3> velocity{};
        std::vector<Vertex> vertices{};
        std::vector<std::uint32_t> indices{};
    };
} // namespace xayah::projects::bouncing_ball
