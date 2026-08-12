module;
#include <exr.h>
#include <lodepng.h>
#include <nanovdb/io/IO.h>

module spectra.scene.asset_import;

import std;

namespace spectra::scene {
    namespace {
        enum class PlyFormat : std::uint8_t {
            Ascii,
            BinaryLittleEndian,
            BinaryBigEndian,
        };

        enum class PlyScalarType : std::uint8_t {
            Int8,
            Uint8,
            Int16,
            Uint16,
            Int32,
            Uint32,
            Float32,
            Float64,
        };

        struct PlyProperty {
            std::string name{};
            PlyScalarType value_type{};
            std::optional<PlyScalarType> count_type{};
        };

        struct PlyElement {
            std::string name{};
            std::uint64_t count{};
            std::vector<PlyProperty> properties{};
        };

        struct ExrReader {
            exr_reader* value{};

            ~ExrReader() {
                if (this->value) exr_reader_close(this->value);
            }
        };

        void check_exr(const exr_result result, const std::string_view operation) {
            if (!EXR_OK(result)) throw std::runtime_error(std::format("TinyEXR failed to {}: {}", operation, exr_result_string(result)));
        }

        [[nodiscard]] PlyScalarType ply_scalar_type(const std::string_view name) {
            if (name == "char" || name == "int8") return PlyScalarType::Int8;
            if (name == "uchar" || name == "uint8") return PlyScalarType::Uint8;
            if (name == "short" || name == "int16") return PlyScalarType::Int16;
            if (name == "ushort" || name == "uint16") return PlyScalarType::Uint16;
            if (name == "int" || name == "int32") return PlyScalarType::Int32;
            if (name == "uint" || name == "uint32") return PlyScalarType::Uint32;
            if (name == "float" || name == "float32") return PlyScalarType::Float32;
            if (name == "double" || name == "float64") return PlyScalarType::Float64;
            throw std::runtime_error(std::format("Unsupported PLY scalar type {}", name));
        }

        template <class Value>
        [[nodiscard]] Value ply_binary_value(std::istream& stream, const bool swap_bytes) {
            Value value{};
            stream.read(reinterpret_cast<char*>(&value), sizeof(Value));
            if (!stream) throw std::runtime_error("Unexpected end of PLY binary payload");
            if (!swap_bytes || sizeof(Value) == 1) return value;
            if constexpr (std::integral<Value>)
                return std::byteswap(value);
            else if constexpr (sizeof(Value) == sizeof(std::uint32_t))
                return std::bit_cast<Value>(std::byteswap(std::bit_cast<std::uint32_t>(value)));
            else
                return std::bit_cast<Value>(std::byteswap(std::bit_cast<std::uint64_t>(value)));
        }

        [[nodiscard]] double ply_number(std::istream& stream, const PlyFormat format, const PlyScalarType type) {
            if (format == PlyFormat::Ascii) {
                double value{};
                stream >> value;
                if (!stream) throw std::runtime_error("Invalid PLY ASCII scalar value");
                return value;
            }
            const bool file_little_endian = format == PlyFormat::BinaryLittleEndian;
            const bool swap_bytes         = file_little_endian != (std::endian::native == std::endian::little);
            switch (type) {
            case PlyScalarType::Int8: return ply_binary_value<std::int8_t>(stream, false);
            case PlyScalarType::Uint8: return ply_binary_value<std::uint8_t>(stream, false);
            case PlyScalarType::Int16: return ply_binary_value<std::int16_t>(stream, swap_bytes);
            case PlyScalarType::Uint16: return ply_binary_value<std::uint16_t>(stream, swap_bytes);
            case PlyScalarType::Int32: return ply_binary_value<std::int32_t>(stream, swap_bytes);
            case PlyScalarType::Uint32: return ply_binary_value<std::uint32_t>(stream, swap_bytes);
            case PlyScalarType::Float32: return ply_binary_value<float>(stream, swap_bytes);
            case PlyScalarType::Float64: return ply_binary_value<double>(stream, swap_bytes);
            }
            std::unreachable();
        }

