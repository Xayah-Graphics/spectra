spectra_prepare_commit_dependency(
        SPECTRA_XCLI_SOURCE_DIR
        Xayah-Graphics
        util
        "3b4627ae0ae9d2744de8e9d0d5a9d4161cfd4764"
        "0038A581367976D61D435FAD8C30F52889A9407EBB5A5BAA6ED006401E06C114"
)
add_subdirectory("${SPECTRA_XCLI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xcli" EXCLUDE_FROM_ALL)
