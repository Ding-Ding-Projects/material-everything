add_library(system_monitor STATIC
    system_monitor.cpp
    system_monitor.hpp
)

target_include_directories(system_monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(system_monitor PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(system_monitor PUBLIC pdh psapi wbemuuid)
endif()