        [[nodiscard]] std::uint64_t ply_count(std::istream& stream, const PlyFormat format, const PlyScalarType type) {
            const double value = ply_number(stream, format, type);
            if (!std::isfinite(value) || value < 0.0 || value != std::floor(value)) throw std::runtime_error("PLY list count must be a non-negative integer");
            return static_cast<std::uint64_t>(value);
        }

        [[nodiscard]] std::uint32_t ply_index(std::istream& stream, const PlyFormat format, const PlyScalarType type) {
            const double value = ply_number(stream, format, type);
            if (!std::isfinite(value) || value < 0.0 || value > std::numeric_limits<std::uint32_t>::max() || value != std::floor(value)) throw std::runtime_error("PLY vertex index is outside the uint32 range");
            return static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] math::Float2 project_polygon_vertex(const math::Float3 position, const std::uint32_t axis) noexcept {
            if (axis == 0) return {position.y, position.z};
            if (axis == 1) return {position.x, position.z};
            return {position.x, position.y};
        }

        [[nodiscard]] float cross_2d(const math::Float2 first, const math::Float2 second, const math::Float2 third) noexcept {
            return (second.x - first.x) * (third.y - first.y) - (second.y - first.y) * (third.x - first.x);
        }

        [[nodiscard]] bool point_in_triangle(const math::Float2 point, const math::Float2 first, const math::Float2 second, const math::Float2 third, const float orientation) noexcept {
            return cross_2d(first, second, point) * orientation >= 0.0f && cross_2d(second, third, point) * orientation >= 0.0f && cross_2d(third, first, point) * orientation >= 0.0f;
        }

        void triangulate_polygon(const std::span<const std::uint32_t> polygon, const std::span<const math::Float3> positions, std::vector<std::uint32_t>& indices) {
            if (polygon.size() < 3) throw std::runtime_error("PLY face contains fewer than three vertices");
            if (polygon.size() == 3) {
                indices.insert(indices.end(), polygon.begin(), polygon.end());
                return;
            }
            math::Float3 normal{};
            for (std::size_t index = 0; index != polygon.size(); ++index) {
                const math::Float3 current = positions[polygon[index]];
                const math::Float3 next    = positions[polygon[(index + 1) % polygon.size()]];
                normal.x += (current.y - next.y) * (current.z + next.z);
                normal.y += (current.z - next.z) * (current.x + next.x);
                normal.z += (current.x - next.x) * (current.y + next.y);
            }
            const math::Float3 absolute{std::abs(normal.x), std::abs(normal.y), std::abs(normal.z)};
            const std::uint32_t axis = absolute.x >= absolute.y && absolute.x >= absolute.z ? 0u : absolute.y >= absolute.z ? 1u : 2u;
            std::vector<math::Float2> projected{};
            projected.reserve(polygon.size());
            for (const std::uint32_t vertex : polygon) projected.push_back(project_polygon_vertex(positions[vertex], axis));
            float signed_area{};
            for (std::size_t index = 0; index != projected.size(); ++index) {
                const math::Float2 current = projected[index];
                const math::Float2 next    = projected[(index + 1) % projected.size()];
                signed_area += current.x * next.y - next.x * current.y;
            }
            if (signed_area == 0.0f) throw std::runtime_error("PLY face is degenerate");
            const float orientation = signed_area > 0.0f ? 1.0f : -1.0f;
            std::vector<std::size_t> remaining(polygon.size());
            std::ranges::iota(remaining, std::size_t{});
            while (remaining.size() > 3) {
                bool clipped{};
                for (std::size_t index = 0; index != remaining.size(); ++index) {
                    const std::size_t previous = remaining[(index + remaining.size() - 1) % remaining.size()];
                    const std::size_t current  = remaining[index];
                    const std::size_t next     = remaining[(index + 1) % remaining.size()];
                    if (cross_2d(projected[previous], projected[current], projected[next]) * orientation <= 0.0f) continue;
                    bool contains_vertex{};
                    for (const std::size_t candidate : remaining)
                        if (candidate != previous && candidate != current && candidate != next && point_in_triangle(projected[candidate], projected[previous], projected[current], projected[next], orientation)) {
                            contains_vertex = true;
                            break;
                        }
                    if (contains_vertex) continue;
                    indices.push_back(polygon[previous]);
                    indices.push_back(polygon[current]);
                    indices.push_back(polygon[next]);
                    remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
                    clipped = true;
                    break;
                }
                if (!clipped) throw std::runtime_error("PLY face cannot be triangulated without changing its topology");
            }
            for (const std::size_t vertex : remaining) indices.push_back(polygon[vertex]);
        }

