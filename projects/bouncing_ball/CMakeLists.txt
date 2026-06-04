cmake_minimum_required(VERSION 4.3.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
project(xayah_bouncing_ball LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

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
set_property(TARGET xayah_bouncing_ball PROPERTY CXX_MODULE_STD ON)

add_executable(xayah_bouncing_ball_cli main.cpp)
target_link_libraries(xayah_bouncing_ball_cli PRIVATE xayah::bouncing_ball)
set_property(TARGET xayah_bouncing_ball_cli PROPERTY CXX_MODULE_STD ON)
