module;
#include "pyro.h"
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <cusparse.h>

export module xayah.projects.pyro;
import std;

namespace xayah::projects::pyro {
    export enum class FlowBoundaryType : std::uint32_t {
        NoSlipWall   = 0,
        FreeSlipWall = 1,
        Outflow      = 2,
        Periodic     = 3,
    };

    export enum class ScalarBoundaryType : std::uint32_t {
        FixedValue = 0,
        ZeroFlux   = 1,
        Periodic   = 2,
    };

    export enum class ScalarAdvectionMode : std::uint32_t {
        Linear         = 0,
        MonotonicCubic = 1,
    };

    export struct FlowBoundaryFace {
        FlowBoundaryType type{FlowBoundaryType::NoSlipWall};
        float velocity_x{0.0f};
        float velocity_y{0.0f};
        float velocity_z{0.0f};
        float pressure{0.0f};
    };

    export struct FlowBoundary {
        FlowBoundaryFace x_minus{FlowBoundaryType::Periodic};
        FlowBoundaryFace x_plus{FlowBoundaryType::Periodic};
        FlowBoundaryFace y_minus{FlowBoundaryType::NoSlipWall};
        FlowBoundaryFace y_plus{FlowBoundaryType::Outflow};
        FlowBoundaryFace z_minus{FlowBoundaryType::Periodic};
        FlowBoundaryFace z_plus{FlowBoundaryType::Periodic};
    };

    export struct ScalarBoundaryFace {
        ScalarBoundaryType type{ScalarBoundaryType::FixedValue};
        float value{0.0f};
    };

    export struct ScalarBoundary {
        ScalarBoundaryFace x_minus{ScalarBoundaryType::Periodic, 0.0f};
        ScalarBoundaryFace x_plus{ScalarBoundaryType::Periodic, 0.0f};
        ScalarBoundaryFace y_minus{ScalarBoundaryType::FixedValue, 0.0f};
        ScalarBoundaryFace y_plus{ScalarBoundaryType::FixedValue, 0.0f};
        ScalarBoundaryFace z_minus{ScalarBoundaryType::Periodic, 0.0f};
        ScalarBoundaryFace z_plus{ScalarBoundaryType::Periodic, 0.0f};
    };

    export struct SolverConfiguration {
        std::array<std::uint32_t, 3> resolution{64, 96, 64};
        float cell_size{0.01875f};
        std::int32_t pressure_iterations{64};
        float ambient_temperature{0.0f};
        float buoyancy_density_factor{0.15f};
        float buoyancy_temperature_factor{1.2f};
        float vorticity_confinement{0.22f};
        ScalarAdvectionMode scalar_advection_mode{ScalarAdvectionMode::MonotonicCubic};
        FlowBoundary flow_boundary{};
        ScalarBoundary density_boundary{};
        ScalarBoundary temperature_boundary{};
    };

    export struct PlumeSource {
        std::array<float, 3> center{0.5f, 0.12f, 0.5f};
        std::array<float, 3> radius{0.07f, 0.05f, 0.07f};
        float density{18.0f};
        float temperature{36.0f};
        float falloff{2.2f};
    };

    export struct CudaVolumeView {
        void* cuda_stream{};
        std::array<std::uint32_t, 3> resolution{};
        const float* density{};
        const float* temperature{};
        const float* velocity_x{};
        const float* velocity_y{};
        const float* velocity_z{};
        std::uint64_t cell_count{};
    };

    export struct Solver {
        explicit Solver(const SolverConfiguration& configuration = {});
        ~Solver() noexcept;

        Solver(const Solver& other) = delete;
        Solver(Solver&& other) noexcept;
        Solver& operator=(const Solver& other) = delete;
        Solver& operator=(Solver&& other) noexcept;

