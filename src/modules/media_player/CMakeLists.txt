add_library(material_everything_media_player STATIC
    src/media_player_module.cpp
    src/qt_multimedia_media_player_backend.cpp
)

target_include_directories(material_everything_media_player PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(material_everything_media_player PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

target_compile_features(material_everything_media_player PUBLIC cxx_std_20)