        [[nodiscard]] float srgb_to_linear(const float value) noexcept {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        [[nodiscard]] math::Float4 filtered_texel(const std::span<const math::Float4> previous, const std::uint32_t previous_width, const std::uint32_t previous_height, const std::uint32_t width, const std::uint32_t height, const std::uint32_t x, const std::uint32_t y) {
            const double minimum_x = static_cast<double>(x) * previous_width / width;
            const double maximum_x = static_cast<double>(x + 1u) * previous_width / width;
            const double minimum_y = static_cast<double>(y) * previous_height / height;
            const double maximum_y = static_cast<double>(y + 1u) * previous_height / height;
            math::Float4 result{};
            double weight_sum{};
            for (std::uint32_t source_y = static_cast<std::uint32_t>(minimum_y); source_y < static_cast<std::uint32_t>(std::ceil(maximum_y)); ++source_y)
                for (std::uint32_t source_x = static_cast<std::uint32_t>(minimum_x); source_x < static_cast<std::uint32_t>(std::ceil(maximum_x)); ++source_x) {
                    const double weight_x    = std::min(maximum_x, static_cast<double>(source_x + 1u)) - std::max(minimum_x, static_cast<double>(source_x));
                    const double weight_y    = std::min(maximum_y, static_cast<double>(source_y + 1u)) - std::max(minimum_y, static_cast<double>(source_y));
                    const float weight       = static_cast<float>(weight_x * weight_y);
                    const math::Float4 value = previous[static_cast<std::size_t>(source_y) * previous_width + source_x];
                    result.x += value.x * weight;
                    result.y += value.y * weight;
                    result.z += value.z * weight;
                    result.w += value.w * weight;
                    weight_sum += weight;
                }
            const float inverse_weight = 1.0f / static_cast<float>(weight_sum);
            return {result.x * inverse_weight, result.y * inverse_weight, result.z * inverse_weight, result.w * inverse_weight};
        }
    } // namespace

