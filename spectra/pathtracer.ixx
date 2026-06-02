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
    class SpectraPathtracer final {
    public:
        explicit SpectraPathtracer(std::string scene_name);
        ~SpectraPathtracer() noexcept;

        SpectraPathtracer(const SpectraPathtracer& other) = delete;
        SpectraPathtracer(SpectraPathtracer&& other) noexcept;
        SpectraPathtracer& operator=(const SpectraPathtracer& other) = delete;
        SpectraPathtracer& operator=(SpectraPathtracer&& other) noexcept;

        [[nodiscard]] std::string_view name() const;
        void attach(Spectra& spectra);
        void detach(Spectra& spectra) noexcept;
        void before_imgui_shutdown(Spectra& spectra) noexcept;
        void after_imgui_created(Spectra& spectra);
        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& spectra, const SpectraFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace xayah
