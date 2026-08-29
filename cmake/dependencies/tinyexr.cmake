include_guard(GLOBAL)

FetchContent_MakeAvailable(tinyexr)

add_library(spectra_tinyexr STATIC
        "${tinyexr_SOURCE_DIR}/src/exr_attr.c"
        "${tinyexr_SOURCE_DIR}/src/exr_b44.c"
        "${tinyexr_SOURCE_DIR}/src/exr_codec.c"
        "${tinyexr_SOURCE_DIR}/src/exr_color.c"
        "${tinyexr_SOURCE_DIR}/src/exr_convert.c"
        "${tinyexr_SOURCE_DIR}/src/exr_core.c"
        "${tinyexr_SOURCE_DIR}/src/exr_cpu.c"
        "${tinyexr_SOURCE_DIR}/src/exr_deep.c"
        "${tinyexr_SOURCE_DIR}/src/exr_deflate.c"
        "${tinyexr_SOURCE_DIR}/src/exr_fpnge.c"
        "${tinyexr_SOURCE_DIR}/src/exr_half.c"
        "${tinyexr_SOURCE_DIR}/src/exr_jph.c"
        "${tinyexr_SOURCE_DIR}/src/exr_jph_simd.c"
        "${tinyexr_SOURCE_DIR}/src/exr_jph_simd_neon.c"
        "${tinyexr_SOURCE_DIR}/src/exr_mip.c"
        "${tinyexr_SOURCE_DIR}/src/exr_piz.c"
        "${tinyexr_SOURCE_DIR}/src/exr_pxr24.c"
        "${tinyexr_SOURCE_DIR}/src/exr_reader.c"
        "${tinyexr_SOURCE_DIR}/src/exr_resize.c"
        "${tinyexr_SOURCE_DIR}/src/exr_rle.c"
        "${tinyexr_SOURCE_DIR}/src/exr_simd_neon.c"
        "${tinyexr_SOURCE_DIR}/src/exr_simd_x86.c"
        "${tinyexr_SOURCE_DIR}/src/exr_spectral.c"
        "${tinyexr_SOURCE_DIR}/src/exr_stdio.c"
        "${tinyexr_SOURCE_DIR}/src/exr_thread.c"
        "${tinyexr_SOURCE_DIR}/src/exr_tonemap.c"
        "${tinyexr_SOURCE_DIR}/src/exr_util_simd_neon.c"
        "${tinyexr_SOURCE_DIR}/src/exr_util_simd_x86.c"
        "${tinyexr_SOURCE_DIR}/src/exr_writer.c"
        "${tinyexr_SOURCE_DIR}/src/exr_zip.c"
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
        SYSTEM PUBLIC "${tinyexr_SOURCE_DIR}/include"
        PRIVATE "${tinyexr_SOURCE_DIR}/src"
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
