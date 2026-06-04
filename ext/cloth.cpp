module;
#include "cloth.h"

#include <cuda_runtime.h>

module cloth;
import std;

namespace {
    std::uint32_t cloth_index(const std::uint32_t column, const std::uint32_t row, const std::uint32_t columns) {
        return row * columns + column;
    }

    float length2(const float x, const float z) {
        return std::sqrt(x * x + z * z);
    }

    unsigned ceil_div_u32(const std::uint32_t value, const std::uint32_t divisor) {
        return (value + divisor - 1u) / divisor;
    }

    void validate_config(const spectra::ClothConfig& config, const spectra::ClothSphereCollider& collider) {
        if (config.columns < 3u || config.rows < 3u) throw std::runtime_error("CUDA cloth grid must be at least 3 x 3");
        if (config.width <= 0.0f || config.depth <= 0.0f) throw std::runtime_error("Cloth size must be positive");
        if (config.velocity_damping < 0.0f || config.velocity_damping > 1.0f) throw std::runtime_error("Cloth velocity_damping must be in [0, 1]");
        if (config.max_substep_seconds <= 0.0f) throw std::runtime_error("Cloth max_substep_seconds must be positive");
        if (config.solver_iterations == 0u) throw std::runtime_error("Cloth solver_iterations must be positive");
        if (config.stretch_compliance < 0.0f || config.shear_compliance < 0.0f || config.bend_compliance < 0.0f) throw std::runtime_error("Cloth compliance values must be non-negative");
        if (config.collision_margin < 0.0f) throw std::runtime_error("Cloth collision_margin must be non-negative");
        if (collider.radius <= 0.0f) throw std::runtime_error("Cloth sphere collider radius must be positive");
        const std::uint64_t vertex_count = static_cast<std::uint64_t>(config.columns) * static_cast<std::uint64_t>(config.rows);
        if (vertex_count == 0u || vertex_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Cloth grid is too large");
    }

    void check_cuda(const cudaError_t status, const char* what) {
        if (status == cudaSuccess) return;
        throw std::runtime_error(std::string{what} + ": " + cudaGetErrorString(status));
    }

    cudaStream_t cloth_stream(const void* stream) {
        return reinterpret_cast<cudaStream_t>(const_cast<void*>(stream));
    }

    void cuda_malloc_float(float*& pointer, const std::uint32_t count, const char* label) {
        if (count == 0u) return;
        check_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer), static_cast<std::size_t>(count) * sizeof(float)), label);
    }
} // namespace

namespace spectra {
    ClothSolver::ClothSolver(const ClothConfig& config, const ClothSphereCollider& collider) : config{config}, collider{collider} {
        validate_config(this->config, this->collider);

        this->host.columns                          = this->config.columns;
        this->host.rows                             = this->config.rows;
        this->host.dx                               = this->config.width / static_cast<float>(this->config.columns - 1u);
        this->host.dz                               = this->config.depth / static_cast<float>(this->config.rows - 1u);
        this->host.shear_rest_length                = length2(this->host.dx, this->host.dz);
        this->host.vertex_count                     = this->config.columns * this->config.rows;
        this->host.horizontal_constraint_count      = this->config.rows * (this->config.columns - 1u);
        this->host.vertical_constraint_count        = (this->config.rows - 1u) * this->config.columns;
        this->host.shear_constraint_count           = (this->config.rows - 1u) * (this->config.columns - 1u);
        this->host.horizontal_bend_constraint_count = this->config.rows * (this->config.columns - 2u);
        this->host.vertical_bend_constraint_count   = (this->config.rows - 2u) * this->config.columns;
        this->host.vertex_grid                      = ceil_div_u32(this->host.vertex_count, this->host.block_size);

        this->host.indices.reserve(static_cast<std::size_t>(this->config.columns - 1u) * static_cast<std::size_t>(this->config.rows - 1u) * 6u);
        for (std::uint32_t row = 0; row + 1u < this->config.rows; ++row) {
            for (std::uint32_t column = 0; column + 1u < this->config.columns; ++column) {
                const std::uint32_t i0 = cloth_index(column, row, this->config.columns);
                const std::uint32_t i1 = cloth_index(column + 1u, row, this->config.columns);
                const std::uint32_t i2 = cloth_index(column, row + 1u, this->config.columns);
                const std::uint32_t i3 = cloth_index(column + 1u, row + 1u, this->config.columns);
                this->host.indices.emplace_back(i0);
                this->host.indices.emplace_back(i2);
                this->host.indices.emplace_back(i1);
                this->host.indices.emplace_back(i1);
                this->host.indices.emplace_back(i2);
                this->host.indices.emplace_back(i3);
            }
        }

        this->host.triangle_count = static_cast<std::uint32_t>(this->host.indices.size() / 3u);
        this->host.triangle_grid  = ceil_div_u32(this->host.triangle_count, this->host.block_size);
        this->host.position_x.resize(this->host.vertex_count);
        this->host.position_y.resize(this->host.vertex_count);
        this->host.position_z.resize(this->host.vertex_count);
        this->host.normal_x.resize(this->host.vertex_count);
        this->host.normal_y.resize(this->host.vertex_count);
        this->host.normal_z.resize(this->host.vertex_count);
        this->host.vertices.resize(this->host.vertex_count);

        this->create_device();
        this->reset();
    }

