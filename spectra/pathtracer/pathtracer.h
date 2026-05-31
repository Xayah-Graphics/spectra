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
        void attach(SpectraContext& context) override;
        void detach(SpectraContext& context) noexcept override;
        void before_imgui_shutdown(SpectraContext& context) noexcept override;
        void after_imgui_created(SpectraContext& context) override;
        void begin_frame(SpectraFrameContext& context) override;
        void record_frame(SpectraRecordContext& context) override;

    private:
        void register_panels(SpectraContext& context);

        xayah::pathtracer::InteractiveSession session;
    };
} // namespace xayah

#endif
