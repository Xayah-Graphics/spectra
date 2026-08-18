module;

#include <Windows.h>

#include <shlobj.h>
#include <wrl/client.h>

#undef interface

module spectra.editor.platform.dialogs;
import std;

namespace spectra::editor {
    Dialogs::Dialogs(WindowPlatform& platform) : platform{platform} {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (result != S_OK && result != S_FALSE) throw std::runtime_error(std::format("COM initialization failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
    }

    Dialogs::~Dialogs() {
        CoUninitialize();
    }

    std::optional<std::filesystem::path> Dialogs::choose_scene_file() {
        Microsoft::WRL::ComPtr<IFileOpenDialog> dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows open dialog");
        constexpr std::array filters{COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"}};
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetTitle(L"Open Spectra Scene");
        const HRESULT shown = dialog->Show(this->platform.native_window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
        if (FAILED(shown)) throw std::runtime_error("The Windows open dialog failed");
        Microsoft::WRL::ComPtr<IShellItem> item{};
        if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows open dialog returned no item");
        PWSTR path{};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows open dialog returned no filesystem path");
        const std::filesystem::path result{path};
        CoTaskMemFree(path);
        return result;
    }

    std::optional<std::filesystem::path> Dialogs::choose_scene_save_path(const std::filesystem::path& current_path) {
        Microsoft::WRL::ComPtr<IFileSaveDialog> dialog{};
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) throw std::runtime_error("Failed to create the Windows save dialog");
        constexpr std::array filters{COMDLG_FILTERSPEC{L"Spectra Scene", L"*.spectra"}};
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
        dialog->SetDefaultExtension(L"spectra");
        dialog->SetTitle(L"Save Spectra Scene");
        dialog->SetFileName(current_path.filename().c_str());
        const HRESULT shown = dialog->Show(this->platform.native_window);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return std::nullopt;
        if (FAILED(shown)) throw std::runtime_error("The Windows save dialog failed");
        Microsoft::WRL::ComPtr<IShellItem> item{};
        if (FAILED(dialog->GetResult(&item))) throw std::runtime_error("The Windows save dialog returned no item");
        PWSTR path{};
        if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) throw std::runtime_error("The Windows save dialog returned no filesystem path");
        const std::filesystem::path result{path};
        CoTaskMemFree(path);
        return result;
    }

    SceneReplacementDecision Dialogs::confirm_scene_replacement() const noexcept {
        const int result = MessageBoxW(this->platform.native_window, L"The current Spectra scene has unsaved changes.\n\nSave before continuing?", L"Spectra", MB_ICONWARNING | MB_YESNOCANCEL);
        if (result == IDYES) return SceneReplacementDecision::Save;
        if (result == IDNO) return SceneReplacementDecision::Discard;
        return SceneReplacementDecision::Cancel;
    }
} // namespace spectra::editor
