export module spectra.plugin;

import spectra.scene;
import std;

namespace spectra::plugin {
    export struct Controls {
        bool running{};
        bool can_start{};
        bool can_stop{};
        bool can_advance{};
    };

    export struct Timeline {
        double seconds{};
        std::uint64_t frame{};
    };

    export struct PluginHost {
        PluginHost(const std::filesystem::path& path, scene::Scene& scene);
        ~PluginHost();

        PluginHost(const PluginHost&) = delete;
        PluginHost(PluginHost&&) = delete;
        PluginHost& operator=(const PluginHost&) = delete;
        PluginHost& operator=(PluginHost&&) = delete;

        void start();
        void stop();
        void advance(double seconds);
        [[nodiscard]] Controls controls() const;
        [[nodiscard]] Timeline timeline() const;

    private:
        struct State;
        std::unique_ptr<State> state{};

    public:
        const std::string& name;
    };
} // namespace spectra::plugin
