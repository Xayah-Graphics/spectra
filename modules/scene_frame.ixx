export module scene_frame;
export import scene;
import std;

namespace xayah {
    export struct SceneFrameRequest {
        int frame_index{0};
        float delta_seconds{0.0f};
        bool reset_stream{false};
    };

    export template <typename Producer>
    concept SceneFrameProducer = requires(Producer& producer, const SceneFrameRequest& request) {
        { producer(request) } -> std::same_as<SceneFrameSnapshot>;
    };

    export struct SceneFrameRecordConfig {
        std::filesystem::path output_directory{"build/spectra_record"};
        std::uint64_t max_host_cache_bytes{1024ull * 1024ull * 1024ull};
    };

    export struct SceneFrameRecordStats {
        int produced_frames{0};
        int written_frames{0};
        int latest_ready_frame{-1};
        int written_frame_max{-1};
        std::uint64_t host_cache_bytes{0};
        std::uint64_t max_host_cache_bytes{0};
        bool finished{false};
    };

    export class SceneFrameRecorder {
    public:
        SceneFrameRecorder();
        ~SceneFrameRecorder() noexcept;

        SceneFrameRecorder(const SceneFrameRecorder& other)                = delete;
        SceneFrameRecorder(SceneFrameRecorder&& other) noexcept            = delete;
        SceneFrameRecorder& operator=(const SceneFrameRecorder& other)     = delete;
        SceneFrameRecorder& operator=(SceneFrameRecorder&& other) noexcept = delete;

        void start(const SceneFrameRecordConfig& config, const std::function<SceneFrameSnapshot(const SceneFrameRequest&)>& producer);
        void request_stop() noexcept;
        void stop();
        void stop_noexcept() noexcept;
        void reset();
        [[nodiscard]] bool finish_if_ready();
        [[nodiscard]] SceneFrameRecordStats stats() const;
        [[nodiscard]] bool read(int frame_index, SceneFrameSnapshot& snapshot) const;

    private:
        void producer_loop(std::function<SceneFrameSnapshot(const SceneFrameRequest&)> producer);
        void writer_loop();
        void evict_cache_locked(std::uint64_t required_free_bytes);

        struct {
            SceneFrameRecordConfig config{};
            std::filesystem::path current_output_directory{};
            std::filesystem::path frames_directory{};
            int run_index{0};
            mutable std::mutex mutex{};
            std::condition_variable condition{};
            std::map<int, std::shared_ptr<SceneFrameSnapshot>> cached_frames{};
            std::deque<std::shared_ptr<SceneFrameSnapshot>> write_queue{};
            std::thread producer_thread{};
            std::thread writer_thread{};
            std::uint64_t host_cache_bytes{0};
            int produced_frames{0};
            int written_frames{0};
            int latest_ready_frame{-1};
            int written_frame_max{-1};
            bool stop_requested{false};
            bool producer_finished{false};
            bool writer_finished{false};
            std::exception_ptr failure{};
        } state;
    };
} // namespace xayah
