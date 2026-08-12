module spectra.scene.package;

import spectra.scene.content_hash;
import spectra.scene.asset_import;
import std;

namespace spectra::scene {
    void AssetTransaction::record(const std::filesystem::path& path) {
        this->created_paths.push_back(path);
    }
    void AssetTransaction::commit() noexcept {
        this->created_paths.clear();
    }

    void AssetTransaction::rollback() {
        std::vector<std::string> failures{};
        for (const std::filesystem::path& path : this->created_paths | std::views::reverse) {
            std::error_code error{};
            std::filesystem::remove(path, error);
            if (error) failures.emplace_back(std::format("{}: {}", path.string(), error.message()));
        }
        this->created_paths.clear();
        if (!failures.empty()) throw std::runtime_error(std::format("Failed to roll back Spectra package assets: {}", std::ranges::fold_left_first(failures, [](std::string left, const std::string& right) { return std::move(left) + "; " + right; }).value()));
    }

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

        struct SphereSetAssetHeader {
            std::array<char, 8> magic{'S', 'P', 'S', 'P', 'H', '0', '0', '1'};
            std::uint32_t version{1};
            std::uint32_t reserved{};
            std::uint64_t position_count{};
            std::uint64_t radius_count{};
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

        struct AssetWriter {
            std::vector<std::byte> bytes{};

            void magic(const std::array<char, 8>& value) {
                for (const char byte : value) this->bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
            }

            void uint32(const std::uint32_t value) {
                for (std::uint32_t shift = 0; shift != 32; shift += 8) this->bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }

            void uint64(const std::uint64_t value) {
                for (std::uint32_t shift = 0; shift != 64; shift += 8) this->bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }

            void float32(const float value) {
                this->uint32(std::bit_cast<std::uint32_t>(value));
            }

            template <typename Value>
            void values(const std::span<const Value> source) {
                for (const Value& value : source) {
                    if constexpr (std::same_as<Value, std::uint32_t>)
                        this->uint32(value);
                    else if constexpr (std::same_as<Value, float>)
                        this->float32(value);
                    else if constexpr (std::same_as<Value, math::Float2>) {
                        this->float32(value.x);
                        this->float32(value.y);
                    } else if constexpr (std::same_as<Value, math::Float3>) {
                        this->float32(value.x);
                        this->float32(value.y);
                        this->float32(value.z);
                    } else if constexpr (std::same_as<Value, math::Float4>) {
                        this->float32(value.x);
                        this->float32(value.y);
                        this->float32(value.z);
                        this->float32(value.w);
                    }
                }
            }

            void header(const GeometryAssetHeader& value) {
                this->magic(value.magic);
                this->uint32(value.version);
                this->uint32(value.reserved);
                this->uint64(value.position_count);
                this->uint64(value.normal_count);
                this->uint64(value.tangent_count);
                this->uint64(value.texture_coordinate_count);
                this->uint64(value.index_count);
            }

            void header(const SphereSetAssetHeader& value) {
                this->magic(value.magic);
                this->uint32(value.version);
                this->uint32(value.reserved);
                this->uint64(value.position_count);
                this->uint64(value.radius_count);
            }

            void header(const VolumeAssetHeader& value) {
                this->magic(value.magic);
                this->uint32(value.version);
                this->uint32(std::to_underlying(value.kind));
                this->uint32(value.resolution.x);
                this->uint32(value.resolution.y);
                this->uint32(value.resolution.z);
                this->uint32(value.reserved);
                this->uint64(value.primary_element_count);
                this->uint64(value.secondary_element_count);
                this->uint64(value.tertiary_element_count);
            }

            void header(const TextureAssetHeader& value) {
                this->magic(value.magic);
                this->uint32(value.version);
                this->uint32(value.mip_count);
                this->uint32(value.width);
                this->uint32(value.height);
                this->uint64(value.texel_count);
            }
        };

        struct AssetReader {
            std::span<const std::byte> bytes{};
            std::size_t offset{};

