import spectra;
import pyro;
import std;

namespace {
    struct PyroDemoConfig {
        std::uint64_t volume_id{1};
        xayah::PyroConfig simulation{};
        xayah::PyroPlumeSource source{};
        std::filesystem::path record_output_directory{"build/pyro_record"};
        std::uint64_t max_record_host_cache_bytes{1024ull * 1024ull * 1024ull};
        bool overwrite_record_output_directory{true};
    };

    constexpr std::array<char, 8> pyro_record_frame_magic{'S', 'P', 'Y', 'R', 'O', 'R', '1', '\0'};

    struct PyroRecordFrameFileHeader {
        std::array<char, 8> magic{};
        std::uint32_t version{1};
        std::int32_t frame_index{0};
        std::array<std::uint32_t, 3> resolution{};
        float cell_size{0.0f};
        std::array<std::uint64_t, 5> counts{};
    };

    struct PyroFrameRecorderConfig {
        std::filesystem::path output_directory{"build/pyro_record"};
        std::uint64_t max_host_cache_bytes{1024ull * 1024ull * 1024ull};
        bool overwrite_output_directory{true};
    };

    struct PyroFrameRecorderStats {
        int simulated_frames{0};
        int written_frames{0};
        int latest_ready_frame{-1};
        int written_frame_max{-1};
        std::uint64_t host_cache_bytes{0};
        std::uint64_t max_host_cache_bytes{0};
        bool finished{false};
    };

    struct PyroFrameRecorder {
        std::unique_ptr<xayah::PyroSolver> solver{};
        xayah::PyroConfig pyro_config{};
        PyroFrameRecorderConfig config{};
        std::filesystem::path frames_directory{};
        mutable std::mutex mutex{};
        std::condition_variable condition{};
        std::map<int, std::shared_ptr<xayah::PyroFrame>> cached_frames{};
        std::deque<std::shared_ptr<xayah::PyroFrame>> write_queue{};
        std::thread simulation_thread{};
        std::thread writer_thread{};
        std::uint64_t host_cache_bytes{0};
        int simulated_frames{0};
        int written_frames{0};
        int latest_ready_frame{-1};
        int written_frame_max{-1};
        bool started{false};
        bool stop_requested{false};
        bool simulation_finished{false};
        bool writer_finished{false};
        std::exception_ptr failure{};
    };

    enum class PyroRunMode : std::uint32_t {
        idle            = 0,
        preview_running = 1,
        preview_stopped = 2,
        record_running  = 3,
        record_stopping = 4,
        record_stopped  = 5,
    };

    struct PyroDemoState {
        PyroDemoConfig config{};
        PyroRunMode mode{PyroRunMode::idle};
        std::unique_ptr<xayah::PyroSolver> preview_solver{};
        std::unique_ptr<PyroFrameRecorder> recorder{};
        int preview_next_frame{0};
        int applied_record_frame{-1};
        int record_run_index{0};
    };

    xayah::VolumeSnapshot make_pyro_volume_snapshot(const std::uint64_t object_id, const xayah::PyroFrame& frame) {
        xayah::CenteredScalarGrid density_grid;
        density_grid.name       = "density";
        density_grid.resolution = frame.resolution;
        density_grid.values     = frame.density;

        xayah::CenteredScalarGrid temperature_grid;
        temperature_grid.name       = "temperature";
        temperature_grid.resolution = frame.resolution;
        temperature_grid.values     = frame.temperature;

        xayah::StaggeredVectorGrid velocity_grid;
        velocity_grid.name       = "velocity";
        velocity_grid.resolution = frame.resolution;
        velocity_grid.x_values   = frame.velocity_x;
        velocity_grid.y_values   = frame.velocity_y;
        velocity_grid.z_values   = frame.velocity_z;

        xayah::VolumeSnapshot snapshot;
        snapshot.object_id = object_id;
        snapshot.centered_scalar_grids.emplace_back(std::move(density_grid));
        snapshot.centered_scalar_grids.emplace_back(std::move(temperature_grid));
        snapshot.staggered_vector_grids.emplace_back(std::move(velocity_grid));
        return snapshot;
    }

