module;
#include <vulkan/vulkan_raii.hpp>

module scene_renderer;
import scene;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    std::vector<std::uint32_t> read_spirv(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) throw std::runtime_error(std::string{"Failed to open SPIR-V shader: "} + path.string());

        const std::streamoff byte_count = file.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::string{"Invalid SPIR-V shader size: "} + path.string());

        std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(code.data()), byte_count);
        if (!file) throw std::runtime_error(std::string{"Failed to read SPIR-V shader: "} + path.string());
        return code;
    }

    [[nodiscard]] std::array<float, 16> multiply_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        std::array<float, 16> result{};
        for (std::uint32_t column = 0; column < 4; ++column) {
            for (std::uint32_t row = 0; row < 4; ++row) {
                result[column * 4 + row] = left[0 * 4 + row] * right[column * 4 + 0] + left[1 * 4 + row] * right[column * 4 + 1] + left[2 * 4 + row] * right[column * 4 + 2] + left[3 * 4 + row] * right[column * 4 + 3];
            }
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> multiply_transform_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        std::array<float, 16> result{};
        for (std::uint32_t row = 0; row < 4; ++row) {
            const std::uint32_t base = row * 4;
            result[base + 0]        = left[base + 0] * right[0] + left[base + 1] * right[4] + left[base + 2] * right[8] + left[base + 3] * right[12];
            result[base + 1]        = left[base + 0] * right[1] + left[base + 1] * right[5] + left[base + 2] * right[9] + left[base + 3] * right[13];
            result[base + 2]        = left[base + 0] * right[2] + left[base + 1] * right[6] + left[base + 2] * right[10] + left[base + 3] * right[14];
            result[base + 3]        = left[base + 0] * right[3] + left[base + 1] * right[7] + left[base + 2] * right[11] + left[base + 3] * right[15];
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> transform_matrix(const xayah::Transform& transform) {
        constexpr float degrees_to_radians = 0.017453292519943295769f;
        const float x = transform.rotation_degrees[0] * degrees_to_radians;
        const float y = transform.rotation_degrees[1] * degrees_to_radians;
        const float z = transform.rotation_degrees[2] * degrees_to_radians;
        const float cx = std::cos(x);
        const float sx = std::sin(x);
        const float cy = std::cos(y);
        const float sy = std::sin(y);
        const float cz = std::cos(z);
        const float sz = std::sin(z);

        const std::array rotation_x{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, cx, sx, 0.0f,
            0.0f, -sx, cx, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        const std::array rotation_y{
            cy, 0.0f, -sy, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            sy, 0.0f, cy, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        const std::array rotation_z{
            cz, sz, 0.0f, 0.0f,
            -sz, cz, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };

        std::array<float, 16> matrix = multiply_transform_matrix(multiply_transform_matrix(rotation_x, rotation_y), rotation_z);
        for (std::uint32_t row = 0; row < 3; ++row) {
            matrix[0 * 4 + row] *= transform.scale[0];
            matrix[1 * 4 + row] *= transform.scale[1];
            matrix[2 * 4 + row] *= transform.scale[2];
        }
        matrix[12] = transform.translation[0];
        matrix[13] = transform.translation[1];
        matrix[14] = transform.translation[2];
        matrix[15] = 1.0f;
        return matrix;
    }

    [[nodiscard]] std::array<float, 16> inverse_affine_matrix(const std::array<float, 16>& matrix) {
        const float a00 = matrix[0];
        const float a01 = matrix[4];
        const float a02 = matrix[8];
        const float a10 = matrix[1];
        const float a11 = matrix[5];
        const float a12 = matrix[9];
        const float a20 = matrix[2];
        const float a21 = matrix[6];
        const float a22 = matrix[10];
        const float determinant = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
        if (std::abs(determinant) <= 0.000001f) throw std::runtime_error("Cannot invert singular scene object transform");

        const float inv_det = 1.0f / determinant;
        const float b00 = (a11 * a22 - a12 * a21) * inv_det;
        const float b01 = (a02 * a21 - a01 * a22) * inv_det;
        const float b02 = (a01 * a12 - a02 * a11) * inv_det;
        const float b10 = (a12 * a20 - a10 * a22) * inv_det;
        const float b11 = (a00 * a22 - a02 * a20) * inv_det;
        const float b12 = (a02 * a10 - a00 * a12) * inv_det;
        const float b20 = (a10 * a21 - a11 * a20) * inv_det;
        const float b21 = (a01 * a20 - a00 * a21) * inv_det;
        const float b22 = (a00 * a11 - a01 * a10) * inv_det;
        const float tx = matrix[12];
        const float ty = matrix[13];
        const float tz = matrix[14];

        return {
            b00, b10, b20, 0.0f,
            b01, b11, b21, 0.0f,
            b02, b12, b22, 0.0f,
            -(b00 * tx + b01 * ty + b02 * tz),
            -(b10 * tx + b11 * ty + b12 * tz),
            -(b20 * tx + b21 * ty + b22 * tz),
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> normal_matrix(const std::array<float, 16>& matrix) {
        const std::array<float, 16> inverse = inverse_affine_matrix(matrix);
        return {
            inverse[0], inverse[4], inverse[8], 0.0f,
            inverse[1], inverse[5], inverse[9], 0.0f,
            inverse[2], inverse[6], inverse[10], 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    }

    [[nodiscard]] std::array<float, 3> transform_point(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
        return {
            matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12],
            matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13],
            matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14],
        };
    }

    [[nodiscard]] float maximum_scale(const xayah::Transform& transform) {
        return std::max(transform.scale[0], std::max(transform.scale[1], transform.scale[2]));
    }

    struct VolumeShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 16> model{};
        std::array<float, 4> camera_local_step{};
        std::array<float, 4> local_min_opacity{};
        std::array<float, 4> spacing_value_min{};
        std::array<std::uint32_t, 4> resolution_kind{};
        std::array<std::uint32_t, 4> mode_options{};
        std::array<float, 4> slice_value_max{};
    };

    struct MeshShaderVertex {
        [[maybe_unused]] std::array<float, 4> position{};
        [[maybe_unused]] std::array<float, 4> normal{};
        [[maybe_unused]] std::array<float, 4> color{};
    };

    MeshShaderVertex mesh_shader_vertex(const xayah::MeshVertex& vertex) {
        return MeshShaderVertex{
            {vertex.position[0], vertex.position[1], vertex.position[2], 1.0f},
            {vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0f},
            {vertex.color[0], vertex.color[1], vertex.color[2], 1.0f},
        };
    }

    struct MeshShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 16> model{};
        std::array<float, 16> normal_matrix{};
        std::array<float, 4> light_direction{};
    };

    struct ParticleShaderParameters {
        std::array<float, 16> view_projection{};
    };

    struct ParticleShaderVertex {
        [[maybe_unused]] std::array<float, 4> position{};
        [[maybe_unused]] std::array<float, 4> local_position{};
        [[maybe_unused]] std::array<float, 4> color{};
    };

    struct BoundingBoxShaderParameters {
        std::array<float, 16> model_view_projection{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<float, 4> color{};
    };

    struct BoundingBoxBounds {
        std::array<float, 3> minimum{};
        std::array<float, 3> maximum{};
    };

    BoundingBoxBounds volume_bounds(const xayah::Volume& volume) {
        return BoundingBoxBounds{
            {
                -volume.size[0] * 0.5f,
                -volume.size[1] * 0.5f,
                -volume.size[2] * 0.5f,
            },
            {
                volume.size[0] * 0.5f,
                volume.size[1] * 0.5f,
                volume.size[2] * 0.5f,
            },
        };
    }

    BoundingBoxBounds mesh_bounds(const xayah::Mesh& mesh) {
        if (mesh.vertices.empty()) throw std::runtime_error(std::string{"Cannot compute bounding box for empty mesh: "} + mesh.name);
        BoundingBoxBounds bounds{mesh.vertices.front().position, mesh.vertices.front().position};
        for (const xayah::MeshVertex& vertex : mesh.vertices) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (vertex.position[axis] < bounds.minimum[axis]) bounds.minimum[axis] = vertex.position[axis];
                if (vertex.position[axis] > bounds.maximum[axis]) bounds.maximum[axis] = vertex.position[axis];
            }
        }
        return bounds;
    }

    BoundingBoxBounds particles_bounds(const xayah::Particles& particles) {
        if (particles.particles.empty()) throw std::runtime_error(std::string{"Cannot compute bounding box for empty particles object: "} + particles.name);
        if (particles.render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + particles.name);

        const xayah::Particle& first_particle = particles.particles.front();
        if (first_particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + particles.name);
        const float first_radius = first_particle.radius * particles.render_settings.radius_scale;
        BoundingBoxBounds bounds{
            {
                first_particle.position[0] - first_radius,
                first_particle.position[1] - first_radius,
                first_particle.position[2] - first_radius,
            },
            {
                first_particle.position[0] + first_radius,
                first_particle.position[1] + first_radius,
                first_particle.position[2] + first_radius,
            },
        };

        for (const xayah::Particle& particle : particles.particles) {
            if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + particles.name);
            const float radius = particle.radius * particles.render_settings.radius_scale;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const float minimum = particle.position[axis] - radius;
                const float maximum = particle.position[axis] + radius;
                if (minimum < bounds.minimum[axis]) bounds.minimum[axis] = minimum;
                if (maximum > bounds.maximum[axis]) bounds.maximum[axis] = maximum;
            }
        }
        return bounds;
    }

    std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching memory type for buffer");
    }

    void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, const vk::DeviceSize requested_size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags required_properties) {
        if (requested_size == 0) throw std::runtime_error("Cannot create zero-size buffer");
        if (*buffer && *memory && buffer_size == requested_size) return;

        vk::raii::Buffer next_buffer{nullptr};
        vk::raii::DeviceMemory next_memory{nullptr};
        const vk::BufferCreateInfo buffer_create_info{{}, requested_size, usage, vk::SharingMode::eExclusive};
        next_buffer = vk::raii::Buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = next_buffer.getMemoryRequirements();
        const std::uint32_t type_index                   = memory_type_index(physical_device, memory_requirements.memoryTypeBits, required_properties);
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, type_index};
        next_memory = vk::raii::DeviceMemory{device, allocate_info};
        next_buffer.bindMemory(*next_memory, 0);

        buffer      = std::move(next_buffer);
        memory      = std::move(next_memory);
        buffer_size = requested_size;
    }

    void write_buffer(const vk::raii::DeviceMemory& memory, const vk::DeviceSize buffer_size, const void* data, const vk::DeviceSize write_size) {
        if (!*memory) throw std::runtime_error("Cannot write unallocated buffer");
        if (write_size > buffer_size) throw std::runtime_error("Buffer write exceeds allocation size");
        void* mapped = memory.mapMemory(0, write_size);
        std::memcpy(mapped, data, static_cast<std::size_t>(write_size));
        memory.unmapMemory();
    }
} // namespace

