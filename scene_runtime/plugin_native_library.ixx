export module spectra.scene_runtime.plugin_native_library;

import std;

namespace spectra::scene_runtime {
    export class NativeLibrary final {
    public:
        explicit NativeLibrary(std::filesystem::path path);
        NativeLibrary(const NativeLibrary& other) = delete;
        NativeLibrary(NativeLibrary&& other) = delete;
        NativeLibrary& operator=(const NativeLibrary& other) = delete;
        NativeLibrary& operator=(NativeLibrary&& other) = delete;
        ~NativeLibrary() noexcept;

        [[nodiscard]] void* symbol(const char* name) const;

    private:
        std::filesystem::path path{};
        void* handle{};
    };
} // namespace spectra::scene_runtime