    xayah::PyroFrame make_empty_pyro_frame(const xayah::PyroConfig& config) {
        const std::uint64_t nx = config.resolution[0];
        const std::uint64_t ny = config.resolution[1];
        const std::uint64_t nz = config.resolution[2];

        xayah::PyroFrame frame;
        frame.frame_index = 0;
        frame.resolution  = config.resolution;
        frame.cell_size   = config.cell_size;
        frame.density.resize(static_cast<std::size_t>(nx * ny * nz), 0.0f);
        frame.temperature.resize(static_cast<std::size_t>(nx * ny * nz), 0.0f);
        frame.velocity_x.resize(static_cast<std::size_t>((nx + 1u) * ny * nz), 0.0f);
        frame.velocity_y.resize(static_cast<std::size_t>(nx * (ny + 1u) * nz), 0.0f);
        frame.velocity_z.resize(static_cast<std::size_t>(nx * ny * (nz + 1u)), 0.0f);
        return frame;
    }

    xayah::Volume make_pyro_volume(const std::uint64_t object_id, const xayah::PyroConfig& config) {
        xayah::Volume volume;
        volume.id                       = object_id;
        volume.name                     = "pyro_smoke";
        volume.size                     = {
            static_cast<float>(config.resolution[0]) * config.cell_size,
            static_cast<float>(config.resolution[1]) * config.cell_size,
            static_cast<float>(config.resolution[2]) * config.cell_size,
        };
        volume.transform.translation[1] = volume.size[1] * 0.5f;

        xayah::VolumeSnapshot first_snapshot = make_pyro_volume_snapshot(object_id, make_empty_pyro_frame(config));
        volume.centered_scalar_grids         = std::move(first_snapshot.centered_scalar_grids);
        volume.staggered_vector_grids        = std::move(first_snapshot.staggered_vector_grids);
        volume.render_settings.grid_kind     = xayah::VolumeGridKind::centered_scalar;
        volume.render_settings.grid_name     = "density";
        volume.render_settings.display_mode  = xayah::VolumeDisplayMode::direct;
        volume.render_settings.opacity       = 0.78f;
        volume.render_settings.value_min     = 0.0f;
        volume.render_settings.value_max     = 1.0f;
        volume.render_settings.raymarch_step = 0.018f;
        return volume;
    }

    std::uint64_t pyro_record_frame_bytes(const xayah::PyroFrame& frame) {
        return (static_cast<std::uint64_t>(frame.density.size()) + static_cast<std::uint64_t>(frame.temperature.size()) + static_cast<std::uint64_t>(frame.velocity_x.size()) + static_cast<std::uint64_t>(frame.velocity_y.size()) + static_cast<std::uint64_t>(frame.velocity_z.size())) * sizeof(float);
    }

    std::uint64_t pyro_record_frame_bytes(const std::array<std::uint32_t, 3>& resolution) {
        const std::uint64_t nx = resolution[0];
        const std::uint64_t ny = resolution[1];
        const std::uint64_t nz = resolution[2];
        return (nx * ny * nz * 2u + (nx + 1u) * ny * nz + nx * (ny + 1u) * nz + nx * ny * (nz + 1u)) * sizeof(float);
    }

    std::filesystem::path pyro_record_frame_path(const std::filesystem::path& frames_directory, const int frame_index) {
        if (frame_index < 0) throw std::runtime_error("Pyro record frame index must be non-negative");
        return frames_directory / std::format("frame_{:06}.bin", frame_index);
    }

