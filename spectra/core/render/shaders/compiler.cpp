#include <slang-com-ptr.h>
#include <slang.h>

import std;

namespace {
    struct ReflectedField {
        std::string name{};
        std::string cpp_type{};
        std::size_t offset{};
        std::size_t alignment{};
    };

    struct ReflectedType {
        std::string name{};
        std::size_t size{};
        std::size_t alignment{};
        std::vector<ReflectedField> fields{};
    };

    struct ShaderEntry {
        std::string output_name{};
        std::string module_name{};
        std::string entry_point{};
        bool optimized{};
        std::string category{};
    };

    [[nodiscard]] std::string diagnostic_text(slang::IBlob* blob) {
        if (!blob) return {};
        return std::string{static_cast<const char*>(blob->getBufferPointer()), blob->getBufferSize()};
    }

    void require_slang_success(const SlangResult result, std::string_view operation, slang::IBlob* diagnostic_blob = nullptr) {
        if (SLANG_SUCCEEDED(result)) return;
        throw std::runtime_error(std::format("{} failed\n{}", operation, diagnostic_text(diagnostic_blob)));
    }

    [[nodiscard]] std::string cpp_scalar_name(const slang::TypeReflection::ScalarType scalar) {
        switch (scalar) {
        case slang::TypeReflection::ScalarType::Bool: return "std::uint32_t";
        case slang::TypeReflection::ScalarType::Int32: return "std::int32_t";
        case slang::TypeReflection::ScalarType::UInt32: return "std::uint32_t";
        case slang::TypeReflection::ScalarType::Int64: return "std::int64_t";
        case slang::TypeReflection::ScalarType::UInt64: return "std::uint64_t";
        case slang::TypeReflection::ScalarType::Float32: return "float";
        default: throw std::runtime_error("Shader ABI contains an unsupported scalar type");
        }
    }

    [[nodiscard]] std::string reflected_cpp_type(slang::TypeReflection* type) {
        const slang::TypeReflection::Kind kind = type->getKind();
        if (kind == slang::TypeReflection::Kind::Scalar) return cpp_scalar_name(type->getScalarType());
        if (kind == slang::TypeReflection::Kind::Vector) return std::format("std::array<{}, {}>", cpp_scalar_name(type->getScalarType()), type->getElementCount());
        if (kind == slang::TypeReflection::Kind::Array) return std::format("std::array<{}, {}>", reflected_cpp_type(type->getElementType()), type->getElementCount());
        const std::string_view name = type->getName() ? type->getName() : "";
        if (name.starts_with("DescriptorHandle")) return "DescriptorHandle";
        if (name == "UntypedDescriptorHandle") return "DescriptorHandle";
        if (kind == slang::TypeReflection::Kind::Struct || kind == slang::TypeReflection::Kind::Specialized) return std::string{name};
        throw std::runtime_error(std::format("Shader ABI type '{}' has unsupported reflection kind {}", name, static_cast<int>(kind)));
    }

