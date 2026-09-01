module spectra.command_line;

import spectra.headless;
import std;

namespace spectra {
    namespace {
        [[nodiscard]] std::filesystem::path utf8_path(const std::string_view text) {
            std::u8string encoded{};
            encoded.reserve(text.size());
            for (const char byte : text) encoded.push_back(static_cast<char8_t>(byte));
            return std::filesystem::path{encoded};
        }

        [[nodiscard]] std::string_view option_value(const std::span<char* const> arguments, std::size_t& index, const std::optional<std::string_view> inline_value, const std::string_view option) {
            if (inline_value) {
                if (inline_value->empty()) throw std::runtime_error(std::format("--{} requires a value", option));
                return *inline_value;
            }
            if (++index == arguments.size()) throw std::runtime_error(std::format("--{} requires a value", option));
            const std::string_view value{arguments[index]};
            if (value.empty()) throw std::runtime_error(std::format("--{} requires a value", option));
            return value;
        }

        template <class Number>
        [[nodiscard]] Number parse_number(const std::string_view text, const std::string_view option) {
            Number value{};
            const std::from_chars_result result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw std::runtime_error(std::format("Invalid --{} value '{}'", option, text));
            if constexpr (std::floating_point<Number>)
                if (!std::isfinite(value)) throw std::runtime_error(std::format("Invalid --{} value '{}'", option, text));
            return value;
        }

        [[nodiscard]] render::RendererKind parse_renderer(const std::string_view identifier) {
            if (identifier == render::rasterizer_descriptor.id) return render::RendererKind::Rasterizer;
            if (identifier == render::pathtracer_descriptor.id) return render::RendererKind::PathTracer;
            throw std::runtime_error(std::format("Unknown renderer: {}", identifier));
        }

        [[nodiscard]] render::RasterDisplayMode parse_raster_display_mode(const std::string_view identifier) {
            if (identifier == "material") return render::RasterDisplayMode::Material;
            if (identifier == "wireframe") return render::RasterDisplayMode::Wireframe;
            throw std::runtime_error(std::format("Unknown Raster display mode: {}", identifier));
        }

        [[nodiscard]] headless::Output parse_output(const std::string_view identifier) {
            if (identifier == "renderer-linear") return headless::Output::RendererLinear;
            if (identifier == "renderer-display") return headless::Output::RendererDisplay;
            if (identifier == "composed-display") return headless::Output::ComposedDisplay;
            throw std::runtime_error(std::format("Unknown headless output: {}", identifier));
        }

        [[nodiscard]] headless::DisplayLayers parse_display_layers(const std::string_view identifier) {
            if (identifier == "all") return {};
            if (identifier == "diagnostics") return {true, false, false};
            if (identifier == "visualizations") return {false, true, false};
            if (identifier == "overlays") return {false, false, true};
            if (identifier == "none") return {false, false, false};
            throw std::runtime_error(std::format("Unknown display layers: {}", identifier));
        }

        [[nodiscard]] render::AxesPlane parse_axes_plane(const std::string_view identifier) {
            if (identifier == "xz") return render::AxesPlane::Xz;
            if (identifier == "xy") return render::AxesPlane::Xy;
            if (identifier == "yz") return render::AxesPlane::Yz;
            throw std::runtime_error(std::format("Unknown axes plane: {}", identifier));
        }
    } // namespace