namespace xayah {
    SceneRenderer::SceneRenderer() = default;

    SceneRenderer::~SceneRenderer() noexcept = default;

    void SceneRenderer::create(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect, const std::uint32_t frame_count, const Scene& scene) {
        if (this->active()) throw std::runtime_error("Scene renderer is already initialized");
        this->create_bounding_box_renderer(device, color_format, depth_format, depth_aspect);
        if (!scene.meshes.empty()) this->create_mesh_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
        if (!scene.particles.empty()) this->create_particles_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
        if (!scene.volumes.empty()) this->create_volume_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
    }

    void SceneRenderer::destroy() noexcept {
        this->destroy_volume_renderer();
        this->destroy_particles_renderer();
        this->destroy_mesh_renderer();
        this->destroy_bounding_box_renderer();
    }

    void SceneRenderer::recreate(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect, const std::uint32_t frame_count, const Scene& scene) {
        const bool recreate_bounding_box       = static_cast<bool>(*this->bounding_box_renderer.pipeline);
        const bool recreate_mesh_renderer      = static_cast<bool>(*this->mesh_renderer.surface_pipeline);
        const bool recreate_particles_renderer = static_cast<bool>(*this->particles_renderer.pipeline);
        const bool recreate_volume_renderer    = static_cast<bool>(*this->volume_renderer.pipeline);
        this->destroy();
        if (recreate_bounding_box) this->create_bounding_box_renderer(device, color_format, depth_format, depth_aspect);
        if (recreate_mesh_renderer) this->create_mesh_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
        if (recreate_particles_renderer) this->create_particles_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
        if (recreate_volume_renderer) this->create_volume_renderer(device, color_format, depth_format, depth_aspect, frame_count, scene);
    }

