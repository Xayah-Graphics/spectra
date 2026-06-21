export module spectra.dynamic_scene.host;

export import spectra.dynamic_scene.contracts;
import std;

namespace spectra::dynamic_scene {
    export class HostServices {
    public:
        HostServices() = default;
        HostServices(const HostServices& other) = delete;
        HostServices(HostServices&& other) = delete;
        HostServices& operator=(const HostServices& other) = delete;
        HostServices& operator=(HostServices&& other) = delete;
        virtual ~HostServices() noexcept = default;

        [[nodiscard]] virtual GpuBufferAllocation request_gpu_buffer(const GpuBufferRequest& request) = 0;
        virtual void release_gpu_buffer(std::uint64_t resource_id) = 0;
        [[nodiscard]] virtual std::string_view last_error() const = 0;
    };

    export class HostServiceRouter final : public HostServices {
    public:
        HostServiceRouter() = default;
        HostServiceRouter(const HostServiceRouter& other) = delete;
        HostServiceRouter(HostServiceRouter&& other) = delete;
        HostServiceRouter& operator=(const HostServiceRouter& other) = delete;
        HostServiceRouter& operator=(HostServiceRouter&& other) = delete;
        ~HostServiceRouter() noexcept override = default;

        void set_gpu_buffer_backend(std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback);
        void clear_gpu_buffer_backend() noexcept;
        [[nodiscard]] GpuBufferAllocation request_gpu_buffer(const GpuBufferRequest& request) override;
        void release_gpu_buffer(std::uint64_t resource_id) override;
        [[nodiscard]] std::string_view last_error() const override;

    private:
        std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_gpu_buffer_callback{};
        std::move_only_function<void(std::uint64_t)> release_gpu_buffer_callback{};
        std::map<std::uint64_t, GpuBufferAllocation> gpu_buffer_allocations{};
        std::string last_error_message{};
    };

} // namespace spectra::dynamic_scene
