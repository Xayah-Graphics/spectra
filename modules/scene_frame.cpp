module;

module scene_frame;
import std;

namespace {
    constexpr std::array<char, 8> scene_frame_record_magic{'S', 'S', 'F', 'R', 'M', '0', '2', '\0'};

    struct SceneFrameRecordHeader {
        std::array<char, 8> magic{};
        std::uint32_t version{2};
        std::int32_t frame_index{0};
        std::uint64_t object_count{0};
    };

    void write_record_bytes(std::ofstream& file, const void* data, const std::size_t byte_count, const std::filesystem::path& path) {
        if (byte_count == 0) return;
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(byte_count));
        if (!file) throw std::runtime_error(std::string{"Failed to write scene frame record file: "} + path.string());
    }

    void read_record_bytes(std::ifstream& file, void* data, const std::size_t byte_count, const std::filesystem::path& path) {
        if (byte_count == 0) return;
        file.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(byte_count));
        if (!file) throw std::runtime_error(std::string{"Failed to read scene frame record file: "} + path.string());
    }

    template <typename Value>
    void write_record_value(std::ofstream& file, const Value& value, const std::filesystem::path& path) {
        static_assert(std::is_trivially_copyable_v<Value>);
        write_record_bytes(file, &value, sizeof(Value), path);
    }

    template <typename Value>
    [[nodiscard]] Value read_record_value(std::ifstream& file, const std::filesystem::path& path) {
        static_assert(std::is_trivially_copyable_v<Value>);
        Value value{};
        read_record_bytes(file, &value, sizeof(Value), path);
        return value;
    }

    void write_record_string(std::ofstream& file, const std::string& value, const std::filesystem::path& path) {
        const std::uint64_t size = static_cast<std::uint64_t>(value.size());
        write_record_value(file, size, path);
        write_record_bytes(file, value.data(), value.size(), path);
    }

    [[nodiscard]] std::string read_record_string(std::ifstream& file, const std::filesystem::path& path) {
        const std::uint64_t size = read_record_value<std::uint64_t>(file, path);
        if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::string{"Scene frame record string is too large: "} + path.string());
        std::string value(static_cast<std::size_t>(size), '\0');
        read_record_bytes(file, value.data(), value.size(), path);
        return value;
    }

    template <typename Value>
    void write_record_vector(std::ofstream& file, const std::vector<Value>& values, const std::filesystem::path& path) {
        static_assert(std::is_trivially_copyable_v<Value>);
        const std::uint64_t count = static_cast<std::uint64_t>(values.size());
        write_record_value(file, count, path);
        write_record_bytes(file, values.data(), values.size() * sizeof(Value), path);
    }

    template <typename Value>
    [[nodiscard]] std::vector<Value> read_record_vector(std::ifstream& file, const std::filesystem::path& path) {
        static_assert(std::is_trivially_copyable_v<Value>);
        const std::uint64_t count = read_record_value<std::uint64_t>(file, path);
        if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::string{"Scene frame record vector is too large: "} + path.string());
        std::vector<Value> values(static_cast<std::size_t>(count));
        read_record_bytes(file, values.data(), values.size() * sizeof(Value), path);
        return values;
    }

    void write_record_grid(std::ofstream& file, const xayah::CenteredScalarGrid& grid, const std::filesystem::path& path) {
        write_record_string(file, grid.name, path);
        write_record_value(file, grid.resolution, path);
        write_record_vector(file, grid.values, path);
    }

    [[nodiscard]] xayah::CenteredScalarGrid read_record_centered_scalar_grid(std::ifstream& file, const std::filesystem::path& path) {
        xayah::CenteredScalarGrid grid;
        grid.name       = read_record_string(file, path);
        grid.resolution = read_record_value<std::array<std::uint32_t, 3>>(file, path);
        grid.values     = read_record_vector<float>(file, path);
        return grid;
    }

    void write_record_grid(std::ofstream& file, const xayah::StaggeredVectorGrid& grid, const std::filesystem::path& path) {
        write_record_string(file, grid.name, path);
        write_record_value(file, grid.resolution, path);
        write_record_vector(file, grid.x_values, path);
        write_record_vector(file, grid.y_values, path);
        write_record_vector(file, grid.z_values, path);
    }

    [[nodiscard]] xayah::StaggeredVectorGrid read_record_staggered_vector_grid(std::ifstream& file, const std::filesystem::path& path) {
        xayah::StaggeredVectorGrid grid;
        grid.name       = read_record_string(file, path);
        grid.resolution = read_record_value<std::array<std::uint32_t, 3>>(file, path);
        grid.x_values   = read_record_vector<float>(file, path);
        grid.y_values   = read_record_vector<float>(file, path);
        grid.z_values   = read_record_vector<float>(file, path);
        return grid;
    }

    void write_record_snapshot(std::ofstream& file, const xayah::VolumeSnapshot& snapshot, const std::filesystem::path& path) {
        write_record_value(file, snapshot.object_id, path);
        write_record_value(file, static_cast<std::uint64_t>(snapshot.centered_scalar_grids.size()), path);
        for (const xayah::CenteredScalarGrid& grid : snapshot.centered_scalar_grids) write_record_grid(file, grid, path);
        write_record_value(file, static_cast<std::uint64_t>(snapshot.staggered_vector_grids.size()), path);
        for (const xayah::StaggeredVectorGrid& grid : snapshot.staggered_vector_grids) write_record_grid(file, grid, path);
    }

    [[nodiscard]] xayah::VolumeSnapshot read_record_volume_snapshot(std::ifstream& file, const std::filesystem::path& path) {
        xayah::VolumeSnapshot snapshot;
        snapshot.object_id                    = read_record_value<std::uint64_t>(file, path);
        const std::uint64_t scalar_grid_count = read_record_value<std::uint64_t>(file, path);
        if (scalar_grid_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::string{"Scene frame record scalar grid count is too large: "} + path.string());
        snapshot.centered_scalar_grids.reserve(static_cast<std::size_t>(scalar_grid_count));
        for (std::uint64_t index = 0; index < scalar_grid_count; ++index) snapshot.centered_scalar_grids.emplace_back(read_record_centered_scalar_grid(file, path));
        const std::uint64_t vector_grid_count = read_record_value<std::uint64_t>(file, path);
        if (vector_grid_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::string{"Scene frame record vector grid count is too large: "} + path.string());
        snapshot.staggered_vector_grids.reserve(static_cast<std::size_t>(vector_grid_count));
        for (std::uint64_t index = 0; index < vector_grid_count; ++index) snapshot.staggered_vector_grids.emplace_back(read_record_staggered_vector_grid(file, path));
        return snapshot;
    }

    void write_record_snapshot(std::ofstream& file, const xayah::MeshSnapshot& snapshot, const std::filesystem::path& path) {
        write_record_value(file, snapshot.object_id, path);
        write_record_value(file, snapshot.transform, path);
        write_record_vector(file, snapshot.vertices, path);
    }

    [[nodiscard]] xayah::MeshSnapshot read_record_mesh_snapshot(std::ifstream& file, const std::filesystem::path& path) {
        xayah::MeshSnapshot snapshot;
        snapshot.object_id = read_record_value<std::uint64_t>(file, path);
        snapshot.transform = read_record_value<xayah::Transform>(file, path);
        snapshot.vertices  = read_record_vector<xayah::MeshVertex>(file, path);
        return snapshot;
    }

    void write_record_snapshot(std::ofstream& file, const xayah::ParticlesSnapshot& snapshot, const std::filesystem::path& path) {
        write_record_value(file, snapshot.object_id, path);
        write_record_vector(file, snapshot.particles, path);
    }

    [[nodiscard]] xayah::ParticlesSnapshot read_record_particles_snapshot(std::ifstream& file, const std::filesystem::path& path) {
        xayah::ParticlesSnapshot snapshot;
        snapshot.object_id = read_record_value<std::uint64_t>(file, path);
        snapshot.particles = read_record_vector<xayah::Particle>(file, path);
        return snapshot;
    }

    [[nodiscard]] std::filesystem::path scene_frame_record_frame_path(const std::filesystem::path& frames_directory, const int frame_index) {
        if (frame_index < 0) throw std::runtime_error("Scene frame record index must be non-negative");
        return frames_directory / std::format("frame_{:06}.bin", frame_index);
    }

    void write_scene_frame_snapshot_file(const std::filesystem::path& path, const xayah::SceneFrameSnapshot& snapshot) {
        std::ofstream file{path, std::ios::binary};
        if (!file) throw std::runtime_error(std::string{"Failed to open scene frame record file for writing: "} + path.string());

        SceneFrameRecordHeader header;
        header.magic        = scene_frame_record_magic;
        header.frame_index  = snapshot.frame_index;
        header.object_count = static_cast<std::uint64_t>(snapshot.objects.size());
        write_record_value(file, header, path);

        for (const std::variant<xayah::VolumeSnapshot, xayah::MeshSnapshot, xayah::ParticlesSnapshot>& object_snapshot : snapshot.objects) {
            std::visit(
                [&](const auto& value) {
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::VolumeSnapshot>)
                        write_record_value(file, xayah::SceneObjectKind::volume, path);
                    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::MeshSnapshot>)
                        write_record_value(file, xayah::SceneObjectKind::mesh, path);
                    else
                        write_record_value(file, xayah::SceneObjectKind::particles, path);
                    write_record_snapshot(file, value, path);
                },
                object_snapshot);
        }
    }

    [[nodiscard]] xayah::SceneFrameSnapshot read_scene_frame_snapshot_file(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary};
        if (!file) throw std::runtime_error(std::string{"Failed to open scene frame record file for reading: "} + path.string());

        const SceneFrameRecordHeader header = read_record_value<SceneFrameRecordHeader>(file, path);
        if (header.magic != scene_frame_record_magic) throw std::runtime_error(std::string{"Invalid scene frame record magic: "} + path.string());
        if (header.version != 2u) throw std::runtime_error(std::string{"Unsupported scene frame record version: "} + path.string());
        if (header.object_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::string{"Scene frame record object count is too large: "} + path.string());

        xayah::SceneFrameSnapshot snapshot;
        snapshot.frame_index = header.frame_index;
        snapshot.objects.reserve(static_cast<std::size_t>(header.object_count));
        for (std::uint64_t index = 0; index < header.object_count; ++index) {
            const xayah::SceneObjectKind kind = read_record_value<xayah::SceneObjectKind>(file, path);
            if (kind == xayah::SceneObjectKind::volume)
                snapshot.objects.emplace_back(read_record_volume_snapshot(file, path));
            else if (kind == xayah::SceneObjectKind::mesh)
                snapshot.objects.emplace_back(read_record_mesh_snapshot(file, path));
            else if (kind == xayah::SceneObjectKind::particles)
                snapshot.objects.emplace_back(read_record_particles_snapshot(file, path));
            else
                throw std::runtime_error(std::string{"Unsupported scene frame record object kind: "} + path.string());
        }
        return snapshot;
    }

    [[nodiscard]] std::uint64_t centered_scalar_grid_bytes(const xayah::CenteredScalarGrid& grid) {
        return sizeof(grid) + static_cast<std::uint64_t>(grid.name.size()) + static_cast<std::uint64_t>(grid.values.size()) * sizeof(float);
    }

    [[nodiscard]] std::uint64_t staggered_vector_grid_bytes(const xayah::StaggeredVectorGrid& grid) {
        return sizeof(grid) + static_cast<std::uint64_t>(grid.name.size()) + (static_cast<std::uint64_t>(grid.x_values.size()) + static_cast<std::uint64_t>(grid.y_values.size()) + static_cast<std::uint64_t>(grid.z_values.size())) * sizeof(float);
    }

    [[nodiscard]] std::uint64_t scene_frame_snapshot_bytes(const xayah::SceneFrameSnapshot& snapshot) {
        std::uint64_t bytes = sizeof(snapshot);
        for (const std::variant<xayah::VolumeSnapshot, xayah::MeshSnapshot, xayah::ParticlesSnapshot>& object_snapshot : snapshot.objects) {
            std::visit(
                [&](const auto& value) {
                    bytes += sizeof(value);
                    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::VolumeSnapshot>) {
                        for (const xayah::CenteredScalarGrid& grid : value.centered_scalar_grids) bytes += centered_scalar_grid_bytes(grid);
                        for (const xayah::StaggeredVectorGrid& grid : value.staggered_vector_grids) bytes += staggered_vector_grid_bytes(grid);
                    } else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, xayah::MeshSnapshot>) {
                        bytes += static_cast<std::uint64_t>(value.vertices.size()) * sizeof(xayah::MeshVertex);
                    } else {
                        bytes += static_cast<std::uint64_t>(value.particles.size()) * sizeof(xayah::Particle);
                    }
                },
                object_snapshot);
        }
        return bytes;
    }
} // namespace