    ClothSolver::~ClothSolver() noexcept {
        this->destroy_device();
    }

    void ClothSolver::create_device() {
        if (this->device.stream != nullptr) throw std::runtime_error("Cloth CUDA device is already initialized");
        try {
            cudaStream_t stream{};
            check_cuda(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags cloth");
            this->device.stream = stream;

            cuda_malloc_float(this->device.position_x, this->host.vertex_count, "cudaMalloc cloth position_x");
            cuda_malloc_float(this->device.position_y, this->host.vertex_count, "cudaMalloc cloth position_y");
            cuda_malloc_float(this->device.position_z, this->host.vertex_count, "cudaMalloc cloth position_z");
            cuda_malloc_float(this->device.previous_x, this->host.vertex_count, "cudaMalloc cloth previous_x");
            cuda_malloc_float(this->device.previous_y, this->host.vertex_count, "cudaMalloc cloth previous_y");
            cuda_malloc_float(this->device.previous_z, this->host.vertex_count, "cudaMalloc cloth previous_z");
            cuda_malloc_float(this->device.velocity_x, this->host.vertex_count, "cudaMalloc cloth velocity_x");
            cuda_malloc_float(this->device.velocity_y, this->host.vertex_count, "cudaMalloc cloth velocity_y");
            cuda_malloc_float(this->device.velocity_z, this->host.vertex_count, "cudaMalloc cloth velocity_z");
            cuda_malloc_float(this->device.inverse_mass, this->host.vertex_count, "cudaMalloc cloth inverse_mass");
            cuda_malloc_float(this->device.normal_x, this->host.vertex_count, "cudaMalloc cloth normal_x");
            cuda_malloc_float(this->device.normal_y, this->host.vertex_count, "cudaMalloc cloth normal_y");
            cuda_malloc_float(this->device.normal_z, this->host.vertex_count, "cudaMalloc cloth normal_z");
            cuda_malloc_float(this->device.horizontal_lambda, this->host.horizontal_constraint_count, "cudaMalloc cloth horizontal_lambda");
            cuda_malloc_float(this->device.vertical_lambda, this->host.vertical_constraint_count, "cudaMalloc cloth vertical_lambda");
            cuda_malloc_float(this->device.shear_down_lambda, this->host.shear_constraint_count, "cudaMalloc cloth shear_down_lambda");
            cuda_malloc_float(this->device.shear_up_lambda, this->host.shear_constraint_count, "cudaMalloc cloth shear_up_lambda");
            cuda_malloc_float(this->device.horizontal_bend_lambda, this->host.horizontal_bend_constraint_count, "cudaMalloc cloth horizontal_bend_lambda");
            cuda_malloc_float(this->device.vertical_bend_lambda, this->host.vertical_bend_constraint_count, "cudaMalloc cloth vertical_bend_lambda");

            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.indices), this->host.indices.size() * sizeof(std::uint32_t)), "cudaMalloc cloth indices");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device.error_flag), sizeof(int)), "cudaMalloc cloth error_flag");
            check_cuda(cudaMemcpyAsync(this->device.indices, this->host.indices.data(), this->host.indices.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth indices");
            check_cuda(cudaStreamSynchronize(cloth_stream(this->device.stream)), "cudaStreamSynchronize cloth create");
        } catch (...) {
            this->destroy_device();
            throw;
        }
    }

    void ClothSolver::destroy_device() noexcept {
        try {
            if (this->device.stream != nullptr) cudaStreamSynchronize(cloth_stream(this->device.stream));
            if (this->device.position_x != nullptr) cudaFree(this->device.position_x);
            if (this->device.position_y != nullptr) cudaFree(this->device.position_y);
            if (this->device.position_z != nullptr) cudaFree(this->device.position_z);
            if (this->device.previous_x != nullptr) cudaFree(this->device.previous_x);
            if (this->device.previous_y != nullptr) cudaFree(this->device.previous_y);
            if (this->device.previous_z != nullptr) cudaFree(this->device.previous_z);
            if (this->device.velocity_x != nullptr) cudaFree(this->device.velocity_x);
            if (this->device.velocity_y != nullptr) cudaFree(this->device.velocity_y);
            if (this->device.velocity_z != nullptr) cudaFree(this->device.velocity_z);
            if (this->device.inverse_mass != nullptr) cudaFree(this->device.inverse_mass);
            if (this->device.normal_x != nullptr) cudaFree(this->device.normal_x);
            if (this->device.normal_y != nullptr) cudaFree(this->device.normal_y);
            if (this->device.normal_z != nullptr) cudaFree(this->device.normal_z);
            if (this->device.indices != nullptr) cudaFree(this->device.indices);
            if (this->device.horizontal_lambda != nullptr) cudaFree(this->device.horizontal_lambda);
            if (this->device.vertical_lambda != nullptr) cudaFree(this->device.vertical_lambda);
            if (this->device.shear_down_lambda != nullptr) cudaFree(this->device.shear_down_lambda);
            if (this->device.shear_up_lambda != nullptr) cudaFree(this->device.shear_up_lambda);
            if (this->device.horizontal_bend_lambda != nullptr) cudaFree(this->device.horizontal_bend_lambda);
            if (this->device.vertical_bend_lambda != nullptr) cudaFree(this->device.vertical_bend_lambda);
            if (this->device.error_flag != nullptr) cudaFree(this->device.error_flag);
            if (this->device.stream != nullptr) cudaStreamDestroy(cloth_stream(this->device.stream));
        } catch (...) {
        }
        this->device = {};
    }

    void ClothSolver::clear_constraint_lambdas(float* lambda, const std::uint32_t count) {
        if (count == 0u) return;
        check_cuda(cudaMemsetAsync(lambda, 0, static_cast<std::size_t>(count) * sizeof(float), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth lambda");
    }

    void ClothSolver::solve_constraint_batch(const std::uint32_t kind, const std::uint32_t color, float* lambda, const std::uint32_t count, const float compliance, const float rest_length, const float substep_seconds) {
        if (count == 0u) return;
        spectra::cloth_cuda::launch_solve_distance_constraints(cloth_stream(this->device.stream), ceil_div_u32(count, this->host.block_size), this->host.block_size, kind, color, this->device.position_x, this->device.position_y, this->device.position_z, this->device.inverse_mass, lambda, this->device.error_flag, count, this->host.columns, this->host.rows, rest_length, compliance, substep_seconds);
    }

    void ClothSolver::reset() {
        if (this->device.stream == nullptr) throw std::runtime_error("Cannot reset cloth before CUDA device creation");
        check_cuda(cudaMemsetAsync(this->device.error_flag, 0, sizeof(int), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth error_flag");
        spectra::cloth_cuda::launch_reset(cloth_stream(this->device.stream), this->host.vertex_grid, this->host.block_size, this->device.position_x, this->device.position_y, this->device.position_z, this->device.previous_x, this->device.previous_y, this->device.previous_z, this->device.velocity_x, this->device.velocity_y, this->device.velocity_z, this->device.inverse_mass, this->host.columns, this->host.rows, this->config.origin[0], this->config.origin[1], this->config.origin[2], this->host.dx, this->host.dz);
        this->compute_normals();
    }

    void ClothSolver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("Cloth delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        if (this->device.stream == nullptr) throw std::runtime_error("Cannot step cloth before CUDA device creation");

        check_cuda(cudaMemsetAsync(this->device.error_flag, 0, sizeof(int), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth error_flag");
        float remaining_seconds = delta_seconds;
        while (remaining_seconds > 0.0f) {
            const float substep_seconds = std::min(remaining_seconds, this->config.max_substep_seconds);
            remaining_seconds -= substep_seconds;
            const float damping = std::pow(this->config.velocity_damping, substep_seconds * 60.0f);
            spectra::cloth_cuda::launch_integrate(cloth_stream(this->device.stream), this->host.vertex_grid, this->host.block_size, this->device.position_x, this->device.position_y, this->device.position_z, this->device.previous_x, this->device.previous_y, this->device.previous_z, this->device.velocity_x, this->device.velocity_y, this->device.velocity_z, this->device.inverse_mass, this->host.vertex_count, this->config.gravity[0], this->config.gravity[1], this->config.gravity[2], substep_seconds, damping);

            this->clear_constraint_lambdas(this->device.horizontal_lambda, this->host.horizontal_constraint_count);
            this->clear_constraint_lambdas(this->device.vertical_lambda, this->host.vertical_constraint_count);
            this->clear_constraint_lambdas(this->device.shear_down_lambda, this->host.shear_constraint_count);
            this->clear_constraint_lambdas(this->device.shear_up_lambda, this->host.shear_constraint_count);
            this->clear_constraint_lambdas(this->device.horizontal_bend_lambda, this->host.horizontal_bend_constraint_count);
            this->clear_constraint_lambdas(this->device.vertical_bend_lambda, this->host.vertical_bend_constraint_count);

            for (std::uint32_t iteration = 0; iteration < this->config.solver_iterations; ++iteration) {
                for (std::uint32_t color = 0; color < 2u; ++color) {
                    this->solve_constraint_batch(0u, color, this->device.horizontal_lambda, this->host.horizontal_constraint_count, this->config.stretch_compliance, this->host.dx, substep_seconds);
                    this->solve_constraint_batch(1u, color, this->device.vertical_lambda, this->host.vertical_constraint_count, this->config.stretch_compliance, this->host.dz, substep_seconds);
                }
                for (std::uint32_t color = 0; color < 4u; ++color) {
                    this->solve_constraint_batch(2u, color, this->device.shear_down_lambda, this->host.shear_constraint_count, this->config.shear_compliance, this->host.shear_rest_length, substep_seconds);
                    this->solve_constraint_batch(3u, color, this->device.shear_up_lambda, this->host.shear_constraint_count, this->config.shear_compliance, this->host.shear_rest_length, substep_seconds);
                    this->solve_constraint_batch(4u, color, this->device.horizontal_bend_lambda, this->host.horizontal_bend_constraint_count, this->config.bend_compliance, this->host.dx * 2.0f, substep_seconds);
                    this->solve_constraint_batch(5u, color, this->device.vertical_bend_lambda, this->host.vertical_bend_constraint_count, this->config.bend_compliance, this->host.dz * 2.0f, substep_seconds);
                }
                spectra::cloth_cuda::launch_solve_sphere_collision(cloth_stream(this->device.stream), this->host.vertex_grid, this->host.block_size, this->device.position_x, this->device.position_y, this->device.position_z, this->device.inverse_mass, this->device.error_flag, this->host.vertex_count, this->collider.center[0], this->collider.center[1], this->collider.center[2], this->collider.radius + this->config.collision_margin);
            }
            spectra::cloth_cuda::launch_update_velocities(cloth_stream(this->device.stream), this->host.vertex_grid, this->host.block_size, this->device.position_x, this->device.position_y, this->device.position_z, this->device.previous_x, this->device.previous_y, this->device.previous_z, this->device.velocity_x, this->device.velocity_y, this->device.velocity_z, this->device.inverse_mass, this->host.vertex_count, substep_seconds);
        }
        this->compute_normals();
    }

    void ClothSolver::compute_normals() {
        check_cuda(cudaMemsetAsync(this->device.normal_x, 0, static_cast<std::size_t>(this->host.vertex_count) * sizeof(float), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth normal_x");
        check_cuda(cudaMemsetAsync(this->device.normal_y, 0, static_cast<std::size_t>(this->host.vertex_count) * sizeof(float), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth normal_y");
        check_cuda(cudaMemsetAsync(this->device.normal_z, 0, static_cast<std::size_t>(this->host.vertex_count) * sizeof(float), cloth_stream(this->device.stream)), "cudaMemsetAsync cloth normal_z");
        spectra::cloth_cuda::launch_accumulate_normals(cloth_stream(this->device.stream), this->host.triangle_grid, this->host.block_size, this->device.position_x, this->device.position_y, this->device.position_z, this->device.normal_x, this->device.normal_y, this->device.normal_z, this->device.indices, this->device.error_flag, this->host.triangle_count);
        spectra::cloth_cuda::launch_normalize_normals(cloth_stream(this->device.stream), this->host.vertex_grid, this->host.block_size, this->device.normal_x, this->device.normal_y, this->device.normal_z, this->device.error_flag, this->host.vertex_count);
    }

    const std::vector<ClothVertex>& ClothSolver::download_vertices() const {
        int error_flag          = 0;
        const std::size_t bytes = static_cast<std::size_t>(this->host.vertex_count) * sizeof(float);
        check_cuda(cudaMemcpyAsync(this->host.position_x.data(), this->device.position_x, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth position_x download");
        check_cuda(cudaMemcpyAsync(this->host.position_y.data(), this->device.position_y, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth position_y download");
        check_cuda(cudaMemcpyAsync(this->host.position_z.data(), this->device.position_z, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth position_z download");
        check_cuda(cudaMemcpyAsync(this->host.normal_x.data(), this->device.normal_x, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth normal_x download");
        check_cuda(cudaMemcpyAsync(this->host.normal_y.data(), this->device.normal_y, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth normal_y download");
        check_cuda(cudaMemcpyAsync(this->host.normal_z.data(), this->device.normal_z, bytes, cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth normal_z download");
        check_cuda(cudaMemcpyAsync(&error_flag, this->device.error_flag, sizeof(int), cudaMemcpyDeviceToHost, cloth_stream(this->device.stream)), "cudaMemcpyAsync cloth error download");
        check_cuda(cudaStreamSynchronize(cloth_stream(this->device.stream)), "cudaStreamSynchronize cloth download");
        if (error_flag != 0) throw std::runtime_error("Cloth CUDA kernel reported an invalid simulation state");

        for (std::uint32_t index = 0; index < this->host.vertex_count; ++index) {
            this->host.vertices[index].position = {this->host.position_x[index], this->host.position_y[index], this->host.position_z[index]};
            this->host.vertices[index].normal   = {this->host.normal_x[index], this->host.normal_y[index], this->host.normal_z[index]};
        }
        return this->host.vertices;
    }

    const std::vector<ClothVertex>& ClothSolver::mesh_vertices() const {
        return this->download_vertices();
    }

    const std::vector<std::uint32_t>& ClothSolver::mesh_indices() const {
        return this->host.indices;
    }
} // namespace spectra
