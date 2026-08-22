add_library(material_notes_module
    NotesModule.cpp
    NotesModule.hpp
)
target_include_directories(material_notes_module PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(material_notes_module PUBLIC cxx_std_17)
