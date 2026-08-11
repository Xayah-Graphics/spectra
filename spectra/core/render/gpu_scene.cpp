module spectra.render.gpu_scene;

import std;
import vulkan;

namespace spectra {
    GpuScene::GpuScene(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept : context{runtime, std::move(shader_directory)} {}

    GpuScene::~GpuScene() {
        this->destroy();
    }

    namespace {
        struct DynamicInstanceBinding {
            std::array<std::uint32_t, 4> metadata{};
            math::Transform primitive_from_instance{};
        };

        static_assert(sizeof(DynamicInstanceBinding) == 80);

        struct GpuDerivedBounds {
            std::array<std::uint32_t, 4> minimum{};
            std::array<std::uint32_t, 4> maximum{};
        };

        static_assert(sizeof(GpuDerivedBounds) == 32);

        [[nodiscard]] float ordered_float(const std::uint32_t value) noexcept {
            const std::uint32_t bits = (value & 0x80000000u) != 0 ? value ^ 0x80000000u : ~value;
            return std::bit_cast<float>(bits);
        }

        [[nodiscard]] std::uint16_t float_to_half(const float value) noexcept {
            const std::uint32_t bits     = std::bit_cast<std::uint32_t>(value);
            const std::uint32_t sign     = (bits >> 16u) & 0x8000u;
            const std::uint32_t mantissa = bits & 0x7fffffu;
            const std::int32_t exponent  = static_cast<std::int32_t>((bits >> 23u) & 0xffu) - 112;
            if (exponent <= 0) {
                if (exponent < -10) return static_cast<std::uint16_t>(sign);
                const std::uint32_t denormal = (mantissa | 0x800000u) >> (1 - exponent);
                return static_cast<std::uint16_t>(sign | ((denormal + 0x1000u) >> 13u));
            }
            if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa == 0 ? 0u : 0x0200u));
            return static_cast<std::uint16_t>(sign | ((static_cast<std::uint32_t>(exponent) << 10u) + ((mantissa + 0x1000u) >> 13u)));
        }

