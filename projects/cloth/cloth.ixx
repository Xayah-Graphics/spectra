export module xayah.projects.cloth;
import std;

namespace xayah::projects::cloth {
    export struct Vertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
    };

    export struct SphereCollider {
        std::array<float, 3> center{0.0f, 0.65f, 0.0f};
        float radius{0.72f};
    };

    export struct Config {
        std::uint32_t columns{45};
        std::uint32_t rows{37};
        float width{3.4f};
        float depth{2.7f};
        std::array<float, 3> origin{-1.7f, 2.35f, -1.35f};
        std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
        float velocity_damping{0.995f};
        float max_substep_seconds{1.0f / 120.0f};
        std::uint32_t solver_iterations{10};
        float stretch_compliance{0.000001f};
        float shear_compliance{0.00001f};
        float bend_compliance{0.00045f};
        float collision_margin{0.018f};
    };

    export class Solver {
    public:
        explicit Solver(const Config& config, const SphereCollider& collider);
        ~Solver() noexcept;

        Solver(const Solver& other)                = delete;
        Solver(Solver&& other) noexcept            = delete;
        Solver& operator=(const Solver& other)     = delete;
        Solver& operator=(Solver&& other) noexcept = delete;

        void reset();
        void step(float delta_seconds);
        [[nodiscard]] const std::vector<Vertex>& mesh_vertices() const;
        [[nodiscard]] const std::vector<std::uint32_t>& mesh_indices() const;

    private:
        struct HostState {
            std::uint32_t columns{0};
            std::uint32_t rows{0};
            float dx{0.0f};
            float dz{0.0f};
            float shear_rest_length{0.0f};
            std::uint32_t block_size{256};
            unsigned vertex_grid{0};
            unsigned triangle_grid{0};
            std::uint32_t vertex_count{0};
            std::uint32_t triangle_count{0};
            std::uint32_t horizontal_constraint_count{0};
            std::uint32_t vertical_constraint_count{0};
            std::uint32_t shear_constraint_count{0};
            std::uint32_t horizontal_bend_constraint_count{0};
            std::uint32_t vertical_bend_constraint_count{0};
            std::vector<float> position_x{};
            std::vector<float> position_y{};
            std::vector<float> position_z{};
            std::vector<float> normal_x{};
            std::vector<float> normal_y{};
            std::vector<float> normal_z{};
            std::vector<Vertex> vertices{};
            std::vector<std::uint32_t> indices{};
        };

        struct DeviceState {
            void* stream{nullptr};
            float* position_x{nullptr};
            float* position_y{nullptr};
            float* position_z{nullptr};
            float* previous_x{nullptr};
            float* previous_y{nullptr};
            float* previous_z{nullptr};
            float* velocity_x{nullptr};
            float* velocity_y{nullptr};
            float* velocity_z{nullptr};
            float* inverse_mass{nullptr};
            float* normal_x{nullptr};
            float* normal_y{nullptr};
            float* normal_z{nullptr};
            std::uint32_t* indices{nullptr};
            float* horizontal_lambda{nullptr};
            float* vertical_lambda{nullptr};
            float* shear_down_lambda{nullptr};
            float* shear_up_lambda{nullptr};
            float* horizontal_bend_lambda{nullptr};
            float* vertical_bend_lambda{nullptr};
            int* error_flag{nullptr};
        };

        Config config{};
        SphereCollider collider{};
        mutable HostState host{};
        DeviceState device{};

        void create_device();
        void destroy_device() noexcept;
        void clear_constraint_lambdas(float* lambda, std::uint32_t count);
        void solve_constraint_batch(std::uint32_t kind, std::uint32_t color, float* lambda, std::uint32_t count, float compliance, float rest_length, float substep_seconds);
        void compute_normals();
        [[nodiscard]] const std::vector<Vertex>& download_vertices() const;
    };
} // namespace xayah::projects::cloth