            [[nodiscard]] std::array<char, 8> magic() {
                if (this->bytes.size() - this->offset < 8) throw std::runtime_error("Truncated Spectra asset header");
                std::array<char, 8> result{};
                for (char& byte : result) byte = static_cast<char>(std::to_integer<unsigned char>(this->bytes[this->offset++]));
                return result;
            }

            [[nodiscard]] std::uint32_t uint32() {
                if (this->bytes.size() - this->offset < 4) throw std::runtime_error("Truncated Spectra asset value");
                std::uint32_t result{};
                for (std::uint32_t shift = 0; shift != 32; shift += 8) result |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(this->bytes[this->offset++])) << shift;
                return result;
            }

            [[nodiscard]] std::uint64_t uint64() {
                if (this->bytes.size() - this->offset < 8) throw std::runtime_error("Truncated Spectra asset value");
                std::uint64_t result{};
                for (std::uint32_t shift = 0; shift != 64; shift += 8) result |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(this->bytes[this->offset++])) << shift;
                return result;
            }

            [[nodiscard]] float float32() {
                return std::bit_cast<float>(this->uint32());
            }

            template <typename Value>
            void values(std::vector<Value>& destination, const std::uint64_t count) {
                constexpr std::size_t encoded_size = std::same_as<Value, math::Float2> ? 8u : std::same_as<Value, math::Float3> ? 12u : std::same_as<Value, math::Float4> ? 16u : 4u;
                if (count > (this->bytes.size() - this->offset) / encoded_size) throw std::runtime_error("Spectra asset element count exceeds its payload");
                destination.resize(static_cast<std::size_t>(count));
                for (Value& value : destination) {
                    if constexpr (std::same_as<Value, std::uint32_t>)
                        value = this->uint32();
                    else if constexpr (std::same_as<Value, float>)
                        value = this->float32();
                    else if constexpr (std::same_as<Value, math::Float2>)
                        value = {this->float32(), this->float32()};
                    else if constexpr (std::same_as<Value, math::Float3>)
                        value = {this->float32(), this->float32(), this->float32()};
                    else if constexpr (std::same_as<Value, math::Float4>)
                        value = {this->float32(), this->float32(), this->float32(), this->float32()};
                }
            }

            [[nodiscard]] GeometryAssetHeader geometry_header() {
                return {this->magic(), this->uint32(), this->uint32(), this->uint64(), this->uint64(), this->uint64(), this->uint64(), this->uint64()};
            }

            [[nodiscard]] SphereSetAssetHeader sphere_set_header() {
                return {this->magic(), this->uint32(), this->uint32(), this->uint64(), this->uint64()};
            }

            [[nodiscard]] VolumeAssetHeader volume_header() {
                const std::array<char, 8> magic_value = this->magic();
                const std::uint32_t version           = this->uint32();
                const VolumeAssetKind kind            = static_cast<VolumeAssetKind>(this->uint32());
                const math::UInt3 resolution{this->uint32(), this->uint32(), this->uint32()};
                return {magic_value, version, kind, resolution, this->uint32(), this->uint64(), this->uint64(), this->uint64()};
            }

            [[nodiscard]] TextureAssetHeader texture_header() {
                return {this->magic(), this->uint32(), this->uint32(), this->uint32(), this->uint32(), this->uint64()};
            }

