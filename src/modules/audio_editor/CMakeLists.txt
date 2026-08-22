add_library(material_audio_editor STATIC
    audio_editor.cpp
    audio_editor_ui.cpp
)
target_include_directories(material_audio_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(material_audio_editor PUBLIC cxx_std_20)