namespace xayah {
    SceneFrameRecorder::SceneFrameRecorder() = default;

    SceneFrameRecorder::~SceneFrameRecorder() noexcept {
        this->stop_noexcept();
    }

    void SceneFrameRecorder::start(const SceneFrameRecordConfig& config, const std::function<SceneFrameSnapshot(const SceneFrameRequest&)>& producer) {
        if (!producer) throw std::runtime_error("Scene frame recorder producer must be valid");
        if (config.output_directory.empty()) throw std::runtime_error("Scene frame record output directory must not be empty");
        if (config.max_host_cache_bytes == 0) throw std::runtime_error("Scene frame record max host cache bytes must be positive");
        if (this->state.producer_thread.joinable() || this->state.writer_thread.joinable()) throw std::runtime_error("Scene frame recorder is already running");

        this->reset();
        ++this->state.run_index;
        this->state.config                   = config;
        this->state.current_output_directory = config.output_directory / std::format("run_{:04}", this->state.run_index);
        this->state.frames_directory         = this->state.current_output_directory / "frames";
        if (std::filesystem::exists(this->state.current_output_directory)) std::filesystem::remove_all(this->state.current_output_directory);
        std::filesystem::create_directories(this->state.frames_directory);

        std::ofstream manifest{this->state.current_output_directory / "manifest.txt"};
        if (!manifest) throw std::runtime_error(std::string{"Failed to create scene frame record manifest: "} + this->state.current_output_directory.string());
        manifest << "spectra_scene_frame_record_version 2\n";

        {
            std::lock_guard lock{this->state.mutex};
            this->state.stop_requested    = false;
            this->state.producer_finished = false;
            this->state.writer_finished   = false;
        }

        this->state.writer_thread   = std::thread{[this] { this->writer_loop(); }};
        this->state.producer_thread = std::thread{[this, producer] { this->producer_loop(producer); }};
    }

