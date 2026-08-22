add_library(clipboard_manager STATIC
  clipboard_manager.cpp
  clipboard_manager_ui.cpp
)
target_include_directories(clipboard_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(clipboard_manager PUBLIC cxx_std_17)
