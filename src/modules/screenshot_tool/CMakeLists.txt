add_library(screenshot_tool STATIC
  screenshot_tool.cpp
)
target_include_directories(screenshot_tool PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
  target_link_libraries(screenshot_tool PRIVATE user32 gdi32)
endif()
