module;
#include <kdlpp.h>

module spectra.scene.format;

import spectra.util.hash;
import std;

namespace spectra::scene {
    namespace {
        struct GeometryAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'G', 'E', 'O', 'M', '0', '3'};
            std::uint32_t version{3};
            std::uint32_t reserved{};
            std::uint64_t position_count{};
            std::uint64_t normal_count{};
            std::uint64_t tangent_count{};
            std::uint64_t texture_coordinate_count{};
            std::uint64_t index_count{};
        };

        struct ParticleAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'P', 'A', 'R', 'T', '0', '1'};
            std::uint32_t version{1};
            std::uint32_t reserved{};
            std::uint64_t position_count{};
            std::uint64_t radius_count{};
            std::uint64_t velocity_count{};
            std::uint64_t color_count{};
            std::uint64_t temperature_count{};
            std::uint64_t material_count{};
        };

        enum class VolumeAssetKind : std::uint32_t {
            DensityGrid,
            RgbGrid,
            NanoVdb,
        };

        struct VolumeAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'V', 'O', 'L', '0', '0', '2'};
            std::uint32_t version{2};
            VolumeAssetKind kind{};
            math::UInt3 resolution{};
            std::uint32_t reserved{};
            std::uint64_t primary_element_count{};
            std::uint64_t secondary_element_count{};
            std::uint64_t tertiary_element_count{};
        };

        struct TextureAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'T', 'E', 'X', '0', '0', '1'};
            std::uint32_t version{1};
            std::uint32_t mip_count{};
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint64_t texel_count{};
        };

        static_assert(sizeof(math::Float2) == sizeof(float) * 2);
        static_assert(sizeof(math::Float3) == sizeof(float) * 3);
        static_assert(sizeof(MaterialId) == sizeof(std::uint64_t));
        static_assert(sizeof(GeometryAssetHeader) == 56);
        static_assert(sizeof(ParticleAssetHeader) == 64);
        static_assert(sizeof(VolumeAssetHeader) == 56);
        static_assert(sizeof(TextureAssetHeader) == 32);

        [[nodiscard]] std::uint64_t volume_sample_count(const math::UInt3 resolution) {
            if (resolution.x == 0 || resolution.y == 0 || resolution.z == 0) throw std::runtime_error("Spectra volume resolution must be positive");
            const std::uint64_t xy = static_cast<std::uint64_t>(resolution.x) * resolution.y;
            if (xy > std::numeric_limits<std::uint64_t>::max() / resolution.z) throw std::runtime_error("Spectra volume sample count overflows uint64");
            return xy * resolution.z;
        }

        [[nodiscard]] std::filesystem::path asset_path(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            return package_root / "assets" / std::format("{}{}", reference.content_hash, extension);
        }

        void verify_asset(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            if (sha256_file(path) != reference.content_hash) throw std::runtime_error(std::format("Spectra asset SHA-256 mismatch: {}", path.string()));
        }

        template <class Value>
        void write_values(std::ofstream& stream, const std::span<const Value> values) {
            const std::span<const std::byte> bytes = std::as_bytes(values);
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        template <class Value>
        void read_values(std::ifstream& stream, std::vector<Value>& values) {
            const std::span<std::byte> bytes = std::as_writable_bytes(std::span{values});
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        [[nodiscard]] AssetReference write_geometry_asset(const TriangleMeshGeometry& mesh, const std::filesystem::path& package_root) {
            const GeometryAssetHeader header{
                .position_count           = mesh.positions.size(),
                .normal_count             = mesh.normals.size(),
                .tangent_count            = mesh.tangents.size(),
                .texture_coordinate_count = mesh.texture_coordinates.size(),
                .index_count              = mesh.indices.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{mesh.positions}),
                std::as_bytes(std::span{mesh.normals}),
                std::as_bytes(std::span{mesh.tangents}),
                std::as_bytes(std::span{mesh.texture_coordinates}),
                std::as_bytes(std::span{mesh.indices}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            const std::filesystem::path path = asset_path(package_root, reference, ".geometry");
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".geometry");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra geometry asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{mesh.positions});
            write_values(stream, std::span{mesh.normals});
            write_values(stream, std::span{mesh.tangents});
            write_values(stream, std::span{mesh.texture_coordinates});
            write_values(stream, std::span{mesh.indices});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra geometry asset: {}", path.string()));
            return reference;
        }

        void load_geometry_asset(TriangleMeshGeometry& mesh, const std::filesystem::path& package_root) {
            verify_asset(package_root, mesh.asset, ".geometry");
            const std::filesystem::path path = asset_path(package_root, mesh.asset, ".geometry");
            std::ifstream stream{path, std::ios::binary};
            GeometryAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != GeometryAssetHeader{}.magic || header.version != 3 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra geometry asset header: {}", path.string()));
            const std::uint64_t expected_size = sizeof(header) + header.position_count * sizeof(math::Float3) + header.normal_count * sizeof(math::Float3) + header.tangent_count * sizeof(math::Float3) + header.texture_coordinate_count * sizeof(math::Float2) + header.index_count * sizeof(std::uint32_t);
            if (expected_size != std::filesystem::file_size(path)) throw std::runtime_error(std::format("Invalid Spectra geometry asset payload size: {}", path.string()));
            mesh.positions.resize(header.position_count);
            mesh.normals.resize(header.normal_count);
            mesh.tangents.resize(header.tangent_count);
            mesh.texture_coordinates.resize(header.texture_coordinate_count);
            mesh.indices.resize(header.index_count);
            read_values(stream, mesh.positions);
            read_values(stream, mesh.normals);
            read_values(stream, mesh.tangents);
            read_values(stream, mesh.texture_coordinates);
            read_values(stream, mesh.indices);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra geometry asset payload: {}", path.string()));
        }

        [[nodiscard]] AssetReference write_particle_asset(const ParticleSet& particles, const std::filesystem::path& package_root) {
            const std::size_t particle_count = particles.positions.size();
            if (particles.radii.size() != particle_count || (!particles.velocities.empty() && particles.velocities.size() != particle_count) || (!particles.colors.empty() && particles.colors.size() != particle_count) || (!particles.temperatures.empty() && particles.temperatures.size() != particle_count) || (!particles.particle_materials.empty() && particles.particle_materials.size() != particle_count)) throw std::runtime_error("Spectra particle attributes do not match the particle count");
            const ParticleAssetHeader header{
                .position_count    = particles.positions.size(),
                .radius_count      = particles.radii.size(),
                .velocity_count    = particles.velocities.size(),
                .color_count       = particles.colors.size(),
                .temperature_count = particles.temperatures.size(),
                .material_count    = particles.particle_materials.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{particles.positions}),
                std::as_bytes(std::span{particles.radii}),
                std::as_bytes(std::span{particles.velocities}),
                std::as_bytes(std::span{particles.colors}),
                std::as_bytes(std::span{particles.temperatures}),
                std::as_bytes(std::span{particles.particle_materials}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            const std::filesystem::path path = asset_path(package_root, reference, ".particles");
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".particles");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra particle asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{particles.positions});
            write_values(stream, std::span{particles.radii});
            write_values(stream, std::span{particles.velocities});
            write_values(stream, std::span{particles.colors});
            write_values(stream, std::span{particles.temperatures});
            write_values(stream, std::span{particles.particle_materials});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra particle asset: {}", path.string()));
            return reference;
        }

        void load_particle_asset(ParticleSet& particles, const std::filesystem::path& package_root) {
            verify_asset(package_root, particles.asset, ".particles");
            const std::filesystem::path path = asset_path(package_root, particles.asset, ".particles");
            std::ifstream stream{path, std::ios::binary};
            ParticleAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != ParticleAssetHeader{}.magic || header.version != 1 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra particle asset header: {}", path.string()));
            if (header.radius_count != header.position_count || (header.velocity_count != 0 && header.velocity_count != header.position_count) || (header.color_count != 0 && header.color_count != header.position_count) || (header.temperature_count != 0 && header.temperature_count != header.position_count) || (header.material_count != 0 && header.material_count != header.position_count)) throw std::runtime_error(std::format("Invalid Spectra particle attribute counts: {}", path.string()));
            const std::uint64_t expected_size = sizeof(header) + header.position_count * sizeof(math::Float3) + header.radius_count * sizeof(float) + header.velocity_count * sizeof(math::Float3) + header.color_count * sizeof(math::Float3) + header.temperature_count * sizeof(float) + header.material_count * sizeof(MaterialId);
            if (expected_size != std::filesystem::file_size(path)) throw std::runtime_error(std::format("Invalid Spectra particle asset payload size: {}", path.string()));
            particles.positions.resize(header.position_count);
            particles.radii.resize(header.radius_count);
            particles.velocities.resize(header.velocity_count);
            particles.colors.resize(header.color_count);
            particles.temperatures.resize(header.temperature_count);
            particles.particle_materials.resize(header.material_count);
            read_values(stream, particles.positions);
            read_values(stream, particles.radii);
            read_values(stream, particles.velocities);
            read_values(stream, particles.colors);
            read_values(stream, particles.temperatures);
            read_values(stream, particles.particle_materials);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra particle asset payload: {}", path.string()));
        }

        template <typename... Element>
        [[nodiscard]] AssetReference write_volume_asset_payload(const VolumeAssetHeader& header, const std::filesystem::path& package_root, const std::span<const Element>... payload) {
            const std::array blocks{std::as_bytes(std::span{&header, 1}), std::as_bytes(payload)...};
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            const std::filesystem::path path = asset_path(package_root, reference, ".volume");
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".volume");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra volume asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            (write_values(stream, payload), ...);
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra volume asset: {}", path.string()));
            return reference;
        }

        [[nodiscard]] AssetReference write_volume_asset(const DensityGridVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if (volume.density.size() != sample_count || (!volume.temperature.empty() && volume.temperature.size() != sample_count) || (!volume.emission_scale.empty() && volume.emission_scale.size() != sample_count)) throw std::runtime_error("Spectra density-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::DensityGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.density.size(),
                .secondary_element_count = volume.temperature.size(),
                .tertiary_element_count  = volume.emission_scale.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const float>{volume.density}, std::span<const float>{volume.temperature}, std::span<const float>{volume.emission_scale});
        }

        [[nodiscard]] AssetReference write_volume_asset(const RgbGridVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if ((volume.sigma_a.empty() && volume.sigma_s.empty()) || (!volume.sigma_a.empty() && volume.sigma_a.size() != sample_count) || (!volume.sigma_s.empty() && volume.sigma_s.size() != sample_count) || (!volume.emission.empty() && (volume.sigma_a.empty() || volume.emission.size() != sample_count))) throw std::runtime_error("Spectra RGB-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::RgbGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.sigma_a.size(),
                .secondary_element_count = volume.sigma_s.size(),
                .tertiary_element_count  = volume.emission.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const math::Float3>{volume.sigma_a}, std::span<const math::Float3>{volume.sigma_s}, std::span<const math::Float3>{volume.emission});
        }

        [[nodiscard]] AssetReference write_volume_asset(const NanoVdbVolume& volume, const std::filesystem::path& package_root) {
            const std::uint64_t majorant_count = volume_sample_count(volume.majorant_resolution);
            if (volume.density_data.empty() || volume.majorant.size() != majorant_count) throw std::runtime_error("Spectra NanoVDB payload and majorant grid are required");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::NanoVdb,
                .resolution              = volume.majorant_resolution,
                .primary_element_count   = volume.density_data.size(),
                .secondary_element_count = volume.temperature_data.size(),
                .tertiary_element_count  = volume.majorant.size(),
            };
            return write_volume_asset_payload(header, package_root, std::span<const std::uint32_t>{volume.density_data}, std::span<const std::uint32_t>{volume.temperature_data}, std::span<const float>{volume.majorant});
        }

        [[nodiscard]] AssetReference write_texture_asset(const ImageTexture& texture, const std::filesystem::path& package_root) {
            if (texture.width == 0 || texture.height == 0 || texture.mip_offsets.empty()) throw std::runtime_error("Image Texture dimensions and mip offsets are required");
            std::uint32_t width  = texture.width;
            std::uint32_t height = texture.height;
            std::uint64_t expected_texels{};
            for (const std::uint64_t offset : texture.mip_offsets) {
                if (offset != expected_texels) throw std::runtime_error("Image Texture mip offsets must be tightly packed");
                expected_texels += static_cast<std::uint64_t>(width) * height;
                width  = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            if (expected_texels != texture.texels.size() || texture.mip_offsets.size() > std::bit_width(std::max(texture.width, texture.height))) throw std::runtime_error("Image Texture mip pyramid is invalid");
            const TextureAssetHeader header{
                .mip_count   = static_cast<std::uint32_t>(texture.mip_offsets.size()),
                .width       = texture.width,
                .height      = texture.height,
                .texel_count = texture.texels.size(),
            };
            const std::array blocks{
                std::as_bytes(std::span{&header, 1}),
                std::as_bytes(std::span{texture.texels}),
            };
            AssetReference reference{
                .content_hash = sha256_hex(blocks),
            };
            const std::filesystem::path path = asset_path(package_root, reference, ".texture");
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, ".texture");
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra texture asset: {}", path.string()));
            write_values(stream, std::span{&header, 1});
            write_values(stream, std::span{texture.texels});
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra texture asset: {}", path.string()));
            return reference;
        }

        [[nodiscard]] VolumeAssetHeader read_volume_asset_header(const AssetReference& reference, const std::filesystem::path& package_root, std::ifstream& stream) {
            verify_asset(package_root, reference, ".volume");
            const std::filesystem::path path = asset_path(package_root, reference, ".volume");
            stream.open(path, std::ios::binary);
            VolumeAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != VolumeAssetHeader{}.magic || header.version != 2 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra volume asset header: {}", path.string()));
            return header;
        }

        void load_volume_asset(DensityGridVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::DensityGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra density-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if (header.primary_element_count != sample_count || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && header.tertiary_element_count != sample_count)) throw std::runtime_error("Invalid Spectra density-grid volume asset sample counts");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(float);
            if (expected_size != std::filesystem::file_size(asset_path(package_root, volume.asset, ".volume"))) throw std::runtime_error("Invalid Spectra density-grid volume asset payload size");
            volume.density.resize(header.primary_element_count);
            volume.temperature.resize(header.secondary_element_count);
            volume.emission_scale.resize(header.tertiary_element_count);
            read_values(stream, volume.density);
            read_values(stream, volume.temperature);
            read_values(stream, volume.emission_scale);
            if (!stream) throw std::runtime_error("Failed to read Spectra density-grid volume asset payload");
        }

        void load_volume_asset(RgbGridVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::RgbGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra RGB-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if ((header.primary_element_count == 0 && header.secondary_element_count == 0) || (header.primary_element_count != 0 && header.primary_element_count != sample_count) || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && (header.primary_element_count == 0 || header.tertiary_element_count != sample_count))) throw std::runtime_error("Invalid Spectra RGB-grid volume asset sample counts");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(math::Float3);
            if (expected_size != std::filesystem::file_size(asset_path(package_root, volume.asset, ".volume"))) throw std::runtime_error("Invalid Spectra RGB-grid volume asset payload size");
            volume.sigma_a.resize(header.primary_element_count);
            volume.sigma_s.resize(header.secondary_element_count);
            volume.emission.resize(header.tertiary_element_count);
            read_values(stream, volume.sigma_a);
            read_values(stream, volume.sigma_s);
            read_values(stream, volume.emission);
            if (!stream) throw std::runtime_error("Failed to read Spectra RGB-grid volume asset payload");
        }

        void load_volume_asset(NanoVdbVolume& volume, const std::filesystem::path& package_root) {
            std::ifstream stream{};
            const VolumeAssetHeader header = read_volume_asset_header(volume.asset, package_root, stream);
            if (header.kind != VolumeAssetKind::NanoVdb || header.primary_element_count == 0 || header.tertiary_element_count != volume_sample_count(header.resolution)) throw std::runtime_error("Invalid Spectra NanoVDB volume asset metadata");
            const std::uint64_t expected_size = sizeof(header) + (header.primary_element_count + header.secondary_element_count + header.tertiary_element_count) * sizeof(std::uint32_t);
            if (expected_size != std::filesystem::file_size(asset_path(package_root, volume.asset, ".volume"))) throw std::runtime_error("Invalid Spectra NanoVDB volume asset payload size");
            volume.majorant_resolution = header.resolution;
            volume.density_data.resize(header.primary_element_count);
            volume.temperature_data.resize(header.secondary_element_count);
            volume.majorant.resize(header.tertiary_element_count);
            read_values(stream, volume.density_data);
            read_values(stream, volume.temperature_data);
            read_values(stream, volume.majorant);
            if (!stream) throw std::runtime_error("Failed to read Spectra NanoVDB volume asset payload");
        }

        void load_texture_asset(ImageTexture& texture, const std::filesystem::path& package_root) {
            verify_asset(package_root, texture.asset, ".texture");
            const std::filesystem::path path = asset_path(package_root, texture.asset, ".texture");
            std::ifstream stream{path, std::ios::binary};
            TextureAssetHeader header{};
            stream.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!stream || header.magic != TextureAssetHeader{}.magic || header.version != 1 || header.width == 0 || header.height == 0 || header.mip_count == 0) throw std::runtime_error(std::format("Invalid Spectra texture asset header: {}", path.string()));
            std::uint32_t width  = header.width;
            std::uint32_t height = header.height;
            std::uint64_t expected_texels{};
            texture.mip_offsets.clear();
            texture.mip_offsets.reserve(header.mip_count);
            for (std::uint32_t level = 0; level != header.mip_count; ++level) {
                texture.mip_offsets.push_back(expected_texels);
                expected_texels += static_cast<std::uint64_t>(width) * height;
                width  = std::max(1u, width / 2u);
                height = std::max(1u, height / 2u);
            }
            const std::uint32_t maximum_mips  = std::bit_width(std::max(header.width, header.height));
            const std::uint64_t expected_size = sizeof(header) + header.texel_count * sizeof(math::Float4);
            if (header.mip_count > maximum_mips || header.texel_count != expected_texels || expected_size != std::filesystem::file_size(path)) throw std::runtime_error(std::format("Invalid Spectra texture asset payload size: {}", path.string()));
            texture.width  = header.width;
            texture.height = header.height;
            texture.texels.resize(header.texel_count);
            read_values(stream, texture.texels);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra texture asset payload: {}", path.string()));
        }

        void copy_asset(AssetReference& reference, const std::string_view extension, const std::filesystem::path& source_root, const std::filesystem::path& target_root) {
            verify_asset(source_root, reference, extension);
            const std::filesystem::path source = asset_path(source_root, reference, extension);
            const std::filesystem::path target = asset_path(target_root, reference, extension);
            if (source == target) return;
            std::filesystem::create_directories(target.parent_path());
            if (std::filesystem::exists(target)) {
                verify_asset(target_root, reference, extension);
                return;
            }
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none);
        }

        struct KdlWriter {
            std::string content{};
            std::uint32_t indentation{};

            void line(const std::string_view value) {
                this->content.append(this->indentation * 4, ' ');
                this->content.append(value);
                this->content.push_back('\n');
            }

            void begin(const std::string_view value) {
                this->line(std::format("{} {{", value));
                ++this->indentation;
            }

            void end() {
                --this->indentation;
                this->line("}");
            }
        };

        [[nodiscard]] std::string kdl_string(const std::string_view value) {
            std::string result{"\""};
            for (const unsigned char character : value) {
                switch (character) {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\b': result += "\\b"; break;
                    case '\f': result += "\\f"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default:
                        if (character < 0x20) result += std::format("\\u{{{:x}}}", character);
                        else result.push_back(static_cast<char>(character));
                }
            }
            result.push_back('"');
            return result;
        }

        template <class Value>
        void kdl_number_property(std::string& line, const std::string_view name, const Value value) {
            line += std::format(" {}={}", name, value);
        }

        void kdl_string_property(std::string& line, const std::string_view name, const std::string_view value) {
            line += std::format(" {}={}", name, kdl_string(value));
        }

        void kdl_bool_property(std::string& line, const std::string_view name, const bool value) {
            line += std::format(" {}=#{}", name, value ? "true" : "false");
        }

        void write_transform(KdlWriter& writer, const std::string_view name, const math::Transform& transform) {
            writer.begin(name);
            for (std::uint32_t row = 0; row != 4; ++row) writer.line(std::format("row {} {} {} {}", transform.matrix[row * 4], transform.matrix[row * 4 + 1], transform.matrix[row * 4 + 2], transform.matrix[row * 4 + 3]));
            writer.end();
        }

        void write_bounds(KdlWriter& writer, const math::Bounds3 bounds) {
            writer.line(std::format("bounds {} {} {} {} {} {}", bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y, bounds.maximum.z));
        }

        [[nodiscard]] std::string spectrum_encoding_name(const SpectrumEncoding encoding) {
            switch (encoding) {
                case SpectrumEncoding::RgbAlbedo: return "rgb-albedo";
                case SpectrumEncoding::RgbUnbounded: return "rgb-unbounded";
                case SpectrumEncoding::RgbIlluminant: return "rgb-illuminant";
                case SpectrumEncoding::Constant: return "constant";
                case SpectrumEncoding::Blackbody: return "blackbody";
                case SpectrumEncoding::PiecewiseLinear: return "piecewise-linear";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string spectrum_color_space_name(const SpectrumColorSpace color_space) {
            switch (color_space) {
                case SpectrumColorSpace::Srgb: return "srgb";
                case SpectrumColorSpace::Rec2020: return "rec2020";
                case SpectrumColorSpace::Aces2065_1: return "aces2065-1";
            }
            std::unreachable();
        }

        void write_spectrum(KdlWriter& writer, const std::string_view name, const SpectrumParameter& spectrum) {
            std::string line = std::format("{} {}", name, spectrum_encoding_name(spectrum.encoding));
            if (spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) line += std::format(" {} {} {}", spectrum.value.x, spectrum.value.y, spectrum.value.z);
            if (spectrum.encoding == SpectrumEncoding::Constant) line += std::format(" {}", spectrum.scalar);
            if (spectrum.encoding == SpectrumEncoding::Blackbody) line += std::format(" {}", spectrum.temperature);
            if (spectrum.texture.value != 0) kdl_number_property(line, "texture", spectrum.texture.value);
            if ((spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) && spectrum.color_space != SpectrumColorSpace::Srgb) kdl_string_property(line, "color-space", spectrum_color_space_name(spectrum.color_space));
            if (spectrum.encoding != SpectrumEncoding::PiecewiseLinear) {
                writer.line(line);
                return;
            }
            writer.begin(line);
            for (std::size_t index = 0; index != spectrum.wavelengths.size(); ++index) writer.line(std::format("sample {} {}", spectrum.wavelengths[index], spectrum.samples[index]));
            writer.end();
        }

        void write_float_parameter(KdlWriter& writer, const std::string_view name, const FloatParameter parameter) {
            std::string line = std::format("{} {}", name, parameter.value);
            if (parameter.texture.value != 0) kdl_number_property(line, "texture", parameter.texture.value);
            writer.line(line);
        }

        void write_roughness(KdlWriter& writer, const MaterialRoughness& roughness) {
            if (roughness.roughness.value != 0.0f || roughness.roughness.texture.value != 0) write_float_parameter(writer, "roughness", roughness.roughness);
            if (roughness.u_roughness) write_float_parameter(writer, "u-roughness", *roughness.u_roughness);
            if (roughness.v_roughness) write_float_parameter(writer, "v-roughness", *roughness.v_roughness);
        }

        void write_texture_mapping(KdlWriter& writer, const TextureMapping& mapping) {
            std::visit(
                [&writer](const auto& value) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, UvTextureMapping>) {
                        if (value.scale == math::Float2{1.0f, 1.0f} && value.offset == math::Float2{}) return;
                        writer.begin("mapping uv");
                        if (value.scale != math::Float2{1.0f, 1.0f}) writer.line(std::format("scale {} {}", value.scale.x, value.scale.y));
                        if (value.offset != math::Float2{}) writer.line(std::format("offset {} {}", value.offset.x, value.offset.y));
                        writer.end();
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, PlanarTextureMapping>) {
                        writer.begin("mapping planar");
                        if (value.first_axis != math::Float3{1.0f, 0.0f, 0.0f}) writer.line(std::format("first-axis {} {} {}", value.first_axis.x, value.first_axis.y, value.first_axis.z));
                        if (value.second_axis != math::Float3{0.0f, 1.0f, 0.0f}) writer.line(std::format("second-axis {} {} {}", value.second_axis.x, value.second_axis.y, value.second_axis.z));
                        if (value.offset != math::Float2{}) writer.line(std::format("offset {} {}", value.offset.x, value.offset.y));
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, SphericalTextureMapping>) {
                        writer.begin("mapping spherical");
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    } else {
                        writer.begin("mapping cylindrical");
                        if (value.texture_from_render != math::Transform{}) write_transform(writer, "transform", value.texture_from_render);
                        writer.end();
                    }
                },
                mapping.data);
        }

        void write_checkerboard_mapping(KdlWriter& writer, const CheckerboardMapping& mapping) {
            if (const TextureMapping* two_dimensional = std::get_if<TextureMapping>(&mapping.data)) {
                write_texture_mapping(writer, *two_dimensional);
                return;
            }
            writer.begin("mapping \"3d\"");
            const TextureMapping3D& three_dimensional = std::get<TextureMapping3D>(mapping.data);
            if (three_dimensional.texture_from_render != math::Transform{}) write_transform(writer, "transform", three_dimensional.texture_from_render);
            writer.end();
        }

        void write_geometries(KdlWriter& writer, const std::vector<Geometry>& geometries) {
            if (geometries.empty()) return;
            writer.begin("geometries");
            for (const Geometry& geometry : geometries) {
                std::visit(
                    [&writer, &geometry](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                            writer.line(std::format("triangle-mesh {} {} asset={}", geometry.id.value, kdl_string(geometry.name), kdl_string(data.asset.content_hash)));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) {
                            std::string line = std::format("sphere {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.z_min != -1.0f) kdl_number_property(line, "z-min", data.z_min);
                            if (data.z_max != 1.0f) kdl_number_property(line, "z-max", data.z_max);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                            const std::string line = std::format("box {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.bounds == BoxGeometry{}.bounds) writer.line(line);
                            else {
                                writer.begin(line);
                                write_bounds(writer, data.bounds);
                                writer.end();
                            }
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) {
                            const std::string line = std::format("rectangle {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.minimum == RectangleGeometry{}.minimum && data.maximum == RectangleGeometry{}.maximum) writer.line(line);
                            else {
                                writer.begin(line);
                                if (data.minimum != RectangleGeometry{}.minimum) writer.line(std::format("minimum {} {}", data.minimum.x, data.minimum.y));
                                if (data.maximum != RectangleGeometry{}.maximum) writer.line(std::format("maximum {} {}", data.maximum.x, data.maximum.y));
                                writer.end();
                            }
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) {
                            std::string line = std::format("disk {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.height != 0.0f) kdl_number_property(line, "height", data.height);
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.inner_radius != 0.0f) kdl_number_property(line, "inner-radius", data.inner_radius);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        } else {
                            std::string line = std::format("cylinder {} {}", geometry.id.value, kdl_string(geometry.name));
                            if (data.radius != 1.0f) kdl_number_property(line, "radius", data.radius);
                            if (data.z_min != -1.0f) kdl_number_property(line, "z-min", data.z_min);
                            if (data.z_max != 1.0f) kdl_number_property(line, "z-max", data.z_max);
                            if (data.phi_max != 360.0f) kdl_number_property(line, "phi-max", data.phi_max);
                            writer.line(line);
                        }
                    },
                    geometry.data);
            }
            writer.end();
        }

        void write_particle_sets(KdlWriter& writer, const std::vector<ParticleSet>& particle_sets) {
            if (particle_sets.empty()) return;
            writer.begin("particle-sets");
            for (const ParticleSet& particles : particle_sets) {
                std::string line = std::format("particle-set {} {} asset={}", particles.id.value, kdl_string(particles.name), kdl_string(particles.asset.content_hash));
                if (particles.material.value != 0) kdl_number_property(line, "material", particles.material.value);
                writer.line(line);
            }
            writer.end();
        }

        void write_volumes(KdlWriter& writer, const std::vector<Volume>& volumes) {
            if (volumes.empty()) return;
            writer.begin("volumes");
            for (const Volume& volume : volumes) {
                std::visit(
                    [&writer, &volume](const auto& data) {
                        std::string kind{};
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DensityGridVolume>) kind = "density-grid";
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RgbGridVolume>) kind = "rgb-grid";
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, NanoVdbVolume>) kind = "nanovdb";
                        else kind = "procedural-cloud";
                        std::string line = std::format("{} {} {}", kind, volume.id.value, kdl_string(volume.name));
                        if constexpr (!std::same_as<std::remove_cvref_t<decltype(data)>, ProceduralCloudVolume>) kdl_string_property(line, "asset", data.asset.content_hash);
                        writer.begin(line);
                        if (volume.bounds != math::Bounds3{}) write_bounds(writer, volume.bounds);
                        if (volume.transform != math::Transform{}) write_transform(writer, "transform", volume.transform);
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DensityGridVolume>) {
                            writer.line(std::format("resolution {} {} {}", data.resolution.x, data.resolution.y, data.resolution.z));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RgbGridVolume>) {
                            writer.line(std::format("resolution {} {} {}", data.resolution.x, data.resolution.y, data.resolution.z));
                            if (data.color_space != SpectrumColorSpace::Srgb) writer.line(std::format("color-space {}", spectrum_color_space_name(data.color_space)));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, NanoVdbVolume>) {
                            if (data.density_grid != "density") writer.line(std::format("density-grid {}", kdl_string(data.density_grid)));
                            if (data.temperature_grid) writer.line(std::format("temperature-grid {}", kdl_string(*data.temperature_grid)));
                        } else {
                            if (data.density != 1.0f) writer.line(std::format("density {}", data.density));
                            if (data.wispiness != 1.0f) writer.line(std::format("wispiness {}", data.wispiness));
                            if (data.frequency != 5.0f) writer.line(std::format("frequency {}", data.frequency));
                        }
                        writer.end();
                    },
                    volume.data);
            }
            writer.end();
        }

        [[nodiscard]] std::string texture_value_kind_name(const TextureValueKind kind) {
            return kind == TextureValueKind::Float ? "float" : "spectrum";
        }

        [[nodiscard]] std::string texture_spectrum_type_name(const TextureSpectrumType type) {
            switch (type) {
                case TextureSpectrumType::Albedo: return "albedo";
                case TextureSpectrumType::Unbounded: return "unbounded";
                case TextureSpectrumType::Illuminant: return "illuminant";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_color_space_name(const TextureColorSpace color_space) {
            switch (color_space) {
                case TextureColorSpace::Linear: return "linear";
                case TextureColorSpace::Srgb: return "srgb";
                case TextureColorSpace::Aces2065_1: return "aces2065-1";
                case TextureColorSpace::Rec2020: return "rec2020";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_wrap_name(const TextureWrapMode wrap) {
            switch (wrap) {
                case TextureWrapMode::Repeat: return "repeat";
                case TextureWrapMode::Clamp: return "clamp";
                case TextureWrapMode::Black: return "black";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_channel_name(const TextureChannel channel) {
            switch (channel) {
                case TextureChannel::Red: return "red";
                case TextureChannel::Green: return "green";
                case TextureChannel::Blue: return "blue";
                case TextureChannel::Alpha: return "alpha";
                case TextureChannel::Average: return "average";
                case TextureChannel::Luminance: return "luminance";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string texture_filter_name(const TextureFilter filter) {
            switch (filter) {
                case TextureFilter::Point: return "point";
                case TextureFilter::Bilinear: return "bilinear";
                case TextureFilter::Trilinear: return "trilinear";
                case TextureFilter::Ewa: return "ewa";
            }
            std::unreachable();
        }

        void add_texture_properties(std::string& line, const Texture& texture) {
            if (texture.value_kind != TextureValueKind::Spectrum) kdl_string_property(line, "value", texture_value_kind_name(texture.value_kind));
            if (texture.spectrum_type != TextureSpectrumType::Albedo) kdl_string_property(line, "spectrum-type", texture_spectrum_type_name(texture.spectrum_type));
            if (texture.color_space != TextureColorSpace::Srgb) kdl_string_property(line, "color-space", texture_color_space_name(texture.color_space));
        }

        void write_textures(KdlWriter& writer, const std::vector<Texture>& textures) {
            if (textures.empty()) return;
            writer.begin("textures");
            for (const Texture& texture : textures) {
                std::visit(
                    [&writer, &texture](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConstantTexture>) {
                            std::string line = std::format("constant {} {}", texture.id.value, kdl_string(texture.name));
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            if (texture.value_kind == TextureValueKind::Float) writer.line(std::format("scalar {}", data.scalar));
                            else write_spectrum(writer, "spectrum", data.spectrum);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ImageTexture>) {
                            std::string line = std::format("image {} {} asset={}", texture.id.value, kdl_string(texture.name), kdl_string(data.asset.content_hash));
                            add_texture_properties(line, texture);
                            if (data.wrap != TextureWrapMode::Repeat) kdl_string_property(line, "wrap", texture_wrap_name(data.wrap));
                            if (data.channel != TextureChannel::Luminance) kdl_string_property(line, "channel", texture_channel_name(data.channel));
                            if (data.filter != TextureFilter::Bilinear) kdl_string_property(line, "filter", texture_filter_name(data.filter));
                            if (data.maximum_anisotropy != 8.0f) kdl_number_property(line, "maximum-anisotropy", data.maximum_anisotropy);
                            if (data.scale != 1.0f) kdl_number_property(line, "scale", data.scale);
                            if (data.invert) kdl_bool_property(line, "invert", true);
                            writer.begin(line);
                            write_texture_mapping(writer, data.mapping);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CheckerboardTexture>) {
                            std::string line = std::format("checkerboard {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            write_checkerboard_mapping(writer, data.mapping);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ScaleTexture>) {
                            std::string line = std::format("scale {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, MixTexture>) {
                            std::string line = std::format("mix {} {} first={} second={} amount={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value, data.amount.value);
                            add_texture_properties(line, texture);
                            writer.line(line);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DirectionMixTexture>) {
                            std::string line = std::format("direction-mix {} {} first={} second={}", texture.id.value, kdl_string(texture.name), data.first.value, data.second.value);
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            if (data.direction != math::Float3{0.0f, 1.0f, 0.0f}) writer.line(std::format("direction {} {} {}", data.direction.x, data.direction.y, data.direction.z));
                            writer.end();
                        } else {
                            std::string line = std::format("bilerp {} {}", texture.id.value, kdl_string(texture.name));
                            add_texture_properties(line, texture);
                            writer.begin(line);
                            for (std::uint32_t corner = 0; corner != 4; ++corner) {
                                if (texture.value_kind == TextureValueKind::Float) writer.line(std::format("corner {} {}", corner, data.scalars[corner]));
                                else write_spectrum(writer, std::format("corner-{}", corner), data.spectra[corner]);
                            }
                            write_texture_mapping(writer, data.mapping);
                            writer.end();
                        }
                    },
                    texture.data);
            }
            writer.end();
        }

        void write_normal_and_bump(KdlWriter& writer, const TextureId normal_map, const TextureId bump_map) {
            if (normal_map.value != 0) writer.line(std::format("normal-map {}", normal_map.value));
            if (bump_map.value != 0) writer.line(std::format("bump-map {}", bump_map.value));
        }

        void write_conductor_optics(KdlWriter& writer, const std::variant<ConductorEtaK, ConductorReflectance>& optics) {
            if (const ConductorEtaK* eta_k = std::get_if<ConductorEtaK>(&optics)) {
                writer.begin("eta-k");
                write_spectrum(writer, "eta", eta_k->eta);
                write_spectrum(writer, "k", eta_k->k);
                writer.end();
                return;
            }
            writer.begin("reflectance-optics");
            write_spectrum(writer, "reflectance", std::get<ConductorReflectance>(optics).reflectance);
            writer.end();
        }

        void write_coating(KdlWriter& writer, const CoatingLayer& coating) {
            writer.begin("coating");
            if (coating.thickness.value != 0.01f || coating.thickness.texture.value != 0) write_float_parameter(writer, "thickness", coating.thickness);
            write_spectrum(writer, "albedo", coating.albedo);
            if (coating.g.value != 0.0f || coating.g.texture.value != 0) write_float_parameter(writer, "g", coating.g);
            if (coating.max_depth != 10) writer.line(std::format("maximum-depth {}", coating.max_depth));
            if (coating.sample_count != 1) writer.line(std::format("samples {}", coating.sample_count));
            writer.end();
        }

        void write_materials(KdlWriter& writer, const std::vector<Material>& materials) {
            if (materials.empty()) return;
            writer.begin("materials");
            for (const Material& material : materials) {
                std::visit(
                    [&writer, &material](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InterfaceMaterialData>) {
                            writer.line(std::format("interface {} {}", material.id.value, kdl_string(material.name)));
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseMaterialData>) {
                            writer.begin(std::format("diffuse {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseTransmissionMaterialData>) {
                            writer.begin(std::format("diffuse-transmission {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_spectrum(writer, "transmittance", data.transmittance);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ConductorMaterialData>) {
                            writer.begin(std::format("conductor {} {}", material.id.value, kdl_string(material.name)));
                            write_conductor_optics(writer, data.optics);
                            write_roughness(writer, data.distribution);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DielectricMaterialData>) {
                            writer.begin(std::format("dielectric {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "eta", data.eta);
                            write_roughness(writer, data.distribution);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, ThinDielectricMaterialData>) {
                            writer.begin(std::format("thin-dielectric {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "eta", data.eta);
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedDiffuseMaterialData>) {
                            writer.begin(std::format("coated-diffuse {} {}", material.id.value, kdl_string(material.name)));
                            write_spectrum(writer, "reflectance", data.reflectance);
                            write_spectrum(writer, "eta", data.eta);
                            writer.begin("interface");
                            write_roughness(writer, data.interface);
                            writer.end();
                            write_coating(writer, data.coating);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, CoatedConductorMaterialData>) {
                            writer.begin(std::format("coated-conductor {} {}", material.id.value, kdl_string(material.name)));
                            writer.begin("interface");
                            write_spectrum(writer, "eta", data.interface_eta);
                            write_roughness(writer, data.interface);
                            writer.end();
                            writer.begin("conductor");
                            write_conductor_optics(writer, data.optics);
                            write_roughness(writer, data.conductor);
                            writer.end();
                            write_coating(writer, data.coating);
                            if (!data.remap_roughness) writer.line("remap-roughness #false");
                            write_normal_and_bump(writer, data.normal_map, data.bump_map);
                            writer.end();
                        } else {
                            writer.begin(std::format("mix {} {} first={} second={}", material.id.value, kdl_string(material.name), data.first.value, data.second.value));
                            if (data.amount.value != 0.5f || data.amount.texture.value != 0) write_float_parameter(writer, "amount", data.amount);
                            writer.end();
                        }
                    },
                    material.data);
            }
            writer.end();
        }

        void write_media(KdlWriter& writer, const std::vector<Medium>& media) {
            if (media.empty()) return;
            writer.begin("media");
            for (const Medium& medium : media) {
                std::visit(
                    [&writer, &medium](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, HomogeneousMedium>) {
                            writer.begin(std::format("homogeneous {} {}", medium.id.value, kdl_string(medium.name)));
                            write_spectrum(writer, "sigma-a", data.sigma_a);
                            write_spectrum(writer, "sigma-s", data.sigma_s);
                            write_spectrum(writer, "emission", data.emission);
                            if (data.density_scale != 1.0f) writer.line(std::format("density-scale {}", data.density_scale));
                            if (data.emission_scale != 1.0f) writer.line(std::format("emission-scale {}", data.emission_scale));
                            if (data.anisotropy != 0.0f) writer.line(std::format("anisotropy {}", data.anisotropy));
                            writer.end();
                        } else {
                            writer.begin(std::format("volume {} {} volume={}", medium.id.value, kdl_string(medium.name), data.volume.value));
                            write_spectrum(writer, "sigma-a", data.sigma_a);
                            write_spectrum(writer, "sigma-s", data.sigma_s);
                            write_spectrum(writer, "emission", data.emission);
                            if (data.density_scale != 1.0f) writer.line(std::format("density-scale {}", data.density_scale));
                            if (data.emission_scale != 1.0f) writer.line(std::format("emission-scale {}", data.emission_scale));
                            if (data.anisotropy != 0.0f) writer.line(std::format("anisotropy {}", data.anisotropy));
                            if (data.temperature_scale != 1.0f) writer.line(std::format("temperature-scale {}", data.temperature_scale));
                            if (data.temperature_offset != 0.0f) writer.line(std::format("temperature-offset {}", data.temperature_offset));
                            if (data.minimum_emission_temperature != 100.0f) writer.line(std::format("minimum-emission-temperature {}", data.minimum_emission_temperature));
                            if (data.blackbody_emission) writer.line("blackbody-emission #true");
                            writer.end();
                        }
                    },
                    medium.data);
            }
            writer.end();
        }

        void write_infinite_light(KdlWriter& writer, const InfiniteLight& light) {
            write_spectrum(writer, "radiance", light.radiance);
            if (light.transform != math::Transform{}) write_transform(writer, "transform", light.transform);
            if (light.scale != 1.0f) writer.line(std::format("scale {}", light.scale));
            if (light.emission_texture.value != 0) writer.line(std::format("emission-texture {}", light.emission_texture.value));
        }

        void write_lights(KdlWriter& writer, const std::vector<Light>& lights) {
            if (lights.empty()) return;
            writer.begin("lights");
            for (const Light& light : lights) {
                std::visit(
                    [&writer, &light](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PointLight>) {
                            writer.begin(std::format("point {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "intensity", data.intensity);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SpotLight>) {
                            writer.begin(std::format("spot {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "intensity", data.intensity);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            if (data.cone_angle != 30.0f) writer.line(std::format("cone-angle {}", data.cone_angle));
                            if (data.cone_delta != 5.0f) writer.line(std::format("cone-delta {}", data.cone_delta));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DistantLight>) {
                            writer.begin(std::format("distant {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "radiance", data.radiance);
                            if (data.transform != math::Transform{}) write_transform(writer, "transform", data.transform);
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiffuseAreaLight>) {
                            writer.begin(std::format("diffuse-area {} {}", light.id.value, kdl_string(light.name)));
                            write_spectrum(writer, "radiance", data.radiance);
                            if (data.sidedness == EmissionSidedness::Both) writer.line("sidedness both");
                            if (data.scale != 1.0f) writer.line(std::format("scale {}", data.scale));
                            if (data.power) writer.line(std::format("power {}", *data.power));
                            if (data.emission_texture.value != 0) writer.line(std::format("emission-texture {}", data.emission_texture.value));
                            writer.end();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, InfiniteLight>) {
                            writer.begin(std::format("infinite {} {}", light.id.value, kdl_string(light.name)));
                            write_infinite_light(writer, data);
                            writer.end();
                        } else {
                            writer.begin(std::format("portal-infinite {} {}", light.id.value, kdl_string(light.name)));
                            writer.begin("environment");
                            write_infinite_light(writer, data.environment);
                            writer.end();
                            for (const std::array<math::Float3, 4>& portal : data.portals) {
                                writer.begin("portal");
                                for (const math::Float3 corner : portal) writer.line(std::format("corner {} {} {}", corner.x, corner.y, corner.z));
                                writer.end();
                            }
                            writer.end();
                        }
                    },
                    light.data);
            }
            writer.end();
        }

        void write_cameras(KdlWriter& writer, const std::vector<Camera>& cameras) {
            if (cameras.empty()) return;
            writer.begin("cameras");
            for (const Camera& camera : cameras) {
                std::visit(
                    [&writer, &camera](const auto& data) {
                        const std::string kind = std::same_as<std::remove_cvref_t<decltype(data)>, PerspectiveCameraData> ? "perspective" : "orthographic";
                        std::string line       = std::format("{} {} {}", kind, camera.id.value, kdl_string(camera.name));
                        if (camera.exposure_time != 1.0f) kdl_number_property(line, "exposure-time", camera.exposure_time);
                        if (camera.medium.value != 0) kdl_number_property(line, "medium", camera.medium.value);
                        writer.begin(line);
                        if (camera.transform != math::Transform{}) write_transform(writer, "transform", camera.transform);
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, PerspectiveCameraData>)
                            if (data.vertical_fov != 45.0f) writer.line(std::format("vertical-fov {}", data.vertical_fov));
                        if (data.screen_window.minimum != math::Float2{-1.0f, -1.0f} || data.screen_window.maximum != math::Float2{1.0f, 1.0f}) writer.line(std::format("screen {} {} {} {}", data.screen_window.minimum.x, data.screen_window.minimum.y, data.screen_window.maximum.x, data.screen_window.maximum.y));
                        if (data.lens_radius != 0.0f) writer.line(std::format("lens-radius {}", data.lens_radius));
                        if (data.focal_distance != 1.0f) writer.line(std::format("focal-distance {}", data.focal_distance));
                        if (data.near_plane != 0.01f) writer.line(std::format("near-plane {}", data.near_plane));
                        if (data.far_plane != 1000.0f) writer.line(std::format("far-plane {}", data.far_plane));
                        writer.end();
                    },
                    camera.data);
            }
            writer.end();
        }

        [[nodiscard]] std::string filter_kind_name(const FilterKind kind) {
            switch (kind) {
                case FilterKind::Box: return "box";
                case FilterKind::Gaussian: return "gaussian";
                case FilterKind::Mitchell: return "mitchell";
                case FilterKind::Sinc: return "sinc";
                case FilterKind::Triangle: return "triangle";
            }
            std::unreachable();
        }

        void write_float_chunks(KdlWriter& writer, const std::string_view name, const std::span<const float> values) {
            if (values.empty()) return;
            writer.begin(name);
            for (std::size_t offset = 0; offset < values.size(); offset += 16) {
                std::string line{"values"};
                for (const float value : values.subspan(offset, std::min<std::size_t>(16, values.size() - offset))) line += std::format(" {}", value);
                writer.line(line);
            }
            writer.end();
        }

        void write_films(KdlWriter& writer, const std::vector<Film>& films) {
            if (films.empty()) return;
            writer.begin("films");
            for (const Film& film : films) {
                std::string line = std::format("film {} {}", film.id.value, kdl_string(film.name));
                if (film.exposure != 0.0f) kdl_number_property(line, "exposure", film.exposure);
                if (film.iso != 100.0f) kdl_number_property(line, "iso", film.iso);
                if (film.color_space != SpectrumColorSpace::Srgb) kdl_string_property(line, "color-space", spectrum_color_space_name(film.color_space));
                if (film.maximum_component_value) kdl_number_property(line, "maximum-component", *film.maximum_component_value);
                if (film.gbuffer) kdl_bool_property(line, "gbuffer", true);
                if (film.gbuffer && !film.gbuffer_camera_space) kdl_bool_property(line, "gbuffer-camera-space", false);
                writer.begin(line);
                writer.line(std::format("resolution {} {}", film.resolution[0], film.resolution[1]));
                if (film.pixel_minimum != std::array<std::uint32_t, 2>{} || film.pixel_maximum != film.resolution) writer.line(std::format("pixel-range {} {} {} {}", film.pixel_minimum[0], film.pixel_minimum[1], film.pixel_maximum[0], film.pixel_maximum[1]));
                write_float_chunks(writer, "sensor-response", film.sensor_response);
                if (film.sensor_to_output_rgb != Film{}.sensor_to_output_rgb) {
                    writer.begin("sensor-to-output-rgb");
                    for (std::uint32_t row = 0; row != 3; ++row) writer.line(std::format("row {} {} {}", film.sensor_to_output_rgb[row * 3], film.sensor_to_output_rgb[row * 3 + 1], film.sensor_to_output_rgb[row * 3 + 2]));
                    writer.end();
                }
                if (film.filter != Filter{}) {
                    std::string filter = std::format("filter {}", filter_kind_name(film.filter.kind));
                    if (film.filter.radius != math::Float2{0.5f, 0.5f}) filter += std::format(" radius-x={} radius-y={}", film.filter.radius.x, film.filter.radius.y);
                    if (film.filter.sigma != 0.5f) kdl_number_property(filter, "sigma", film.filter.sigma);
                    if (film.filter.b != 1.0f / 3.0f) kdl_number_property(filter, "b", film.filter.b);
                    if (film.filter.c != 1.0f / 3.0f) kdl_number_property(filter, "c", film.filter.c);
                    if (film.filter.tau != 3.0f) kdl_number_property(filter, "tau", film.filter.tau);
                    writer.line(filter);
                }
                writer.end();
            }
            writer.end();
        }

        [[nodiscard]] std::string sampler_kind_name(const SamplerKind kind) {
            switch (kind) {
                case SamplerKind::Independent: return "independent";
                case SamplerKind::Stratified: return "stratified";
                case SamplerKind::Halton: return "halton";
                case SamplerKind::Sobol: return "sobol";
                case SamplerKind::PaddedSobol: return "padded-sobol";
                case SamplerKind::ZSobol: return "zsobol";
                case SamplerKind::Pmj02bn: return "pmj02bn";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string sampler_randomization_name(const SamplerRandomization randomization) {
            switch (randomization) {
                case SamplerRandomization::None: return "none";
                case SamplerRandomization::PermuteDigits: return "permute-digits";
                case SamplerRandomization::FastOwen: return "fast-owen";
                case SamplerRandomization::Owen: return "owen";
            }
            std::unreachable();
        }

        void write_samplers(KdlWriter& writer, const std::vector<Sampler>& samplers) {
            if (samplers.empty()) return;
            writer.begin("samplers");
            for (const Sampler& sampler : samplers) {
                std::string line = std::format("sampler {} {}", sampler.id.value, kdl_string(sampler.name));
                if (sampler.kind != SamplerKind::Independent) kdl_string_property(line, "kind", sampler_kind_name(sampler.kind));
                if (sampler.samples_per_pixel != 1) kdl_number_property(line, "samples", sampler.samples_per_pixel);
                if (sampler.seed != 0) kdl_number_property(line, "seed", sampler.seed);
                if (!sampler.jitter) kdl_bool_property(line, "jitter", false);
                if (sampler.x_strata != 1) kdl_number_property(line, "x-strata", sampler.x_strata);
                if (sampler.y_strata != 1) kdl_number_property(line, "y-strata", sampler.y_strata);
                if (sampler.randomization != SamplerRandomization::Owen) kdl_string_property(line, "randomization", sampler_randomization_name(sampler.randomization));
                writer.line(line);
            }
            writer.end();
        }

        void write_prototypes(KdlWriter& writer, const std::vector<Prototype>& prototypes) {
            if (prototypes.empty()) return;
            writer.begin("prototypes");
            for (const Prototype& prototype : prototypes) {
                writer.begin(std::format("prototype {} {}", prototype.id.value, kdl_string(prototype.name)));
                for (const Primitive& primitive : prototype.primitives) {
                    std::string line{"primitive"};
                    if (primitive.geometry.value != 0) kdl_number_property(line, "geometry", primitive.geometry.value);
                    if (primitive.particles.value != 0) kdl_number_property(line, "particles", primitive.particles.value);
                    if (primitive.volume.value != 0) kdl_number_property(line, "volume", primitive.volume.value);
                    if (primitive.material.value != 0) kdl_number_property(line, "material", primitive.material.value);
                    if (primitive.area_light.value != 0) kdl_number_property(line, "area-light", primitive.area_light.value);
                    if (primitive.alpha.value != 0) kdl_number_property(line, "alpha", primitive.alpha.value);
                    if (primitive.reverse_orientation) kdl_bool_property(line, "reverse-orientation", true);
                    const bool has_children = primitive.media.inside.value != 0 || primitive.media.outside.value != 0 || !primitive.face_materials.empty() || primitive.transform != math::Transform{};
                    if (!has_children) {
                        writer.line(line);
                        continue;
                    }
                    writer.begin(line);
                    if (primitive.media.inside.value != 0 || primitive.media.outside.value != 0) {
                        std::string media{"media"};
                        if (primitive.media.inside.value != 0) kdl_number_property(media, "inside", primitive.media.inside.value);
                        if (primitive.media.outside.value != 0) kdl_number_property(media, "outside", primitive.media.outside.value);
                        writer.line(media);
                    }
                    if (!primitive.face_materials.empty()) {
                        std::string face_materials{"face-materials"};
                        for (const MaterialId material : primitive.face_materials) face_materials += std::format(" {}", material.value);
                        writer.line(face_materials);
                    }
                    if (primitive.transform != math::Transform{}) write_transform(writer, "transform", primitive.transform);
                    writer.end();
                }
                writer.end();
            }
            writer.end();
        }

        void write_instances(KdlWriter& writer, const std::vector<Instance>& instances) {
            if (instances.empty()) return;
            writer.begin("instances");
            for (const Instance& instance : instances) {
                std::string line = std::format("instance {} {} prototype={}", instance.id.value, kdl_string(instance.name), instance.prototype.value);
                if (!instance.visible) kdl_bool_property(line, "visible", false);
                if (instance.transform == math::Transform{}) writer.line(line);
                else {
                    writer.begin(line);
                    write_transform(writer, "transform", instance.transform);
                    writer.end();
                }
            }
            writer.end();
        }

        [[nodiscard]] std::string light_sampler_name(const LightSamplerKind kind) {
            switch (kind) {
                case LightSamplerKind::Uniform: return "uniform";
                case LightSamplerKind::Power: return "power";
                case LightSamplerKind::Bvh: return "bvh";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string dynamic_resource_kind_name(const DynamicResourceKind kind) {
            switch (kind) {
                case DynamicResourceKind::Instance: return "instance";
                case DynamicResourceKind::Geometry: return "geometry";
                case DynamicResourceKind::ParticleSet: return "particle-set";
                case DynamicResourceKind::Volume: return "volume";
            }
            std::unreachable();
        }

        [[nodiscard]] std::string dynamic_parameter_kind_name(const DynamicParameterKind kind) {
            switch (kind) {
                case DynamicParameterKind::Boolean: return "boolean";
                case DynamicParameterKind::Integer: return "integer";
                case DynamicParameterKind::Float: return "float";
                case DynamicParameterKind::Float3: return "float3";
                case DynamicParameterKind::Enumeration: return "enumeration";
            }
            std::unreachable();
        }

        void write_dynamics(KdlWriter& writer, const DynamicSetup& setup) {
            std::string line{"dynamics"};
            if (setup.seed != 0) kdl_number_property(line, "seed", setup.seed);
            writer.begin(line);
            std::string clock{"clock"};
            if (setup.clock.step_seconds != 1.0 / 120.0) kdl_number_property(clock, "step-seconds", setup.clock.step_seconds);
            if (setup.clock.start_step != 0) kdl_number_property(clock, "start-step", setup.clock.start_step);
            if (setup.clock.end_step) kdl_number_property(clock, "end-step", *setup.clock.end_step);
            if (setup.clock.loop) kdl_bool_property(clock, "loop", true);
            if (clock != "clock") writer.line(clock);
            for (const DynamicSystem& system : setup.systems) {
                std::string system_line = std::format("system {} {} provider={}", kdl_string(system.id.value), kdl_string(system.name), kdl_string(system.provider_id));
                if (!system.enabled) kdl_bool_property(system_line, "enabled", false);
                if (!system.visible) kdl_bool_property(system_line, "visible", false);
                writer.begin(system_line);
                for (const DynamicParameterSetting& parameter : system.parameters) {
                    std::string parameter_line = std::format("parameter {} {}", kdl_string(parameter.parameter_id), dynamic_parameter_kind_name(parameter.value.kind));
                    if (parameter.value.kind == DynamicParameterKind::Boolean) parameter_line += std::format(" #{}", parameter.value.integer != 0 ? "true" : "false");
                    else if (parameter.value.kind == DynamicParameterKind::Integer || parameter.value.kind == DynamicParameterKind::Enumeration) parameter_line += std::format(" {}", parameter.value.integer);
                    else if (parameter.value.kind == DynamicParameterKind::Float) parameter_line += std::format(" {}", parameter.value.floating[0]);
                    else parameter_line += std::format(" {} {} {}", parameter.value.floating[0], parameter.value.floating[1], parameter.value.floating[2]);
                    writer.line(parameter_line);
                }
                for (const DynamicPortBinding& binding : system.bindings) writer.line(std::format("bind {} {} {}", kdl_string(binding.port_id), dynamic_resource_kind_name(binding.resource_kind), binding.resource_id));
                writer.end();
            }
            writer.end();
        }

        [[nodiscard]] std::string serialize_scene_kdl(const Scene& scene) {
            KdlWriter writer{};
            writer.begin(std::format("spectra {} {}", current_scene_format_version, kdl_string(scene.name)));
            writer.line(std::format("active camera={} film={} sampler={}", scene.active_camera.value, scene.active_film.value, scene.active_sampler.value));
            std::string transport{"transport"};
            if (scene.transport.maximum_depth != 5) kdl_number_property(transport, "maximum-depth", scene.transport.maximum_depth);
            if (scene.transport.light_sampler != LightSamplerKind::Bvh) kdl_string_property(transport, "light-sampler", light_sampler_name(scene.transport.light_sampler));
            if (scene.transport.regularize) kdl_bool_property(transport, "regularize", true);
            if (transport != "transport") writer.line(transport);
            write_geometries(writer, scene.resources.geometries);
            write_particle_sets(writer, scene.resources.particle_sets);
            write_volumes(writer, scene.resources.volumes);
            write_textures(writer, scene.resources.textures);
            write_materials(writer, scene.resources.materials);
            write_media(writer, scene.resources.media);
            write_lights(writer, scene.resources.lights);
            write_cameras(writer, scene.resources.cameras);
            write_films(writer, scene.resources.films);
            write_samplers(writer, scene.resources.samplers);
            write_prototypes(writer, scene.resources.prototypes);
            write_instances(writer, scene.resources.instances);
            if (scene.dynamic_setup) write_dynamics(writer, *scene.dynamic_setup);
            writer.end();
            return writer.content;
        }

        [[nodiscard]] std::string kdl_text(const std::u8string_view value) {
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }

        [[nodiscard]] std::string kdl_value_text(const kdl::Value& value) {
            return kdl_text(value.as<std::u8string_view>());
        }

        template <class Value>
        [[nodiscard]] Value kdl_number(const kdl::Value& value) {
            return value.as<Value>();
        }

        [[nodiscard]] const kdl::Value* kdl_property(const kdl::Node& node, const std::u8string_view name) {
            const auto found = node.properties().find(name);
            return found == node.properties().end() ? nullptr : &found->second;
        }

        template <class Value>
        [[nodiscard]] Value kdl_number_property(const kdl::Node& node, const std::u8string_view name, const Value fallback) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? fallback : kdl_number<Value>(*value);
        }

        [[nodiscard]] bool kdl_bool_property(const kdl::Node& node, const std::u8string_view name, const bool fallback) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? fallback : value->as<bool>();
        }

        [[nodiscard]] std::string kdl_string_property(const kdl::Node& node, const std::u8string_view name, const std::string_view fallback = {}) {
            const kdl::Value* value = kdl_property(node, name);
            return value == nullptr ? std::string{fallback} : kdl_value_text(*value);
        }

        [[nodiscard]] const kdl::Node* kdl_child(const kdl::Node& node, const std::u8string_view name) {
            const auto found = std::ranges::find(node.children(), name, &kdl::Node::name);
            return found == node.children().end() ? nullptr : &*found;
        }

        [[nodiscard]] math::Float2 read_float2(const kdl::Node& node, const std::size_t offset = 0) {
            return {kdl_number<float>(node.args()[offset]), kdl_number<float>(node.args()[offset + 1])};
        }

        [[nodiscard]] math::Float3 read_float3(const kdl::Node& node, const std::size_t offset = 0) {
            return {kdl_number<float>(node.args()[offset]), kdl_number<float>(node.args()[offset + 1]), kdl_number<float>(node.args()[offset + 2])};
        }

        [[nodiscard]] math::Transform read_transform(const kdl::Node& parent, const std::u8string_view name = u8"transform") {
            math::Transform transform{};
            const kdl::Node* node = kdl_child(parent, name);
            if (node == nullptr) return transform;
            for (std::uint32_t row = 0; row != 4; ++row)
                for (std::uint32_t column = 0; column != 4; ++column) transform.matrix[row * 4 + column] = kdl_number<float>(node->children()[row].args()[column]);
            return transform;
        }

        [[nodiscard]] math::Bounds3 read_bounds(const kdl::Node& parent, const math::Bounds3 fallback = {}) {
            const kdl::Node* node = kdl_child(parent, u8"bounds");
            if (node == nullptr) return fallback;
            return {read_float3(*node), read_float3(*node, 3)};
        }

        [[nodiscard]] SpectrumEncoding read_spectrum_encoding(const std::string_view value) {
            if (value == "rgb-albedo") return SpectrumEncoding::RgbAlbedo;
            if (value == "rgb-unbounded") return SpectrumEncoding::RgbUnbounded;
            if (value == "rgb-illuminant") return SpectrumEncoding::RgbIlluminant;
            if (value == "constant") return SpectrumEncoding::Constant;
            if (value == "blackbody") return SpectrumEncoding::Blackbody;
            if (value == "piecewise-linear") return SpectrumEncoding::PiecewiseLinear;
            throw std::runtime_error(std::format("Unknown Spectrum encoding {}", value));
        }

        [[nodiscard]] SpectrumColorSpace read_spectrum_color_space(const std::string_view value) {
            if (value == "srgb") return SpectrumColorSpace::Srgb;
            if (value == "rec2020") return SpectrumColorSpace::Rec2020;
            if (value == "aces2065-1") return SpectrumColorSpace::Aces2065_1;
            throw std::runtime_error(std::format("Unknown Spectrum color space {}", value));
        }

        [[nodiscard]] SpectrumParameter read_spectrum(const kdl::Node& node) {
            SpectrumParameter spectrum{};
            spectrum.encoding = read_spectrum_encoding(kdl_value_text(node.args()[0]));
            if (spectrum.encoding == SpectrumEncoding::RgbAlbedo || spectrum.encoding == SpectrumEncoding::RgbUnbounded || spectrum.encoding == SpectrumEncoding::RgbIlluminant) spectrum.value = read_float3(node, 1);
            if (spectrum.encoding == SpectrumEncoding::Constant) spectrum.scalar = kdl_number<float>(node.args()[1]);
            if (spectrum.encoding == SpectrumEncoding::Blackbody) spectrum.temperature = kdl_number<float>(node.args()[1]);
            spectrum.texture.value = kdl_number_property<std::uint64_t>(node, u8"texture", 0);
            spectrum.color_space   = read_spectrum_color_space(kdl_string_property(node, u8"color-space", "srgb"));
            if (spectrum.encoding == SpectrumEncoding::PiecewiseLinear)
                for (const kdl::Node& sample : node.children()) {
                    spectrum.wavelengths.push_back(kdl_number<float>(sample.args()[0]));
                    spectrum.samples.push_back(kdl_number<float>(sample.args()[1]));
                }
            return spectrum;
        }

        [[nodiscard]] FloatParameter read_float_parameter(const kdl::Node& node) {
            return {
                .value   = kdl_number<float>(node.args()[0]),
                .texture = {kdl_number_property<std::uint64_t>(node, u8"texture", 0)},
            };
        }

        [[nodiscard]] MaterialRoughness read_roughness(const kdl::Node& node) {
            MaterialRoughness roughness{};
            if (const kdl::Node* value = kdl_child(node, u8"roughness")) roughness.roughness = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(node, u8"u-roughness")) roughness.u_roughness = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(node, u8"v-roughness")) roughness.v_roughness = read_float_parameter(*value);
            return roughness;
        }

        [[nodiscard]] TextureMapping read_texture_mapping(const kdl::Node& parent) {
            const kdl::Node* node = kdl_child(parent, u8"mapping");
            if (node == nullptr) return {};
            const std::string kind = kdl_value_text(node->args()[0]);
            if (kind == "uv") {
                UvTextureMapping value{};
                if (const kdl::Node* scale = kdl_child(*node, u8"scale")) value.scale = read_float2(*scale);
                if (const kdl::Node* offset = kdl_child(*node, u8"offset")) value.offset = read_float2(*offset);
                return {value};
            }
            if (kind == "planar") {
                PlanarTextureMapping value{};
                if (const kdl::Node* axis = kdl_child(*node, u8"first-axis")) value.first_axis = read_float3(*axis);
                if (const kdl::Node* axis = kdl_child(*node, u8"second-axis")) value.second_axis = read_float3(*axis);
                if (const kdl::Node* offset = kdl_child(*node, u8"offset")) value.offset = read_float2(*offset);
                value.texture_from_render = read_transform(*node);
                return {value};
            }
            if (kind == "spherical") return {SphericalTextureMapping{read_transform(*node)}};
            if (kind == "cylindrical") return {CylindricalTextureMapping{read_transform(*node)}};
            throw std::runtime_error(std::format("Unknown Texture mapping {}", kind));
        }

        [[nodiscard]] CheckerboardMapping read_checkerboard_mapping(const kdl::Node& parent) {
            const kdl::Node* node = kdl_child(parent, u8"mapping");
            if (node == nullptr) return {};
            if (kdl_value_text(node->args()[0]) == "3d") return {TextureMapping3D{read_transform(*node)}};
            return {read_texture_mapping(parent)};
        }

        [[nodiscard]] TextureValueKind read_texture_value_kind(const std::string_view value) {
            if (value == "float") return TextureValueKind::Float;
            if (value == "spectrum") return TextureValueKind::Spectrum;
            throw std::runtime_error(std::format("Unknown Texture value kind {}", value));
        }

        [[nodiscard]] TextureSpectrumType read_texture_spectrum_type(const std::string_view value) {
            if (value == "albedo") return TextureSpectrumType::Albedo;
            if (value == "unbounded") return TextureSpectrumType::Unbounded;
            if (value == "illuminant") return TextureSpectrumType::Illuminant;
            throw std::runtime_error(std::format("Unknown Texture Spectrum type {}", value));
        }

        [[nodiscard]] TextureColorSpace read_texture_color_space(const std::string_view value) {
            if (value == "linear") return TextureColorSpace::Linear;
            if (value == "srgb") return TextureColorSpace::Srgb;
            if (value == "aces2065-1") return TextureColorSpace::Aces2065_1;
            if (value == "rec2020") return TextureColorSpace::Rec2020;
            throw std::runtime_error(std::format("Unknown Texture color space {}", value));
        }

        [[nodiscard]] TextureWrapMode read_texture_wrap(const std::string_view value) {
            if (value == "repeat") return TextureWrapMode::Repeat;
            if (value == "clamp") return TextureWrapMode::Clamp;
            if (value == "black") return TextureWrapMode::Black;
            throw std::runtime_error(std::format("Unknown Texture wrap {}", value));
        }

        [[nodiscard]] TextureChannel read_texture_channel(const std::string_view value) {
            if (value == "red") return TextureChannel::Red;
            if (value == "green") return TextureChannel::Green;
            if (value == "blue") return TextureChannel::Blue;
            if (value == "alpha") return TextureChannel::Alpha;
            if (value == "average") return TextureChannel::Average;
            if (value == "luminance") return TextureChannel::Luminance;
            throw std::runtime_error(std::format("Unknown Texture channel {}", value));
        }

        [[nodiscard]] TextureFilter read_texture_filter(const std::string_view value) {
            if (value == "point") return TextureFilter::Point;
            if (value == "bilinear") return TextureFilter::Bilinear;
            if (value == "trilinear") return TextureFilter::Trilinear;
            if (value == "ewa") return TextureFilter::Ewa;
            throw std::runtime_error(std::format("Unknown Texture filter {}", value));
        }

        void read_texture_common(Texture& texture, const kdl::Node& node) {
            texture.id.value      = kdl_number<std::uint64_t>(node.args()[0]);
            texture.name          = kdl_value_text(node.args()[1]);
            texture.value_kind    = read_texture_value_kind(kdl_string_property(node, u8"value", "spectrum"));
            texture.spectrum_type = read_texture_spectrum_type(kdl_string_property(node, u8"spectrum-type", "albedo"));
            texture.color_space   = read_texture_color_space(kdl_string_property(node, u8"color-space", "srgb"));
        }

        void read_geometries(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Geometry geometry{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"triangle-mesh") geometry.data = TriangleMeshGeometry{.asset = {.content_hash = kdl_string_property(node, u8"asset")}};
                else if (node.name() == u8"sphere")
                    geometry.data = SphereGeometry{
                        .radius  = kdl_number_property<float>(node, u8"radius", 1.0f),
                        .z_min   = kdl_number_property<float>(node, u8"z-min", -1.0f),
                        .z_max   = kdl_number_property<float>(node, u8"z-max", 1.0f),
                        .phi_max = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                else if (node.name() == u8"box") geometry.data = BoxGeometry{read_bounds(node, BoxGeometry{}.bounds)};
                else if (node.name() == u8"rectangle") {
                    RectangleGeometry rectangle{};
                    if (const kdl::Node* minimum = kdl_child(node, u8"minimum")) rectangle.minimum = read_float2(*minimum);
                    if (const kdl::Node* maximum = kdl_child(node, u8"maximum")) rectangle.maximum = read_float2(*maximum);
                    geometry.data = rectangle;
                } else if (node.name() == u8"disk")
                    geometry.data = DiskGeometry{
                        .height       = kdl_number_property<float>(node, u8"height", 0.0f),
                        .radius       = kdl_number_property<float>(node, u8"radius", 1.0f),
                        .inner_radius = kdl_number_property<float>(node, u8"inner-radius", 0.0f),
                        .phi_max      = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                else if (node.name() == u8"cylinder")
                    geometry.data = CylinderGeometry{
                        .radius  = kdl_number_property<float>(node, u8"radius", 1.0f),
                        .z_min   = kdl_number_property<float>(node, u8"z-min", -1.0f),
                        .z_max   = kdl_number_property<float>(node, u8"z-max", 1.0f),
                        .phi_max = kdl_number_property<float>(node, u8"phi-max", 360.0f),
                    };
                else throw std::runtime_error(std::format("Unknown Geometry {}", kdl_text(node.name())));
                resources.geometries.push_back(std::move(geometry));
            }
        }

        void read_particle_sets(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.particle_sets.push_back({
                    .id       = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name     = kdl_value_text(node.args()[1]),
                    .asset    = {.content_hash = kdl_string_property(node, u8"asset")},
                    .material = {kdl_number_property<std::uint64_t>(node, u8"material", 0)},
                });
        }

        void read_volumes(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Volume volume{
                    .id        = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name      = kdl_value_text(node.args()[1]),
                    .bounds    = read_bounds(node),
                    .transform = read_transform(node),
                };
                if (node.name() == u8"density-grid") {
                    const kdl::Node& resolution = *kdl_child(node, u8"resolution");
                    volume.data = DensityGridVolume{
                        .resolution = {kdl_number<std::uint32_t>(resolution.args()[0]), kdl_number<std::uint32_t>(resolution.args()[1]), kdl_number<std::uint32_t>(resolution.args()[2])},
                        .asset      = {.content_hash = kdl_string_property(node, u8"asset")},
                    };
                } else if (node.name() == u8"rgb-grid") {
                    const kdl::Node& resolution = *kdl_child(node, u8"resolution");
                    RgbGridVolume data{
                        .resolution = {kdl_number<std::uint32_t>(resolution.args()[0]), kdl_number<std::uint32_t>(resolution.args()[1]), kdl_number<std::uint32_t>(resolution.args()[2])},
                        .color_space = SpectrumColorSpace::Srgb,
                        .asset       = {.content_hash = kdl_string_property(node, u8"asset")},
                    };
                    if (const kdl::Node* color_space = kdl_child(node, u8"color-space")) data.color_space = read_spectrum_color_space(kdl_value_text(color_space->args()[0]));
                    volume.data = data;
                } else if (node.name() == u8"nanovdb") {
                    NanoVdbVolume data{.asset = {.content_hash = kdl_string_property(node, u8"asset")}};
                    if (const kdl::Node* density = kdl_child(node, u8"density-grid")) data.density_grid = kdl_value_text(density->args()[0]);
                    if (const kdl::Node* temperature = kdl_child(node, u8"temperature-grid")) data.temperature_grid = kdl_value_text(temperature->args()[0]);
                    volume.data = std::move(data);
                } else if (node.name() == u8"procedural-cloud") {
                    ProceduralCloudVolume data{};
                    if (const kdl::Node* value = kdl_child(node, u8"density")) data.density = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"wispiness")) data.wispiness = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"frequency")) data.frequency = kdl_number<float>(value->args()[0]);
                    volume.data = data;
                } else throw std::runtime_error(std::format("Unknown Volume {}", kdl_text(node.name())));
                resources.volumes.push_back(std::move(volume));
            }
        }

        void read_textures(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Texture texture{};
                read_texture_common(texture, node);
                if (node.name() == u8"constant") {
                    ConstantTexture data{};
                    if (texture.value_kind == TextureValueKind::Float) data.scalar = kdl_number<float>(kdl_child(node, u8"scalar")->args()[0]);
                    else data.spectrum = read_spectrum(*kdl_child(node, u8"spectrum"));
                    texture.data = std::move(data);
                } else if (node.name() == u8"image") {
                    texture.data = ImageTexture{
                        .asset              = {.content_hash = kdl_string_property(node, u8"asset")},
                        .mapping            = read_texture_mapping(node),
                        .wrap               = read_texture_wrap(kdl_string_property(node, u8"wrap", "repeat")),
                        .channel            = read_texture_channel(kdl_string_property(node, u8"channel", "luminance")),
                        .filter             = read_texture_filter(kdl_string_property(node, u8"filter", "bilinear")),
                        .maximum_anisotropy = kdl_number_property<float>(node, u8"maximum-anisotropy", 8.0f),
                        .scale              = kdl_number_property<float>(node, u8"scale", 1.0f),
                        .invert             = kdl_bool_property(node, u8"invert", false),
                    };
                } else if (node.name() == u8"checkerboard") {
                    texture.data = CheckerboardTexture{
                        .first   = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second  = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                        .mapping = read_checkerboard_mapping(node),
                    };
                } else if (node.name() == u8"scale") {
                    texture.data = ScaleTexture{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                } else if (node.name() == u8"mix") {
                    texture.data = MixTexture{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                        .amount = {kdl_number_property<std::uint64_t>(node, u8"amount", 0)},
                    };
                } else if (node.name() == u8"direction-mix") {
                    DirectionMixTexture data{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                    if (const kdl::Node* direction = kdl_child(node, u8"direction")) data.direction = read_float3(*direction);
                    texture.data = data;
                } else if (node.name() == u8"bilerp") {
                    BilerpTexture data{.mapping = read_texture_mapping(node)};
                    if (texture.value_kind == TextureValueKind::Float)
                        for (const kdl::Node& corner : node.children())
                            if (corner.name() == u8"corner") data.scalars[kdl_number<std::uint32_t>(corner.args()[0])] = kdl_number<float>(corner.args()[1]);
                    if (texture.value_kind == TextureValueKind::Spectrum) {
                        constexpr std::array<std::u8string_view, 4> names{u8"corner-0", u8"corner-1", u8"corner-2", u8"corner-3"};
                        for (std::uint32_t corner = 0; corner != 4; ++corner) data.spectra[corner] = read_spectrum(*kdl_child(node, names[corner]));
                    }
                    texture.data = std::move(data);
                } else throw std::runtime_error(std::format("Unknown Texture {}", kdl_text(node.name())));
                resources.textures.push_back(std::move(texture));
            }
        }

        [[nodiscard]] std::variant<ConductorEtaK, ConductorReflectance> read_conductor_optics(const kdl::Node& node) {
            if (const kdl::Node* eta_k = kdl_child(node, u8"eta-k"))
                return ConductorEtaK{
                    .eta = read_spectrum(*kdl_child(*eta_k, u8"eta")),
                    .k   = read_spectrum(*kdl_child(*eta_k, u8"k")),
                };
            const kdl::Node& reflectance = *kdl_child(node, u8"reflectance-optics");
            return ConductorReflectance{read_spectrum(*kdl_child(reflectance, u8"reflectance"))};
        }

        [[nodiscard]] CoatingLayer read_coating(const kdl::Node& node) {
            CoatingLayer coating{};
            const kdl::Node& source = *kdl_child(node, u8"coating");
            if (const kdl::Node* value = kdl_child(source, u8"thickness")) coating.thickness = read_float_parameter(*value);
            coating.albedo = read_spectrum(*kdl_child(source, u8"albedo"));
            if (const kdl::Node* value = kdl_child(source, u8"g")) coating.g = read_float_parameter(*value);
            if (const kdl::Node* value = kdl_child(source, u8"maximum-depth")) coating.max_depth = kdl_number<std::int32_t>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(source, u8"samples")) coating.sample_count = kdl_number<std::int32_t>(value->args()[0]);
            return coating;
        }

        void read_normal_and_bump(const kdl::Node& node, TextureId& normal_map, TextureId& bump_map) {
            if (const kdl::Node* value = kdl_child(node, u8"normal-map")) normal_map.value = kdl_number<std::uint64_t>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(node, u8"bump-map")) bump_map.value = kdl_number<std::uint64_t>(value->args()[0]);
        }

        void read_materials(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Material material{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"interface") material.data = InterfaceMaterialData{};
                else if (node.name() == u8"diffuse") {
                    DiffuseMaterialData data{};
                    data.reflectance = read_spectrum(*kdl_child(node, u8"reflectance"));
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"diffuse-transmission") {
                    DiffuseTransmissionMaterialData data{};
                    data.reflectance   = read_spectrum(*kdl_child(node, u8"reflectance"));
                    data.transmittance = read_spectrum(*kdl_child(node, u8"transmittance"));
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"conductor") {
                    ConductorMaterialData data{};
                    data.optics          = read_conductor_optics(node);
                    data.distribution    = read_roughness(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"dielectric") {
                    DielectricMaterialData data{};
                    data.eta              = read_spectrum(*kdl_child(node, u8"eta"));
                    data.distribution     = read_roughness(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"thin-dielectric") {
                    ThinDielectricMaterialData data{};
                    data.eta = read_spectrum(*kdl_child(node, u8"eta"));
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"coated-diffuse") {
                    CoatedDiffuseMaterialData data{};
                    data.reflectance = read_spectrum(*kdl_child(node, u8"reflectance"));
                    data.eta         = read_spectrum(*kdl_child(node, u8"eta"));
                    data.interface   = read_roughness(*kdl_child(node, u8"interface"));
                    data.coating     = read_coating(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"coated-conductor") {
                    CoatedConductorMaterialData data{};
                    const kdl::Node& interface = *kdl_child(node, u8"interface");
                    const kdl::Node& conductor = *kdl_child(node, u8"conductor");
                    data.interface_eta         = read_spectrum(*kdl_child(interface, u8"eta"));
                    data.interface             = read_roughness(interface);
                    data.optics                = read_conductor_optics(conductor);
                    data.conductor             = read_roughness(conductor);
                    data.coating               = read_coating(node);
                    if (const kdl::Node* value = kdl_child(node, u8"remap-roughness")) data.remap_roughness = value->args()[0].as<bool>();
                    read_normal_and_bump(node, data.normal_map, data.bump_map);
                    material.data = std::move(data);
                } else if (node.name() == u8"mix") {
                    MixMaterialData data{
                        .first  = {kdl_number_property<std::uint64_t>(node, u8"first", 0)},
                        .second = {kdl_number_property<std::uint64_t>(node, u8"second", 0)},
                    };
                    if (const kdl::Node* value = kdl_child(node, u8"amount")) data.amount = read_float_parameter(*value);
                    material.data = data;
                } else throw std::runtime_error(std::format("Unknown Material {}", kdl_text(node.name())));
                resources.materials.push_back(std::move(material));
            }
        }

        void read_media(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Medium medium{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"homogeneous") {
                    HomogeneousMedium data{};
                    data.sigma_a  = read_spectrum(*kdl_child(node, u8"sigma-a"));
                    data.sigma_s  = read_spectrum(*kdl_child(node, u8"sigma-s"));
                    data.emission = read_spectrum(*kdl_child(node, u8"emission"));
                    if (const kdl::Node* value = kdl_child(node, u8"density-scale")) data.density_scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"emission-scale")) data.emission_scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"anisotropy")) data.anisotropy = kdl_number<float>(value->args()[0]);
                    medium.data = std::move(data);
                } else if (node.name() == u8"volume") {
                    VolumeMedium data{.volume = {kdl_number_property<std::uint64_t>(node, u8"volume", 0)}};
                    data.sigma_a  = read_spectrum(*kdl_child(node, u8"sigma-a"));
                    data.sigma_s  = read_spectrum(*kdl_child(node, u8"sigma-s"));
                    data.emission = read_spectrum(*kdl_child(node, u8"emission"));
                    if (const kdl::Node* value = kdl_child(node, u8"density-scale")) data.density_scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"emission-scale")) data.emission_scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"anisotropy")) data.anisotropy = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"temperature-scale")) data.temperature_scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"temperature-offset")) data.temperature_offset = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"minimum-emission-temperature")) data.minimum_emission_temperature = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"blackbody-emission")) data.blackbody_emission = value->args()[0].as<bool>();
                    medium.data = std::move(data);
                } else throw std::runtime_error(std::format("Unknown Medium {}", kdl_text(node.name())));
                resources.media.push_back(std::move(medium));
            }
        }

        [[nodiscard]] InfiniteLight read_infinite_light(const kdl::Node& node) {
            InfiniteLight light{};
            light.radiance = read_spectrum(*kdl_child(node, u8"radiance"));
            light.transform = read_transform(node);
            if (const kdl::Node* value = kdl_child(node, u8"scale")) light.scale = kdl_number<float>(value->args()[0]);
            if (const kdl::Node* value = kdl_child(node, u8"emission-texture")) light.emission_texture.value = kdl_number<std::uint64_t>(value->args()[0]);
            return light;
        }

        void read_lights(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Light light{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                if (node.name() == u8"point") {
                    PointLight data{};
                    data.intensity = read_spectrum(*kdl_child(node, u8"intensity"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"spot") {
                    SpotLight data{};
                    data.intensity = read_spectrum(*kdl_child(node, u8"intensity"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"cone-angle")) data.cone_angle = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"cone-delta")) data.cone_delta = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"distant") {
                    DistantLight data{};
                    data.radiance  = read_spectrum(*kdl_child(node, u8"radiance"));
                    data.transform = read_transform(node);
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"diffuse-area") {
                    DiffuseAreaLight data{};
                    data.radiance = read_spectrum(*kdl_child(node, u8"radiance"));
                    if (const kdl::Node* value = kdl_child(node, u8"sidedness")) data.sidedness = kdl_value_text(value->args()[0]) == "both" ? EmissionSidedness::Both : EmissionSidedness::Front;
                    if (const kdl::Node* value = kdl_child(node, u8"scale")) data.scale = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"power")) data.power = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"emission-texture")) data.emission_texture.value = kdl_number<std::uint64_t>(value->args()[0]);
                    light.data = std::move(data);
                } else if (node.name() == u8"infinite") light.data = read_infinite_light(node);
                else if (node.name() == u8"portal-infinite") {
                    PortalInfiniteLight data{.environment = read_infinite_light(*kdl_child(node, u8"environment"))};
                    for (const kdl::Node& portal : node.children())
                        if (portal.name() == u8"portal") {
                            std::array<math::Float3, 4> corners{};
                            for (std::uint32_t corner = 0; corner != 4; ++corner) corners[corner] = read_float3(portal.children()[corner]);
                            data.portals.push_back(corners);
                        }
                    light.data = std::move(data);
                } else throw std::runtime_error(std::format("Unknown Light {}", kdl_text(node.name())));
                resources.lights.push_back(std::move(light));
            }
        }

        void read_cameras(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Camera camera{
                    .id            = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name          = kdl_value_text(node.args()[1]),
                    .transform     = read_transform(node),
                    .exposure_time = kdl_number_property<float>(node, u8"exposure-time", 1.0f),
                    .medium        = {kdl_number_property<std::uint64_t>(node, u8"medium", 0)},
                };
                ScreenWindow screen{};
                if (const kdl::Node* value = kdl_child(node, u8"screen")) {
                    screen.minimum = read_float2(*value);
                    screen.maximum = read_float2(*value, 2);
                }
                if (node.name() == u8"perspective") {
                    PerspectiveCameraData data{.screen_window = screen};
                    if (const kdl::Node* value = kdl_child(node, u8"vertical-fov")) data.vertical_fov = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"lens-radius")) data.lens_radius = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"focal-distance")) data.focal_distance = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"near-plane")) data.near_plane = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"far-plane")) data.far_plane = kdl_number<float>(value->args()[0]);
                    camera.data = data;
                } else if (node.name() == u8"orthographic") {
                    OrthographicCameraData data{.screen_window = screen};
                    if (const kdl::Node* value = kdl_child(node, u8"lens-radius")) data.lens_radius = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"focal-distance")) data.focal_distance = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"near-plane")) data.near_plane = kdl_number<float>(value->args()[0]);
                    if (const kdl::Node* value = kdl_child(node, u8"far-plane")) data.far_plane = kdl_number<float>(value->args()[0]);
                    camera.data = data;
                } else throw std::runtime_error(std::format("Unknown Camera {}", kdl_text(node.name())));
                resources.cameras.push_back(std::move(camera));
            }
        }

        [[nodiscard]] FilterKind read_filter_kind(const std::string_view value) {
            if (value == "box") return FilterKind::Box;
            if (value == "gaussian") return FilterKind::Gaussian;
            if (value == "mitchell") return FilterKind::Mitchell;
            if (value == "sinc") return FilterKind::Sinc;
            if (value == "triangle") return FilterKind::Triangle;
            throw std::runtime_error(std::format("Unknown Film filter {}", value));
        }

        void read_films(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Film film{
                    .id           = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name         = kdl_value_text(node.args()[1]),
                    .exposure     = kdl_number_property<float>(node, u8"exposure", 0.0f),
                    .iso          = kdl_number_property<float>(node, u8"iso", 100.0f),
                    .color_space  = read_spectrum_color_space(kdl_string_property(node, u8"color-space", "srgb")),
                    .gbuffer      = kdl_bool_property(node, u8"gbuffer", false),
                    .gbuffer_camera_space = kdl_bool_property(node, u8"gbuffer-camera-space", true),
                };
                if (const kdl::Value* value = kdl_property(node, u8"maximum-component")) film.maximum_component_value = kdl_number<float>(*value);
                const kdl::Node& resolution = *kdl_child(node, u8"resolution");
                film.resolution   = {kdl_number<std::uint32_t>(resolution.args()[0]), kdl_number<std::uint32_t>(resolution.args()[1])};
                film.pixel_maximum = film.resolution;
                if (const kdl::Node* range = kdl_child(node, u8"pixel-range")) {
                    film.pixel_minimum = {kdl_number<std::uint32_t>(range->args()[0]), kdl_number<std::uint32_t>(range->args()[1])};
                    film.pixel_maximum = {kdl_number<std::uint32_t>(range->args()[2]), kdl_number<std::uint32_t>(range->args()[3])};
                }
                if (const kdl::Node* response = kdl_child(node, u8"sensor-response"))
                    for (const kdl::Node& values : response->children())
                        for (const kdl::Value& value : values.args()) film.sensor_response.push_back(kdl_number<float>(value));
                if (const kdl::Node* matrix = kdl_child(node, u8"sensor-to-output-rgb"))
                    for (std::uint32_t row = 0; row != 3; ++row)
                        for (std::uint32_t column = 0; column != 3; ++column) film.sensor_to_output_rgb[row * 3 + column] = kdl_number<float>(matrix->children()[row].args()[column]);
                if (const kdl::Node* filter = kdl_child(node, u8"filter")) {
                    film.filter.kind     = read_filter_kind(kdl_value_text(filter->args()[0]));
                    film.filter.radius.x = kdl_number_property<float>(*filter, u8"radius-x", 0.5f);
                    film.filter.radius.y = kdl_number_property<float>(*filter, u8"radius-y", 0.5f);
                    film.filter.sigma    = kdl_number_property<float>(*filter, u8"sigma", 0.5f);
                    film.filter.b        = kdl_number_property<float>(*filter, u8"b", 1.0f / 3.0f);
                    film.filter.c        = kdl_number_property<float>(*filter, u8"c", 1.0f / 3.0f);
                    film.filter.tau      = kdl_number_property<float>(*filter, u8"tau", 3.0f);
                }
                resources.films.push_back(std::move(film));
            }
        }

        [[nodiscard]] SamplerKind read_sampler_kind(const std::string_view value) {
            if (value == "independent") return SamplerKind::Independent;
            if (value == "stratified") return SamplerKind::Stratified;
            if (value == "halton") return SamplerKind::Halton;
            if (value == "sobol") return SamplerKind::Sobol;
            if (value == "padded-sobol") return SamplerKind::PaddedSobol;
            if (value == "zsobol") return SamplerKind::ZSobol;
            if (value == "pmj02bn") return SamplerKind::Pmj02bn;
            throw std::runtime_error(std::format("Unknown Sampler kind {}", value));
        }

        [[nodiscard]] SamplerRandomization read_sampler_randomization(const std::string_view value) {
            if (value == "none") return SamplerRandomization::None;
            if (value == "permute-digits") return SamplerRandomization::PermuteDigits;
            if (value == "fast-owen") return SamplerRandomization::FastOwen;
            if (value == "owen") return SamplerRandomization::Owen;
            throw std::runtime_error(std::format("Unknown Sampler randomization {}", value));
        }

        void read_samplers(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.samplers.push_back({
                    .id                = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name              = kdl_value_text(node.args()[1]),
                    .kind              = read_sampler_kind(kdl_string_property(node, u8"kind", "independent")),
                    .samples_per_pixel = kdl_number_property<std::uint32_t>(node, u8"samples", 1),
                    .seed              = kdl_number_property<std::uint32_t>(node, u8"seed", 0),
                    .jitter            = kdl_bool_property(node, u8"jitter", true),
                    .x_strata          = kdl_number_property<std::uint32_t>(node, u8"x-strata", 1),
                    .y_strata          = kdl_number_property<std::uint32_t>(node, u8"y-strata", 1),
                    .randomization     = read_sampler_randomization(kdl_string_property(node, u8"randomization", "owen")),
                });
        }

        void read_prototypes(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children()) {
                Prototype prototype{
                    .id   = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name = kdl_value_text(node.args()[1]),
                };
                for (const kdl::Node& primitive_node : node.children()) {
                    Primitive primitive{
                        .geometry            = {kdl_number_property<std::uint64_t>(primitive_node, u8"geometry", 0)},
                        .particles           = {kdl_number_property<std::uint64_t>(primitive_node, u8"particles", 0)},
                        .volume              = {kdl_number_property<std::uint64_t>(primitive_node, u8"volume", 0)},
                        .material            = {kdl_number_property<std::uint64_t>(primitive_node, u8"material", 0)},
                        .area_light          = {kdl_number_property<std::uint64_t>(primitive_node, u8"area-light", 0)},
                        .alpha               = {kdl_number_property<std::uint64_t>(primitive_node, u8"alpha", 0)},
                        .reverse_orientation = kdl_bool_property(primitive_node, u8"reverse-orientation", false),
                        .transform           = read_transform(primitive_node),
                    };
                    if (const kdl::Node* media = kdl_child(primitive_node, u8"media")) {
                        primitive.media.inside.value  = kdl_number_property<std::uint64_t>(*media, u8"inside", 0);
                        primitive.media.outside.value = kdl_number_property<std::uint64_t>(*media, u8"outside", 0);
                    }
                    if (const kdl::Node* materials = kdl_child(primitive_node, u8"face-materials"))
                        for (const kdl::Value& value : materials->args()) primitive.face_materials.push_back({kdl_number<std::uint64_t>(value)});
                    prototype.primitives.push_back(std::move(primitive));
                }
                resources.prototypes.push_back(std::move(prototype));
            }
        }

        void read_instances(SceneResources& resources, const kdl::Node& group) {
            for (const kdl::Node& node : group.children())
                resources.instances.push_back({
                    .id        = {kdl_number<std::uint64_t>(node.args()[0])},
                    .name      = kdl_value_text(node.args()[1]),
                    .prototype = {kdl_number_property<std::uint64_t>(node, u8"prototype", 0)},
                    .transform = read_transform(node),
                    .visible   = kdl_bool_property(node, u8"visible", true),
                });
        }

        [[nodiscard]] DynamicParameterKind read_dynamic_parameter_kind(const std::string_view value) {
            if (value == "boolean") return DynamicParameterKind::Boolean;
            if (value == "integer") return DynamicParameterKind::Integer;
            if (value == "float") return DynamicParameterKind::Float;
            if (value == "float3") return DynamicParameterKind::Float3;
            if (value == "enumeration") return DynamicParameterKind::Enumeration;
            throw std::runtime_error(std::format("Unknown Dynamic parameter kind {}", value));
        }

        [[nodiscard]] DynamicResourceKind read_dynamic_resource_kind(const std::string_view value) {
            if (value == "instance") return DynamicResourceKind::Instance;
            if (value == "geometry") return DynamicResourceKind::Geometry;
            if (value == "particle-set") return DynamicResourceKind::ParticleSet;
            if (value == "volume") return DynamicResourceKind::Volume;
            throw std::runtime_error(std::format("Unknown Dynamic resource kind {}", value));
        }

        [[nodiscard]] DynamicSetup read_dynamics(const kdl::Node& node) {
            DynamicSetup setup{.seed = kdl_number_property<std::uint64_t>(node, u8"seed", 0)};
            if (const kdl::Node* clock = kdl_child(node, u8"clock")) {
                setup.clock.step_seconds = kdl_number_property<double>(*clock, u8"step-seconds", 1.0 / 120.0);
                setup.clock.start_step   = kdl_number_property<std::uint64_t>(*clock, u8"start-step", 0);
                if (const kdl::Value* value = kdl_property(*clock, u8"end-step")) setup.clock.end_step = kdl_number<std::uint64_t>(*value);
                setup.clock.loop = kdl_bool_property(*clock, u8"loop", false);
            }
            for (const kdl::Node& system_node : node.children()) {
                if (system_node.name() != u8"system") continue;
                DynamicSystem system{
                    .id          = {kdl_value_text(system_node.args()[0])},
                    .name        = kdl_value_text(system_node.args()[1]),
                    .provider_id = kdl_string_property(system_node, u8"provider"),
                    .enabled     = kdl_bool_property(system_node, u8"enabled", true),
                    .visible     = kdl_bool_property(system_node, u8"visible", true),
                };
                for (const kdl::Node& child : system_node.children()) {
                    if (child.name() == u8"parameter") {
                        DynamicParameterSetting parameter{
                            .parameter_id = kdl_value_text(child.args()[0]),
                        };
                        parameter.value.kind = read_dynamic_parameter_kind(kdl_value_text(child.args()[1]));
                        if (parameter.value.kind == DynamicParameterKind::Boolean) parameter.value.integer = child.args()[2].as<bool>() ? 1 : 0;
                        else if (parameter.value.kind == DynamicParameterKind::Integer || parameter.value.kind == DynamicParameterKind::Enumeration) parameter.value.integer = kdl_number<std::int64_t>(child.args()[2]);
                        else if (parameter.value.kind == DynamicParameterKind::Float) parameter.value.floating[0] = kdl_number<double>(child.args()[2]);
                        else
                            for (std::uint32_t component = 0; component != 3; ++component) parameter.value.floating[component] = kdl_number<double>(child.args()[component + 2]);
                        system.parameters.push_back(std::move(parameter));
                    } else if (child.name() == u8"bind")
                        system.bindings.push_back({
                            .port_id       = kdl_value_text(child.args()[0]),
                            .resource_kind = read_dynamic_resource_kind(kdl_value_text(child.args()[1])),
                            .resource_id   = kdl_number<std::uint64_t>(child.args()[2]),
                        });
                }
                setup.systems.push_back(std::move(system));
            }
            return setup;
        }

        [[nodiscard]] LightSamplerKind read_light_sampler(const std::string_view value) {
            if (value == "uniform") return LightSamplerKind::Uniform;
            if (value == "power") return LightSamplerKind::Power;
            if (value == "bvh") return LightSamplerKind::Bvh;
            throw std::runtime_error(std::format("Unknown Light sampler {}", value));
        }

        [[nodiscard]] Scene parse_scene(const std::filesystem::path& path) {
            std::ifstream stream{path, std::ios::binary};
            if (!stream) throw std::runtime_error(std::format("Failed to open Spectra scene: {}", path.string()));
            const std::string text{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
            const kdl::Document document = kdl::parse({reinterpret_cast<const char8_t*>(text.data()), text.size()}, kdl::KdlVersion::Kdl_2);
            const kdl::Node& root         = document.nodes()[0];
            Scene scene{
                .format_version = kdl_number<std::uint32_t>(root.args()[0]),
                .name           = kdl_value_text(root.args()[1]),
            };
            if (scene.format_version != current_scene_format_version) throw std::runtime_error(std::format("Unsupported Spectra scene format {}", scene.format_version));
            for (const kdl::Node& node : root.children()) {
                if (node.name() == u8"active") {
                    scene.active_camera.value  = kdl_number_property<std::uint64_t>(node, u8"camera", 0);
                    scene.active_film.value    = kdl_number_property<std::uint64_t>(node, u8"film", 0);
                    scene.active_sampler.value = kdl_number_property<std::uint64_t>(node, u8"sampler", 0);
                } else if (node.name() == u8"transport") {
                    scene.transport.maximum_depth = kdl_number_property<std::uint32_t>(node, u8"maximum-depth", 5);
                    scene.transport.light_sampler = read_light_sampler(kdl_string_property(node, u8"light-sampler", "bvh"));
                    scene.transport.regularize    = kdl_bool_property(node, u8"regularize", false);
                } else if (node.name() == u8"geometries") read_geometries(scene.resources, node);
                else if (node.name() == u8"particle-sets") read_particle_sets(scene.resources, node);
                else if (node.name() == u8"volumes") read_volumes(scene.resources, node);
                else if (node.name() == u8"textures") read_textures(scene.resources, node);
                else if (node.name() == u8"materials") read_materials(scene.resources, node);
                else if (node.name() == u8"media") read_media(scene.resources, node);
                else if (node.name() == u8"lights") read_lights(scene.resources, node);
                else if (node.name() == u8"cameras") read_cameras(scene.resources, node);
                else if (node.name() == u8"films") read_films(scene.resources, node);
                else if (node.name() == u8"samplers") read_samplers(scene.resources, node);
                else if (node.name() == u8"prototypes") read_prototypes(scene.resources, node);
                else if (node.name() == u8"instances") read_instances(scene.resources, node);
                else if (node.name() == u8"dynamics") scene.dynamic_setup = read_dynamics(node);
                else throw std::runtime_error(std::format("Unknown Spectra scene section {}", kdl_text(node.name())));
            }
            return scene;
        }
    } // namespace

    Scene load_scene(const std::filesystem::path& path) {
        Scene scene                              = parse_scene(path);
        const std::filesystem::path package_root = path.parent_path();
        for (Geometry& geometry : scene.resources.geometries)
            if (TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) load_geometry_asset(*mesh, package_root);
        for (ParticleSet& particles : scene.resources.particle_sets) load_particle_asset(particles, package_root);
        for (Volume& volume : scene.resources.volumes) {
            if (DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data))
                load_volume_asset(*density, package_root);
            else if (RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data))
                load_volume_asset(*rgb, package_root);
            else if (NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                load_volume_asset(*nanovdb, package_root);
        }
        for (Texture& texture : scene.resources.textures)
            if (ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) load_texture_asset(*image, package_root);
        scene.mark_all_changed();
        scene.acknowledge_changes();
        return scene;
    }

    void save_scene(Scene package, const std::filesystem::path& path, const std::filesystem::path& source_scene_path) {
        package.format_version                   = current_scene_format_version;
        const std::filesystem::path package_root = path.parent_path();
        const std::filesystem::path source_root  = source_scene_path.empty() ? package_root : source_scene_path.parent_path();
        for (Geometry& geometry : package.resources.geometries)
            if (TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) mesh->asset = write_geometry_asset(*mesh, package_root);
        for (ParticleSet& particles : package.resources.particle_sets) particles.asset = write_particle_asset(particles, package_root);
        for (Volume& volume : package.resources.volumes) {
            if (DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data))
                density->asset = write_volume_asset(*density, package_root);
            else if (RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data))
                rgb->asset = write_volume_asset(*rgb, package_root);
            else if (NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                if (!nanovdb->density_data.empty())
                    nanovdb->asset = write_volume_asset(*nanovdb, package_root);
                else
                    copy_asset(nanovdb->asset, ".volume", source_root, package_root);
        }
        for (Texture& texture : package.resources.textures)
            if (ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) {
                if (!image->texels.empty())
                    image->asset = write_texture_asset(*image, package_root);
                else if (std::filesystem::exists(asset_path(package_root, image->asset, ".texture")))
                    verify_asset(package_root, image->asset, ".texture");
                else
                    copy_asset(image->asset, ".texture", source_root, package_root);
            }
        const std::string document = serialize_scene_kdl(package);
        std::filesystem::path temporary_path = path;
        temporary_path += ".tmp";
        std::ofstream stream{
            temporary_path,
            std::ios::binary | std::ios::trunc,
        };
        if (!stream) throw std::runtime_error(std::format("Failed to create Spectra scene: {}", temporary_path.string()));
        stream.write(document.data(), static_cast<std::streamsize>(document.size()));
        if (!stream) throw std::runtime_error(std::format("Failed to write Spectra scene: {}", temporary_path.string()));
        stream.close();
        if (std::filesystem::exists(path)) std::filesystem::remove(path);
        std::filesystem::rename(temporary_path, path);
    }
} // namespace spectra::scene
