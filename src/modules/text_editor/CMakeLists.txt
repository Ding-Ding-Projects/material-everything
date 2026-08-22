cmake_minimum_required(VERSION 3.20)
add_library(text_editor STATIC text_editor_module.cpp)
target_include_directories(text_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(text_editor PUBLIC cxx_std_20)
