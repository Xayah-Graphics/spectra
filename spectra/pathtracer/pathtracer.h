#ifndef XAYAH_SPECTRA_PATHTRACER_PATHTRACER_H
#define XAYAH_SPECTRA_PATHTRACER_PATHTRACER_H

#include "../spectra.h"
#include "session.h"

#include <filesystem>
#include <string_view>

namespace xayah {
    class SpectraPathtracer final : public SpectraPlugin {
    public:
        explicit SpectraPathtracer(std::filesystem::path scene_path);
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
        void register_panels(Spectra& spectra);

        xayah::pathtracer::InteractiveSession session;
    };
} // namespace xayah

#endif
