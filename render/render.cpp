module;

#include <exr.h>

module spectra.render;

import std;

namespace spectra::render {
    std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        if (!input) throw std::runtime_error(std::format("Cannot open Spectra shader: {}", path.string()));
        const std::streamsize byte_count = input.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::format("Spectra shader has an invalid SPIR-V size: {}", path.string()));
        std::vector<std::uint32_t> words(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), byte_count);
        if (!input) throw std::runtime_error(std::format("Cannot read Spectra shader: {}", path.string()));
        return words;
    }

    namespace {
        [[nodiscard]] std::string texture_cache_key(const scene::Texture& texture) {
            const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
            const std::string identity       = image.asset.content_hash.empty() ? std::format("memory:{}:{}:{}", texture.id.value, texture.revision.content, texture.revision.topology) : image.asset.content_hash;
            return std::format("{}:{}:{}:{}", identity, std::to_underlying(image.wrap), std::to_underlying(image.filter), std::bit_cast<std::uint32_t>(image.maximum_anisotropy));
        }

    } // namespace

    GpuTextureImage upload_texture_image(Spectra& runtime, const scene::ImageTexture& data, const vk::Format format, const vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer* command_buffer) {
        GpuTextureImage result{runtime.create_image_2d({data.width, data.height}, format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::ImageAspectFlagBits::eColor, static_cast<std::uint32_t>(data.mip_offsets.size())), runtime.allocate_resource_descriptor(), runtime.allocate_sampler_descriptor()};
        runtime.write_sampled_image(result.image_descriptor, result.image, vk::ImageLayout::eShaderReadOnlyOptimal);
        const vk::SamplerAddressMode address_mode = data.wrap == scene::TextureWrapMode::Repeat ? vk::SamplerAddressMode::eRepeat : data.wrap == scene::TextureWrapMode::Clamp ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eClampToBorder;
        const bool linear                         = data.filter != scene::TextureFilter::Point;
        runtime.write_sampler(result.sampler_descriptor, vk::SamplerCreateInfo{{}, linear ? vk::Filter::eLinear : vk::Filter::eNearest, linear ? vk::Filter::eLinear : vk::Filter::eNearest, data.filter == scene::TextureFilter::Trilinear || data.filter == scene::TextureFilter::Ewa ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest, address_mode, address_mode, address_mode, 0.0f, data.filter == scene::TextureFilter::Ewa ? vk::True : vk::False, data.maximum_anisotropy, vk::False, vk::CompareOp::eNever, 0.0f, static_cast<float>(data.mip_offsets.size() - 1u), vk::BorderColor::eFloatTransparentBlack});
        vk::DeviceSize texel_size{};
        GpuBuffer staging{};
        if (format == vk::Format::eR16G16B16A16Sfloat) {
            std::vector<std::uint16_t> texels(data.texels.size() * 4u);
            constexpr std::size_t conversion_block_size = 4096;
            std::vector<float> components(std::min(conversion_block_size, data.texels.size()) * 4u);
            for (std::size_t first = 0; first != data.texels.size(); first += conversion_block_size) {
                const std::size_t count = std::min(conversion_block_size, data.texels.size() - first);
                for (std::size_t index = 0; index != count; ++index) {
                    components[index * 4u]      = data.texels[first + index].x;
                    components[index * 4u + 1u] = data.texels[first + index].y;
                    components[index * 4u + 2u] = data.texels[first + index].z;
                    components[index * 4u + 3u] = data.texels[first + index].w;
                }
                exr_float_to_half(components.data(), texels.data() + first * 4u, count * 4u);
            }
            texel_size = 4u * sizeof(std::uint16_t);
            staging    = runtime.create_buffer(texels.size() * sizeof(std::uint16_t), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, texels.data(), texels.size() * sizeof(std::uint16_t));
        } else if (format == vk::Format::eR32G32B32A32Sfloat) {
            texel_size = sizeof(scene::Float4);
            staging    = runtime.create_buffer(data.texels.size() * texel_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, data.texels.data(), data.texels.size() * texel_size);
        } else
            throw std::runtime_error("GPU Texture Image format must be RGBA16F or RGBA32F");
        std::vector<vk::BufferImageCopy> copies{};
        copies.reserve(data.mip_offsets.size());
        std::uint32_t mip_width  = data.width;
        std::uint32_t mip_height = data.height;
        for (std::uint32_t level = 0; level != data.mip_offsets.size(); ++level) {
            copies.push_back(vk::BufferImageCopy{data.mip_offsets[level] * texel_size, 0, 0, {vk::ImageAspectFlagBits::eColor, level, 0, 1}, {0, 0, 0}, {mip_width, mip_height, 1}});
            mip_width  = std::max(1u, mip_width / 2u);
            mip_height = std::max(1u, mip_height / 2u);
        }
        const auto record = [&result, &staging, &copies, destination_stages](const vk::raii::CommandBuffer& commands) {
            const vk::ImageMemoryBarrier2 to_transfer{vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *result.image.image, {vk::ImageAspectFlagBits::eColor, 0, result.image.mip_levels, 0, 1}};
            commands.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_transfer});
            commands.copyBufferToImage(*staging.buffer, *result.image.image, vk::ImageLayout::eTransferDstOptimal, copies);
            const vk::ImageMemoryBarrier2 to_shader{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, destination_stages, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *result.image.image, {vk::ImageAspectFlagBits::eColor, 0, result.image.mip_levels, 0, 1}};
            commands.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_shader});
        };
        if (command_buffer) {
            record(*command_buffer);
            runtime.defer([upload = std::move(staging)]() mutable {});
        } else
            runtime.immediate(record);
        return result;
    }

    namespace {
        [[nodiscard]] vk::AccelerationStructureGeometryKHR triangle_geometry(const GpuGeometry& mesh) {
            const vk::AccelerationStructureGeometryTrianglesDataKHR triangles{
                vk::Format::eR32G32B32Sfloat,
                vk::DeviceOrHostAddressConstKHR{mesh.positions.address},
                sizeof(scene::Float3),
                mesh.vertex_count - 1u,
                vk::IndexType::eUint32,
                vk::DeviceOrHostAddressConstKHR{mesh.indices.address},
                vk::DeviceOrHostAddressConstKHR{},
            };
            return vk::AccelerationStructureGeometryKHR{
                vk::GeometryTypeKHR::eTriangles,
                vk::AccelerationStructureGeometryDataKHR{triangles},
                {},
            };
        }

        [[nodiscard]] vk::AccelerationStructureGeometryKHR procedural_geometry(const GpuGeometry& mesh) {
            const vk::AccelerationStructureGeometryAabbsDataKHR aabbs{vk::DeviceOrHostAddressConstKHR{mesh.aabbs.address}, sizeof(vk::AabbPositionsKHR)};
            return {
                vk::GeometryTypeKHR::eAabbs,
                vk::AccelerationStructureGeometryDataKHR{aabbs},
                {},
            };
        }

        [[nodiscard]] scene::TriangleMeshGeometry tessellate_geometry(const scene::Geometry& geometry) {
            if (const scene::TriangleMeshGeometry* mesh = std::get_if<scene::TriangleMeshGeometry>(&geometry.data)) return *mesh;
            scene::TriangleMeshGeometry result{};
            const auto vertex = [&result](const scene::Float3 position, const scene::Float3 normal, const scene::Float3 tangent, const scene::Float2 uv) {
                result.positions.push_back(position);
                result.normals.push_back(normal);
                result.tangents.push_back(tangent);
                result.texture_coordinates.push_back(uv);
                return static_cast<std::uint32_t>(result.positions.size() - 1u);
            };
            if (const scene::BoxGeometry* box = std::get_if<scene::BoxGeometry>(&geometry.data)) {
                const scene::Float3 minimum = box->bounds.minimum;
                const scene::Float3 maximum = box->bounds.maximum;
                const std::array positions{std::array{scene::Float3{minimum.x, minimum.y, minimum.z}, scene::Float3{maximum.x, minimum.y, minimum.z}, scene::Float3{maximum.x, maximum.y, minimum.z}, scene::Float3{minimum.x, maximum.y, minimum.z}}, std::array{scene::Float3{minimum.x, minimum.y, maximum.z}, scene::Float3{minimum.x, maximum.y, maximum.z}, scene::Float3{maximum.x, maximum.y, maximum.z}, scene::Float3{maximum.x, minimum.y, maximum.z}}, std::array{scene::Float3{minimum.x, minimum.y, minimum.z}, scene::Float3{minimum.x, minimum.y, maximum.z}, scene::Float3{maximum.x, minimum.y, maximum.z}, scene::Float3{maximum.x, minimum.y, minimum.z}}, std::array{scene::Float3{minimum.x, maximum.y, minimum.z}, scene::Float3{maximum.x, maximum.y, minimum.z}, scene::Float3{maximum.x, maximum.y, maximum.z}, scene::Float3{minimum.x, maximum.y, maximum.z}},
                    std::array{scene::Float3{minimum.x, minimum.y, minimum.z}, scene::Float3{minimum.x, maximum.y, minimum.z}, scene::Float3{minimum.x, maximum.y, maximum.z}, scene::Float3{minimum.x, minimum.y, maximum.z}}, std::array{scene::Float3{maximum.x, minimum.y, minimum.z}, scene::Float3{maximum.x, minimum.y, maximum.z}, scene::Float3{maximum.x, maximum.y, maximum.z}, scene::Float3{maximum.x, maximum.y, minimum.z}}};
                const std::array normals{scene::Float3{0.0f, 0.0f, -1.0f}, scene::Float3{0.0f, 0.0f, 1.0f}, scene::Float3{0.0f, -1.0f, 0.0f}, scene::Float3{0.0f, 1.0f, 0.0f}, scene::Float3{-1.0f, 0.0f, 0.0f}, scene::Float3{1.0f, 0.0f, 0.0f}};
                const std::array tangents{scene::Float3{1.0f, 0.0f, 0.0f}, scene::Float3{-1.0f, 0.0f, 0.0f}, scene::Float3{1.0f, 0.0f, 0.0f}, scene::Float3{1.0f, 0.0f, 0.0f}, scene::Float3{0.0f, 1.0f, 0.0f}, scene::Float3{0.0f, -1.0f, 0.0f}};
                constexpr std::array texture_coordinates{scene::Float2{0.0f, 0.0f}, scene::Float2{1.0f, 0.0f}, scene::Float2{1.0f, 1.0f}, scene::Float2{0.0f, 1.0f}};
                for (std::uint32_t face = 0; face != 6; ++face) {
                    const std::uint32_t first = static_cast<std::uint32_t>(result.positions.size());
                    for (std::uint32_t corner = 0; corner != 4; ++corner) vertex(positions[face][corner], normals[face], tangents[face], texture_coordinates[corner]);
                    result.indices.insert(result.indices.end(), {first, first + 2u, first + 1u, first, first + 3u, first + 2u});
                }
                return result;
            }
            if (const scene::RectangleGeometry* rectangle = std::get_if<scene::RectangleGeometry>(&geometry.data)) {
                vertex({rectangle->minimum.x, rectangle->minimum.y, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f});
                vertex({rectangle->maximum.x, rectangle->minimum.y, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f});
                vertex({rectangle->maximum.x, rectangle->maximum.y, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f});
                vertex({rectangle->minimum.x, rectangle->maximum.y, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f});
                result.indices = {0, 1, 2, 0, 2, 3};
                return result;
            }
            constexpr std::uint32_t segments = 64;
            if (const scene::SphereGeometry* sphere = std::get_if<scene::SphereGeometry>(&geometry.data)) {
                constexpr std::uint32_t rings = 32;
                const float phi_max           = sphere->phi_max * std::numbers::pi_v<float> / 180.0f;
                for (std::uint32_t ring = 0; ring <= rings; ++ring) {
                    const float v      = static_cast<float>(ring) / rings;
                    const float z      = std::lerp(sphere->z_min, sphere->z_max, v);
                    const float radial = std::sqrt(std::max(0.0f, sphere->radius * sphere->radius - z * z));
                    for (std::uint32_t segment = 0; segment <= segments; ++segment) {
                        const float u   = static_cast<float>(segment) / segments;
                        const float phi = phi_max * u;
                        const scene::Float3 position{radial * std::cos(phi), radial * std::sin(phi), z};
                        vertex(position, {position.x / sphere->radius, position.y / sphere->radius, position.z / sphere->radius}, {-std::sin(phi), std::cos(phi), 0.0f}, {u, v});
                    }
                }
                for (std::uint32_t ring = 0; ring != rings; ++ring)
                    for (std::uint32_t segment = 0; segment != segments; ++segment) {
                        const std::uint32_t first  = ring * (segments + 1u) + segment;
                        const std::uint32_t second = first + segments + 1u;
                        result.indices.insert(result.indices.end(), {first, second, second + 1u, first, second + 1u, first + 1u});
                    }
                return result;
            }
            if (const scene::DiskGeometry* disk = std::get_if<scene::DiskGeometry>(&geometry.data)) {
                const float phi_max = disk->phi_max * std::numbers::pi_v<float> / 180.0f;
                for (std::uint32_t segment = 0; segment <= segments; ++segment) {
                    const float u   = static_cast<float>(segment) / segments;
                    const float phi = phi_max * u;
                    for (const float radius : {disk->inner_radius, disk->radius}) vertex({radius * std::cos(phi), radius * std::sin(phi), disk->height}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {u, (disk->radius - radius) / (disk->radius - disk->inner_radius)});
                }
                for (std::uint32_t segment = 0; segment != segments; ++segment) {
                    const std::uint32_t first = segment * 2u;
                    result.indices.insert(result.indices.end(), {first, first + 1u, first + 3u});
                    if (disk->inner_radius != 0.0f) result.indices.insert(result.indices.end(), {first, first + 3u, first + 2u});
                }
                return result;
            }
            const scene::CylinderGeometry& cylinder = std::get<scene::CylinderGeometry>(geometry.data);
            const float phi_max                     = cylinder.phi_max * std::numbers::pi_v<float> / 180.0f;
            for (std::uint32_t segment = 0; segment <= segments; ++segment) {
                const float u   = static_cast<float>(segment) / segments;
                const float phi = phi_max * u;
                const scene::Float3 normal{std::cos(phi), std::sin(phi), 0.0f};
                for (std::uint32_t end = 0; end != 2; ++end) vertex({cylinder.radius * normal.x, cylinder.radius * normal.y, end == 0 ? cylinder.z_min : cylinder.z_max}, normal, {-normal.y, normal.x, 0.0f}, {u, static_cast<float>(end)});
            }
            for (std::uint32_t segment = 0; segment != segments; ++segment) {
                const std::uint32_t first = segment * 2u;
                result.indices.insert(result.indices.end(), {first, first + 1u, first + 3u, first, first + 3u, first + 2u});
            }
            return result;
        }

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(Spectra& runtime, const std::span<const Element> elements, const vk::BufferUsageFlags usage, const std::size_t element_capacity = 0) {
            GpuBuffer staging = runtime.create_buffer(elements.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, elements.data(), elements.size_bytes());
            GpuBuffer destination = runtime.create_buffer(std::max(elements.size(), element_capacity) * sizeof(Element), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            runtime.immediate([&staging, &destination, usage](const vk::raii::CommandBuffer& command_buffer) {
                command_buffer.copyBuffer(*staging.buffer, *destination.buffer, vk::BufferCopy{0, 0, staging.size});
                const bool acceleration_structure = static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
                const vk::BufferMemoryBarrier2 dependency{
                    vk::PipelineStageFlagBits2::eCopy,
                    vk::AccessFlagBits2::eTransferWrite,
                    acceleration_structure ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlagBits2::eAllCommands,
                    acceleration_structure ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlagBits2::eShaderStorageRead,
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    *destination.buffer,
                    0,
                    destination.size,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &dependency});
            });
            return destination;
        }

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(Spectra& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage, const std::size_t element_capacity = 0) {
            GpuBuffer destination       = runtime.create_buffer(std::max(elements.size(), element_capacity) * sizeof(Element), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload = runtime.stage_upload(std::as_bytes(elements));
            command_buffer.copyBuffer(upload.buffer, *destination.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const bool acceleration_structure = static_cast<bool>(usage & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
            const vk::BufferMemoryBarrier2 dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                acceleration_structure ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlagBits2::eAllCommands,
                acceleration_structure ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *destination.buffer,
                0,
                destination.size,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 1, &dependency});
            return destination;
        }
    } // namespace

    GpuScene::GpuScene(Spectra& runtime, const scene::Scene& source, const std::filesystem::path& shader_directory, const std::span<const GpuGeometryBinding> geometry_bindings, const std::span<const std::pair<scene::ParticleSetId, std::uint32_t>> particle_capacities, const std::span<const scene::InstanceId> hidden_instances) : state(source), runtime(&runtime), geometry_bindings(geometry_bindings.begin(), geometry_bindings.end()) {
        this->instance_placements.reserve(source.resources.instances.size());
        for (const scene::Instance& instance : source.resources.instances) this->instance_placements.emplace_back(instance.id, instance.transform);
        for (scene::Instance& instance : this->state.resources.instances)
            if (std::ranges::contains(hidden_instances, instance.id)) instance.visible = false;
        const scene::SceneView scene = this->state.view();
        const auto create_shader     = [&runtime, &shader_directory](const std::string_view file, const char* entry) {
            const std::vector<std::uint32_t> code = load_spirv(shader_directory / file);
            return vk::raii::ShaderEXT{runtime.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), entry}};
        };
        this->dynamic_mesh_clear_shader      = create_shader("dynamic_mesh_clear.spv", "clear_dynamic_attributes");
        this->dynamic_mesh_accumulate_shader = create_shader("dynamic_mesh_accumulate.spv", "accumulate_dynamic_attributes");
        this->dynamic_mesh_normalize_shader  = create_shader("dynamic_mesh_normalize.spv", "normalize_dynamic_attributes");
        this->particle_material_shader       = create_shader("particle_material.spv", "map_particle_materials");
        std::vector<std::array<std::uint32_t, 4>> material_lookup{};
        material_lookup.reserve(std::max<std::size_t>(scene.resources.materials.size(), 1));
        for (std::uint32_t index = 0; index != scene.resources.materials.size(); ++index) {
            const std::uint64_t id = scene.resources.materials[index].id.value;
            material_lookup.push_back({
                static_cast<std::uint32_t>(id),
                static_cast<std::uint32_t>(id >> 32),
                index,
                0,
            });
        }
        if (material_lookup.empty()) material_lookup.emplace_back();
        this->material_count             = static_cast<std::uint32_t>(scene.resources.materials.size());
        this->material_lookup            = upload_buffer(runtime, std::span<const std::array<std::uint32_t, 4>>{material_lookup}, vk::BufferUsageFlagBits::eStorageBuffer);
        this->material_lookup_descriptor = runtime.allocate_resource_descriptor();
        runtime.write_buffer(this->material_lookup_descriptor, vk::DescriptorType::eStorageBuffer, this->material_lookup);
        this->cache_texture_images(scene);
        this->geometries.reserve(scene.resources.geometries.size());
        for (const scene::Geometry& geometry : scene.resources.geometries) this->geometries.emplace_back(this->create_geometry(geometry));
        this->particle_sets.reserve(scene.resources.particle_sets.size());
        for (const scene::ParticleSet& particles : scene.resources.particle_sets) {
            const auto capacity = std::ranges::find(particle_capacities, particles.id, &std::pair<scene::ParticleSetId, std::uint32_t>::first);
            this->particle_sets.emplace_back(this->create_particle_set(particles, scene, nullptr, capacity == particle_capacities.end() ? 0 : capacity->second));
        }
        this->volumes.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) this->volumes.emplace_back(this->create_volume(volume));

        const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
        const std::array<vk::AccelerationStructureInstanceKHR, 1> empty_instance_storage{};
        this->acceleration_structure_instances = upload_buffer(runtime, instances.empty() ? std::span<const vk::AccelerationStructureInstanceKHR>{empty_instance_storage} : std::span<const vk::AccelerationStructureInstanceKHR>{instances}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        this->top_level_acceleration_structure = this->build_top_level(instances);
        this->uploaded_revision                = scene.revision;
    }

    GpuScene::~GpuScene() {
        for (const HostVolumeVector& field : this->host_volume_vectors) this->runtime->release_resource_descriptor(field.descriptor);
        this->runtime->release_resource_descriptor(this->material_lookup_descriptor);
        for (const GpuTextureImage& image : this->texture_images) {
            this->runtime->release_resource_descriptor(image.image_descriptor);
            this->runtime->release_sampler_descriptor(image.sampler_descriptor);
        }
        for (const GpuGeometry& mesh : this->geometries) {
            this->runtime->release_resource_descriptor(mesh.positions_descriptor);
            this->runtime->release_resource_descriptor(mesh.normals_descriptor);
            this->runtime->release_resource_descriptor(mesh.tangents_descriptor);
            this->runtime->release_resource_descriptor(mesh.texture_coordinates_descriptor);
            this->runtime->release_resource_descriptor(mesh.indices_descriptor);
        }
        for (const GpuParticleSet& particles : this->particle_sets) {
            this->runtime->release_resource_descriptor(particles.positions_descriptor);
            this->runtime->release_resource_descriptor(particles.radii_descriptor);
            this->runtime->release_resource_descriptor(particles.velocities_descriptor);
            this->runtime->release_resource_descriptor(particles.colors_descriptor);
            this->runtime->release_resource_descriptor(particles.temperatures_descriptor);
            this->runtime->release_resource_descriptor(particles.materials_descriptor);
        }
        for (const GpuVolume& volume : this->volumes)
            for (std::size_t field = 0; field != volume.fields.size(); ++field)
                if (volume.present[field]) this->runtime->release_resource_descriptor(volume.descriptors[field]);
    }

    void GpuScene::cache_texture_images(const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer) {
        for (const scene::Texture& texture : scene.resources.textures) {
            const scene::ImageTexture* image = std::get_if<scene::ImageTexture>(&texture.data);
            if (!image) continue;
            const vk::Format format = texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
            const std::pair key{texture_cache_key(texture), format};
            if (this->texture_image_indices.contains(key)) continue;
            const std::size_t index = this->texture_images.size();
            this->texture_images.emplace_back(upload_texture_image(*this->runtime, *image, format, vk::PipelineStageFlagBits2::eAllCommands, command_buffer));
            this->texture_image_indices.emplace(key, index);
        }
    }

    const GpuTextureImage& GpuScene::texture_image(const scene::Texture& texture, const vk::Format format) const {
        return this->texture_images[this->texture_image_indices.at({texture_cache_key(texture), format})];
    }

    FrozenScene GpuScene::record_frozen_scene(const vk::raii::CommandBuffer& command_buffer, const scene::CameraResource& camera, const vk::Extent2D extent, const float exposure) const {
        FrozenScene result{};
        result.scene = this->state;
        result.scene.dynamic_setup.reset();
        result.revision                       = this->state.revision().value;
        scene::CameraResource snapshot_camera = camera;
        snapshot_camera.id                    = {std::ranges::fold_left(result.scene.resources.cameras, std::uint64_t{}, [](const std::uint64_t maximum, const scene::CameraResource& value) { return std::max(maximum, value.id.value); }) + 1};
        snapshot_camera.name                  = "Snapshot Camera";
        snapshot_camera.revision              = {};
        result.scene.resources.cameras.push_back(snapshot_camera);
        result.scene.active_camera  = snapshot_camera.id;
        scene::Film snapshot_film   = result.scene.film();
        snapshot_film.id            = {std::ranges::fold_left(result.scene.resources.films, std::uint64_t{}, [](const std::uint64_t maximum, const scene::Film& value) { return std::max(maximum, value.id.value); }) + 1};
        snapshot_film.name          = "Snapshot Film";
        snapshot_film.revision      = {};
        snapshot_film.resolution    = {extent.width, extent.height};
        snapshot_film.pixel_minimum = {};
        snapshot_film.pixel_maximum = snapshot_film.resolution;
        snapshot_film.exposure += exposure;
        result.scene.resources.films.push_back(snapshot_film);
        result.scene.active_film = snapshot_film.id;

        vk::DeviceSize size{};
        struct SourceCopy {
            const GpuBuffer* source{};
            vk::BufferCopy region{};
        };
        std::vector<SourceCopy> sources{};
        for (const GpuGeometry& geometry : this->geometries) {
            if (!geometry.gpu_modified) continue;
            const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(this->state.resources.geometries, geometry.id, &scene::Geometry::id) - this->state.resources.geometries.begin());
            const auto add               = [&result, &sources, &size, resource](const FrozenDataKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                if (count == 0) return;
                size                       = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes = count * element_size;
                result.copies.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                sources.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            add(FrozenDataKind::GeometryPosition, geometry.positions, geometry.vertex_count, sizeof(scene::Float3));
            if ((geometry.attribute_flags & 1u) != 0) add(FrozenDataKind::GeometryNormal, geometry.normals, geometry.vertex_count, sizeof(scene::Float3));
            if ((geometry.attribute_flags & 2u) != 0) add(FrozenDataKind::GeometryTangent, geometry.tangents, geometry.vertex_count, sizeof(scene::Float3));
            if ((geometry.attribute_flags & 4u) != 0) add(FrozenDataKind::GeometryTextureCoordinate, geometry.texture_coordinates, geometry.vertex_count, sizeof(scene::Float2));
            add(FrozenDataKind::GeometryIndex, geometry.indices, geometry.index_count, sizeof(std::uint32_t));
        }
        for (const GpuParticleSet& particles : this->particle_sets) {
            if (!particles.gpu_modified) continue;
            const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(this->state.resources.particle_sets, particles.id, &scene::ParticleSet::id) - this->state.resources.particle_sets.begin());
            const auto add               = [&result, &sources, &size, resource](const FrozenDataKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                if (count == 0) return;
                size                       = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes = count * element_size;
                result.copies.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                sources.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            add(FrozenDataKind::ParticlePosition, particles.positions, particles.particle_count, sizeof(scene::Float3));
            add(FrozenDataKind::ParticleRadius, particles.radii, particles.particle_count, sizeof(float));
            if ((particles.attribute_flags & 1u) != 0) add(FrozenDataKind::ParticleVelocity, particles.velocities, particles.particle_count, sizeof(scene::Float3));
            if ((particles.attribute_flags & 2u) != 0) add(FrozenDataKind::ParticleColor, particles.colors, particles.particle_count, sizeof(scene::Float3));
            if ((particles.attribute_flags & 4u) != 0) add(FrozenDataKind::ParticleTemperature, particles.temperatures, particles.particle_count, sizeof(float));
            if ((particles.attribute_flags & 8u) != 0) add(FrozenDataKind::ParticleMaterial, particles.materials, particles.particle_count, sizeof(std::uint32_t));
        }
        for (const GpuVolume& volume : this->volumes) {
            if (!volume.gpu_modified) continue;
            const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(this->state.resources.volumes, volume.id, &scene::Volume::id) - this->state.resources.volumes.begin());
            for (std::size_t field = 0; field != volume.fields.size(); ++field) {
                if (!volume.present[field]) continue;
                const GpuVolumeField kind         = static_cast<GpuVolumeField>(field);
                const vk::DeviceSize element_size = kind == GpuVolumeField::SigmaA || kind == GpuVolumeField::SigmaS || kind == GpuVolumeField::Emission ? sizeof(scene::Float3) : kind == GpuVolumeField::NanoVdbDensity || kind == GpuVolumeField::NanoVdbTemperature ? sizeof(std::uint32_t) : sizeof(float);
                const std::uint64_t count         = volume.fields[field].size / element_size;
                size                              = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes        = count * element_size;
                result.copies.emplace_back(FrozenDataKind::VolumeField, resource, kind, size, count);
                sources.emplace_back(&volume.fields[field], vk::BufferCopy{0, size, bytes});
                size += bytes;
            }
        }
        if (size != 0) {
            result.readback = this->runtime->create_buffer(size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            for (const SourceCopy& copy : sources) command_buffer.copyBuffer(*copy.source->buffer, *result.readback.buffer, copy.region);
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        }
        result.scene.mark_all_changed();
        return result;
    }

    void FrozenScene::materialize() {
        const auto copy = [this]<class Element>(std::vector<Element>& destination, const FrozenDataCopy& source) {
            destination.resize(source.count);
            std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback.mapped) + source.offset, source.count * sizeof(Element));
        };
        for (const FrozenDataCopy& source : this->copies) switch (source.kind) {
            case FrozenDataKind::GeometryPosition: copy(std::get<scene::TriangleMeshGeometry>(this->scene.resources.geometries[source.resource].data).positions, source); break;
            case FrozenDataKind::GeometryNormal: copy(std::get<scene::TriangleMeshGeometry>(this->scene.resources.geometries[source.resource].data).normals, source); break;
            case FrozenDataKind::GeometryTangent: copy(std::get<scene::TriangleMeshGeometry>(this->scene.resources.geometries[source.resource].data).tangents, source); break;
            case FrozenDataKind::GeometryTextureCoordinate: copy(std::get<scene::TriangleMeshGeometry>(this->scene.resources.geometries[source.resource].data).texture_coordinates, source); break;
            case FrozenDataKind::GeometryIndex: copy(std::get<scene::TriangleMeshGeometry>(this->scene.resources.geometries[source.resource].data).indices, source); break;
            case FrozenDataKind::ParticlePosition: copy(this->scene.resources.particle_sets[source.resource].positions, source); break;
            case FrozenDataKind::ParticleRadius: copy(this->scene.resources.particle_sets[source.resource].radii, source); break;
            case FrozenDataKind::ParticleVelocity: copy(this->scene.resources.particle_sets[source.resource].velocities, source); break;
            case FrozenDataKind::ParticleColor: copy(this->scene.resources.particle_sets[source.resource].colors, source); break;
            case FrozenDataKind::ParticleTemperature: copy(this->scene.resources.particle_sets[source.resource].temperatures, source); break;
            case FrozenDataKind::ParticleMaterial:
                {
                    std::vector<std::uint32_t> material_indices{};
                    copy(material_indices, source);
                    std::vector<scene::MaterialId>& materials = this->scene.resources.particle_sets[source.resource].particle_materials;
                    materials.clear();
                    materials.reserve(material_indices.size());
                    for (const std::uint32_t index : material_indices) materials.push_back(this->scene.resources.materials[index].id);
                }
                break;
            case FrozenDataKind::VolumeField:
                {
                    scene::Volume& volume = this->scene.resources.volumes[source.resource];
                    std::visit(
                        [this, &copy, &source](auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                                if (source.volume_field == GpuVolumeField::Density)
                                    copy(data.density, source);
                                else if (source.volume_field == GpuVolumeField::Temperature)
                                    copy(data.temperature, source);
                                else if (source.volume_field == GpuVolumeField::EmissionScale)
                                    copy(data.emission_scale, source);
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                                if (source.volume_field == GpuVolumeField::SigmaA)
                                    copy(data.sigma_a, source);
                                else if (source.volume_field == GpuVolumeField::SigmaS)
                                    copy(data.sigma_s, source);
                                else if (source.volume_field == GpuVolumeField::Emission)
                                    copy(data.emission, source);
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::NanoVdbVolume>) {
                                if (source.volume_field == GpuVolumeField::NanoVdbDensity)
                                    copy(data.density_data, source);
                                else if (source.volume_field == GpuVolumeField::NanoVdbTemperature)
                                    copy(data.temperature_data, source);
                            }
                        },
                        volume.data);
                }
                break;
            }
        this->scene.mark_all_changed();
    }

    GpuGeometry GpuScene::create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer* command_buffer) {
        const scene::TriangleMeshGeometry mesh = tessellate_geometry(geometry);
        GpuGeometry result{};
        result.id                           = geometry.id;
        const auto binding                  = std::ranges::find(this->geometry_bindings, geometry.id, &GpuGeometryBinding::geometry);
        result.mode                         = binding == this->geometry_bindings.end() ? GpuMeshUpdateMode::Immutable : binding->mode;
        result.acceleration_kind            = std::holds_alternative<scene::SphereGeometry>(geometry.data) || std::holds_alternative<scene::DiskGeometry>(geometry.data) || std::holds_alternative<scene::CylinderGeometry>(geometry.data) ? GpuGeometryKind::Procedural : GpuGeometryKind::Triangle;
        result.vertex_count                 = static_cast<std::uint32_t>(mesh.positions.size());
        result.index_count                  = static_cast<std::uint32_t>(mesh.indices.size());
        result.vertex_capacity              = result.vertex_count;
        result.index_capacity               = result.index_count;
        result.acceleration_primitive_count = result.acceleration_kind == GpuGeometryKind::Triangle ? result.index_count / 3u : 1u;
        result.attribute_flags              = (mesh.normals.empty() ? 0u : 1u) | (mesh.tangents.empty() ? 0u : 2u) | (mesh.texture_coordinates.empty() ? 0u : 4u);
        const std::array<scene::Float3, 1> missing_float3{};
        const std::vector<scene::Float3> missing_dynamic_float3(result.mode == GpuMeshUpdateMode::Immutable ? 0 : mesh.positions.size());
        const std::array<scene::Float2, 1> missing_float2{};
        const std::span<const scene::Float3> positions{mesh.positions};
        const std::span<const scene::Float3> normals             = mesh.normals.empty() ? missing_dynamic_float3.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{missing_dynamic_float3} : std::span<const scene::Float3>{mesh.normals};
        const std::span<const scene::Float3> tangents            = mesh.tangents.empty() ? missing_dynamic_float3.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{missing_dynamic_float3} : std::span<const scene::Float3>{mesh.tangents};
        const std::span<const scene::Float2> texture_coordinates = mesh.texture_coordinates.empty() ? std::span<const scene::Float2>{missing_float2} : std::span<const scene::Float2>{mesh.texture_coordinates};
        const std::span<const std::uint32_t> indices{mesh.indices};
        const vk::BufferUsageFlags position_usage  = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer;
        const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer;
        result.positions                           = command_buffer ? upload_buffer(*this->runtime, *command_buffer, positions, position_usage) : upload_buffer(*this->runtime, positions, position_usage);
        result.normals                             = command_buffer ? upload_buffer(*this->runtime, *command_buffer, normals, attribute_usage) : upload_buffer(*this->runtime, normals, attribute_usage);
        result.tangents                            = command_buffer ? upload_buffer(*this->runtime, *command_buffer, tangents, attribute_usage) : upload_buffer(*this->runtime, tangents, attribute_usage);
        result.texture_coordinates                 = command_buffer ? upload_buffer(*this->runtime, *command_buffer, texture_coordinates, attribute_usage) : upload_buffer(*this->runtime, texture_coordinates, attribute_usage);
        result.indices                             = command_buffer ? upload_buffer(*this->runtime, *command_buffer, indices, position_usage) : upload_buffer(*this->runtime, indices, position_usage);
        if (result.acceleration_kind == GpuGeometryKind::Procedural) {
            const scene::Bounds3 bounds = scene::geometry_bounds(geometry);
            const std::array aabbs{vk::AabbPositionsKHR{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}};
            result.aabbs = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const vk::AabbPositionsKHR>{aabbs}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) : upload_buffer(*this->runtime, std::span<const vk::AabbPositionsKHR>{aabbs}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        }
        result.positions_descriptor           = this->runtime->allocate_resource_descriptor();
        result.normals_descriptor             = this->runtime->allocate_resource_descriptor();
        result.tangents_descriptor            = this->runtime->allocate_resource_descriptor();
        result.texture_coordinates_descriptor = this->runtime->allocate_resource_descriptor();
        result.indices_descriptor             = this->runtime->allocate_resource_descriptor();
        this->runtime->write_buffer(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->runtime->write_buffer(result.normals_descriptor, vk::DescriptorType::eStorageBuffer, result.normals);
        this->runtime->write_buffer(result.tangents_descriptor, vk::DescriptorType::eStorageBuffer, result.tangents);
        this->runtime->write_buffer(result.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, result.texture_coordinates);
        this->runtime->write_buffer(result.indices_descriptor, vk::DescriptorType::eStorageBuffer, result.indices);
        if (command_buffer && result.mode != GpuMeshUpdateMode::Immutable && (mesh.normals.empty() || mesh.tangents.empty())) this->generate_dynamic_attributes(result, mesh.normals.empty(), mesh.tangents.empty(), *command_buffer);
        result.blas         = this->build_bottom_level(result.acceleration_kind == GpuGeometryKind::Triangle ? triangle_geometry(result) : procedural_geometry(result), result.acceleration_primitive_count, result.mode, command_buffer);
        result.gpu_modified = command_buffer && result.mode != GpuMeshUpdateMode::Immutable;
        return result;
    }

    GpuParticleSet GpuScene::create_particle_set(const scene::ParticleSet& particles, const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer, const std::uint32_t capacity) {
        GpuParticleSet result{};
        result.id                = particles.id;
        result.particle_count    = static_cast<std::uint32_t>(particles.positions.size());
        result.particle_capacity = std::max(result.particle_count, capacity);
        result.attribute_flags   = (particles.velocities.empty() ? 0u : 1u) | (particles.colors.empty() ? 0u : 2u) | (particles.temperatures.empty() ? 0u : 4u) | (particles.particle_materials.empty() ? 0u : 8u);
        const std::array<scene::Float3, 1> missing_float3{};
        const std::span<const scene::Float3> velocities = particles.velocities.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{particles.velocities};
        const std::span<const scene::Float3> colors     = particles.colors.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{particles.colors};
        const std::array<float, 1> missing_float{};
        const std::span<const float> temperatures = particles.temperatures.empty() ? std::span<const float>{missing_float} : std::span<const float>{particles.temperatures};
        std::vector<std::uint32_t> materials{};
        materials.reserve(std::max<std::size_t>(particles.particle_materials.size(), 1));
        for (const scene::MaterialId material : particles.particle_materials) materials.push_back(static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, material, &scene::MaterialResource::id) - scene.resources.materials.begin()));
        if (materials.empty()) materials.emplace_back();
        const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer;
        result.positions                           = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const scene::Float3>{particles.positions}, attribute_usage, result.particle_capacity) : upload_buffer(*this->runtime, std::span<const scene::Float3>{particles.positions}, attribute_usage, result.particle_capacity);
        result.radii                               = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const float>{particles.radii}, attribute_usage, result.particle_capacity) : upload_buffer(*this->runtime, std::span<const float>{particles.radii}, attribute_usage, result.particle_capacity);
        result.velocities                          = command_buffer ? upload_buffer(*this->runtime, *command_buffer, velocities, attribute_usage, particles.velocities.empty() ? 0 : result.particle_capacity) : upload_buffer(*this->runtime, velocities, attribute_usage, particles.velocities.empty() ? 0 : result.particle_capacity);
        result.colors                              = command_buffer ? upload_buffer(*this->runtime, *command_buffer, colors, attribute_usage, particles.colors.empty() ? 0 : result.particle_capacity) : upload_buffer(*this->runtime, colors, attribute_usage, particles.colors.empty() ? 0 : result.particle_capacity);
        result.temperatures                        = command_buffer ? upload_buffer(*this->runtime, *command_buffer, temperatures, attribute_usage, particles.temperatures.empty() ? 0 : result.particle_capacity) : upload_buffer(*this->runtime, temperatures, attribute_usage, particles.temperatures.empty() ? 0 : result.particle_capacity);
        result.materials                           = command_buffer ? upload_buffer(*this->runtime, *command_buffer, std::span<const std::uint32_t>{materials}, attribute_usage, particles.particle_materials.empty() ? 0 : result.particle_capacity) : upload_buffer(*this->runtime, std::span<const std::uint32_t>{materials}, attribute_usage, particles.particle_materials.empty() ? 0 : result.particle_capacity);
        result.positions_descriptor                = this->runtime->allocate_resource_descriptor();
        result.radii_descriptor                    = this->runtime->allocate_resource_descriptor();
        result.velocities_descriptor               = this->runtime->allocate_resource_descriptor();
        result.colors_descriptor                   = this->runtime->allocate_resource_descriptor();
        result.temperatures_descriptor             = this->runtime->allocate_resource_descriptor();
        result.materials_descriptor                = this->runtime->allocate_resource_descriptor();
        this->runtime->write_buffer(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->runtime->write_buffer(result.radii_descriptor, vk::DescriptorType::eStorageBuffer, result.radii);
        this->runtime->write_buffer(result.velocities_descriptor, vk::DescriptorType::eStorageBuffer, result.velocities);
        this->runtime->write_buffer(result.colors_descriptor, vk::DescriptorType::eStorageBuffer, result.colors);
        this->runtime->write_buffer(result.temperatures_descriptor, vk::DescriptorType::eStorageBuffer, result.temperatures);
        this->runtime->write_buffer(result.materials_descriptor, vk::DescriptorType::eStorageBuffer, result.materials);
        return result;
    }

    GpuVolume GpuScene::create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer* command_buffer) {
        GpuVolume result{};
        result.id         = volume.id;
        result.revision   = volume.revision;
        const auto upload = [this, command_buffer, &result](const GpuVolumeField field, const auto values) {
            if (values.empty()) return;
            const std::size_t index   = std::to_underlying(field);
            result.fields[index]      = command_buffer ? upload_buffer(*this->runtime, *command_buffer, values, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(*this->runtime, values, vk::BufferUsageFlagBits::eStorageBuffer);
            result.descriptors[index] = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(result.descriptors[index], vk::DescriptorType::eStorageBuffer, result.fields[index]);
            result.present[index] = true;
        };
        std::visit(
            [&result, &upload](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DensityGridVolume>) {
                    result.resolution = data.resolution;
                    upload(GpuVolumeField::Density, std::span<const float>{data.density});
                    upload(GpuVolumeField::Temperature, std::span<const float>{data.temperature});
                    upload(GpuVolumeField::EmissionScale, std::span<const float>{data.emission_scale});
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::RgbGridVolume>) {
                    result.resolution = data.resolution;
                    upload(GpuVolumeField::SigmaA, std::span<const scene::Float3>{data.sigma_a});
                    upload(GpuVolumeField::SigmaS, std::span<const scene::Float3>{data.sigma_s});
                    upload(GpuVolumeField::Emission, std::span<const scene::Float3>{data.emission});
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::NanoVdbVolume>) {
                    upload(GpuVolumeField::NanoVdbDensity, std::span<const std::uint32_t>{data.density_data});
                    upload(GpuVolumeField::NanoVdbTemperature, std::span<const std::uint32_t>{data.temperature_data});
                }
            },
            volume.data);
        return result;
    }

    std::vector<vk::AccelerationStructureInstanceKHR> GpuScene::acceleration_structure_instance_data(const scene::SceneView scene) {
        std::vector<vk::AccelerationStructureInstanceKHR> instances{};
        std::size_t primitive_count{};
        for (const scene::Instance& instance : scene.resources.instances) {
            if (!instance.visible) continue;
            const std::vector<scene::Prototype>::const_iterator prototype = std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            primitive_count += prototype->primitives.size();
        }
        instances.reserve(primitive_count);
        this->draws.clear();
        this->draws.reserve(primitive_count);
        this->source_instances.clear();
        this->source_instances.reserve(primitive_count);
        this->acceleration_draw_indices.clear();
        this->acceleration_draw_indices.reserve(primitive_count);
        this->acceleration_source_instances.clear();
        this->acceleration_source_instances.reserve(primitive_count);
        for (std::uint32_t instance_index = 0; instance_index < scene.resources.instances.size(); ++instance_index) {
            const scene::Instance& instance = scene.resources.instances[instance_index];
            if (!instance.visible) continue;
            const std::vector<scene::Prototype>::const_iterator prototype = std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            for (std::uint32_t primitive_index = 0; primitive_index < prototype->primitives.size(); ++primitive_index) {
                const scene::Primitive& primitive                           = prototype->primitives[primitive_index];
                const std::vector<GpuGeometry>::const_iterator mesh         = std::ranges::find(this->geometries, primitive.geometry, &GpuGeometry::id);
                const std::vector<GpuParticleSet>::const_iterator particles = std::ranges::find(this->particle_sets, primitive.particles, &GpuParticleSet::id);
                if (mesh == this->geometries.end() && particles == this->particle_sets.end()) throw std::runtime_error("GpuScene requires a Geometry or Particle Set for every compiled surface Primitive");

                const bool particle_draw     = particles != this->particle_sets.end();
                const std::uint32_t gpu_draw = static_cast<std::uint32_t>(this->draws.size());
                this->draws.emplace_back(particle_draw ? GpuDrawKind::ParticleSet : GpuDrawKind::Geometry, static_cast<std::uint32_t>(particle_draw ? particles - this->particle_sets.begin() : mesh - this->geometries.begin()), gpu_draw, instance_index, primitive_index);
                this->source_instances.push_back(instance.id);
                if (particle_draw) continue;

                const scene::Transform world_transform = instance.transform * primitive.transform;
                vk::TransformMatrixKHR transform{};
                for (std::uint32_t row = 0; row < 3; ++row)
                    for (std::uint32_t column = 0; column < 4; ++column) transform.matrix[row][column] = world_transform.matrix[row * 4u + column];
                vk::GeometryInstanceFlagsKHR instance_flags = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
                if (primitive.alpha.value == 0) instance_flags |= vk::GeometryInstanceFlagBitsKHR::eForceOpaque;
                const std::vector<scene::MaterialResource>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::MaterialResource::id);
                const bool volume_boundary                                          = (primitive.media.inside.value != 0 || primitive.media.outside.value != 0) && material != scene.resources.materials.end() && std::holds_alternative<scene::InterfaceMaterialData>(material->data);
                const std::uint32_t acceleration_index                              = static_cast<std::uint32_t>(instances.size());
                instances.emplace_back(transform, acceleration_index, volume_boundary ? 0x80u : 0x7fu, mesh->acceleration_kind == GpuGeometryKind::Procedural ? 1u : 0u, instance_flags, mesh->blas.address);
                this->acceleration_draw_indices.push_back(gpu_draw);
                this->acceleration_source_instances.push_back(instance.id);
            }
        }
        return instances;
    }
    vk::DeviceAddress GpuScene::acquire_scratch(const vk::DeviceSize size, const bool immediate) {
        const vk::DeviceSize alignment      = this->runtime->acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;
        GpuBuffer* buffer                   = immediate ? &this->immediate_scratch : &this->frame_scratch[this->runtime->frame_index];
        vk::DeviceSize* offset              = immediate ? nullptr : &this->scratch_offsets[this->runtime->frame_index];
        const vk::DeviceSize current_offset = offset ? *offset : 0;
        vk::DeviceAddress address           = buffer->address + current_offset;
        address                             = (address + alignment - 1u) & ~(alignment - 1u);
        const bool available                = buffer->buffer != nullptr && address + size <= buffer->address + buffer->size;
        if (!available) {
            GpuBuffer replacement = this->runtime->create_buffer(std::max(size + alignment - 1u, buffer->size * 2u), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            if (!immediate && buffer->buffer != nullptr) this->runtime->defer([previous = std::move(*buffer)]() mutable {});
            *buffer = std::move(replacement);
            if (offset) *offset = 0;
            address = (buffer->address + alignment - 1u) & ~(alignment - 1u);
        }
        if (offset) *offset = address - buffer->address + size;
        return address;
    }

    void GpuScene::update_bottom_level(GpuGeometry& mesh, const scene::Geometry& source, const vk::raii::CommandBuffer& command_buffer) {
        const scene::TriangleMeshGeometry& triangle_mesh = std::get<scene::TriangleMeshGeometry>(source.data);
        const std::array<scene::Float3, 1> missing_float3{};
        const std::array<scene::Float2, 1> missing_float2{};
        const GpuUploadSlice position_upload                = this->runtime->stage_upload(std::as_bytes(std::span<const scene::Float3>{
            triangle_mesh.positions,
        }));
        const GpuUploadSlice normal_upload                  = this->runtime->stage_upload(std::as_bytes(triangle_mesh.normals.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{triangle_mesh.normals}));
        const GpuUploadSlice tangent_upload                 = this->runtime->stage_upload(std::as_bytes(triangle_mesh.tangents.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{triangle_mesh.tangents}));
        const GpuUploadSlice texture_coordinate_upload      = this->runtime->stage_upload(std::as_bytes(triangle_mesh.texture_coordinates.empty() ? std::span<const scene::Float2>{missing_float2} : std::span<const scene::Float2>{triangle_mesh.texture_coordinates}));
        const vk::AccelerationStructureGeometryKHR geometry = mesh.acceleration_kind == GpuGeometryKind::Triangle ? triangle_geometry(mesh) : procedural_geometry(mesh);
        const std::uint32_t primitive_count                 = mesh.acceleration_primitive_count;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *mesh.blas.structure,
            *mesh.blas.structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->runtime->device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.copyBuffer(position_upload.buffer, *mesh.positions.buffer,
            vk::BufferCopy{
                position_upload.offset,
                0,
                position_upload.size,
            });
        command_buffer.copyBuffer(normal_upload.buffer, *mesh.normals.buffer,
            vk::BufferCopy{
                normal_upload.offset,
                0,
                normal_upload.size,
            });
        command_buffer.copyBuffer(tangent_upload.buffer, *mesh.tangents.buffer,
            vk::BufferCopy{
                tangent_upload.offset,
                0,
                tangent_upload.size,
            });
        command_buffer.copyBuffer(texture_coordinate_upload.buffer, *mesh.texture_coordinates.buffer,
            vk::BufferCopy{
                texture_coordinate_upload.offset,
                0,
                texture_coordinate_upload.size,
            });
        const std::array<vk::BufferMemoryBarrier2, 4> upload_dependencies{
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *mesh.positions.buffer,
                0,
                mesh.positions.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *mesh.normals.buffer,
                0,
                mesh.normals.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *mesh.tangents.buffer,
                0,
                mesh.tangents.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *mesh.texture_coordinates.buffer,
                0,
                mesh.texture_coordinates.size,
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{
            {},
            0,
            nullptr,
            static_cast<std::uint32_t>(upload_dependencies.size()),
            upload_dependencies.data(),
            0,
            nullptr,
        });
        if (triangle_mesh.normals.empty() || triangle_mesh.tangents.empty()) this->generate_dynamic_attributes(mesh, triangle_mesh.normals.empty(), triangle_mesh.tangents.empty(), command_buffer);
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        mesh.gpu_modified = true;
    }

    void GpuScene::generate_dynamic_attributes(GpuGeometry& geometry, const bool generate_normals, const bool generate_tangents, const vk::raii::CommandBuffer& command_buffer) {
        struct alignas(16) DynamicMeshPushData {
            DescriptorHandle positions{};
            DescriptorHandle normals{};
            DescriptorHandle tangents{};
            DescriptorHandle texture_coordinates{};
            DescriptorHandle indices{};
            std::array<std::uint32_t, 2> reserved{};
            std::array<std::uint32_t, 4> counts{};
        };
        static_assert(sizeof(DynamicMeshPushData) == 64);
        const DynamicMeshPushData push_data{
            geometry.positions_descriptor,
            geometry.normals_descriptor,
            geometry.tangents_descriptor,
            geometry.texture_coordinates_descriptor,
            geometry.indices_descriptor,
            {},
            {
                geometry.vertex_count,
                geometry.index_count,
                (generate_normals ? 1u : 0u) | (generate_tangents ? 2u : 0u) | ((geometry.attribute_flags & 4u) != 0 ? 4u : 0u),
                0,
            },
        };
        this->runtime->bind_descriptor_heaps(command_buffer);
        this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->dynamic_mesh_clear_shader);
        command_buffer.dispatch((geometry.vertex_count + 255u) / 256u, 1, 1);
        const vk::MemoryBarrier2 pass_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &pass_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->dynamic_mesh_accumulate_shader);
        command_buffer.dispatch((geometry.index_count / 3u + 255u) / 256u, 1, 1);
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &pass_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->dynamic_mesh_normalize_shader);
        command_buffer.dispatch((geometry.vertex_count + 255u) / 256u, 1, 1);
        const vk::MemoryBarrier2 output_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &output_dependency});
        if (generate_normals) geometry.attribute_flags |= 1u;
        if (generate_tangents) geometry.attribute_flags |= 2u;
    }

    void GpuScene::synchronize_external_geometry(const scene::GeometryId geometry_id, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, const std::uint32_t vertex_count, const std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer) {
        if (this->external_geometries.empty()) this->scratch_offsets[this->runtime->frame_index] = 0;
        GpuGeometry& mesh = *std::ranges::find(this->geometries, geometry_id, &GpuGeometry::id);
        if (mesh.mode == GpuMeshUpdateMode::Immutable) throw std::runtime_error("CUDA External Geometry requires a dynamic update mode");
        const bool vertex_attribute = positions || normals || tangents || texture_coordinates;
        if (vertex_attribute && !positions && vertex_count != mesh.vertex_count) throw std::runtime_error("A TriangleMesh vertex count change must publish positions");
        const std::uint32_t updated_vertex_count = positions ? vertex_count : mesh.vertex_count;
        const std::uint32_t updated_index_count  = indices ? index_count : mesh.index_count;
        if (mesh.mode == GpuMeshUpdateMode::Deformable && (updated_vertex_count != mesh.vertex_count || indices)) throw std::runtime_error("CUDA External deformable Geometry changed topology");
        if (mesh.mode == GpuMeshUpdateMode::TopologyChanging) {
            const bool reallocate = updated_vertex_count > mesh.vertex_capacity || updated_index_count > mesh.index_capacity;
            if (updated_vertex_count > mesh.vertex_count && !texture_coordinates && (mesh.attribute_flags & 4u) != 0) throw std::runtime_error("CUDA External topology-changing Geometry must publish texture coordinates when adding textured vertices");
            if (reallocate) {
                GpuGeometry replacement{};
                replacement.id                             = mesh.id;
                replacement.mode                           = mesh.mode;
                replacement.acceleration_kind              = GpuGeometryKind::Triangle;
                replacement.vertex_count                   = updated_vertex_count;
                replacement.index_count                    = updated_index_count;
                replacement.vertex_capacity                = std::bit_ceil(std::max(updated_vertex_count, 1u));
                replacement.index_capacity                 = std::bit_ceil(std::max(updated_index_count, 3u));
                replacement.acceleration_primitive_count   = updated_index_count / 3u;
                replacement.attribute_flags                = mesh.attribute_flags;
                const vk::BufferUsageFlags geometry_usage  = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
                const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
                replacement.positions                      = this->runtime->create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(scene::Float3), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.normals                        = this->runtime->create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(scene::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.tangents                       = this->runtime->create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(scene::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.texture_coordinates            = this->runtime->create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(scene::Float2), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.indices                        = this->runtime->create_buffer(static_cast<vk::DeviceSize>(replacement.index_capacity) * sizeof(std::uint32_t), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.positions_descriptor           = this->runtime->allocate_resource_descriptor();
                replacement.normals_descriptor             = this->runtime->allocate_resource_descriptor();
                replacement.tangents_descriptor            = this->runtime->allocate_resource_descriptor();
                replacement.texture_coordinates_descriptor = this->runtime->allocate_resource_descriptor();
                replacement.indices_descriptor             = this->runtime->allocate_resource_descriptor();
                this->runtime->write_buffer(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
                this->runtime->write_buffer(replacement.normals_descriptor, vk::DescriptorType::eStorageBuffer, replacement.normals);
                this->runtime->write_buffer(replacement.tangents_descriptor, vk::DescriptorType::eStorageBuffer, replacement.tangents);
                this->runtime->write_buffer(replacement.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, replacement.texture_coordinates);
                this->runtime->write_buffer(replacement.indices_descriptor, vk::DescriptorType::eStorageBuffer, replacement.indices);
                const std::uint32_t preserved_vertices = std::min(mesh.vertex_count, updated_vertex_count);
                if (preserved_vertices != 0 && !positions) command_buffer.copyBuffer(*mesh.positions.buffer, *replacement.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(scene::Float3)});
                if (preserved_vertices != 0 && !normals && (mesh.attribute_flags & 1u) != 0) command_buffer.copyBuffer(*mesh.normals.buffer, *replacement.normals.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(scene::Float3)});
                if (preserved_vertices != 0 && !tangents && (mesh.attribute_flags & 2u) != 0) command_buffer.copyBuffer(*mesh.tangents.buffer, *replacement.tangents.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(scene::Float3)});
                if (preserved_vertices != 0 && !texture_coordinates && (mesh.attribute_flags & 4u) != 0) command_buffer.copyBuffer(*mesh.texture_coordinates.buffer, *replacement.texture_coordinates.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(scene::Float2)});
                const std::uint32_t preserved_indices = std::min(mesh.index_count, updated_index_count);
                if (preserved_indices != 0 && !indices) command_buffer.copyBuffer(*mesh.indices.buffer, *replacement.indices.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_indices) * sizeof(std::uint32_t)});
                this->runtime->release_resource_descriptor(mesh.positions_descriptor);
                this->runtime->release_resource_descriptor(mesh.normals_descriptor);
                this->runtime->release_resource_descriptor(mesh.tangents_descriptor);
                this->runtime->release_resource_descriptor(mesh.texture_coordinates_descriptor);
                this->runtime->release_resource_descriptor(mesh.indices_descriptor);
                this->runtime->defer([previous = std::move(mesh)]() mutable {});
                mesh                  = std::move(replacement);
                this->binding_changes = this->binding_changes | scene::SceneChange::Geometry;
            } else {
                mesh.vertex_count                 = updated_vertex_count;
                mesh.index_count                  = updated_index_count;
                mesh.acceleration_primitive_count = updated_index_count / 3u;
            }
        }

        const vk::DeviceSize position_bytes = static_cast<vk::DeviceSize>(updated_vertex_count) * sizeof(scene::Float3);
        if (positions && updated_vertex_count != 0) command_buffer.copyBuffer(*positions->buffer, *mesh.positions.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (normals && updated_vertex_count != 0) command_buffer.copyBuffer(*normals->buffer, *mesh.normals.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (tangents && updated_vertex_count != 0) command_buffer.copyBuffer(*tangents->buffer, *mesh.tangents.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (texture_coordinates && updated_vertex_count != 0) command_buffer.copyBuffer(*texture_coordinates->buffer, *mesh.texture_coordinates.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(updated_vertex_count) * sizeof(scene::Float2)});
        if (indices && updated_index_count != 0) command_buffer.copyBuffer(*indices->buffer, *mesh.indices.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(updated_index_count) * sizeof(std::uint32_t)});

        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
        if (normals) mesh.attribute_flags |= 1u;
        if (tangents) mesh.attribute_flags |= 2u;
        if (texture_coordinates) mesh.attribute_flags |= 4u;
        const bool geometry_changed  = positions || indices;
        const bool generate_normals  = geometry_changed && !normals;
        const bool generate_tangents = (geometry_changed || normals || texture_coordinates) && !tangents;
        if (generate_normals || generate_tangents) this->generate_dynamic_attributes(mesh, generate_normals, generate_tangents, command_buffer);

        if (!std::ranges::contains(this->external_geometries, geometry_id)) this->external_geometries.push_back(geometry_id);
        mesh.gpu_modified = true;
        if (!geometry_changed) return;

        const vk::AccelerationStructureGeometryKHR geometry = triangle_geometry(mesh);
        if (mesh.mode == GpuMeshUpdateMode::TopologyChanging) {
            GpuAccelerationStructure replacement = this->build_bottom_level(geometry, mesh.acceleration_primitive_count, mesh.mode, &command_buffer);
            if (*mesh.blas.structure) this->runtime->defer([previous = std::move(mesh.blas)]() mutable {});
            mesh.blas                           = std::move(replacement);
            this->rebuilt_external_bottom_level = true;
            return;
        }
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *mesh.blas.structure,
            *mesh.blas.structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->runtime->device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, mesh.acceleration_primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{mesh.acceleration_primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    void GpuScene::update_particle_set(GpuParticleSet& particles, const scene::ParticleSet& source, const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        const std::array<scene::Float3, 1> missing_float3{};
        const std::span<const scene::Float3> velocities = source.velocities.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{source.velocities};
        const std::span<const scene::Float3> colors     = source.colors.empty() ? std::span<const scene::Float3>{missing_float3} : std::span<const scene::Float3>{source.colors};
        const std::array<float, 1> missing_float{};
        const std::span<const float> temperatures = source.temperatures.empty() ? std::span<const float>{missing_float} : std::span<const float>{source.temperatures};
        std::vector<std::uint32_t> materials{};
        materials.reserve(std::max<std::size_t>(source.particle_materials.size(), 1));
        for (const scene::MaterialId material : source.particle_materials) materials.push_back(static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, material, &scene::MaterialResource::id) - scene.resources.materials.begin()));
        if (materials.empty()) materials.emplace_back();
        const std::array uploads{
            this->runtime->stage_upload(std::as_bytes(std::span<const scene::Float3>{source.positions})),
            this->runtime->stage_upload(std::as_bytes(std::span<const float>{source.radii})),
            this->runtime->stage_upload(std::as_bytes(colors)),
            this->runtime->stage_upload(std::as_bytes(velocities)),
            this->runtime->stage_upload(std::as_bytes(temperatures)),
            this->runtime->stage_upload(std::as_bytes(std::span<const std::uint32_t>{materials})),
        };
        const std::array<GpuBuffer*, 6> destinations{&particles.positions, &particles.radii, &particles.colors, &particles.velocities, &particles.temperatures, &particles.materials};
        for (std::size_t index = 0; index != uploads.size(); ++index) command_buffer.copyBuffer(uploads[index].buffer, *destinations[index]->buffer, vk::BufferCopy{uploads[index].offset, 0, uploads[index].size});
        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
    }

    void GpuScene::synchronize_external_particles(const scene::ParticleSetId particle_set_id, const GpuBuffer* positions, const GpuBuffer* radii, const GpuBuffer* velocities, const GpuBuffer* colors, const GpuBuffer* temperatures, const GpuBuffer* materials, const DescriptorHandle materials_descriptor, const std::uint32_t particle_count, const vk::raii::CommandBuffer& command_buffer) {
        GpuParticleSet& particles = *std::ranges::find(this->particle_sets, particle_set_id, &GpuParticleSet::id);
        if (particle_count > particles.particle_capacity) {
            GpuParticleSet replacement{};
            replacement.id                             = particles.id;
            replacement.particle_count                 = particle_count;
            replacement.particle_capacity              = std::bit_ceil(std::max(particle_count, 1u));
            replacement.attribute_flags                = particles.attribute_flags;
            const vk::DeviceSize capacity              = replacement.particle_capacity;
            const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            replacement.positions                      = this->runtime->create_buffer(capacity * sizeof(scene::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.radii                          = this->runtime->create_buffer(capacity * sizeof(float), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.velocities                     = this->runtime->create_buffer(capacity * sizeof(scene::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.colors                         = this->runtime->create_buffer(capacity * sizeof(scene::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.temperatures                   = this->runtime->create_buffer(capacity * sizeof(float), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.materials                      = this->runtime->create_buffer(capacity * sizeof(std::uint32_t), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.positions_descriptor           = this->runtime->allocate_resource_descriptor();
            replacement.radii_descriptor               = this->runtime->allocate_resource_descriptor();
            replacement.velocities_descriptor          = this->runtime->allocate_resource_descriptor();
            replacement.colors_descriptor              = this->runtime->allocate_resource_descriptor();
            replacement.temperatures_descriptor        = this->runtime->allocate_resource_descriptor();
            replacement.materials_descriptor           = this->runtime->allocate_resource_descriptor();
            this->runtime->write_buffer(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
            this->runtime->write_buffer(replacement.radii_descriptor, vk::DescriptorType::eStorageBuffer, replacement.radii);
            this->runtime->write_buffer(replacement.velocities_descriptor, vk::DescriptorType::eStorageBuffer, replacement.velocities);
            this->runtime->write_buffer(replacement.colors_descriptor, vk::DescriptorType::eStorageBuffer, replacement.colors);
            this->runtime->write_buffer(replacement.temperatures_descriptor, vk::DescriptorType::eStorageBuffer, replacement.temperatures);
            this->runtime->write_buffer(replacement.materials_descriptor, vk::DescriptorType::eStorageBuffer, replacement.materials);
            const std::uint32_t preserved_particles = std::min(particles.particle_count, particle_count);
            if (preserved_particles != 0 && !positions) command_buffer.copyBuffer(*particles.positions.buffer, *replacement.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(scene::Float3)});
            if (preserved_particles != 0 && !radii) command_buffer.copyBuffer(*particles.radii.buffer, *replacement.radii.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(float)});
            if (preserved_particles != 0 && !velocities && (particles.attribute_flags & 1u) != 0) command_buffer.copyBuffer(*particles.velocities.buffer, *replacement.velocities.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(scene::Float3)});
            if (preserved_particles != 0 && !colors && (particles.attribute_flags & 2u) != 0) command_buffer.copyBuffer(*particles.colors.buffer, *replacement.colors.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(scene::Float3)});
            if (preserved_particles != 0 && !temperatures && (particles.attribute_flags & 4u) != 0) command_buffer.copyBuffer(*particles.temperatures.buffer, *replacement.temperatures.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(float)});
            if (preserved_particles != 0 && !materials && (particles.attribute_flags & 8u) != 0) command_buffer.copyBuffer(*particles.materials.buffer, *replacement.materials.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(std::uint32_t)});
            this->runtime->release_resource_descriptor(particles.positions_descriptor);
            this->runtime->release_resource_descriptor(particles.radii_descriptor);
            this->runtime->release_resource_descriptor(particles.velocities_descriptor);
            this->runtime->release_resource_descriptor(particles.colors_descriptor);
            this->runtime->release_resource_descriptor(particles.temperatures_descriptor);
            this->runtime->release_resource_descriptor(particles.materials_descriptor);
            this->runtime->defer([previous = std::move(particles)]() mutable {});
            particles             = std::move(replacement);
            this->binding_changes = this->binding_changes | scene::SceneChange::Visualization;
        } else
            particles.particle_count = particle_count;

        if (positions && particle_count != 0) command_buffer.copyBuffer(*positions->buffer, *particles.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(scene::Float3)});
        if (radii && particle_count != 0) command_buffer.copyBuffer(*radii->buffer, *particles.radii.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(float)});
        if (velocities && particle_count != 0) {
            command_buffer.copyBuffer(*velocities->buffer, *particles.velocities.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(scene::Float3)});
            particles.attribute_flags |= 1u;
        }
        if (colors && particle_count != 0) {
            command_buffer.copyBuffer(*colors->buffer, *particles.colors.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(scene::Float3)});
            particles.attribute_flags |= 2u;
        }
        if (temperatures && particle_count != 0) {
            command_buffer.copyBuffer(*temperatures->buffer, *particles.temperatures.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(float)});
            particles.attribute_flags |= 4u;
        }
        if (materials && particle_count != 0) {
            struct alignas(8) ParticleMaterialPushData {
                DescriptorHandle source{};
                DescriptorHandle destination{};
                DescriptorHandle lookup{};
                std::array<std::uint32_t, 2> reserved{};
                std::array<std::uint32_t, 4> counts{};
            };
            static_assert(sizeof(ParticleMaterialPushData) == 48);
            const ParticleMaterialPushData push_data{
                materials_descriptor,
                particles.materials_descriptor,
                this->material_lookup_descriptor,
                {},
                {
                    particle_count,
                    this->material_count,
                    0,
                    0,
                },
            };
            this->runtime->bind_descriptor_heaps(command_buffer);
            this->runtime->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->particle_material_shader);
            command_buffer.dispatch((particle_count + 255u) / 256u, 1, 1);
            particles.attribute_flags |= 8u;
        }
        const vk::MemoryBarrier2 copy_dependency{vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &copy_dependency});
        if (!std::ranges::contains(this->external_particle_sets, particle_set_id)) this->external_particle_sets.push_back(particle_set_id);
        particles.gpu_modified = true;
    }

    void GpuScene::synchronize_external_volume(const scene::VolumeId volume_id, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, const std::uint64_t voxel_count, const scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer) {
        GpuVolume& volume                  = *std::ranges::find(this->volumes, volume_id, &GpuVolume::id);
        const std::uint64_t expected_count = static_cast<std::uint64_t>(dirty_region.maximum.x - dirty_region.minimum.x) * (dirty_region.maximum.y - dirty_region.minimum.y) * (dirty_region.maximum.z - dirty_region.minimum.z);
        if (voxel_count != expected_count) throw std::runtime_error("CUDA External Volume element count differs from its dirty region");
        const auto copy = [&command_buffer, dirty_region, &volume](const GpuBuffer* source, const GpuVolumeField field, const vk::DeviceSize element_size) {
            if (!source) return;
            const std::size_t index = std::to_underlying(field);
            if (!volume.present[index]) throw std::runtime_error("CUDA External Volume published a field absent from its Scene resource");
            std::vector<vk::BufferCopy> regions{};
            const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(dirty_region.maximum.x - dirty_region.minimum.x) * element_size;
            for (std::uint32_t z = dirty_region.minimum.z; z != dirty_region.maximum.z; ++z)
                for (std::uint32_t y = dirty_region.minimum.y; y != dirty_region.maximum.y; ++y) {
                    const vk::DeviceSize offset = (static_cast<vk::DeviceSize>(z) * volume.resolution.y * volume.resolution.x + static_cast<vk::DeviceSize>(y) * volume.resolution.x + dirty_region.minimum.x) * element_size;
                    regions.emplace_back(offset, offset, bytes);
                }
            command_buffer.copyBuffer(*source->buffer, *volume.fields[index].buffer, regions);
        };
        copy(density, GpuVolumeField::Density, sizeof(float));
        copy(temperature, GpuVolumeField::Temperature, sizeof(float));
        copy(emission_scale, GpuVolumeField::EmissionScale, sizeof(float));
        copy(sigma_a, GpuVolumeField::SigmaA, sizeof(scene::Float3));
        copy(sigma_s, GpuVolumeField::SigmaS, sizeof(scene::Float3));
        copy(emission, GpuVolumeField::Emission, sizeof(scene::Float3));
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        if (!std::ranges::contains(this->external_volumes, volume_id)) this->external_volumes.push_back(volume_id);
        volume.dirty_region = dirty_region;
        ++volume.revision.content;
        volume.gpu_modified = true;
    }

    void GpuScene::update_volumes(const vk::raii::CommandBuffer& command_buffer) {
        for (const scene::Volume& source : this->state.resources.volumes) {
            GpuVolume& volume = *std::ranges::find(this->volumes, source.id, &GpuVolume::id);
            if (std::ranges::contains(this->external_volumes, source.id) || source.revision.content == volume.revision.content) continue;
            GpuVolume replacement = this->create_volume(source, &command_buffer);
            for (std::size_t field = 0; field != volume.fields.size(); ++field)
                if (volume.present[field]) this->runtime->release_resource_descriptor(volume.descriptors[field]);
            this->runtime->defer([previous = std::move(volume)]() mutable {});
            volume                = std::move(replacement);
            this->binding_changes = this->binding_changes | scene::SceneChange::Volume;
        }
    }

    void GpuScene::update_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer) {
        const vk::AccelerationStructureGeometryInstancesDataKHR instance_data{
            vk::False,
            vk::DeviceOrHostAddressConstKHR{this->acceleration_structure_instances.address},
        };
        const vk::AccelerationStructureGeometryKHR geometry{
            vk::GeometryTypeKHR::eInstances,
            vk::AccelerationStructureGeometryDataKHR{instance_data},
        };
        const std::uint32_t primitive_count = static_cast<std::uint32_t>(instances.size());
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eTopLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *this->top_level_acceleration_structure.structure,
            *this->top_level_acceleration_structure.structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->runtime->device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        const vk::MemoryBarrier2 bottom_level_dependency{
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        if (instances.empty())
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &bottom_level_dependency});
        else {
            const GpuUploadSlice upload = this->runtime->stage_upload(std::as_bytes(instances));
            command_buffer.copyBuffer(upload.buffer, *this->acceleration_structure_instances.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->acceleration_structure_instances.buffer, 0, this->acceleration_structure_instances.size};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &bottom_level_dependency, 1, &upload_dependency});
        }
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    scene::SceneChange GpuScene::apply(const scene::dynamics::PublishedFrame& frame, const vk::raii::CommandBuffer& command_buffer) {
        this->binding_changes = scene::SceneChange::None;
        scene::SceneUpdate update_scene{this->state};
        update_scene.begin_frame();
        for (const scene::dynamics::InstanceTransformUpdate& update : frame.transforms) {
            const auto placement = std::ranges::find(this->instance_placements, update.instance, &std::pair<scene::InstanceId, scene::Transform>::first);
            update_scene.update_transform(update.instance, placement->second * update.transform);
        }
        for (const scene::dynamics::TriangleMeshUpdate& update : frame.meshes) {
            scene::Geometry& resource                  = *std::ranges::find(this->state.resources.geometries, update.geometry, &scene::Geometry::id);
            const scene::TriangleMeshGeometry& current = std::get<scene::TriangleMeshGeometry>(resource.data);
            const auto includes                        = [&update](const scene::dynamics::Attribute attribute) { return (update.attributes & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            if (update.vertex_count != current.positions.size() && !includes(scene::dynamics::Attribute::Position)) throw std::runtime_error("A Host TriangleMesh vertex count change must publish positions");
            if (update.index_count != current.indices.size() && !includes(scene::dynamics::Attribute::Index)) throw std::runtime_error("A Host TriangleMesh index count change must publish indices");
            const bool shape_changed         = includes(scene::dynamics::Attribute::Position) || includes(scene::dynamics::Attribute::Index);
            const bool tangent_frame_changed = shape_changed || includes(scene::dynamics::Attribute::Normal) || includes(scene::dynamics::Attribute::TextureCoordinate);
            update_scene.update_triangle_mesh(update.geometry, includes(scene::dynamics::Attribute::Position) ? std::span<const scene::Float3>{update.positions} : std::span<const scene::Float3>{current.positions}, includes(scene::dynamics::Attribute::Normal) ? std::span<const scene::Float3>{update.normals} : shape_changed ? std::span<const scene::Float3>{} : std::span<const scene::Float3>{current.normals}, includes(scene::dynamics::Attribute::Tangent) ? std::span<const scene::Float3>{update.tangents} : tangent_frame_changed ? std::span<const scene::Float3>{} : std::span<const scene::Float3>{current.tangents}, includes(scene::dynamics::Attribute::TextureCoordinate) ? std::span<const scene::Float2>{update.texture_coordinates} : std::span<const scene::Float2>{current.texture_coordinates}, includes(scene::dynamics::Attribute::Index) ? std::span<const std::uint32_t>{update.indices} : std::span<const std::uint32_t>{current.indices});
        }
        for (const scene::dynamics::ParticleSetUpdate& update : frame.particles) {
            const scene::ParticleSet& current = *std::ranges::find(this->state.resources.particle_sets, update.particles, &scene::ParticleSet::id);
            const auto includes               = [&update](const scene::dynamics::Attribute attribute) { return (update.attributes & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            const bool count_changed          = update.particle_count != current.positions.size();
            if (count_changed && (!includes(scene::dynamics::Attribute::Position) || !includes(scene::dynamics::Attribute::Radius))) throw std::runtime_error("A Host ParticleSet count change must publish positions and radii together");
            if (count_changed && ((!current.velocities.empty() && !includes(scene::dynamics::Attribute::Velocity)) || (!current.colors.empty() && !includes(scene::dynamics::Attribute::Color)) || (!current.temperatures.empty() && !includes(scene::dynamics::Attribute::Temperature)) || (!current.particle_materials.empty() && !includes(scene::dynamics::Attribute::Material)))) throw std::runtime_error("A Host ParticleSet count change must publish every populated per-particle attribute");
            update_scene.update_particle_set(update.particles, includes(scene::dynamics::Attribute::Position) ? std::span<const scene::Float3>{update.positions} : std::span<const scene::Float3>{current.positions}, includes(scene::dynamics::Attribute::Radius) ? std::span<const float>{update.radii} : std::span<const float>{current.radii}, includes(scene::dynamics::Attribute::Velocity) ? std::span<const scene::Float3>{update.velocities} : std::span<const scene::Float3>{current.velocities}, includes(scene::dynamics::Attribute::Color) ? std::span<const scene::Float3>{update.colors} : std::span<const scene::Float3>{current.colors}, includes(scene::dynamics::Attribute::Temperature) ? std::span<const float>{update.temperatures} : std::span<const float>{current.temperatures}, includes(scene::dynamics::Attribute::Material) ? std::span<const scene::MaterialId>{update.materials} : std::span<const scene::MaterialId>{current.particle_materials});
        }
        for (const scene::dynamics::VolumeUpdate& update : frame.volumes) {
            const auto includes = [&update](const scene::dynamics::Attribute attribute) { return (update.attributes & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            if (includes(scene::dynamics::Attribute::Density) || includes(scene::dynamics::Attribute::Temperature) || includes(scene::dynamics::Attribute::EmissionScale)) update_scene.update_density_grid(update.volume, update.region, update.density, update.temperature, update.emission_scale);
            if (includes(scene::dynamics::Attribute::SigmaA) || includes(scene::dynamics::Attribute::SigmaS) || includes(scene::dynamics::Attribute::Emission)) {
                scene::Volume& volume                                   = *std::ranges::find(this->state.resources.volumes, update.volume, &scene::Volume::id);
                std::get<scene::RgbGridVolume>(volume.data).color_space = update.color_space;
                update_scene.update_rgb_grid(update.volume, update.region, update.sigma_a, update.sigma_s, update.emission);
            }
            if (includes(scene::dynamics::Attribute::Velocity)) {
                HostVolumeVector* storage{};
                const auto found = std::ranges::find(this->host_volume_vectors, update.volume, &HostVolumeVector::volume);
                if (found == this->host_volume_vectors.end()) {
                    this->host_volume_vectors.emplace_back();
                    storage             = &this->host_volume_vectors.back();
                    storage->volume     = update.volume;
                    storage->descriptor = this->runtime->allocate_resource_descriptor();
                } else
                    storage = std::to_address(found);
                const std::uint64_t capacity = static_cast<std::uint64_t>(update.resolution.x) * update.resolution.y * update.resolution.z;
                if (storage->capacity < capacity) {
                    if (*storage->buffer.buffer) this->runtime->defer([previous = std::move(storage->buffer)]() mutable {});
                    storage->buffer   = this->runtime->create_buffer(capacity * sizeof(scene::Float3), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                    storage->capacity = capacity;
                    this->runtime->write_buffer(storage->descriptor, vk::DescriptorType::eStorageBuffer, storage->buffer);
                    command_buffer.fillBuffer(*storage->buffer.buffer, 0, storage->buffer.size, 0);
                }
                const GpuUploadSlice upload = this->runtime->stage_upload(std::as_bytes(std::span<const scene::Float3>{update.velocity}));
                std::vector<vk::BufferCopy> regions{};
                const std::uint32_t width  = update.region.maximum.x - update.region.minimum.x;
                const std::uint32_t height = update.region.maximum.y - update.region.minimum.y;
                for (std::uint32_t z = 0; z != update.region.maximum.z - update.region.minimum.z; ++z)
                    for (std::uint32_t y = 0; y != height; ++y) {
                        const vk::DeviceSize source      = upload.offset + (static_cast<vk::DeviceSize>(z) * height + y) * width * sizeof(scene::Float3);
                        const vk::DeviceSize destination = (static_cast<vk::DeviceSize>(update.region.minimum.z + z) * update.resolution.y * update.resolution.x + static_cast<vk::DeviceSize>(update.region.minimum.y + y) * update.resolution.x + update.region.minimum.x) * sizeof(scene::Float3);
                        regions.emplace_back(source, destination, static_cast<vk::DeviceSize>(width) * sizeof(scene::Float3));
                    }
                command_buffer.copyBuffer(upload.buffer, *storage->buffer.buffer, regions);
                const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead};
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
                const scene::Volume& volume = *std::ranges::find(this->state.resources.volumes, update.volume, &scene::Volume::id);
                const VolumeVectorField field{update.volume, storage->descriptor, update.resolution, volume.bounds, volume.transform};
                const auto visible = std::ranges::find(this->volume_vector_fields, update.volume, &VolumeVectorField::volume);
                if (visible == this->volume_vector_fields.end())
                    this->volume_vector_fields.push_back(field);
                else
                    *visible = field;
                update_scene.mark(scene::SceneChange::Visualization);
            }
        }
        for (const scene::dynamics::ExternalOutputView& output : frame.external) {
            std::uint64_t attributes{};
            for (const scene::dynamics::ExternalBufferView& buffer : output.buffers) attributes |= 1ull << static_cast<std::uint32_t>(buffer.attribute);
            if (output.kind == scene::dynamics::ResourceKind::TriangleMesh)
                update_scene.mark(scene::SceneChange::Geometry);
            else if (output.kind == scene::dynamics::ResourceKind::ParticleSet)
                update_scene.mark(scene::SceneChange::Visualization);
            else if (output.kind == scene::dynamics::ResourceKind::Volume) {
                constexpr std::uint64_t volume_fields = (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::Density)) | (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::Temperature)) | (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::EmissionScale)) | (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::SigmaA)) | (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::SigmaS)) | (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::Emission));
                if ((attributes & volume_fields) != 0) update_scene.mark(scene::SceneChange::Volume);
                if ((attributes & (1ull << static_cast<std::uint32_t>(scene::dynamics::Attribute::Velocity))) != 0) update_scene.mark(scene::SceneChange::Visualization);
            }
        }
        update_scene.commit_frame();

        std::vector<scene::GeometryId> external_geometries{};
        std::vector<scene::ParticleSetId> external_particles{};
        std::vector<scene::VolumeId> external_volumes{};
        bool mixed_transfers{};
        for (const scene::dynamics::ExternalOutputView& output : frame.external) {
            if (output.kind == scene::dynamics::ResourceKind::TriangleMesh) {
                const scene::GeometryId resource = std::get<scene::GeometryId>(output.resource);
                if (std::ranges::contains(frame.meshes, resource, &scene::dynamics::TriangleMeshUpdate::geometry))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_geometries, resource))
                    external_geometries.push_back(resource);
            } else if (output.kind == scene::dynamics::ResourceKind::ParticleSet) {
                const scene::ParticleSetId resource = std::get<scene::ParticleSetId>(output.resource);
                if (std::ranges::contains(frame.particles, resource, &scene::dynamics::ParticleSetUpdate::particles))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_particles, resource))
                    external_particles.push_back(resource);
            } else if (output.kind == scene::dynamics::ResourceKind::Volume) {
                const scene::VolumeId resource = std::get<scene::VolumeId>(output.resource);
                if (std::ranges::contains(frame.volumes, resource, &scene::dynamics::VolumeUpdate::volume))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_volumes, resource))
                    external_volumes.push_back(resource);
            }
        }
        this->begin_external_updates(external_geometries, external_particles, external_volumes);
        this->synchronize_state(command_buffer);
        if (mixed_transfers) {
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        }
        for (const scene::dynamics::ExternalOutputView& output : frame.external) {
            if (output.kind == scene::dynamics::ResourceKind::Volume) {
                const GpuBuffer* density{};
                const GpuBuffer* temperature{};
                const GpuBuffer* emission_scale{};
                const GpuBuffer* sigma_a{};
                const GpuBuffer* sigma_s{};
                const GpuBuffer* emission{};
                const scene::dynamics::ExternalBufferView* velocity{};
                for (const scene::dynamics::ExternalBufferView& buffer : output.buffers) {
                    if (buffer.attribute == scene::dynamics::Attribute::Density)
                        density = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Temperature)
                        temperature = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::EmissionScale)
                        emission_scale = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::SigmaA)
                        sigma_a = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::SigmaS)
                        sigma_s = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Emission)
                        emission = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Velocity)
                        velocity = &buffer;
                }
                if (density || temperature || emission_scale || sigma_a || sigma_s || emission) this->synchronize_external_volume(std::get<scene::VolumeId>(output.resource), density, temperature, emission_scale, sigma_a, sigma_s, emission, output.active_count, *output.dirty_region, command_buffer);
                if (velocity) {
                    const scene::VolumeId volume_id = std::get<scene::VolumeId>(output.resource);
                    const scene::Volume& volume     = *std::ranges::find(this->state.resources.volumes, volume_id, &scene::Volume::id);
                    scene::UInt3 resolution{};
                    std::visit(
                        [&resolution](const auto& data) {
                            if constexpr (requires { data.resolution; }) resolution = data.resolution;
                        },
                        volume.data);
                    const VolumeVectorField field{volume_id, velocity->descriptor, resolution, volume.bounds, volume.transform};
                    const auto found = std::ranges::find(this->volume_vector_fields, volume_id, &VolumeVectorField::volume);
                    if (found == this->volume_vector_fields.end())
                        this->volume_vector_fields.push_back(field);
                    else
                        *found = field;
                }
                continue;
            }
            if (output.kind == scene::dynamics::ResourceKind::ParticleSet) {
                const GpuBuffer* positions{};
                const GpuBuffer* radii{};
                const GpuBuffer* velocities{};
                const GpuBuffer* colors{};
                const GpuBuffer* temperatures{};
                const GpuBuffer* materials{};
                DescriptorHandle materials_descriptor{};
                for (const scene::dynamics::ExternalBufferView& buffer : output.buffers) {
                    if (buffer.attribute == scene::dynamics::Attribute::Position)
                        positions = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Radius)
                        radii = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Velocity)
                        velocities = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Color)
                        colors = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Temperature)
                        temperatures = buffer.buffer;
                    else if (buffer.attribute == scene::dynamics::Attribute::Material) {
                        materials            = buffer.buffer;
                        materials_descriptor = buffer.descriptor;
                    }
                }
                const GpuParticleSet& current = *std::ranges::find(this->particle_sets, std::get<scene::ParticleSetId>(output.resource), &GpuParticleSet::id);
                if (output.active_count != current.particle_count && (!positions || !radii)) throw std::runtime_error("A ParticleSet count change must publish positions and radii together");
                this->synchronize_external_particles(std::get<scene::ParticleSetId>(output.resource), positions, radii, velocities, colors, temperatures, materials, materials_descriptor, static_cast<std::uint32_t>(output.active_count), command_buffer);
                continue;
            }
            if (output.kind != scene::dynamics::ResourceKind::TriangleMesh) continue;
            const GpuBuffer* positions{};
            const GpuBuffer* normals{};
            const GpuBuffer* tangents{};
            const GpuBuffer* texture_coordinates{};
            const GpuBuffer* indices{};
            for (const scene::dynamics::ExternalBufferView& buffer : output.buffers) {
                if (buffer.attribute == scene::dynamics::Attribute::Position)
                    positions = buffer.buffer;
                else if (buffer.attribute == scene::dynamics::Attribute::Normal)
                    normals = buffer.buffer;
                else if (buffer.attribute == scene::dynamics::Attribute::Tangent)
                    tangents = buffer.buffer;
                else if (buffer.attribute == scene::dynamics::Attribute::TextureCoordinate)
                    texture_coordinates = buffer.buffer;
                else if (buffer.attribute == scene::dynamics::Attribute::Index)
                    indices = buffer.buffer;
            }
            this->synchronize_external_geometry(std::get<scene::GeometryId>(output.resource), positions, normals, tangents, texture_coordinates, indices, static_cast<std::uint32_t>(output.active_count), static_cast<std::uint32_t>(output.secondary_count), command_buffer);
        }
        this->end_external_updates(command_buffer);
        return std::exchange(this->binding_changes, scene::SceneChange::None);
    }

    scene::SceneChange GpuScene::synchronize(const vk::raii::CommandBuffer& command_buffer) {
        this->binding_changes = scene::SceneChange::None;
        this->synchronize_state(command_buffer);
        return std::exchange(this->binding_changes, scene::SceneChange::None);
    }

    void GpuScene::synchronize_state(const vk::raii::CommandBuffer& command_buffer) {
        const scene::SceneView scene = this->state.view();
        if (scene.revision.value == this->uploaded_revision.value) return;
        this->cache_texture_images(scene, &command_buffer);
        if (this->external_geometries.empty()) this->scratch_offsets[this->runtime->frame_index] = 0;
        bool rebuilt_bottom_level = std::exchange(this->rebuilt_external_bottom_level, false);
        if ((scene.revision.changes & scene::SceneChange::Geometry) != scene::SceneChange::None) {
            for (GpuGeometry& mesh : this->geometries) {
                const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, mesh.id, &scene::Geometry::id);
                if (std::ranges::contains(this->external_geometries, mesh.id)) continue;
                if (mesh.mode != GpuMeshUpdateMode::Deformable) {
                    GpuGeometry replacement = this->create_geometry(geometry, &command_buffer);
                    this->runtime->release_resource_descriptor(mesh.positions_descriptor);
                    this->runtime->release_resource_descriptor(mesh.normals_descriptor);
                    this->runtime->release_resource_descriptor(mesh.tangents_descriptor);
                    this->runtime->release_resource_descriptor(mesh.texture_coordinates_descriptor);
                    this->runtime->release_resource_descriptor(mesh.indices_descriptor);
                    this->runtime->defer([previous = std::move(mesh)]() mutable {});
                    mesh                  = std::move(replacement);
                    this->binding_changes = this->binding_changes | scene::SceneChange::Geometry;
                    rebuilt_bottom_level  = true;
                    continue;
                }
                this->update_bottom_level(mesh, geometry, command_buffer);
            }
        }
        if ((scene.revision.changes & scene::SceneChange::Visualization) != scene::SceneChange::None) {
            for (GpuParticleSet& particles : this->particle_sets) {
                const scene::ParticleSet& source = *std::ranges::find(scene.resources.particle_sets, particles.id, &scene::ParticleSet::id);
                if (std::ranges::contains(this->external_particle_sets, particles.id)) continue;
                const std::uint32_t attribute_flags = (source.velocities.empty() ? 0u : 1u) | (source.colors.empty() ? 0u : 2u) | (source.temperatures.empty() ? 0u : 4u) | (source.particle_materials.empty() ? 0u : 8u);
                if (source.positions.size() > particles.particle_capacity || particles.attribute_flags != attribute_flags) {
                    GpuParticleSet replacement = this->create_particle_set(source, scene, &command_buffer, std::bit_ceil(static_cast<std::uint32_t>(std::max<std::size_t>(source.positions.size(), 1))));
                    this->runtime->release_resource_descriptor(particles.positions_descriptor);
                    this->runtime->release_resource_descriptor(particles.radii_descriptor);
                    this->runtime->release_resource_descriptor(particles.velocities_descriptor);
                    this->runtime->release_resource_descriptor(particles.colors_descriptor);
                    this->runtime->release_resource_descriptor(particles.temperatures_descriptor);
                    this->runtime->release_resource_descriptor(particles.materials_descriptor);
                    this->runtime->defer([previous = std::move(particles)]() mutable {});
                    particles             = std::move(replacement);
                    this->binding_changes = this->binding_changes | scene::SceneChange::Visualization;
                    continue;
                }
                particles.particle_count  = static_cast<std::uint32_t>(source.positions.size());
                particles.attribute_flags = attribute_flags;
                this->update_particle_set(particles, source, scene, command_buffer);
            }
        }
        if ((scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(command_buffer);
        this->external_geometries.clear();
        this->external_particle_sets.clear();
        this->external_volumes.clear();
        if (rebuilt_bottom_level || (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            this->update_top_level(instances, command_buffer);
        }
        this->uploaded_revision = scene.revision;
    }

    void GpuScene::begin_external_updates(const std::span<const scene::GeometryId> geometries, const std::span<const scene::ParticleSetId> particle_sets, const std::span<const scene::VolumeId> volumes) {
        this->external_geometries.assign(geometries.begin(), geometries.end());
        this->external_particle_sets.assign(particle_sets.begin(), particle_sets.end());
        this->external_volumes.assign(volumes.begin(), volumes.end());
    }

    void GpuScene::end_external_updates(const vk::raii::CommandBuffer& command_buffer) {
        if (std::exchange(this->rebuilt_external_bottom_level, false)) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(this->state.view());
            this->update_top_level(instances, command_buffer);
        }
        this->external_geometries.clear();
        this->external_particle_sets.clear();
        this->external_volumes.clear();
    }
    GpuAccelerationStructure GpuScene::build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, const std::uint32_t primitive_count, const GpuMeshUpdateMode mode, const vk::raii::CommandBuffer* command_buffer) {
        vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess;
        if (mode == GpuMeshUpdateMode::Deformable)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        else if (!command_buffer)
            flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            flags,
            vk::BuildAccelerationStructureModeKHR::eBuild,
            {},
            {},
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->runtime->device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);

        GpuAccelerationStructure result{};
        result.storage   = this->runtime->create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.structure = vk::raii::AccelerationStructureKHR{
            this->runtime->device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel},
        };
        build_info.dstAccelerationStructure = *result.structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_scratch(sizes.buildScratchSize, !command_buffer)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        if (command_buffer) {
            command_buffer->buildAccelerationStructuresKHR(build_info, ranges);
            result.address = this->runtime->device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.structure});
            return result;
        }
        std::optional<vk::raii::QueryPool> compaction_query{};
        if (mode == GpuMeshUpdateMode::Immutable) compaction_query.emplace(this->runtime->device, vk::QueryPoolCreateInfo{{}, vk::QueryType::eAccelerationStructureCompactedSizeKHR, 1});
        this->runtime->immediate([&build_info, &ranges, &result, &compaction_query](const vk::raii::CommandBuffer& command_buffer) {
            if (compaction_query) command_buffer.resetQueryPool(**compaction_query, 0, 1);
            command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
            if (compaction_query) {
                const vk::MemoryBarrier2 build_dependency{
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                    vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                    vk::AccessFlagBits2::eAccelerationStructureReadKHR,
                };
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &build_dependency});
                const std::array<vk::AccelerationStructureKHR, 1> structures{*result.structure};
                command_buffer.writeAccelerationStructuresPropertiesKHR(structures, vk::QueryType::eAccelerationStructureCompactedSizeKHR, **compaction_query, 0);
            }
        });
        if (compaction_query) {
            std::uint64_t compacted_size{};
            if (compaction_query->getResults(0, 1, sizeof(compacted_size), &compacted_size, sizeof(compacted_size), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait) != vk::Result::eSuccess) throw std::runtime_error("Static BLAS compaction size query failed");
            if (compacted_size != 0 && compacted_size < result.storage.size) {
                GpuAccelerationStructure compacted{};
                compacted.storage   = this->runtime->create_buffer(compacted_size, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                compacted.structure = vk::raii::AccelerationStructureKHR{this->runtime->device, vk::AccelerationStructureCreateInfoKHR{{}, *compacted.storage.buffer, 0, compacted_size, vk::AccelerationStructureTypeKHR::eBottomLevel}};
                this->runtime->immediate([&result, &compacted](const vk::raii::CommandBuffer& command_buffer) { command_buffer.copyAccelerationStructureKHR(vk::CopyAccelerationStructureInfoKHR{*result.structure, *compacted.structure, vk::CopyAccelerationStructureModeKHR::eCompact}); });
                result = std::move(compacted);
            }
        }
        result.address = this->runtime->device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.structure});
        return result;
    }

    GpuAccelerationStructure GpuScene::build_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances) {
        const vk::AccelerationStructureGeometryInstancesDataKHR instance_data{
            vk::False,
            vk::DeviceOrHostAddressConstKHR{this->acceleration_structure_instances.address},
        };
        const vk::AccelerationStructureGeometryKHR geometry{
            vk::GeometryTypeKHR::eInstances,
            vk::AccelerationStructureGeometryDataKHR{instance_data},
        };
        const std::uint32_t primitive_count = static_cast<std::uint32_t>(instances.size());
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eTopLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eBuild,
            {},
            {},
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->runtime->device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);

        GpuAccelerationStructure result{};
        result.storage   = this->runtime->create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.structure = vk::raii::AccelerationStructureKHR{
            this->runtime->device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eTopLevel},
        };
        build_info.dstAccelerationStructure = *result.structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_scratch(sizes.buildScratchSize, true)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        this->runtime->immediate([&build_info, &ranges](const vk::raii::CommandBuffer& command_buffer) {
            const vk::MemoryBarrier2 blas_build_dependency{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &blas_build_dependency});
            command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        });
        result.address = this->runtime->device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.structure});
        return result;
    }
} // namespace spectra::render
