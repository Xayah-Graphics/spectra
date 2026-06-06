add_library(spectra_core)
add_library(spectra::core ALIAS spectra_core)
target_sources(
        spectra_core
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra.ixx
        PRIVATE
        spectra.cpp
)
target_link_libraries(spectra_core PUBLIC Vulkan::Vulkan PRIVATE glfw vendor::imgui)
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)
target_include_directories(spectra_core PUBLIC ${SPECTRA_ROOT_DIR}/vendor/imgui PRIVATE ${SPECTRA_ROOT_DIR}/vendor)
