cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(spectra VERSION 1.1.1 LANGUAGES C CXX CUDA)


# ============================================================================
# C++ Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)


# ============================================================================
# CUDA Configuration
# ============================================================================
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES 120)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


set(SPECTRA_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(SPECTRA_BINARY_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(SPECTRA_SCENES_ROOT "${SPECTRA_ROOT_DIR}/scenes")


# ============================================================================
# Dependencies
# ============================================================================
find_package(Vulkan 1.4 REQUIRED)
find_package(Threads REQUIRED)

function(spectra_download_if_needed url out_path)
    if (EXISTS "${out_path}")
        message(STATUS "Using cached file: ${out_path}")
        return()
    endif ()

    get_filename_component(out_dir "${out_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${out_dir}")

    message(STATUS "Downloading ${url}")
    set(tmp_path "${out_path}.download")
    file(REMOVE "${tmp_path}")
    file(DOWNLOAD
            "${url}"
            "${tmp_path}"
            TLS_VERIFY ON
            STATUS dl_status
    )

    list(GET dl_status 0 dl_code)
    list(GET dl_status 1 dl_msg)
    if (NOT dl_code EQUAL 0)
        file(REMOVE "${tmp_path}")
        message(FATAL_ERROR "Failed to download ${url}: [${dl_code}] ${dl_msg}")
    endif ()
    file(RENAME "${tmp_path}" "${out_path}")
endfunction()

function(spectra_download_archive_if_needed url out_path sha256)
    string(TOLOWER "${sha256}" expected_sha256)
    if (EXISTS "${out_path}")
        file(SHA256 "${out_path}" existing_sha256)
        string(TOLOWER "${existing_sha256}" existing_sha256)
        if (NOT existing_sha256 STREQUAL expected_sha256)
            message(FATAL_ERROR "Cached archive hash mismatch for ${out_path}: expected ${expected_sha256}, got ${existing_sha256}")
        endif ()
        message(STATUS "Using cached archive: ${out_path}")
        return()
    endif ()

    get_filename_component(out_dir "${out_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${out_dir}")

    message(STATUS "Downloading ${url}")
    set(tmp_path "${out_path}.download")
    file(REMOVE "${tmp_path}")
    file(DOWNLOAD
            "${url}"
            "${tmp_path}"
            TLS_VERIFY ON
            EXPECTED_HASH "SHA256=${sha256}"
            STATUS dl_status
    )

    list(GET dl_status 0 dl_code)
    list(GET dl_status 1 dl_msg)
    if (NOT dl_code EQUAL 0)
        file(REMOVE "${tmp_path}")
        message(FATAL_ERROR "Failed to download ${url}: [${dl_code}] ${dl_msg}")
    endif ()
    file(RENAME "${tmp_path}" "${out_path}")
endfunction()

function(spectra_prepare_archive_dependency out_source_dir owner repository commit archive_sha256 cache_name)
    set(cache_dir "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/${cache_name}/${commit}")
    set(archive_path "${cache_dir}/${cache_name}.zip")
    set(extract_dir "${cache_dir}/source")
    set(source_dir "${extract_dir}/${repository}-${commit}")
    spectra_download_archive_if_needed(
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
        message(FATAL_ERROR "Failed to extract ${cache_name} archive to ${source_dir}.")
    endif ()
    set(${out_source_dir} "${source_dir}" PARENT_SCOPE)
endfunction()

set(SPECTRA_XCLI_COMMIT "bb261075e7c8a4722b23f42c3be01aa24024731a")
set(SPECTRA_XCLI_VENDOR_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/util/${SPECTRA_XCLI_COMMIT}")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/Xayah-Graphics/util/${SPECTRA_XCLI_COMMIT}/xcli.ixx"
        "${SPECTRA_XCLI_VENDOR_DIR}/xcli.ixx"
)
spectra_download_if_needed(
        "https://raw.githubusercontent.com/Xayah-Graphics/util/${SPECTRA_XCLI_COMMIT}/xcli.cpp"
        "${SPECTRA_XCLI_VENDOR_DIR}/xcli.cpp"
)

add_library(xayah-util-xcli)
target_sources(
        xayah-util-xcli
        PRIVATE
        "${SPECTRA_XCLI_VENDOR_DIR}/xcli.cpp"
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "${SPECTRA_XCLI_VENDOR_DIR}/xcli.ixx"
)
set_property(TARGET xayah-util-xcli PROPERTY CXX_MODULE_STD ON)

set(SPECTRA_LODEPNG_COMMIT "ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a")
set(SPECTRA_LODEPNG_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/lodepng/${SPECTRA_LODEPNG_COMMIT}")
foreach (lodepng_file IN ITEMS
        lodepng.cpp
        lodepng.h
)
    spectra_download_if_needed(
            "https://raw.githubusercontent.com/lvandeve/lodepng/${SPECTRA_LODEPNG_COMMIT}/${lodepng_file}"
            "${SPECTRA_LODEPNG_DIR}/lodepng/${lodepng_file}"
    )
endforeach ()

add_library(spectra_lodepng STATIC "${SPECTRA_LODEPNG_DIR}/lodepng/lodepng.cpp")
add_library(spectra::lodepng ALIAS spectra_lodepng)
target_include_directories(spectra_lodepng PUBLIC "${SPECTRA_LODEPNG_DIR}")

set(SPECTRA_STB_COMMIT "31c1ad37456438565541f4919958214b6e762fb4")
set(SPECTRA_STB_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/stb/${SPECTRA_STB_COMMIT}")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/nothings/stb/${SPECTRA_STB_COMMIT}/stb_image.h"
        "${SPECTRA_STB_DIR}/stb/stb_image.h"
)

set(SPECTRA_QOI_COMMIT "97bacc86a9c4abf5a2d452102dc26546c4c670b9")
set(SPECTRA_QOI_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/qoi/${SPECTRA_QOI_COMMIT}")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/phoboslab/qoi/${SPECTRA_QOI_COMMIT}/qoi.h"
        "${SPECTRA_QOI_DIR}/qoi/qoi.h"
)

set(SPECTRA_NVPRO_CORE2_COMMIT "eb7c2f25859b0ff279e3bbd8376a8418528da556")
set(SPECTRA_UI_ASSET_INCLUDE_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/nvpro_core2/${SPECTRA_NVPRO_CORE2_COMMIT}")
spectra_download_if_needed(
        "https://raw.githubusercontent.com/nvpro-samples/nvpro_core2/${SPECTRA_NVPRO_CORE2_COMMIT}/third_party/roboto/roboto_regular.h"
        "${SPECTRA_UI_ASSET_INCLUDE_DIR}/roboto/roboto_regular.h"
)
spectra_download_if_needed(
        "https://raw.githubusercontent.com/nvpro-samples/nvpro_core2/${SPECTRA_NVPRO_CORE2_COMMIT}/third_party/material_symbols/material_symbols_rounded_regular.h"
        "${SPECTRA_UI_ASSET_INCLUDE_DIR}/material_symbols/material_symbols_rounded_regular.h"
)
spectra_download_if_needed(
        "https://raw.githubusercontent.com/nvpro-samples/nvpro_core2/${SPECTRA_NVPRO_CORE2_COMMIT}/nvgui/IconsMaterialSymbols.h"
        "${SPECTRA_UI_ASSET_INCLUDE_DIR}/material_symbols/IconsMaterialSymbols.h"
)

set(SPECTRA_GLFW_COMMIT "7b6aead9fb88b3623e3b3725ebb42670cbe4c579")
set(SPECTRA_GLFW_ARCHIVE_SHA256 "699BF0B3D0BD422C0212263F30C4B6FC1AB4F67320824B27854F1E5C6949A2A0")
spectra_prepare_archive_dependency(
        SPECTRA_GLFW_SOURCE_DIR
        glfw
        glfw
        "${SPECTRA_GLFW_COMMIT}"
        "${SPECTRA_GLFW_ARCHIVE_SHA256}"
        glfw
)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
add_subdirectory("${SPECTRA_GLFW_SOURCE_DIR}" "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/glfw-build/${SPECTRA_GLFW_COMMIT}")

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
set_property(TARGET spectra_vulkan PROPERTY CXX_MODULE_STD ON)

set(SPECTRA_ZLIB_COMMIT "da607da739fa6047df13e66a2af6b8bec7c2a498")
set(SPECTRA_ZLIB_ARCHIVE_SHA256 "21DE001766ABB1F5BFDFB3C23AA42D30A6BBD7EDDD37131D5E7FE89C5EC068F8")
spectra_prepare_archive_dependency(
        SPECTRA_ZLIB_SOURCE_DIR
        madler
        zlib
        "${SPECTRA_ZLIB_COMMIT}"
        "${SPECTRA_ZLIB_ARCHIVE_SHA256}"
        zlib
)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(ZLIB_CONF_WRITTEN OFF CACHE BOOL "" FORCE)
set(SPECTRA_ZLIB_BINARY_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/zlib-build/${SPECTRA_ZLIB_COMMIT}")
add_subdirectory("${SPECTRA_ZLIB_SOURCE_DIR}" "${SPECTRA_ZLIB_BINARY_DIR}")
if (NOT TARGET zlibstatic)
    message(FATAL_ERROR "Downloaded zlib did not define the zlibstatic target.")
endif ()
if (NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif ()
set(ZLIB_LIBRARIES ZLIB::ZLIB CACHE STRING "zlib target" FORCE)
set(ZLIB_INCLUDE_DIRS "${SPECTRA_ZLIB_SOURCE_DIR}" "${SPECTRA_ZLIB_BINARY_DIR}" CACHE STRING "zlib include dirs" FORCE)
set(ZLIB_LIBRARY ZLIB::ZLIB CACHE STRING "zlib target" FORCE)
set(ZLIB_INCLUDE_DIR "${SPECTRA_ZLIB_SOURCE_DIR}" "${SPECTRA_ZLIB_BINARY_DIR}" CACHE STRING "zlib include dir" FORCE)
set(ZLIB_FOUND TRUE CACHE BOOL "zlib found" FORCE)

set(SPECTRA_RPLY_COMMIT "5f7a606806a4ac7b939131ded9d7a30ebd02416e")
set(SPECTRA_RPLY_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/rply/${SPECTRA_RPLY_COMMIT}")
foreach (rply_file IN ITEMS
        rply.cpp
        rply.h
)
    spectra_download_if_needed(
            "https://raw.githubusercontent.com/mmp/pbrt-v4/${SPECTRA_RPLY_COMMIT}/src/ext/rply/${rply_file}"
            "${SPECTRA_RPLY_DIR}/rply/${rply_file}"
    )
endforeach ()
add_library(spectra_rply STATIC "${SPECTRA_RPLY_DIR}/rply/rply.cpp")
add_library(spectra::rply ALIAS spectra_rply)
target_include_directories(spectra_rply PUBLIC "${SPECTRA_RPLY_DIR}")
target_link_libraries(spectra_rply PUBLIC ZLIB::ZLIB)

set(SPECTRA_LIBDEFLATE_COMMIT "c8c56a20f8f621e6a966b716b31f1dedab6a41e3")
set(SPECTRA_LIBDEFLATE_ARCHIVE_SHA256 "BC0DDBC4E26C8B1379A0FC639E9B8159744C13A435017BB8EF3A30BBB53BDF47")
spectra_prepare_archive_dependency(
        SPECTRA_LIBDEFLATE_SOURCE_DIR
        ebiggers
        libdeflate
        "${SPECTRA_LIBDEFLATE_COMMIT}"
        "${SPECTRA_LIBDEFLATE_ARCHIVE_SHA256}"
        libdeflate
)
set(LIBDEFLATE_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SPECTRA_LIBDEFLATE_SOURCE_DIR}" "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/libdeflate-build/${SPECTRA_LIBDEFLATE_COMMIT}")
if (NOT TARGET libdeflate::libdeflate_static)
    message(FATAL_ERROR "Downloaded libdeflate did not define libdeflate::libdeflate_static.")
endif ()

set(SPECTRA_IMATH_COMMIT "1e480d11cb98b032a2dece9b9a8730512effc7f6")
set(SPECTRA_IMATH_ARCHIVE_SHA256 "B707D359CF0ADC0E75DD4C68AB06601D58433E301295F04D44B8A60FF148C939")
spectra_prepare_archive_dependency(
        SPECTRA_IMATH_SOURCE_DIR
        AcademySoftwareFoundation
        Imath
        "${SPECTRA_IMATH_COMMIT}"
        "${SPECTRA_IMATH_ARCHIVE_SHA256}"
        imath
)
foreach (cache_var IN ITEMS
        IMATH_INTERNAL_NAMESPACE
        IMATH_LIB_SUFFIX
        IMATH_PACKAGE_NAME
        IMATH_STATIC_LIB_SUFFIX
        IMATH_VERSION_RELEASE_TYPE)
    unset(${cache_var} CACHE)
endforeach ()
set(IMATH_INSTALL OFF CACHE BOOL "" FORCE)
set(IMATH_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(IMATH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${SPECTRA_IMATH_SOURCE_DIR}" "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/imath-build/${SPECTRA_IMATH_COMMIT}")

set(SPECTRA_OPENEXR_COMMIT "d1dcb760459e6cf9985b98deb118abc6de12f9bb")
set(SPECTRA_OPENEXR_ARCHIVE_SHA256 "1EAC27323A849E936E85D1B6AB245D1547BCD267B1379F0045B08FF227B926F9")
spectra_prepare_archive_dependency(
        SPECTRA_OPENEXR_SOURCE_DIR
        AcademySoftwareFoundation
        openexr
        "${SPECTRA_OPENEXR_COMMIT}"
        "${SPECTRA_OPENEXR_ARCHIVE_SHA256}"
        openexr
)
foreach (cache_var IN ITEMS
        OPENEXR_IMATH_REPO
        OPENEXR_IMATH_TAG
        OPENEXR_INTERNAL_IMF_NAMESPACE
        OPENEXR_LIB_SUFFIX
        OPENEXR_PACKAGE_NAME
        OPENEXR_STATIC_LIB_SUFFIX
        OPENEXR_VERSION_RELEASE_TYPE)
    unset(${cache_var} CACHE)
endforeach ()
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_PKG_CONFIG OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_INSTALL_DEVELOPER_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_BUILD_OSS_FUZZ OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_LIBRARIES OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_TOOLS OFF CACHE BOOL "" FORCE)
set(OPENEXR_TEST_PYTHON OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_DEFLATE OFF CACHE BOOL "" FORCE)
set(OPENEXR_FORCE_INTERNAL_IMATH OFF CACHE BOOL "" FORCE)
set(EXR_DEFLATE_LIB libdeflate::libdeflate_static CACHE STRING "" FORCE)
set(EXR_DEFLATE_VERSION 1.25 CACHE STRING "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_libdeflate ON CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Imath ON CACHE BOOL "" FORCE)
add_subdirectory("${SPECTRA_OPENEXR_SOURCE_DIR}" "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/openexr-build/${SPECTRA_OPENEXR_COMMIT}")
set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_libdeflate OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Imath OFF CACHE BOOL "" FORCE)

set(SPECTRA_PTEX_COMMIT "a3729367596c827ace086f4baa6cab89b6a7f439")
set(SPECTRA_PTEX_ARCHIVE_SHA256 "5C079E439B4910826F8D4BA29698A325C886EC6C0D471AEF9BAC8FE63931BDBC")
spectra_prepare_archive_dependency(
        SPECTRA_PTEX_SOURCE_DIR
        wdas
        ptex
        "${SPECTRA_PTEX_COMMIT}"
        "${SPECTRA_PTEX_ARCHIVE_SHA256}"
        ptex
)
include(GNUInstallDirs)
set(PTEX_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(PTEX_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(PTEX_MAJOR_VERSION 2)
set(PTEX_MINOR_VERSION 5)
add_subdirectory("${SPECTRA_PTEX_SOURCE_DIR}/src/ptex" "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/ptex-build/${SPECTRA_PTEX_COMMIT}/src/ptex")
set(PTEX_INCLUDE "${SPECTRA_PTEX_SOURCE_DIR}/src/ptex")

set(SPECTRA_OPENVDB_COMMIT "7c03e1f084873cd1b3422c7ff7aec6ee681b3b38")
set(SPECTRA_OPENVDB_ARCHIVE_SHA256 "7492C5D80F56E961193FD6DD27F17642C7A5250960FFBDE72BC39806237BFDA4")
spectra_prepare_archive_dependency(
        SPECTRA_OPENVDB_SOURCE_DIR
        AcademySoftwareFoundation
        openvdb
        "${SPECTRA_OPENVDB_COMMIT}"
        "${SPECTRA_OPENVDB_ARCHIVE_SHA256}"
        openvdb
)
set(NANOVDB_INCLUDE "${SPECTRA_OPENVDB_SOURCE_DIR}/nanovdb")

set(SPECTRA_IMGUI_MODULE_COMMIT "8fff16535a92346f681bfb8a61ee1454142eef82")
set(SPECTRA_IMGUI_SOURCE_COMMIT "b61e56346a92cfcaf1f43a545ca37b0b32239654")
set(SPECTRA_IMGUI_MODULE_DIR "${SPECTRA_BINARY_ROOT_DIR}/vendor-cache/imgui-module/${SPECTRA_IMGUI_MODULE_COMMIT}")
set(SPECTRA_IMGUI_SOURCE_DIR "${SPECTRA_IMGUI_MODULE_DIR}/imgui/${SPECTRA_IMGUI_SOURCE_COMMIT}")

foreach (imgui_module_file IN ITEMS
        generated/imgui.cppm
        generated/imgui_internal.cppm
        generated/backends/imgui_impl_glfw.cppm
        generated/backends/imgui_impl_vulkan.cppm
)
    spectra_download_if_needed(
            "https://raw.githubusercontent.com/stripe2933/imgui-module/${SPECTRA_IMGUI_MODULE_COMMIT}/${imgui_module_file}"
            "${SPECTRA_IMGUI_MODULE_DIR}/${imgui_module_file}"
    )
endforeach ()

foreach (imgui_source_file IN ITEMS
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        imgui.h
        imgui_internal.h
        imconfig.h
        imstb_rectpack.h
        imstb_textedit.h
        imstb_truetype.h
        backends/imgui_impl_glfw.cpp
        backends/imgui_impl_glfw.h
        backends/imgui_impl_vulkan.cpp
        backends/imgui_impl_vulkan.h
)
    spectra_download_if_needed(
            "https://raw.githubusercontent.com/ocornut/imgui/${SPECTRA_IMGUI_SOURCE_COMMIT}/${imgui_source_file}"
            "${SPECTRA_IMGUI_SOURCE_DIR}/${imgui_source_file}"
    )
endforeach ()

add_library(spectra_imgui)
add_library(spectra::imgui ALIAS spectra_imgui)
target_sources(
        spectra_imgui
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS "${SPECTRA_IMGUI_MODULE_DIR}/generated"
        FILES
        "${SPECTRA_IMGUI_MODULE_DIR}/generated/imgui.cppm"
        "${SPECTRA_IMGUI_MODULE_DIR}/generated/imgui_internal.cppm"
        "${SPECTRA_IMGUI_MODULE_DIR}/generated/backends/imgui_impl_glfw.cppm"
        "${SPECTRA_IMGUI_MODULE_DIR}/generated/backends/imgui_impl_vulkan.cppm"
        PRIVATE
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${SPECTRA_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(spectra_imgui PUBLIC "${SPECTRA_IMGUI_SOURCE_DIR}" "${SPECTRA_IMGUI_SOURCE_DIR}/backends")
target_link_libraries(spectra_imgui PUBLIC spectra::vulkan PRIVATE glfw)
set_property(TARGET spectra_imgui PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Spectra Core
# ============================================================================
add_library(spectra_core)
add_library(spectra::core ALIAS spectra_core)
target_sources(
        spectra_core
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        spectra/spectra.ixx
        PRIVATE
        spectra/spectra.cpp
)
target_link_libraries(spectra_core PUBLIC spectra::vulkan spectra::imgui PRIVATE glfw)
target_include_directories(spectra_core PRIVATE "${SPECTRA_UI_ASSET_INCLUDE_DIR}")
set_property(TARGET spectra_core PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Scene
# ============================================================================
add_library(spectra_scene)
add_library(spectra::scene ALIAS spectra_scene)
target_sources(
        spectra_scene
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        scene/math.ixx
        scene/spatial.ixx
        scene/scene.ixx
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        scene/plugin_abi.ixx
        scene/plugin_host.ixx
        PRIVATE
        scene/spatial.cpp
        scene/scene.cpp
        scene/plugin_host.cpp
)
target_compile_definitions(spectra_scene PRIVATE "SPECTRA_SCENES_ROOT=\"${SPECTRA_SCENES_ROOT}\"")
target_link_libraries(spectra_scene PRIVATE ZLIB::ZLIB spectra::lodepng ${CMAKE_DL_LIBS})
set_property(TARGET spectra_scene PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# Pathtracer Configuration
# ============================================================================
option(SPECTRA_FLOAT_AS_DOUBLE "Use 64-bit floats for the Spectra pathtracer" OFF)
set(SPECTRA_OPTIX_PATH "" CACHE PATH "Path to OptiX SDK for the Spectra pathtracer")
set(SPECTRA_OPTIX_GPU_ARCH "" CACHE STRING "CUDA architecture used when compiling spectra OptiX PTX")
set(SPECTRA_OPTIX_CACHE_DIR "${SPECTRA_BINARY_ROOT_DIR}/optix-cache" CACHE PATH "OptiX disk cache directory for the Spectra pathtracer")
set(SPECTRA_OPTIX_CACHE_SIZE_MB 512 CACHE STRING "OptiX disk cache high-water size in MiB for the Spectra pathtracer")

find_package(CUDAToolkit REQUIRED)
if (CMAKE_CUDA_COMPILER_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA compiler 13.0 or newer; found ${CMAKE_CUDA_COMPILER_VERSION}.")
endif ()
if (CUDAToolkit_VERSION VERSION_LESS 13.0)
    message(FATAL_ERROR "spectra requires CUDA Toolkit 13.0 or newer; found ${CUDAToolkit_VERSION}.")
endif ()

if (SPECTRA_OPTIX_PATH STREQUAL "")
    file(GLOB SPECTRA_OPTIX_DEFAULT_PATHS LIST_DIRECTORIES true
            "C:/ProgramData/NVIDIA Corporation/OptiX SDK *"
            "/mnt/c/ProgramData/NVIDIA Corporation/OptiX SDK *"
            "/opt/NVIDIA-OptiX-SDK-*"
            "/opt/OptiX SDK *"
            "/opt/optix*"
            "/usr/local/NVIDIA-OptiX-SDK-*"
            "/usr/local/OptiX SDK *"
            "/usr/local/optix*"
    )
    foreach (SPECTRA_OPTIX_DEFAULT_PATH IN LISTS SPECTRA_OPTIX_DEFAULT_PATHS)
        if (EXISTS "${SPECTRA_OPTIX_DEFAULT_PATH}/include/optix.h")
            list(APPEND SPECTRA_OPTIX_CANDIDATE_PATHS "${SPECTRA_OPTIX_DEFAULT_PATH}")
        endif ()
    endforeach ()
    if (SPECTRA_OPTIX_CANDIDATE_PATHS)
        list(SORT SPECTRA_OPTIX_CANDIDATE_PATHS COMPARE NATURAL ORDER DESCENDING)
        list(GET SPECTRA_OPTIX_CANDIDATE_PATHS 0 SPECTRA_OPTIX_PATH)
        set(SPECTRA_OPTIX_PATH "${SPECTRA_OPTIX_PATH}" CACHE PATH "Path to OptiX SDK for the Spectra pathtracer" FORCE)
    endif ()
endif ()
if (SPECTRA_OPTIX_PATH STREQUAL "")
    message(FATAL_ERROR "SPECTRA_OPTIX_PATH must point to an OptiX SDK root; no default installation was found.")
endif ()
if (SPECTRA_OPTIX_GPU_ARCH STREQUAL "")
    list(GET CMAKE_CUDA_ARCHITECTURES 0 SPECTRA_OPTIX_DERIVED_GPU_ARCH)
    if (SPECTRA_OPTIX_DERIVED_GPU_ARCH MATCHES "^([0-9]+)")
        set(SPECTRA_OPTIX_GPU_ARCH "sm_${CMAKE_MATCH_1}")
        if (CMAKE_MATCH_1 GREATER_EQUAL 100)
            set(SPECTRA_OPTIX_GPU_ARCH "sm_80")
        endif ()
    else ()
        message(FATAL_ERROR "Unable to derive SPECTRA_OPTIX_GPU_ARCH from CMAKE_CUDA_ARCHITECTURES='${CMAKE_CUDA_ARCHITECTURES}'.")
    endif ()
endif ()
message(STATUS "Spectra OptiX PTX CUDA Architecture: ${SPECTRA_OPTIX_GPU_ARCH}")
math(EXPR SPECTRA_OPTIX_CACHE_SIZE_BYTES "${SPECTRA_OPTIX_CACHE_SIZE_MB} * 1024 * 1024")

set(SPECTRA_PATHTRACER_DEFINITIONS
        PTEX_STATIC
)
if (SPECTRA_FLOAT_AS_DOUBLE)
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS SPECTRA_FLOAT_AS_DOUBLE)
endif ()

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC.")
    endif ()
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS
            SPECTRA_IS_WINDOWS
            SPECTRA_IS_MSVC
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _ENABLE_EXTENDED_ALIGNED_STORAGE
            SPECTRA_HAS_INTRIN_H
            "SPECTRA_NOINLINE=__declspec(noinline)"
            "SPECTRA_RESTRICT=__restrict"
            SPECTRA_HAVE__ALIGNED_MALLOC
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPECTRA_PATHTRACER_DEFINITIONS
            SPECTRA_IS_LINUX
            "SPECTRA_NOINLINE=__attribute__((noinline))"
            "SPECTRA_RESTRICT=__restrict__"
            SPECTRA_HAVE_POSIX_MEMALIGN
    )
else ()
    message(FATAL_ERROR "Only Linux/WSL and Windows/MSVC are supported by the Spectra pathtracer.")
endif ()

set(SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/Zc:preprocessor>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4244>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4267>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4305>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4552>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4838>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4843>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd26451>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd26495>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4334>"
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcompiler >/wd4146>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=partial_override>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=virtual_function_decl_hidden>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=integer_sign_change>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=declared_but_not_referenced>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=implicit_return_from_non_void_function>"
)
set(SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS
        "$<$<CXX_COMPILER_ID:MSVC>:$<$<COMPILE_LANGUAGE:CUDA>:--forward-slash-prefix-opts>>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>"
        "$<$<COMPILE_LANGUAGE:CUDA>:--forward-unknown-to-host-compiler>"
)


# ============================================================================
# Pathtracer Generated Sources
# ============================================================================
get_filename_component(SPECTRA_PATHTRACER_CUDA_COMPILER_BIN "${CMAKE_CUDA_COMPILER}" DIRECTORY)
find_program(
        SPECTRA_PATHTRACER_BIN2C
        NAMES bin2c
        PATHS ${SPECTRA_PATHTRACER_CUDA_COMPILER_BIN} ${CUDAToolkit_BIN_DIR}
        NO_DEFAULT_PATH
)
if (NOT SPECTRA_PATHTRACER_BIN2C)
    message(FATAL_ERROR "CUDA bin2c was not found next to '${CMAKE_CUDA_COMPILER}'.")
endif ()

set(SPECTRA_PATHTRACER_SOURCE_DIR ${SPECTRA_ROOT_DIR}/pathtracer)
set(SPECTRA_PATHTRACER_GENERATED_DIR ${SPECTRA_BINARY_ROOT_DIR}/pathtracer/generated/spectra_pathtracer)
file(MAKE_DIRECTORY "${SPECTRA_PATHTRACER_GENERATED_DIR}")
file(MAKE_DIRECTORY "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\\" "/" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR}")
string(REPLACE "\"" "\\\"" SPECTRA_OPTIX_CACHE_DIR_CONFIG "${SPECTRA_OPTIX_CACHE_DIR_CONFIG}")
file(WRITE "${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_optix_config.h"
        "#pragma once\n"
        "\n"
        "#include <cstddef>\n"
        "\n"
        "namespace spectra::optix\n"
        "{\n"
        "    inline constexpr char SpectraOptiXCacheDir[] = \"${SPECTRA_OPTIX_CACHE_DIR_CONFIG}\";\n"
        "    inline constexpr std::size_t SpectraOptiXCacheSizeBytes = ${SPECTRA_OPTIX_CACHE_SIZE_BYTES}ull;\n"
        "} // namespace spectra::optix\n")

set(SPECTRA_PATHTRACER_INCLUDE_DIRS
        ${SPECTRA_ROOT_DIR}
        ${SPECTRA_UI_ASSET_INCLUDE_DIR}
        ${SPECTRA_STB_DIR}
        ${SPECTRA_QOI_DIR}
        ${ZLIB_INCLUDE_DIRS}
        ${PTEX_INCLUDE}
        ${NANOVDB_INCLUDE}
        ${SPECTRA_BINARY_ROOT_DIR}
        ${SPECTRA_PATHTRACER_GENERATED_DIR}
        ${SPECTRA_OPTIX_PATH}/include
        ${CUDAToolkit_INCLUDE_DIRS}
)

set(SPECTRA_PATHTRACER_BIN2C_SCRIPT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_pathtracer_bin2c_embed.cmake)
file(WRITE "${SPECTRA_PATHTRACER_BIN2C_SCRIPT}" [=[
set(file_contents)
foreach (obj IN LISTS OBJECTS)
    get_filename_component(obj_ext "${obj}" EXT)
    get_filename_component(obj_dir "${obj}" DIRECTORY)
    if (obj_ext MATCHES "\\.(optixir|ptx)$")
        execute_process(
                COMMAND "${BIN_TO_C_COMMAND}" --name "${VAR_NAME}" "${obj}"
                WORKING_DIRECTORY "${obj_dir}"
                RESULT_VARIABLE result
                OUTPUT_VARIABLE output
                ERROR_VARIABLE error_var
        )
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "bin2c failed for ${obj}: ${error_var}")
        endif ()
        file(SIZE "${obj}" obj_size)
        string(APPEND file_contents " \n${output}\nunsigned long long ${VAR_NAME}_SIZE = ${obj_size}ull;\n")
    endif ()
endforeach ()
if (file_contents STREQUAL "")
    message(FATAL_ERROR "No OptiX input was passed to bin2c: ${OBJECTS}")
endif ()
file(WRITE "${OUTPUT}" "${file_contents}")
]=])

add_executable(spectra_pathtracer_soac pathtracer/tools/soac.cpp)
target_compile_definitions(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer_soac PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})

add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/core/spectra.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        DEPENDS spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/core/spectra.soa
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/wavefront/workitems.soa > ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
        DEPENDS spectra_pathtracer_soac ${SPECTRA_PATHTRACER_SOURCE_DIR}/wavefront/workitems.soa
        VERBATIM
)
set(SPECTRA_PATHTRACER_SOA_GENERATED
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_soa.cuh
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/spectra_wavefront_workitems_soa.cuh
)
add_custom_target(spectra_pathtracer_soa_generated DEPENDS ${SPECTRA_PATHTRACER_SOA_GENERATED})

add_executable(spectra_pathtracer_rgb2spec_opt pathtracer/tools/rgb2spec_opt.cpp)
target_compile_definitions(spectra_pathtracer_rgb2spec_opt PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer_rgb2spec_opt PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_link_libraries(spectra_pathtracer_rgb2spec_opt PRIVATE Threads::Threads)

set(SPECTRA_PATHTRACER_RGB_TO_SPECTRUM_SOURCES
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_aces.cpp ACES2065_1
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_dci_p3.cpp DCI_P3
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_rec2020.cpp REC2020
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND spectra_pathtracer_rgb2spec_opt 64 ${SPECTRA_PATHTRACER_GENERATED_DIR}/rgbspectrum_srgb.cpp sRGB
        DEPENDS spectra_pathtracer_rgb2spec_opt
        VERBATIM
)


# ============================================================================
# Pathtracer Source Lists
# ============================================================================
set(SPECTRA_PATHTRACER_CORE_SOURCE
        pathtracer/core/bssrdf.cu
        pathtracer/core/bxdfs.cu
        pathtracer/core/cameras.cu
        pathtracer/core/film.cu
        pathtracer/core/filters.cu
        pathtracer/core/interaction.cu
        pathtracer/core/kernel_config.cu
        pathtracer/core/lights.cu
        pathtracer/core/lightsamplers.cu
        pathtracer/core/materials.cu
        pathtracer/core/media_host.cu
        pathtracer/core/media.cu
        pathtracer/core/paramdict.cu
        pathtracer/core/samplers.cu
        pathtracer/core/shapes.cu
        pathtracer/core/textures.cu
)
set(SPECTRA_PATHTRACER_UTIL_SOURCE
        pathtracer/util/bluenoise.cu
        pathtracer/util/color.cu
        pathtracer/util/colorspace.cu
        pathtracer/util/file.cpp
        pathtracer/util/image.cpp
        pathtracer/util/loopsubdiv.cu
        pathtracer/util/lowdiscrepancy.cu
        pathtracer/util/math.cu
        pathtracer/util/memory.cu
        pathtracer/util/mesh.cu
        pathtracer/util/mipmap.cu
        pathtracer/util/noise.cu
        pathtracer/util/parallel.cu
        pathtracer/util/pmj02tables.cu
        pathtracer/util/primes.cu
        pathtracer/util/pstd.cu
        pathtracer/util/sampling.cu
        pathtracer/util/scattering.cu
        pathtracer/util/sobolmatrices.cu
        pathtracer/util/spectrum.cu
        pathtracer/util/stbimage.cu
        pathtracer/util/transform.cu
        pathtracer/util/vecmath.cu
)
set(SPECTRA_PATHTRACER_DEVICE_SOURCE
        pathtracer/gpu/util.cu
        pathtracer/memory/memory.cu
)

add_library(spectra_pathtracer_optix_input OBJECT pathtracer/optix/optix.cu)
set_property(TARGET spectra_pathtracer_optix_input PROPERTY CUDA_PTX_COMPILATION ON)
set_property(TARGET spectra_pathtracer_optix_input PROPERTY CUDA_ARCHITECTURES OFF)
target_compile_definitions(spectra_pathtracer_optix_input PRIVATE ${SPECTRA_PATHTRACER_DEFINITIONS} CCCL_DISABLE_INT128_SUPPORT)
target_include_directories(spectra_pathtracer_optix_input PRIVATE ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_compile_options(
        spectra_pathtracer_optix_input
        PRIVATE
        ${SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS}
        "$<$<COMPILE_LANGUAGE:CUDA>:--gpu-architecture=${SPECTRA_OPTIX_GPU_ARCH}>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --display_error_number>"
        "$<$<COMPILE_LANGUAGE:CUDA>:SHELL:-Xcudafe --diag_suppress=20044>"
)
add_dependencies(spectra_pathtracer_optix_input spectra_pathtracer_soa_generated)


# ============================================================================
# Pathtracer Targets
# ============================================================================
set(SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT ${SPECTRA_PATHTRACER_GENERATED_DIR}/optix.cu.ptx_embedded.c)
add_custom_command(
        OUTPUT ${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPECTRA_PATHTRACER_GENERATED_DIR}
        COMMAND ${CMAKE_COMMAND}
        "-DBIN_TO_C_COMMAND=${SPECTRA_PATHTRACER_BIN2C}"
        "-DOBJECTS=$<TARGET_OBJECTS:spectra_pathtracer_optix_input>"
        "-DVAR_NAME=SPECTRA_EMBEDDED_OPTIX_INPUT"
        "-DOUTPUT=${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT}"
        -P ${SPECTRA_PATHTRACER_BIN2C_SCRIPT}
        DEPENDS spectra_pathtracer_optix_input $<TARGET_OBJECTS:spectra_pathtracer_optix_input>
        VERBATIM
)
add_library(spectra_pathtracer_embedded_optix_input STATIC ${SPECTRA_PATHTRACER_EMBEDDED_OPTIX_INPUT})

add_library(spectra_pathtracer)
add_library(spectra::pathtracer ALIAS spectra_pathtracer)
target_sources(
        spectra_pathtracer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        pathtracer/host.ixx
        pathtracer/renderer.ixx
        PRIVATE
        ${SPECTRA_PATHTRACER_RGB_TO_SPECTRUM_SOURCES}
        ${SPECTRA_PATHTRACER_SOA_GENERATED}
        ${SPECTRA_PATHTRACER_CORE_SOURCE}
        ${SPECTRA_PATHTRACER_UTIL_SOURCE}
        ${SPECTRA_PATHTRACER_DEVICE_SOURCE}
        pathtracer/renderer.cpp
        pathtracer/compiled_scene.cpp
        pathtracer/optix/aggregate.cu
        pathtracer/integrator.cu
)
add_dependencies(spectra_pathtracer spectra_pathtracer_soa_generated)
target_compile_definitions(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_DEFINITIONS})
target_include_directories(spectra_pathtracer PUBLIC ${SPECTRA_PATHTRACER_INCLUDE_DIRS})
target_compile_options(
        spectra_pathtracer
        PUBLIC
        "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-ffp-contract=off>"
        ${SPECTRA_PATHTRACER_CUDA_COMPILE_OPTIONS}
        ${SPECTRA_PATHTRACER_CUDA_WARNING_OPTIONS}
)
target_link_libraries(
        spectra_pathtracer
        PUBLIC
        spectra::vulkan
        spectra::imgui
        spectra::scene
        PRIVATE
        spectra::lodepng
        spectra::rply
        OpenEXR::OpenEXR
        Ptex_static
        ZLIB::ZLIB
        CUDA::cudart
        CUDA::cuda_driver
        Threads::Threads
        spectra_pathtracer_embedded_optix_input
)
set_property(TARGET spectra_pathtracer PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(spectra_pathtracer PRIVATE dbghelp shell32 wsock32 ws2_32)
endif ()
set_property(TARGET spectra_pathtracer PROPERTY CUDA_SEPARABLE_COMPILATION ON)
set_property(TARGET spectra_pathtracer PROPERTY CUDA_RESOLVE_DEVICE_SYMBOLS ON)

add_executable(spectra_pathtracer_cli pathtracer/pathtracer_cli.cpp)
add_executable(spectra::pathtracer_cli ALIAS spectra_pathtracer_cli)
target_link_libraries(spectra_pathtracer_cli PRIVATE spectra::pathtracer xayah-util-xcli Threads::Threads)
set_property(TARGET spectra_pathtracer_cli PROPERTY CXX_MODULE_STD ON)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spectra_pathtracer_cli PRIVATE -rdynamic "LINKER:--no-as-needed")
endif ()


# ============================================================================
# Rasterizer
# ============================================================================
add_library(spectra_rasterizer)
add_library(spectra::rasterizer ALIAS spectra_rasterizer)

find_program(SLANGC_EXECUTABLE NAMES slangc REQUIRED)
set(SPECTRA_RASTERIZER_SOURCE_DIR ${SPECTRA_ROOT_DIR}/rasterizer)
set(SPECTRA_RASTERIZER_GENERATED_DIR ${SPECTRA_BINARY_ROOT_DIR}/rasterizer/generated)

file(GLOB SPECTRA_RASTERIZER_SHADER_FILES CONFIGURE_DEPENDS "${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders/*.slang")
file(GLOB SPECTRA_RASTERIZER_SHADER_INCLUDE_FILES CONFIGURE_DEPENDS "${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders/*.slangh")
if (NOT SPECTRA_RASTERIZER_SHADER_FILES)
    message(FATAL_ERROR "Spectra rasterizer requires at least one Slang shader in ${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders")
endif ()

function(spectra_rasterizer_shader_stage output_variable shader_file shader_name stage)
    set(entry "${stage}_main")
    set(embed_name "spectra_rasterizer_${shader_name}_${stage}_spv")
    set(output_file "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_${shader_name}_${stage}_spv.h")
    add_custom_command(
            OUTPUT "${output_file}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${SPECTRA_RASTERIZER_GENERATED_DIR}"
            COMMAND "${SLANGC_EXECUTABLE}" "${shader_file}" -I "${SPECTRA_RASTERIZER_SOURCE_DIR}/shaders" -target spirv -profile glsl_460 -entry "${entry}" -stage "${stage}" -matrix-layout-row-major -source-embed-style u32 -source-embed-name "${embed_name}" -o "${output_file}"
            DEPENDS "${shader_file}" ${SPECTRA_RASTERIZER_SHADER_INCLUDE_FILES}
            VERBATIM
    )
    set("${output_variable}" "${output_file}" PARENT_SCOPE)
endfunction()

set(SPECTRA_RASTERIZER_SHADER_HEADERS)
set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT)
foreach (shader_file IN LISTS SPECTRA_RASTERIZER_SHADER_FILES)
    get_filename_component(shader_name "${shader_file}" NAME_WE)
    if (shader_name STREQUAL "viewport_voxel_grid_compact" OR shader_name STREQUAL "volume_upload" OR shader_name STREQUAL "volume_color_upload")
        set(shader_stages compute)
    else ()
        set(shader_stages vertex fragment)
    endif ()
    foreach (stage IN LISTS shader_stages)
        spectra_rasterizer_shader_stage(shader_header "${shader_file}" "${shader_name}" "${stage}")
        list(APPEND SPECTRA_RASTERIZER_SHADER_HEADERS "${shader_header}")
        get_filename_component(shader_header_name "${shader_header}" NAME)
        string(APPEND SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT "#include <${shader_header_name}>\n")
    endforeach ()
endforeach ()

set(SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER "${SPECTRA_RASTERIZER_GENERATED_DIR}/spectra_rasterizer_shaders_spv.h")
file(MAKE_DIRECTORY "${SPECTRA_RASTERIZER_GENERATED_DIR}")
file(GENERATE OUTPUT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" CONTENT "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_CONTENT}")
set_source_files_properties("${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}" PROPERTIES GENERATED TRUE)

target_sources(
        spectra_rasterizer
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        rasterizer/host.ixx
        rasterizer/renderer.ixx
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        rasterizer/math.ixx
        PRIVATE
        rasterizer/renderer.cpp
        "${SPECTRA_RASTERIZER_SHADER_AGGREGATE_HEADER}"
        ${SPECTRA_RASTERIZER_SHADER_HEADERS}
)
target_link_libraries(spectra_rasterizer PUBLIC spectra::vulkan spectra::imgui spectra::scene)
target_include_directories(spectra_rasterizer PRIVATE "${SPECTRA_UI_ASSET_INCLUDE_DIR}" "${SPECTRA_RASTERIZER_GENERATED_DIR}")
set_property(TARGET spectra_rasterizer PROPERTY CXX_MODULE_STD ON)


# ============================================================================
# GUI
# ============================================================================
add_executable(spectra main.cpp)
add_executable(spectra::gui ALIAS spectra)
if (WIN32)
    target_sources(spectra PRIVATE spectra.rc)
endif ()
target_sources(
        spectra
        PRIVATE
        FILE_SET private_cxx_modules TYPE CXX_MODULES FILES
        scene/ui.ixx
        PRIVATE
        scene/ui.cpp
)
target_link_libraries(
        spectra
        PRIVATE
        spectra::core
        spectra::scene
        spectra::pathtracer
        spectra::rasterizer
        xayah-util-xcli
)
target_include_directories(spectra PRIVATE "${SPECTRA_UI_ASSET_INCLUDE_DIR}")
set_property(TARGET spectra PROPERTY CXX_MODULE_STD ON)
