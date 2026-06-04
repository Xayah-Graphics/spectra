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

    __device__ bool constraint_vertices(const std::uint32_t kind, const std::uint32_t index, const std::uint32_t columns, std::uint32_t& first, std::uint32_t& second, std::uint32_t& constraint_color) {
        if (kind == 0u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first                      = cloth_index_device(column, row, columns);
            second                     = first + 1u;
            constraint_color           = column & 1u;
            return true;
        }
        if (kind == 1u) {
            const std::uint32_t row    = index / columns;
            const std::uint32_t column = index - row * columns;
            first                      = cloth_index_device(column, row, columns);
            second                     = first + columns;
            constraint_color           = row & 1u;
            return true;
        }
        if (kind == 2u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first                      = cloth_index_device(column, row, columns);
            second                     = cloth_index_device(column + 1u, row + 1u, columns);
            constraint_color           = ((row & 1u) << 1u) | (column & 1u);
            return true;
        }
        if (kind == 3u) {
            const std::uint32_t row    = index / (columns - 1u);
            const std::uint32_t column = index - row * (columns - 1u);
            first                      = cloth_index_device(column + 1u, row, columns);
            second                     = cloth_index_device(column, row + 1u, columns);
            constraint_color           = ((row & 1u) << 1u) | (column & 1u);
            return true;
        }
        if (kind == 4u) {
            const std::uint32_t row    = index / (columns - 2u);
            const std::uint32_t column = index - row * (columns - 2u);
            first                      = cloth_index_device(column, row, columns);
            second                     = first + 2u;
            constraint_color           = column & 3u;
            return true;
        }
        if (kind == 5u) {
            const std::uint32_t row    = index / columns;
            const std::uint32_t column = index - row * columns;
            first                      = cloth_index_device(column, row, columns);
            second                     = first + 2u * columns;
            constraint_color           = row & 3u;
            return true;
        }
        return false;
    }

    __global__ void reset_kernel(float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, float* inverse_mass, const std::uint32_t columns, const std::uint32_t rows, const float origin_x, const float origin_y, const float origin_z, const float dx, const float dz) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        const std::uint32_t count = columns * rows;
        if (index >= count) return;
        const std::uint32_t row    = index / columns;
        const std::uint32_t column = index - row * columns;
        const float x              = origin_x + static_cast<float>(column) * dx;
        const float y              = origin_y;
        const float z              = origin_z + static_cast<float>(row) * dz;
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

    __global__ void solve_distance_constraints_kernel(const std::uint32_t kind, const std::uint32_t color, float* position_x, float* position_y, float* position_z, const float* inverse_mass, float* lambda, int* error_flag, const std::uint32_t constraint_count, const std::uint32_t columns, const std::uint32_t rows, const float rest_length, const float compliance, const float substep_seconds) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= constraint_count) return;
        std::uint32_t first            = 0u;
        std::uint32_t second           = 0u;
        std::uint32_t constraint_color = 0u;
        if (!constraint_vertices(kind, index, columns, first, second, constraint_color)) return;
        if (constraint_color != color) return;
        if (first >= columns * rows || second >= columns * rows) {
            *error_flag = 1;
            return;
        }

        const float w0         = inverse_mass[first];
        const float w1         = inverse_mass[second];
        const float weight_sum = w0 + w1;
        if (weight_sum == 0.0f) return;

        const float dx     = position_x[first] - position_x[second];
        const float dy     = position_y[first] - position_y[second];
        const float dz     = position_z[first] - position_z[second];
        const float length = vector_length(dx, dy, dz);
        if (length <= 0.000001f) {
            *error_flag = 1;
            return;
        }

        const float alpha        = compliance / (substep_seconds * substep_seconds);
        const float c            = length - rest_length;
        const float delta_lambda = -(c + alpha * lambda[index]) / (weight_sum + alpha);
        const float scale        = delta_lambda / length;
        const float correction_x = dx * scale;
        const float correction_y = dy * scale;
        const float correction_z = dz * scale;
        if (w0 > 0.0f) {
            position_x[first] += w0 * correction_x;
            position_y[first] += w0 * correction_y;
            position_z[first] += w0 * correction_z;
        }
        if (w1 > 0.0f) {
            position_x[second] -= w1 * correction_x;
            position_y[second] -= w1 * correction_y;
            position_z[second] -= w1 * correction_z;
        }
        lambda[index] += delta_lambda;
    }

    __global__ void solve_sphere_collision_kernel(float* position_x, float* position_y, float* position_z, const float* inverse_mass, int* error_flag, const std::uint32_t vertex_count, const float center_x, const float center_y, const float center_z, const float radius) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= vertex_count) return;
        if (inverse_mass[index] == 0.0f) return;
        const float dx     = position_x[index] - center_x;
        const float dy     = position_y[index] - center_y;
        const float dz     = position_z[index] - center_z;
        const float length = vector_length(dx, dy, dz);
        if (length >= radius) return;
        if (length <= 0.000001f) {
            *error_flag = 1;
            return;
        }
        const float scale = radius / length;
        position_x[index] = center_x + dx * scale;
        position_y[index] = center_y + dy * scale;
        position_z[index] = center_z + dz * scale;
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

    __global__ void accumulate_normals_kernel(const float* position_x, const float* position_y, const float* position_z, float* normal_x, float* normal_y, float* normal_z, const std::uint32_t* indices, int* error_flag, const std::uint32_t triangle_count) {
        const std::uint32_t triangle = blockIdx.x * blockDim.x + threadIdx.x;
        if (triangle >= triangle_count) return;
        const std::uint32_t i0 = indices[triangle * 3u + 0u];
        const std::uint32_t i1 = indices[triangle * 3u + 1u];
        const std::uint32_t i2 = indices[triangle * 3u + 2u];
        const float e0x        = position_x[i1] - position_x[i0];
        const float e0y        = position_y[i1] - position_y[i0];
        const float e0z        = position_z[i1] - position_z[i0];
        const float e1x        = position_x[i2] - position_x[i0];
        const float e1y        = position_y[i2] - position_y[i0];
        const float e1z        = position_z[i2] - position_z[i0];
        float nx               = e0y * e1z - e0z * e1y;
        float ny               = e0z * e1x - e0x * e1z;
        float nz               = e0x * e1y - e0y * e1x;
        const float length     = vector_length(nx, ny, nz);
        if (length <= 0.000001f) {
            *error_flag = 1;
            return;
        }
        nx /= length;
        ny /= length;
        nz /= length;
        atomicAdd(&normal_x[i0], nx);
        atomicAdd(&normal_y[i0], ny);
        atomicAdd(&normal_z[i0], nz);
        atomicAdd(&normal_x[i1], nx);
        atomicAdd(&normal_y[i1], ny);
        atomicAdd(&normal_z[i1], nz);
        atomicAdd(&normal_x[i2], nx);
        atomicAdd(&normal_y[i2], ny);
        atomicAdd(&normal_z[i2], nz);
    }

    __global__ void normalize_normals_kernel(float* normal_x, float* normal_y, float* normal_z, int* error_flag, const std::uint32_t vertex_count) {
        const std::uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
        if (index >= vertex_count) return;
        const float length = vector_length(normal_x[index], normal_y[index], normal_z[index]);
        if (length <= 0.000001f) {
            *error_flag = 1;
            return;
        }
        normal_x[index] /= length;
        normal_y[index] /= length;
        normal_z[index] /= length;
    }

    void check_cuda(const cudaError_t status, const char* what) {
        if (status == cudaSuccess) return;
        throw std::runtime_error(std::string{what} + ": " + cudaGetErrorString(status));
    }

    void validate_kind(const std::uint32_t kind) {
        if (kind > 5u) throw std::runtime_error("Cloth constraint kind is invalid");
    }
} // namespace

