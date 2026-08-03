#include "cloth.h"
#include <cstdint>
#include <stdexcept>
#include <string>

namespace {
    __device__ std::uint32_t cloth_index_device(const std::uint32_t column, const std::uint32_t row, const std::uint32_t columns) {
        return row * columns + column;
    }

    __device__ float vector_length(const float x, const float y, const float z) {
        return sqrtf(x * x + y * y + z * z);
    }

    __device__ bool constraint_vertices(const std::uint32_t constraint_kind, const std::uint32_t index, const std::uint32_t columns, std::uint32_t& first_vertex, std::uint32_t& second_vertex, std::uint32_t& computed_color) {
        if (constraint_kind == 0u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first_vertex               = cloth_index_device(column, row, columns);
            second_vertex              = first_vertex + 1u;
            computed_color             = column & 1u;
            return true;
        }
        if (constraint_kind == 1u) {
            const std::uint32_t row    = index / columns;
            const std::uint32_t column = index - row * columns;
            first_vertex               = cloth_index_device(column, row, columns);
            second_vertex              = first_vertex + columns;
            computed_color             = row & 1u;
            return true;
        }
        if (constraint_kind == 2u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first_vertex               = cloth_index_device(column, row, columns);
            second_vertex              = cloth_index_device(column + 1u, row + 1u, columns);
            computed_color             = ((row & 1u) << 1u) | (column & 1u);
            return true;
        }
        if (constraint_kind == 3u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first_vertex               = cloth_index_device(column + 1u, row, columns);
            second_vertex              = cloth_index_device(column, row + 1u, columns);
            computed_color             = ((row & 1u) << 1u) | (column & 1u);
            return true;
        }
        if (constraint_kind == 4u) {
            const std::uint32_t row    = index / (columns - 2u);
            const std::uint32_t column = index - row * (columns - 2u);
            first_vertex               = cloth_index_device(column, row, columns);
            second_vertex              = first_vertex + 2u;
            computed_color             = column & 3u;
            return true;
        }
        if (constraint_kind == 5u) {
            const std::uint32_t row    = index / columns;
            const std::uint32_t column = index - row * columns;
            first_vertex               = cloth_index_device(column, row, columns);
            second_vertex              = first_vertex + 2u * columns;
            computed_color             = row & 3u;
            return true;
        }
        return false;
    }

    __global__ void reset_kernel(float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, float* inverse_mass, const std::uint32_t columns, const std::uint32_t rows, const float origin_x, const float origin_y, const float origin_z, const float column_spacing, const float row_spacing) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        const std::uint32_t count = columns * rows;
        if (index >= count) return;
        const std::uint32_t row    = index / columns;
        const std::uint32_t column = index - row * columns;
        const float x              = origin_x + static_cast<float>(column) * column_spacing;
        const float y              = origin_y;
        const float z              = origin_z + static_cast<float>(row) * row_spacing;
        position_x[index]          = x;
        position_y[index]          = y;
        position_z[index]          = z;
        previous_x[index]          = x;
        previous_y[index]          = y;
        previous_z[index]          = z;
        velocity_x[index]          = 0.0f;
        velocity_y[index]          = 0.0f;
        velocity_z[index]          = 0.0f;
        inverse_mass[index]        = (row == 0u && (column == 0u || column + 1u == columns)) ? 0.0f : 1.0f;
    }

    __global__ void integrate_kernel(float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float gravity_x, const float gravity_y, const float gravity_z, const float substep_seconds, const float damping) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= vertex_count) return;
        previous_x[index] = position_x[index];
        previous_y[index] = position_y[index];
        previous_z[index] = position_z[index];
        if (inverse_mass[index] == 0.0f) {
            velocity_x[index] = 0.0f;
            velocity_y[index] = 0.0f;
            velocity_z[index] = 0.0f;
            return;
        }
        velocity_x[index] = (velocity_x[index] + gravity_x * substep_seconds) * damping;
        velocity_y[index] = (velocity_y[index] + gravity_y * substep_seconds) * damping;
        velocity_z[index] = (velocity_z[index] + gravity_z * substep_seconds) * damping;
        position_x[index] += velocity_x[index] * substep_seconds;
        position_y[index] += velocity_y[index] * substep_seconds;
        position_z[index] += velocity_z[index] * substep_seconds;
    }

    __global__ void solve_distance_constraints_kernel(const std::uint32_t constraint_kind, const std::uint32_t constraint_color, float* position_x, float* position_y, float* position_z, const float* inverse_mass, float* lambda, int* constraint_error_flag, const std::uint32_t constraint_count, const std::uint32_t columns, const std::uint32_t rows, const float rest_length, const float compliance, const float substep_seconds) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= constraint_count) return;
        std::uint32_t first_vertex   = 0u;
        std::uint32_t second_vertex  = 0u;
        std::uint32_t computed_color = 0u;
        if (!constraint_vertices(constraint_kind, index, columns, first_vertex, second_vertex, computed_color)) return;
        if (computed_color != constraint_color) return;
        if (first_vertex >= columns * rows || second_vertex >= columns * rows) {
            *constraint_error_flag = 1;
            return;
        }

        const float first_weight  = inverse_mass[first_vertex];
        const float second_weight = inverse_mass[second_vertex];
        const float weight_sum    = first_weight + second_weight;
        if (weight_sum == 0.0f) return;

        const float x_difference = position_x[first_vertex] - position_x[second_vertex];
        const float y_difference = position_y[first_vertex] - position_y[second_vertex];
        const float z_difference = position_z[first_vertex] - position_z[second_vertex];
        const float length       = vector_length(x_difference, y_difference, z_difference);
        if (length <= 0.000001f) {
            *constraint_error_flag = 1;
            return;
        }

        const float alpha            = compliance / (substep_seconds * substep_seconds);
        const float constraint_value = length - rest_length;
        const float delta_lambda     = -(constraint_value + alpha * lambda[index]) / (weight_sum + alpha);
        const float scale            = delta_lambda / length;
        const float correction_x     = x_difference * scale;
        const float correction_y     = y_difference * scale;
        const float correction_z     = z_difference * scale;
        if (first_weight > 0.0f) {
            position_x[first_vertex] += first_weight * correction_x;
            position_y[first_vertex] += first_weight * correction_y;
            position_z[first_vertex] += first_weight * correction_z;
        }
        if (second_weight > 0.0f) {
            position_x[second_vertex] -= second_weight * correction_x;
            position_y[second_vertex] -= second_weight * correction_y;
            position_z[second_vertex] -= second_weight * correction_z;
        }
        lambda[index] += delta_lambda;
    }

    __global__ void solve_sphere_collision_kernel(float* position_x, float* position_y, float* position_z, const float* inverse_mass, int* constraint_error_flag, const std::uint32_t vertex_count, const float center_x, const float center_y, const float center_z, const float radius) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= vertex_count) return;
        if (inverse_mass[index] == 0.0f) return;
        const float x_difference = position_x[index] - center_x;
        const float y_difference = position_y[index] - center_y;
        const float z_difference = position_z[index] - center_z;
        const float length       = vector_length(x_difference, y_difference, z_difference);
        if (length >= radius) return;
        if (length <= 0.000001f) {
            *constraint_error_flag = 1;
            return;
        }
        const float scale = radius / length;
        position_x[index] = center_x + x_difference * scale;
        position_y[index] = center_y + y_difference * scale;
        position_z[index] = center_z + z_difference * scale;
    }

    __global__ void update_velocities_kernel(const float* position_x, const float* position_y, const float* position_z, const float* previous_x, const float* previous_y, const float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float substep_seconds) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= vertex_count) return;
        if (inverse_mass[index] == 0.0f) {
            velocity_x[index] = 0.0f;
            velocity_y[index] = 0.0f;
            velocity_z[index] = 0.0f;
            return;
        }
        velocity_x[index] = (position_x[index] - previous_x[index]) / substep_seconds;
        velocity_y[index] = (position_y[index] - previous_y[index]) / substep_seconds;
        velocity_z[index] = (position_z[index] - previous_z[index]) / substep_seconds;
    }

    void check_cuda(const cudaError_t status, const char* what) {
        if (status == cudaSuccess) return;
        throw std::runtime_error(std::string{what} + ": " + cudaGetErrorString(status));
    }

    void validate_constraint_kind(const std::uint32_t constraint_kind) {
        if (constraint_kind > 5u) throw std::runtime_error("Cloth constraint kind is invalid");
    }
} // namespace

