module;

#include <Windows.h>

#include <exr.h>

#undef interface

module spectra.render;

import :common;

import std;
import vulkan;

namespace spectra {
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

    GpuScene::GpuScene(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, std::filesystem::path shader_directory) noexcept : context{runtime, document, dynamics, std::move(shader_directory)} {}

    GpuScene::~GpuScene() {
        this->destroy();
    }

    namespace {
        [[nodiscard]] std::string texture_cache_key(const scene::Texture& texture) {
            const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
            const std::string identity       = image.asset.content_hash.empty() ? std::format("memory:{}:{}:{}", texture.id.value, texture.revision.content, texture.revision.topology) : image.asset.content_hash;
            return std::format("{}:{}:{}:{}", identity, std::to_underlying(image.wrap), std::to_underlying(image.filter), std::bit_cast<std::uint32_t>(image.maximum_anisotropy));
        }

    } // namespace

    GpuTextureImage upload_texture_image(VulkanRuntime& runtime, const scene::ImageTexture& data, const vk::Format format, const vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer* command_buffer) {
        GpuTextureImage result{runtime.resources.create_image_2d({data.width, data.height}, format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::ImageAspectFlagBits::eColor, static_cast<std::uint32_t>(data.mip_offsets.size())), runtime.resources.allocate_resource_descriptor(), runtime.resources.allocate_sampler_descriptor()};
        runtime.resources.write_sampled_image_descriptor(result.image_descriptor, result.image, vk::ImageLayout::eShaderReadOnlyOptimal);
        const vk::SamplerAddressMode address_mode = data.wrap == scene::TextureWrapMode::Repeat ? vk::SamplerAddressMode::eRepeat : data.wrap == scene::TextureWrapMode::Clamp ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eClampToBorder;
        const bool linear                         = data.filter != scene::TextureFilter::Point;
        runtime.resources.write_sampler_descriptor(result.sampler_descriptor, vk::SamplerCreateInfo{{}, linear ? vk::Filter::eLinear : vk::Filter::eNearest, linear ? vk::Filter::eLinear : vk::Filter::eNearest, data.filter == scene::TextureFilter::Trilinear || data.filter == scene::TextureFilter::Ewa ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest, address_mode, address_mode, address_mode, 0.0f, data.filter == scene::TextureFilter::Ewa ? vk::True : vk::False, data.maximum_anisotropy, vk::False, vk::CompareOp::eNever, 0.0f, static_cast<float>(data.mip_offsets.size() - 1u), vk::BorderColor::eFloatTransparentBlack});
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
            staging    = runtime.resources.create_buffer(texels.size() * sizeof(std::uint16_t), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, texels.data(), texels.size() * sizeof(std::uint16_t));
        } else if (format == vk::Format::eR32G32B32A32Sfloat) {
            texel_size = sizeof(math::Float4);
            staging    = runtime.resources.create_buffer(data.texels.size() * texel_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
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
            runtime.frames.defer_destruction([upload = std::move(staging)]() mutable {});
        } else
            runtime.resources.submit_immediate(record);
        return result;
    }