    CommandLine parse_command_line(const std::span<char* const> arguments) {
        CommandLine result{};
        std::unordered_set<std::string_view> provided{};
        for (std::size_t index = 1uz; index != arguments.size(); ++index) {
            const std::string_view argument{arguments[index]};
            if (argument == "-h") {
                if (!provided.emplace("help").second) throw std::runtime_error("--help was provided more than once");
                result.help_requested = true;
                continue;
            }
            if (argument.starts_with("--")) {
                const std::size_t assignment                       = argument.find('=');
                const std::string_view option                      = assignment == std::string_view::npos ? argument.substr(2uz) : argument.substr(2uz, assignment - 2uz);
                const std::optional<std::string_view> inline_value = assignment == std::string_view::npos ? std::nullopt : std::optional{argument.substr(assignment + 1uz)};
                if (option.empty()) throw std::runtime_error(std::format("Unknown argument '{}'", argument));
                if (!provided.emplace(option).second) throw std::runtime_error(std::format("--{} was provided more than once", option));

                if (option == "help" || option == "version" || option == "axes"
#if defined(SPECTRA_HAS_EDITOR)
                    || option == "gui"
#endif
                ) {
                    if (inline_value) throw std::runtime_error(std::format("--{} does not accept a value", option));
                    if (option == "help") result.help_requested = true;
                    else if (option == "version") result.version_requested = true;
                    else if (option == "axes") result.axes = true;
#if defined(SPECTRA_HAS_EDITOR)
                    else result.gui = true;
#endif
                    continue;
                }

                if (option == "renderer") result.renderer = parse_renderer(option_value(arguments, index, inline_value, option));
                else if (option == "raster-mode") result.raster_display_mode = parse_raster_display_mode(option_value(arguments, index, inline_value, option));
                else if (option == "render-output") result.render_output = parse_output(option_value(arguments, index, inline_value, option));
                else if (option == "display-layers") result.display_layers = parse_display_layers(option_value(arguments, index, inline_value, option));
                else if (option == "axes-plane") result.axes_plane = parse_axes_plane(option_value(arguments, index, inline_value, option));
                else if (option == "outline-instance") result.outlined_instance = parse_number<std::uint64_t>(option_value(arguments, index, inline_value, option), option);
                else if (option == "simulation-step") result.simulation_step = parse_number<std::uint64_t>(option_value(arguments, index, inline_value, option), option);
                else if (option == "simulation-time") result.simulation_seconds = parse_number<double>(option_value(arguments, index, inline_value, option), option);
                else if (option == "presentation-frame") result.presentation_frame = parse_number<std::uint64_t>(option_value(arguments, index, inline_value, option), option);
                else if (option == "presentation-time") result.presentation_seconds = parse_number<double>(option_value(arguments, index, inline_value, option), option);
                else if (option == "output") result.output_base = utf8_path(option_value(arguments, index, inline_value, option));
                else if (option == "gbuffer-output") result.gbuffer_output_path = utf8_path(option_value(arguments, index, inline_value, option));
                else if (option == "telemetry-output") result.telemetry_output_path = utf8_path(option_value(arguments, index, inline_value, option));
                else throw std::runtime_error(std::format("Unknown option '--{}'", option));
                continue;
            }
            if (argument.starts_with('-')) throw std::runtime_error(std::format("Unknown argument '{}'", argument));
            if (!result.scene_path.empty()) throw std::runtime_error(std::format("Unknown argument '{}'", argument));
            if (argument.empty()) throw std::runtime_error("Scene path must not be empty");
            result.scene_path = utf8_path(argument);
        }

        if (result.help_requested || result.version_requested) return result;

#if defined(SPECTRA_HAS_EDITOR)
        if (result.gui) {
            if (provided.contains("render-output") || provided.contains("display-layers") || provided.contains("axes") || provided.contains("axes-plane") || provided.contains("outline-instance") || provided.contains("simulation-step") || provided.contains("simulation-time") || provided.contains("presentation-frame") || provided.contains("presentation-time") || provided.contains("output") || provided.contains("gbuffer-output") || provided.contains("telemetry-output")) throw std::runtime_error("Headless-only options cannot be used with --gui");
            if (result.renderer == render::RendererKind::PathTracer && provided.contains("raster-mode")) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
            return result;
        }
#endif

        if (result.scene_path.empty()) throw std::runtime_error("Headless rendering requires a scene path");
        const render::RendererKind renderer = result.renderer.value_or(render::RendererKind::PathTracer);
        if (renderer == render::RendererKind::PathTracer && provided.contains("raster-mode")) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
        if (result.gbuffer_output_path && renderer != render::RendererKind::PathTracer) throw std::runtime_error("--gbuffer-output requires the Path Tracer");
        if ((result.axes || result.outlined_instance) && (result.render_output != headless::Output::ComposedDisplay || !result.display_layers.overlays)) throw std::runtime_error("Axes and Instance outlines require overlays in composed-display output");
        if (result.simulation_step && result.simulation_seconds) throw std::runtime_error("Select either --simulation-step or --simulation-time");
        if (result.presentation_frame && result.presentation_seconds) throw std::runtime_error("Select either --presentation-frame or --presentation-time");
        if (result.simulation_seconds && *result.simulation_seconds < 0.0) throw std::runtime_error("--simulation-time must be nonnegative");
        return result;
    }

    std::string command_line_help(const std::string_view executable) {
        std::string result = std::format(
            R"(Usage:
  {} [scene] [options]

Spectra scene visualization and physically based rendering.

Arguments:
  scene                                   Scene file to open or render.

Options:
  --version                               Print the Spectra version.
  --renderer <RENDERER>                   Select rasterizer or pathtracer.
  --raster-mode <MODE>                    Select material or wireframe Raster display.
  --render-output <OUTPUT>                Select renderer-linear, renderer-display, or composed-display.
  --display-layers <LAYERS>               Select all, diagnostics, visualizations, overlays, or none for composed output.
  --axes                                  Include the world axes overlay in composed output.
  --axes-plane <PLANE>                    Select the xz, xy, or yz axes grid plane.
  --outline-instance <ID>                 Outline one Scene Instance ID in composed output.
  --simulation-step <STEP>                Evaluate a simulation scene at an exact step.
  --simulation-time <SECONDS>             Evaluate the fixed simulation step at or immediately before this time.
  --presentation-frame <FRAME>            Select a Presentation Sequence frame.
  --presentation-time <SECONDS>           Select the Presentation Sequence frame at or immediately before this time.
  --output <BASE>                         Override the default renders/<scene>/<renderer> basename.
  --gbuffer-output <IMAGE>                Optional GBuffer EXR output.
  --telemetry-output <CSV>                Optional current Telemetry CSV output.
)",
            utf8_path(executable).filename().string());
#if defined(SPECTRA_HAS_EDITOR)
        result += "  --gui                                   Open the visualization UI.\n";
#endif
        result += "  -h, --help                              Print this help.\n";
        return result;
    }
} // namespace spectra
