module;
#include "pyro.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <cusparse.h>

module xayah.projects.pyro;
import std;

namespace xayah::projects::pyro {
    namespace {
        constexpr std::uint32_t flow_boundary_periodic = 3u;

        void write_flow_face(const std::size_t index, const FlowBoundaryFace& face, std::array<std::uint32_t, 6>& types, std::array<float, 18>& velocity, std::array<float, 6>& pressure) {
            types[index]              = static_cast<std::uint32_t>(face.type);
            velocity[index * 3u + 0u] = face.velocity_x;
            velocity[index * 3u + 1u] = face.velocity_y;
            velocity[index * 3u + 2u] = face.velocity_z;
            pressure[index]           = face.pressure;
        }

        void write_scalar_face(const std::size_t index, const ScalarBoundaryFace& face, std::array<std::uint32_t, 6>& types, std::array<float, 6>& values) {
            types[index]  = static_cast<std::uint32_t>(face.type);
            values[index] = face.value;
        }

        void write_flow_boundary(const FlowBoundary& boundary, std::array<std::uint32_t, 6>& types, std::array<float, 18>& velocity, std::array<float, 6>& pressure) {
            write_flow_face(0u, boundary.x_minus, types, velocity, pressure);
            write_flow_face(1u, boundary.x_plus, types, velocity, pressure);
            write_flow_face(2u, boundary.y_minus, types, velocity, pressure);
            write_flow_face(3u, boundary.y_plus, types, velocity, pressure);
            write_flow_face(4u, boundary.z_minus, types, velocity, pressure);
            write_flow_face(5u, boundary.z_plus, types, velocity, pressure);
        }

        void write_scalar_boundary(const ScalarBoundary& boundary, std::array<std::uint32_t, 6>& types, std::array<float, 6>& values) {
            write_scalar_face(0u, boundary.x_minus, types, values);
            write_scalar_face(1u, boundary.x_plus, types, values);
            write_scalar_face(2u, boundary.y_minus, types, values);
            write_scalar_face(3u, boundary.y_plus, types, values);
            write_scalar_face(4u, boundary.z_minus, types, values);
            write_scalar_face(5u, boundary.z_plus, types, values);
        }

        bool paired_periodic(const FlowBoundaryFace& minus_face, const FlowBoundaryFace& plus_face) {
            return (minus_face.type == FlowBoundaryType::Periodic) == (plus_face.type == FlowBoundaryType::Periodic);
        }

        bool paired_periodic(const ScalarBoundaryFace& minus_face, const ScalarBoundaryFace& plus_face) {
            return (minus_face.type == ScalarBoundaryType::Periodic) == (plus_face.type == ScalarBoundaryType::Periodic);
        }

        void validate_config(const SolverConfiguration& configuration) {
            if (configuration.resolution[0] == 0 || configuration.resolution[1] == 0 || configuration.resolution[2] == 0) throw std::runtime_error("Pyro resolution must be positive");
            if (configuration.resolution[0] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || configuration.resolution[1] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || configuration.resolution[2] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) throw std::runtime_error("Pyro resolution exceeds CUDA solver int range");
            if (configuration.cell_size <= 0.0f) throw std::runtime_error("Pyro cell_size must be positive");
            if (configuration.pressure_iterations <= 0) throw std::runtime_error("Pyro pressure_iterations must be positive");
            if (!paired_periodic(configuration.flow_boundary.x_minus, configuration.flow_boundary.x_plus) || !paired_periodic(configuration.flow_boundary.y_minus, configuration.flow_boundary.y_plus) || !paired_periodic(configuration.flow_boundary.z_minus, configuration.flow_boundary.z_plus)) throw std::runtime_error("Pyro flow Periodic boundaries must be paired");
            if (!paired_periodic(configuration.density_boundary.x_minus, configuration.density_boundary.x_plus) || !paired_periodic(configuration.density_boundary.y_minus, configuration.density_boundary.y_plus) || !paired_periodic(configuration.density_boundary.z_minus, configuration.density_boundary.z_plus)) throw std::runtime_error("Pyro density Periodic boundaries must be paired");
            if (!paired_periodic(configuration.temperature_boundary.x_minus, configuration.temperature_boundary.x_plus) || !paired_periodic(configuration.temperature_boundary.y_minus, configuration.temperature_boundary.y_plus) || !paired_periodic(configuration.temperature_boundary.z_minus, configuration.temperature_boundary.z_plus)) throw std::runtime_error("Pyro temperature Periodic boundaries must be paired");

            const std::uint64_t cell_count = static_cast<std::uint64_t>(configuration.resolution[0]) * static_cast<std::uint64_t>(configuration.resolution[1]) * static_cast<std::uint64_t>(configuration.resolution[2]);
            if (cell_count == 0 || cell_count > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) throw std::runtime_error("Pyro cell count exceeds pressure solver int range");
        }

        void validate_source(const PlumeSource& source) {
            if (source.radius[0] <= 0.0f || source.radius[1] <= 0.0f || source.radius[2] <= 0.0f) throw std::runtime_error("Pyro plume source radius must be positive");
            if (source.density < 0.0f) throw std::runtime_error("Pyro plume source density must be non-negative");
            if (source.temperature < 0.0f) throw std::runtime_error("Pyro plume source temperature must be non-negative");
            if (source.falloff <= 0.0f) throw std::runtime_error("Pyro plume source falloff must be positive");
        }

