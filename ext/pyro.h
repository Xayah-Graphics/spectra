#ifndef SPECTRA_PYRO_H
#define SPECTRA_PYRO_H

#include <cstdint>
#include <cuda_runtime.h>

namespace xayah::pyro_cuda {
    void launch_fill_float(cudaStream_t stream, unsigned grid, unsigned block, float* field, float value, std::uint64_t count);
    void launch_fill_int(cudaStream_t stream, unsigned grid, unsigned block, int* field, int value, std::uint64_t count);
    void launch_add_scaled(cudaStream_t stream, unsigned grid, unsigned block, float* destination, const float* current, const float* source, float scale, std::uint64_t count);
    void launch_center_staggered_vector(cudaStream_t stream, dim3 grid, dim3 block, float* cell_x, float* cell_y, float* cell_z, const float* velocity_x, const float* velocity_y, const float* velocity_z, int nx, int ny, int nz);
    void launch_compute_vorticity(cudaStream_t stream, dim3 grid, dim3 block, float* omega_x, float* omega_y, float* omega_z, float* omega_magnitude, const float* cell_x, const float* cell_y, const float* cell_z, const std::uint8_t* occupancy, int nx, int ny, int nz, float h, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_add_buoyancy(cudaStream_t stream, dim3 grid, dim3 block, float* force_y, const float* density, const float* temperature, const std::uint8_t* occupancy, int nx, int ny, int nz, float ambient_temperature, float density_factor, float temperature_factor, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_add_vorticity_confinement(cudaStream_t stream, dim3 grid, dim3 block, float* force_x, float* force_y, float* force_z, const float* omega_x, const float* omega_y, const float* omega_z, const float* omega_magnitude, const std::uint8_t* occupancy, int nx, int ny, int nz, float h, float epsilon, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_add_center_force_to_staggered_component(cudaStream_t stream, dim3 grid, dim3 block, std::uint32_t axis, float* velocity_component, const float* force_component, int nx, int ny, int nz, float dt);
    void launch_enforce_staggered_boundary(cudaStream_t stream, dim3 grid, dim3 block, std::uint32_t axis, float* velocity_component, const std::uint8_t* occupancy, const float* solid_velocity_component, int nx, int ny, int nz, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_sync_periodic_staggered_component(cudaStream_t stream, dim3 grid, dim3 block, std::uint32_t axis, float* velocity_component, int nx, int ny, int nz);
    void launch_advect_staggered_component(cudaStream_t stream, dim3 grid, dim3 block, std::uint32_t axis, float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, int nx, int ny, int nz, float h, float dt, std::uint32_t advection_mode, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_advect_centered_scalar(cudaStream_t stream, dim3 grid, dim3 block, float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, int nx, int ny, int nz, float h, float dt, std::uint32_t advection_mode, const std::uint32_t* scalar_boundary_types, const float* scalar_boundary_values, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_apply_solid_scalar(cudaStream_t stream, unsigned grid, unsigned block, float* scalar, const std::uint8_t* occupancy, const float* solid_scalar, int nx, int ny, int nz, float default_value);
    void launch_boundary_fill_centered_scalar(cudaStream_t stream, dim3 grid, dim3 block, float* destination, const float* source, const std::uint8_t* occupancy, int nx, int ny, int nz, const std::uint32_t* scalar_boundary_types, const float* scalar_boundary_values);
    void launch_find_pressure_anchor(cudaStream_t stream, unsigned grid, unsigned block, int* pressure_anchor, const std::uint8_t* occupancy, std::uint64_t count);
    void launch_compute_projection_rhs(cudaStream_t stream, dim3 grid, dim3 block, float* rhs, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, const int* pressure_anchor, int nx, int ny, int nz, float h, float dt, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_build_projection_matrix(cudaStream_t stream, unsigned grid, unsigned block, float* values, const int* row_offsets, const int* column_indices, const std::uint8_t* occupancy, const int* pressure_anchor, int nx, int ny, int nz, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
    void launch_compute_ratio(cudaStream_t stream, float* destination, const float* numerator, const float* denominator);
    void launch_negate_scalar(cudaStream_t stream, float* destination, const float* source);
    void launch_project_staggered_component(cudaStream_t stream, dim3 grid, dim3 block, std::uint32_t axis, float* velocity_component, const float* pressure, const std::uint8_t* occupancy, const float* solid_velocity_component, int nx, int ny, int nz, float h, float dt, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure);
} // namespace xayah::pyro_cuda

#endif // SPECTRA_PYRO_H
