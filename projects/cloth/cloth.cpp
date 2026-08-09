module;
#include "cloth.h"

#include <cuda_runtime.h>

module xayah.projects.cloth;
import std;

namespace xayah::projects::cloth {
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

        void validate_config(const SolverConfiguration& configuration, const SphereCollider& collider) {
            if (configuration.columns < 3u || configuration.rows < 3u) throw std::runtime_error("CUDA cloth grid must be at least 3 x 3");
            if (configuration.width <= 0.0f || configuration.depth <= 0.0f) throw std::runtime_error("Cloth size must be positive");
            if (configuration.velocity_damping < 0.0f || configuration.velocity_damping > 1.0f) throw std::runtime_error("Cloth velocity_damping must be in [0, 1]");
            if (configuration.maximum_substep_seconds <= 0.0f) throw std::runtime_error("Cloth maximum_substep_seconds must be positive");
            if (configuration.solver_iterations == 0u) throw std::runtime_error("Cloth solver_iterations must be positive");
            if (configuration.stretch_compliance < 0.0f || configuration.shear_compliance < 0.0f || configuration.bend_compliance < 0.0f) throw std::runtime_error("Cloth compliance values must be non-negative");
            if (configuration.collision_margin < 0.0f) throw std::runtime_error("Cloth collision_margin must be non-negative");
            if (collider.radius <= 0.0f) throw std::runtime_error("Cloth sphere collider radius must be positive");
            const std::uint64_t vertex_count = static_cast<std::uint64_t>(configuration.columns) * static_cast<std::uint64_t>(configuration.rows);
            if (vertex_count == 0u || vertex_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Cloth grid is too large");
        }

        void check_cuda(const cudaError_t status, const char* what) {
            if (status == cudaSuccess) return;
            throw std::runtime_error(std::string{what} + ": " + cudaGetErrorString(status));
        }

        cudaStream_t as_cuda_stream(const void* cuda_stream) {
            return reinterpret_cast<cudaStream_t>(const_cast<void*>(cuda_stream));
        }

        void cuda_malloc_float(float*& pointer, const std::uint32_t count, const char* label) {
            if (count == 0u) return;
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&pointer), static_cast<std::size_t>(count) * sizeof(float)), label);
        }
    } // namespace

    Solver::Solver(const SolverConfiguration& configuration, const SphereCollider& collider) : configuration{configuration}, collider{collider} {
        validate_config(this->configuration, this->collider);

        this->host_data.columns                          = this->configuration.columns;
        this->host_data.rows                             = this->configuration.rows;
        this->host_data.column_spacing                   = this->configuration.width / static_cast<float>(this->configuration.columns - 1u);
        this->host_data.row_spacing                      = this->configuration.depth / static_cast<float>(this->configuration.rows - 1u);
        this->host_data.shear_rest_length                = length2(this->host_data.column_spacing, this->host_data.row_spacing);
        this->host_data.vertex_count                     = this->configuration.columns * this->configuration.rows;
        this->host_data.horizontal_constraint_count      = this->configuration.rows * (this->configuration.columns - 1u);
        this->host_data.vertical_constraint_count        = (this->configuration.rows - 1u) * this->configuration.columns;
        this->host_data.shear_constraint_count           = (this->configuration.rows - 1u) * (this->configuration.columns - 1u);
        this->host_data.horizontal_bend_constraint_count = this->configuration.rows * (this->configuration.columns - 2u);
        this->host_data.vertical_bend_constraint_count   = (this->configuration.rows - 2u) * this->configuration.columns;
        this->host_data.vertex_block_count               = ceil_div_u32(this->host_data.vertex_count, this->host_data.threads_per_block);

        this->host_data.indices.reserve(static_cast<std::size_t>(this->configuration.columns - 1u) * static_cast<std::size_t>(this->configuration.rows - 1u) * 6u);
        for (std::uint32_t row = 0; row + 1u < this->configuration.rows; ++row) {
            for (std::uint32_t column = 0; column + 1u < this->configuration.columns; ++column) {
                const std::uint32_t i0 = cloth_index(column, row, this->configuration.columns);
                const std::uint32_t i1 = cloth_index(column + 1u, row, this->configuration.columns);
                const std::uint32_t i2 = cloth_index(column, row + 1u, this->configuration.columns);
                const std::uint32_t i3 = cloth_index(column + 1u, row + 1u, this->configuration.columns);
                this->host_data.indices.emplace_back(i0);
                this->host_data.indices.emplace_back(i2);
                this->host_data.indices.emplace_back(i1);
                this->host_data.indices.emplace_back(i1);
                this->host_data.indices.emplace_back(i2);
                this->host_data.indices.emplace_back(i3);
            }
        }

        this->allocate_device_data();
        this->reset();
    }

    Solver::~Solver() noexcept {
        this->release_device_data();
    }

    void Solver::set_configuration(const SolverConfiguration& configuration) noexcept {
        this->configuration = configuration;
    }

    void Solver::allocate_device_data() {
        if (this->device_data.cuda_stream != nullptr) throw std::runtime_error("Cloth CUDA device_data is already initialized");
        try {
            cudaStream_t cuda_stream{};
            check_cuda(cudaStreamCreateWithFlags(&cuda_stream, cudaStreamNonBlocking), "cudaStreamCreateWithFlags cloth");
            this->device_data.cuda_stream = cuda_stream;

            cuda_malloc_float(this->device_data.position_x, this->host_data.vertex_count, "cudaMalloc cloth position_x");
            cuda_malloc_float(this->device_data.position_y, this->host_data.vertex_count, "cudaMalloc cloth position_y");
            cuda_malloc_float(this->device_data.position_z, this->host_data.vertex_count, "cudaMalloc cloth position_z");
            cuda_malloc_float(this->device_data.previous_x, this->host_data.vertex_count, "cudaMalloc cloth previous_x");
            cuda_malloc_float(this->device_data.previous_y, this->host_data.vertex_count, "cudaMalloc cloth previous_y");
            cuda_malloc_float(this->device_data.previous_z, this->host_data.vertex_count, "cudaMalloc cloth previous_z");
            cuda_malloc_float(this->device_data.velocity_x, this->host_data.vertex_count, "cudaMalloc cloth velocity_x");
            cuda_malloc_float(this->device_data.velocity_y, this->host_data.vertex_count, "cudaMalloc cloth velocity_y");
            cuda_malloc_float(this->device_data.velocity_z, this->host_data.vertex_count, "cudaMalloc cloth velocity_z");
            cuda_malloc_float(this->device_data.inverse_mass, this->host_data.vertex_count, "cudaMalloc cloth inverse_mass");
            cuda_malloc_float(this->device_data.horizontal_lambda, this->host_data.horizontal_constraint_count, "cudaMalloc cloth horizontal_lambda");
            cuda_malloc_float(this->device_data.vertical_lambda, this->host_data.vertical_constraint_count, "cudaMalloc cloth vertical_lambda");
            cuda_malloc_float(this->device_data.shear_down_lambda, this->host_data.shear_constraint_count, "cudaMalloc cloth shear_down_lambda");
            cuda_malloc_float(this->device_data.shear_up_lambda, this->host_data.shear_constraint_count, "cudaMalloc cloth shear_up_lambda");
            cuda_malloc_float(this->device_data.horizontal_bend_lambda, this->host_data.horizontal_bend_constraint_count, "cudaMalloc cloth horizontal_bend_lambda");
            cuda_malloc_float(this->device_data.vertical_bend_lambda, this->host_data.vertical_bend_constraint_count, "cudaMalloc cloth vertical_bend_lambda");

            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.indices), this->host_data.indices.size() * sizeof(std::uint32_t)), "cudaMalloc cloth indices");
            check_cuda(cudaMalloc(reinterpret_cast<void**>(&this->device_data.constraint_error_flag), sizeof(int)), "cudaMalloc cloth constraint_error_flag");
            check_cuda(cudaMemcpyAsync(this->device_data.indices, this->host_data.indices.data(), this->host_data.indices.size() * sizeof(std::uint32_t), cudaMemcpyHostToDevice, as_cuda_stream(this->device_data.cuda_stream)), "cudaMemcpyAsync cloth indices");
            check_cuda(cudaStreamSynchronize(as_cuda_stream(this->device_data.cuda_stream)), "cudaStreamSynchronize cloth create");
        } catch (...) {
            this->release_device_data();
            throw;
        }
    }

    void Solver::release_device_data() noexcept {
        try {
            if (this->device_data.cuda_stream != nullptr) cudaStreamSynchronize(as_cuda_stream(this->device_data.cuda_stream));
            if (this->device_data.position_x != nullptr) cudaFree(this->device_data.position_x);
            if (this->device_data.position_y != nullptr) cudaFree(this->device_data.position_y);
            if (this->device_data.position_z != nullptr) cudaFree(this->device_data.position_z);
            if (this->device_data.previous_x != nullptr) cudaFree(this->device_data.previous_x);
            if (this->device_data.previous_y != nullptr) cudaFree(this->device_data.previous_y);
            if (this->device_data.previous_z != nullptr) cudaFree(this->device_data.previous_z);
            if (this->device_data.velocity_x != nullptr) cudaFree(this->device_data.velocity_x);
            if (this->device_data.velocity_y != nullptr) cudaFree(this->device_data.velocity_y);
            if (this->device_data.velocity_z != nullptr) cudaFree(this->device_data.velocity_z);
            if (this->device_data.inverse_mass != nullptr) cudaFree(this->device_data.inverse_mass);
            if (this->device_data.indices != nullptr) cudaFree(this->device_data.indices);
            if (this->device_data.horizontal_lambda != nullptr) cudaFree(this->device_data.horizontal_lambda);
            if (this->device_data.vertical_lambda != nullptr) cudaFree(this->device_data.vertical_lambda);
            if (this->device_data.shear_down_lambda != nullptr) cudaFree(this->device_data.shear_down_lambda);
            if (this->device_data.shear_up_lambda != nullptr) cudaFree(this->device_data.shear_up_lambda);
            if (this->device_data.horizontal_bend_lambda != nullptr) cudaFree(this->device_data.horizontal_bend_lambda);
            if (this->device_data.vertical_bend_lambda != nullptr) cudaFree(this->device_data.vertical_bend_lambda);
            if (this->device_data.constraint_error_flag != nullptr) cudaFree(this->device_data.constraint_error_flag);
            if (this->device_data.cuda_stream != nullptr) cudaStreamDestroy(as_cuda_stream(this->device_data.cuda_stream));
        } catch (...) {
        }
        this->device_data = {};
    }

    void Solver::clear_constraint_lambdas(float* lambda, const std::uint32_t count) {
        if (count == 0u) return;
        check_cuda(cudaMemsetAsync(lambda, 0, static_cast<std::size_t>(count) * sizeof(float), as_cuda_stream(this->device_data.cuda_stream)), "cudaMemsetAsync cloth lambda");
    }

    void Solver::solve_constraint_batch(const std::uint32_t constraint_kind, const std::uint32_t constraint_color, float* lambda, const std::uint32_t count, const float compliance, const float rest_length, const float substep_seconds) {
        if (count == 0u) return;
        cuda::launch_solve_distance_constraints(as_cuda_stream(this->device_data.cuda_stream), ceil_div_u32(count, this->host_data.threads_per_block), this->host_data.threads_per_block, constraint_kind, constraint_color, this->device_data.position_x, this->device_data.position_y, this->device_data.position_z, this->device_data.inverse_mass, lambda, this->device_data.constraint_error_flag, count, this->host_data.columns, this->host_data.rows, rest_length, compliance, substep_seconds);
    }

    void Solver::reset() {
        if (this->device_data.cuda_stream == nullptr) throw std::runtime_error("Cannot reset cloth before CUDA device_data creation");
        check_cuda(cudaMemsetAsync(this->device_data.constraint_error_flag, 0, sizeof(int), as_cuda_stream(this->device_data.cuda_stream)), "cudaMemsetAsync cloth constraint_error_flag");
        cuda::launch_reset(as_cuda_stream(this->device_data.cuda_stream), this->host_data.vertex_block_count, this->host_data.threads_per_block, this->device_data.position_x, this->device_data.position_y, this->device_data.position_z, this->device_data.previous_x, this->device_data.previous_y, this->device_data.previous_z, this->device_data.velocity_x, this->device_data.velocity_y, this->device_data.velocity_z, this->device_data.inverse_mass, this->host_data.columns, this->host_data.rows, this->configuration.origin[0], this->configuration.origin[1], this->configuration.origin[2], this->host_data.column_spacing, this->host_data.row_spacing);
    }

    void Solver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("Cloth delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        if (this->device_data.cuda_stream == nullptr) throw std::runtime_error("Cannot step cloth before CUDA device_data creation");

        check_cuda(cudaMemsetAsync(this->device_data.constraint_error_flag, 0, sizeof(int), as_cuda_stream(this->device_data.cuda_stream)), "cudaMemsetAsync cloth constraint_error_flag");
        float remaining_seconds = delta_seconds;
        while (remaining_seconds > 0.0f) {
            const float substep_seconds = std::min(remaining_seconds, this->configuration.maximum_substep_seconds);
            remaining_seconds -= substep_seconds;
            const float damping = std::pow(this->configuration.velocity_damping, substep_seconds * 60.0f);
            cuda::launch_integrate(as_cuda_stream(this->device_data.cuda_stream), this->host_data.vertex_block_count, this->host_data.threads_per_block, this->device_data.position_x, this->device_data.position_y, this->device_data.position_z, this->device_data.previous_x, this->device_data.previous_y, this->device_data.previous_z, this->device_data.velocity_x, this->device_data.velocity_y, this->device_data.velocity_z, this->device_data.inverse_mass, this->host_data.vertex_count, this->configuration.gravity[0], this->configuration.gravity[1], this->configuration.gravity[2], substep_seconds, damping);

            this->clear_constraint_lambdas(this->device_data.horizontal_lambda, this->host_data.horizontal_constraint_count);
            this->clear_constraint_lambdas(this->device_data.vertical_lambda, this->host_data.vertical_constraint_count);
            this->clear_constraint_lambdas(this->device_data.shear_down_lambda, this->host_data.shear_constraint_count);
            this->clear_constraint_lambdas(this->device_data.shear_up_lambda, this->host_data.shear_constraint_count);
            this->clear_constraint_lambdas(this->device_data.horizontal_bend_lambda, this->host_data.horizontal_bend_constraint_count);
            this->clear_constraint_lambdas(this->device_data.vertical_bend_lambda, this->host_data.vertical_bend_constraint_count);

            for (std::uint32_t iteration = 0; iteration < this->configuration.solver_iterations; ++iteration) {
                for (std::uint32_t constraint_color = 0; constraint_color < 2u; ++constraint_color) {
                    this->solve_constraint_batch(0u, constraint_color, this->device_data.horizontal_lambda, this->host_data.horizontal_constraint_count, this->configuration.stretch_compliance, this->host_data.column_spacing, substep_seconds);
                    this->solve_constraint_batch(1u, constraint_color, this->device_data.vertical_lambda, this->host_data.vertical_constraint_count, this->configuration.stretch_compliance, this->host_data.row_spacing, substep_seconds);
                }
                for (std::uint32_t constraint_color = 0; constraint_color < 4u; ++constraint_color) {
                    this->solve_constraint_batch(2u, constraint_color, this->device_data.shear_down_lambda, this->host_data.shear_constraint_count, this->configuration.shear_compliance, this->host_data.shear_rest_length, substep_seconds);
                    this->solve_constraint_batch(3u, constraint_color, this->device_data.shear_up_lambda, this->host_data.shear_constraint_count, this->configuration.shear_compliance, this->host_data.shear_rest_length, substep_seconds);
                    this->solve_constraint_batch(4u, constraint_color, this->device_data.horizontal_bend_lambda, this->host_data.horizontal_bend_constraint_count, this->configuration.bend_compliance, this->host_data.column_spacing * 2.0f, substep_seconds);
                    this->solve_constraint_batch(5u, constraint_color, this->device_data.vertical_bend_lambda, this->host_data.vertical_bend_constraint_count, this->configuration.bend_compliance, this->host_data.row_spacing * 2.0f, substep_seconds);
                }
                cuda::launch_solve_sphere_collision(as_cuda_stream(this->device_data.cuda_stream), this->host_data.vertex_block_count, this->host_data.threads_per_block, this->device_data.position_x, this->device_data.position_y, this->device_data.position_z, this->device_data.inverse_mass, this->device_data.constraint_error_flag, this->host_data.vertex_count, this->collider.center[0], this->collider.center[1], this->collider.center[2], this->collider.radius + this->configuration.collision_margin);
            }
            cuda::launch_update_velocities(as_cuda_stream(this->device_data.cuda_stream), this->host_data.vertex_block_count, this->host_data.threads_per_block, this->device_data.position_x, this->device_data.position_y, this->device_data.position_z, this->device_data.previous_x, this->device_data.previous_y, this->device_data.previous_z, this->device_data.velocity_x, this->device_data.velocity_y, this->device_data.velocity_z, this->device_data.inverse_mass, this->host_data.vertex_count, substep_seconds);
        }
    }

    CudaMeshView Solver::cuda_mesh() const noexcept {
        return {
            this->device_data.cuda_stream,
            this->device_data.position_x,
            this->device_data.position_y,
            this->device_data.position_z,
            this->host_data.vertex_count,
        };
    }
} // namespace xayah::projects::cloth