        void check_cuda(const cudaError_t status, const char* what) {
            if (status == cudaSuccess) return;
            throw std::runtime_error(std::string{what} + ": " + cudaGetErrorString(status));
        }

        void check_cublas(const cublasStatus_t status, const char* what) {
            if (status == CUBLAS_STATUS_SUCCESS) return;
            throw std::runtime_error(what);
        }

        void check_cusparse(const cusparseStatus_t status, const char* what) {
            if (status == CUSPARSE_STATUS_SUCCESS) return;
            throw std::runtime_error(what);
        }

        unsigned ceil_div_u32(const std::uint64_t value, const std::uint64_t divisor) {
            return static_cast<unsigned>((value + divisor - 1u) / divisor);
        }

        int wrap_index(const int value, const int size) {
            const int remainder = value % size;
            return remainder < 0 ? remainder + size : remainder;
        }

        std::uint64_t index_3d(const int x, const int y, const int z, const int nx, const int ny) {
            return static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(nx) * (static_cast<std::uint64_t>(y) + static_cast<std::uint64_t>(ny) * static_cast<std::uint64_t>(z));
        }

        void add_pressure_column(std::array<int, 7>& row_columns, int& row_entry_count, const int column) {
            for (int entry = 0; entry < row_entry_count; ++entry) {
                if (row_columns[entry] == column) return;
            }
            row_columns[row_entry_count] = column;
            ++row_entry_count;
        }

        void add_pressure_neighbor(std::array<int, 7>& row_columns, int& row_entry_count, int next_x, int next_y, int next_z, const bool periodic_axis, const int nx, const int ny, const int nz) {
            if (next_x < 0 || next_x >= nx || next_y < 0 || next_y >= ny || next_z < 0 || next_z >= nz) {
                if (!periodic_axis) return;
                if (next_x < 0 || next_x >= nx) next_x = wrap_index(next_x, nx);
                if (next_y < 0 || next_y >= ny) next_y = wrap_index(next_y, ny);
                if (next_z < 0 || next_z >= nz) next_z = wrap_index(next_z, nz);
            }
            add_pressure_column(row_columns, row_entry_count, static_cast<int>(index_3d(next_x, next_y, next_z, nx, ny)));
        }
    } // namespace

    Solver::Solver(const SolverConfiguration& configuration) {
        validate_config(configuration);
        this->host_data.resolution                  = configuration.resolution;
        this->host_data.nx                          = static_cast<std::int32_t>(configuration.resolution[0]);
        this->host_data.ny                          = static_cast<std::int32_t>(configuration.resolution[1]);
        this->host_data.nz                          = static_cast<std::int32_t>(configuration.resolution[2]);
        this->host_data.cell_size                   = configuration.cell_size;
        this->host_data.pressure_iterations         = configuration.pressure_iterations;
        this->host_data.ambient_temperature         = configuration.ambient_temperature;
        this->host_data.buoyancy_density_factor     = configuration.buoyancy_density_factor;
        this->host_data.buoyancy_temperature_factor = configuration.buoyancy_temperature_factor;
        this->host_data.vorticity_confinement       = configuration.vorticity_confinement;
        this->host_data.scalar_advection_mode       = static_cast<std::uint32_t>(configuration.scalar_advection_mode);
        write_flow_boundary(configuration.flow_boundary, this->host_data.flow_boundary_types, this->host_data.flow_boundary_velocity, this->host_data.flow_boundary_pressure);
        write_scalar_boundary(configuration.density_boundary, this->host_data.density_boundary_types, this->host_data.density_boundary_values);
        write_scalar_boundary(configuration.temperature_boundary, this->host_data.temperature_boundary_types, this->host_data.temperature_boundary_values);
        this->host_data.cell_count        = static_cast<std::uint64_t>(configuration.resolution[0]) * static_cast<std::uint64_t>(configuration.resolution[1]) * static_cast<std::uint64_t>(configuration.resolution[2]);
        this->host_data.velocity_count[0] = static_cast<std::uint64_t>(configuration.resolution[0] + 1u) * static_cast<std::uint64_t>(configuration.resolution[1]) * static_cast<std::uint64_t>(configuration.resolution[2]);
        this->host_data.velocity_count[1] = static_cast<std::uint64_t>(configuration.resolution[0]) * static_cast<std::uint64_t>(configuration.resolution[1] + 1u) * static_cast<std::uint64_t>(configuration.resolution[2]);
        this->host_data.velocity_count[2] = static_cast<std::uint64_t>(configuration.resolution[0]) * static_cast<std::uint64_t>(configuration.resolution[1]) * static_cast<std::uint64_t>(configuration.resolution[2] + 1u);
        this->host_data.cell_bytes        = static_cast<std::size_t>(this->host_data.cell_count) * sizeof(float);
        this->host_data.velocity_bytes[0] = static_cast<std::size_t>(this->host_data.velocity_count[0]) * sizeof(float);
        this->host_data.velocity_bytes[1] = static_cast<std::size_t>(this->host_data.velocity_count[1]) * sizeof(float);
        this->host_data.velocity_bytes[2] = static_cast<std::size_t>(this->host_data.velocity_count[2]) * sizeof(float);
        this->host_data.density_source.resize(static_cast<std::size_t>(this->host_data.cell_count), 0.0f);
        this->host_data.temperature_source.resize(static_cast<std::size_t>(this->host_data.cell_count), 0.0f);
        this->allocate_device_data();
        this->set_plume_source(this->host_data.plume_source);
    }

