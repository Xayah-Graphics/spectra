module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.scene_runtime.plugin_native_library;

import std;

namespace spectra::scene_runtime {
    NativeLibrary::NativeLibrary(std::filesystem::path path) : path(std::move(path)) {
#if defined(_WIN32)
        this->handle = static_cast<void*>(::LoadLibraryW(this->path.wstring().c_str()));
        if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin, Win32 error {}", this->path.string(), ::GetLastError()));
#else
        this->handle = ::dlopen(this->path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load dynamic scene plugin: {}", this->path.string(), ::dlerror()));
#endif
    }

    NativeLibrary::~NativeLibrary() noexcept {
#if defined(_WIN32)
        if (this->handle != nullptr) static_cast<void>(::FreeLibrary(static_cast<HMODULE>(this->handle)));
#else
        if (this->handle != nullptr) static_cast<void>(::dlclose(this->handle));
#endif
    }

    void* NativeLibrary::symbol(const char* name) const {
#if defined(_WIN32)
        void* symbol_address = reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(this->handle), name));
        if (symbol_address == nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\", Win32 error {}", this->path.string(), name, ::GetLastError()));
        return symbol_address;
#else
        ::dlerror();
        void* symbol_address = ::dlsym(this->handle, name);
        const char* error = ::dlerror();
        if (error != nullptr) throw std::runtime_error(std::format("{}: dynamic scene plugin is missing export \"{}\": {}", this->path.string(), name, error));
        return symbol_address;
#endif
    }
} // namespace spectra::scene_runtime
