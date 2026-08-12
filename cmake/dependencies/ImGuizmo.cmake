spectra_prepare_commit_dependency(
        SPECTRA_IMGUIZMO_SOURCE_DIR
        CedricGuillemet
        ImGuizmo
        "dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d"
        "0CBB48F6A9041FE18E0D66380AF76BF8EF1D836C6913F9F4E8A0D512F42A3116"
)
add_library(spectra_imguizmo STATIC "${SPECTRA_IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp")
add_library(spectra::imguizmo ALIAS spectra_imguizmo)
target_include_directories(spectra_imguizmo PUBLIC "${SPECTRA_IMGUIZMO_SOURCE_DIR}/src")
target_link_libraries(spectra_imguizmo PUBLIC spectra::imgui)
