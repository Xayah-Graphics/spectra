add_library(spectra_scene)
add_library(spectra::scene ALIAS spectra_scene)

target_sources(
        spectra_scene
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene.ixx
        pbrt.ixx
        pbrt_preview.ixx
        PRIVATE
        scene.cpp
        pbrt.cpp
        pbrt_preview.cpp
)
target_compile_definitions(spectra_scene PRIVATE "SPECTRA_SCENES_ROOT=\"${SPECTRA_SCENES_ROOT}\"")
target_link_libraries(spectra_scene PRIVATE ZLIB::ZLIB)
set_property(TARGET spectra_scene PROPERTY CXX_MODULE_STD ON)
