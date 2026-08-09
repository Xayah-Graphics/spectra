add_library(xayah_bouncing_ball)
add_library(xayah::bouncing_ball ALIAS xayah_bouncing_ball)
target_sources(
        xayah_bouncing_ball
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
        bouncing_ball.ixx
        PRIVATE
        bouncing_ball.cpp
)
target_link_libraries(xayah_bouncing_ball PRIVATE SpectraPluginSDK::contract)

add_library(xayah_bouncing_ball_plugin MODULE plugin.cpp)
target_link_libraries(
        xayah_bouncing_ball_plugin
        PRIVATE
        xayah::bouncing_ball
        SpectraPluginSDK::contract
)
spectra_add_provider(
        xayah_bouncing_ball_plugin
        xayah.rigid.bouncing-ball
        "${CMAKE_CURRENT_SOURCE_DIR}/bouncing-ball"
)
