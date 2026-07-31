cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
project(spectra VERSION 2.0.0 LANGUAGES C CXX)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (MSVC)
    add_compile_options(
            "$<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>"
    )
endif ()


set(SPECTRA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SPECTRA_BINARY_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(SPECTRA_VENDOR_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor")
set(SPECTRA_GENERATED_DIR "${SPECTRA_BINARY_ROOT_DIR}/generated")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
list(GET Vulkan_INCLUDE_DIRS 0 SPECTRA_VULKAN_INCLUDE_DIR)
cmake_path(
        GET SPECTRA_VULKAN_INCLUDE_DIR
        PARENT_PATH SPECTRA_VULKAN_SDK_DIR
)
if (NOT EXISTS "${SPECTRA_VULKAN_SDK_DIR}/Include/slang/slang.h"
        OR NOT EXISTS "${SPECTRA_VULKAN_SDK_DIR}/Lib/slang.lib"
        OR NOT EXISTS "${SPECTRA_VULKAN_SDK_DIR}/Bin/slang.dll")
    message(FATAL_ERROR "The Vulkan SDK at ${SPECTRA_VULKAN_SDK_DIR} does not contain its bundled Slang toolchain.")
endif ()
add_library(spectra_slang SHARED IMPORTED)
add_library(spectra::slang ALIAS spectra_slang)
set_target_properties(
        spectra_slang PROPERTIES
        IMPORTED_IMPLIB "${SPECTRA_VULKAN_SDK_DIR}/Lib/slang.lib"
        IMPORTED_LOCATION "${SPECTRA_VULKAN_SDK_DIR}/Bin/slang.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${SPECTRA_VULKAN_SDK_DIR}/Include/slang"
)
find_program(
        SPECTRA_SPIRV_VAL
        NAMES spirv-val
        HINTS "${Vulkan_INCLUDE_DIRS}/../Bin"
        REQUIRED
)

function(spectra_download_if_needed url out_path sha256)
    string(TOLOWER "${sha256}" expected_sha256)
    if (EXISTS "${out_path}")
        file(SHA256 "${out_path}" existing_sha256)
        string(TOLOWER "${existing_sha256}" existing_sha256)
        if (NOT existing_sha256 STREQUAL expected_sha256)
            message(FATAL_ERROR "Cached dependency hash mismatch for ${out_path}: expected ${expected_sha256}, got ${existing_sha256}")
        endif ()
        return()
    endif ()

    cmake_path(GET out_path PARENT_PATH out_dir)
    file(MAKE_DIRECTORY "${out_dir}")
    file(DOWNLOAD
            "${url}"
            "${out_path}"
            TLS_VERIFY ON
            EXPECTED_HASH "SHA256=${sha256}"
            STATUS download_status
    )
    list(GET download_status 0 download_code)
    list(GET download_status 1 download_message)
    if (NOT download_code EQUAL 0)
        file(REMOVE "${out_path}")
        message(FATAL_ERROR "Failed to download ${url}: [${download_code}] ${download_message}")
    endif ()
endfunction()

function(spectra_prepare_archive_dependency out_source_dir owner repository tag archive_sha256)
    set(archive_dir "${SPECTRA_VENDOR_DIR}/${repository}/${tag}")
    set(archive_path "${archive_dir}/${repository}.zip")
    set(extract_dir "${archive_dir}/source")
    string(REGEX REPLACE "^v" "" source_tag "${tag}")
    set(source_dir "${extract_dir}/${repository}-${source_tag}")
    spectra_download_if_needed(
            "https://codeload.github.com/${owner}/${repository}/zip/refs/tags/${tag}"
            "${archive_path}"
            "${archive_sha256}"
    )
    if (NOT EXISTS "${source_dir}")
        file(REMOVE_RECURSE "${extract_dir}")
        file(MAKE_DIRECTORY "${extract_dir}")
        file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${extract_dir}")
    endif ()
    if (NOT EXISTS "${source_dir}")
        message(FATAL_ERROR "Failed to extract ${repository} ${tag}.")
    endif ()
    set(${out_source_dir} "${source_dir}" PARENT_SCOPE)
endfunction()

function(spectra_prepare_commit_dependency out_source_dir owner repository commit archive_sha256)
    set(archive_dir "${SPECTRA_VENDOR_DIR}/${repository}/${commit}")
    set(archive_path "${archive_dir}/${repository}.zip")
    set(extract_dir "${archive_dir}/source")
    set(source_dir "${extract_dir}/${repository}-${commit}")
    spectra_download_if_needed(
            "https://codeload.github.com/${owner}/${repository}/zip/${commit}"
            "${archive_path}"
            "${archive_sha256}"
    )
    if (NOT EXISTS "${source_dir}")
        file(REMOVE_RECURSE "${extract_dir}")
        file(MAKE_DIRECTORY "${extract_dir}")
        file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${extract_dir}")
    endif ()
    if (NOT EXISTS "${source_dir}")
        message(FATAL_ERROR "Failed to extract ${repository} ${commit}.")
    endif ()
    set(${out_source_dir} "${source_dir}" PARENT_SCOPE)
endfunction()

set(SPECTRA_GLAZE_TAG "v7.5.0")
spectra_prepare_archive_dependency(
        SPECTRA_GLAZE_SOURCE_DIR
        stephenberry
        glaze
        "${SPECTRA_GLAZE_TAG}"
        "AABC62B0C0A10FB99DE9245822DC3136B6F3F8346C880B5BBE673ABA1218D952"
)

set(SPECTRA_GLFW_TAG "3.4")
spectra_prepare_archive_dependency(
        SPECTRA_GLFW_SOURCE_DIR
        glfw
        glfw
        "${SPECTRA_GLFW_TAG}"
        "A133DDC3D3C66143EBA9035621DB8E0BCF34DBA1EE9514A9E23E96AFD39FD57A"
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SPECTRA_GLFW_SOURCE_DIR}" "${SPECTRA_BINARY_ROOT_DIR}/glfw" EXCLUDE_FROM_ALL)

set(SPECTRA_IMGUI_COMMIT "b334d19b667958ed970000073644d911fae17e57")
spectra_prepare_commit_dependency(
        SPECTRA_IMGUI_SOURCE_DIR
        ocornut
        imgui
        "${SPECTRA_IMGUI_COMMIT}"
        "504BC8171B80B8C92F035EBC899F6B3086C9CFA56EFADEE4962753DEB38626A2"
)
add_library(spectra_imgui STATIC
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
)
add_library(spectra::imgui ALIAS spectra_imgui)
target_include_directories(spectra_imgui PUBLIC
        "${SPECTRA_IMGUI_SOURCE_DIR}"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends"
)
target_compile_definitions(spectra_imgui PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(spectra_imgui PUBLIC glfw)

set(SPECTRA_IMGUIZMO_COMMIT "dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d")
spectra_prepare_commit_dependency(
        SPECTRA_IMGUIZMO_SOURCE_DIR
        CedricGuillemet
        ImGuizmo
        "${SPECTRA_IMGUIZMO_COMMIT}"
        "0CBB48F6A9041FE18E0D66380AF76BF8EF1D836C6913F9F4E8A0D512F42A3116"
)
add_library(spectra_imguizmo STATIC "${SPECTRA_IMGUIZMO_SOURCE_DIR}/src/ImGuizmo.cpp")
add_library(spectra::imguizmo ALIAS spectra_imguizmo)
target_include_directories(spectra_imguizmo PUBLIC "${SPECTRA_IMGUIZMO_SOURCE_DIR}/src")
target_link_libraries(spectra_imguizmo PUBLIC spectra::imgui)

set(SPECTRA_TINYEXR_TAG "v3.2.0")
spectra_prepare_archive_dependency(
        SPECTRA_TINYEXR_SOURCE_DIR
        syoyo
        tinyexr
        "${SPECTRA_TINYEXR_TAG}"
        "12A458E2E92A7072E26927BA4E53276CE35605EFB2E0418E7C5210CBC81DE2DF"
)
set(SPECTRA_TINYEXR_SOURCES
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_attr.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_b44.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_codec.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_color.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_convert.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_core.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_cpu.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_deep.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_deflate.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_fpnge.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_half.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_jph.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_jph_simd.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_jph_simd_neon.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_mip.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_piz.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_pxr24.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_reader.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_resize.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_rle.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_simd_neon.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_simd_x86.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_spectral.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_stdio.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_thread.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_tonemap.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_util_simd_neon.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_util_simd_x86.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_writer.c"
        "${SPECTRA_TINYEXR_SOURCE_DIR}/src/exr_zip.c"
)
add_library(spectra_tinyexr STATIC ${SPECTRA_TINYEXR_SOURCES})
add_library(spectra::tinyexr ALIAS spectra_tinyexr)
set_target_properties(
        spectra_tinyexr PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
target_compile_definitions(
        spectra_tinyexr PRIVATE
        EXR_NO_ZSTD
        _CRT_SECURE_NO_WARNINGS
        __builtin_clz=_lzcnt_u32
)
target_compile_options(
        spectra_tinyexr PRIVATE
        /experimental:c11atomics
        /wd4556
)
target_include_directories(
        spectra_tinyexr
        SYSTEM PUBLIC "${SPECTRA_TINYEXR_SOURCE_DIR}/include"
        PRIVATE "${SPECTRA_TINYEXR_SOURCE_DIR}/src"
)

set(SPECTRA_OPENVDB_TAG "v13.0.0")
set(SPECTRA_PNANOVDB_INCLUDE_DIR "${SPECTRA_VENDOR_DIR}/openvdb/${SPECTRA_OPENVDB_TAG}/include")
set(SPECTRA_PNANOVDB_HEADER "${SPECTRA_PNANOVDB_INCLUDE_DIR}/nanovdb/PNanoVDB.h")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/AcademySoftwareFoundation/openvdb/${SPECTRA_OPENVDB_TAG}/nanovdb/nanovdb/PNanoVDB.h"
        "${SPECTRA_PNANOVDB_HEADER}"
        "588F213B0DA23F9451DA000CD66566EDA6F42D594E8612093FE4A49519A9AA52"
)

set(CMAKE_CXX_MODULE_STD ON)
# MSVC interprets CMake's relative @cmake_cxx_std.lib entry as a response file.
add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")

add_executable(
        spectra_shader_build
        tools/shader_build.cpp
)
target_link_libraries(
        spectra_shader_build
        PRIVATE spectra::slang
)
target_compile_definitions(spectra_shader_build PRIVATE NOMINMAX)


# ============================================================================
# Vulkan
# ============================================================================
add_library(spectra_vulkan)
add_library(spectra::vulkan ALIAS spectra_vulkan)
target_sources(
        spectra_vulkan
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${Vulkan_INCLUDE_DIRS}"
        FILES
        "${Vulkan_INCLUDE_DIRS}/vulkan/vulkan.cppm"
)
target_compile_definitions(spectra_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
target_link_libraries(spectra_vulkan PUBLIC Vulkan::Vulkan)


# ============================================================================
# Plugin API 5
# ============================================================================
add_library(spectra_plugin_contract INTERFACE)
add_library(spectra::plugin_contract ALIAS spectra_plugin_contract)
target_include_directories(spectra_plugin_contract INTERFACE "${SPECTRA_ROOT_DIR}")

# ============================================================================
# Slang
# ============================================================================
set(SPECTRA_SHADER_OUTPUT_DIR "${SPECTRA_GENERATED_DIR}/shaders")
set(SPECTRA_PATH_ABI_MODULE "${SPECTRA_GENERATED_DIR}/pathtracer/abi.ixx")
set(SPECTRA_SHADER_ENTRY_LIST "${SPECTRA_ROOT_DIR}/tools/shaders.entries")
set(SPECTRA_SHADER_STAMP "${SPECTRA_SHADER_OUTPUT_DIR}/build.stamp")
set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${SPECTRA_SHADER_ENTRY_LIST}"
)
file(
        STRINGS
        "${SPECTRA_SHADER_ENTRY_LIST}"
        SPECTRA_SHADER_ENTRY_LINES
        REGEX "^[^#].*"
)
set(SPECTRA_SHADER_OUTPUTS)
foreach (shader_entry IN LISTS SPECTRA_SHADER_ENTRY_LINES)
    string(
            REGEX MATCH
            "^([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]*$"
            shader_entry_match
            "${shader_entry}"
    )
    if (NOT shader_entry_match)
        message(FATAL_ERROR "Invalid shader entry: ${shader_entry}")
    endif ()
    list(APPEND SPECTRA_SHADER_OUTPUTS "${SPECTRA_SHADER_OUTPUT_DIR}/${CMAKE_MATCH_1}.spv")
endforeach ()
file(
        GLOB
        SPECTRA_SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${SPECTRA_ROOT_DIR}/app/shaders/*.slang"
        "${SPECTRA_ROOT_DIR}/rasterizer/shaders/*.slang"
        "${SPECTRA_ROOT_DIR}/pathtracer/shaders/*.slang"
        "${SPECTRA_ROOT_DIR}/workspace/shaders/*.slang"
        "${SPECTRA_ROOT_DIR}/ui/shaders/*.slang"
)
add_custom_command(
        OUTPUT "${SPECTRA_SHADER_STAMP}"
        BYPRODUCTS
        "${SPECTRA_PATH_ABI_MODULE}"
        ${SPECTRA_SHADER_OUTPUTS}
        COMMAND
        "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:spectra_slang>;$ENV{PATH}"
        "$<TARGET_FILE:spectra_shader_build>"
        "${SPECTRA_ROOT_DIR}/pathtracer/shaders/abi.types"
        "${SPECTRA_PATH_ABI_MODULE}"
        "${SPECTRA_SHADER_ENTRY_LIST}"
        "${SPECTRA_SHADER_OUTPUT_DIR}"
        "${SPECTRA_SPIRV_VAL}"
        "${SPECTRA_ROOT_DIR}/app/shaders"
        "${SPECTRA_ROOT_DIR}/rasterizer/shaders"
        "${SPECTRA_ROOT_DIR}/pathtracer/shaders"
        "${SPECTRA_ROOT_DIR}/workspace/shaders"
        "${SPECTRA_ROOT_DIR}/ui/shaders"
        "${SPECTRA_PNANOVDB_INCLUDE_DIR}"
        COMMAND
        "${CMAKE_COMMAND}" -E touch
        "${SPECTRA_SHADER_STAMP}"
        DEPENDS
        spectra_shader_build
        "$<TARGET_FILE:spectra_slang>"
        "${SPECTRA_ROOT_DIR}/pathtracer/shaders/abi.types"
        "${SPECTRA_SHADER_ENTRY_LIST}"
        "${SPECTRA_PNANOVDB_HEADER}"
        ${SPECTRA_SHADER_SOURCES}
        VERBATIM
)
add_custom_target(spectra_shaders DEPENDS "${SPECTRA_SHADER_STAMP}")


# ============================================================================
# Spectra
# ============================================================================
add_executable(spectra WIN32)
target_sources(
        spectra
        PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/spectra.ixx
        scene/schema.ixx
        scene/scene.ixx
        scene/format.ixx
        plugin/plugin.ixx
        render/assets.ixx
        render/output.ixx
        ui/ui.ixx
        pathtracer/renderer.ixx
        pathtracer/spectrum.ixx
        "${SPECTRA_PATH_ABI_MODULE}"
        rasterizer/renderer.ixx
        workspace/workspace.ixx
        workspace/picker.ixx
        workspace/overlay.ixx
        app/application.ixx
        app/workspace_ui.ixx
        app/image_io.ixx
        app/presenter.ixx
        PRIVATE
        main.cpp
        spectra/spectra.cpp
        scene/schema.cpp
        scene/scene.cpp
        scene/format.cpp
        plugin/plugin.cpp
        render/assets.cpp
        ui/ui.cpp
        pathtracer/spectrum.cpp
        pathtracer/scene.cpp
        pathtracer/session.cpp
        pathtracer/integrator.cpp
        pathtracer/renderer.cpp
        rasterizer/scene.cpp
        rasterizer/renderer.cpp
        workspace/workspace.cpp
        workspace/picker.cpp
        workspace/overlay.cpp
        app/application.cpp
        app/workspace_ui.cpp
        app/image_io.cpp
        app/presenter.cpp
)
target_include_directories(spectra PRIVATE "${SPECTRA_GLAZE_SOURCE_DIR}/include")
target_link_libraries(spectra PRIVATE
        spectra::vulkan
        spectra::plugin_contract
        spectra::imgui
        spectra::imguizmo
        spectra::tinyexr
        glfw
        bcrypt
        dwmapi
        ole32
        shell32
        windowscodecs
)
target_compile_definitions(spectra PRIVATE
        GLFW_EXPOSE_NATIVE_WIN32
        "SPECTRA_DEFAULT_SCENE=\"${SPECTRA_ROOT_DIR}/scenes/cornell-box.spectra\""
        "SPECTRA_SHADER_DIRECTORY=\"${SPECTRA_SHADER_OUTPUT_DIR}\""
)
set_property(
        SOURCE pathtracer/scene.cpp
        APPEND
        PROPERTY COMPILE_DEFINITIONS
        "SPECTRA_SPECTRAL_ASSET_DIRECTORY=\"${SPECTRA_ROOT_DIR}/pathtracer/assets\""
)
add_dependencies(spectra spectra_shaders)
