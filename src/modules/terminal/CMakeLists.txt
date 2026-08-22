cmake_minimum_required(VERSION 3.16)

# Terminal module — Material Everything
add_library(material_everything_terminal STATIC)

target_sources(material_everything_terminal PRIVATE
    terminal.cpp
    terminal_widget.cpp
)

target_include_directories(material_everything_terminal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Qt6 REQUIRED COMPONENTS Widgets)
target_link_libraries(material_everything_terminal PUBLIC Qt6::Widgets)