    void load_triangle_mesh_source(TriangleMeshGeometry& mesh, const std::filesystem::path& path) {
        if (path.extension() != ".ply") throw std::runtime_error(std::format("Triangle Mesh source must use the PLY format: {}", path.string()));
        std::ifstream stream{path, std::ios::binary};
        if (!stream) throw std::runtime_error(std::format("Failed to open PLY source {}", path.string()));
        std::string line{};
        std::getline(stream, line);
        if (line.ends_with('\r')) line.pop_back();
        if (line != "ply") throw std::runtime_error(std::format("PLY source has an invalid signature: {}", path.string()));
        PlyFormat format{};
        bool format_present{};
        bool header_complete{};
        std::vector<PlyElement> elements{};
        while (std::getline(stream, line)) {
            if (line.ends_with('\r')) line.pop_back();
            std::istringstream tokens{line};
            std::string command{};
            tokens >> command;
            if (command.empty() || command == "comment" || command == "obj_info") continue;
            if (command == "format") {
                std::string identifier{};
                std::string version{};
                tokens >> identifier >> version;
                if (version != "1.0") throw std::runtime_error(std::format("Unsupported PLY version {}", version));
                if (identifier == "ascii")
                    format = PlyFormat::Ascii;
                else if (identifier == "binary_little_endian")
                    format = PlyFormat::BinaryLittleEndian;
                else if (identifier == "binary_big_endian")
                    format = PlyFormat::BinaryBigEndian;
                else
                    throw std::runtime_error(std::format("Unsupported PLY format {}", identifier));
                format_present = true;
            } else if (command == "element") {
                PlyElement element{};
                tokens >> element.name >> element.count;
                if (!tokens) throw std::runtime_error("Invalid PLY element declaration");
                elements.push_back(std::move(element));
            } else if (command == "property") {
                if (elements.empty()) throw std::runtime_error("PLY property appears before an element declaration");
                std::string type{};
                tokens >> type;
                PlyProperty property{};
                if (type == "list") {
                    std::string count_type{};
                    std::string value_type{};
                    tokens >> count_type >> value_type >> property.name;
                    property.count_type = ply_scalar_type(count_type);
                    property.value_type = ply_scalar_type(value_type);
                } else {
                    tokens >> property.name;
                    property.value_type = ply_scalar_type(type);
                }
                if (!tokens) throw std::runtime_error("Invalid PLY property declaration");
                elements.back().properties.push_back(std::move(property));
            } else if (command == "end_header") {
                header_complete = true;
                break;
            } else
                throw std::runtime_error(std::format("Unsupported PLY header declaration {}", command));
        }
        if (!format_present || !header_complete) throw std::runtime_error(std::format("Incomplete PLY header: {}", path.string()));

        const auto vertex_element = std::ranges::find(elements, std::string{"vertex"}, &PlyElement::name);
        const auto face_element   = std::ranges::find(elements, std::string{"face"}, &PlyElement::name);
        if (vertex_element == elements.end() || face_element == elements.end()) throw std::runtime_error("PLY Triangle Mesh requires vertex and face elements");
        const auto has_vertex_property = [&](const std::string_view name) { return std::ranges::contains(vertex_element->properties, name, &PlyProperty::name); };
        if (!has_vertex_property("x") || !has_vertex_property("y") || !has_vertex_property("z")) throw std::runtime_error("PLY Triangle Mesh requires x, y and z vertex properties");
        const bool has_nx = has_vertex_property("nx");
        const bool has_ny = has_vertex_property("ny");
        const bool has_nz = has_vertex_property("nz");
        if (has_nx != has_ny || has_nx != has_nz) throw std::runtime_error("PLY normals require nx, ny and nz properties");
        const bool has_tangent_x = has_vertex_property("tangent_x");
        const bool has_tangent_y = has_vertex_property("tangent_y");
        const bool has_tangent_z = has_vertex_property("tangent_z");
        if (has_tangent_x != has_tangent_y || has_tangent_x != has_tangent_z) throw std::runtime_error("PLY tangents require tangent_x, tangent_y and tangent_z properties");
        const std::array uv_names{std::array<std::string_view, 2>{"u", "v"}, std::array<std::string_view, 2>{"s", "t"}, std::array<std::string_view, 2>{"texture_u", "texture_v"}};
        std::optional<std::array<std::string_view, 2>> uv_properties{};
        for (const auto& names : uv_names) {
            const bool first  = has_vertex_property(names[0]);
            const bool second = has_vertex_property(names[1]);
            if (first != second) throw std::runtime_error(std::format("PLY texture coordinates require both {} and {}", names[0], names[1]));
            if (first) {
                if (uv_properties) throw std::runtime_error("PLY source contains multiple texture-coordinate property sets");
                uv_properties = names;
            }
        }

        mesh.positions.assign(vertex_element->count, {});
        if (has_nx)
            mesh.normals.assign(vertex_element->count, {});
        else
            mesh.normals.clear();
        if (uv_properties)
            mesh.texture_coordinates.assign(vertex_element->count, {});
        else
            mesh.texture_coordinates.clear();
        if (has_tangent_x)
            mesh.tangents.assign(vertex_element->count, {});
        else
            mesh.tangents.clear();
        std::vector<std::vector<std::uint32_t>> polygons{};
        polygons.reserve(face_element->count);
        for (const PlyElement& element : elements)
            for (std::uint64_t element_index = 0; element_index != element.count; ++element_index) {
                std::vector<std::uint32_t> polygon{};
                for (const PlyProperty& property : element.properties) {
                    if (property.count_type) {
                        const std::uint64_t count = ply_count(stream, format, *property.count_type);
                        if (element.name == "face" && (property.name == "vertex_indices" || property.name == "vertex_index")) {
                            polygon.reserve(count);
                            for (std::uint64_t index = 0; index != count; ++index) polygon.push_back(ply_index(stream, format, property.value_type));
                        } else
                            for (std::uint64_t index = 0; index != count; ++index) static_cast<void>(ply_number(stream, format, property.value_type));
                    } else {
                        const double value = ply_number(stream, format, property.value_type);
                        if (element.name != "vertex") continue;
                        math::Float3& position = mesh.positions[element_index];
                        if (property.name == "x")
                            position.x = static_cast<float>(value);
                        else if (property.name == "y")
                            position.y = static_cast<float>(value);
                        else if (property.name == "z")
                            position.z = static_cast<float>(value);
                        else if (property.name == "nx")
                            mesh.normals[element_index].x = static_cast<float>(value);
                        else if (property.name == "ny")
                            mesh.normals[element_index].y = static_cast<float>(value);
                        else if (property.name == "nz")
                            mesh.normals[element_index].z = static_cast<float>(value);
                        else if (property.name == "tangent_x")
                            mesh.tangents[element_index].x = static_cast<float>(value);
                        else if (property.name == "tangent_y")
                            mesh.tangents[element_index].y = static_cast<float>(value);
                        else if (property.name == "tangent_z")
                            mesh.tangents[element_index].z = static_cast<float>(value);
                        else if (uv_properties && property.name == (*uv_properties)[0])
                            mesh.texture_coordinates[element_index].x = static_cast<float>(value);
                        else if (uv_properties && property.name == (*uv_properties)[1])
                            mesh.texture_coordinates[element_index].y = static_cast<float>(value);
                    }
                }
                if (element.name == "face") {
                    if (polygon.empty()) throw std::runtime_error("PLY face does not define vertex_indices");
                    polygons.push_back(std::move(polygon));
                }
            }
        mesh.indices.clear();
        for (const std::vector<std::uint32_t>& polygon : polygons) {
            for (const std::uint32_t index : polygon)
                if (index >= mesh.positions.size()) throw std::runtime_error("PLY face references a vertex outside the vertex element");
            triangulate_polygon(polygon, mesh.positions, mesh.indices);
        }
    }

