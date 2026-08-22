add_library(me_diagram STATIC
  diagram_module.cpp
)
target_include_directories(me_diagram PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(me_diagram PUBLIC cxx_std_20)
