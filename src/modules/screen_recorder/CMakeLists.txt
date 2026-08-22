cmake_minimum_required(VERSION 3.20)
project(material_everything_screen_recorder LANGUAGES CXX)

add_library(screen_recorder STATIC
    screen_recorder.cpp
    screen_recorder.hpp
)

target_include_directories(screen_recorder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(screen_recorder PUBLIC cxx_std_20)
target_compile_definitions(screen_recorder PRIVATE
    ME_SCREEN_RECORDER_MODULE=1
)