    void load_png_source(ImageTexture& image, const TextureColorSpace color_space, const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary | std::ios::ate};
        if (!stream) throw std::runtime_error(std::format("Failed to open PNG source {}", path.string()));
        const std::streamsize size = stream.tellg();
        stream.seekg(0);
        std::vector<std::uint8_t> encoded(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(encoded.data()), size);
        if (!stream) throw std::runtime_error(std::format("Failed to read PNG source {}", path.string()));
        constexpr std::array<std::uint8_t, 8> signature{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
        if (encoded.size() < signature.size() || !std::ranges::equal(signature, std::span{encoded}.first(signature.size()))) throw std::runtime_error(std::format("PNG source has an invalid signature: {}", path.string()));
        LodePNGState state{};
        lodepng_state_init(&state);
        state.info_raw.colortype = LCT_RGBA;
        state.info_raw.bitdepth  = 16;
        unsigned char* decoded{};
        unsigned width{};
        unsigned height{};
        const unsigned error = lodepng_decode(&decoded, &width, &height, &state, encoded.data(), encoded.size());
        lodepng_state_cleanup(&state);
        if (error != 0) {
            std::free(decoded);
            throw std::runtime_error(std::format("Failed to decode PNG source {}: {}", path.string(), lodepng_error_text(error)));
        }
        const std::unique_ptr<unsigned char, decltype(&std::free)> decoded_owner{decoded, &std::free};
        image.width       = width;
        image.height      = height;
        image.mip_offsets = {0};
        image.texels.resize(static_cast<std::size_t>(width) * height);
        for (std::size_t index = 0; index != image.texels.size(); ++index) {
            const auto channel = [&](const std::size_t component) { return static_cast<float>((static_cast<std::uint16_t>(decoded_owner.get()[index * 8u + component * 2u]) << 8u) | decoded_owner.get()[index * 8u + component * 2u + 1u]) / 65535.0f; };
            math::Float4 value{channel(0), channel(1), channel(2), channel(3)};
            if (color_space == TextureColorSpace::Srgb) {
                value.x = srgb_to_linear(value.x);
                value.y = srgb_to_linear(value.y);
                value.z = srgb_to_linear(value.z);
            }
            image.texels[index] = value;
        }
        std::uint32_t previous_width  = image.width;
        std::uint32_t previous_height = image.height;
        std::uint64_t previous_offset{};
        while (previous_width != 1 || previous_height != 1) {
            const std::uint32_t width  = std::max(1u, previous_width / 2u);
            const std::uint32_t height = std::max(1u, previous_height / 2u);
            const std::uint64_t offset = image.texels.size();
            image.mip_offsets.push_back(offset);
            image.texels.resize(offset + static_cast<std::uint64_t>(width) * height);
            const std::span<const math::Float4> previous{image.texels.data() + previous_offset, static_cast<std::size_t>(previous_width) * previous_height};
            for (std::uint32_t y = 0; y != height; ++y)
                for (std::uint32_t x = 0; x != width; ++x) image.texels[offset + static_cast<std::uint64_t>(y) * width + x] = filtered_texel(previous, previous_width, previous_height, width, height, x, y);
            previous_offset = offset;
            previous_width  = width;
            previous_height = height;
        }
    }

