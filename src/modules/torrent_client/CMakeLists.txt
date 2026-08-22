add_library(me_torrent_client STATIC
  torrent_client.cpp
)
target_include_directories(me_torrent_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(me_torrent_client PUBLIC cxx_std_20)

# Real backend: link libtorrent when available.
find_package(LibtorrentRasterbar QUIET)
if(LibtorrentRasterbar_FOUND)
  target_link_libraries(me_torrent_client PRIVATE LibtorrentRasterbar::torrent-rasterbar)
  target_compile_definitions(me_torrent_client PRIVATE ME_HAVE_LIBTORRENT=1)
endif()
