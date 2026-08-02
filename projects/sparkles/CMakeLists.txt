add_library(xayah_sparkles)
add_library(xayah::sparkles ALIAS xayah_sparkles)
target_sources(
        xayah_sparkles
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        sparkles.ixx
        PRIVATE
        sparkles.cpp
)

add_library(xayah_sparkles_plugin MODULE plugin.cpp)
target_link_libraries(
        xayah_sparkles_plugin
        PRIVATE
        xayah::sparkles
        SpectraPluginSDK::contract
)
spectra_add_provider(
        xayah_sparkles_plugin
        xayah.particles.sparkles
        "${CMAKE_CURRENT_SOURCE_DIR}/sparkles"
)
