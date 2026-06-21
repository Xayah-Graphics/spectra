export module spectra.scene_runtime.host_services;

export import spectra.scene_runtime.contracts;
import std;

namespace spectra::scene_runtime {
    export class DynamicSceneHostServices {
    public:
        DynamicSceneHostServices() = default;
        DynamicSceneHostServices(const DynamicSceneHostServices& other) = delete;
        DynamicSceneHostServices(DynamicSceneHostServices&& other) = delete;
        DynamicSceneHostServices& operator=(const DynamicSceneHostServices& other) = delete;
        DynamicSceneHostServices& operator=(DynamicSceneHostServices&& other) = delete;
        virtual ~DynamicSceneHostServices() noexcept = default;

        [[nodiscard]] virtual DynamicSceneGpuBufferAllocation request_gpu_buffer(const DynamicSceneGpuBufferRequest& request) = 0;
        virtual void release_gpu_buffer(std::uint64_t resource_id) = 0;
        [[nodiscard]] virtual std::string_view last_error() const = 0;
    };

    export class DynamicSceneHostServiceRouter final : public DynamicSceneHostServices {
    public:
        DynamicSceneHostServiceRouter() = default;
        DynamicSceneHostServiceRouter(const DynamicSceneHostServiceRouter& other) = delete;
        DynamicSceneHostServiceRouter(DynamicSceneHostServiceRouter&& other) = delete;
        DynamicSceneHostServiceRouter& operator=(const DynamicSceneHostServiceRouter& other) = delete;
        DynamicSceneHostServiceRouter& operator=(DynamicSceneHostServiceRouter&& other) = delete;
        ~DynamicSceneHostServiceRouter() noexcept override = default;

        void set_gpu_buffer_backend(std::move_only_function<DynamicSceneGpuBufferAllocation(const DynamicSceneGpuBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback);
        void clear_gpu_buffer_backend() noexcept;
        [[nodiscard]] DynamicSceneGpuBufferAllocation request_gpu_buffer(const DynamicSceneGpuBufferRequest& request) override;
        void release_gpu_buffer(std::uint64_t resource_id) override;
        [[nodiscard]] std::string_view last_error() const override;

    private:
        std::move_only_function<DynamicSceneGpuBufferAllocation(const DynamicSceneGpuBufferRequest&)> request_gpu_buffer_callback{};
        std::move_only_function<void(std::uint64_t)> release_gpu_buffer_callback{};
        std::map<std::uint64_t, DynamicSceneGpuBufferAllocation> gpu_buffer_allocations{};
        std::string last_error_message{};
    };

} // namespace spectra::scene_runtime
