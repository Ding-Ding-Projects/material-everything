add_library(material_everything_presentation STATIC
    src/presentation.cpp
)
target_include_directories(material_everything_presentation PUBLIC include)
target_link_libraries(material_everything_presentation PUBLIC Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Gui)
