#include "pyro.h"
#include <cmath>
#include <stdexcept>
#include <string>

namespace xayah::projects::pyro::cuda {
    constexpr std::uint32_t SMOKE_SIMULATION_FLOW_BOUNDARY_NO_SLIP_WALL   = 0;
    constexpr std::uint32_t SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL = 1;
    constexpr std::uint32_t SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW        = 2;
    constexpr std::uint32_t SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC       = 3;

    constexpr std::uint32_t SMOKE_SIMULATION_SCALAR_BOUNDARY_FIXED_VALUE = 0;
    constexpr std::uint32_t SMOKE_SIMULATION_SCALAR_BOUNDARY_ZERO_FLUX   = 1;
    constexpr std::uint32_t SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC    = 2;

    constexpr std::uint32_t SMOKE_SIMULATION_SCALAR_ADVECTION_MONOTONIC_CUBIC = 1;

    struct SmokeSimulationFlowBoundaryFaceDesc {
        std::uint32_t type{SMOKE_SIMULATION_FLOW_BOUNDARY_NO_SLIP_WALL};
        float velocity_x{0.0f};
        float velocity_y{0.0f};
        float velocity_z{0.0f};
        float pressure{0.0f};
    };

    struct SmokeSimulationFlowBoundaryConfig {
        SmokeSimulationFlowBoundaryFaceDesc x_minus{};
        SmokeSimulationFlowBoundaryFaceDesc x_plus{};
        SmokeSimulationFlowBoundaryFaceDesc y_minus{};
        SmokeSimulationFlowBoundaryFaceDesc y_plus{};
        SmokeSimulationFlowBoundaryFaceDesc z_minus{};
        SmokeSimulationFlowBoundaryFaceDesc z_plus{};
    };

    struct SmokeSimulationScalarBoundaryFaceDesc {
        std::uint32_t type{SMOKE_SIMULATION_SCALAR_BOUNDARY_FIXED_VALUE};
        float value{0.0f};
    };

    struct SmokeSimulationScalarBoundaryConfig {
        SmokeSimulationScalarBoundaryFaceDesc x_minus{};
        SmokeSimulationScalarBoundaryFaceDesc x_plus{};
        SmokeSimulationScalarBoundaryFaceDesc y_minus{};
        SmokeSimulationScalarBoundaryFaceDesc y_plus{};
        SmokeSimulationScalarBoundaryFaceDesc z_minus{};
        SmokeSimulationScalarBoundaryFaceDesc z_plus{};
    };

    SmokeSimulationFlowBoundaryConfig make_flow_boundary(const std::uint32_t* types, const float* velocity, const float* pressure) {
        if (types == nullptr || velocity == nullptr || pressure == nullptr) throw std::runtime_error("Pyro flow boundary arrays must not be null");
        return SmokeSimulationFlowBoundaryConfig{
            {types[0], velocity[0], velocity[1], velocity[2], pressure[0]},
            {types[1], velocity[3], velocity[4], velocity[5], pressure[1]},
            {types[2], velocity[6], velocity[7], velocity[8], pressure[2]},
            {types[3], velocity[9], velocity[10], velocity[11], pressure[3]},
            {types[4], velocity[12], velocity[13], velocity[14], pressure[4]},
            {types[5], velocity[15], velocity[16], velocity[17], pressure[5]},
        };
    }

    SmokeSimulationScalarBoundaryConfig make_scalar_boundary(const std::uint32_t* types, const float* values) {
        if (types == nullptr || values == nullptr) throw std::runtime_error("Pyro scalar boundary arrays must not be null");
        return SmokeSimulationScalarBoundaryConfig{
            {types[0], values[0]},
            {types[1], values[1]},
            {types[2], values[2]},
            {types[3], values[3]},
            {types[4], values[4]},
            {types[5], values[5]},
        };
    }

    __host__ __device__ std::uint64_t index_3d(const int x, const int y, const int z, const int sx, const int sy) {
        return static_cast<std::uint64_t>(z) * static_cast<std::uint64_t>(sx) * static_cast<std::uint64_t>(sy) + static_cast<std::uint64_t>(y) * static_cast<std::uint64_t>(sx) + static_cast<std::uint64_t>(x);
    }

    __host__ __device__ std::uint64_t index_velocity_x(const int i, const int j, const int k, const int nx, const int ny) {
        return static_cast<std::uint64_t>(k) * static_cast<std::uint64_t>(nx + 1) * static_cast<std::uint64_t>(ny) + static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(nx + 1) + static_cast<std::uint64_t>(i);
    }

    __host__ __device__ std::uint64_t index_velocity_y(const int i, const int j, const int k, const int nx, const int ny) {
        return static_cast<std::uint64_t>(k) * static_cast<std::uint64_t>(nx) * static_cast<std::uint64_t>(ny + 1) + static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(nx) + static_cast<std::uint64_t>(i);
    }

    __host__ __device__ std::uint64_t index_velocity_z(const int i, const int j, const int k, const int nx, const int ny) {
        return static_cast<std::uint64_t>(k) * static_cast<std::uint64_t>(nx) * static_cast<std::uint64_t>(ny) + static_cast<std::uint64_t>(j) * static_cast<std::uint64_t>(nx) + static_cast<std::uint64_t>(i);
    }

    __host__ __device__ int wrap_index(int value, const int size) {
        if (size <= 0) return 0;
        value %= size;
        if (value < 0) value += size;
        return value;
    }

    __host__ __device__ int clamp_int(const int value, const int low, const int high) {
        return value < low ? low : (value > high ? high : value);
    }

    __host__ __device__ bool cell_in_bounds(const int x, const int y, const int z, const int nx, const int ny, const int nz) {
        return x >= 0 && x < nx && y >= 0 && y < ny && z >= 0 && z < nz;
    }

