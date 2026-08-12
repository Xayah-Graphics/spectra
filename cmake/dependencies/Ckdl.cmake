spectra_prepare_commit_dependency(
        SPECTRA_CKDL_SOURCE_DIR
        tjol
        ckdl
        "c9c33fe64446287215e80705545139d92a48f829"
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
set_target_properties(
        spectra_ckdl
        PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
target_include_directories(
        spectra_ckdl
        PUBLIC "${SPECTRA_CKDL_SOURCE_DIR}/include"
        PRIVATE "${SPECTRA_CKDL_SOURCE_DIR}/src"
)
target_compile_definitions(
        spectra_ckdl
        PUBLIC KDL_STATIC_LIB
        PRIVATE BUILDING_KDL _CRT_SECURE_NO_WARNINGS
)
if (UNIX)
    target_link_libraries(spectra_ckdl PRIVATE m)
endif ()

add_library(spectra_ckdlpp STATIC "${SPECTRA_CKDL_SOURCE_DIR}/bindings/cpp/src/kdlpp.cpp")
target_include_directories(spectra_ckdlpp PUBLIC "${SPECTRA_CKDL_SOURCE_DIR}/bindings/cpp/include")
target_compile_definitions(spectra_ckdlpp PUBLIC KDLPP_STATIC_LIB)
target_link_libraries(spectra_ckdlpp PUBLIC spectra_ckdl)
