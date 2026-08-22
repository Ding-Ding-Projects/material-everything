cmake_minimum_required(VERSION 3.24)
project(material_everything_video_editor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(material_everything_video_editor STATIC
    timeline.cpp
    preview.cpp
    export.cpp
    module.cpp
)

target_include_directories(material_everything_video_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(material_everything_video_editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)
