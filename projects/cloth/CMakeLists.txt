add_library(xayah_cloth)
add_library(xayah::cloth ALIAS xayah_cloth)
target_sources(
        xayah_cloth
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        cloth.ixx
        PRIVATE
        cloth.cpp
        cloth.cu
)
target_include_directories(xayah_cloth PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(xayah_cloth PRIVATE CUDA::cudart)

add_library(xayah_cloth_plugin MODULE plugin.cpp)
target_link_libraries(
        xayah_cloth_plugin
        PRIVATE
        xayah::cloth
        SpectraPluginSDK::contract
        spectra_cuda_plugin_interop
)
spectra_add_provider(
        xayah_cloth_plugin
        xayah.cloth.xpbd
        "${CMAKE_CURRENT_SOURCE_DIR}/cloth"
)