    [[nodiscard]] std::vector<std::array<std::string, 2>> load_abi_type_entries(const std::filesystem::path& path) {
        std::ifstream stream{path};
        if (!stream) throw std::runtime_error(std::format("Cannot read shader ABI type list: {}", path.string()));
        std::vector<std::array<std::string, 2>> entries{};
        std::string line{};
        while (std::getline(stream, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream tokens{line};
            std::array<std::string, 2> entry{};
            std::string trailing{};
            if (!(tokens >> entry[0] >> entry[1]) || tokens >> trailing) throw std::runtime_error(std::format("Invalid shader ABI type entry: {}", line));
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] std::vector<ShaderEntry> load_shader_entries(const std::filesystem::path& path) {
        std::ifstream stream{path};
        if (!stream) throw std::runtime_error(std::format("Cannot read shader entry list: {}", path.string()));
        std::vector<ShaderEntry> entries{};
        std::string line{};
        while (std::getline(stream, line)) {
            if (line.empty() || line.front() == '#') continue;
            std::istringstream tokens{line};
            ShaderEntry entry{};
            std::string optimization{};
            std::string trailing{};
            if (!(tokens >> entry.output_name >> entry.module_name >> entry.entry_point >> optimization >> entry.category) || tokens >> trailing || (optimization != "precise" && optimization != "optimized") || (entry.category != "runtime" && entry.category != "path-compute" && entry.category != "path-ray" && entry.category != "editor")) throw std::runtime_error(std::format("Invalid shader entry: {}", line));
            entry.optimized = optimization == "optimized";
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    [[nodiscard]] std::string generate_abi_module(slang::ISession& session, const std::vector<std::array<std::string, 2>>& type_entries, const std::string_view module_name, const std::string_view namespace_name) {
        std::map<std::string, slang::IModule*> modules{};
        std::map<std::string, Slang::ComPtr<slang::IComponentType>> linked_modules{};
        std::map<std::string, slang::ProgramLayout*> program_layouts{};
        std::vector<ReflectedType> types{};
        for (const std::array<std::string, 2>& entry : type_entries) {
            slang::IModule*& module = modules[entry[0]];
            if (!module) {
                Slang::ComPtr<slang::IBlob> diagnostic_blob{};
                module = session.loadModule(entry[0].c_str(), diagnostic_blob.writeRef());
                if (!module) throw std::runtime_error(std::format("Loading Slang module '{}' failed\n{}", entry[0], diagnostic_text(diagnostic_blob)));
                require_slang_success(module->link(linked_modules[entry[0]].writeRef(), diagnostic_blob.writeRef()), std::format("Linking Slang module '{}'", entry[0]), diagnostic_blob);
                program_layouts[entry[0]] = linked_modules[entry[0]]->getLayout(0, diagnostic_blob.writeRef());
                if (!program_layouts[entry[0]]) throw std::runtime_error(std::format("Reflecting Slang module '{}' failed\n{}", entry[0], diagnostic_text(diagnostic_blob)));
            }
            slang::TypeReflection* type = program_layouts[entry[0]]->findTypeByName(entry[1].c_str());
            if (!type) throw std::runtime_error(std::format("Slang type '{}.{}' is not reflected", entry[0], entry[1]));
            Slang::ComPtr<slang::IBlob> layout_diagnostics{};
            slang::TypeReflection* container = session.getContainerType(type, slang::ContainerType::StructuredBuffer, layout_diagnostics.writeRef());
            if (!container) throw std::runtime_error(std::format("Creating StructuredBuffer layout for Slang type '{}.{}' failed\n{}", entry[0], entry[1], diagnostic_text(layout_diagnostics)));
            slang::TypeLayoutReflection* container_layout = session.getTypeLayout(container, 0, slang::LayoutRules::Default, layout_diagnostics.writeRef());
            if (!container_layout) throw std::runtime_error(std::format("Laying out StructuredBuffer for Slang type '{}.{}' failed\n{}", entry[0], entry[1], diagnostic_text(layout_diagnostics)));
            slang::TypeLayoutReflection* layout = container_layout->getElementTypeLayout();
            if (!layout) throw std::runtime_error(std::format("Laying out Slang type '{}.{}' failed", entry[0], entry[1]));
            ReflectedType reflected{
                entry[1],
                layout->getSize(),
                static_cast<std::size_t>(layout->getAlignment()),
            };
            for (unsigned index = 0; index != layout->getFieldCount(); ++index) {
                slang::VariableLayoutReflection* field = layout->getFieldByIndex(index);
                reflected.fields.push_back(ReflectedField{
                    field->getName(),
                    reflected_cpp_type(field->getType()),
                    field->getOffset(),
                    static_cast<std::size_t>(field->getTypeLayout()->getAlignment()),
                });
            }
            types.push_back(std::move(reflected));
        }

        std::ostringstream output{};
        output << "module;\n"
               << "#include <cstddef>\n\n"
               << "export module " << module_name << ";\n\n"
               << "export import spectra.runtime.resources;\n"
               << "import std;\n\n"
               << "namespace " << namespace_name << " {\n";
        for (const ReflectedType& type : types) {
            output << "    export struct alignas(" << type.alignment << ") " << type.name << " {\n";
            for (const ReflectedField& field : type.fields) output << "        alignas(" << field.alignment << ") " << field.cpp_type << " " << field.name << "{};\n";
            output << "    };\n\n";
        }
        for (const ReflectedType& type : types) {
            output << "    static_assert(sizeof(" << type.name << ") == " << type.size << ");\n"
                   << "    static_assert(alignof(" << type.name << ") == " << type.alignment << ");\n";
            for (const ReflectedField& field : type.fields) output << "    static_assert(offsetof(" << type.name << ", " << field.name << ") == " << field.offset << ");\n";
            output << "\n";
        }
        output << "} // namespace " << namespace_name << "\n";
        return std::move(output).str();
    }

    [[nodiscard]] std::string generate_shader_entries_module(const std::vector<ShaderEntry>& entries) {
        std::ostringstream output{};
        output << "export module spectra.render.pathtracer.shader_entries;\n\n"
               << "import std;\n\n"
               << "namespace spectra {\n"
               << "    export struct PathTracerShaderEntry {\n"
               << "        std::string_view file;\n"
               << "        std::string_view entry;\n"
               << "    };\n\n"
               << "    export inline constexpr std::array path_compute_shader_entries{\n";
        for (const ShaderEntry& entry : entries) if (entry.category == "path-compute") output << "        PathTracerShaderEntry{\"" << entry.output_name << ".spv\", \"" << entry.entry_point << "\"},\n";
        output << "    };\n\n"
               << "    export inline constexpr std::array path_ray_shader_entries{\n";
        for (const ShaderEntry& entry : entries) if (entry.category == "path-ray") output << "        PathTracerShaderEntry{\"" << entry.output_name << ".spv\", \"" << entry.entry_point << "\"},\n";
        output << "    };\n"
               << "} // namespace spectra\n";
        return std::move(output).str();
    }

    void write_if_different(const std::filesystem::path& path, const void* contents, const std::size_t size, std::string_view description) {
        std::ifstream existing_stream{path, std::ios::binary};
        const std::string existing{std::istreambuf_iterator<char>{existing_stream}, std::istreambuf_iterator<char>{}};
        if (existing.size() == size && (size == 0 || std::memcmp(existing.data(), contents, size) == 0)) return;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream.write(static_cast<const char*>(contents), static_cast<std::streamsize>(size));
        if (!stream) throw std::runtime_error(std::format("Cannot write {}: {}", description, path.string()));
    }

    void compile_shader(slang::ISession& session, const ShaderEntry& entry, const std::filesystem::path& output_directory) {
        Slang::ComPtr<slang::IBlob> diagnostic_blob{};
        slang::IModule* module = session.loadModule(entry.module_name.c_str(), diagnostic_blob.writeRef());
        if (!module) throw std::runtime_error(std::format("Loading Slang module '{}' failed\n{}", entry.module_name, diagnostic_text(diagnostic_blob)));

        Slang::ComPtr<slang::IEntryPoint> entry_point{};
        require_slang_success(module->findEntryPointByName(entry.entry_point.c_str(), entry_point.writeRef()), std::format("Finding Slang entry point '{}.{}'", entry.module_name, entry.entry_point));
        std::array<slang::IComponentType*, 2> components{
            module,
            entry_point.get(),
        };
        Slang::ComPtr<slang::IComponentType> composite{};
        require_slang_success(session.createCompositeComponentType(components.data(), static_cast<SlangInt>(components.size()), composite.writeRef(), diagnostic_blob.writeRef()), std::format("Composing Slang entry point '{}.{}'", entry.module_name, entry.entry_point), diagnostic_blob);
        Slang::ComPtr<slang::IComponentType> linked{};
        require_slang_success(composite->link(linked.writeRef(), diagnostic_blob.writeRef()), std::format("Linking Slang entry point '{}.{}'", entry.module_name, entry.entry_point), diagnostic_blob);
        Slang::ComPtr<slang::IBlob> code{};
        require_slang_success(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostic_blob.writeRef()), std::format("Generating SPIR-V for '{}.{}'", entry.module_name, entry.entry_point), diagnostic_blob);
        if (diagnostic_blob && diagnostic_blob->getBufferSize() != 0) std::print(std::cerr, "{}", diagnostic_text(diagnostic_blob));

        const std::filesystem::path output_path = output_directory / (entry.category.starts_with("path-") ? "pathtracer" : "runtime") / (entry.output_name + ".spv");
        write_if_different(output_path, code->getBufferPointer(), code->getBufferSize(), "SPIR-V shader");
    }
} // namespace

int main(const int argument_count, const char* const* arguments) {
    try {
        Slang::ComPtr<slang::IGlobalSession> global_session{};
        require_slang_success(slang::createGlobalSession(global_session.writeRef()), "Creating Slang global session");

        std::vector<const char*> options{
            "-emit-spirv-directly",
            "-matrix-layout-row-major",
            "-fp-mode",
            "precise",
            "-fvk-use-c-layout",
            "-fvk-use-entrypoint-name",
            "-capability",
            "SPIRV_1_6+spvDescriptorHeapEXT+spvMeshShadingEXT+spvRayTracingKHR+spvRayTracingPositionFetchKHR+spvCooperativeVectorNV",
            "-spirv-unified-descriptor-heap-stride",
        };
        std::vector<const char*> search_paths{};
        search_paths.reserve(argument_count - 15);
        for (int index = 15; index != argument_count; ++index) search_paths.push_back(arguments[index]);
        slang::TargetDesc target_description{
            .format  = SLANG_SPIRV,
            .profile = global_session->findProfile("sm_6_6"),
        };
        slang::SessionDesc default_session_description{};
        Slang::ComPtr<ISlangUnknown> default_session_allocation{};
        require_slang_success(global_session->parseCommandLineArguments(static_cast<int>(options.size()), options.data(), &default_session_description, default_session_allocation.writeRef()), "Parsing default Slang compiler options");
        default_session_description.targets         = &target_description;
        default_session_description.targetCount     = 1;
        default_session_description.searchPaths     = search_paths.data();
        default_session_description.searchPathCount = static_cast<SlangInt>(search_paths.size());
        Slang::ComPtr<slang::ISession> default_session{};
        require_slang_success(global_session->createSession(default_session_description, default_session.writeRef()), "Creating default Slang compile session");

        std::vector<const char*> optimized_options = options;
        optimized_options.push_back("-O2");
        slang::SessionDesc optimized_session_description{};
        Slang::ComPtr<ISlangUnknown> optimized_session_allocation{};
        require_slang_success(global_session->parseCommandLineArguments(static_cast<int>(optimized_options.size()), optimized_options.data(), &optimized_session_description, optimized_session_allocation.writeRef()), "Parsing optimized Slang compiler options");
        optimized_session_description.targets         = &target_description;
        optimized_session_description.targetCount     = 1;
        optimized_session_description.searchPaths     = search_paths.data();
        optimized_session_description.searchPathCount = static_cast<SlangInt>(search_paths.size());
        Slang::ComPtr<slang::ISession> optimized_session{};
        require_slang_success(global_session->createSession(optimized_session_description, optimized_session.writeRef()), "Creating optimized Slang compile session");

        const std::string path_abi_module = generate_abi_module(*default_session, load_abi_type_entries(arguments[1]), "spectra.render.pathtracer.abi", "spectra::pathtracer");
        write_if_different(arguments[2], path_abi_module.data(), path_abi_module.size(), "generated Path Tracer shader ABI module");
        const std::string raster_abi_module = generate_abi_module(*default_session, load_abi_type_entries(arguments[3]), "spectra.render.rasterizer.abi", "spectra");
        write_if_different(arguments[4], raster_abi_module.data(), raster_abi_module.size(), "generated Rasterizer shader ABI module");
        const std::string runtime_abi_module = generate_abi_module(*default_session, load_abi_type_entries(arguments[5]), "spectra.render.runtime_abi", "spectra");
        write_if_different(arguments[6], runtime_abi_module.data(), runtime_abi_module.size(), "generated runtime shader ABI module");
        if (std::string_view{arguments[14]} == "ON") {
            const std::string editor_abi_module = generate_abi_module(*default_session, load_abi_type_entries(arguments[7]), "spectra.render.editor_abi", "spectra");
            write_if_different(arguments[8], editor_abi_module.data(), editor_abi_module.size(), "generated editor shader ABI module");
        }
        std::vector<ShaderEntry> shader_entries = load_shader_entries(arguments[11]);
        shader_entries.append_range(load_shader_entries(arguments[12]));
        if (std::string_view{arguments[14]} == "ON") shader_entries.append_range(load_shader_entries(arguments[13]));
        const std::string shader_entries_module = generate_shader_entries_module(shader_entries);
        write_if_different(arguments[9], shader_entries_module.data(), shader_entries_module.size(), "generated Path Tracer shader entry module");
        for (const ShaderEntry& entry : shader_entries) compile_shader(entry.optimized ? *optimized_session : *default_session, entry, arguments[10]);
    } catch (const std::exception& error) {
        std::println(std::cerr, "{}", error.what());
        return 1;
    }
    return 0;
}
