if (WIN32)
    find_path(
            SPECTRA_SLANG_INCLUDE_DIR
            slang.h
            HINTS
            "${SPECTRA_VULKAN_SDK_DIR}/Include/slang"
            "${SPECTRA_VULKAN_SDK_DIR}/include/slang"
            REQUIRED
            NO_DEFAULT_PATH
    )
    find_library(
            SPECTRA_SLANG_LIBRARY
            NAMES slang
            HINTS
            "${SPECTRA_VULKAN_SDK_DIR}/Lib"
            "${SPECTRA_VULKAN_SDK_DIR}/lib"
            REQUIRED
            NO_DEFAULT_PATH
    )
    find_file(
            SPECTRA_SLANG_RUNTIME
            NAMES slang.dll
            HINTS "${SPECTRA_VULKAN_SDK_DIR}/Bin"
            REQUIRED
            NO_DEFAULT_PATH
    )
    add_library(spectra::slang SHARED IMPORTED)
    set_target_properties(
            spectra::slang
            PROPERTIES
            IMPORTED_IMPLIB "${SPECTRA_SLANG_LIBRARY}"
            IMPORTED_LOCATION "${SPECTRA_SLANG_RUNTIME}"
            INTERFACE_INCLUDE_DIRECTORIES "${SPECTRA_SLANG_INCLUDE_DIR}"
    )
else ()
    find_program(SPECTRA_SLANG_COMPILER NAMES slangc REQUIRED)
    file(REAL_PATH "${SPECTRA_SLANG_COMPILER}" SPECTRA_SLANG_COMPILER_REAL)
    cmake_path(GET SPECTRA_SLANG_COMPILER_REAL PARENT_PATH SPECTRA_SLANG_BINARY_DIR)
    cmake_path(GET SPECTRA_SLANG_BINARY_DIR PARENT_PATH SPECTRA_SLANG_INSTALL_DIR)
    find_package(
            slang
            CONFIG
            REQUIRED
            PATHS "${SPECTRA_SLANG_INSTALL_DIR}/lib/cmake/slang"
            NO_DEFAULT_PATH
    )
    add_library(spectra::slang ALIAS slang::slang)
endif ()