    __host__ __device__ bool resolve_cell_coordinates(int& x, int& y, int& z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) x = wrap_index(x, nx);
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) y = wrap_index(y, ny);
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) z = wrap_index(z, nz);
        return cell_in_bounds(x, y, z, nx, ny, nz);
    }

    __host__ __device__ bool resolve_scalar_cell_coordinates(int& x, int& y, int& z, const int nx, const int ny, const int nz, const SmokeSimulationScalarBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nx > 0) x = wrap_index(x, nx);
        if (boundary.y_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && ny > 0) y = wrap_index(y, ny);
        if (boundary.z_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nz > 0) z = wrap_index(z, nz);
        return cell_in_bounds(x, y, z, nx, ny, nz);
    }

    __device__ bool load_occupancy(const uint8_t* occupancy, int x, int y, int z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (occupancy == nullptr) return false;
        if (!resolve_cell_coordinates(x, y, z, nx, ny, nz, boundary)) return true;
        return occupancy[index_3d(x, y, z, nx, ny)] != 0;
    }

    __device__ float load_scalar(const float* field, int x, int y, int z, const int nx, const int ny, const int nz, const SmokeSimulationScalarBoundaryConfig boundary) {
        if (x < 0 || x >= nx) {
            const auto [type, value] = x < 0 ? boundary.x_minus : boundary.x_plus;
            if (boundary.x_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nx > 0) {
                x = wrap_index(x, nx);
            } else if (type == SMOKE_SIMULATION_SCALAR_BOUNDARY_ZERO_FLUX && nx > 0) {
                x = x < 0 ? 0 : nx - 1;
            } else {
                return value;
            }
        }
        if (y < 0 || y >= ny) {
            const auto [type, value] = y < 0 ? boundary.y_minus : boundary.y_plus;
            if (boundary.y_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && ny > 0) {
                y = wrap_index(y, ny);
            } else if (type == SMOKE_SIMULATION_SCALAR_BOUNDARY_ZERO_FLUX && ny > 0) {
                y = y < 0 ? 0 : ny - 1;
            } else {
                return value;
            }
        }
        if (z < 0 || z >= nz) {
            const auto [type, value] = z < 0 ? boundary.z_minus : boundary.z_plus;
            if (boundary.z_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nz > 0) {
                z = wrap_index(z, nz);
            } else if (type == SMOKE_SIMULATION_SCALAR_BOUNDARY_ZERO_FLUX && nz > 0) {
                z = z < 0 ? 0 : nz - 1;
            } else {
                return value;
            }
        }
        return field[index_3d(x, y, z, nx, ny)];
    }

    __device__ float load_flow_cell(const float* field, int x, int y, int z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (x < 0 || x >= nx) {
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                x = wrap_index(x, nx);
            } else {
                x = x < 0 ? 0 : nx - 1;
            }
        }
        if (y < 0 || y >= ny) {
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                y = wrap_index(y, ny);
            } else {
                y = y < 0 ? 0 : ny - 1;
            }
        }
        if (z < 0 || z >= nz) {
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                z = wrap_index(z, nz);
            } else {
                z = z < 0 ? 0 : nz - 1;
            }
        }
        return field[index_3d(x, y, z, nx, ny)];
    }

    __device__ float load_center_velocity_component(const float* field, const int component_axis, int x, int y, int z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (x < 0 || x >= nx) {
            const auto face = x < 0 ? boundary.x_minus : boundary.x_plus;
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                x = wrap_index(x, nx);
            } else {
                const float interior = field[index_3d(x < 0 ? 0 : nx - 1, clamp_int(y, 0, ny - 1), clamp_int(z, 0, nz - 1), nx, ny)];
                float prescribed     = 0.0f;
                if (component_axis == 0) prescribed = face.velocity_x;
                if (component_axis == 1) prescribed = face.velocity_y;
                if (component_axis == 2) prescribed = face.velocity_z;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL && component_axis != 0) return interior;
                return 2.0f * prescribed - interior;
            }
        }
        if (y < 0 || y >= ny) {
            const auto face = y < 0 ? boundary.y_minus : boundary.y_plus;
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                y = wrap_index(y, ny);
            } else {
                const float interior = field[index_3d(clamp_int(x, 0, nx - 1), y < 0 ? 0 : ny - 1, clamp_int(z, 0, nz - 1), nx, ny)];
                float prescribed     = 0.0f;
                if (component_axis == 0) prescribed = face.velocity_x;
                if (component_axis == 1) prescribed = face.velocity_y;
                if (component_axis == 2) prescribed = face.velocity_z;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL && component_axis != 1) return interior;
                return 2.0f * prescribed - interior;
            }
        }
        if (z < 0 || z >= nz) {
            const auto face = z < 0 ? boundary.z_minus : boundary.z_plus;
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                z = wrap_index(z, nz);
            } else {
                const float interior = field[index_3d(clamp_int(x, 0, nx - 1), clamp_int(y, 0, ny - 1), z < 0 ? 0 : nz - 1, nx, ny)];
                float prescribed     = 0.0f;
                if (component_axis == 0) prescribed = face.velocity_x;
                if (component_axis == 1) prescribed = face.velocity_y;
                if (component_axis == 2) prescribed = face.velocity_z;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL && component_axis != 2) return interior;
                return 2.0f * prescribed - interior;
            }
        }
        return field[index_3d(x, y, z, nx, ny)];
    }

    __device__ float load_velocity_x(const float* field, int i, int j, int k, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (i < 0 || i > nx) {
            const auto face = i < 0 ? boundary.x_minus : boundary.x_plus;
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                i = wrap_index(i, nx);
            } else {
                const float interior = field[index_velocity_x(i < 0 ? 0 : nx, clamp_int(j, 0, ny - 1), clamp_int(k, 0, nz - 1), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                return 2.0f * face.velocity_x - interior;
            }
        }
        if (j < 0 || j >= ny) {
            const auto face = j < 0 ? boundary.y_minus : boundary.y_plus;
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                j = wrap_index(j, ny);
            } else {
                const float interior = field[index_velocity_x(clamp_int(i, 0, nx), j < 0 ? 0 : ny - 1, clamp_int(k, 0, nz - 1), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_x - interior;
            }
        }
        if (k < 0 || k >= nz) {
            const auto face = k < 0 ? boundary.z_minus : boundary.z_plus;
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                k = wrap_index(k, nz);
            } else {
                const float interior = field[index_velocity_x(clamp_int(i, 0, nx), clamp_int(j, 0, ny - 1), k < 0 ? 0 : nz - 1, nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_x - interior;
            }
        }
        return field[index_velocity_x(i, j, k, nx, ny)];
    }

    __device__ float load_velocity_y(const float* field, int i, int j, int k, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (i < 0 || i >= nx) {
            const auto face = i < 0 ? boundary.x_minus : boundary.x_plus;
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                i = wrap_index(i, nx);
            } else {
                const float interior = field[index_velocity_y(i < 0 ? 0 : nx - 1, clamp_int(j, 0, ny), clamp_int(k, 0, nz - 1), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_y - interior;
            }
        }
        if (j < 0 || j > ny) {
            const auto face = j < 0 ? boundary.y_minus : boundary.y_plus;
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                j = wrap_index(j, ny);
            } else {
                const float interior = field[index_velocity_y(clamp_int(i, 0, nx - 1), j < 0 ? 0 : ny, clamp_int(k, 0, nz - 1), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                return 2.0f * face.velocity_y - interior;
            }
        }
        if (k < 0 || k >= nz) {
            const auto face = k < 0 ? boundary.z_minus : boundary.z_plus;
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                k = wrap_index(k, nz);
            } else {
                const float interior = field[index_velocity_y(clamp_int(i, 0, nx - 1), clamp_int(j, 0, ny), k < 0 ? 0 : nz - 1, nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_y - interior;
            }
        }
        return field[index_velocity_y(i, j, k, nx, ny)];
    }

    __device__ float load_velocity_z(const float* field, int i, int j, int k, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (i < 0 || i >= nx) {
            const auto face = i < 0 ? boundary.x_minus : boundary.x_plus;
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                i = wrap_index(i, nx);
            } else {
                const float interior = field[index_velocity_z(i < 0 ? 0 : nx - 1, clamp_int(j, 0, ny - 1), clamp_int(k, 0, nz), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_z - interior;
            }
        }
        if (j < 0 || j >= ny) {
            const auto face = j < 0 ? boundary.y_minus : boundary.y_plus;
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                j = wrap_index(j, ny);
            } else {
                const float interior = field[index_velocity_z(clamp_int(i, 0, nx - 1), j < 0 ? 0 : ny - 1, clamp_int(k, 0, nz), nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW || face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_FREE_SLIP_WALL) return interior;
                return 2.0f * face.velocity_z - interior;
            }
        }
        if (k < 0 || k > nz) {
            const auto face = k < 0 ? boundary.z_minus : boundary.z_plus;
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                k = wrap_index(k, nz);
            } else {
                const float interior = field[index_velocity_z(clamp_int(i, 0, nx - 1), clamp_int(j, 0, ny - 1), k < 0 ? 0 : nz, nx, ny)];
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) return interior;
                return 2.0f * face.velocity_z - interior;
            }
        }
        return field[index_velocity_z(i, j, k, nx, ny)];
    }

    __device__ float monotonic_cubic_1d(const float p0, const float p1, const float p2, const float p3, const float t) {
        const float delta = p2 - p1;
        float m1          = 0.5f * (p2 - p0);
        float m2          = 0.5f * (p3 - p1);
        if (fabsf(delta) < 1.0e-6f) {
            m1 = 0.0f;
            m2 = 0.0f;
        } else {
            if (m1 * delta <= 0.0f) m1 = 0.0f;
            if (m2 * delta <= 0.0f) m2 = 0.0f;
        }
        const float t2 = t * t;
        const float t3 = t2 * t;
        return (2.0f * t3 - 3.0f * t2 + 1.0f) * p1 + (t3 - 2.0f * t2 + t) * m1 + (-2.0f * t3 + 3.0f * t2) * p2 + (t3 - t2) * m2;
    }

    __device__ float sample_scalar_linear(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationScalarBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }

        const float gx = x / h - 0.5f;
        const float gy = y / h - 0.5f;
        const float gz = z / h - 0.5f;
        const int x0   = static_cast<int>(floorf(gx));
        const int y0   = static_cast<int>(floorf(gy));
        const int z0   = static_cast<int>(floorf(gz));
        const int x1   = x0 + 1;
        const int y1   = y0 + 1;
        const int z1   = z0 + 1;
        const float tx = gx - static_cast<float>(x0);
        const float ty = gy - static_cast<float>(y0);
        const float tz = gz - static_cast<float>(z0);

        const float c000 = load_scalar(field, x0, y0, z0, nx, ny, nz, boundary);
        const float c100 = load_scalar(field, x1, y0, z0, nx, ny, nz, boundary);
        const float c010 = load_scalar(field, x0, y1, z0, nx, ny, nz, boundary);
        const float c110 = load_scalar(field, x1, y1, z0, nx, ny, nz, boundary);
        const float c001 = load_scalar(field, x0, y0, z1, nx, ny, nz, boundary);
        const float c101 = load_scalar(field, x1, y0, z1, nx, ny, nz, boundary);
        const float c011 = load_scalar(field, x0, y1, z1, nx, ny, nz, boundary);
        const float c111 = load_scalar(field, x1, y1, z1, nx, ny, nz, boundary);

        const float c00 = c000 + (c100 - c000) * tx;
        const float c10 = c010 + (c110 - c010) * tx;
        const float c01 = c001 + (c101 - c001) * tx;
        const float c11 = c011 + (c111 - c011) * tx;
        const float c0  = c00 + (c10 - c00) * ty;
        const float c1  = c01 + (c11 - c01) * ty;
        return c0 + (c1 - c0) * tz;
    }

    __device__ float sample_scalar_cubic(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationScalarBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_SCALAR_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }

        const float gx = x / h - 0.5f;
        const float gy = y / h - 0.5f;
        const float gz = z / h - 0.5f;
        const int x1   = static_cast<int>(floorf(gx));
        const int y1   = static_cast<int>(floorf(gy));
        const int z1   = static_cast<int>(floorf(gz));
        const float tx = gx - static_cast<float>(x1);
        const float ty = gy - static_cast<float>(y1);
        const float tz = gz - static_cast<float>(z1);

        float z_samples[4];
        for (int dz = 0; dz < 4; ++dz) {
            float y_samples[4];
            for (int dy = 0; dy < 4; ++dy) {
                const int yy   = y1 + dy - 1;
                const int zz   = z1 + dz - 1;
                const float p0 = load_scalar(field, x1 - 1, yy, zz, nx, ny, nz, boundary);
                const float p1 = load_scalar(field, x1, yy, zz, nx, ny, nz, boundary);
                const float p2 = load_scalar(field, x1 + 1, yy, zz, nx, ny, nz, boundary);
                const float p3 = load_scalar(field, x1 + 2, yy, zz, nx, ny, nz, boundary);
                y_samples[dy]  = monotonic_cubic_1d(p0, p1, p2, p3, tx);
            }
            z_samples[dz] = monotonic_cubic_1d(y_samples[0], y_samples[1], y_samples[2], y_samples[3], ty);
        }
        return monotonic_cubic_1d(z_samples[0], z_samples[1], z_samples[2], z_samples[3], tz);
    }

    __device__ float sample_velocity_x(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }

        const float gx = x / h;
        const float gy = y / h - 0.5f;
        const float gz = z / h - 0.5f;
        const int i0   = static_cast<int>(floorf(gx));
        const int j0   = static_cast<int>(floorf(gy));
        const int k0   = static_cast<int>(floorf(gz));
        const int i1   = i0 + 1;
        const int j1   = j0 + 1;
        const int k1   = k0 + 1;
        const float tx = gx - static_cast<float>(i0);
        const float ty = gy - static_cast<float>(j0);
        const float tz = gz - static_cast<float>(k0);

        const float c000 = load_velocity_x(field, i0, j0, k0, nx, ny, nz, boundary);
        const float c100 = load_velocity_x(field, i1, j0, k0, nx, ny, nz, boundary);
        const float c010 = load_velocity_x(field, i0, j1, k0, nx, ny, nz, boundary);
        const float c110 = load_velocity_x(field, i1, j1, k0, nx, ny, nz, boundary);
        const float c001 = load_velocity_x(field, i0, j0, k1, nx, ny, nz, boundary);
        const float c101 = load_velocity_x(field, i1, j0, k1, nx, ny, nz, boundary);
        const float c011 = load_velocity_x(field, i0, j1, k1, nx, ny, nz, boundary);
        const float c111 = load_velocity_x(field, i1, j1, k1, nx, ny, nz, boundary);

        const float c00 = c000 + (c100 - c000) * tx;
        const float c10 = c010 + (c110 - c010) * tx;
        const float c01 = c001 + (c101 - c001) * tx;
        const float c11 = c011 + (c111 - c011) * tx;
        const float c0  = c00 + (c10 - c00) * ty;
        const float c1  = c01 + (c11 - c01) * ty;
        return c0 + (c1 - c0) * tz;
    }

    __device__ float sample_velocity_x_cubic(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }
        const float gx = x / h;
        const float gy = y / h - 0.5f;
        const float gz = z / h - 0.5f;
        const int i1   = static_cast<int>(floorf(gx));
        const int j1   = static_cast<int>(floorf(gy));
        const int k1   = static_cast<int>(floorf(gz));
        const float tx = gx - static_cast<float>(i1);
        const float ty = gy - static_cast<float>(j1);
        const float tz = gz - static_cast<float>(k1);
        float z_samples[4];
        for (int dz = 0; dz < 4; ++dz) {
            float y_samples[4];
            for (int dy = 0; dy < 4; ++dy) {
                const int jj   = j1 + dy - 1;
                const int kk   = k1 + dz - 1;
                const float p0 = load_velocity_x(field, i1 - 1, jj, kk, nx, ny, nz, boundary);
                const float p1 = load_velocity_x(field, i1, jj, kk, nx, ny, nz, boundary);
                const float p2 = load_velocity_x(field, i1 + 1, jj, kk, nx, ny, nz, boundary);
                const float p3 = load_velocity_x(field, i1 + 2, jj, kk, nx, ny, nz, boundary);
                y_samples[dy]  = monotonic_cubic_1d(p0, p1, p2, p3, tx);
            }
            z_samples[dz] = monotonic_cubic_1d(y_samples[0], y_samples[1], y_samples[2], y_samples[3], ty);
        }
        return monotonic_cubic_1d(z_samples[0], z_samples[1], z_samples[2], z_samples[3], tz);
    }

    __device__ float sample_velocity_y(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }

        const float gx = x / h - 0.5f;
        const float gy = y / h;
        const float gz = z / h - 0.5f;
        const int i0   = static_cast<int>(floorf(gx));
        const int j0   = static_cast<int>(floorf(gy));
        const int k0   = static_cast<int>(floorf(gz));
        const int i1   = i0 + 1;
        const int j1   = j0 + 1;
        const int k1   = k0 + 1;
        const float tx = gx - static_cast<float>(i0);
        const float ty = gy - static_cast<float>(j0);
        const float tz = gz - static_cast<float>(k0);

        const float c000 = load_velocity_y(field, i0, j0, k0, nx, ny, nz, boundary);
        const float c100 = load_velocity_y(field, i1, j0, k0, nx, ny, nz, boundary);
        const float c010 = load_velocity_y(field, i0, j1, k0, nx, ny, nz, boundary);
        const float c110 = load_velocity_y(field, i1, j1, k0, nx, ny, nz, boundary);
        const float c001 = load_velocity_y(field, i0, j0, k1, nx, ny, nz, boundary);
        const float c101 = load_velocity_y(field, i1, j0, k1, nx, ny, nz, boundary);
        const float c011 = load_velocity_y(field, i0, j1, k1, nx, ny, nz, boundary);
        const float c111 = load_velocity_y(field, i1, j1, k1, nx, ny, nz, boundary);

        const float c00 = c000 + (c100 - c000) * tx;
        const float c10 = c010 + (c110 - c010) * tx;
        const float c01 = c001 + (c101 - c001) * tx;
        const float c11 = c011 + (c111 - c011) * tx;
        const float c0  = c00 + (c10 - c00) * ty;
        const float c1  = c01 + (c11 - c01) * ty;
        return c0 + (c1 - c0) * tz;
    }

    __device__ float sample_velocity_y_cubic(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }
        const float gx = x / h - 0.5f;
        const float gy = y / h;
        const float gz = z / h - 0.5f;
        const int i1   = static_cast<int>(floorf(gx));
        const int j1   = static_cast<int>(floorf(gy));
        const int k1   = static_cast<int>(floorf(gz));
        const float tx = gx - static_cast<float>(i1);
        const float ty = gy - static_cast<float>(j1);
        const float tz = gz - static_cast<float>(k1);
        float z_samples[4];
        for (int dz = 0; dz < 4; ++dz) {
            float y_samples[4];
            for (int dy = 0; dy < 4; ++dy) {
                const int yy   = j1 + dy - 1;
                const int zz   = k1 + dz - 1;
                const float p0 = load_velocity_y(field, i1 - 1, yy, zz, nx, ny, nz, boundary);
                const float p1 = load_velocity_y(field, i1, yy, zz, nx, ny, nz, boundary);
                const float p2 = load_velocity_y(field, i1 + 1, yy, zz, nx, ny, nz, boundary);
                const float p3 = load_velocity_y(field, i1 + 2, yy, zz, nx, ny, nz, boundary);
                y_samples[dy]  = monotonic_cubic_1d(p0, p1, p2, p3, tx);
            }
            z_samples[dz] = monotonic_cubic_1d(y_samples[0], y_samples[1], y_samples[2], y_samples[3], ty);
        }
        return monotonic_cubic_1d(z_samples[0], z_samples[1], z_samples[2], z_samples[3], tz);
    }

    __device__ float sample_velocity_z(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }

        const float gx = x / h - 0.5f;
        const float gy = y / h - 0.5f;
        const float gz = z / h;
        const int i0   = static_cast<int>(floorf(gx));
        const int j0   = static_cast<int>(floorf(gy));
        const int k0   = static_cast<int>(floorf(gz));
        const int i1   = i0 + 1;
        const int j1   = j0 + 1;
        const int k1   = k0 + 1;
        const float tx = gx - static_cast<float>(i0);
        const float ty = gy - static_cast<float>(j0);
        const float tz = gz - static_cast<float>(k0);

        const float c000 = load_velocity_z(field, i0, j0, k0, nx, ny, nz, boundary);
        const float c100 = load_velocity_z(field, i1, j0, k0, nx, ny, nz, boundary);
        const float c010 = load_velocity_z(field, i0, j1, k0, nx, ny, nz, boundary);
        const float c110 = load_velocity_z(field, i1, j1, k0, nx, ny, nz, boundary);
        const float c001 = load_velocity_z(field, i0, j0, k1, nx, ny, nz, boundary);
        const float c101 = load_velocity_z(field, i1, j0, k1, nx, ny, nz, boundary);
        const float c011 = load_velocity_z(field, i0, j1, k1, nx, ny, nz, boundary);
        const float c111 = load_velocity_z(field, i1, j1, k1, nx, ny, nz, boundary);

        const float c00 = c000 + (c100 - c000) * tx;
        const float c10 = c010 + (c110 - c010) * tx;
        const float c01 = c001 + (c101 - c001) * tx;
        const float c11 = c011 + (c111 - c011) * tx;
        const float c0  = c00 + (c10 - c00) * ty;
        const float c1  = c01 + (c11 - c01) * ty;
        return c0 + (c1 - c0) * tz;
    }

    __device__ float sample_velocity_z_cubic(const float* field, float x, float y, float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            x                    = fmodf(x, extent_x);
            if (x < 0.0f) x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            y                    = fmodf(y, extent_y);
            if (y < 0.0f) y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            z                    = fmodf(z, extent_z);
            if (z < 0.0f) z += extent_z;
        }
        const float gx = x / h - 0.5f;
        const float gy = y / h - 0.5f;
        const float gz = z / h;
        const int i1   = static_cast<int>(floorf(gx));
        const int j1   = static_cast<int>(floorf(gy));
        const int k1   = static_cast<int>(floorf(gz));
        const float tx = gx - static_cast<float>(i1);
        const float ty = gy - static_cast<float>(j1);
        const float tz = gz - static_cast<float>(k1);
        float z_samples[4];
        for (int dz = 0; dz < 4; ++dz) {
            float y_samples[4];
            for (int dy = 0; dy < 4; ++dy) {
                const int yy   = j1 + dy - 1;
                const int zz   = k1 + dz - 1;
                const float p0 = load_velocity_z(field, i1 - 1, yy, zz, nx, ny, nz, boundary);
                const float p1 = load_velocity_z(field, i1, yy, zz, nx, ny, nz, boundary);
                const float p2 = load_velocity_z(field, i1 + 1, yy, zz, nx, ny, nz, boundary);
                const float p3 = load_velocity_z(field, i1 + 2, yy, zz, nx, ny, nz, boundary);
                y_samples[dy]  = monotonic_cubic_1d(p0, p1, p2, p3, tx);
            }
            z_samples[dz] = monotonic_cubic_1d(y_samples[0], y_samples[1], y_samples[2], y_samples[3], ty);
        }
        return monotonic_cubic_1d(z_samples[0], z_samples[1], z_samples[2], z_samples[3], tz);
    }

    __device__ float3 sample_velocity(const float* velocity_x, const float* velocity_y, const float* velocity_z, const float x, const float y, const float z, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        return make_float3(sample_velocity_x(velocity_x, x, y, z, nx, ny, nz, h, boundary), sample_velocity_y(velocity_y, x, y, z, nx, ny, nz, h, boundary), sample_velocity_z(velocity_z, x, y, z, nx, ny, nz, h, boundary));
    }

    __device__ float3 trace_particle_rk2(const float3 start, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const float dt, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        const auto [v0_x, v0_y, v0_z]    = sample_velocity(velocity_x, velocity_y, velocity_z, start.x, start.y, start.z, nx, ny, nz, h, boundary);
        const auto [mid_x, mid_y, mid_z] = make_float3(start.x - 0.5f * dt * v0_x, start.y - 0.5f * dt * v0_y, start.z - 0.5f * dt * v0_z);
        const auto [v1_x, v1_y, v1_z]    = sample_velocity(velocity_x, velocity_y, velocity_z, mid_x, mid_y, mid_z, nx, ny, nz, h, boundary);
        float3 traced                    = make_float3(start.x - dt * v1_x, start.y - dt * v1_y, start.z - dt * v1_z);
        float end_x                      = traced.x;
        float end_y                      = traced.y;
        float end_z                      = traced.z;
        if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
            const float extent_x = static_cast<float>(nx) * h;
            end_x                = fmodf(end_x, extent_x);
            if (end_x < 0.0f) end_x += extent_x;
        }
        if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
            const float extent_y = static_cast<float>(ny) * h;
            end_y                = fmodf(end_y, extent_y);
            if (end_y < 0.0f) end_y += extent_y;
        }
        if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
            const float extent_z = static_cast<float>(nz) * h;
            end_z                = fmodf(end_z, extent_z);
            if (end_z < 0.0f) end_z += extent_z;
        }

        bool traced_hits_solid = end_x < 0.0f || end_x > static_cast<float>(nx) * h || end_y < 0.0f || end_y > static_cast<float>(ny) * h || end_z < 0.0f || end_z > static_cast<float>(nz) * h;
        if (!traced_hits_solid && occupancy != nullptr) {
            int end_cell_x = static_cast<int>(floorf(end_x / h));
            int end_cell_y = static_cast<int>(floorf(end_y / h));
            int end_cell_z = static_cast<int>(floorf(end_z / h));
            if (end_cell_x == nx) end_cell_x = nx - 1;
            if (end_cell_y == ny) end_cell_y = ny - 1;
            if (end_cell_z == nz) end_cell_z = nz - 1;
            traced_hits_solid = !cell_in_bounds(end_cell_x, end_cell_y, end_cell_z, nx, ny, nz) || occupancy[index_3d(end_cell_x, end_cell_y, end_cell_z, nx, ny)] != 0;
        }
        if (!traced_hits_solid) return traced;

        float lo = 0.0f;
        float hi = 1.0f;
        for (int iteration = 0; iteration < 10; ++iteration) {
            const float mid_t = 0.5f * (lo + hi);
            float test_x      = start.x + (traced.x - start.x) * mid_t;
            float test_y      = start.y + (traced.y - start.y) * mid_t;
            float test_z      = start.z + (traced.z - start.z) * mid_t;
            if (boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nx > 0) {
                const float extent_x = static_cast<float>(nx) * h;
                test_x               = fmodf(test_x, extent_x);
                if (test_x < 0.0f) test_x += extent_x;
            }
            if (boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && ny > 0) {
                const float extent_y = static_cast<float>(ny) * h;
                test_y               = fmodf(test_y, extent_y);
                if (test_y < 0.0f) test_y += extent_y;
            }
            if (boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && nz > 0) {
                const float extent_z = static_cast<float>(nz) * h;
                test_z               = fmodf(test_z, extent_z);
                if (test_z < 0.0f) test_z += extent_z;
            }

            bool test_hits_solid = test_x < 0.0f || test_x > static_cast<float>(nx) * h || test_y < 0.0f || test_y > static_cast<float>(ny) * h || test_z < 0.0f || test_z > static_cast<float>(nz) * h;
            if (!test_hits_solid && occupancy != nullptr) {
                int test_cell_x = static_cast<int>(floorf(test_x / h));
                int test_cell_y = static_cast<int>(floorf(test_y / h));
                int test_cell_z = static_cast<int>(floorf(test_z / h));
                if (test_cell_x == nx) test_cell_x = nx - 1;
                if (test_cell_y == ny) test_cell_y = ny - 1;
                if (test_cell_z == nz) test_cell_z = nz - 1;
                test_hits_solid = !cell_in_bounds(test_cell_x, test_cell_y, test_cell_z, nx, ny, nz) || occupancy[index_3d(test_cell_x, test_cell_y, test_cell_z, nx, ny)] != 0;
            }
            if (test_hits_solid)
                hi = mid_t;
            else
                lo = mid_t;
        }
        traced.x = start.x + (traced.x - start.x) * lo;
        traced.y = start.y + (traced.y - start.y) * lo;
        traced.z = start.z + (traced.z - start.z) * lo;
        return traced;
    }

    __global__ void fill_float_kernel(float* field, const float value, const std::uint64_t count) {
        const auto index = static_cast<std::uint64_t>(blockIdx.x) * static_cast<std::uint64_t>(blockDim.x) + static_cast<std::uint64_t>(threadIdx.x);
        if (index >= count) return;
        field[index] = value;
    }

    __global__ void add_source_kernel(float* destination, const float* current, const float* source, const float dt, const std::uint64_t count) {
        const auto index = static_cast<std::uint64_t>(blockIdx.x) * static_cast<std::uint64_t>(blockDim.x) + static_cast<std::uint64_t>(threadIdx.x);
        if (index >= count) return;
        const float source_value = source != nullptr ? source[index] : 0.0f;
        destination[index]       = current[index] + dt * source_value;
    }

    __global__ void compute_center_velocity_kernel(float* cell_x, float* cell_y, float* cell_z, const float* velocity_x, const float* velocity_y, const float* velocity_z, const int nx, const int ny, const int nz) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;
        const auto index = index_3d(x, y, z, nx, ny);
        cell_x[index]    = 0.5f * (velocity_x[index_velocity_x(x, y, z, nx, ny)] + velocity_x[index_velocity_x(x + 1, y, z, nx, ny)]);
        cell_y[index]    = 0.5f * (velocity_y[index_velocity_y(x, y, z, nx, ny)] + velocity_y[index_velocity_y(x, y + 1, z, nx, ny)]);
        cell_z[index]    = 0.5f * (velocity_z[index_velocity_z(x, y, z, nx, ny)] + velocity_z[index_velocity_z(x, y, z + 1, nx, ny)]);
    }

    __global__ void compute_vorticity_kernel(float* omega_x, float* omega_y, float* omega_z, float* omega_magnitude, const float* cell_x, const float* cell_y, const float* cell_z, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;

        const auto index = index_3d(x, y, z, nx, ny);
        if (load_occupancy(occupancy, x, y, z, nx, ny, nz, boundary)) {
            omega_x[index]         = 0.0f;
            omega_y[index]         = 0.0f;
            omega_z[index]         = 0.0f;
            omega_magnitude[index] = 0.0f;
            return;
        }

        const float dvz_dy = 0.5f * (load_center_velocity_component(cell_z, 2, x, y + 1, z, nx, ny, nz, boundary) - load_center_velocity_component(cell_z, 2, x, y - 1, z, nx, ny, nz, boundary)) / h;
        const float dvy_dz = 0.5f * (load_center_velocity_component(cell_y, 1, x, y, z + 1, nx, ny, nz, boundary) - load_center_velocity_component(cell_y, 1, x, y, z - 1, nx, ny, nz, boundary)) / h;
        const float dvx_dz = 0.5f * (load_center_velocity_component(cell_x, 0, x, y, z + 1, nx, ny, nz, boundary) - load_center_velocity_component(cell_x, 0, x, y, z - 1, nx, ny, nz, boundary)) / h;
        const float dvz_dx = 0.5f * (load_center_velocity_component(cell_z, 2, x + 1, y, z, nx, ny, nz, boundary) - load_center_velocity_component(cell_z, 2, x - 1, y, z, nx, ny, nz, boundary)) / h;
        const float dvy_dx = 0.5f * (load_center_velocity_component(cell_y, 1, x + 1, y, z, nx, ny, nz, boundary) - load_center_velocity_component(cell_y, 1, x - 1, y, z, nx, ny, nz, boundary)) / h;
        const float dvx_dy = 0.5f * (load_center_velocity_component(cell_x, 0, x, y + 1, z, nx, ny, nz, boundary) - load_center_velocity_component(cell_x, 0, x, y - 1, z, nx, ny, nz, boundary)) / h;

        const float wx = dvz_dy - dvy_dz;
        const float wy = dvx_dz - dvz_dx;
        const float wz = dvy_dx - dvx_dy;

        omega_x[index]         = wx;
        omega_y[index]         = wy;
        omega_z[index]         = wz;
        omega_magnitude[index] = sqrtf(wx * wx + wy * wy + wz * wz);
    }

    __global__ void add_buoyancy_kernel(float* force_y, const float* density, const float* temperature, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float ambient_temperature, const float density_factor, const float temperature_factor, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;
        if (load_occupancy(occupancy, x, y, z, nx, ny, nz, boundary)) return;
        const auto index = index_3d(x, y, z, nx, ny);
        force_y[index] += -density_factor * density[index] + temperature_factor * (temperature[index] - ambient_temperature);
    }

    __global__ void add_confinement_kernel(float* force_x, float* force_y, float* force_z, const float* omega_x, const float* omega_y, const float* omega_z, const float* omega_magnitude, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float epsilon, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;
        if (load_occupancy(occupancy, x, y, z, nx, ny, nz, boundary)) return;

        const float grad_x   = 0.5f * (load_flow_cell(omega_magnitude, x + 1, y, z, nx, ny, nz, boundary) - load_flow_cell(omega_magnitude, x - 1, y, z, nx, ny, nz, boundary)) / h;
        const float grad_y   = 0.5f * (load_flow_cell(omega_magnitude, x, y + 1, z, nx, ny, nz, boundary) - load_flow_cell(omega_magnitude, x, y - 1, z, nx, ny, nz, boundary)) / h;
        const float grad_z   = 0.5f * (load_flow_cell(omega_magnitude, x, y, z + 1, nx, ny, nz, boundary) - load_flow_cell(omega_magnitude, x, y, z - 1, nx, ny, nz, boundary)) / h;
        const float grad_mag = sqrtf(grad_x * grad_x + grad_y * grad_y + grad_z * grad_z);
        if (grad_mag < 1.0e-6f) return;

        const float inv_grad      = 1.0f / grad_mag;
        const float normal_x      = grad_x * inv_grad;
        const float normal_y      = grad_y * inv_grad;
        const float normal_z      = grad_z * inv_grad;
        const auto index          = index_3d(x, y, z, nx, ny);
        const float wx            = omega_x[index];
        const float wy            = omega_y[index];
        const float wz            = omega_z[index];
        const float confinement_x = epsilon * h * (normal_y * wz - normal_z * wy);
        const float confinement_y = epsilon * h * (normal_z * wx - normal_x * wz);
        const float confinement_z = epsilon * h * (normal_x * wy - normal_y * wx);

        force_x[index] += confinement_x;
        force_y[index] += confinement_y;
        force_z[index] += confinement_z;
    }

    __global__ void add_center_forces_to_velocity_x_kernel(float* velocity_x, const float* force_x, const int nx, const int ny, const int nz, const float dt) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i > nx || j >= ny || k >= nz) return;

        float sum    = 0.0f;
        float weight = 0.0f;
        if (i > 0) {
            sum += force_x[index_3d(i - 1, j, k, nx, ny)];
            weight += 1.0f;
        }
        if (i < nx) {
            sum += force_x[index_3d(i, j, k, nx, ny)];
            weight += 1.0f;
        }
        if (weight > 0.0f) velocity_x[index_velocity_x(i, j, k, nx, ny)] += dt * (sum / weight);
    }

    __global__ void add_center_forces_to_velocity_y_kernel(float* velocity_y, const float* force_y, const int nx, const int ny, const int nz, const float dt) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j > ny || k >= nz) return;

        float sum    = 0.0f;
        float weight = 0.0f;
        if (j > 0) {
            sum += force_y[index_3d(i, j - 1, k, nx, ny)];
            weight += 1.0f;
        }
        if (j < ny) {
            sum += force_y[index_3d(i, j, k, nx, ny)];
            weight += 1.0f;
        }
        if (weight > 0.0f) velocity_y[index_velocity_y(i, j, k, nx, ny)] += dt * (sum / weight);
    }

    __global__ void add_center_forces_to_velocity_z_kernel(float* velocity_z, const float* force_z, const int nx, const int ny, const int nz, const float dt) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j >= ny || k > nz) return;

        float sum    = 0.0f;
        float weight = 0.0f;
        if (k > 0) {
            sum += force_z[index_3d(i, j, k - 1, nx, ny)];
            weight += 1.0f;
        }
        if (k < nz) {
            sum += force_z[index_3d(i, j, k, nx, ny)];
            weight += 1.0f;
        }
        if (weight > 0.0f) velocity_z[index_velocity_z(i, j, k, nx, ny)] += dt * (sum / weight);
    }

    __device__ float solid_velocity_value(const float* solid_velocity, const uint8_t* occupancy, int x, int y, int z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        if (solid_velocity == nullptr || occupancy == nullptr) return 0.0f;
        if (!resolve_cell_coordinates(x, y, z, nx, ny, nz, boundary)) return 0.0f;
        if (occupancy[index_3d(x, y, z, nx, ny)] == 0) return 0.0f;
        return solid_velocity[index_3d(x, y, z, nx, ny)];
    }

    __global__ void enforce_velocity_x_boundaries_kernel(float* velocity_x, const uint8_t* occupancy, const float* solid_velocity_x, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i > nx || j >= ny || k >= nz) return;

        auto& face = velocity_x[index_velocity_x(i, j, k, nx, ny)];
        if (i == 0) {
            if (const auto domain_face = boundary.x_minus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nx > 0)
                    face = velocity_x[index_velocity_x(1, j, k, nx, ny)];
                else
                    face = domain_face.velocity_x;
                return;
            }
        }
        if (i == nx) {
            if (const auto domain_face = boundary.x_plus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nx > 0)
                    face = velocity_x[index_velocity_x(nx - 1, j, k, nx, ny)];
                else
                    face = domain_face.velocity_x;
                return;
            }
        }
        if (occupancy == nullptr) return;

        int left_x                = i - 1;
        int left_y                = j;
        int left_z                = k;
        int right_x               = i;
        int right_y               = j;
        int right_z               = k;
        const bool has_left       = resolve_cell_coordinates(left_x, left_y, left_z, nx, ny, nz, boundary);
        const bool has_right      = resolve_cell_coordinates(right_x, right_y, right_z, nx, ny, nz, boundary);
        const bool left_occupied  = has_left && occupancy[index_3d(left_x, left_y, left_z, nx, ny)] != 0;
        const bool right_occupied = has_right && occupancy[index_3d(right_x, right_y, right_z, nx, ny)] != 0;
        if (!left_occupied && !right_occupied) return;

        float value  = 0.0f;
        float weight = 0.0f;
        if (left_occupied) {
            value += solid_velocity_value(solid_velocity_x, occupancy, left_x, left_y, left_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        if (right_occupied) {
            value += solid_velocity_value(solid_velocity_x, occupancy, right_x, right_y, right_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        face = weight > 0.0f ? value / weight : 0.0f;
    }

    __global__ void enforce_velocity_y_boundaries_kernel(float* velocity_y, const uint8_t* occupancy, const float* solid_velocity_y, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j > ny || k >= nz) return;

        auto& face = velocity_y[index_velocity_y(i, j, k, nx, ny)];
        if (j == 0) {
            if (const auto domain_face = boundary.y_minus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && ny > 0)
                    face = velocity_y[index_velocity_y(i, 1, k, nx, ny)];
                else
                    face = domain_face.velocity_y;
                return;
            }
        }
        if (j == ny) {
            if (const auto domain_face = boundary.y_plus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && ny > 0)
                    face = velocity_y[index_velocity_y(i, ny - 1, k, nx, ny)];
                else
                    face = domain_face.velocity_y;
                return;
            }
        }
        if (occupancy == nullptr) return;

        int down_x               = i;
        int down_y               = j - 1;
        int down_z               = k;
        int up_x                 = i;
        int up_y                 = j;
        int up_z                 = k;
        const bool has_down      = resolve_cell_coordinates(down_x, down_y, down_z, nx, ny, nz, boundary);
        const bool has_up        = resolve_cell_coordinates(up_x, up_y, up_z, nx, ny, nz, boundary);
        const bool down_occupied = has_down && occupancy[index_3d(down_x, down_y, down_z, nx, ny)] != 0;
        const bool up_occupied   = has_up && occupancy[index_3d(up_x, up_y, up_z, nx, ny)] != 0;
        if (!down_occupied && !up_occupied) return;

        float value  = 0.0f;
        float weight = 0.0f;
        if (down_occupied) {
            value += solid_velocity_value(solid_velocity_y, occupancy, down_x, down_y, down_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        if (up_occupied) {
            value += solid_velocity_value(solid_velocity_y, occupancy, up_x, up_y, up_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        face = weight > 0.0f ? value / weight : 0.0f;
    }

    __global__ void enforce_velocity_z_boundaries_kernel(float* velocity_z, const uint8_t* occupancy, const float* solid_velocity_z, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j >= ny || k > nz) return;

        auto& face = velocity_z[index_velocity_z(i, j, k, nx, ny)];
        if (k == 0) {
            if (const auto domain_face = boundary.z_minus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nz > 0)
                    face = velocity_z[index_velocity_z(i, j, 1, nx, ny)];
                else
                    face = domain_face.velocity_z;
                return;
            }
        }
        if (k == nz) {
            if (const auto domain_face = boundary.z_plus; domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nz > 0)
                    face = velocity_z[index_velocity_z(i, j, nz - 1, nx, ny)];
                else
                    face = domain_face.velocity_z;
                return;
            }
        }
        if (occupancy == nullptr) return;

        int back_x                = i;
        int back_y                = j;
        int back_z                = k - 1;
        int front_x               = i;
        int front_y               = j;
        int front_z               = k;
        const bool has_back       = resolve_cell_coordinates(back_x, back_y, back_z, nx, ny, nz, boundary);
        const bool has_front      = resolve_cell_coordinates(front_x, front_y, front_z, nx, ny, nz, boundary);
        const bool back_occupied  = has_back && occupancy[index_3d(back_x, back_y, back_z, nx, ny)] != 0;
        const bool front_occupied = has_front && occupancy[index_3d(front_x, front_y, front_z, nx, ny)] != 0;
        if (!back_occupied && !front_occupied) return;

        float value  = 0.0f;
        float weight = 0.0f;
        if (back_occupied) {
            value += solid_velocity_value(solid_velocity_z, occupancy, back_x, back_y, back_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        if (front_occupied) {
            value += solid_velocity_value(solid_velocity_z, occupancy, front_x, front_y, front_z, nx, ny, nz, boundary);
            weight += 1.0f;
        }
        face = weight > 0.0f ? value / weight : 0.0f;
    }

    __global__ void sync_periodic_velocity_x_kernel(float* velocity_x, const int nx, const int ny, const int nz) {
        const int j = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int k = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        if (j >= ny || k >= nz) return;
        velocity_x[index_velocity_x(nx, j, k, nx, ny)] = velocity_x[index_velocity_x(0, j, k, nx, ny)];
    }

    __global__ void sync_periodic_velocity_y_kernel(float* velocity_y, const int nx, const int ny, const int nz) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int k = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        if (i >= nx || k >= nz) return;
        velocity_y[index_velocity_y(i, ny, k, nx, ny)] = velocity_y[index_velocity_y(i, 0, k, nx, ny)];
    }

    __global__ void sync_periodic_velocity_z_kernel(float* velocity_z, const int nx, const int ny, const int nz) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        if (i >= nx || j >= ny) return;
        velocity_z[index_velocity_z(i, j, nz, nx, ny)] = velocity_z[index_velocity_z(i, j, 0, nx, ny)];
    }

    __global__ void advect_velocity_x_kernel(float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const uint32_t advection_mode, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i > nx || j >= ny || k >= nz) return;
        const float3 start                             = make_float3(static_cast<float>(i) * h, (static_cast<float>(j) + 0.5f) * h, (static_cast<float>(k) + 0.5f) * h);
        const auto [p_x, p_y, p_z]                     = trace_particle_rk2(start, velocity_x, velocity_y, velocity_z, occupancy, dt, nx, ny, nz, h, boundary);
        destination[index_velocity_x(i, j, k, nx, ny)] = advection_mode == SMOKE_SIMULATION_SCALAR_ADVECTION_MONOTONIC_CUBIC ? sample_velocity_x_cubic(source, p_x, p_y, p_z, nx, ny, nz, h, boundary) : sample_velocity_x(source, p_x, p_y, p_z, nx, ny, nz, h, boundary);
    }

    __global__ void advect_velocity_y_kernel(float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const uint32_t advection_mode, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j > ny || k >= nz) return;
        const float3 start                             = make_float3((static_cast<float>(i) + 0.5f) * h, static_cast<float>(j) * h, (static_cast<float>(k) + 0.5f) * h);
        const auto [p_x, p_y, p_z]                     = trace_particle_rk2(start, velocity_x, velocity_y, velocity_z, occupancy, dt, nx, ny, nz, h, boundary);
        destination[index_velocity_y(i, j, k, nx, ny)] = advection_mode == SMOKE_SIMULATION_SCALAR_ADVECTION_MONOTONIC_CUBIC ? sample_velocity_y_cubic(source, p_x, p_y, p_z, nx, ny, nz, h, boundary) : sample_velocity_y(source, p_x, p_y, p_z, nx, ny, nz, h, boundary);
    }

    __global__ void advect_velocity_z_kernel(float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const uint32_t advection_mode, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j >= ny || k > nz) return;
        const float3 start                             = make_float3((static_cast<float>(i) + 0.5f) * h, (static_cast<float>(j) + 0.5f) * h, static_cast<float>(k) * h);
        const auto [p_x, p_y, p_z]                     = trace_particle_rk2(start, velocity_x, velocity_y, velocity_z, occupancy, dt, nx, ny, nz, h, boundary);
        destination[index_velocity_z(i, j, k, nx, ny)] = advection_mode == SMOKE_SIMULATION_SCALAR_ADVECTION_MONOTONIC_CUBIC ? sample_velocity_z_cubic(source, p_x, p_y, p_z, nx, ny, nz, h, boundary) : sample_velocity_z(source, p_x, p_y, p_z, nx, ny, nz, h, boundary);
    }

    __global__ void advect_scalar_kernel(float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const uint32_t advection_mode, const SmokeSimulationScalarBoundaryConfig scalar_boundary, const SmokeSimulationFlowBoundaryConfig flow_boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;
        if (load_occupancy(occupancy, x, y, z, nx, ny, nz, flow_boundary)) {
            destination[index_3d(x, y, z, nx, ny)] = 0.0f;
            return;
        }
        const float3 start                     = make_float3((static_cast<float>(x) + 0.5f) * h, (static_cast<float>(y) + 0.5f) * h, (static_cast<float>(z) + 0.5f) * h);
        const auto [p_x, p_y, p_z]             = trace_particle_rk2(start, velocity_x, velocity_y, velocity_z, occupancy, dt, nx, ny, nz, h, flow_boundary);
        destination[index_3d(x, y, z, nx, ny)] = advection_mode == SMOKE_SIMULATION_SCALAR_ADVECTION_MONOTONIC_CUBIC ? sample_scalar_cubic(source, p_x, p_y, p_z, nx, ny, nz, h, scalar_boundary) : sample_scalar_linear(source, p_x, p_y, p_z, nx, ny, nz, h, scalar_boundary);
    }

    __global__ void apply_solid_temperature_kernel(float* temperature, const uint8_t* occupancy, const float* solid_temperature, const int nx, const int ny, const int nz, const float ambient_temperature) {
        const auto index = static_cast<std::uint64_t>(blockIdx.x) * static_cast<std::uint64_t>(blockDim.x) + static_cast<std::uint64_t>(threadIdx.x);
        const auto count = static_cast<std::uint64_t>(nx) * static_cast<std::uint64_t>(ny) * static_cast<std::uint64_t>(nz);
        if (index >= count) return;
        if (occupancy == nullptr || occupancy[index] == 0) return;
        temperature[index] = solid_temperature != nullptr ? solid_temperature[index] : ambient_temperature;
    }

    __global__ void boundary_fill_density_kernel(float* destination, const float* source, const uint8_t* occupancy, const int nx, const int ny, const int nz, const SmokeSimulationScalarBoundaryConfig boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;

        const auto index = index_3d(x, y, z, nx, ny);
        if (occupancy == nullptr || occupancy[index] == 0) {
            destination[index] = source[index];
            return;
        }

        int max_radius = nx;
        if (ny > max_radius) max_radius = ny;
        if (nz > max_radius) max_radius = nz;
        for (int radius = 1; radius <= max_radius; ++radius) {
            bool found         = false;
            float best_value   = 0.0f;
            int best_distance2 = 0;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dy = -radius; dy <= radius; ++dy) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        int shell_radius = abs(dx);
                        if (abs(dy) > shell_radius) shell_radius = abs(dy);
                        if (abs(dz) > shell_radius) shell_radius = abs(dz);
                        if (shell_radius != radius) continue;
                        int next_x = x + dx;
                        int next_y = y + dy;
                        int next_z = z + dz;
                        if (!resolve_scalar_cell_coordinates(next_x, next_y, next_z, nx, ny, nz, boundary)) continue;
                        const auto neighbor_index = index_3d(next_x, next_y, next_z, nx, ny);
                        if (occupancy[neighbor_index] != 0) continue;
                        const int distance2 = dx * dx + dy * dy + dz * dz;
                        if (!found || distance2 < best_distance2) {
                            found          = true;
                            best_distance2 = distance2;
                            best_value     = source[neighbor_index];
                        }
                    }
                }
            }
            if (found) {
                destination[index] = best_value;
                return;
            }
        }
        destination[index] = 0.0f;
    }

    __global__ void fill_int_kernel(int* field, const int value, const std::uint64_t count) {
        const auto index = static_cast<std::uint64_t>(blockIdx.x) * static_cast<std::uint64_t>(blockDim.x) + static_cast<std::uint64_t>(threadIdx.x);
        if (index >= count) return;
        field[index] = value;
    }

    __global__ void find_pressure_anchor_kernel(int* pressure_anchor, const uint8_t* occupancy, const std::uint64_t count) {
        const auto index = static_cast<std::uint64_t>(blockIdx.x) * static_cast<std::uint64_t>(blockDim.x) + static_cast<std::uint64_t>(threadIdx.x);
        if (index >= count) return;
        if (occupancy != nullptr && occupancy[index] != 0) return;
        atomicMin(pressure_anchor, static_cast<int>(index));
    }

    __global__ void compute_pressure_rhs_kernel(float* rhs, const float* velocity_x, const float* velocity_y, const float* velocity_z, const uint8_t* occupancy, const int* pressure_anchor, const int nx, const int ny, const int nz, const float h, const float dt, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int z = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (x >= nx || y >= ny || z >= nz) return;
        const auto index = index_3d(x, y, z, nx, ny);
        const int anchor = *pressure_anchor;
        if (static_cast<int>(index) == anchor) {
            rhs[index] = 0.0f;
            return;
        }
        if (occupancy != nullptr && occupancy[index] != 0) {
            rhs[index] = 0.0f;
            return;
        }
        const float divergence = (velocity_x[index_velocity_x(x + 1, y, z, nx, ny)] - velocity_x[index_velocity_x(x, y, z, nx, ny)] + velocity_y[index_velocity_y(x, y + 1, z, nx, ny)] - velocity_y[index_velocity_y(x, y, z, nx, ny)] + velocity_z[index_velocity_z(x, y, z + 1, nx, ny)] - velocity_z[index_velocity_z(x, y, z, nx, ny)]) / h;
        float boundary_sum     = 0.0f;
        if (x == 0 && boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.x_minus.pressure;
        if (x == nx - 1 && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.x_plus.pressure;
        if (y == 0 && boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.y_minus.pressure;
        if (y == ny - 1 && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.y_plus.pressure;
        if (z == 0 && boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.z_minus.pressure;
        if (z == nz - 1 && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) boundary_sum += boundary.z_plus.pressure;
        rhs[index] = -(h * h / dt) * divergence + boundary_sum;
    }

    __device__ void accumulate_pressure_neighbor(int* active_neighbors, int& active_neighbor_count, float& diagonal, int next_x, int next_y, int next_z, const SmokeSimulationFlowBoundaryFaceDesc minus_face, const SmokeSimulationFlowBoundaryFaceDesc plus_face, const bool periodic_axis, const uint8_t* occupancy, const int anchor, const int nx, const int ny, const int nz) {
        if (next_x < 0 || next_x >= nx || next_y < 0 || next_y >= ny || next_z < 0 || next_z >= nz) {
            if (periodic_axis) {
                if (next_x < 0 || next_x >= nx) next_x = wrap_index(next_x, nx);
                if (next_y < 0 || next_y >= ny) next_y = wrap_index(next_y, ny);
                if (next_z < 0 || next_z >= nz) next_z = wrap_index(next_z, nz);
            } else {
                const auto face = next_x < 0 || next_y < 0 || next_z < 0 ? minus_face : plus_face;
                if (face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW) diagonal += 1.0f;
                return;
            }
        }
        const int neighbor = static_cast<int>(index_3d(next_x, next_y, next_z, nx, ny));
        if (occupancy != nullptr && occupancy[static_cast<std::uint64_t>(neighbor)] != 0) return;
        diagonal += 1.0f;
        if (neighbor == anchor) return;
        for (int index = 0; index < active_neighbor_count; ++index) {
            if (active_neighbors[index] == neighbor) return;
        }
        active_neighbors[active_neighbor_count] = neighbor;
        ++active_neighbor_count;
    }

    __global__ void build_pressure_matrix_kernel(float* values, const int* row_offsets, const int* column_indices, const uint8_t* occupancy, const int* pressure_anchor, const int nx, const int ny, const int nz, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int row = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        if (row >= nx * ny * nz) return;

        const int anchor      = *pressure_anchor;
        const int x           = row % nx;
        const int yz          = row / nx;
        const int y           = yz % ny;
        const int z           = yz / ny;
        const bool occupied   = occupancy != nullptr && occupancy[static_cast<std::uint64_t>(row)] != 0;
        const bool special    = occupied || row == anchor;
        const bool periodic_x = boundary.x_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.x_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC;
        const bool periodic_y = boundary.y_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.y_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC;
        const bool periodic_z = boundary.z_minus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC && boundary.z_plus.type == SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC;

        int active_neighbors[6]{};
        int active_neighbor_count = 0;
        float diagonal            = 0.0f;

        if (!special) {
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x - 1, y, z, boundary.x_minus, boundary.x_plus, periodic_x, occupancy, anchor, nx, ny, nz);
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x + 1, y, z, boundary.x_minus, boundary.x_plus, periodic_x, occupancy, anchor, nx, ny, nz);
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x, y - 1, z, boundary.y_minus, boundary.y_plus, periodic_y, occupancy, anchor, nx, ny, nz);
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x, y + 1, z, boundary.y_minus, boundary.y_plus, periodic_y, occupancy, anchor, nx, ny, nz);
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x, y, z - 1, boundary.z_minus, boundary.z_plus, periodic_z, occupancy, anchor, nx, ny, nz);
            accumulate_pressure_neighbor(active_neighbors, active_neighbor_count, diagonal, x, y, z + 1, boundary.z_minus, boundary.z_plus, periodic_z, occupancy, anchor, nx, ny, nz);
            if (diagonal <= 0.0f) diagonal = 1.0f;
        }

        for (int entry = row_offsets[row]; entry < row_offsets[row + 1]; ++entry) {
            const int column = column_indices[entry];
            float value      = 0.0f;
            if (special) {
                value = column == row ? 1.0f : 0.0f;
            } else if (column == row) {
                value = diagonal;
            } else {
                for (int index = 0; index < active_neighbor_count; ++index) {
                    if (active_neighbors[index] == column) {
                        value = -1.0f;
                        break;
                    }
                }
            }
            values[entry] = value;
        }
    }

    __global__ void compute_ratio_kernel(float* destination, const float* numerator, const float* denominator) {
        if (blockIdx.x != 0 || threadIdx.x != 0) return;
        const float value = fabsf(*denominator) > 1.0e-20f ? *numerator / *denominator : 0.0f;
        *destination      = value;
    }

    __global__ void negate_scalar_kernel(float* destination, const float* source) {
        if (blockIdx.x != 0 || threadIdx.x != 0) return;
        *destination = -*source;
    }

    __global__ void project_velocity_x_kernel(float* velocity_x, const float* pressure, const uint8_t* occupancy, const float* solid_velocity_x, const int nx, const int ny, const int nz, const float h, const float dt, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i > nx || j >= ny || k >= nz) return;

        auto& face = velocity_x[index_velocity_x(i, j, k, nx, ny)];
        if (i == 0) {
            const auto domain_face = boundary.x_minus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nx > 0)
                    face = velocity_x[index_velocity_x(1, j, k, nx, ny)];
                else
                    face = domain_face.velocity_x;
                return;
            }
        }
        if (i == nx) {
            const auto domain_face = boundary.x_plus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nx > 0)
                    face = velocity_x[index_velocity_x(nx - 1, j, k, nx, ny)];
                else
                    face = domain_face.velocity_x;
                return;
            }
        }

        int left_x                = i - 1;
        int left_y                = j;
        int left_z                = k;
        int right_x               = i;
        int right_y               = j;
        int right_z               = k;
        const bool has_left       = resolve_cell_coordinates(left_x, left_y, left_z, nx, ny, nz, boundary);
        const bool has_right      = resolve_cell_coordinates(right_x, right_y, right_z, nx, ny, nz, boundary);
        const bool left_occupied  = has_left && occupancy != nullptr && occupancy[index_3d(left_x, left_y, left_z, nx, ny)] != 0;
        const bool right_occupied = has_right && occupancy != nullptr && occupancy[index_3d(right_x, right_y, right_z, nx, ny)] != 0;
        if (left_occupied || right_occupied) {
            float value  = 0.0f;
            float weight = 0.0f;
            if (left_occupied) {
                value += solid_velocity_value(solid_velocity_x, occupancy, left_x, left_y, left_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            if (right_occupied) {
                value += solid_velocity_value(solid_velocity_x, occupancy, right_x, right_y, right_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            face = weight > 0.0f ? value / weight : 0.0f;
            return;
        }
        if (has_left && has_right) {
            const float pressure_right = pressure[index_3d(right_x, right_y, right_z, nx, ny)];
            const float pressure_left  = pressure[index_3d(left_x, left_y, left_z, nx, ny)];
            face -= dt * (pressure_right - pressure_left) / h;
        }
    }

    __global__ void project_velocity_y_kernel(float* velocity_y, const float* pressure, const uint8_t* occupancy, const float* solid_velocity_y, const int nx, const int ny, const int nz, const float h, const float dt, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j > ny || k >= nz) return;

        auto& face = velocity_y[index_velocity_y(i, j, k, nx, ny)];
        if (j == 0) {
            const auto domain_face = boundary.y_minus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && ny > 0)
                    face = velocity_y[index_velocity_y(i, 1, k, nx, ny)];
                else
                    face = domain_face.velocity_y;
                return;
            }
        }
        if (j == ny) {
            const auto domain_face = boundary.y_plus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && ny > 0)
                    face = velocity_y[index_velocity_y(i, ny - 1, k, nx, ny)];
                else
                    face = domain_face.velocity_y;
                return;
            }
        }

        int down_x               = i;
        int down_y               = j - 1;
        int down_z               = k;
        int up_x                 = i;
        int up_y                 = j;
        int up_z                 = k;
        const bool has_down      = resolve_cell_coordinates(down_x, down_y, down_z, nx, ny, nz, boundary);
        const bool has_up        = resolve_cell_coordinates(up_x, up_y, up_z, nx, ny, nz, boundary);
        const bool down_occupied = has_down && occupancy != nullptr && occupancy[index_3d(down_x, down_y, down_z, nx, ny)] != 0;
        const bool up_occupied   = has_up && occupancy != nullptr && occupancy[index_3d(up_x, up_y, up_z, nx, ny)] != 0;
        if (down_occupied || up_occupied) {
            float value  = 0.0f;
            float weight = 0.0f;
            if (down_occupied) {
                value += solid_velocity_value(solid_velocity_y, occupancy, down_x, down_y, down_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            if (up_occupied) {
                value += solid_velocity_value(solid_velocity_y, occupancy, up_x, up_y, up_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            face = weight > 0.0f ? value / weight : 0.0f;
            return;
        }
        if (has_down && has_up) {
            const float pressure_up   = pressure[index_3d(up_x, up_y, up_z, nx, ny)];
            const float pressure_down = pressure[index_3d(down_x, down_y, down_z, nx, ny)];
            face -= dt * (pressure_up - pressure_down) / h;
        }
    }

    __global__ void project_velocity_z_kernel(float* velocity_z, const float* pressure, const uint8_t* occupancy, const float* solid_velocity_z, const int nx, const int ny, const int nz, const float h, const float dt, const SmokeSimulationFlowBoundaryConfig boundary) {
        const int i = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
        const int j = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
        const int k = static_cast<int>(blockIdx.z * blockDim.z + threadIdx.z);
        if (i >= nx || j >= ny || k > nz) return;

        auto& face = velocity_z[index_velocity_z(i, j, k, nx, ny)];
        if (k == 0) {
            const auto domain_face = boundary.z_minus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nz > 0)
                    face = velocity_z[index_velocity_z(i, j, 1, nx, ny)];
                else
                    face = domain_face.velocity_z;
                return;
            }
        }
        if (k == nz) {
            const auto domain_face = boundary.z_plus;
            if (domain_face.type != SMOKE_SIMULATION_FLOW_BOUNDARY_PERIODIC) {
                if (domain_face.type == SMOKE_SIMULATION_FLOW_BOUNDARY_OUTFLOW && nz > 0)
                    face = velocity_z[index_velocity_z(i, j, nz - 1, nx, ny)];
                else
                    face = domain_face.velocity_z;
                return;
            }
        }

        int back_x                = i;
        int back_y                = j;
        int back_z                = k - 1;
        int front_x               = i;
        int front_y               = j;
        int front_z               = k;
        const bool has_back       = resolve_cell_coordinates(back_x, back_y, back_z, nx, ny, nz, boundary);
        const bool has_front      = resolve_cell_coordinates(front_x, front_y, front_z, nx, ny, nz, boundary);
        const bool back_occupied  = has_back && occupancy != nullptr && occupancy[index_3d(back_x, back_y, back_z, nx, ny)] != 0;
        const bool front_occupied = has_front && occupancy != nullptr && occupancy[index_3d(front_x, front_y, front_z, nx, ny)] != 0;
        if (back_occupied || front_occupied) {
            float value  = 0.0f;
            float weight = 0.0f;
            if (back_occupied) {
                value += solid_velocity_value(solid_velocity_z, occupancy, back_x, back_y, back_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            if (front_occupied) {
                value += solid_velocity_value(solid_velocity_z, occupancy, front_x, front_y, front_z, nx, ny, nz, boundary);
                weight += 1.0f;
            }
            face = weight > 0.0f ? value / weight : 0.0f;
            return;
        }
        if (has_back && has_front) {
            const float pressure_front = pressure[index_3d(front_x, front_y, front_z, nx, ny)];
            const float pressure_back  = pressure[index_3d(back_x, back_y, back_z, nx, ny)];
            face -= dt * (pressure_front - pressure_back) / h;
        }
    }

    void check_cuda(const cudaError_t status, const char* what) {
        if (status == cudaSuccess) return;
        throw std::runtime_error(std::string(what) + ": " + cudaGetErrorString(status));
    }

    void validate_axis(const std::uint32_t axis, const char* what) {
        if (axis < 3u) return;
        throw std::runtime_error(std::string(what) + ": axis must be 0, 1, or 2");
    }

    void launch_fill_float(const cudaStream_t stream, const unsigned grid, const unsigned block, float* field, const float value, const std::uint64_t count) {
        fill_float_kernel<<<grid, block, 0, stream>>>(field, value, count);
        check_cuda(cudaGetLastError(), "fill_float_kernel");
    }

    void launch_fill_int(const cudaStream_t stream, const unsigned grid, const unsigned block, int* field, const int value, const std::uint64_t count) {
        fill_int_kernel<<<grid, block, 0, stream>>>(field, value, count);
        check_cuda(cudaGetLastError(), "fill_int_kernel");
    }

    void launch_add_scaled(const cudaStream_t stream, const unsigned grid, const unsigned block, float* destination, const float* current, const float* source, const float scale, const std::uint64_t count) {
        add_source_kernel<<<grid, block, 0, stream>>>(destination, current, source, scale, count);
        check_cuda(cudaGetLastError(), "add_source_kernel");
    }

    void launch_center_staggered_vector(const cudaStream_t stream, const dim3 grid, const dim3 block, float* cell_x, float* cell_y, float* cell_z, const float* velocity_x, const float* velocity_y, const float* velocity_z, const int nx, const int ny, const int nz) {
        compute_center_velocity_kernel<<<grid, block, 0, stream>>>(cell_x, cell_y, cell_z, velocity_x, velocity_y, velocity_z, nx, ny, nz);
        check_cuda(cudaGetLastError(), "compute_center_velocity_kernel");
    }

    void launch_compute_vorticity(const cudaStream_t stream, const dim3 grid, const dim3 block, float* omega_x, float* omega_y, float* omega_z, float* omega_magnitude, const float* cell_x, const float* cell_y, const float* cell_z, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        compute_vorticity_kernel<<<grid, block, 0, stream>>>(omega_x, omega_y, omega_z, omega_magnitude, cell_x, cell_y, cell_z, occupancy, nx, ny, nz, h, boundary);
        check_cuda(cudaGetLastError(), "compute_vorticity_kernel");
    }

    void launch_add_buoyancy(const cudaStream_t stream, const dim3 grid, const dim3 block, float* force_y, const float* density, const float* temperature, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const float ambient_temperature, const float density_factor, const float temperature_factor, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        add_buoyancy_kernel<<<grid, block, 0, stream>>>(force_y, density, temperature, occupancy, nx, ny, nz, ambient_temperature, density_factor, temperature_factor, boundary);
        check_cuda(cudaGetLastError(), "add_buoyancy_kernel");
    }

    void launch_add_vorticity_confinement(const cudaStream_t stream, const dim3 grid, const dim3 block, float* force_x, float* force_y, float* force_z, const float* omega_x, const float* omega_y, const float* omega_z, const float* omega_magnitude, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float epsilon, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        add_confinement_kernel<<<grid, block, 0, stream>>>(force_x, force_y, force_z, omega_x, omega_y, omega_z, omega_magnitude, occupancy, nx, ny, nz, h, epsilon, boundary);
        check_cuda(cudaGetLastError(), "add_confinement_kernel");
    }

    void launch_add_center_force_to_staggered_component(const cudaStream_t stream, const dim3 grid, const dim3 block, const std::uint32_t axis, float* velocity_component, const float* force_component, const int nx, const int ny, const int nz, const float dt) {
        validate_axis(axis, "launch_add_center_force_to_staggered_component");
        if (axis == 0u) add_center_forces_to_velocity_x_kernel<<<grid, block, 0, stream>>>(velocity_component, force_component, nx, ny, nz, dt);
        if (axis == 1u) add_center_forces_to_velocity_y_kernel<<<grid, block, 0, stream>>>(velocity_component, force_component, nx, ny, nz, dt);
        if (axis == 2u) add_center_forces_to_velocity_z_kernel<<<grid, block, 0, stream>>>(velocity_component, force_component, nx, ny, nz, dt);
        check_cuda(cudaGetLastError(), "add_center_force_to_staggered_component_kernel");
    }

    void launch_enforce_staggered_boundary(const cudaStream_t stream, const dim3 grid, const dim3 block, const std::uint32_t axis, float* velocity_component, const std::uint8_t* occupancy, const float* solid_velocity_component, const int nx, const int ny, const int nz, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        validate_axis(axis, "launch_enforce_staggered_boundary");
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        if (axis == 0u) enforce_velocity_x_boundaries_kernel<<<grid, block, 0, stream>>>(velocity_component, occupancy, solid_velocity_component, nx, ny, nz, boundary);
        if (axis == 1u) enforce_velocity_y_boundaries_kernel<<<grid, block, 0, stream>>>(velocity_component, occupancy, solid_velocity_component, nx, ny, nz, boundary);
        if (axis == 2u) enforce_velocity_z_boundaries_kernel<<<grid, block, 0, stream>>>(velocity_component, occupancy, solid_velocity_component, nx, ny, nz, boundary);
        check_cuda(cudaGetLastError(), "enforce_staggered_boundary_kernel");
    }

    void launch_sync_periodic_staggered_component(const cudaStream_t stream, const dim3 grid, const dim3 block, const std::uint32_t axis, float* velocity_component, const int nx, const int ny, const int nz) {
        validate_axis(axis, "launch_sync_periodic_staggered_component");
        if (axis == 0u) sync_periodic_velocity_x_kernel<<<grid, block, 0, stream>>>(velocity_component, nx, ny, nz);
        if (axis == 1u) sync_periodic_velocity_y_kernel<<<grid, block, 0, stream>>>(velocity_component, nx, ny, nz);
        if (axis == 2u) sync_periodic_velocity_z_kernel<<<grid, block, 0, stream>>>(velocity_component, nx, ny, nz);
        check_cuda(cudaGetLastError(), "sync_periodic_staggered_component_kernel");
    }

    void launch_advect_staggered_component(const cudaStream_t stream, const dim3 grid, const dim3 block, const std::uint32_t axis, float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const std::uint32_t advection_mode, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        validate_axis(axis, "launch_advect_staggered_component");
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        if (axis == 0u) advect_velocity_x_kernel<<<grid, block, 0, stream>>>(destination, source, velocity_x, velocity_y, velocity_z, occupancy, nx, ny, nz, h, dt, advection_mode, boundary);
        if (axis == 1u) advect_velocity_y_kernel<<<grid, block, 0, stream>>>(destination, source, velocity_x, velocity_y, velocity_z, occupancy, nx, ny, nz, h, dt, advection_mode, boundary);
        if (axis == 2u) advect_velocity_z_kernel<<<grid, block, 0, stream>>>(destination, source, velocity_x, velocity_y, velocity_z, occupancy, nx, ny, nz, h, dt, advection_mode, boundary);
        check_cuda(cudaGetLastError(), "advect_staggered_component_kernel");
    }

    void launch_advect_centered_scalar(const cudaStream_t stream, const dim3 grid, const dim3 block, float* destination, const float* source, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const float h, const float dt, const std::uint32_t advection_mode, const std::uint32_t* scalar_boundary_types, const float* scalar_boundary_values, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationScalarBoundaryConfig scalar_boundary = make_scalar_boundary(scalar_boundary_types, scalar_boundary_values);
        const SmokeSimulationFlowBoundaryConfig flow_boundary     = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        advect_scalar_kernel<<<grid, block, 0, stream>>>(destination, source, velocity_x, velocity_y, velocity_z, occupancy, nx, ny, nz, h, dt, advection_mode, scalar_boundary, flow_boundary);
        check_cuda(cudaGetLastError(), "advect_scalar_kernel");
    }

    void launch_apply_solid_scalar(const cudaStream_t stream, const unsigned grid, const unsigned block, float* scalar, const std::uint8_t* occupancy, const float* solid_scalar, const int nx, const int ny, const int nz, const float default_value) {
        apply_solid_temperature_kernel<<<grid, block, 0, stream>>>(scalar, occupancy, solid_scalar, nx, ny, nz, default_value);
        check_cuda(cudaGetLastError(), "apply_solid_temperature_kernel");
    }

    void launch_boundary_fill_centered_scalar(const cudaStream_t stream, const dim3 grid, const dim3 block, float* destination, const float* source, const std::uint8_t* occupancy, const int nx, const int ny, const int nz, const std::uint32_t* scalar_boundary_types, const float* scalar_boundary_values) {
        const SmokeSimulationScalarBoundaryConfig boundary = make_scalar_boundary(scalar_boundary_types, scalar_boundary_values);
        boundary_fill_density_kernel<<<grid, block, 0, stream>>>(destination, source, occupancy, nx, ny, nz, boundary);
        check_cuda(cudaGetLastError(), "boundary_fill_density_kernel");
    }

    void launch_find_pressure_anchor(const cudaStream_t stream, const unsigned grid, const unsigned block, int* pressure_anchor, const std::uint8_t* occupancy, const std::uint64_t count) {
        find_pressure_anchor_kernel<<<grid, block, 0, stream>>>(pressure_anchor, occupancy, count);
        check_cuda(cudaGetLastError(), "find_pressure_anchor_kernel");
    }

    void launch_compute_projection_rhs(const cudaStream_t stream, const dim3 grid, const dim3 block, float* rhs, const float* velocity_x, const float* velocity_y, const float* velocity_z, const std::uint8_t* occupancy, const int* pressure_anchor, const int nx, const int ny, const int nz, const float h, const float dt, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        compute_pressure_rhs_kernel<<<grid, block, 0, stream>>>(rhs, velocity_x, velocity_y, velocity_z, occupancy, pressure_anchor, nx, ny, nz, h, dt, boundary);
        check_cuda(cudaGetLastError(), "compute_pressure_rhs_kernel");
    }

    void launch_build_projection_matrix(const cudaStream_t stream, const unsigned grid, const unsigned block, float* values, const int* row_offsets, const int* column_indices, const std::uint8_t* occupancy, const int* pressure_anchor, const int nx, const int ny, const int nz, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        build_pressure_matrix_kernel<<<grid, block, 0, stream>>>(values, row_offsets, column_indices, occupancy, pressure_anchor, nx, ny, nz, boundary);
        check_cuda(cudaGetLastError(), "build_pressure_matrix_kernel");
    }

    void launch_compute_ratio(const cudaStream_t stream, float* destination, const float* numerator, const float* denominator) {
        compute_ratio_kernel<<<1, 1, 0, stream>>>(destination, numerator, denominator);
        check_cuda(cudaGetLastError(), "compute_ratio_kernel");
    }

    void launch_negate_scalar(const cudaStream_t stream, float* destination, const float* source) {
        negate_scalar_kernel<<<1, 1, 0, stream>>>(destination, source);
        check_cuda(cudaGetLastError(), "negate_scalar_kernel");
    }

    void launch_project_staggered_component(const cudaStream_t stream, const dim3 grid, const dim3 block, const std::uint32_t axis, float* velocity_component, const float* pressure, const std::uint8_t* occupancy, const float* solid_velocity_component, const int nx, const int ny, const int nz, const float h, const float dt, const std::uint32_t* flow_boundary_types, const float* flow_boundary_velocity, const float* flow_boundary_pressure) {
        validate_axis(axis, "launch_project_staggered_component");
        const SmokeSimulationFlowBoundaryConfig boundary = make_flow_boundary(flow_boundary_types, flow_boundary_velocity, flow_boundary_pressure);
        if (axis == 0u) project_velocity_x_kernel<<<grid, block, 0, stream>>>(velocity_component, pressure, occupancy, solid_velocity_component, nx, ny, nz, h, dt, boundary);
        if (axis == 1u) project_velocity_y_kernel<<<grid, block, 0, stream>>>(velocity_component, pressure, occupancy, solid_velocity_component, nx, ny, nz, h, dt, boundary);
        if (axis == 2u) project_velocity_z_kernel<<<grid, block, 0, stream>>>(velocity_component, pressure, occupancy, solid_velocity_component, nx, ny, nz, h, dt, boundary);
        check_cuda(cudaGetLastError(), "project_staggered_component_kernel");
    }

} // namespace xayah::projects::pyro::cuda