    void write_pyro_record_bytes(std::ofstream& file, const void* data, const std::size_t byte_count, const std::filesystem::path& path) {
        if (byte_count == 0) return;
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(byte_count));
        if (!file) throw std::runtime_error(std::string{"Failed to write Pyro record file: "} + path.string());
    }

    void read_pyro_record_bytes(std::ifstream& file, void* data, const std::size_t byte_count, const std::filesystem::path& path) {
        if (byte_count == 0) return;
        file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(byte_count));
        if (!file) throw std::runtime_error(std::string{"Failed to read Pyro record file: "} + path.string());
    }

    void write_pyro_record_frame_file(const std::filesystem::path& path, const xayah::PyroFrame& frame) {
        std::ofstream file{path, std::ios::binary};
        if (!file) throw std::runtime_error(std::string{"Failed to open Pyro record file for writing: "} + path.string());

        PyroRecordFrameFileHeader header{};
        header.magic       = pyro_record_frame_magic;
        header.frame_index = frame.frame_index;
        header.resolution  = frame.resolution;
        header.cell_size   = frame.cell_size;
        header.counts      = {
            static_cast<std::uint64_t>(frame.density.size()),
            static_cast<std::uint64_t>(frame.temperature.size()),
            static_cast<std::uint64_t>(frame.velocity_x.size()),
            static_cast<std::uint64_t>(frame.velocity_y.size()),
            static_cast<std::uint64_t>(frame.velocity_z.size()),
        };
        write_pyro_record_bytes(file, &header, sizeof(header), path);
        write_pyro_record_bytes(file, frame.density.data(), frame.density.size() * sizeof(float), path);
        write_pyro_record_bytes(file, frame.temperature.data(), frame.temperature.size() * sizeof(float), path);
        write_pyro_record_bytes(file, frame.velocity_x.data(), frame.velocity_x.size() * sizeof(float), path);
        write_pyro_record_bytes(file, frame.velocity_y.data(), frame.velocity_y.size() * sizeof(float), path);
        write_pyro_record_bytes(file, frame.velocity_z.data(), frame.velocity_z.size() * sizeof(float), path);
    }

    xayah::PyroFrame read_pyro_record_frame_file(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary};
        if (!file) throw std::runtime_error(std::string{"Failed to open Pyro record file for reading: "} + path.string());

        PyroRecordFrameFileHeader header{};
        read_pyro_record_bytes(file, &header, sizeof(header), path);
        if (header.magic != pyro_record_frame_magic) throw std::runtime_error(std::string{"Invalid Pyro record frame magic: "} + path.string());
        if (header.version != 1u) throw std::runtime_error(std::string{"Unsupported Pyro record frame version: "} + path.string());

        xayah::PyroFrame frame;
        frame.frame_index = header.frame_index;
        frame.resolution  = header.resolution;
        frame.cell_size   = header.cell_size;
        frame.density.resize(static_cast<std::size_t>(header.counts[0]));
        frame.temperature.resize(static_cast<std::size_t>(header.counts[1]));
        frame.velocity_x.resize(static_cast<std::size_t>(header.counts[2]));
        frame.velocity_y.resize(static_cast<std::size_t>(header.counts[3]));
        frame.velocity_z.resize(static_cast<std::size_t>(header.counts[4]));
        read_pyro_record_bytes(file, frame.density.data(), frame.density.size() * sizeof(float), path);
        read_pyro_record_bytes(file, frame.temperature.data(), frame.temperature.size() * sizeof(float), path);
        read_pyro_record_bytes(file, frame.velocity_x.data(), frame.velocity_x.size() * sizeof(float), path);
        read_pyro_record_bytes(file, frame.velocity_y.data(), frame.velocity_y.size() * sizeof(float), path);
        read_pyro_record_bytes(file, frame.velocity_z.data(), frame.velocity_z.size() * sizeof(float), path);
        return frame;
    }

    void throw_if_pyro_recorder_failed(const PyroFrameRecorder& recorder) {
        std::lock_guard lock{recorder.mutex};
        if (recorder.failure) std::rethrow_exception(recorder.failure);
    }

    void evict_pyro_frame_cache_locked(PyroFrameRecorder& recorder, const std::uint64_t required_free_bytes) {
        if (required_free_bytes > recorder.config.max_host_cache_bytes) throw std::runtime_error("Pyro recorder required cache space exceeds max_host_cache_bytes");
        while (recorder.host_cache_bytes + required_free_bytes > recorder.config.max_host_cache_bytes && !recorder.cached_frames.empty()) {
            auto victim = recorder.cached_frames.begin();
            if (victim->first > recorder.written_frame_max) return;
            recorder.host_cache_bytes -= pyro_record_frame_bytes(*victim->second);
            recorder.cached_frames.erase(victim);
        }
    }

    void pyro_frame_recorder_simulation_loop(PyroFrameRecorder* recorder) {
        try {
            if (recorder == nullptr || recorder->solver == nullptr) throw std::runtime_error("Pyro recorder simulation loop has no solver");
            const std::uint64_t frame_bytes = pyro_record_frame_bytes(recorder->pyro_config.resolution);
            for (int frame_index = 0;; ++frame_index) {
                {
                    std::unique_lock lock{recorder->mutex};
                    evict_pyro_frame_cache_locked(*recorder, frame_bytes);
                    while (!recorder->stop_requested && !recorder->failure && recorder->host_cache_bytes + frame_bytes > recorder->config.max_host_cache_bytes) {
                        recorder->condition.wait(lock);
                    }
                    if (recorder->failure) std::rethrow_exception(recorder->failure);
                    if (recorder->stop_requested) break;
                }

                recorder->solver->step();
                auto frame = std::make_shared<xayah::PyroFrame>(recorder->solver->read_frame(frame_index));
                {
                    std::lock_guard lock{recorder->mutex};
                    recorder->host_cache_bytes += pyro_record_frame_bytes(*frame);
                    recorder->cached_frames.emplace(frame->frame_index, frame);
                    recorder->write_queue.emplace_back(frame);
                    ++recorder->simulated_frames;
                    recorder->latest_ready_frame = std::max(recorder->latest_ready_frame, frame->frame_index);
                    evict_pyro_frame_cache_locked(*recorder, 0);
                }
                recorder->condition.notify_all();
            }

            {
                std::lock_guard lock{recorder->mutex};
                recorder->simulation_finished = true;
            }
            recorder->condition.notify_all();
        } catch (...) {
            if (recorder != nullptr) {
                {
                    std::lock_guard lock{recorder->mutex};
                    recorder->failure             = std::current_exception();
                    recorder->simulation_finished = true;
                    recorder->stop_requested      = true;
                }
                recorder->condition.notify_all();
            }
        }
    }

    void pyro_frame_recorder_writer_loop(PyroFrameRecorder* recorder) {
        try {
            if (recorder == nullptr) throw std::runtime_error("Pyro recorder writer loop has no recorder");
            while (true) {
                std::shared_ptr<xayah::PyroFrame> frame{};
                {
                    std::unique_lock lock{recorder->mutex};
                    recorder->condition.wait(lock, [recorder] { return recorder->failure || !recorder->write_queue.empty() || (recorder->simulation_finished && recorder->write_queue.empty()); });
                    if (recorder->failure) std::rethrow_exception(recorder->failure);
                    if (recorder->write_queue.empty() && recorder->simulation_finished) break;
                    frame = recorder->write_queue.front();
                    recorder->write_queue.pop_front();
                }

                write_pyro_record_frame_file(pyro_record_frame_path(recorder->frames_directory, frame->frame_index), *frame);

                {
                    std::lock_guard lock{recorder->mutex};
                    ++recorder->written_frames;
                    recorder->written_frame_max = std::max(recorder->written_frame_max, frame->frame_index);
                    evict_pyro_frame_cache_locked(*recorder, pyro_record_frame_bytes(recorder->pyro_config.resolution));
                }
                recorder->condition.notify_all();
            }

            {
                std::lock_guard lock{recorder->mutex};
                recorder->writer_finished = true;
            }
            recorder->condition.notify_all();
        } catch (...) {
            if (recorder != nullptr) {
                {
                    std::lock_guard lock{recorder->mutex};
                    recorder->failure         = std::current_exception();
                    recorder->writer_finished = true;
                    recorder->stop_requested  = true;
                }
                recorder->condition.notify_all();
            }
        }
    }

    void start_pyro_frame_recorder(PyroFrameRecorder& recorder, const xayah::PyroConfig& pyro_config, const xayah::PyroPlumeSource& source, const PyroFrameRecorderConfig& config) {
        if (config.output_directory.empty()) throw std::runtime_error("Pyro recorder output_directory must not be empty");
        if (config.max_host_cache_bytes == 0) throw std::runtime_error("Pyro recorder max_host_cache_bytes must be positive");
        if (config.max_host_cache_bytes < pyro_record_frame_bytes(pyro_config.resolution)) throw std::runtime_error("Pyro recorder max_host_cache_bytes must fit at least one frame");

        {
            std::lock_guard lock{recorder.mutex};
            if (recorder.started) throw std::runtime_error("Pyro recorder has already started");
            recorder.pyro_config         = pyro_config;
            recorder.config              = config;
            recorder.frames_directory    = config.output_directory / "frames";
            recorder.started             = true;
            recorder.stop_requested      = false;
            recorder.simulation_finished = false;
            recorder.writer_finished     = false;
        }

        if (config.overwrite_output_directory && std::filesystem::exists(config.output_directory)) std::filesystem::remove_all(config.output_directory);
        std::filesystem::create_directories(recorder.frames_directory);
        std::ofstream manifest{config.output_directory / "manifest.txt"};
        if (!manifest) throw std::runtime_error(std::string{"Failed to create Pyro record manifest: "} + config.output_directory.string());
        manifest << "spectra_pyro_record_version 1\n";
        manifest << "resolution " << pyro_config.resolution[0] << ' ' << pyro_config.resolution[1] << ' ' << pyro_config.resolution[2] << '\n';
        manifest << "cell_size " << pyro_config.cell_size << '\n';

        recorder.solver = std::make_unique<xayah::PyroSolver>(pyro_config);
        recorder.solver->set_plume_source(source);
        PyroFrameRecorder* recorder_pointer = &recorder;
        recorder.writer_thread              = std::thread{[recorder_pointer] { pyro_frame_recorder_writer_loop(recorder_pointer); }};
        recorder.simulation_thread          = std::thread{[recorder_pointer] { pyro_frame_recorder_simulation_loop(recorder_pointer); }};
    }

    void request_stop_pyro_frame_recorder(PyroFrameRecorder& recorder) noexcept {
        {
            std::lock_guard lock{recorder.mutex};
            recorder.stop_requested = true;
        }
        recorder.condition.notify_all();
    }

    void finish_pyro_frame_recorder(PyroFrameRecorder& recorder) {
        if (recorder.simulation_thread.joinable()) recorder.simulation_thread.join();
        if (recorder.writer_thread.joinable()) recorder.writer_thread.join();
        throw_if_pyro_recorder_failed(recorder);
    }

    void stop_pyro_frame_recorder(std::unique_ptr<PyroFrameRecorder>& recorder) {
        if (recorder == nullptr) return;
        request_stop_pyro_frame_recorder(*recorder);
        finish_pyro_frame_recorder(*recorder);
        recorder.reset();
    }

    void stop_pyro_frame_recorder_noexcept(std::unique_ptr<PyroFrameRecorder>& recorder) noexcept {
        try {
            stop_pyro_frame_recorder(recorder);
        } catch (...) {
            recorder.reset();
        }
    }

    PyroFrameRecorderStats pyro_frame_recorder_stats(const PyroFrameRecorder& recorder) {
        std::lock_guard lock{recorder.mutex};
        PyroFrameRecorderStats stats;
        stats.simulated_frames     = recorder.simulated_frames;
        stats.written_frames       = recorder.written_frames;
        stats.latest_ready_frame   = recorder.latest_ready_frame;
        stats.written_frame_max    = recorder.written_frame_max;
        stats.host_cache_bytes     = recorder.host_cache_bytes;
        stats.max_host_cache_bytes = recorder.config.max_host_cache_bytes;
        stats.finished             = recorder.simulation_finished && recorder.writer_finished;
        return stats;
    }

    void finish_pyro_frame_recorder_if_ready(PyroFrameRecorder& recorder) {
        const PyroFrameRecorderStats stats = pyro_frame_recorder_stats(recorder);
        if (!stats.finished) return;
        finish_pyro_frame_recorder(recorder);
    }

    bool read_pyro_frame_recorder_frame(const PyroFrameRecorder& recorder, const int frame_index, xayah::PyroFrame& frame) {
        if (frame_index < 0) throw std::runtime_error("Pyro recorder frame index must be non-negative");
        std::filesystem::path path{};
        {
            std::lock_guard lock{recorder.mutex};
            if (recorder.failure) std::rethrow_exception(recorder.failure);
            if (const auto found = recorder.cached_frames.find(frame_index); found != recorder.cached_frames.end()) {
                frame = *found->second;
                return true;
            }
            if (frame_index > recorder.written_frame_max) return false;
            path = pyro_record_frame_path(recorder.frames_directory, frame_index);
        }

        frame = read_pyro_record_frame_file(path);
        return true;
    }

    const char* pyro_mode_label(const PyroRunMode mode) {
        if (mode == PyroRunMode::idle) return "Idle";
        if (mode == PyroRunMode::preview_running) return "Preview";
        if (mode == PyroRunMode::preview_stopped) return "Preview Stopped";
        if (mode == PyroRunMode::record_running) return "Record";
        if (mode == PyroRunMode::record_stopping) return "Record Stopping";
        if (mode == PyroRunMode::record_stopped) return "Record Stopped";
        throw std::runtime_error("Unsupported Pyro run mode");
    }

    void reset_pyro_scene(PyroDemoState& state, xayah::Scene& scene) {
        scene.apply_snapshot(make_pyro_volume_snapshot(state.config.volume_id, make_empty_pyro_frame(state.config.simulation)));
    }

    void start_pyro_preview(PyroDemoState& state, xayah::Scene& scene) {
        stop_pyro_frame_recorder(state.recorder);
        state.preview_solver = std::make_unique<xayah::PyroSolver>(state.config.simulation);
        state.preview_solver->set_plume_source(state.config.source);
        state.preview_next_frame   = 0;
        state.applied_record_frame = -1;
        reset_pyro_scene(state, scene);
        state.mode = PyroRunMode::preview_running;
    }

    void start_pyro_record(PyroDemoState& state, xayah::Scene& scene) {
        state.preview_solver.reset();
        stop_pyro_frame_recorder(state.recorder);

        PyroFrameRecorderConfig recorder_config;
        ++state.record_run_index;
        recorder_config.output_directory           = state.config.record_output_directory / std::format("run_{:04}", state.record_run_index);
        recorder_config.max_host_cache_bytes       = state.config.max_record_host_cache_bytes;
        recorder_config.overwrite_output_directory = state.config.overwrite_record_output_directory;
        state.recorder                             = std::make_unique<PyroFrameRecorder>();
        start_pyro_frame_recorder(*state.recorder, state.config.simulation, state.config.source, recorder_config);
        state.applied_record_frame = -1;
        reset_pyro_scene(state, scene);
        state.mode = PyroRunMode::record_running;
    }

    void handle_pyro_space(PyroDemoState& state, xayah::Scene& scene, const bool shift_down) {
        if (state.mode == PyroRunMode::preview_running) {
            state.mode = PyroRunMode::preview_stopped;
            return;
        }
        if (state.mode == PyroRunMode::record_running) {
            if (state.recorder == nullptr) throw std::runtime_error("Cannot stop record session without a recorder");
            request_stop_pyro_frame_recorder(*state.recorder);
            state.mode = PyroRunMode::record_stopping;
            return;
        }
        if (state.mode == PyroRunMode::record_stopping) return;
        if (shift_down) start_pyro_record(state, scene);
        else start_pyro_preview(state, scene);
    }

    xayah::SpectraFrameUpdateResult make_pyro_idle_result(const PyroDemoState& state) {
        xayah::SpectraFrameUpdateResult result;
        result.mode_label = pyro_mode_label(state.mode);
        return result;
    }

    xayah::SpectraFrameUpdateResult make_pyro_preview_result(const PyroDemoState& state, const char* mode_label) {
        xayah::SpectraFrameUpdateResult result;
        result.mode_label       = mode_label;
        result.simulated_frames = state.preview_next_frame;
        return result;
    }

    xayah::SpectraFrameUpdateResult make_pyro_record_result(const PyroFrameRecorderStats& stats, const char* mode_label, const bool follow_latest) {
        xayah::SpectraFrameUpdateResult result;
        const int total_frame_max           = std::max({0, stats.simulated_frames - 1, stats.latest_ready_frame, stats.written_frame_max});
        const int available_source_max      = std::max(stats.latest_ready_frame, stats.written_frame_max);
        const int available_frame_max       = std::clamp(available_source_max, 0, total_frame_max);
        result.mode_label                   = mode_label;
        result.timeline_visible             = true;
        result.timeline_frame_min           = 0;
        result.timeline_frame_max           = total_frame_max;
        result.timeline_available_frame_max = available_frame_max;
        result.show_record_stats            = true;
        result.simulated_frames             = stats.simulated_frames;
        result.written_frames               = stats.written_frames;
        result.cache_bytes                  = stats.host_cache_bytes;
        result.max_cache_bytes              = stats.max_host_cache_bytes;
        if (follow_latest && stats.latest_ready_frame >= 0) result.timeline_current_frame = available_frame_max;
        return result;
    }

    void apply_pyro_record_frame(PyroDemoState& state, xayah::Scene& scene, const int requested_frame, const int available_frame_max) {
        if (state.recorder == nullptr) throw std::runtime_error("Record session has no recorder");
        if (available_frame_max < 0) return;
        const int frame_index = std::clamp(requested_frame, 0, available_frame_max);
        if (frame_index == state.applied_record_frame) return;
        xayah::PyroFrame frame;
        if (!read_pyro_frame_recorder_frame(*state.recorder, frame_index, frame)) return;
        scene.apply_snapshot(make_pyro_volume_snapshot(state.config.volume_id, frame));
        state.applied_record_frame = frame_index;
    }

    xayah::SpectraFrameUpdateResult update_pyro_preview_running(PyroDemoState& state, xayah::Scene& scene) {
        if (state.preview_solver == nullptr) throw std::runtime_error("Preview session has no solver");
        state.preview_solver->step();
        xayah::PyroFrame frame = state.preview_solver->read_frame(state.preview_next_frame);
        scene.apply_snapshot(make_pyro_volume_snapshot(state.config.volume_id, frame));
        ++state.preview_next_frame;
        return make_pyro_preview_result(state, pyro_mode_label(state.mode));
    }

    xayah::SpectraFrameUpdateResult update_pyro_record_running(PyroDemoState& state, xayah::Scene& scene) {
        if (state.recorder == nullptr) throw std::runtime_error("Record session has no recorder");
        PyroFrameRecorderStats stats = pyro_frame_recorder_stats(*state.recorder);
        if (stats.finished) {
            finish_pyro_frame_recorder_if_ready(*state.recorder);
            state.mode = PyroRunMode::record_stopped;
            stats      = pyro_frame_recorder_stats(*state.recorder);
            const int available_frame_max = std::max(stats.latest_ready_frame, stats.written_frame_max);
            apply_pyro_record_frame(state, scene, available_frame_max, available_frame_max);
            return make_pyro_record_result(stats, pyro_mode_label(state.mode), false);
        }
        const int available_frame_max = std::max(stats.latest_ready_frame, stats.written_frame_max);
        apply_pyro_record_frame(state, scene, available_frame_max, available_frame_max);
        return make_pyro_record_result(stats, pyro_mode_label(state.mode), true);
    }

    xayah::SpectraFrameUpdateResult update_pyro_record_stopping(PyroDemoState& state, xayah::Scene& scene) {
        if (state.recorder == nullptr) throw std::runtime_error("Record session has no recorder");
        PyroFrameRecorderStats stats  = pyro_frame_recorder_stats(*state.recorder);
        const int available_frame_max = std::max(stats.latest_ready_frame, stats.written_frame_max);
        apply_pyro_record_frame(state, scene, available_frame_max, available_frame_max);
        if (stats.finished) {
            finish_pyro_frame_recorder_if_ready(*state.recorder);
            state.mode = PyroRunMode::record_stopped;
            stats      = pyro_frame_recorder_stats(*state.recorder);
            return make_pyro_record_result(stats, pyro_mode_label(state.mode), false);
        }
        return make_pyro_record_result(stats, pyro_mode_label(state.mode), true);
    }

    xayah::SpectraFrameUpdateResult update_pyro_record_stopped(PyroDemoState& state, xayah::Scene& scene, const xayah::SpectraFrameUpdateContext& context) {
        if (state.recorder == nullptr) throw std::runtime_error("Record session has no recorder");
        const PyroFrameRecorderStats stats = pyro_frame_recorder_stats(*state.recorder);
        const int available_frame_max      = std::max(stats.latest_ready_frame, stats.written_frame_max);
        apply_pyro_record_frame(state, scene, context.timeline_current_frame, available_frame_max);
        return make_pyro_record_result(stats, pyro_mode_label(state.mode), false);
    }

    xayah::SpectraFrameUpdateResult update_pyro_demo(PyroDemoState& state, xayah::Scene& scene, const xayah::SpectraFrameUpdateContext& context) {
        if (context.space_pressed) handle_pyro_space(state, scene, context.shift_down);

        if (state.mode == PyroRunMode::preview_running) return update_pyro_preview_running(state, scene);
        if (state.mode == PyroRunMode::preview_stopped) return make_pyro_preview_result(state, pyro_mode_label(state.mode));
        if (state.mode == PyroRunMode::record_running) return update_pyro_record_running(state, scene);
        if (state.mode == PyroRunMode::record_stopping) return update_pyro_record_stopping(state, scene);
        if (state.mode == PyroRunMode::record_stopped) return update_pyro_record_stopped(state, scene, context);
        return make_pyro_idle_result(state);
    }

    void stop_pyro_demo(PyroDemoState& state) {
        stop_pyro_frame_recorder(state.recorder);
        state.preview_solver.reset();
    }

    void stop_pyro_demo_noexcept(PyroDemoState& state) noexcept {
        stop_pyro_frame_recorder_noexcept(state.recorder);
        state.preview_solver.reset();
    }
} // namespace

int main() {
    PyroDemoState pyro;
    pyro.config.volume_id                      = 1;
    pyro.config.simulation.resolution          = {48, 96, 48};
    pyro.config.simulation.cell_size           = 0.035f;
    pyro.config.simulation.pressure_iterations = 48;
    pyro.config.source.center                  = {0.5f, 0.10f, 0.5f};
    pyro.config.source.radius                  = {0.28f, 0.075f, 0.28f};
    pyro.config.source.density                 = 26.0f;
    pyro.config.source.temperature             = 42.0f;
    pyro.config.source.falloff                 = 1.25f;

    xayah::Scene scene;
    scene.add(make_pyro_volume(pyro.config.volume_id, pyro.config.simulation));

    xayah::Spectra spectra;
    try {
        spectra.render(scene, [&](xayah::Scene& active_scene, const xayah::SpectraFrameUpdateContext& context) { return update_pyro_demo(pyro, active_scene, context); });
        stop_pyro_demo(pyro);
    } catch (...) {
        stop_pyro_demo_noexcept(pyro);
        throw;
    }
    return 0;
}