    Solver::~Solver() noexcept {
        this->release_device_data();
    }

    Solver::Solver(Solver&& other) noexcept {
        this->host_data   = std::move(other.host_data);
        this->device_data = other.device_data;
        other.reset_moved_from();
    }

    Solver& Solver::operator=(Solver&& other) noexcept {
        if (this == &other) return *this;
        this->release_device_data();
        this->host_data   = std::move(other.host_data);
        this->device_data = other.device_data;
        other.reset_moved_from();
        return *this;
    }

    void Solver::reset() {
        this->release_device_data();
        this->allocate_device_data();
        check_cuda(cudaMemcpyAsync(this->device_data.density_source, this->host_data.density_source.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync density_source");
        check_cuda(cudaMemcpyAsync(this->device_data.temperature_source, this->host_data.temperature_source.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync temperature_source");
        if (!this->host_data.initial_density.empty()) check_cuda(cudaMemcpyAsync(this->device_data.density, this->host_data.initial_density.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync initial density");
        if (!this->host_data.initial_temperature.empty()) check_cuda(cudaMemcpyAsync(this->device_data.temperature, this->host_data.initial_temperature.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync initial temperature");
        check_cuda(cudaStreamSynchronize(this->host_data.cuda_stream), "cudaStreamSynchronize reset sources");
    }

    void Solver::set_initial_fields(const std::span<const float> density, const std::span<const float> temperature) {
        this->host_data.initial_density.assign(density.begin(), density.end());
        this->host_data.initial_temperature.assign(temperature.begin(), temperature.end());
        if (!this->host_data.initial_density.empty()) check_cuda(cudaMemcpyAsync(this->device_data.density, this->host_data.initial_density.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync initial density");
        if (!this->host_data.initial_temperature.empty()) check_cuda(cudaMemcpyAsync(this->device_data.temperature, this->host_data.initial_temperature.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync initial temperature");
        check_cuda(cudaStreamSynchronize(this->host_data.cuda_stream), "cudaStreamSynchronize initial fields");
    }

    void Solver::set_plume_source(const PlumeSource& source) {
        validate_source(source);
        this->host_data.plume_source = source;
        std::ranges::fill(this->host_data.density_source, 0.0f);
        std::ranges::fill(this->host_data.temperature_source, 0.0f);

        const std::uint32_t nx = this->host_data.resolution[0];
        const std::uint32_t ny = this->host_data.resolution[1];
        const std::uint32_t nz = this->host_data.resolution[2];
        const std::array extent{
            static_cast<float>(nx) * this->host_data.cell_size,
            static_cast<float>(ny) * this->host_data.cell_size,
            static_cast<float>(nz) * this->host_data.cell_size,
        };
        const std::array center{
            source.center[0] * extent[0],
            source.center[1] * extent[1],
            source.center[2] * extent[2],
        };
        const std::array radius{
            source.radius[0] * extent[0],
            source.radius[1] * extent[1],
            source.radius[2] * extent[2],
        };

        for (std::uint32_t z = 0; z < nz; ++z) {
            for (std::uint32_t y = 0; y < ny; ++y) {
                for (std::uint32_t x = 0; x < nx; ++x) {
                    const std::size_t index = static_cast<std::size_t>(x) + static_cast<std::size_t>(nx) * (static_cast<std::size_t>(y) + static_cast<std::size_t>(ny) * static_cast<std::size_t>(z));
                    const float px          = (static_cast<float>(x) + 0.5f) * this->host_data.cell_size;
                    const float py          = (static_cast<float>(y) + 0.5f) * this->host_data.cell_size;
                    const float pz          = (static_cast<float>(z) + 0.5f) * this->host_data.cell_size;
                    const float dx          = (px - center[0]) / radius[0];
                    const float dy          = (py - center[1]) / radius[1];
                    const float dz          = (pz - center[2]) / radius[2];
                    const float r2          = dx * dx + dy * dy + dz * dz;
                    if (r2 > 1.0f) continue;
                    const float plume                         = std::exp(-source.falloff * r2);
                    this->host_data.density_source[index]     = source.density * plume;
                    this->host_data.temperature_source[index] = source.temperature * plume;
                }
            }
        }

        check_cuda(cudaMemcpyAsync(this->device_data.density_source, this->host_data.density_source.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync density_source");
        check_cuda(cudaMemcpyAsync(this->device_data.temperature_source, this->host_data.temperature_source.data(), this->host_data.cell_bytes, cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync temperature_source");
        check_cuda(cudaStreamSynchronize(this->host_data.cuda_stream), "cudaStreamSynchronize plume source");
    }

    void Solver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("Pyro delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        const std::uint32_t* flow_types = this->host_data.flow_boundary_types.data();
        const float* flow_velocity      = this->host_data.flow_boundary_velocity.data();
        const float* flow_pressure      = this->host_data.flow_boundary_pressure.data();
        const std::array<bool, 3> periodic_axes{
            this->host_data.flow_boundary_types[0] == flow_boundary_periodic && this->host_data.flow_boundary_types[1] == flow_boundary_periodic,
            this->host_data.flow_boundary_types[2] == flow_boundary_periodic && this->host_data.flow_boundary_types[3] == flow_boundary_periodic,
            this->host_data.flow_boundary_types[4] == flow_boundary_periodic && this->host_data.flow_boundary_types[5] == flow_boundary_periodic,
        };

        cuda::launch_apply_solid_scalar(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature, this->device_data.occupancy, this->device_data.solid_temperature, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.ambient_temperature);
        cuda::launch_center_staggered_vector(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.centered_velocity[0], this->device_data.centered_velocity[1], this->device_data.centered_velocity[2], this->device_data.velocity[0], this->device_data.velocity[1], this->device_data.velocity[2], this->host_data.nx, this->host_data.ny, this->host_data.nz);
        cuda::launch_compute_vorticity(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.vorticity[0], this->device_data.vorticity[1], this->device_data.vorticity[2], this->device_data.vorticity_magnitude, this->device_data.centered_velocity[0], this->device_data.centered_velocity[1], this->device_data.centered_velocity[2], this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, flow_types, flow_velocity, flow_pressure);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.force[axis], 0.0f, this->host_data.cell_count);
        }
        cuda::launch_add_buoyancy(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.force[1], this->device_data.density, this->device_data.temperature, this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.ambient_temperature, this->host_data.buoyancy_density_factor, this->host_data.buoyancy_temperature_factor, flow_types, flow_velocity, flow_pressure);
        cuda::launch_add_vorticity_confinement(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.force[0], this->device_data.force[1], this->device_data.force[2], this->device_data.vorticity[0], this->device_data.vorticity[1], this->device_data.vorticity[2], this->device_data.vorticity_magnitude, this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, this->host_data.vorticity_confinement, flow_types, flow_velocity, flow_pressure);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            cuda::launch_add_center_force_to_staggered_component(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity[axis], this->device_data.force[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz, delta_seconds);
            cuda::launch_enforce_staggered_boundary(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity[axis], this->device_data.occupancy, this->device_data.solid_velocity[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic_axes[axis]) cuda::launch_sync_periodic_staggered_component(this->host_data.cuda_stream, this->host_data.sync_velocity_grid[axis], this->host_data.sync_block, axis, this->device_data.velocity[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz);
        }
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            cuda::launch_advect_staggered_component(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity_scratch[axis], this->device_data.velocity[axis], this->device_data.velocity[0], this->device_data.velocity[1], this->device_data.velocity[2], this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, delta_seconds, this->host_data.scalar_advection_mode, flow_types, flow_velocity, flow_pressure);
            cuda::launch_enforce_staggered_boundary(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity_scratch[axis], this->device_data.occupancy, this->device_data.solid_velocity[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic_axes[axis]) cuda::launch_sync_periodic_staggered_component(this->host_data.cuda_stream, this->host_data.sync_velocity_grid[axis], this->host_data.sync_block, axis, this->device_data.velocity_scratch[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz);
        }
        this->solve_pressure(delta_seconds);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            cuda::launch_project_staggered_component(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity_scratch[axis], this->device_data.pressure, this->device_data.occupancy, this->device_data.solid_velocity[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, delta_seconds, flow_types, flow_velocity, flow_pressure);
            cuda::launch_enforce_staggered_boundary(this->host_data.cuda_stream, this->host_data.velocity_cells[axis], this->host_data.block, axis, this->device_data.velocity_scratch[axis], this->device_data.occupancy, this->device_data.solid_velocity[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic_axes[axis]) cuda::launch_sync_periodic_staggered_component(this->host_data.cuda_stream, this->host_data.sync_velocity_grid[axis], this->host_data.sync_block, axis, this->device_data.velocity_scratch[axis], this->host_data.nx, this->host_data.ny, this->host_data.nz);
            check_cuda(cudaMemcpyAsync(this->device_data.velocity[axis], this->device_data.velocity_scratch[axis], this->host_data.velocity_bytes[axis], cudaMemcpyDeviceToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync velocity");
        }
        cuda::launch_add_scaled(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature_scratch, this->device_data.temperature, this->device_data.temperature_source, delta_seconds, this->host_data.cell_count);
        cuda::launch_advect_centered_scalar(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.temperature, this->device_data.temperature_scratch, this->device_data.velocity[0], this->device_data.velocity[1], this->device_data.velocity[2], this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, delta_seconds, this->host_data.scalar_advection_mode, this->host_data.temperature_boundary_types.data(), this->host_data.temperature_boundary_values.data(), flow_types, flow_velocity, flow_pressure);
        cuda::launch_apply_solid_scalar(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature, this->device_data.occupancy, this->device_data.solid_temperature, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.ambient_temperature);
        cuda::launch_add_scaled(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.density_scratch, this->device_data.density, this->device_data.density_source, delta_seconds, this->host_data.cell_count);
        cuda::launch_advect_centered_scalar(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.density, this->device_data.density_scratch, this->device_data.velocity[0], this->device_data.velocity[1], this->device_data.velocity[2], this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, delta_seconds, this->host_data.scalar_advection_mode, this->host_data.density_boundary_types.data(), this->host_data.density_boundary_values.data(), flow_types, flow_velocity, flow_pressure);
        cuda::launch_boundary_fill_centered_scalar(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.density_scratch, this->device_data.density, this->device_data.occupancy, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.density_boundary_types.data(), this->host_data.density_boundary_values.data());
        check_cuda(cudaMemcpyAsync(this->device_data.density, this->device_data.density_scratch, this->host_data.cell_bytes, cudaMemcpyDeviceToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync density");
    }

    CudaVolumeView Solver::cuda_volume() const noexcept {
        return {
            this->host_data.cuda_stream,
            this->host_data.resolution,
            this->device_data.density,
            this->device_data.temperature,
            this->device_data.centered_velocity[0],
            this->device_data.centered_velocity[1],
            this->device_data.centered_velocity[2],
            this->host_data.cell_count,
        };
    }

    void Solver::allocate_device_data() {
        if (this->host_data.cuda_stream != nullptr) throw std::runtime_error("Pyro device_data is already initialized");
        try {
            this->host_data.block                 = dim3(8u, 8u, 4u);
            this->host_data.cells                 = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny), this->host_data.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz), this->host_data.block.z));
            this->host_data.velocity_cells[0]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx + 1), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny), this->host_data.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz), this->host_data.block.z));
            this->host_data.velocity_cells[1]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny + 1), this->host_data.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz), this->host_data.block.z));
            this->host_data.velocity_cells[2]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny), this->host_data.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz + 1), this->host_data.block.z));
            this->host_data.sync_block            = dim3(this->host_data.block.x, this->host_data.block.y, 1u);
            this->host_data.sync_velocity_grid[0] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz), this->host_data.block.y), 1u);
            this->host_data.sync_velocity_grid[1] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nz), this->host_data.block.y), 1u);
            this->host_data.sync_velocity_grid[2] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host_data.nx), this->host_data.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host_data.ny), this->host_data.block.y), 1u);
            this->host_data.linear_grid           = ceil_div_u32(this->host_data.cell_count, 256u);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                this->host_data.velocity_linear_grid[axis] = ceil_div_u32(this->host_data.velocity_count[axis], 256u);
            }

            check_cuda(cudaStreamCreateWithFlags(&this->host_data.cuda_stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
            check_cublas(cublasCreate(&this->host_data.cublas), "cublasCreate");
            check_cublas(cublasSetStream(this->host_data.cublas, this->host_data.cuda_stream), "cublasSetStream");
            check_cublas(cublasSetPointerMode(this->host_data.cublas, CUBLAS_POINTER_MODE_DEVICE), "cublasSetPointerMode");
            check_cusparse(cusparseCreate(&this->host_data.cusparse), "cusparseCreate");
            check_cusparse(cusparseSetStream(this->host_data.cusparse, this->host_data.cuda_stream), "cusparseSetStream");

            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.velocity[axis]), this->host_data.velocity_bytes[axis]), "cudaMalloc velocity");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.velocity_scratch[axis]), this->host_data.velocity_bytes[axis]), "cudaMalloc velocity_scratch");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.centered_velocity[axis]), this->host_data.cell_bytes), "cudaMalloc centered_velocity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure), this->host_data.cell_bytes), "cudaMalloc pressure");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_rhs), this->host_data.cell_bytes), "cudaMalloc pressure_rhs");
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.vorticity[axis]), this->host_data.cell_bytes), "cudaMalloc vorticity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.vorticity_magnitude), this->host_data.cell_bytes), "cudaMalloc vorticity_magnitude");
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.force[axis]), this->host_data.cell_bytes), "cudaMalloc force");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.solid_velocity[axis]), this->host_data.cell_bytes), "cudaMalloc solid_velocity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.occupancy), this->host_data.cell_count * sizeof(std::uint8_t)), "cudaMalloc occupancy");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.solid_temperature), this->host_data.cell_bytes), "cudaMalloc solid_temperature");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.density), this->host_data.cell_bytes), "cudaMalloc density");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.density_scratch), this->host_data.cell_bytes), "cudaMalloc density_scratch");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.density_source), this->host_data.cell_bytes), "cudaMalloc density_source");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.temperature), this->host_data.cell_bytes), "cudaMalloc temperature");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.temperature_scratch), this->host_data.cell_bytes), "cudaMalloc temperature_scratch");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.temperature_source), this->host_data.cell_bytes), "cudaMalloc temperature_source");

            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.velocity_linear_grid[axis], 256u, this->device_data.velocity[axis], 0.0f, this->host_data.velocity_count[axis]);
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.velocity_linear_grid[axis], 256u, this->device_data.velocity_scratch[axis], 0.0f, this->host_data.velocity_count[axis]);
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.centered_velocity[axis], 0.0f, this->host_data.cell_count);
            }
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.pressure, 0.0f, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.pressure_rhs, 0.0f, this->host_data.cell_count);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.vorticity[axis], 0.0f, this->host_data.cell_count);
            }
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.vorticity_magnitude, 0.0f, this->host_data.cell_count);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.force[axis], 0.0f, this->host_data.cell_count);
                cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.solid_velocity[axis], 0.0f, this->host_data.cell_count);
            }
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.solid_temperature, this->host_data.ambient_temperature, this->host_data.cell_count);
            check_cuda(cudaMemsetAsync(this->device_data.occupancy, 0, this->host_data.cell_count * sizeof(std::uint8_t), this->host_data.cuda_stream), "cudaMemsetAsync occupancy");
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.density, 0.0f, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.density_scratch, 0.0f, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.density_source, 0.0f, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature, this->host_data.ambient_temperature, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature_scratch, this->host_data.ambient_temperature, this->host_data.cell_count);
            cuda::launch_fill_float(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.temperature_source, 0.0f, this->host_data.cell_count);
            this->initialize_pressure_system();
        } catch (...) {
            this->release_device_data();
            throw;
        }
    }

    void Solver::release_device_data() noexcept {
        try {
            if (this->host_data.cuda_stream != nullptr) cudaStreamSynchronize(this->host_data.cuda_stream);
            if (this->host_data.pressure_matrix != nullptr) cusparseDestroySpMat(this->host_data.pressure_matrix);
            if (this->host_data.pressure_vec_p != nullptr) cusparseDestroyDnVec(this->host_data.pressure_vec_p);
            if (this->host_data.pressure_vec_ap != nullptr) cusparseDestroyDnVec(this->host_data.pressure_vec_ap);
            if (this->host_data.cublas != nullptr) cublasDestroy(this->host_data.cublas);
            if (this->host_data.cusparse != nullptr) cusparseDestroy(this->host_data.cusparse);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device_data.velocity[axis] != nullptr) cudaFree(this->device_data.velocity[axis]);
                if (this->device_data.velocity_scratch[axis] != nullptr) cudaFree(this->device_data.velocity_scratch[axis]);
                if (this->device_data.centered_velocity[axis] != nullptr) cudaFree(this->device_data.centered_velocity[axis]);
            }
            if (this->device_data.pressure != nullptr) cudaFree(this->device_data.pressure);
            if (this->device_data.pressure_rhs != nullptr) cudaFree(this->device_data.pressure_rhs);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device_data.vorticity[axis] != nullptr) cudaFree(this->device_data.vorticity[axis]);
            }
            if (this->device_data.vorticity_magnitude != nullptr) cudaFree(this->device_data.vorticity_magnitude);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device_data.force[axis] != nullptr) cudaFree(this->device_data.force[axis]);
                if (this->device_data.solid_velocity[axis] != nullptr) cudaFree(this->device_data.solid_velocity[axis]);
            }
            if (this->device_data.pressure_anchor != nullptr) cudaFree(this->device_data.pressure_anchor);
            if (this->device_data.pressure_row_offsets != nullptr) cudaFree(this->device_data.pressure_row_offsets);
            if (this->device_data.pressure_column_indices != nullptr) cudaFree(this->device_data.pressure_column_indices);
            if (this->device_data.pressure_values != nullptr) cudaFree(this->device_data.pressure_values);
            if (this->device_data.pcg_r != nullptr) cudaFree(this->device_data.pcg_r);
            if (this->device_data.pcg_p != nullptr) cudaFree(this->device_data.pcg_p);
            if (this->device_data.pcg_ap != nullptr) cudaFree(this->device_data.pcg_ap);
            if (this->device_data.pressure_dot_rz != nullptr) cudaFree(this->device_data.pressure_dot_rz);
            if (this->device_data.pressure_dot_pap != nullptr) cudaFree(this->device_data.pressure_dot_pap);
            if (this->device_data.pressure_dot_rr != nullptr) cudaFree(this->device_data.pressure_dot_rr);
            if (this->device_data.pressure_alpha != nullptr) cudaFree(this->device_data.pressure_alpha);
            if (this->device_data.pressure_negative_alpha != nullptr) cudaFree(this->device_data.pressure_negative_alpha);
            if (this->device_data.pressure_beta != nullptr) cudaFree(this->device_data.pressure_beta);
            if (this->device_data.pressure_one != nullptr) cudaFree(this->device_data.pressure_one);
            if (this->device_data.occupancy != nullptr) cudaFree(this->device_data.occupancy);
            if (this->device_data.solid_temperature != nullptr) cudaFree(this->device_data.solid_temperature);
            if (this->device_data.spmv_buffer != nullptr) cudaFree(this->device_data.spmv_buffer);
            if (this->device_data.density != nullptr) cudaFree(this->device_data.density);
            if (this->device_data.density_scratch != nullptr) cudaFree(this->device_data.density_scratch);
            if (this->device_data.density_source != nullptr) cudaFree(this->device_data.density_source);
            if (this->device_data.temperature != nullptr) cudaFree(this->device_data.temperature);
            if (this->device_data.temperature_scratch != nullptr) cudaFree(this->device_data.temperature_scratch);
            if (this->device_data.temperature_source != nullptr) cudaFree(this->device_data.temperature_source);
            if (this->host_data.cuda_stream != nullptr) cudaStreamDestroy(this->host_data.cuda_stream);
        } catch (...) {
        }
        this->host_data.cuda_stream      = nullptr;
        this->host_data.cublas           = nullptr;
        this->host_data.cusparse         = nullptr;
        this->host_data.pressure_matrix  = nullptr;
        this->host_data.pressure_vec_p   = nullptr;
        this->host_data.pressure_vec_ap  = nullptr;
        this->host_data.spmv_buffer_size = 0;
        this->device_data                = {};
    }

    void Solver::initialize_pressure_system() {
        const int cells       = static_cast<int>(this->host_data.cell_count);
        const bool periodic_x = this->host_data.flow_boundary_types[0] == flow_boundary_periodic && this->host_data.flow_boundary_types[1] == flow_boundary_periodic;
        const bool periodic_y = this->host_data.flow_boundary_types[2] == flow_boundary_periodic && this->host_data.flow_boundary_types[3] == flow_boundary_periodic;
        const bool periodic_z = this->host_data.flow_boundary_types[4] == flow_boundary_periodic && this->host_data.flow_boundary_types[5] == flow_boundary_periodic;
        std::vector<int> host_row_offsets(static_cast<std::size_t>(cells) + 1u, 0);
        std::vector<int> host_column_indices{};
        host_column_indices.reserve(static_cast<std::size_t>(cells) * 7u);

        for (int row = 0; row < cells; ++row) {
            host_row_offsets[static_cast<std::size_t>(row)] = static_cast<int>(host_column_indices.size());
            const int x                                     = row % this->host_data.nx;
            const int yz                                    = row / this->host_data.nx;
            const int y                                     = yz % this->host_data.ny;
            const int z                                     = yz / this->host_data.ny;
            std::array<int, 7> row_columns{};
            int row_entry_count = 0;
            add_pressure_neighbor(row_columns, row_entry_count, x - 1, y, z, periodic_x, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x + 1, y, z, periodic_x, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y - 1, z, periodic_y, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y + 1, z, periodic_y, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y, z - 1, periodic_z, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y, z + 1, periodic_z, this->host_data.nx, this->host_data.ny, this->host_data.nz);
            add_pressure_column(row_columns, row_entry_count, row);
            for (int left = 0; left < row_entry_count; ++left) {
                for (int right = left + 1; right < row_entry_count; ++right) {
                    if (row_columns[right] < row_columns[left]) {
                        const int swapped_column = row_columns[left];
                        row_columns[left]        = row_columns[right];
                        row_columns[right]       = swapped_column;
                    }
                }
            }
            for (int entry = 0; entry < row_entry_count; ++entry) {
                host_column_indices.push_back(row_columns[entry]);
            }
        }

        host_row_offsets[static_cast<std::size_t>(cells)] = static_cast<int>(host_column_indices.size());
        const int pressure_nnz                            = static_cast<int>(host_column_indices.size());
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_anchor), sizeof(int)), "cudaMalloc pressure_anchor");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_row_offsets), static_cast<std::size_t>(cells + 1) * sizeof(int)), "cudaMalloc pressure_row_offsets");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_column_indices), static_cast<std::size_t>(pressure_nnz) * sizeof(int)), "cudaMalloc pressure_column_indices");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_values), static_cast<std::size_t>(pressure_nnz) * sizeof(float)), "cudaMalloc pressure_values");
        check_cuda(cudaMemcpyAsync(this->device_data.pressure_row_offsets, host_row_offsets.data(), static_cast<std::size_t>(cells + 1) * sizeof(int), cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync pressure_row_offsets");
        check_cuda(cudaMemcpyAsync(this->device_data.pressure_column_indices, host_column_indices.data(), static_cast<std::size_t>(pressure_nnz) * sizeof(int), cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync pressure_column_indices");
        check_cuda(cudaMemsetAsync(this->device_data.pressure_values, 0, static_cast<std::size_t>(pressure_nnz) * sizeof(float), this->host_data.cuda_stream), "cudaMemsetAsync pressure_values");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pcg_r), this->host_data.cell_bytes), "cudaMalloc pcg_r");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pcg_p), this->host_data.cell_bytes), "cudaMalloc pcg_p");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pcg_ap), this->host_data.cell_bytes), "cudaMalloc pcg_ap");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_dot_rz), sizeof(float)), "cudaMalloc pressure_dot_rz");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_dot_pap), sizeof(float)), "cudaMalloc pressure_dot_pap");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_dot_rr), sizeof(float)), "cudaMalloc pressure_dot_rr");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_alpha), sizeof(float)), "cudaMalloc pressure_alpha");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_negative_alpha), sizeof(float)), "cudaMalloc pressure_negative_alpha");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_beta), sizeof(float)), "cudaMalloc pressure_beta");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.pressure_one), sizeof(float)), "cudaMalloc pressure_one");
        constexpr float one = 1.0f;
        check_cuda(cudaMemcpyAsync(this->device_data.pressure_one, &one, sizeof(float), cudaMemcpyHostToDevice, this->host_data.cuda_stream), "cudaMemcpyAsync pressure_one");
        check_cuda(cudaMemsetAsync(this->device_data.pcg_r, 0, this->host_data.cell_bytes, this->host_data.cuda_stream), "cudaMemsetAsync pcg_r");
        check_cuda(cudaMemsetAsync(this->device_data.pcg_p, 0, this->host_data.cell_bytes, this->host_data.cuda_stream), "cudaMemsetAsync pcg_p");
        check_cuda(cudaMemsetAsync(this->device_data.pcg_ap, 0, this->host_data.cell_bytes, this->host_data.cuda_stream), "cudaMemsetAsync pcg_ap");
        check_cuda(cudaStreamSynchronize(this->host_data.cuda_stream), "cudaStreamSynchronize pressure_system_upload");
        check_cusparse(cusparseCreateCsr(&this->host_data.pressure_matrix, cells, cells, pressure_nnz, this->device_data.pressure_row_offsets, this->device_data.pressure_column_indices, this->device_data.pressure_values, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_32F), "cusparseCreateCsr matrix");
        check_cusparse(cusparseCreateDnVec(&this->host_data.pressure_vec_p, cells, this->device_data.pcg_p, CUDA_R_32F), "cusparseCreateDnVec vec_p");
        check_cusparse(cusparseCreateDnVec(&this->host_data.pressure_vec_ap, cells, this->device_data.pcg_ap, CUDA_R_32F), "cusparseCreateDnVec vec_ap");
        constexpr float spmv_alpha = 1.0f;
        constexpr float spmv_beta  = 0.0f;
        check_cusparse(cusparseSpMV_bufferSize(this->host_data.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &spmv_alpha, this->host_data.pressure_matrix, this->host_data.pressure_vec_p, &spmv_beta, this->host_data.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, &this->host_data.spmv_buffer_size), "cusparseSpMV_bufferSize");
        if (this->host_data.spmv_buffer_size > 0) check_cuda(cudaMalloc(&this->device_data.spmv_buffer, this->host_data.spmv_buffer_size), "cudaMalloc spmv_buffer");
        check_cusparse(cusparseSpMV_preprocess(this->host_data.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &spmv_alpha, this->host_data.pressure_matrix, this->host_data.pressure_vec_p, &spmv_beta, this->host_data.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, this->device_data.spmv_buffer), "cusparseSpMV_preprocess");
    }

    void Solver::solve_pressure(const float delta_seconds) {
        const std::uint32_t* flow_types = this->host_data.flow_boundary_types.data();
        const float* flow_velocity      = this->host_data.flow_boundary_velocity.data();
        const float* flow_pressure      = this->host_data.flow_boundary_pressure.data();
        cuda::launch_fill_int(this->host_data.cuda_stream, 1u, 1u, this->device_data.pressure_anchor, static_cast<int>(this->host_data.cell_count), 1u);
        cuda::launch_find_pressure_anchor(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.pressure_anchor, this->device_data.occupancy, this->host_data.cell_count);
        cuda::launch_compute_projection_rhs(this->host_data.cuda_stream, this->host_data.cells, this->host_data.block, this->device_data.pressure_rhs, this->device_data.velocity_scratch[0], this->device_data.velocity_scratch[1], this->device_data.velocity_scratch[2], this->device_data.occupancy, this->device_data.pressure_anchor, this->host_data.nx, this->host_data.ny, this->host_data.nz, this->host_data.cell_size, delta_seconds, flow_types, flow_velocity, flow_pressure);
        cuda::launch_build_projection_matrix(this->host_data.cuda_stream, this->host_data.linear_grid, 256u, this->device_data.pressure_values, this->device_data.pressure_row_offsets, this->device_data.pressure_column_indices, this->device_data.occupancy, this->device_data.pressure_anchor, this->host_data.nx, this->host_data.ny, this->host_data.nz, flow_types, flow_velocity, flow_pressure);
        check_cuda(cudaMemsetAsync(this->device_data.pressure, 0, this->host_data.cell_bytes, this->host_data.cuda_stream), "cudaMemsetAsync pressure");
        check_cublas(cublasScopy(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pressure_rhs, 1, this->device_data.pcg_r, 1), "cublasScopy rhs");
        check_cublas(cublasScopy(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pcg_r, 1, this->device_data.pcg_p, 1), "cublasScopy pcg_p");
        check_cublas(cublasSdot(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pcg_r, 1, this->device_data.pcg_r, 1, this->device_data.pressure_dot_rz), "cublasSdot pressure_dot_rz");
        constexpr float one  = 1.0f;
        constexpr float zero = 0.0f;
        for (int iteration = 0; iteration < this->host_data.pressure_iterations; ++iteration) {
            check_cusparse(cusparseSpMV(this->host_data.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &one, this->host_data.pressure_matrix, this->host_data.pressure_vec_p, &zero, this->host_data.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, this->device_data.spmv_buffer), "cusparseSpMV");
            check_cublas(cublasSdot(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pcg_p, 1, this->device_data.pcg_ap, 1, this->device_data.pressure_dot_pap), "cublasSdot pressure_dot_pap");
            cuda::launch_compute_ratio(this->host_data.cuda_stream, this->device_data.pressure_alpha, this->device_data.pressure_dot_rz, this->device_data.pressure_dot_pap);
            check_cublas(cublasSaxpy(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pressure_alpha, this->device_data.pcg_p, 1, this->device_data.pressure, 1), "cublasSaxpy pressure");
            cuda::launch_negate_scalar(this->host_data.cuda_stream, this->device_data.pressure_negative_alpha, this->device_data.pressure_alpha);
            check_cublas(cublasSaxpy(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pressure_negative_alpha, this->device_data.pcg_ap, 1, this->device_data.pcg_r, 1), "cublasSaxpy pcg_r");
            check_cublas(cublasSdot(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pcg_r, 1, this->device_data.pcg_r, 1, this->device_data.pressure_dot_rr), "cublasSdot rho_new");
            cuda::launch_compute_ratio(this->host_data.cuda_stream, this->device_data.pressure_beta, this->device_data.pressure_dot_rr, this->device_data.pressure_dot_rz);
            check_cublas(cublasSscal(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pressure_beta, this->device_data.pcg_p, 1), "cublasSscal pcg_p");
            check_cublas(cublasSaxpy(this->host_data.cublas, static_cast<int>(this->host_data.cell_count), this->device_data.pressure_one, this->device_data.pcg_r, 1, this->device_data.pcg_p, 1), "cublasSaxpy pcg_p");
            check_cublas(cublasScopy(this->host_data.cublas, 1, this->device_data.pressure_dot_rr, 1, this->device_data.pressure_dot_rz, 1), "cublasScopy rho");
        }
    }

    void Solver::reset_moved_from() noexcept {
        this->host_data.cuda_stream      = nullptr;
        this->host_data.cublas           = nullptr;
        this->host_data.cusparse         = nullptr;
        this->host_data.pressure_matrix  = nullptr;
        this->host_data.pressure_vec_p   = nullptr;
        this->host_data.pressure_vec_ap  = nullptr;
        this->host_data.spmv_buffer_size = 0;
        this->device_data                = {};
    }
} // namespace xayah::projects::pyro
