module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

export module xayah.spectra.pathtracer;

import std;
export import xayah.spectra;

export namespace xayah {
    class SpectraPathtracer final : public SpectraPlugin {
    public:
        explicit SpectraPathtracer(std::string scene_name);
        ~SpectraPathtracer() noexcept override;

        SpectraPathtracer(const SpectraPathtracer& other)                = delete;
        SpectraPathtracer(SpectraPathtracer&& other) noexcept            = delete;
        SpectraPathtracer& operator=(const SpectraPathtracer& other)     = delete;
        SpectraPathtracer& operator=(SpectraPathtracer&& other) noexcept = delete;

        [[nodiscard]] std::string_view name() const override;
        void attach(Spectra& spectra) override;
        void detach(Spectra& spectra) noexcept override;
        void before_imgui_shutdown(Spectra& spectra) noexcept override;
        void after_imgui_created(Spectra& spectra) override;
        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& spectra, const SpectraFrameInfo& frame) override;
        void record_frame(const vk::raii::CommandBuffer& command_buffer) override;

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace xayah