    void SceneFrameRecorder::request_stop() noexcept {
        {
            std::lock_guard lock{this->state.mutex};
            this->state.stop_requested = true;
        }
        this->state.condition.notify_all();
    }

    void SceneFrameRecorder::stop() {
        this->request_stop();
        if (this->state.producer_thread.joinable()) this->state.producer_thread.join();
        if (this->state.writer_thread.joinable()) this->state.writer_thread.join();
        std::exception_ptr failure{};
        {
            std::lock_guard lock{this->state.mutex};
            failure = this->state.failure;
        }
        if (failure) std::rethrow_exception(failure);
    }

    void SceneFrameRecorder::stop_noexcept() noexcept {
        try {
            this->stop();
        } catch (...) {
        }
    }

    void SceneFrameRecorder::reset() {
        if (this->state.producer_thread.joinable() || this->state.writer_thread.joinable()) throw std::runtime_error("Cannot reset active scene frame recorder");
        std::lock_guard lock{this->state.mutex};
        this->state.cached_frames.clear();
        this->state.write_queue.clear();
        this->state.host_cache_bytes   = 0;
        this->state.produced_frames    = 0;
        this->state.written_frames     = 0;
        this->state.latest_ready_frame = -1;
        this->state.written_frame_max  = -1;
        this->state.stop_requested     = false;
        this->state.producer_finished  = false;
        this->state.writer_finished    = false;
        this->state.failure            = {};
    }