            [[nodiscard]] bool finished() const noexcept {
                return this->offset == this->bytes.size();
            }
        };

        [[nodiscard]] std::uint64_t volume_sample_count(const math::UInt3 resolution) {
            if (resolution.x == 0 || resolution.y == 0 || resolution.z == 0) throw std::runtime_error("Spectra volume resolution must be positive");
            const std::uint64_t xy = static_cast<std::uint64_t>(resolution.x) * resolution.y;
            if (xy > std::numeric_limits<std::uint64_t>::max() / resolution.z) throw std::runtime_error("Spectra volume sample count overflows uint64");
            return xy * resolution.z;
        }

        [[nodiscard]] std::filesystem::path asset_path(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            return package_root / "assets" / std::format("{}{}", reference.content_hash, extension);
        }

        [[nodiscard]] std::filesystem::path source_path(const std::filesystem::path& package_root, const SourceReference& reference) {
            const std::filesystem::path relative = std::filesystem::path{reference.path}.lexically_normal();
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") throw std::runtime_error(std::format("Spectra source path must stay inside the scene package: {}", reference.path));
            return package_root / relative;
        }

        void verify_asset(const std::filesystem::path& package_root, const AssetReference& reference, const std::string_view extension) {
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            if (content_hash::sha256_file(path) != reference.content_hash) throw std::runtime_error(std::format("Spectra asset SHA-256 mismatch: {}", path.string()));
        }

        [[nodiscard]] AssetReference store_asset_bytes(const AssetWriter& writer, const std::filesystem::path& package_root, const std::string_view extension, const std::string_view label, AssetTransaction& transaction) {
            const std::span<const std::byte> bytes{writer.bytes};
            AssetReference reference{.content_hash = content_hash::sha256_hex(std::array{bytes})};
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            if (std::filesystem::exists(path)) {
                verify_asset(package_root, reference, extension);
                return reference;
            }
            std::filesystem::create_directories(path.parent_path());
            transaction.record(path);
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) throw std::runtime_error(std::format("Failed to create Spectra {} asset: {}", label, path.string()));
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!stream) throw std::runtime_error(std::format("Failed to write Spectra {} asset: {}", label, path.string()));
            return reference;
        }

        [[nodiscard]] std::vector<std::byte> load_asset_bytes(const AssetReference& reference, const std::filesystem::path& package_root, const std::string_view extension) {
            verify_asset(package_root, reference, extension);
            const std::filesystem::path path = asset_path(package_root, reference, extension);
            std::ifstream stream{path, std::ios::binary | std::ios::ate};
            if (!stream) throw std::runtime_error(std::format("Failed to open Spectra asset: {}", path.string()));
            const std::streamsize size = stream.tellg();
            if (size < 0) throw std::runtime_error(std::format("Failed to determine Spectra asset size: {}", path.string()));
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            stream.seekg(0);
            stream.read(reinterpret_cast<char*>(bytes.data()), size);
            if (!stream) throw std::runtime_error(std::format("Failed to read Spectra asset: {}", path.string()));
            return bytes;
        }

        [[nodiscard]] AssetReference write_geometry_asset(const TriangleMeshGeometry& mesh, const std::filesystem::path& package_root, AssetTransaction& transaction) {
            const GeometryAssetHeader header{
                .position_count           = mesh.positions.size(),
                .normal_count             = mesh.normals.size(),
                .tangent_count            = mesh.tangents.size(),
                .texture_coordinate_count = mesh.texture_coordinates.size(),
                .index_count              = mesh.indices.size(),
            };
            AssetWriter writer{};
            writer.header(header);
            writer.values(std::span{mesh.positions});
            writer.values(std::span{mesh.normals});
            writer.values(std::span{mesh.tangents});
            writer.values(std::span{mesh.texture_coordinates});
            writer.values(std::span{mesh.indices});
            return store_asset_bytes(writer, package_root, ".geometry", "geometry", transaction);
        }

        void load_geometry_asset(TriangleMeshGeometry& mesh, const std::filesystem::path& package_root) {
            const std::filesystem::path path   = asset_path(package_root, mesh.asset, ".geometry");
            const std::vector<std::byte> bytes = load_asset_bytes(mesh.asset, package_root, ".geometry");
            AssetReader reader{bytes};
            const GeometryAssetHeader header = reader.geometry_header();
            if (header.magic != GeometryAssetHeader{}.magic || header.version != 3 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra geometry asset header: {}", path.string()));
            reader.values(mesh.positions, header.position_count);
            reader.values(mesh.normals, header.normal_count);
            reader.values(mesh.tangents, header.tangent_count);
            reader.values(mesh.texture_coordinates, header.texture_coordinate_count);
            reader.values(mesh.indices, header.index_count);
            if (!reader.finished()) throw std::runtime_error(std::format("Invalid Spectra geometry asset payload size: {}", path.string()));
        }

        [[nodiscard]] AssetReference write_sphere_set_asset(const SphereSet& spheres, const std::filesystem::path& package_root, AssetTransaction& transaction) {
            if (spheres.radii.size() != spheres.positions.size()) throw std::runtime_error("Spectra SphereSet radii do not match the sphere count");
            const SphereSetAssetHeader header{
                .position_count = spheres.positions.size(),
                .radius_count   = spheres.radii.size(),
            };
            AssetWriter writer{};
            writer.header(header);
            writer.values(std::span{spheres.positions});
            writer.values(std::span{spheres.radii});
            return store_asset_bytes(writer, package_root, ".spheres", "SphereSet", transaction);
        }

        void load_sphere_set_asset(SphereSet& spheres, const std::filesystem::path& package_root) {
            const std::filesystem::path path   = asset_path(package_root, spheres.asset, ".spheres");
            const std::vector<std::byte> bytes = load_asset_bytes(spheres.asset, package_root, ".spheres");
            AssetReader reader{bytes};
            const SphereSetAssetHeader header = reader.sphere_set_header();
            if (header.magic != SphereSetAssetHeader{}.magic || header.version != 1 || header.reserved != 0) throw std::runtime_error(std::format("Invalid Spectra SphereSet asset header: {}", path.string()));
            if (header.radius_count != header.position_count) throw std::runtime_error(std::format("Invalid Spectra SphereSet attribute counts: {}", path.string()));
            reader.values(spheres.positions, header.position_count);
            reader.values(spheres.radii, header.radius_count);
            if (!reader.finished()) throw std::runtime_error(std::format("Invalid Spectra SphereSet asset payload size: {}", path.string()));
        }

        template <typename... Element>
        [[nodiscard]] AssetReference write_volume_asset_payload(const VolumeAssetHeader& header, const std::filesystem::path& package_root, AssetTransaction& transaction, const std::span<const Element>... payload) {
            AssetWriter writer{};
            writer.header(header);
            (writer.values(payload), ...);
            return store_asset_bytes(writer, package_root, ".volume", "volume", transaction);
        }

        [[nodiscard]] AssetReference write_volume_asset(const DensityGridVolume& volume, const std::filesystem::path& package_root, AssetTransaction& transaction) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if (volume.density.size() != sample_count || (!volume.temperature.empty() && volume.temperature.size() != sample_count) || (!volume.emission_scale.empty() && volume.emission_scale.size() != sample_count)) throw std::runtime_error("Spectra density-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::DensityGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.density.size(),
                .secondary_element_count = volume.temperature.size(),
                .tertiary_element_count  = volume.emission_scale.size(),
            };
            return write_volume_asset_payload(header, package_root, transaction, std::span<const float>{volume.density}, std::span<const float>{volume.temperature}, std::span<const float>{volume.emission_scale});
        }

        [[nodiscard]] AssetReference write_volume_asset(const RgbGridVolume& volume, const std::filesystem::path& package_root, AssetTransaction& transaction) {
            const std::uint64_t sample_count = volume_sample_count(volume.resolution);
            if ((volume.sigma_a.empty() && volume.sigma_s.empty()) || (!volume.sigma_a.empty() && volume.sigma_a.size() != sample_count) || (!volume.sigma_s.empty() && volume.sigma_s.size() != sample_count) || (!volume.emission.empty() && (volume.sigma_a.empty() || volume.emission.size() != sample_count))) throw std::runtime_error("Spectra RGB-grid payload does not match its resolution");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::RgbGrid,
                .resolution              = volume.resolution,
                .primary_element_count   = volume.sigma_a.size(),
                .secondary_element_count = volume.sigma_s.size(),
                .tertiary_element_count  = volume.emission.size(),
            };
            return write_volume_asset_payload(header, package_root, transaction, std::span<const math::Float3>{volume.sigma_a}, std::span<const math::Float3>{volume.sigma_s}, std::span<const math::Float3>{volume.emission});
        }

        [[nodiscard]] AssetReference write_volume_asset(const NanoVdbVolume& volume, const std::filesystem::path& package_root, AssetTransaction& transaction) {
            const std::uint64_t majorant_count = volume_sample_count(volume.majorant_resolution);
            if (volume.density_data.empty() || volume.majorant.size() != majorant_count) throw std::runtime_error("Spectra NanoVDB payload and majorant grid are required");
            const VolumeAssetHeader header{
                .kind                    = VolumeAssetKind::NanoVdb,
                .resolution              = volume.majorant_resolution,
                .primary_element_count   = volume.density_data.size(),
                .secondary_element_count = volume.temperature_data.size(),
                .tertiary_element_count  = volume.majorant.size(),
            };
            return write_volume_asset_payload(header, package_root, transaction, std::span<const std::uint32_t>{volume.density_data}, std::span<const std::uint32_t>{volume.temperature_data}, std::span<const float>{volume.majorant});
        }

        [[nodiscard]] AssetReference write_texture_asset(const ImageTexture& texture, const std::filesystem::path& package_root, AssetTransaction& transaction) {
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
            AssetWriter writer{};
            writer.header(header);
            writer.values(std::span{texture.texels});
            return store_asset_bytes(writer, package_root, ".texture", "texture", transaction);
        }

        void load_volume_asset(DensityGridVolume& volume, const std::filesystem::path& package_root) {
            const std::vector<std::byte> bytes = load_asset_bytes(volume.asset, package_root, ".volume");
            AssetReader reader{bytes};
            const VolumeAssetHeader header = reader.volume_header();
            if (header.magic != VolumeAssetHeader{}.magic || header.version != 2 || header.reserved != 0) throw std::runtime_error("Invalid Spectra volume asset header");
            if (header.kind != VolumeAssetKind::DensityGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra density-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if (header.primary_element_count != sample_count || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && header.tertiary_element_count != sample_count)) throw std::runtime_error("Invalid Spectra density-grid volume asset sample counts");
            reader.values(volume.density, header.primary_element_count);
            reader.values(volume.temperature, header.secondary_element_count);
            reader.values(volume.emission_scale, header.tertiary_element_count);
            if (!reader.finished()) throw std::runtime_error("Invalid Spectra density-grid volume asset payload size");
        }

        void load_volume_asset(RgbGridVolume& volume, const std::filesystem::path& package_root) {
            const std::vector<std::byte> bytes = load_asset_bytes(volume.asset, package_root, ".volume");
            AssetReader reader{bytes};
            const VolumeAssetHeader header = reader.volume_header();
            if (header.magic != VolumeAssetHeader{}.magic || header.version != 2 || header.reserved != 0) throw std::runtime_error("Invalid Spectra volume asset header");
            if (header.kind != VolumeAssetKind::RgbGrid || header.resolution != volume.resolution) throw std::runtime_error("Spectra RGB-grid volume asset metadata is inconsistent");
            const std::uint64_t sample_count = volume_sample_count(header.resolution);
            if ((header.primary_element_count == 0 && header.secondary_element_count == 0) || (header.primary_element_count != 0 && header.primary_element_count != sample_count) || (header.secondary_element_count != 0 && header.secondary_element_count != sample_count) || (header.tertiary_element_count != 0 && (header.primary_element_count == 0 || header.tertiary_element_count != sample_count))) throw std::runtime_error("Invalid Spectra RGB-grid volume asset sample counts");
            reader.values(volume.sigma_a, header.primary_element_count);
            reader.values(volume.sigma_s, header.secondary_element_count);
            reader.values(volume.emission, header.tertiary_element_count);
            if (!reader.finished()) throw std::runtime_error("Invalid Spectra RGB-grid volume asset payload size");
        }

        void load_volume_asset(NanoVdbVolume& volume, const std::filesystem::path& package_root) {
            const std::vector<std::byte> bytes = load_asset_bytes(volume.asset, package_root, ".volume");
            AssetReader reader{bytes};
            const VolumeAssetHeader header = reader.volume_header();
            if (header.magic != VolumeAssetHeader{}.magic || header.version != 2 || header.reserved != 0) throw std::runtime_error("Invalid Spectra volume asset header");
            if (header.kind != VolumeAssetKind::NanoVdb || header.primary_element_count == 0 || header.tertiary_element_count != volume_sample_count(header.resolution)) throw std::runtime_error("Invalid Spectra NanoVDB volume asset metadata");
            volume.majorant_resolution = header.resolution;
            reader.values(volume.density_data, header.primary_element_count);
            reader.values(volume.temperature_data, header.secondary_element_count);
            reader.values(volume.majorant, header.tertiary_element_count);
            if (!reader.finished()) throw std::runtime_error("Invalid Spectra NanoVDB volume asset payload size");
        }

        void load_texture_asset(ImageTexture& texture, const std::filesystem::path& package_root) {
            const std::filesystem::path path   = asset_path(package_root, texture.asset, ".texture");
            const std::vector<std::byte> bytes = load_asset_bytes(texture.asset, package_root, ".texture");
            AssetReader reader{bytes};
            const TextureAssetHeader header = reader.texture_header();
            if (header.magic != TextureAssetHeader{}.magic || header.version != 1 || header.width == 0 || header.height == 0 || header.mip_count == 0) throw std::runtime_error(std::format("Invalid Spectra texture asset header: {}", path.string()));
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
            const std::uint32_t maximum_mips = std::bit_width(std::max(header.width, header.height));
            if (header.mip_count > maximum_mips || header.texel_count != expected_texels) throw std::runtime_error(std::format("Invalid Spectra texture asset payload size: {}", path.string()));
            texture.width  = header.width;
            texture.height = header.height;
            reader.values(texture.texels, header.texel_count);
            if (!reader.finished()) throw std::runtime_error(std::format("Invalid Spectra texture asset payload size: {}", path.string()));
        }

        void copy_asset(const AssetReference& reference, const std::string_view extension, const std::filesystem::path& source_root, const std::filesystem::path& target_root, AssetTransaction& transaction) {
            verify_asset(source_root, reference, extension);
            const std::filesystem::path source = asset_path(source_root, reference, extension);
            const std::filesystem::path target = asset_path(target_root, reference, extension);
            if (source == target) return;
            std::filesystem::create_directories(target.parent_path());
            if (std::filesystem::exists(target)) {
                verify_asset(target_root, reference, extension);
                return;
            }
            transaction.record(target);
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none);
        }

        void copy_source(const SourceReference& reference, const std::filesystem::path& source_root, const std::filesystem::path& target_root, AssetTransaction& transaction) {
            if (std::filesystem::absolute(source_root).lexically_normal() == std::filesystem::absolute(target_root).lexically_normal()) return;
            const std::filesystem::path source = source_path(source_root, reference);
            const std::filesystem::path target = source_path(target_root, reference);
            std::filesystem::create_directories(target.parent_path());
            if (std::filesystem::exists(target)) {
                if (content_hash::sha256_file(source) != content_hash::sha256_file(target)) throw std::runtime_error(std::format("Spectra source already exists with different content: {}", target.string()));
                return;
            }
            transaction.record(target);
            std::filesystem::copy_file(source, target, std::filesystem::copy_options::none);
        }

    } // namespace

    void load_package_resources(Scene& scene, const std::filesystem::path& package_root) {
        for (Geometry& geometry : scene.resources.geometries)
            if (TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&geometry.data)) {
                if (mesh->source.path.empty())
                    load_geometry_asset(*mesh, package_root);
                else
                    load_triangle_mesh_source(*mesh, source_path(package_root, mesh->source));
            }
        for (SphereSet& spheres : scene.resources.sphere_sets) load_sphere_set_asset(spheres, package_root);
        for (Volume& volume : scene.resources.volumes) {
            if (DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data)) {
                if (density->source.path.empty())
                    load_volume_asset(*density, package_root);
                else
                    load_volume_source(*density, source_path(package_root, density->source));
            } else if (RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data)) {
                if (rgb->source.path.empty())
                    load_volume_asset(*rgb, package_root);
                else
                    load_volume_source(*rgb, source_path(package_root, rgb->source));
            } else if (NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data))
                load_volume_asset(*nanovdb, package_root);
        }
        for (Texture& texture : scene.resources.textures)
            if (ImageTexture* image = std::get_if<ImageTexture>(&texture.data)) {
                if (image->source.path.empty())
                    load_texture_asset(*image, package_root);
                else
                    load_image_source(*image, texture.color_space, source_path(package_root, image->source));
            }
    }

    PackageReferences prepare_package_resources(const Scene& scene, const std::filesystem::path& package_root, const std::filesystem::path& source_root, AssetTransaction& transaction) {
        PackageReferences references{};
        references.geometries.resize(scene.resources.geometries.size());
        for (std::size_t index = 0; index != scene.resources.geometries.size(); ++index)
            if (const TriangleMeshGeometry* mesh = std::get_if<TriangleMeshGeometry>(&scene.resources.geometries[index].data)) {
                PackageResourceReference reference{};
                if (!mesh->source.path.empty()) {
                    copy_source(mesh->source, source_root, package_root, transaction);
                    reference.source = mesh->source;
                } else
                    reference.asset = write_geometry_asset(*mesh, package_root, transaction);
                references.geometries[index] = std::move(reference);
            }
        references.sphere_sets.reserve(scene.resources.sphere_sets.size());
        for (const SphereSet& spheres : scene.resources.sphere_sets) references.sphere_sets.emplace_back(write_sphere_set_asset(spheres, package_root, transaction));
        references.volumes.resize(scene.resources.volumes.size());
        for (std::size_t index = 0; index != scene.resources.volumes.size(); ++index) {
            const Volume& volume = scene.resources.volumes[index];
            if (const DensityGridVolume* density = std::get_if<DensityGridVolume>(&volume.data)) {
                PackageResourceReference reference{};
                if (!density->source.path.empty()) {
                    copy_source(density->source, source_root, package_root, transaction);
                    reference.source = density->source;
                } else
                    reference.asset = write_volume_asset(*density, package_root, transaction);
                references.volumes[index] = std::move(reference);
            } else if (const RgbGridVolume* rgb = std::get_if<RgbGridVolume>(&volume.data)) {
                PackageResourceReference reference{};
                if (!rgb->source.path.empty()) {
                    copy_source(rgb->source, source_root, package_root, transaction);
                    reference.source = rgb->source;
                } else
                    reference.asset = write_volume_asset(*rgb, package_root, transaction);
                references.volumes[index] = std::move(reference);
            } else if (const NanoVdbVolume* nanovdb = std::get_if<NanoVdbVolume>(&volume.data)) {
                PackageResourceReference reference{};
                if (!nanovdb->density_data.empty())
                    reference.asset = write_volume_asset(*nanovdb, package_root, transaction);
                else {
                    copy_asset(nanovdb->asset, ".volume", source_root, package_root, transaction);
                    reference.asset = nanovdb->asset;
                }
                references.volumes[index] = std::move(reference);
            }
        }
        references.textures.resize(scene.resources.textures.size());
        for (std::size_t index = 0; index != scene.resources.textures.size(); ++index)
            if (const ImageTexture* image = std::get_if<ImageTexture>(&scene.resources.textures[index].data)) {
                PackageResourceReference reference{};
                if (!image->source.path.empty()) {
                    copy_source(image->source, source_root, package_root, transaction);
                    reference.source = image->source;
                } else if (!image->texels.empty())
                    reference.asset = write_texture_asset(*image, package_root, transaction);
                else {
                    if (std::filesystem::exists(asset_path(package_root, image->asset, ".texture")))
                        verify_asset(package_root, image->asset, ".texture");
                    else
                        copy_asset(image->asset, ".texture", source_root, package_root, transaction);
                    reference.asset = image->asset;
                }
                references.textures[index] = std::move(reference);
            }
        return references;
    }
} // namespace spectra::scene
