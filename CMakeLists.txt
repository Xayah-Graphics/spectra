cmake_minimum_required(VERSION 4.4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "f35a9ac6-8463-4d38-8eec-5d6008153e7d")
project(spectra VERSION 2.0.0 LANGUAGES C CXX)

if (WIN32)
    set(SPECTRA_BUILD_UI_DEFAULT ON)
else ()
    set(SPECTRA_BUILD_UI_DEFAULT OFF)
endif ()
option(SPECTRA_BUILD_UI "Build the Windows visualization UI into the spectra executable" ${SPECTRA_BUILD_UI_DEFAULT})
option(SPECTRA_BUILD_PROJECTS "Build the research Simulator Provider projects" OFF)
if (SPECTRA_BUILD_UI AND NOT WIN32)
    message(FATAL_ERROR "SPECTRA_BUILD_UI is currently supported only on Windows.")
endif ()


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
set(CMAKE_CXX_MODULE_STD ON)
if (MSVC)
    # MSVC interprets CMake's relative @cmake_cxx_std.lib entry as a response file.
    add_link_options("$<$<LINK_LANGUAGE:CXX>:${CMAKE_CURRENT_BINARY_DIR}/@cmake_cxx_std.lib>")
endif ()


# ============================================================================
# Dependencies
# ============================================================================
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
    set(archive_dir "${CMAKE_CURRENT_BINARY_DIR}/vendor/${repository}/${tag}")
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
    set(archive_dir "${CMAKE_CURRENT_BINARY_DIR}/vendor/${repository}/${commit}")
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

set(SPECTRA_XCLI_COMMIT "3b4627ae0ae9d2744de8e9d0d5a9d4161cfd4764")
spectra_prepare_commit_dependency(
        SPECTRA_XCLI_SOURCE_DIR
        Xayah-Graphics
        util
        "${SPECTRA_XCLI_COMMIT}"
        "0038A581367976D61D435FAD8C30F52889A9407EBB5A5BAA6ED006401E06C114"
)
add_subdirectory("${SPECTRA_XCLI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xcli" EXCLUDE_FROM_ALL)

