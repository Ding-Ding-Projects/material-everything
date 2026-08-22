# Material Everything — FTP/SFTP Client Module

add_library(me_ftp_client STATIC
    ftp_client.cpp
)

target_include_directories(me_ftp_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(me_ftp_client PUBLIC cxx_std_17)

# Backend detection — these are set from the top-level build.
option(ME_WITH_SFTP "Enable SFTP/SCP backend via libssh2" ON)
option(ME_WITH_CURL "Enable FTP/FTPS backend via libcurl" ON)

if(ME_WITH_SFTP)
    find_package(libssh2 QUIET)
    if(libssh2_FOUND OR TARGET libssh2::libssh2)
        target_link_libraries(me_ftp_client PRIVATE libssh2::libssh2)
    else()
        # Fall back to pkg-config or manual paths.
        find_library(LIBSSH2_LIBRARY NAMES ssh2)
        find_path(LIBSSH2_INCLUDE_DIR NAMES libssh2.h PATH_SUFFIXES libssh2)
        if(LIBSSH2_LIBRARY AND LIBSSH2_INCLUDE_DIR)
            target_include_directories(me_ftp_client PRIVATE ${LIBSSH2_INCLUDE_DIR})
            target_link_libraries(me_ftp_client PRIVATE ${LIBSSH2_LIBRARY})
        else()
            message(WARNING "libssh2 not found; SFTP support disabled")
            target_compile_definitions(me_ftp_client PRIVATE ME_DISABLE_SFTP)
        endif()
    endif()
endif()

if(NOT ME_DISABLE_SFTP AND ME_WITH_SFTP AND (TARGET libssh2::libssh2 OR LIBSSH2_LIBRARY))
    target_compile_definitions(me_ftp_client PRIVATE ME_WITH_SFTP)
endif()

if(ME_WITH_CURL)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        target_link_libraries(me_ftp_client PRIVATE CURL::libcurl)
    else()
        message(WARNING "libcurl not found; FTP/FTPS support disabled")
    endif()
endif()
