module;
#include "pyro.h"

#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <cusparse.h>

module pyro;
import std;

namespace {
    constexpr std::uint32_t flow_boundary_periodic = 3u;

    void write_flow_face(const std::size_t index, const spectra::PyroFlowBoundaryFace& face, std::array<std::uint32_t, 6>& types, std::array<float, 18>& velocity, std::array<float, 6>& pressure) {
        types[index]              = static_cast<std::uint32_t>(face.type);
        velocity[index * 3u + 0u] = face.velocity_x;
        velocity[index * 3u + 1u] = face.velocity_y;
        velocity[index * 3u + 2u] = face.velocity_z;
        pressure[index]           = face.pressure;
    }

    void write_scalar_face(const std::size_t index, const spectra::PyroScalarBoundaryFace& face, std::array<std::uint32_t, 6>& types, std::array<float, 6>& values) {
        types[index]  = static_cast<std::uint32_t>(face.type);
        values[index] = face.value;
    }

    void write_flow_boundary(const spectra::PyroFlowBoundary& boundary, std::array<std::uint32_t, 6>& types, std::array<float, 18>& velocity, std::array<float, 6>& pressure) {
        write_flow_face(0u, boundary.x_minus, types, velocity, pressure);
        write_flow_face(1u, boundary.x_plus, types, velocity, pressure);
        write_flow_face(2u, boundary.y_minus, types, velocity, pressure);
        write_flow_face(3u, boundary.y_plus, types, velocity, pressure);
        write_flow_face(4u, boundary.z_minus, types, velocity, pressure);
        write_flow_face(5u, boundary.z_plus, types, velocity, pressure);
    }

    void write_scalar_boundary(const spectra::PyroScalarBoundary& boundary, std::array<std::uint32_t, 6>& types, std::array<float, 6>& values) {
        write_scalar_face(0u, boundary.x_minus, types, values);
        write_scalar_face(1u, boundary.x_plus, types, values);
        write_scalar_face(2u, boundary.y_minus, types, values);
        write_scalar_face(3u, boundary.y_plus, types, values);
        write_scalar_face(4u, boundary.z_minus, types, values);
        write_scalar_face(5u, boundary.z_plus, types, values);
    }

    bool paired_periodic(const spectra::PyroFlowBoundaryFace& minus_face, const spectra::PyroFlowBoundaryFace& plus_face) {
        return (minus_face.type == spectra::PyroFlowBoundaryType::periodic) == (plus_face.type == spectra::PyroFlowBoundaryType::periodic);
    }

    bool paired_periodic(const spectra::PyroScalarBoundaryFace& minus_face, const spectra::PyroScalarBoundaryFace& plus_face) {
        return (minus_face.type == spectra::PyroScalarBoundaryType::periodic) == (plus_face.type == spectra::PyroScalarBoundaryType::periodic);
    }

    void validate_config(const spectra::PyroConfig& config) {
        if (config.resolution[0] == 0 || config.resolution[1] == 0 || config.resolution[2] == 0) throw std::runtime_error("Pyro resolution must be positive");
        if (config.resolution[0] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || config.resolution[1] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) || config.resolution[2] > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) throw std::runtime_error("Pyro resolution exceeds CUDA solver int range");
        if (config.cell_size <= 0.0f) throw std::runtime_error("Pyro cell_size must be positive");
        if (config.pressure_iterations <= 0) throw std::runtime_error("Pyro pressure_iterations must be positive");
        if (!paired_periodic(config.flow_boundary.x_minus, config.flow_boundary.x_plus) || !paired_periodic(config.flow_boundary.y_minus, config.flow_boundary.y_plus) || !paired_periodic(config.flow_boundary.z_minus, config.flow_boundary.z_plus)) throw std::runtime_error("Pyro flow periodic boundaries must be paired");
        if (!paired_periodic(config.density_boundary.x_minus, config.density_boundary.x_plus) || !paired_periodic(config.density_boundary.y_minus, config.density_boundary.y_plus) || !paired_periodic(config.density_boundary.z_minus, config.density_boundary.z_plus)) throw std::runtime_error("Pyro density periodic boundaries must be paired");
        if (!paired_periodic(config.temperature_boundary.x_minus, config.temperature_boundary.x_plus) || !paired_periodic(config.temperature_boundary.y_minus, config.temperature_boundary.y_plus) || !paired_periodic(config.temperature_boundary.z_minus, config.temperature_boundary.z_plus)) throw std::runtime_error("Pyro temperature periodic boundaries must be paired");