set(SPECTRA_CKDL_COMMIT "c9c33fe64446287215e80705545139d92a48f829")
spectra_prepare_commit_dependency(
        SPECTRA_CKDL_SOURCE_DIR
        tjol
        ckdl
        "${SPECTRA_CKDL_COMMIT}"
        "EFC6B7DFA0722AF9270F3F9E376C3CDEA53A4C86E3B5E04AA83697207B4E0320"
)
add_library(spectra_ckdl STATIC
        "${SPECTRA_CKDL_SOURCE_DIR}/src/bigint.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/compat.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/emitter.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/parser.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/str.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/tokenizer.c"
        "${SPECTRA_CKDL_SOURCE_DIR}/src/utf8.c"
)
target_include_directories(spectra_ckdl PUBLIC "${SPECTRA_CKDL_SOURCE_DIR}/include" PRIVATE "${SPECTRA_CKDL_SOURCE_DIR}/src")
target_compile_definitions(spectra_ckdl PUBLIC KDL_STATIC_LIB PRIVATE BUILDING_KDL _CRT_SECURE_NO_WARNINGS)
set_target_properties(spectra_ckdl PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if (UNIX)
    target_link_libraries(spectra_ckdl PRIVATE m)
endif ()

add_library(spectra_ckdlpp STATIC "${SPECTRA_CKDL_SOURCE_DIR}/bindings/cpp/src/kdlpp.cpp")
target_include_directories(spectra_ckdlpp PUBLIC "${SPECTRA_CKDL_SOURCE_DIR}/bindings/cpp/include")
target_compile_definitions(spectra_ckdlpp PUBLIC KDLPP_STATIC_LIB)
target_link_libraries(spectra_ckdlpp PUBLIC spectra_ckdl)
target_compile_features(spectra_ckdlpp PUBLIC cxx_std_23)

if (SPECTRA_BUILD_UI)
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
    add_subdirectory("${SPECTRA_GLFW_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)

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
    target_compile_definitions(spectra_imgui PRIVATE GLFW_INCLUDE_NONE)
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
endif ()

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
find_package(Threads REQUIRED)
target_link_libraries(spectra_tinyexr PRIVATE Threads::Threads "$<$<PLATFORM_ID:Linux>:m>")
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
)
if (MSVC)
    target_compile_definitions(spectra_tinyexr PRIVATE __builtin_clz=_lzcnt_u32)
    target_compile_options(spectra_tinyexr PRIVATE /experimental:c11atomics /wd4556)
endif ()
target_include_directories(
        spectra_tinyexr
        SYSTEM PUBLIC "${SPECTRA_TINYEXR_SOURCE_DIR}/include"
        PRIVATE "${SPECTRA_TINYEXR_SOURCE_DIR}/src"
)

set(SPECTRA_OPENVDB_TAG "v13.0.0")
set(SPECTRA_PNANOVDB_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vendor/openvdb/${SPECTRA_OPENVDB_TAG}/include")
set(SPECTRA_PNANOVDB_HEADER "${SPECTRA_PNANOVDB_INCLUDE_DIR}/nanovdb/PNanoVDB.h")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/AcademySoftwareFoundation/openvdb/${SPECTRA_OPENVDB_TAG}/nanovdb/nanovdb/PNanoVDB.h"
        "${SPECTRA_PNANOVDB_HEADER}"
        "588F213B0DA23F9451DA000CD66566EDA6F42D594E8612093FE4A49519A9AA52"
)


# ============================================================================
# Vulkan
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
list(GET Vulkan_INCLUDE_DIRS 0 SPECTRA_VULKAN_INCLUDE_DIR)
cmake_path(
        GET SPECTRA_VULKAN_INCLUDE_DIR
        PARENT_PATH SPECTRA_VULKAN_SDK_DIR
)
if (WIN32)
    find_path(SPECTRA_SLANG_INCLUDE_DIR slang.h HINTS "${SPECTRA_VULKAN_SDK_DIR}/Include/slang" "${SPECTRA_VULKAN_SDK_DIR}/include/slang" REQUIRED NO_DEFAULT_PATH)
    find_library(SPECTRA_SLANG_LIBRARY NAMES slang HINTS "${SPECTRA_VULKAN_SDK_DIR}/Lib" "${SPECTRA_VULKAN_SDK_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
    find_file(SPECTRA_SLANG_RUNTIME NAMES slang.dll HINTS "${SPECTRA_VULKAN_SDK_DIR}/Bin" REQUIRED NO_DEFAULT_PATH)
    add_library(spectra_slang SHARED IMPORTED)
    set_target_properties(spectra_slang PROPERTIES IMPORTED_IMPLIB "${SPECTRA_SLANG_LIBRARY}" IMPORTED_LOCATION "${SPECTRA_SLANG_RUNTIME}" INTERFACE_INCLUDE_DIRECTORIES "${SPECTRA_SLANG_INCLUDE_DIR}")
    set(SPECTRA_SLANG_TARGET spectra_slang)
else ()
    find_program(SPECTRA_SLANG_COMPILER NAMES slangc REQUIRED)
    file(REAL_PATH "${SPECTRA_SLANG_COMPILER}" SPECTRA_SLANG_COMPILER_REAL)
    cmake_path(GET SPECTRA_SLANG_COMPILER_REAL PARENT_PATH SPECTRA_SLANG_BINARY_DIR)
    cmake_path(GET SPECTRA_SLANG_BINARY_DIR PARENT_PATH SPECTRA_SLANG_INSTALL_DIR)
    find_package(slang CONFIG REQUIRED PATHS "${SPECTRA_SLANG_INSTALL_DIR}/lib/cmake/slang" NO_DEFAULT_PATH)
    set(SPECTRA_SLANG_TARGET slang::slang)
endif ()
add_library(spectra::slang ALIAS ${SPECTRA_SLANG_TARGET})
find_program(
        SPECTRA_SPIRV_VAL
        NAMES spirv-val
        HINTS "${Vulkan_INCLUDE_DIRS}/../Bin"
        REQUIRED
)
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
if (WIN32)
    target_compile_definitions(spectra_vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
endif ()
target_link_libraries(spectra_vulkan PUBLIC Vulkan::Vulkan)


# ============================================================================
# Slang
# ============================================================================
add_executable(
        spectra_shader_compiler
        spectra/core/render/shaders/compiler.cpp
)
target_link_libraries(
        spectra_shader_compiler
        PRIVATE spectra::slang
)
target_compile_definitions(spectra_shader_compiler PRIVATE NOMINMAX)

set(SPECTRA_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
set(SPECTRA_PATH_TRACER_ABI_MODULE "${CMAKE_CURRENT_BINARY_DIR}/generated/path_tracer/abi.ixx")
set(SPECTRA_SHADER_CORE_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/entries_core.txt")
set(SPECTRA_SHADER_EDITOR_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/entries_editor.txt")
set(SPECTRA_SHADER_ENTRIES "${CMAKE_CURRENT_BINARY_DIR}/generated/shader_entries.txt")
file(READ "${SPECTRA_SHADER_CORE_ENTRIES}" SPECTRA_SHADER_ENTRY_CONTENT)
if (SPECTRA_BUILD_UI)
    file(READ "${SPECTRA_SHADER_EDITOR_ENTRIES}" SPECTRA_SHADER_EDITOR_ENTRY_CONTENT)
    string(APPEND SPECTRA_SHADER_ENTRY_CONTENT "\n${SPECTRA_SHADER_EDITOR_ENTRY_CONTENT}")
endif ()
file(CONFIGURE OUTPUT "${SPECTRA_SHADER_ENTRIES}" CONTENT "${SPECTRA_SHADER_ENTRY_CONTENT}" @ONLY)
set(SPECTRA_SHADER_STAMP "${SPECTRA_SHADER_OUTPUT_DIR}/build.stamp")
set_property(
        DIRECTORY
        APPEND
        PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${SPECTRA_SHADER_CORE_ENTRIES}"
)
if (SPECTRA_BUILD_UI)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SPECTRA_SHADER_EDITOR_ENTRIES}")
endif ()
file(
        STRINGS
        "${SPECTRA_SHADER_ENTRIES}"
        SPECTRA_SHADER_ENTRY_LINES
        REGEX "^[^#].*"
)
set(SPECTRA_SHADER_OUTPUTS)
set(SPECTRA_RUNTIME_SHADER_OUTPUTS)
set(SPECTRA_PATH_TRACER_SHADER_OUTPUTS)
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
    set(shader_name "${CMAKE_MATCH_1}")
    set(shader_output "${SPECTRA_SHADER_OUTPUT_DIR}/${shader_name}.spv")
    list(APPEND SPECTRA_SHADER_OUTPUTS "${shader_output}")
    if (shader_name MATCHES "^path_" OR shader_name STREQUAL "volume_majorant")
        list(APPEND SPECTRA_PATH_TRACER_SHADER_OUTPUTS "${shader_output}")
    endif ()
    if (NOT shader_name MATCHES "^path_")
        list(APPEND SPECTRA_RUNTIME_SHADER_OUTPUTS "${shader_output}")
    endif ()
endforeach ()
set(
        SPECTRA_SPECTRAL_ASSETS
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/srgb.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/rec2020.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/aces2065_1.rgb2spec"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/cie1931.spectrum"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/assets/sampling.tables"
)
file(
        GLOB
        SPECTRA_SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/*.slang"
)
set(SPECTRA_SHADER_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders")
if (SPECTRA_BUILD_UI)
    file(GLOB SPECTRA_EDITOR_SHADER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders/*.slang")
    list(APPEND SPECTRA_SHADER_SOURCES ${SPECTRA_EDITOR_SHADER_SOURCES})
    list(PREPEND SPECTRA_SHADER_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/spectra/editor/shaders")
endif ()
add_custom_command(
        OUTPUT "${SPECTRA_SHADER_STAMP}"
        BYPRODUCTS
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        ${SPECTRA_SHADER_OUTPUTS}
        COMMAND
        "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:${SPECTRA_SLANG_TARGET}>"
        "$<TARGET_FILE:spectra_shader_compiler>"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        "${SPECTRA_SHADER_ENTRIES}"
        "${SPECTRA_SHADER_OUTPUT_DIR}"
        "${SPECTRA_SPIRV_VAL}"
        ${SPECTRA_SHADER_INCLUDE_DIRECTORIES}
        "${SPECTRA_PNANOVDB_INCLUDE_DIR}"
        COMMAND
        "${CMAKE_COMMAND}" -E touch
        "${SPECTRA_SHADER_STAMP}"
        DEPENDS
        spectra_shader_compiler
        "$<TARGET_FILE:${SPECTRA_SLANG_TARGET}>"
        "${CMAKE_CURRENT_SOURCE_DIR}/spectra/core/render/shaders/abi.types"
        "${SPECTRA_SHADER_ENTRIES}"
        "${SPECTRA_PNANOVDB_HEADER}"
        ${SPECTRA_SHADER_SOURCES}
        VERBATIM
)
add_custom_target(spectra_shaders DEPENDS "${SPECTRA_SHADER_STAMP}")


# ============================================================================
# Dynamic Provider SDK
# ============================================================================
add_subdirectory(sdk)


# ============================================================================
# Spectra
# ============================================================================
add_library(spectra_core STATIC)
target_sources(
        spectra_core
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        spectra/core/headless.ixx
        spectra/core/runtime/runtime.ixx
        spectra/core/runtime/graphics.ixx
        spectra/core/runtime/resources.ixx
        spectra/core/runtime/frames.ixx
        spectra/core/util/math.ixx
        spectra/core/util/hash.ixx
        spectra/core/scene/scene.ixx
        spectra/core/scene/format.ixx
        spectra/core/scene/document.ixx
        spectra/core/scene/dynamics.ixx
        spectra/core/render/render_common.ixx
        spectra/core/render/rasterizer.ixx
        spectra/core/render/pathtracer/runtime.ixx
        spectra/core/render/pathtracer.ixx
        spectra/core/render/render.ixx
        spectra/core/render/display.ixx
        PUBLIC
        FILE_SET generated_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_BINARY_DIR}"
        FILES "${SPECTRA_PATH_TRACER_ABI_MODULE}"
        PRIVATE
        spectra/core/headless.cpp
        spectra/core/runtime/runtime.cpp
        spectra/core/runtime/graphics.cpp
        spectra/core/runtime/resources.cpp
        spectra/core/runtime/frames.cpp
        spectra/core/util/math.cpp
        spectra/core/util/hash.cpp
        spectra/core/scene/scene.cpp
        spectra/core/scene/format.cpp
        spectra/core/scene/document.cpp
        spectra/core/scene/dynamics.cpp
        spectra/core/render/render_common.cpp
        spectra/core/render/rasterizer.cpp
        spectra/core/render/pathtracer/runtime.cpp
        spectra/core/render/pathtracer.cpp
        spectra/core/render/render.cpp
        spectra/core/render/display.cpp
)
target_link_libraries(
        spectra_core
        PUBLIC
        spectra::vulkan
        spectra::plugin_contract
        PRIVATE
        spectra_ckdlpp
        spectra::tinyexr
)
if (UNIX AND NOT APPLE)
    target_link_libraries(spectra_core PRIVATE dl)
endif ()
add_dependencies(spectra_core spectra_shaders)

if (SPECTRA_BUILD_UI)
    add_library(spectra_editor STATIC)
    target_sources(
            spectra_editor
            PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            spectra/editor/editor.ixx
            PRIVATE
            FILE_SET editor_partitions TYPE CXX_MODULES
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES
            spectra/editor/platform/window.ixx
            spectra/editor/platform/presentation.ixx
            spectra/editor/platform/dialogs.ixx
            spectra/editor/viewport/interaction.ixx
            spectra/editor/viewport/picker.ixx
            spectra/editor/viewport/overlay.ixx
            spectra/editor/ui/imgui.ixx
            spectra/editor/ui/ui.ixx
            spectra/editor/output/capture.ixx
            spectra/editor/output/frozen_scene.ixx
            PRIVATE
            spectra/editor/editor.cpp
            spectra/editor/platform/window.cpp
            spectra/editor/platform/presentation.cpp
            spectra/editor/platform/dialogs.cpp
            spectra/editor/viewport/interaction.cpp
            spectra/editor/viewport/picker.cpp
            spectra/editor/viewport/overlay.cpp
            spectra/editor/ui/imgui.cpp
            spectra/editor/ui/ui.cpp
            spectra/editor/output/capture.cpp
            spectra/editor/output/frozen_scene.cpp
    )
    target_link_libraries(
            spectra_editor
            PRIVATE spectra_core spectra::imgui spectra::imguizmo glfw dwmapi ole32
    )
    target_compile_definitions(
            spectra_editor
            PRIVATE
            GLFW_INCLUDE_NONE
            GLFW_EXPOSE_NATIVE_WIN32
    )
endif ()

add_executable(spectra)
target_sources(
        spectra
        PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES spectra/spectra.ixx
        PRIVATE
        main.cpp
)
target_link_libraries(spectra PRIVATE spectra_core xayah::util::xcli)
if (SPECTRA_BUILD_UI)
    target_compile_definitions(spectra PRIVATE SPECTRA_HAS_EDITOR)
    target_link_libraries(spectra PRIVATE spectra_editor)
endif ()

add_custom_target(
        spectra_runtime_resources ALL
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:spectra>/resources/shaders"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:spectra>/resources/pathtracer/shaders"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:spectra>/resources/pathtracer/spectral"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SPECTRA_RUNTIME_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:spectra>/resources/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SPECTRA_PATH_TRACER_SHADER_OUTPUTS} "$<TARGET_FILE_DIR:spectra>/resources/pathtracer/shaders"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${SPECTRA_SPECTRAL_ASSETS} "$<TARGET_FILE_DIR:spectra>/resources/pathtracer/spectral"
        DEPENDS spectra
        VERBATIM
)
install(TARGETS spectra RUNTIME DESTINATION "." COMPONENT Spectra)
install(FILES ${SPECTRA_RUNTIME_SHADER_OUTPUTS} DESTINATION "resources/shaders" COMPONENT Spectra)
install(FILES ${SPECTRA_PATH_TRACER_SHADER_OUTPUTS} DESTINATION "resources/pathtracer/shaders" COMPONENT Spectra)
install(FILES ${SPECTRA_SPECTRAL_ASSETS} DESTINATION "resources/pathtracer/spectral" COMPONENT Spectra)

if (SPECTRA_BUILD_PROJECTS)
    add_subdirectory(projects)
endif ()
