add_library(material_everything_presentation STATIC
    src/presentation.cpp
)
target_include_directories(material_everything_presentation PUBLIC include)
target_link_libraries(material_everything_presentation PUBLIC Qt6::Widgets Qt6::Gui)
target_compile_features(material_everything_presentation PUBLIC cxx_std_20)
