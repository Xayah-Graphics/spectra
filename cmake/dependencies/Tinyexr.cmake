spectra_prepare_archive_dependency(
        SPECTRA_TINYEXR_SOURCE_DIR
        syoyo
        tinyexr
        "v3.2.0"
        "12A458E2E92A7072E26927BA4E53276CE35605EFB2E0418E7C5210CBC81DE2DF"
)
add_library(spectra_tinyexr STATIC
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
add_library(spectra::tinyexr ALIAS spectra_tinyexr)
set_target_properties(
        spectra_tinyexr
        PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
target_include_directories(
        spectra_tinyexr
        SYSTEM PUBLIC "${SPECTRA_TINYEXR_SOURCE_DIR}/include"
        PRIVATE "${SPECTRA_TINYEXR_SOURCE_DIR}/src"
)
target_compile_definitions(
        spectra_tinyexr
        PRIVATE
        EXR_NO_ZSTD
        _CRT_SECURE_NO_WARNINGS
)
if (MSVC)
    target_compile_definitions(spectra_tinyexr PRIVATE __builtin_clz=_lzcnt_u32)
    target_compile_options(spectra_tinyexr PRIVATE /experimental:c11atomics /wd4556)
endif ()
