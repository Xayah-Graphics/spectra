module;

#include <exr.h>

module spectra.render.assets;

import std;

namespace spectra::render {
    namespace {
        [[nodiscard]] std::string texture_cache_key(
            const scene::Texture& texture) {
            const scene::ImageTexture& image =
                std::get<scene::ImageTexture>(
                    texture.data);
            const std::string identity =
                image.asset.content_hash.empty()
                ? std::format(
                      "memory:{}:{}:{}",
                      texture.id.value,
                      texture.revision.content,
                      texture.revision.topology)
                : image.asset.content_hash;
            return std::format(
                "{}:{}:{}:{}",
                identity,
                std::to_underlying(image.wrap),
                std::to_underlying(image.filter),
                std::bit_cast<std::uint32_t>(
                    image.maximum_anisotropy));
        }

    } // namespace

    GpuTextureImage upload_texture_image(GpuDevice& gpu, const scene::ImageTexture& data, const vk::Format format, const vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer* command_buffer) {
        GpuTextureImage result{gpu.create_image_2d({data.width, data.height}, format, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::ImageAspectFlagBits::eColor, static_cast<std::uint32_t>(data.mip_offsets.size())), gpu.allocate_resource_descriptor(), gpu.allocate_sampler_descriptor()};
        gpu.write_sampled_image(result.image_descriptor, result.image, vk::ImageLayout::eShaderReadOnlyOptimal);
        const vk::SamplerAddressMode address_mode = data.wrap == scene::TextureWrapMode::Repeat ? vk::SamplerAddressMode::eRepeat : data.wrap == scene::TextureWrapMode::Clamp ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eClampToBorder;
        const bool linear                         = data.filter != scene::TextureFilter::Point;
        gpu.write_sampler(result.sampler_descriptor, vk::SamplerCreateInfo{{}, linear ? vk::Filter::eLinear : vk::Filter::eNearest, linear ? vk::Filter::eLinear : vk::Filter::eNearest, data.filter == scene::TextureFilter::Trilinear || data.filter == scene::TextureFilter::Ewa ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest, address_mode, address_mode, address_mode, 0.0f, data.filter == scene::TextureFilter::Ewa ? vk::True : vk::False, data.maximum_anisotropy, vk::False, vk::CompareOp::eNever, 0.0f, static_cast<float>(data.mip_offsets.size() - 1u), vk::BorderColor::eFloatTransparentBlack});
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
            staging    = gpu.create_buffer(texels.size() * sizeof(std::uint16_t), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            std::memcpy(staging.mapped, texels.data(), texels.size() * sizeof(std::uint16_t));
        } else if (format == vk::Format::eR32G32B32A32Sfloat) {
            texel_size = sizeof(scene::Float4);
            staging    = gpu.create_buffer(data.texels.size() * texel_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
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
            gpu.defer([upload = std::move(staging)]() mutable {});
        } else
            gpu.immediate(record);
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

        [[nodiscard]] vk::AccelerationStructureGeometryKHR
        procedural_geometry(
            const GpuGeometry& mesh) {
            const vk::AccelerationStructureGeometryAabbsDataKHR
                aabbs{
                    vk::DeviceOrHostAddressConstKHR{
                        mesh.aabbs.address},
                    sizeof(vk::AabbPositionsKHR)};
            return {
                vk::GeometryTypeKHR::eAabbs,
                vk::AccelerationStructureGeometryDataKHR{
                    aabbs},
                {},
            };
        }

        [[nodiscard]] vk::AccelerationStructureGeometryKHR
        particle_geometry(
            const GpuParticleSet& particles) {
            const vk::AccelerationStructureGeometryAabbsDataKHR
                aabbs{
                    particles.aabbs.address,
                    sizeof(vk::AabbPositionsKHR)};
            return vk::AccelerationStructureGeometryKHR{
                vk::GeometryTypeKHR::eAabbs,
                vk::AccelerationStructureGeometryDataKHR{
                    aabbs},
                {},
            };
        }

        [[nodiscard]] scene::TriangleMeshGeometry
        tessellate_geometry(
            const scene::Geometry& geometry) {
            if (const scene::TriangleMeshGeometry* mesh =
                    std::get_if<
                        scene::TriangleMeshGeometry>(
                        &geometry.data))
                return *mesh;
            scene::TriangleMeshGeometry result{};
            const auto vertex =
                [&result](
                    const scene::Float3 position,
                    const scene::Float3 normal,
                    const scene::Float3 tangent,
                    const scene::Float2 uv) {
                    result.positions.push_back(
                        position);
                    result.normals.push_back(
                        normal);
                    result.tangents.push_back(
                        tangent);
                    result.texture_coordinates
                        .push_back(uv);
                    return static_cast<
                        std::uint32_t>(
                        result.positions.size() -
                        1u);
                };
            if (const scene::BoxGeometry* box =
                    std::get_if<
                        scene::BoxGeometry>(
                        &geometry.data)) {
                const scene::Float3 minimum =
                    box->bounds.minimum;
                const scene::Float3 maximum =
                    box->bounds.maximum;
                const std::array positions{
                    std::array{
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            minimum.z},
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            minimum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            minimum.z},
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            minimum.z}},
                    std::array{
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            maximum.z},
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            maximum.z}},
                    std::array{
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            minimum.z},
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            minimum.z}},
                    std::array{
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            minimum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            minimum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            maximum.z},
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            maximum.z}},
                    std::array{
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            minimum.z},
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            minimum.z},
                        scene::Float3{
                            minimum.x,
                            maximum.y,
                            maximum.z},
                        scene::Float3{
                            minimum.x,
                            minimum.y,
                            maximum.z}},
                    std::array{
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            minimum.z},
                        scene::Float3{
                            maximum.x,
                            minimum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            maximum.z},
                        scene::Float3{
                            maximum.x,
                            maximum.y,
                            minimum.z}}};
                const std::array normals{
                    scene::Float3{0.0f, 0.0f, -1.0f},
                    scene::Float3{0.0f, 0.0f, 1.0f},
                    scene::Float3{0.0f, -1.0f, 0.0f},
                    scene::Float3{0.0f, 1.0f, 0.0f},
                    scene::Float3{-1.0f, 0.0f, 0.0f},
                    scene::Float3{1.0f, 0.0f, 0.0f}};
                const std::array tangents{
                    scene::Float3{1.0f, 0.0f, 0.0f},
                    scene::Float3{-1.0f, 0.0f, 0.0f},
                    scene::Float3{1.0f, 0.0f, 0.0f},
                    scene::Float3{1.0f, 0.0f, 0.0f},
                    scene::Float3{0.0f, 1.0f, 0.0f},
                    scene::Float3{0.0f, -1.0f, 0.0f}};
                constexpr std::array texture_coordinates{
                    scene::Float2{0.0f, 0.0f},
                    scene::Float2{1.0f, 0.0f},
                    scene::Float2{1.0f, 1.0f},
                    scene::Float2{0.0f, 1.0f}};
                for (std::uint32_t face = 0;
                     face != 6;
                     ++face) {
                    const std::uint32_t first =
                        static_cast<std::uint32_t>(
                            result.positions.size());
                    for (std::uint32_t corner = 0;
                         corner != 4;
                         ++corner)
                        vertex(
                            positions[face][corner],
                            normals[face],
                            tangents[face],
                            texture_coordinates[
                                corner]);
                    result.indices.insert(
                        result.indices.end(),
                        {
                            first,
                            first + 1u,
                            first + 2u,
                            first,
                            first + 2u,
                            first + 3u});
                }
                return result;
            }
            if (const scene::RectangleGeometry*
                    rectangle =
                    std::get_if<
                        scene::RectangleGeometry>(
                        &geometry.data)) {
                vertex(
                    {
                        rectangle->minimum.x,
                        rectangle->minimum.y,
                        0.0f},
                    {0.0f, 0.0f, 1.0f},
                    {1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f});
                vertex(
                    {
                        rectangle->maximum.x,
                        rectangle->minimum.y,
                        0.0f},
                    {0.0f, 0.0f, 1.0f},
                    {1.0f, 0.0f, 0.0f},
                    {1.0f, 0.0f});
                vertex(
                    {
                        rectangle->maximum.x,
                        rectangle->maximum.y,
                        0.0f},
                    {0.0f, 0.0f, 1.0f},
                    {1.0f, 0.0f, 0.0f},
                    {1.0f, 1.0f});
                vertex(
                    {
                        rectangle->minimum.x,
                        rectangle->maximum.y,
                        0.0f},
                    {0.0f, 0.0f, 1.0f},
                    {1.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f});
                result.indices = {
                    0, 1, 2,
                    0, 2, 3};
                return result;
            }
            constexpr std::uint32_t segments = 64;
            if (const scene::SphereGeometry* sphere =
                    std::get_if<
                        scene::SphereGeometry>(
                        &geometry.data)) {
                constexpr std::uint32_t rings = 32;
                const float phi_max =
                    sphere->phi_max *
                    std::numbers::pi_v<float> /
                    180.0f;
                for (std::uint32_t ring = 0;
                     ring <= rings;
                     ++ring) {
                    const float v =
                        static_cast<float>(ring) /
                        rings;
                    const float z =
                        std::lerp(
                            sphere->z_min,
                            sphere->z_max,
                            v);
                    const float radial =
                        std::sqrt(
                            std::max(
                                0.0f,
                                sphere->radius *
                                        sphere->radius -
                                    z * z));
                    for (std::uint32_t segment = 0;
                         segment <= segments;
                         ++segment) {
                        const float u =
                            static_cast<float>(
                                segment) /
                            segments;
                        const float phi =
                            phi_max * u;
                        const scene::Float3 position{
                            radial *
                                std::cos(phi),
                            radial *
                                std::sin(phi),
                            z};
                        vertex(
                            position,
                            {
                                position.x /
                                    sphere->radius,
                                position.y /
                                    sphere->radius,
                                position.z /
                                    sphere->radius},
                            {
                                -std::sin(phi),
                                std::cos(phi),
                                0.0f},
                            {u, v});
                    }
                }
                for (std::uint32_t ring = 0;
                     ring != rings;
                     ++ring)
                    for (std::uint32_t segment = 0;
                         segment != segments;
                         ++segment) {
                        const std::uint32_t first =
                            ring *
                                (segments + 1u) +
                            segment;
                        const std::uint32_t second =
                            first + segments + 1u;
                        result.indices.insert(
                            result.indices.end(),
                            {
                                first,
                                second,
                                second + 1u,
                                first,
                                second + 1u,
                                first + 1u});
                    }
                return result;
            }
            if (const scene::DiskGeometry* disk =
                    std::get_if<
                        scene::DiskGeometry>(
                        &geometry.data)) {
                const float phi_max =
                    disk->phi_max *
                    std::numbers::pi_v<float> /
                    180.0f;
                for (std::uint32_t segment = 0;
                     segment <= segments;
                     ++segment) {
                    const float u =
                        static_cast<float>(
                            segment) /
                        segments;
                    const float phi =
                        phi_max * u;
                    for (const float radius :
                         {
                             disk->inner_radius,
                             disk->radius})
                        vertex(
                            {
                                radius *
                                    std::cos(phi),
                                radius *
                                    std::sin(phi),
                                disk->height},
                            {0.0f, 0.0f, 1.0f},
                            {1.0f, 0.0f, 0.0f},
                            {
                                u,
                                (
                                    disk->radius -
                                    radius
                                ) /
                                    (
                                        disk->radius -
                                        disk->inner_radius
                                    )});
                }
                for (std::uint32_t segment = 0;
                     segment != segments;
                     ++segment) {
                    const std::uint32_t first =
                        segment * 2u;
                    result.indices.insert(
                        result.indices.end(),
                        {
                            first,
                            first + 1u,
                            first + 3u});
                    if (disk->inner_radius != 0.0f)
                        result.indices.insert(
                            result.indices.end(),
                            {
                                first,
                                first + 3u,
                                first + 2u});
                }
                return result;
            }
            const scene::CylinderGeometry& cylinder =
                std::get<
                    scene::CylinderGeometry>(
                    geometry.data);
            const float phi_max =
                cylinder.phi_max *
                std::numbers::pi_v<float> /
                180.0f;
            for (std::uint32_t segment = 0;
                 segment <= segments;
                 ++segment) {
                const float u =
                    static_cast<float>(segment) /
                    segments;
                const float phi =
                    phi_max * u;
                const scene::Float3 normal{
                    std::cos(phi),
                    std::sin(phi),
                    0.0f};
                for (std::uint32_t end = 0;
                     end != 2;
                     ++end)
                    vertex(
                        {
                            cylinder.radius *
                                normal.x,
                            cylinder.radius *
                                normal.y,
                            end == 0
                                ? cylinder.z_min
                                : cylinder.z_max},
                        normal,
                        {
                            -normal.y,
                            normal.x,
                            0.0f},
                        {u, static_cast<float>(end)});
            }
            for (std::uint32_t segment = 0;
                 segment != segments;
                 ++segment) {
                const std::uint32_t first =
                    segment * 2u;
                result.indices.insert(
                    result.indices.end(),
                    {
                        first,
                        first + 1u,
                        first + 3u,
                        first,
                        first + 3u,
                        first + 2u});
            }
            return result;
        }

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(
            GpuDevice& gpu,
            const std::span<const Element> elements,
            const vk::BufferUsageFlags usage) {
            GpuBuffer staging = gpu.create_buffer(
                elements.size_bytes(),
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible |
                    vk::MemoryPropertyFlagBits::eHostCoherent,
                true);
            std::memcpy(
                staging.mapped,
                elements.data(),
                elements.size_bytes());
            GpuBuffer destination = gpu.create_buffer(
                elements.size_bytes(),
                usage |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                false);
            gpu.immediate(
                [&staging, &destination, usage](
                    const vk::raii::CommandBuffer& command_buffer) {
                    command_buffer.copyBuffer(
                        *staging.buffer,
                        *destination.buffer,
                        vk::BufferCopy{0, 0, staging.size});
                    const bool acceleration_structure =
                        static_cast<bool>(
                            usage &
                            vk::BufferUsageFlagBits::
                                eAccelerationStructureBuildInputReadOnlyKHR);
                    const vk::BufferMemoryBarrier2 dependency{
                        vk::PipelineStageFlagBits2::eCopy,
                        vk::AccessFlagBits2::eTransferWrite,
                        acceleration_structure
                            ? vk::PipelineStageFlagBits2::
                                  eAccelerationStructureBuildKHR
                            : vk::PipelineStageFlagBits2::eAllCommands,
                        acceleration_structure
                            ? vk::AccessFlagBits2::
                                  eAccelerationStructureReadKHR
                            : vk::AccessFlagBits2::eShaderStorageRead,
                        vk::QueueFamilyIgnored,
                        vk::QueueFamilyIgnored,
                        *destination.buffer,
                        0,
                        destination.size,
                    };
                    command_buffer.pipelineBarrier2(
                        vk::DependencyInfo{
                            {},
                            0,
                            nullptr,
                            1,
                            &dependency});
                });
            return destination;
        }

        template <typename Element>
        [[nodiscard]] GpuBuffer upload_buffer(
            GpuDevice& gpu,
            const vk::raii::CommandBuffer& command_buffer,
            const std::span<const Element> elements,
            const vk::BufferUsageFlags usage) {
            GpuBuffer destination = gpu.create_buffer(
                elements.size_bytes(),
                usage |
                    vk::BufferUsageFlagBits::eShaderDeviceAddress |
                    vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                false);
            const GpuUploadSlice upload =
                gpu.stage_upload(std::as_bytes(elements));
            command_buffer.copyBuffer(
                upload.buffer,
                *destination.buffer,
                vk::BufferCopy{
                    upload.offset,
                    0,
                    upload.size});
            const bool acceleration_structure =
                static_cast<bool>(
                    usage &
                    vk::BufferUsageFlagBits::
                        eAccelerationStructureBuildInputReadOnlyKHR);
            const vk::BufferMemoryBarrier2 dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                acceleration_structure
                    ? vk::PipelineStageFlagBits2::
                          eAccelerationStructureBuildKHR
                    : vk::PipelineStageFlagBits2::eAllCommands,
                acceleration_structure
                    ? vk::AccessFlagBits2::
                          eAccelerationStructureReadKHR
                    : vk::AccessFlagBits2::eShaderStorageRead,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *destination.buffer,
                0,
                destination.size,
            };
            command_buffer.pipelineBarrier2(
                vk::DependencyInfo{
                    {},
                    0,
                    nullptr,
                    1,
                    &dependency});
            return destination;
        }
    } // namespace

    GpuAssetCache::GpuAssetCache(
        GpuDevice& gpu,
        const scene::SceneView scene)
        : gpu(&gpu),
          geometries(this->gpu_geometries),
          particle_sets(this->gpu_particle_sets),
          draws(this->gpu_draws),
          source_instances(this->source_instance_ids),
          top_level_acceleration_structure(this->tlas) {
        this->cache_texture_images(scene);
        this->gpu_geometries.reserve(scene.resources.geometries.size());
        for (const scene::Geometry& geometry :
             scene.resources.geometries)
            this->gpu_geometries.emplace_back(
                this->create_geometry(geometry));
        this->gpu_particle_sets.reserve(
            scene.resources.particle_sets.size());
        for (const scene::ParticleSet& particles :
             scene.resources.particle_sets)
            this->gpu_particle_sets.emplace_back(
                this->create_particle_set(
                    particles));

        const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
        this->acceleration_structure_instances = upload_buffer(
            gpu,
            std::span<const vk::AccelerationStructureInstanceKHR>{instances},
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR);
        this->tlas = this->build_top_level(instances);
        this->uploaded_revision = scene.revision;
    }

    GpuAssetCache::~GpuAssetCache() {
        for (const GpuTextureImage& image :
             this->texture_images) {
            this->gpu->release_resource_descriptor(
                image.image_descriptor);
            this->gpu->release_sampler_descriptor(
                image.sampler_descriptor);
        }
        for (const GpuGeometry& mesh : this->gpu_geometries) {
            this->gpu->release_resource_descriptor(mesh.positions_descriptor);
            this->gpu->release_resource_descriptor(mesh.normals_descriptor);
            this->gpu->release_resource_descriptor(mesh.tangents_descriptor);
            this->gpu->release_resource_descriptor(mesh.texture_coordinates_descriptor);
            this->gpu->release_resource_descriptor(mesh.indices_descriptor);
        }
        for (
            const GpuParticleSet& particles :
            this->gpu_particle_sets) {
            this->gpu->release_resource_descriptor(
                particles.positions_descriptor);
            this->gpu->release_resource_descriptor(
                particles.radii_descriptor);
            this->gpu->release_resource_descriptor(
                particles.colors_descriptor);
        }
    }

    void GpuAssetCache::cache_texture_images(
        const scene::SceneView scene,
        const vk::raii::CommandBuffer*
            command_buffer) {
        for (const scene::Texture& texture :
             scene.resources.textures) {
            const scene::ImageTexture* image =
                std::get_if<scene::ImageTexture>(
                    &texture.data);
            if (!image) continue;
            const vk::Format format =
                texture.spectrum_type ==
                        scene::TextureSpectrumType::
                            Albedo
                    ? vk::Format::
                        eR16G16B16A16Sfloat
                    : vk::Format::
                        eR32G32B32A32Sfloat;
            const std::pair key{
                texture_cache_key(texture),
                format};
            if (this->texture_image_indices.contains(
                    key))
                continue;
            const std::size_t index =
                this->texture_images.size();
            this->texture_images.emplace_back(
                upload_texture_image(
                    *this->gpu,
                    *image,
                    format,
                    vk::PipelineStageFlagBits2::
                        eAllCommands,
                    command_buffer));
            this->texture_image_indices.emplace(
                key,
                index);
        }
    }

    const GpuTextureImage&
    GpuAssetCache::texture_image(
        const scene::Texture& texture,
        const vk::Format format) const {
        return this->texture_images[
            this->texture_image_indices.at(
                {
                    texture_cache_key(texture),
                    format})];
    }

    GpuGeometry GpuAssetCache::create_geometry(
        const scene::Geometry& geometry,
        const vk::raii::CommandBuffer*
            command_buffer) {
        const scene::TriangleMeshGeometry mesh =
            tessellate_geometry(geometry);
        GpuGeometry result{};
        result.id = geometry.id;
        result.update_mode = mesh.update_mode;
        result.acceleration_kind =
            std::holds_alternative<
                    scene::SphereGeometry>(
                    geometry.data) ||
                std::holds_alternative<
                    scene::DiskGeometry>(
                    geometry.data) ||
                std::holds_alternative<
                    scene::CylinderGeometry>(
                    geometry.data)
                ? GpuGeometryKind::Procedural
                : GpuGeometryKind::Triangle;
        result.vertex_count =
            static_cast<std::uint32_t>(
                mesh.positions.size());
        result.index_count =
            static_cast<std::uint32_t>(
                mesh.indices.size());
        result.acceleration_primitive_count =
            result.acceleration_kind ==
                    GpuGeometryKind::Triangle
                ? result.index_count / 3u
                : 1u;
        result.attribute_flags =
            (mesh.normals.empty() ? 0u : 1u) |
            (mesh.tangents.empty() ? 0u : 2u) |
            (mesh.texture_coordinates.empty()
                 ? 0u
                 : 4u);
        const std::array<scene::Float3, 1>
            missing_float3{};
        const std::array<scene::Float2, 1>
            missing_float2{};
        const std::span<const scene::Float3>
            positions{mesh.positions};
        const std::span<const scene::Float3>
            normals = mesh.normals.empty()
                ? std::span<const scene::Float3>{
                      missing_float3}
                : std::span<const scene::Float3>{
                      mesh.normals};
        const std::span<const scene::Float3>
            tangents = mesh.tangents.empty()
                ? std::span<const scene::Float3>{
                      missing_float3}
                : std::span<const scene::Float3>{
                      mesh.tangents};
        const std::span<const scene::Float2>
            texture_coordinates =
                mesh.texture_coordinates.empty()
                    ? std::span<const scene::Float2>{
                          missing_float2}
                    : std::span<const scene::Float2>{
                          mesh.texture_coordinates};
        const std::span<const std::uint32_t>
            indices{mesh.indices};
        const vk::BufferUsageFlags
            position_usage =
                vk::BufferUsageFlagBits::
                        eAccelerationStructureBuildInputReadOnlyKHR |
                    vk::BufferUsageFlagBits::
                        eStorageBuffer;
        const vk::BufferUsageFlags
            attribute_usage =
                vk::BufferUsageFlagBits::
                    eStorageBuffer;
        result.positions = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  positions,
                  position_usage)
            : upload_buffer(
                  *this->gpu,
                  positions,
                  position_usage);
        result.normals = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  normals,
                  attribute_usage)
            : upload_buffer(
                  *this->gpu,
                  normals,
                  attribute_usage);
        result.tangents = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  tangents,
                  attribute_usage)
            : upload_buffer(
                  *this->gpu,
                  tangents,
                  attribute_usage);
        result.texture_coordinates =
            command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      texture_coordinates,
                      attribute_usage)
                : upload_buffer(
                      *this->gpu,
                      texture_coordinates,
                      attribute_usage);
        result.indices = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  indices,
                  position_usage)
            : upload_buffer(
                  *this->gpu,
                  indices,
                  position_usage);
        if (
            result.acceleration_kind ==
            GpuGeometryKind::Procedural) {
            const scene::Bounds3 bounds =
                scene::geometry_bounds(geometry);
            const std::array aabbs{
                vk::AabbPositionsKHR{
                    bounds.minimum.x,
                    bounds.minimum.y,
                    bounds.minimum.z,
                    bounds.maximum.x,
                    bounds.maximum.y,
                    bounds.maximum.z}};
            result.aabbs = command_buffer
                ? upload_buffer(
                      *this->gpu,
                      *command_buffer,
                      std::span<
                          const vk::AabbPositionsKHR>{
                          aabbs},
                      vk::BufferUsageFlagBits::
                          eAccelerationStructureBuildInputReadOnlyKHR)
                : upload_buffer(
                      *this->gpu,
                      std::span<
                          const vk::AabbPositionsKHR>{
                          aabbs},
                      vk::BufferUsageFlagBits::
                          eAccelerationStructureBuildInputReadOnlyKHR);
        }
        result.positions_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.normals_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.tangents_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.texture_coordinates_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.indices_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        this->gpu->write_buffer(
            result.positions_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.positions);
        this->gpu->write_buffer(
            result.normals_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.normals);
        this->gpu->write_buffer(
            result.tangents_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.tangents);
        this->gpu->write_buffer(
            result.texture_coordinates_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.texture_coordinates);
        this->gpu->write_buffer(
            result.indices_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.indices);
        result.blas =
            this->build_bottom_level(
                result.acceleration_kind ==
                        GpuGeometryKind::Triangle
                    ? triangle_geometry(result)
                    : procedural_geometry(result),
                result.acceleration_primitive_count,
                result.update_mode,
                command_buffer);
        return result;
    }

    GpuParticleSet
    GpuAssetCache::create_particle_set(
        const scene::ParticleSet& particles,
        const vk::raii::CommandBuffer*
            command_buffer) {
        GpuParticleSet result{};
        result.id = particles.id;
        result.update_mode =
            particles.update_mode;
        result.particle_count =
            static_cast<std::uint32_t>(
                particles.positions.size());
        result.attribute_flags =
            (particles.colors.empty()
                 ? 0u
                 : 2u) |
            (particles.particle_materials.empty()
                 ? 0u
                 : 8u);
        const std::array<scene::Float3, 1>
            missing_float3{};
        const std::span<const scene::Float3>
            colors =
                particles.colors.empty()
                    ? std::span<
                          const scene::Float3>{
                          missing_float3}
                    : std::span<
                          const scene::Float3>{
                          particles.colors};
        std::vector<vk::AabbPositionsKHR>
            aabbs(
                particles.positions.size());
        for (std::size_t index = 0;
             index != particles.positions.size();
             ++index) {
            const scene::Float3 position =
                particles.positions[index];
            const float radius =
                particles.radii[index];
            aabbs[index] =
                vk::AabbPositionsKHR{
                    position.x - radius,
                    position.y - radius,
                    position.z - radius,
                    position.x + radius,
                    position.y + radius,
                    position.z + radius};
        }
        const vk::BufferUsageFlags
            attribute_usage =
                vk::BufferUsageFlagBits::
                    eStorageBuffer;
        result.positions = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  std::span<
                      const scene::Float3>{
                      particles.positions},
                  attribute_usage)
            : upload_buffer(
                  *this->gpu,
                  std::span<
                      const scene::Float3>{
                      particles.positions},
                  attribute_usage);
        result.radii = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  std::span<const float>{
                      particles.radii},
                  attribute_usage)
            : upload_buffer(
                  *this->gpu,
                  std::span<const float>{
                      particles.radii},
                  attribute_usage);
        result.colors = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  colors,
                  attribute_usage)
            : upload_buffer(
                  *this->gpu,
                  colors,
                  attribute_usage);
        result.aabbs = command_buffer
            ? upload_buffer(
                  *this->gpu,
                  *command_buffer,
                  std::span<
                      const vk::AabbPositionsKHR>{
                      aabbs},
                  vk::BufferUsageFlagBits::
                      eAccelerationStructureBuildInputReadOnlyKHR)
            : upload_buffer(
                  *this->gpu,
                  std::span<
                      const vk::AabbPositionsKHR>{
                      aabbs},
                  vk::BufferUsageFlagBits::
                      eAccelerationStructureBuildInputReadOnlyKHR);
        result.positions_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.radii_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        result.colors_descriptor =
            this->gpu
                ->allocate_resource_descriptor();
        this->gpu->write_buffer(
            result.positions_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.positions);
        this->gpu->write_buffer(
            result.radii_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.radii);
        this->gpu->write_buffer(
            result.colors_descriptor,
            vk::DescriptorType::eStorageBuffer,
            result.colors);
        result.blas =
            this->build_bottom_level(
                particle_geometry(result),
                result.particle_count,
                result.update_mode,
                command_buffer);
        return result;
    }

    std::vector<vk::AccelerationStructureInstanceKHR>
    GpuAssetCache::acceleration_structure_instance_data(
        const scene::SceneView scene) {
        std::vector<vk::AccelerationStructureInstanceKHR> instances{};
        std::size_t primitive_count{};
        for (const scene::Instance& instance : scene.resources.instances) {
            const std::vector<scene::Prototype>::const_iterator prototype =
                std::ranges::find(
                    scene.resources.prototypes,
                    instance.prototype,
                    &scene::Prototype::id);
            primitive_count += prototype->primitives.size();
        }
        instances.reserve(primitive_count);
        this->gpu_draws.clear();
        this->gpu_draws.reserve(primitive_count);
        this->source_instance_ids.clear();
        this->source_instance_ids.reserve(primitive_count);
        for (std::uint32_t instance_index = 0;
             instance_index < scene.resources.instances.size();
             ++instance_index) {
            const scene::Instance& instance =
                scene.resources.instances[instance_index];
            const std::vector<scene::Prototype>::const_iterator prototype =
                std::ranges::find(
                    scene.resources.prototypes,
                    instance.prototype,
                    &scene::Prototype::id);
            for (std::uint32_t primitive_index = 0;
                 primitive_index < prototype->primitives.size();
                 ++primitive_index) {
                const scene::Primitive& primitive =
                    prototype->primitives[primitive_index];
                const std::vector<GpuGeometry>::const_iterator mesh =
                    std::ranges::find(
                        this->gpu_geometries,
                        primitive.geometry,
                        &GpuGeometry::id);
                const std::vector<
                    GpuParticleSet>::const_iterator
                    particles =
                        std::ranges::find(
                            this->gpu_particle_sets,
                            primitive.particles,
                            &GpuParticleSet::id);
                if (
                    mesh == this->gpu_geometries.end() &&
                    particles ==
                        this->gpu_particle_sets.end())
                    throw std::runtime_error(
                        "GpuAssetCache requires a Geometry or Particle Set for every compiled surface Primitive");

                const scene::Transform world_transform = instance.transform * primitive.transform;
                vk::TransformMatrixKHR transform{};
                for (std::uint32_t row = 0; row < 3; ++row)
                    for (std::uint32_t column = 0; column < 4; ++column)
                        transform.matrix[row][column] =
                            world_transform.matrix[row * 4u + column];
                const std::uint32_t gpu_instance =
                    static_cast<std::uint32_t>(instances.size());
                const bool particle_draw =
                    particles !=
                    this->gpu_particle_sets.end();
                vk::GeometryInstanceFlagsKHR
                    instance_flags =
                        vk::GeometryInstanceFlagBitsKHR::
                            eTriangleFacingCullDisable;
                if (primitive.alpha.value == 0)
                    instance_flags |=
                        vk::GeometryInstanceFlagBitsKHR::
                            eForceOpaque;
                instances.emplace_back(
                    transform,
                    gpu_instance,
                    0xffu,
                    particle_draw ||
                            mesh->acceleration_kind ==
                                GpuGeometryKind::
                                    Procedural
                        ? 1u
                        : 0u,
                    instance_flags,
                    particle_draw
                        ? particles->blas.address
                        : mesh->blas.address);
                this->gpu_draws.emplace_back(
                    particle_draw
                        ? GpuDrawKind::ParticleSet
                        : GpuDrawKind::Geometry,
                    static_cast<std::uint32_t>(
                        particle_draw
                            ? particles -
                                  this->gpu_particle_sets
                                      .begin()
                            : mesh -
                                  this->gpu_geometries.begin()),
                    gpu_instance,
                    instance_index,
                    primitive_index);
                this->source_instance_ids.push_back(instance.id);
            }
        }
        return instances;
    }
    vk::DeviceAddress GpuAssetCache::acquire_scratch(
        const vk::DeviceSize size,
        const bool immediate) {
        const vk::DeviceSize alignment =
            this->gpu
                ->acceleration_structure_properties
                .minAccelerationStructureScratchOffsetAlignment;
        GpuBuffer* buffer = immediate
            ? &this->immediate_scratch
            : &this->frame_scratch[
                  this->gpu->frame_index];
        vk::DeviceSize* offset = immediate
            ? nullptr
            : &this->scratch_offsets[
                  this->gpu->frame_index];
        const vk::DeviceSize current_offset =
            offset ? *offset : 0;
        vk::DeviceAddress address =
            buffer->address +
            current_offset;
        address =
            (address + alignment - 1u) &
            ~(alignment - 1u);
        const bool available =
            buffer->buffer != nullptr &&
            address + size <=
                buffer->address +
                    buffer->size;
        if (!available) {
            GpuBuffer replacement =
                this->gpu->create_buffer(
                    std::max(
                        size + alignment - 1u,
                        buffer->size * 2u),
                    vk::BufferUsageFlagBits::
                            eStorageBuffer |
                        vk::BufferUsageFlagBits::
                            eShaderDeviceAddress,
                    vk::MemoryPropertyFlagBits::
                        eDeviceLocal,
                    false);
            if (
                !immediate &&
                buffer->buffer != nullptr)
                this->gpu->defer(
                    [
                        previous =
                            std::move(*buffer)
                    ]() mutable {});
            *buffer =
                std::move(replacement);
            if (offset) *offset = 0;
            address =
                (buffer->address +
                 alignment - 1u) &
                ~(alignment - 1u);
        }
        if (offset)
            *offset =
                address -
                buffer->address +
                size;
        return address;
    }

    void GpuAssetCache::update_bottom_level(
        GpuGeometry& mesh,
        const scene::Geometry& source,
        const vk::raii::CommandBuffer& command_buffer) {
        const scene::TriangleMeshGeometry& triangle_mesh =
            std::get<scene::TriangleMeshGeometry>(source.data);
        const std::array<scene::Float3, 1>
            missing_float3{};
        const std::array<scene::Float2, 1>
            missing_float2{};
        const GpuUploadSlice position_upload =
            this->gpu->stage_upload(
                std::as_bytes(
                    std::span<const scene::Float3>{
                        triangle_mesh.positions,
                    }));
        const GpuUploadSlice normal_upload =
            this->gpu->stage_upload(
                std::as_bytes(
                    triangle_mesh.normals.empty()
                        ? std::span<const scene::Float3>{
                              missing_float3}
                        : std::span<const scene::Float3>{
                              triangle_mesh.normals}));
        const GpuUploadSlice tangent_upload =
            this->gpu->stage_upload(
                std::as_bytes(
                    triangle_mesh.tangents.empty()
                        ? std::span<const scene::Float3>{
                              missing_float3}
                        : std::span<const scene::Float3>{
                              triangle_mesh.tangents}));
        const GpuUploadSlice texture_coordinate_upload =
            this->gpu->stage_upload(
                std::as_bytes(
                    triangle_mesh
                            .texture_coordinates
                            .empty()
                        ? std::span<const scene::Float2>{
                              missing_float2}
                        : std::span<const scene::Float2>{
                              triangle_mesh
                                  .texture_coordinates}));
        const vk::AccelerationStructureGeometryKHR
            geometry =
                mesh.acceleration_kind ==
                        GpuGeometryKind::Triangle
                    ? triangle_geometry(mesh)
                    : procedural_geometry(mesh);
        const std::uint32_t primitive_count =
            mesh.acceleration_primitive_count;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
                vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess |
                vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::BuildAccelerationStructureModeKHR::eUpdate,
            *mesh.blas.structure,
            *mesh.blas.structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->gpu->device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            build_info,
            primitive_count);
        build_info.scratchData =
            vk::DeviceOrHostAddressKHR{
                this->acquire_scratch(
                    sizes.updateScratchSize,
                    false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.copyBuffer(
            position_upload.buffer,
            *mesh.positions.buffer,
            vk::BufferCopy{
                position_upload.offset,
                0,
                position_upload.size,
            });
        command_buffer.copyBuffer(
            normal_upload.buffer,
            *mesh.normals.buffer,
            vk::BufferCopy{
                normal_upload.offset,
                0,
                normal_upload.size,
            });
        command_buffer.copyBuffer(
            tangent_upload.buffer,
            *mesh.tangents.buffer,
            vk::BufferCopy{
                tangent_upload.offset,
                0,
                tangent_upload.size,
            });
        command_buffer.copyBuffer(
            texture_coordinate_upload.buffer,
            *mesh.texture_coordinates.buffer,
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
        command_buffer.pipelineBarrier2(
            vk::DependencyInfo{
                {},
                0,
                nullptr,
                static_cast<std::uint32_t>(
                    upload_dependencies.size()),
                upload_dependencies.data(),
                0,
                nullptr,
            });
        command_buffer.buildAccelerationStructuresKHR(
            build_info,
            ranges);
    }

    void GpuAssetCache::update_bottom_level(
        GpuParticleSet& particles,
        const scene::ParticleSet& source,
        const vk::raii::CommandBuffer&
            command_buffer) {
        std::vector<vk::AabbPositionsKHR>
            aabbs(source.positions.size());
        for (std::size_t index = 0;
             index != source.positions.size();
             ++index) {
            const scene::Float3 position =
                source.positions[index];
            const float radius =
                source.radii[index];
            aabbs[index] =
                vk::AabbPositionsKHR{
                    position.x - radius,
                    position.y - radius,
                    position.z - radius,
                    position.x + radius,
                    position.y + radius,
                    position.z + radius};
        }
        const std::array<scene::Float3, 1>
            missing_float3{};
        const std::span<const scene::Float3>
            colors =
                source.colors.empty()
                    ? std::span<
                          const scene::Float3>{
                          missing_float3}
                    : std::span<
                          const scene::Float3>{
                          source.colors};
        const std::array uploads{
            this->gpu->stage_upload(
                std::as_bytes(
                    std::span<
                        const scene::Float3>{
                        source.positions})),
            this->gpu->stage_upload(
                std::as_bytes(
                    std::span<const float>{
                        source.radii})),
            this->gpu->stage_upload(
                std::as_bytes(colors)),
            this->gpu->stage_upload(
                std::as_bytes(
                    std::span<
                        const vk::AabbPositionsKHR>{
                        aabbs})),
        };
        const std::array<GpuBuffer*, 4>
            destinations{
                &particles.positions,
                &particles.radii,
                &particles.colors,
                &particles.aabbs};
        for (std::size_t index = 0;
             index != uploads.size();
             ++index)
            command_buffer.copyBuffer(
                uploads[index].buffer,
                *destinations[index]->buffer,
                vk::BufferCopy{
                    uploads[index].offset,
                    0,
                    uploads[index].size});
        const vk::MemoryBarrier2
            upload_dependency{
                vk::PipelineStageFlagBits2::eCopy,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::
                        eAccelerationStructureBuildKHR |
                    vk::PipelineStageFlagBits2::
                        eAllCommands,
                vk::AccessFlagBits2::
                        eAccelerationStructureReadKHR |
                    vk::AccessFlagBits2::
                        eShaderStorageRead};
        command_buffer.pipelineBarrier2(
            vk::DependencyInfo{
                {},
                1,
                &upload_dependency});
        const vk::AccelerationStructureGeometryKHR
            geometry =
                particle_geometry(particles);
        vk::AccelerationStructureBuildGeometryInfoKHR
            build_info{
                vk::AccelerationStructureTypeKHR::
                    eBottomLevel,
                vk::BuildAccelerationStructureFlagBitsKHR::
                        ePreferFastTrace |
                    vk::BuildAccelerationStructureFlagBitsKHR::
                        eAllowDataAccess |
                    vk::BuildAccelerationStructureFlagBitsKHR::
                        eAllowUpdate,
                vk::BuildAccelerationStructureModeKHR::
                    eUpdate,
                *particles.blas.structure,
                *particles.blas.structure,
                1,
                &geometry};
        const vk::AccelerationStructureBuildSizesInfoKHR
            sizes =
                this->gpu->device
                    .getAccelerationStructureBuildSizesKHR(
                        vk::AccelerationStructureBuildTypeKHR::
                            eDevice,
                        build_info,
                        particles.particle_count);
        build_info.scratchData =
            vk::DeviceOrHostAddressKHR{
                this->acquire_scratch(
                    sizes.updateScratchSize,
                    false)};
        const vk::AccelerationStructureBuildRangeInfoKHR
            range{
                particles.particle_count,
                0,
                0,
                0};
        const std::array<
            const vk::AccelerationStructureBuildRangeInfoKHR*,
            1>
            ranges{&range};
        command_buffer
            .buildAccelerationStructuresKHR(
                build_info,
                ranges);
    }

    void GpuAssetCache::update_top_level(
        const std::span<const vk::AccelerationStructureInstanceKHR> instances,
        const vk::raii::CommandBuffer& command_buffer) {
        const GpuUploadSlice upload =
            this->gpu->stage_upload(
                std::as_bytes(instances));
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
            *this->tlas.structure,
            *this->tlas.structure,
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->gpu->device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            build_info,
            primitive_count);
        build_info.scratchData =
            vk::DeviceOrHostAddressKHR{
                this->acquire_scratch(
                    sizes.updateScratchSize,
                    false)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        command_buffer.copyBuffer(
            upload.buffer,
            *this->acceleration_structure_instances.buffer,
            vk::BufferCopy{
                upload.offset,
                0,
                upload.size,
            });
        const vk::BufferMemoryBarrier2 upload_dependency{
            vk::PipelineStageFlagBits2::eCopy,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->acceleration_structure_instances.buffer,
            0,
            this->acceleration_structure_instances.size,
        };
        const vk::MemoryBarrier2
            bottom_level_dependency{
                vk::PipelineStageFlagBits2::
                    eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::
                    eAccelerationStructureWriteKHR,
                vk::PipelineStageFlagBits2::
                    eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::
                    eAccelerationStructureReadKHR,
            };
        command_buffer.pipelineBarrier2(
            vk::DependencyInfo{
                {},
                1,
                &bottom_level_dependency,
                1,
                &upload_dependency,
            });
        command_buffer.buildAccelerationStructuresKHR(
            build_info,
            ranges);
    }

    void GpuAssetCache::synchronize(
        const scene::SceneView scene,
        const vk::raii::CommandBuffer& command_buffer) {
        if (scene.revision.value == this->uploaded_revision.value) return;
        this->cache_texture_images(
            scene,
            &command_buffer);
        this->scratch_offsets[
            this->gpu->frame_index] = 0;
        bool rebuilt_bottom_level{};
        if ((scene.revision.changes & scene::SceneChange::Geometry) != scene::SceneChange::None) {
            for (GpuGeometry& mesh : this->gpu_geometries)
                if (
                    mesh.update_mode !=
                    scene::GeometryUpdateMode::
                        Static) {
                    const scene::Geometry&
                        geometry =
                            *std::ranges::find(
                                scene.resources
                                    .geometries,
                                mesh.id,
                                &scene::Geometry::id);
                    if (
                        mesh.update_mode ==
                        scene::GeometryUpdateMode::
                            TopologyChanging) {
                        GpuGeometry replacement =
                            this->create_geometry(
                                geometry,
                                &command_buffer);
                        this->gpu
                            ->release_resource_descriptor(
                                mesh.positions_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                mesh.normals_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                mesh.tangents_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                mesh.texture_coordinates_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                mesh.indices_descriptor);
                        this->gpu->defer(
                            [
                                previous =
                                    std::move(mesh)
                            ]() mutable {});
                        mesh =
                            std::move(replacement);
                        rebuilt_bottom_level =
                            true;
                        continue;
                    }
                    this->update_bottom_level(
                        mesh,
                        geometry,
                        command_buffer);
                }
            for (
                GpuParticleSet& particles :
                this->gpu_particle_sets)
                if (
                    particles.update_mode !=
                    scene::GeometryUpdateMode::
                        Static) {
                    const scene::ParticleSet& source =
                        *std::ranges::find(
                            scene.resources
                                .particle_sets,
                            particles.id,
                            &scene::ParticleSet::id);
                    const std::uint32_t
                        attribute_flags =
                            (source.colors.empty()
                                 ? 0u
                                 : 2u) |
                            (source.particle_materials.empty()
                                 ? 0u
                                 : 8u);
                    if (
                        particles.update_mode ==
                            scene::GeometryUpdateMode::
                                TopologyChanging ||
                        particles.particle_count !=
                            source.positions.size() ||
                        particles.attribute_flags !=
                            attribute_flags) {
                        GpuParticleSet replacement =
                            this->create_particle_set(
                                source,
                                &command_buffer);
                        this->gpu
                            ->release_resource_descriptor(
                                particles
                                    .positions_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                particles
                                    .radii_descriptor);
                        this->gpu
                            ->release_resource_descriptor(
                                particles
                                    .colors_descriptor);
                        this->gpu->defer(
                            [
                                previous =
                                    std::move(
                                        particles)
                            ]() mutable {});
                        particles =
                            std::move(replacement);
                        rebuilt_bottom_level =
                            true;
                        continue;
                    }
                    this->update_bottom_level(
                        particles,
                        source,
                        command_buffer);
                }
        }
        if (
            rebuilt_bottom_level ||
            (scene.revision.changes &
             scene::SceneChange::Transform) !=
                scene::SceneChange::None) {
            const std::vector<vk::AccelerationStructureInstanceKHR> instances = this->acceleration_structure_instance_data(scene);
            this->update_top_level(
                instances,
                command_buffer);
        }
        this->uploaded_revision = scene.revision;
    }
    GpuAccelerationStructure
    GpuAssetCache::build_bottom_level(
        const vk::AccelerationStructureGeometryKHR&
            geometry,
        const std::uint32_t primitive_count,
        const scene::GeometryUpdateMode update_mode,
        const vk::raii::CommandBuffer*
            command_buffer) {
        vk::BuildAccelerationStructureFlagsKHR flags =
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace |
            vk::BuildAccelerationStructureFlagBitsKHR::eAllowDataAccess;
        if (update_mode == scene::GeometryUpdateMode::Deformable)
            flags |=
                vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
        else if (!command_buffer)
            flags |=
                vk::BuildAccelerationStructureFlagBitsKHR::
                    eAllowCompaction;
        vk::AccelerationStructureBuildGeometryInfoKHR build_info{
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            flags,
            vk::BuildAccelerationStructureModeKHR::eBuild,
            {},
            {},
            1,
            &geometry,
        };
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->gpu->device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            build_info,
            primitive_count);

        GpuAccelerationStructure result{};
        result.storage = this->gpu->create_buffer(
            sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            false);
        result.structure = vk::raii::AccelerationStructureKHR{
            this->gpu->device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel},
        };
        build_info.dstAccelerationStructure = *result.structure;
        build_info.scratchData =
            vk::DeviceOrHostAddressKHR{
                this->acquire_scratch(
                    sizes.buildScratchSize,
                    !command_buffer)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        if (command_buffer) {
            command_buffer
                ->buildAccelerationStructuresKHR(
                    build_info,
                    ranges);
            result.address =
                this->gpu->device
                    .getAccelerationStructureAddressKHR(
                        vk::AccelerationStructureDeviceAddressInfoKHR{
                            *result.structure});
            return result;
        }
        std::optional<vk::raii::QueryPool>
            compaction_query{};
        if (
            update_mode ==
            scene::GeometryUpdateMode::Static)
            compaction_query.emplace(
                this->gpu->device,
                vk::QueryPoolCreateInfo{
                    {},
                    vk::QueryType::
                        eAccelerationStructureCompactedSizeKHR,
                    1});
        this->gpu->immediate(
            [
                &build_info,
                &ranges,
                &result,
                &compaction_query
            ](
                const vk::raii::CommandBuffer&
                    command_buffer) {
                if (compaction_query)
                    command_buffer.resetQueryPool(
                        **compaction_query,
                        0,
                        1);
                command_buffer
                    .buildAccelerationStructuresKHR(
                        build_info,
                        ranges);
                if (compaction_query) {
                    const vk::MemoryBarrier2
                        build_dependency{
                            vk::PipelineStageFlagBits2::
                                eAccelerationStructureBuildKHR,
                            vk::AccessFlagBits2::
                                eAccelerationStructureWriteKHR,
                            vk::PipelineStageFlagBits2::
                                eAccelerationStructureBuildKHR,
                            vk::AccessFlagBits2::
                                eAccelerationStructureReadKHR,
                        };
                    command_buffer.pipelineBarrier2(
                        vk::DependencyInfo{
                            {},
                            1,
                            &build_dependency});
                    const std::array<
                        vk::AccelerationStructureKHR,
                        1>
                        structures{
                            *result.structure};
                    command_buffer
                        .writeAccelerationStructuresPropertiesKHR(
                            structures,
                            vk::QueryType::
                                eAccelerationStructureCompactedSizeKHR,
                            **compaction_query,
                            0);
                }
            });
        if (compaction_query) {
            std::uint64_t compacted_size{};
            if (
                compaction_query->getResults(
                    0,
                    1,
                    sizeof(compacted_size),
                    &compacted_size,
                    sizeof(compacted_size),
                    vk::QueryResultFlagBits::e64 |
                        vk::QueryResultFlagBits::eWait) !=
                vk::Result::eSuccess)
                throw std::runtime_error(
                    "Static BLAS compaction size query failed");
            if (
                compacted_size != 0 &&
                compacted_size <
                    result.storage.size) {
                GpuAccelerationStructure
                    compacted{};
                compacted.storage =
                    this->gpu->create_buffer(
                        compacted_size,
                        vk::BufferUsageFlagBits::
                                eAccelerationStructureStorageKHR |
                            vk::BufferUsageFlagBits::
                                eShaderDeviceAddress,
                        vk::MemoryPropertyFlagBits::
                            eDeviceLocal,
                        false);
                compacted.structure =
                    vk::raii::AccelerationStructureKHR{
                        this->gpu->device,
                        vk::AccelerationStructureCreateInfoKHR{
                            {},
                            *compacted.storage.buffer,
                            0,
                            compacted_size,
                            vk::AccelerationStructureTypeKHR::
                                eBottomLevel}};
                this->gpu->immediate(
                    [
                        &result,
                        &compacted
                    ](
                        const vk::raii::CommandBuffer&
                            command_buffer) {
                        command_buffer
                            .copyAccelerationStructureKHR(
                                vk::CopyAccelerationStructureInfoKHR{
                                    *result.structure,
                                    *compacted.structure,
                                    vk::CopyAccelerationStructureModeKHR::
                                        eCompact});
                    });
                result =
                    std::move(compacted);
            }
        }
        result.address = this->gpu->device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.structure});
        return result;
    }

    GpuAccelerationStructure
    GpuAssetCache::build_top_level(
        const std::span<
            const vk::AccelerationStructureInstanceKHR>
            instances) {
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
        const vk::AccelerationStructureBuildSizesInfoKHR sizes = this->gpu->device.getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            build_info,
            primitive_count);

        GpuAccelerationStructure result{};
        result.storage = this->gpu->create_buffer(
            sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            false);
        result.structure = vk::raii::AccelerationStructureKHR{
            this->gpu->device,
            vk::AccelerationStructureCreateInfoKHR{{}, *result.storage.buffer, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eTopLevel},
        };
        build_info.dstAccelerationStructure = *result.structure;
        build_info.scratchData =
            vk::DeviceOrHostAddressKHR{
                this->acquire_scratch(
                    sizes.buildScratchSize,
                    true)};
        const vk::AccelerationStructureBuildRangeInfoKHR range{primitive_count, 0, 0, 0};
        const std::array<const vk::AccelerationStructureBuildRangeInfoKHR*, 1> ranges{&range};
        this->gpu->immediate([&build_info, &ranges](const vk::raii::CommandBuffer& command_buffer) {
            const vk::MemoryBarrier2 blas_build_dependency{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &blas_build_dependency});
            command_buffer.buildAccelerationStructuresKHR(build_info, ranges);
        });
        result.address = this->gpu->device.getAccelerationStructureAddressKHR(vk::AccelerationStructureDeviceAddressInfoKHR{*result.structure});
        return result;
    }
} // namespace spectra::render
