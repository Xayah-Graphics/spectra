#include <slang-com-ptr.h>
#include <slang.h>

#include <process.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
    struct ReflectedField {
        std::string name{};
        std::string cpp_type{};
        std::size_t offset{};
    };

    struct ReflectedType {
        std::string name{};
        std::size_t size{};
        std::size_t alignment{};
        std::vector<ReflectedField> fields{};
    };

    struct ShaderEntry {
        std::string output{};
        std::string module{};
        std::string entry{};
    };

    [[nodiscard]] std::string diagnostics(
        slang::IBlob* blob) {
        if (!blob) return {};
        return std::string{
            static_cast<const char*>(
                blob->getBufferPointer()),
            blob->getBufferSize()};
    }

    void require(
        const SlangResult result,
        std::string_view operation,
        slang::IBlob* diagnostic_blob = nullptr) {
        if (SLANG_SUCCEEDED(result)) return;
        throw std::runtime_error(
            std::format(
                "{} failed\n{}",
                operation,
                diagnostics(
                    diagnostic_blob)));
    }

    [[nodiscard]] std::string scalar_name(
        const slang::TypeReflection::ScalarType scalar) {
        switch (scalar) {
        case slang::TypeReflection::ScalarType::Bool:
            return "std::uint32_t";
        case slang::TypeReflection::ScalarType::Int32:
            return "std::int32_t";
        case slang::TypeReflection::ScalarType::UInt32:
            return "std::uint32_t";
        case slang::TypeReflection::ScalarType::Int64:
            return "std::int64_t";
        case slang::TypeReflection::ScalarType::UInt64:
            return "std::uint64_t";
        case slang::TypeReflection::ScalarType::Float32:
            return "float";
        default:
            throw std::runtime_error(
                "Shader ABI contains an unsupported scalar type");
        }
    }

    [[nodiscard]] std::string cpp_type(
        slang::TypeReflection* type) {
        const slang::TypeReflection::Kind kind =
            type->getKind();
        if (kind == slang::TypeReflection::Kind::Scalar)
            return scalar_name(
                type->getScalarType());
        if (kind == slang::TypeReflection::Kind::Vector)
            return std::format(
                "std::array<{}, {}>",
                scalar_name(
                    type->getScalarType()),
                type->getElementCount());
        if (kind == slang::TypeReflection::Kind::Array)
            return std::format(
                "std::array<{}, {}>",
                cpp_type(
                    type->getElementType()),
                type->getElementCount());
        const std::string_view name =
            type->getName()
                ? type->getName()
                : "";
        if (name.starts_with("DescriptorHandle"))
            return "DescriptorHandle";
        if (
            kind ==
                slang::TypeReflection::Kind::Struct ||
            kind ==
                slang::TypeReflection::Kind::Specialized)
            return std::string{name};
        throw std::runtime_error(
            std::format(
                "Shader ABI type '{}' has unsupported reflection kind {}",
                name,
                static_cast<int>(kind)));
    }

    [[nodiscard]] std::vector<std::array<std::string, 2>>
    load_type_list(
        const std::filesystem::path& path) {
        std::ifstream stream{path};
        if (!stream)
            throw std::runtime_error(
                std::format(
                    "Cannot read shader ABI type list: {}",
                    path.string()));
        std::vector<std::array<std::string, 2>>
            entries{};
        std::string line{};
        while (std::getline(stream, line)) {
            if (
                line.empty() ||
                line.front() == '#')
                continue;
            std::istringstream tokens{line};
            std::array<std::string, 2> entry{};
            std::string trailing{};
            if (
                !(tokens >> entry[0] >> entry[1]) ||
                tokens >> trailing)
                throw std::runtime_error(
                    std::format(
                        "Invalid shader ABI type entry: {}",
                        line));
            entries.push_back(
                std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] std::vector<ShaderEntry>
    load_shader_list(
        const std::filesystem::path& path) {
        std::ifstream stream{path};
        if (!stream)
            throw std::runtime_error(
                std::format(
                    "Cannot read shader entry list: {}",
                    path.string()));
        std::vector<ShaderEntry> entries{};
        std::string line{};
        while (std::getline(stream, line)) {
            if (
                line.empty() ||
                line.front() == '#')
                continue;
            std::istringstream tokens{line};
            ShaderEntry entry{};
            std::string trailing{};
            if (
                !(tokens >>
                  entry.output >>
                  entry.module >>
                  entry.entry) ||
                tokens >> trailing)
                throw std::runtime_error(
                    std::format(
                        "Invalid shader entry: {}",
                        line));
            entries.push_back(
                std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] std::string generate_module(
        slang::ISession& session,
        const std::vector<
            std::array<std::string, 2>>& entries) {
        std::map<std::string, slang::IModule*>
            modules{};
        std::vector<ReflectedType> types{};
        for (const std::array<std::string, 2>& entry :
             entries) {
            slang::IModule*& module =
                modules[entry[0]];
            if (!module) {
                Slang::ComPtr<slang::IBlob>
                    diagnostic_blob{};
                module =
                    session.loadModule(
                        entry[0].c_str(),
                        diagnostic_blob.writeRef());
                if (!module)
                    throw std::runtime_error(
                        std::format(
                            "Loading Slang module '{}' failed\n{}",
                            entry[0],
                            diagnostics(
                                diagnostic_blob)));
            }
            Slang::ComPtr<slang::IBlob>
                diagnostic_blob{};
            slang::ProgramLayout* program_layout =
                module->getLayout(
                    0,
                    diagnostic_blob.writeRef());
            if (!program_layout)
                throw std::runtime_error(
                    std::format(
                        "Reflecting Slang module '{}' failed\n{}",
                        entry[0],
                        diagnostics(
                            diagnostic_blob)));
            slang::TypeReflection* type =
                program_layout->findTypeByName(
                    entry[1].c_str());
            if (!type)
                throw std::runtime_error(
                    std::format(
                        "Slang type '{}.{}' is not reflected",
                        entry[0],
                        entry[1]));
            slang::TypeLayoutReflection* layout =
                program_layout->getTypeLayout(
                    type,
                    slang::LayoutRules::
                        DefaultStructuredBuffer);
            if (!layout)
                throw std::runtime_error(
                    std::format(
                        "Laying out Slang type '{}.{}' failed",
                        entry[0],
                        entry[1]));
            ReflectedType reflected{
                entry[1],
                layout->getSize(),
                static_cast<std::size_t>(
                    layout->getAlignment()),
            };
            for (unsigned index = 0;
                 index != layout->getFieldCount();
                 ++index) {
                slang::VariableLayoutReflection*
                    field =
                        layout->getFieldByIndex(
                            index);
                reflected.fields.push_back(
                    ReflectedField{
                        field->getName(),
                        cpp_type(
                            field->getType()),
                        field->getOffset(),
                    });
            }
            types.push_back(
                std::move(reflected));
        }

        std::ostringstream output{};
        output
            << "module;\n"
            << "#include <cstddef>\n\n"
            << "export module spectra.pathtracer.abi;\n\n"
            << "export import spectra;\n"
            << "import std;\n\n"
            << "namespace spectra::pathtracer {\n";
        for (const ReflectedType& type : types) {
            output
                << "    export struct alignas("
                << type.alignment
                << ") "
                << type.name
                << " {\n";
            for (const ReflectedField& field :
                 type.fields)
                output
                    << "        "
                    << field.cpp_type
                    << " "
                    << field.name
                    << "{};\n";
            output
                << "    };\n\n";
        }
        for (const ReflectedType& type : types) {
            output
                << "    static_assert(sizeof("
                << type.name
                << ") == "
                << type.size
                << ");\n"
                << "    static_assert(alignof("
                << type.name
                << ") == "
                << type.alignment
                << ");\n";
            for (const ReflectedField& field :
                 type.fields)
                output
                    << "    static_assert(offsetof("
                    << type.name
                    << ", "
                    << field.name
                    << ") == "
                    << field.offset
                    << ");\n";
            output << "\n";
        }
        output << "} // namespace spectra::pathtracer\n";
        return std::move(output).str();
    }

    void write_if_different(
        const std::filesystem::path& path,
        const void* contents,
        const std::size_t size,
        std::string_view description) {
        std::ifstream existing_stream{
            path,
            std::ios::binary};
        const std::string existing{
            std::istreambuf_iterator<char>{
                existing_stream},
            std::istreambuf_iterator<char>{}};
        if (
            existing.size() == size &&
            (
                size == 0 ||
                std::memcmp(
                    existing.data(),
                    contents,
                    size) == 0))
            return;
        std::filesystem::create_directories(
            path.parent_path());
        std::ofstream stream{
            path,
            std::ios::binary |
                std::ios::trunc};
        stream.write(
            static_cast<const char*>(contents),
            static_cast<std::streamsize>(size));
        if (!stream)
            throw std::runtime_error(
                std::format(
                    "Cannot write {}: {}",
                    description,
                    path.string()));
    }

    void compile_shader(
        slang::ISession& session,
        const ShaderEntry& entry,
        const std::filesystem::path& output_directory,
        const std::filesystem::path& spirv_validator) {
        Slang::ComPtr<slang::IBlob>
            diagnostic_blob{};
        slang::IModule* module =
            session.loadModule(
                entry.module.c_str(),
                diagnostic_blob.writeRef());
        if (!module)
            throw std::runtime_error(
                std::format(
                    "Loading Slang module '{}' failed\n{}",
                    entry.module,
                    diagnostics(
                        diagnostic_blob)));

        Slang::ComPtr<slang::IEntryPoint>
            entry_point{};
        require(
            module->findEntryPointByName(
                entry.entry.c_str(),
                entry_point.writeRef()),
            std::format(
                "Finding Slang entry point '{}.{}'",
                entry.module,
                entry.entry));
        std::array<slang::IComponentType*, 2>
            components{
                module,
                entry_point.get(),
            };
        Slang::ComPtr<slang::IComponentType>
            composite{};
        require(
            session.createCompositeComponentType(
                components.data(),
                static_cast<SlangInt>(
                    components.size()),
                composite.writeRef(),
                diagnostic_blob.writeRef()),
            std::format(
                "Composing Slang entry point '{}.{}'",
                entry.module,
                entry.entry),
            diagnostic_blob);
        Slang::ComPtr<slang::IComponentType>
            linked{};
        require(
            composite->link(
                linked.writeRef(),
                diagnostic_blob.writeRef()),
            std::format(
                "Linking Slang entry point '{}.{}'",
                entry.module,
                entry.entry),
            diagnostic_blob);
        Slang::ComPtr<slang::IBlob> code{};
        require(
            linked->getEntryPointCode(
                0,
                0,
                code.writeRef(),
                diagnostic_blob.writeRef()),
            std::format(
                "Generating SPIR-V for '{}.{}'",
                entry.module,
                entry.entry),
            diagnostic_blob);
        if (
            diagnostic_blob &&
            diagnostic_blob->getBufferSize() != 0)
            std::fprintf(
                stderr,
                "%s",
                diagnostics(
                    diagnostic_blob).c_str());

        const std::filesystem::path output_path =
            output_directory /
            (entry.output + ".spv");
        write_if_different(
            output_path,
            code->getBufferPointer(),
            code->getBufferSize(),
            "SPIR-V shader");

        const std::wstring validator =
            spirv_validator.wstring();
        std::vector<std::wstring> arguments{
            validator,
            L"--target-env",
            L"vulkan1.4",
            output_path.wstring(),
        };
        std::vector<const wchar_t*> argument_pointers{};
        argument_pointers.reserve(
            arguments.size() +
            1);
        for (const std::wstring& argument :
             arguments)
            argument_pointers.push_back(
                argument.c_str());
        argument_pointers.push_back(
            nullptr);
        const std::intptr_t validation_result =
            _wspawnv(
                _P_WAIT,
                validator.c_str(),
                argument_pointers.data());
        if (validation_result == -1)
            throw std::runtime_error(
                std::format(
                    "Starting SPIR-V validator failed for '{}': {}",
                    output_path.string(),
                    std::strerror(errno)));
        if (validation_result != 0)
            throw std::runtime_error(
                std::format(
                    "SPIR-V validation failed for '{}' with exit code {}",
                    output_path.string(),
                    validation_result));
    }
}

int main(
    const int argument_count,
    const char* const* arguments) {
    try {
        if (argument_count < 7)
            throw std::runtime_error(
                "Usage: spectra_shader_build <abi.types> <abi.ixx> <shaders.entries> <output-directory> <spirv-val> <shader-search-path>...");

        Slang::ComPtr<slang::IGlobalSession>
            global_session{};
        require(
            slang::createGlobalSession(
                global_session.writeRef()),
            "Creating Slang global session");

        std::vector<const char*> options{
            "-target",
            "spirv",
            "-profile",
            "sm_6_6",
            "-emit-spirv-directly",
            "-matrix-layout-row-major",
            "-fp-mode",
            "precise",
            "-fvk-use-c-layout",
            "-fvk-use-entrypoint-name",
            "-capability",
            "SPIRV_1_6+spvDescriptorHeapEXT+spvMeshShadingEXT+spvRayTracingKHR+spvRayTracingPositionFetchKHR",
            "-spirv-unified-descriptor-heap-stride",
        };
        std::vector<const char*> search_paths{};
        search_paths.reserve(
            argument_count -
            6);
        for (int index = 6;
             index != argument_count;
             ++index)
            search_paths.push_back(
                arguments[index]);
        slang::SessionDesc default_session_description{};
        Slang::ComPtr<ISlangUnknown>
            default_session_allocation{};
        require(
            global_session
                ->parseCommandLineArguments(
                    static_cast<int>(
                        options.size()),
                    options.data(),
                    &default_session_description,
                    default_session_allocation.writeRef()),
            "Parsing default Slang compiler options");
        default_session_description.searchPaths =
            search_paths.data();
        default_session_description.searchPathCount =
            static_cast<SlangInt>(
                search_paths.size());
        Slang::ComPtr<slang::ISession>
            default_session{};
        require(
            global_session->createSession(
                default_session_description,
                default_session.writeRef()),
            "Creating default Slang compile session");

        std::vector<const char*>
            optimized_options = options;
        optimized_options.push_back("-O2");
        slang::SessionDesc optimized_session_description{};
        Slang::ComPtr<ISlangUnknown>
            optimized_session_allocation{};
        require(
            global_session
                ->parseCommandLineArguments(
                    static_cast<int>(
                        optimized_options.size()),
                    optimized_options.data(),
                    &optimized_session_description,
                    optimized_session_allocation.writeRef()),
            "Parsing optimized Slang compiler options");
        optimized_session_description.searchPaths =
            search_paths.data();
        optimized_session_description.searchPathCount =
            static_cast<SlangInt>(
                search_paths.size());
        Slang::ComPtr<slang::ISession>
            optimized_session{};
        require(
            global_session->createSession(
                optimized_session_description,
                optimized_session.writeRef()),
            "Creating optimized Slang compile session");

        const std::string abi_module =
            generate_module(
                *default_session,
                load_type_list(
                    arguments[1]));
        write_if_different(
            arguments[2],
            abi_module.data(),
            abi_module.size(),
            "generated shader ABI module");

        const std::vector<ShaderEntry>
            entries =
                load_shader_list(
                    arguments[3]);
        for (const ShaderEntry& entry :
             entries)
            compile_shader(
                entry.output ==
                        "path_evaluate_surface_textures"
                    ? *default_session
                    : *optimized_session,
                entry,
                arguments[4],
                arguments[5]);
    }
    catch (const std::exception& error) {
        std::fprintf(
            stderr,
            "%s\n",
            error.what());
        return 1;
    }
    return 0;
}
