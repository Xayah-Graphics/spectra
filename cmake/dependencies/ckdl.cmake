include_guard(GLOBAL)

FetchContent_MakeAvailable(ckdl)

add_library(spectra_ckdl STATIC
        "${ckdl_SOURCE_DIR}/src/bigint.c"
        "${ckdl_SOURCE_DIR}/src/compat.c"
        "${ckdl_SOURCE_DIR}/src/emitter.c"
        "${ckdl_SOURCE_DIR}/src/parser.c"
        "${ckdl_SOURCE_DIR}/src/str.c"
        "${ckdl_SOURCE_DIR}/src/tokenizer.c"
        "${ckdl_SOURCE_DIR}/src/utf8.c"
)
add_library(spectra::ckdl ALIAS spectra_ckdl)
set_target_properties(
        spectra_ckdl
        PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
target_include_directories(
        spectra_ckdl
        PUBLIC "${ckdl_SOURCE_DIR}/include"
        PRIVATE "${ckdl_SOURCE_DIR}/src"
)
target_compile_definitions(
        spectra_ckdl
        PUBLIC KDL_STATIC_LIB
        PRIVATE BUILDING_KDL _CRT_SECURE_NO_WARNINGS
)
if (UNIX)
    target_link_libraries(spectra_ckdl PRIVATE m)
endif ()

add_library(spectra_ckdlpp STATIC "${ckdl_SOURCE_DIR}/bindings/cpp/src/kdlpp.cpp")
add_library(spectra::ckdlpp ALIAS spectra_ckdlpp)
target_include_directories(spectra_ckdlpp PUBLIC "${ckdl_SOURCE_DIR}/bindings/cpp/include")
target_compile_definitions(spectra_ckdlpp PUBLIC KDLPP_STATIC_LIB)
target_link_libraries(spectra_ckdlpp PUBLIC spectra::ckdl)
