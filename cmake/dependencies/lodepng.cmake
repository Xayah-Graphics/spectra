include_guard(GLOBAL)

FetchContent_MakeAvailable(lodepng)

add_library(spectra_lodepng STATIC "${lodepng_SOURCE_DIR}/lodepng.cpp")
add_library(spectra::lodepng ALIAS spectra_lodepng)
target_include_directories(spectra_lodepng SYSTEM PUBLIC "${lodepng_SOURCE_DIR}")
target_compile_definitions(spectra_lodepng PRIVATE _CRT_SECURE_NO_WARNINGS)
