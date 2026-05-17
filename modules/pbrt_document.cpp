module;
#include <imgui.h>

#include <pbrt/cpu/render.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/wavefront/wavefront.h>

#include <cstring>
#include <vulkan/vulkan_raii.hpp>

module pbrt_document;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct PbrtPreviewShaderParameters {
        std::array<float, 16> model_view_projection{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<float, 4> color{};
    };

    [[nodiscard]] std::array<float, 16> identity_matrix() {
        return {
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> pbrt_file_matrix_to_spectra_matrix(const std::vector<float>& values) {
        if (values.size() != 16) throw std::runtime_error("PBRT transform command must have 16 values");
        std::array<float, 16> result{};
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t column = 0; column < 4; ++column) result[column * 4u + row] = values[row * 4u + column];
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> spectra_matrix_to_pbrt_file_matrix(const std::array<float, 16>& matrix) {
        std::array<float, 16> result{};
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t column = 0; column < 4; ++column) result[row * 4u + column] = matrix[column * 4u + row];
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> translation_matrix(const float x, const float y, const float z) {
        std::array<float, 16> result = identity_matrix();
        result[12]                  = x;
        result[13]                  = y;
        result[14]                  = z;
        return result;
    }

    [[nodiscard]] std::array<float, 16> scale_matrix(const float x, const float y, const float z) {
        std::array<float, 16> result = identity_matrix();
        result[0]                   = x;
        result[5]                   = y;
        result[10]                  = z;
        return result;
    }

    [[nodiscard]] std::array<float, 16> rotation_matrix(const float angle_degrees, const float x, const float y, const float z) {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.000001f) throw std::runtime_error("PBRT Rotate axis must be non-zero");
        const float axis_x = x / length;
        const float axis_y = y / length;
        const float axis_z = z / length;
        const float angle  = angle_degrees * 0.017453292519943295769f;
        const float c      = std::cos(angle);
        const float s      = std::sin(angle);
        const float t      = 1.0f - c;
        return {
            t * axis_x * axis_x + c,
            t * axis_x * axis_y + s * axis_z,
            t * axis_x * axis_z - s * axis_y,
            0.0f,
            t * axis_x * axis_y - s * axis_z,
            t * axis_y * axis_y + c,
            t * axis_y * axis_z + s * axis_x,
            0.0f,
            t * axis_x * axis_z + s * axis_y,
            t * axis_y * axis_z - s * axis_x,
            t * axis_z * axis_z + c,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> multiply_preview_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        return xayah::multiply_matrix(left, right);
    }

    [[nodiscard]] float parameter_float(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name, const float fallback) {
        for (const xayah::PbrtParameter& parameter : parameters) {
            if (parameter.name == name && !parameter.floats.empty()) return parameter.floats.front();
            if (parameter.name == name && !parameter.ints.empty()) return static_cast<float>(parameter.ints.front());
        }
        return fallback;
    }

    [[nodiscard]] xayah::BoundingBoxBounds preview_bounds_for_shape(const std::string& type, const std::vector<xayah::PbrtParameter>& parameters) {
        if (type == "sphere") {
            const float radius = parameter_float(parameters, "radius", 1.0f);
            return {{-radius, -radius, -radius}, {radius, radius, radius}};
        }
        if (type == "disk") {
            const float radius = parameter_float(parameters, "radius", 1.0f);
            return {{-radius, -radius, -0.001f}, {radius, radius, 0.001f}};
        }
        if (type == "trianglemesh") {
            bool initialized{};
            xayah::BoundingBoxBounds result{};
            for (const xayah::PbrtParameter& parameter : parameters) {
                if (parameter.name != "P" || parameter.floats.size() < 3) continue;
                for (std::size_t index = 0; index + 2 < parameter.floats.size(); index += 3) {
                    const std::array<float, 3> point{parameter.floats[index], parameter.floats[index + 1], parameter.floats[index + 2]};
                    if (!initialized) {
                        result      = {point, point};
                        initialized = true;
                    } else {
                        for (std::uint32_t axis = 0; axis < 3; ++axis) {
                            if (point[axis] < result.minimum[axis]) result.minimum[axis] = point[axis];
                            if (point[axis] > result.maximum[axis]) result.maximum[axis] = point[axis];
                        }
                    }
                }
            }
            if (initialized) return result;
        }
        return {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    }

    [[nodiscard]] xayah::BoundingBoxBounds transformed_bounds(const std::array<float, 16>& matrix, const xayah::BoundingBoxBounds& bounds) {
        bool initialized{};
        xayah::BoundingBoxBounds result{};
        for (std::uint32_t corner = 0; corner < 8; ++corner) {
            const std::array<float, 3> local_point{
                (corner & 1u) != 0u ? bounds.maximum[0] : bounds.minimum[0],
                (corner & 2u) != 0u ? bounds.maximum[1] : bounds.minimum[1],
                (corner & 4u) != 0u ? bounds.maximum[2] : bounds.minimum[2],
            };
            const std::array<float, 3> point = xayah::transform_point(matrix, local_point);
            if (!initialized) {
                result      = {point, point};
                initialized = true;
            } else {
                for (std::uint32_t axis = 0; axis < 3; ++axis) {
                    if (point[axis] < result.minimum[axis]) result.minimum[axis] = point[axis];
                    if (point[axis] > result.maximum[axis]) result.maximum[axis] = point[axis];
                }
            }
        }
        if (!initialized) throw std::runtime_error("Cannot transform an empty PBRT preview bound");
        return result;
    }

    void expand_bounds(xayah::BoundingBoxBounds& result, const xayah::BoundingBoxBounds& value) {
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            if (value.minimum[axis] < result.minimum[axis]) result.minimum[axis] = value.minimum[axis];
            if (value.maximum[axis] > result.maximum[axis]) result.maximum[axis] = value.maximum[axis];
        }
    }

    const char* element_kind_label(const xayah::PbrtElementKind kind) {
        if (kind == xayah::PbrtElementKind::camera) return "Camera";
        if (kind == xayah::PbrtElementKind::shape) return "Shape";
        if (kind == xayah::PbrtElementKind::material) return "Material";
        if (kind == xayah::PbrtElementKind::texture) return "Texture";
        if (kind == xayah::PbrtElementKind::light) return "Light";
        if (kind == xayah::PbrtElementKind::medium) return "Medium";
        if (kind == xayah::PbrtElementKind::render_setting) return "Render Setting";
        if (kind == xayah::PbrtElementKind::instance) return "Instance";
        throw std::runtime_error("Unknown PBRT element kind");
    }

    void copy_parameters(const pbrt::ParsedParameterVector& source, std::vector<xayah::PbrtParameter>& destination) {
        destination.clear();
        destination.reserve(source.size());
        for (const pbrt::ParsedParameter* parsed : source) {
            xayah::PbrtParameter parameter{};
            parameter.type = parsed->type;
            parameter.name = parsed->name;
            parameter.floats.reserve(parsed->floats.size());
            for (const pbrt::Float value : parsed->floats) parameter.floats.push_back(static_cast<float>(value));
            parameter.ints.reserve(parsed->ints.size());
            for (const int value : parsed->ints) parameter.ints.push_back(value);
            parameter.strings = std::vector<std::string>{parsed->strings.begin(), parsed->strings.end()};
            parameter.bools.reserve(parsed->bools.size());
            for (const std::uint8_t value : parsed->bools) parameter.bools.push_back(value != 0);
            destination.push_back(std::move(parameter));
        }
    }

    pbrt::ParsedParameterVector make_parsed_parameters(const std::vector<xayah::PbrtParameter>& parameters) {
        pbrt::ParsedParameterVector result;
        for (const xayah::PbrtParameter& parameter : parameters) {
            pbrt::ParsedParameter* parsed = new pbrt::ParsedParameter({});
            parsed->type                  = parameter.type;
            parsed->name                  = parameter.name;
            for (const float value : parameter.floats) parsed->AddFloat(static_cast<pbrt::Float>(value));
            for (const int value : parameter.ints) parsed->AddInt(value);
            for (const std::string& value : parameter.strings) parsed->AddString(value);
            for (const bool value : parameter.bools) parsed->AddBool(value);
            result.push_back(parsed);
        }
        return result;
    }

    void delete_parsed_parameters(const pbrt::ParsedParameterVector& parameters) {
        for (pbrt::ParsedParameter* parameter : parameters) delete parameter;
    }

    void replay_transform_override(pbrt::BasicSceneBuilder& builder, const xayah::PbrtElement& element) {
        std::array<float, 16> row_major = spectra_matrix_to_pbrt_file_matrix(element.transform);
        builder.AttributeBegin({});
        builder.Transform(row_major.data(), {});
    }
}

namespace xayah {
    class PbrtDocumentLoader final : public pbrt::ParserTarget {
    public:
        explicit PbrtDocumentLoader(PbrtDocument& document) : document{document} {}

        void Option(const std::string& name, const std::string& value, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::option};
            command.text[0] = name;
            command.text[1] = value;
            this->append_command(std::move(command));
            this->append_element(PbrtElementKind::render_setting, name, value, {});
        }

        void Identity(pbrt::FileLoc) override {
            this->matrix_stack.back() = identity_matrix();
            this->append_command({PbrtDocument::PbrtCommandKind::identity});
        }

        void Translate(const pbrt::Float dx, const pbrt::Float dy, const pbrt::Float dz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::translate};
            command.values = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), translation_matrix(command.values[0], command.values[1], command.values[2]));
            this->append_command(std::move(command));
        }

        void Rotate(const pbrt::Float angle, const pbrt::Float ax, const pbrt::Float ay, const pbrt::Float az, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::rotate};
            command.values = {static_cast<float>(angle), static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(az)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), rotation_matrix(command.values[0], command.values[1], command.values[2], command.values[3]));
            this->append_command(std::move(command));
        }

        void Scale(const pbrt::Float sx, const pbrt::Float sy, const pbrt::Float sz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::scale};
            command.values = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), scale_matrix(command.values[0], command.values[1], command.values[2]));
            this->append_command(std::move(command));
        }

        void LookAt(const pbrt::Float ex, const pbrt::Float ey, const pbrt::Float ez, const pbrt::Float lx, const pbrt::Float ly, const pbrt::Float lz, const pbrt::Float ux, const pbrt::Float uy, const pbrt::Float uz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::look_at};
            command.values = {static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez), static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz)};
            this->append_command(std::move(command));
        }

        void ConcatTransform(pbrt::Float transform[16], pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::concat_transform};
            command.values.assign(transform, transform + 16);
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), pbrt_file_matrix_to_spectra_matrix(command.values));
            this->append_command(std::move(command));
        }

        void Transform(pbrt::Float transform[16], pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::transform};
            command.values.assign(transform, transform + 16);
            this->matrix_stack.back() = pbrt_file_matrix_to_spectra_matrix(command.values);
            this->append_command(std::move(command));
        }

        void CoordinateSystem(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::coordinate_system};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void CoordSysTransform(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::coord_sys_transform};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void ActiveTransformAll(pbrt::FileLoc) override { this->append_command({PbrtDocument::PbrtCommandKind::active_transform_all}); }

        void ActiveTransformEndTime(pbrt::FileLoc) override { this->append_command({PbrtDocument::PbrtCommandKind::active_transform_end_time}); }

        void ActiveTransformStartTime(pbrt::FileLoc) override { this->append_command({PbrtDocument::PbrtCommandKind::active_transform_start_time}); }

        void TransformTimes(const pbrt::Float start, const pbrt::Float end, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::transform_times};
            command.values = {static_cast<float>(start), static_cast<float>(end)};
            this->append_command(std::move(command));
        }

        void ColorSpace(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::color_space};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void PixelFilter(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::pixel_filter, PbrtElementKind::render_setting, name, {}, params);
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::film, PbrtElementKind::render_setting, "Film", type, params);
        }

        void Sampler(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::sampler, PbrtElementKind::render_setting, "Sampler", name, params);
        }

        void Accelerator(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::accelerator, PbrtElementKind::render_setting, "Accelerator", name, params);
        }

        void Integrator(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::integrator, PbrtElementKind::render_setting, "Integrator", name, params);
        }

        void Camera(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::camera, PbrtElementKind::camera, "Camera", name, params);
        }

        void MakeNamedMedium(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::make_named_medium, PbrtElementKind::medium, name, {}, params);
        }

        void MediumInterface(const std::string& insideName, const std::string& outsideName, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::medium_interface};
            command.text[0] = insideName;
            command.text[1] = outsideName;
            this->append_command(std::move(command));
        }

        void WorldBegin(pbrt::FileLoc) override { this->append_command({PbrtDocument::PbrtCommandKind::world_begin}); }

        void AttributeBegin(pbrt::FileLoc) override {
            this->matrix_stack.push_back(this->matrix_stack.back());
            this->material_stack.push_back(this->material_stack.back());
            this->append_command({PbrtDocument::PbrtCommandKind::attribute_begin});
        }

        void AttributeEnd(pbrt::FileLoc) override {
            if (this->matrix_stack.size() <= 1) throw std::runtime_error("PBRT AttributeEnd without matching AttributeBegin");
            this->matrix_stack.pop_back();
            this->material_stack.pop_back();
            this->append_command({PbrtDocument::PbrtCommandKind::attribute_end});
        }

        void Attribute(const std::string& target, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::attribute};
            command.text[0] = target;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            this->append_command(std::move(command));
        }

        void Texture(const std::string& name, const std::string& type, const std::string& texname, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::texture};
            command.text[0] = name;
            command.text[1] = type;
            command.text[2] = texname;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(PbrtElementKind::texture, name, type + " " + texname, command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void Material(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->material_stack.back() = name;
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::material, PbrtElementKind::material, "inline material", name, params);
        }

        void MakeNamedMaterial(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::make_named_material, PbrtElementKind::material, name, {}, params);
        }

        void NamedMaterial(const std::string& name, pbrt::FileLoc) override {
            this->material_stack.back() = name;
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::named_material};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void LightSource(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::light_source, PbrtElementKind::light, "Light", name, params);
        }

        void AreaLightSource(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::area_light_source, PbrtElementKind::light, "Area Light", name, params);
        }

        void Shape(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::shape};
            command.text[0] = name;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(PbrtElementKind::shape, name, this->material_stack.back(), command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void ReverseOrientation(pbrt::FileLoc) override { this->append_command({PbrtDocument::PbrtCommandKind::reverse_orientation}); }

        void ObjectBegin(const std::string& name, pbrt::FileLoc) override {
            this->matrix_stack.push_back(this->matrix_stack.back());
            this->material_stack.push_back(this->material_stack.back());
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::object_begin};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void ObjectEnd(pbrt::FileLoc) override {
            if (this->matrix_stack.size() <= 1) throw std::runtime_error("PBRT ObjectEnd without matching ObjectBegin");
            this->matrix_stack.pop_back();
            this->material_stack.pop_back();
            this->append_command({PbrtDocument::PbrtCommandKind::object_end});
        }

        void ObjectInstance(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::object_instance};
            command.text[0] = name;
            const std::uint64_t element_id = this->append_element(PbrtElementKind::instance, name, {}, {});
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void EndOfFiles() override {}

    private:
        PbrtDocument& document;
        std::vector<std::array<float, 16>> matrix_stack{identity_matrix()};
        std::vector<std::string> material_stack{""};

        void append_command(PbrtDocument::PbrtCommand&& command) {
            this->document.commands.push_back(std::move(command));
        }

        std::uint64_t append_element(const PbrtElementKind kind, const std::string& name, const std::string& detail, const std::vector<PbrtParameter>& parameters) {
            PbrtElement element{};
            element.id            = this->document.next_element_id++;
            element.kind          = kind;
            element.type          = name;
            element.name          = name.empty() ? std::string{element_kind_label(kind)} + " " + std::to_string(element.id) : name;
            element.detail        = detail;
            element.command_index = this->document.commands.size();
            element.parameters    = parameters;
            element.transform     = this->matrix_stack.back();
            if (kind == PbrtElementKind::shape) element.local_bounds = preview_bounds_for_shape(name, parameters);
            this->document.elements.push_back(std::move(element));
            return this->document.elements.back().id;
        }

        void append_named_parameter_command(const PbrtDocument::PbrtCommandKind command_kind, const PbrtElementKind element_kind, const std::string& name, const std::string& type, const pbrt::ParsedParameterVector& params) {
            PbrtDocument::PbrtCommand command{command_kind};
            command.text[0] = name;
            command.text[1] = type;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(element_kind, name, type, command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }
    };

    PbrtPreviewRenderer::PbrtPreviewRenderer() = default;

    PbrtPreviewRenderer::~PbrtPreviewRenderer() noexcept = default;

    void PbrtPreviewRenderer::create(const SceneRenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("PBRT preview renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create PBRT preview renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined || context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create PBRT preview renderer without swapchain formats");

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(PbrtPreviewShaderParameters)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;
        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
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
        const vk::Format stencil_format = static_cast<bool>(context.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? context.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &context.color_format;
        rendering_create_info.depthAttachmentFormat   = context.depth_format;
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
        pipeline_create_info.layout              = *this->pipeline_layout;
        this->pipeline                           = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
    }

    void PbrtPreviewRenderer::destroy() noexcept {
        this->pipeline        = nullptr;
        this->pipeline_layout = nullptr;
    }

    void PbrtPreviewRenderer::render(const SceneRenderFrameContext& context, const std::array<float, 16>& transform, const BoundingBoxBounds& bounds, const std::array<float, 4>& color) {
        if (context.command_buffer == nullptr) throw std::runtime_error("PBRT preview render context is incomplete");
        if (!*this->pipeline_layout || !*this->pipeline) throw std::runtime_error("PBRT preview renderer is not initialized");
        PbrtPreviewShaderParameters parameters{};
        parameters.model_view_projection = multiply_matrix(context.view_projection, transform);
        parameters.bounds_min            = {bounds.minimum[0], bounds.minimum[1], bounds.minimum[2], 1.0f};
        parameters.bounds_max            = {bounds.maximum[0], bounds.maximum[1], bounds.maximum[2], 1.0f};
        parameters.color                 = color;
        context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->pipeline);
        context.command_buffer->pushConstants(*this->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const PbrtPreviewShaderParameters>{1, &parameters});
        context.command_buffer->draw(24, 1, 0, 0);
    }

    bool PbrtPreviewRenderer::active() const {
        return static_cast<bool>(*this->pipeline);
    }

    PbrtDocument::PbrtDocument() = default;

    PbrtDocument::~PbrtDocument() noexcept = default;

    void PbrtDocument::load(const std::filesystem::path& path) {
        if (path.empty()) throw std::runtime_error("PBRT document path must not be empty");
        if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"PBRT document does not exist: "} + path.string());
        this->source_path      = std::filesystem::absolute(path);
        this->elements         = {};
        this->commands         = {};
        this->selection        = {};
        this->next_element_id  = 1;
        PbrtDocumentLoader loader{*this};
        const std::string filename = this->source_path.string();
        std::array<std::string, 1> files{filename};
        pbrt::ParseFiles(&loader, pstd::MakeConstSpan(files.data(), files.size()));
        this->dirty = false;
        this->validate();
    }

    void PbrtDocument::create_default() {
        this->source_path     = std::filesystem::path{};
        this->elements        = {};
        this->commands        = {};
        this->selection       = {};
        this->next_element_id = 1;
        constexpr const char* default_scene = R"(
Film "rgb" "integer xresolution" [1280] "integer yresolution" [720] "string filename" "render-output.exr"
Sampler "zsobol" "integer pixelsamples" [64]
Integrator "volpath" "integer maxdepth" [5]
LookAt 4 3 5 0 0 0 0 1 0
Camera "perspective" "float fov" [45]
WorldBegin
LightSource "distant" "rgb L" [3 3 3] "point3 from" [0 4 4] "point3 to" [0 0 0]
MakeNamedMaterial "matte_gray" "string type" "diffuse" "rgb reflectance" [0.72 0.72 0.72]
NamedMaterial "matte_gray"
Shape "sphere" "float radius" [1]
)";
        PbrtDocumentLoader loader{*this};
        pbrt::ParseString(&loader, default_scene);
        this->dirty = false;
        this->validate();
    }

    void PbrtDocument::validate() const {
        if (this->commands.empty()) throw std::runtime_error("PBRT document has no commands");
        if (this->elements.empty()) throw std::runtime_error("PBRT document has no editable elements");
        std::set<std::uint64_t> ids{};
        for (const PbrtElement& element : this->elements) {
            if (element.id == 0) throw std::runtime_error("PBRT element id must not be zero");
            if (!ids.insert(element.id).second) throw std::runtime_error(std::string{"Duplicate PBRT element id: "} + std::to_string(element.id));
        }
        if (this->selection.element_id != 0 && this->find_element(this->selection.element_id) == nullptr) throw std::runtime_error("PBRT selection points to a missing element");
    }

    void PbrtDocument::create_render_resources(const SceneRenderCreateContext& context) {
        this->preview_renderer.create(context);
    }

    void PbrtDocument::destroy_render_resources() noexcept {
        this->preview_renderer.destroy();
    }

    void PbrtDocument::recreate_render_resources(const SceneRenderCreateContext& context) {
        this->destroy_render_resources();
        this->create_render_resources(context);
    }

    void PbrtDocument::render(const SceneRenderFrameContext& context) {
        constexpr std::array<float, 4> shape_color{0.72f, 0.78f, 0.90f, 0.92f};
        constexpr std::array<float, 4> selected_color{1.0f, 0.76f, 0.30f, 0.98f};
        constexpr std::array<float, 4> light_color{1.0f, 0.88f, 0.45f, 0.92f};
        for (const PbrtElement& element : this->elements) {
            if (!element.visible) continue;
            if (element.kind != PbrtElementKind::shape && element.kind != PbrtElementKind::light) continue;
            std::array<float, 4> color = element.kind == PbrtElementKind::light ? light_color : shape_color;
            if (this->selection.element_id == element.id) color = selected_color;
            this->preview_renderer.render(context, element.transform, element.local_bounds, color);
        }
    }

    PbrtRenderResult PbrtDocument::render_final(const PbrtRenderSettings& settings) {
        if (settings.samples_per_pixel <= 0) throw std::runtime_error("PBRT samples per pixel must be positive");
        if (settings.thread_count <= 0) throw std::runtime_error("PBRT thread count must be positive");
        if (settings.resolution[0] <= 0 || settings.resolution[1] <= 0) throw std::runtime_error("PBRT render resolution must be positive");
        const std::string output_path = settings.output_path.data();
        if (output_path.empty()) throw std::runtime_error("PBRT output path must not be empty");

        pbrt::PBRTOptions options{};
        options.nThreads      = settings.thread_count;
        options.pixelSamples  = settings.samples_per_pixel;
        options.imageFile     = output_path;
        options.renderingSpace = pbrt::RenderingCoordinateSystem::CameraWorld;
        options.quiet         = true;
        options.useGPU        = settings.backend == PbrtPathTraceBackend::gpu;
        options.wavefront     = settings.backend == PbrtPathTraceBackend::wavefront;

        const auto started = std::chrono::steady_clock::now();
        bool initialized{};
        try {
            pbrt::InitPBRT(options);
            initialized = true;
            pbrt::BasicScene scene;
            pbrt::BasicSceneBuilder builder{&scene};
            for (const PbrtCommand& command : this->commands) {
                PbrtElement* element = command.element_id == 0 ? nullptr : this->find_element(command.element_id);
                const std::vector<PbrtParameter>& replay_parameters = element == nullptr ? command.parameters : element->parameters;
                const bool transform_override = element != nullptr && element->transform_override && (command.kind == PbrtCommandKind::shape || command.kind == PbrtCommandKind::light_source);
                if (transform_override) replay_transform_override(builder, *element);
                switch (command.kind) {
                    case PbrtCommandKind::option:
                        builder.Option(command.text[0], command.text[1], {});
                        break;
                    case PbrtCommandKind::identity:
                        builder.Identity({});
                        break;
                    case PbrtCommandKind::translate:
                        builder.Translate(command.values[0], command.values[1], command.values[2], {});
                        break;
                    case PbrtCommandKind::rotate:
                        builder.Rotate(command.values[0], command.values[1], command.values[2], command.values[3], {});
                        break;
                    case PbrtCommandKind::scale:
                        builder.Scale(command.values[0], command.values[1], command.values[2], {});
                        break;
                    case PbrtCommandKind::look_at:
                        builder.LookAt(command.values[0], command.values[1], command.values[2], command.values[3], command.values[4], command.values[5], command.values[6], command.values[7], command.values[8], {});
                        break;
                    case PbrtCommandKind::concat_transform: {
                        std::array<pbrt::Float, 16> values{};
                        for (std::size_t index = 0; index < values.size(); ++index) values[index] = command.values[index];
                        builder.ConcatTransform(values.data(), {});
                        break;
                    }
                    case PbrtCommandKind::transform: {
                        std::array<pbrt::Float, 16> values{};
                        for (std::size_t index = 0; index < values.size(); ++index) values[index] = command.values[index];
                        builder.Transform(values.data(), {});
                        break;
                    }
                    case PbrtCommandKind::coordinate_system:
                        builder.CoordinateSystem(command.text[0], {});
                        break;
                    case PbrtCommandKind::coord_sys_transform:
                        builder.CoordSysTransform(command.text[0], {});
                        break;
                    case PbrtCommandKind::active_transform_all:
                        builder.ActiveTransformAll({});
                        break;
                    case PbrtCommandKind::active_transform_end_time:
                        builder.ActiveTransformEndTime({});
                        break;
                    case PbrtCommandKind::active_transform_start_time:
                        builder.ActiveTransformStartTime({});
                        break;
                    case PbrtCommandKind::transform_times:
                        builder.TransformTimes(command.values[0], command.values[1], {});
                        break;
                    case PbrtCommandKind::color_space:
                        builder.ColorSpace(command.text[0], {});
                        break;
                    case PbrtCommandKind::pixel_filter:
                        builder.PixelFilter(command.text[0], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::film:
                        builder.Film(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::sampler:
                        builder.Sampler(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::accelerator:
                        builder.Accelerator(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::integrator:
                        builder.Integrator(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::camera:
                        builder.Camera(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::make_named_medium:
                        builder.MakeNamedMedium(command.text[0], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::medium_interface:
                        builder.MediumInterface(command.text[0], command.text[1], {});
                        break;
                    case PbrtCommandKind::world_begin:
                        builder.WorldBegin({});
                        break;
                    case PbrtCommandKind::attribute_begin:
                        builder.AttributeBegin({});
                        break;
                    case PbrtCommandKind::attribute_end:
                        builder.AttributeEnd({});
                        break;
                    case PbrtCommandKind::attribute:
                        builder.Attribute(command.text[0], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::texture:
                        builder.Texture(command.text[0], command.text[1], command.text[2], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::material:
                        builder.Material(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::make_named_material:
                        builder.MakeNamedMaterial(command.text[0], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::named_material:
                        builder.NamedMaterial(command.text[0], {});
                        break;
                    case PbrtCommandKind::light_source:
                        builder.LightSource(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::area_light_source:
                        builder.AreaLightSource(command.text[1], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::reverse_orientation:
                        builder.ReverseOrientation({});
                        break;
                    case PbrtCommandKind::shape:
                        builder.Shape(command.text[0], make_parsed_parameters(replay_parameters), {});
                        break;
                    case PbrtCommandKind::object_begin:
                        builder.ObjectBegin(command.text[0], {});
                        break;
                    case PbrtCommandKind::object_end:
                        builder.ObjectEnd({});
                        break;
                    case PbrtCommandKind::object_instance:
                        builder.ObjectInstance(command.text[0], {});
                        break;
                }
                if (transform_override) builder.AttributeEnd({});
            }
            builder.EndOfFiles();
            if (options.useGPU || options.wavefront)
                pbrt::RenderWavefront(scene);
            else
                pbrt::RenderCPU(scene);
            pbrt::CleanupPBRT();
        } catch (...) {
            if (initialized) pbrt::CleanupPBRT();
            throw;
        }
        const auto finished = std::chrono::steady_clock::now();
        PbrtRenderResult result{};
        result.success     = true;
        result.seconds     = std::chrono::duration<double>(finished - started).count();
        result.output_path = output_path;
        result.message     = "Render complete";
        return result;
    }

    const std::filesystem::path& PbrtDocument::path() const {
        return this->source_path;
    }

    std::size_t PbrtDocument::element_count() const {
        return this->elements.size();
    }

    std::size_t PbrtDocument::object_count() const {
        return static_cast<std::size_t>(std::ranges::count_if(this->elements, [](const PbrtElement& element) { return element.kind == PbrtElementKind::shape || element.kind == PbrtElementKind::instance; }));
    }

    PbrtDocumentStats PbrtDocument::stats() const {
        PbrtDocumentStats result{};
        result.commands = this->commands.size();
        for (const PbrtElement& element : this->elements) {
            if (element.kind == PbrtElementKind::camera) ++result.cameras;
            if (element.kind == PbrtElementKind::shape) ++result.shapes;
            if (element.kind == PbrtElementKind::material) ++result.materials;
            if (element.kind == PbrtElementKind::texture) ++result.textures;
            if (element.kind == PbrtElementKind::light) ++result.lights;
            if (element.kind == PbrtElementKind::medium) ++result.media;
            if (element.kind == PbrtElementKind::instance) ++result.instances;
            if (element.kind == PbrtElementKind::render_setting) ++result.render_settings;
        }
        return result;
    }

    bool PbrtDocument::has_selection() const {
        return this->selection.element_id != 0;
    }

    void PbrtDocument::clear_selection() {
        this->selection.element_id = 0;
    }

    void PbrtDocument::select_element(const std::uint64_t element_id) {
        if (this->find_element(element_id) == nullptr) throw std::runtime_error("Cannot select a missing PBRT element");
        this->selection.element_id = element_id;
    }

    PbrtElement& PbrtDocument::selected_element() {
        PbrtElement* element = this->find_element(this->selection.element_id);
        if (element == nullptr) throw std::runtime_error("PBRT selection is empty or invalid");
        return *element;
    }

    const PbrtElement& PbrtDocument::selected_element() const {
        const PbrtElement* element = this->find_element(this->selection.element_id);
        if (element == nullptr) throw std::runtime_error("PBRT selection is empty or invalid");
        return *element;
    }

    BoundingBoxBounds PbrtDocument::world_bounds() const {
        bool initialized{};
        BoundingBoxBounds result{};
        for (const PbrtElement& element : this->elements) {
            if (element.kind != PbrtElementKind::shape && element.kind != PbrtElementKind::light) continue;
            const BoundingBoxBounds bounds = transformed_bounds(element.transform, element.local_bounds);
            if (!initialized) {
                result      = bounds;
                initialized = true;
            } else {
                expand_bounds(result, bounds);
            }
        }
        if (!initialized) throw std::runtime_error("PBRT document has no previewable bounds");
        return result;
    }

    BoundingBoxBounds PbrtDocument::selected_world_bounds() const {
        const PbrtElement& element = this->selected_element();
        return transformed_bounds(element.transform, element.local_bounds);
    }

    void PbrtDocument::draw_scene_browser_ui() {
        const PbrtDocumentStats document_stats = this->stats();
        if (ImGui::BeginTable("PbrtSceneSummary", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            const auto row = [](const char* label, const std::size_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", value);
            };
            row("Elements", this->elements.size());
            row("Shapes", document_stats.shapes);
            row("Materials", document_stats.materials);
            row("Textures", document_stats.textures);
            row("Lights", document_stats.lights);
            row("Media", document_stats.media);
            row("Commands", document_stats.commands);
            ImGui::EndTable();
        }

        if (!this->source_path.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", this->source_path.string().c_str());
        }

        ImGui::Separator();
        if (ImGui::BeginTabBar("PbrtSceneBrowserTabs")) {
            if (ImGui::BeginTabItem("Scene List")) {
                for (const PbrtElementKind kind : {PbrtElementKind::camera, PbrtElementKind::shape, PbrtElementKind::material, PbrtElementKind::texture, PbrtElementKind::light, PbrtElementKind::medium, PbrtElementKind::instance, PbrtElementKind::render_setting}) {
                    if (!ImGui::CollapsingHeader(element_kind_label(kind))) continue;
                    for (PbrtElement& element : this->elements) {
                        if (element.kind != kind) continue;
                        const bool selected  = this->selection.element_id == element.id;
                        const std::string id = std::to_string(element.id);
                        const std::string label = std::format("{}  {}  {}##PbrtElement{}", element_kind_label(element.kind), element.name, element.detail, id);
                        if (ImGui::Selectable(label.c_str(), selected)) this->selection.element_id = element.id;
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                ImGui::Text("Dirty: %s", this->dirty ? "yes" : "no");
                ImGui::Text("Next id: %llu", static_cast<unsigned long long>(this->next_element_id));
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void PbrtDocument::draw_selected_inspector_ui() {
        if (!this->has_selection()) {
            ImGui::TextDisabled("No selection");
            return;
        }
        PbrtElement& element = this->selected_element();
        ImGui::Text("%s: %s", element_kind_label(element.kind), element.name.c_str());
        if (!element.detail.empty()) ImGui::TextDisabled("%s", element.detail.c_str());
        ImGui::Separator();

        if (element.kind == PbrtElementKind::shape || element.kind == PbrtElementKind::light) {
            if (ImGui::CollapsingHeader("Preview Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                std::array<float, 3> translation{element.transform[12], element.transform[13], element.transform[14]};
                if (ImGui::InputFloat3("Translation", translation.data(), "%.3f")) {
                    element.transform[12]       = translation[0];
                    element.transform[13]       = translation[1];
                    element.transform[14]       = translation[2];
                    element.transform_override  = true;
                    this->dirty                 = true;
                }
            }
        }

        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (element.parameters.empty()) {
                ImGui::TextDisabled("No parameters");
            } else if (ImGui::BeginTable("PbrtParameterTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                for (PbrtParameter& parameter : element.parameters) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(parameter.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", parameter.type.c_str());
                    ImGui::TableNextColumn();
                    ImGui::PushID(&parameter);
                    bool changed = false;
                    for (std::size_t index = 0; index < parameter.floats.size(); ++index) {
                        ImGui::SetNextItemWidth(-1.0f);
                        changed |= ImGui::InputFloat(std::format("##float{}", index).c_str(), &parameter.floats[index], 0.0f, 0.0f, "%.6f");
                    }
                    for (std::size_t index = 0; index < parameter.ints.size(); ++index) {
                        ImGui::SetNextItemWidth(-1.0f);
                        changed |= ImGui::InputInt(std::format("##int{}", index).c_str(), &parameter.ints[index]);
                    }
                    for (std::size_t index = 0; index < parameter.strings.size(); ++index) {
                        std::array<char, 512> buffer{};
                        const std::size_t copy_size = std::min(buffer.size() - 1u, parameter.strings[index].size());
                        std::memcpy(buffer.data(), parameter.strings[index].data(), copy_size);
                        ImGui::SetNextItemWidth(-1.0f);
                        if (ImGui::InputText(std::format("##string{}", index).c_str(), buffer.data(), buffer.size())) {
                            parameter.strings[index] = buffer.data();
                            changed                  = true;
                        }
                    }
                    for (std::size_t index = 0; index < parameter.bools.size(); ++index) {
                        bool value = parameter.bools[index];
                        if (ImGui::Checkbox(std::format("##bool{}", index).c_str(), &value)) {
                            parameter.bools[index] = value;
                            changed                = true;
                        }
                    }
                    if (changed) {
                        if (element.kind == PbrtElementKind::shape) element.local_bounds = preview_bounds_for_shape(element.type, element.parameters);
                        this->dirty = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }

    PbrtElement* PbrtDocument::find_element(const std::uint64_t element_id) {
        const auto iterator = std::ranges::find(this->elements, element_id, &PbrtElement::id);
        if (iterator == this->elements.end()) return nullptr;
        return &*iterator;
    }

    const PbrtElement* PbrtDocument::find_element(const std::uint64_t element_id) const {
        const auto iterator = std::ranges::find(this->elements, element_id, &PbrtElement::id);
        if (iterator == this->elements.end()) return nullptr;
        return &*iterator;
    }
} // namespace xayah
