add_library(spectra_util)
add_library(spectra::util ALIAS spectra_util)
target_sources(
        spectra_util
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        util/math.ixx
        PRIVATE
        util/math.cpp
)
set_property(TARGET spectra_util PROPERTY CXX_MODULE_STD ON)

add_library(spectra_scene)
add_library(spectra::scene ALIAS spectra_scene)
target_sources(
        spectra_scene
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene.ixx
        scene_pbrt.ixx
        PRIVATE
        scene.cpp
        scene_pbrt.cpp
)
target_link_libraries(spectra_scene PUBLIC spectra::util PRIVATE ZLIB::ZLIB)
set_property(TARGET spectra_scene PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_scene PRIVATE "SPECTRA_PROJECT_SCENE_ROOT=\"${SPECTRA_PROJECT_SCENE_ROOT}\"")

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
target_compile_definitions(spectra_core PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_core PUBLIC ${SPECTRA_ROOT_DIR}/vendor/imgui PRIVATE ${SPECTRA_ROOT_DIR}/vendor)

add_library(spectra_scene_library)
add_library(spectra::scene_library ALIAS spectra_scene_library)
target_sources(
        spectra_scene_library
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene_library.ixx
        PRIVATE
        scene_library.cpp
)
target_link_libraries(spectra_scene_library PUBLIC spectra::core spectra::scene PRIVATE vendor::imgui)
set_property(TARGET spectra_scene_library PROPERTY CXX_MODULE_STD ON)
target_compile_definitions(spectra_scene_library PRIVATE IMGUI_DEFINE_MATH_OPERATORS)
target_include_directories(spectra_scene_library PRIVATE ${SPECTRA_ROOT_DIR}/vendor)
