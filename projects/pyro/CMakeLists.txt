add_library(xayah_pyro)
add_library(xayah::pyro ALIAS xayah_pyro)
target_sources(
        xayah_pyro
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        pyro.ixx
        PRIVATE
        pyro.cpp
        pyro.cu
)
target_include_directories(xayah_pyro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
        xayah_pyro
        PRIVATE
        CUDA::cudart
        CUDA::cublas
        CUDA::cusparse
        SpectraPluginSDK::contract
)

add_library(xayah_pyro_plugin MODULE plugin.cpp)
target_link_libraries(
        xayah_pyro_plugin
        PRIVATE
        xayah::pyro
        SpectraPluginSDK::contract
        xayah_cuda_interop
)
spectra_add_provider(
        xayah_pyro_plugin
        xayah.volume.pyro
        "${CMAKE_CURRENT_SOURCE_DIR}/pyro"
)
