import std;
import xayah.projects.sparkles;

namespace {
    class UsageError final : public std::runtime_error {
    public:
        explicit UsageError(std::string message) : std::runtime_error(std::move(message)) {}
    };

    struct CliOptions {
        std::uint32_t frames{120};
        float delta_seconds{1.0f / 60.0f};
        bool quiet{false};
        bool help{false};
    };

    void print_usage(const std::string_view message) {
        if (!message.empty()) std::cerr << "xayah_sparkles_cli: " << message << "\n\n";
        std::cerr << "usage: xayah_sparkles_cli [--frames <n>] [--dt <seconds>] [--quiet]\n";
    }

    [[nodiscard]] const std::string& require_value(const std::vector<std::string>& arguments, std::size_t& index, const std::string_view option) {
        ++index;
        if (index >= arguments.size()) throw UsageError(std::string{option} + " requires a value");
        return arguments[index];
    }

    [[nodiscard]] std::uint32_t parse_positive_u32(const std::string_view text, const std::string_view option) {
        std::uint32_t value{};
        const char* begin                   = text.data();
        const char* end                     = begin + text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || value == 0u) throw UsageError(std::string{option} + " requires a positive integer value");
        return value;
    }

    [[nodiscard]] float parse_positive_float(const std::string_view text, const std::string_view option) {
        float value{};
        const char* begin                   = text.data();
        const char* end                     = begin + text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value) || value <= 0.0f) throw UsageError(std::string{option} + " requires a positive finite value");
        return value;
    }

    [[nodiscard]] std::vector<std::string> command_line_arguments(const int argc, char* argv[]) {
        std::vector<std::string> arguments{};
        if (argc > 1) arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
        return arguments;
    }

    [[nodiscard]] CliOptions parse_cli(const std::vector<std::string>& arguments) {
        CliOptions options{};
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const std::string& argument = arguments[index];
            if (argument == "--frames") {
                options.frames = parse_positive_u32(require_value(arguments, index, argument), argument);
            } else if (argument == "--dt") {
                options.delta_seconds = parse_positive_float(require_value(arguments, index, argument), argument);
            } else if (argument == "--quiet") {
                options.quiet = true;
            } else if (argument == "--help" || argument == "-h") {
                options.help = true;
            } else {
                throw UsageError("unknown argument \"" + argument + "\"");
            }
        }
        return options;
    }
} // namespace

int main(const int argc, char* argv[]) {
    try {
        const CliOptions options = parse_cli(command_line_arguments(argc, argv));
        if (options.help) {
            print_usage({});
            return 0;
        }

        xayah::projects::sparkles::Solver solver{};
        for (std::uint32_t frame = 0; frame < options.frames; ++frame) {
            solver.step(options.delta_seconds);
            if (!options.quiet) std::cout << "frame " << frame + 1u << "/" << options.frames << "\n";
        }

        const std::span<const xayah::projects::sparkles::Particle> particles = solver.particles();
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "sparkles frames=" << options.frames
                  << " dt=" << options.delta_seconds
                  << " particles=" << particles.size();
        if (!particles.empty()) {
            const xayah::projects::sparkles::Particle& first = particles.front();
            std::cout << " first=(" << first.position[0] << ", " << first.position[1] << ", " << first.position[2] << ")";
        }
        std::cout << "\n";
        return 0;
    } catch (const UsageError& error) {
        print_usage(error.what());
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