    bool SceneFrameRecorder::finish_if_ready() {
        const SceneFrameRecordStats current_stats = this->stats();
        if (!current_stats.finished) return false;
        if (this->state.producer_thread.joinable()) this->state.producer_thread.join();
        if (this->state.writer_thread.joinable()) this->state.writer_thread.join();
        {
            std::lock_guard lock{this->state.mutex};
            if (this->state.failure) std::rethrow_exception(this->state.failure);
        }
        return true;
    }

    SceneFrameRecordStats SceneFrameRecorder::stats() const {
        std::lock_guard lock{this->state.mutex};
        if (this->state.failure) std::rethrow_exception(this->state.failure);
        SceneFrameRecordStats stats;
        stats.produced_frames      = this->state.produced_frames;
        stats.written_frames       = this->state.written_frames;
        stats.latest_ready_frame   = this->state.latest_ready_frame;
        stats.written_frame_max    = this->state.written_frame_max;
        stats.host_cache_bytes     = this->state.host_cache_bytes;
        stats.max_host_cache_bytes = this->state.config.max_host_cache_bytes;
        stats.finished             = this->state.producer_finished && this->state.writer_finished;
        return stats;
    }

    bool SceneFrameRecorder::read(const int frame_index, SceneFrameSnapshot& snapshot) const {
        if (frame_index < 0) throw std::runtime_error("Scene frame record index must be non-negative");
        std::filesystem::path path{};
        {
            std::lock_guard lock{this->state.mutex};
            if (this->state.failure) std::rethrow_exception(this->state.failure);
            if (const auto found = this->state.cached_frames.find(frame_index); found != this->state.cached_frames.end()) {
                snapshot = *found->second;
                return true;
            }
            if (frame_index > this->state.written_frame_max) return false;
            path = scene_frame_record_frame_path(this->state.frames_directory, frame_index);
        }
        snapshot = read_scene_frame_snapshot_file(path);
        return true;
    }

