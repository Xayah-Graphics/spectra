module;

#include <abi.h>

export module spectra.simulation.provider;

import spectra.simulation;
import std;

namespace spectra::simulation {
    export struct ProviderLibrary {
        explicit ProviderLibrary(const std::filesystem::path& path);
        ~ProviderLibrary();

        ProviderLibrary(const ProviderLibrary&)            = delete;
        ProviderLibrary(ProviderLibrary&&)                 = delete;
        ProviderLibrary& operator=(const ProviderLibrary&) = delete;
        ProviderLibrary& operator=(ProviderLibrary&&)      = delete;

        void* handle{};
        const SpectraSdkApi* api{};
        SpectraSdkProviderDescriptor descriptor{};
        ProviderDescriptor provider{};
    };
} // namespace spectra::simulation
