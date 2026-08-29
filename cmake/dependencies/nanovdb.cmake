include_guard(GLOBAL)

FetchContent_MakeAvailable(openvdb)

add_library(spectra_nanovdb INTERFACE)
add_library(spectra::nanovdb ALIAS spectra_nanovdb)
target_include_directories(spectra_nanovdb SYSTEM INTERFACE "${openvdb_SOURCE_DIR}/nanovdb")
target_compile_options(spectra_nanovdb INTERFACE "$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>")
