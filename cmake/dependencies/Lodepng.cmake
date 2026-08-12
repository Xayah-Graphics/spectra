spectra_prepare_commit_dependency(
        SPECTRA_LODEPNG_SOURCE_DIR
        lvandeve
        lodepng
        "ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a"
        "E3E5A62322B7AFD5037D0AAA27188457226CCDAACC1353D0A1EFCCCD8F87F504"
)
add_library(spectra_lodepng STATIC "${SPECTRA_LODEPNG_SOURCE_DIR}/lodepng.cpp")
target_include_directories(spectra_lodepng SYSTEM PUBLIC "${SPECTRA_LODEPNG_SOURCE_DIR}")
target_compile_definitions(spectra_lodepng PRIVATE _CRT_SECURE_NO_WARNINGS)
