module;
#include "pyro.h"
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <cusparse.h>

export module pyro;
import std;

namespace spectra {
    export enum class PyroFlowBoundaryType : std::uint32_t {
        no_slip_wall   = 0,
        free_slip_wall = 1,
        outflow        = 2,
        periodic       = 3,
    };

    export enum class PyroScalarBoundaryType : std::uint32_t {
        fixed_value = 0,
        zero_flux   = 1,
        periodic    = 2,
    };

    export enum class PyroScalarAdvectionMode : std::uint32_t {
        linear          = 0,
        monotonic_cubic = 1,
    };

    export struct PyroFlowBoundaryFace {
        PyroFlowBoundaryType type{PyroFlowBoundaryType::no_slip_wall};
        float velocity_x{0.0f};
        float velocity_y{0.0f};
        float velocity_z{0.0f};
        float pressure{0.0f};
    };

    export struct PyroFlowBoundary {
        PyroFlowBoundaryFace x_minus{PyroFlowBoundaryType::periodic};
        PyroFlowBoundaryFace x_plus{PyroFlowBoundaryType::periodic};
        PyroFlowBoundaryFace y_minus{PyroFlowBoundaryType::no_slip_wall};
        PyroFlowBoundaryFace y_plus{PyroFlowBoundaryType::outflow};
        PyroFlowBoundaryFace z_minus{PyroFlowBoundaryType::periodic};
        PyroFlowBoundaryFace z_plus{PyroFlowBoundaryType::periodic};
    };

    export struct PyroScalarBoundaryFace {
        PyroScalarBoundaryType type{PyroScalarBoundaryType::fixed_value};
        float value{0.0f};
    };

    export struct PyroScalarBoundary {
        PyroScalarBoundaryFace x_minus{PyroScalarBoundaryType::periodic, 0.0f};
        PyroScalarBoundaryFace x_plus{PyroScalarBoundaryType::periodic, 0.0f};
        PyroScalarBoundaryFace y_minus{PyroScalarBoundaryType::fixed_value, 0.0f};
        PyroScalarBoundaryFace y_plus{PyroScalarBoundaryType::fixed_value, 0.0f};
        PyroScalarBoundaryFace z_minus{PyroScalarBoundaryType::periodic, 0.0f};
        PyroScalarBoundaryFace z_plus{PyroScalarBoundaryType::periodic, 0.0f};
    };

    export struct PyroConfig {
        std::array<std::uint32_t, 3> resolution{64, 96, 64};
        float cell_size{0.01875f};
        std::int32_t pressure_iterations{64};
        float ambient_temperature{0.0f};
        float buoyancy_density_factor{0.15f};
        float buoyancy_temperature_factor{1.2f};
        float vorticity_confinement{0.22f};
        PyroScalarAdvectionMode scalar_advection_mode{PyroScalarAdvectionMode::monotonic_cubic};
        PyroFlowBoundary flow_boundary{};
        PyroScalarBoundary density_boundary{};
        PyroScalarBoundary temperature_boundary{};
    };

    export struct PyroPlumeSource {
        std::array<float, 3> center{0.5f, 0.12f, 0.5f};
        std::array<float, 3> radius{0.07f, 0.05f, 0.07f};
        float density{18.0f};
        float temperature{36.0f};
        float falloff{2.2f};
    };

    export struct PyroFrame {
        int frame_index{0};
        std::array<std::uint32_t, 3> resolution{0, 0, 0};
        float cell_size{0.0f};
        std::vector<float> density{};
        std::vector<float> temperature{};
        std::vector<float> velocity_x{};
        std::vector<float> velocity_y{};
        std::vector<float> velocity_z{};
    };

    export class PyroSolver {
    public:
        explicit PyroSolver(const PyroConfig& config = {});
        ~PyroSolver() noexcept;

        PyroSolver(const PyroSolver& other) = delete;
        PyroSolver(PyroSolver&& other) noexcept;
        PyroSolver& operator=(const PyroSolver& other) = delete;
        PyroSolver& operator=(PyroSolver&& other) noexcept;

        void reset();
        void set_plume_source(const PyroPlumeSource& source);
        void step(float delta_seconds);
        [[nodiscard]] PyroFrame read_frame(int frame_index);

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
            cudaStream_t stream{nullptr};
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
            PyroPlumeSource plume_source{};
            std::vector<float> density_source{};
            std::vector<float> temperature_source{};
        } host;

        struct {
            float* density_data{nullptr};
            float* density_temp{nullptr};
            float* density_source{nullptr};
            float* temperature_data{nullptr};
            float* temperature_temp{nullptr};
            float* temperature_source{nullptr};
            std::array<float*, 3> force{};
            std::array<float*, 3> solid_velocity{};
            std::array<float*, 3> velocity{};
            std::array<float*, 3> temp_velocity{};
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
        } device;

        void create_device();
        void destroy_device() noexcept;
        void initialize_pressure_system();
        void solve_pressure(float delta_seconds);
        void reset_moved_from() noexcept;
    };
} // namespace spectra