    void SceneFrameRecorder::producer_loop(std::function<SceneFrameSnapshot(const SceneFrameRequest&)> producer) {
        try {
            for (int frame_index = 0;; ++frame_index) {
                {
                    std::lock_guard lock{this->state.mutex};
                    if (this->state.failure) std::rethrow_exception(this->state.failure);
                    if (this->state.stop_requested) break;
                }

                SceneFrameSnapshot snapshot = producer(SceneFrameRequest{frame_index, 0.0f, frame_index == 0});
                if (snapshot.frame_index != frame_index) throw std::runtime_error("Scene frame producer returned an unexpected record frame index");
                const std::uint64_t frame_bytes = scene_frame_snapshot_bytes(snapshot);
                if (frame_bytes > this->state.config.max_host_cache_bytes) throw std::runtime_error("Scene frame record cache cannot fit one frame");
                auto frame = std::make_shared<SceneFrameSnapshot>(std::move(snapshot));

                {
                    std::unique_lock lock{this->state.mutex};
                    while (!this->state.stop_requested && !this->state.failure) {
                        this->evict_cache_locked(frame_bytes);
                        if (this->state.host_cache_bytes + frame_bytes <= this->state.config.max_host_cache_bytes) break;
                        this->state.condition.wait(lock);
                    }
                    if (this->state.failure) std::rethrow_exception(this->state.failure);
                    if (this->state.stop_requested && this->state.host_cache_bytes + frame_bytes > this->state.config.max_host_cache_bytes) break;

                    this->state.host_cache_bytes += frame_bytes;
                    this->state.cached_frames.emplace(frame->frame_index, frame);
                    this->state.write_queue.emplace_back(frame);
                    ++this->state.produced_frames;
                    this->state.latest_ready_frame = std::max(this->state.latest_ready_frame, frame->frame_index);
                }
                this->state.condition.notify_all();
            }
            {
                std::lock_guard lock{this->state.mutex};
                this->state.producer_finished = true;
            }
            this->state.condition.notify_all();
        } catch (...) {
            {
                std::lock_guard lock{this->state.mutex};
                this->state.failure           = std::current_exception();
                this->state.stop_requested    = true;
                this->state.producer_finished = true;
            }
            this->state.condition.notify_all();
        }
    }

    void SceneFrameRecorder::writer_loop() {
        try {
            while (true) {
                std::shared_ptr<SceneFrameSnapshot> frame{};
                {
                    std::unique_lock lock{this->state.mutex};
                    this->state.condition.wait(lock, [this] { return this->state.failure || !this->state.write_queue.empty() || (this->state.producer_finished && this->state.write_queue.empty()); });
                    if (this->state.failure) std::rethrow_exception(this->state.failure);
                    if (this->state.write_queue.empty() && this->state.producer_finished) break;
                    frame = this->state.write_queue.front();
                    this->state.write_queue.pop_front();
                }

                write_scene_frame_snapshot_file(scene_frame_record_frame_path(this->state.frames_directory, frame->frame_index), *frame);

                {
                    std::lock_guard lock{this->state.mutex};
                    ++this->state.written_frames;
                    this->state.written_frame_max = std::max(this->state.written_frame_max, frame->frame_index);
                    this->evict_cache_locked(0);
                }
                this->state.condition.notify_all();
            }
            {
                std::lock_guard lock{this->state.mutex};
                this->state.writer_finished = true;
            }
            this->state.condition.notify_all();
        } catch (...) {
            {
                std::lock_guard lock{this->state.mutex};
                this->state.failure         = std::current_exception();
                this->state.stop_requested  = true;
                this->state.writer_finished = true;
            }
            this->state.condition.notify_all();
        }
    }

    void SceneFrameRecorder::evict_cache_locked(const std::uint64_t required_free_bytes) {
        if (required_free_bytes > this->state.config.max_host_cache_bytes) throw std::runtime_error("Scene frame record required cache space exceeds max_host_cache_bytes");
        while (this->state.host_cache_bytes + required_free_bytes > this->state.config.max_host_cache_bytes && !this->state.cached_frames.empty()) {
            auto victim = this->state.cached_frames.begin();
            if (victim->first > this->state.written_frame_max) return;
            this->state.host_cache_bytes -= scene_frame_snapshot_bytes(*victim->second);
            this->state.cached_frames.erase(victim);
        }
    }
} // namespace xayah
