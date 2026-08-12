function(spectra_prepare_nanovdb_dependency out_include_dir tag archive_sha256)
    set(archive_dir "${CMAKE_CURRENT_BINARY_DIR}/vendor/nanovdb/${tag}")
    set(archive_path "${archive_dir}/nanovdb.zip")
    set(include_dir "${archive_dir}/include")
    string(REGEX REPLACE "^v" "" source_tag "${tag}")
    set(source_prefix "openvdb-${source_tag}/nanovdb/nanovdb")
    spectra_download_if_needed(
            "https://codeload.github.com/AcademySoftwareFoundation/openvdb/zip/refs/tags/${tag}"
            "${archive_path}"
            "${archive_sha256}"
    )
    if (NOT EXISTS "${include_dir}/nanovdb/io/IO.h" OR NOT EXISTS "${include_dir}/nanovdb/tools/GridChecksum.h" OR EXISTS "${include_dir}/nanovdb/tools/GridBuilder.h" OR EXISTS "${include_dir}/nanovdb/PNanoVDB.h")
        set(extract_dir "${archive_dir}/source")
        file(REMOVE_RECURSE "${extract_dir}" "${include_dir}")
        file(MAKE_DIRECTORY "${extract_dir}" "${include_dir}")
        file(ARCHIVE_EXTRACT
                INPUT "${archive_path}"
                DESTINATION "${extract_dir}"
                PATTERNS "${source_prefix}/*"
        )
        file(RENAME "${extract_dir}/${source_prefix}" "${include_dir}/nanovdb")
        file(REMOVE_RECURSE "${extract_dir}")
        file(REMOVE_RECURSE
                "${include_dir}/nanovdb/cmd"
                "${include_dir}/nanovdb/cuda"
                "${include_dir}/nanovdb/docs"
                "${include_dir}/nanovdb/examples"
                "${include_dir}/nanovdb/putil"
                "${include_dir}/nanovdb/python"
                "${include_dir}/nanovdb/tools/cuda"
                "${include_dir}/nanovdb/unittest"
        )
        file(REMOVE
                "${include_dir}/nanovdb/CMakeLists.txt"
                "${include_dir}/nanovdb/CNanoVDB.h"
                "${include_dir}/nanovdb/PNanoVDB.h"
                "${include_dir}/nanovdb/Readme.md"
                "${include_dir}/nanovdb/tools/CreateNanoGrid.h"
                "${include_dir}/nanovdb/tools/CreatePrimitives.h"
                "${include_dir}/nanovdb/tools/GridBuilder.h"
                "${include_dir}/nanovdb/tools/GridStats.h"
                "${include_dir}/nanovdb/tools/GridValidator.h"
                "${include_dir}/nanovdb/tools/NanoToOpenVDB.h"
                "${include_dir}/nanovdb/tools/VoxelBlockManager.h"
        )
    endif ()
    set(${out_include_dir} "${include_dir}" PARENT_SCOPE)
endfunction()

spectra_prepare_nanovdb_dependency(
        SPECTRA_NANOVDB_INCLUDE_DIR
        "v13.0.0"
        "37B31E0E67CD071B4AAFBF924B4AC7DCE9B15C439C0E25B4B35AA47F3939A5FC"
)
add_library(spectra_nanovdb INTERFACE)
add_library(spectra::nanovdb ALIAS spectra_nanovdb)
target_include_directories(spectra_nanovdb SYSTEM INTERFACE "${SPECTRA_NANOVDB_INCLUDE_DIR}")
target_compile_options(spectra_nanovdb INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")
