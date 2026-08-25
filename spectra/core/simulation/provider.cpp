module;

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <abi.h>

#undef interface

module spectra.simulation.provider;

import std;

namespace spectra::simulation {
    ProviderLibrary::ProviderLibrary(const std::filesystem::path& path) {
#if !defined(_WIN32)
        throw std::runtime_error("Spectra simulation providers are supported only on Windows");
#else
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path);
        const HMODULE loaded                  = LoadLibraryW(canonical.c_str());
        if (!loaded) throw std::runtime_error(std::format("Windows failed to load simulation provider: {}", canonical.string()));
        const auto* dos_header = reinterpret_cast<const IMAGE_DOS_HEADER*>(loaded);
        const auto* nt_headers = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const std::byte*>(loaded) + dos_header->e_lfanew);
        this->build_time        = std::chrono::sys_seconds{std::chrono::seconds{nt_headers->FileHeader.TimeDateStamp}};
        try {
            const auto entry = reinterpret_cast<const SpectraSdkApi* (*) () noexcept>(GetProcAddress(loaded, SPECTRA_SDK_ENTRY_NAME));
            if (!entry) throw std::runtime_error(std::format("Simulation provider does not export Spectra SDK ABI {}", SPECTRA_SDK_ABI_VERSION));
            this->api = entry();
            if (this->api->abi_version != SPECTRA_SDK_ABI_VERSION || this->api->struct_size != sizeof(SpectraSdkApi)) throw std::runtime_error("Simulation provider has an incompatible Spectra SDK ABI");
            const SpectraSdkProviderDescriptionResult described = this->api->describe_provider();
            if (described.result.error.size != 0u) throw std::runtime_error(std::format("Provider description failed: {}", std::string_view{described.result.error.data, described.result.error.size}));
            this->descriptor = described.descriptor;
        } catch (...) {
            FreeLibrary(loaded);
            throw;
        }
        this->handle = loaded;
#endif
    }

    ProviderLibrary::~ProviderLibrary() {
#if defined(_WIN32)
        if (this->handle) FreeLibrary(static_cast<HMODULE>(this->handle));
#endif
    }
} // namespace spectra::simulation