    namespace {
        [[nodiscard]] vk::AccelerationStructureGeometryKHR triangle_geometry(const GpuGeometry& mesh) {
            const vk::AccelerationStructureGeometryTrianglesDataKHR triangles{
                vk::Format::eR32G32B32Sfloat,
                vk::DeviceOrHostAddressConstKHR{mesh.positions.address},
                sizeof(math::Float3),
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
            const vk::AccelerationStructureGeometryAabbsDataKHR aabbs{vk::DeviceOrHostAddressConstKHR{mesh.axis_aligned_boxes.address}, sizeof(vk::AabbPositionsKHR)};
            return {
                vk::GeometryTypeKHR::eAabbs,
                vk::AccelerationStructureGeometryDataKHR{aabbs},
                {},
            };
        }

        [[nodiscard]] scene::TriangleMeshGeometry tessellate_geometry(const scene::Geometry& geometry) {
            if (const scene::TriangleMeshGeometry* mesh = std::get_if<scene::TriangleMeshGeometry>(&geometry.data)) return *mesh;
            scene::TriangleMeshGeometry result{};
            const auto vertex = [&result](const math::Float3 position, const math::Float3 normal, const math::Float3 tangent, const math::Float2 uv) {
                result.positions.push_back(position);
                result.normals.push_back(normal);
                result.tangents.push_back(tangent);
                result.texture_coordinates.push_back(uv);
                return static_cast<std::uint32_t>(result.positions.size() - 1u);
            };
            if (const scene::BoxGeometry* box = std::get_if<scene::BoxGeometry>(&geometry.data)) {
                const math::Float3 minimum = box->bounds.minimum;
                const math::Float3 maximum = box->bounds.maximum;
                const std::array positions{std::array{math::Float3{minimum.x, minimum.y, minimum.z}, math::Float3{maximum.x, minimum.y, minimum.z}, math::Float3{maximum.x, maximum.y, minimum.z}, math::Float3{minimum.x, maximum.y, minimum.z}}, std::array{math::Float3{minimum.x, minimum.y, maximum.z}, math::Float3{minimum.x, maximum.y, maximum.z}, math::Float3{maximum.x, maximum.y, maximum.z}, math::Float3{maximum.x, minimum.y, maximum.z}}, std::array{math::Float3{minimum.x, minimum.y, minimum.z}, math::Float3{minimum.x, minimum.y, maximum.z}, math::Float3{maximum.x, minimum.y, maximum.z}, math::Float3{maximum.x, minimum.y, minimum.z}}, std::array{math::Float3{minimum.x, maximum.y, minimum.z}, math::Float3{maximum.x, maximum.y, minimum.z}, math::Float3{maximum.x, maximum.y, maximum.z}, math::Float3{minimum.x, maximum.y, maximum.z}},
                    std::array{math::Float3{minimum.x, minimum.y, minimum.z}, math::Float3{minimum.x, maximum.y, minimum.z}, math::Float3{minimum.x, maximum.y, maximum.z}, math::Float3{minimum.x, minimum.y, maximum.z}}, std::array{math::Float3{maximum.x, minimum.y, minimum.z}, math::Float3{maximum.x, minimum.y, maximum.z}, math::Float3{maximum.x, maximum.y, maximum.z}, math::Float3{maximum.x, maximum.y, minimum.z}}};
                const std::array normals{math::Float3{0.0f, 0.0f, -1.0f}, math::Float3{0.0f, 0.0f, 1.0f}, math::Float3{0.0f, -1.0f, 0.0f}, math::Float3{0.0f, 1.0f, 0.0f}, math::Float3{-1.0f, 0.0f, 0.0f}, math::Float3{1.0f, 0.0f, 0.0f}};
                const std::array tangents{math::Float3{1.0f, 0.0f, 0.0f}, math::Float3{-1.0f, 0.0f, 0.0f}, math::Float3{1.0f, 0.0f, 0.0f}, math::Float3{1.0f, 0.0f, 0.0f}, math::Float3{0.0f, 1.0f, 0.0f}, math::Float3{0.0f, -1.0f, 0.0f}};
                constexpr std::array texture_coordinates{math::Float2{0.0f, 0.0f}, math::Float2{1.0f, 0.0f}, math::Float2{1.0f, 1.0f}, math::Float2{0.0f, 1.0f}};
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
                        const math::Float3 position{radial * std::cos(phi), radial * std::sin(phi), z};
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
                const math::Float3 normal{std::cos(phi), std::sin(phi), 0.0f};
                for (std::uint32_t end = 0; end != 2; ++end) vertex({cylinder.radius * normal.x, cylinder.radius * normal.y, end == 0 ? cylinder.z_min : cylinder.z_max}, normal, {-normal.y, normal.x, 0.0f}, {u, static_cast<float>(end)});
            }
            for (std::uint32_t segment = 0; segment != segments; ++segment) {
                const std::uint32_t first = segment * 2u;
                result.indices.insert(result.indices.end(), {first, first + 1u, first + 3u, first, first + 3u, first + 2u});
            }
            return result;
        }

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(VulkanRuntime& runtime, const std::span<const Element> elements, const vk::BufferUsageFlags usage, const std::size_t element_capacity = 0) {
            GpuBuffer staging = runtime.resources.create_buffer(elements.size_bytes(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, elements.data(), elements.size_bytes());
            GpuBuffer destination = runtime.resources.create_buffer(std::max(elements.size(), element_capacity) * sizeof(Element), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            runtime.resources.submit_immediate([&staging, &destination, usage](const vk::raii::CommandBuffer& command_buffer) {
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
        [[nodiscard]] GpuBuffer upload_buffer(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage, const std::size_t element_capacity = 0) {
            GpuBuffer destination       = runtime.resources.create_buffer(std::max(elements.size(), element_capacity) * sizeof(Element), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload = runtime.frames.stage_upload(std::as_bytes(elements));
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

    void GpuScene::initialize(const scene::Scene& source_scene, const std::span<const GpuGeometryBinding> geometry_bindings, const std::span<const std::pair<scene::ParticleSetId, std::uint32_t>> particle_capacities, const std::span<const scene::InstanceId> hidden_instances) {
        this->context.document.content.evaluated = source_scene;
        this->resources.geometry_bindings        = {geometry_bindings.begin(), geometry_bindings.end()};
        this->resources.instance_transforms.reserve(source_scene.resources.instances.size());
        for (const scene::Instance& instance : source_scene.resources.instances) this->resources.instance_transforms.emplace_back(instance.id, instance.transform);
        for (scene::Instance& instance : this->context.document.content.evaluated.resources.instances)
            if (std::ranges::contains(hidden_instances, instance.id)) instance.visible = false;
        const scene::SceneView scene = this->context.document.content.evaluated.view();
        const auto create_shader     = [this](const std::string_view file, const char* entry) {
            const std::vector<std::uint32_t> code = load_spirv(this->context.shader_directory / file);
            return vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), entry}};
        };
        this->resources.attribute_clear_shader         = create_shader("dynamic_mesh_attribute_clear.spv", "clear_dynamic_attributes");
        this->resources.attribute_accumulation_shader  = create_shader("dynamic_mesh_attribute_accumulation.spv", "accumulate_dynamic_attributes");
        this->resources.attribute_normalization_shader = create_shader("dynamic_mesh_attribute_normalization.spv", "normalize_dynamic_attributes");
        this->resources.particle_material_shader       = create_shader("particle_material.spv", "map_particle_materials");
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
        this->resources.particle_material_lookup_count      = static_cast<std::uint32_t>(scene.resources.materials.size());
        this->resources.particle_material_lookup_buffer     = upload_buffer(this->context.runtime, std::span<const std::array<std::uint32_t, 4>>{material_lookup}, vk::BufferUsageFlagBits::eStorageBuffer);
        this->resources.particle_material_lookup_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(this->resources.particle_material_lookup_descriptor, vk::DescriptorType::eStorageBuffer, this->resources.particle_material_lookup_buffer);
        this->cache_texture_images(scene);
        this->resources.geometries.reserve(scene.resources.geometries.size());
        for (const scene::Geometry& geometry : scene.resources.geometries) this->resources.geometries.emplace_back(this->create_geometry(geometry));
        this->resources.particle_sets.reserve(scene.resources.particle_sets.size());
        for (const scene::ParticleSet& particles : scene.resources.particle_sets) {
            const auto capacity = std::ranges::find(particle_capacities, particles.id, &std::pair<scene::ParticleSetId, std::uint32_t>::first);
            this->resources.particle_sets.emplace_back(this->create_particle_set(particles, scene, nullptr, capacity == particle_capacities.end() ? 0 : capacity->second));
        }
        this->resources.volumes.reserve(scene.resources.volumes.size());
        for (const scene::Volume& volume : scene.resources.volumes) this->resources.volumes.emplace_back(this->create_volume(volume));

        const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
        const std::array<vk::AccelerationStructureInstanceKHR, 1> empty_instance_storage{};
        this->resources.acceleration_structure_instances = upload_buffer(this->context.runtime, instances.empty() ? std::span<const vk::AccelerationStructureInstanceKHR>{empty_instance_storage} : std::span<const vk::AccelerationStructureInstanceKHR>{instances}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        this->resources.top_level_acceleration_structure = this->build_top_level(instances);
        this->resources.synchronized_revision            = scene.revision;
        this->resources.initialized                      = true;
    }

    void GpuScene::destroy() noexcept {
        if (!this->resources.initialized) return;
        for (const GpuVolumeVelocityStorage& storage : this->resources.volume_velocity_storage) this->context.runtime.frames.retire_resource_descriptor(storage.velocity_descriptor);
        this->context.runtime.frames.retire_resource_descriptor(this->resources.particle_material_lookup_descriptor);
        for (const GpuTextureImage& image : this->resources.texture_images) {
            this->context.runtime.frames.retire_resource_descriptor(image.image_descriptor);
            this->context.runtime.frames.retire_sampler_descriptor(image.sampler_descriptor);
        }
        for (const GpuGeometry& mesh : this->resources.geometries) {
            this->context.runtime.frames.retire_resource_descriptor(mesh.positions_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(mesh.normals_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(mesh.tangents_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(mesh.texture_coordinates_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(mesh.indices_descriptor);
        }
        for (const GpuParticleSet& particles : this->resources.particle_sets) {
            this->context.runtime.frames.retire_resource_descriptor(particles.positions_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.radii_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.velocities_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.colors_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.temperatures_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.materials_descriptor);
        }
        for (const GpuVolume& volume : this->resources.volumes)
            for (std::size_t field = 0; field != volume.fields.size(); ++field)
                if (volume.field_present[field]) this->context.runtime.frames.retire_resource_descriptor(volume.descriptors[field]);
        this->resources.attribute_clear_shader          = nullptr;
        this->resources.attribute_accumulation_shader   = nullptr;
        this->resources.attribute_normalization_shader  = nullptr;
        this->resources.particle_material_shader        = nullptr;
        this->resources.particle_material_lookup_buffer = {};
        this->resources.texture_image_indices.clear();
        this->resources.texture_images.clear();
        this->resources.volume_velocity_storage.clear();
        this->resources.acceleration_structure_instances = {};
        this->resources.immediate_scratch                = {};
        this->resources.frame_scratch                    = {};
        this->resources.scratch_offsets                  = {};
        this->resources.instance_transforms.clear();
        this->resources.external_geometries.clear();
        this->resources.external_particle_sets.clear();
        this->resources.external_volumes.clear();
        this->resources.geometry_bindings.clear();
        this->resources.geometries.clear();
        this->resources.particle_sets.clear();
        this->resources.volumes.clear();
        this->resources.volume_velocity_fields.clear();
        this->resources.primitives.clear();
        this->resources.acceleration_primitive_indices.clear();
        this->resources.primitive_instance_ids.clear();
        this->resources.acceleration_instance_ids.clear();
        this->resources.top_level_acceleration_structure = {};
        this->context.document.content.evaluated         = {};
        this->resources.initialized                      = false;
    }

    void GpuScene::cache_texture_images(const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer) {
        for (const scene::Texture& texture : scene.resources.textures) {
            const scene::ImageTexture* image = std::get_if<scene::ImageTexture>(&texture.data);
            if (!image) continue;
            const vk::Format format = texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
            const std::pair key{texture_cache_key(texture), format};
            if (this->resources.texture_image_indices.contains(key)) continue;
            const std::size_t index = this->resources.texture_images.size();
            this->resources.texture_images.emplace_back(upload_texture_image(this->context.runtime, *image, format, vk::PipelineStageFlagBits2::eAllCommands, command_buffer));
            this->resources.texture_image_indices.emplace(key, index);
        }
    }

    const GpuTextureImage& GpuScene::texture_image(const scene::Texture& texture, const vk::Format format) const {
        return this->resources.texture_images[this->resources.texture_image_indices.at({texture_cache_key(texture), format})];
    }

    FrozenSceneSnapshot GpuScene::record_frozen_scene_snapshot(const vk::raii::CommandBuffer& command_buffer, const scene::Camera& camera, const vk::Extent2D extent, const float exposure) {
        FrozenSceneSnapshot snapshot{};
        snapshot.frozen_scene = this->context.document.content.evaluated;
        snapshot.frozen_scene.dynamic_setup.reset();
        scene::Camera snapshot_camera = camera;
        snapshot_camera.id            = {std::ranges::fold_left(snapshot.frozen_scene.resources.cameras, std::uint64_t{}, [](const std::uint64_t maximum, const scene::Camera& value) { return std::max(maximum, value.id.value); }) + 1};
        snapshot_camera.name          = "Snapshot Camera";
        snapshot_camera.revision      = {};
        snapshot.frozen_scene.resources.cameras.push_back(snapshot_camera);
        snapshot.frozen_scene.active_camera = snapshot_camera.id;
        scene::Film snapshot_film           = snapshot.frozen_scene.film();
        snapshot_film.id                    = {std::ranges::fold_left(snapshot.frozen_scene.resources.films, std::uint64_t{}, [](const std::uint64_t maximum, const scene::Film& value) { return std::max(maximum, value.id.value); }) + 1};
        snapshot_film.name                  = "Snapshot Film";
        snapshot_film.revision              = {};
        snapshot_film.resolution            = {extent.width, extent.height};
        snapshot_film.pixel_minimum         = {};
        snapshot_film.pixel_maximum         = snapshot_film.resolution;
        snapshot_film.exposure += exposure;
        snapshot.frozen_scene.resources.films.push_back(snapshot_film);
        snapshot.frozen_scene.active_film = snapshot_film.id;

        vk::DeviceSize size{};
        struct ReadbackCopy {
            const GpuBuffer* source{};
            vk::BufferCopy region{};
        };
        std::vector<ReadbackCopy> readback_copies{};
        for (const GpuGeometry& geometry : this->resources.geometries) {
            if (!geometry.cpu_data_stale) continue;
            const std::uint32_t resource   = static_cast<std::uint32_t>(std::ranges::find(this->context.document.content.evaluated.resources.geometries, geometry.geometry_id, &scene::Geometry::id) - this->context.document.content.evaluated.resources.geometries.begin());
            const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                if (count == 0) return;
                size                       = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes = count * element_size;
                snapshot.readback_regions.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            add_readback_region(FrozenSceneReadbackKind::GeometryPosition, geometry.positions, geometry.vertex_count, sizeof(math::Float3));
            if ((geometry.attribute_mask & 1u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryNormal, geometry.normals, geometry.vertex_count, sizeof(math::Float3));
            if ((geometry.attribute_mask & 2u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTangent, geometry.tangents, geometry.vertex_count, sizeof(math::Float3));
            if ((geometry.attribute_mask & 4u) != 0) add_readback_region(FrozenSceneReadbackKind::GeometryTextureCoordinate, geometry.texture_coordinates, geometry.vertex_count, sizeof(math::Float2));
            add_readback_region(FrozenSceneReadbackKind::GeometryIndex, geometry.indices, geometry.index_count, sizeof(std::uint32_t));
        }
        for (const GpuParticleSet& particles : this->resources.particle_sets) {
            if (!particles.cpu_data_stale) continue;
            const std::uint32_t resource   = static_cast<std::uint32_t>(std::ranges::find(this->context.document.content.evaluated.resources.particle_sets, particles.particle_set_id, &scene::ParticleSet::id) - this->context.document.content.evaluated.resources.particle_sets.begin());
            const auto add_readback_region = [&snapshot, &readback_copies, &size, resource](const FrozenSceneReadbackKind kind, const GpuBuffer& source, const std::uint64_t count, const vk::DeviceSize element_size) {
                if (count == 0) return;
                size                       = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes = count * element_size;
                snapshot.readback_regions.emplace_back(kind, resource, GpuVolumeField::Density, size, count);
                readback_copies.emplace_back(&source, vk::BufferCopy{0, size, bytes});
                size += bytes;
            };
            add_readback_region(FrozenSceneReadbackKind::ParticlePosition, particles.positions, particles.particle_count, sizeof(math::Float3));
            add_readback_region(FrozenSceneReadbackKind::ParticleRadius, particles.radii, particles.particle_count, sizeof(float));
            if ((particles.attribute_mask & 1u) != 0) add_readback_region(FrozenSceneReadbackKind::ParticleVelocity, particles.velocities, particles.particle_count, sizeof(math::Float3));
            if ((particles.attribute_mask & 2u) != 0) add_readback_region(FrozenSceneReadbackKind::ParticleColor, particles.colors, particles.particle_count, sizeof(math::Float3));
            if ((particles.attribute_mask & 4u) != 0) add_readback_region(FrozenSceneReadbackKind::ParticleTemperature, particles.temperatures, particles.particle_count, sizeof(float));
            if ((particles.attribute_mask & 8u) != 0) add_readback_region(FrozenSceneReadbackKind::ParticleMaterial, particles.materials, particles.particle_count, sizeof(std::uint32_t));
        }
        for (const GpuVolume& volume : this->resources.volumes) {
            if (!volume.cpu_data_stale) continue;
            const std::uint32_t resource = static_cast<std::uint32_t>(std::ranges::find(this->context.document.content.evaluated.resources.volumes, volume.volume_id, &scene::Volume::id) - this->context.document.content.evaluated.resources.volumes.begin());
            for (std::size_t field = 0; field != volume.fields.size(); ++field) {
                if (!volume.field_present[field]) continue;
                const GpuVolumeField kind         = static_cast<GpuVolumeField>(field);
                const vk::DeviceSize element_size = kind == GpuVolumeField::SigmaA || kind == GpuVolumeField::SigmaS || kind == GpuVolumeField::Emission ? sizeof(math::Float3) : kind == GpuVolumeField::NanoVdbDensity || kind == GpuVolumeField::NanoVdbTemperature ? sizeof(std::uint32_t) : sizeof(float);
                const std::uint64_t count         = volume.fields[field].size / element_size;
                size                              = (size + 15u) & ~vk::DeviceSize{15u};
                const vk::DeviceSize bytes        = count * element_size;
                snapshot.readback_regions.emplace_back(FrozenSceneReadbackKind::VolumeField, resource, kind, size, count);
                readback_copies.emplace_back(&volume.fields[field], vk::BufferCopy{0, size, bytes});
                size += bytes;
            }
        }
        if (size != 0) {
            snapshot.readback_buffer = this->context.runtime.resources.create_buffer(size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            for (const ReadbackCopy& copy : readback_copies) command_buffer.copyBuffer(*copy.source->buffer, *snapshot.readback_buffer.buffer, copy.region);
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        }
        snapshot.frozen_scene.mark_all_changed();
        return snapshot;
    }

    void FrozenSceneSnapshot::materialize() {
        const auto copy = [this]<class Element>(std::vector<Element>& destination, const FrozenSceneReadbackRegion& source) {
            destination.resize(source.element_count);
            std::memcpy(destination.data(), static_cast<const std::byte*>(this->readback_buffer.mapped) + source.offset, source.element_count * sizeof(Element));
        };
        for (const FrozenSceneReadbackRegion& source : this->readback_regions) switch (source.kind) {
            case FrozenSceneReadbackKind::GeometryPosition: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).positions, source); break;
            case FrozenSceneReadbackKind::GeometryNormal: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).normals, source); break;
            case FrozenSceneReadbackKind::GeometryTangent: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).tangents, source); break;
            case FrozenSceneReadbackKind::GeometryTextureCoordinate: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).texture_coordinates, source); break;
            case FrozenSceneReadbackKind::GeometryIndex: copy(std::get<scene::TriangleMeshGeometry>(this->frozen_scene.resources.geometries[source.resource_index].data).indices, source); break;
            case FrozenSceneReadbackKind::ParticlePosition: copy(this->frozen_scene.resources.particle_sets[source.resource_index].positions, source); break;
            case FrozenSceneReadbackKind::ParticleRadius: copy(this->frozen_scene.resources.particle_sets[source.resource_index].radii, source); break;
            case FrozenSceneReadbackKind::ParticleVelocity: copy(this->frozen_scene.resources.particle_sets[source.resource_index].velocities, source); break;
            case FrozenSceneReadbackKind::ParticleColor: copy(this->frozen_scene.resources.particle_sets[source.resource_index].colors, source); break;
            case FrozenSceneReadbackKind::ParticleTemperature: copy(this->frozen_scene.resources.particle_sets[source.resource_index].temperatures, source); break;
            case FrozenSceneReadbackKind::ParticleMaterial:
                {
                    std::vector<std::uint32_t> material_indices{};
                    copy(material_indices, source);
                    std::vector<scene::MaterialId>& materials = this->frozen_scene.resources.particle_sets[source.resource_index].particle_materials;
                    materials.clear();
                    materials.reserve(material_indices.size());
                    for (const std::uint32_t index : material_indices) materials.push_back(this->frozen_scene.resources.materials[index].id);
                }
                break;
            case FrozenSceneReadbackKind::VolumeField:
                {
                    scene::Volume& volume = this->frozen_scene.resources.volumes[source.resource_index];
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
        this->frozen_scene.mark_all_changed();
    }

    GpuGeometry GpuScene::create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer* command_buffer) {
        const scene::TriangleMeshGeometry mesh = tessellate_geometry(geometry);
        GpuGeometry result{};
        result.geometry_id                  = geometry.id;
        const auto binding                  = std::ranges::find(this->resources.geometry_bindings, geometry.id, &GpuGeometryBinding::geometry_id);
        result.update_mode                  = binding == this->resources.geometry_bindings.end() ? GpuMeshUpdateMode::Immutable : binding->update_mode;
        result.acceleration_kind            = std::holds_alternative<scene::SphereGeometry>(geometry.data) || std::holds_alternative<scene::DiskGeometry>(geometry.data) || std::holds_alternative<scene::CylinderGeometry>(geometry.data) ? AccelerationGeometryKind::Procedural : AccelerationGeometryKind::Triangle;
        result.vertex_count                 = static_cast<std::uint32_t>(mesh.positions.size());
        result.index_count                  = static_cast<std::uint32_t>(mesh.indices.size());
        result.vertex_capacity              = result.vertex_count;
        result.index_capacity               = result.index_count;
        result.acceleration_primitive_count = result.acceleration_kind == AccelerationGeometryKind::Triangle ? result.index_count / 3u : 1u;
        result.attribute_mask               = (mesh.normals.empty() ? 0u : 1u) | (mesh.tangents.empty() ? 0u : 2u) | (mesh.texture_coordinates.empty() ? 0u : 4u);
        const std::array<math::Float3, 1> missing_float3{};
        const std::vector<math::Float3> missing_dynamic_float3(result.update_mode == GpuMeshUpdateMode::Immutable ? 0 : mesh.positions.size());
        const std::array<math::Float2, 1> missing_float2{};
        const std::span<const math::Float3> positions{mesh.positions};
        const std::span<const math::Float3> normals             = mesh.normals.empty() ? missing_dynamic_float3.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{missing_dynamic_float3} : std::span<const math::Float3>{mesh.normals};
        const std::span<const math::Float3> tangents            = mesh.tangents.empty() ? missing_dynamic_float3.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{missing_dynamic_float3} : std::span<const math::Float3>{mesh.tangents};
        const std::span<const math::Float2> texture_coordinates = mesh.texture_coordinates.empty() ? std::span<const math::Float2>{missing_float2} : std::span<const math::Float2>{mesh.texture_coordinates};
        const std::span<const std::uint32_t> indices{mesh.indices};
        const vk::BufferUsageFlags position_usage  = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer;
        const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer;
        result.positions                           = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, positions, position_usage) : upload_buffer(this->context.runtime, positions, position_usage);
        result.normals                             = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, normals, attribute_usage) : upload_buffer(this->context.runtime, normals, attribute_usage);
        result.tangents                            = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, tangents, attribute_usage) : upload_buffer(this->context.runtime, tangents, attribute_usage);
        result.texture_coordinates                 = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, texture_coordinates, attribute_usage) : upload_buffer(this->context.runtime, texture_coordinates, attribute_usage);
        result.indices                             = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, indices, position_usage) : upload_buffer(this->context.runtime, indices, position_usage);
        if (result.acceleration_kind == AccelerationGeometryKind::Procedural) {
            const math::Bounds3 bounds = scene::geometry_bounds(geometry);
            const std::array aabbs{vk::AabbPositionsKHR{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}};
            result.axis_aligned_boxes = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, std::span<const vk::AabbPositionsKHR>{aabbs}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) : upload_buffer(this->context.runtime, std::span<const vk::AabbPositionsKHR>{aabbs}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        }
        result.positions_descriptor           = this->context.runtime.resources.allocate_resource_descriptor();
        result.normals_descriptor             = this->context.runtime.resources.allocate_resource_descriptor();
        result.tangents_descriptor            = this->context.runtime.resources.allocate_resource_descriptor();
        result.texture_coordinates_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        result.indices_descriptor             = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->context.runtime.resources.write_buffer_descriptor(result.normals_descriptor, vk::DescriptorType::eStorageBuffer, result.normals);
        this->context.runtime.resources.write_buffer_descriptor(result.tangents_descriptor, vk::DescriptorType::eStorageBuffer, result.tangents);
        this->context.runtime.resources.write_buffer_descriptor(result.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, result.texture_coordinates);
        this->context.runtime.resources.write_buffer_descriptor(result.indices_descriptor, vk::DescriptorType::eStorageBuffer, result.indices);
        if (command_buffer && result.update_mode != GpuMeshUpdateMode::Immutable && (mesh.normals.empty() || mesh.tangents.empty())) this->generate_dynamic_attributes(result, mesh.normals.empty(), mesh.tangents.empty(), *command_buffer);
        result.bottom_level_acceleration_structure = this->build_bottom_level(result.acceleration_kind == AccelerationGeometryKind::Triangle ? triangle_geometry(result) : procedural_geometry(result), result.acceleration_primitive_count, result.update_mode, command_buffer);
        result.cpu_data_stale                      = command_buffer && result.update_mode != GpuMeshUpdateMode::Immutable;
        return result;
    }

    GpuParticleSet GpuScene::create_particle_set(const scene::ParticleSet& particles, const scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer, const std::uint32_t capacity) {
        GpuParticleSet result{};
        result.particle_set_id   = particles.id;
        result.particle_count    = static_cast<std::uint32_t>(particles.positions.size());
        result.particle_capacity = std::max(result.particle_count, capacity);
        result.attribute_mask    = (particles.velocities.empty() ? 0u : 1u) | (particles.colors.empty() ? 0u : 2u) | (particles.temperatures.empty() ? 0u : 4u) | (particles.particle_materials.empty() ? 0u : 8u);
        const std::array<math::Float3, 1> missing_float3{};
        const std::span<const math::Float3> velocities = particles.velocities.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{particles.velocities};
        const std::span<const math::Float3> colors     = particles.colors.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{particles.colors};
        const std::array<float, 1> missing_float{};
        const std::span<const float> temperatures = particles.temperatures.empty() ? std::span<const float>{missing_float} : std::span<const float>{particles.temperatures};
        std::vector<std::uint32_t> materials{};
        materials.reserve(std::max<std::size_t>(particles.particle_materials.size(), 1));
        for (const scene::MaterialId material : particles.particle_materials) materials.push_back(static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, material, &scene::Material::id) - scene.resources.materials.begin()));
        if (materials.empty()) materials.emplace_back();
        const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer;
        result.positions                           = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, std::span<const math::Float3>{particles.positions}, attribute_usage, result.particle_capacity) : upload_buffer(this->context.runtime, std::span<const math::Float3>{particles.positions}, attribute_usage, result.particle_capacity);
        result.radii                               = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, std::span<const float>{particles.radii}, attribute_usage, result.particle_capacity) : upload_buffer(this->context.runtime, std::span<const float>{particles.radii}, attribute_usage, result.particle_capacity);
        result.velocities                          = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, velocities, attribute_usage, particles.velocities.empty() ? 0 : result.particle_capacity) : upload_buffer(this->context.runtime, velocities, attribute_usage, particles.velocities.empty() ? 0 : result.particle_capacity);
        result.colors                              = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, colors, attribute_usage, particles.colors.empty() ? 0 : result.particle_capacity) : upload_buffer(this->context.runtime, colors, attribute_usage, particles.colors.empty() ? 0 : result.particle_capacity);
        result.temperatures                        = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, temperatures, attribute_usage, particles.temperatures.empty() ? 0 : result.particle_capacity) : upload_buffer(this->context.runtime, temperatures, attribute_usage, particles.temperatures.empty() ? 0 : result.particle_capacity);
        result.materials                           = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, std::span<const std::uint32_t>{materials}, attribute_usage, particles.particle_materials.empty() ? 0 : result.particle_capacity) : upload_buffer(this->context.runtime, std::span<const std::uint32_t>{materials}, attribute_usage, particles.particle_materials.empty() ? 0 : result.particle_capacity);
        result.positions_descriptor                = this->context.runtime.resources.allocate_resource_descriptor();
        result.radii_descriptor                    = this->context.runtime.resources.allocate_resource_descriptor();
        result.velocities_descriptor               = this->context.runtime.resources.allocate_resource_descriptor();
        result.colors_descriptor                   = this->context.runtime.resources.allocate_resource_descriptor();
        result.temperatures_descriptor             = this->context.runtime.resources.allocate_resource_descriptor();
        result.materials_descriptor                = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->context.runtime.resources.write_buffer_descriptor(result.radii_descriptor, vk::DescriptorType::eStorageBuffer, result.radii);
        this->context.runtime.resources.write_buffer_descriptor(result.velocities_descriptor, vk::DescriptorType::eStorageBuffer, result.velocities);
        this->context.runtime.resources.write_buffer_descriptor(result.colors_descriptor, vk::DescriptorType::eStorageBuffer, result.colors);
        this->context.runtime.resources.write_buffer_descriptor(result.temperatures_descriptor, vk::DescriptorType::eStorageBuffer, result.temperatures);
        this->context.runtime.resources.write_buffer_descriptor(result.materials_descriptor, vk::DescriptorType::eStorageBuffer, result.materials);
        return result;
    }

    GpuVolume GpuScene::create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer* command_buffer) {
        GpuVolume result{};
        result.volume_id  = volume.id;
        result.revision   = volume.revision;
        const auto upload = [this, command_buffer, &result](const GpuVolumeField field, const auto values) {
            if (values.empty()) return;
            const std::size_t index   = std::to_underlying(field);
            result.fields[index]      = command_buffer ? upload_buffer(this->context.runtime, *command_buffer, values, vk::BufferUsageFlagBits::eStorageBuffer) : upload_buffer(this->context.runtime, values, vk::BufferUsageFlagBits::eStorageBuffer);
            result.descriptors[index] = this->context.runtime.resources.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(result.descriptors[index], vk::DescriptorType::eStorageBuffer, result.fields[index]);
            result.field_present[index] = true;
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
                    upload(GpuVolumeField::SigmaA, std::span<const math::Float3>{data.sigma_a});
                    upload(GpuVolumeField::SigmaS, std::span<const math::Float3>{data.sigma_s});
                    upload(GpuVolumeField::Emission, std::span<const math::Float3>{data.emission});
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
        this->resources.primitives.clear();
        this->resources.primitives.reserve(primitive_count);
        this->resources.primitive_instance_ids.clear();
        this->resources.primitive_instance_ids.reserve(primitive_count);
        this->resources.acceleration_primitive_indices.clear();
        this->resources.acceleration_primitive_indices.reserve(primitive_count);
        this->resources.acceleration_instance_ids.clear();
        this->resources.acceleration_instance_ids.reserve(primitive_count);
        for (std::uint32_t instance_index = 0; instance_index < scene.resources.instances.size(); ++instance_index) {
            const scene::Instance& instance = scene.resources.instances[instance_index];
            if (!instance.visible) continue;
            const std::vector<scene::Prototype>::const_iterator prototype = std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            for (std::uint32_t primitive_index = 0; primitive_index < prototype->primitives.size(); ++primitive_index) {
                const scene::Primitive& primitive                           = prototype->primitives[primitive_index];
                const std::vector<GpuGeometry>::const_iterator mesh         = std::ranges::find(this->resources.geometries, primitive.geometry, &GpuGeometry::geometry_id);
                const std::vector<GpuParticleSet>::const_iterator particles = std::ranges::find(this->resources.particle_sets, primitive.particles, &GpuParticleSet::particle_set_id);
                if (mesh == this->resources.geometries.end() && particles == this->resources.particle_sets.end()) throw std::runtime_error("Every compiled surface Primitive requires a Geometry or Particle Set");

                const bool particle_draw                  = particles != this->resources.particle_sets.end();
                const std::uint32_t scene_primitive_index = static_cast<std::uint32_t>(this->resources.primitives.size());
                this->resources.primitives.emplace_back(particle_draw ? GpuScenePrimitiveKind::ParticleSet : GpuScenePrimitiveKind::Geometry, static_cast<std::uint32_t>(particle_draw ? particles - this->resources.particle_sets.begin() : mesh - this->resources.geometries.begin()), scene_primitive_index, instance_index, primitive_index);
                this->resources.primitive_instance_ids.push_back(instance.id);
                if (particle_draw) continue;

                const math::Transform world_transform = instance.transform * primitive.transform;
                vk::TransformMatrixKHR transform{};
                for (std::uint32_t row = 0; row < 3; ++row)
                    for (std::uint32_t column = 0; column < 4; ++column) transform.matrix[row][column] = world_transform.matrix[row * 4u + column];
                vk::GeometryInstanceFlagsKHR instance_flags = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
                if (primitive.alpha.value == 0) instance_flags |= vk::GeometryInstanceFlagBitsKHR::eForceOpaque;
                const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::Material::id);
                const bool volume_boundary                                  = (primitive.media.inside.value != 0 || primitive.media.outside.value != 0) && material != scene.resources.materials.end() && std::holds_alternative<scene::InterfaceMaterialData>(material->data);
                const std::uint32_t acceleration_index                      = static_cast<std::uint32_t>(instances.size());
                instances.emplace_back(transform, acceleration_index, volume_boundary ? 0x80u : 0x7fu, mesh->acceleration_kind == AccelerationGeometryKind::Procedural ? 1u : 0u, instance_flags, mesh->bottom_level_acceleration_structure.address);
                this->resources.acceleration_primitive_indices.push_back(scene_primitive_index);
                this->resources.acceleration_instance_ids.push_back(instance.id);
            }
        }
        return instances;
    }
    vk::DeviceAddress GpuScene::acquire_acceleration_scratch(const vk::DeviceSize size, const bool immediate) {
        const vk::DeviceSize alignment      = this->context.runtime.graphics.acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;
        GpuBuffer* buffer                   = immediate ? &this->resources.immediate_scratch : &this->resources.frame_scratch[this->context.runtime.frames.frame.current_slot_index];
        vk::DeviceSize* offset              = immediate ? nullptr : &this->resources.scratch_offsets[this->context.runtime.frames.frame.current_slot_index];
        const vk::DeviceSize current_offset = offset ? *offset : 0;
        vk::DeviceAddress address           = buffer->address + current_offset;
        address                             = (address + alignment - 1u) & ~(alignment - 1u);
        const bool available                = buffer->buffer != nullptr && address + size <= buffer->address + buffer->size;
        if (!available) {
            GpuBuffer replacement = this->context.runtime.resources.create_buffer(std::max(size + alignment - 1u, buffer->size * 2u), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            if (!immediate && buffer->buffer != nullptr) this->context.runtime.frames.defer_destruction([previous = std::move(*buffer)]() mutable {});
            *buffer = std::move(replacement);
            if (offset) *offset = 0;
            address = (buffer->address + alignment - 1u) & ~(alignment - 1u);
        }
        if (offset) *offset = address - buffer->address + size;
        return address;
    }

    void GpuScene::update_bottom_level(GpuGeometry& gpu_geometry, const scene::Geometry& source_geometry, const vk::raii::CommandBuffer& command_buffer) {
        const scene::TriangleMeshGeometry& triangle_mesh = std::get<scene::TriangleMeshGeometry>(source_geometry.data);
        const std::array<math::Float3, 1> missing_float3{};
        const std::array<math::Float2, 1> missing_float2{};
        const GpuUploadSlice position_upload                = this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Float3>{
            triangle_mesh.positions,
        }));
        const GpuUploadSlice normal_upload                  = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.normals.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{triangle_mesh.normals}));
        const GpuUploadSlice tangent_upload                 = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.tangents.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{triangle_mesh.tangents}));
        const GpuUploadSlice texture_coordinate_upload      = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.texture_coordinates.empty() ? std::span<const math::Float2>{missing_float2} : std::span<const math::Float2>{triangle_mesh.texture_coordinates}));
        const vk::AccelerationStructureGeometryKHR geometry = gpu_geometry.acceleration_kind == AccelerationGeometryKind::Triangle ? triangle_geometry(gpu_geometry) : procedural_geometry(gpu_geometry);
        const std::uint32_t primitive_count                 = gpu_geometry.acceleration_primitive_count;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *gpu_geometry.bottom_level_acceleration_structure.acceleration_structure,
            *gpu_geometry.bottom_level_acceleration_structure.acceleration_structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.copyBuffer(position_upload.buffer, *gpu_geometry.positions.buffer,
            vk::BufferCopy{
                position_upload.offset,
                0,
                position_upload.size,
            });
        command_buffer.copyBuffer(normal_upload.buffer, *gpu_geometry.normals.buffer,
            vk::BufferCopy{
                normal_upload.offset,
                0,
                normal_upload.size,
            });
        command_buffer.copyBuffer(tangent_upload.buffer, *gpu_geometry.tangents.buffer,
            vk::BufferCopy{
                tangent_upload.offset,
                0,
                tangent_upload.size,
            });
        command_buffer.copyBuffer(texture_coordinate_upload.buffer, *gpu_geometry.texture_coordinates.buffer,
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
                *gpu_geometry.positions.buffer,
                0,
                gpu_geometry.positions.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *gpu_geometry.normals.buffer,
                0,
                gpu_geometry.normals.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *gpu_geometry.tangents.buffer,
                0,
                gpu_geometry.tangents.size,
            },
            vk::BufferMemoryBarrier2{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *gpu_geometry.texture_coordinates.buffer,
                0,
                gpu_geometry.texture_coordinates.size,
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
        if (triangle_mesh.normals.empty() || triangle_mesh.tangents.empty()) this->generate_dynamic_attributes(gpu_geometry, triangle_mesh.normals.empty(), triangle_mesh.tangents.empty(), command_buffer);
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        gpu_geometry.cpu_data_stale = true;
    }

    void GpuScene::generate_dynamic_attributes(GpuGeometry& geometry, const bool generate_normals, const bool generate_tangents, const vk::raii::CommandBuffer& command_buffer) {
        struct alignas(16) DynamicMeshPushData {
            DescriptorHandle positions{};
            DescriptorHandle normals{};
            DescriptorHandle tangents{};
            DescriptorHandle texture_coordinates{};
            DescriptorHandle indices{};
            std::array<std::uint32_t, 2> reserved{};
            std::uint32_t vertex_count{};
            std::uint32_t index_count{};
            std::uint32_t attribute_flags{};
            std::uint32_t reserved_metadata{};
        };
        static_assert(sizeof(DynamicMeshPushData) == 64);
        const DynamicMeshPushData push_data{
            geometry.positions_descriptor,
            geometry.normals_descriptor,
            geometry.tangents_descriptor,
            geometry.texture_coordinates_descriptor,
            geometry.indices_descriptor,
            {},
            geometry.vertex_count,
            geometry.index_count,
            (generate_normals ? 1u : 0u) | (generate_tangents ? 2u : 0u) | ((geometry.attribute_mask & 4u) != 0 ? 4u : 0u),
            0,
        };
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.attribute_clear_shader);
        command_buffer.dispatch((geometry.vertex_count + 255u) / 256u, 1, 1);
        const vk::MemoryBarrier2 pass_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &pass_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.attribute_accumulation_shader);
        command_buffer.dispatch((geometry.index_count / 3u + 255u) / 256u, 1, 1);
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &pass_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.attribute_normalization_shader);
        command_buffer.dispatch((geometry.vertex_count + 255u) / 256u, 1, 1);
        const vk::MemoryBarrier2 output_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &output_dependency});
        if (generate_normals) geometry.attribute_mask |= 1u;
        if (generate_tangents) geometry.attribute_mask |= 2u;
    }

    void GpuScene::synchronize_external_geometry(const scene::GeometryId geometry_id, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, const std::uint32_t vertex_count, const std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer) {
        if (this->resources.external_geometries.empty()) this->resources.scratch_offsets[this->context.runtime.frames.frame.current_slot_index] = 0;
        GpuGeometry& mesh = *std::ranges::find(this->resources.geometries, geometry_id, &GpuGeometry::geometry_id);
        if (mesh.update_mode == GpuMeshUpdateMode::Immutable) throw std::runtime_error("CUDA External Geometry requires a dynamic update mode");
        const bool vertex_attribute = positions || normals || tangents || texture_coordinates;
        if (vertex_attribute && !positions && vertex_count != mesh.vertex_count) throw std::runtime_error("A TriangleMesh vertex count change must publish positions");
        const std::uint32_t updated_vertex_count = positions ? vertex_count : mesh.vertex_count;
        const std::uint32_t updated_index_count  = indices ? index_count : mesh.index_count;
        if (mesh.update_mode == GpuMeshUpdateMode::Deformable && (updated_vertex_count != mesh.vertex_count || indices)) throw std::runtime_error("CUDA External deformable Geometry changed topology");
        if (mesh.update_mode == GpuMeshUpdateMode::TopologyChanging) {
            const bool reallocate = updated_vertex_count > mesh.vertex_capacity || updated_index_count > mesh.index_capacity;
            if (updated_vertex_count > mesh.vertex_count && !texture_coordinates && (mesh.attribute_mask & 4u) != 0) throw std::runtime_error("CUDA External topology-changing Geometry must publish texture coordinates when adding textured vertices");
            if (reallocate) {
                GpuGeometry replacement{};
                replacement.geometry_id                    = mesh.geometry_id;
                replacement.update_mode                    = mesh.update_mode;
                replacement.acceleration_kind              = AccelerationGeometryKind::Triangle;
                replacement.vertex_count                   = updated_vertex_count;
                replacement.index_count                    = updated_index_count;
                replacement.vertex_capacity                = std::bit_ceil(std::max(updated_vertex_count, 1u));
                replacement.index_capacity                 = std::bit_ceil(std::max(updated_index_count, 3u));
                replacement.acceleration_primitive_count   = updated_index_count / 3u;
                replacement.attribute_mask                 = mesh.attribute_mask;
                const vk::BufferUsageFlags geometry_usage  = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
                const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
                replacement.positions                      = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.normals                        = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.tangents                       = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.texture_coordinates            = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float2), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.indices                        = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.index_capacity) * sizeof(std::uint32_t), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.positions_descriptor           = this->context.runtime.resources.allocate_resource_descriptor();
                replacement.normals_descriptor             = this->context.runtime.resources.allocate_resource_descriptor();
                replacement.tangents_descriptor            = this->context.runtime.resources.allocate_resource_descriptor();
                replacement.texture_coordinates_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
                replacement.indices_descriptor             = this->context.runtime.resources.allocate_resource_descriptor();
                this->context.runtime.resources.write_buffer_descriptor(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
                this->context.runtime.resources.write_buffer_descriptor(replacement.normals_descriptor, vk::DescriptorType::eStorageBuffer, replacement.normals);
                this->context.runtime.resources.write_buffer_descriptor(replacement.tangents_descriptor, vk::DescriptorType::eStorageBuffer, replacement.tangents);
                this->context.runtime.resources.write_buffer_descriptor(replacement.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, replacement.texture_coordinates);
                this->context.runtime.resources.write_buffer_descriptor(replacement.indices_descriptor, vk::DescriptorType::eStorageBuffer, replacement.indices);
                const std::uint32_t preserved_vertices = std::min(mesh.vertex_count, updated_vertex_count);
                if (preserved_vertices != 0 && !positions) command_buffer.copyBuffer(*mesh.positions.buffer, *replacement.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !normals && (mesh.attribute_mask & 1u) != 0) command_buffer.copyBuffer(*mesh.normals.buffer, *replacement.normals.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !tangents && (mesh.attribute_mask & 2u) != 0) command_buffer.copyBuffer(*mesh.tangents.buffer, *replacement.tangents.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !texture_coordinates && (mesh.attribute_mask & 4u) != 0) command_buffer.copyBuffer(*mesh.texture_coordinates.buffer, *replacement.texture_coordinates.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float2)});
                const std::uint32_t preserved_indices = std::min(mesh.index_count, updated_index_count);
                if (preserved_indices != 0 && !indices) command_buffer.copyBuffer(*mesh.indices.buffer, *replacement.indices.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_indices) * sizeof(std::uint32_t)});
                this->context.runtime.frames.retire_resource_descriptor(mesh.positions_descriptor);
                this->context.runtime.frames.retire_resource_descriptor(mesh.normals_descriptor);
                this->context.runtime.frames.retire_resource_descriptor(mesh.tangents_descriptor);
                this->context.runtime.frames.retire_resource_descriptor(mesh.texture_coordinates_descriptor);
                this->context.runtime.frames.retire_resource_descriptor(mesh.indices_descriptor);
                this->context.runtime.frames.defer_destruction([previous = std::move(mesh)]() mutable {});
                mesh                                     = std::move(replacement);
                this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Geometry;
            } else {
                mesh.vertex_count                 = updated_vertex_count;
                mesh.index_count                  = updated_index_count;
                mesh.acceleration_primitive_count = updated_index_count / 3u;
            }
        }

        const vk::DeviceSize position_bytes = static_cast<vk::DeviceSize>(updated_vertex_count) * sizeof(math::Float3);
        if (positions && updated_vertex_count != 0) command_buffer.copyBuffer(*positions->buffer, *mesh.positions.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (normals && updated_vertex_count != 0) command_buffer.copyBuffer(*normals->buffer, *mesh.normals.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (tangents && updated_vertex_count != 0) command_buffer.copyBuffer(*tangents->buffer, *mesh.tangents.buffer, vk::BufferCopy{0, 0, position_bytes});
        if (texture_coordinates && updated_vertex_count != 0) command_buffer.copyBuffer(*texture_coordinates->buffer, *mesh.texture_coordinates.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(updated_vertex_count) * sizeof(math::Float2)});
        if (indices && updated_index_count != 0) command_buffer.copyBuffer(*indices->buffer, *mesh.indices.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(updated_index_count) * sizeof(std::uint32_t)});

        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR | vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
        if (normals) mesh.attribute_mask |= 1u;
        if (tangents) mesh.attribute_mask |= 2u;
        if (texture_coordinates) mesh.attribute_mask |= 4u;
        const bool geometry_changed  = positions || indices;
        const bool generate_normals  = geometry_changed && !normals;
        const bool generate_tangents = (geometry_changed || normals || texture_coordinates) && !tangents;
        if (generate_normals || generate_tangents) this->generate_dynamic_attributes(mesh, generate_normals, generate_tangents, command_buffer);

        if (!std::ranges::contains(this->resources.external_geometries, geometry_id)) this->resources.external_geometries.push_back(geometry_id);
        mesh.cpu_data_stale = true;
        if (!geometry_changed) return;

        const vk::AccelerationStructureGeometryKHR geometry = triangle_geometry(mesh);
        if (mesh.update_mode == GpuMeshUpdateMode::TopologyChanging) {
            GpuAccelerationStructure replacement = this->build_bottom_level(geometry, mesh.acceleration_primitive_count, mesh.update_mode, &command_buffer);
            if (*mesh.bottom_level_acceleration_structure.acceleration_structure) this->context.runtime.frames.defer_destruction([previous = std::move(mesh.bottom_level_acceleration_structure)]() mutable {});
            mesh.bottom_level_acceleration_structure      = std::move(replacement);
            this->resources.external_bottom_level_rebuilt = true;
            return;
        }
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *mesh.bottom_level_acceleration_structure.acceleration_structure,
            *mesh.bottom_level_acceleration_structure.acceleration_structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, mesh.acceleration_primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{mesh.acceleration_primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    void GpuScene::update_particle_set(GpuParticleSet& gpu_particles, const scene::ParticleSet& source_particles, const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        const std::array<math::Float3, 1> missing_float3{};
        const std::span<const math::Float3> velocities = source_particles.velocities.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{source_particles.velocities};
        const std::span<const math::Float3> colors     = source_particles.colors.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{source_particles.colors};
        const std::array<float, 1> missing_float{};
        const std::span<const float> temperatures = source_particles.temperatures.empty() ? std::span<const float>{missing_float} : std::span<const float>{source_particles.temperatures};
        std::vector<std::uint32_t> materials{};
        materials.reserve(std::max<std::size_t>(source_particles.particle_materials.size(), 1));
        for (const scene::MaterialId material : source_particles.particle_materials) materials.push_back(static_cast<std::uint32_t>(std::ranges::find(scene.resources.materials, material, &scene::Material::id) - scene.resources.materials.begin()));
        if (materials.empty()) materials.emplace_back();
        const std::array uploads{
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Float3>{source_particles.positions})),
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const float>{source_particles.radii})),
            this->context.runtime.frames.stage_upload(std::as_bytes(colors)),
            this->context.runtime.frames.stage_upload(std::as_bytes(velocities)),
            this->context.runtime.frames.stage_upload(std::as_bytes(temperatures)),
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const std::uint32_t>{materials})),
        };
        const std::array<GpuBuffer*, 6> destinations{&gpu_particles.positions, &gpu_particles.radii, &gpu_particles.colors, &gpu_particles.velocities, &gpu_particles.temperatures, &gpu_particles.materials};
        for (std::size_t index = 0; index != uploads.size(); ++index) command_buffer.copyBuffer(uploads[index].buffer, *destinations[index]->buffer, vk::BufferCopy{uploads[index].offset, 0, uploads[index].size});
        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
    }

    void GpuScene::synchronize_external_particle_set(const scene::ParticleSetId particle_set_id, const GpuBuffer* positions, const GpuBuffer* radii, const GpuBuffer* velocities, const GpuBuffer* colors, const GpuBuffer* temperatures, const GpuBuffer* materials, const DescriptorHandle materials_descriptor, const std::uint32_t particle_count, const vk::raii::CommandBuffer& command_buffer) {
        GpuParticleSet& particles = *std::ranges::find(this->resources.particle_sets, particle_set_id, &GpuParticleSet::particle_set_id);
        if (particle_count > particles.particle_capacity) {
            GpuParticleSet replacement{};
            replacement.particle_set_id                = particles.particle_set_id;
            replacement.particle_count                 = particle_count;
            replacement.particle_capacity              = std::bit_ceil(std::max(particle_count, 1u));
            replacement.attribute_mask                 = particles.attribute_mask;
            const vk::DeviceSize capacity              = replacement.particle_capacity;
            const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            replacement.positions                      = this->context.runtime.resources.create_buffer(capacity * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.radii                          = this->context.runtime.resources.create_buffer(capacity * sizeof(float), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.velocities                     = this->context.runtime.resources.create_buffer(capacity * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.colors                         = this->context.runtime.resources.create_buffer(capacity * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.temperatures                   = this->context.runtime.resources.create_buffer(capacity * sizeof(float), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.materials                      = this->context.runtime.resources.create_buffer(capacity * sizeof(std::uint32_t), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.positions_descriptor           = this->context.runtime.resources.allocate_resource_descriptor();
            replacement.radii_descriptor               = this->context.runtime.resources.allocate_resource_descriptor();
            replacement.velocities_descriptor          = this->context.runtime.resources.allocate_resource_descriptor();
            replacement.colors_descriptor              = this->context.runtime.resources.allocate_resource_descriptor();
            replacement.temperatures_descriptor        = this->context.runtime.resources.allocate_resource_descriptor();
            replacement.materials_descriptor           = this->context.runtime.resources.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
            this->context.runtime.resources.write_buffer_descriptor(replacement.radii_descriptor, vk::DescriptorType::eStorageBuffer, replacement.radii);
            this->context.runtime.resources.write_buffer_descriptor(replacement.velocities_descriptor, vk::DescriptorType::eStorageBuffer, replacement.velocities);
            this->context.runtime.resources.write_buffer_descriptor(replacement.colors_descriptor, vk::DescriptorType::eStorageBuffer, replacement.colors);
            this->context.runtime.resources.write_buffer_descriptor(replacement.temperatures_descriptor, vk::DescriptorType::eStorageBuffer, replacement.temperatures);
            this->context.runtime.resources.write_buffer_descriptor(replacement.materials_descriptor, vk::DescriptorType::eStorageBuffer, replacement.materials);
            const std::uint32_t preserved_particles = std::min(particles.particle_count, particle_count);
            if (preserved_particles != 0 && !positions) command_buffer.copyBuffer(*particles.positions.buffer, *replacement.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(math::Float3)});
            if (preserved_particles != 0 && !radii) command_buffer.copyBuffer(*particles.radii.buffer, *replacement.radii.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(float)});
            if (preserved_particles != 0 && !velocities && (particles.attribute_mask & 1u) != 0) command_buffer.copyBuffer(*particles.velocities.buffer, *replacement.velocities.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(math::Float3)});
            if (preserved_particles != 0 && !colors && (particles.attribute_mask & 2u) != 0) command_buffer.copyBuffer(*particles.colors.buffer, *replacement.colors.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(math::Float3)});
            if (preserved_particles != 0 && !temperatures && (particles.attribute_mask & 4u) != 0) command_buffer.copyBuffer(*particles.temperatures.buffer, *replacement.temperatures.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(float)});
            if (preserved_particles != 0 && !materials && (particles.attribute_mask & 8u) != 0) command_buffer.copyBuffer(*particles.materials.buffer, *replacement.materials.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_particles) * sizeof(std::uint32_t)});
            this->context.runtime.frames.retire_resource_descriptor(particles.positions_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.radii_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.velocities_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.colors_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.temperatures_descriptor);
            this->context.runtime.frames.retire_resource_descriptor(particles.materials_descriptor);
            this->context.runtime.frames.defer_destruction([previous = std::move(particles)]() mutable {});
            particles                                = std::move(replacement);
            this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Visualization;
        } else
            particles.particle_count = particle_count;

        if (positions && particle_count != 0) command_buffer.copyBuffer(*positions->buffer, *particles.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(math::Float3)});
        if (radii && particle_count != 0) command_buffer.copyBuffer(*radii->buffer, *particles.radii.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(float)});
        if (velocities && particle_count != 0) {
            command_buffer.copyBuffer(*velocities->buffer, *particles.velocities.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(math::Float3)});
            particles.attribute_mask |= 1u;
        }
        if (colors && particle_count != 0) {
            command_buffer.copyBuffer(*colors->buffer, *particles.colors.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(math::Float3)});
            particles.attribute_mask |= 2u;
        }
        if (temperatures && particle_count != 0) {
            command_buffer.copyBuffer(*temperatures->buffer, *particles.temperatures.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(particle_count) * sizeof(float)});
            particles.attribute_mask |= 4u;
        }
        if (materials && particle_count != 0) {
            struct alignas(8) ParticleMaterialPushData {
                DescriptorHandle source{};
                DescriptorHandle destination{};
                DescriptorHandle lookup{};
                std::array<std::uint32_t, 2> reserved{};
                std::uint32_t particle_count{};
                std::uint32_t material_lookup_count{};
                std::array<std::uint32_t, 2> reserved_metadata{};
            };
            static_assert(sizeof(ParticleMaterialPushData) == 48);
            const ParticleMaterialPushData push_data{
                materials_descriptor,
                particles.materials_descriptor,
                this->resources.particle_material_lookup_descriptor,
                {},
                particle_count,
                this->resources.particle_material_lookup_count,
                {},
            };
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.particle_material_shader);
            command_buffer.dispatch((particle_count + 255u) / 256u, 1, 1);
            particles.attribute_mask |= 8u;
        }
        const vk::MemoryBarrier2 copy_dependency{vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &copy_dependency});
        if (!std::ranges::contains(this->resources.external_particle_sets, particle_set_id)) this->resources.external_particle_sets.push_back(particle_set_id);
        particles.cpu_data_stale = true;
    }

    void GpuScene::synchronize_external_volume(const scene::VolumeId volume_id, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, const std::uint64_t voxel_count, const scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer) {
        GpuVolume& volume                  = *std::ranges::find(this->resources.volumes, volume_id, &GpuVolume::volume_id);
        const std::uint64_t expected_count = static_cast<std::uint64_t>(dirty_region.maximum.x - dirty_region.minimum.x) * (dirty_region.maximum.y - dirty_region.minimum.y) * (dirty_region.maximum.z - dirty_region.minimum.z);
        if (voxel_count != expected_count) throw std::runtime_error("CUDA External Volume element count differs from its dirty region");
        const auto copy = [&command_buffer, dirty_region, &volume](const GpuBuffer* source, const GpuVolumeField field, const vk::DeviceSize element_size) {
            if (!source) return;
            const std::size_t index = std::to_underlying(field);
            if (!volume.field_present[index]) throw std::runtime_error("CUDA External Volume published a field absent from its Scene resource");
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
        copy(sigma_a, GpuVolumeField::SigmaA, sizeof(math::Float3));
        copy(sigma_s, GpuVolumeField::SigmaS, sizeof(math::Float3));
        copy(emission, GpuVolumeField::Emission, sizeof(math::Float3));
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        if (!std::ranges::contains(this->resources.external_volumes, volume_id)) this->resources.external_volumes.push_back(volume_id);
        volume.dirty_region = dirty_region;
        ++volume.revision.content;
        volume.cpu_data_stale = true;
    }

    void GpuScene::update_volumes(const vk::raii::CommandBuffer& command_buffer) {
        for (const scene::Volume& source : this->context.document.content.evaluated.resources.volumes) {
            GpuVolume& volume = *std::ranges::find(this->resources.volumes, source.id, &GpuVolume::volume_id);
            if (std::ranges::contains(this->resources.external_volumes, source.id) || source.revision.content == volume.revision.content) continue;
            GpuVolume replacement = this->create_volume(source, &command_buffer);
            for (std::size_t field = 0; field != volume.fields.size(); ++field)
                if (volume.field_present[field]) this->context.runtime.frames.retire_resource_descriptor(volume.descriptors[field]);
            this->context.runtime.frames.defer_destruction([previous = std::move(volume)]() mutable {});
            volume                                   = std::move(replacement);
            this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Volume;
        }
    }

    void GpuScene::update_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer) {
        const vk::AccelerationStructureGeometryInstancesDataKHR instance_data{
            vk::False,
            vk::DeviceOrHostAddressConstKHR{this->resources.acceleration_structure_instances.address},
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
            *this->resources.top_level_acceleration_structure.acceleration_structure,
            *this->resources.top_level_acceleration_structure.acceleration_structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
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
            const GpuUploadSlice upload = this->context.runtime.frames.stage_upload(std::as_bytes(instances));
            command_buffer.copyBuffer(upload.buffer, *this->resources.acceleration_structure_instances.buffer, vk::BufferCopy{upload.offset, 0, upload.size});
            const vk::BufferMemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->resources.acceleration_structure_instances.buffer, 0, this->resources.acceleration_structure_instances.size};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &bottom_level_dependency, 1, &upload_dependency});
        }
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    scene::SceneChange GpuScene::apply(const dynamics::DynamicFrame& frame, const vk::raii::CommandBuffer& command_buffer) {
        this->resources.resource_binding_changes = scene::SceneChange::None;
        this->context.document.begin_update(this->context.document.content.evaluated);
        for (const dynamics::InstanceTransformUpdate& update : frame.instance_transform_updates) {
            const auto instance_transform = std::ranges::find(this->resources.instance_transforms, update.instance_id, &std::pair<scene::InstanceId, math::Transform>::first);
            this->context.document.update_transform(this->context.document.content.evaluated, update.instance_id, instance_transform->second * update.transform);
        }
        for (const dynamics::TriangleMeshUpdate& update : frame.triangle_mesh_updates) {
            scene::Geometry& resource                  = *std::ranges::find(this->context.document.content.evaluated.resources.geometries, update.geometry_id, &scene::Geometry::id);
            const scene::TriangleMeshGeometry& current = std::get<scene::TriangleMeshGeometry>(resource.data);
            const auto includes                        = [&update](const dynamics::Attribute attribute) { return (update.attribute_mask & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            if (update.vertex_count != current.positions.size() && !includes(dynamics::Attribute::Position)) throw std::runtime_error("A Host TriangleMesh vertex count change must publish positions");
            if (update.index_count != current.indices.size() && !includes(dynamics::Attribute::Index)) throw std::runtime_error("A Host TriangleMesh index count change must publish indices");
            const bool shape_changed         = includes(dynamics::Attribute::Position) || includes(dynamics::Attribute::Index);
            const bool tangent_frame_changed = shape_changed || includes(dynamics::Attribute::Normal) || includes(dynamics::Attribute::TextureCoordinate);
            this->context.document.update_triangle_mesh(this->context.document.content.evaluated, update.geometry_id, includes(dynamics::Attribute::Position) ? std::span<const math::Float3>{update.positions} : std::span<const math::Float3>{current.positions}, includes(dynamics::Attribute::Normal) ? std::span<const math::Float3>{update.normals} : shape_changed ? std::span<const math::Float3>{} : std::span<const math::Float3>{current.normals}, includes(dynamics::Attribute::Tangent) ? std::span<const math::Float3>{update.tangents} : tangent_frame_changed ? std::span<const math::Float3>{} : std::span<const math::Float3>{current.tangents}, includes(dynamics::Attribute::TextureCoordinate) ? std::span<const math::Float2>{update.texture_coordinates} : std::span<const math::Float2>{current.texture_coordinates}, includes(dynamics::Attribute::Index) ? std::span<const std::uint32_t>{update.indices} : std::span<const std::uint32_t>{current.indices});
        }
        for (const dynamics::ParticleSetUpdate& update : frame.particle_set_updates) {
            const scene::ParticleSet& current = *std::ranges::find(this->context.document.content.evaluated.resources.particle_sets, update.particle_set_id, &scene::ParticleSet::id);
            const auto includes               = [&update](const dynamics::Attribute attribute) { return (update.attribute_mask & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            const bool count_changed          = update.particle_count != current.positions.size();
            if (count_changed && (!includes(dynamics::Attribute::Position) || !includes(dynamics::Attribute::Radius))) throw std::runtime_error("A Host ParticleSet count change must publish positions and radii together");
            if (count_changed && ((!current.velocities.empty() && !includes(dynamics::Attribute::Velocity)) || (!current.colors.empty() && !includes(dynamics::Attribute::Color)) || (!current.temperatures.empty() && !includes(dynamics::Attribute::Temperature)) || (!current.particle_materials.empty() && !includes(dynamics::Attribute::Material)))) throw std::runtime_error("A Host ParticleSet count change must publish every populated per-particle attribute");
            this->context.document.update_particle_set(this->context.document.content.evaluated, update.particle_set_id, includes(dynamics::Attribute::Position) ? std::span<const math::Float3>{update.positions} : std::span<const math::Float3>{current.positions}, includes(dynamics::Attribute::Radius) ? std::span<const float>{update.radii} : std::span<const float>{current.radii}, includes(dynamics::Attribute::Velocity) ? std::span<const math::Float3>{update.velocities} : std::span<const math::Float3>{current.velocities}, includes(dynamics::Attribute::Color) ? std::span<const math::Float3>{update.colors} : std::span<const math::Float3>{current.colors}, includes(dynamics::Attribute::Temperature) ? std::span<const float>{update.temperatures} : std::span<const float>{current.temperatures}, includes(dynamics::Attribute::Material) ? std::span<const scene::MaterialId>{update.materials} : std::span<const scene::MaterialId>{current.particle_materials});
        }
        for (const dynamics::VolumeUpdate& update : frame.volume_updates) {
            const auto includes = [&update](const dynamics::Attribute attribute) { return (update.attribute_mask & (1ull << static_cast<std::uint32_t>(attribute))) != 0; };
            if (includes(dynamics::Attribute::Density) || includes(dynamics::Attribute::Temperature) || includes(dynamics::Attribute::EmissionScale)) this->context.document.update_density_grid(this->context.document.content.evaluated, update.volume_id, update.region, update.density, update.temperature, update.emission_scale);
            if (includes(dynamics::Attribute::SigmaA) || includes(dynamics::Attribute::SigmaS) || includes(dynamics::Attribute::Emission)) {
                scene::Volume& volume                                   = *std::ranges::find(this->context.document.content.evaluated.resources.volumes, update.volume_id, &scene::Volume::id);
                std::get<scene::RgbGridVolume>(volume.data).color_space = update.color_space;
                this->context.document.update_rgb_grid(this->context.document.content.evaluated, update.volume_id, update.region, update.sigma_a, update.sigma_s, update.emission);
            }
            if (includes(dynamics::Attribute::Velocity)) {
                GpuVolumeVelocityStorage* storage{};
                const auto found = std::ranges::find(this->resources.volume_velocity_storage, update.volume_id, &GpuVolumeVelocityStorage::volume_id);
                if (found == this->resources.volume_velocity_storage.end()) {
                    this->resources.volume_velocity_storage.emplace_back();
                    storage                      = &this->resources.volume_velocity_storage.back();
                    storage->volume_id           = update.volume_id;
                    storage->velocity_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
                } else
                    storage = std::to_address(found);
                const std::uint64_t capacity = static_cast<std::uint64_t>(update.resolution.x) * update.resolution.y * update.resolution.z;
                if (storage->capacity < capacity) {
                    if (*storage->buffer.buffer) this->context.runtime.frames.defer_destruction([previous = std::move(storage->buffer)]() mutable {});
                    storage->buffer   = this->context.runtime.resources.create_buffer(capacity * sizeof(math::Float3), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                    storage->capacity = capacity;
                    this->context.runtime.resources.write_buffer_descriptor(storage->velocity_descriptor, vk::DescriptorType::eStorageBuffer, storage->buffer);
                    command_buffer.fillBuffer(*storage->buffer.buffer, 0, storage->buffer.size, 0);
                }
                const GpuUploadSlice upload = this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Float3>{update.velocity}));
                std::vector<vk::BufferCopy> regions{};
                const std::uint32_t width  = update.region.maximum.x - update.region.minimum.x;
                const std::uint32_t height = update.region.maximum.y - update.region.minimum.y;
                for (std::uint32_t z = 0; z != update.region.maximum.z - update.region.minimum.z; ++z)
                    for (std::uint32_t y = 0; y != height; ++y) {
                        const vk::DeviceSize source      = upload.offset + (static_cast<vk::DeviceSize>(z) * height + y) * width * sizeof(math::Float3);
                        const vk::DeviceSize destination = (static_cast<vk::DeviceSize>(update.region.minimum.z + z) * update.resolution.y * update.resolution.x + static_cast<vk::DeviceSize>(update.region.minimum.y + y) * update.resolution.x + update.region.minimum.x) * sizeof(math::Float3);
                        regions.emplace_back(source, destination, static_cast<vk::DeviceSize>(width) * sizeof(math::Float3));
                    }
                command_buffer.copyBuffer(upload.buffer, *storage->buffer.buffer, regions);
                const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead};
                command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
                const scene::Volume& volume = *std::ranges::find(this->context.document.content.evaluated.resources.volumes, update.volume_id, &scene::Volume::id);
                const GpuVolumeVelocityField field{update.volume_id, storage->velocity_descriptor, update.resolution, volume.bounds, volume.transform};
                const auto visible = std::ranges::find(this->resources.volume_velocity_fields, update.volume_id, &GpuVolumeVelocityField::volume_id);
                if (visible == this->resources.volume_velocity_fields.end())
                    this->resources.volume_velocity_fields.push_back(field);
                else
                    *visible = field;
                this->context.document.mark_change(this->context.document.content.evaluated, scene::SceneChange::Visualization);
            }
        }
        for (const dynamics::GpuOutputResourceView& output : frame.gpu_output_resources) {
            std::uint64_t attribute_mask{};
            for (const dynamics::GpuOutputAttributeView& attribute : output.attributes) attribute_mask |= 1ull << static_cast<std::uint32_t>(attribute.attribute);
            if (output.resource_kind == dynamics::ResourceKind::TriangleMesh)
                this->context.document.mark_change(this->context.document.content.evaluated, scene::SceneChange::Geometry);
            else if (output.resource_kind == dynamics::ResourceKind::ParticleSet)
                this->context.document.mark_change(this->context.document.content.evaluated, scene::SceneChange::Visualization);
            else if (output.resource_kind == dynamics::ResourceKind::Volume) {
                constexpr std::uint64_t volume_attribute_mask = (1ull << static_cast<std::uint32_t>(dynamics::Attribute::Density)) | (1ull << static_cast<std::uint32_t>(dynamics::Attribute::Temperature)) | (1ull << static_cast<std::uint32_t>(dynamics::Attribute::EmissionScale)) | (1ull << static_cast<std::uint32_t>(dynamics::Attribute::SigmaA)) | (1ull << static_cast<std::uint32_t>(dynamics::Attribute::SigmaS)) | (1ull << static_cast<std::uint32_t>(dynamics::Attribute::Emission));
                if ((attribute_mask & volume_attribute_mask) != 0) this->context.document.mark_change(this->context.document.content.evaluated, scene::SceneChange::Volume);
                if ((attribute_mask & (1ull << static_cast<std::uint32_t>(dynamics::Attribute::Velocity))) != 0) this->context.document.mark_change(this->context.document.content.evaluated, scene::SceneChange::Visualization);
            }
        }
        this->context.document.commit_update();

        std::vector<scene::GeometryId> external_geometries{};
        std::vector<scene::ParticleSetId> external_particles{};
        std::vector<scene::VolumeId> external_volumes{};
        bool mixed_transfers{};
        for (const dynamics::GpuOutputResourceView& output : frame.gpu_output_resources) {
            if (output.resource_kind == dynamics::ResourceKind::TriangleMesh) {
                const scene::GeometryId geometry_id = std::get<scene::GeometryId>(output.resource_id);
                if (std::ranges::contains(frame.triangle_mesh_updates, geometry_id, &dynamics::TriangleMeshUpdate::geometry_id))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_geometries, geometry_id))
                    external_geometries.push_back(geometry_id);
            } else if (output.resource_kind == dynamics::ResourceKind::ParticleSet) {
                const scene::ParticleSetId particle_set_id = std::get<scene::ParticleSetId>(output.resource_id);
                if (std::ranges::contains(frame.particle_set_updates, particle_set_id, &dynamics::ParticleSetUpdate::particle_set_id))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_particles, particle_set_id))
                    external_particles.push_back(particle_set_id);
            } else if (output.resource_kind == dynamics::ResourceKind::Volume) {
                const scene::VolumeId volume_id = std::get<scene::VolumeId>(output.resource_id);
                if (std::ranges::contains(frame.volume_updates, volume_id, &dynamics::VolumeUpdate::volume_id))
                    mixed_transfers = true;
                else if (!std::ranges::contains(external_volumes, volume_id))
                    external_volumes.push_back(volume_id);
            }
        }
        this->begin_external_updates(external_geometries, external_particles, external_volumes);
        this->synchronize_scene(command_buffer);
        if (mixed_transfers) {
            const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        }
        for (const dynamics::GpuOutputResourceView& output : frame.gpu_output_resources) {
            if (output.resource_kind == dynamics::ResourceKind::Volume) {
                const GpuBuffer* density{};
                const GpuBuffer* temperature{};
                const GpuBuffer* emission_scale{};
                const GpuBuffer* sigma_a{};
                const GpuBuffer* sigma_s{};
                const GpuBuffer* emission{};
                const dynamics::GpuOutputAttributeView* velocity{};
                for (const dynamics::GpuOutputAttributeView& attribute : output.attributes) {
                    if (attribute.attribute == dynamics::Attribute::Density)
                        density = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Temperature)
                        temperature = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::EmissionScale)
                        emission_scale = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::SigmaA)
                        sigma_a = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::SigmaS)
                        sigma_s = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Emission)
                        emission = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Velocity)
                        velocity = &attribute;
                }
                if (density || temperature || emission_scale || sigma_a || sigma_s || emission) this->synchronize_external_volume(std::get<scene::VolumeId>(output.resource_id), density, temperature, emission_scale, sigma_a, sigma_s, emission, output.active_count, *output.dirty_region, command_buffer);
                if (velocity) {
                    const scene::VolumeId volume_id = std::get<scene::VolumeId>(output.resource_id);
                    const scene::Volume& volume     = *std::ranges::find(this->context.document.content.evaluated.resources.volumes, volume_id, &scene::Volume::id);
                    math::UInt3 resolution{};
                    std::visit(
                        [&resolution](const auto& data) {
                            if constexpr (requires { data.resolution; }) resolution = data.resolution;
                        },
                        volume.data);
                    const GpuVolumeVelocityField field{volume_id, velocity->descriptor, resolution, volume.bounds, volume.transform};
                    const auto found = std::ranges::find(this->resources.volume_velocity_fields, volume_id, &GpuVolumeVelocityField::volume_id);
                    if (found == this->resources.volume_velocity_fields.end())
                        this->resources.volume_velocity_fields.push_back(field);
                    else
                        *found = field;
                }
                continue;
            }
            if (output.resource_kind == dynamics::ResourceKind::ParticleSet) {
                const GpuBuffer* positions{};
                const GpuBuffer* radii{};
                const GpuBuffer* velocities{};
                const GpuBuffer* colors{};
                const GpuBuffer* temperatures{};
                const GpuBuffer* materials{};
                DescriptorHandle materials_descriptor{};
                for (const dynamics::GpuOutputAttributeView& attribute : output.attributes) {
                    if (attribute.attribute == dynamics::Attribute::Position)
                        positions = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Radius)
                        radii = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Velocity)
                        velocities = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Color)
                        colors = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Temperature)
                        temperatures = attribute.buffer;
                    else if (attribute.attribute == dynamics::Attribute::Material) {
                        materials            = attribute.buffer;
                        materials_descriptor = attribute.descriptor;
                    }
                }
                const GpuParticleSet& current = *std::ranges::find(this->resources.particle_sets, std::get<scene::ParticleSetId>(output.resource_id), &GpuParticleSet::particle_set_id);
                if (output.active_count != current.particle_count && (!positions || !radii)) throw std::runtime_error("A ParticleSet count change must publish positions and radii together");
                this->synchronize_external_particle_set(std::get<scene::ParticleSetId>(output.resource_id), positions, radii, velocities, colors, temperatures, materials, materials_descriptor, static_cast<std::uint32_t>(output.active_count), command_buffer);
                continue;
            }
            if (output.resource_kind != dynamics::ResourceKind::TriangleMesh) continue;
            const GpuBuffer* positions{};
            const GpuBuffer* normals{};
            const GpuBuffer* tangents{};
            const GpuBuffer* texture_coordinates{};
            const GpuBuffer* indices{};
            for (const dynamics::GpuOutputAttributeView& attribute : output.attributes) {
                if (attribute.attribute == dynamics::Attribute::Position)
                    positions = attribute.buffer;
                else if (attribute.attribute == dynamics::Attribute::Normal)
                    normals = attribute.buffer;
                else if (attribute.attribute == dynamics::Attribute::Tangent)
                    tangents = attribute.buffer;
                else if (attribute.attribute == dynamics::Attribute::TextureCoordinate)
                    texture_coordinates = attribute.buffer;
                else if (attribute.attribute == dynamics::Attribute::Index)
                    indices = attribute.buffer;
            }
            this->synchronize_external_geometry(std::get<scene::GeometryId>(output.resource_id), positions, normals, tangents, texture_coordinates, indices, static_cast<std::uint32_t>(output.active_count), static_cast<std::uint32_t>(output.secondary_count), command_buffer);
        }
        this->end_external_updates(command_buffer);
        return std::exchange(this->resources.resource_binding_changes, scene::SceneChange::None);
    }

    scene::SceneChange GpuScene::synchronize(const vk::raii::CommandBuffer& command_buffer) {
        this->resources.resource_binding_changes = scene::SceneChange::None;
        this->synchronize_scene(command_buffer);
        return std::exchange(this->resources.resource_binding_changes, scene::SceneChange::None);
    }

    void GpuScene::synchronize_scene(const vk::raii::CommandBuffer& command_buffer) {
        const scene::SceneView scene = this->context.document.content.evaluated.view();
        if (scene.revision.number == this->resources.synchronized_revision.number) return;
        this->cache_texture_images(scene, &command_buffer);
        if (this->resources.external_geometries.empty()) this->resources.scratch_offsets[this->context.runtime.frames.frame.current_slot_index] = 0;
        bool rebuilt_bottom_level = std::exchange(this->resources.external_bottom_level_rebuilt, false);
        if ((scene.revision.changes & scene::SceneChange::Geometry) != scene::SceneChange::None) {
            for (GpuGeometry& mesh : this->resources.geometries) {
                const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, mesh.geometry_id, &scene::Geometry::id);
                if (std::ranges::contains(this->resources.external_geometries, mesh.geometry_id)) continue;
                if (mesh.update_mode != GpuMeshUpdateMode::Deformable) {
                    GpuGeometry replacement = this->create_geometry(geometry, &command_buffer);
                    this->context.runtime.frames.retire_resource_descriptor(mesh.positions_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(mesh.normals_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(mesh.tangents_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(mesh.texture_coordinates_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(mesh.indices_descriptor);
                    this->context.runtime.frames.defer_destruction([previous = std::move(mesh)]() mutable {});
                    mesh                                     = std::move(replacement);
                    this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Geometry;
                    rebuilt_bottom_level                     = true;
                    continue;
                }
                this->update_bottom_level(mesh, geometry, command_buffer);
            }
        }
        if ((scene.revision.changes & scene::SceneChange::Visualization) != scene::SceneChange::None) {
            for (GpuParticleSet& particles : this->resources.particle_sets) {
                const scene::ParticleSet& source = *std::ranges::find(scene.resources.particle_sets, particles.particle_set_id, &scene::ParticleSet::id);
                if (std::ranges::contains(this->resources.external_particle_sets, particles.particle_set_id)) continue;
                const std::uint32_t attribute_mask = (source.velocities.empty() ? 0u : 1u) | (source.colors.empty() ? 0u : 2u) | (source.temperatures.empty() ? 0u : 4u) | (source.particle_materials.empty() ? 0u : 8u);
                if (source.positions.size() > particles.particle_capacity || particles.attribute_mask != attribute_mask) {
                    GpuParticleSet replacement = this->create_particle_set(source, scene, &command_buffer, std::bit_ceil(static_cast<std::uint32_t>(std::max<std::size_t>(source.positions.size(), 1))));
                    this->context.runtime.frames.retire_resource_descriptor(particles.positions_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(particles.radii_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(particles.velocities_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(particles.colors_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(particles.temperatures_descriptor);
                    this->context.runtime.frames.retire_resource_descriptor(particles.materials_descriptor);
                    this->context.runtime.frames.defer_destruction([previous = std::move(particles)]() mutable {});
                    particles                                = std::move(replacement);
                    this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Visualization;
                    continue;
                }
                particles.particle_count = static_cast<std::uint32_t>(source.positions.size());
                particles.attribute_mask = attribute_mask;
                this->update_particle_set(particles, source, scene, command_buffer);
            }
        }
        if ((scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(command_buffer);
        this->resources.external_geometries.clear();
        this->resources.external_particle_sets.clear();
        this->resources.external_volumes.clear();
        if (rebuilt_bottom_level || (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            this->update_top_level(instances, command_buffer);
        }
        this->resources.synchronized_revision = scene.revision;
    }

    void GpuScene::begin_external_updates(const std::span<const scene::GeometryId> geometry_ids, const std::span<const scene::ParticleSetId> particle_set_ids, const std::span<const scene::VolumeId> volume_ids) {
        this->resources.external_geometries.assign(geometry_ids.begin(), geometry_ids.end());
        this->resources.external_particle_sets.assign(particle_set_ids.begin(), particle_set_ids.end());
        this->resources.external_volumes.assign(volume_ids.begin(), volume_ids.end());
    }

    void GpuScene::end_external_updates(const vk::raii::CommandBuffer& command_buffer) {
        if (std::exchange(this->resources.external_bottom_level_rebuilt, false)) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(this->context.document.content.evaluated.view());
            this->update_top_level(instances, command_buffer);
        }
        this->resources.external_geometries.clear();
        this->resources.external_particle_sets.clear();
        this->resources.external_volumes.clear();
    }
    GpuAccelerationStructure GpuScene::build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, const std::uint32_t primitive_count, const GpuMeshUpdateMode update_mode, const vk::raii::CommandBuffer* command_buffer) {
        vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess;
        if (update_mode == GpuMeshUpdateMode::Deformable)
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
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);

        GpuAccelerationStructure result{};
        result.storage                = this->context.runtime.resources.create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.acceleration_structure = vk::raii::AccelerationStructureKHR{
            this->context.runtime.graphics.device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel},
        };
        build_info.dstAccelerationStructure = *result.acceleration_structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.buildScratchSize, !command_buffer)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        if (command_buffer) {
            command_buffer->buildAccelerationStructuresKHR(build_info, ranges);
            result.address = this->context.runtime.graphics.device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.acceleration_structure});
            return result;
        }
        std::optional<vk::raii::QueryPool> compaction_query{};
        if (update_mode == GpuMeshUpdateMode::Immutable) compaction_query.emplace(this->context.runtime.graphics.device, vk::QueryPoolCreateInfo{{}, vk::QueryType::eAccelerationStructureCompactedSizeKHR, 1});
        this->context.runtime.resources.submit_immediate([&build_info, &ranges, &result, &compaction_query](const vk::raii::CommandBuffer& command_buffer) {
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
                const std::array<vk::AccelerationStructureKHR, 1> structures{*result.acceleration_structure};
                command_buffer.writeAccelerationStructuresPropertiesKHR(structures, vk::QueryType::eAccelerationStructureCompactedSizeKHR, **compaction_query, 0);
            }
        });
        if (compaction_query) {
            std::uint64_t compacted_size{};
            if (compaction_query->getResults(0, 1, sizeof(compacted_size), &compacted_size, sizeof(compacted_size), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait) != vk::Result::eSuccess) throw std::runtime_error("Static BLAS compaction size query failed");
            if (compacted_size != 0 && compacted_size < result.storage.size) {
                GpuAccelerationStructure compacted{};
                compacted.storage                = this->context.runtime.resources.create_buffer(compacted_size, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                compacted.acceleration_structure = vk::raii::AccelerationStructureKHR{this->context.runtime.graphics.device, vk::AccelerationStructureCreateInfoKHR{{}, *compacted.storage.buffer, 0, compacted_size, vk::AccelerationStructureTypeKHR::eBottomLevel}};
                this->context.runtime.resources.submit_immediate([&result, &compacted](const vk::raii::CommandBuffer& command_buffer) { command_buffer.copyAccelerationStructureKHR(vk::CopyAccelerationStructureInfoKHR{*result.acceleration_structure, *compacted.acceleration_structure, vk::CopyAccelerationStructureModeKHR::eCompact}); });
                result = std::move(compacted);
            }
        }
        result.address = this->context.runtime.graphics.device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.acceleration_structure});
        return result;
    }

    GpuAccelerationStructure GpuScene::build_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances) {
        const vk::AccelerationStructureGeometryInstancesDataKHR instance_data{
            vk::False,
            vk::DeviceOrHostAddressConstKHR{this->resources.acceleration_structure_instances.address},
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
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);

        GpuAccelerationStructure result{};
        result.storage                = this->context.runtime.resources.create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.acceleration_structure = vk::raii::AccelerationStructureKHR{
            this->context.runtime.graphics.device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eTopLevel},
        };
        build_info.dstAccelerationStructure = *result.acceleration_structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.buildScratchSize, true)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        this->context.runtime.resources.submit_immediate([&build_info, &ranges](const vk::raii::CommandBuffer& command_buffer) {
            const vk::MemoryBarrier2 blas_build_dependency{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &blas_build_dependency});
            command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        });
        result.address = this->context.runtime.graphics.device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.acceleration_structure});
        return result;
    }
} // namespace spectra
