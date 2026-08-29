include_guard(GLOBAL)

spectra_require_dependency(imgui)
FetchContent_MakeAvailable(imguizmo)

add_library(spectra_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
add_library(spectra::imguizmo ALIAS spectra_imguizmo)
target_include_directories(spectra_imguizmo PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(spectra_imguizmo PUBLIC spectra::imgui)
