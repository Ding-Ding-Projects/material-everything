cmake_minimum_required(VERSION 3.24)
project(material_vector_graphics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

add_library(vector_graphics STATIC
  vector_graphics.cpp
)
target_include_directories(vector_graphics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vector_graphics
  PUBLIC Qt6::Widgets Qt6::Svg
)