    void load_exr_source(ImageTexture& image, const std::filesystem::path& path) {
        std::ifstream stream{path, std::ios::binary | std::ios::ate};
        if (!stream) throw std::runtime_error(std::format("Failed to open EXR source {}", path.string()));
        const std::streamsize size = stream.tellg();
        stream.seekg(0);
        std::vector<std::byte> encoded(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(encoded.data()), size);
        if (!stream) throw std::runtime_error(std::format("Failed to read EXR source {}", path.string()));

        ExrReader reader{};
        check_exr(exr_reader_open_memory(encoded.data(), encoded.size(), nullptr, &reader.value), "open the EXR source");
        check_exr(exr_reader_parse_header(reader.value), "parse the EXR source header");
        if (exr_reader_num_parts(reader.value) != 1) throw std::runtime_error(std::format("EXR texture source must contain exactly one part: {}", path.string()));
        const exr_header& header = *exr_reader_part_header(reader.value, 0);
        if (header.part_type == EXR_PART_DEEP_SCANLINE || header.part_type == EXR_PART_DEEP_TILED || header.level_mode == EXR_TILE_RIPMAP_LEVELS) throw std::runtime_error(std::format("EXR texture source must be a flat image or mipmap: {}", path.string()));

        std::array<std::int32_t, 4> channel_indices{-1, -1, -1, -1};
        constexpr std::array<std::string_view, 4> channel_names{"R", "G", "B", "A"};
        for (std::int32_t channel = 0; channel != header.num_channels; ++channel)
            for (std::size_t component = 0; component != channel_names.size(); ++component)
                if (header.channels[channel].name == channel_names[component]) channel_indices[component] = channel;
        for (const std::int32_t channel : channel_indices)
            if (channel < 0 || header.channels[channel].pixel_type != EXR_PIXEL_FLOAT || header.channels[channel].x_sampling != 1 || header.channels[channel].y_sampling != 1) throw std::runtime_error(std::format("EXR texture source requires float32 R, G, B and A channels: {}", path.string()));

        std::uint32_t block_count{};
        check_exr(exr_reader_num_blocks(reader.value, 0, &block_count), "count EXR blocks");
        std::vector<exr_block_info> blocks(block_count);
        std::uint32_t maximum_level{};
        for (std::uint32_t block = 0; block != block_count; ++block) {
            check_exr(exr_reader_block_info(reader.value, 0, block, &blocks[block]), "read EXR block metadata");
            maximum_level = std::max(maximum_level, static_cast<std::uint32_t>(blocks[block].level_x));
        }

        image.width  = static_cast<std::uint32_t>(header.data_window.max_x - header.data_window.min_x + 1);
        image.height = static_cast<std::uint32_t>(header.data_window.max_y - header.data_window.min_y + 1);
        image.mip_offsets.clear();
        image.texels.clear();
        const auto level_dimension = [&header](const std::uint32_t base, const std::uint32_t level) {
            const std::uint32_t divisor = 1u << level;
            return header.rounding_mode == EXR_TILE_ROUND_UP ? std::max(1u, (base + divisor - 1u) / divisor) : std::max(1u, base / divisor);
        };
        for (std::uint32_t level = 0; level <= maximum_level; ++level) {
            image.mip_offsets.push_back(image.texels.size());
            image.texels.resize(image.texels.size() + static_cast<std::size_t>(level_dimension(image.width, level)) * level_dimension(image.height, level));
        }

        for (std::uint32_t block_index = 0; block_index != block_count; ++block_index) {
            const exr_block_info& info = blocks[block_index];
            std::vector<std::byte> decoded(info.uncompressed_size);
            check_exr(exr_reader_decode_block(reader.value, 0, block_index, decoded.data(), decoded.size()), "decode an EXR block");
            const std::size_t block_pixels = static_cast<std::size_t>(info.width) * info.height;
            std::array<std::vector<float>, 4> channels{};
            for (std::size_t component = 0; component != channels.size(); ++component) {
                channels[component].resize(block_pixels);
                check_exr(exr_block_extract_channel(&header, &info, decoded.data(), decoded.size(), channel_indices[component], channels[component].data()), "extract an EXR channel");
            }
            const std::uint32_t level       = static_cast<std::uint32_t>(info.level_x);
            const std::uint32_t level_width = level_dimension(image.width, level);
            const std::uint64_t mip_offset  = image.mip_offsets[level];
            for (std::int32_t y = 0; y != info.height; ++y)
                for (std::int32_t x = 0; x != info.width; ++x) {
                    const std::size_t source_index = static_cast<std::size_t>(y) * info.width + x;
                    const std::size_t target_index = mip_offset + static_cast<std::size_t>(info.y0 - header.data_window.min_y + y) * level_width + static_cast<std::size_t>(info.x0 - header.data_window.min_x + x);
                    image.texels[target_index]      = {channels[0][source_index], channels[1][source_index], channels[2][source_index], channels[3][source_index]};
                }
        }
    }

