add_library(me_database_client STATIC
  database_client.cpp
  sql_highlighter.cpp
)

target_include_directories(me_database_client PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
  target_link_libraries(me_database_client PRIVATE SQLite::SQLite3)
else()
  target_link_libraries(me_database_client PRIVATE sqlite3)
endif()

target_compile_features(me_database_client PUBLIC cxx_std_20)
