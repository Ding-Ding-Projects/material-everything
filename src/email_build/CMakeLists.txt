cmake_minimum_required(VERSION 3.20)
project(EmailClientBuild LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../modules/email_client material_email_client_module)
