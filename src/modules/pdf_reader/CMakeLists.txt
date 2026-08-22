cmake_minimum_required(VERSION 3.24)
project(material_everything_pdf_reader LANGUAGES CXX)

add_library(material_everything_pdf_reader STATIC
    pdf_reader.cpp
)

target_include_directories(material_everything_pdf_reader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(material_everything_pdf_reader PUBLIC cxx_std_20)