namespace xayah::projects::cloth::cuda {
    void launch_reset(const cudaStream_t stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, float* inverse_mass, const std::uint32_t columns, const std::uint32_t rows, const float origin_x, const float origin_y, const float origin_z, const float dx, const float dz) {
        reset_kernel<<<grid, block, 0, stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, columns, rows, origin_x, origin_y, origin_z, dx, dz);
        check_cuda(cudaGetLastError(), "reset cloth kernel");
    }

    void launch_integrate(const cudaStream_t stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, float* previous_x, float* previous_y, float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float gravity_x, const float gravity_y, const float gravity_z, const float substep_seconds, const float damping) {
        integrate_kernel<<<grid, block, 0, stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, vertex_count, gravity_x, gravity_y, gravity_z, substep_seconds, damping);
        check_cuda(cudaGetLastError(), "integrate cloth kernel");
    }

    void launch_solve_distance_constraints(const cudaStream_t stream, const unsigned grid, const unsigned block, const std::uint32_t kind, const std::uint32_t color, float* position_x, float* position_y, float* position_z, const float* inverse_mass, float* lambda, int* error_flag, const std::uint32_t constraint_count, const std::uint32_t columns, const std::uint32_t rows, const float rest_length, const float compliance, const float substep_seconds) {
        validate_kind(kind);
        if (constraint_count == 0u) return;
        solve_distance_constraints_kernel<<<grid, block, 0, stream>>>(kind, color, position_x, position_y, position_z, inverse_mass, lambda, error_flag, constraint_count, columns, rows, rest_length, compliance, substep_seconds);
        check_cuda(cudaGetLastError(), "solve cloth distance constraints kernel");
    }

    void launch_solve_sphere_collision(const cudaStream_t stream, const unsigned grid, const unsigned block, float* position_x, float* position_y, float* position_z, const float* inverse_mass, int* error_flag, const std::uint32_t vertex_count, const float center_x, const float center_y, const float center_z, const float radius) {
        solve_sphere_collision_kernel<<<grid, block, 0, stream>>>(position_x, position_y, position_z, inverse_mass, error_flag, vertex_count, center_x, center_y, center_z, radius);
        check_cuda(cudaGetLastError(), "solve cloth sphere collision kernel");
    }

    void launch_update_velocities(const cudaStream_t stream, const unsigned grid, const unsigned block, const float* position_x, const float* position_y, const float* position_z, const float* previous_x, const float* previous_y, const float* previous_z, float* velocity_x, float* velocity_y, float* velocity_z, const float* inverse_mass, const std::uint32_t vertex_count, const float substep_seconds) {
        update_velocities_kernel<<<grid, block, 0, stream>>>(position_x, position_y, position_z, previous_x, previous_y, previous_z, velocity_x, velocity_y, velocity_z, inverse_mass, vertex_count, substep_seconds);
        check_cuda(cudaGetLastError(), "update cloth velocities kernel");
    }

    void launch_accumulate_normals(const cudaStream_t stream, const unsigned grid, const unsigned block, const float* position_x, const float* position_y, const float* position_z, float* normal_x, float* normal_y, float* normal_z, const std::uint32_t* indices, int* error_flag, const std::uint32_t triangle_count) {
        accumulate_normals_kernel<<<grid, block, 0, stream>>>(position_x, position_y, position_z, normal_x, normal_y, normal_z, indices, error_flag, triangle_count);
        check_cuda(cudaGetLastError(), "accumulate cloth normals kernel");
    }

    void launch_normalize_normals(const cudaStream_t stream, const unsigned grid, const unsigned block, float* normal_x, float* normal_y, float* normal_z, int* error_flag, const std::uint32_t vertex_count) {
        normalize_normals_kernel<<<grid, block, 0, stream>>>(normal_x, normal_y, normal_z, error_flag, vertex_count);
        check_cuda(cudaGetLastError(), "normalize cloth normals kernel");
    }
} // namespace xayah::projects::cloth::cuda