        void set_runtime_configuration(const SolverConfiguration& configuration) noexcept;
        void reset();
        void set_initial_fields(std::span<const float> density, std::span<const float> temperature);
        void set_plume_source(const PlumeSource& source);
        void step(float delta_seconds);
        [[nodiscard]] CudaVolumeView cuda_volume() const noexcept;

    private:
        struct {
            std::array<std::uint32_t, 3> resolution{0, 0, 0};
            std::int32_t nx{0};
            std::int32_t ny{0};
            std::int32_t nz{0};
            float cell_size{0.0f};
            std::int32_t pressure_iterations{0};
            float ambient_temperature{0.0f};
            float buoyancy_density_factor{0.0f};
            float buoyancy_temperature_factor{0.0f};
            float vorticity_confinement{0.0f};
            std::uint32_t scalar_advection_mode{1};
            std::array<std::uint32_t, 6> flow_boundary_types{};
            std::array<float, 18> flow_boundary_velocity{};
            std::array<float, 6> flow_boundary_pressure{};
            std::array<std::uint32_t, 6> density_boundary_types{};
            std::array<float, 6> density_boundary_values{};
            std::array<std::uint32_t, 6> temperature_boundary_types{};
            std::array<float, 6> temperature_boundary_values{};
            std::uint64_t cell_count{0};
            std::array<std::uint64_t, 3> velocity_count{};
            std::size_t cell_bytes{0};
            std::array<std::size_t, 3> velocity_bytes{};
            cudaStream_t cuda_stream{nullptr};
            dim3 block{};
            dim3 cells{};
            std::array<dim3, 3> velocity_cells{};
            dim3 sync_block{};
            std::array<dim3, 3> sync_velocity_grid{};
            std::array<unsigned, 3> velocity_linear_grid{};
            unsigned linear_grid{0};
            cublasHandle_t cublas{nullptr};
            cusparseHandle_t cusparse{nullptr};
            cusparseSpMatDescr_t pressure_matrix{nullptr};
            cusparseDnVecDescr_t pressure_vec_p{nullptr};
            cusparseDnVecDescr_t pressure_vec_ap{nullptr};
            std::size_t spmv_buffer_size{0};
            PlumeSource plume_source{};
            std::vector<float> density_source{};
            std::vector<float> temperature_source{};
            std::vector<float> initial_density{};
            std::vector<float> initial_temperature{};
        } host_data;

        struct {
            float* density{nullptr};
            float* density_scratch{nullptr};
            float* density_source{nullptr};
            float* temperature{nullptr};
            float* temperature_scratch{nullptr};
            float* temperature_source{nullptr};
            std::array<float*, 3> force{};
            std::array<float*, 3> solid_velocity{};
            std::array<float*, 3> velocity{};
            std::array<float*, 3> velocity_scratch{};
            std::array<float*, 3> centered_velocity{};
            float* pressure{nullptr};
            float* pressure_rhs{nullptr};
            std::array<float*, 3> vorticity{};
            float* vorticity_magnitude{nullptr};
            int* pressure_anchor{nullptr};
            int* pressure_row_offsets{nullptr};
            int* pressure_column_indices{nullptr};
            float* pressure_values{nullptr};
            float* pcg_r{nullptr};
            float* pcg_p{nullptr};
            float* pcg_ap{nullptr};
            float* pressure_dot_rz{nullptr};
            float* pressure_dot_pap{nullptr};
            float* pressure_dot_rr{nullptr};
            float* pressure_alpha{nullptr};
            float* pressure_negative_alpha{nullptr};
            float* pressure_beta{nullptr};
            float* pressure_one{nullptr};
            float* solid_temperature{nullptr};
            std::uint8_t* occupancy{nullptr};
            void* spmv_buffer{nullptr};
        } device_data;

        void allocate_device_data();
        void release_device_data() noexcept;
        void initialize_pressure_system();
        void solve_pressure(float delta_seconds);
        void reset_moved_from() noexcept;
    };
} // namespace xayah::projects::pyro