        [[nodiscard]] std::string texture_cache_key(const scene::Texture& texture) {
            const scene::ImageTexture& image = std::get<scene::ImageTexture>(texture.data);
            const std::string identity       = image.asset.content_hash.empty() ? std::format("memory:{}", texture.id.value) : image.asset.content_hash;
            return std::format("{}:{}:{}:{}:{}:{}", identity, texture.revision.content, texture.revision.topology, std::to_underlying(image.wrap), std::to_underlying(image.filter), std::bit_cast<std::uint32_t>(image.maximum_anisotropy));
        }

    } // namespace

    GpuTextureImage upload_texture_image(VulkanRuntime& runtime, const scene::ImageTexture& data, const vk::Format format, const vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer& command_buffer) {
        GpuTextureImage result{runtime.resources.create_image_2d({data.width, data.height}, format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::ImageAspectFlagBits::eColor, static_cast<std::uint32_t>(data.mip_offsets.size())), runtime.frames.allocate_resource_descriptor(), runtime.frames.allocate_sampler_descriptor()};
        runtime.resources.write_sampled_image_descriptor(result.image_descriptor, result.image, vk::ImageLayout::eShaderReadOnlyOptimal);
        const vk::SamplerAddressMode address_mode = data.wrap == scene::TextureWrapMode::Repeat ? vk::SamplerAddressMode::eRepeat : data.wrap == scene::TextureWrapMode::Clamp ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eClampToBorder;
        const bool linear                         = data.filter != scene::TextureFilter::Point;
        runtime.resources.write_sampler_descriptor(result.sampler_descriptor, vk::SamplerCreateInfo{{}, linear ? vk::Filter::eLinear : vk::Filter::eNearest, linear ? vk::Filter::eLinear : vk::Filter::eNearest, data.filter == scene::TextureFilter::Trilinear || data.filter == scene::TextureFilter::Ewa ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest, address_mode, address_mode, address_mode, 0.0f, data.filter == scene::TextureFilter::Ewa ? vk::True : vk::False, data.maximum_anisotropy, vk::False, vk::CompareOp::eNever, 0.0f, static_cast<float>(data.mip_offsets.size() - 1u), vk::BorderColor::eFloatTransparentBlack});
        vk::DeviceSize texel_size{};
        GpuBuffer staging{};
        if (format == vk::Format::eR16G16B16A16Sfloat) {
            std::vector<std::uint16_t> texels(data.texels.size() * 4u);
            for (std::size_t index = 0; index != data.texels.size(); ++index) {
                texels[index * 4u]      = float_to_half(data.texels[index].x);
                texels[index * 4u + 1u] = float_to_half(data.texels[index].y);
                texels[index * 4u + 2u] = float_to_half(data.texels[index].z);
                texels[index * 4u + 3u] = float_to_half(data.texels[index].w);
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
        record(command_buffer);
        runtime.frames.defer_destruction([upload = std::move(staging)]() mutable {});
        return result;
    }

    namespace {
        [[nodiscard]] vk::AccelerationStructureGeometryKHR triangle_geometry(const GpuGeometry& mesh) {
            const vk::AccelerationStructureGeometryTrianglesDataKHR triangles{
                vk::Format::eR32G32B32Sfloat,
                vk::DeviceOrHostAddressConstKHR{mesh.positions.address},
                sizeof(math::Float3),
                std::max(mesh.vertex_count, 1u) - 1u,
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

        [[nodiscard]] vk::AccelerationStructureGeometryKHR sphere_set_geometry(const GpuSphereSet& spheres) {
            const vk::AccelerationStructureGeometryAabbsDataKHR aabbs{vk::DeviceOrHostAddressConstKHR{spheres.axis_aligned_boxes.address}, sizeof(vk::AabbPositionsKHR)};
            return {
                vk::GeometryTypeKHR::eAabbs,
                vk::AccelerationStructureGeometryDataKHR{aabbs},
                {},
            };
        }

    } // namespace

    scene::TriangleMeshGeometry tessellate_geometry(const scene::Geometry& geometry) {
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

    namespace {

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const std::span<const Element> elements, const vk::BufferUsageFlags usage, const std::size_t element_capacity = 0) {
            const std::array<Element, 1> empty{};
            const std::span<const Element> source = elements.empty() ? std::span<const Element>{empty} : elements;
            GpuBuffer destination                 = runtime.resources.create_buffer(std::max(source.size(), element_capacity) * sizeof(Element), usage | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            const GpuUploadSlice upload           = runtime.frames.stage_upload(std::as_bytes(source));
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

    void GpuScene::initialize(const scene::Scene& source_scene, const std::span<const GpuGeometryBinding> geometry_bindings, const std::span<const std::pair<scene::SphereSetId, std::uint32_t>> sphere_capacities) {
        GpuScene next{this->context.runtime, this->context.shader_directory};
        next.initialize_resources(source_scene.view(), geometry_bindings, sphere_capacities, nullptr);
        this->destroy();
        std::swap(this->resources, next.resources);
    }

    void GpuScene::initialize_resources(const scene::SceneView scene, const std::span<const GpuGeometryBinding> geometry_bindings, const std::span<const std::pair<scene::SphereSetId, std::uint32_t>> sphere_capacities, const vk::raii::CommandBuffer* command_buffer) {
        this->resources.dynamic_changes            = GpuSceneChange::None;
        this->resources.dynamic_revision           = 0;
        this->resources.dynamic_structure_revision = 0;
        this->resources.geometry_bindings          = {geometry_bindings.begin(), geometry_bindings.end()};
        const auto create_shader                   = [this](const std::string_view file, const char* entry) {
            const std::vector<std::uint32_t> code = load_spirv(this->context.shader_directory / file);
            return vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, code.size() * sizeof(std::uint32_t), code.data(), entry}};
        };
        this->resources.attribute_clear_shader         = create_shader("gpu_scene_attribute_clear.spv", "clear_missing_attributes");
        this->resources.attribute_accumulation_shader  = create_shader("gpu_scene_attribute_accumulation.spv", "accumulate_missing_attributes");
        this->resources.attribute_normalization_shader = create_shader("gpu_scene_attribute_normalization.spv", "normalize_missing_attributes");
        this->resources.bounds_clear_shader            = create_shader("gpu_scene_bounds_clear.spv", "clear_instance_bounds");
        this->resources.bounds_accumulation_shader     = create_shader("gpu_scene_bounds_accumulation.spv", "accumulate_instance_bounds");
        this->resources.sphere_unpack_shader           = create_shader("dataset_sphere_unpack.spv", "unpack_dataset_spheres");
        this->resources.instance_apply_shader          = create_shader("dataset_instance_apply.spv", "apply_instance_transforms");
        const auto record                              = [&](const vk::raii::CommandBuffer& target) {
            this->cache_texture_images(scene, target);
            this->resources.geometries.reserve(scene.resources.geometries.size());
            for (const scene::Geometry& geometry : scene.resources.geometries) this->resources.geometries.emplace_back(this->create_geometry(geometry, target));
            this->resources.sphere_sets.reserve(scene.resources.sphere_sets.size());
            for (const scene::SphereSet& spheres : scene.resources.sphere_sets) {
                const auto capacity = std::ranges::find(sphere_capacities, spheres.id, &std::pair<scene::SphereSetId, std::uint32_t>::first);
                this->resources.sphere_sets.emplace_back(this->create_sphere_set(spheres, target, capacity == sphere_capacities.end() ? 0 : capacity->second));
            }
            this->resources.volumes.reserve(scene.resources.volumes.size());
            for (const scene::Volume& volume : scene.resources.volumes) this->resources.volumes.emplace_back(this->create_volume(volume, target));

            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            const std::array<vk::AccelerationStructureInstanceKHR, 1> empty_instance_storage{};
            const std::uint32_t instance_capacity                       = static_cast<std::uint32_t>(std::max<std::size_t>(this->resources.primitives.size(), 1));
            const vk::BufferUsageFlags instance_usage                   = vk::BufferUsageFlagBits::eStorageBuffer | (this->context.runtime.graphics.ray_tracing_supported ? vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR : vk::BufferUsageFlags{});
            this->resources.acceleration_structure_instances            = upload_buffer(this->context.runtime, target, instances.empty() ? std::span<const vk::AccelerationStructureInstanceKHR>{empty_instance_storage} : std::span<const vk::AccelerationStructureInstanceKHR>{instances}, instance_usage, instance_capacity);
            this->resources.acceleration_structure_instances_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(this->resources.acceleration_structure_instances_descriptor, vk::DescriptorType::eStorageBuffer, this->resources.acceleration_structure_instances);
            if (this->context.runtime.graphics.ray_tracing_supported) this->resources.top_level_acceleration_structure = this->build_top_level(instances, instance_capacity, target);
            std::vector<math::Transform> primitive_transforms{};
            std::vector<DynamicInstanceBinding> instance_bindings{};
            std::vector<std::uint32_t> acceleration_indices(this->resources.primitives.size(), std::numeric_limits<std::uint32_t>::max());
            for (std::uint32_t acceleration_index = 0; acceleration_index < this->resources.acceleration_primitive_indices.size(); ++acceleration_index) acceleration_indices[this->resources.acceleration_primitive_indices[acceleration_index]] = acceleration_index;
            primitive_transforms.reserve(this->resources.primitives.size());
            instance_bindings.reserve(this->resources.primitives.size());
            for (const GpuScenePrimitive& primitive : this->resources.primitives) {
                const scene::Instance& instance   = scene.resources.instances[primitive.scene_instance_index];
                const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                const math::Transform local       = prototype.primitives[primitive.prototype_primitive_index].transform;
                primitive_transforms.emplace_back(instance.transform * local);
                instance_bindings.emplace_back(std::array{static_cast<std::uint32_t>(instance.id.value), static_cast<std::uint32_t>(instance.id.value >> 32u), acceleration_indices[primitive.scene_primitive_index], 0u}, local);
            }
            if (primitive_transforms.empty()) primitive_transforms.emplace_back();
            if (instance_bindings.empty()) instance_bindings.emplace_back();
            this->resources.primitive_transforms                 = upload_buffer(this->context.runtime, target, std::span<const math::Transform>{primitive_transforms}, vk::BufferUsageFlagBits::eStorageBuffer, primitive_transforms.size());
            this->resources.dynamic_instance_bindings            = upload_buffer(this->context.runtime, target, std::span<const DynamicInstanceBinding>{instance_bindings}, vk::BufferUsageFlagBits::eStorageBuffer, instance_bindings.size());
            this->resources.primitive_transforms_descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
            this->resources.dynamic_instance_bindings_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(this->resources.primitive_transforms_descriptor, vk::DescriptorType::eStorageBuffer, this->resources.primitive_transforms);
            this->context.runtime.resources.write_buffer_descriptor(this->resources.dynamic_instance_bindings_descriptor, vk::DescriptorType::eStorageBuffer, this->resources.dynamic_instance_bindings);
            const vk::DeviceSize instance_bounds_size  = std::max<std::size_t>(scene.resources.instances.size(), 1u) * 32u;
            this->resources.instance_bounds            = this->context.runtime.resources.create_buffer(instance_bounds_size, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            this->resources.instance_bounds_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(this->resources.instance_bounds_descriptor, vk::DescriptorType::eStorageBuffer, this->resources.instance_bounds);
            for (GpuBuffer& readback : this->resources.instance_bounds_readbacks) readback = this->context.runtime.resources.create_buffer(instance_bounds_size, vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            this->update_instance_bounds(scene, target, command_buffer ? this->context.runtime.frames.frame.current_slot_index : 0);
        };
        if (command_buffer)
            record(*command_buffer);
        else
            this->context.runtime.resources.submit_immediate(record);
        if (!command_buffer) this->resolve_instance_bounds(0);
        this->resources.instance_bounds_dirty = false;
        this->resources.synchronized_revision = scene.revision;
    }

    void GpuScene::destroy() noexcept {
        this->context.runtime.frames.defer_destruction([attribute_clear_shader = std::move(this->resources.attribute_clear_shader), attribute_accumulation_shader = std::move(this->resources.attribute_accumulation_shader), attribute_normalization_shader = std::move(this->resources.attribute_normalization_shader), bounds_clear_shader = std::move(this->resources.bounds_clear_shader), bounds_accumulation_shader = std::move(this->resources.bounds_accumulation_shader), sphere_unpack_shader = std::move(this->resources.sphere_unpack_shader), instance_apply_shader = std::move(this->resources.instance_apply_shader), texture_images = std::move(this->resources.texture_images), acceleration_instances = std::move(this->resources.acceleration_structure_instances), primitive_transforms = std::move(this->resources.primitive_transforms), instance_bindings = std::move(this->resources.dynamic_instance_bindings), instance_bounds = std::move(this->resources.instance_bounds),
                                                           bounds_readbacks = std::move(this->resources.instance_bounds_readbacks), immediate_scratch = std::move(this->resources.immediate_scratch), frame_scratch = std::move(this->resources.frame_scratch), geometries = std::move(this->resources.geometries), sphere_sets = std::move(this->resources.sphere_sets), volumes = std::move(this->resources.volumes), top_level = std::move(this->resources.top_level_acceleration_structure)]() mutable {});
        this->resources.texture_image_indices.clear();
        this->resources.acceleration_structure_instances_descriptor = {};
        this->resources.primitive_transforms_descriptor             = {};
        this->resources.dynamic_instance_bindings_descriptor        = {};
        this->resources.instance_bounds_descriptor                  = {};
        this->resources.instance_bounds_readback_counts             = {};
        this->resources.resolved_instance_bounds.clear();
        this->resources.resolved_scene_bounds = math::Bounds3::empty();
        this->resources.scratch_offsets       = {};
        this->resources.external_geometries.clear();
        this->resources.external_sphere_sets.clear();
        this->resources.external_volumes.clear();
        this->resources.geometry_bindings.clear();
        this->resources.primitives.clear();
        this->resources.acceleration_primitive_indices.clear();
        this->resources.primitive_instance_ids.clear();
        this->resources.acceleration_instance_ids.clear();
        this->resources.resource_binding_changes      = scene::SceneChange::None;
        this->resources.dynamic_changes               = GpuSceneChange::None;
        this->resources.dynamic_revision              = 0;
        this->resources.dynamic_structure_revision    = 0;
        this->resources.external_bottom_level_rebuilt = false;
        this->resources.instance_bounds_dirty         = false;
    }

    void GpuScene::cache_texture_images(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        std::map<std::pair<scene::TextureId, vk::Format>, std::size_t> previous_indices = std::move(this->resources.texture_image_indices);
        std::vector<GpuTextureImage> previous_images                                    = std::move(this->resources.texture_images);
        for (const scene::Texture& texture : scene.resources.textures) {
            const scene::ImageTexture* image = std::get_if<scene::ImageTexture>(&texture.data);
            if (!image) continue;
            const vk::Format format = texture.spectrum_type == scene::TextureSpectrumType::Albedo ? vk::Format::eR16G16B16A16Sfloat : vk::Format::eR32G32B32A32Sfloat;
            const std::pair key{texture.id, format};
            const std::string revision = texture_cache_key(texture);
            const std::size_t index    = this->resources.texture_images.size();
            const auto cached          = previous_indices.find(key);
            if (cached != previous_indices.end() && previous_images[cached->second].cache_revision == revision)
                this->resources.texture_images.emplace_back(std::move(previous_images[cached->second]));
            else {
                GpuTextureImage uploaded = upload_texture_image(this->context.runtime, *image, format, vk::PipelineStageFlagBits2::eAllCommands, command_buffer);
                uploaded.cache_revision  = revision;
                this->resources.texture_images.emplace_back(std::move(uploaded));
            }
            this->resources.texture_image_indices.emplace(key, index);
        }
        this->context.runtime.frames.defer_destruction([images = std::move(previous_images)]() mutable {});
    }

    const GpuTextureImage& GpuScene::texture_image(const scene::Texture& texture, const vk::Format format) const {
        return this->resources.texture_images[this->resources.texture_image_indices.at({texture.id, format})];
    }

    GpuSceneView GpuScene::view() const noexcept {
        return {
            this->resources.geometries,
            this->resources.sphere_sets,
            this->resources.volumes,
            this->resources.primitives,
            this->resources.acceleration_primitive_indices,
            this->resources.primitive_instance_ids,
            this->resources.acceleration_instance_ids,
            this->resources.top_level_acceleration_structure.address,
            this->resources.primitive_transforms_descriptor,
            &this->resources.primitive_transforms,
            this->resources.instance_bounds_descriptor,
            this->resources.resolved_instance_bounds,
            this->resources.resolved_scene_bounds,
            this->resources.dynamic_revision,
            this->resources.dynamic_structure_revision,
        };
    }

    GpuGeometry GpuScene::create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer& command_buffer) {
        const scene::TriangleMeshGeometry mesh = tessellate_geometry(geometry);
        GpuGeometry result{};
        result.geometry_id                  = geometry.id;
        const auto binding                  = std::ranges::find(this->resources.geometry_bindings, geometry.id, &GpuGeometryBinding::geometry_id);
        result.update_mode                  = binding == this->resources.geometry_bindings.end() ? GpuMeshUpdateMode::Immutable : binding->update_mode;
        result.acceleration_kind            = std::holds_alternative<scene::SphereGeometry>(geometry.data) || std::holds_alternative<scene::DiskGeometry>(geometry.data) || std::holds_alternative<scene::CylinderGeometry>(geometry.data) ? AccelerationGeometryKind::Procedural : AccelerationGeometryKind::Triangle;
        result.vertex_count                 = static_cast<std::uint32_t>(mesh.positions.size());
        result.index_count                  = static_cast<std::uint32_t>(mesh.indices.size());
        result.vertex_capacity              = std::max({result.vertex_count, binding == this->resources.geometry_bindings.end() ? 0u : binding->vertex_capacity, 1u});
        result.index_capacity               = std::max({result.index_count, binding == this->resources.geometry_bindings.end() ? 0u : binding->index_capacity, 3u});
        result.acceleration_primitive_count = result.acceleration_kind == AccelerationGeometryKind::Triangle ? result.index_count / 3u : 1u;
        result.attribute_mask               = (mesh.normals.empty() ? 0u : gpu_geometry_attribute_normal) | (mesh.tangents.empty() ? 0u : gpu_geometry_attribute_tangent) | (mesh.texture_coordinates.empty() ? 0u : gpu_geometry_attribute_texture_coordinate);
        const std::array<math::Float3, 1> missing_float3{};
        const std::array<math::Float2, 1> missing_float2{};
        const std::array<std::uint32_t, 3> missing_indices{};
        const std::span<const math::Float3> positions           = mesh.positions.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{mesh.positions};
        const std::span<const math::Float3> normals             = mesh.normals.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{mesh.normals};
        const std::span<const math::Float3> tangents            = mesh.tangents.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{mesh.tangents};
        const std::span<const math::Float2> texture_coordinates = mesh.texture_coordinates.empty() ? std::span<const math::Float2>{missing_float2} : std::span<const math::Float2>{mesh.texture_coordinates};
        const std::span<const std::uint32_t> indices            = mesh.indices.empty() ? std::span<const std::uint32_t>{missing_indices} : std::span<const std::uint32_t>{mesh.indices};
        const vk::BufferUsageFlags position_usage               = vk::BufferUsageFlagBits::eStorageBuffer | (this->context.runtime.graphics.ray_tracing_supported ? vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR : vk::BufferUsageFlags{});
        const vk::BufferUsageFlags attribute_usage              = vk::BufferUsageFlagBits::eStorageBuffer;
        const std::uint32_t optional_attribute_capacity         = result.update_mode == GpuMeshUpdateMode::Immutable ? 1u : result.vertex_capacity;
        result.positions                                        = upload_buffer(this->context.runtime, command_buffer, positions, position_usage, result.vertex_capacity);
        result.normals                                          = upload_buffer(this->context.runtime, command_buffer, normals, attribute_usage, result.vertex_capacity);
        result.tangents                                         = upload_buffer(this->context.runtime, command_buffer, tangents, attribute_usage, result.vertex_capacity);
        result.texture_coordinates                              = upload_buffer(this->context.runtime, command_buffer, texture_coordinates, attribute_usage, mesh.texture_coordinates.empty() ? optional_attribute_capacity : result.vertex_capacity);
        result.indices                                          = upload_buffer(this->context.runtime, command_buffer, indices, position_usage, result.index_capacity);
        if (result.acceleration_kind == AccelerationGeometryKind::Procedural && this->context.runtime.graphics.ray_tracing_supported) {
            const math::Bounds3 bounds = scene::geometry_bounds(geometry);
            const std::array aabbs{vk::AabbPositionsKHR{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}};
            result.axis_aligned_boxes = upload_buffer(this->context.runtime, command_buffer, std::span<const vk::AabbPositionsKHR>{aabbs}, vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        }
        result.positions_descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
        result.normals_descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
        result.tangents_descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
        result.texture_coordinates_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        result.indices_descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->context.runtime.resources.write_buffer_descriptor(result.normals_descriptor, vk::DescriptorType::eStorageBuffer, result.normals);
        this->context.runtime.resources.write_buffer_descriptor(result.tangents_descriptor, vk::DescriptorType::eStorageBuffer, result.tangents);
        this->context.runtime.resources.write_buffer_descriptor(result.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, result.texture_coordinates);
        this->context.runtime.resources.write_buffer_descriptor(result.indices_descriptor, vk::DescriptorType::eStorageBuffer, result.indices);
        if (result.vertex_count != 0 && (mesh.normals.empty() || mesh.tangents.empty())) this->generate_missing_attributes(result, mesh.normals.empty(), mesh.tangents.empty(), command_buffer);
        if (this->context.runtime.graphics.ray_tracing_supported) result.bottom_level_acceleration_structure = this->build_bottom_level(result.acceleration_kind == AccelerationGeometryKind::Triangle ? triangle_geometry(result) : procedural_geometry(result), result.acceleration_primitive_count, result.update_mode, command_buffer, result.acceleration_kind == AccelerationGeometryKind::Triangle ? std::max(result.index_capacity / 3u, 1u) : 1u);
        result.cpu_data_stale = result.update_mode != GpuMeshUpdateMode::Immutable;
        return result;
    }

    GpuSphereSet GpuScene::create_sphere_set(const scene::SphereSet& spheres, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t capacity) {
        GpuSphereSet result{};
        result.sphere_set_id   = spheres.id;
        result.sphere_count    = static_cast<std::uint32_t>(spheres.positions.size());
        result.sphere_capacity = std::max({result.sphere_count, capacity, 1u});
        std::vector<vk::AabbPositionsKHR> axis_aligned_boxes{};
        axis_aligned_boxes.reserve(result.sphere_count);
        for (std::size_t index = 0; index != spheres.positions.size(); ++index) {
            const float radius = spheres.radii[index];
            const math::Float3 extent{radius, radius, radius};
            const math::Float3 minimum = spheres.positions[index] - extent;
            const math::Float3 maximum = spheres.positions[index] + extent;
            axis_aligned_boxes.emplace_back(minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
        }
        const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer;
        result.positions                           = upload_buffer(this->context.runtime, command_buffer, std::span<const math::Float3>{spheres.positions}, attribute_usage, result.sphere_capacity);
        result.radii                               = upload_buffer(this->context.runtime, command_buffer, std::span<const float>{spheres.radii}, attribute_usage, result.sphere_capacity);
        const vk::BufferUsageFlags aabb_usage      = vk::BufferUsageFlagBits::eStorageBuffer | (this->context.runtime.graphics.ray_tracing_supported ? vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR : vk::BufferUsageFlags{});
        result.axis_aligned_boxes                  = upload_buffer(this->context.runtime, command_buffer, std::span<const vk::AabbPositionsKHR>{axis_aligned_boxes}, aabb_usage, result.sphere_capacity);
        result.positions_descriptor                = this->context.runtime.frames.allocate_resource_descriptor();
        result.radii_descriptor                    = this->context.runtime.frames.allocate_resource_descriptor();
        result.axis_aligned_boxes_descriptor       = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(result.positions_descriptor, vk::DescriptorType::eStorageBuffer, result.positions);
        this->context.runtime.resources.write_buffer_descriptor(result.radii_descriptor, vk::DescriptorType::eStorageBuffer, result.radii);
        this->context.runtime.resources.write_buffer_descriptor(result.axis_aligned_boxes_descriptor, vk::DescriptorType::eStorageBuffer, result.axis_aligned_boxes);
        if (this->context.runtime.graphics.ray_tracing_supported) result.bottom_level_acceleration_structure = this->build_bottom_level(sphere_set_geometry(result), result.sphere_count, GpuMeshUpdateMode::Deformable, command_buffer, result.sphere_capacity);
        return result;
    }

    GpuVolume GpuScene::create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer& command_buffer) {
        GpuVolume result{};
        result.volume_id  = volume.id;
        result.revision   = volume.revision;
        const auto upload = [this, &command_buffer, &result](const GpuVolumeField field, const auto values) {
            if (values.empty()) return;
            const std::size_t index   = std::to_underlying(field);
            result.fields[index]      = upload_buffer(this->context.runtime, command_buffer, values, vk::BufferUsageFlagBits::eStorageBuffer);
            result.descriptors[index] = this->context.runtime.frames.allocate_resource_descriptor();
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
                const scene::Primitive& primitive                       = prototype->primitives[primitive_index];
                const std::vector<GpuGeometry>::const_iterator mesh     = std::ranges::find(this->resources.geometries, primitive.geometry, &GpuGeometry::geometry_id);
                const std::vector<GpuSphereSet>::const_iterator spheres = std::ranges::find(this->resources.sphere_sets, primitive.spheres, &GpuSphereSet::sphere_set_id);
                if (mesh == this->resources.geometries.end() && spheres == this->resources.sphere_sets.end()) throw std::runtime_error("Every compiled surface Primitive requires Geometry or a SphereSet");

                const bool sphere_draw                    = spheres != this->resources.sphere_sets.end();
                const std::uint32_t scene_primitive_index = static_cast<std::uint32_t>(this->resources.primitives.size());
                this->resources.primitives.emplace_back(sphere_draw ? GpuScenePrimitiveKind::SphereSet : GpuScenePrimitiveKind::Geometry, static_cast<std::uint32_t>(sphere_draw ? spheres - this->resources.sphere_sets.begin() : mesh - this->resources.geometries.begin()), scene_primitive_index, instance_index, primitive_index);
                this->resources.primitive_instance_ids.push_back(instance.id);
                if (!this->context.runtime.graphics.ray_tracing_supported) continue;
                if (sphere_draw && spheres->sphere_count == 0) continue;

                const math::Transform world_transform = instance.transform * primitive.transform;
                vk::TransformMatrixKHR transform{};
                for (std::uint32_t row = 0; row < 3; ++row)
                    for (std::uint32_t column = 0; column < 4; ++column) transform.matrix[row][column] = world_transform.matrix[row * 4u + column];
                vk::GeometryInstanceFlagsKHR instance_flags = vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
                if (primitive.alpha.value == 0) instance_flags |= vk::GeometryInstanceFlagBitsKHR::eForceOpaque;
                const std::vector<scene::Material>::const_iterator material = std::ranges::find(scene.resources.materials, primitive.material, &scene::Material::id);
                const bool volume_boundary                                  = (primitive.media.inside.value != 0 || primitive.media.outside.value != 0) && material != scene.resources.materials.end() && std::holds_alternative<scene::InterfaceMaterialData>(material->data);
                const std::uint32_t acceleration_index                      = static_cast<std::uint32_t>(instances.size());
                const bool procedural                                       = sphere_draw || mesh->acceleration_kind == AccelerationGeometryKind::Procedural;
                const vk::DeviceAddress acceleration_address                = sphere_draw ? spheres->bottom_level_acceleration_structure.address : mesh->bottom_level_acceleration_structure.address;
                instances.emplace_back(transform, acceleration_index, volume_boundary ? 0x80u : 0x7fu, procedural ? 1u : 0u, instance_flags, acceleration_address);
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
        const GpuUploadSlice position_upload           = this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Float3>{
            triangle_mesh.positions,
        }));
        const GpuUploadSlice normal_upload             = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.normals.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{triangle_mesh.normals}));
        const GpuUploadSlice tangent_upload            = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.tangents.empty() ? std::span<const math::Float3>{missing_float3} : std::span<const math::Float3>{triangle_mesh.tangents}));
        const GpuUploadSlice texture_coordinate_upload = this->context.runtime.frames.stage_upload(std::as_bytes(triangle_mesh.texture_coordinates.empty() ? std::span<const math::Float2>{missing_float2} : std::span<const math::Float2>{triangle_mesh.texture_coordinates}));
        if (position_upload.size != 0) command_buffer.copyBuffer(position_upload.buffer, *gpu_geometry.positions.buffer, vk::BufferCopy{position_upload.offset, 0, position_upload.size});
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
                vk::PipelineStageFlagBits2::eAllCommands | (this->context.runtime.graphics.ray_tracing_supported ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlags2{}),
                vk::AccessFlagBits2::eShaderStorageRead | (this->context.runtime.graphics.ray_tracing_supported ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlags2{}),
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
        if (triangle_mesh.normals.empty() || triangle_mesh.tangents.empty()) this->generate_missing_attributes(gpu_geometry, triangle_mesh.normals.empty(), triangle_mesh.tangents.empty(), command_buffer);
        if (this->context.runtime.graphics.ray_tracing_supported) {
            const vk::AccelerationStructureGeometryKHR geometry = gpu_geometry.acceleration_kind == AccelerationGeometryKind::Triangle ? triangle_geometry(gpu_geometry) : procedural_geometry(gpu_geometry);
            const std::uint32_t primitive_count                 = gpu_geometry.acceleration_primitive_count;
            vk::AccelerationStructureBuildGeometryInfoKHR build_info{vk::AccelerationStructureTypeKHR::eBottomLevel, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate, vk::BuildAccelerationStructureModeKHR::eUpdate, *gpu_geometry.bottom_level_acceleration_structure.acceleration_structure, *gpu_geometry.bottom_level_acceleration_structure.acceleration_structure, 1, &geometry};
            const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, primitive_count);
            build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
            const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
            const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
            command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        }
        gpu_geometry.cpu_data_stale = true;
    }

    void GpuScene::generate_missing_attributes(GpuGeometry& geometry, const bool generate_normals, const bool generate_tangents, const vk::raii::CommandBuffer& command_buffer) {
        struct alignas(16) GpuSceneAttributePushData {
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
        static_assert(sizeof(GpuSceneAttributePushData) == 64);
        const GpuSceneAttributePushData push_data{
            geometry.positions_descriptor,
            geometry.normals_descriptor,
            geometry.tangents_descriptor,
            geometry.texture_coordinates_descriptor,
            geometry.indices_descriptor,
            {},
            geometry.vertex_count,
            geometry.index_count,
            (generate_normals ? gpu_geometry_attribute_normal : 0u) | (generate_tangents ? gpu_geometry_attribute_tangent : 0u) | (geometry.attribute_mask & gpu_geometry_attribute_texture_coordinate),
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
        const vk::MemoryBarrier2 output_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAllCommands | (this->context.runtime.graphics.ray_tracing_supported ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlags2{}), vk::AccessFlagBits2::eShaderStorageRead | (this->context.runtime.graphics.ray_tracing_supported ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlags2{})};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &output_dependency});
        if (generate_normals) geometry.attribute_mask |= gpu_geometry_attribute_normal;
        if (generate_tangents) geometry.attribute_mask |= gpu_geometry_attribute_tangent;
    }

    void GpuScene::synchronize_external_geometry(const scene::GeometryId geometry_id, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, const std::uint32_t vertex_count, const std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer) {
        if (this->resources.external_geometries.empty()) this->resources.scratch_offsets[this->context.runtime.frames.frame.current_slot_index] = 0;
        GpuGeometry& mesh                           = *std::ranges::find(this->resources.geometries, geometry_id, &GpuGeometry::geometry_id);
        const std::uint32_t previous_attribute_mask = mesh.attribute_mask;
        if (mesh.update_mode == GpuMeshUpdateMode::Immutable) throw std::runtime_error("Dynamic Geometry requires a dynamic update mode");
        const bool vertex_attribute = positions || normals || tangents || texture_coordinates;
        if (vertex_attribute && !positions && vertex_count != mesh.vertex_count) throw std::runtime_error("A TriangleMesh vertex count change must publish positions");
        const std::uint32_t updated_vertex_count = positions ? vertex_count : mesh.vertex_count;
        const std::uint32_t updated_index_count  = indices ? index_count : mesh.index_count;
        if (mesh.update_mode == GpuMeshUpdateMode::Deformable && (updated_vertex_count != mesh.vertex_count || indices)) throw std::runtime_error("Dynamic deformable Geometry changed topology");
        if (mesh.update_mode == GpuMeshUpdateMode::TopologyChanging) {
            const bool reallocate = updated_vertex_count > mesh.vertex_capacity || updated_index_count > mesh.index_capacity;
            if (updated_vertex_count > mesh.vertex_count && !texture_coordinates && (mesh.attribute_mask & gpu_geometry_attribute_texture_coordinate) != 0) throw std::runtime_error("Dynamic topology-changing Geometry must publish texture coordinates when adding textured vertices");
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
                const vk::BufferUsageFlags geometry_usage  = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | (this->context.runtime.graphics.ray_tracing_supported ? vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR : vk::BufferUsageFlags{});
                const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
                replacement.positions                      = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.normals                        = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.tangents                       = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.texture_coordinates            = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.vertex_capacity) * sizeof(math::Float2), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.indices                        = this->context.runtime.resources.create_buffer(static_cast<vk::DeviceSize>(replacement.index_capacity) * sizeof(std::uint32_t), geometry_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
                replacement.positions_descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
                replacement.normals_descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
                replacement.tangents_descriptor            = this->context.runtime.frames.allocate_resource_descriptor();
                replacement.texture_coordinates_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
                replacement.indices_descriptor             = this->context.runtime.frames.allocate_resource_descriptor();
                this->context.runtime.resources.write_buffer_descriptor(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
                this->context.runtime.resources.write_buffer_descriptor(replacement.normals_descriptor, vk::DescriptorType::eStorageBuffer, replacement.normals);
                this->context.runtime.resources.write_buffer_descriptor(replacement.tangents_descriptor, vk::DescriptorType::eStorageBuffer, replacement.tangents);
                this->context.runtime.resources.write_buffer_descriptor(replacement.texture_coordinates_descriptor, vk::DescriptorType::eStorageBuffer, replacement.texture_coordinates);
                this->context.runtime.resources.write_buffer_descriptor(replacement.indices_descriptor, vk::DescriptorType::eStorageBuffer, replacement.indices);
                const std::uint32_t preserved_vertices = std::min(mesh.vertex_count, updated_vertex_count);
                if (preserved_vertices != 0 && !positions) command_buffer.copyBuffer(*mesh.positions.buffer, *replacement.positions.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !normals && (mesh.attribute_mask & gpu_geometry_attribute_normal) != 0) command_buffer.copyBuffer(*mesh.normals.buffer, *replacement.normals.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !tangents && (mesh.attribute_mask & gpu_geometry_attribute_tangent) != 0) command_buffer.copyBuffer(*mesh.tangents.buffer, *replacement.tangents.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float3)});
                if (preserved_vertices != 0 && !texture_coordinates && (mesh.attribute_mask & gpu_geometry_attribute_texture_coordinate) != 0) command_buffer.copyBuffer(*mesh.texture_coordinates.buffer, *replacement.texture_coordinates.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_vertices) * sizeof(math::Float2)});
                const std::uint32_t preserved_indices = std::min(mesh.index_count, updated_index_count);
                if (preserved_indices != 0 && !indices) command_buffer.copyBuffer(*mesh.indices.buffer, *replacement.indices.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(preserved_indices) * sizeof(std::uint32_t)});
                this->context.runtime.frames.defer_destruction([previous = std::move(mesh)]() mutable {});
                mesh                            = std::move(replacement);
                this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Structure;
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

        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAllCommands | vk::PipelineStageFlagBits2::eComputeShader | (this->context.runtime.graphics.ray_tracing_supported ? vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR : vk::PipelineStageFlags2{}), vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | (this->context.runtime.graphics.ray_tracing_supported ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlags2{})};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
        if (normals) mesh.attribute_mask |= gpu_geometry_attribute_normal;
        if (tangents) mesh.attribute_mask |= gpu_geometry_attribute_tangent;
        if (texture_coordinates) mesh.attribute_mask |= gpu_geometry_attribute_texture_coordinate;
        const bool geometry_changed  = positions || indices;
        const bool generate_normals  = geometry_changed && !normals;
        const bool generate_tangents = (geometry_changed || normals || texture_coordinates) && !tangents;
        if (generate_normals || generate_tangents) this->generate_missing_attributes(mesh, generate_normals, generate_tangents, command_buffer);
        if (mesh.attribute_mask != previous_attribute_mask) this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Structure;

        if (!std::ranges::contains(this->resources.external_geometries, geometry_id)) this->resources.external_geometries.push_back(geometry_id);
        mesh.cpu_data_stale = true;
        if (positions) this->resources.instance_bounds_dirty = true;
        if (!geometry_changed) return;
        if (!this->context.runtime.graphics.ray_tracing_supported) return;

        const vk::AccelerationStructureGeometryKHR geometry = triangle_geometry(mesh);
        if (mesh.update_mode == GpuMeshUpdateMode::TopologyChanging) {
            GpuAccelerationStructure replacement = this->build_bottom_level(geometry, mesh.acceleration_primitive_count, mesh.update_mode, command_buffer);
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

    void GpuScene::update_sphere_set(GpuSphereSet& gpu_spheres, const scene::SphereSet& source_spheres, const vk::raii::CommandBuffer& command_buffer) {
        if (source_spheres.positions.empty()) {
            this->update_sphere_set_acceleration(gpu_spheres, command_buffer);
            return;
        }
        std::vector<vk::AabbPositionsKHR> axis_aligned_boxes{};
        axis_aligned_boxes.reserve(source_spheres.positions.size());
        for (std::size_t index = 0; index != source_spheres.positions.size(); ++index) {
            const float radius = source_spheres.radii[index];
            const math::Float3 extent{radius, radius, radius};
            const math::Float3 minimum = source_spheres.positions[index] - extent;
            const math::Float3 maximum = source_spheres.positions[index] + extent;
            axis_aligned_boxes.emplace_back(minimum.x, minimum.y, minimum.z, maximum.x, maximum.y, maximum.z);
        }
        const std::array uploads{
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Float3>{source_spheres.positions})),
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const float>{source_spheres.radii})),
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const vk::AabbPositionsKHR>{axis_aligned_boxes})),
        };
        const std::array<GpuBuffer*, 3> destinations{&gpu_spheres.positions, &gpu_spheres.radii, &gpu_spheres.axis_aligned_boxes};
        for (std::size_t index = 0; index != uploads.size(); ++index) command_buffer.copyBuffer(uploads[index].buffer, *destinations[index]->buffer, vk::BufferCopy{uploads[index].offset, 0, uploads[index].size});
        const vk::MemoryBarrier2 upload_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead | (this->context.runtime.graphics.ray_tracing_supported ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlags2{})};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &upload_dependency});
        this->update_sphere_set_acceleration(gpu_spheres, command_buffer);
    }

    void GpuScene::update_sphere_set_acceleration(GpuSphereSet& spheres, const vk::raii::CommandBuffer& command_buffer) {
        if (!this->context.runtime.graphics.ray_tracing_supported) return;
        const vk::AccelerationStructureGeometryKHR geometry = sphere_set_geometry(spheres);
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *spheres.bottom_level_acceleration_structure.acceleration_structure,
            *spheres.bottom_level_acceleration_structure.acceleration_structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, spheres.sphere_capacity);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{spheres.sphere_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    void GpuScene::synchronize_external_sphere_set(const scene::SphereSetId sphere_set_id, const DescriptorHandle spheres_descriptor, const std::uint32_t sphere_count, const vk::raii::CommandBuffer& command_buffer) {
        GpuSphereSet& spheres                    = *std::ranges::find(this->resources.sphere_sets, sphere_set_id, &GpuSphereSet::sphere_set_id);
        const bool acceleration_presence_changed = (spheres.sphere_count == 0) != (sphere_count == 0);
        bool rebuild_acceleration{};
        if (sphere_count > spheres.sphere_capacity) {
            GpuSphereSet replacement{};
            replacement.sphere_set_id                  = spheres.sphere_set_id;
            replacement.sphere_count                   = sphere_count;
            replacement.sphere_capacity                = std::bit_ceil(std::max(sphere_count, 1u));
            const vk::DeviceSize capacity              = replacement.sphere_capacity;
            const vk::BufferUsageFlags attribute_usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            replacement.positions                      = this->context.runtime.resources.create_buffer(capacity * sizeof(math::Float3), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.radii                          = this->context.runtime.resources.create_buffer(capacity * sizeof(float), attribute_usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.axis_aligned_boxes             = this->context.runtime.resources.create_buffer(capacity * sizeof(vk::AabbPositionsKHR), attribute_usage | (this->context.runtime.graphics.ray_tracing_supported ? vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR : vk::BufferUsageFlags{}), vk::MemoryPropertyFlagBits::eDeviceLocal, false);
            replacement.positions_descriptor           = this->context.runtime.frames.allocate_resource_descriptor();
            replacement.radii_descriptor               = this->context.runtime.frames.allocate_resource_descriptor();
            replacement.axis_aligned_boxes_descriptor  = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(replacement.positions_descriptor, vk::DescriptorType::eStorageBuffer, replacement.positions);
            this->context.runtime.resources.write_buffer_descriptor(replacement.radii_descriptor, vk::DescriptorType::eStorageBuffer, replacement.radii);
            this->context.runtime.resources.write_buffer_descriptor(replacement.axis_aligned_boxes_descriptor, vk::DescriptorType::eStorageBuffer, replacement.axis_aligned_boxes);
            this->context.runtime.frames.defer_destruction([previous = std::move(spheres)]() mutable {});
            spheres                         = std::move(replacement);
            this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Structure;
            rebuild_acceleration            = true;
        } else
            spheres.sphere_count = sphere_count;

        if (sphere_count != 0) {
            struct alignas(8) SphereUnpackPushData {
                DescriptorHandle source{};
                DescriptorHandle positions{};
                DescriptorHandle radii{};
                DescriptorHandle axis_aligned_boxes{};
                std::uint32_t point_count{};
                std::uint32_t reserved{};
            };
            static_assert(sizeof(SphereUnpackPushData) == 40);
            const SphereUnpackPushData push_data{spheres_descriptor, spheres.positions_descriptor, spheres.radii_descriptor, spheres.axis_aligned_boxes_descriptor, sphere_count, 0};
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.sphere_unpack_shader);
            command_buffer.dispatch((sphere_count + 255u) / 256u, 1, 1);
        }
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderStorageRead | (this->context.runtime.graphics.ray_tracing_supported ? vk::AccessFlagBits2::eAccelerationStructureReadKHR : vk::AccessFlags2{})};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        if (!this->context.runtime.graphics.ray_tracing_supported) {
            if (acceleration_presence_changed) this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Structure;
            if (!std::ranges::contains(this->resources.external_sphere_sets, sphere_set_id)) this->resources.external_sphere_sets.push_back(sphere_set_id);
            spheres.cpu_data_stale                = true;
            this->resources.instance_bounds_dirty = true;
            return;
        }
        if (rebuild_acceleration) {
            spheres.bottom_level_acceleration_structure   = this->build_bottom_level(sphere_set_geometry(spheres), spheres.sphere_count, GpuMeshUpdateMode::Deformable, command_buffer, spheres.sphere_capacity);
            this->resources.external_bottom_level_rebuilt = true;
        } else
            this->update_sphere_set_acceleration(spheres, command_buffer);
        if (acceleration_presence_changed) this->resources.external_bottom_level_rebuilt = true;
        if (acceleration_presence_changed) this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Structure;
        if (!std::ranges::contains(this->resources.external_sphere_sets, sphere_set_id)) this->resources.external_sphere_sets.push_back(sphere_set_id);
        spheres.cpu_data_stale                = true;
        this->resources.instance_bounds_dirty = true;
    }

    void GpuScene::synchronize_external_instance_transforms(const dynamics::GpuInstanceTransformUpdate& update, const vk::raii::CommandBuffer& command_buffer) {
        if (update.count == 0 || this->resources.primitives.empty()) return;
        struct alignas(8) InstanceApplyPushData {
            DescriptorHandle updates{};
            DescriptorHandle bindings{};
            DescriptorHandle transforms{};
            DescriptorHandle acceleration_instances{};
            std::uint32_t update_count{};
            std::uint32_t primitive_count{};
        };
        static_assert(sizeof(InstanceApplyPushData) == 40);
        const InstanceApplyPushData push_data{update.instances.descriptor, this->resources.dynamic_instance_bindings_descriptor, this->resources.primitive_transforms_descriptor, this->resources.acceleration_structure_instances_descriptor, static_cast<std::uint32_t>(update.count), static_cast<std::uint32_t>(this->resources.primitives.size())};
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.instance_apply_shader);
        command_buffer.dispatch((push_data.primitive_count + 63u) / 64u, 1, 1);
        const vk::MemoryBarrier2 transform_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader | (this->context.runtime.graphics.ray_tracing_supported ? vk::PipelineStageFlagBits2::eRayTracingShaderKHR : vk::PipelineStageFlags2{}), vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &transform_dependency});
        this->resources.dynamic_changes       = this->resources.dynamic_changes | GpuSceneChange::Transform;
        this->resources.instance_bounds_dirty = true;
    }

    void GpuScene::synchronize_external_volume(const scene::VolumeId volume_id, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, const std::uint64_t voxel_count, const scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer) {
        GpuVolume& volume                  = *std::ranges::find(this->resources.volumes, volume_id, &GpuVolume::volume_id);
        const std::uint64_t expected_count = static_cast<std::uint64_t>(dirty_region.maximum.x - dirty_region.minimum.x) * (dirty_region.maximum.y - dirty_region.minimum.y) * (dirty_region.maximum.z - dirty_region.minimum.z);
        if (voxel_count != expected_count) throw std::runtime_error("Dynamic Volume element count differs from its dirty region");
        const auto copy = [&command_buffer, dirty_region, &volume](const GpuBuffer* source, const GpuVolumeField field, const vk::DeviceSize element_size) {
            if (!source) return;
            const std::size_t index = std::to_underlying(field);
            if (!volume.field_present[index]) throw std::runtime_error("Dynamic Volume published a field absent from its Scene resource");
            std::vector<vk::BufferCopy> regions{};
            const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(dirty_region.maximum.x - dirty_region.minimum.x) * element_size;
            vk::DeviceSize source_offset{};
            for (std::uint32_t z = dirty_region.minimum.z; z != dirty_region.maximum.z; ++z)
                for (std::uint32_t y = dirty_region.minimum.y; y != dirty_region.maximum.y; ++y) {
                    const vk::DeviceSize destination_offset = (static_cast<vk::DeviceSize>(z) * volume.resolution.y * volume.resolution.x + static_cast<vk::DeviceSize>(y) * volume.resolution.x + dirty_region.minimum.x) * element_size;
                    regions.emplace_back(source_offset, destination_offset, bytes);
                    source_offset += bytes;
                }
            if (source->size < source_offset) throw std::runtime_error("Dynamic Volume field buffer is smaller than its dirty region");
            command_buffer.copyBuffer(*source->buffer, *volume.fields[index].buffer, regions);
        };
        copy(density, GpuVolumeField::Density, sizeof(float));
        copy(temperature, GpuVolumeField::Temperature, sizeof(float));
        copy(emission_scale, GpuVolumeField::EmissionScale, sizeof(float));
        copy(sigma_a, GpuVolumeField::SigmaA, sizeof(math::Float3));
        copy(sigma_s, GpuVolumeField::SigmaS, sizeof(math::Float3));
        copy(emission, GpuVolumeField::Emission, sizeof(math::Float3));
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader | (this->context.runtime.graphics.ray_tracing_supported ? vk::PipelineStageFlagBits2::eRayTracingShaderKHR : vk::PipelineStageFlags2{}), vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        if (!std::ranges::contains(this->resources.external_volumes, volume_id)) this->resources.external_volumes.push_back(volume_id);
        if (volume.dirty_region) {
            volume.dirty_region->minimum = {
                std::min(volume.dirty_region->minimum.x, dirty_region.minimum.x),
                std::min(volume.dirty_region->minimum.y, dirty_region.minimum.y),
                std::min(volume.dirty_region->minimum.z, dirty_region.minimum.z),
            };
            volume.dirty_region->maximum = {
                std::max(volume.dirty_region->maximum.x, dirty_region.maximum.x),
                std::max(volume.dirty_region->maximum.y, dirty_region.maximum.y),
                std::max(volume.dirty_region->maximum.z, dirty_region.maximum.z),
            };
        } else
            volume.dirty_region = dirty_region;
        ++volume.revision.content;
        volume.cpu_data_stale = true;
    }

    void GpuScene::update_volumes(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        for (const scene::Volume& source : scene.resources.volumes) {
            GpuVolume& volume = *std::ranges::find(this->resources.volumes, source.id, &GpuVolume::volume_id);
            if (std::ranges::contains(this->resources.external_volumes, source.id) || source.revision.content == volume.revision.content) continue;
            GpuVolume replacement = this->create_volume(source, command_buffer);
            this->context.runtime.frames.defer_destruction([previous = std::move(volume)]() mutable {});
            volume                                   = std::move(replacement);
            this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Volume;
        }
    }

    void GpuScene::update_instance_state(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        std::vector<math::Transform> primitive_transforms{};
        std::vector<DynamicInstanceBinding> instance_bindings{};
        std::vector<std::uint32_t> acceleration_indices(this->resources.primitives.size(), std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t acceleration_index = 0; acceleration_index < this->resources.acceleration_primitive_indices.size(); ++acceleration_index) acceleration_indices[this->resources.acceleration_primitive_indices[acceleration_index]] = acceleration_index;
        primitive_transforms.reserve(this->resources.primitives.size());
        instance_bindings.reserve(this->resources.primitives.size());
        for (const GpuScenePrimitive& primitive : this->resources.primitives) {
            const scene::Instance& instance   = scene.resources.instances[primitive.scene_instance_index];
            const scene::Prototype& prototype = *std::ranges::find(scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const math::Transform local       = prototype.primitives[primitive.prototype_primitive_index].transform;
            primitive_transforms.emplace_back(instance.transform * local);
            instance_bindings.emplace_back(std::array{static_cast<std::uint32_t>(instance.id.value), static_cast<std::uint32_t>(instance.id.value >> 32u), acceleration_indices[primitive.scene_primitive_index], 0u}, local);
        }
        if (primitive_transforms.empty()) primitive_transforms.emplace_back();
        if (instance_bindings.empty()) instance_bindings.emplace_back();
        const std::array uploads{
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const math::Transform>{primitive_transforms})),
            this->context.runtime.frames.stage_upload(std::as_bytes(std::span<const DynamicInstanceBinding>{instance_bindings})),
        };
        command_buffer.copyBuffer(uploads[0].buffer, *this->resources.primitive_transforms.buffer, vk::BufferCopy{uploads[0].offset, 0, uploads[0].size});
        command_buffer.copyBuffer(uploads[1].buffer, *this->resources.dynamic_instance_bindings.buffer, vk::BufferCopy{uploads[1].offset, 0, uploads[1].size});
        const vk::MemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eMeshShaderEXT | vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &dependency});
        this->resources.instance_bounds_dirty = true;
    }

    void GpuScene::update_instance_bounds(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        const std::uint32_t instance_count = static_cast<std::uint32_t>(scene.resources.instances.size());
        if (instance_count == 0) {
            this->resources.instance_bounds_readback_counts[frame_slot_index] = 0;
            return;
        }
        struct alignas(16) GpuSceneBoundsPushData {
            DescriptorHandle positions{};
            DescriptorHandle radii{};
            DescriptorHandle bounds{};
            DescriptorHandle transforms{};
            std::array<std::uint32_t, 4> metadata{};
        };
        static_assert(sizeof(GpuSceneBoundsPushData) == 48);
        const GpuSceneBoundsPushData clear_push{{}, {}, this->resources.instance_bounds_descriptor, this->resources.primitive_transforms_descriptor, {instance_count, 0, 0, 0}};
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&clear_push, 1}));
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.bounds_clear_shader);
        command_buffer.dispatch((instance_count + 63u) / 64u, 1, 1);
        const vk::MemoryBarrier2 clear_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &clear_dependency});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->resources.bounds_accumulation_shader);
        for (const GpuScenePrimitive& primitive : this->resources.primitives) {
            if (!scene.resources.instances[primitive.scene_instance_index].visible) continue;
            GpuSceneBoundsPushData push{{}, {}, this->resources.instance_bounds_descriptor, this->resources.primitive_transforms_descriptor, {0, primitive.scene_instance_index, static_cast<std::uint32_t>(primitive.kind), primitive.scene_primitive_index}};
            if (primitive.kind == GpuScenePrimitiveKind::Geometry) {
                const GpuGeometry& geometry = this->resources.geometries[primitive.resource_index];
                push.positions              = geometry.positions_descriptor;
                push.radii                  = geometry.positions_descriptor;
                push.metadata[0]            = geometry.vertex_count;
            } else {
                const GpuSphereSet& spheres = this->resources.sphere_sets[primitive.resource_index];
                push.positions              = spheres.positions_descriptor;
                push.radii                  = spheres.radii_descriptor;
                push.metadata[0]            = spheres.sphere_count;
            }
            if (push.metadata[0] == 0) continue;
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch((push.metadata[0] + 255u) / 256u, 1, 1);
        }
        const vk::MemoryBarrier2 output_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eCopy | vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &output_dependency});
        GpuBuffer& readback = this->resources.instance_bounds_readbacks[frame_slot_index];
        command_buffer.copyBuffer(*this->resources.instance_bounds.buffer, *readback.buffer, vk::BufferCopy{0, 0, static_cast<vk::DeviceSize>(instance_count) * sizeof(GpuDerivedBounds)});
        this->resources.instance_bounds_readback_counts[frame_slot_index] = instance_count;
    }

    void GpuScene::resolve_instance_bounds(const std::uint32_t frame_slot_index) {
        const std::uint32_t instance_count = std::exchange(this->resources.instance_bounds_readback_counts[frame_slot_index], 0);
        if (instance_count == 0) {
            this->resources.resolved_instance_bounds.clear();
            this->resources.resolved_scene_bounds = math::Bounds3::empty();
            return;
        }
        const auto* source = static_cast<const GpuDerivedBounds*>(this->resources.instance_bounds_readbacks[frame_slot_index].mapped);
        this->resources.resolved_instance_bounds.assign(instance_count, math::Bounds3::empty());
        this->resources.resolved_scene_bounds = math::Bounds3::empty();
        for (std::uint32_t index = 0; index != instance_count; ++index) {
            if (source[index].minimum[0] == 0xffffffffu) continue;
            math::Bounds3& bounds = this->resources.resolved_instance_bounds[index];
            bounds.minimum        = {ordered_float(source[index].minimum[0]), ordered_float(source[index].minimum[1]), ordered_float(source[index].minimum[2])};
            bounds.maximum        = {ordered_float(source[index].maximum[0]), ordered_float(source[index].maximum[1]), ordered_float(source[index].maximum[2])};
            this->resources.resolved_scene_bounds.include(bounds);
        }
    }

    void GpuScene::update_top_level_from_gpu(const std::uint32_t instance_count, const vk::raii::CommandBuffer& command_buffer) {
        if (!this->context.runtime.graphics.ray_tracing_supported) return;
        const vk::AccelerationStructureGeometryInstancesDataKHR instance_data{vk::False, vk::DeviceOrHostAddressConstKHR{this->resources.acceleration_structure_instances.address}};
        const vk::AccelerationStructureGeometryKHR geometry{vk::GeometryTypeKHR::eInstances, vk::AccelerationStructureGeometryDataKHR{instance_data}};
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{vk::AccelerationStructureTypeKHR::eTopLevel, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate, vk::BuildAccelerationStructureModeKHR::eUpdate, *this->resources.top_level_acceleration_structure.acceleration_structure, *this->resources.top_level_acceleration_structure.acceleration_structure, 1, &geometry};
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, instance_count);
        build_info.scratchData                                 = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.updateScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{instance_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        const vk::BufferMemoryBarrier2 dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->resources.acceleration_structure_instances.buffer, 0, this->resources.acceleration_structure_instances.size};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, 1, &dependency});
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
    }

    void GpuScene::update_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer) {
        if (!this->context.runtime.graphics.ray_tracing_supported) return;
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

    GpuSceneUpdate GpuScene::apply(const dynamics::DynamicFrame& frame, const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        this->resources.resource_binding_changes = scene::SceneChange::None;
        this->resources.dynamic_changes          = GpuSceneChange::None;
        const std::uint32_t frame_slot_index     = this->context.runtime.frames.frame.current_slot_index;
        this->resolve_instance_bounds(frame_slot_index);
        std::vector<scene::GeometryId> external_geometries{};
        std::vector<scene::SphereSetId> external_sphere_sets{};
        std::vector<scene::VolumeId> external_volumes{};
        for (const dynamics::GpuSceneUpdate& update : frame.scene_updates) {
            if (const auto* mesh = std::get_if<dynamics::GpuTriangleMeshUpdate>(&update.data))
                external_geometries.push_back(mesh->geometry_id);
            else if (const auto* spheres = std::get_if<dynamics::GpuSphereSetUpdate>(&update.data))
                external_sphere_sets.push_back(spheres->sphere_set_id);
            else if (const auto* field = std::get_if<dynamics::GpuFieldUpdate>(&update.data))
                external_volumes.push_back(field->volume_id);
        }
        this->begin_external_updates(external_geometries, external_sphere_sets, external_volumes);
        this->synchronize_scene(scene, command_buffer);

        for (const dynamics::GpuSceneUpdate& update : frame.scene_updates) {
            if (const auto* mesh = std::get_if<dynamics::GpuTriangleMeshUpdate>(&update.data)) {
                this->synchronize_external_geometry(mesh->geometry_id, mesh->positions.buffer, mesh->normals ? mesh->normals->buffer : nullptr, mesh->tangents ? mesh->tangents->buffer : nullptr, mesh->texture_coordinates ? mesh->texture_coordinates->buffer : nullptr, mesh->indices ? mesh->indices->buffer : nullptr, static_cast<std::uint32_t>(mesh->vertex_count), static_cast<std::uint32_t>(mesh->index_count), command_buffer);
                this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Geometry;
                continue;
            }
            if (const auto* spheres = std::get_if<dynamics::GpuSphereSetUpdate>(&update.data)) {
                this->synchronize_external_sphere_set(spheres->sphere_set_id, spheres->spheres.descriptor, static_cast<std::uint32_t>(spheres->count), command_buffer);
                this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Geometry;
                continue;
            }
            const auto* field = std::get_if<dynamics::GpuFieldUpdate>(&update.data);
            if (!field) continue;
            const auto bound_volume = std::ranges::find(this->resources.volumes, field->volume_id, &GpuVolume::volume_id);
            if (bound_volume == this->resources.volumes.end()) throw std::runtime_error("Dynamic field update references an unknown Scene Volume");
            if (field->resolution != bound_volume->resolution) throw std::runtime_error("Dynamic field resolution differs from its bound Scene Volume");
            if (field->local_from_grid != math::Transform{}) throw std::runtime_error("A dynamic field bound to a Scene Volume requires an identity local-from-grid transform");
            std::array<const GpuBuffer*, static_cast<std::size_t>(GpuVolumeField::Count)> fields{};
            for (const dynamics::GpuFieldChannelView& channel_view : field->channels) {
                const std::string& channel = channel_view.channel.id;
                if (channel == "density")
                    fields[std::to_underlying(GpuVolumeField::Density)] = channel_view.values.buffer;
                else if (channel == "temperature")
                    fields[std::to_underlying(GpuVolumeField::Temperature)] = channel_view.values.buffer;
                else if (channel == "emission-scale")
                    fields[std::to_underlying(GpuVolumeField::EmissionScale)] = channel_view.values.buffer;
                else if (channel == "sigma-a")
                    fields[std::to_underlying(GpuVolumeField::SigmaA)] = channel_view.values.buffer;
                else if (channel == "sigma-s")
                    fields[std::to_underlying(GpuVolumeField::SigmaS)] = channel_view.values.buffer;
                else if (channel == "emission")
                    fields[std::to_underlying(GpuVolumeField::Emission)] = channel_view.values.buffer;
                else
                    throw std::runtime_error(std::format("Dynamic field update contains unknown channel {}", channel));
            }
            const scene::VolumeRegion region = field->dirty_region.value_or(scene::VolumeRegion{{}, field->resolution});
            if (region.minimum.x >= region.maximum.x || region.minimum.y >= region.maximum.y || region.minimum.z >= region.maximum.z || region.maximum.x > field->resolution.x || region.maximum.y > field->resolution.y || region.maximum.z > field->resolution.z) throw std::runtime_error("Dynamic field dirty region is empty, reversed, or outside its resolution");
            const std::uint64_t voxel_count = static_cast<std::uint64_t>(region.maximum.x - region.minimum.x) * (region.maximum.y - region.minimum.y) * (region.maximum.z - region.minimum.z);
            this->synchronize_external_volume(field->volume_id, fields[std::to_underlying(GpuVolumeField::Density)], fields[std::to_underlying(GpuVolumeField::Temperature)], fields[std::to_underlying(GpuVolumeField::EmissionScale)], fields[std::to_underlying(GpuVolumeField::SigmaA)], fields[std::to_underlying(GpuVolumeField::SigmaS)], fields[std::to_underlying(GpuVolumeField::Emission)], voxel_count, region, command_buffer);
            this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Volume;
        }
        this->end_external_updates(scene, command_buffer);
        bool instance_transforms_updated{};
        for (const dynamics::GpuSceneUpdate& update : frame.scene_updates)
            if (const auto* transforms = std::get_if<dynamics::GpuInstanceTransformUpdate>(&update.data)) {
                this->synchronize_external_instance_transforms(*transforms, command_buffer);
                instance_transforms_updated = instance_transforms_updated || transforms->count != 0;
            }
        if (instance_transforms_updated) {
            this->update_top_level_from_gpu(static_cast<std::uint32_t>(this->resources.acceleration_primitive_indices.size()), command_buffer);
            this->resources.dynamic_changes = this->resources.dynamic_changes | GpuSceneChange::Transform;
        }
        if (std::exchange(this->resources.instance_bounds_dirty, false)) this->update_instance_bounds(scene, command_buffer, frame_slot_index);
        if (this->resources.dynamic_changes != GpuSceneChange::None) ++this->resources.dynamic_revision;
        if ((this->resources.dynamic_changes & GpuSceneChange::Structure) != GpuSceneChange::None) ++this->resources.dynamic_structure_revision;
        return {
            std::exchange(this->resources.resource_binding_changes, scene::SceneChange::None),
            std::exchange(this->resources.dynamic_changes, GpuSceneChange::None),
            this->resources.dynamic_revision,
            this->resources.dynamic_structure_revision,
        };
    }

    GpuSceneUpdate GpuScene::synchronize(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        this->resources.resource_binding_changes = scene::SceneChange::None;
        this->resources.dynamic_changes          = GpuSceneChange::None;
        const std::uint32_t frame_slot_index     = this->context.runtime.frames.frame.current_slot_index;
        this->resolve_instance_bounds(frame_slot_index);
        this->synchronize_scene(scene, command_buffer);
        if (std::exchange(this->resources.instance_bounds_dirty, false)) this->update_instance_bounds(scene, command_buffer, frame_slot_index);
        return {
            std::exchange(this->resources.resource_binding_changes, scene::SceneChange::None),
            std::exchange(this->resources.dynamic_changes, GpuSceneChange::None),
            this->resources.dynamic_revision,
            this->resources.dynamic_structure_revision,
        };
    }

    void GpuScene::synchronize_scene(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.number == this->resources.synchronized_revision.number) return;
        if ((scene.revision.changes & scene::SceneChange::Structure) != scene::SceneChange::None) {
            const std::vector<scene::GeometryId> external_geometries   = this->resources.external_geometries;
            const std::vector<scene::SphereSetId> external_sphere_sets = this->resources.external_sphere_sets;
            const std::vector<scene::VolumeId> external_volumes        = this->resources.external_volumes;
            const std::vector<GpuGeometryBinding> geometry_bindings    = this->resources.geometry_bindings;
            std::vector<std::pair<scene::SphereSetId, std::uint32_t>> sphere_capacities{};
            sphere_capacities.reserve(this->resources.sphere_sets.size());
            for (const GpuSphereSet& spheres : this->resources.sphere_sets) sphere_capacities.emplace_back(spheres.sphere_set_id, spheres.sphere_capacity);
            const std::uint64_t dynamic_revision   = this->resources.dynamic_revision + 1;
            const std::uint64_t structure_revision = this->resources.dynamic_structure_revision + 1;
            GpuScene next{this->context.runtime, this->context.shader_directory};
            next.initialize_resources(scene, geometry_bindings, sphere_capacities, &command_buffer);
            next.resources.external_geometries        = external_geometries;
            next.resources.external_sphere_sets       = external_sphere_sets;
            next.resources.external_volumes           = external_volumes;
            next.resources.resource_binding_changes   = scene::SceneChange::All;
            next.resources.dynamic_changes            = GpuSceneChange::Structure;
            next.resources.dynamic_revision           = dynamic_revision;
            next.resources.dynamic_structure_revision = structure_revision;
            this->destroy();
            std::swap(this->resources, next.resources);
            return;
        }
        this->cache_texture_images(scene, command_buffer);
        if (this->resources.external_geometries.empty()) this->resources.scratch_offsets[this->context.runtime.frames.frame.current_slot_index] = 0;
        bool rebuilt_bottom_level = std::exchange(this->resources.external_bottom_level_rebuilt, false);
        if ((scene.revision.changes & scene::SceneChange::Geometry) != scene::SceneChange::None) {
            this->resources.instance_bounds_dirty = true;
            for (GpuGeometry& mesh : this->resources.geometries) {
                const scene::Geometry& geometry = *std::ranges::find(scene.resources.geometries, mesh.geometry_id, &scene::Geometry::id);
                if (std::ranges::contains(this->resources.external_geometries, mesh.geometry_id)) continue;
                if (mesh.update_mode != GpuMeshUpdateMode::Deformable) {
                    GpuGeometry replacement = this->create_geometry(geometry, command_buffer);
                    this->context.runtime.frames.defer_destruction([previous = std::move(mesh)]() mutable {});
                    mesh                                     = std::move(replacement);
                    this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Geometry;
                    rebuilt_bottom_level                     = true;
                    continue;
                }
                this->update_bottom_level(mesh, geometry, command_buffer);
            }
            for (GpuSphereSet& spheres : this->resources.sphere_sets) {
                const scene::SphereSet& source = *std::ranges::find(scene.resources.sphere_sets, spheres.sphere_set_id, &scene::SphereSet::id);
                if (std::ranges::contains(this->resources.external_sphere_sets, spheres.sphere_set_id)) continue;
                if (source.positions.size() > spheres.sphere_capacity) {
                    GpuSphereSet replacement = this->create_sphere_set(source, command_buffer, std::bit_ceil(static_cast<std::uint32_t>(std::max<std::size_t>(source.positions.size(), 1))));
                    this->context.runtime.frames.defer_destruction([previous = std::move(spheres)]() mutable {});
                    spheres                                  = std::move(replacement);
                    this->resources.resource_binding_changes = this->resources.resource_binding_changes | scene::SceneChange::Geometry;
                    rebuilt_bottom_level                     = true;
                    continue;
                }
                const bool source_empty = source.positions.empty();
                if ((spheres.sphere_count == 0) != source_empty) rebuilt_bottom_level = true;
                spheres.sphere_count = static_cast<std::uint32_t>(source.positions.size());
                this->update_sphere_set(spheres, source, command_buffer);
            }
        }
        if ((scene.revision.changes & scene::SceneChange::Volume) != scene::SceneChange::None) this->update_volumes(scene, command_buffer);
        this->resources.external_geometries.clear();
        this->resources.external_sphere_sets.clear();
        this->resources.external_volumes.clear();
        if (rebuilt_bottom_level || (scene.revision.changes & scene::SceneChange::Transform) != scene::SceneChange::None) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            this->update_top_level(instances, command_buffer);
            this->update_instance_state(scene, command_buffer);
        }
        this->resources.synchronized_revision = scene.revision;
    }

    void GpuScene::begin_external_updates(const std::span<const scene::GeometryId> geometry_ids, const std::span<const scene::SphereSetId> sphere_set_ids, const std::span<const scene::VolumeId> volume_ids) {
        this->resources.external_geometries.assign(geometry_ids.begin(), geometry_ids.end());
        this->resources.external_sphere_sets.assign(sphere_set_ids.begin(), sphere_set_ids.end());
        this->resources.external_volumes.assign(volume_ids.begin(), volume_ids.end());
    }

    void GpuScene::end_external_updates(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        if (std::exchange(this->resources.external_bottom_level_rebuilt, false)) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            this->update_top_level(instances, command_buffer);
            this->update_instance_state(scene, command_buffer);
        }
        this->resources.external_geometries.clear();
        this->resources.external_sphere_sets.clear();
        this->resources.external_volumes.clear();
    }
    GpuAccelerationStructure GpuScene::build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, const std::uint32_t primitive_count, const GpuMeshUpdateMode update_mode, const vk::raii::CommandBuffer& command_buffer, const std::uint32_t maximum_primitive_count) {
        vk::BuildAccelerationStructureFlagsKHR flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace | vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess;
        if (update_mode == GpuMeshUpdateMode::Deformable) flags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            flags,
            vk::BuildAccelerationStructureModeKHR::eBuild,
            {},
            {},
            1,
            &geometry,
        };
        const std::uint32_t build_capacity                     = maximum_primitive_count == 0 ? primitive_count : maximum_primitive_count;
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, build_capacity);

        GpuAccelerationStructure result{};
        result.storage                = this->context.runtime.resources.create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.acceleration_structure = vk::raii::AccelerationStructureKHR{
            this->context.runtime.graphics.device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel},
        };
        build_info.dstAccelerationStructure = *result.acceleration_structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.buildScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        result.address = this->context.runtime.graphics.device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.acceleration_structure});
        return result;
    }

    GpuAccelerationStructure GpuScene::build_top_level(const std::span<const vk::AccelerationStructureInstanceKHR> instances, const std::uint32_t maximum_primitive_count, const vk::raii::CommandBuffer& command_buffer) {
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
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->context.runtime.graphics.device.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, build_info, maximum_primitive_count);

        GpuAccelerationStructure result{};
        result.storage                = this->context.runtime.resources.create_buffer(sizes.accelerationStructureSize, vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        result.acceleration_structure = vk::raii::AccelerationStructureKHR{
            this->context.runtime.graphics.device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eTopLevel},
        };
        build_info.dstAccelerationStructure = *result.acceleration_structure;
        build_info.scratchData              = vk::DeviceOrHostAddressKHR{this->acquire_acceleration_scratch(sizes.buildScratchSize, false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        const vk::MemoryBarrier2 blas_build_dependency{
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &blas_build_dependency});
        command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        result.address = this->context.runtime.graphics.device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.acceleration_structure});
        return result;
    }
} // namespace spectra