namespace xayah::projects::cloth::cuda {
    void launch_reset(const cudaStream_t cuda_stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, float* inverse_mass, const std::uint32_t columns, const std::uint32_t rows, const float origin_x, const float origin_y, const float origin_z, const float column_spacing, const float row_spacing) {
        reset_kernel<<<grid, block, 0, cuda_stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, columns, rows, origin_x, origin_y, origin_z, column_spacing, row_spacing);
        check_cuda(cudaGetLastError(), "reset cloth kernel");
    }

    void launch_integrate(const cudaStream_t cuda_stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float gravity_x, const float gravity_y, const float gravity_z, const float substep_seconds, const float damping) {
        integrate_kernel<<<grid, block, 0, cuda_stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, vertex_count, gravity_x, gravity_y, gravity_z, substep_seconds, damping);
        check_cuda(cudaGetLastError(), "integrate cloth kernel");
    }

    void launch_solve_distance_constraints(const cudaStream_t cuda_stream, const unsigned grid, const unsigned block, const std::uint32_t constraint_kind, const std::uint32_t constraint_color, float* position_x, float* position_y, float* position_z, const float* inverse_mass, float* lambda, int* constraint_error_flag, const std::uint32_t constraint_count, const std::uint32_t columns, const std::uint32_t rows, const float rest_length, const float compliance, const float substep_seconds) {
        validate_constraint_kind(constraint_kind);
        if (constraint_count == 0u) return;
        solve_distance_constraints_kernel<<<grid, block, 0, cuda_stream>>>(constraint_kind, constraint_color, position_x, position_y, position_z, inverse_mass, lambda, constraint_error_flag, constraint_count, columns, rows, rest_length, compliance, substep_seconds);
        check_cuda(cudaGetLastError(), "solve cloth distance constraints kernel");
    }

    void launch_solve_sphere_collision(const cudaStream_t cuda_stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, const float* inverse_mass, int* constraint_error_flag, const std::uint32_t vertex_count, const float center_x, const float center_y, const float center_z, const float radius) {
        solve_sphere_collision_kernel<<<grid, block, 0, cuda_stream>>>(position_x, position_y, position_z, inverse_mass, constraint_error_flag, vertex_count, center_x, center_y, center_z, radius);
        check_cuda(cudaGetLastError(), "solve cloth sphere collision kernel");
    }

    void launch_update_velocities(const cudaStream_t cuda_stream, const unsigned grid, const unsigned block, const float* position_x, const float* position_y, const float* position_z, const float* previous_x, const float* previous_y, const float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float substep_seconds) {
        update_velocities_kernel<<<grid, block, 0, cuda_stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, vertex_count, substep_seconds);
        check_cuda(cudaGetLastError(), "update cloth velocities kernel");
    }

} // namespace xayah::projects::cloth::cuda
