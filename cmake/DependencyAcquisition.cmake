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
