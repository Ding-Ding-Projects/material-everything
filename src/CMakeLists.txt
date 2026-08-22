file(GLOB MODULE_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/modules/*)
set(EXCLUDED_MODULES screenshot_tool ftp_client video_editor media_player database_client presentation terminal)
foreach(DIR ${MODULE_DIRS})
    get_filename_component(MOD_NAME ${DIR} NAME)
    if(IS_DIRECTORY ${DIR} AND EXISTS ${DIR}/CMakeLists.txt AND NOT MOD_NAME IN_LIST EXCLUDED_MODULES)
        add_subdirectory(${DIR})
    endif()
endforeach()
add_executable(material-everything main.cpp module_registry.cpp)
target_include_directories(material-everything PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modules)

# Exclude demo executables that link against Qt directly
set_property(GLOBAL PROPERTY EXCLUDE_DEMOS word_processor_demo)
