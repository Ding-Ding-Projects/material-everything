cmake_minimum_required(VERSION 3.21)

project(material_everything_word_processor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets PrintSupport)

add_library(word_processor_module STATIC
    word_processor_module.cpp
)

target_include_directories(word_processor_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(word_processor_module PUBLIC Qt6::Widgets Qt6::PrintSupport)

add_executable(word_processor_demo main_demo.cpp)
target_link_libraries(word_processor_demo PRIVATE word_processor_module Qt6::Widgets)