    void load_image_source(ImageTexture& image, const TextureColorSpace color_space, const std::filesystem::path& path) {
        if (path.extension() == ".png")
            load_png_source(image, color_space, path);
        else if (path.extension() == ".exr")
            load_exr_source(image, path);
        else
            throw std::runtime_error(std::format("Image Texture source must use the PNG or OpenEXR format: {}", path.string()));
    }

    void load_volume_source(DensityGridVolume& volume, const std::filesystem::path& path) {
        if (path.extension() != ".nvdb") throw std::runtime_error(std::format("Density Grid source must use the NanoVDB format: {}", path.string()));
        const nanovdb::GridHandle density_handle = nanovdb::io::readGrid(path.string(), "density");
        const auto density = density_handle.grid<float>()->tree().getAccessor();
        const std::size_t sample_count = static_cast<std::size_t>(volume.resolution.x) * volume.resolution.y * volume.resolution.z;
        volume.density.resize(sample_count);
        volume.temperature.clear();
        volume.emission_scale.clear();
        for (std::uint32_t z = 0; z != volume.resolution.z; ++z)
            for (std::uint32_t y = 0; y != volume.resolution.y; ++y)
                for (std::uint32_t x = 0; x != volume.resolution.x; ++x) volume.density[(static_cast<std::size_t>(z) * volume.resolution.y + y) * volume.resolution.x + x] = density.getValue(nanovdb::Coord{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z)});
        if (nanovdb::io::hasGrid(path.string(), "temperature")) {
            const nanovdb::GridHandle temperature_handle = nanovdb::io::readGrid(path.string(), "temperature");
            const auto temperature = temperature_handle.grid<float>()->tree().getAccessor();
            volume.temperature.resize(sample_count);
            for (std::uint32_t z = 0; z != volume.resolution.z; ++z)
                for (std::uint32_t y = 0; y != volume.resolution.y; ++y)
                    for (std::uint32_t x = 0; x != volume.resolution.x; ++x) volume.temperature[(static_cast<std::size_t>(z) * volume.resolution.y + y) * volume.resolution.x + x] = temperature.getValue(nanovdb::Coord{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z)});
        }
        if (nanovdb::io::hasGrid(path.string(), "emission_scale")) {
            const nanovdb::GridHandle emission_scale_handle = nanovdb::io::readGrid(path.string(), "emission_scale");
            const auto emission_scale = emission_scale_handle.grid<float>()->tree().getAccessor();
            volume.emission_scale.resize(sample_count);
            for (std::uint32_t z = 0; z != volume.resolution.z; ++z)
                for (std::uint32_t y = 0; y != volume.resolution.y; ++y)
                    for (std::uint32_t x = 0; x != volume.resolution.x; ++x) volume.emission_scale[(static_cast<std::size_t>(z) * volume.resolution.y + y) * volume.resolution.x + x] = emission_scale.getValue(nanovdb::Coord{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z)});
        }
    }

    void load_volume_source(RgbGridVolume& volume, const std::filesystem::path& path) {
        if (path.extension() != ".nvdb") throw std::runtime_error(std::format("RGB Grid source must use the NanoVDB format: {}", path.string()));
        const nanovdb::GridHandle sigma_a_handle = nanovdb::io::readGrid(path.string(), "sigma_a");
        const nanovdb::GridHandle sigma_s_handle = nanovdb::io::readGrid(path.string(), "sigma_s");
        const nanovdb::GridHandle emission_handle = nanovdb::io::readGrid(path.string(), "emission");
        const auto sigma_a = sigma_a_handle.grid<nanovdb::Vec3f>()->tree().getAccessor();
        const auto sigma_s = sigma_s_handle.grid<nanovdb::Vec3f>()->tree().getAccessor();
        const auto emission = emission_handle.grid<nanovdb::Vec3f>()->tree().getAccessor();
        const std::size_t sample_count = static_cast<std::size_t>(volume.resolution.x) * volume.resolution.y * volume.resolution.z;
        volume.sigma_a.resize(sample_count);
        volume.sigma_s.resize(sample_count);
        volume.emission.resize(sample_count);
        for (std::uint32_t z = 0; z != volume.resolution.z; ++z)
            for (std::uint32_t y = 0; y != volume.resolution.y; ++y)
                for (std::uint32_t x = 0; x != volume.resolution.x; ++x) {
                    const std::size_t index = (static_cast<std::size_t>(z) * volume.resolution.y + y) * volume.resolution.x + x;
                    const nanovdb::Coord coordinate{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y), static_cast<std::int32_t>(z)};
                    const nanovdb::Vec3f absorption = sigma_a.getValue(coordinate);
                    const nanovdb::Vec3f scattering = sigma_s.getValue(coordinate);
                    const nanovdb::Vec3f radiance   = emission.getValue(coordinate);
                    volume.sigma_a[index]           = {absorption[0], absorption[1], absorption[2]};
                    volume.sigma_s[index]           = {scattering[0], scattering[1], scattering[2]};
                    volume.emission[index]          = {radiance[0], radiance[1], radiance[2]};
                }
    }
} // namespace spectra::scene
