#ifndef SPECTRA_CLOTH_H
#define SPECTRA_CLOTH_H

#include <cstdint>
#include <cuda_runtime.h>

namespace spectra::cloth_cuda {
    void launch_reset(cudaStream_t stream, unsigned grid, unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, float* inverse_mass, std::uint32_t columns, std::uint32_t rows, float origin_x, float origin_y, float origin_z, float dx, float dz);
    void launch_integrate(cudaStream_t stream, unsigned grid, unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, std::uint32_t vertex_count, float gravity_x, float gravity_y, float gravity_z, float substep_seconds, float damping);
    void launch_solve_distance_constraints(cudaStream_t stream, unsigned grid, unsigned block, std::uint32_t kind, std::uint32_t color, float* position_x, float* position_y, float* position_z, const float* inverse_mass, float* lambda, int* error_flag, std::uint32_t constraint_count, std::uint32_t columns, std::uint32_t rows, float rest_length, float compliance, float substep_seconds);
    void launch_solve_sphere_collision(cudaStream_t stream, unsigned grid, unsigned block, float* position_x, float* position_y, float* position_z, const float* inverse_mass, int* error_flag, std::uint32_t vertex_count, float center_x, float center_y, float center_z, float radius);
    void launch_update_velocities(cudaStream_t stream, unsigned grid, unsigned block, const float* position_x, const float* position_y, const float* position_z, const float* previous_x, const float* previous_y, const float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, std::uint32_t vertex_count, float substep_seconds);
    void launch_accumulate_normals(cudaStream_t stream, unsigned grid, unsigned block, const float* position_x, const float* position_y, const float* position_z, float* normal_x, float* normal_y, float* normal_z, const std::uint32_t* indices, int* error_flag, std::uint32_t triangle_count);
    void launch_normalize_normals(cudaStream_t stream, unsigned grid, unsigned block, float* normal_x, float* normal_y, float* normal_z, int* error_flag, std::uint32_t vertex_count);
} // namespace spectra::cloth_cuda

#endif // SPECTRA_CLOTH_H