        const std::uint64_t cell_count = static_cast<std::uint64_t>(config.resolution[0]) * static_cast<std::uint64_t>(config.resolution[1]) * static_cast<std::uint64_t>(config.resolution[2]);
        if (cell_count == 0 || cell_count > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) throw std::runtime_error("Pyro cell count exceeds pressure solver int range");
    }

    void validate_source(const spectra::PyroPlumeSource& source) {
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

namespace spectra {
    PyroSolver::PyroSolver(const PyroConfig& config) {
        validate_config(config);
        this->host.resolution                  = config.resolution;
        this->host.nx                          = static_cast<std::int32_t>(config.resolution[0]);
        this->host.ny                          = static_cast<std::int32_t>(config.resolution[1]);
        this->host.nz                          = static_cast<std::int32_t>(config.resolution[2]);
        this->host.cell_size                   = config.cell_size;
        this->host.pressure_iterations         = config.pressure_iterations;
        this->host.ambient_temperature         = config.ambient_temperature;
        this->host.buoyancy_density_factor     = config.buoyancy_density_factor;
        this->host.buoyancy_temperature_factor = config.buoyancy_temperature_factor;
        this->host.vorticity_confinement       = config.vorticity_confinement;
        this->host.scalar_advection_mode       = static_cast<std::uint32_t>(config.scalar_advection_mode);
        write_flow_boundary(config.flow_boundary, this->host.flow_boundary_types, this->host.flow_boundary_velocity, this->host.flow_boundary_pressure);
        write_scalar_boundary(config.density_boundary, this->host.density_boundary_types, this->host.density_boundary_values);
        write_scalar_boundary(config.temperature_boundary, this->host.temperature_boundary_types, this->host.temperature_boundary_values);
        this->host.cell_count        = static_cast<std::uint64_t>(config.resolution[0]) * static_cast<std::uint64_t>(config.resolution[1]) * static_cast<std::uint64_t>(config.resolution[2]);
        this->host.velocity_count[0] = static_cast<std::uint64_t>(config.resolution[0] + 1u) * static_cast<std::uint64_t>(config.resolution[1]) * static_cast<std::uint64_t>(config.resolution[2]);
        this->host.velocity_count[1] = static_cast<std::uint64_t>(config.resolution[0]) * static_cast<std::uint64_t>(config.resolution[1] + 1u) * static_cast<std::uint64_t>(config.resolution[2]);
        this->host.velocity_count[2] = static_cast<std::uint64_t>(config.resolution[0]) * static_cast<std::uint64_t>(config.resolution[1]) * static_cast<std::uint64_t>(config.resolution[2] + 1u);
        this->host.cell_bytes        = static_cast<std::size_t>(this->host.cell_count) * sizeof(float);
        this->host.velocity_bytes[0] = static_cast<std::size_t>(this->host.velocity_count[0]) * sizeof(float);
        this->host.velocity_bytes[1] = static_cast<std::size_t>(this->host.velocity_count[1]) * sizeof(float);
        this->host.velocity_bytes[2] = static_cast<std::size_t>(this->host.velocity_count[2]) * sizeof(float);
        this->host.density_source.resize(static_cast<std::size_t>(this->host.cell_count), 0.0f);
        this->host.temperature_source.resize(static_cast<std::size_t>(this->host.cell_count), 0.0f);
        this->create_device();
        this->set_plume_source(this->host.plume_source);
    }

    PyroSolver::~PyroSolver() noexcept {
        this->destroy_device();
    }

    PyroSolver::PyroSolver(PyroSolver&& other) noexcept {
        this->host   = std::move(other.host);
        this->device = other.device;
        other.reset_moved_from();
    }

    PyroSolver& PyroSolver::operator=(PyroSolver&& other) noexcept {
        if (this == &other) return *this;
        this->destroy_device();
        this->host   = std::move(other.host);
        this->device = other.device;
        other.reset_moved_from();
        return *this;
    }

    void PyroSolver::reset() {
        this->destroy_device();
        this->create_device();
        check_cuda(cudaMemcpyAsync(this->device.density_source, this->host.density_source.data(), this->host.cell_bytes, cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync density_source");
        check_cuda(cudaMemcpyAsync(this->device.temperature_source, this->host.temperature_source.data(), this->host.cell_bytes, cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync temperature_source");
        check_cuda(cudaStreamSynchronize(this->host.stream), "cudaStreamSynchronize reset sources");
    }

    void PyroSolver::set_plume_source(const PyroPlumeSource& source) {
        validate_source(source);
        this->host.plume_source = source;
        std::ranges::fill(this->host.density_source, 0.0f);
        std::ranges::fill(this->host.temperature_source, 0.0f);

        const std::uint32_t nx = this->host.resolution[0];
        const std::uint32_t ny = this->host.resolution[1];
        const std::uint32_t nz = this->host.resolution[2];
        const std::array extent{
            static_cast<float>(nx) * this->host.cell_size,
            static_cast<float>(ny) * this->host.cell_size,
            static_cast<float>(nz) * this->host.cell_size,
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
                    const float px          = (static_cast<float>(x) + 0.5f) * this->host.cell_size;
                    const float py          = (static_cast<float>(y) + 0.5f) * this->host.cell_size;
                    const float pz          = (static_cast<float>(z) + 0.5f) * this->host.cell_size;
                    const float dx          = (px - center[0]) / radius[0];
                    const float dy          = (py - center[1]) / radius[1];
                    const float dz          = (pz - center[2]) / radius[2];
                    const float r2          = dx * dx + dy * dy + dz * dz;
                    if (r2 > 1.0f) continue;
                    const float plume                    = std::exp(-source.falloff * r2);
                    this->host.density_source[index]     = source.density * plume;
                    this->host.temperature_source[index] = source.temperature * plume;
                }
            }
        }

        check_cuda(cudaMemcpyAsync(this->device.density_source, this->host.density_source.data(), this->host.cell_bytes, cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync density_source");
        check_cuda(cudaMemcpyAsync(this->device.temperature_source, this->host.temperature_source.data(), this->host.cell_bytes, cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync temperature_source");
        check_cuda(cudaStreamSynchronize(this->host.stream), "cudaStreamSynchronize plume source");
    }

    void PyroSolver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("Pyro delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        const std::uint32_t* flow_types = this->host.flow_boundary_types.data();
        const float* flow_velocity      = this->host.flow_boundary_velocity.data();
        const float* flow_pressure      = this->host.flow_boundary_pressure.data();
        const std::array<bool, 3> periodic{
            this->host.flow_boundary_types[0] == flow_boundary_periodic && this->host.flow_boundary_types[1] == flow_boundary_periodic,
            this->host.flow_boundary_types[2] == flow_boundary_periodic && this->host.flow_boundary_types[3] == flow_boundary_periodic,
            this->host.flow_boundary_types[4] == flow_boundary_periodic && this->host.flow_boundary_types[5] == flow_boundary_periodic,
        };

        pyro_cuda::launch_apply_solid_scalar(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_data, this->device.occupancy, this->device.solid_temperature, this->host.nx, this->host.ny, this->host.nz, this->host.ambient_temperature);
        pyro_cuda::launch_center_staggered_vector(this->host.stream, this->host.cells, this->host.block, this->device.centered_velocity[0], this->device.centered_velocity[1], this->device.centered_velocity[2], this->device.velocity[0], this->device.velocity[1], this->device.velocity[2], this->host.nx, this->host.ny, this->host.nz);
        pyro_cuda::launch_compute_vorticity(this->host.stream, this->host.cells, this->host.block, this->device.vorticity[0], this->device.vorticity[1], this->device.vorticity[2], this->device.vorticity_magnitude, this->device.centered_velocity[0], this->device.centered_velocity[1], this->device.centered_velocity[2], this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, flow_types, flow_velocity, flow_pressure);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.force[axis], 0.0f, this->host.cell_count);
        }
        pyro_cuda::launch_add_buoyancy(this->host.stream, this->host.cells, this->host.block, this->device.force[1], this->device.density_data, this->device.temperature_data, this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.ambient_temperature, this->host.buoyancy_density_factor, this->host.buoyancy_temperature_factor, flow_types, flow_velocity, flow_pressure);
        pyro_cuda::launch_add_vorticity_confinement(this->host.stream, this->host.cells, this->host.block, this->device.force[0], this->device.force[1], this->device.force[2], this->device.vorticity[0], this->device.vorticity[1], this->device.vorticity[2], this->device.vorticity_magnitude, this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, this->host.vorticity_confinement, flow_types, flow_velocity, flow_pressure);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            pyro_cuda::launch_add_center_force_to_staggered_component(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.velocity[axis], this->device.force[axis], this->host.nx, this->host.ny, this->host.nz, delta_seconds);
            pyro_cuda::launch_enforce_staggered_boundary(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.velocity[axis], this->device.occupancy, this->device.solid_velocity[axis], this->host.nx, this->host.ny, this->host.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic[axis]) pyro_cuda::launch_sync_periodic_staggered_component(this->host.stream, this->host.sync_velocity_grid[axis], this->host.sync_block, axis, this->device.velocity[axis], this->host.nx, this->host.ny, this->host.nz);
        }
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            pyro_cuda::launch_advect_staggered_component(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.temp_velocity[axis], this->device.velocity[axis], this->device.velocity[0], this->device.velocity[1], this->device.velocity[2], this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, delta_seconds, this->host.scalar_advection_mode, flow_types, flow_velocity, flow_pressure);
            pyro_cuda::launch_enforce_staggered_boundary(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.temp_velocity[axis], this->device.occupancy, this->device.solid_velocity[axis], this->host.nx, this->host.ny, this->host.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic[axis]) pyro_cuda::launch_sync_periodic_staggered_component(this->host.stream, this->host.sync_velocity_grid[axis], this->host.sync_block, axis, this->device.temp_velocity[axis], this->host.nx, this->host.ny, this->host.nz);
        }
        this->solve_pressure(delta_seconds);
        for (std::uint32_t axis = 0; axis < 3u; ++axis) {
            pyro_cuda::launch_project_staggered_component(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.temp_velocity[axis], this->device.pressure, this->device.occupancy, this->device.solid_velocity[axis], this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, delta_seconds, flow_types, flow_velocity, flow_pressure);
            pyro_cuda::launch_enforce_staggered_boundary(this->host.stream, this->host.velocity_cells[axis], this->host.block, axis, this->device.temp_velocity[axis], this->device.occupancy, this->device.solid_velocity[axis], this->host.nx, this->host.ny, this->host.nz, flow_types, flow_velocity, flow_pressure);
            if (periodic[axis]) pyro_cuda::launch_sync_periodic_staggered_component(this->host.stream, this->host.sync_velocity_grid[axis], this->host.sync_block, axis, this->device.temp_velocity[axis], this->host.nx, this->host.ny, this->host.nz);
            check_cuda(cudaMemcpyAsync(this->device.velocity[axis], this->device.temp_velocity[axis], this->host.velocity_bytes[axis], cudaMemcpyDeviceToDevice, this->host.stream), "cudaMemcpyAsync velocity");
        }
        pyro_cuda::launch_add_scaled(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_temp, this->device.temperature_data, this->device.temperature_source, delta_seconds, this->host.cell_count);
        pyro_cuda::launch_advect_centered_scalar(this->host.stream, this->host.cells, this->host.block, this->device.temperature_data, this->device.temperature_temp, this->device.velocity[0], this->device.velocity[1], this->device.velocity[2], this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, delta_seconds, this->host.scalar_advection_mode, this->host.temperature_boundary_types.data(), this->host.temperature_boundary_values.data(), flow_types, flow_velocity, flow_pressure);
        pyro_cuda::launch_apply_solid_scalar(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_data, this->device.occupancy, this->device.solid_temperature, this->host.nx, this->host.ny, this->host.nz, this->host.ambient_temperature);
        pyro_cuda::launch_add_scaled(this->host.stream, this->host.linear_grid, 256u, this->device.density_temp, this->device.density_data, this->device.density_source, delta_seconds, this->host.cell_count);
        pyro_cuda::launch_advect_centered_scalar(this->host.stream, this->host.cells, this->host.block, this->device.density_data, this->device.density_temp, this->device.velocity[0], this->device.velocity[1], this->device.velocity[2], this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, delta_seconds, this->host.scalar_advection_mode, this->host.density_boundary_types.data(), this->host.density_boundary_values.data(), flow_types, flow_velocity, flow_pressure);
        pyro_cuda::launch_boundary_fill_centered_scalar(this->host.stream, this->host.cells, this->host.block, this->device.density_temp, this->device.density_data, this->device.occupancy, this->host.nx, this->host.ny, this->host.nz, this->host.density_boundary_types.data(), this->host.density_boundary_values.data());
        check_cuda(cudaMemcpyAsync(this->device.density_data, this->device.density_temp, this->host.cell_bytes, cudaMemcpyDeviceToDevice, this->host.stream), "cudaMemcpyAsync density");
    }

    PyroFrame PyroSolver::read_frame(const int frame_index) {
        PyroFrame frame{};
        frame.frame_index = frame_index;
        frame.resolution  = this->host.resolution;
        frame.cell_size   = this->host.cell_size;
        frame.density.resize(static_cast<std::size_t>(this->host.cell_count));
        frame.temperature.resize(static_cast<std::size_t>(this->host.cell_count));
        frame.velocity_x.resize(static_cast<std::size_t>(this->host.velocity_count[0]));
        frame.velocity_y.resize(static_cast<std::size_t>(this->host.velocity_count[1]));
        frame.velocity_z.resize(static_cast<std::size_t>(this->host.velocity_count[2]));
        check_cuda(cudaMemcpyAsync(frame.density.data(), this->device.density_data, this->host.cell_bytes, cudaMemcpyDeviceToHost, this->host.stream), "cudaMemcpyAsync density download");
        check_cuda(cudaMemcpyAsync(frame.temperature.data(), this->device.temperature_data, this->host.cell_bytes, cudaMemcpyDeviceToHost, this->host.stream), "cudaMemcpyAsync temperature download");
        check_cuda(cudaMemcpyAsync(frame.velocity_x.data(), this->device.velocity[0], this->host.velocity_bytes[0], cudaMemcpyDeviceToHost, this->host.stream), "cudaMemcpyAsync velocity_x download");
        check_cuda(cudaMemcpyAsync(frame.velocity_y.data(), this->device.velocity[1], this->host.velocity_bytes[1], cudaMemcpyDeviceToHost, this->host.stream), "cudaMemcpyAsync velocity_y download");
        check_cuda(cudaMemcpyAsync(frame.velocity_z.data(), this->device.velocity[2], this->host.velocity_bytes[2], cudaMemcpyDeviceToHost, this->host.stream), "cudaMemcpyAsync velocity_z download");
        check_cuda(cudaStreamSynchronize(this->host.stream), "cudaStreamSynchronize pyro download");
        return frame;
    }

    void PyroSolver::create_device() {
        if (this->host.stream != nullptr) throw std::runtime_error("Pyro device is already initialized");
        try {
            this->host.block                 = dim3(8u, 8u, 4u);
            this->host.cells                 = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.ny), this->host.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz), this->host.block.z));
            this->host.velocity_cells[0]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx + 1), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.ny), this->host.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz), this->host.block.z));
            this->host.velocity_cells[1]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.ny + 1), this->host.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz), this->host.block.z));
            this->host.velocity_cells[2]     = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.ny), this->host.block.y), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz + 1), this->host.block.z));
            this->host.sync_block            = dim3(this->host.block.x, this->host.block.y, 1u);
            this->host.sync_velocity_grid[0] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.ny), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz), this->host.block.y), 1u);
            this->host.sync_velocity_grid[1] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.nz), this->host.block.y), 1u);
            this->host.sync_velocity_grid[2] = dim3(ceil_div_u32(static_cast<std::uint64_t>(this->host.nx), this->host.block.x), ceil_div_u32(static_cast<std::uint64_t>(this->host.ny), this->host.block.y), 1u);
            this->host.linear_grid           = ceil_div_u32(this->host.cell_count, 256u);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                this->host.velocity_linear_grid[axis] = ceil_div_u32(this->host.velocity_count[axis], 256u);
            }

            check_cuda(cudaStreamCreateWithFlags(&this->host.stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags");
            check_cublas(cublasCreate(&this->host.cublas), "cublasCreate");
            check_cublas(cublasSetStream(this->host.cublas, this->host.stream), "cublasSetStream");
            check_cublas(cublasSetPointerMode(this->host.cublas, CUBLAS_POINTER_MODE_DEVICE), "cublasSetPointerMode");
            check_cusparse(cusparseCreate(&this->host.cusparse), "cusparseCreate");
            check_cusparse(cusparseSetStream(this->host.cusparse, this->host.stream), "cusparseSetStream");

            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.velocity[axis]), this->host.velocity_bytes[axis]), "cudaMalloc velocity");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.temp_velocity[axis]), this->host.velocity_bytes[axis]), "cudaMalloc temp_velocity");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.centered_velocity[axis]), this->host.cell_bytes), "cudaMalloc centered_velocity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure), this->host.cell_bytes), "cudaMalloc pressure");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_rhs), this->host.cell_bytes), "cudaMalloc pressure_rhs");
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.vorticity[axis]), this->host.cell_bytes), "cudaMalloc vorticity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.vorticity_magnitude), this->host.cell_bytes), "cudaMalloc vorticity_magnitude");
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.force[axis]), this->host.cell_bytes), "cudaMalloc force");
                check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.solid_velocity[axis]), this->host.cell_bytes), "cudaMalloc solid_velocity");
            }
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.occupancy), this->host.cell_count * sizeof(std::uint8_t)), "cudaMalloc occupancy");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.solid_temperature), this->host.cell_bytes), "cudaMalloc solid_temperature");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.density_data), this->host.cell_bytes), "cudaMalloc density_data");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.density_temp), this->host.cell_bytes), "cudaMalloc density_temp");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.density_source), this->host.cell_bytes), "cudaMalloc density_source");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.temperature_data), this->host.cell_bytes), "cudaMalloc temperature_data");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.temperature_temp), this->host.cell_bytes), "cudaMalloc temperature_temp");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.temperature_source), this->host.cell_bytes), "cudaMalloc temperature_source");

            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                pyro_cuda::launch_fill_float(this->host.stream, this->host.velocity_linear_grid[axis], 256u, this->device.velocity[axis], 0.0f, this->host.velocity_count[axis]);
                pyro_cuda::launch_fill_float(this->host.stream, this->host.velocity_linear_grid[axis], 256u, this->device.temp_velocity[axis], 0.0f, this->host.velocity_count[axis]);
                pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.centered_velocity[axis], 0.0f, this->host.cell_count);
            }
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.pressure, 0.0f, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.pressure_rhs, 0.0f, this->host.cell_count);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.vorticity[axis], 0.0f, this->host.cell_count);
            }
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.vorticity_magnitude, 0.0f, this->host.cell_count);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.force[axis], 0.0f, this->host.cell_count);
                pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.solid_velocity[axis], 0.0f, this->host.cell_count);
            }
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.solid_temperature, this->host.ambient_temperature, this->host.cell_count);
            check_cuda(cudaMemsetAsync(this->device.occupancy, 0, this->host.cell_count * sizeof(std::uint8_t), this->host.stream), "cudaMemsetAsync occupancy");
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.density_data, 0.0f, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.density_temp, 0.0f, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.density_source, 0.0f, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_data, this->host.ambient_temperature, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_temp, this->host.ambient_temperature, this->host.cell_count);
            pyro_cuda::launch_fill_float(this->host.stream, this->host.linear_grid, 256u, this->device.temperature_source, 0.0f, this->host.cell_count);
            this->initialize_pressure_system();
        } catch (...) {
            this->destroy_device();
            throw;
        }
    }

    void PyroSolver::destroy_device() noexcept {
        try {
            if (this->host.stream != nullptr) cudaStreamSynchronize(this->host.stream);
            if (this->host.pressure_matrix != nullptr) cusparseDestroySpMat(this->host.pressure_matrix);
            if (this->host.pressure_vec_p != nullptr) cusparseDestroyDnVec(this->host.pressure_vec_p);
            if (this->host.pressure_vec_ap != nullptr) cusparseDestroyDnVec(this->host.pressure_vec_ap);
            if (this->host.cublas != nullptr) cublasDestroy(this->host.cublas);
            if (this->host.cusparse != nullptr) cusparseDestroy(this->host.cusparse);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device.velocity[axis] != nullptr) cudaFree(this->device.velocity[axis]);
                if (this->device.temp_velocity[axis] != nullptr) cudaFree(this->device.temp_velocity[axis]);
                if (this->device.centered_velocity[axis] != nullptr) cudaFree(this->device.centered_velocity[axis]);
            }
            if (this->device.pressure != nullptr) cudaFree(this->device.pressure);
            if (this->device.pressure_rhs != nullptr) cudaFree(this->device.pressure_rhs);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device.vorticity[axis] != nullptr) cudaFree(this->device.vorticity[axis]);
            }
            if (this->device.vorticity_magnitude != nullptr) cudaFree(this->device.vorticity_magnitude);
            for (std::uint32_t axis = 0; axis < 3u; ++axis) {
                if (this->device.force[axis] != nullptr) cudaFree(this->device.force[axis]);
                if (this->device.solid_velocity[axis] != nullptr) cudaFree(this->device.solid_velocity[axis]);
            }
            if (this->device.pressure_anchor != nullptr) cudaFree(this->device.pressure_anchor);
            if (this->device.pressure_row_offsets != nullptr) cudaFree(this->device.pressure_row_offsets);
            if (this->device.pressure_column_indices != nullptr) cudaFree(this->device.pressure_column_indices);
            if (this->device.pressure_values != nullptr) cudaFree(this->device.pressure_values);
            if (this->device.pcg_r != nullptr) cudaFree(this->device.pcg_r);
            if (this->device.pcg_p != nullptr) cudaFree(this->device.pcg_p);
            if (this->device.pcg_ap != nullptr) cudaFree(this->device.pcg_ap);
            if (this->device.pressure_dot_rz != nullptr) cudaFree(this->device.pressure_dot_rz);
            if (this->device.pressure_dot_pap != nullptr) cudaFree(this->device.pressure_dot_pap);
            if (this->device.pressure_dot_rr != nullptr) cudaFree(this->device.pressure_dot_rr);
            if (this->device.pressure_alpha != nullptr) cudaFree(this->device.pressure_alpha);
            if (this->device.pressure_negative_alpha != nullptr) cudaFree(this->device.pressure_negative_alpha);
            if (this->device.pressure_beta != nullptr) cudaFree(this->device.pressure_beta);
            if (this->device.pressure_one != nullptr) cudaFree(this->device.pressure_one);
            if (this->device.occupancy != nullptr) cudaFree(this->device.occupancy);
            if (this->device.solid_temperature != nullptr) cudaFree(this->device.solid_temperature);
            if (this->device.spmv_buffer != nullptr) cudaFree(this->device.spmv_buffer);
            if (this->device.density_data != nullptr) cudaFree(this->device.density_data);
            if (this->device.density_temp != nullptr) cudaFree(this->device.density_temp);
            if (this->device.density_source != nullptr) cudaFree(this->device.density_source);
            if (this->device.temperature_data != nullptr) cudaFree(this->device.temperature_data);
            if (this->device.temperature_temp != nullptr) cudaFree(this->device.temperature_temp);
            if (this->device.temperature_source != nullptr) cudaFree(this->device.temperature_source);
            if (this->host.stream != nullptr) cudaStreamDestroy(this->host.stream);
        } catch (...) {
        }
        this->host.stream           = nullptr;
        this->host.cublas           = nullptr;
        this->host.cusparse         = nullptr;
        this->host.pressure_matrix  = nullptr;
        this->host.pressure_vec_p   = nullptr;
        this->host.pressure_vec_ap  = nullptr;
        this->host.spmv_buffer_size = 0;
        this->device                = {};
    }

    void PyroSolver::initialize_pressure_system() {
        const int cells       = static_cast<int>(this->host.cell_count);
        const bool periodic_x = this->host.flow_boundary_types[0] == flow_boundary_periodic && this->host.flow_boundary_types[1] == flow_boundary_periodic;
        const bool periodic_y = this->host.flow_boundary_types[2] == flow_boundary_periodic && this->host.flow_boundary_types[3] == flow_boundary_periodic;
        const bool periodic_z = this->host.flow_boundary_types[4] == flow_boundary_periodic && this->host.flow_boundary_types[5] == flow_boundary_periodic;
        std::vector<int> host_row_offsets(static_cast<std::size_t>(cells) + 1u, 0);
        std::vector<int> host_column_indices{};
        host_column_indices.reserve(static_cast<std::size_t>(cells) * 7u);

        for (int row = 0; row < cells; ++row) {
            host_row_offsets[static_cast<std::size_t>(row)] = static_cast<int>(host_column_indices.size());
            const int x                                     = row % this->host.nx;
            const int yz                                    = row / this->host.nx;
            const int y                                     = yz % this->host.ny;
            const int z                                     = yz / this->host.ny;
            std::array<int, 7> row_columns{};
            int row_entry_count = 0;
            add_pressure_neighbor(row_columns, row_entry_count, x - 1, y, z, periodic_x, this->host.nx, this->host.ny, this->host.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x + 1, y, z, periodic_x, this->host.nx, this->host.ny, this->host.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y - 1, z, periodic_y, this->host.nx, this->host.ny, this->host.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y + 1, z, periodic_y, this->host.nx, this->host.ny, this->host.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y, z - 1, periodic_z, this->host.nx, this->host.ny, this->host.nz);
            add_pressure_neighbor(row_columns, row_entry_count, x, y, z + 1, periodic_z, this->host.nx, this->host.ny, this->host.nz);
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
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_anchor), sizeof(int)), "cudaMalloc pressure_anchor");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_row_offsets), static_cast<std::size_t>(cells + 1) * sizeof(int)), "cudaMalloc pressure_row_offsets");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_column_indices), static_cast<std::size_t>(pressure_nnz) * sizeof(int)), "cudaMalloc pressure_column_indices");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_values), static_cast<std::size_t>(pressure_nnz) * sizeof(float)), "cudaMalloc pressure_values");
        check_cuda(cudaMemcpyAsync(this->device.pressure_row_offsets, host_row_offsets.data(), static_cast<std::size_t>(cells + 1) * sizeof(int), cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync pressure_row_offsets");
        check_cuda(cudaMemcpyAsync(this->device.pressure_column_indices, host_column_indices.data(), static_cast<std::size_t>(pressure_nnz) * sizeof(int), cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync pressure_column_indices");
        check_cuda(cudaMemsetAsync(this->device.pressure_values, 0, static_cast<std::size_t>(pressure_nnz) * sizeof(float), this->host.stream), "cudaMemsetAsync pressure_values");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pcg_r), this->host.cell_bytes), "cudaMalloc pcg_r");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pcg_p), this->host.cell_bytes), "cudaMalloc pcg_p");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pcg_ap), this->host.cell_bytes), "cudaMalloc pcg_ap");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_dot_rz), sizeof(float)), "cudaMalloc pressure_dot_rz");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_dot_pap), sizeof(float)), "cudaMalloc pressure_dot_pap");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_dot_rr), sizeof(float)), "cudaMalloc pressure_dot_rr");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_alpha), sizeof(float)), "cudaMalloc pressure_alpha");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_negative_alpha), sizeof(float)), "cudaMalloc pressure_negative_alpha");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_beta), sizeof(float)), "cudaMalloc pressure_beta");
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.pressure_one), sizeof(float)), "cudaMalloc pressure_one");
        constexpr float one = 1.0f;
        check_cuda(cudaMemcpyAsync(this->device.pressure_one, &one, sizeof(float), cudaMemcpyHostToDevice, this->host.stream), "cudaMemcpyAsync pressure_one");
        check_cuda(cudaMemsetAsync(this->device.pcg_r, 0, this->host.cell_bytes, this->host.stream), "cudaMemsetAsync pcg_r");
        check_cuda(cudaMemsetAsync(this->device.pcg_p, 0, this->host.cell_bytes, this->host.stream), "cudaMemsetAsync pcg_p");
        check_cuda(cudaMemsetAsync(this->device.pcg_ap, 0, this->host.cell_bytes, this->host.stream), "cudaMemsetAsync pcg_ap");
        check_cuda(cudaStreamSynchronize(this->host.stream), "cudaStreamSynchronize pressure_system_upload");
        check_cusparse(cusparseCreateCsr(&this->host.pressure_matrix, cells, cells, pressure_nnz, this->device.pressure_row_offsets, this->device.pressure_column_indices, this->device.pressure_values, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_32F), "cusparseCreateCsr matrix");
        check_cusparse(cusparseCreateDnVec(&this->host.pressure_vec_p, cells, this->device.pcg_p, CUDA_R_32F), "cusparseCreateDnVec vec_p");
        check_cusparse(cusparseCreateDnVec(&this->host.pressure_vec_ap, cells, this->device.pcg_ap, CUDA_R_32F), "cusparseCreateDnVec vec_ap");
        constexpr float spmv_alpha = 1.0f;
        constexpr float spmv_beta  = 0.0f;
        check_cusparse(cusparseSpMV_bufferSize(this->host.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &spmv_alpha, this->host.pressure_matrix, this->host.pressure_vec_p, &spmv_beta, this->host.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, &this->host.spmv_buffer_size), "cusparseSpMV_bufferSize");
        if (this->host.spmv_buffer_size > 0) check_cuda(cudaMalloc(&this->device.spmv_buffer, this->host.spmv_buffer_size), "cudaMalloc spmv_buffer");
        check_cusparse(cusparseSpMV_preprocess(this->host.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &spmv_alpha, this->host.pressure_matrix, this->host.pressure_vec_p, &spmv_beta, this->host.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, this->device.spmv_buffer), "cusparseSpMV_preprocess");
    }

    void PyroSolver::solve_pressure(const float delta_seconds) {
        const std::uint32_t* flow_types = this->host.flow_boundary_types.data();
        const float* flow_velocity      = this->host.flow_boundary_velocity.data();
        const float* flow_pressure      = this->host.flow_boundary_pressure.data();
        pyro_cuda::launch_fill_int(this->host.stream, 1u, 1u, this->device.pressure_anchor, static_cast<int>(this->host.cell_count), 1u);
        pyro_cuda::launch_find_pressure_anchor(this->host.stream, this->host.linear_grid, 256u, this->device.pressure_anchor, this->device.occupancy, this->host.cell_count);
        pyro_cuda::launch_compute_projection_rhs(this->host.stream, this->host.cells, this->host.block, this->device.pressure_rhs, this->device.temp_velocity[0], this->device.temp_velocity[1], this->device.temp_velocity[2], this->device.occupancy, this->device.pressure_anchor, this->host.nx, this->host.ny, this->host.nz, this->host.cell_size, delta_seconds, flow_types, flow_velocity, flow_pressure);
        pyro_cuda::launch_build_projection_matrix(this->host.stream, this->host.linear_grid, 256u, this->device.pressure_values, this->device.pressure_row_offsets, this->device.pressure_column_indices, this->device.occupancy, this->device.pressure_anchor, this->host.nx, this->host.ny, this->host.nz, flow_types, flow_velocity, flow_pressure);
        check_cuda(cudaMemsetAsync(this->device.pressure, 0, this->host.cell_bytes, this->host.stream), "cudaMemsetAsync pressure");
        check_cublas(cublasScopy(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pressure_rhs, 1, this->device.pcg_r, 1), "cublasScopy rhs");
        check_cublas(cublasScopy(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pcg_r, 1, this->device.pcg_p, 1), "cublasScopy pcg_p");
        check_cublas(cublasSdot(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pcg_r, 1, this->device.pcg_r, 1, this->device.pressure_dot_rz), "cublasSdot pressure_dot_rz");
        constexpr float one  = 1.0f;
        constexpr float zero = 0.0f;
        for (int iteration = 0; iteration < this->host.pressure_iterations; ++iteration) {
            check_cusparse(cusparseSpMV(this->host.cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &one, this->host.pressure_matrix, this->host.pressure_vec_p, &zero, this->host.pressure_vec_ap, CUDA_R_32F, CUSPARSE_SPMV_ALG_DEFAULT, this->device.spmv_buffer), "cusparseSpMV");
            check_cublas(cublasSdot(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pcg_p, 1, this->device.pcg_ap, 1, this->device.pressure_dot_pap), "cublasSdot pressure_dot_pap");
            pyro_cuda::launch_compute_ratio(this->host.stream, this->device.pressure_alpha, this->device.pressure_dot_rz, this->device.pressure_dot_pap);
            check_cublas(cublasSaxpy(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pressure_alpha, this->device.pcg_p, 1, this->device.pressure, 1), "cublasSaxpy pressure");
            pyro_cuda::launch_negate_scalar(this->host.stream, this->device.pressure_negative_alpha, this->device.pressure_alpha);
            check_cublas(cublasSaxpy(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pressure_negative_alpha, this->device.pcg_ap, 1, this->device.pcg_r, 1), "cublasSaxpy pcg_r");
            check_cublas(cublasSdot(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pcg_r, 1, this->device.pcg_r, 1, this->device.pressure_dot_rr), "cublasSdot rho_new");
            pyro_cuda::launch_compute_ratio(this->host.stream, this->device.pressure_beta, this->device.pressure_dot_rr, this->device.pressure_dot_rz);
            check_cublas(cublasSscal(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pressure_beta, this->device.pcg_p, 1), "cublasSscal pcg_p");
            check_cublas(cublasSaxpy(this->host.cublas, static_cast<int>(this->host.cell_count), this->device.pressure_one, this->device.pcg_r, 1, this->device.pcg_p, 1), "cublasSaxpy pcg_p");
            check_cublas(cublasScopy(this->host.cublas, 1, this->device.pressure_dot_rr, 1, this->device.pressure_dot_rz, 1), "cublasScopy rho");
        }
    }

    void PyroSolver::reset_moved_from() noexcept {
        this->host.stream           = nullptr;
        this->host.cublas           = nullptr;
        this->host.cusparse         = nullptr;
        this->host.pressure_matrix  = nullptr;
        this->host.pressure_vec_p   = nullptr;
        this->host.pressure_vec_ap  = nullptr;
        this->host.spmv_buffer_size = 0;
        this->device                = {};
    }
} // namespace spectra