    void SceneRenderer::render(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_position, const std::array<float, 3>& camera_right, const std::array<float, 3>& camera_up, Scene& scene) {
        if (frame_index >= frame_count) throw std::runtime_error("Frame index is outside scene renderer frame resource range");
        this->render_meshes(physical_device, device, command_buffer, frame_index, frame_count, view_projection, scene);
        this->render_particles(physical_device, device, command_buffer, frame_index, frame_count, view_projection, camera_right, camera_up, scene);
        this->render_volumes(physical_device, device, command_buffer, frame_index, frame_count, view_projection, camera_position, scene);
        this->render_bounding_boxes(command_buffer, view_projection, scene);
    }

    bool SceneRenderer::active() const {
        return static_cast<bool>(*this->bounding_box_renderer.pipeline) || static_cast<bool>(*this->mesh_renderer.surface_pipeline) || static_cast<bool>(*this->particles_renderer.pipeline) || static_cast<bool>(*this->volume_renderer.pipeline);
    }

    void SceneRenderer::render_meshes(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const std::uint32_t frame_count, const std::array<float, 16>& view_projection, Scene& scene) {
        const std::size_t scene_mesh_count = scene.meshes.size();
        if (scene_mesh_count == 0) return;
        if (!*this->mesh_renderer.pipeline_layout || !*this->mesh_renderer.surface_pipeline || !*this->mesh_renderer.wireframe_pipeline || this->mesh_renderer.descriptor_sets.size() == 0) throw std::runtime_error("Mesh renderer is not initialized");
        if (this->mesh_renderer.frame_resources.size() != static_cast<std::size_t>(frame_count) * scene_mesh_count) throw std::runtime_error("Mesh renderer resources do not match scene mesh count");
        if (this->mesh_renderer.descriptor_sets.size() != static_cast<std::size_t>(frame_count) * scene_mesh_count) throw std::runtime_error("Mesh descriptor sets do not match scene mesh count");

        constexpr vk::BufferUsageFlags storage_buffer_usage{vk::BufferUsageFlagBits::eStorageBuffer};
        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        for (std::size_t mesh_index = 0; mesh_index < scene_mesh_count; ++mesh_index) {
            const Mesh& mesh = scene.meshes[mesh_index];
            if (!mesh.visible) continue;
            const std::size_t resource_index = static_cast<std::size_t>(frame_index) * scene_mesh_count + mesh_index;
            MeshDrawResources& resources     = this->mesh_renderer.frame_resources.at(resource_index);

            std::vector<MeshShaderVertex> shader_vertices{};
            if (mesh.render_settings.display_mode == MeshDisplayMode::wireframe) {
                if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 2)) throw std::runtime_error(std::string{"Mesh has too many indices for wireframe draw: "} + mesh.name);
                shader_vertices.reserve(mesh.indices.size() * 2);
                for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
                    const std::uint32_t i0 = mesh.indices[index + 0];
                    const std::uint32_t i1 = mesh.indices[index + 1];
                    const std::uint32_t i2 = mesh.indices[index + 2];
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i0]));
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i1]));
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i1]));
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i2]));
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i2]));
                    shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[i0]));
                }
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.wireframe_pipeline);
            } else {
                shader_vertices.reserve(mesh.indices.size());
                for (const std::uint32_t index : mesh.indices) shader_vertices.emplace_back(mesh_shader_vertex(mesh.vertices[index]));
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.surface_pipeline);
            }
            if (shader_vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::string{"Mesh has too many expanded vertices for draw: "} + mesh.name);

            MeshShaderParameters parameters{};
            parameters.view_projection = view_projection;
            parameters.model           = transform_matrix(mesh.transform);
            parameters.normal_matrix   = normal_matrix(parameters.model);
            parameters.light_direction = {-0.45f, -0.85f, -0.25f, 0.0f};

            ensure_buffer(physical_device, device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, shader_vertices.size() * sizeof(MeshShaderVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
            ensure_buffer(physical_device, device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(MeshShaderParameters), storage_buffer_usage, upload_memory_properties);
            write_buffer(resources.vertex_memory, resources.vertex_size, shader_vertices.data(), shader_vertices.size() * sizeof(MeshShaderVertex));
            write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(MeshShaderParameters));

            const std::array buffer_infos{
                vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
            };
            const vk::DescriptorSet descriptor_set = *this->mesh_renderer.descriptor_sets[resource_index];
            const std::array writes{
                vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
            };
            device.updateDescriptorSets(writes, {});

            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
            const std::array vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            command_buffer.bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            command_buffer.draw(static_cast<std::uint32_t>(shader_vertices.size()), 1, 0, 0);
        }
    }

    void SceneRenderer::render_particles(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_right, const std::array<float, 3>& camera_up, Scene& scene) {
        const std::size_t scene_particles_count = scene.particles.size();
        if (scene_particles_count == 0) return;
        if (!*this->particles_renderer.pipeline_layout || !*this->particles_renderer.pipeline) throw std::runtime_error("Particles renderer is not initialized");
        if (this->particles_renderer.frame_resources.size() != static_cast<std::size_t>(frame_count) * scene_particles_count) throw std::runtime_error("Particles renderer resources do not match scene particles count");

        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->particles_renderer.pipeline);
        for (std::size_t particles_index = 0; particles_index < scene_particles_count; ++particles_index) {
            const Particles& particles = scene.particles[particles_index];
            if (!particles.visible || particles.particles.empty()) continue;
            if (particles.render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + particles.name);
            if (particles.particles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 6)) throw std::runtime_error(std::string{"Particles object has too many particles for draw: "} + particles.name);

            const std::size_t resource_index = static_cast<std::size_t>(frame_index) * scene_particles_count + particles_index;
            ParticleDrawResources& resources = this->particles_renderer.frame_resources.at(resource_index);

            std::vector<ParticleShaderVertex> shader_vertices{};
            shader_vertices.reserve(particles.particles.size() * 6);
            const std::array<float, 16> model = transform_matrix(particles.transform);
            const float object_radius_scale   = maximum_scale(particles.transform);
            constexpr std::array particle_corners{
                std::array{-1.0f, -1.0f},
                std::array{1.0f, -1.0f},
                std::array{1.0f, 1.0f},
                std::array{-1.0f, -1.0f},
                std::array{1.0f, 1.0f},
                std::array{-1.0f, 1.0f},
            };
            for (const Particle& particle : particles.particles) {
                if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + particles.name);
                const std::array<float, 3> center = transform_point(model, particle.position);
                const float radius = particle.radius * particles.render_settings.radius_scale * object_radius_scale;
                for (const std::array<float, 2>& corner : particle_corners) {
                    shader_vertices.emplace_back(ParticleShaderVertex{
                        {
                            center[0] + camera_right[0] * corner[0] * radius + camera_up[0] * corner[1] * radius,
                            center[1] + camera_right[1] * corner[0] * radius + camera_up[1] * corner[1] * radius,
                            center[2] + camera_right[2] * corner[0] * radius + camera_up[2] * corner[1] * radius,
                            1.0f,
                        },
                        {corner[0], corner[1], 0.0f, 0.0f},
                        {particle.color[0], particle.color[1], particle.color[2], 1.0f},
                    });
                }
            }

            ParticleShaderParameters parameters{};
            parameters.view_projection = view_projection;

            ensure_buffer(physical_device, device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, shader_vertices.size() * sizeof(ParticleShaderVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
            write_buffer(resources.vertex_memory, resources.vertex_size, shader_vertices.data(), shader_vertices.size() * sizeof(ParticleShaderVertex));

            command_buffer.pushConstants(*this->particles_renderer.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const ParticleShaderParameters>{1, &parameters});
            const std::array particle_vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
            constexpr std::array<vk::DeviceSize, 1> particle_vertex_offsets{0};
            command_buffer.bindVertexBuffers(0, particle_vertex_buffers, particle_vertex_offsets);
            command_buffer.draw(static_cast<std::uint32_t>(shader_vertices.size()), 1, 0, 0);
        }
    }

    void SceneRenderer::render_volumes(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index, const std::uint32_t frame_count, const std::array<float, 16>& view_projection, const std::array<float, 3>& camera_position, Scene& scene) {
        const std::size_t scene_volume_count = scene.volumes.size();
        if (scene_volume_count == 0) return;
        if (!*this->volume_renderer.pipeline_layout || !*this->volume_renderer.pipeline || this->volume_renderer.descriptor_sets.size() == 0) throw std::runtime_error("Volume renderer is not initialized");
        if (this->volume_renderer.frame_resources.size() != static_cast<std::size_t>(frame_count) * scene_volume_count) throw std::runtime_error("Volume renderer resources do not match scene volume count");
        if (this->volume_renderer.descriptor_sets.size() != static_cast<std::size_t>(frame_count) * scene_volume_count) throw std::runtime_error("Volume descriptor sets do not match scene volume count");

        constexpr vk::BufferUsageFlags storage_buffer_usage{vk::BufferUsageFlagBits::eStorageBuffer};
        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->volume_renderer.pipeline);
        for (std::size_t volume_index = 0; volume_index < scene_volume_count; ++volume_index) {
            const Volume& volume = scene.volumes[volume_index];
            if (!volume.visible) continue;
            const VolumeRenderSettings& render_settings = volume.render_settings;
            if (render_settings.opacity < 0.0f || render_settings.opacity > 1.0f) throw std::runtime_error(std::string{"Volume opacity must be in [0, 1]: "} + volume.name);
            if (render_settings.raymarch_step <= 0.0f) throw std::runtime_error(std::string{"Volume raymarch step must be positive: "} + volume.name);
            if (render_settings.value_min >= render_settings.value_max) throw std::runtime_error(std::string{"Volume value range is invalid: "} + volume.name);
            if (render_settings.slice_position < 0.0f || render_settings.slice_position > 1.0f) throw std::runtime_error(std::string{"Volume slice position must be in [0, 1]: "} + volume.name);

            const std::size_t resource_index = static_cast<std::size_t>(frame_index) * scene_volume_count + volume_index;
            VolumeDrawResources& resources   = this->volume_renderer.frame_resources.at(resource_index);
            const std::array<float, 16> model = transform_matrix(volume.transform);
            const std::array<float, 16> inverse_model = inverse_affine_matrix(model);
            const std::array<float, 3> camera_local   = transform_point(inverse_model, camera_position);
            const std::array<float, 3> local_min{
                -volume.size[0] * 0.5f,
                -volume.size[1] * 0.5f,
                -volume.size[2] * 0.5f,
            };

            if (render_settings.grid_kind == VolumeGridKind::centered_scalar) {
                const CenteredScalarGrid& grid = scene.render_centered_scalar_grid(volume);
                const std::array spacing{
                    volume.size[0] / static_cast<float>(grid.resolution[0]),
                    volume.size[1] / static_cast<float>(grid.resolution[1]),
                    volume.size[2] / static_cast<float>(grid.resolution[2]),
                };

                VolumeShaderParameters parameters{};
                parameters.view_projection   = view_projection;
                parameters.model             = model;
                parameters.camera_local_step = {camera_local[0], camera_local[1], camera_local[2], render_settings.raymarch_step};
                parameters.local_min_opacity = {local_min[0], local_min[1], local_min[2], render_settings.opacity};
                parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], render_settings.value_min};
                parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::centered_scalar)};
                parameters.mode_options      = {static_cast<std::uint32_t>(render_settings.display_mode), static_cast<std::uint32_t>(render_settings.slice_axis), static_cast<std::uint32_t>(render_settings.color_map), 0};
                parameters.slice_value_max   = {render_settings.slice_position, render_settings.value_max, 0.0f, 0.0f};

                ensure_buffer(physical_device, device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(physical_device, device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
                write_buffer(resources.x_data_memory, resources.x_data_size, grid.values.data(), grid.values.size() * sizeof(float));
                write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
            } else {
                const StaggeredVectorGrid& grid = scene.render_staggered_vector_grid(volume);
                const std::array spacing{
                    volume.size[0] / static_cast<float>(grid.resolution[0]),
                    volume.size[1] / static_cast<float>(grid.resolution[1]),
                    volume.size[2] / static_cast<float>(grid.resolution[2]),
                };

                VolumeShaderParameters parameters{};
                parameters.view_projection   = view_projection;
                parameters.model             = model;
                parameters.camera_local_step = {camera_local[0], camera_local[1], camera_local[2], render_settings.raymarch_step};
                parameters.local_min_opacity = {local_min[0], local_min[1], local_min[2], render_settings.opacity};
                parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], render_settings.value_min};
                parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::staggered_vector)};
                parameters.mode_options      = {static_cast<std::uint32_t>(render_settings.display_mode), static_cast<std::uint32_t>(render_settings.slice_axis), static_cast<std::uint32_t>(render_settings.color_map), 0};
                parameters.slice_value_max   = {render_settings.slice_position, render_settings.value_max, 0.0f, 0.0f};

                ensure_buffer(physical_device, device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.x_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(physical_device, device, resources.y_data_buffer, resources.y_data_memory, resources.y_data_size, grid.y_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(physical_device, device, resources.z_data_buffer, resources.z_data_memory, resources.z_data_size, grid.z_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(physical_device, device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
                write_buffer(resources.x_data_memory, resources.x_data_size, grid.x_values.data(), grid.x_values.size() * sizeof(float));
                write_buffer(resources.y_data_memory, resources.y_data_size, grid.y_values.data(), grid.y_values.size() * sizeof(float));
                write_buffer(resources.z_data_memory, resources.z_data_size, grid.z_values.data(), grid.z_values.size() * sizeof(float));
                write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
            }

            const vk::raii::Buffer& y_data_buffer = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.y_data_buffer;
            const vk::raii::Buffer& z_data_buffer = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.z_data_buffer;
            const vk::DeviceSize y_data_size      = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.y_data_size;
            const vk::DeviceSize z_data_size      = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.z_data_size;
            const std::array buffer_infos{
                vk::DescriptorBufferInfo{*resources.x_data_buffer, 0, resources.x_data_size},
                vk::DescriptorBufferInfo{*y_data_buffer, 0, y_data_size},
                vk::DescriptorBufferInfo{*z_data_buffer, 0, z_data_size},
                vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
            };
            const vk::DescriptorSet descriptor_set = *this->volume_renderer.descriptor_sets[resource_index];
            const std::array writes{
                vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
                vk::WriteDescriptorSet{descriptor_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1]},
                vk::WriteDescriptorSet{descriptor_set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[2]},
                vk::WriteDescriptorSet{descriptor_set, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[3]},
            };
            device.updateDescriptorSets(writes, {});

            const std::uint32_t volume_vertex_count = render_settings.display_mode == VolumeDisplayMode::direct ? 36u : 6u;
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->volume_renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
            command_buffer.draw(volume_vertex_count, 1, 0, 0);
        }
    }

    void SceneRenderer::render_bounding_boxes(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& view_projection, const Scene& scene) {
        if (!*this->bounding_box_renderer.pipeline_layout || !*this->bounding_box_renderer.pipeline) throw std::runtime_error("Bounding box renderer is not initialized");
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->bounding_box_renderer.pipeline);

        constexpr std::array<float, 4> selected_bounding_box_color{1.0f, 0.76f, 0.30f, 0.96f};
        constexpr std::array<float, 4> volume_bounding_box_color{0.28f, 0.70f, 1.0f, 0.90f};
        constexpr std::array<float, 4> mesh_bounding_box_color{0.72f, 0.54f, 1.0f, 0.90f};
        constexpr std::array<float, 4> particles_bounding_box_color{0.36f, 0.92f, 0.68f, 0.90f};
        for (const Volume& volume : scene.volumes) {
            if (!volume.visible || !volume.render_settings.show_bounding_box) continue;
            const BoundingBoxBounds bounds = volume_bounds(volume);
            BoundingBoxShaderParameters parameters{};
            parameters.model_view_projection = multiply_matrix(view_projection, transform_matrix(volume.transform));
            parameters.bounds_min            = {bounds.minimum[0], bounds.minimum[1], bounds.minimum[2], 1.0f};
            parameters.bounds_max            = {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2], 1.0f};
            parameters.color                 = scene.selection.object_id == volume.id ? selected_bounding_box_color : volume_bounding_box_color;
            command_buffer.pushConstants(*this->bounding_box_renderer.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const BoundingBoxShaderParameters>{1, &parameters});
            command_buffer.draw(24, 1, 0, 0);
        }
        for (const Mesh& mesh : scene.meshes) {
            if (!mesh.visible || !mesh.render_settings.show_bounding_box) continue;
            const BoundingBoxBounds bounds = mesh_bounds(mesh);
            BoundingBoxShaderParameters parameters{};
            parameters.model_view_projection = multiply_matrix(view_projection, transform_matrix(mesh.transform));
            parameters.bounds_min            = {bounds.minimum[0], bounds.minimum[1], bounds.minimum[2], 1.0f};
            parameters.bounds_max            = {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2], 1.0f};
            parameters.color                 = scene.selection.object_id == mesh.id ? selected_bounding_box_color : mesh_bounding_box_color;
            command_buffer.pushConstants(*this->bounding_box_renderer.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const BoundingBoxShaderParameters>{1, &parameters});
            command_buffer.draw(24, 1, 0, 0);
        }
        for (const Particles& particles : scene.particles) {
            if (!particles.visible || !particles.render_settings.show_bounding_box || particles.particles.empty()) continue;
            const BoundingBoxBounds bounds = particles_bounds(particles);
            BoundingBoxShaderParameters parameters{};
            parameters.model_view_projection = multiply_matrix(view_projection, transform_matrix(particles.transform));
            parameters.bounds_min            = {bounds.minimum[0], bounds.minimum[1], bounds.minimum[2], 1.0f};
            parameters.bounds_max            = {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2], 1.0f};
            parameters.color                 = scene.selection.object_id == particles.id ? selected_bounding_box_color : particles_bounding_box_color;
            command_buffer.pushConstants(*this->bounding_box_renderer.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const BoundingBoxShaderParameters>{1, &parameters});
            command_buffer.draw(24, 1, 0, 0);
        }
    }

    void SceneRenderer::create_bounding_box_renderer(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect) {
        if (*this->bounding_box_renderer.pipeline || *this->bounding_box_renderer.pipeline_layout) throw std::runtime_error("Bounding box renderer is already initialized");
        if (!*device) throw std::runtime_error("Cannot create bounding box renderer without a Vulkan device");
        if (color_format == vk::Format::eUndefined || depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create bounding box renderer without swapchain formats");

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(BoundingBoxShaderParameters)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->bounding_box_renderer.pipeline_layout = vk::raii::PipelineLayout{device, pipeline_layout_create_info};

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable         = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format stencil_format = static_cast<bool>(depth_aspect & vk::ImageAspectFlagBits::eStencil) ? depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->bounding_box_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->bounding_box_renderer.pipeline     = vk::raii::Pipeline{device, nullptr, pipeline_create_info};
    }

    void SceneRenderer::destroy_bounding_box_renderer() noexcept {
        this->bounding_box_renderer.pipeline        = nullptr;
        this->bounding_box_renderer.pipeline_layout = nullptr;
    }

    void SceneRenderer::create_mesh_renderer(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect, const std::uint32_t frame_count, const Scene& scene) {
        if (*this->mesh_renderer.surface_pipeline || *this->mesh_renderer.wireframe_pipeline || this->mesh_renderer.descriptor_sets.size() != 0 || !this->mesh_renderer.frame_resources.empty()) throw std::runtime_error("Mesh renderer is already initialized");
        if (!*device) throw std::runtime_error("Cannot create mesh renderer without a Vulkan device");
        if (color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a color format");
        if (depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a depth format");
        if (frame_count == 0) throw std::runtime_error("Cannot create mesh renderer without frames in flight");
        if (scene.meshes.empty()) throw std::runtime_error("Cannot create mesh renderer for a scene without meshes");
        if (scene.meshes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / frame_count)) throw std::runtime_error("Scene has too many meshes for frame resources");

        const std::uint32_t descriptor_set_count = static_cast<std::uint32_t>(scene.meshes.size() * frame_count);
        this->mesh_renderer.frame_resources.resize(descriptor_set_count);
        if (descriptor_set_count == 0) throw std::runtime_error("Mesh descriptor set count must not be zero");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->mesh_renderer.descriptor_layout = vk::raii::DescriptorSetLayout{device, descriptor_layout_create_info};

        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, descriptor_set_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, descriptor_set_count, 1, &pool_size};
        this->mesh_renderer.descriptor_pool = vk::raii::DescriptorPool{device, descriptor_pool_create_info};

        std::vector layouts(descriptor_set_count, *this->mesh_renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->mesh_renderer.descriptor_pool, descriptor_set_count, layouts.data()};
        this->mesh_renderer.descriptor_sets = vk::raii::DescriptorSets{device, allocate_info};
        if (this->mesh_renderer.descriptor_sets.size() != descriptor_set_count) throw std::runtime_error("Failed to allocate mesh descriptor sets");

        const vk::DescriptorSetLayout descriptor_layout = *this->mesh_renderer.descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout};
        this->mesh_renderer.pipeline_layout = vk::raii::PipelineLayout{device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(MeshShaderVertex), vk::VertexInputRate::eVertex};
        constexpr std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>))},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>) * 2)},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format stencil_format = static_cast<bool>(depth_aspect & vk::ImageAspectFlagBits::eStencil) ? depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->mesh_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->mesh_renderer.surface_pipeline     = vk::raii::Pipeline{device, nullptr, pipeline_create_info};
        input_assembly_state.topology            = vk::PrimitiveTopology::eLineList;
        this->mesh_renderer.wireframe_pipeline   = vk::raii::Pipeline{device, nullptr, pipeline_create_info};
    }

    void SceneRenderer::destroy_mesh_renderer() noexcept {
        this->mesh_renderer.wireframe_pipeline = nullptr;
        this->mesh_renderer.surface_pipeline   = nullptr;
        this->mesh_renderer.pipeline_layout    = nullptr;
        this->mesh_renderer.descriptor_sets    = nullptr;
        this->mesh_renderer.descriptor_pool    = nullptr;
        this->mesh_renderer.descriptor_layout  = nullptr;
        this->mesh_renderer.frame_resources.clear();
    }

    void SceneRenderer::create_particles_renderer(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect, const std::uint32_t frame_count, const Scene& scene) {
        if (*this->particles_renderer.pipeline || !this->particles_renderer.frame_resources.empty()) throw std::runtime_error("Particles renderer is already initialized");
        if (!*device) throw std::runtime_error("Cannot create particles renderer without a Vulkan device");
        if (color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a color format");
        if (depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a depth format");
        if (frame_count == 0) throw std::runtime_error("Cannot create particles renderer without frames in flight");
        if (scene.particles.empty()) throw std::runtime_error("Cannot create particles renderer for a scene without particles");
        if (scene.particles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / frame_count)) throw std::runtime_error("Scene has too many particles objects for frame resources");

        const std::uint32_t resource_count = static_cast<std::uint32_t>(scene.particles.size() * frame_count);
        this->particles_renderer.frame_resources.resize(resource_count);

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(ParticleShaderParameters)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->particles_renderer.pipeline_layout = vk::raii::PipelineLayout{device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(ParticleShaderVertex), vk::VertexInputRate::eVertex};
        constexpr std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>))},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 4>) * 2)},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format stencil_format = static_cast<bool>(depth_aspect & vk::ImageAspectFlagBits::eStencil) ? depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->particles_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->particles_renderer.pipeline        = vk::raii::Pipeline{device, nullptr, pipeline_create_info};
    }

    void SceneRenderer::destroy_particles_renderer() noexcept {
        this->particles_renderer.pipeline        = nullptr;
        this->particles_renderer.pipeline_layout = nullptr;
        this->particles_renderer.frame_resources.clear();
    }

    void SceneRenderer::create_volume_renderer(const vk::raii::Device& device, const vk::Format color_format, const vk::Format depth_format, const vk::ImageAspectFlags depth_aspect, const std::uint32_t frame_count, const Scene& scene) {
        if (*this->volume_renderer.pipeline || this->volume_renderer.descriptor_sets.size() != 0 || !this->volume_renderer.frame_resources.empty()) throw std::runtime_error("Volume renderer is already initialized");
        if (!*device) throw std::runtime_error("Cannot create volume renderer without a Vulkan device");
        if (color_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a color format");
        if (depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a depth format");
        if (frame_count == 0) throw std::runtime_error("Cannot create volume renderer without frames in flight");
        if (scene.volumes.empty()) throw std::runtime_error("Cannot create volume renderer for a scene without volumes");
        if (scene.volumes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / frame_count)) throw std::runtime_error("Scene has too many volumes for frame resources");

        const std::uint32_t descriptor_set_count = static_cast<std::uint32_t>(scene.volumes.size() * frame_count);
        this->volume_renderer.frame_resources.resize(descriptor_set_count);
        if (descriptor_set_count > std::numeric_limits<std::uint32_t>::max() / 4) throw std::runtime_error("Volume descriptor pool size is too large");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->volume_renderer.descriptor_layout = vk::raii::DescriptorSetLayout{device, descriptor_layout_create_info};

        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, descriptor_set_count * 4};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, descriptor_set_count, 1, &pool_size};
        this->volume_renderer.descriptor_pool = vk::raii::DescriptorPool{device, descriptor_pool_create_info};

        std::vector layouts(descriptor_set_count, *this->volume_renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->volume_renderer.descriptor_pool, descriptor_set_count, layouts.data()};
        this->volume_renderer.descriptor_sets = vk::raii::DescriptorSets{device, allocate_info};
        if (this->volume_renderer.descriptor_sets.size() != descriptor_set_count) throw std::runtime_error("Failed to allocate volume descriptor sets");

        const vk::DescriptorSetLayout descriptor_layout = *this->volume_renderer.descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout};
        this->volume_renderer.pipeline_layout = vk::raii::PipelineLayout{device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable         = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format stencil_format = static_cast<bool>(depth_aspect & vk::ImageAspectFlagBits::eStencil) ? depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->volume_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->volume_renderer.pipeline           = vk::raii::Pipeline{device, nullptr, pipeline_create_info};
    }

    void SceneRenderer::destroy_volume_renderer() noexcept {
        this->volume_renderer.pipeline          = nullptr;
        this->volume_renderer.pipeline_layout   = nullptr;
        this->volume_renderer.descriptor_sets   = nullptr;
        this->volume_renderer.descriptor_pool   = nullptr;
        this->volume_renderer.descriptor_layout = nullptr;
        this->volume_renderer.frame_resources.clear();
    }
} // namespace xayah
